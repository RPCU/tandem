package xtest

import (
	"bytes"
	"encoding/binary"
	"io"
	"net"
	"os"
	"testing"
)

var le = binary.LittleEndian

// fakeServer plays an X server with XTEST on the other end of a pipe: it
// answers the setup, QueryExtension and GetKeyboardMapping, then hands every
// FakeInput request it reads to got.
func fakeServer(t *testing.T, conn net.Conn, got chan<- []byte) {
	t.Helper()
	go func() {
		defer close(got)
		head := make([]byte, 12)
		if _, err := io.ReadFull(conn, head); err != nil {
			return
		}
		nameLen, dataLen := int(le.Uint16(head[6:])), int(le.Uint16(head[8:]))
		auth := make([]byte, nameLen+pad4(nameLen)+dataLen+pad4(dataLen))
		if _, err := io.ReadFull(conn, auth); err != nil {
			return
		}
		// Setup reply: vendor "fake" (4), one format, one screen with root
		// 0x1234 and no depths, keycodes 8..10.
		var extra bytes.Buffer
		w := func(v any) { _ = binary.Write(&extra, le, v) }
		w(uint32(1))        // release
		w(uint32(0x200000)) // resource id base
		w(uint32(0x1fffff)) // mask
		w(uint32(0))        // motion buffer
		w(uint16(4))        // vendor length
		w(uint16(65535))    // max request length
		extra.Write([]byte{1, 1, 0, 0, 32, 32, 8, 10})
		w(uint32(0))
		extra.WriteString("fake")
		extra.Write(make([]byte, 8)) // the format
		w(uint32(0x1234))            // root
		extra.Write(make([]byte, 35))
		extra.WriteByte(0) // no depths
		reply := []byte{1, 0}
		reply = le.AppendUint16(reply, 11)
		reply = le.AppendUint16(reply, 0)
		reply = le.AppendUint16(reply, uint16(extra.Len()/4))
		_, _ = conn.Write(append(reply, extra.Bytes()...))

		for {
			req := make([]byte, 4)
			if _, err := io.ReadFull(conn, req); err != nil {
				return
			}
			body := make([]byte, int(le.Uint16(req[2:]))*4-4)
			if _, err := io.ReadFull(conn, body); err != nil {
				return
			}
			switch req[0] {
			case 98: // QueryExtension: present, opcode 140
				r := make([]byte, 32)
				r[0], r[8], r[9] = 1, 1, 140
				_, _ = conn.Write(r)
			case 101: // GetKeyboardMapping: 2 keysyms per keycode
				r := make([]byte, 32)
				r[0], r[1] = 1, 2
				syms := []uint32{'z', 'Z', 'w', 'W', KeysymShiftL, 0} // an AZERTY-ish map
				le.PutUint32(r[4:], uint32(len(syms)))
				for _, s := range syms {
					r = le.AppendUint32(r, s)
				}
				_, _ = conn.Write(r)
			case 140:
				got <- append(req, body...)
			}
		}
	}()
}

func dial(t *testing.T) (*Conn, chan []byte) {
	t.Helper()
	client, server := net.Pipe()
	got := make(chan []byte, 16)
	fakeServer(t, server, got)
	c, err := Handshake(client, 0, "", nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = c.Close(); _ = server.Close() })
	return c, got
}

func TestHandshakeAndKeymap(t *testing.T) {
	c, _ := dial(t)
	if c.opcode != 140 || c.root != 0x1234 {
		t.Fatalf("opcode %d root %#x", c.opcode, c.root)
	}
	// The display's own keymap decides: w is keycode 9 here, not QWERTY's.
	if code, ok := c.Keycode('w'); !ok || code != 9 {
		t.Fatalf("w: %d %v", code, ok)
	}
	if code, ok := c.Keycode(KeysymShiftL); !ok || code != 10 {
		t.Fatalf("shift: %d %v", code, ok)
	}
	if _, ok := c.Keycode('q'); ok {
		t.Fatal("q is on no key here")
	}
	if err := c.Key('q', true); err == nil {
		t.Fatal("a key that is not there must fail")
	}
}

func TestFakeInput(t *testing.T) {
	c, got := dial(t)
	if err := c.Key('w', true); err != nil {
		t.Fatal(err)
	}
	if err := c.Move(-5, 300); err != nil {
		t.Fatal(err)
	}
	if err := c.Button(1, false); err != nil {
		t.Fatal(err)
	}
	key := <-got
	if len(key) != 36 || key[1] != 2 || key[4] != keyPress || key[5] != 9 {
		t.Fatalf("key request % x", key)
	}
	move := <-got
	if move[4] != motionNotify || move[5] != 1 || int16(le.Uint16(move[24:])) != -5 || int16(le.Uint16(move[26:])) != 300 {
		t.Fatalf("move request % x", move)
	}
	button := <-got
	if button[4] != buttonRelease || button[5] != 1 {
		t.Fatalf("button request % x", button)
	}
}

func TestParseDisplay(t *testing.T) {
	for text, want := range map[string]Display{":1": {1, 0}, ":12.0": {12, 0}, "unix:3": {3, 0}, ":2.1": {2, 1}} {
		got, err := ParseDisplay(text)
		if err != nil || got != want {
			t.Errorf("%s: %v %v", text, got, err)
		}
	}
	for _, text := range []string{"", "anya:0", ":x", "1"} {
		if _, err := ParseDisplay(text); err == nil {
			t.Errorf("%q must be refused", text)
		}
	}
}

func TestFindCookie(t *testing.T) {
	entry := func(family uint16, number, name, data string) []byte {
		out := binary.BigEndian.AppendUint16(nil, family)
		for _, s := range []string{"anya", number, name, data} {
			out = binary.BigEndian.AppendUint16(out, uint16(len(s)))
			out = append(out, s...)
		}
		return out
	}
	file := append(entry(256, "0", "MIT-MAGIC-COOKIE-1", "zero"), entry(256, "2", "MIT-MAGIC-COOKIE-1", "two")...)
	if name, data := FindCookie(file, 2); name != "MIT-MAGIC-COOKIE-1" || string(data) != "two" {
		t.Fatalf("display 2: %q %q", name, data)
	}
	if name, _ := FindCookie(file, 5); name != "" {
		t.Fatal("display 5 has no cookie")
	}
	if name, _ := FindCookie(file[:7], 0); name != "" {
		t.Fatal("a cut file has no cookie")
	}
}

// TANDEM_PILOT_X11=1 connects to $DISPLAY for real and reads its keymap,
// sending no input: the handshake against a real X server.
func TestRealDisplay(t *testing.T) {
	if os.Getenv("TANDEM_PILOT_X11") != "1" {
		t.Skip("TANDEM_PILOT_X11=1 to connect to $DISPLAY (no input is sent)")
	}
	d, err := ParseDisplay(os.Getenv("DISPLAY"))
	if err != nil {
		t.Fatal(err)
	}
	c, err := Dial(d, "")
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	code, ok := c.Keycode('w')
	t.Logf("display %s: XTEST opcode %d, root %#x, w is keycode %d (%v), %d keysyms", d, c.opcode, c.root, code, ok, len(c.keysyms))
	if !ok {
		t.Fatal("no key types w")
	}
}
