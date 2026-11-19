package relay

import (
	"net/netip"
	"sync"
	"sync/atomic"
	"time"
)

// Self-diagnosis of the path players reach us by. The relay knows a client
// by the address its datagrams come from: a proxy, a load balancer or a
// masquerade on the way that rewrites it (kube-proxy's Cluster policy, a
// CNI's SNAT, Envoy) makes every player look like a few internal
// addresses, which breaks hole punching (we would introduce those), the
// connection check and every per-source limit. Nothing in the protocol can
// tell; what the relay can see is the addresses themselves.
//
// It samples per address, never per packet: the clients registered (the
// table the data path already keeps) and the sources of registration-type
// requests (registrations, code lookups, probes: control traffic, a few per
// client), each once per window, at most sourceWatchCap of them.

const (
	// Sources kept per window, beyond which a new one is not added (the
	// verdict needs a sample, not a census).
	sourceWatchCap = 1024
	// Distinct sources needed before judging "mostly internal", and before
	// judging "everyone from one or two addresses".
	minSourcesInternal = 4
	minSourcesFew      = 8
	sourceWindow       = time.Minute
)

// The shared address space (RFC 6598): carrier-grade NAT and many
// clusters' pod networks, never a player's address as the internet shows it.
var sharedSpace = netip.MustParsePrefix("100.64.0.0/10")

type sourceWatch struct {
	mu          sync.Mutex
	seen        map[netip.AddrPort]struct{}
	windowStart time.Time
	lastWarn    time.Time
	suspected   atomic.Bool
}

// SourceVerdict is what the last check found.
type SourceVerdict struct {
	// Distinct sources (address and port) sampled, those internal, and the
	// distinct addresses among them (counted up to 3).
	Sources, Internal, Addresses int
	// "internal" (mostly private, cluster or link-local addresses), "few"
	// (everyone from one or two addresses), or "" (looks right).
	Reason string
	// One internal address seen, to tell which hop rewrote them.
	Example netip.Addr
}

// observe notes a control datagram's source (its kind may come from an
// unregistered address). Not on the data path.
func (s *Server) observe(from netip.AddrPort) {
	w := &s.watch
	w.mu.Lock()
	if _, ok := w.seen[from]; !ok && len(w.seen) < sourceWatchCap {
		w.seen[from] = struct{}{}
	}
	w.mu.Unlock()
}

// samples reports whether a datagram of this kind is a source sample.
func samples(kind msgType) bool {
	switch kind {
	case typeRegisterHost, typeRegisterHostKeep, typeRegisterGuest, typeRejoinGuest, typeResolveCode, typeProbe:
		return true
	}
	return false
}

// internal is an address no player on the internet is seen from: private,
// loopback, link-local, the shared space, or one of the operator's
// prefixes (a cluster's pod or service network outside those).
func (s *Server) internal(a netip.Addr) bool {
	a = a.Unmap()
	if !a.IsGlobalUnicast() || a.IsPrivate() || sharedSpace.Contains(a) {
		return true
	}
	for _, p := range s.cfg.InternalPrefixes {
		if p.Contains(a) {
			return true
		}
	}
	return false
}

// checkSources judges the window's sources and the registered clients
// (every 10 s, with the sweep), sets the gauge, and warns at most once a
// minute while it suspects a NAT. A window lasts a minute.
func (s *Server) checkSources(now time.Time) SourceVerdict {
	if !s.cfg.ExpectPublicClients {
		return SourceVerdict{}
	}
	var registered []netip.AddrPort
	for i := range s.clients.shards {
		sh := &s.clients.shards[i]
		sh.mu.RLock()
		for a := range sh.m {
			registered = append(registered, a)
		}
		sh.mu.RUnlock()
	}

	w := &s.watch
	w.mu.Lock()
	defer w.mu.Unlock()
	for _, a := range registered {
		if len(w.seen) >= sourceWatchCap {
			break
		}
		w.seen[a] = struct{}{}
	}
	var v SourceVerdict
	var addrs [3]netip.Addr
	for a := range w.seen {
		v.Sources++
		ip := a.Addr().Unmap()
		if s.internal(ip) {
			v.Internal++
			if !v.Example.IsValid() {
				v.Example = ip
			}
		}
		known := false
		for _, b := range addrs[:v.Addresses] {
			known = known || b == ip
		}
		if !known && v.Addresses < len(addrs) {
			addrs[v.Addresses] = ip
			v.Addresses++
		}
	}
	switch {
	case v.Sources >= minSourcesInternal && v.Internal*2 > v.Sources:
		v.Reason = "internal"
	case v.Sources >= minSourcesFew && v.Addresses <= 2:
		v.Reason = "few"
	}
	w.suspected.Store(v.Reason != "")
	if v.Reason != "" && now.Sub(w.lastWarn) >= sourceWindow {
		w.lastWarn = now
		attrs := []any{"reason", v.Reason, "sources", v.Sources, "internal", v.Internal, "addresses", v.Addresses}
		if v.Example.IsValid() {
			attrs = append(attrs, "example", v.Example)
		}
		s.log.Warn("client addresses look NATed: hole punching and per-player limits will be wrong; "+
			"see docs/relay.md, \"Relay pods behind kernel forwarding\" (--expect-public-clients=false on a LAN)", attrs...)
	}
	if now.Sub(w.windowStart) >= sourceWindow {
		clear(w.seen)
		w.windowStart = now
	}
	return v
}

// SourceNATSuspected is the last check's verdict.
func (s *Server) SourceNATSuspected() bool { return s.watch.suspected.Load() }
