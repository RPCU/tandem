// tandem-pilot plays the real game's V without a human, for tests: it runs
// on the game machine next to the game, reads the pilot state the plugin
// writes, sends tandem its commands and holds a virtual gamepad.
// tools/game.sh pilot starts and stops it; docs/architecture/autopilot.md
// has the design and the routine language.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"syscall"
	"time"

	"github.com/Banh-Canh/tandem/tools/pilot/internal/guard"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/link"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/pad"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/routine"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/sim"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/spool"
)

const usage = `tandem-pilot: play the game's V for a test

  tandem-pilot check ROUTINE...        parse routines, say what is wrong
  tandem-pilot dry ROUTINE...          play routines on the built-in simulator,
                                       printing the pad events and the
                                       commands the game would get (no game,
                                       no device)
  tandem-pilot run [FLAGS] ROUTINE     play a routine on the running game (by
                                       the serving pilot of the instance when
                                       there is one: the flags are then its)
  tandem-pilot serve [FLAGS]           hold the pad and play the routines "run"
                                       hands over, until stopped or idle: for
                                       a game that must see the pad at launch
  tandem-pilot owner [FLAGS]           is the running game one our tools started?
  tandem-pilot state [FLAGS]           print the plugin's pilot state
  tandem-pilot pad [FLAGS] [SECONDS]   create the virtual pad and hold it at
                                       rest (10 s): to see who picks it up

Flags of run, serve, owner, state, pad:
`

type flags struct {
	gameDir    string
	instance   string
	cache      string
	uinput     string
	padKind    string
	focus      string
	focusClass string
	saves      string
	product    string
	idle       float64
	proc       string
}

func parseFlags(args []string) (*flags, []string, error) {
	f := &flags{}
	set := flag.NewFlagSet("tandem-pilot", flag.ContinueOnError)
	home, _ := os.UserHomeDir()
	set.StringVar(&f.gameDir, "game-dir", "/data/Steam/steamapps/common/Cyberpunk 2077", "the game's folder")
	set.StringVar(&f.instance, "instance", "main", "the game instance this pilot drives (its own lock, owner file and pad)")
	set.StringVar(&f.cache, "cache", filepath.Join(home, ".cache", "tandem", "pilot"), "where the lock and owner files live")
	set.StringVar(&f.uinput, "uinput", "/dev/uinput", "the uinput device")
	set.StringVar(&f.padKind, "pad", "uinput", "uinput: a virtual pad; none: script-level actions only; dry: log the events")
	set.StringVar(&f.focus, "focus", "require", "require: the game's window must have the focus (it is asked for once); off: not checked")
	set.StringVar(&f.focusClass, "focus-class", "steam_app_1091500", "the game window's X11 class")
	set.StringVar(&f.saves, "saves", "", "the save folder to watch (default: the game's Proton prefix)")
	set.StringVar(&f.product, "product", "", "the pad's USB product id (default 0x028e; another per instance)")
	set.StringVar(&f.proc, "proc", "/proc", "where processes are listed (tests)")
	set.Float64Var(&f.idle, "idle", 900, "serve: seconds without a routine after which it ends")
	set.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		set.PrintDefaults()
	}
	if err := set.Parse(args); err != nil {
		return nil, nil, err
	}
	if f.saves == "" {
		f.saves = guard.DefaultSaves(f.gameDir)
	}
	if f.focus != "require" && f.focus != "off" {
		return nil, nil, errors.New("--focus require|off")
	}
	if f.padKind != "uinput" && f.padKind != "none" && f.padKind != "dry" {
		return nil, nil, errors.New("--pad uinput|none|dry")
	}
	return f, set.Args(), nil
}

func (f *flags) dir() string       { return filepath.Join(f.cache, f.instance) }
func (f *flags) ownerFile() string { return filepath.Join(f.dir(), "game.owner") }

func (f *flags) identity() (pad.Identity, error) {
	id := pad.Xbox360()
	if f.product != "" {
		product, err := strconv.ParseUint(f.product, 0, 16)
		if err != nil {
			return id, fmt.Errorf("--product: %w", err)
		}
		id.Product = uint16(product)
	}
	return id, nil
}

