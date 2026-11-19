package pad

import (
	"fmt"
	"strings"
	"sync"
	"testing"
	"time"
)

type recorder struct {
	mu     sync.Mutex
	events []string
	dx, dy int
	closed bool
}

func (r *recorder) Key(sym uint32, down bool) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.events = append(r.events, fmt.Sprintf("%s %v", Binding{Keysym: sym}, down))
	return nil
}

func (r *recorder) Button(b byte, down bool) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.events = append(r.events, fmt.Sprintf("mouse%d %v", b, down))
	return nil
}

func (r *recorder) Move(dx, dy int) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.dx += dx
	r.dy += dy
	return nil
}

func (r *recorder) Close() error { r.closed = true; return nil }

func (r *recorder) take() string {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := strings.Join(r.events, ", ")
	r.events = nil
	return out
}

func TestKeyDeviceMovesAndSprints(t *testing.T) {
	r := &recorder{}
	d := NewKeyDevice(r, DefaultKeymap(), 1000, time.Millisecond)
	defer d.Close()
	_ = d.Stick(LeftStick, 0.7, 0.7)
	if got := r.take(); got != "w true, d true" {
		t.Fatalf("forward right: %s", got)
	}
	// Sprint: Shift stays down after the click, while V moves.
	_ = d.Button(L3, true)
	_ = d.Button(L3, false)
	_ = d.Stick(LeftStick, 0, 1)
	if got := r.take(); got != "shift true, d false" {
		t.Fatalf("sprint: %s", got)
	}
	_ = d.Stick(LeftStick, 0, 0)
	if got := r.take(); got != "w false, shift false" {
		t.Fatalf("stop: %s", got)
	}
	// A click with V at rest lets go at once.
	_ = d.Button(L3, true)
	_ = d.Button(L3, false)
	if got := r.take(); got != "shift true, shift false" {
		t.Fatalf("click at rest: %s", got)
	}
	_ = d.Button(A, true)
	_ = d.Trigger(RT, 0.9)
	_ = d.Trigger(RT, 0.2)
	_ = d.Button(A, false)
	if got := r.take(); got != "space true, mouse1 true, mouse1 false, space false" {
		t.Fatalf("jump and fire: %s", got)
	}
	if err := d.Button(LB, true); err == nil || !strings.Contains(err.Error(), "--keys lb=KEY") {
		t.Fatalf("an unbound button must say how to bind it: %v", err)
	}
}

func TestKeyDeviceLooksAndLetsGo(t *testing.T) {
	r := &recorder{}
	d := NewKeyDevice(r, DefaultKeymap(), 1000, time.Millisecond)
	_ = d.Stick(RightStick, 1, 1) // right and up
	time.Sleep(60 * time.Millisecond)
	_ = d.Stick(RightStick, 0, 0)
	r.mu.Lock()
	dx, dy := r.dx, r.dy
	r.mu.Unlock()
	if dx < 20 || dy > -20 || dx != -dy {
		t.Fatalf("the pointer went %d, %d", dx, dy)
	}
	_ = d.Button(B, true)
	_ = d.Trigger(LT, 1)
	r.take()
	if err := d.Close(); err != nil {
		t.Fatal(err)
	}
	if got := r.take(); got != "c false, mouse3 false" && got != "mouse3 false, c false" {
		t.Fatalf("close: %s", got)
	}
	if !r.closed {
		t.Fatal("the connection stays open")
	}
}

func TestParseKeymap(t *testing.T) {
	m, err := ParseKeymap("lb=alt, rb=e, r3=none, rt=mouse2")
	if err != nil {
		t.Fatal(err)
	}
	if m.Buttons[LB] != (Binding{Keysym: 0xffe9}) || m.Buttons[RB] != (Binding{Keysym: 'e'}) || m.Triggers[RT] != (Binding{Mouse: 2}) {
		t.Fatalf("keymap %s", m)
	}
	if _, ok := m.Buttons[R3]; ok {
		t.Fatal("r3=none unbinds it")
	}
	for _, bad := range []string{"lb", "zz=a", "a=mouse9", "a=enter"} {
		if _, err := ParseKeymap(bad); err == nil {
			t.Errorf("%q must be refused", bad)
		}
	}
}
