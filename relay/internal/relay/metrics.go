package relay

import (
	"fmt"
	"io"
	"sync/atomic"
)

type dropReason int

const (
	dropMalformed dropReason = iota
	dropBadToken
	dropUnknown
	dropRate
	dropNoChannel
	dropReasons
)

var dropNames = [dropReasons]string{"malformed", "bad_token", "unknown_client", "rate_limited", "no_channel"}

type lookupResult int

const (
	lookupFound lookupResult = iota
	lookupUnknown
	lookupLimited
	// Sent to the replica owning the code.
	lookupRedirected
	lookupResults
)

var lookupNames = [lookupResults]string{"found", "unknown", "limited", "redirected"}

// Registrations refused (a bad token is a drop: bad_token).
type refusal int

const (
	refusedNoSession refusal = iota
	refusedFull
	refusedTooMany
	refusedTaken
	refusedDraining
	refusals
)

var refusalNames = [refusals]string{"no_session", "session_full", "too_many_sessions", "session_taken", "draining"}

type redirectKind int

const (
	// A new session, to us from the entrance or to a peer while we drain.
	redirectHost redirectKind = iota
	// A guest's secret, from the entrance to us.
	redirectGuest
	redirectKinds
)

var redirectNames = [redirectKinds]string{"host", "guest"}

// Counters of the forwarding path, one set per reader so readers on other
// cores do not share a cache line.
type forwardCounters struct {
	packets atomic.Uint64
	bytes   atomic.Uint64
	_       [48]byte
}

const counterSlots = 64

// Metrics are plain counters, written in the Prometheus text format.
type Metrics struct {
	forward   [counterSlots]forwardCounters
	drops     [dropReasons]atomic.Uint64
	codes     atomic.Uint64
	reclaimed atomic.Uint64
	lookups   [lookupResults]atomic.Uint64
	refusals  [refusals]atomic.Uint64
	redirects [redirectKinds]atomic.Uint64
	// Guests and hosts introduced to each other for a direct path.
	introductions atomic.Uint64
}

func (m *Metrics) codeIssued() { m.codes.Add(1) }

func (m *Metrics) codeReclaimed() { m.reclaimed.Add(1) }

func (m *Metrics) introduced() { m.introductions.Add(1) }

func (m *Metrics) lookup(result lookupResult) { m.lookups[result].Add(1) }

func (m *Metrics) refused(why refusal) { m.refusals[why].Add(1) }

func (m *Metrics) redirect(kind redirectKind) { m.redirects[kind].Add(1) }

func (m *Metrics) forwarded(slot, payload int) {
	c := &m.forward[slot%counterSlots]
	c.packets.Add(1)
	c.bytes.Add(uint64(payload))
}

func (m *Metrics) dropped(reason dropReason) { m.drops[reason].Add(1) }

// Snapshot is every counter at one moment (expvar shows it as JSON).
type Snapshot struct {
	Sessions, Clients int
	Draining          bool
	Packets, Bytes    uint64
	Drops             map[string]uint64
	CodesIssued       uint64
	CodesReclaimed    uint64
	Lookups           map[string]uint64
	Refusals          map[string]uint64
	Redirects         map[string]uint64
	Introductions     uint64
}

// Snapshot reads the counters, with the live session and client counts.
func (s *Server) Snapshot() Snapshot {
	m := &s.Metrics
	sessions, clients := s.Counts()
	out := Snapshot{
		Sessions: sessions, Clients: clients, Draining: s.Draining(),
		CodesIssued: m.codes.Load(), CodesReclaimed: m.reclaimed.Load(), Introductions: m.introductions.Load(),
		Drops: map[string]uint64{}, Lookups: map[string]uint64{},
		Refusals: map[string]uint64{}, Redirects: map[string]uint64{},
	}
	for i := range m.forward {
		out.Packets += m.forward[i].packets.Load()
		out.Bytes += m.forward[i].bytes.Load()
	}
	for i, name := range dropNames {
		out.Drops[name] = m.drops[i].Load()
	}
	for i, name := range lookupNames {
		out.Lookups[name] = m.lookups[i].Load()
	}
	for i, name := range refusalNames {
		out.Refusals[name] = m.refusals[i].Load()
	}
	for i, name := range redirectNames {
		out.Redirects[name] = m.redirects[i].Load()
	}
	return out
}

// WriteMetrics prints the counters in the Prometheus text format.
func (s *Server) WriteMetrics(w io.Writer) {
	snap := s.Snapshot()
	gauge := func(name, help string, v int) {
		fmt.Fprintf(w, "# HELP %s %s\n# TYPE %s gauge\n%s %d\n", name, help, name, name, v)
	}
	counter := func(name, help string, v uint64) {
		fmt.Fprintf(w, "# HELP %s %s\n# TYPE %s counter\n%s %d\n", name, help, name, name, v)
	}
	labelled := func(name, help, label string, names []string, values map[string]uint64) {
		fmt.Fprintf(w, "# HELP %s %s\n# TYPE %s counter\n", name, help, name)
		for _, n := range names {
			fmt.Fprintf(w, "%s{%s=%q} %d\n", name, label, n, values[n])
		}
	}
	draining := 0
	if snap.Draining {
		draining = 1
	}
	gauge("tandem_relay_sessions", "Open co-op sessions.", snap.Sessions)
	gauge("tandem_relay_clients", "Registered hosts and guests.", snap.Clients)
	gauge("tandem_relay_draining", "1 while the relay takes no new session (shutting down).", draining)
	counter("tandem_relay_forwarded_packets_total", "Datagrams forwarded.", snap.Packets)
	counter("tandem_relay_forwarded_bytes_total", "Payload bytes forwarded.", snap.Bytes)
	labelled("tandem_relay_dropped_total", "Datagrams dropped, by reason.", "reason", dropNames[:], snap.Drops)
	counter("tandem_relay_codes_issued_total", "Join codes issued.", snap.CodesIssued)
	counter("tandem_relay_codes_reclaimed_total", "Join codes given back to their host after a restart.", snap.CodesReclaimed)
	labelled("tandem_relay_code_lookups_total", "Join code lookups, by result.", "result", lookupNames[:], snap.Lookups)
	labelled("tandem_relay_refused_total", "Registrations refused, by reason.", "reason", refusalNames[:], snap.Refusals)
	counter("tandem_relay_introductions_total", "Guests introduced to their host for a direct path.", snap.Introductions)
	labelled("tandem_relay_redirects_total", "Registrations sent to another replica (or from the entrance to us).", "kind", redirectNames[:], snap.Redirects)
}
