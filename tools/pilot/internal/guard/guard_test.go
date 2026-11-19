package guard

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestLock(t *testing.T) {
	path := filepath.Join(t.TempDir(), "main", "lock")
	first, err := Lock(path)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Lock(path); err == nil || !strings.Contains(err.Error(), fmt.Sprint(os.Getpid())) {
		t.Errorf("a second lock: %v", err)
	}
	// Another instance has its own.
	other, err := Lock(filepath.Join(filepath.Dir(filepath.Dir(path)), "second", "lock"))
	if err != nil {
		t.Error(err)
	} else {
		other.Close()
	}
	first.Close()
	again, err := Lock(path)
	if err != nil {
		t.Fatalf("after the first holder is gone: %v", err)
	}
	again.Close()
}

// proc builds a fake /proc with these processes.
func proc(t *testing.T, processes map[int][2]string) string {
	t.Helper()
	dir := t.TempDir()
	for pid, p := range processes {
		at := filepath.Join(dir, fmt.Sprint(pid))
		if err := os.MkdirAll(at, 0o755); err != nil {
			t.Fatal(err)
		}
		_ = os.WriteFile(filepath.Join(at, "cmdline"), []byte(p[0]), 0o644)
		_ = os.WriteFile(filepath.Join(at, "stat"), []byte(p[1]), 0o644)
	}
	_ = os.MkdirAll(filepath.Join(dir, "self"), 0o755)
	return dir
}

func stat(pid int, comm string, start uint64) string {
	return fmt.Sprintf("%d (%s) S 1 2 3 0 -1 4194560 100 0 0 0 10 20 0 0 20 0 30 0 %d 1000 2000", pid, comm, start)
}

const gameDir = "/data/Steam/steamapps/common/Cyberpunk 2077"

