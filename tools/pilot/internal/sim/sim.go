// Package sim is a toy game for the pilot: enough of a world (a V that
// walks where the left stick points, a view the right stick turns, a car,
// NPCs, a door, a phone call, dialogue choices) and of the plugin's pilot
// (its state line, its actions and their answers, its heartbeat) to run a
// routine without the game. It is the pad, the game and the clock at once:
// time passes only when the interpreter sleeps, so a run takes milliseconds.
//
// It checks a routine's own logic and the interpreter's control loops; it
// says nothing about what the real game does.
package sim

import (
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/RPCU/tandem/tools/pilot/internal/pad"
	"github.com/RPCU/tandem/tools/pilot/internal/state"
)

// NPC is somebody in the toy world.
type NPC struct {
	ID      int
	Pos     [3]float64
	Health  float64
	Hostile bool
	Vendor  bool
	Dead    bool
}

// Sim is the toy world.
type Sim struct {
	// Echo, when set, gets every pad event and command (the dry run).
	Echo io.Writer

	// Knobs for tests.
	PadDead    bool // the game does not read the pad
	InvertLook bool // the camera's y axis is inverted
	Frozen     bool // the game stopped writing its state
	// Wall blocks walking through this box (min x, min y, max x, max y); a
	// jump clears it.
	Wall *[4]float64

	Pos    [3]float64
	Yaw    float64 // radians, counter-clockwise from +y
	Pitch  float64
	Health float64
	NPCs   []*NPC

	now      time.Time
	start    time.Time
	seq      int64
	pilot    bool
	beat     time.Time
	ack      int
	res      string
	saveLock bool

	buttons  [12]bool
	sticks   [2][2]float64
	triggers [2]float64

	sprint bool
	crouch bool
	// NoAuto: the game refuses AutoDrive (off the road).
	NoAuto   bool
	airUntil time.Time
	weapon   bool
	fireTo   time.Time
	shotAt   time.Time
	dilated  time.Time
	target   *NPC
	lookTo   time.Time
	lookAt   [3]float64
	lookNPC  bool
	session  bool
	down     bool
	downAt   time.Time
	takedown int

	carThere bool
	carPos   [3]float64
	carYaw   float64
	carSpeed float64
	inCar    bool
	mountAt  time.Time
	exitAt   time.Time
	auto     bool

	door     *[3]float64
	doorOpen bool
	lift     *[3]float64
	floor    int
	call     int
	choices  int
	sel      int

	log     []string
	logRead int
	later   []event
	// Commands is every command the game got, in order.
	Commands []string
	nextID   int
}

// Field is the test field's spot (tools/game.sh cmd teleport home).
var Field = [3]float64{-915.0, 1879.5, 36.19}

// New is a world with V on foot at the test field, looking along +y.
func New() *Sim {
	start := time.Unix(1_800_000_000, 0)
	return &Sim{Pos: Field, Health: 100, now: start, start: start, res: "none", nextID: 100}
}

type event struct {
	at time.Time
	do func()
}

// After runs do once the world is d older (tests: a freeze, a call).
func (s *Sim) After(d time.Duration, do func()) {
	s.later = append(s.later, event{s.now.Add(d), do})
}

// AtRest says whether nothing on the pad is held.
func (s *Sim) AtRest() bool {
	return s.buttons == [12]bool{} && s.sticks == [2][2]float64{} && s.triggers == [2]float64{}
}

// PilotOn says whether the plugin's pilot is on.
func (s *Sim) PilotOn() bool { return s.pilot }

// SetChoices puts dialogue choices on screen.
func (s *Sim) SetChoices(count int) { s.choices, s.sel = count, 0 }

// Now is the world's time.
func (s *Sim) Now() time.Time { return s.now }

// Sleep lets the world run for d.
func (s *Sim) Sleep(d time.Duration) {
	const dt = 10 * time.Millisecond
	for ; d > 0; d -= dt {
		for i := 0; i < len(s.later); i++ {
			if !s.later[i].at.After(s.now) {
				do := s.later[i].do
				s.later = append(s.later[:i], s.later[i+1:]...)
				i--
				do()
			}
		}
		s.step(dt.Seconds())
		s.now = s.now.Add(dt)
	}
}

