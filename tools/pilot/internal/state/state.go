// Package state reads the pilot state the plugin writes: one line of
// key=value words (scripts/tandem/Pilot.reds, docs/architecture/autopilot.md).
package state

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

// State is one sample of the game as the plugin reports it.
type State struct {
	fields map[string]string
}

// Parse reads a state line. A line without a sequence number is refused: it
// is a torn or foreign file.
func Parse(line string) (State, error) {
	fields := map[string]string{}
	for _, word := range strings.Fields(line) {
		key, value, found := strings.Cut(word, "=")
		if !found || key == "" {
			return State{}, fmt.Errorf("not a key=value word: %q", word)
		}
		fields[key] = value
	}
	s := State{fields: fields}
	if _, ok := s.Num("seq"); !ok {
		return State{}, fmt.Errorf("no seq in the state line")
	}
	return s, nil
}

// Of builds a state from fields (tests, the simulator).
func Of(fields map[string]string) State { return State{fields: fields} }

// Valid says whether this is a parsed state.
func (s State) Valid() bool { return s.fields != nil }

// Str is a field's text, empty when missing.
func (s State) Str(key string) string { return s.fields[key] }

// Has says whether the field is there.
func (s State) Has(key string) bool {
	_, ok := s.fields[key]
	return ok
}

// Num is a field as a number.
func (s State) Num(key string) (float64, bool) {
	text, ok := s.fields[key]
	if !ok {
		return 0, false
	}
	value, err := strconv.ParseFloat(text, 64)
	if err != nil || math.IsNaN(value) || math.IsInf(value, 0) {
		return 0, false
	}
	return value, true
}

// N is a field as a number, 0 when missing.
func (s State) N(key string) float64 {
	value, _ := s.Num(key)
	return value
}

// Seq is the sample's number: it grows with every sample the game writes.
func (s State) Seq() int64 { return int64(s.N("seq")) }

// Vec reads three fields as a vector; ok is false when x or y is missing.
func (s State) Vec(x, y, z string) (v [3]float64, ok bool) {
	var okx, oky bool
	v[0], okx = s.Num(x)
	v[1], oky = s.Num(y)
	v[2], _ = s.Num(z)
	return v, okx && oky
}

// Line writes the state back as a line, keys in the given order first.
func (s State) Line(order ...string) string {
	var out []string
	seen := map[string]bool{}
	for _, key := range order {
		if value, ok := s.fields[key]; ok {
			out = append(out, key+"="+value)
			seen[key] = true
		}
	}
	var rest []string
	for key, value := range s.fields {
		if !seen[key] {
			rest = append(rest, key+"="+value)
		}
	}
	// Stable: sorted by text.
	for i := range rest {
		for j := i + 1; j < len(rest); j++ {
			if rest[j] < rest[i] {
				rest[i], rest[j] = rest[j], rest[i]
			}
		}
	}
	return strings.Join(append(out, rest...), " ")
}
