package relay

import (
	"context"
	"io"
	"log/slog"
	"net"
	"net/netip"
	"testing"
	"time"
)

var fleet = []netip.AddrPort{
	netip.MustParseAddrPort("192.0.2.10:27460"),
	netip.MustParseAddrPort("192.0.2.11:27460"),
	netip.MustParseAddrPort("[2001:db8::12]:27460"),
}

// replica is shard `shard` of a fleet of `n`.
func replica(t *testing.T, n, shard int) *Server {
	t.Helper()
	cfg := DefaultConfig()
	cfg.HostTokens = [][TokenSize]byte{token}
	cfg.Peers = fleet[:n]
	cfg.Shard = shard
	if err := cfg.Validate(); err != nil {
		t.Fatal(err)
	}
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

// codeOn is a well-formed code that shard `shard` of `n` owns.
func codeOn(n, shard int) string {
	for v := 0; ; v++ {
		var raw [CodeSize]byte
		for i := range raw {
			raw[CodeSize-1-i] = codeAlphabet[(v>>(5*i))&31]
		}
		if shardOf(string(raw[:]), n) == shard {
			return string(raw[:])
		}
	}
}

func TestReplicasIssueOnlyTheirOwnCodes(t *testing.T) {
	now := time.Now()
	for shard := range 3 {
		s := replica(t, 3, shard)
		for i := range 40 {
			sec := [SecretSize]byte{byte(i), 1}
			host := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 0, 1, byte(i)}), 5000)
			mustHandle(t, s, host, RegisterHost(sec, token), now)
			code := mustHandle(t, s, host, RequestCode(sec, false), now).Code
			if got := shardOf(code, 3); got != shard {
				t.Fatalf("shard %d issued %s, which shard %d owns", shard, code, got)
			}
		}
	}
}

func TestShardOfIsEven(t *testing.T) {
	// Every code has one owner, and each replica owns a third of the space,
	// so its live codes are as hard to hit as one relay's.
	counts := [3]int{}
	for v := range 1 << 15 {
		var raw [CodeSize]byte
		for i := range raw {
			raw[CodeSize-1-i] = codeAlphabet[(v*32749>>(5*i))&31]
		}
		counts[shardOf(string(raw[:]), 3)]++
	}
	for shard, n := range counts {
		if n < (1<<15)/3-200 || n > (1<<15)/3+200 {
			t.Fatalf("shard %d owns %d of %d", shard, n, 1<<15)
		}
	}
}

func TestResolveRedirectsToTheOwner(t *testing.T) {
	now := time.Now()
	a, b := replica(t, 2, 0), replica(t, 2, 1)
	// hostA's session is on b.
	mustHandle(t, b, hostA, RegisterHost(secret, token), now)
	code := mustHandle(t, b, hostA, RequestCode(secret, false), now).Code

	req := ResolveCode(code)
	out, ok := a.handle(guestA, req, now)
	r := mustReply(t, out, ok)
	if r.Redirect != fleet[1] {
		t.Fatalf("a answered %+v", r)
	}
	if len(out.data) > len(req) {
		t.Fatalf("a redirect of %d bytes to a %d-byte request", len(out.data), len(req))
	}
	// Followed: the owner answers with the secret, and never sends it back.
	if r := mustHandle(t, b, guestA, req, now); !r.HasSecret || r.Secret != secret {
		t.Fatalf("b answered %+v", r)
	}
	if r := mustHandle(t, b, guestB, ResolveCode(codeOn(2, 1)), now); r.Error != ErrNoCode {
		t.Fatalf("b, its own unknown code: %+v", r)
	}
	// A redirect is no failure: a's budget of failures is untouched.
	if a.failures != 0 {
		t.Fatalf("a counted %d failures", a.failures)
	}
}

func TestNoReplicaRedirectsToItself(t *testing.T) {
	now := time.Now()
	for shard := range 3 {
		s := replica(t, 3, shard)
		for v := range 60 {
			from := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 9, byte(shard), byte(v)}), 1)
			code := codeOn(3, v%3)
			r := mustHandle(t, s, from, ResolveCode(code), now)
			if r.Redirect.IsValid() && (r.Redirect == fleet[shard] || v%3 == shard) {
				t.Fatalf("shard %d sent %s to %v", shard, code, r.Redirect)
			}
		}
	}
}

