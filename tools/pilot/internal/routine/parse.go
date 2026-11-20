// Package routine is the pilot's routine language: small scripts of steps
// with checks (tools/pilot/routines/*.pilot), their parser and the
// interpreter that plays them on the game through a pad and the plugin's
// pilot commands. docs/architecture/autopilot.md describes the language.
package routine

import (
	"bufio"
	"fmt"
	"io"
	"regexp"
	"strconv"
	"strings"

	"github.com/RPCU/tandem/tools/pilot/internal/pad"
)

// Kind is what a step does.
type Kind string

// The steps of the language.
const (
	Cmd       Kind = "cmd"
	Act       Kind = "act"
	Press     Kind = "press"
	Hold      Kind = "hold"
	Release   Kind = "release"
	StickStep Kind = "stick"
	Trigger   Kind = "trigger"
	Walk      Kind = "walk"
	Drive     Kind = "drive"
	Face      Kind = "face"
	Wait      Kind = "wait"
	Until     Kind = "until"
	Check     Kind = "check"
	Expect    Kind = "expect"
	Reject    Kind = "reject"
	Mark      Kind = "mark"
	Say       Kind = "say"
	Calibrate Kind = "calibrate"
	Choose    Kind = "choose"
	Show      Kind = "show"
	If        Kind = "if"
	Repeat    Kind = "repeat"
)

// Coord is one coordinate: absolute, or (~) from the routine's origin.
type Coord struct {
	Value    float64
	Relative bool
}

// Pos is where a step goes or looks: coordinates, or the target the
// plugin's pilot holds (act target ...).
type Pos struct {
	Target bool
	At     [3]Coord
	HasZ   bool
}

// Term is one comparison of a condition; Near instead when Key is empty.
type Term struct {
	Key    string
	Op     string
	Number float64
	Word   string
	IsWord bool
	Near   *Pos
	Radius float64
}

// Cond is terms that must all hold.
type Cond struct {
	Terms []Term
	Text  string
}

// Step is one line of a routine, parsed.
type Step struct {
	Line     int
	Source   string
	Kind     Kind
	Optional bool

	Text    string // cmd, act, say
	Regex   *regexp.Regexp
	Buttons []pad.Button
	All     bool // release all
	Stick   pad.Stick
	Trig    pad.Trigger
	X, Y    float64
	Value   float64
	Seconds float64 // 0: not given
	Count   int

	Target    Pos
	Within    float64
	Timeout   float64
	Tolerance float64
	Speed     float64
	Pace      string
	What      string // calibrate move|look

	Cond Cond
	Then []Step
	Else []Step
}

// Routine is a parsed file.
type Routine struct {
	Name  string
	Limit float64 // seconds for the whole routine
	Steps []Step
	// Needs are conditions on the state checked before the first step.
	Needs []Cond
	// Fixtures are the "sim ..." lines: what the simulator puts in its world
	// for a dry run of this routine. The real game never sees them.
	Fixtures []string
	// Origin, when set, is where ~ coordinates are measured from instead of
	// where V stands at the start.
	Origin *[3]float64
}

// DefaultLimit bounds a routine that sets no limit of its own.
const DefaultLimit = 300.0

// Error is a parse error at a line.
type Error struct {
	Line int
	Msg  string
}

func (e *Error) Error() string { return fmt.Sprintf("line %d: %s", e.Line, e.Msg) }

type parser struct {
	lines []string
	at    int
	out   *Routine
}

// Parse reads a routine.
func Parse(name string, in io.Reader) (*Routine, error) {
	p := &parser{out: &Routine{Name: name, Limit: DefaultLimit}}
	scan := bufio.NewScanner(in)
	for scan.Scan() {
		p.lines = append(p.lines, scan.Text())
	}
	if err := scan.Err(); err != nil {
		return nil, err
	}
	steps, closer, err := p.block()
	if err != nil {
		return nil, err
	}
	if closer != "" {
		return nil, &Error{p.at, closer + " without an if or repeat"}
	}
	p.out.Steps = steps
	return p.out, nil
}

