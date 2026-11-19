package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/Banh-Canh/tandem/tools/pilot/internal/guard"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/link"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/pad"
	"github.com/Banh-Canh/tandem/tools/pilot/internal/sim"
)

// fakeGame is a game folder with the simulator behind its files, in real
// time: it takes tandem.cmd, writes pilot.state and appends to tandem's
// log, as the plugin does. With a fake /proc and an owner file, the whole
// "run" path plays on it: the files, the guards, the save check.
type fakeGame struct {
	dir   string
	proc  string
	cache string
	world *sim.Sim
	mu    sync.Mutex
	stop  chan struct{}
	done  chan struct{}
}

func newFakeGame(t *testing.T) *fakeGame {
	t.Helper()
	root := t.TempDir()
	g := &fakeGame{
		dir:   filepath.Join(root, "steamapps", "common", "Cyberpunk 2077"),
		proc:  filepath.Join(root, "proc"),
		cache: filepath.Join(root, "cache"),
		world: sim.New(),
		stop:  make(chan struct{}),
		done:  make(chan struct{}),
	}
	plugin := link.PluginDir(g.dir)
	logs := filepath.Join(g.dir, "red4ext", "logs")
	for _, dir := range []string{plugin, logs, filepath.Join(g.proc, "4242"), filepath.Join(g.cache, "main"), g.saves()} {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	logPath := filepath.Join(logs, "tandem-2026-11-19-09-00-00.log")
	_ = os.WriteFile(logPath, []byte("[info] playing\n"), 0o644)
	_ = os.WriteFile(filepath.Join(g.saves(), "sav.dat"), []byte("the player's save"), 0o644)
	cmdline := `Z:` + strings.ReplaceAll(g.dir, "/", `\`) + `\bin\x64\Cyberpunk2077.exe` + "\x00"
	_ = os.WriteFile(filepath.Join(g.proc, "4242", "cmdline"), []byte(cmdline), 0o644)
	_ = os.WriteFile(filepath.Join(g.proc, "4242", "stat"), []byte("4242 (Cyberpunk2077.e) S 1 2 3 0 -1 0 0 0 0 0 0 0 0 0 20 0 30 0 5555 1 2"), 0o644)
	g.own("4242 5555")

	go func() {
		defer close(g.done)
		log, _ := os.OpenFile(logPath, os.O_APPEND|os.O_WRONLY, 0)
		defer log.Close()
		cmd := filepath.Join(plugin, "tandem.cmd")
		state := filepath.Join(plugin, "pilot.state")
		for {
			select {
			case <-g.stop:
				return
			case <-time.After(10 * time.Millisecond):
			}
			g.mu.Lock()
			if text, err := os.ReadFile(cmd); err == nil {
				_ = os.Remove(cmd)
				_ = g.world.Command(strings.TrimSpace(string(text)))
			}
			g.world.Sleep(10 * time.Millisecond)
			for _, line := range g.world.LogLines() {
				fmt.Fprintln(log, line)
			}
			if st, ok := g.world.State(); ok {
				_ = os.WriteFile(state+".tmp", []byte(st.Line("seq")+"\n"), 0o644)
				_ = os.Rename(state+".tmp", state)
			} else {
				_ = os.Remove(state)
			}
			g.mu.Unlock()
		}
	}()
	t.Cleanup(func() {
		close(g.stop)
		<-g.done
	})
	return g
}

// lockedPad hands pad events to the fake game's simulator.
type lockedPad struct{ g *fakeGame }

func (p lockedPad) Button(b pad.Button, down bool) error {
	p.g.mu.Lock()
	defer p.g.mu.Unlock()
	return p.g.world.Button(b, down)
}

func (p lockedPad) Stick(s pad.Stick, x, y float64) error {
	p.g.mu.Lock()
	defer p.g.mu.Unlock()
	return p.g.world.Stick(s, x, y)
}

func (p lockedPad) Trigger(tr pad.Trigger, value float64) error {
	p.g.mu.Lock()
	defer p.g.mu.Unlock()
	return p.g.world.Trigger(tr, value)
}

func (lockedPad) Close() error { return nil }

func (g *fakeGame) saves() string { return filepath.Join(filepath.Dir(g.dir), "saves") }

func (g *fakeGame) own(text string) {
	_ = os.WriteFile(filepath.Join(g.cache, "main", "game.owner"), []byte(text+"\n"), 0o644)
}

func (g *fakeGame) flags() *flags {
	return &flags{
		gameDir: g.dir, instance: "main", cache: g.cache, padKind: "dry", focus: "off",
		saves: g.saves(), proc: g.proc, idle: 900,
	}
}

func (g *fakeGame) routine(t *testing.T, text string) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "test.pilot")
	if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestPlayOnAGameFolder(t *testing.T) {
	g := newFakeGame(t)
	var out strings.Builder
	text := "show\nwalk ~0 ~2 within 0.8\ncmd teleport home\nexpect teleport: timeout 5\nact equip weapon\ncheck weapon == 1\ncheck savelock == 1\n"
	// The pad is the simulator's own, behind the game's lock.
	guarded := pad.NewGuarded(lockedPad{g}, 500*time.Millisecond, time.Now(), nil)
	code, err := play(g.flags(), g.routine(t, text), guarded, &out, nil)
	t.Log(out.String())
	if err != nil || code != 0 {
		t.Fatalf("play: %d %v", code, err)
	}
	for _, want := range []string{"state: seq=", "ok: walk ~0 ~2", "act equip weapon -> asked", "PASS: 7 steps"} {
		if !strings.Contains(out.String(), want) {
			t.Errorf("the log lacks %q", want)
		}
	}
	// The last command, "pilot off", is taken by the game's next tick.
	on := true
	for i := 0; i < 100 && on; i++ {
		time.Sleep(10 * time.Millisecond)
		g.mu.Lock()
		on = g.world.PilotOn()
		g.mu.Unlock()
	}
	g.mu.Lock()
	defer g.mu.Unlock()
	if !g.world.AtRest() {
		t.Error("the pad is not at rest")
	}
	if on {
		t.Error("the plugin's pilot is still on")
	}
}

func TestPlayRefusesAGameThatIsNotOurs(t *testing.T) {
	g := newFakeGame(t)
	path := g.routine(t, "wait 1\n")
	var out strings.Builder
	// The player's game: another process than the one our tools started.
	g.own("4242 1111")
	if code, err := play(g.flags(), path, nil, &out, nil); err == nil || code == 0 || !strings.Contains(err.Error(), "someone else started") {
		t.Errorf("another game: %d %v", code, err)
	}
	_ = os.Remove(filepath.Join(g.cache, "main", "game.owner"))
	if code, err := play(g.flags(), path, nil, &out, nil); err == nil || code == 0 || !strings.Contains(err.Error(), "not started by tools/game.sh") {
		t.Errorf("no owner file: %d %v", code, err)
	}
	g.mu.Lock()
	sent := len(g.world.Commands)
	g.mu.Unlock()
	if sent != 0 || out.Len() != 0 {
		t.Errorf("a refused run sent %d commands and logged %q", sent, out.String())
	}
	// The game goes away mid-run (the player took over): the run ends.
	g.own("4242 5555")
	go func() {
		time.Sleep(1500 * time.Millisecond)
		g.own("4242 9999")
	}()
	code, err := play(g.flags(), g.routine(t, "wait 20\n"), nil, &out, nil)
	if err != nil || code != 1 || !strings.Contains(out.String(), "FAIL at line 1: guard:") {
		t.Errorf("owner lost mid-run: %d %v\n%s", code, err, out.String())
	}
}

func TestPlayFailsWhenASaveIsWritten(t *testing.T) {
	g := newFakeGame(t)
	go func() {
		time.Sleep(500 * time.Millisecond)
		_ = os.MkdirAll(filepath.Join(g.saves(), "AutoSave-7"), 0o755)
		_ = os.WriteFile(filepath.Join(g.saves(), "AutoSave-7", "sav.dat"), []byte("a test's world"), 0o644)
	}()
	var out strings.Builder
	code, err := play(g.flags(), g.routine(t, "wait 1.5\n"), nil, &out, nil)
	if err != nil || code != 1 || !strings.Contains(out.String(), "SAVE new: AutoSave-7/sav.dat") || !strings.Contains(out.String(), "the save lock did not hold") {
		t.Errorf("a save written during the run: %d %v\n%s", code, err, out.String())
	}
}

// A game folder of tools/instance.sh is driven as that instance: its name,
// pad and saves, keys on its own display, never on the desktop's.
func TestInstanceFlags(t *testing.T) {
	inst := filepath.Join(t.TempDir(), "b")
	if err := os.MkdirAll(filepath.Join(inst, "game"), 0o755); err != nil {
		t.Fatal(err)
	}
	_ = os.WriteFile(filepath.Join(inst, ".tandem-instance"), []byte("name = b\npad = 0x02d1\n"), 0o644)
	f, _, err := parseFlags([]string{"--game-dir", filepath.Join(inst, "game")})
	if err != nil {
		t.Fatal(err)
	}
	if f.instance != "b" || f.padKind != "keys" || f.focus != "display" || !strings.HasPrefix(f.saves, filepath.Join(inst, "compatdata")) {
		t.Fatalf("%+v", f)
	}
	if id, err := f.identity(); err != nil || id.Product != 0x02d1 {
		t.Fatalf("pad %v %v", id, err)
	}
	// The Steam install keeps the pad and the sway focus.
	g, _, err := parseFlags([]string{"--game-dir", "/data/Steam/steamapps/common/Cyberpunk 2077"})
	if err != nil || g.padKind != "uinput" || g.focus != "require" || g.instance != "main" {
		t.Fatalf("%+v %v", g, err)
	}

	// The game's display must be its own.
	proc := filepath.Join(t.TempDir(), "proc")
	_ = os.MkdirAll(filepath.Join(proc, "7"), 0o755)
	_ = os.WriteFile(filepath.Join(proc, "7", "environ"), []byte("DISPLAY=:0\x00"), 0o644)
	f.proc = proc
	t.Setenv("DISPLAY", ":0")
	if _, err := f.gameDisplay(guardProcess(7)); err == nil || !strings.Contains(err.Error(), "desktop") {
		t.Fatalf("the desktop's display: %v", err)
	}
	t.Setenv("DISPLAY", ":1")
	if d, err := f.gameDisplay(guardProcess(7)); err != nil || d != ":0" {
		t.Fatalf("its own display: %q %v", d, err)
	}
	if _, _, err := parseFlags([]string{"--keys", "lb=nothing"}); err == nil {
		t.Fatal("a bad keymap must be refused")
	}
}

func guardProcess(pid int) guard.Process { return guard.Process{PID: pid} }
