package relay

import (
	"crypto/rand"
	"net/netip"
	"testing"
	"time"
)

var hostKey = [HostIDSize]byte{0x5e, 0xc2, 0xe7}

// A host registered with secret on s, its ID bound (open or not).
func boundHost(t *testing.T, s *Server, open bool, now time.Time) [HostIDSize]byte {
	t.Helper()
	mustHandle(t, s, hostA, RegisterHost(secret, token), now)
	flags := byte(0)
	if open {
		flags = FlagHostOpen
	}
	r := mustHandle(t, s, hostA, BindHostID(secret, hostKey, flags, netip.AddrPort{}), now)
	if !r.HasHostBound {
		t.Fatalf("bind: %+v", r)
	}
	return HostIDOf(hostKey)
}

func TestARecentHostJoinsByItsID(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	id := boundHost(t, s, true, now)
	if r := mustHandle(t, s, guestA, QueryHost(id), now); !r.HasHostState || r.HostState != HostJoinable {
		t.Fatalf("query: %+v", r)
	}
	r := mustHandle(t, s, guestA, JoinHost(id), now)
	if !r.HasSecret || r.Secret != secret || r.At.IsValid() {
		t.Fatalf("join: %+v", r)
	}
	if r := mustHandle(t, s, guestA, RegisterGuest(r.Secret), now); !r.Registered {
		t.Fatalf("register: %+v", r)
	}
	// The binding lives while its host says it again; then it is offline.
	later := now.Add(s.cfg.HostIDTTL + time.Second)
	s.handle(hostA, Keepalive(), later)
	s.sweep(later)
	if r := mustHandle(t, s, guestB, QueryHost(id), later); r.HostState != HostOffline {
		t.Fatalf("stale: %+v", r)
	}
	// Its session gone, it is offline at once.
	mustHandle(t, s, hostA, BindHostID(secret, hostKey, FlagHostOpen, netip.AddrPort{}), later)
	s.forgetForTest(hostA)
	if r := mustHandle(t, s, guestB, QueryHost(id), later); r.HostState != HostOffline {
		t.Fatalf("session gone: %+v", r)
	}
}

func TestAHostNotOpenToItsIDIsNotJoinableByIt(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	// Invite only (or closed): online, but its ID gives no secret.
	id := boundHost(t, s, false, now)
	if r := mustHandle(t, s, guestA, QueryHost(id), now); r.HostState != HostOnline {
		t.Fatalf("query: %+v", r)
	}
	if r := mustHandle(t, s, guestA, JoinHost(id), now); r.HasSecret || !r.HasHostState || r.HostState != HostOnline {
		t.Fatalf("join: %+v", r)
	}
	// Full: online, not joinable.
	mustHandle(t, s, hostA, BindHostID(secret, hostKey, FlagHostOpen, netip.AddrPort{}), now)
	for i := range s.cfg.MaxGuests {
		g := netip.AddrPortFrom(netip.AddrFrom4([4]byte{203, 0, 113, byte(100 + i)}), 6000)
		mustHandle(t, s, g, RegisterGuest(secret), now)
	}
	if r := mustHandle(t, s, guestA, JoinHost(id), now); r.HasSecret || r.HostState != HostOnline {
		t.Fatalf("full: %+v", r)
	}
}

func TestOnlyTheKeyBindsAHostID(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	id := boundHost(t, s, true, now)
	// A guest knows the ID, not the key: the ID used as a key binds another
	// ID, never this one, and only for a session of its own.
	if r := mustHandle(t, s, guestA, BindHostID(secret, id, FlagHostOpen, netip.AddrPort{}), now); r.Error != ErrNoSession {
		t.Fatalf("a guest's bind: %+v", r)
	}
	if HostIDOf(id) == id {
		t.Fatal("an ID is its own key")
	}
	// Another address with the host's secret and key: not the session's host.
	if r := mustHandle(t, s, nobody, BindHostID(secret, hostKey, FlagHostOpen, netip.AddrPort{}), now); r.Error != ErrNoSession {
		t.Fatalf("a stranger's bind: %+v", r)
	}
	if r := mustHandle(t, s, guestB, JoinHost(id), now); r.Secret != secret {
		t.Fatalf("still the host's: %+v", r)
	}
}

