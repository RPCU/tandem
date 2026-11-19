package spool

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// serve plays the server: each job's "routine" is logged in three lines,
// 30 ms apart, and exits with code; a stop request ends it early with 130.
func serve(t *testing.T, d Dir, code int, done <-chan struct{}) {
	t.Helper()
	if err := d.Serving(true); err != nil {
		t.Error(err)
		return
	}
	defer func() { _ = d.Serving(false) }()
	for {
		select {
		case <-done:
			return
		default:
		}
		routine, log, ok := d.Take()
		if !ok {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		result := code
		for i := 1; i <= 3; i++ {
			fmt.Fprintf(log, "pilot: %s step %d\n", routine, i)
			time.Sleep(30 * time.Millisecond)
			if d.Stopped() {
				fmt.Fprintln(log, "pilot: stopped")
				result = 130
				break
			}
		}
		_ = log.Close()
		_ = d.Finish(result)
	}
}

func TestSubmit(t *testing.T) {
	d := Dir(t.TempDir())
	if d.Server() != 0 {
		t.Fatal("a server in an empty folder")
	}
	var out strings.Builder
	if _, err := d.Submit("street.pilot", &out, nil); err == nil || !strings.Contains(err.Error(), "no pilot is serving") {
		t.Fatalf("no server: %v", err)
	}
	// A mark left by a server that died names no live process.
	_ = os.WriteFile(filepath.Join(string(d), "serving"), []byte("999999999\n"), 0o644)
	if d.Server() != 0 {
		t.Error("a dead server counts as up")
	}

	done := make(chan struct{})
	go serve(t, d, 0, done)
	for i := 0; i < 100 && d.Server() == 0; i++ {
		time.Sleep(5 * time.Millisecond)
	}
	if d.Server() != os.Getpid() {
		t.Fatalf("server: %d", d.Server())
	}
	code, err := d.Submit("street.pilot", &out, nil)
	if err != nil || code != 0 {
		t.Fatalf("submit: %d %v", code, err)
	}
	want := "pilot: street.pilot step 1\npilot: street.pilot step 2\npilot: street.pilot step 3\n"
	if out.String() != want {
		t.Errorf("log: %q", out.String())
	}
	// A second job: its log is its own.
	out.Reset()
	if code, err := d.Submit("fight.pilot", &out, nil); err != nil || code != 0 || !strings.HasPrefix(out.String(), "pilot: fight.pilot step 1\n") || strings.Contains(out.String(), "street") {
		t.Errorf("second job: %d %v %q", code, err, out.String())
	}
	// A stop request reaches the running job.
	out.Reset()
	stop := make(chan struct{})
	close(stop)
	if code, err := d.Submit("drive.pilot", &out, stop); err != nil || code != 130 || !strings.Contains(out.String(), "pilot: stopped") {
		t.Errorf("stopped job: %d %v %q", code, err, out.String())
	}
	close(done)
}

func TestSubmitFailureAndServerGone(t *testing.T) {
	d := Dir(t.TempDir())
	done := make(chan struct{})
	go serve(t, d, 1, done)
	for i := 0; i < 100 && d.Server() == 0; i++ {
		time.Sleep(5 * time.Millisecond)
	}
	var out strings.Builder
	if code, err := d.Submit("down.pilot", &out, nil); err != nil || code != 1 {
		t.Errorf("a failed routine: %d %v", code, err)
	}
	close(done)
	for i := 0; i < 100 && d.Server() != 0; i++ {
		time.Sleep(5 * time.Millisecond)
	}
	// The server went away with a job waiting: the client does not hang.
	_ = os.WriteFile(filepath.Join(string(d), "serving"), []byte(fmt.Sprint(os.Getpid())), 0o644)
	go func() {
		time.Sleep(80 * time.Millisecond)
		_ = os.Remove(filepath.Join(string(d), "serving"))
	}()
	if _, err := d.Submit("street.pilot", &out, nil); err == nil || !strings.Contains(err.Error(), "is gone") {
		t.Errorf("server gone: %v", err)
	}
}