func (s *Sim) echo(format string, args ...any) {
	if s.Echo != nil {
		fmt.Fprintf(s.Echo, "  "+format+"\n", args...)
	}
}

func (s *Sim) logf(format string, args ...any) {
	s.log = append(s.log, "[sim] "+fmt.Sprintf(format, args...))
}

func (s *Sim) forward() (float64, float64) { return -math.Sin(s.Yaw), math.Cos(s.Yaw) }

func (s *Sim) eye() [3]float64 { return [3]float64{s.Pos[0], s.Pos[1], s.Pos[2] + 1.6} }

func aimPoint(n *NPC) [3]float64 {
	up := 1.3
	if n.Dead {
		up = 0.3
	}
	return [3]float64{n.Pos[0], n.Pos[1], n.Pos[2] + up}
}

// lookTowards turns the view to a point at once.
func (s *Sim) lookTowards(to [3]float64) {
	eye := s.eye()
	dx, dy, dz := to[0]-eye[0], to[1]-eye[1], to[2]-eye[2]
	if math.Hypot(dx, dy) > 0.01 {
		s.Yaw = math.Atan2(-dx, dy)
	}
	s.Pitch = math.Atan2(dz, math.Hypot(dx, dy))
}

// offAxis is the angle between the view and a point, in radians.
func (s *Sim) offAxis(to [3]float64) float64 {
	eye := s.eye()
	fx, fy := s.forward()
	f := [3]float64{fx * math.Cos(s.Pitch), fy * math.Cos(s.Pitch), math.Sin(s.Pitch)}
	d := [3]float64{to[0] - eye[0], to[1] - eye[1], to[2] - eye[2]}
	m := math.Sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2])
	if m < 1e-6 {
		return 0
	}
	return math.Acos(math.Max(-1, math.Min(1, (f[0]*d[0]+f[1]*d[1]+f[2]*d[2])/m)))
}

func dist(a, b [3]float64) float64 { return math.Hypot(a[0]-b[0], a[1]-b[1]) }

func (s *Sim) step(dt float64) {
	if s.Frozen {
		return
	}
	s.seq++
	if s.pilot && s.now.Sub(s.beat) > 3*time.Second {
		s.pilot = false
		s.fireTo = time.Time{}
		s.lookTo = time.Time{}
		s.logf("pilot: off (no heartbeat for 3 s); saving stays locked")
	}
	if s.down && s.now.Sub(s.downAt) > 5*time.Second {
		s.down = false
		s.Health = 30
		s.logf("revive: our V is up again")
	}
	if !s.mountAt.IsZero() && !s.now.Before(s.mountAt) {
		s.mountAt = time.Time{}
		s.inCar = true
		s.Pos = s.carPos
		s.logf("vehicle: our V sits in seat_front_left")
	}
	if !s.exitAt.IsZero() && !s.now.Before(s.exitAt) {
		s.exitAt = time.Time{}
		s.inCar = false
		s.auto = false
		s.carSpeed = 0
		hx, hy := -math.Sin(s.carYaw), math.Cos(s.carYaw)
		s.Pos = [3]float64{s.carPos[0] - hy*1.5, s.carPos[1] + hx*1.5, s.carPos[2]}
		s.logf("vehicle: our V is out")
	}
	if s.lookTo.After(s.now) {
		if s.lookNPC && s.target != nil {
			s.lookAt = aimPoint(s.target)
		}
		s.lookTowards(s.lookAt)
	}
	left, right := s.sticks[pad.LeftStick], s.sticks[pad.RightStick]
	gas, brake := s.triggers[pad.RT], s.triggers[pad.LT]
	if s.PadDead || s.down {
		left, right, gas, brake = [2]float64{}, [2]float64{}, 0, 0
	}
	// The view.
	lookY := right[1]
	if s.InvertLook {
		lookY = -lookY
	}
	s.Yaw -= right[0] * 2.1 * dt
	s.Pitch = math.Max(-1.4, math.Min(1.4, s.Pitch+lookY*1.4*dt))

	if s.inCar {
		if s.auto {
			gas, brake, left = 0, 0, [2]float64{}
			s.carSpeed += (8 - s.carSpeed) * dt
		}
		s.carSpeed += gas*6*dt - brake*12*dt - 0.15*s.carSpeed*dt
		s.carSpeed = math.Max(0, s.carSpeed)
		s.carYaw -= left[0] * 0.9 * math.Min(s.carSpeed, 10) / 10 * dt
		hx, hy := -math.Sin(s.carYaw), math.Cos(s.carYaw)
		s.carPos[0] += hx * s.carSpeed * dt
		s.carPos[1] += hy * s.carSpeed * dt
		s.Pos = s.carPos
		return
	}
	// On foot.
	size := math.Hypot(left[0], left[1])
	if size < 0.1 {
		s.sprint = false
	} else if s.mountAt.IsZero() && s.takedown == 0 {
		speed := 3.5
		switch {
		case s.sprint:
			speed = 6.5
		case size < 0.5:
			speed = 1.6
		}
		fx, fy := s.forward()
		rx, ry := fy, -fx
		nx := s.Pos[0] + (rx*left[0]+fx*left[1])/size*speed*dt
		ny := s.Pos[1] + (ry*left[0]+fy*left[1])/size*speed*dt
		blocked := s.Wall != nil && nx > s.Wall[0] && nx < s.Wall[2] && ny > s.Wall[1] && ny < s.Wall[3] && !s.airUntil.After(s.now)
		if !blocked {
			s.Pos[0], s.Pos[1] = nx, ny
		}
	}
	// Firing: a shot every 0.15 s, a hit when the view is on the target.
	if s.fireTo.After(s.now) && s.weapon {
		if s.now.Sub(s.shotAt) >= 150*time.Millisecond {
			s.shotAt = s.now
			hit := s.target != nil && !s.target.Dead && s.offAxis(aimPoint(s.target)) < 0.07
			if hit {
				s.hurt(s.target, 34)
			}
			s.logf("shot: our V fired (hit %v)", hit)
		}
	}
}

