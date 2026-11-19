package relay

import (
	"bytes"
	"log/slog"
	"net/netip"
	"strings"
	"testing"
	"time"
)

// watchServer logs to a buffer, so the tests can count its warnings.
func watchServer(t *testing.T, edit func(*Config)) (*Server, *bytes.Buffer) {
	t.Helper()
	cfg := DefaultConfig()
	cfg.AllowAnyHost = true
	if edit != nil {
		edit(&cfg)
	}
	var logs bytes.Buffer
	return NewServer(cfg, slog.New(slog.NewTextHandler(&logs, nil))), &logs
}

func warnings(logs *bytes.Buffer) int { return strings.Count(logs.String(), "look NATed") }

func gaugeLine(s *Server) string {
	var b bytes.Buffer
	s.WriteMetrics(&b)
	for _, line := range strings.Split(b.String(), "\n") {
		if strings.HasPrefix(line, "tandem_relay_source_nat_suspected ") {
			return line
		}
	}
	return ""
}

// sessions registers n hosts with a guest each, at the addresses addr(i)
// gives (host 2i, guest 2i+1).
func sessions(t *testing.T, s *Server, n int, addr func(i int) netip.AddrPort, now time.Time) {
	t.Helper()
	for i := range n {
		sec := [SecretSize]byte{byte(i), 7}
		mustHandle(t, s, addr(2*i), RegisterHost(sec, token), now)
		mustHandle(t, s, addr(2*i+1), RegisterGuest(sec), now)
	}
}

// Behind a masquerade every player comes from the node's (or a gateway
// pod's) private address: warned about once a minute, and the gauge says so.
func TestNATedSourcesAreReported(t *testing.T) {
	s, logs := watchServer(t, nil)
	now := time.Now()
	sessions(t, s, 3, func(i int) netip.AddrPort {
		return netip.AddrPortFrom(netip.MustParseAddr("10.42.0.1"), uint16(40000+i))
	}, now)
	v := s.checkSources(now)
	if v.Reason != "internal" || v.Sources != 6 || v.Internal != 6 || v.Example != netip.MustParseAddr("10.42.0.1") {
		t.Fatalf("%+v", v)
	}
	if !s.SourceNATSuspected() || gaugeLine(s) != "tandem_relay_source_nat_suspected 1" || warnings(logs) != 1 {
		t.Fatalf("suspected %v, %q, %d warnings", s.SourceNATSuspected(), gaugeLine(s), warnings(logs))
	}
	// Once a minute, not at every check.
	s.checkSources(now.Add(10 * time.Second))
	if warnings(logs) != 1 {
		t.Fatalf("%d warnings within a minute", warnings(logs))
	}
	s.checkSources(now.Add(61 * time.Second))
	if warnings(logs) != 2 {
		t.Fatalf("%d warnings after a minute", warnings(logs))
	}
}

// Players from all over the internet: nothing to say.
func TestPublicSourcesAreFine(t *testing.T) {
	s, logs := watchServer(t, nil)
	now := time.Now()
	sessions(t, s, 8, func(i int) netip.AddrPort {
		return netip.AddrPortFrom(netip.AddrFrom4([4]byte{198, 51, 100, byte(i + 1)}), 5000)
	}, now)
	// A player on the operator's own network does not tip it.
	s.handle(netip.MustParseAddrPort("192.168.1.20:5000"), Probe(1), now)
	if v := s.checkSources(now); v.Reason != "" || v.Sources != 17 || v.Internal != 1 || v.Addresses != 3 {
		t.Fatalf("%+v", v)
	}
	if s.SourceNATSuspected() || gaugeLine(s) != "tandem_relay_source_nat_suspected 0" || warnings(logs) != 0 {
		t.Fatalf("suspected %v, %q, %d warnings", s.SourceNATSuspected(), gaugeLine(s), warnings(logs))
	}
}