// block parses steps until the file's end or a line that closes it (else,
// end), which it returns.
func (p *parser) block() ([]Step, string, error) {
	var steps []Step
	for p.at < len(p.lines) {
		raw := p.lines[p.at]
		p.at++
		line := p.at
		text := strings.TrimSpace(raw)
		if i := strings.Index(text, " #"); i >= 0 && !strings.HasPrefix(text, "expect") && !strings.HasPrefix(text, "reject") {
			text = strings.TrimSpace(text[:i])
		}
		if text == "" || strings.HasPrefix(text, "#") {
			continue
		}
		words := strings.Fields(text)
		switch words[0] {
		case "else", "end":
			if len(words) != 1 {
				return nil, "", &Error{line, words[0] + " takes nothing"}
			}
			return steps, words[0], nil
		case "limit":
			v, err := number(words, 1, line)
			if err != nil || v <= 0 {
				return nil, "", &Error{line, "limit SECONDS"}
			}
			p.out.Limit = v
			continue
		case "needs":
			cond, err := parseCond(words[1:], line)
			if err != nil {
				return nil, "", err
			}
			p.out.Needs = append(p.out.Needs, cond)
			continue
		case "sim":
			if len(words) < 2 {
				return nil, "", &Error{line, "sim FIXTURE (for the dry run)"}
			}
			p.out.Fixtures = append(p.out.Fixtures, strings.Join(words[1:], " "))
			continue
		case "origin":
			if len(words) != 4 {
				return nil, "", &Error{line, "origin X Y Z"}
			}
			var o [3]float64
			for i := range o {
				v, err := strconv.ParseFloat(words[i+1], 64)
				if err != nil {
					return nil, "", &Error{line, "origin X Y Z"}
				}
				o[i] = v
			}
			p.out.Origin = &o
			continue
		}
		step, err := p.step(words, text, line)
		if err != nil {
			return nil, "", err
		}
		steps = append(steps, step)
	}
	return steps, "", nil
}