func (s *Sim) hurt(n *NPC, amount float64) {
	n.Health -= amount
	if n.Health <= 0 && !n.Dead {
		n.Health = 0
		n.Dead = true
		s.logf("npc %d: dead", n.ID)
	}
}

// ---- the pad ----

// Button takes a press or a release.
func (s *Sim) Button(b pad.Button, down bool) error {
	if down {
		s.echo("pad: %s down", b)
	} else {
		s.echo("pad: %s up", b)
	}
	was := s.buttons[b]
	s.buttons[b] = down
	if !down || was || s.PadDead {
		return nil
	}
	switch b {
	case pad.A:
		if !s.inCar {
			s.airUntil = s.now.Add(700 * time.Millisecond)
		}
	case pad.L3:
		if math.Hypot(s.sticks[pad.LeftStick][0], s.sticks[pad.LeftStick][1]) > 0.5 {
			s.sprint = true
		}
	case pad.Up:
		if s.choices > 0 && s.sel > 0 {
			s.sel--
		}
	case pad.Down:
		if s.choices > 0 && s.sel < s.choices-1 {
			s.sel++
		}
	case pad.X:
		if s.choices > 0 {
			s.logf("story: choice %d picked", s.sel+1)
			s.choices, s.sel = 0, 0
		}
	case pad.Y:
		s.weapon = !s.weapon
	case pad.B:
		if !s.inCar {
			s.crouch = !s.crouch
		}
	}
	return nil
}

// Stick takes a stick position.
func (s *Sim) Stick(st pad.Stick, x, y float64) error {
	s.echo("pad: %s stick %+.2f %+.2f", st, x, y)
	s.sticks[st] = [2]float64{x, y}
	return nil
}

// Trigger takes a trigger position.
func (s *Sim) Trigger(t pad.Trigger, value float64) error {
	s.echo("pad: %s %.2f", t, value)
	s.triggers[t] = value
	if t == pad.RT && !s.inCar && !s.PadDead && value > 0.5 && s.weapon {
		s.fireTo = s.now.Add(200 * time.Millisecond)
	}
	return nil
}

// Close does nothing.
func (s *Sim) Close() error { return nil }

// ---- the game ----

// LogLines are the log lines since the last call.
func (s *Sim) LogLines() []string {
	lines := s.log[s.logRead:]
	s.logRead = len(s.log)
	return lines
}

