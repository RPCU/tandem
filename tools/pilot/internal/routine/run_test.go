package routine_test

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/Banh-Canh/tandem/tools/pilot/internal/pad"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/routine"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/sim"
)

// play runs a routine's text on a simulator the test may prepare.
func play(t *testing.T, text string, prepare func(*sim.Sim), opt routine.Options) (routine.Result, *sim.Sim, []string) {
	t.Helper()
	parsed, err := routine.Parse("test", strings.NewReader(text))
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	world := sim.New()
	if prepare != nil {
		prepare(world)
	}
	var lines []string
	opt.Logf = func(format string, args ...any) {
		lines = append(lines, format)
		t.Logf(format, args...)
	}
	guarded := pad.NewGuarded(world, 500*time.Millisecond, world.Now(), nil)
	result := routine.Run(parsed, world, guarded, world, opt)
	// Whatever happened: nothing held, and the plugin's pilot told to stop.
	if !world.AtRest() {
		t.Errorf("the pad is not at rest after the run")
	}
	if last := world.Commands[len(world.Commands)-1]; last != "pilot off" {
		t.Errorf("the last command is %q, not pilot off", last)
	}
	return result, world, lines
}

func passes(t *testing.T, result routine.Result) {
	t.Helper()
	if !result.Passed {
		t.Fatalf("failed at line %d: %s", result.Line, result.Reason)
	}
}

func fails(t *testing.T, result routine.Result, line int, reason string) {
	t.Helper()
	if result.Passed {
		t.Fatalf("passed, want a failure at line %d (%s)", line, reason)
	}
	if result.Line != line || !strings.Contains(result.Reason, reason) {
		t.Fatalf("failed at line %d: %s; want line %d: …%s…", result.Line, result.Reason, line, reason)
	}
}

func TestWalkArrives(t *testing.T) {
	result, world, _ := play(t, "walk ~6 ~8\nwalk ~-3 ~2 pace walk\ncheck near ~-3 ~2 1\n", nil, routine.Options{})
	passes(t, result)
	if d := math.Hypot(world.Pos[0]-(sim.Field[0]-3), world.Pos[1]-(sim.Field[1]+2)); d > 0.8 {
		t.Errorf("V is %.2f m from the last waypoint", d)
	}
}

func TestWalkGoesWhereItLooksAway(t *testing.T) {
	// The view points elsewhere (yaw 2 rad): the stick still leads V to the
	// goal, in the view's frame.
	result, _, _ := play(t, "walk ~5 ~5 within 0.5\n", func(w *sim.Sim) { w.Yaw = 2 }, routine.Options{})
	passes(t, result)
}

func TestWalkSprints(t *testing.T) {
	result, world, _ := play(t, "walk ~0 ~40 pace sprint\n", nil, routine.Options{})
	passes(t, result)
	if result.Seconds > 9 {
		t.Errorf("40 m took %.1f s: it did not sprint", result.Seconds)
	}
	_ = world
}

func TestWalkJumpsAWallThenGivesUp(t *testing.T) {
	// A low wall across the way: a jump clears it.
	low := func(w *sim.Sim) {
		w.Wall = &[4]float64{sim.Field[0] - 5, sim.Field[1] + 3, sim.Field[0] + 5, sim.Field[1] + 3.5}
	}
	result, _, _ := play(t, "walk ~0 ~8\n", low, routine.Options{})
	passes(t, result)
	// A thick one: three jumps, then stuck, with the place it is stuck at.
	thick := func(w *sim.Sim) {
		w.Wall = &[4]float64{sim.Field[0] - 50, sim.Field[1] + 3, sim.Field[0] + 50, sim.Field[1] + 30}
	}
	result, _, _ = play(t, "say go\nwalk ~0 ~40\n", thick, routine.Options{})
	fails(t, result, 2, "stuck at")
}

func TestPadThatDoesNotReachTheGame(t *testing.T) {
	dead := func(w *sim.Sim) { w.PadDead = true }
	result, _, _ := play(t, "calibrate move\n", dead, routine.Options{})
	fails(t, result, 1, "the game does not read the pad")
	result, _, _ = play(t, "calibrate look\n", dead, routine.Options{})
	fails(t, result, 1, "did not turn the view")
}