func gameLine(dir string) string {
	return `Z:` + strings.ReplaceAll(dir, "/", `\`) + `\bin\x64\Cyberpunk2077.exe` + "\x00-modded\x00"
}

func TestFindGameAndOwner(t *testing.T) {
	others := map[int][2]string{
		10: {"/bin/bash\x00/home/u/.steam/steam.sh\x00", stat(10, "steam.sh", 5)},
		// The launcher around the game names the exe too, but is not it.
		20: {"/usr/bin/python3\x00proton\x00waitforexitandrun\x00" + gameDir + "/bin/x64/Cyberpunk2077.exe\x00", stat(20, "python3", 6)},
		30: {`C:\windows\system32\services.exe` + "\x00", stat(30, "services.exe", 7)},
	}
	if _, err := FindGame(proc(t, others), gameDir); err == nil || !strings.Contains(err.Error(), "not running") {
		t.Errorf("no game: %v", err)
	}

	others[40] = [2]string{gameLine(gameDir), stat(40, "Cyberpunk (2077) e", 123456)}
	root := proc(t, others)
	game, err := FindGame(root, gameDir)
	if err != nil || game.PID != 40 || game.Start != 123456 {
		t.Fatalf("game: %+v %v", game, err)
	}

	owner := filepath.Join(t.TempDir(), "game.owner")
	if _, err := CheckOwner(root, gameDir, owner); err == nil || !strings.Contains(err.Error(), "not started by tools/game.sh") {
		t.Errorf("no owner file: %v", err)
	}
	// The pid of a game our tools started earlier, since replaced by one the
	// player launched: the same pid would not have the same start time.
	_ = os.WriteFile(owner, []byte("40 999\n"), 0o644)
	if _, err := CheckOwner(root, gameDir, owner); err == nil || !strings.Contains(err.Error(), "not the one tools/game.sh started") {
		t.Errorf("another game: %v", err)
	}
	_ = os.WriteFile(owner, []byte("40 123456\n"), 0o644)
	if _, err := CheckOwner(root, gameDir, owner); err != nil {
		t.Errorf("our game: %v", err)
	}

	// Two instances: each pilot finds its own, by folder.
	second := "/data/Steam2/steamapps/common/Cyberpunk 2077"
	others[50] = [2]string{gameLine(second), stat(50, "Cyberpunk2077.e", 777)}
	root = proc(t, others)
	if game, err := FindGame(root, second); err != nil || game.PID != 50 {
		t.Errorf("second instance: %+v %v", game, err)
	}
	if game, err := FindGame(root, gameDir); err != nil || game.PID != 40 {
		t.Errorf("first instance: %+v %v", game, err)
	}
	if _, err := FindGame(root, "/elsewhere/Cyberpunk 2077"); err == nil || !strings.Contains(err.Error(), "none from") {
		t.Errorf("a third folder: %v", err)
	}
}

const tree = `{"name":"root","focused":false,"nodes":[{"name":"HEADLESS-1","nodes":[{"name":"1","nodes":[
 {"name":"Steam Big Picture Mode","focused":%v,"window_properties":{"class":"steam"}},
 {"name":"Cyberpunk 2077 (C) 2020 by CD Projekt RED","focused":%v,"app_id":null,"shell":"xwayland","window_properties":{"class":"steam_app_1091500","instance":"steam_app_1091500"}}],
 "floating_nodes":[{"name":"term","focused":%v,"app_id":"foot"}]}]}]}`

func TestFocus(t *testing.T) {
	class, err := FocusedClass([]byte(fmt.Sprintf(tree, false, true, false)))
	if err != nil || class != "steam_app_1091500" {
		t.Errorf("game focused: %q %v", class, err)
	}
	class, err = FocusedClass([]byte(fmt.Sprintf(tree, false, false, true)))
	if err != nil || class != "foot" {
		t.Errorf("a wayland window focused: %q %v", class, err)
	}
	if _, err := FocusedClass([]byte(fmt.Sprintf(tree, false, false, false))); err == nil {
		t.Error("nothing focused: no error")
	}

	// Take asks sway for the focus once, then checks again.
	focusedGame := false
	var asked []string
	focus := &Focus{Class: "steam_app_1091500", Run: func(args ...string) ([]byte, error) {
		if args[0] == "-t" {
			return []byte(fmt.Sprintf(tree, !focusedGame, focusedGame, false)), nil
		}
		asked = append(asked, args[0])
		focusedGame = true
		return nil, nil
	}}
	if err := focus.Check(); err == nil || !strings.Contains(err.Error(), "the focus is on steam") {
		t.Errorf("check: %v", err)
	}
	if err := focus.Take(); err != nil || len(asked) != 1 || asked[0] != `[class="^steam_app_1091500$"] focus` {
		t.Errorf("take: %v, asked %q", err, asked)
	}
	// A sway that does not give it.
	stubborn := &Focus{Class: "steam_app_1091500", Run: func(args ...string) ([]byte, error) {
		if args[0] == "-t" {
			return []byte(fmt.Sprintf(tree, true, false, false)), nil
		}
		return nil, nil
	}}
	if err := stubborn.Take(); err == nil {
		t.Error("take without the focus: no error")
	}
	broken := &Focus{Class: "x", Run: func(...string) ([]byte, error) { return nil, errors.New("no socket") }}
	if err := broken.Check(); err == nil || !strings.Contains(err.Error(), "SWAYSOCK") {
		t.Errorf("no sway: %v", err)
	}
}

func TestSaves(t *testing.T) {
	dir := t.TempDir()
	write := func(name, text string) {
		path := filepath.Join(dir, name)
		_ = os.MkdirAll(filepath.Dir(path), 0o755)
		if err := os.WriteFile(path, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("ManualSave-12/sav.dat", "aaaa")
	write("AutoSave-3/sav.dat", "bbbb")
	write("AutoSave-3/metadata.9.json", "{}")
	before, err := SnapshotSaves(dir)
	if err != nil || len(before) != 3 {
		t.Fatalf("snapshot: %v %v", before, err)
	}
	same, _ := SnapshotSaves(dir)
	if changed := before.Diff(same); len(changed) != 0 {
		t.Errorf("nothing changed: %v", changed)
	}
	// An autosave: a new folder, and the oldest one rotated out.
	write("AutoSave-4/sav.dat", "cccc")
	_ = os.RemoveAll(filepath.Join(dir, "AutoSave-3"))
	write("ManualSave-12/sav.dat", "aaaaa")
	after, _ := SnapshotSaves(dir)
	got := strings.Join(before.Diff(after), "; ")
	want := "changed: ManualSave-12/sav.dat; new: AutoSave-4/sav.dat; removed: AutoSave-3/metadata.9.json; removed: AutoSave-3/sav.dat"
	if got != want {
		t.Errorf("diff: %s", got)
	}
	if _, err := SnapshotSaves(filepath.Join(dir, "missing")); err == nil {
		t.Error("a missing folder: no error")
	}
	if got := DefaultSaves(gameDir); got != "/data/Steam/steamapps/compatdata/1091500/pfx/drive_c/users/steamuser/Saved Games/CD Projekt Red/Cyberpunk 2077" {
		t.Errorf("default saves: %s", got)
	}
}