func f2(v float64) string { return strconv.FormatFloat(v, 'f', 2, 64) }
func f4(v float64) string { return strconv.FormatFloat(v, 'f', 4, 64) }
func b01(v bool) string {
	if v {
		return "1"
	}
	return "0"
}

// State is the pilot state, as scripts/tandem/Pilot.reds writes it; none
// while the plugin's pilot is off.
func (s *Sim) State() (state.State, bool) {
	if !s.pilot {
		return state.State{}, false
	}
	fx, fy := s.forward()
	eye := s.eye()
	hx, hy, speed, seat := fx, fy, 0.0, "none"
	if s.inCar {
		hx, hy, speed, seat = -math.Sin(s.carYaw), math.Cos(s.carYaw), s.carSpeed, "driver"
	} else if size := math.Hypot(s.sticks[0][0], s.sticks[0][1]); size > 0.1 && !s.PadDead {
		speed = 3.5
		if s.sprint {
			speed = 6.5
		}
	}
	hostile, alive := 0, 0
	for _, n := range s.NPCs {
		if !n.Dead {
			alive++
			if n.Hostile {
				hostile++
			}
		}
	}
	role, car := "none", "-1"
	if s.session {
		role = "host"
	}
	if s.carThere {
		car = f2(dist(s.Pos, s.carPos))
	}
	fields := map[string]string{
		"seq": strconv.FormatInt(s.seq, 10), "t": f2(s.now.Sub(s.start).Seconds()), "pilot": "1",
		"ack": strconv.Itoa(s.ack), "res": s.res,
		"x": f2(s.Pos[0]), "y": f2(s.Pos[1]), "z": f2(s.Pos[2]),
		"fx": f4(fx * math.Cos(s.Pitch)), "fy": f4(fy * math.Cos(s.Pitch)), "fz": f4(math.Sin(s.Pitch)),
		"cx": f2(eye[0]), "cy": f2(eye[1]), "cz": f2(eye[2]),
		"hx": f4(hx), "hy": f4(hy), "speed": f2(speed),
		"veh": b01(s.inCar), "seat": seat, "hp": f2(s.Health),
		"crouch": b01(s.crouch), "sprint": b01(s.sprint), "air": b01(s.airUntil.After(s.now)), "swim": "0",
		"combat": b01(hostile > 0), "ads": b01(s.triggers[pad.LT] > 0.5 && !s.inCar), "tier": "0",
		"takedown": strconv.Itoa(s.takedown), "carry": "0", "dead": "0", "down": b01(s.down),
		"weapon": b01(s.weapon), "firing": b01(s.fireTo.After(s.now)), "dilated": b01(s.dilated.After(s.now)),
		"menu": "0", "call": strconv.Itoa(s.call), "choices": strconv.Itoa(s.choices), "sel": strconv.Itoa(s.sel),
		"prompt": b01(s.door != nil && dist(s.Pos, *s.door) < 3), "auto": b01(s.auto), "autoavail": b01(s.inCar),
		"savelock": b01(s.saveLock), "session": b01(s.session), "role": role, "car": car,
		"npcs": strconv.Itoa(alive), "hostile": strconv.Itoa(hostile), "tid": "0",
		"door": b01(s.doorOpen), "floor": strconv.Itoa(s.floor),
	}
	if s.target != nil {
		aim := aimPoint(s.target)
		fields["tid"] = strconv.Itoa(s.target.ID)
		fields["tx"], fields["ty"], fields["tz"] = f2(aim[0]), f2(aim[1]), f2(aim[2])
		fields["td"] = f2(dist(s.Pos, s.target.Pos))
		fields["thp"] = f2(s.target.Health)
		fields["tdead"] = b01(s.target.Dead)
	}
	return state.Of(fields), true
}

// ahead is a point DX to the right and DY ahead of V, as it looks now.
func (s *Sim) ahead(dx, dy float64) [3]float64 {
	fx, fy := s.forward()
	return [3]float64{s.Pos[0] + fy*dx + fx*dy, s.Pos[1] - fx*dx + fy*dy, s.Pos[2]}
}

