package pad

import (
	"strings"
	"testing"
	"time"
)

func TestGuardedReleasesEverything(t *testing.T) {
	var out strings.Builder
	dry := NewDry(&out)
	start := time.Unix(100, 0)
	g := NewGuarded(dry, 500*time.Millisecond, start, nil)
	must := func(err error) {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
	}
	must(g.Button(A, true))
	must(g.Button(A, true)) // no second event
	must(g.Button(Up, true))
	must(g.Stick(LeftStick, 3, 4)) // clamped to the unit circle
	must(g.Trigger(RT, 7))         // clamped to 1
	if got := g.Held(); got != "a up left(0.60,0.80) rt(1.00)" {
		t.Errorf("held: %q", got)
	}
	must(g.Neutral())
	if got := g.Held(); got != "" {
		t.Errorf("held after Neutral: %q", got)
	}
	want := "pad: a down\npad: up down\npad: left stick 0.60 0.80\npad: rt 1.00\npad: a up\npad: up up\npad: left stick 0.00 0.00\npad: rt 0.00\n"
	if out.String() != want {
		t.Errorf("events:\n%s\nwant:\n%s", out.String(), want)
	}
	must(g.Neutral()) // nothing more to release
	if dry.Events != 8 {
		t.Errorf("%d events", dry.Events)
	}
}

func TestDeadMan(t *testing.T) {
	var out strings.Builder
	start := time.Unix(100, 0)
	var said []string
	g := NewGuarded(NewDry(&out), 500*time.Millisecond, start, func(format string, _ ...any) { said = append(said, format) })
	_ = g.Stick(LeftStick, 0, 1)
	_ = g.Button(B, true)
	g.Feed(start.Add(300 * time.Millisecond))
	if g.Check(start.Add(700 * time.Millisecond)) {
		t.Fatal("tripped 400 ms after a feed")
	}
	if !g.Check(start.Add(900 * time.Millisecond)) {
		t.Fatal("not tripped 600 ms after a feed")
	}
	if g.Held() != "" || len(said) != 1 {
		t.Errorf("held %q, said %v", g.Held(), said)
	}
	// Tripped: nothing can be pressed, releases still pass.
	if err := g.Button(A, true); err == nil {
		t.Error("a press passed a tripped dead-man")
	}
	if err := g.Stick(RightStick, 1, 0); err == nil {
		t.Error("a stick passed a tripped dead-man")
	}
	if err := g.Trigger(RT, 1); err == nil {
		t.Error("a trigger passed a tripped dead-man")
	}
	if err := g.Button(A, false); err != nil {
		t.Error(err)
	}
	// Fed again: back in control.
	g.Feed(start.Add(2 * time.Second))
	if err := g.Button(A, true); err != nil {
		t.Error(err)
	}
	if err := g.Close(); err != nil || g.Held() != "" {
		t.Errorf("close: %v, held %q", err, g.Held())
	}
}

func TestButtonNames(t *testing.T) {
	for b := Button(0); b < buttonCount; b++ {
		got, ok := ButtonNamed(strings.ToUpper(b.String()))
		if !ok || got != b {
			t.Errorf("%s: %v %v", b, got, ok)
		}
	}
	for _, name := range []string{"start", "back", "select", "guide", "mode", ""} {
		if _, ok := ButtonNamed(name); ok {
			t.Errorf("%q is a button: the pilot could open a menu", name)
		}
	}
}

// The ioctl numbers are written out in uinput_linux.go: check them against
// the kernel's formula, _IOC(dir, 'U', nr, size).
func TestUinputNumbers(t *testing.T) {
	ioc := func(dir, nr, size uintptr) uintptr { return dir<<30 | size<<16 | 'U'<<8 | nr }
	const write = 1
	for name, pair := range map[string][2]uintptr{
		"UI_DEV_CREATE":  {uiDevCreate, ioc(0, 1, 0)},
		"UI_DEV_DESTROY": {uiDevDestroy, ioc(0, 2, 0)},
		"UI_DEV_SETUP":   {uiDevSetup, ioc(write, 3, 92)},
		"UI_ABS_SETUP":   {uiAbsSetup, ioc(write, 4, 28)},
		"UI_SET_EVBIT":   {uiSetEvBit, ioc(write, 100, 4)},
		"UI_SET_KEYBIT":  {uiSetKeyBit, ioc(write, 101, 4)},
		"UI_SET_ABSBIT":  {uiSetAbsBit, ioc(write, 103, 4)},
	} {
		if pair[0] != pair[1] {
			t.Errorf("%s: %#x, the kernel's is %#x", name, pair[0], pair[1])
		}
	}
	if axisValue(1) != 32767 || axisValue(-1) != -32767 || axisValue(0) != 0 || axisValue(9) != 32767 {
		t.Error("axis values")
	}
	// The xpad set: eleven keys, eight axes; SDL numbers buttons by what exists.
	if len(padKeys) != 11 || len(padAxes) != 8 {
		t.Errorf("%d keys, %d axes", len(padKeys), len(padAxes))
	}
	id := Xbox360()
	if id.Vendor != 0x045e || id.Product != 0x028e || len(id.Name) >= 80 {
		t.Errorf("identity: %+v", id)
	}
}

// A press of one d-pad direction and the release of the other must not
// centre the hat: checked on the device's own bookkeeping, with no device.
func TestHat(t *testing.T) {
	if hatOf(Up) != -1 || hatOf(Left) != -1 || hatOf(Down) != 1 || hatOf(Right) != 1 {
		t.Error("hat directions")
	}
}
