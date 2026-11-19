package pad

import (
	"fmt"
	"io"
	"sync"
)

// Dry is a device that only writes what it would send: the pilot's dry run.
type Dry struct {
	mu  sync.Mutex
	out io.Writer
	// Events counts what was sent, for tests.
	Events int
}

// NewDry logs the events to out.
func NewDry(out io.Writer) *Dry { return &Dry{out: out} }

func (d *Dry) line(format string, args ...any) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.Events++
	_, err := fmt.Fprintf(d.out, "pad: "+format+"\n", args...)
	return err
}

// Button logs a press or a release.
func (d *Dry) Button(b Button, down bool) error {
	if down {
		return d.line("%s down", b)
	}
	return d.line("%s up", b)
}

// Stick logs a stick position.
func (d *Dry) Stick(s Stick, x, y float64) error { return d.line("%s stick %.2f %.2f", s, x, y) }

// Trigger logs a trigger position.
func (d *Dry) Trigger(t Trigger, value float64) error { return d.line("%s %.2f", t, value) }

// Close does nothing.
func (d *Dry) Close() error { return nil }