func (p *parser) step(words []string, text string, line int) (Step, error) {
	step := Step{Line: line, Source: text}
	if words[0] == "optional" {
		if len(words) < 2 {
			return step, &Error{line, "optional STEP"}
		}
		step.Optional = true
		words = words[1:]
		text = strings.TrimSpace(strings.TrimPrefix(text, "optional"))
	}
	rest := strings.TrimSpace(strings.TrimPrefix(text, words[0]))
	bad := func(usage string) (Step, error) { return step, &Error{line, usage} }
	step.Kind = Kind(words[0])
	var err error
	switch step.Kind {
	case Cmd, Act, Say:
		if rest == "" {
			return bad(words[0] + " TEXT")
		}
		step.Text = rest
		if step.Kind == Act {
			var opts map[string]float64
			if words, opts, err = options(words, line, "timeout"); err != nil {
				return step, err
			}
			step.Text = strings.Join(words[1:], " ")
			step.Timeout = opts["timeout"]
			if step.Text == "" {
				return bad("act ACTION [ARGS] [timeout S]")
			}
		}
		if step.Kind == Cmd && strings.HasPrefix(rest, "pilot") {
			return bad("pilot commands are sent with act (they are acknowledged)")
		}
	case Press:
		if len(words) < 2 || len(words) > 3 {
			return bad("press BUTTON [SECONDS]")
		}
		if step.Buttons, err = buttons(words[1:2], line); err != nil {
			return step, err
		}
		step.Seconds = 0.1
		if len(words) == 3 {
			if step.Seconds, err = number(words, 2, line); err != nil || step.Seconds <= 0 || step.Seconds > 30 {
				return bad("press BUTTON [SECONDS] (at most 30)")
			}
		}
	case Hold:
		if len(words) < 2 {
			return bad("hold BUTTON...")
		}
		if step.Buttons, err = buttons(words[1:], line); err != nil {
			return step, err
		}
	case Release:
		if len(words) == 2 && words[1] == "all" {
			step.All = true
			break
		}
		if len(words) < 2 {
			return bad("release BUTTON... | release all")
		}
		if step.Buttons, err = buttons(words[1:], line); err != nil {
			return step, err
		}
	case StickStep:
		if len(words) < 4 || len(words) > 5 {
			return bad("stick left|right X Y [SECONDS]")
		}
		switch words[1] {
		case "left":
			step.Stick = pad.LeftStick
		case "right":
			step.Stick = pad.RightStick
		default:
			return bad("stick left|right X Y [SECONDS]")
		}
		if step.X, err = number(words, 2, line); err != nil {
			return step, err
		}
		if step.Y, err = number(words, 3, line); err != nil {
			return step, err
		}
		if step.X < -1 || step.X > 1 || step.Y < -1 || step.Y > 1 {
			return bad("stick values run from -1 to 1")
		}
		if len(words) == 5 {
			if step.Seconds, err = number(words, 4, line); err != nil || step.Seconds <= 0 || step.Seconds > 60 {
				return bad("stick ... SECONDS (at most 60)")
			}
		}
	case Trigger:
		if len(words) < 3 || len(words) > 4 {
			return bad("trigger lt|rt VALUE [SECONDS]")
		}
		switch words[1] {
		case "lt":
			step.Trig = pad.LT
		case "rt":
			step.Trig = pad.RT
		default:
			return bad("trigger lt|rt VALUE [SECONDS]")
		}
		if step.Value, err = number(words, 2, line); err != nil || step.Value < 0 || step.Value > 1 {
			return bad("trigger values run from 0 to 1")
		}
		if len(words) == 4 {
			if step.Seconds, err = number(words, 3, line); err != nil || step.Seconds <= 0 || step.Seconds > 60 {
				return bad("trigger ... SECONDS (at most 60)")
			}
		}
	case Walk, Drive, Face:
		var opts map[string]float64
		var pace string
		if words, pace, err = wordOption(words, line, "pace", "walk", "jog", "sprint"); err != nil {
			return step, err
		}
		if words, opts, err = options(words, line, "within", "timeout", "tolerance", "speed"); err != nil {
			return step, err
		}
		step.Pace = pace
		step.Within, step.Timeout, step.Tolerance, step.Speed = opts["within"], opts["timeout"], opts["tolerance"], opts["speed"]
		if step.Target, err = position(words[1:], line); err != nil {
			return step, err
		}
		if step.Kind != Walk && pace != "" {
			return bad("pace is for walk")
		}
	case Wait:
		if len(words) != 2 {
			return bad("wait SECONDS")
		}
		if step.Seconds, err = number(words, 1, line); err != nil || step.Seconds < 0 || step.Seconds > 600 {
			return bad("wait SECONDS (at most 600)")
		}
	case Until, Check:
		var opts map[string]float64
		if words, opts, err = options(words, line, "timeout"); err != nil {
			return step, err
		}
		step.Timeout = opts["timeout"]
		if step.Kind == Check && step.Timeout != 0 {
			return bad("check is immediate: use until COND timeout S")
		}
		if step.Cond, err = parseCond(words[1:], line); err != nil {
			return step, err
		}
	case Expect, Reject:
		pattern := rest
		if step.Kind == Expect {
			// A trailing "timeout S" is the option, not the pattern.
			if n := len(words); n >= 4 && words[n-2] == "timeout" {
				if v, perr := strconv.ParseFloat(words[n-1], 64); perr == nil && v > 0 {
					step.Timeout = v
					pattern = strings.TrimSpace(strings.TrimSuffix(strings.TrimSpace(strings.TrimSuffix(rest, words[n-1])), "timeout"))
				}
			}
		}
		if pattern == "" {
			return bad(words[0] + " REGEX")
		}
		if step.Regex, err = regexp.Compile(pattern); err != nil {
			return step, &Error{line, "bad pattern: " + err.Error()}
		}
		step.Text = pattern
	case Mark, Show:
		if len(words) != 1 {
			return bad(words[0] + " takes nothing")
		}
	case Calibrate:
		step.What = "all"
		if len(words) == 2 && (words[1] == "move" || words[1] == "look" || words[1] == "steer") {
			step.What = words[1]
		} else if len(words) != 1 {
			return bad("calibrate [move|look|steer]")
		}
	case Choose:
		var opts map[string]float64
		if words, opts, err = options(words, line, "timeout"); err != nil {
			return step, err
		}
		step.Timeout = opts["timeout"]
		if len(words) != 2 {
			return bad("choose NUMBER [timeout S]")
		}
		count, cerr := strconv.Atoi(words[1])
		if cerr != nil || count < 1 || count > 12 {
			return bad("choose NUMBER (1 is the first choice)")
		}
		step.Count = count
	case If:
		if step.Optional {
			return bad("optional if makes no sense")
		}
		if step.Cond, err = parseCond(words[1:], line); err != nil {
			return step, err
		}
		var closer string
		if step.Then, closer, err = p.block(); err != nil {
			return step, err
		}
		if closer == "else" {
			if step.Else, closer, err = p.block(); err != nil {
				return step, err
			}
		}
		if closer != "end" {
			return step, &Error{line, "if without end"}
		}
	case Repeat:
		if len(words) != 2 {
			return bad("repeat COUNT")
		}
		count, cerr := strconv.Atoi(words[1])
		if cerr != nil || count < 1 || count > 1000 {
			return bad("repeat COUNT (1 to 1000)")
		}
		step.Count = count
		var closer string
		if step.Then, closer, err = p.block(); err != nil {
			return step, err
		}
		if closer != "end" {
			return step, &Error{line, "repeat without end"}
		}
	default:
		return step, &Error{line, "unknown step: " + words[0]}
	}
	return step, nil
}

