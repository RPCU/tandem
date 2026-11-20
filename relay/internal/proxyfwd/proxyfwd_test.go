package proxyfwd

import (
	"context"
	"io"
	"log/slog"
	"net"
	"net/netip"
	"testing"
	"time"

	"github.com/RPCU/tandem/relay/internal/relay"
)

var (
	secret = [relay.SecretSize]byte{4, 2}
	token  = [relay.TokenSize]byte{1}
)

// A session through a forwarder that adds PROXY v2 headers, over real
// sockets: the relay sees the clients' own addresses (Observed), answers the
// forwarder's sockets, and takes nothing from anyone else.
func TestSessionsThroughAProxy(t *testing.T) {
	for _, every := range []bool{false, true} {
		t.Run(map[bool]string{false: "header once", true: "header every datagram"}[every], func(t *testing.T) {
			session(t, every)
		})
	}
}

func session(t *testing.T, every bool) {
	cfg := relay.DefaultConfig()
	cfg.HostTokens = [][relay.TokenSize]byte{token}
	cfg.ProxyProtocol = true
	// The forwarder speaks from 127.0.0.2; anyone else on loopback is a
	// stranger.
	cfg.TrustedProxies = []netip.Prefix{netip.MustParsePrefix("127.0.0.2/32")}
	server := relay.NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
	conns, err := relay.Listen("127.0.0.1:0", 1)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go server.ServeAll(ctx, conns, nil)
	relayAt := conns[0].LocalAddr().(*net.UDPAddr).AddrPort()

	fwd, err := Listen("127.0.0.1:0", relayAt)
	if err != nil {
		t.Fatal(err)
	}
	fwd.EveryDatagram = every
	fwd.From = netip.MustParseAddr("127.0.0.2")
	go fwd.Serve(ctx)

	dial := func(to netip.AddrPort) *net.UDPConn {
		c, err := net.DialUDP("udp", nil, net.UDPAddrFromAddrPort(to))
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		_ = c.SetDeadline(time.Now().Add(2 * time.Second))
		return c
	}
	read := func(c *net.UDPConn) relay.Reply {
		t.Helper()
		buf := make([]byte, 2048)
		n, err := c.Read(buf)
		if err != nil {
			t.Fatal(err)
		}
		r, err := relay.ParseReply(buf[:n])
		if err != nil {
			t.Fatal(err)
		}
		return r
	}
	ask := func(c *net.UDPConn, req []byte) relay.Reply {
		t.Helper()
		if _, err := c.Write(req); err != nil {
			t.Fatal(err)
		}
		return read(c)
	}

	host, guest := dial(fwd.Addr()), dial(fwd.Addr())
	if r := ask(host, relay.RegisterHost(secret, token)); !r.Registered {
		t.Fatalf("host: %+v", r)
	}
	code := ask(host, relay.RequestCode(secret, false)).Code
	if r := ask(guest, relay.ResolveCode(code)); r.Secret != secret {
		t.Fatalf("resolve: %+v", r)
	}
	ch := ask(guest, relay.RegisterGuest(secret)).Channel
	_, _ = guest.Write(relay.Data(0, []byte("ping")))
	if r := read(host); r.Channel != ch || string(r.Payload) != "ping" {
		t.Fatalf("host got %+v", r)
	}
	_, _ = host.Write(relay.Data(ch, []byte("pong")))
	if r := read(guest); string(r.Payload) != "pong" {
		t.Fatalf("guest got %+v", r)
	}
	// What the relay saw: the guest itself, not the forwarder.
	if r := ask(guest, relay.Probe(7)); r.Observed != guest.LocalAddr().(*net.UDPAddr).AddrPort() {
		t.Fatalf("observed %v, the guest is %v", r.Observed, guest.LocalAddr())
	}

	// Straight to the relay, past the forwarder (an address it does not
	// trust): nothing.
	stranger := dial(relayAt)
	_ = stranger.SetDeadline(time.Now().Add(300 * time.Millisecond))
	_, _ = stranger.Write(relay.Probe(1))
	if _, err := stranger.Read(make([]byte, 64)); err == nil {
		t.Fatal("an untrusted sender was answered")
	}
}
