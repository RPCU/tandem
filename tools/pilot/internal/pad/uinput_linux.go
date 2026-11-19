package pad

import (
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"syscall"
	"unsafe"
)

// The kernel's uinput and input-event numbers (linux/uinput.h,
// linux/input-event-codes.h).
const (
	uiDevCreate  = 0x5501
	uiDevDestroy = 0x5502
	uiDevSetup   = 0x405c5503 // _IOW('U', 3, struct uinput_setup), 92 bytes
	uiAbsSetup   = 0x401c5504 // _IOW('U', 4, struct uinput_abs_setup), 28 bytes
	uiSetEvBit   = 0x40045564
	uiSetKeyBit  = 0x40045565
	uiSetAbsBit  = 0x40045567

	evSyn = 0
	evKey = 1
	evAbs = 3

	absX     = 0x00
	absY     = 0x01
	absZ     = 0x02
	absRX    = 0x03
	absRY    = 0x04
	absRZ    = 0x05
	absHat0X = 0x10
	absHat0Y = 0x11

	btnA      = 0x130
	btnB      = 0x131
	btnX      = 0x133
	btnY      = 0x134
	btnTL     = 0x136
	btnTR     = 0x137
	btnSelect = 0x13a
	btnStart  = 0x13b
	btnMode   = 0x13c
	btnThumbL = 0x13d
	btnThumbR = 0x13e

	busUSB = 0x03
)

// Identity is how the device names itself. The kernel's xpad driver gives a
// wired Xbox 360 pad these numbers and capabilities, and SDL, Steam Input
// and Wine pick their mapping from them; Product differs per game instance
// so that each instance can be told to read only its own pad.
type Identity struct {
	Name    string
	Vendor  uint16
	Product uint16
	Version uint16
}

// Xbox360 is the identity of a wired Xbox 360 pad.
func Xbox360() Identity {
	return Identity{Name: "Microsoft X-Box 360 pad", Vendor: 0x045e, Product: 0x028e, Version: 0x0110}
}

// Uinput is a virtual gamepad made through /dev/uinput. The kernel removes
// it when this process ends, however it ends: the game then sees the pad
// unplugged, with nothing held.
type Uinput struct {
	file *os.File
	hat  [2]int32
}

// keys and axes an xpad Xbox 360 pad declares, in the driver's order. Every
// one is declared, pressed or not: SDL numbers buttons by what exists.
var (
	padKeys = []uint16{btnA, btnB, btnX, btnY, btnTL, btnTR, btnSelect, btnStart, btnMode, btnThumbL, btnThumbR}
	padAxes = []struct {
		code                 uint16
		min, max, fuzz, flat int32
	}{
		{absX, -32768, 32767, 16, 128},
		{absY, -32768, 32767, 16, 128},
		{absRX, -32768, 32767, 16, 128},
		{absRY, -32768, 32767, 16, 128},
		{absZ, 0, 255, 0, 0},
		{absRZ, 0, 255, 0, 0},
		{absHat0X, -1, 1, 0, 0},
		{absHat0Y, -1, 1, 0, 0},
	}
)

// OpenUinput creates the pad. path is /dev/uinput.
func OpenUinput(path string, id Identity) (*Uinput, error) {
	file, err := os.OpenFile(path, os.O_WRONLY|syscall.O_NONBLOCK, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w (the user needs write access: group uinput or input)", path, err)
	}
	u := &Uinput{file: file}
	fail := func(what string, err error) (*Uinput, error) {
		_ = file.Close()
		return nil, fmt.Errorf("uinput %s: %w", what, err)
	}
	for _, ev := range []uintptr{evKey, evAbs} {
		if err := u.ioctl(uiSetEvBit, ev); err != nil {
			return fail("event type", err)
		}
	}
	for _, key := range padKeys {
		if err := u.ioctl(uiSetKeyBit, uintptr(key)); err != nil {
			return fail("key", err)
		}
	}
	for _, axis := range padAxes {
		if err := u.ioctl(uiSetAbsBit, uintptr(axis.code)); err != nil {
			return fail("axis", err)
		}
		// struct uinput_abs_setup { __u16 code; struct input_absinfo
		// { value, minimum, maximum, fuzz, flat, resolution }; }
		var setup [28]byte
		binary.LittleEndian.PutUint16(setup[0:], axis.code)
		binary.LittleEndian.PutUint32(setup[8:], uint32(axis.min))
		binary.LittleEndian.PutUint32(setup[12:], uint32(axis.max))
		binary.LittleEndian.PutUint32(setup[16:], uint32(axis.fuzz))
		binary.LittleEndian.PutUint32(setup[20:], uint32(axis.flat))
		if err := u.ioctl(uiAbsSetup, uintptr(unsafe.Pointer(&setup[0]))); err != nil {
			return fail("axis range", err)
		}
	}
	// struct uinput_setup { struct input_id { bustype, vendor, product,
	// version }; char name[80]; __u32 ff_effects_max; }
	var setup [92]byte
	binary.LittleEndian.PutUint16(setup[0:], busUSB)
	binary.LittleEndian.PutUint16(setup[2:], id.Vendor)
	binary.LittleEndian.PutUint16(setup[4:], id.Product)
	binary.LittleEndian.PutUint16(setup[6:], id.Version)
	copy(setup[8:87], id.Name)
	if err := u.ioctl(uiDevSetup, uintptr(unsafe.Pointer(&setup[0]))); err != nil {
		return fail("setup", err)
	}
	if err := u.ioctl(uiDevCreate, 0); err != nil {
		return fail("create", err)
	}
	return u, nil
}