func (s *Sim) spawn(hostile bool, at [3]float64) *NPC {
	s.nextID++
	n := &NPC{ID: s.nextID, Pos: at, Health: 100, Hostile: hostile}
	s.NPCs = append(s.NPCs, n)
	return n
}

// Command takes a tandem command.
func (s *Sim) Command(text string) error {
	if text != "pilot beat" {
		s.echo("cmd: %s", text)
	}
	s.Commands = append(s.Commands, text)
	if s.Frozen {
		return nil
	}
	words := strings.Fields(text)
	if len(words) == 0 {
		return nil
	}
	num := func(i int, fallback float64) float64 {
		if i < len(words) {
			if v, err := strconv.ParseFloat(words[i], 64); err == nil {
				return v
			}
		}
		return fallback
	}
	switch words[0] {
	case "pilot":
		s.pilotCommand(words, text)
	case "teleport":
		if len(words) >= 4 {
			s.Pos = [3]float64{num(1, 0), num(2, 0), num(3, 0)}
			s.Yaw = num(4, 0) * math.Pi / 180
		} else {
			s.Pos, s.Yaw = Field, 0
		}
		s.Pitch = 0
		s.logf("teleport: (%.1f, %.1f, %.1f) yaw %.0f", s.Pos[0], s.Pos[1], s.Pos[2], s.Yaw*180/math.Pi)
	case "test":
		s.Pos, s.Yaw, s.Pitch, s.Health = Field, 0, 0, 100
		s.inCar, s.carThere, s.auto, s.down, s.weapon = false, false, false, false, false
		s.NPCs, s.target = nil, nil
		s.pilot = false
		s.logf("test reset: ready (on foot, at the test field)")
	case "npc":
		if len(words) >= 2 && words[1] == "spawn" {
			count := int(num(2, 3))
			for i := 0; i < count; i++ {
				s.spawn(false, s.ahead((float64(i)-float64(count-1)/2)*2, 6))
			}
			s.logf("npc spawn: %d gangers ahead", count)
		} else if len(words) >= 2 && words[1] == "hostile" {
			s.spawn(true, s.ahead(0, 12))
			s.logf("npc hostile: spawning (true)")
		}
	case "record":
		s.logf("%s: asked", text)
	case "story":
		if len(words) >= 3 && words[1] == "call" {
			s.call = 1
			if words[2] == "end" {
				s.call = 0
			}
			s.logf("story: call %s queued", strings.Join(words[2:], " "))
		}
	case "god":
		s.logf("%s", text)
	default:
		s.logf("command: %s", text)
	}
	return nil
}

// Setup takes a routine's "sim ..." line: the fixtures of its dry run.
//
//	sim npc hostile|neutral|vendor DX DY   somebody DX right, DY ahead of V
//	sim door DX DY | sim lift DX DY        a device there
//	sim car DX DY                          V's car, parked there
//	sim call | sim choices N | sim session (another V is in the session)
//	sim wall X0 Y0 X1 Y1                   a low wall, relative to V
func (s *Sim) Setup(text string) error {
	words := strings.Fields(text)
	num := func(i int) float64 {
		if i < len(words) {
			v, _ := strconv.ParseFloat(words[i], 64)
			return v
		}
		return 0
	}
	if len(words) == 0 {
		return fmt.Errorf("sim what?")
	}
	switch words[0] {
	case "npc":
		if len(words) != 4 {
			return fmt.Errorf("sim npc hostile|neutral|vendor DX DY")
		}
		n := s.spawn(words[1] == "hostile", s.ahead(num(2), num(3)))
		n.Vendor = words[1] == "vendor"
	case "door":
		at := s.ahead(num(1), num(2))
		s.door = &at
	case "lift":
		at := s.ahead(num(1), num(2))
		s.lift = &at
	case "car":
		s.carThere, s.carPos, s.carYaw, s.carSpeed = true, s.ahead(num(1), num(2)), s.Yaw, 0
	case "call":
		s.call = 1
	case "choices":
		s.choices, s.sel = int(num(1)), 0
	case "session":
		s.session = true
	case "wall":
		s.Wall = &[4]float64{s.Pos[0] + num(1), s.Pos[1] + num(2), s.Pos[0] + num(3), s.Pos[1] + num(4)}
	default:
		return fmt.Errorf("sim: unknown fixture %q", words[0])
	}
	return nil
}