func number(words []string, at, line int) (float64, error) {
	if at >= len(words) {
		return 0, &Error{line, "a number is missing"}
	}
	v, err := strconv.ParseFloat(words[at], 64)
	if err != nil {
		return 0, &Error{line, "not a number: " + words[at]}
	}
	return v, nil
}

func buttons(names []string, line int) ([]pad.Button, error) {
	var out []pad.Button
	for _, name := range names {
		b, ok := pad.ButtonNamed(name)
		if !ok {
			return nil, &Error{line, "no such button: " + name + " (a b x y lb rb l3 r3 up down left right; start, back and guide are never pressed)"}
		}
		out = append(out, b)
	}
	return out, nil
}

// options takes "KEY NUMBER" pairs off the end of words.
func options(words []string, line int, keys ...string) ([]string, map[string]float64, error) {
	out := map[string]float64{}
	for len(words) >= 3 {
		key := words[len(words)-2]
		known := false
		for _, k := range keys {
			known = known || k == key
		}
		if !known {
			break
		}
		v, err := strconv.ParseFloat(words[len(words)-1], 64)
		if err != nil || v <= 0 {
			return nil, nil, &Error{line, key + " takes a positive number"}
		}
		if _, twice := out[key]; twice {
			return nil, nil, &Error{line, key + " given twice"}
		}
		out[key] = v
		words = words[:len(words)-2]
	}
	return words, out, nil
}

// wordOption takes "KEY WORD" out of words, wherever it is.
func wordOption(words []string, line int, key string, values ...string) ([]string, string, error) {
	for i := 1; i < len(words); i++ {
		if words[i] != key {
			continue
		}
		if i+1 >= len(words) {
			return nil, "", &Error{line, key + " " + strings.Join(values, "|")}
		}
		for _, v := range values {
			if v == words[i+1] {
				rest := append(append([]string{}, words[:i]...), words[i+2:]...)
				return rest, v, nil
			}
		}
		return nil, "", &Error{line, key + " " + strings.Join(values, "|")}
	}
	return words, "", nil
}

func coord(word string) (Coord, bool) {
	c := Coord{}
	if strings.HasPrefix(word, "~") {
		c.Relative = true
		word = word[1:]
		if word == "" {
			return c, true
		}
	}
	v, err := strconv.ParseFloat(word, 64)
	if err != nil {
		return c, false
	}
	c.Value = v
	return c, true
}

