package guard

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Instance is a game set up by tools/instance.sh: its directory holds
// .tandem-instance, game/ (the game folder) and compatdata/ (its prefix).
// Its processes carry TANDEM_INSTANCE_ID=<its directory> in their
// environment, which is how the pilot knows the game is ours: instance.sh
// never starts the player's game, and the player's game never carries it.
type Instance struct {
	Dir  string
	Name string
	// Pad is the USB product id of the one pad this instance's game reads
	// (instance.sh starts it with SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT
	// naming it); 0: none recorded.
	Pad uint16
}

// InstanceMark is the variable instance.sh marks an instance's processes with.
const InstanceMark = "TANDEM_INSTANCE_ID"

// PadHint is the SDL hint that keeps a game to the pads it names.
const PadHint = "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"

// FindInstance answers the instance whose game folder gameDir is, or nil
// for a game that is not one (the Steam install).
func FindInstance(gameDir string) (*Instance, error) {
	dir := filepath.Dir(filepath.Clean(gameDir))
	if filepath.Base(filepath.Clean(gameDir)) != "game" {
		return nil, nil
	}
	data, err := os.ReadFile(filepath.Join(dir, ".tandem-instance"))
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	inst := &Instance{Dir: dir, Name: filepath.Base(dir)}
	for _, line := range strings.Split(string(data), "\n") {
		key, value, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		switch strings.TrimSpace(key) {
		case "name":
			inst.Name = strings.TrimSpace(value)
		case "pad":
			if v, err := strconv.ParseUint(strings.TrimSpace(value), 0, 16); err == nil {
				inst.Pad = uint16(v)
			}
		}
	}
	return inst, nil
}

// Saves is the instance's own save folder, in its own prefix.
func (i *Instance) Saves() string {
	return filepath.Join(i.Dir, "compatdata", "pfx", "drive_c", "users", "steamuser", "Saved Games", "CD Projekt Red", "Cyberpunk 2077")
}

// Environ reads a process's environment.
func Environ(proc string, pid int) map[string]string {
	data, err := os.ReadFile(filepath.Join(proc, strconv.Itoa(pid), "environ"))
	if err != nil {
		return nil
	}
	out := map[string]string{}
	for _, item := range bytes.Split(data, []byte{0}) {
		if key, value, ok := strings.Cut(string(item), "="); ok {
			out[key] = value
		}
	}
	return out
}

// CheckInstance finds the instance's game and says whether it is ours: run
// from the instance's game folder and marked with the instance's directory.
func CheckInstance(proc string, inst *Instance) (Process, error) {
	game, err := FindGame(proc, filepath.Join(inst.Dir, "game"))
	if err != nil {
		return game, fmt.Errorf("instance %s: %w", inst.Name, err)
	}
	if mark := Environ(proc, game.PID)[InstanceMark]; filepath.Clean(mark) != filepath.Clean(inst.Dir) {
		return game, fmt.Errorf("the running game (pid %d) is not instance %s's (its mark: %q): the pilot does not touch a game someone else started", game.PID, inst.Name, mark)
	}
	return game, nil
}

// Games lists every running game process.
func Games(proc string) []Process {
	entries, err := os.ReadDir(proc)
	if err != nil {
		return nil
	}
	var out []Process
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
		out = append(out, Process{PID: pid, Start: start})
	}
	return out
}

// hinted says whether a game's environment keeps it to a list of pads that
// includes (or, with want false, excludes) vendor:product.
func hinted(env map[string]string, vendor, product uint16) (set, includes bool) {
	value, set := env[PadHint]
	if !set {
		return false, false
	}
	id := fmt.Sprintf("0x%04x/0x%04x", vendor, product)
	for _, item := range strings.Split(strings.ToLower(value), ",") {
		if strings.TrimSpace(item) == id {
			return true, true
		}
	}
	return true, false
}

// PadReachesOnly checks that a virtual pad vendor:product would be read by
// self alone: self's game is kept to it by the SDL hint, and every other
// running game (another instance, the player's) is kept to other pads. A
// uinput pad is seen by every program on the machine; this is what keeps
// it out of the other games.
func PadReachesOnly(proc string, self Process, vendor, product uint16) error {
	if set, includes := hinted(Environ(proc, self.PID), vendor, product); !set || !includes {
		return fmt.Errorf("the game (pid %d) was not started with %s=0x%04x/0x%04x: it would read every pad (start the instance with its pad: tools/instance.sh)", self.PID, PadHint, vendor, product)
	}
	for _, other := range Games(proc) {
		if other.PID == self.PID {
			continue
		}
		if set, includes := hinted(Environ(proc, other.PID), vendor, product); !set || includes {
			return fmt.Errorf("another game (pid %d) would read this pad too (its %s does not leave it out): no pad for this run", other.PID, PadHint)
		}
	}
	return nil
}

// Display is the X server gamescope started for the instance, as its launch
// log names it ("Starting Xwayland on :N", what tools/instance.sh xdo
// uses); "" when it has none (not started in gamescope).
func (i *Instance) Display() string {
	data, err := os.ReadFile(filepath.Join(i.Dir, "run", "launch.log"))
	if err != nil {
		return ""
	}
	display := ""
	for _, line := range strings.Split(string(data), "\n") {
		_, rest, found := strings.Cut(line, "Starting Xwayland on :")
		if !found {
			continue
		}
		end := 0
		for end < len(rest) && rest[end] >= '0' && rest[end] <= '9' {
			end++
		}
		if end > 0 {
			display = ":" + rest[:end]
		}
	}
	return display
}

// GameDisplay is the X display the game draws on (its DISPLAY): under
// gamescope, gamescope's own nested Xwayland, with nothing else in it.
func GameDisplay(proc string, pid int) string {
	return Environ(proc, pid)["DISPLAY"]
}