func TestCalibrateLearnsAnInvertedCamera(t *testing.T) {
	text := "cmd npc hostile\nuntil hostile > 0\nact target hostile\ncalibrate look\nstick right 0.8 0.5 0.7\nface target tolerance 2\n"
	result, world, _ := play(t, text, func(w *sim.Sim) { w.InvertLook = true }, routine.Options{})
	passes(t, result)
	if world.Pitch < -0.08 || world.Pitch > 0.03 {
		t.Errorf("the view's pitch is %.2f rad, not on the target", world.Pitch)
	}
	// Without the calibration the pitch runs away from the target.
	result, _, _ = play(t, "cmd npc hostile\nuntil hostile > 0\nact target hostile\nstick right 0.8 0.5 0.7\nface target tolerance 2 timeout 4\n",
		func(w *sim.Sim) { w.InvertLook = true }, routine.Options{})
	fails(t, result, 5, "not facing it in time")
}

func TestFaceTurnsTheView(t *testing.T) {
	result, _, _ := play(t, "face ~-10 ~-10 tolerance 2\nface ~10 ~0 ~5 tolerance 2\n", nil, routine.Options{})
	passes(t, result)
}

func TestDriveByPad(t *testing.T) {
	text := `sim car 3 6
act mount
until veh == 1 and seat == driver timeout 5
optional act autodrive on
calibrate steer
drive ~30 ~80 speed 10
check speed < 0.6
check near ~30 ~80 8
act exit
until veh == 0 timeout 5
`
	result, _, _ := play(t, text, func(w *sim.Sim) { w.NoAuto = true }, routine.Options{})
	passes(t, result)
	if len(result.Warnings) != 1 || !strings.Contains(result.Warnings[0], "not available here") {
		t.Errorf("warnings: %v", result.Warnings)
	}
	// On foot, drive refuses.
	result, _, _ = play(t, "drive ~0 ~50\n", nil, routine.Options{})
	fails(t, result, 1, "not driving")
}

func TestActRefusedAndOptional(t *testing.T) {
	result, _, _ := play(t, "say first\nact fire 1\n", nil, routine.Options{})
	fails(t, result, 2, "no weapon in hand")
	result, world, _ := play(t, "optional act fire 1\nact equip weapon\nact fire 0.2\nuntil firing == 0 timeout 2\n", nil, routine.Options{})
	passes(t, result)
	if len(result.Warnings) != 1 {
		t.Errorf("warnings: %v", result.Warnings)
	}
	// Every action is numbered, and the numbers grow.
	var numbered []string
	for _, command := range world.Commands {
		if strings.HasPrefix(command, "pilot ") && strings.Contains(command, " fire") {
			numbered = append(numbered, command)
		}
	}
	if len(numbered) != 2 || numbered[0] == numbered[1] {
		t.Errorf("fire commands: %v", numbered)
	}
}

func TestBlocksReportTheInnerLine(t *testing.T) {
	text := `if veh == 1
  say never
else
  repeat 2
    act equip weapon
    act takedown grab
  end
end
`
	result, _, _ := play(t, text, nil, routine.Options{})
	fails(t, result, 6, "no npc target")
}

func TestGameStopsAnswering(t *testing.T) {
	// The state freezes while V walks: the stick is let go at once, and the
	// run fails once the stall limit is over.
	var world *sim.Sim
	released := false
	prepare := func(w *sim.Sim) {
		world = w
		w.After(2*time.Second, func() { w.Frozen = true })
		w.After(5*time.Second, func() { released = w.AtRest() })
	}
	result, _, _ := play(t, "walk ~0 ~200 timeout 300\n", prepare, routine.Options{StallLimit: 20 * time.Second})
	fails(t, result, 1, "no pilot state for")
	if !released {
		t.Errorf("3 s into the freeze the stick was still held")
	}
	_ = world
}

func TestGameComesBackAfterALoadingScreen(t *testing.T) {
	prepare := func(w *sim.Sim) {
		w.After(2*time.Second, func() { w.Frozen = true })
		// The plugin's pilot lost its heartbeat meanwhile: it comes back off.
		w.After(10*time.Second, func() {
			w.Frozen = false
			_ = w.Command("pilot off")
		})
	}
	result, world, _ := play(t, "walk ~0 ~30 timeout 60\ncheck near ~0 ~30 1\n", prepare, routine.Options{})
	passes(t, result)
	on := 0
	for _, command := range world.Commands {
		if command == "pilot on" {
			on++
		}
	}
	if on < 2 {
		t.Errorf("pilot on was sent %d time(s): not again after the pilot went off", on)
	}
}

