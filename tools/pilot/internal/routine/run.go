package routine

import (
	"errors"
	"fmt"
	"math"
	"strings"
	"time"

	"github.com/RPCU/tandem/tools/pilot/internal/pad"
	"github.com/RPCU/tandem/tools/pilot/internal/state"
)

// Game is the running game as the pilot reaches it: tandem's command file,
// the plugin's pilot state and tandem's log.
type Game interface {
	// Command hands tandem one command (tools/game.sh cmd).
	Command(text string) error
	// State is the latest pilot state; false when there is none to read.
	State() (state.State, bool)
	// LogLines are the game log's lines since the last call.
	LogLines() []string
}

// Clock is time as the interpreter sees it (real, or the simulator's).
type Clock interface {
	Now() time.Time
	Sleep(d time.Duration)
}

// Options tune a run; zero values take the defaults.
type Options struct {
	Tick       time.Duration // control period (50 ms)
	StaleAfter time.Duration // state not advancing: inputs released (1.5 s)
	StallLimit time.Duration // state not advancing: the run fails (90 s)
	AckTimeout time.Duration // an act's acknowledgement (4 s)
	Heartbeat  time.Duration // "pilot beat" period (1 s)
	Logf       func(format string, args ...any)
	// Stop ends the run when closed (a signal).
	Stop <-chan struct{}
	// Guard is asked every second whether the pilot may still act (the game
	// is ours and has the focus); an error ends the run.
	Guard func() error
	// WakePad: before the first step that uses the pad, nudge the view
	// until the game says its last input came from a pad (the state's
	// `pad`). The game reads a new pad only after its first input: a press
	// before that is lost (stealth's crouch did nothing, 2026-11-21). For a
	// uinput pad; keys and mouse never make `pad` 1.
	WakePad bool
}

// Result is how a run ended.
type Result struct {
	Passed   bool
	Line     int
	Reason   string
	Steps    int
	Warnings []string
	Seconds  float64
}

func (r Result) String() string {
	if r.Passed {
		return fmt.Sprintf("PASS: %d steps in %.1f s, %d warning(s)", r.Steps, r.Seconds, len(r.Warnings))
	}
	return fmt.Sprintf("FAIL at line %d: %s", r.Line, r.Reason)
}

var errStopped = errors.New("stopped")

type runner struct {
	routine *Routine
	game    Game
	pad     *pad.Guarded
	clock   Clock
	opt     Options

	start      time.Time
	deadline   time.Time
	last       state.State
	lastChange time.Time
	stale      bool
	lastBeat   time.Time
	lastOn     time.Time
	lastGuard  time.Time
	seq        int
	origin     [3]float64
	log        []string
	mark       int
	steps      int
	warnings   []string

	// Calibration: which way each stick axis acts (1, or -1 when inverted),
	// and how fast the right stick turns the view at full deflection.
	moveSign  [2]float64
	lookSign  [2]float64
	steerSign float64

	padAwake bool // the game took the pad's input (WakePad)
}

// Run plays a routine. It always leaves the pad at rest and the plugin's
// pilot off.
func Run(routine *Routine, game Game, p *pad.Guarded, clock Clock, opt Options) (result Result) {
	if opt.Tick == 0 {
		opt.Tick = 50 * time.Millisecond
	}
	if opt.StaleAfter == 0 {
		opt.StaleAfter = 1500 * time.Millisecond
	}
	if opt.StallLimit == 0 {
		opt.StallLimit = 90 * time.Second
	}
	if opt.AckTimeout == 0 {
		opt.AckTimeout = 4 * time.Second
	}
	if opt.Heartbeat == 0 {
		opt.Heartbeat = time.Second
	}
	if opt.Logf == nil {
		opt.Logf = func(string, ...any) {}
	}
	r := &runner{
		routine: routine, game: game, pad: p, clock: clock, opt: opt,
		moveSign: [2]float64{1, 1}, lookSign: [2]float64{1, 1}, steerSign: 1,
	}
	r.start = clock.Now()
	r.deadline = r.start.Add(time.Duration(routine.Limit * float64(time.Second)))
	r.lastChange = r.start
	// A sequence no earlier run of this game used: the plugin's last
	// acknowledgement survives the pilot going off.
	r.seq = int(r.start.Unix()%100000) * 100

	defer func() {
		if p := recover(); p != nil {
			result = Result{Line: 0, Reason: fmt.Sprintf("the pilot broke: %v", p)}
		}
		_ = r.pad.Neutral()
		_ = r.game.Command("pilot off")
		result.Steps = r.steps
		result.Warnings = r.warnings
		result.Seconds = r.clock.Now().Sub(r.start).Seconds()
	}()

	line, err := r.begin()
	if err == nil {
		line, err = r.run(routine.Steps)
	}
	if err != nil {
		return Result{Line: line, Reason: err.Error()}
	}
	return Result{Passed: true}
}