// device makes the pad this run asked for.
func (f *flags) device(out io.Writer) (pad.Device, error) {
	switch f.padKind {
	case "none":
		return noPad{}, nil
	case "dry":
		return pad.NewDry(out), nil
	}
	id, err := f.identity()
	if err != nil {
		return nil, err
	}
	device, err := pad.OpenUinput(f.uinput, id)
	if err != nil {
		return nil, err
	}
	// The game (or Steam) needs a moment to pick a new pad up.
	time.Sleep(2 * time.Second)
	return device, nil
}

type realClock struct{}

func (realClock) Now() time.Time        { return time.Now() }
func (realClock) Sleep(d time.Duration) { time.Sleep(d) }

// noPad refuses every event: a run with script-level actions only.
type noPad struct{}

var errNoPad = errors.New("this run has no pad (--pad none)")

func (noPad) Button(pad.Button, bool) error           { return errNoPad }
func (noPad) Stick(pad.Stick, float64, float64) error { return errNoPad }
func (noPad) Trigger(pad.Trigger, float64) error      { return errNoPad }
func (noPad) Close() error                            { return nil }

func logTo(out io.Writer) func(string, ...any) {
	return func(format string, args ...any) {
		fmt.Fprintf(out, "pilot: "+format+"\n", args...)
	}
}

func load(path string) (*routine.Routine, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	r, err := routine.Parse(filepath.Base(path), file)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return r, nil
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprint(os.Stderr, usage)
		os.Exit(2)
	}
	var err error
	code := 0
	switch os.Args[1] {
	case "check":
		code, err = check(os.Args[2:])
	case "dry":
		code, err = dry(os.Args[2:])
	case "run":
		code, err = run(os.Args[2:])
	case "serve":
		code, err = serve(os.Args[2:])
	case "owner":
		code, err = owner(os.Args[2:])
	case "state":
		code, err = showState(os.Args[2:])
	case "pad":
		code, err = holdPad(os.Args[2:])
	default:
		fmt.Fprint(os.Stderr, usage)
		os.Exit(2)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "pilot:", err)
		if code == 0 {
			code = 1
		}
	}
	os.Exit(code)
}

func check(paths []string) (int, error) {
	if len(paths) == 0 {
		return 2, errors.New("check ROUTINE...")
	}
	bad := 0
	for _, path := range paths {
		r, err := load(path)
		if err != nil {
			fmt.Fprintln(os.Stderr, "pilot:", err)
			bad++
			continue
		}
		fmt.Printf("%s: ok, %d steps, limit %.0f s\n", path, len(r.Steps), r.Limit)
	}
	if bad > 0 {
		return 1, fmt.Errorf("%d routine(s) do not parse", bad)
	}
	return 0, nil
}

// dry plays routines on the simulator: what the pilot would send, and
// whether the routine's own logic holds together.
func dry(paths []string) (int, error) {
	if len(paths) == 0 {
		return 2, errors.New("dry ROUTINE...")
	}
	logf := logTo(os.Stdout)
	failed := 0
	for _, path := range paths {
		r, err := load(path)
		if err != nil {
			return 1, err
		}
		world := sim.New()
		world.Echo = os.Stdout
		guarded := pad.NewGuarded(world, 500*time.Millisecond, world.Now(), logf)
		logf("dry run of %s on the simulator", path)
		result := routine.Run(r, world, guarded, world, routine.Options{Logf: logf})
		logf("%s: %s", path, result)
		if !result.Passed {
			failed++
		}
	}
	if failed > 0 {
		return 1, nil
	}
	return 0, nil
}

func owner(args []string) (int, error) {
	f, _, err := parseFlags(args)
	if err != nil {
		return 2, err
	}
	game, err := guard.CheckOwner(f.proc, f.gameDir, f.ownerFile())
	if err != nil {
		return 1, err
	}
	fmt.Printf("the running game (pid %d) was started by our tools\n", game.PID)
	return 0, nil
}

