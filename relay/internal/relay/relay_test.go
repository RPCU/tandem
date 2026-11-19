package relay

import (
	"bytes"
	"context"
	"io"
	"log/slog"
	"net"
	"net/netip"
	"strings"
	"testing"
	"time"
)

var (
	token  = [TokenSize]byte{1, 2, 3}
	secret = [SecretSize]byte{9, 9, 9}
	hostA  = netip.MustParseAddrPort("198.51.100.1:5000")
	guestA = netip.MustParseAddrPort("203.0.113.7:6000")
	guestB = netip.MustParseAddrPort("203.0.113.8:6000")
	nobody = netip.MustParseAddrPort("192.0.2.66:7000")
)

func newServer(t *testing.T) *Server {
	t.Helper()
	cfg := DefaultConfig()
	cfg.HostTokens = [][TokenSize]byte{token}
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func mustHandle(t *testing.T, s *Server, from netip.AddrPort, b []byte, now time.Time) Reply {
	t.Helper()
	out, ok := s.handle(from, b, now)
	return mustReply(t, out, ok)
}

func mustReply(t *testing.T, out outgoing, ok bool) Reply {
	t.Helper()
	if !ok {
		t.Fatal("expected an answer")
	}
	r, err := ParseReply(out.data)
	if err != nil {
		t.Fatal(err)
	}
	return r
}

func TestHostNeedsAToken(t *testing.T) {
	s := newServer(t)
	r := mustHandle(t, s, hostA, RegisterHost(secret, [TokenSize]byte{}), time.Now())
	if r.Error != ErrBadToken {
		t.Fatalf("got %+v", r)
	}
	if n, _ := s.Counts(); n != 0 {
		t.Fatal("a session opened without a token")
	}
}

func TestSessionForwardsBothWays(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	if r := mustHandle(t, s, hostA, RegisterHost(secret, token), now); !r.Registered || r.Channel != 0 {
		t.Fatalf("host: %+v", r)
	}
	ra := mustHandle(t, s, guestA, RegisterGuest(secret), now)
	rb := mustHandle(t, s, guestB, RegisterGuest(secret), now)
	if !ra.Registered || !rb.Registered || ra.Channel == rb.Channel || ra.Channel == 0 {
		t.Fatalf("guests: %+v %+v", ra, rb)
	}

	// Guest -> host, stamped with the guest's channel.
	out, ok := s.handle(guestA, Data(0, []byte("hello")), now)
	r := mustReply(t, out, ok)
	if out.to != hostA || r.Channel != ra.Channel || !bytes.Equal(r.Payload, []byte("hello")) {
		t.Fatalf("to host: %v %+v", out.to, r)
	}

	// Host -> the guest it names.
	out, ok = s.handle(hostA, Data(rb.Channel, []byte("hi b")), now)
	r = mustReply(t, out, ok)
	if out.to != guestB || !bytes.Equal(r.Payload, []byte("hi b")) {
		t.Fatalf("to guest: %v %+v", out.to, r)
	}
}

func TestStrangersGetNothing(t *testing.T) {
	s := newServer(t)
	if _, ok := s.handle(nobody, Data(1, []byte("x")), time.Now()); ok {
		t.Fatal("an unregistered address got an answer")
	}
	if _, ok := s.handle(nobody, []byte("garbage"), time.Now()); ok {
		t.Fatal("garbage got an answer")
	}
	r := mustHandle(t, s, nobody, RegisterGuest(secret), time.Now())
	if r.Error != ErrNoSession {
		t.Fatalf("got %+v", r)
	}
}

func TestNoAmplification(t *testing.T) {
	s := newServer(t)
	for _, req := range [][]byte{RegisterHost(secret, token), RegisterGuest(secret), RegisterGuest([SecretSize]byte{7})} {
		out, ok := s.handle(nobody, req, time.Now())
		if ok && len(out.data) > len(req) {
			t.Fatalf("answer of %d bytes to a %d-byte request", len(out.data), len(req))
		}
	}
}

func TestSessionFullAndTaken(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	s.handle(hostA, RegisterHost(secret, token), now)
	for i := range s.cfg.MaxGuests {
		addr := netip.AddrPortFrom(netip.MustParseAddr("203.0.113.20"), uint16(7000+i))
		if r := mustHandle(t, s, addr, RegisterGuest(secret), now); !r.Registered {
			t.Fatalf("guest %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, s, guestA, RegisterGuest(secret), now); r.Error != ErrSessionFull {
		t.Fatalf("guest past the limit: %+v", r)
	}
	if r := mustHandle(t, s, nobody, RegisterHost(secret, token), now); r.Error != ErrSessionTaken {
		t.Fatalf("second host: %+v", r)
	}
}

func TestIdleSessionsEnd(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	s.handle(hostA, RegisterHost(secret, token), now)
	s.handle(guestA, RegisterGuest(secret), now)
	s.sweep(now.Add(s.cfg.IdleTimeout + time.Second))
	if sessions, clients := s.Counts(); sessions != 0 || clients != 0 {
		t.Fatalf("left %d sessions, %d clients", sessions, clients)
	}
}

func TestRateLimit(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	s.handle(hostA, RegisterHost(secret, token), now)
	mustHandle(t, s, guestA, RegisterGuest(secret), now)
	forwarded := 0
	for range s.cfg.MaxPacketsPerSecond + 50 {
		if _, ok := s.handle(guestA, Data(0, []byte("x")), now); ok {
			forwarded++
		}
	}
	if forwarded != s.cfg.MaxPacketsPerSecond {
		t.Fatalf("forwarded %d", forwarded)
	}
	if _, ok := s.handle(guestA, Data(0, []byte("x")), now.Add(time.Second)); !ok {
		t.Fatal("still limited in the next second")
	}
}

func TestOverRealSockets(t *testing.T) {
	s := newServer(t)
	conn, err := net.ListenUDP("udp", net.UDPAddrFromAddrPort(netip.MustParseAddrPort("127.0.0.1:0")))
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go s.Serve(ctx, conn)
	relay := conn.LocalAddr().(*net.UDPAddr)

	dial := func() *net.UDPConn {
		c, err := net.DialUDP("udp", nil, relay)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		_ = c.SetDeadline(time.Now().Add(2 * time.Second))
		return c
	}
	read := func(c *net.UDPConn) Reply {
		buf := make([]byte, 2048)
		n, err := c.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		r, err := ParseReply(buf[:n])
		if err != nil {
			t.Fatal(err)
		}
		return r
	}

	host, guest := dial(), dial()
	host.Write(RegisterHost(secret, token))
	if r := read(host); !r.Registered {
		t.Fatalf("host: %+v", r)
	}
	guest.Write(RegisterGuest(secret))
	ch := read(guest).Channel
	guest.Write(Data(0, []byte("ping")))
	if r := read(host); r.Channel != ch || string(r.Payload) != "ping" {
		t.Fatalf("host got %+v", r)
	}
	host.Write(Data(ch, []byte("pong")))
	if r := read(guest); string(r.Payload) != "pong" {
		t.Fatalf("guest got %+v", r)
	}
}

// codeOf registers hostA's session and returns its join code.
func codeOf(t *testing.T, s *Server, now time.Time) string {
	t.Helper()
	if r := mustHandle(t, s, hostA, RegisterHost(secret, token), now); !r.Registered {
		t.Fatalf("host: %+v", r)
	}
	r := mustHandle(t, s, hostA, RequestCode(secret, false), now)
	if len(r.Code) != CodeSize || r.CodeLeft == 0 {
		t.Fatalf("code: %+v", r)
	}
	return r.Code
}

// otherThan is a well-formed code that is not code.
func otherThan(code string) string {
	if code == "ZZZZZZ" {
		return "YYYYYY"
	}
	return "ZZZZZZ"
}

func TestJoinByCode(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	code := codeOf(t, s, now)
	if again := mustHandle(t, s, hostA, RequestCode(secret, false), now); again.Code != code {
		t.Fatalf("asked again: %q, then %q", code, again.Code)
	}

	// Typed by hand: lower case, with a dash.
	typed := strings.ToLower(code[:3]) + "-" + code[3:]
	r := mustHandle(t, s, guestA, ResolveCode(typed), now)
	if !r.HasSecret || r.Secret != secret {
		t.Fatalf("resolve %q: %+v", typed, r)
	}
	if r := mustHandle(t, s, guestA, RegisterGuest(r.Secret), now); !r.Registered {
		t.Fatalf("register: %+v", r)
	}
	out, ok := s.handle(guestA, Data(0, []byte("hi")), now)
	if r := mustReply(t, out, ok); out.to != hostA || string(r.Payload) != "hi" {
		t.Fatalf("to host: %v %+v", out.to, r)
	}
}

func TestNormalizeCode(t *testing.T) {
	for typed, want := range map[string]string{
		"abc-def": "ABCDEF", "O1L-I00": "011100", " k7q 2m9 ": "K7Q2M9",
	} {
		if got, ok := NormalizeCode(typed); !ok || got != want {
			t.Errorf("%q: %q %v", typed, got, ok)
		}
	}
	for _, bad := range []string{"", "ABCDE", "ABCDEFG", "ABCDEU", "ABC_DE", "\xc3\x80BCDEF"} {
		if got, ok := NormalizeCode(bad); ok {
			t.Errorf("%q: accepted as %q", bad, got)
		}
	}
}

func TestOnlyTheHostGetsACode(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	s.handle(hostA, RegisterHost(secret, token), now)
	mustHandle(t, s, guestA, RegisterGuest(secret), now)
	for _, c := range []struct {
		from netip.AddrPort
		req  []byte
	}{
		{nobody, RequestCode(secret, false)},             // a stranger
		{guestA, RequestCode(secret, false)},             // a guest
		{hostA, RequestCode([SecretSize]byte{7}, false)}, // the host's address, another secret
		{nobody, RequestCode([SecretSize]byte{}, true)},  // a stranger, renewing
		{hostA, RequestCode(secret, false)[:63]},         // malformed
	} {
		if _, ok := s.handle(c.from, c.req, now); ok {
			t.Fatalf("%v got an answer", c.from)
		}
	}
}

func TestCodesRenewAndExpire(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	first := codeOf(t, s, now)

	// Renewing too soon keeps the code; later it replaces it.
	if r := mustHandle(t, s, hostA, RequestCode(secret, true), now.Add(time.Second)); r.Code != first {
		t.Fatalf("renewed at once: %+v", r)
	}
	later := now.Add(s.cfg.CodeRenewInterval)
	second := mustHandle(t, s, hostA, RequestCode(secret, true), later).Code
	if second == first {
		t.Fatal("renew gave the same code")
	}
	if r := mustHandle(t, s, guestA, ResolveCode(first), later); r.Error != ErrNoCode {
		t.Fatalf("old code: %+v", r)
	}

	// Expired: unknown to guests; the host's next request gets a new one.
	expired := later.Add(s.cfg.CodeTTL)
	if r := mustHandle(t, s, guestB, ResolveCode(second), expired); r.Error != ErrNoCode {
		t.Fatalf("expired code: %+v", r)
	}
	s.handle(hostA, Keepalive(), expired)
	s.sweep(expired)
	if r := mustHandle(t, s, hostA, RequestCode(secret, false), expired); r.Code == second || r.Code == "" {
		t.Fatalf("after expiry: %+v", r)
	}
}

func TestCodeDiesWithItsSession(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	code := codeOf(t, s, now)
	gone := now.Add(s.cfg.IdleTimeout + time.Second)
	s.sweep(gone)
	if r := mustHandle(t, s, guestA, ResolveCode(code), gone); r.Error != ErrNoCode {
		t.Fatalf("closed session's code: %+v", r)
	}
}

func TestCodeGuessingIsSlow(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	code := codeOf(t, s, now)
	wrong := otherThan(code)

	// One source: a burst of tries, told once it is over, then nothing; even
	// the right code waits for the refill.
	for i := range s.cfg.CodeLookupBurst {
		if r := mustHandle(t, s, nobody, ResolveCode(wrong), now); r.Error != ErrNoCode {
			t.Fatalf("try %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, s, nobody, ResolveCode(code), now); r.Error != ErrSlowDown {
		t.Fatalf("over the burst: %+v", r)
	}
	if _, ok := s.handle(nobody, ResolveCode(code), now); ok {
		t.Fatal("answered again while over the limit")
	}
	if r := mustHandle(t, s, nobody, ResolveCode(code), now.Add(s.cfg.CodeLookupInterval)); !r.HasSecret {
		t.Fatalf("after the refill: %+v", r)
	}

	// An IPv6 /64 is one source, whatever its addresses and ports.
	for i := range s.cfg.CodeLookupBurst + 1 {
		from := netip.AddrPortFrom(netip.AddrFrom16([16]byte{0x20, 0x01, 0x0d, 0xb8, 15: byte(i)}), uint16(9000+i))
		r := mustHandle(t, s, from, ResolveCode(wrong), now)
		if (i < s.cfg.CodeLookupBurst) != (r.Error == ErrNoCode) {
			t.Fatalf("v6 try %d: %+v", i, r)
		}
	}
}

func TestCodeFailuresFromEveryoneAreCapped(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	code := codeOf(t, s, now)
	source := func(i int) netip.AddrPort {
		return netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, byte(i >> 16), byte(i >> 8), byte(i)}), 5000)
	}
	for i := range s.cfg.MaxCodeFailuresPerMinute {
		if r := mustHandle(t, s, source(i), ResolveCode(otherThan(code)), now); r.Error != ErrNoCode {
			t.Fatalf("try %d: %+v", i, r)
		}
	}
	// Many sources together are as slow as the cap: the next lookup waits,
	// right code or not, until the minute is over.
	if r := mustHandle(t, s, source(1<<20), ResolveCode(code), now); r.Error != ErrSlowDown {
		t.Fatalf("past the cap: %+v", r)
	}
	if r := mustHandle(t, s, source(1<<21), ResolveCode(code), now.Add(time.Minute)); !r.HasSecret {
		t.Fatalf("next minute: %+v", r)
	}
}

func TestLookupSourcesAreBounded(t *testing.T) {
	s := newServer(t)
	s.cfg.MaxLookupSources = 3
	now := time.Now()
	for i := range 3 {
		mustHandle(t, s, netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 0, 0, byte(i)}), 1), ResolveCode("ZZZZZZ"), now)
	}
	if _, ok := s.handle(nobody, ResolveCode("ZZZZZZ"), now); ok {
		t.Fatal("a fourth source was answered")
	}
	later := now.Add(time.Duration(s.cfg.CodeLookupBurst) * s.cfg.CodeLookupInterval)
	s.sweep(later)
	if _, ok := s.handle(nobody, ResolveCode("ZZZZZZ"), later); !ok {
		t.Fatal("sources were not forgotten")
	}
}

