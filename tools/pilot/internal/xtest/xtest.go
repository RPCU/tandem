// Package xtest sends keyboard and mouse input to one X display through the
// XTEST extension, the core X11 protocol written by hand (standard library
// only). The pilot uses it for a game instance under gamescope: the
// instance's own nested Xwayland (DISPLAY=:N) has that game alone in it, so
// what is sent there reaches that game and nothing else, whatever has the
// focus on the desktop, and the keyboard layout is looked up from the
// display's own keymap.
package xtest

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Event types of XTEST's FakeInput (the core protocol's event codes).
const (
	keyPress      = 2
	keyRelease    = 3
	buttonPress   = 4
	buttonRelease = 5
	motionNotify  = 6
)

// Common keysyms (X11 keysymdef.h).
const (
	KeysymShiftL  = 0xffe1
	KeysymControl = 0xffe3
	KeysymAltL    = 0xffe9
	KeysymTab     = 0xff09
	KeysymLeft    = 0xff51
	KeysymUp      = 0xff52
	KeysymRight   = 0xff53
	KeysymDown    = 0xff54
)

// Display is a parsed DISPLAY value: ":N" or ":N.S" (local displays only).
type Display struct {
	Number int
	Screen int
}

// ParseDisplay reads ":N", ":N.S" or "unix:N". A display on another host is
// refused: the pilot only ever talks to the machine it runs on.
func ParseDisplay(text string) (Display, error) {
	host, rest, ok := strings.Cut(text, ":")
	if !ok || (host != "" && host != "unix") {
		return Display{}, fmt.Errorf("DISPLAY %q: only a local display (:N)", text)
	}
	number, screen, _ := strings.Cut(rest, ".")
	n, err := strconv.Atoi(number)
	if err != nil || n < 0 {
		return Display{}, fmt.Errorf("DISPLAY %q: no display number", text)
	}
	d := Display{Number: n}
	if screen != "" {
		if d.Screen, err = strconv.Atoi(screen); err != nil {
			return Display{}, fmt.Errorf("DISPLAY %q: bad screen", text)
		}
	}
	return d, nil
}

func (d Display) String() string { return ":" + strconv.Itoa(d.Number) }

// Conn is one connection to a display, with XTEST found on it.
type Conn struct {
	mu       sync.Mutex
	conn     net.Conn
	order    binary.ByteOrder
	opcode   byte // XTEST's major opcode
	root     uint32
	minCode  byte
	keysyms  map[uint32]byte // keysym -> the first keycode that types it
	failures []string
	done     chan struct{}
}

// Dial connects to the display's local socket (the file, else Linux's
// abstract socket of the same name), authenticates with the
// MIT-MAGIC-COOKIE-1 found for it in xauthority (none: no authentication),
// and checks that XTEST is there.
func Dial(d Display, xauthority string) (*Conn, error) {
	path := fmt.Sprintf("/tmp/.X11-unix/X%d", d.Number)
	conn, err := net.DialTimeout("unix", path, 3*time.Second)
	if err != nil {
		var abstract error
		if conn, abstract = net.DialTimeout("unix", "@"+path, 3*time.Second); abstract != nil {
			return nil, fmt.Errorf("display %s: %w", d, err)
		}
	}
	name, data := cookie(xauthority, d.Number)
	c, err := Handshake(conn, d.Screen, name, data)
	if err != nil {
		_ = conn.Close()
		return nil, fmt.Errorf("display %s: %w", d, err)
	}
	return c, nil
}