func (u *Uinput) ioctl(request, arg uintptr) error {
	if _, _, errno := syscall.Syscall(syscall.SYS_IOCTL, u.file.Fd(), request, arg); errno != 0 {
		return errno
	}
	return nil
}

// emit writes events and the report that ends them.
func (u *Uinput) emit(events ...[3]int32) error {
	// struct input_event { struct timeval time; __u16 type, code; __s32 value; }
	buf := make([]byte, 0, 24*(len(events)+1))
	events = append(events, [3]int32{evSyn, 0, 0})
	for _, e := range events {
		var raw [24]byte
		binary.LittleEndian.PutUint16(raw[16:], uint16(e[0]))
		binary.LittleEndian.PutUint16(raw[18:], uint16(e[1]))
		binary.LittleEndian.PutUint32(raw[20:], uint32(e[2]))
		buf = append(buf, raw[:]...)
	}
	_, err := u.file.Write(buf)
	return err
}

// Button presses or releases a button; the d-pad is the hat.
func (u *Uinput) Button(b Button, down bool) error {
	value := int32(0)
	if down {
		value = 1
	}
	switch b {
	case A:
		return u.emit([3]int32{evKey, btnA, value})
	case B:
		return u.emit([3]int32{evKey, btnB, value})
	case X:
		return u.emit([3]int32{evKey, btnX, value})
	case Y:
		return u.emit([3]int32{evKey, btnY, value})
	case LB:
		return u.emit([3]int32{evKey, btnTL, value})
	case RB:
		return u.emit([3]int32{evKey, btnTR, value})
	case L3:
		return u.emit([3]int32{evKey, btnThumbL, value})
	case R3:
		return u.emit([3]int32{evKey, btnThumbR, value})
	case Up, Down:
		// Up is -1 on the hat's y axis.
		want := value
		if b == Up {
			want = -value
		}
		if !down && u.hat[1] != hatOf(b) {
			return nil // the other direction is held
		}
		u.hat[1] = want
		return u.emit([3]int32{evAbs, absHat0Y, want})
	case Left, Right:
		want := value
		if b == Left {
			want = -value
		}
		if !down && u.hat[0] != hatOf(b) {
			return nil
		}
		u.hat[0] = want
		return u.emit([3]int32{evAbs, absHat0X, want})
	}
	return fmt.Errorf("no such button: %d", b)
}

func hatOf(b Button) int32 {
	if b == Up || b == Left {
		return -1
	}
	return 1
}

// Stick moves a stick: y up is the low end of the kernel's axis.
func (u *Uinput) Stick(s Stick, x, y float64) error {
	xc, yc := int32(absX), int32(absY)
	if s == RightStick {
		xc, yc = absRX, absRY
	}
	return u.emit([3]int32{evAbs, xc, axisValue(x)}, [3]int32{evAbs, yc, axisValue(-y)})
}

func axisValue(v float64) int32 {
	return int32(math.Round(math.Max(-1, math.Min(1, v)) * 32767))
}

// Trigger pulls a trigger.
func (u *Uinput) Trigger(t Trigger, value float64) error {
	code := int32(absZ)
	if t == RT {
		code = absRZ
	}
	return u.emit([3]int32{evAbs, code, int32(math.Round(math.Max(0, math.Min(1, value)) * 255))})
}

// Close removes the device.
func (u *Uinput) Close() error {
	_ = u.ioctl(uiDevDestroy, 0)
	return u.file.Close()
}