func TestLimitAndStop(t *testing.T) {
	result, _, _ := play(t, "limit 5\nwait 3\nwait 3\n", nil, routine.Options{})
	fails(t, result, 3, "limit of 5 s")

	stop := make(chan struct{})
	prepare := func(w *sim.Sim) { w.After(time.Second, func() { close(stop) }) }
	result, _, _ = play(t, "trigger rt 1\nstick left 0 1\nwait 30\n", prepare, routine.Options{Stop: stop})
	fails(t, result, 3, "stopped")
}

func TestGuardEndsTheRun(t *testing.T) {
	calls := 0
	guard := func() error {
		if calls++; calls > 3 {
			return os.ErrPermission
		}
		return nil
	}
	result, _, _ := play(t, "stick left 0 1\nwait 30\n", nil, routine.Options{Guard: guard})
	fails(t, result, 2, "guard:")
}

func TestHeartbeatKeepsThePilotOn(t *testing.T) {
	result, world, _ := play(t, "wait 20\ncheck pilot == 1\n", nil, routine.Options{})
	passes(t, result)
	beats := 0
	for _, command := range world.Commands {
		if command == "pilot beat" {
			beats++
		}
	}
	if beats < 18 || beats > 22 {
		t.Errorf("%d heartbeats in 20 s", beats)
	}
}

func TestExpectRejectMark(t *testing.T) {
	text := `cmd teleport home
expect teleport: \(-915
reject test reset
mark
cmd test reset
`
	result, _, _ := play(t, text+"expect teleport: timeout 2\n", nil, routine.Options{})
	fails(t, result, 6, "no game log line within 2 s")
	// (test reset turns the pilot off: the runner turns it on again.)
	result, _, _ = play(t, text+"expect test reset: ready timeout 2\nreject teleport:\n", nil, routine.Options{})
	passes(t, result)
	result, _, _ = play(t, "cmd teleport home\nwait 0.2\nreject teleport:\n", nil, routine.Options{})
	fails(t, result, 3, "the game logged")
}

func TestChoose(t *testing.T) {
	result, world, _ := play(t, "choose 3\nexpect story: choice 3 picked timeout 1\n", func(w *sim.Sim) { w.SetChoices(4) }, routine.Options{})
	passes(t, result)
	_ = world
	// The choices come later; a choice that is not offered times out.
	late := func(w *sim.Sim) { w.After(3*time.Second, func() { w.SetChoices(2) }) }
	result, _, _ = play(t, "choose 2\n", late, routine.Options{})
	passes(t, result)
	result, _, _ = play(t, "choose 3 timeout 4\n", late, routine.Options{})
	fails(t, result, 1, "not chosen within 4 s")
	// A pad the game does not read: the selection never moves.
	result, _, _ = play(t, "choose 2 timeout 30\n", func(w *sim.Sim) { w.SetChoices(2); w.PadDead = true }, routine.Options{})
	fails(t, result, 1, "does not follow the d-pad")
}

func TestNeeds(t *testing.T) {
	result, _, _ := play(t, "needs session == 1\nsay hello\n", nil, routine.Options{})
	fails(t, result, 0, "needs session == 1")
	result, _, _ = play(t, "sim session\nneeds session == 1\nsay hello\n", nil, routine.Options{})
	passes(t, result)
}

func TestNoPilotState(t *testing.T) {
	// A game that never writes a state (no tandem, not in a save).
	result, _, _ := play(t, "say hello\n", func(w *sim.Sim) { w.Frozen = true }, routine.Options{})
	fails(t, result, 0, "no pilot state from the plugin")
}

// Every routine we ship parses and passes on the simulator: its own logic
// holds together (what the real game does is another matter).
func TestShippedRoutines(t *testing.T) {
	paths, err := filepath.Glob("../../routines/*.pilot")
	if err != nil || len(paths) < 10 {
		t.Fatalf("routines: %v, %d found", err, len(paths))
	}
	for _, path := range paths {
		t.Run(filepath.Base(path), func(t *testing.T) {
			text, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			result, _, _ := play(t, string(text), nil, routine.Options{})
			passes(t, result)
		})
	}
	// The drive without AutoDrive: the pad's way.
	text, _ := os.ReadFile("../../routines/drive.pilot")
	result, _, _ := play(t, string(text), func(w *sim.Sim) { w.NoAuto = true }, routine.Options{})
	passes(t, result)
}
