package relay

import (
	"net/netip"
	"testing"
	"time"
)

func TestAKeptCodeComesBackEverySession(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	mustHandle(t, s, hostA, RegisterHostKeep(secret, token, 1), now)
	first := mustHandle(t, s, hostA, RequestCode(secret, false), now)
	if len(first.Code) != CodeSize || first.CodeLeft != KeptCodeLeft {
		t.Fatalf("kept code: %+v", first)
	}
	// It does not expire while its host is registered, and renewing it
	// does not change it.
	later := now.Add(2 * s.cfg.CodeTTL)
	s.handle(hostA, Keepalive(), later)
	s.sweep(later)
	if r := mustHandle(t, s, hostA, RequestCode(secret, true), later); r.Code != first.Code {
		t.Fatalf("after its TTL: %+v", r)
	}
	if r := mustHandle(t, s, guestA, ResolveCode(first.Code), later); r.Secret != secret {
		t.Fatalf("resolve: %+v", r)
	}

	// The session ends: the code dies with it.
	gone := later.Add(s.cfg.IdleTimeout + time.Second)
	s.sweep(gone)
	if r := mustHandle(t, s, guestB, ResolveCode(first.Code), gone); r.Error != ErrNoCode {
		t.Fatalf("after the session: %+v", r)
	}
	// Another session of the same host (another secret, address): the same code.
	other := [SecretSize]byte{42}
	mustHandle(t, s, nobody, RegisterHostKeep(other, token, 1), gone)
	if r := mustHandle(t, s, nobody, RequestCode(other, false), gone); r.Code != first.Code {
		t.Fatalf("next session: %+v, want %s", r, first.Code)
	}
	// Another salt (the "new code" action): another code, at once.
	mustHandle(t, s, nobody, RegisterHostKeep(other, token, 2), gone)
	second := mustHandle(t, s, nobody, RequestCode(other, false), gone)
	if second.Code == first.Code || second.CodeLeft != KeptCodeLeft {
		t.Fatalf("new salt: %+v", second)
	}
	if r := mustHandle(t, s, guestA, ResolveCode(first.Code), gone); r.Error != ErrNoCode {
		t.Fatalf("the old one: %+v", r)
	}
}

func TestKeptCodesNeedATokenAndStayApart(t *testing.T) {
	now := time.Now()
	s := newServer(t)
	if r := mustHandle(t, s, hostA, RegisterHostKeep(secret, [TokenSize]byte{9}, 1), now); r.Error != ErrBadToken {
		t.Fatalf("no token: %+v", r)
	}
	// The same token again before its old session idled out (a game back
	// from a crash): the newest session takes the kept code, the old one
	// gets a code of its own the next time it asks.
	mustHandle(t, s, hostA, RegisterHostKeep(secret, token, 1), now)
	kept := mustHandle(t, s, hostA, RequestCode(secret, false), now).Code
	other := [SecretSize]byte{43}
	mustHandle(t, s, nobody, RegisterHostKeep(other, token, 1), now)
	if r := mustHandle(t, s, nobody, RequestCode(other, false), now); r.Code != kept || r.CodeLeft != KeptCodeLeft {
		t.Fatalf("newest session: %+v", r)
	}
	if r := mustHandle(t, s, hostA, RequestCode(secret, false), now); r.Code == kept || r.CodeLeft == KeptCodeLeft {
		t.Fatalf("old session: %+v", r)
	}
	if r := mustHandle(t, s, guestA, ResolveCode(kept), now); r.Secret != other {
		t.Fatalf("the code leads to %+v", r)
	}
	// Another token, another code.
	tokenB := [TokenSize]byte{5, 5}
	s.cfg.HostTokens = append(s.cfg.HostTokens, tokenB)
	third := [SecretSize]byte{44}
	mustHandle(t, s, guestB, RegisterHostKeep(third, tokenB, 1), now)
	if r := mustHandle(t, s, guestB, RequestCode(third, false), now); r.Code == kept {
		t.Fatal("two tokens share a kept code")
	}
}

func TestKeptCodesLiveOnTheirTokensReplica(t *testing.T) {
	now := time.Now()
	servers := []*Server{replica(t, 3, 0), replica(t, 3, 1), replica(t, 3, 2)}
	owner := servers[0].keepOwner(token)
	// Anyone else, and the entrance, send the host there.
	for i, s := range servers {
		req := RegisterHostKeep(secret, token, 7)
		if i != owner {
			out, ok := s.handle(hostA, req, now)
			if r := mustReply(t, out, ok); r.Redirect != fleet[owner] || len(out.data) > len(req) {
				t.Fatalf("replica %d: %+v", i, r)
			}
		}
		out, ok := s.handleFront(hostA, req, now)
		if r := mustReply(t, out, ok); r.Redirect != fleet[owner] {
			t.Fatalf("entrance of %d: %+v", i, r)
		}
	}
	s := servers[owner]
	mustHandle(t, s, hostA, RegisterHostKeep(secret, token, 7), now)
	code := mustHandle(t, s, hostA, RequestCode(secret, false), now).Code
	if !s.owns(code) {
		t.Fatalf("%s is not the owner's", code)
	}
	// Every salt lands on the same replica.
	for salt := range uint32(20) {
		if c := s.keptCode(token, salt); !s.owns(c) {
			t.Fatalf("salt %d: %s", salt, c)
		}
	}
	// A guest asking another replica is sent to it, as for any code.
	if r := mustHandle(t, servers[(owner+1)%3], guestA, ResolveCode(code), now); r.Redirect != fleet[owner] {
		t.Fatalf("resolve elsewhere: %+v", r)
	}
	// Draining, the owner gives a new host a code elsewhere.
	s.Drain()
	newHost := netip.MustParseAddrPort("198.51.100.77:1")
	if r := mustHandle(t, s, newHost, RegisterHostKeep([SecretSize]byte{8}, token, 7), now); r.Redirect != fleet[(owner+1)%3] {
		t.Fatalf("draining owner: %+v", r)
	}
}
