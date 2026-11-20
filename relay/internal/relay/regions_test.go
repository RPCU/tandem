package relay

import (
	"io"
	"log/slog"
	"net/netip"
	"testing"
	"time"
)

var entrances = []netip.AddrPort{
	netip.MustParseAddrPort("198.51.100.1:27460"), // region 0
	netip.MustParseAddrPort("203.0.113.1:27460"),  // region 1
}

// regional is shard `shard` of a 2-replica fleet in region `region` of two.
func regional(t *testing.T, region, shard int) *Server {
	t.Helper()
	cfg := DefaultConfig()
	cfg.HostTokens = [][TokenSize]byte{token}
	cfg.Peers = fleet[:2]
	cfg.Shard = shard
	cfg.Regions = entrances
	cfg.Region = region
	if err := cfg.Validate(); err != nil {
		t.Fatal(err)
	}
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

func TestCodesNameTheirRegion(t *testing.T) {
	now := time.Now()
	for region := range 2 {
		for shard := range 2 {
			s := regional(t, region, shard)
			for i := range 20 {
				sec := [SecretSize]byte{byte(i), byte(region), byte(shard)}
				host := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 3, byte(region*2 + shard), byte(i)}), 1)
				mustHandle(t, s, host, RegisterHost(sec, token), now)
				code := mustHandle(t, s, host, RequestCode(sec, false), now).Code
				if g, sh := s.ownerOf(code); g != region || sh != shard {
					t.Fatalf("region %d shard %d issued %s, owned by %d/%d", region, shard, code, g, sh)
				}
			}
		}
	}
}

func TestLookupsCrossRegionsThroughTheEntrance(t *testing.T) {
	now := time.Now()
	here := regional(t, 0, 0)
	there := regional(t, 1, 1)
	mustHandle(t, there, hostA, RegisterHost(secret, token), now)
	code := mustHandle(t, there, hostA, RequestCode(secret, false), now).Code

	// Asked in region 0: sent to region 1's entrance, never to a replica
	// it does not know of.
	req := ResolveCode(code)
	out, ok := here.handle(guestA, req, now)
	if r := mustReply(t, out, ok); r.Redirect != entrances[1] || len(out.data) > len(req) {
		t.Fatalf("region 0 answered %+v", r)
	}
	// Region 1's entrance (any replica of it) sends it on to the owner.
	entrance := regional(t, 1, 0)
	out, ok = entrance.handleFront(guestA, req, now)
	if r := mustReply(t, out, ok); r.Redirect != fleet[1] {
		t.Fatalf("region 1's entrance answered %+v", r)
	}
	if r := mustHandle(t, there, guestA, req, now); r.Secret != secret {
		t.Fatalf("the owner answered %+v", r)
	}
	// The failure budget is the world's: 4 replicas, a quarter each.
	if here.failCap != (here.cfg.MaxCodeFailuresPerMinute+3)/4 {
		t.Fatalf("share: %d", here.failCap)
	}
}

func TestProbesAreAnsweredSmall(t *testing.T) {
	s := replica(t, 2, 0)
	now := time.Now()
	for _, from := range []netip.AddrPort{nobody, netip.MustParseAddrPort("[2001:db8::7]:4000")} {
		req := Probe(0xC0FFEE)
		for _, p := range []port{portDirect, portFront} {
			out, _, ok := s.handleTwo(p, from, req, now)
			r := mustReply(t, out, ok)
			if r.Nonce != 0xC0FFEE || r.Observed != from || len(out.data) > len(req) {
				t.Fatalf("port %d, %v: %+v (%d bytes)", p, from, r, len(out.data))
			}
		}
	}
	// A short probe is not one.
	if _, ok := s.handle(nobody, Probe(1)[:40], now); ok {
		t.Fatal("a short probe was answered")
	}
}