// A client follows one redirect at most (core/src/relay.cpp): replicas that
// disagree on the fleet cannot bounce it around.
func TestMisconfiguredReplicasCannotLoop(t *testing.T) {
	now := time.Now()
	a := replica(t, 2, 0)
	// b believes the fleet is the other way round.
	cfg := DefaultConfig()
	cfg.Peers = []netip.AddrPort{fleet[1], fleet[0]}
	cfg.Shard = 0
	b := NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	servers := map[netip.AddrPort]*Server{fleet[0]: a, fleet[1]: b}

	code := codeOn(2, 1) // a says b owns it, b says a does
	at, hops := fleet[0], 0
	var last Reply
	for {
		last = mustHandle(t, servers[at], guestA, ResolveCode(code), now)
		if !last.Redirect.IsValid() || hops == 1 {
			break
		}
		at, hops = last.Redirect, hops+1
	}
	if hops != 1 || !last.Redirect.IsValid() {
		t.Fatalf("hops %d, last %+v", hops, last)
	}
}

func TestResolveFloodAcrossReplicasIsLimited(t *testing.T) {
	now := time.Now()
	a := replica(t, 2, 0)
	// One source: its burst on a is spent by redirects as by lookups.
	for i := range a.cfg.CodeLookupBurst {
		if r := mustHandle(t, a, nobody, ResolveCode(codeOn(2, 1)), now); !r.Redirect.IsValid() {
			t.Fatalf("try %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, a, nobody, ResolveCode(codeOn(2, 1)), now); r.Error != ErrSlowDown {
		t.Fatalf("over the burst: %+v", r)
	}
	if _, ok := a.handle(nobody, ResolveCode(codeOn(2, 1)), now); ok {
		t.Fatal("answered again while over the limit")
	}

	// Many sources: each replica takes its share of the fleet's failures,
	// so the fleet together fails no more often than one relay.
	if a.failCap != (a.cfg.MaxCodeFailuresPerMinute+1)/2 {
		t.Fatalf("a's share: %d", a.failCap)
	}
	for i := range a.failCap {
		from := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 7, byte(i >> 8), byte(i)}), 1)
		if r := mustHandle(t, a, from, ResolveCode(codeOn(2, 0)), now); r.Error != ErrNoCode {
			t.Fatalf("failure %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, a, guestA, ResolveCode(codeOn(2, 0)), now); r.Error != ErrSlowDown {
		t.Fatalf("past a's share: %+v", r)
	}
}

func TestTheEntranceOnlyRedirects(t *testing.T) {
	now := time.Now()
	a, b := replica(t, 2, 0), replica(t, 2, 1)
	ownCode := codeOf(t, a, now) // hostA on a
	mustHandle(t, b, guestB, RegisterHost([SecretSize]byte{5}, token), now)
	otherCode := mustHandle(t, b, guestB, RequestCode([SecretSize]byte{5}, false), now).Code

	for _, c := range []struct {
		name string
		req  []byte
		want netip.AddrPort
		err  ErrorCode
	}{
		{"new host", RegisterHost([SecretSize]byte{6}, token), fleet[0], 0},
		{"own code", ResolveCode(ownCode), fleet[0], 0},
		{"other's code", ResolveCode(otherCode), fleet[1], 0},
		{"own unknown code", ResolveCode(codeOn(2, 0)), netip.AddrPort{}, ErrNoCode},
		{"own session's secret", RegisterGuest(secret), fleet[0], 0},
		{"unknown secret", RegisterGuest([SecretSize]byte{8}), netip.AddrPort{}, ErrNoSession},
	} {
		out, ok := a.handleFront(nobody, c.req, now)
		r := mustReply(t, out, ok)
		if r.Redirect != c.want || r.Error != c.err || r.Registered || r.HasSecret {
			t.Fatalf("%s: %+v", c.name, r)
		}
		if len(out.data) > len(c.req) {
			t.Fatalf("%s: %d bytes to a %d-byte request", c.name, len(out.data), len(c.req))
		}
	}
	// Nothing registers there, and nothing else is answered.
	if n, _ := a.Counts(); n != 1 {
		t.Fatalf("%d sessions after the entrance", n)
	}
	for _, req := range [][]byte{Data(0, []byte("x")), RequestCode(secret, false), Keepalive(), ReclaimCode(secret, ownCode, 60)} {
		if _, ok := a.handleFront(hostA, req, now); ok {
			t.Fatalf("the entrance answered %x", req[:5])
		}
	}
}

func TestDrainSendsNewSessionsElsewhere(t *testing.T) {
	now := time.Now()
	a := replica(t, 3, 1)
	mustHandle(t, a, hostA, RegisterHost(secret, token), now)
	ch := mustHandle(t, a, guestA, RegisterGuest(secret), now).Channel
	a.Drain()

	// New sessions go to the next replica, from both ports.
	if r := mustHandle(t, a, nobody, RegisterHost([SecretSize]byte{3}, token), now); r.Redirect != fleet[2] {
		t.Fatalf("new host: %+v", r)
	}
	out, ok := a.handleFront(nobody, RegisterHost([SecretSize]byte{3}, token), now)
	if r := mustReply(t, out, ok); r.Redirect != fleet[2] {
		t.Fatalf("new host at the entrance: %+v", r)
	}
	// The running one goes on: its host's refresh, a new guest, data.
	if r := mustHandle(t, a, hostA, RegisterHost(secret, token), now); !r.Registered {
		t.Fatalf("refresh: %+v", r)
	}
	if r := mustHandle(t, a, guestB, RegisterGuest(secret), now); !r.Registered {
		t.Fatalf("a guest: %+v", r)
	}
	out, ok = a.handle(hostA, Data(ch, []byte("hi")), now)
	if r := mustReply(t, out, ok); out.to != guestA || string(r.Payload) != "hi" {
		t.Fatalf("data: %v %+v", out.to, r)
	}

	// A relay on its own has nowhere to send them.
	solo := newServer(t)
	solo.Drain()
	if r := mustHandle(t, solo, hostA, RegisterHost(secret, token), now); r.Error != ErrDraining {
		t.Fatalf("solo: %+v", r)
	}
}

func TestRestartGivesTheCodeAndChannelsBack(t *testing.T) {
	now := time.Now()
	before := replica(t, 2, 1)
	mustHandle(t, before, hostA, RegisterHost(secret, token), now)
	code := mustHandle(t, before, hostA, RequestCode(secret, false), now).Code
	mustHandle(t, before, guestA, RegisterGuest(secret), now)
	chB := mustHandle(t, before, guestB, RegisterGuest(secret), now).Channel

	// The same shard, started again: it knows nothing.
	later := now.Add(5 * time.Second)
	after := replica(t, 2, 1)
	after.started = later
	mustHandle(t, after, hostA, RegisterHost(secret, token), later)
	r := mustHandle(t, after, hostA, ReclaimCode(secret, code, 500), later)
	if r.Code != code || r.CodeLeft != 500 {
		t.Fatalf("reclaim: %+v", r)
	}
	// Asked again: the same code (already ours).
	if r := mustHandle(t, after, hostA, ReclaimCode(secret, code, 500), later); r.Code != code {
		t.Fatalf("again: %+v", r)
	}
	// guestB comes back first and keeps its channel; guestA asks for a
	// taken one and gets another.
	if r := mustHandle(t, after, guestB, RejoinGuest(secret, chB), later); r.Channel != chB {
		t.Fatalf("guestB: %+v", r)
	}
	if r := mustHandle(t, after, guestA, RejoinGuest(secret, chB), later); !r.Registered || r.Channel == chB {
		t.Fatalf("guestA: %+v", r)
	}
	if r := mustHandle(t, after, nobody, ResolveCode(code), later); r.Secret != secret {
		t.Fatalf("resolve: %+v", r)
	}
}

func TestReclaimIsRefusedWhenItCouldSteal(t *testing.T) {
	now := time.Now()
	s := replica(t, 2, 0)
	taken := codeOf(t, s, now) // hostA's
	other := [SecretSize]byte{4}
	mustHandle(t, s, guestA, RegisterHost(other, token), now)
	for _, c := range []struct {
		name string
		code string
		left uint16
		at   time.Time
	}{
		{"another session's code", taken, 300, now},
		{"another replica's code", codeOn(2, 1), 300, now},
		{"no time left", codeOn(2, 0), 0, now},
		{"long after the start", codeOn(2, 0), 300, s.started.Add(s.cfg.CodeTTL)},
		{"not a code", "!!!!!!", 300, now},
	} {
		got := mustHandle(t, s, guestA, ReclaimCode(other, c.code, c.left), c.at).Code
		if got == c.code || got == "" || shardOf(got, 2) != 0 {
			t.Fatalf("%s: got %q", c.name, got)
		}
		s.sweep(c.at)
	}
	// Only the session's host asks: anyone else gets nothing.
	if _, ok := s.handle(nobody, ReclaimCode(other, codeOn(2, 0), 300), now); ok {
		t.Fatal("a stranger got an answer")
	}
}

func TestNewRepliesAreSmall(t *testing.T) {
	now := time.Now()
	s := replica(t, 3, 0)
	code := codeOf(t, s, now)
	for _, c := range []struct {
		from netip.AddrPort
		req  []byte
	}{
		{nobody, ResolveCode(codeOn(3, 2))}, // a redirect to an IPv6 peer: 24 bytes
		{nobody, RejoinGuest(secret, 9)},
		{nobody, RejoinGuest([SecretSize]byte{1}, 9)},
		{hostA, ReclaimCode(secret, code, 100)},
		{hostA, ReclaimCode(secret, codeOn(3, 1), 100)},
	} {
		out, ok := s.handle(c.from, c.req, now)
		if ok && len(out.data) > len(c.req) {
			t.Fatalf("answer of %d bytes to a %d-byte request", len(out.data), len(c.req))
		}
	}
	s.Drain()
	req := RegisterHost([SecretSize]byte{2}, token)
	if out, ok := s.handle(nobody, req, now); !ok || len(out.data) > len(req) {
		t.Fatalf("draining: %v %d", ok, len(out.data))
	}
}

func TestConfigValidates(t *testing.T) {
	for _, c := range []Config{
		{Shard: 1},
		{Peers: fleet[:2], Shard: 2},
		{Peers: []netip.AddrPort{fleet[0], netip.MustParseAddrPort("0.0.0.0:1")}},
		{Peers: []netip.AddrPort{netip.MustParseAddrPort("192.0.2.1:0")}},
	} {
		if c.Validate() == nil {
			t.Errorf("%+v accepted", c)
		}
	}
}

// Several sockets on one port (SO_REUSEPORT): sessions work whichever one
// their clients land on.
func TestSeveralReaders(t *testing.T) {
	s := newServer(t)
	conns, err := Listen("127.0.0.1:0", 4)
	if err != nil {
		t.Fatal(err)
	}
	port := conns[0].LocalAddr().(*net.UDPAddr).Port
	for _, c := range conns {
		if c.LocalAddr().(*net.UDPAddr).Port != port {
			t.Fatalf("readers on %v and %v", conns[0].LocalAddr(), c.LocalAddr())
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go s.ServeAll(ctx, conns, nil)
	relay := conns[0].LocalAddr().(*net.UDPAddr)
	for i := range 6 {
		sec := [SecretSize]byte{byte(i), 3}
		host, guest := dialTest(t, relay), dialTest(t, relay)
		if r := ask(t, host, RegisterHost(sec, token)); !r.Registered {
			t.Fatalf("host %d: %+v", i, r)
		}
		ch := ask(t, guest, RegisterGuest(sec)).Channel
		_, _ = guest.Write(Data(0, []byte("ping")))
		if r := read(t, host); r.Channel != ch || string(r.Payload) != "ping" {
			t.Fatalf("pair %d: %+v", i, r)
		}
	}
}
