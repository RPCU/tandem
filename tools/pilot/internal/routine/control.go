package routine

import (
	"errors"
	"fmt"
	"math"
	"time"

	"github.com/RPCU/tandem/tools/pilot/internal/pad"
)

// The closed loops: each reads the plugin's pilot state every tick and
// moves the sticks towards the goal. Directions come from the state's own
// vectors (fx fy fz: where the view looks; hx hy: where the body or the
// vehicle points), so no angle convention is assumed. In the game's world,
// with the view along (fx, fy), the right-hand side is (fy, -fx).

func unit(x, y float64) (float64, float64, float64) {
	m := math.Hypot(x, y)
	if m < 1e-9 {
		return 0, 0, 0
	}
	return x / m, y / m, m
}

// leftOf is the signed angle from direction a to direction b, in radians,
// positive when b is to the left of a (counter-clockwise seen from above).
func leftOf(ax, ay, bx, by float64) float64 {
	return math.Atan2(ax*by-ay*bx, ax*bx+ay*by)
}

func clamp(v, lo, hi float64) float64 { return math.Max(lo, math.Min(hi, v)) }

func (r *runner) forward() (float64, float64, bool) {
	fx, okx := r.last.Num("fx")
	fy, oky := r.last.Num("fy")
	x, y, m := unit(fx, fy)
	return x, y, okx && oky && m > 0
}

func (r *runner) heading() (float64, float64, bool) {
	hx, okx := r.last.Num("hx")
	hy, oky := r.last.Num("hy")
	x, y, m := unit(hx, hy)
	return x, y, okx && oky && m > 0
}

// walk steers V on foot to a point with the left stick: the stick points at
// the goal in the view's frame, so V goes there whatever it looks at.
func (r *runner) walk(step *Step) (string, error) {
	within := step.Within
	if within == 0 {
		within = 0.75
	}
	pace := step.Pace
	if pace == "" {
		pace = "jog"
	}
	began := r.clock.Now()
	var limit time.Time
	best, bestAt := math.Inf(1), began
	jumps := 0
	lastSprint := began.Add(-time.Hour)
	defer func() { _ = r.pad.Stick(pad.LeftStick, 0, 0) }()
	for {
		if !r.stale {
			if r.last.N("veh") != 0 {
				return "", errors.New("V is in a vehicle (drive, or act exit first)")
			}
			to, err := r.resolve(step.Target)
			if err != nil {
				return "", err
			}
			here, _ := r.last.Vec("x", "y", "z")
			dx, dy, dist := unit(to[0]-here[0], to[1]-here[1])
			now := r.clock.Now()
			if limit.IsZero() {
				timeout := step.Timeout
				if timeout == 0 {
					timeout = 15 + dist/1.2
				}
				limit = began.Add(time.Duration(timeout * float64(time.Second)))
			}
			floors := step.Target.HasZ && math.Abs(to[2]-here[2]) > 2.5
			if dist <= within && !floors {
				_ = r.pad.Stick(pad.LeftStick, 0, 0)
				return fmt.Sprintf(": arrived, %.2f m off", dist), nil
			}
			if now.After(limit) {
				return "", fmt.Errorf("not there in time: %.1f m away at %.1f %.1f %.1f", dist, here[0], here[1], here[2])
			}
			// Progress, or a jump over what blocks the way, then give up.
			if dist < best-0.25 {
				best, bestAt = dist, now
			} else if now.Sub(bestAt) > 2500*time.Millisecond {
				if jumps >= 3 {
					return "", fmt.Errorf("stuck at %.1f %.1f %.1f, %.1f m from the goal", here[0], here[1], here[2], dist)
				}
				jumps++
				bestAt = now
				r.logf("walk: no progress, a jump (%d)", jumps)
				_ = r.pad.Button(pad.A, true)
				if err := r.sleep(0.15); err != nil {
					return "", err
				}
				_ = r.pad.Button(pad.A, false)
			}
			fx, fy, ok := r.forward()
			if !ok {
				return "", errors.New("the state has no view direction (fx fy)")
			}
			size := 1.0
			if pace == "walk" {
				size = 0.45
			}
			// Slow for the last step so V does not overshoot a small radius.
			if dist < within+0.6 {
				size = math.Min(size, 0.6)
			}
			side := dx*fy - dy*fx  // along the view's right
			ahead := dx*fx + dy*fy // along the view
			err = r.pad.Stick(pad.LeftStick, side*size*r.moveSign[0], ahead*size*r.moveSign[1])
			if err != nil {
				return "", err
			}
			if pace == "sprint" && r.last.N("sprint") != 1 && dist > 3 && now.Sub(lastSprint) > 1200*time.Millisecond && now.Sub(began) > 300*time.Millisecond {
				lastSprint = now
				_ = r.pad.Button(pad.L3, true)
				if err := r.sleep(0.1); err != nil {
					return "", err
				}
				_ = r.pad.Button(pad.L3, false)
			}
		}
		if err := r.tick(); err != nil {
			return "", err
		}
	}
}