func showState(args []string) (int, error) {
	f, _, err := parseFlags(args)
	if err != nil {
		return 2, err
	}
	game, err := link.Open(f.gameDir)
	if err != nil {
		return 1, err
	}
	st, ok := game.State()
	if !ok {
		return 1, errors.New("no pilot state (the plugin writes it while its pilot is on)")
	}
	fmt.Println(st.Line("seq", "t", "pilot", "ack", "res", "x", "y", "z"))
	return 0, nil
}

// signalled is closed by the first SIGINT, SIGTERM or SIGHUP; a second
// one, or 8 s without the process ending, runs last and exits.
func signalled(last func()) <-chan struct{} {
	stop := make(chan struct{})
	signals := make(chan os.Signal, 2)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM, syscall.SIGHUP)
	go func() {
		<-signals
		close(stop)
		select {
		case <-signals:
		case <-time.After(8 * time.Second):
		}
		last()
		os.Exit(130)
	}()
	return stop
}

// holdPad creates the pad and holds it at rest: nothing is pressed. It
// shows whether the device can be made and who picks it up.
func holdPad(args []string) (int, error) {
	f, rest, err := parseFlags(args)
	if err != nil {
		return 2, err
	}
	seconds := 10.0
	if len(rest) == 1 {
		if seconds, err = strconv.ParseFloat(rest[0], 64); err != nil || seconds <= 0 || seconds > 600 {
			return 2, errors.New("pad [SECONDS]")
		}
	}
	lock, err := guard.Lock(filepath.Join(f.dir(), "lock"))
	if err != nil {
		return 1, err
	}
	defer lock.Close()
	id, err := f.identity()
	if err != nil {
		return 2, err
	}
	device, err := pad.OpenUinput(f.uinput, id)
	if err != nil {
		return 1, err
	}
	defer device.Close()
	fmt.Printf("pilot: the pad %q (%04x:%04x) exists for %.0f s, at rest\n", id.Name, id.Vendor, id.Product, seconds)
	stop := signalled(func() { _ = device.Close() })
	select {
	case <-stop:
	case <-time.After(time.Duration(seconds * float64(time.Second))):
	}
	fmt.Println("pilot: the pad is gone")
	return 0, nil
}

// play runs one routine on the game with every guard: the game is the one
// our tools started, its window has the focus, the pad is released however
// the run ends, and no save was written meanwhile. shared is a pad that
// outlives the run (serve), nil to make one for it.
func play(f *flags, path string, shared *pad.Guarded, out io.Writer, stop <-chan struct{}) (int, error) {
	logf := logTo(out)
	r, err := load(path)
	if err != nil {
		return 2, err
	}
	process, err := guard.CheckOwner(f.proc, f.gameDir, f.ownerFile())
	if err != nil {
		return 1, err
	}
	game, err := link.Open(f.gameDir)
	if err != nil {
		return 1, err
	}
	game.ClearState()

	var focus *guard.Focus
	if f.focus == "require" {
		focus = guard.NewFocus(f.focusClass)
		if err := focus.Take(); err != nil {
			return 1, fmt.Errorf("the game's window: %w", err)
		}
	}
	saves, savesErr := guard.SnapshotSaves(f.saves)
	if savesErr != nil {
		logf("WARN the save folder %s cannot be read (%v): saves are not watched", f.saves, savesErr)
	}

	guarded := shared
	if guarded == nil {
		device, err := f.device(out)
		if err != nil {
			return 1, err
		}
		guarded = pad.NewGuarded(device, 500*time.Millisecond, time.Now(), logf)
		defer guarded.Close()
	}
	defer func() { _ = guarded.Neutral() }()

	// The dead-man releases the pad if the run hangs.
	done := make(chan struct{})
	defer close(done)
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-done:
				return
			case now := <-ticker.C:
				guarded.Check(now)
			}
		}
	}()

	logf("playing %s on the game (pid %d, instance %s, pad %s, log %s)", path, process.PID, f.instance, f.padKind, game.LogPath())
	guarded.Feed(time.Now())
	result := routine.Run(r, game, guarded, realClock{}, routine.Options{
		Logf: logf,
		Stop: stop,
		Guard: func() error {
			// Still the game we were allowed to drive, and still in front.
			if _, err := guard.CheckOwner(f.proc, f.gameDir, f.ownerFile()); err != nil {
				return err
			}
			if focus != nil {
				return focus.Check()
			}
			return nil
		},
	})
	_ = guarded.Neutral()
	if savesErr == nil {
		after, err := guard.SnapshotSaves(f.saves)
		if err != nil {
			logf("WARN the save folder cannot be read again: %v", err)
		} else if changed := saves.Diff(after); len(changed) > 0 {
			for _, line := range changed {
				logf("SAVE %s", line)
			}
			logf("%s: FAIL: the game wrote to its save folder during the run (%d files): the save lock did not hold", path, len(changed))
			return 1, nil
		}
	}
	logf("%s: %s", path, result)
	if !result.Passed {
		return 1, nil
	}
	return 0, nil
}