func TestHostIDsCannotBeGuessedOrListed(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	boundHost(t, s, true, now)
	// A random ID reads as an offline host: nothing tells it from a host
	// that is away; one source gets a burst, is told once, then nothing.
	for i := range s.cfg.HostLookupBurst {
		var guess [HostIDSize]byte
		_, _ = rand.Read(guess[:])
		r := mustHandle(t, s, nobody, JoinHost(guess), now)
		if r.HasSecret || !r.HasHostState || r.HostState != HostOffline {
			t.Fatalf("guess %d: %+v", i, r)
		}
	}
	if r := mustHandle(t, s, nobody, QueryHost(HostIDOf(hostKey)), now); r.Error != ErrSlowDown {
		t.Fatalf("over the burst: %+v", r)
	}
	if _, ok := s.handle(nobody, QueryHost(HostIDOf(hostKey)), now); ok {
		t.Fatal("answered again while over the limit")
	}
	if r := mustHandle(t, s, nobody, QueryHost(HostIDOf(hostKey)), now.Add(s.cfg.HostLookupInterval)); r.HostState != HostJoinable {
		t.Fatalf("after the refill: %+v", r)
	}
	// Bindings are capped per source: a flood of keys binds no more.
	for i := range s.cfg.MaxHostIDsPerSource + 2 {
		key := [HostIDSize]byte{byte(i), 7}
		sec := [SecretSize]byte{byte(i), 7}
		at := netip.MustParseAddrPort("192.0.2.20:27460") // a session elsewhere: the key alone
		r := mustHandle(t, s, guestB, BindHostID(sec, key, FlagHostOpen, at), now)
		if (i < s.cfg.MaxHostIDsPerSource) != r.HasHostBound {
			t.Fatalf("bind %d: %+v", i, r)
		}
	}
}

func TestHostLookupsAreApartFromCodeLookups(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	id := boundHost(t, s, true, now)
	code := codeOf(t, s, now)
	// Every host lookup spent: a code lookup is still answered; every code
	// lookup spent (and failures), a host lookup still is.
	for range s.cfg.HostLookupBurst {
		mustHandle(t, s, guestA, QueryHost(id), now)
	}
	if r := mustHandle(t, s, guestA, ResolveCode(code), now); !r.HasSecret {
		t.Fatalf("code after host lookups: %+v", r)
	}
	for range s.cfg.CodeLookupBurst {
		mustHandle(t, s, guestB, ResolveCode(otherThan(code)), now)
	}
	if r := mustHandle(t, s, guestB, QueryHost(id), now); r.HostState != HostJoinable {
		t.Fatalf("host after code lookups: %+v", r)
	}
	if s.failures != s.cfg.CodeLookupBurst {
		t.Fatalf("host lookups counted as code failures: %d", s.failures)
	}
}

func TestHostIDRepliesAreSmall(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	id := boundHost(t, s, true, now)
	v6 := netip.MustParseAddrPort("[2001:db8::20]:27460")
	for _, c := range []struct {
		from netip.AddrPort
		req  []byte
	}{
		{hostA, BindHostID(secret, hostKey, FlagHostOpen, netip.AddrPort{})},
		{nobody, BindHostID(secret, [HostIDSize]byte{1}, FlagHostOpen, v6)},
		{nobody, QueryHost(id)},
		{guestA, JoinHost(id)},
		{guestB, JoinHost([HostIDSize]byte{3})},
	} {
		out, ok := s.handle(c.from, c.req, now)
		if ok && len(out.data) > len(c.req) {
			t.Fatalf("answer of %d bytes to a %d-byte request", len(out.data), len(c.req))
		}
	}
	// A session elsewhere: its replica's address comes with the secret.
	mustHandle(t, s, nobody, BindHostID([SecretSize]byte{4}, [HostIDSize]byte{1}, FlagHostOpen, v6), now)
	if r := mustHandle(t, s, guestA, JoinHost(HostIDOf([HostIDSize]byte{1})), now); r.At != v6 || r.Secret != ([SecretSize]byte{4}) {
		t.Fatalf("elsewhere: %+v", r)
	}
}

func TestAHostIDLivesOnItsOwner(t *testing.T) {
	now := time.Now()
	// An ID each replica of three owns: the others and the entrance send it there.
	for shard := range 3 {
		var key [HostIDSize]byte
		for i := 0; ; i++ {
			key = [HostIDSize]byte{byte(i), byte(i >> 8), 0x77}
			if r := replica(t, 3, 0); func() int { _, sh := r.hostOwner(HostIDOf(key)); return sh }() == shard {
				break
			}
		}
		id := HostIDOf(key)
		for other := range 3 {
			s := replica(t, 3, other)
			r := mustHandle(t, s, guestA, QueryHost(id), now)
			if other == shard {
				if !r.HasHostState || r.HostState != HostOffline {
					t.Fatalf("owner %d: %+v", shard, r)
				}
				continue
			}
			if r.Redirect != fleet[shard] {
				t.Fatalf("replica %d for owner %d: %+v", other, shard, r)
			}
			out, ok := s.handleFront(guestA, JoinHost(id), now)
			if f := mustReply(t, out, ok); f.Redirect != fleet[shard] {
				t.Fatalf("entrance for owner %d: %+v", shard, f)
			}
		}
	}
}
