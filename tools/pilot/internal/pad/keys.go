package pad

import (
	"fmt"
	"math"
	"sort"
	"strings"
	"sync"
	"time"
)

// Keys is what the keyboard-and-mouse device (KeyDevice) sends: X11
// keysyms and mouse buttons, and relative pointer motion. The xtest
// package's connection is one; tests use a recorder.
type Keys interface {
	Key(keysym uint32, down bool) error
	Button(button byte, down bool) error
	Move(dx, dy int) error
	Close() error
}

// A Binding is a key (an X11 keysym) or a mouse button.
type Binding struct {
	Keysym uint32
	Mouse  byte // 1 left .. 5; 0: a key
}

func (b Binding) String() string {
	if b.Mouse != 0 {
		return fmt.Sprintf("mouse%d", b.Mouse)
	}
	for name, sym := range namedKeys {
		if sym == b.Keysym {
			return name
		}
	}
	if b.Keysym >= 0x20 && b.Keysym < 0x7f {
		return string(rune(b.Keysym))
	}
	return fmt.Sprintf("%#x", b.Keysym)
}

var namedKeys = map[string]uint32{
	"space": 0x20, "shift": 0xffe1, "ctrl": 0xffe3, "alt": 0xffe9, "tab": 0xff09,
	"left": 0xff51, "up": 0xff52, "right": 0xff53, "down": 0xff54,
}

// Keymap binds the pad's buttons and triggers to keys. The moves (the left
// stick) are W A S D, the look (the right stick) is the mouse.
type Keymap struct {
	Buttons  map[Button]Binding
	Triggers [2]Binding
}

// DefaultKeymap is the game's default keyboard layout as remembered
// [reported, unverified]: space jump, C crouch, F interact and dialogue,
// left Shift sprint (L3 holds it until V stops), Q quick melee, 1 the
// first weapon, the arrows for dialogue choices, left mouse fire, right
// mouse aim. LB and RB (the operating system) have no default: set them
// with --keys lb=KEY,rb=KEY once known. Keys are looked up by letter in the
// display's own keymap, so no keyboard layout is assumed.
func DefaultKeymap() Keymap {
	return Keymap{
		Buttons: map[Button]Binding{
			A: {Keysym: 0x20}, B: {Keysym: 'c'}, X: {Keysym: 'f'}, Y: {Keysym: '1'},
			L3: {Keysym: 0xffe1}, R3: {Keysym: 'q'},
			Up: {Keysym: 0xff52}, Down: {Keysym: 0xff54}, Left: {Keysym: 0xff51}, Right: {Keysym: 0xff53},
		},
		Triggers: [2]Binding{LT: {Mouse: 3}, RT: {Mouse: 1}},
	}
}

func parseBinding(text string) (Binding, error) {
	text = strings.ToLower(strings.TrimSpace(text))
	if sym, ok := namedKeys[text]; ok {
		return Binding{Keysym: sym}, nil
	}
	if strings.HasPrefix(text, "mouse") && len(text) == 6 && text[5] >= '1' && text[5] <= '5' {
		return Binding{Mouse: text[5] - '0'}, nil
	}
	if len(text) == 1 && text[0] > 0x20 && text[0] < 0x7f {
		return Binding{Keysym: uint32(text[0])}, nil
	}
	return Binding{}, fmt.Errorf("no key %q (a letter or digit, space, shift, ctrl, alt, tab, up, down, left, right, mouse1..mouse5, none)", text)
}

// ParseKeymap changes the default keymap by "BUTTON=KEY,..." (lt and rt
// too; KEY none unbinds).
func ParseKeymap(text string) (Keymap, error) {
	m := DefaultKeymap()
	for _, item := range strings.Split(text, ",") {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		name, key, ok := strings.Cut(item, "=")
		if !ok {
			return m, fmt.Errorf("--keys: %q is not BUTTON=KEY", item)
		}
		name = strings.ToLower(strings.TrimSpace(name))
		unbind := strings.TrimSpace(strings.ToLower(key)) == "none"
		var binding Binding
		if !unbind {
			var err error
			if binding, err = parseBinding(key); err != nil {
				return m, fmt.Errorf("--keys %s: %w", name, err)
			}
		}
		switch name {
		case "lt", "rt":
			t := LT
			if name == "rt" {
				t = RT
			}
			m.Triggers[t] = binding
		default:
			b, ok := ButtonNamed(name)
			if !ok {
				return m, fmt.Errorf("--keys: no button %q", name)
			}
			if unbind {
				delete(m.Buttons, b)
			} else {
				m.Buttons[b] = binding
			}
		}
	}
	return m, nil
}

func (m Keymap) String() string {
	var out []string
	for b, k := range m.Buttons {
		out = append(out, b.String()+"="+k.String())
	}
	sort.Strings(out)
	for t, k := range m.Triggers {
		if k != (Binding{}) {
			out = append(out, Trigger(t).String()+"="+k.String())
		}
	}
	return strings.Join(out, ",")
}

