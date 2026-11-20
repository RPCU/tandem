package relay

import (
	"io"
	"log/slog"
	"net/netip"
	"testing"
	"time"
)

var noToken [TokenSize]byte

// openServer: the default (open) relay, with a quick proof of work.
func openServer(t testing.TB) *Server {
	t.Helper()
	cfg := DefaultConfig()
	cfg.HostTokens = [][TokenSize]byte{token}
	cfg.PowBits = 8
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

// openHost registers a host with no token, as the client does: asked a
// challenge, it solves it.
func openHost(t *testing.T, s *Server, from netip.AddrPort, sec [SecretSize]byte, now time.Time) Reply {
	t.Helper()
	req := RegisterHost(sec, noToken)
	out, ok := s.handle(from, req, now)
	c := mustReply(t, out, ok)
	if !c.Challenged || c.Bits != byte(s.cfg.PowBits) || len(out.data) > len(req) {
		t.Fatalf("challenge: %+v (%d bytes)", c, len(out.data))
	}
	return mustHandle(t, s, from, RegisterHostOpen(sec, c, SolveChallenge(sec, c)), now)
}

func TestAnyoneHostsWithAProof(t *testing.T) {
	s := openServer(t)
	now := time.Now()
	if r := openHost(t, s, hostA, secret, now); !r.Registered {
		t.Fatalf("open host: %+v", r)
	}
	// Its refreshes and probes need no proof again.
	if r := mustHandle(t, s, hostA, RegisterHost(secret, noToken), now); !r.Registered {
		t.Fatalf("refresh: %+v", r)
	}
	code := mustHandle(t, s, hostA, RequestCode(secret, false), now).Code
	if r := mustHandle(t, s, guestA, ResolveCode(code), now); r.Secret != secret {
		t.Fatalf("resolve: %+v", r)
	}
	ch := mustHandle(t, s, guestA, RegisterGuest(secret), now).Channel
	out, ok := s.handle(guestA, Data(0, []byte("hi")), now)
	if r := mustReply(t, out, ok); out.to != hostA || r.Channel != ch {
		t.Fatalf("data: %+v", r)
	}
	if s.openCount() != 1 {
		t.Fatalf("%d open sessions", s.openCount())
	}
}

func TestProofsAreChecked(t *testing.T) {
	s := openServer(t)
	now := time.Now()
	out, _ := s.handle(hostA, RegisterHost(secret, noToken), now)
	c, _ := ParseReply(out.data)
	good := SolveChallenge(secret, c)
	other := [SecretSize]byte{7, 7}

	wrongNonce := good + 1
	for powHolds(c.MAC, secret, wrongNonce, c.Bits) {
		wrongNonce++
	}
	lowered := c
	lowered.Bits = 1
	for name, attempt := range map[string]struct {
		from netip.AddrPort
		req  []byte
		at   time.Time
	}{
		"wrong nonce":         {hostA, RegisterHostOpen(secret, c, wrongNonce), now},
		"another secret":      {hostA, RegisterHostOpen(other, c, good), now},
		"another address":     {guestB, RegisterHostOpen(secret, c, good), now},
		"stale":               {hostA, RegisterHostOpen(secret, c, good), now.Add(s.cfg.ChallengeTTL + 2*time.Second)},
		"easier than we ask":  {hostA, RegisterHostOpen(secret, lowered, SolveChallenge(secret, lowered)), now},
		"forged mac":          {hostA, RegisterHostOpen(secret, Reply{Stamp: c.Stamp, Bits: c.Bits}, 0), now},
		"from the future too": {hostA, RegisterHostOpen(secret, Reply{Stamp: c.Stamp + 60, Bits: c.Bits, MAC: c.MAC}, good), now},
	} {
		r := mustHandle(t, s, attempt.from, attempt.req, attempt.at)
		if r.Registered || !r.Challenged {
			t.Fatalf("%s: %+v", name, r)
		}
	}
	if n, _ := s.Counts(); n != 0 {
		t.Fatalf("%d sessions from bad proofs", n)
	}

	// The right one, once.
	if r := mustHandle(t, s, hostA, RegisterHostOpen(secret, c, good), now); !r.Registered {
		t.Fatalf("good proof: %+v", r)
	}
	// Replayed once the session is gone (the challenge still fresh):
	// refused, another challenge.
	s.forgetForTest(hostA)
	if r := mustHandle(t, s, hostA, RegisterHostOpen(secret, c, good), now.Add(time.Minute)); r.Registered || !r.Challenged {
		t.Fatalf("replayed: %+v", r)
	}
	if s.Metrics.replays.Load() == 0 {
		t.Fatal("the replay was not counted")
	}
}

func TestOpenSessionsPerSource(t *testing.T) {
	s := openServer(t)
	now := time.Now()
	ip := netip.MustParseAddr("198.51.100.30")
	for i := range s.cfg.MaxSessionsPerSource {
		if r := openHost(t, s, netip.AddrPortFrom(ip, uint16(5000+i)), [SecretSize]byte{byte(i), 30}, now); !r.Registered {
			t.Fatalf("session %d: %+v", i, r)
		}
	}
	if r := openHost(t, s, netip.AddrPortFrom(ip, 6000), [SecretSize]byte{99, 30}, now); r.Error != ErrSourceBusy {
		t.Fatalf("past the source's cap: %+v", r)
	}
	// An IPv6 /64 is one source.
	for i := range s.cfg.MaxSessionsPerSource + 1 {
		from := netip.AddrPortFrom(netip.AddrFrom16([16]byte{0x20, 0x01, 0x0d, 0xb8, 7: 1, 15: byte(i + 1)}), 7000)
		r := openHost(t, s, from, [SecretSize]byte{byte(i), 64}, now)
		if (i < s.cfg.MaxSessionsPerSource) != r.Registered {
			t.Fatalf("v6 session %d: %+v", i, r)
		}
	}
	// A token bypasses it.
	if r := mustHandle(t, s, netip.AddrPortFrom(ip, 6001), RegisterHost([SecretSize]byte{98, 30}, token), now); !r.Registered {
		t.Fatalf("token holder: %+v", r)
	}
	// A session that ends frees its place.
	s.forgetForTest(netip.AddrPortFrom(ip, 5000))
	if r := openHost(t, s, netip.AddrPortFrom(ip, 6002), [SecretSize]byte{97, 30}, now); !r.Registered {
		t.Fatalf("after one ended: %+v", r)
	}
}

func TestOpenSessionsInAll(t *testing.T) {
	s := openServer(t)
	s.cfg.MaxOpenSessions = 2
	now := time.Now()
	for i := range 2 {
		from := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 40, 0, byte(i)}), 1)
		if r := openHost(t, s, from, [SecretSize]byte{byte(i), 40}, now); !r.Registered {
			t.Fatalf("session %d: %+v", i, r)
		}
	}
	if r := openHost(t, s, nobody, [SecretSize]byte{9, 40}, now); r.Error != ErrTooManySession {
		t.Fatalf("past the cap: %+v", r)
	}
	if r := mustHandle(t, s, guestB, RegisterHost([SecretSize]byte{8, 40}, token), now); !r.Registered {
		t.Fatalf("token holder: %+v", r)
	}
}