func (s *Sim) pilotCommand(words []string, text string) {
	if len(words) < 2 {
		return
	}
	switch words[1] {
	case "on":
		s.beat = s.now
		s.saveLock = true
		if !s.pilot {
			s.pilot = true
			s.logf("pilot: on (saving locked)")
		}
		return
	case "off":
		if s.pilot {
			s.pilot = false
			s.fireTo, s.lookTo = time.Time{}, time.Time{}
			s.logf("pilot: off (asked); saving stays locked")
		}
		return
	case "beat":
		if s.pilot {
			s.beat = s.now
		}
		return
	case "probe":
		s.logf("pilot probe: functional tests: FunctionalTestsGameSystem=absent")
		return
	}
	id, err := strconv.Atoi(words[1])
	if err != nil || len(words) < 3 || !s.pilot {
		s.logf("pilot: ignored: %s", text)
		return
	}
	s.beat = s.now
	s.ack = id
	s.res = s.act(words[2], words[3:])
	s.logf("pilot: %s -> %s", strings.Join(words[2:], " "), s.res)
}

// view is the device or car V looks at: within 10 m and 20° of the view.
func (s *Sim) inView(at *[3]float64, reach float64) bool {
	if at == nil || dist(s.Pos, *at) > reach {
		return false
	}
	to := *at
	to[2] += 1.2
	return s.offAxis(to) < 0.35
}