// drive steers the vehicle V drives to a point: the left stick turns the
// wheels towards it, the triggers hold a speed that drops in turns and near
// the goal, and it brakes to a stop there.
func (r *runner) drive(step *Step) (string, error) {
	within := step.Within
	if within == 0 {
		within = 6
	}
	cruise := step.Speed
	if cruise == 0 {
		cruise = 12
	}
	began := r.clock.Now()
	var limit time.Time
	best, bestAt := math.Inf(1), began
	rest := func() {
		_ = r.pad.Stick(pad.LeftStick, 0, 0)
		_ = r.pad.Trigger(pad.RT, 0)
		_ = r.pad.Trigger(pad.LT, 0)
	}
	defer rest()
	for {
		if !r.stale {
			if r.last.N("veh") != 1 || r.last.Str("seat") != "driver" {
				return "", errors.New("V is not driving a vehicle")
			}
			to, err := r.resolve(step.Target)
			if err != nil {
				return "", err
			}
			here, _ := r.last.Vec("x", "y", "z")
			dx, dy, dist := unit(to[0]-here[0], to[1]-here[1])
			now := r.clock.Now()
			speed := math.Abs(r.last.N("speed"))
			if limit.IsZero() {
				timeout := step.Timeout
				if timeout == 0 {
					timeout = 30 + dist/4
				}
				limit = began.Add(time.Duration(timeout * float64(time.Second)))
			}
			if dist <= within {
				// Brake to a stop.
				_ = r.pad.Stick(pad.LeftStick, 0, 0)
				_ = r.pad.Trigger(pad.RT, 0)
				stopBy := now.Add(6 * time.Second)
				for math.Abs(r.last.N("speed")) > 0.5 && r.clock.Now().Before(stopBy) {
					if !r.stale {
						_ = r.pad.Trigger(pad.LT, 1)
					}
					if err := r.tick(); err != nil {
						return "", err
					}
				}
				return fmt.Sprintf(": arrived, %.1f m off", dist), nil
			}
			if now.After(limit) {
				return "", fmt.Errorf("not there in time: %.0f m away at %.1f %.1f %.1f", dist, here[0], here[1], here[2])
			}
			if dist < best-1 {
				best, bestAt = dist, now
			} else if now.Sub(bestAt) > 12*time.Second {
				return "", fmt.Errorf("stuck at %.1f %.1f %.1f, %.0f m from the goal", here[0], here[1], here[2], dist)
			}
			hx, hy, ok := r.heading()
			if !ok {
				return "", errors.New("the state has no heading (hx hy)")
			}
			off := leftOf(hx, hy, dx, dy) // + : the goal is to the left
			steer := clamp(-off/0.6, -1, 1) * r.steerSign
			want := cruise * (1 - 0.6*math.Min(1, math.Abs(off)/0.8))
			want = math.Min(want, math.Max(3, (dist-within)/1.5))
			gas, brake := 0.0, 0.0
			switch {
			case speed < want-1:
				gas = 0.8
			case speed > want+2:
				brake = 0.6
			default:
				gas = 0.35
			}
			if err := r.pad.Stick(pad.LeftStick, steer, 0); err != nil {
				return "", err
			}
			_ = r.pad.Trigger(pad.RT, gas)
			_ = r.pad.Trigger(pad.LT, brake)
		}
		if err := r.tick(); err != nil {
			return "", err
		}
	}
}

