// Package pad is the pilot's virtual gamepad: an Xbox 360 pad as the game
// sees one, a dry-run device that only logs, and the guard that releases
// everything when the pilot stops feeding it.
package pad

import (
	"fmt"
	"math"
	"sort"
	"strings"
	"sync"
	"time"
)

// Button is one of the pad's buttons. Start, Back and Guide are declared on
// the device (the game's driver expects them) but have no Button: the pilot
// can never open the pause menu, the map or Steam's overlay.
type Button int

// The buttons a routine can press.
const (
	A Button = iota
	B
	X
	Y
	LB
	RB
	L3
	R3
	Up
	Down
	Left
	Right
	buttonCount
)

var buttonNames = [...]string{"a", "b", "x", "y", "lb", "rb", "l3", "r3", "up", "down", "left", "right"}

func (b Button) String() string { return buttonNames[b] }

// ButtonNamed finds a button by the name routines use.
func ButtonNamed(name string) (Button, bool) {
	for i, n := range buttonNames {
		if n == strings.ToLower(name) {
			return Button(i), true
		}
	}
	return 0, false
}

// Stick is the left (move, steer) or right (look) stick.
type Stick int

// The two sticks.
const (
	LeftStick Stick = iota
	RightStick
)

func (s Stick) String() string {
	if s == LeftStick {
		return "left"
	}
	return "right"
}

// Trigger is LT (aim, brake) or RT (fire, accelerate).
type Trigger int

// The two triggers.
const (
	LT Trigger = iota
	RT
)

func (t Trigger) String() string {
	if t == LT {
		return "lt"
	}
	return "rt"
}

// Device takes the events. Stick axes run from -1 to 1 with y up (forward),
// triggers from 0 to 1.
type Device interface {
	Button(b Button, down bool) error
	Stick(s Stick, x, y float64) error
	Trigger(t Trigger, value float64) error
	Close() error
}

// Guarded wraps a Device with what keeps it safe: it remembers what is held,
// releases all of it on Neutral, and Check releases it when Feed has not
// been called for the dead-man time (the pilot hung, or lost the game).
type Guarded struct {
	mu       sync.Mutex
	dev      Device
	deadman  time.Duration
	fed      time.Time
	held     [buttonCount]bool
	sticks   [2][2]float64
	triggers [2]float64
	tripped  bool
	logf     func(format string, args ...any)
}

// NewGuarded guards dev; deadman is how long it may go without Feed.
func NewGuarded(dev Device, deadman time.Duration, now time.Time, logf func(string, ...any)) *Guarded {
	if logf == nil {
		logf = func(string, ...any) {}
	}
	return &Guarded{dev: dev, deadman: deadman, fed: now, logf: logf}
}

// Feed says the pilot is alive and in control at this time.
func (g *Guarded) Feed(now time.Time) {
	g.mu.Lock()
	defer g.mu.Unlock()
	g.fed = now
	g.tripped = false
}

// Check releases everything when the pilot stopped feeding. It answers
// whether the dead-man is tripped.
func (g *Guarded) Check(now time.Time) bool {
	g.mu.Lock()
	defer g.mu.Unlock()
	if now.Sub(g.fed) <= g.deadman {
		return false
	}
	if !g.tripped {
		g.tripped = true
		if !g.neutralLocked() {
			g.logf("dead-man: no control for %.1f s, everything released", now.Sub(g.fed).Seconds())
			_ = g.releaseLocked()
		}
	}
	return true
}

// Button presses or releases a button.
func (g *Guarded) Button(b Button, down bool) error {
	g.mu.Lock()
	defer g.mu.Unlock()
	if g.tripped && down {
		return fmt.Errorf("dead-man tripped: %s not pressed", b)
	}
	if g.held[b] == down {
		return nil
	}
	g.held[b] = down
	return g.dev.Button(b, down)
}

// Stick moves a stick; values are clamped to the unit circle.
func (g *Guarded) Stick(s Stick, x, y float64) error {
	g.mu.Lock()
	defer g.mu.Unlock()
	if math.IsNaN(x) || math.IsNaN(y) {
		x, y = 0, 0
	}
	if m := math.Hypot(x, y); m > 1 {
		x, y = x/m, y/m
	}
	if g.tripped && (x != 0 || y != 0) {
		return fmt.Errorf("dead-man tripped: %s stick not moved", s)
	}
	if g.sticks[s] == [2]float64{x, y} {
		return nil
	}
	g.sticks[s] = [2]float64{x, y}
	return g.dev.Stick(s, x, y)
}

// Trigger pulls a trigger; values are clamped to 0..1.
func (g *Guarded) Trigger(t Trigger, value float64) error {
	g.mu.Lock()
	defer g.mu.Unlock()
	if math.IsNaN(value) {
		value = 0
	}
	value = math.Max(0, math.Min(1, value))
	if g.tripped && value != 0 {
		return fmt.Errorf("dead-man tripped: %s not pulled", t)
	}
	if g.triggers[t] == value {
		return nil
	}
	g.triggers[t] = value
	return g.dev.Trigger(t, value)
}

// Neutral releases every button, centres both sticks and lets go of both
// triggers.
func (g *Guarded) Neutral() error {
	g.mu.Lock()
	defer g.mu.Unlock()
	return g.releaseLocked()
}

// Held lists what is not at rest, for the log.
func (g *Guarded) Held() string {
	g.mu.Lock()
	defer g.mu.Unlock()
	var out []string
	for b, down := range g.held {
		if down {
			out = append(out, Button(b).String())
		}
	}
	sort.Strings(out)
	for s, v := range g.sticks {
		if v != [2]float64{} {
			out = append(out, fmt.Sprintf("%s(%.2f,%.2f)", Stick(s), v[0], v[1]))
		}
	}
	for t, v := range g.triggers {
		if v != 0 {
			out = append(out, fmt.Sprintf("%s(%.2f)", Trigger(t), v))
		}
	}
	return strings.Join(out, " ")
}

// Close releases everything and closes the device.
func (g *Guarded) Close() error {
	g.mu.Lock()
	defer g.mu.Unlock()
	err := g.releaseLocked()
	if cerr := g.dev.Close(); err == nil {
		err = cerr
	}
	return err
}

func (g *Guarded) neutralLocked() bool {
	return g.held == [buttonCount]bool{} && g.sticks == [2][2]float64{} && g.triggers == [2]float64{}
}

func (g *Guarded) releaseLocked() error {
	var first error
	keep := func(err error) {
		if err != nil && first == nil {
			first = err
		}
	}
	for b, down := range g.held {
		if down {
			g.held[b] = false
			keep(g.dev.Button(Button(b), false))
		}
	}
	for s, v := range g.sticks {
		if v != [2]float64{} {
			g.sticks[s] = [2]float64{}
			keep(g.dev.Stick(Stick(s), 0, 0))
		}
	}
	for t, v := range g.triggers {
		if v != 0 {
			g.triggers[t] = 0
			keep(g.dev.Trigger(Trigger(t), 0))
		}
	}
	return first
}