func (r *runner) logf(format string, args ...any) {
	r.opt.Logf("[%6.1f] "+format, append([]any{r.clock.Now().Sub(r.start).Seconds()}, args...)...)
}

// begin turns the plugin's pilot on and waits for its state.
func (r *runner) begin() (int, error) {
	if err := r.game.Command("pilot on"); err != nil {
		return 0, fmt.Errorf("pilot on: %w", err)
	}
	r.lastOn = r.clock.Now()
	limit := r.clock.Now().Add(15 * time.Second)
	for !(r.last.Valid() && r.last.N("pilot") == 1 && !r.stale) {
		if r.clock.Now().After(limit) {
			if !r.last.Valid() {
				return 0, errors.New("no pilot state from the plugin within 15 s: is the game in a loaded save with this build of tandem?")
			}
			return 0, errors.New("the plugin's pilot did not turn on within 15 s (see tandem's log)")
		}
		if err := r.tick(); err != nil {
			return 0, err
		}
	}
	if r.routine.Origin != nil {
		r.origin = *r.routine.Origin
	} else if at, ok := r.last.Vec("x", "y", "z"); ok {
		r.origin = at
	} else {
		return 0, errors.New("the pilot state has no position")
	}
	r.logf("origin %.2f %.2f %.2f", r.origin[0], r.origin[1], r.origin[2])
	// A dry run's fixtures: only the simulator takes them.
	if fixture, ok := r.game.(interface{ Setup(string) error }); ok {
		for _, text := range r.routine.Fixtures {
			if err := fixture.Setup(text); err != nil {
				return 0, err
			}
		}
		if err := r.tick(); err != nil {
			return 0, err
		}
	}
	for _, need := range r.routine.Needs {
		if ok, why := r.eval(need); !ok {
			return 0, fmt.Errorf("needs %s: %s", need.Text, why)
		}
	}
	return 0, nil
}

// tick lets one control period pass: it reads the state, keeps the pad's
// dead-man fed while the game answers, releases everything when it does
// not, and sends the plugin its heartbeat.
func (r *runner) tick() error {
	select {
	case <-r.opt.Stop:
		return errStopped
	default:
	}
	r.clock.Sleep(r.opt.Tick)
	now := r.clock.Now()
	if now.After(r.deadline) {
		return fmt.Errorf("the routine's limit of %.0f s is over", r.routine.Limit)
	}
	if st, ok := r.game.State(); ok && (!r.last.Valid() || st.Seq() != r.last.Seq()) {
		r.last = st
		r.lastChange = now
	}
	silent := now.Sub(r.lastChange)
	if silent > r.opt.StaleAfter {
		if !r.stale {
			r.stale = true
			r.logf("the game's state stopped (a loading screen, a pause, a crash?): inputs released")
			_ = r.pad.Neutral()
		}
		if silent > r.opt.StallLimit {
			return fmt.Errorf("no pilot state for %.0f s", silent.Seconds())
		}
	} else {
		if r.stale {
			r.stale = false
			r.logf("the game's state is back after %.1f s", silent.Seconds())
		}
		r.pad.Feed(now)
	}
	if !r.stale && now.Sub(r.lastBeat) >= r.opt.Heartbeat {
		r.lastBeat = now
		if err := r.game.Command("pilot beat"); err != nil {
			return fmt.Errorf("heartbeat: %w", err)
		}
	}
	// No state for a while: the plugin's pilot may have gone off on its own
	// (its dead-man during a loading screen, a loaded save, a test reset),
	// and it writes no state while off. Asked on again every 3 s; a game
	// that takes no command now will take a later one.
	if r.stale && silent > 3*time.Second && now.Sub(r.lastOn) > 3*time.Second {
		r.lastOn = now
		_ = r.game.Command("pilot on")
	}
	if r.opt.Guard != nil && now.Sub(r.lastGuard) >= time.Second {
		r.lastGuard = now
		if err := r.opt.Guard(); err != nil {
			return fmt.Errorf("guard: %w", err)
		}
	}
	r.log = append(r.log, r.game.LogLines()...)
	return nil
}

