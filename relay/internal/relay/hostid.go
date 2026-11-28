package relay

// Host IDs (wire.go: HostID, QueryHost, JoinHost): the one-click rejoin of a
// recent host, and its online badge (docs/architecture/relay.md, "Host IDs").
//
// A host game keeps a permanent random 128-bit host key, and its guests are
// told its host ID, HostIDOf(key): knowing the ID lets a guest ask about the
// host and join it, never bind it (that takes the key). Each binding lives
// on the ID's owner (hostOwner, like a code's: our region and replica from
// its bits), HostIDTTL after the host's last HostID (it sends one every 20 s
// while it hosts). A binding of a session of ours (at invalid) is taken from
// the session's registered host only; one of a session on another replica,
// from whoever holds the key.
//
// Guessing: 2^128 IDs, an unknown one answered as an offline host (nothing
// tells the two apart), and lookups limited per source on their own bucket
// (HostLookupBurst, then one per HostLookupInterval), apart from the code
// lookups and their failure budget: a code guesser gains nothing here, and
// a Join list asking about its recent hosts spends no code lookup.

import (
	"crypto/sha256"
	"crypto/subtle"
	"encoding/binary"
	"net/netip"
	"time"
)

type hostBinding struct {
	secret [SecretSize]byte
	// Where the session is: invalid for one of ours.
	at     netip.AddrPort
	open   bool
	seen   time.Time
	source netip.Prefix
}

// HostIDOf is a host key's ID: what the host's guests are told.
func HostIDOf(key [HostIDSize]byte) (id [HostIDSize]byte) {
	h := sha256.Sum256(append([]byte("tandem-host-id\x00"), key[:]...))
	copy(id[:], h[:HostIDSize])
	return id
}

// hostOwner is an ID's region and, in it, its replica, from its first 31
// bits (as ownerOf a code's).
func (s *Server) hostOwner(id [HostIDSize]byte) (region, shard int) {
	v, regions := int(binary.BigEndian.Uint32(id[:])&0x7FFFFFFF), max(len(s.cfg.Regions), 1)
	return v % regions, (v / regions) % s.replicas()
}

// hostElsewhere is where a request about this ID goes when another replica
// or region owns it (ok false: we do).
func (s *Server) hostElsewhere(id [HostIDSize]byte) (netip.AddrPort, bool) {
	switch region, shard := s.hostOwner(id); {
	case region != s.cfg.Region:
		return s.cfg.Regions[region], true
	case shard != s.cfg.Shard:
		return s.cfg.Peers[shard], true
	}
	return netip.AddrPort{}, false
}

// hostStateOf: offline (none, stale, its session gone), online (not open to
// a join by its ID, or its session full here) or joinable. Under s.mu.
func (s *Server) hostStateOf(b *hostBinding, now time.Time) byte {
	if b == nil || now.Sub(b.seen) > s.cfg.HostIDTTL {
		return HostOffline
	}
	if !b.at.IsValid() {
		sess := s.sessions[b.secret]
		if sess == nil {
			return HostOffline
		}
		sess.mu.RLock()
		full := len(sess.guests) >= s.cfg.MaxGuests
		sess.mu.RUnlock()
		if full {
			return HostOnline
		}
	}
	if !b.open {
		return HostOnline
	}
	return HostJoinable
}

