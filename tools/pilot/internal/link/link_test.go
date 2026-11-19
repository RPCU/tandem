package link

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func game(t *testing.T) (string, *Files) {
	t.Helper()
	dir := t.TempDir()
	if _, err := Open(dir); err == nil || !strings.Contains(err.Error(), "not installed") {
		t.Fatalf("no tandem: %v", err)
	}
	_ = os.MkdirAll(PluginDir(dir), 0o755)
	if _, err := Open(dir); err == nil || !strings.Contains(err.Error(), "no tandem log") {
		t.Fatalf("no log: %v", err)
	}
	logs := filepath.Join(dir, "red4ext", "logs")
	_ = os.MkdirAll(logs, 0o755)
	_ = os.WriteFile(filepath.Join(logs, "tandem-2026-11-18-10-00-00.log"), []byte("an older game\n"), 0o644)
	old := time.Now().Add(-time.Hour)
	_ = os.Chtimes(filepath.Join(logs, "tandem-2026-11-18-10-00-00.log"), old, old)
	_ = os.WriteFile(filepath.Join(logs, "tandem-2026-11-19-09-00-00.log"), []byte("[info] before the pilot\n"), 0o644)
	files, err := Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	return dir, files
}

func TestCommand(t *testing.T) {
	dir, files := game(t)
	cmd := filepath.Join(PluginDir(dir), "tandem.cmd")
	if err := files.Command("pilot 3 fire 0.5"); err != nil {
		t.Fatal(err)
	}
	if text, _ := os.ReadFile(cmd); string(text) != "pilot 3 fire 0.5\n" {
		t.Errorf("command file: %q", text)
	}
	// The slot is taken: a heartbeat and a "pilot on" are dropped at once...
	began := time.Now()
	if err := files.Command("pilot beat"); err != nil {
		t.Error(err)
	}
	if err := files.Command("pilot on"); err != nil {
		t.Error(err)
	}
	if text, _ := os.ReadFile(cmd); string(text) != "pilot 3 fire 0.5\n" || time.Since(began) > 200*time.Millisecond {
		t.Errorf("a heartbeat replaced or waited for a command: %q", text)
	}
	// ... a command waits until the game took the one before,
	go func() {
		time.Sleep(60 * time.Millisecond)
		_ = os.Remove(cmd)
	}()
	if err := files.Command("teleport home"); err != nil {
		t.Fatal(err)
	}
	if text, _ := os.ReadFile(cmd); string(text) != "teleport home\n" {
		t.Errorf("command file: %q", text)
	}
	// ... and gives up when it never does.
	files.TakeTimeout = 80 * time.Millisecond
	if err := files.Command("where"); err == nil || !strings.Contains(err.Error(), "did not take") {
		t.Errorf("a game that takes nothing: %v", err)
	}
	if err := files.Command("two\nlines"); err == nil {
		t.Error("two lines passed")
	}
	if left, _ := filepath.Glob(filepath.Join(PluginDir(dir), "*.pilot")); len(left) != 0 {
		t.Errorf("temporary files left: %v", left)
	}
}

func TestState(t *testing.T) {
	dir, files := game(t)
	path := filepath.Join(PluginDir(dir), "pilot.state")
	if _, ok := files.State(); ok {
		t.Error("a state without a file")
	}
	_ = os.WriteFile(path, []byte("seq=7 t=12.50 pilot=1 x=-915.00 y=1879.50 z=36.19 seat=none\n"), 0o644)
	st, ok := files.State()
	if !ok || st.Seq() != 7 || st.N("y") != 1879.5 || st.Str("seat") != "none" {
		t.Errorf("state: %v %v", st, ok)
	}
	// A torn or foreign file: the last good state stands.
	_ = os.WriteFile(path, []byte("seq=8 t=12.5"+"\x00garbage here\n"), 0o644)
	if st, ok := files.State(); !ok || st.Seq() != 7 {
		t.Errorf("after a bad file: %v %v", st, ok)
	}
	_ = os.WriteFile(path, []byte("seq=9 x=1 y=2\n"), 0o644)
	if st, _ := files.State(); st.Seq() != 9 {
		t.Errorf("seq %d", st.Seq())
	}
	files.ClearState()
	if _, err := os.Stat(path); err == nil {
		t.Error("the state file is still there")
	}
}

func TestLogLines(t *testing.T) {
	dir, files := game(t)
	if !strings.HasSuffix(files.LogPath(), "tandem-2026-11-19-09-00-00.log") {
		t.Fatalf("log: %s", files.LogPath())
	}
	if lines := files.LogLines(); len(lines) != 0 {
		t.Errorf("lines from before the pilot: %q", lines)
	}
	file, _ := os.OpenFile(filepath.Join(dir, "red4ext", "logs", "tandem-2026-11-19-09-00-00.log"), os.O_APPEND|os.O_WRONLY, 0)
	defer file.Close()
	_, _ = file.WriteString("[info] pilot: on\r\n[info] half a li")
	if lines := files.LogLines(); len(lines) != 1 || lines[0] != "[info] pilot: on" {
		t.Errorf("lines: %q", lines)
	}
	_, _ = file.WriteString("ne\n[info] teleport: (1, 2, 3) yaw 0\n")
	if lines := files.LogLines(); len(lines) != 2 || lines[0] != "[info] half a line" || lines[1] != "[info] teleport: (1, 2, 3) yaw 0" {
		t.Errorf("lines: %q", lines)
	}
	if lines := files.LogLines(); len(lines) != 0 {
		t.Errorf("lines twice: %q", lines)
	}
}