// sleep lets time pass in ticks.
func (r *runner) sleep(seconds float64) error {
	until := r.clock.Now().Add(time.Duration(seconds * float64(time.Second)))
	for r.clock.Now().Before(until) {
		if err := r.tick(); err != nil {
			return err
		}
	}
	return nil
}

func (r *runner) run(steps []Step) (int, error) {
	for i := range steps {
		step := &steps[i]
		r.steps++
		began := r.clock.Now()
		detail, err := r.step(step)
		if err != nil && errors.Is(err, errStopped) {
			return step.Line, errors.New("stopped")
		}
		var nested *nestedError
		if errors.As(err, &nested) {
			return nested.line, nested.err
		}
		took := r.clock.Now().Sub(began).Seconds()
		switch {
		case err == nil:
			if step.Kind != If && step.Kind != Repeat {
				r.logf("line %d ok: %s%s (%.1f s)", step.Line, step.Source, detail, took)
			}
		case step.Optional:
			_ = r.pad.Neutral()
			warning := fmt.Sprintf("line %d (optional) %s: %v", step.Line, step.Source, err)
			r.warnings = append(r.warnings, warning)
			r.logf("WARN %s", warning)
		default:
			return step.Line, err
		}
	}
	return 0, nil
}

// nestedError carries the line of a step that failed inside a block.
type nestedError struct {
	line int
	err  error
}

func (e *nestedError) Error() string { return e.err.Error() }

// usesPad: the step sends the pad something.
func usesPad(kind Kind) bool {
	switch kind {
	case Press, Hold, StickStep, Trigger, Walk, Drive, Face, Choose, Calibrate:
		return true
	}
	return false
}

// wake nudges the view right, then back, until the nudge turns it and the
// game says its last input came from a pad (`pad` 1; a plugin without the
// field: the turn alone), for at most 5 s. The pad's own last-used flag can
// be left over from an earlier pad: the turn says this one is read.
func (r *runner) wake() error {
	if r.padAwake || !r.opt.WakePad {
		return nil
	}
	limit := r.clock.Now().Add(5 * time.Second)
	for nudges := 1; ; nudges++ {
		fx, fy, ok := r.forward()
		if err := r.pad.Stick(pad.RightStick, 0.35, 0); err != nil {
			return err
		}
		err := r.sleep(0.15)
		gx, gy, _ := r.forward()
		if err == nil {
			err = r.pad.Stick(pad.RightStick, -0.35, 0)
		}
		if err == nil {
			err = r.sleep(0.15)
		}
		_ = r.pad.Stick(pad.RightStick, 0, 0)
		if err != nil {
			return err
		}
		turned := ok && math.Abs(leftOf(fx, fy, gx, gy)) > 0.2*math.Pi/180
		if turned && (!r.last.Has("pad") || r.last.N("pad") == 1) {
			r.padAwake = true
			r.logf("the game reads the pad (after %d nudge(s) of the view)", nudges)
			return nil
		}
		if r.clock.Now().After(limit) {
			return fmt.Errorf("the game never took the pad's input in 5 s (%d nudges: view turned %t, pad %s): Steam Input, or the pad not seen (autopilot.md, step 6)", nudges, turned, r.last.Str("pad"))
		}
		if err := r.sleep(0.4); err != nil {
			return err
		}
	}
}

