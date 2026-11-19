package state

import "testing"

func TestParse(t *testing.T) {
	st, err := Parse("seq=42 t=10.25 pilot=1 ack=3 res=ok:asked x=-915.00 y=1879.50 z=36.19 seat=driver hp=NaN")
	if err != nil {
		t.Fatal(err)
	}
	if !st.Valid() || st.Seq() != 42 || st.N("x") != -915 || st.Str("res") != "ok:asked" || st.Str("seat") != "driver" {
		t.Errorf("state: %+v", st)
	}
	if _, ok := st.Num("seat"); ok {
		t.Error("a word read as a number")
	}
	if _, ok := st.Num("hp"); ok {
		t.Error("NaN read as a number")
	}
	if _, ok := st.Num("missing"); ok || st.Has("missing") || st.N("missing") != 0 {
		t.Error("a missing field")
	}
	if v, ok := st.Vec("x", "y", "z"); !ok || v != [3]float64{-915, 1879.5, 36.19} {
		t.Errorf("vec: %v %v", v, ok)
	}
	if _, ok := st.Vec("tx", "ty", "tz"); ok {
		t.Error("a vector without its fields")
	}
	if got := Of(map[string]string{"seq": "1", "b": "2", "a": "3"}).Line("seq"); got != "seq=1 a=3 b=2" {
		t.Errorf("line: %q", got)
	}
	for _, bad := range []string{"", "x=1 y=2", "seq=1 nonsense", "seq=x", "=3 seq=1"} {
		if _, err := Parse(bad); err == nil {
			t.Errorf("%q parsed", bad)
		}
	}
	if (State{}).Valid() {
		t.Error("the zero state is valid")
	}
}