// act does a script-level action and answers as Pilot.reds does.
func (s *Sim) act(action string, args []string) string {
	arg := func(i int) string {
		if i < len(args) {
			return args[i]
		}
		return ""
	}
	num := func(i int, fallback float64) float64 {
		if v, err := strconv.ParseFloat(arg(i), 64); err == nil {
			return v
		}
		return fallback
	}
	switch action {
	case "target":
		s.target = nil
		kind := arg(0)
		if kind == "none" {
			return "ok"
		}
		if kind != "nearest" && kind != "hostile" && kind != "view" && kind != "proxy" {
			return "fail:target_nearest|hostile|proxy|view|none"
		}
		best := num(1, 30) + 1
		for _, n := range s.NPCs {
			if n.Dead && kind != "view" {
				continue
			}
			if kind == "hostile" && !n.Hostile {
				continue
			}
			if kind == "view" && s.offAxis(aimPoint(n)) > 0.2 {
				continue
			}
			if d := dist(s.Pos, n.Pos); d < best {
				best, s.target = d, n
			}
		}
		if s.target == nil {
			return "fail:nothing_found"
		}
		return fmt.Sprintf("ok:NPCPuppet_%.2fm", best)
	case "look":
		seconds := 1.0
		switch {
		case arg(0) == "stop":
			s.lookTo = time.Time{}
			return "ok"
		case arg(0) == "target":
			if s.target == nil {
				return "fail:no_target"
			}
			s.lookNPC = true
			seconds = num(1, 1)
		case len(args) >= 3:
			s.lookNPC = false
			s.lookAt = [3]float64{num(0, 0), num(1, 0), num(2, 0)}
			seconds = num(3, 1)
		default:
			return "fail:look_target|X_Y_Z|stop"
		}
		s.lookTo = s.now.Add(time.Duration(seconds * float64(time.Second)))
		return "ok"
	case "equip":
		switch arg(0) {
		case "weapon", "slot1", "slot2", "slot3", "slot4":
			s.weapon = true
		case "fists", "holster":
			s.weapon = false
		default:
			return "fail:equip_weapon|slot1..slot4|fists|holster"
		}
		return "ok:asked"
	case "fire":
		if arg(0) == "stop" {
			s.fireTo = time.Time{}
			return "ok"
		}
		if !s.weapon {
			return "fail:no_weapon_in_hand"
		}
		s.fireTo = s.now.Add(time.Duration(num(0, 0.4) * float64(time.Second)))
		return "ok"
	case "reload":
		if !s.weapon {
			return "fail:no_weapon_in_hand"
		}
		return "ok"
	case "ability":
		if arg(0) == "off" {
			s.dilated = time.Time{}
			return "ok"
		}
		s.dilated = s.now.Add(8 * time.Second)
		return "ok:asked"
	case "heal":
		s.Health = 100
		return "ok"
	case "hurt":
		if arg(0) == "all" {
			if !s.session {
				return "fail:nobody_to_revive_V_it_would_die"
			}
			s.Health, s.down, s.downAt = 0, true, s.now
			s.logf("revive: our V is down")
			return "ok"
		}
		percent := num(0, 0)
		if percent <= 0 || percent >= 100 {
			return "fail:hurt_PERCENT|all"
		}
		s.Health = math.Max(1, s.Health-percent)
		return "ok"
	case "hack":
		if s.target == nil {
			return "fail:no_npc_target"
		}
		if s.target.Dead {
			return "fail:target_dead"
		}
		s.hurt(s.target, 30)
		s.logf("hacks: our upload on npc %d", s.target.ID)
		return "ok:asked"
	case "takedown":
		if s.target == nil {
			return "fail:no_npc_target"
		}
		if s.target.Dead {
			return "fail:target_down"
		}
		if dist(s.Pos, s.target.Pos) > 3.5 {
			return "fail:too_far"
		}
		switch arg(0) {
		case "grab":
			s.takedown = 1
			s.logf("takedowns: our grab on %d", s.target.ID)
		case "kill", "nonlethal":
			s.takedown = 0
			s.hurt(s.target, 1000)
			s.logf("takedowns: our %s on %d", arg(0), s.target.ID)
		default:
			return "fail:takedown_grab|kill|nonlethal"
		}
		return "ok:asked"
	case "summon":
		s.carThere, s.carPos, s.carYaw, s.carSpeed = true, s.ahead(3, 5), s.Yaw, 0
		return "ok:asked"
	case "mount":
		if s.inCar {
			return "fail:already_mounted"
		}
		if !s.carThere || dist(s.Pos, s.carPos) > 15 {
			return "fail:no_vehicle_in_view_or_called"
		}
		s.mountAt = s.now.Add(1500 * time.Millisecond)
		return "ok:seat_front_left"
	case "exit":
		if !s.inCar {
			return "fail:not_in_a_vehicle"
		}
		s.exitAt = s.now.Add(time.Second)
		return "ok:asked"
	case "autodrive":
		switch {
		case arg(0) == "off":
			s.auto = false
		case !s.inCar:
			return "fail:not_driving"
		case s.NoAuto:
			return "fail:not_available_here"
		default:
			s.auto = true
		}
		return "ok:asked"
	case "device":
		if !s.inView(s.door, 4) {
			return "fail:no_device_in_view"
		}
		switch arg(0) {
		case "":
			return "ok:Door:ToggleOpen"
		case "ToggleOpen":
			s.doorOpen = !s.doorOpen
			return "ok:ToggleOpen"
		}
		return "fail:Door_offers:ToggleOpen"
	case "lift":
		if !s.inView(s.lift, 4) {
			return "fail:no_lift_in_view"
		}
		floor := int(num(0, -1))
		if floor < 0 || floor > 3 {
			return fmt.Sprintf("fail:floors_0_to_3_now_%d", s.floor)
		}
		from := s.floor
		s.floor = floor
		s.Pos[2] += float64(floor-from) * 4
		s.lift[2] = s.Pos[2]
		return fmt.Sprintf("ok:from_%d", from)
	case "call":
		if arg(0) != "answer" && arg(0) != "reject" {
			return "fail:call_answer|reject"
		}
		if s.call != 1 {
			return "fail:no_incoming_call"
		}
		s.call = 0
		if arg(0) == "answer" {
			s.call = 2
		}
		return "ok"
	case "loot":
		if s.target == nil {
			return "fail:no_target_and_nothing_in_view"
		}
		if dist(s.Pos, s.target.Pos) > 4 {
			return "fail:too_far"
		}
		if !s.target.Dead {
			return "fail:target_alive"
		}
		return "ok"
	case "vendor":
		if s.target == nil || !s.target.Vendor {
			return "fail:nothing_V_can_buy"
		}
		if arg(0) == "list" {
			return "ok:12_items"
		}
		if arg(0) != "buy" {
			return "fail:vendor_list|buy"
		}
		return "ok:Items.FirstAidWhiffV0"
	}
	return "fail:unknown_action"
}
