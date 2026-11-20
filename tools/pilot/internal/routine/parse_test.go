package routine

import (
	"strings"
	"testing"

	"github.com/RPCU/tandem/tools/pilot/internal/pad"
)

func TestParse(t *testing.T) {
	text := `# a routine
limit 90
needs veh == 0 and hp > 50
origin -915 1879.5 36.19
sim npc hostile 0 12

walk ~10 ~-2.5 within 2 pace sprint timeout 12   # a comment
walk target within 1.5
face 1 2 3 tolerance 5
drive ~0 ~60 speed 8
press a
press rb 0.4
hold lb rb
release all
stick right -0.5 0.25 2
trigger rt 1 0.5
act look target 2 timeout 6
cmd teleport home
until near ~10 ~0 3 and speed<0.2 timeout 8
check seat == driver
expect ^\[info\] hacks: our upload # not a comment timeout 4
reject watchdog: game thread silent
optional choose 2
if role == guest
  act target proxy
else
  repeat 3
    press x
  end
end
`
	r, err := Parse("t", strings.NewReader(text))
	if err != nil {
		t.Fatal(err)
	}
	if r.Limit != 90 || len(r.Needs) != 1 || len(r.Needs[0].Terms) != 2 || r.Origin == nil || r.Origin[1] != 1879.5 {
		t.Errorf("header: %+v", r)
	}
	if len(r.Fixtures) != 1 || r.Fixtures[0] != "npc hostile 0 12" {
		t.Errorf("fixtures: %q", r.Fixtures)
	}
	if len(r.Steps) != 18 {
		t.Fatalf("%d steps", len(r.Steps))
	}
	s := r.Steps
	walk := s[0]
	if walk.Kind != Walk || walk.Line != 7 || walk.Within != 2 || walk.Timeout != 12 || walk.Pace != "sprint" ||
		!walk.Target.At[0].Relative || walk.Target.At[0].Value != 10 || walk.Target.At[1].Value != -2.5 || walk.Target.HasZ {
		t.Errorf("walk: %+v", walk)
	}
	if !s[1].Target.Target || s[1].Within != 1.5 {
		t.Errorf("walk target: %+v", s[1])
	}
	if !s[2].Target.HasZ || s[2].Target.At[2].Value != 3 || s[2].Tolerance != 5 {
		t.Errorf("face: %+v", s[2])
	}
	if s[3].Speed != 8 {
		t.Errorf("drive: %+v", s[3])
	}
	if s[4].Seconds != 0.1 || s[5].Seconds != 0.4 || s[5].Buttons[0] != pad.RB || len(s[6].Buttons) != 2 || !s[7].All {
		t.Errorf("buttons: %+v %+v %+v %+v", s[4], s[5], s[6], s[7])
	}
	if s[8].Stick != pad.RightStick || s[8].X != -0.5 || s[8].Y != 0.25 || s[8].Seconds != 2 {
		t.Errorf("stick: %+v", s[8])
	}
	if s[9].Trig != pad.RT || s[9].Value != 1 || s[9].Seconds != 0.5 {
		t.Errorf("trigger: %+v", s[9])
	}
	if s[10].Text != "look target 2" || s[10].Timeout != 6 {
		t.Errorf("act: %+v", s[10])
	}
	until := s[12]
	if until.Timeout != 8 || len(until.Cond.Terms) != 2 || until.Cond.Terms[0].Near == nil || until.Cond.Terms[0].Radius != 3 ||
		until.Cond.Terms[0].Near.HasZ || until.Cond.Terms[1].Key != "speed" || until.Cond.Terms[1].Op != "<" || until.Cond.Terms[1].Number != 0.2 {
		t.Errorf("until: %+v", until.Cond)
	}
	if term := s[13].Cond.Terms[0]; !term.IsWord || term.Word != "driver" || term.Op != "==" {
		t.Errorf("check: %+v", term)
	}
	if s[14].Timeout != 4 || s[14].Text != `^\[info\] hacks: our upload # not a comment` {
		t.Errorf("expect: %q timeout %v", s[14].Text, s[14].Timeout)
	}
	if !s[16].Optional || s[16].Kind != Choose || s[16].Count != 2 {
		t.Errorf("choose: %+v", s[16])
	}
	branch := s[17]
	if branch.Kind != If || len(branch.Then) != 1 || len(branch.Else) != 1 || branch.Else[0].Count != 3 || branch.Else[0].Then[0].Line != 28 {
		t.Errorf("if: %+v", branch)
	}
}

func TestParseErrors(t *testing.T) {
	for text, want := range map[string]string{
		"jump":                         "unknown step",
		"show all":                     "show takes nothing",
		"mark here":                    "mark takes nothing",
		"press start":                  "no such button",
		"press guide":                  "no such button",
		"hold":                         "hold BUTTON",
		"press a 99":                   "at most 30",
		"stick left 2 0":               "from -1 to 1",
		"trigger rt 1.5":               "from 0 to 1",
		"walk":                         "a position is",
		"walk 1":                       "a position is",
		"walk 1 2 pace crawl":          "pace walk|jog|sprint",
		"walk 1 2 within -1":           "positive number",
		"walk 1 2 3 4":                 "unexpected: 4",
		"face 1 2 pace walk":           "pace is for walk",
		"wait":                         "wait SECONDS",
		"until":                        "a condition is missing",
		"until hp":                     "not a condition",
		"check seat > driver":          "== or != only",
		"check hp > 1 timeout 3":       "check is immediate",
		"check hp > 1 and":             "an empty condition",
		"expect (":                     "bad pattern",
		"cmd pilot on":                 "sent with act",
		"if veh == 1\nsay x":           "if without end",
		"repeat 0\nend":                "repeat COUNT",
		"end":                          "end without",
		"say a\nelse":                  "else without",
		"choose 0":                     "choose NUMBER",
		"limit -1":                     "limit SECONDS",
		"optional":                     "optional STEP",
		"optional if veh == 1\nend":    "makes no sense",
		"until near 1 2 timeout 3":     "near X Y",
		"walk ~x ~1":                   "not a coordinate",
		"sim":                          "sim FIXTURE",
		"calibrate everything":         "calibrate [move|look|steer]",
		"act":                          "act TEXT",
		"release":                      "release BUTTON",
		"walk 1 2 timeout 5 timeout 6": "given twice",
	} {
		_, err := Parse("t", strings.NewReader(text))
		if err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("%q: %v, want …%s…", text, err, want)
		}
	}
}