// position reads "target" or "X Y [Z]" (each a number, or ~N from the
// origin); nothing may follow.
func position(words []string, line int) (Pos, error) {
	pos, used, err := positionPrefix(words, line)
	if err != nil {
		return pos, err
	}
	if used != len(words) {
		return pos, &Error{line, "unexpected: " + strings.Join(words[used:], " ")}
	}
	return pos, nil
}

func positionPrefix(words []string, line int) (Pos, int, error) {
	var pos Pos
	if len(words) >= 1 && words[0] == "target" {
		pos.Target = true
		return pos, 1, nil
	}
	if len(words) < 2 {
		return pos, 0, &Error{line, "a position is X Y [Z] (~N: from the origin) or target"}
	}
	for i := 0; i < 2; i++ {
		c, ok := coord(words[i])
		if !ok {
			return pos, 0, &Error{line, "not a coordinate: " + words[i]}
		}
		pos.At[i] = c
	}
	used := 2
	if len(words) >= 3 {
		if c, ok := coord(words[2]); ok {
			pos.At[2] = c
			pos.HasZ = true
			used = 3
		}
	}
	if !pos.HasZ {
		// No height given: V's own.
		pos.At[2] = Coord{Relative: true}
	}
	return pos, used, nil
}

var ops = []string{"==", "!=", "<=", ">=", "<", ">", "="}

// parseCond reads "TERM and TERM ...": KEY OP VALUE, or near X Y [Z] RADIUS.
func parseCond(words []string, line int) (Cond, error) {
	cond := Cond{Text: strings.Join(words, " ")}
	if len(words) == 0 {
		return cond, &Error{line, "a condition is missing"}
	}
	var term []string
	flush := func() error {
		if len(term) == 0 {
			return &Error{line, "an empty condition"}
		}
		t, err := parseTerm(term, line)
		if err != nil {
			return err
		}
		cond.Terms = append(cond.Terms, t)
		term = nil
		return nil
	}
	for _, word := range words {
		if word == "and" {
			if err := flush(); err != nil {
				return cond, err
			}
			continue
		}
		term = append(term, word)
	}
	if err := flush(); err != nil {
		return cond, err
	}
	return cond, nil
}

func parseTerm(words []string, line int) (Term, error) {
	if words[0] == "near" {
		pos, used, err := positionPrefix(words[1:], line)
		if err != nil {
			return Term{}, err
		}
		// "near X Y R" reads R as a height: the last number is the radius.
		if used == 3 && len(words) == 4 && !pos.Target {
			pos.HasZ = false
			pos.At[2] = Coord{Relative: true}
			used = 2
		}
		if len(words) != used+2 {
			return Term{}, &Error{line, "near X Y [Z] RADIUS | near target RADIUS"}
		}
		radius, err := strconv.ParseFloat(words[used+1], 64)
		if err != nil || radius <= 0 {
			return Term{}, &Error{line, "near: the radius is a positive number"}
		}
		return Term{Near: &pos, Radius: radius}, nil
	}
	// KEY OP VALUE, spaced or not.
	joined := strings.Join(words, "")
	for _, op := range ops {
		i := strings.Index(joined, op)
		if i <= 0 {
			continue
		}
		key, value := joined[:i], joined[i+len(op):]
		if value == "" {
			break
		}
		t := Term{Key: key, Op: op}
		if op == "=" {
			t.Op = "=="
		}
		if v, err := strconv.ParseFloat(value, 64); err == nil {
			t.Number = v
		} else {
			t.IsWord, t.Word = true, value
			if t.Op != "==" && t.Op != "!=" {
				return Term{}, &Error{line, "a word compares with == or != only: " + strings.Join(words, " ")}
			}
		}
		return t, nil
	}
	return Term{}, &Error{line, "not a condition (KEY OP VALUE, or near X Y RADIUS): " + strings.Join(words, " ")}
}