// An SNAT to a public address (a gateway node's): everyone from one
// address, each on a port of its own. Probes and lookups count, registered
// or not.
func TestEveryoneFromOneAddress(t *testing.T) {
	s, logs := watchServer(t, nil)
	now := time.Now()
	gateway := netip.MustParseAddr("203.0.113.9")
	for i := range 6 {
		s.handle(netip.AddrPortFrom(gateway, uint16(30000+i)), Probe(uint32(i)), now)
	}
	s.handle(netip.AddrPortFrom(gateway, 30006), ResolveCode("K7Q2M9"), now)
	if v := s.checkSources(now); v.Reason != "" {
		t.Fatalf("judged on 7 sources: %+v", v)
	}
	sessions(t, s, 1, func(i int) netip.AddrPort { return netip.AddrPortFrom(gateway, uint16(31000+i)) }, now)
	if v := s.checkSources(now); v.Reason != "few" || v.Sources != 9 || v.Addresses != 1 || v.Internal != 0 {
		t.Fatalf("%+v", v)
	}
	if warnings(logs) != 1 {
		t.Fatalf("%d warnings", warnings(logs))
	}
}

// A window ends after a minute: sources that stopped coming are forgotten,
// and the gauge goes back to 0 once players are seen as they are.
func TestVerdictRecovers(t *testing.T) {
	s, _ := watchServer(t, nil)
	now := time.Now()
	for i := range 5 {
		s.handle(netip.AddrPortFrom(netip.MustParseAddr("100.64.3.4"), uint16(1000+i)), Probe(1), now)
	}
	if s.checkSources(now.Add(61*time.Second)).Reason != "internal" {
		t.Fatal("not suspected")
	}
	later := now.Add(70 * time.Second)
	sessions(t, s, 4, func(i int) netip.AddrPort {
		return netip.AddrPortFrom(netip.AddrFrom4([4]byte{203, 0, 113, byte(i + 1)}), 5000)
	}, later)
	if v := s.checkSources(later); v.Reason != "" || v.Sources != 8 || s.SourceNATSuspected() {
		t.Fatalf("%+v", v)
	}
}

// --internal-cidrs: a cluster's network outside the private ranges.
func TestOperatorsInternalPrefixes(t *testing.T) {
	s, _ := watchServer(t, func(c *Config) { c.InternalPrefixes = []netip.Prefix{netip.MustParsePrefix("198.18.0.0/15")} })
	now := time.Now()
	for i := range 4 {
		s.handle(netip.AddrPortFrom(netip.AddrFrom4([4]byte{198, 18, 0, byte(i)}), 5000), Probe(1), now)
	}
	if v := s.checkSources(now); v.Reason != "internal" || v.Internal != 4 {
		t.Fatalf("%+v", v)
	}
	for _, a := range []string{"127.0.0.1", "169.254.1.1", "fe80::1", "fd00::1", "::ffff:10.0.0.1", "100.100.1.1"} {
		if !s.internal(netip.MustParseAddr(a)) {
			t.Errorf("%s is not internal", a)
		}
	}
	for _, a := range []string{"198.51.100.1", "2001:db8::1", "::ffff:203.0.113.1"} {
		if s.internal(netip.MustParseAddr(a)) {
			t.Errorf("%s is internal", a)
		}
	}
}

// --expect-public-clients=false (a LAN, the tests on 127.0.0.1): no
// sampling, no verdict.
func TestSourceWatchOff(t *testing.T) {
	s, logs := watchServer(t, func(c *Config) { c.ExpectPublicClients = false })
	now := time.Now()
	sessions(t, s, 4, func(i int) netip.AddrPort {
		return netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(5000+i))
	}, now)
	if v := s.checkSources(now); v.Sources != 0 || s.SourceNATSuspected() || len(s.watch.seen) != 0 || warnings(logs) != 0 {
		t.Fatalf("%+v", v)
	}
}

// The data path never samples: a forwarded datagram allocates nothing and
// leaves the window alone.
func TestForwardDoesNotSample(t *testing.T) {
	s, _ := watchServer(t, nil)
	now := time.Now()
	mustHandle(t, s, hostA, RegisterHost(secret, token), now)
	r := mustHandle(t, s, guestA, RegisterGuest(secret), now)
	s.checkSources(now.Add(time.Minute)) // a new window
	up := Data(0, make([]byte, 200))
	down := Data(r.Channel, make([]byte, 200))
	w := newWorker(0)
	allocs := testing.AllocsPerRun(1000, func() {
		s.process(w, portDirect, guestA, up, now)
		binaryPutChannel(down, r.Channel)
		s.process(w, portDirect, hostA, down, now)
	})
	if allocs != 0 || len(s.watch.seen) != 0 {
		t.Fatalf("%v allocations, %d sources sampled", allocs, len(s.watch.seen))
	}
}
