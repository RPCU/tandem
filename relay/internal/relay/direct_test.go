package relay

import (
	"net/netip"
	"testing"
	"time"
)

var (
	hostLAN  = netip.MustParseAddrPort("192.168.1.20:27450")
	guestLAN = netip.MustParseAddrPort("10.0.0.7:51000")
)

func TestGuestsAndHostsAreIntroduced(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	mustHandle(t, s, hostA, RegisterHost(secret, token), now)
	// The host's addresses are kept, not answered.
	if _, ok := s.handle(hostA, Addresses(FlagMappingStable, []netip.AddrPort{hostLAN}), now); ok {
		t.Fatal("the host's addresses were answered")
	}
	ch := mustHandle(t, s, guestA, RegisterGuest(secret), now).Channel

	req := Addresses(FlagMappingVaries, []netip.AddrPort{guestLAN})
	toGuest, toHost, ok := s.handleTwo(portDirect, guestA, req, now)
	r := mustReply(t, toGuest, ok)
	if toGuest.to != guestA || !r.Introduced || r.Peer != hostA || r.PeerLAN != hostLAN ||
		r.PeerFlags != FlagMappingStable || r.Channel != ch {
		t.Fatalf("to the guest: %v %+v", toGuest.to, r)
	}
	h, err := ParseReply(toHost.data)
	if err != nil || toHost.to != hostA || !h.Introduced || h.Peer != guestA || h.PeerLAN != guestLAN ||
		h.PeerFlags != FlagMappingVaries || h.Channel != ch || h.Nonce != r.Nonce {
		t.Fatalf("to the host: %v %+v %v", toHost.to, h, err)
	}
	// Both answers together are smaller than the request.
	if n := len(toGuest.data) + len(toHost.data); n > len(req) {
		t.Fatalf("%d bytes answer a %d-byte request", n, len(req))
	}

	// Once per interval; then again, with another nonce.
	if _, ok := s.handle(guestA, req, now.Add(time.Second)); ok {
		t.Fatal("introduced twice in a second")
	}
	later := now.Add(s.cfg.IntroduceInterval)
	if again := mustHandle(t, s, guestA, req, later); !again.Introduced || again.Nonce == r.Nonce {
		t.Fatalf("again: %+v", again)
	}
}

func TestIntroductionsStayInTheSession(t *testing.T) {
	s := newServer(t)
	now := time.Now()
	mustHandle(t, s, hostA, RegisterHost(secret, token), now)
	// A stranger gets nothing, and its addresses go nowhere.
	if _, ok := s.handle(nobody, Addresses(0, []netip.AddrPort{guestLAN}), now); ok {
		t.Fatal("a stranger was answered")
	}
	// A LAN address that is not on a LAN is dropped: a guest cannot have
	// its host punch at someone else.
	mustHandle(t, s, guestA, RegisterGuest(secret), now)
	_, toHost, ok := s.handleTwo(portDirect, guestA,
		Addresses(0, []netip.AddrPort{netip.MustParseAddrPort("198.51.100.99:80")}), now)
	h, err := ParseReply(toHost.data)
	if !ok || err != nil || h.PeerLAN.Port() != 0 || h.Peer != guestA {
		t.Fatalf("to the host: %+v %v", h, err)
	}
	// Malformed: too many addresses.
	bad := Addresses(0, nil)
	bad[headerSize+1] = MaxLAN + 1
	if _, ok := s.handle(guestA, bad, now.Add(time.Minute)); ok {
		t.Fatal("a malformed Addresses was answered")
	}
	// The entrance never introduces anyone.
	if _, ok := s.handleFront(guestA, Addresses(0, nil), now.Add(time.Minute)); ok {
		t.Fatal("the entrance answered")
	}
	// Punches are between clients: the relay drops them.
	if _, ok := s.handle(guestA, Punch(1, 1, false), now); ok {
		t.Fatal("the relay answered a punch")
	}
}
