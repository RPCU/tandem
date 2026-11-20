package relay

import (
	"bytes"
	"io"
	"log/slog"
	"net/netip"
	"testing"
	"time"
)

var (
	// The tunnel agent's sockets, one per client flow, on the pod's loopback.
	agentHost  = netip.MustParseAddrPort("127.0.0.1:40001")
	agentGuest = netip.MustParseAddrPort("127.0.0.1:40002")
	agentOther = netip.MustParseAddrPort("127.0.0.1:40003")
	relayLocal = netip.MustParseAddrPort("127.0.0.1:27460")
)

func proxied(t testing.TB) *Server {
	t.Helper()
	cfg := DefaultConfig()
	cfg.HostTokens = [][TokenSize]byte{token}
	cfg.ProxyProtocol = true
	if err := cfg.Validate(); err != nil {
		t.Fatal(err)
	}
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func proxyHeader(client netip.AddrPort) []byte { return AppendProxyV2(nil, client, relayLocal) }

// flowOpens is what the agent sends first on a new flow: the header alone.
func flowOpens(t *testing.T, s *Server, agent, client netip.AddrPort, now time.Time) {
	t.Helper()
	if _, ok := s.handle(agent, proxyHeader(client), now); ok {
		t.Fatal("a header alone was answered")
	}
}

func TestProxyHeadersParse(t *testing.T) {
	v4 := proxyHeader(hostA)
	if c, n, local, err := parseProxyV2(v4); err != nil || local || c != hostA || n != len(v4) {
		t.Fatalf("v4: %v %d %v %v", c, n, local, err)
	}
	six := netip.MustParseAddrPort("[2001:db8::5]:7000")
	v6 := AppendProxyV2(nil, six, netip.MustParseAddrPort("[::1]:27460"))
	if c, n, _, err := parseProxyV2(v6); err != nil || c != six || n != 16+36 {
		t.Fatalf("v6: %v %d %v", c, n, err)
	}
	// TLVs after the addresses are skipped, inside the header's length.
	tlv := append(bytes.Clone(v4), 0x04, 0x00, 0x02, 'x', 'y') // PP2_TYPE_NOOP
	tlv[15] += 5
	payload := append(bytes.Clone(tlv), Keepalive()...)
	if c, n, _, err := parseProxyV2(payload); err != nil || c != hostA || n != len(tlv) {
		t.Fatalf("TLVs: %v %d %v", c, n, err)
	}
	// LOCAL: the proxy itself, no client.
	local := bytes.Clone(v4)
	local[12] = 0x20
	if _, _, isLocal, err := parseProxyV2(local); err != nil || !isLocal {
		t.Fatalf("LOCAL: %v %v", isLocal, err)
	}
	for name, bad := range map[string][]byte{
		"truncated":       v4[:20],
		"short length":    append(bytes.Clone(v4[:14]), 0, 4, 1, 2, 3, 4),
		"wrong signature": append([]byte("\r\n\r\n\x00\r\nQUIT!"), v4[12:]...),
		"version 1":       append(bytes.Clone(v4[:12]), append([]byte{0x11}, v4[13:]...)...),
		"command 2":       append(bytes.Clone(v4[:12]), append([]byte{0x22}, v4[13:]...)...),
		"unspec family":   append(bytes.Clone(v4[:13]), append([]byte{0x00}, v4[14:]...)...),
		"port 0":          AppendProxyV2(nil, netip.MustParseAddrPort("198.51.100.1:0"), relayLocal),
		"v1 text":         []byte("PROXY UDP4 198.51.100.1 127.0.0.1 5000 27460\r\n"),
	} {
		if _, _, _, err := parseProxyV2(bad); err == nil {
			t.Errorf("%s: accepted", name)
		}
	}
}

func TestOnlyTrustedProxiesSpeakForClients(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	stranger := netip.MustParseAddrPort("203.0.113.50:40001")
	// A header from outside the trusted proxies: nothing, not even noted.
	if _, ok := s.handle(stranger, append(proxyHeader(hostA), RegisterHost(secret, token)...), now); ok {
		t.Fatal("an untrusted header was answered")
	}
	if s.flowCount() != 0 || s.Metrics.drops[dropUntrusted].Load() != 1 {
		t.Fatalf("flows %d, untrusted drops %d", s.flowCount(), s.Metrics.drops[dropUntrusted].Load())
	}
	// Nor anything else from it, registration or data.
	for _, req := range [][]byte{RegisterHost(secret, token), ResolveCode("ZZZZZZ"), Probe(1), Data(0, []byte("x"))} {
		if _, ok := s.handle(stranger, req, now); ok {
			t.Fatalf("an untrusted sender got an answer to %x", req[:5])
		}
	}
	// A forged or broken header from the proxy: dropped too.
	broken := proxyHeader(hostA)[:20]
	if _, ok := s.handle(agentHost, broken, now); ok || s.Metrics.drops[dropProxyHeader].Load() != 1 {
		t.Fatal("a broken header was taken")
	}
	local := proxyHeader(hostA)
	local[12] = 0x20
	if _, ok := s.handle(agentHost, append(local, Probe(1)...), now); ok {
		t.Fatal("a LOCAL header's payload was answered")
	}
}

func TestProxiedSessionsKeepEveryRule(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	flowOpens(t, s, agentHost, hostA, now)
	flowOpens(t, s, agentGuest, guestA, now)

	// Registered by flow; answers go back to the agent's socket.
	out, ok := s.handle(agentHost, RegisterHost(secret, token), now)
	if r := mustReply(t, out, ok); !r.Registered || out.to != agentHost {
		t.Fatalf("host: %v %+v", out.to, r)
	}
	code := mustHandle(t, s, agentHost, RequestCode(secret, false), now).Code
	if r := mustHandle(t, s, agentGuest, ResolveCode(code), now); r.Secret != secret {
		t.Fatalf("resolve: %+v", r)
	}
	ch := mustHandle(t, s, agentGuest, RegisterGuest(secret), now).Channel

	// Data both ways, to the agent's sockets.
	out, ok = s.handle(agentGuest, Data(0, []byte("up")), now)
	if r := mustReply(t, out, ok); out.to != agentHost || r.Channel != ch || string(r.Payload) != "up" {
		t.Fatalf("to the host: %v %+v", out.to, r)
	}
	out, ok = s.handle(agentHost, Data(ch, []byte("down")), now)
	if r := mustReply(t, out, ok); out.to != agentGuest || string(r.Payload) != "down" {
		t.Fatalf("to the guest: %v %+v", out.to, r)
	}

	// The connection check and the introductions name the real clients.
	if r := mustHandle(t, s, agentGuest, Probe(9), now); r.Observed != guestA {
		t.Fatalf("observed %v", r.Observed)
	}
	s.handle(agentHost, Addresses(FlagMappingStable, []netip.AddrPort{hostLAN}), now)
	toGuest, toHost, ok := s.handleTwo(portDirect, agentGuest, Addresses(0, nil), now)
	g := mustReply(t, toGuest, ok)
	h, _ := ParseReply(toHost.data)
	if toGuest.to != agentGuest || g.Peer != hostA || toHost.to != agentHost || h.Peer != guestA {
		t.Fatalf("introductions: %v %+v / %v %+v", toGuest.to, g, toHost.to, h)
	}

	// A flow nobody registered gets nothing for data (no reflection).
	flowOpens(t, s, agentOther, nobody, now)
	if _, ok := s.handle(agentOther, Data(1, []byte("x")), now); ok {
		t.Fatal("an unregistered flow's data was answered")
	}
}

func TestProxiedLookupsAreLimitedPerRealSource(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	// One client IP behind several agent sockets (its game restarted, say):
	// one budget.
	for i := range s.cfg.CodeLookupBurst {
		agent := netip.AddrPortFrom(netip.MustParseAddr("127.0.0.1"), uint16(41000+i))
		client := netip.AddrPortFrom(nobody.Addr(), uint16(5000+i))
		flowOpens(t, s, agent, client, now)
		if r := mustHandle(t, s, agent, ResolveCode("ZZZZZZ"), now); r.Error != ErrNoCode {
			t.Fatalf("try %d: %+v", i, r)
		}
	}
	agent := netip.MustParseAddrPort("127.0.0.1:41999")
	flowOpens(t, s, agent, netip.AddrPortFrom(nobody.Addr(), 6000), now)
	if r := mustHandle(t, s, agent, ResolveCode("ZZZZZZ"), now); r.Error != ErrSlowDown {
		t.Fatalf("over its burst: %+v", r)
	}
	// Another client, through the same agent: its own budget.
	flowOpens(t, s, agentOther, guestB, now)
	if r := mustHandle(t, s, agentOther, ResolveCode("ZZZZZZ"), now); r.Error != ErrNoCode {
		t.Fatalf("another client: %+v", r)
	}
}

// Agents up to 0.15 put the header in front of every datagram.
func TestAHeaderOnEveryDatagram(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	req := append(proxyHeader(hostA), RegisterHost(secret, token)...)
	out, ok := s.handle(agentHost, req, now)
	r := mustReply(t, out, ok)
	if !r.Registered || out.to != agentHost {
		t.Fatalf("host: %+v", r)
	}
	// The size rule counts the payload: the answer is no larger than the
	// 64 bytes of the registration, header aside.
	if len(out.data) > registerSize {
		t.Fatalf("%d bytes for a %d-byte registration", len(out.data), registerSize)
	}
	ch := mustHandle(t, s, agentGuest, append(proxyHeader(guestA), RegisterGuest(secret)...), now).Channel
	out, ok = s.handle(agentGuest, append(proxyHeader(guestA), Data(0, []byte("hi"))...), now)
	if r := mustReply(t, out, ok); out.to != agentHost || r.Channel != ch || string(r.Payload) != "hi" {
		t.Fatalf("data: %v %+v", out.to, r)
	}
	if r := mustHandle(t, s, agentGuest, append(proxyHeader(guestA), Probe(3)...), now); r.Observed != guestA {
		t.Fatalf("observed %v", r.Observed)
	}
}

// A relay restarted under a running agent: its flows send no header again.
// They are served by their socket, but get no Observed, no introduction,
// and share the proxy's lookup budget.
func TestFlowsOlderThanTheRelay(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	if r := mustHandle(t, s, agentHost, RegisterHost(secret, token), now); !r.Registered {
		t.Fatalf("host: %+v", r)
	}
	ch := mustHandle(t, s, agentGuest, RegisterGuest(secret), now).Channel
	out, ok := s.handle(agentGuest, Data(0, []byte("hi")), now)
	if r := mustReply(t, out, ok); out.to != agentHost || r.Channel != ch {
		t.Fatalf("data: %+v", r)
	}
	if _, ok := s.handle(agentGuest, Probe(1), now); ok {
		t.Fatal("observed an unknown address")
	}
	s.handle(agentHost, Addresses(0, nil), now)
	if _, ok := s.handle(agentGuest, Addresses(0, nil), now); ok {
		t.Fatal("introduced by an unknown address")
	}
}

func TestProxiedRepliesAreSmall(t *testing.T) {
	s := proxied(t)
	now := time.Now()
	for _, req := range [][]byte{RegisterHost(secret, token), RegisterGuest([SecretSize]byte{3}), ResolveCode("ZZZZZZ"), Probe(4)} {
		out, ok := s.handle(agentGuest, append(proxyHeader(guestA), req...), now)
		if ok && len(out.data) > len(req) {
			t.Fatalf("answer of %d bytes to a %d-byte request", len(out.data), len(req))
		}
	}
}

// The packet path behind a proxy: playit's agents (0.16 on) send data with
// no header, older ones with one on each datagram.
func BenchmarkForwardProxied(b *testing.B) {
	for _, every := range []bool{false, true} {
		name := "header-once"
		if every {
			name = "header-every-datagram"
		}
		b.Run(name, func(b *testing.B) {
			s := proxied(b)
			s.cfg.MaxPacketsPerSecond, s.cfg.MaxBytesPerSecond = 1<<30, 1<<30
			now := time.Now()
			s.handle(agentHost, append(proxyHeader(hostA), RegisterHost(secret, token)...), now)
			out, _ := s.handle(agentGuest, append(proxyHeader(guestA), RegisterGuest(secret)...), now)
			r, _ := ParseReply(out.data)
			up := Data(0, make([]byte, 200))
			down := Data(r.Channel, make([]byte, 200))
			// The data starts after the header, if any (its channel is
			// rewritten in place on the way: put back each time).
			off := 0
			if every {
				off = len(proxyHeader(hostA))
				up, down = append(proxyHeader(guestA), up...), append(proxyHeader(hostA), down...)
			}
			w := newWorker(0)
			b.ReportAllocs()
			b.ResetTimer()
			for i := range b.N {
				if i&1 == 0 {
					s.process(w, portDirect, agentGuest, up, now)
				} else {
					binaryPutChannel(down[off:], r.Channel)
					s.process(w, portDirect, agentHost, down, now)
				}
			}
		})
	}
}
