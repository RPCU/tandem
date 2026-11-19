// Package link reaches the running game through files in its folder: the
// command file tandem reads (tandem.cmd), the pilot state the plugin writes
// (pilot.state) and tandem's log. The pilot runs on the game machine, so
// these are local files and the control loop has no network in it.
package link

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/Banh-Canh/tandem/tools/pilot/internal/state"
)

// Files is the game in one folder.
type Files struct {
	cmdPath   string
	statePath string
	logPath   string
	logOffset int64
	partial   []byte
	last      state.State
	// TakeTimeout is how long a command may wait for tandem to take the one
	// before it.
	TakeTimeout time.Duration
}

// PluginDir is where tandem lives in a game folder.
func PluginDir(gameDir string) string {
	return filepath.Join(gameDir, "red4ext", "plugins", "tandem")
}

// Open finds the files of the game in gameDir. The log is read from its
// end: only what the game writes from now on.
func Open(gameDir string) (*Files, error) {
	plugin := PluginDir(gameDir)
	if info, err := os.Stat(plugin); err != nil || !info.IsDir() {
		return nil, fmt.Errorf("tandem is not installed in %s", gameDir)
	}
	f := &Files{
		cmdPath:     filepath.Join(plugin, "tandem.cmd"),
		statePath:   filepath.Join(plugin, "pilot.state"),
		TakeTimeout: 5 * time.Second,
	}
	logs, _ := filepath.Glob(filepath.Join(gameDir, "red4ext", "logs", "tandem-*.log"))
	if len(logs) == 0 {
		return nil, fmt.Errorf("no tandem log in %s/red4ext/logs: has the game run with tandem?", gameDir)
	}
	sort.Slice(logs, func(i, j int) bool {
		a, _ := os.Stat(logs[i])
		b, _ := os.Stat(logs[j])
		return a != nil && b != nil && a.ModTime().After(b.ModTime())
	})
	f.logPath = logs[0]
	if info, err := os.Stat(f.logPath); err == nil {
		f.logOffset = info.Size()
	}
	return f, nil
}

// LogPath is the game log being followed.
func (f *Files) LogPath() string { return f.logPath }

// Command hands tandem one command: it waits until tandem took the one
// before, then puts this one in place whole (a rename). A heartbeat or a
// "pilot on" is not worth waiting for (both are sent again): they are
// dropped when the slot is taken.
func (f *Files) Command(text string) error {
	if strings.ContainsAny(text, "\r\n") {
		return errors.New("a command is one line")
	}
	limit := time.Now().Add(f.TakeTimeout)
	for {
		if _, err := os.Stat(f.cmdPath); errors.Is(err, os.ErrNotExist) {
			break
		}
		if text == "pilot beat" || text == "pilot on" {
			return nil
		}
		if time.Now().After(limit) {
			return fmt.Errorf("tandem did not take the previous command within %.0f s (paused, loading, or not in a game)", f.TakeTimeout.Seconds())
		}
		time.Sleep(5 * time.Millisecond)
	}
	tmp := f.cmdPath + ".pilot"
	if err := os.WriteFile(tmp, []byte(text+"\n"), 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, f.cmdPath)
}

// State reads the pilot state. A file that cannot be read or parsed this
// instant answers the last good one.
func (f *Files) State() (state.State, bool) {
	data, err := os.ReadFile(f.statePath)
	if err != nil {
		return f.last, f.last.Valid()
	}
	line, _, _ := bytes.Cut(data, []byte("\n"))
	st, err := state.Parse(string(line))
	if err != nil {
		return f.last, f.last.Valid()
	}
	f.last = st
	return st, true
}

// ClearState removes a state file left by an earlier run, so that a game
// that is not writing one is noticed.
func (f *Files) ClearState() { _ = os.Remove(f.statePath) }

// LogLines are the lines the game logged since the last call.
func (f *Files) LogLines() []string {
	file, err := os.Open(f.logPath)
	if err != nil {
		return nil
	}
	defer file.Close()
	info, err := file.Stat()
	if err != nil || info.Size() <= f.logOffset {
		return nil
	}
	buf := make([]byte, info.Size()-f.logOffset)
	n, _ := file.ReadAt(buf, f.logOffset)
	f.logOffset += int64(n)
	data := append(f.partial, buf[:n]...)
	var lines []string
	for {
		line, rest, found := bytes.Cut(data, []byte("\n"))
		if !found {
			break
		}
		lines = append(lines, strings.TrimRight(string(line), "\r"))
		data = rest
	}
	f.partial = append([]byte(nil), data...)
	return lines
}