func run(args []string) (int, error) {
	f, rest, err := parseFlags(args)
	if err != nil {
		return 2, err
	}
	if len(rest) != 1 {
		return 2, errors.New("run [FLAGS] ROUTINE")
	}
	// A pilot serves this instance: it plays the routine, with its pad.
	if jobs := spool.Dir(f.dir()); jobs.Server() != 0 {
		if _, err := load(rest[0]); err != nil {
			return 2, err
		}
		path, err := filepath.Abs(rest[0])
		if err != nil {
			return 2, err
		}
		stop := signalled(func() {})
		return jobs.Submit(path, os.Stdout, stop)
	}
	// One pilot per instance.
	lock, err := guard.Lock(filepath.Join(f.dir(), "lock"))
	if err != nil {
		return 1, err
	}
	defer lock.Close()
	stop := signalled(func() {})
	return play(f, rest[0], nil, os.Stdout, stop)
}

// serve holds the pad and plays the routines handed to it, one at a time,
// until it is stopped or has had none for the idle time: it is never left
// running. Each routine has every guard of a single run.
func serve(args []string) (int, error) {
	f, rest, err := parseFlags(args)
	if err != nil {
		return 2, err
	}
	if len(rest) != 0 {
		return 2, errors.New("serve [FLAGS]")
	}
	lock, err := guard.Lock(filepath.Join(f.dir(), "lock"))
	if err != nil {
		return 1, err
	}
	defer lock.Close()
	logf := logTo(os.Stdout)
	device, err := f.device(os.Stdout)
	if err != nil {
		return 1, err
	}
	guarded := pad.NewGuarded(device, 500*time.Millisecond, time.Now(), logf)
	jobs := spool.Dir(f.dir())
	closeAll := func() {
		_ = guarded.Close()
		_ = jobs.Serving(false)
	}
	defer closeAll()
	if err := jobs.Serving(true); err != nil {
		return 1, err
	}
	stop := signalled(closeAll)
	logf("serving instance %s (pad %s): routines from %s; ends after %.0f s without one", f.instance, f.padKind, f.dir(), f.idle)
	last := time.Now()
	for {
		select {
		case <-stop:
			logf("stopped")
			return 0, nil
		default:
		}
		path, log, ok := jobs.Take()
		if !ok {
			// At rest between routines: nothing fed, nothing held.
			guarded.Check(time.Now())
			if time.Since(last).Seconds() > f.idle {
				logf("no routine for %.0f s: the pilot ends", f.idle)
				return 0, nil
			}
			time.Sleep(200 * time.Millisecond)
			continue
		}
		logf("routine %s", path)
		// The job stops when its client asks, or when we are stopped.
		jobStop := make(chan struct{})
		finished := make(chan struct{})
		go func() {
			defer close(jobStop)
			for {
				select {
				case <-stop:
					return
				case <-finished:
					return
				case <-time.After(100 * time.Millisecond):
					if jobs.Stopped() {
						return
					}
				}
			}
		}()
		code, err := play(f, path, guarded, log, jobStop)
		close(finished)
		if err != nil {
			fmt.Fprintln(log, "pilot:", err)
			if code == 0 {
				code = 1
			}
		}
		_ = log.Close()
		_ = jobs.Finish(code)
		logf("routine %s: exit %d", path, code)
		last = time.Now()
	}
}
