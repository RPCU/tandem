package pad

import (
	"encoding/binary"
	"fmt"
	"os"
	"regexp"
	"strings"
	"testing"
	"time"
)

// A real device, made and read back on this machine: the kernel accepts
// our setup and names it as an Xbox 360 pad with an event and a joystick
// node (TANDEM_PILOT_UINPUT=1: a pad appears for a moment, at rest), and
// what we emit comes out of its event node as we meant it
// (TANDEM_PILOT_UINPUT=events: whatever reads joysticks on this machine,
// Steam for one, sees a stick move and two buttons; not on a desktop
// someone is using).
func TestUinputDevice(t *testing.T) {
	level := os.Getenv("TANDEM_PILOT_UINPUT")
	if level != "1" && level != "events" {
		t.Skip("set TANDEM_PILOT_UINPUT=1 (or =events) to create a real virtual pad")
	}
	id := Xbox360()
	id.Name = "tandem pilot test pad"
	id.Product = 0x02a1
	device, err := OpenUinput("/dev/uinput", id)
	if err != nil {
		t.Fatal(err)
	}
	closed := false
	defer func() {
		if !closed {
			_ = device.Close()
		}
	}()

	// udev needs a moment to make the nodes.
	var block string
	for i := 0; i < 50 && block == ""; i++ {
		time.Sleep(40 * time.Millisecond)
		devices, _ := os.ReadFile("/proc/bus/input/devices")
		for _, entry := range strings.Split(string(devices), "\n\n") {
			if strings.Contains(entry, `Name="tandem pilot test pad"`) {
				block = entry
			}
		}
	}
	if block == "" {
		t.Fatal("the pad is not in /proc/bus/input/devices")
	}
	if !strings.Contains(block, "Bus=0003 Vendor=045e Product=02a1 Version=0110") {
		t.Errorf("identity:\n%s", block)
	}
	handlers := regexp.MustCompile(`Handlers=(.*)`).FindStringSubmatch(block)
	if len(handlers) != 2 || !strings.Contains(handlers[1], "js") {
		t.Errorf("no joystick node: %v", handlers)
	}
	event := regexp.MustCompile(`event\d+`).FindString(block)
	if event == "" {
		t.Fatalf("no event node:\n%s", block)
	}
	// The keys an xpad has (bits 0x130..0x13e of the key bitmap) and its axes.
	if !strings.Contains(block, "B: ABS=3003f") {
		t.Errorf("axes:\n%s", block)
	}
	if !regexp.MustCompile(`B: KEY=7cdb000000000000 0 0 0 0\b`).MatchString(block) {
		t.Errorf("keys:\n%s", block)
	}

	if level != "events" {
		closed = true
		if err := device.Close(); err != nil {
			t.Fatal(err)
		}
		return
	}

	var node *os.File
	for i := 0; i < 50 && node == nil; i++ {
		if node, err = os.Open("/dev/input/" + event); err != nil {
			node = nil
			time.Sleep(40 * time.Millisecond)
		}
	}
	if node == nil {
		t.Skipf("/dev/input/%s cannot be read here (%v): the device exists, its events are not checked", event, err)
	}
	defer node.Close()

	read := make(chan string, 64)
	go func() {
		buf := make([]byte, 24*16)
		for {
			n, err := node.Read(buf)
			if err != nil {
				close(read)
				return
			}
			for at := 0; at+24 <= n; at += 24 {
				read <- fmt.Sprintf("%d/%#x=%d", binary.LittleEndian.Uint16(buf[at+16:]), binary.LittleEndian.Uint16(buf[at+18:]),
					int32(binary.LittleEndian.Uint32(buf[at+20:])))
			}
		}
	}()
	expect := func(what string, want ...string) {
		t.Helper()
		for _, w := range append(want, "0/0x0=0") {
			select {
			case got := <-read:
				if got != w {
					t.Errorf("%s: read %s, want %s", what, got, w)
				}
			case <-time.After(time.Second):
				t.Fatalf("%s: nothing read, want %s", what, w)
			}
		}
	}
	must := func(err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
	}
	must(device.Stick(RightStick, 0.5, 1))
	expect("right stick", "3/0x3=16384", "3/0x4=-32767") // up is the low end
	must(device.Stick(RightStick, 0, 0))
	expect("right stick back", "3/0x3=0", "3/0x4=0")
	must(device.Trigger(LT, 1))
	expect("lt", "3/0x2=255")
	must(device.Trigger(LT, 0))
	expect("lt back", "3/0x2=0")
	must(device.Button(R3, true))
	expect("r3", "1/0x13e=1")
	must(device.Button(R3, false))
	expect("r3 up", "1/0x13e=0")
	must(device.Button(Up, true))
	expect("d-pad up", "3/0x11=-1")
	must(device.Button(Down, false)) // not the direction held: nothing
	must(device.Button(Up, false))
	expect("d-pad up released", "3/0x11=0")

	closed = true
	must(device.Close())
	time.Sleep(200 * time.Millisecond)
	devices, _ := os.ReadFile("/proc/bus/input/devices")
	if strings.Contains(string(devices), "tandem pilot test pad") {
		t.Error("the pad is still there after Close")
	}
}