func (r *runner) step(step *Step) (string, error) {
	if usesPad(step.Kind) {
		if err := r.wake(); err != nil {
			return "", err
		}
	}
	switch step.Kind {
	case Cmd:
		if err := r.game.Command(step.Text); err != nil {
			return "", err
		}
		return "", r.tick()
	case Act:
		return r.act(step)
	case Say:
		r.logf("%s", step.Text)
		return "", nil
	case Show:
		if err := r.tick(); err != nil {
			return "", err
		}
		r.logf("state: %s", r.last.Line("seq", "t", "pilot", "ack", "res", "x", "y", "z", "fx", "fy", "fz", "hx", "hy", "speed", "veh", "seat", "hp"))
		return "", nil
	case Press:
		if r.stale {
			return "", errors.New("the game's state stopped: nothing pressed")
		}
		for _, b := range step.Buttons {
			if err := r.pad.Button(b, true); err != nil {
				return "", err
			}
		}
		err := r.sleep(step.Seconds)
		for _, b := range step.Buttons {
			_ = r.pad.Button(b, false)
		}
		if err != nil {
			return "", err
		}
		// Let the game see the release before the next press.
		return "", r.sleep(0.05)
	case Hold:
		for _, b := range step.Buttons {
			if err := r.pad.Button(b, true); err != nil {
				return "", err
			}
		}
		return "", r.tick()
	case Release:
		if step.All {
			return "", r.pad.Neutral()
		}
		for _, b := range step.Buttons {
			if err := r.pad.Button(b, false); err != nil {
				return "", err
			}
		}
		return "", r.tick()
	case StickStep:
		if err := r.pad.Stick(step.Stick, step.X, step.Y); err != nil {
			return "", err
		}
		if step.Seconds == 0 {
			return "", r.tick()
		}
		err := r.sleep(step.Seconds)
		_ = r.pad.Stick(step.Stick, 0, 0)
		return "", err
	case Trigger:
		if err := r.pad.Trigger(step.Trig, step.Value); err != nil {
			return "", err
		}
		if step.Seconds == 0 {
			return "", r.tick()
		}
		err := r.sleep(step.Seconds)
		_ = r.pad.Trigger(step.Trig, 0)
		return "", err
	case Walk:
		return r.walk(step)
	case Drive:
		return r.drive(step)
	case Face:
		return r.face(step)
	case Wait:
		return "", r.sleep(step.Seconds)
	case Until:
		timeout := step.Timeout
		if timeout == 0 {
			timeout = 30
		}
		limit := r.clock.Now().Add(time.Duration(timeout * float64(time.Second)))
		for {
			ok, why := r.eval(step.Cond)
			if ok && !r.stale {
				return "", nil
			}
			if r.clock.Now().After(limit) {
				return "", fmt.Errorf("not within %.0f s: %s", timeout, why)
			}
			if err := r.tick(); err != nil {
				return "", err
			}
		}
	case Check:
		if err := r.tick(); err != nil {
			return "", err
		}
		if ok, why := r.eval(step.Cond); !ok {
			return "", errors.New(why)
		}
		return "", nil
	case Expect:
		timeout := step.Timeout
		if timeout == 0 {
			timeout = 30
		}
		limit := r.clock.Now().Add(time.Duration(timeout * float64(time.Second)))
		from := r.mark
		for {
			for ; from < len(r.log); from++ {
				if step.Regex.MatchString(r.log[from]) {
					return ": " + clip(r.log[from]), nil
				}
			}
			if r.clock.Now().After(limit) {
				return "", fmt.Errorf("no game log line within %.0f s", timeout)
			}
			if err := r.tick(); err != nil {
				return "", err
			}
		}
	case Reject:
		if err := r.tick(); err != nil {
			return "", err
		}
		for _, line := range r.log[r.mark:] {
			if step.Regex.MatchString(line) {
				return "", fmt.Errorf("the game logged: %s", clip(line))
			}
		}
		return "", nil
	case Mark:
		if err := r.tick(); err != nil {
			return "", err
		}
		r.mark = len(r.log)
		return "", nil
	case Calibrate:
		return r.calibrate(step)
	case Choose:
		return r.choose(step)
	case If:
		branch := step.Else
		if ok, _ := r.eval(step.Cond); ok {
			branch = step.Then
		}
		if line, err := r.run(branch); err != nil {
			return "", &nestedError{line, err}
		}
		return "", nil
	case Repeat:
		for i := 0; i < step.Count; i++ {
			if line, err := r.run(step.Then); err != nil {
				return "", &nestedError{line, err}
			}
		}
		return "", nil
	}
	return "", fmt.Errorf("unknown step %q", step.Kind)
}

