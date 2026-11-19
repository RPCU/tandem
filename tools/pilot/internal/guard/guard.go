// Package guard holds what keeps the pilot from acting where it must not:
// one pilot per game instance (a lock file), only on a game our tools
// started (the owner file tools/game.sh start writes), only while the game's
// window has the focus, and a check that no save was written meanwhile.
package guard

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"
)

// Lock takes the instance's lock file, or says who holds it. The lock goes
// with the process: a pilot that dies cannot leave it held.
func Lock(path string) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, err
	}
	file, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, err
	}
	if err := syscall.Flock(int(file.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		holder, _ := os.ReadFile(path)
		_ = file.Close()
		return nil, fmt.Errorf("another pilot holds %s (pid %s)", path, strings.TrimSpace(string(holder)))
	}
	_ = file.Truncate(0)
	_, _ = file.WriteAt([]byte(strconv.Itoa(os.Getpid())+"\n"), 0)
	return file, nil
}

// Process is a running game: its pid and the kernel's start time for it
// (clock ticks since boot), which together never name another process.
type Process struct {
	PID   int
	Start uint64
}

func (p Process) String() string { return fmt.Sprintf("%d %d", p.PID, p.Start) }

// winPath is a path as the game's command line shows it under Wine, without
// the drive: backslashes, compared without case.
func winPath(path string) string {
	return strings.ToLower(strings.ReplaceAll(filepath.Clean(path), "/", `\`))
}

// isGame says whether a command line is the game's own process (not the
// launcher scripts around it): "Z:\...\Cyberpunk2077.exe ...".
func isGame(cmdline string) bool {
	first, _, _ := strings.Cut(cmdline, "\x00")
	return len(first) > 2 && first[1] == ':' && strings.HasSuffix(strings.ToLower(first), `\cyberpunk2077.exe`)
}

// FindGame finds the game process running from gameDir under proc (/proc).
// With several games running, only the one whose path is gameDir matches.
func FindGame(proc, gameDir string) (Process, error) {
	entries, err := os.ReadDir(proc)
	if err != nil {
		return Process{}, err
	}
	want := winPath(gameDir)
	var all, ours []Process
	for _, entry := range entries {
		pid, err := strconv.Atoi(entry.Name())
		if err != nil {
			continue
		}
		cmdline, err := os.ReadFile(filepath.Join(proc, entry.Name(), "cmdline"))
		if err != nil || !isGame(string(cmdline)) {
			continue
		}
		start, err := startTime(filepath.Join(proc, entry.Name(), "stat"))
		if err != nil {
			continue
		}
		p := Process{PID: pid, Start: start}
		all = append(all, p)
		first, _, _ := strings.Cut(string(cmdline), "\x00")
		if strings.Contains(strings.ToLower(first), want) {
			ours = append(ours, p)
		}
	}
	switch {
	case len(ours) == 1:
		return ours[0], nil
	case len(ours) > 1:
		return Process{}, fmt.Errorf("%d games run from %s", len(ours), gameDir)
	case len(all) == 1:
		// One game, whose path reads differently (a symlinked library).
		return all[0], nil
	case len(all) == 0:
		return Process{}, errors.New("the game is not running")
	}
	return Process{}, fmt.Errorf("%d games run, none from %s", len(all), gameDir)
}

// startTime is field 22 of /proc/PID/stat; the name before it may hold
// spaces and brackets, so the fields are counted after the last ')'.
func startTime(statPath string) (uint64, error) {
	data, err := os.ReadFile(statPath)
	if err != nil {
		return 0, err
	}
	text := string(data)
	i := strings.LastIndex(text, ")")
	if i < 0 {
		return 0, errors.New("unexpected stat")
	}
	fields := strings.Fields(text[i+1:])
	if len(fields) < 20 {
		return 0, errors.New("unexpected stat")
	}
	return strconv.ParseUint(fields[19], 10, 64)
}

// CheckOwner says whether the running game is the one our tools started:
// ownerFile holds "PID START" written by tools/game.sh start. A game the
// player launched has no such file, or another process in it.
func CheckOwner(proc, gameDir, ownerFile string) (Process, error) {
	game, err := FindGame(proc, gameDir)
	if err != nil {
		return game, err
	}
	data, err := os.ReadFile(ownerFile)
	if err != nil {
		return game, fmt.Errorf("the running game (pid %d) was not started by tools/game.sh (no %s): the pilot does not touch a game someone else started", game.PID, ownerFile)
	}
	if strings.TrimSpace(string(data)) != game.String() {
		return game, fmt.Errorf("the running game (pid %d) is not the one tools/game.sh started (%s): the pilot does not touch a game someone else started", game.PID, strings.TrimSpace(string(data)))
	}
	return game, nil
}

// swayNode is the part of sway's tree the focus check reads.
type swayNode struct {
	Focused bool   `json:"focused"`
	Name    string `json:"name"`
	AppID   string `json:"app_id"`
	Window  struct {
		Class string `json:"class"`
	} `json:"window_properties"`
	Nodes    []swayNode `json:"nodes"`
	Floating []swayNode `json:"floating_nodes"`
}

func focused(node *swayNode) *swayNode {
	if node.Focused {
		return node
	}
	for i := range node.Nodes {
		if found := focused(&node.Nodes[i]); found != nil {
			return found
		}
	}
	for i := range node.Floating {
		if found := focused(&node.Floating[i]); found != nil {
			return found
		}
	}
	return nil
}

// FocusedClass reads sway's tree (swaymsg -t get_tree, as JSON) and answers
// the focused window's X11 class, or its app id.
func FocusedClass(tree []byte) (string, error) {
	var root swayNode
	if err := json.Unmarshal(tree, &root); err != nil {
		return "", fmt.Errorf("sway's tree: %w", err)
	}
	node := focused(&root)
	if node == nil {
		return "", errors.New("no window has the focus")
	}
	if node.Window.Class != "" {
		return node.Window.Class, nil
	}
	if node.AppID != "" {
		return node.AppID, nil
	}
	return "", fmt.Errorf("the focus is on %q, not a window", node.Name)
}

// Focus checks that the game's window has the focus under sway.
type Focus struct {
	Class string // steam_app_1091500
	// Run runs swaymsg; replaced in tests.
	Run func(args ...string) ([]byte, error)
}

// NewFocus checks for class with the real swaymsg.
func NewFocus(class string) *Focus {
	return &Focus{Class: class, Run: func(args ...string) ([]byte, error) {
		return exec.Command("swaymsg", args...).Output()
	}}
}

// Check answers nil when the game's window has the focus.
func (f *Focus) Check() error {
	tree, err := f.Run("-t", "get_tree")
	if err != nil {
		return fmt.Errorf("swaymsg: %w (is SWAYSOCK set?)", err)
	}
	class, err := FocusedClass(tree)
	if err != nil {
		return err
	}
	if class != f.Class {
		return fmt.Errorf("the focus is on %s, not the game (%s)", class, f.Class)
	}
	return nil
}

// Take asks sway to focus the game's window, then checks.
func (f *Focus) Take() error {
	if f.Check() == nil {
		return nil
	}
	if _, err := f.Run(fmt.Sprintf(`[class="^%s$"] focus`, f.Class)); err != nil {
		return fmt.Errorf("swaymsg focus: %w", err)
	}
	return f.Check()
}

// Saves is what the save folder held: each file's size and time.
type Saves map[string]string

// SnapshotSaves lists every file under dir.
func SnapshotSaves(dir string) (Saves, error) {
	out := Saves{}
	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() {
			rel, _ := filepath.Rel(dir, path)
			out[rel] = fmt.Sprintf("%d %d", info.Size(), info.ModTime().UnixNano())
		}
		return nil
	})
	return out, err
}

// Diff lists what changed from before to after: files written, changed or
// removed. Empty: no save was touched.
func (before Saves) Diff(after Saves) []string {
	var out []string
	for name, stamp := range after {
		if old, was := before[name]; !was {
			out = append(out, "new: "+name)
		} else if old != stamp {
			out = append(out, "changed: "+name)
		}
	}
	for name := range before {
		if _, still := after[name]; !still {
			out = append(out, "removed: "+name)
		}
	}
	sort.Strings(out)
	return out
}

// DefaultSaves is where the game's saves are under Proton, for a game in
// LIBRARY/steamapps/common/Cyberpunk 2077.
func DefaultSaves(gameDir string) string {
	return filepath.Join(filepath.Dir(filepath.Dir(filepath.Clean(gameDir))), "compatdata", "1091500", "pfx", "drive_c",
		"users", "steamuser", "Saved Games", "CD Projekt Red", "Cyberpunk 2077")
}
