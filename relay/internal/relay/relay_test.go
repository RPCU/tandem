package relay

import (
	"bytes"
	"context"
	"io"
	"log/slog"
	"net"
	"net/netip"
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
	for i := range 3 {
		addr := netip.AddrPortFrom(netip.MustParseAddr("203.0.113.20"), uint16(7000+i))
		if r := mustHandle(t, s, addr, RegisterGuest(secret), now); !r.Registered {
			t.Fatalf("guest %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, s, guestA, RegisterGuest(secret), now); r.Error != ErrSessionFull {
		t.Fatalf("fourth guest: %+v", r)
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