func TestOpenSessionsBandwidth(t *testing.T) {
	s := openServer(t)
	now := time.Now()
	openHost(t, s, hostA, secret, now)
	ch := mustHandle(t, s, guestA, RegisterGuest(secret), now).Channel
	payload := make([]byte, 1000)
	count := func(from netip.AddrPort, req []byte, at time.Time, n int) int {
		passed := 0
		for range n {
			if _, ok := s.handle(from, req, at); ok {
				passed++
			}
		}
		return passed
	}
	up := Data(0, payload)
	// The burst, then nothing until the bucket refills.
	burst := s.cfg.OpenBurst / len(up)
	if got := count(guestA, up, now, 100); got != burst {
		t.Fatalf("up: %d passed, want the burst %d", got, burst)
	}
	// A second's worth, and what the burst left.
	again := (s.cfg.OpenBurst - burst*len(up) + s.cfg.OpenRate) / len(up)
	if got := count(guestA, up, now.Add(time.Second), 100); got != again {
		t.Fatalf("a second later: %d passed, want %d", got, again)
	}
	// The other way has its own.
	if got := count(hostA, Data(ch, payload), now, 100); got != burst {
		t.Fatalf("down: %d passed", got)
	}
	if s.Metrics.drops[dropBandwidth].Load() == 0 {
		t.Fatal("no bandwidth drop counted")
	}
	// A token holder's session is not capped.
	tokenSecret := [SecretSize]byte{6, 6}
	mustHandle(t, s, guestB, RegisterHost(tokenSecret, token), now)
	mustHandle(t, s, nobody, RegisterGuest(tokenSecret), now)
	if got := count(nobody, up, now, 100); got != 100 {
		t.Fatalf("token session: %d of 100", got)
	}
}

func TestRequireToken(t *testing.T) {
	s := openServer(t)
	s.cfg.Open = false
	r := mustHandle(t, s, hostA, RegisterHost(secret, noToken), time.Now())
	if r.Error != ErrBadToken {
		t.Fatalf("no token, token required: %+v", r)
	}
	if _, ok := s.handle(hostA, RegisterHostOpen(secret, Reply{}, 0), time.Now()); !ok {
		t.Fatal("no answer")
	}
}

// The relay's default difficulty costs a client about 50 ms (the C++
// client's own measure is in tests/relay_test.cpp).
func TestDefaultDifficultyCost(t *testing.T) {
	if testing.Short() {
		t.Skip("short")
	}
	c := Reply{Bits: byte(DefaultConfig().PowBits), MAC: [ChallengeMACSize]byte{1, 2, 3}}
	start := time.Now()
	for i := range 8 {
		SolveChallenge([SecretSize]byte{byte(i)}, c)
	}
	t.Logf("%d bits: %v a proof on average (Go)", c.Bits, time.Since(start)/8)
}

// The packet path of an open session (both buckets taken, never empty).
func BenchmarkForwardOpen(b *testing.B) {
	s := openServer(b)
	s.cfg.OpenRate, s.cfg.OpenBurst = 1<<40, 1<<40
	s.cfg.MaxPacketsPerSecond, s.cfg.MaxBytesPerSecond = 1<<30, 1<<30
	now := time.Now()
	out, _ := s.handle(hostA, RegisterHost(secret, noToken), now)
	c, _ := ParseReply(out.data)
	s.handle(hostA, RegisterHostOpen(secret, c, SolveChallenge(secret, c)), now)
	out, _ = s.handle(guestA, RegisterGuest(secret), now)
	r, _ := ParseReply(out.data)
	up := Data(0, make([]byte, 200))
	down := Data(r.Channel, make([]byte, 200))
	w := newWorker(0)
	b.ReportAllocs()
	b.ResetTimer()
	for i := range b.N {
		at := now.Add(time.Duration(i) * time.Microsecond)
		if i&1 == 0 {
			s.process(w, portDirect, guestA, up, at)
		} else {
			binaryPutChannel(down, r.Channel)
			s.process(w, portDirect, hostA, down, at)
		}
	}
}