// Handshake runs the connection setup, finds XTEST and reads the keymap on
// an open connection (Dial's, or a test's).
func Handshake(conn net.Conn, screen int, authName string, authData []byte) (*Conn, error) {
	c := &Conn{conn: conn, order: binary.LittleEndian, keysyms: map[uint32]byte{}, done: make(chan struct{})}
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
	defer conn.SetDeadline(time.Time{})

	// Setup request: byte order, protocol 11.0, the authorization.
	var req bytes.Buffer
	req.WriteByte('l')
	req.WriteByte(0)
	c.put16(&req, 11)
	c.put16(&req, 0)
	c.put16(&req, uint16(len(authName)))
	c.put16(&req, uint16(len(authData)))
	c.put16(&req, 0)
	req.WriteString(authName)
	req.Write(make([]byte, pad4(len(authName))))
	req.Write(authData)
	req.Write(make([]byte, pad4(len(authData))))
	if _, err := conn.Write(req.Bytes()); err != nil {
		return nil, err
	}
	reader := bufio.NewReader(conn)
	head := make([]byte, 8)
	if _, err := io.ReadFull(reader, head); err != nil {
		return nil, fmt.Errorf("setup: %w", err)
	}
	extra := make([]byte, int(c.order.Uint16(head[6:]))*4)
	if _, err := io.ReadFull(reader, extra); err != nil {
		return nil, fmt.Errorf("setup: %w", err)
	}
	switch head[0] {
	case 1:
	case 0:
		reason := extra
		if n := int(head[1]); n <= len(reason) {
			reason = reason[:n]
		}
		return nil, fmt.Errorf("the X server refused the connection: %s", strings.TrimSpace(string(reason)))
	default:
		return nil, errors.New("the X server asks for more authentication")
	}
	if len(extra) < 32 {
		return nil, errors.New("setup: a short reply")
	}
	vendorLen := int(c.order.Uint16(extra[16:]))
	screens := int(extra[20])
	formats := int(extra[21])
	c.minCode = extra[26]
	maxCode := extra[27]
	at := 32 + vendorLen + pad4(vendorLen) + 8*formats
	if screen >= screens {
		return nil, fmt.Errorf("setup: no screen %d (%d)", screen, screens)
	}
	for i := 0; ; i++ {
		if at+40 > len(extra) {
			return nil, errors.New("setup: a short screen list")
		}
		if i == screen {
			c.root = c.order.Uint32(extra[at:])
			break
		}
		// Skip this screen: 40 bytes, then its depths.
		depths := int(extra[at+39])
		at += 40
		for d := 0; d < depths; d++ {
			if at+8 > len(extra) {
				return nil, errors.New("setup: a short depth list")
			}
			at += 8 + 24*int(c.order.Uint16(extra[at+2:]))
		}
	}

	// QueryExtension "XTEST" (opcode 98).
	name := "XTEST"
	req.Reset()
	req.WriteByte(98)
	req.WriteByte(0)
	c.put16(&req, uint16(2+(len(name)+pad4(len(name)))/4))
	c.put16(&req, uint16(len(name)))
	c.put16(&req, 0)
	req.WriteString(name)
	req.Write(make([]byte, pad4(len(name))))
	reply, err := c.roundTrip(reader, req.Bytes())
	if err != nil {
		return nil, fmt.Errorf("QueryExtension: %w", err)
	}
	if reply[8] == 0 {
		return nil, errors.New("the display has no XTEST extension")
	}
	c.opcode = reply[9]

	// GetKeyboardMapping (opcode 101) for every keycode.
	count := int(maxCode) - int(c.minCode) + 1
	req.Reset()
	req.WriteByte(101)
	req.WriteByte(0)
	c.put16(&req, 2)
	req.WriteByte(c.minCode)
	req.WriteByte(byte(count))
	c.put16(&req, 0)
	reply, err = c.roundTrip(reader, req.Bytes())
	if err != nil {
		return nil, fmt.Errorf("GetKeyboardMapping: %w", err)
	}
	per := int(reply[1])
	syms := reply[32:]
	for i := 0; i < count && per > 0; i++ {
		for j := 0; j < per; j++ {
			at := (i*per + j) * 4
			if at+4 > len(syms) {
				break
			}
			sym := c.order.Uint32(syms[at:])
			if _, have := c.keysyms[sym]; sym != 0 && !have {
				c.keysyms[sym] = c.minCode + byte(i)
			}
		}
	}
	// From now on nothing is asked: only errors come back, read aside.
	go c.drain(reader)
	return c, nil
}

// roundTrip sends one request and reads its reply (an error is one too).
func (c *Conn) roundTrip(reader *bufio.Reader, request []byte) ([]byte, error) {
	if _, err := c.conn.Write(request); err != nil {
		return nil, err
	}
	head := make([]byte, 32)
	if _, err := io.ReadFull(reader, head); err != nil {
		return nil, err
	}
	switch head[0] {
	case 0:
		return nil, fmt.Errorf("X error %d", head[1])
	case 1:
		more := make([]byte, int(c.order.Uint32(head[4:]))*4)
		if _, err := io.ReadFull(reader, more); err != nil {
			return nil, err
		}
		return append(head, more...), nil
	}
	return nil, fmt.Errorf("unexpected X packet %d", head[0])
}

// drain reads what the server sends (errors, events: 32 bytes each) and
// keeps the errors.
func (c *Conn) drain(reader *bufio.Reader) {
	defer close(c.done)
	packet := make([]byte, 32)
	for {
		if _, err := io.ReadFull(reader, packet); err != nil {
			return
		}
		if packet[0] == 0 {
			c.mu.Lock()
			c.failures = append(c.failures, fmt.Sprintf("X error %d on request %d.%d", packet[1], packet[10], c.order.Uint16(packet[8:])))
			c.mu.Unlock()
		} else if packet[0] == 1 {
			// A reply we did not ask for: skip its tail.
			_, _ = io.CopyN(io.Discard, reader, int64(c.order.Uint32(packet[4:]))*4)
		}
	}
}