// KeyDevice plays the pad on a keyboard and a mouse: the left stick is
// W A S D (each key down past a third of the axis), the right stick moves
// the pointer for as long as it is held, at Look pixels a second at full
// deflection. A trigger is its binding, down past half way. It cannot
// steer a car by the triggers' travel: drive with AutoDrive (act autodrive)
// on this device.
type KeyDevice struct {
	mu      sync.Mutex
	out     Keys
	keymap  Keymap
	look    float64
	keys    map[uint32]bool // what we hold
	mouse   [6]bool
	right   [2]float64
	left    [2]float64
	l3      bool // sprint: Shift is latched while V moves
	latched bool
	residue [2]float64
	stop    chan struct{}
	stopped chan struct{}
}

// NewKeyDevice drives out with keymap; look is the pointer's speed at full
// right stick, in pixels a second; tick how often the pointer moves.
func NewKeyDevice(out Keys, keymap Keymap, look float64, tick time.Duration) *KeyDevice {
	d := &KeyDevice{
		out: out, keymap: keymap, look: look, keys: map[uint32]bool{},
		stop: make(chan struct{}), stopped: make(chan struct{}),
	}
	go d.loop(tick)
	return d
}

func (d *KeyDevice) loop(tick time.Duration) {
	defer close(d.stopped)
	ticker := time.NewTicker(tick)
	defer ticker.Stop()
	for {
		select {
		case <-d.stop:
			return
		case <-ticker.C:
			d.mu.Lock()
			if d.right != [2]float64{} {
				step := d.look * tick.Seconds()
				// y up on the stick is the pointer going up: X's y grows down.
				d.residue[0] += d.right[0] * step
				d.residue[1] -= d.right[1] * step
				dx, dy := math.Trunc(d.residue[0]), math.Trunc(d.residue[1])
				d.residue[0] -= dx
				d.residue[1] -= dy
				if dx != 0 || dy != 0 {
					_ = d.out.Move(int(dx), int(dy))
				}
			}
			d.mu.Unlock()
		}
	}
}

func (d *KeyDevice) key(sym uint32, down bool) error {
	if d.keys[sym] == down {
		return nil
	}
	d.keys[sym] = down
	return d.out.Key(sym, down)
}

func (d *KeyDevice) bind(b Binding, down bool) error {
	if b.Mouse != 0 {
		if d.mouse[b.Mouse] == down {
			return nil
		}
		d.mouse[b.Mouse] = down
		return d.out.Button(b.Mouse, down)
	}
	return d.key(b.Keysym, down)
}

// Button presses the button's key. L3 holds its key (sprint) until the
// left stick is back at rest.
func (d *KeyDevice) Button(b Button, down bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	binding, ok := d.keymap.Buttons[b]
	if !ok {
		if !down {
			return nil
		}
		return fmt.Errorf("%s has no key on this device (--keys %s=KEY)", b, b)
	}
	if b == L3 {
		d.l3 = down
		if down {
			d.latched = true
			return d.bind(binding, true)
		}
		if d.left == [2]float64{} {
			d.latched = false
			return d.bind(binding, false)
		}
		return nil
	}
	return d.bind(binding, down)
}

// Stick moves: the left one presses W A S D, the right one moves the
// pointer (in loop).
func (d *KeyDevice) Stick(s Stick, x, y float64) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	if s == RightStick {
		d.right = [2]float64{x, y}
		if x == 0 && y == 0 {
			d.residue = [2]float64{}
		}
		return nil
	}
	d.left = [2]float64{x, y}
	const past = 1.0 / 3
	var first error
	keep := func(err error) {
		if err != nil && first == nil {
			first = err
		}
	}
	keep(d.key('w', y > past))
	keep(d.key('s', y < -past))
	keep(d.key('d', x > past))
	keep(d.key('a', x < -past))
	if x == 0 && y == 0 && d.latched && !d.l3 {
		d.latched = false
		if binding, ok := d.keymap.Buttons[L3]; ok {
			keep(d.bind(binding, false))
		}
	}
	return first
}

// Trigger presses the trigger's binding past half way.
func (d *KeyDevice) Trigger(t Trigger, value float64) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	binding := d.keymap.Triggers[t]
	if binding == (Binding{}) {
		if value <= 0.5 {
			return nil
		}
		return fmt.Errorf("%s has no key on this device (--keys %s=KEY)", t, t)
	}
	return d.bind(binding, value > 0.5)
}

// Held lists the keys and buttons down, for tests and the log.
func (d *KeyDevice) Held() string {
	d.mu.Lock()
	defer d.mu.Unlock()
	var out []string
	for sym, down := range d.keys {
		if down {
			out = append(out, Binding{Keysym: sym}.String())
		}
	}
	for b, down := range d.mouse {
		if down {
			out = append(out, Binding{Mouse: byte(b)}.String())
		}
	}
	sort.Strings(out)
	return strings.Join(out, " ")
}

// Close lets go of every key and button, stops the pointer and closes the
// connection.
func (d *KeyDevice) Close() error {
	select {
	case <-d.stop:
		return nil
	default:
	}
	close(d.stop)
	<-d.stopped
	d.mu.Lock()
	defer d.mu.Unlock()
	var first error
	for sym, down := range d.keys {
		if down {
			d.keys[sym] = false
			if err := d.out.Key(sym, false); err != nil && first == nil {
				first = err
			}
		}
	}
	for b, down := range d.mouse {
		if down {
			d.mouse[b] = false
			if err := d.out.Button(byte(b), false); err != nil && first == nil {
				first = err
			}
		}
	}
	if err := d.out.Close(); err != nil && first == nil {
		first = err
	}
	return first
}