// face turns the view to a point with the right stick, yaw and pitch, until
// both are within the tolerance (degrees) for a few ticks.
func (r *runner) face(step *Step) (string, error) {
	tolerance := step.Tolerance
	if tolerance == 0 {
		tolerance = 3
	}
	tolerance *= math.Pi / 180
	timeout := step.Timeout
	if timeout == 0 {
		timeout = 8
	}
	limit := r.clock.Now().Add(time.Duration(timeout * float64(time.Second)))
	defer func() { _ = r.pad.Stick(pad.RightStick, 0, 0) }()
	settled := 0
	for {
		if !r.stale {
			to, err := r.resolve(step.Target)
			if err != nil {
				return "", err
			}
			eye, ok := r.last.Vec("cx", "cy", "cz")
			if !ok {
				eye, _ = r.last.Vec("x", "y", "z")
				eye[2] += 1.6
			}
			fx, fy, ok := r.forward()
			if !ok {
				return "", errors.New("the state has no view direction (fx fy)")
			}
			dx, dy, flat := unit(to[0]-eye[0], to[1]-eye[1])
			yaw := 0.0
			if flat > 0.05 {
				yaw = leftOf(fx, fy, dx, dy)
			}
			pitch := 0.0
			if step.Target.HasZ || step.Target.Target {
				pitch = math.Atan2(to[2]-eye[2], flat) - math.Asin(clamp(r.last.N("fz"), -1, 1))
			}
			if math.Abs(yaw) <= tolerance && math.Abs(pitch) <= tolerance {
				_ = r.pad.Stick(pad.RightStick, 0, 0)
				if settled++; settled >= 3 {
					return fmt.Sprintf(": %.1f° off", math.Hypot(yaw, pitch)*180/math.Pi), nil
				}
			} else {
				settled = 0
				if r.clock.Now().After(limit) {
					return "", fmt.Errorf("not facing it in time: yaw %.0f°, pitch %.0f° off", yaw*180/math.Pi, pitch*180/math.Pi)
				}
				// Proportional, with a floor that clears the stick's dead zone.
				push := func(off float64) float64 {
					if math.Abs(off) <= tolerance/2 {
						return 0
					}
					return math.Copysign(clamp(math.Abs(off)/0.7, 0.22, 1), off)
				}
				// A goal to the left: the stick goes left (negative x).
				if err := r.pad.Stick(pad.RightStick, -push(yaw)*r.lookSign[0], push(pitch)*r.lookSign[1]); err != nil {
					return "", err
				}
			}
		}
		if err := r.tick(); err != nil {
			return "", err
		}
	}
}

// calibrate proves the pad reaches the game and learns which way each axis
// acts: a push forward and one to the right must move V that way, a push of
// the right stick must turn the view, and (steer) the wheels must turn the
// car. A reversed axis (an inverted camera) is remembered; an axis that
// does nothing fails: the game does not read the pad.
func (r *runner) calibrate(step *Step) (string, error) {
	if r.stale {
		return "", errors.New("the game's state stopped")
	}
	out := ""
	if step.What == "all" || step.What == "move" {
		if r.last.N("veh") != 0 {
			return "", errors.New("calibrate move: V is in a vehicle")
		}
		fx, fy, ok := r.forward()
		if !ok {
			return "", errors.New("the state has no view direction (fx fy)")
		}
		for axis, name := range []string{"right", "forward"} {
			from, _ := r.last.Vec("x", "y", "z")
			x, y := 0.0, 0.0
			if axis == 0 {
				x = 0.7
			} else {
				y = 0.7
			}
			if err := r.pad.Stick(pad.LeftStick, x, y); err != nil {
				return "", err
			}
			err := r.sleep(1.0)
			_ = r.pad.Stick(pad.LeftStick, 0, 0)
			if err == nil {
				err = r.sleep(0.5)
			}
			if err != nil {
				return "", err
			}
			to, _ := r.last.Vec("x", "y", "z")
			mx, my := to[0]-from[0], to[1]-from[1]
			if math.Hypot(mx, my) < 0.3 {
				return "", fmt.Errorf("the left stick did not move V (%.2f m): the game does not read the pad (focus, Steam Input, the pad's permissions?)", math.Hypot(mx, my))
			}
			along := mx*fx + my*fy
			if axis == 0 {
				along = mx*fy - my*fx
			}
			if math.Abs(along) < 0.6*math.Hypot(mx, my) {
				return "", fmt.Errorf("the left stick's %s moved V sideways of it (%.2f of %.2f m): the view direction or the stick is not what the pilot assumes", name, along, math.Hypot(mx, my))
			}
			r.moveSign[axis] = math.Copysign(1, along)
			out += fmt.Sprintf(" %s %+.1f m", name, along)
		}
	}
	if step.What == "all" || step.What == "look" {
		for axis, name := range []string{"yaw", "pitch"} {
			fx, fy, ok := r.forward()
			if !ok {
				return "", errors.New("the state has no view direction (fx fy)")
			}
			fz := r.last.N("fz")
			x, y := 0.0, 0.0
			if axis == 0 {
				x = 0.6
			} else {
				y = 0.6
			}
			if err := r.pad.Stick(pad.RightStick, x, y); err != nil {
				return "", err
			}
			err := r.sleep(0.5)
			_ = r.pad.Stick(pad.RightStick, 0, 0)
			if err == nil {
				err = r.sleep(0.3)
			}
			if err != nil {
				return "", err
			}
			gx, gy, _ := r.forward()
			// Stick right turns right (a negative left-angle); stick up looks up.
			turned := -leftOf(fx, fy, gx, gy)
			if axis == 1 {
				turned = math.Asin(clamp(r.last.N("fz"), -1, 1)) - math.Asin(clamp(fz, -1, 1))
			}
			if math.Abs(turned) < 2*math.Pi/180 {
				return "", fmt.Errorf("the right stick did not turn the view (%s %.1f°): the game does not read the pad", name, turned*180/math.Pi)
			}
			r.lookSign[axis] = math.Copysign(1, turned)
			out += fmt.Sprintf(" %s %+.0f°", name, turned*180/math.Pi)
			// And back, roughly.
			_ = r.pad.Stick(pad.RightStick, -x, -y)
			err = r.sleep(0.5)
			_ = r.pad.Stick(pad.RightStick, 0, 0)
			if err != nil {
				return "", err
			}
		}
	}
	if step.What == "steer" {
		if r.last.N("veh") != 1 || r.last.Str("seat") != "driver" {
			return "", errors.New("calibrate steer: V is not driving a vehicle")
		}
		hx, hy, ok := r.heading()
		if !ok {
			return "", errors.New("the state has no heading (hx hy)")
		}
		from, _ := r.last.Vec("x", "y", "z")
		_ = r.pad.Stick(pad.LeftStick, 0.6, 0)
		_ = r.pad.Trigger(pad.RT, 0.6)
		err := r.sleep(2.0)
		_ = r.pad.Stick(pad.LeftStick, 0, 0)
		_ = r.pad.Trigger(pad.RT, 0)
		_ = r.pad.Trigger(pad.LT, 1)
		if err == nil {
			err = r.sleep(1.5)
		}
		_ = r.pad.Trigger(pad.LT, 0)
		if err != nil {
			return "", err
		}
		to, _ := r.last.Vec("x", "y", "z")
		if math.Hypot(to[0]-from[0], to[1]-from[1]) < 1 {
			return "", errors.New("the right trigger did not move the car: the game does not read the pad")
		}
		gx, gy, _ := r.heading()
		turned := -leftOf(hx, hy, gx, gy)
		if math.Abs(turned) < 3*math.Pi/180 {
			return "", fmt.Errorf("the left stick did not turn the car (%.1f°)", turned*180/math.Pi)
		}
		r.steerSign = math.Copysign(1, turned)
		out += fmt.Sprintf(" steer %+.0f°", turned*180/math.Pi)
	}
	return ":" + out, nil
}