// Failures are the X errors the server sent since the last call.
func (c *Conn) Failures() []string {
	c.mu.Lock()
	defer c.mu.Unlock()
	out := c.failures
	c.failures = nil
	return out
}

// Keycode is the key that types keysym on this display, by its own keymap.
func (c *Conn) Keycode(keysym uint32) (byte, bool) {
	code, ok := c.keysyms[keysym]
	return code, ok
}

func (c *Conn) fake(kind, detail byte, x, y int16) error {
	var req bytes.Buffer
	req.WriteByte(c.opcode)
	req.WriteByte(2) // FakeInput
	c.put16(&req, 9)
	req.WriteByte(kind)
	req.WriteByte(detail)
	c.put16(&req, 0)
	c.put32(&req, 0) // time: now
	c.put32(&req, 0) // root: the pointer's screen
	req.Write(make([]byte, 8))
	c.put16(&req, uint16(x))
	c.put16(&req, uint16(y))
	req.Write(make([]byte, 7))
	req.WriteByte(0) // device: the core one
	c.mu.Lock()
	defer c.mu.Unlock()
	_, err := c.conn.Write(req.Bytes())
	return err
}

// Key presses or releases the key of a keysym.
func (c *Conn) Key(keysym uint32, down bool) error {
	code, ok := c.Keycode(keysym)
	if !ok {
		return fmt.Errorf("no key types keysym %#x on this display", keysym)
	}
	if down {
		return c.fake(keyPress, code, 0, 0)
	}
	return c.fake(keyRelease, code, 0, 0)
}

// Button presses or releases a mouse button (1 left, 2 middle, 3 right).
func (c *Conn) Button(button byte, down bool) error {
	if down {
		return c.fake(buttonPress, button, 0, 0)
	}
	return c.fake(buttonRelease, button, 0, 0)
}

// Move moves the pointer by dx, dy pixels (relative motion).
func (c *Conn) Move(dx, dy int) error {
	clamp := func(v int) int16 { return int16(max(-32768, min(32767, v))) }
	return c.fake(motionNotify, 1, clamp(dx), clamp(dy))
}

// Close closes the connection; the server releases nothing by itself, so
// callers release first.
func (c *Conn) Close() error {
	err := c.conn.Close()
	<-c.done
	return err
}

func (c *Conn) put16(b *bytes.Buffer, v uint16) { _ = binary.Write(b, c.order, v) }
func (c *Conn) put32(b *bytes.Buffer, v uint32) { _ = binary.Write(b, c.order, v) }

func pad4(n int) int { return (4 - n%4) % 4 }

// cookie finds the MIT-MAGIC-COOKIE-1 for a local display in an Xauthority
// file (an empty path: $XAUTHORITY, else ~/.Xauthority). None found: no
// authentication, which a display started without -auth accepts.
func cookie(path string, display int) (string, []byte) {
	if path == "" {
		path = os.Getenv("XAUTHORITY")
	}
	if path == "" {
		home, _ := os.UserHomeDir()
		path = filepath.Join(home, ".Xauthority")
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return "", nil
	}
	return FindCookie(data, display)
}

// FindCookie reads Xauthority entries: family(2) then four counted strings
// (address, display number, name, data), big-endian.
func FindCookie(data []byte, display int) (string, []byte) {
	want := strconv.Itoa(display)
	field := func() ([]byte, bool) {
		if len(data) < 2 {
			return nil, false
		}
		n := int(binary.BigEndian.Uint16(data))
		if len(data) < 2+n {
			return nil, false
		}
		out := data[2 : 2+n]
		data = data[2+n:]
		return out, true
	}
	for len(data) >= 2 {
		family := binary.BigEndian.Uint16(data)
		data = data[2:]
		_, ok1 := field()
		number, ok2 := field()
		name, ok3 := field()
		secret, ok4 := field()
		if !(ok1 && ok2 && ok3 && ok4) {
			return "", nil
		}
		// 256: FamilyLocal, 65535: FamilyWild.
		if (family == 256 || family == 65535) && (string(number) == want || len(number) == 0) && string(name) == "MIT-MAGIC-COOKIE-1" {
			return string(name), append([]byte(nil), secret...)
		}
	}
	return "", nil
}