// bindHost answers a host's HostID: bound (HostBound with its state), sent
// to the ID's owner, or refused. Its request is 64 bytes, every answer 24 at
// most. Under s.mu.
func (s *Server) bindHost(out []byte, from, who netip.AddrPort, d datagram, now time.Time) ([]byte, bool) {
	id := HostIDOf(d.hostKey)
	if to, elsewhere := s.hostElsewhere(id); elsewhere {
		return appendRedirect(out, to), true
	}
	source := sourceOf(from)
	if who.IsValid() {
		source = sourceOf(who)
	}
	if !d.at.IsValid() {
		// A session of ours: only its host, from its address, with its secret.
		sess := s.sessions[d.secret]
		if sess == nil || sess.host.addr != from || subtle.ConstantTimeCompare(sess.secret[:], d.secret[:]) != 1 {
			s.Metrics.refused(refusedNoSession)
			return appendError(out, ErrNoSession), true
		}
	}
	b := s.hosts[id]
	if b == nil {
		if len(s.hosts) >= s.cfg.MaxHostIDs || s.hostsBySource[source] >= s.cfg.MaxHostIDsPerSource {
			return appendError(out, ErrSlowDown), true
		}
		b = &hostBinding{source: source}
		s.hosts[id] = b
		s.hostsBySource[source]++
		s.log.Info("host ID bound", "host", from, "bound", len(s.hosts))
	}
	b.secret, b.at, b.open, b.seen = d.secret, d.at, d.flags&FlagHostOpen != 0, now
	return appendHostState(out, typeHostBound, s.hostStateOf(b, now)), true
}

// allowHostLookup spends one of the source's host lookups (their own
// bucket: never the code lookups'). answer: tell it it is over its limit
// (once per refill; after that it is ignored).
func (s *Server) allowHostLookup(from netip.AddrPort, now time.Time) (allowed, answer bool) {
	key := sourceOf(from)
	b := s.hostLookups[key]
	if b == nil {
		if len(s.hostLookups) >= s.cfg.MaxLookupSources {
			return false, false
		}
		b = &lookupBucket{tokens: s.cfg.HostLookupBurst, last: now}
		s.hostLookups[key] = b
	}
	if n := int(now.Sub(b.last) / s.cfg.HostLookupInterval); n > 0 {
		b.tokens = min(s.cfg.HostLookupBurst, b.tokens+n)
		b.last = b.last.Add(time.Duration(n) * s.cfg.HostLookupInterval)
		b.warned = false
	}
	if b.tokens == 0 {
		answer = !b.warned
		b.warned = true
		return false, answer
	}
	b.tokens--
	return true, true
}

// lookupHost answers QueryHost (its state) and JoinHost (its session's
// secret and where it is, when it is joinable; its state otherwise). Like
// a code's lookup it may answer an unregistered address: 64 bytes in, 40
// at most out. Under s.mu.
func (s *Server) lookupHost(out []byte, from, who netip.AddrPort, d datagram, now time.Time) ([]byte, bool) {
	source := who
	if !source.IsValid() {
		source = from
	}
	allowed, answer := s.allowHostLookup(source, now)
	if !allowed {
		if answer {
			return appendError(out, ErrSlowDown), true
		}
		return out, false
	}
	if to, elsewhere := s.hostElsewhere(d.hostID); elsewhere {
		return appendRedirect(out, to), true
	}
	b := s.hosts[d.hostID]
	state := s.hostStateOf(b, now)
	if d.kind == typeJoinHost && state == HostJoinable {
		return appendHostSession(out, b.secret, b.at), true
	}
	return appendHostState(out, typeHostState, state), true
}

// frontHost: the entrance sends a host ID's request to its owner.
func (s *Server) frontHost(out []byte, d datagram) ([]byte, bool) {
	id := d.hostID
	if d.kind == typeHostID {
		id = HostIDOf(d.hostKey)
	}
	if to, elsewhere := s.hostElsewhere(id); elsewhere {
		return appendRedirect(out, to), true
	}
	if self := s.self(); self.IsValid() {
		return appendRedirect(out, self), true
	}
	return out, false
}

// sweepHosts forgets stale bindings and full host lookup buckets. Under s.mu.
func (s *Server) sweepHosts(now time.Time) {
	for id, b := range s.hosts {
		if now.Sub(b.seen) > s.cfg.HostIDTTL {
			delete(s.hosts, id)
			if s.hostsBySource[b.source]--; s.hostsBySource[b.source] <= 0 {
				delete(s.hostsBySource, b.source)
			}
		}
	}
	full := time.Duration(s.cfg.HostLookupBurst) * s.cfg.HostLookupInterval
	for key, b := range s.hostLookups {
		if now.Sub(b.last) >= full {
			delete(s.hostLookups, key)
		}
	}
}
