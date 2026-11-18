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

// Metrics are plain counters, written in the Prometheus text format.
type Metrics struct {
	packets atomic.Uint64
	bytes   atomic.Uint64
	drops   [dropReasons]atomic.Uint64
}

func (m *Metrics) forwarded(payload int) {
	m.packets.Add(1)
	m.bytes.Add(uint64(payload))
}

func (m *Metrics) dropped(reason dropReason) { m.drops[reason].Add(1) }

// Write prints the metrics, with the live session and client counts.
func (m *Metrics) Write(w io.Writer, sessions, clients int) {
	fmt.Fprintf(w, "# HELP tandem_relay_sessions Open co-op sessions.\n# TYPE tandem_relay_sessions gauge\ntandem_relay_sessions %d\n", sessions)
	fmt.Fprintf(w, "# HELP tandem_relay_clients Registered hosts and guests.\n# TYPE tandem_relay_clients gauge\ntandem_relay_clients %d\n", clients)
	fmt.Fprintf(w, "# HELP tandem_relay_forwarded_packets_total Datagrams forwarded.\n# TYPE tandem_relay_forwarded_packets_total counter\ntandem_relay_forwarded_packets_total %d\n", m.packets.Load())
	fmt.Fprintf(w, "# HELP tandem_relay_forwarded_bytes_total Payload bytes forwarded.\n# TYPE tandem_relay_forwarded_bytes_total counter\ntandem_relay_forwarded_bytes_total %d\n", m.bytes.Load())
	fmt.Fprintf(w, "# HELP tandem_relay_dropped_total Datagrams dropped, by reason.\n# TYPE tandem_relay_dropped_total counter\n")
	for i, name := range dropNames {
		fmt.Fprintf(w, "tandem_relay_dropped_total{reason=%q} %d\n", name, m.drops[i].Load())
	}
}
