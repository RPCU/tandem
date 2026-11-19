package guard

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func environ(t *testing.T, procDir string, pid int, vars ...string) {
	t.Helper()
	data := strings.Join(vars, "\x00") + "\x00"
	if err := os.WriteFile(filepath.Join(procDir, strconv.Itoa(pid), "environ"), []byte(data), 0o644); err != nil {
		t.Fatal(err)
	}
}

func newInstance(t *testing.T, name, extra string) string {
	t.Helper()
	dir := filepath.Join(t.TempDir(), name)
	if err := os.MkdirAll(filepath.Join(dir, "game"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, ".tandem-instance"), []byte("name = "+name+"\n"+extra), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestFindInstance(t *testing.T) {
	dir := newInstance(t, "b", "pad = 0x02d1\n")
	inst, err := FindInstance(filepath.Join(dir, "game"))
	if err != nil || inst == nil {
		t.Fatalf("%v %v", inst, err)
	}
	if inst.Name != "b" || inst.Pad != 0x02d1 || inst.Dir != dir {
		t.Fatalf("%+v", inst)
	}
	if !strings.HasPrefix(inst.Saves(), filepath.Join(dir, "compatdata", "pfx")) {
		t.Fatalf("saves %s", inst.Saves())
	}
	// The Steam install is no instance.
	if inst, err := FindInstance(gameDir); inst != nil || err != nil {
		t.Fatalf("steam: %v %v", inst, err)
	}
}

func TestCheckInstance(t *testing.T) {
	dir := newInstance(t, "b", "")
	inst, _ := FindInstance(filepath.Join(dir, "game"))
	p := proc(t, map[int][2]string{
		10: {gameLine(gameDir), stat(10, "Cyberpunk2077.e", 100)},                    // the player's
		20: {gameLine(filepath.Join(dir, "game")), stat(20, "Cyberpunk2077.e", 200)}, // the instance's
	})
	environ(t, p, 10, "HOME=/home/x")
	environ(t, p, 20, InstanceMark+"="+dir, "DISPLAY=:2")
	game, err := CheckInstance(p, inst)
	if err != nil || game.PID != 20 {
		t.Fatalf("%v %v", game, err)
	}
	if GameDisplay(p, game.PID) != ":2" {
		t.Fatal("the instance's display")
	}
	// Unmarked (or marked for another instance): not ours.
	environ(t, p, 20, InstanceMark+"="+dir+"x")
	if _, err := CheckInstance(p, inst); err == nil || !strings.Contains(err.Error(), "someone else") {
		t.Fatalf("a foreign mark: %v", err)
	}
	// Not running: the player's game is never taken for it.
	p2 := proc(t, map[int][2]string{10: {gameLine(gameDir), stat(10, "Cyberpunk2077.e", 100)}})
	environ(t, p2, 10, "HOME=/home/x")
	if _, err := CheckInstance(p2, inst); err == nil {
		t.Fatal("the player's game must not pass for the instance")
	}
}

func TestPadReachesOnly(t *testing.T) {
	p := proc(t, map[int][2]string{
		20: {gameLine("/i/b/game"), stat(20, "Cyberpunk2077.e", 200)},
		30: {gameLine("/i/c/game"), stat(30, "Cyberpunk2077.e", 300)},
	})
	self := Process{PID: 20, Start: 200}
	environ(t, p, 20, PadHint+"=0x045e/0x02d1")
	environ(t, p, 30, PadHint+"=0x045e/0x02dd")
	if err := PadReachesOnly(p, self, 0x045e, 0x02d1); err != nil {
		t.Fatal(err)
	}
	// Ours not hinted: it would read every pad, the player's real one too.
	if err := PadReachesOnly(p, self, 0x045e, 0x02dd); err == nil {
		t.Fatal("a pad our game is not kept to")
	}
	// Another game without the hint (the player's) would read it.
	environ(t, p, 30, "HOME=/home/x")
	if err := PadReachesOnly(p, self, 0x045e, 0x02d1); err == nil || !strings.Contains(err.Error(), "pid 30") {
		t.Fatalf("an unhinted game: %v", err)
	}
	// Or one that names the same pad.
	environ(t, p, 30, PadHint+"=0x045e/0x02D1")
	if err := PadReachesOnly(p, self, 0x045e, 0x02d1); err == nil {
		t.Fatal("two games on one pad")
	}
}

func TestInstanceDisplay(t *testing.T) {
	dir := newInstance(t, "b", "")
	inst, _ := FindInstance(filepath.Join(dir, "game"))
	if inst.Display() != "" {
		t.Fatal("no launch log: no display")
	}
	_ = os.MkdirAll(filepath.Join(dir, "run"), 0o755)
	log := "[gamescope] [Info]  xwm: Starting Xwayland on :1\nwine: ...\n[gamescope] [Info]  xwm: Starting Xwayland on :3 (second start)\n"
	_ = os.WriteFile(filepath.Join(dir, "run", "launch.log"), []byte(log), 0o644)
	if got := inst.Display(); got != ":3" {
		t.Fatalf("display %q", got)
	}
}