// choose picks a dialogue choice, the one thing no script can do: it waits
// for the choices, moves the selection with the d-pad until the state shows
// the wanted one selected, confirms with X and waits for the choices to go
// or change.
func (r *runner) choose(step *Step) (string, error) {
	timeout := step.Timeout
	if timeout == 0 {
		timeout = 20
	}
	limit := r.clock.Now().Add(time.Duration(timeout * float64(time.Second)))
	want := float64(step.Count - 1)
	presses := 0
	for {
		if r.clock.Now().After(limit) {
			return "", fmt.Errorf("not chosen within %.0f s: %d choices on screen, %d selected", timeout, int(r.last.N("choices")), int(r.last.N("sel"))+1)
		}
		if r.stale || r.last.N("choices") < float64(step.Count) {
			if err := r.tick(); err != nil {
				return "", err
			}
			continue
		}
		selected := r.last.N("sel")
		if selected == want {
			break
		}
		if presses++; presses > 24 {
			return "", fmt.Errorf("the selection does not follow the d-pad (at %d, want %d): the game does not read the pad, or the state's sel is not the dialogue's", int(selected)+1, step.Count)
		}
		button := pad.Down
		if selected > want {
			button = pad.Up
		}
		if err := r.pad.Button(button, true); err != nil {
			return "", err
		}
		err := r.sleep(0.1)
		_ = r.pad.Button(button, false)
		if err == nil {
			err = r.sleep(0.25)
		}
		if err != nil {
			return "", err
		}
	}
	before := r.last.N("choices")
	if err := r.pad.Button(pad.X, true); err != nil {
		return "", err
	}
	err := r.sleep(0.1)
	_ = r.pad.Button(pad.X, false)
	if err != nil {
		return "", err
	}
	for r.last.N("choices") == before && r.last.N("sel") == want {
		if r.clock.Now().After(limit) {
			return "", fmt.Errorf("choice %d confirmed with X, but the choices stayed on screen", step.Count)
		}
		if err := r.tick(); err != nil {
			return "", err
		}
	}
	return fmt.Sprintf(": choice %d of %d", step.Count, int(before)), nil
}
