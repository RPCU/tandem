// Package spool hands routines to a pilot that stays up between them
// (tandem-pilot serve: it holds the pad from before the game starts, for a
// game that only takes pads it saw at launch). Files in the instance's
// cache folder: "job" names the routine, "job.log" is its log as it runs,
// "job.result" its exit code, "job.stop" asks it to stop, "serving" says a
// server is up.
package spool

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// Dir is one instance's spool.
type Dir string

func (d Dir) path(name string) string { return filepath.Join(string(d), name) }

// Serving marks a server up (its pid), or takes the mark away.
func (d Dir) Serving(up bool) error {
	if !up {
		_ = os.Remove(d.path("job"))
		return os.Remove(d.path("serving"))
	}
	return os.WriteFile(d.path("serving"), []byte(strconv.Itoa(os.Getpid())+"\n"), 0o644)
}

// Server answers the pid of the server that is up, 0 when none is: the
// mark must name a live process.
func (d Dir) Server() int {
	data, err := os.ReadFile(d.path("serving"))
	if err != nil {
		return 0
	}
	pid, err := strconv.Atoi(strings.TrimSpace(string(data)))
	if err != nil || pid <= 0 {
		return 0
	}
	if _, err := os.Stat(filepath.Join("/proc", strconv.Itoa(pid))); err != nil {
		return 0
	}
	return pid
}

// Take is the server's side: the routine of a waiting job, if there is
// one. The job is then running: its log is open. (A stop request is the
// client's to clear before it submits: it may come before the job is taken.)
func (d Dir) Take() (routine string, log *os.File, ok bool) {
	data, err := os.ReadFile(d.path("job"))
	if err != nil {
		return "", nil, false
	}
	_ = os.Remove(d.path("job"))
	_ = os.Remove(d.path("job.result"))
	log, err = os.Create(d.path("job.log"))
	if err != nil {
		return "", nil, false
	}
	return strings.TrimSpace(string(data)), log, true
}

// Stopped says whether the running job was asked to stop.
func (d Dir) Stopped() bool {
	_, err := os.Stat(d.path("job.stop"))
	return err == nil
}

// Finish records the running job's exit code.
func (d Dir) Finish(code int) error {
	tmp := d.path("job.result.tmp")
	if err := os.WriteFile(tmp, []byte(strconv.Itoa(code)+"\n"), 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, d.path("job.result"))
}

// Submit is the client's side: it hands the routine to the server, copies
// the job's log to out as it grows, and answers its exit code. A closed
// stop asks the job to stop (its log still runs to its end). It gives up
// when the server is gone.
func (d Dir) Submit(routine string, out io.Writer, stop <-chan struct{}) (int, error) {
	if d.Server() == 0 {
		return 1, errors.New("no pilot is serving")
	}
	for _, name := range []string{"job.result", "job.stop", "job.log"} {
		_ = os.Remove(d.path(name))
	}
	tmp := d.path("job.tmp")
	if err := os.WriteFile(tmp, []byte(routine+"\n"), 0o644); err != nil {
		return 1, err
	}
	if err := os.Rename(tmp, d.path("job")); err != nil {
		return 1, err
	}
	var offset int64
	copyLog := func() {
		file, err := os.Open(d.path("job.log"))
		if err != nil {
			return
		}
		defer file.Close()
		if _, err := file.Seek(offset, io.SeekStart); err == nil {
			n, _ := io.Copy(out, file)
			offset += n
		}
	}
	asked := false
	for {
		select {
		case <-stop:
			if !asked {
				asked = true
				_ = os.WriteFile(d.path("job.stop"), nil, 0o644)
			}
		default:
		}
		copyLog()
		if data, err := os.ReadFile(d.path("job.result")); err == nil {
			copyLog()
			code, err := strconv.Atoi(strings.TrimSpace(string(data)))
			if err != nil {
				return 1, fmt.Errorf("the job's result: %q", data)
			}
			return code, nil
		}
		if d.Server() == 0 {
			copyLog()
			return 1, errors.New("the serving pilot is gone")
		}
		time.Sleep(50 * time.Millisecond)
	}
}