func clip(line string) string {
	line = strings.TrimSpace(line)
	if len(line) > 140 {
		return line[:140] + "…"
	}
	return line
}

// act sends a script-level action to the plugin's pilot and waits for its
// acknowledgement: "pilot N ACTION ARGS" is answered in the state by ack=N
// and res=ok... or res=fail:REASON.
func (r *runner) act(step *Step) (string, error) {
	if r.stale {
		return "", errors.New("the game's state stopped: not sent")
	}
	r.seq++
	if err := r.game.Command(fmt.Sprintf("pilot %d %s", r.seq, step.Text)); err != nil {
		return "", err
	}
	timeout := r.opt.AckTimeout
	if step.Timeout != 0 {
		timeout = time.Duration(step.Timeout * float64(time.Second))
	}
	limit := r.clock.Now().Add(timeout)
	for int(r.last.N("ack")) != r.seq {
		if r.clock.Now().After(limit) {
			return "", fmt.Errorf("the plugin did not acknowledge within %.0f s", timeout.Seconds())
		}
		if err := r.tick(); err != nil {
			return "", err
		}
	}
	res := r.last.Str("res")
	if !strings.HasPrefix(res, "ok") {
		return "", fmt.Errorf("the game refused: %s", strings.ReplaceAll(strings.TrimPrefix(res, "fail:"), "_", " "))
	}
	if res == "ok" {
		return "", nil
	}
	return " -> " + strings.ReplaceAll(strings.TrimPrefix(res, "ok:"), "_", " "), nil
}

// resolve turns a step's position into world coordinates.
func (r *runner) resolve(pos Pos) ([3]float64, error) {
	if pos.Target {
		at, ok := r.last.Vec("tx", "ty", "tz")
		if !ok || r.last.N("tid") == 0 {
			return at, errors.New("no target (act target ... first)")
		}
		return at, nil
	}
	var out [3]float64
	here, _ := r.last.Vec("x", "y", "z")
	for i, c := range pos.At {
		out[i] = c.Value
		if c.Relative {
			out[i] += r.origin[i]
		}
	}
	if !pos.HasZ {
		out[2] = here[2]
	}
	return out, nil
}

// eval says whether a condition holds on the latest state, and why not.
func (r *runner) eval(cond Cond) (bool, string) {
	if !r.last.Valid() {
		return false, "no state"
	}
	for _, t := range cond.Terms {
		if t.Near != nil {
			to, err := r.resolve(*t.Near)
			if err != nil {
				return false, err.Error()
			}
			here, _ := r.last.Vec("x", "y", "z")
			d := math.Hypot(to[0]-here[0], to[1]-here[1])
			if t.Near.HasZ || t.Near.Target {
				d = math.Sqrt(d*d + (to[2]-here[2])*(to[2]-here[2]))
			}
			if d > t.Radius {
				return false, fmt.Sprintf("%.2f m away (near %.1f)", d, t.Radius)
			}
			continue
		}
		if !r.last.Has(t.Key) {
			return false, fmt.Sprintf("the state has no %s", t.Key)
		}
		if t.IsWord {
			got := r.last.Str(t.Key)
			if (got == t.Word) != (t.Op == "==") {
				return false, fmt.Sprintf("%s is %s", t.Key, got)
			}
			continue
		}
		got, ok := r.last.Num(t.Key)
		if !ok {
			return false, fmt.Sprintf("%s is %s, not a number", t.Key, r.last.Str(t.Key))
		}
		var holds bool
		switch t.Op {
		case "==":
			holds = got == t.Number
		case "!=":
			holds = got != t.Number
		case "<":
			holds = got < t.Number
		case "<=":
			holds = got <= t.Number
		case ">":
			holds = got > t.Number
		case ">=":
			holds = got >= t.Number
		}
		if !holds {
			return false, fmt.Sprintf("%s is %g (want %s %g)", t.Key, got, t.Op, t.Number)
		}
	}
	return true, ""
}