func TestCodeRepliesAreSmall(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	code := codeOf(t, s, now)
	for _, c := range []struct {
		from netip.AddrPort
		req  []byte
	}{
		{hostA, RequestCode(secret, false)},
		{hostA, RequestCode(secret, true)},
		{nobody, ResolveCode(code)},
		{guestA, ResolveCode(otherThan(code))},
	} {
		out, ok := s.handle(c.from, c.req, now)
		if ok && len(out.data) > len(c.req) {
			t.Fatalf("answer of %d bytes to a %d-byte request", len(out.data), len(c.req))
		}
	}
}

func TestCodesOverRealSockets(t *testing.T) {
	s := newServer(t)
	conn, err := net.ListenUDP("udp", net.UDPAddrFromAddrPort(netip.MustParseAddrPort("127.0.0.1:0")))
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go s.Serve(ctx, conn)
	relay := conn.LocalAddr().(*net.UDPAddr)
	dial := func() *net.UDPConn {
		c, err := net.DialUDP("udp", nil, relay)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		_ = c.SetDeadline(time.Now().Add(2 * time.Second))
		return c
	}
	ask := func(c *net.UDPConn, req []byte) Reply {
		t.Helper()
		if _, err := c.Write(req); err != nil {
			t.Fatal(err)
		}
		buf := make([]byte, 2048)
		n, err := c.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		r, err := ParseReply(buf[:n])
		if err != nil {
			t.Fatal(err)
		}
		return r
	}
	host, guest := dial(), dial()
	ask(host, RegisterHost(secret, token))
	code := ask(host, RequestCode(secret, false)).Code
	got := ask(guest, ResolveCode(code))
	if !got.HasSecret || got.Secret != secret {
		t.Fatalf("resolve: %+v", got)
	}
	if r := ask(guest, RegisterGuest(got.Secret)); !r.Registered {
		t.Fatalf("register: %+v", r)
	}
}
