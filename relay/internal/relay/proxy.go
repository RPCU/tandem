package relay

import (
	"encoding/binary"
	"errors"
	"net/netip"
	"time"
)

// The PROXY protocol, version 2 (HAProxy's proxy-protocol.txt, section 2.2),
// as a tunnel in front of the relay sends it (Config.ProxyProtocol). playit.gg's
// agent keeps one socket per client flow and sends the header once, as a
// datagram of its own, before the flow's first one (agent 0.16 and later;
// earlier ones put it in front of every datagram): the relay learns the
// flow's real client there and keeps it (flows). Either form is read.
//
//	signature (12) | version 2, command (1) | family, transport (1) | length (2)
//	IPv4: source (4) | destination (4) | source port (2) | destination port (2)
//	IPv6: source (16) | destination (16) | source port (2) | destination port (2)
//	then TLVs to the length (skipped)
const proxySignature = "\r\n\r\n\x00\r\nQUIT\n"

const proxyHeaderSize = 16

var errProxyHeader = errors.New("not a PROXY v2 header")

// parseProxyV2 reads a header at the start of b: the client's address, the
// header's size (the payload follows), and whether it is a LOCAL one (the
// proxy speaking for itself: no client).
func parseProxyV2(b []byte) (client netip.AddrPort, size int, local bool, err error) {
	if len(b) < proxyHeaderSize || string(b[:12]) != proxySignature || b[12]>>4 != 2 {
		return client, 0, false, errProxyHeader
	}
	length := int(binary.BigEndian.Uint16(b[14:]))
	size = proxyHeaderSize + length
	if len(b) < size {
		return client, 0, false, errProxyHeader // truncated
	}
	switch b[12] & 0xF {
	case 0: // LOCAL
		return client, size, true, nil
	case 1: // PROXY
	default:
		return client, 0, false, errProxyHeader
	}
	a := b[proxyHeaderSize:size]
	switch b[13] >> 4 {
	case 1: // AF_INET
		if len(a) < 12 {
			return client, 0, false, errProxyHeader
		}
		client = netip.AddrPortFrom(netip.AddrFrom4([4]byte(a[:4])), binary.BigEndian.Uint16(a[8:]))
	case 2: // AF_INET6
		if len(a) < 36 {
			return client, 0, false, errProxyHeader
		}
		client = netip.AddrPortFrom(netip.AddrFrom16([16]byte(a[:16])).Unmap(), binary.BigEndian.Uint16(a[32:]))
	default: // AF_UNSPEC, AF_UNIX: no address to go by
		return client, 0, false, errProxyHeader
	}
	if client.Port() == 0 || client.Addr().IsUnspecified() {
		return netip.AddrPort{}, 0, false, errProxyHeader
	}
	return client, size, false, nil
}

// proxySize is the size of the PROXY v2 (command PROXY) header at the start
// of b, 0 when there is none whole.
func proxySize(b []byte) int {
	if len(b) < proxyHeaderSize || b[12] != 0x21 || string(b[:12]) != proxySignature {
		return 0
	}
	if size := proxyHeaderSize + int(binary.BigEndian.Uint16(b[14:])); size <= len(b) {
		return size
	}
	return 0
}

// AppendProxyV2 builds a PROXY v2 header (UDP) for a client (tests, and a
// forwarder in front of a relay).
func AppendProxyV2(b []byte, client, server netip.AddrPort) []byte {
	b = append(b, proxySignature...)
	b = append(b, 0x21) // version 2, PROXY
	c, s := client.Addr().Unmap(), server.Addr().Unmap()
	if c.Is4() && s.Is4() {
		b = append(b, 0x12) // AF_INET, DGRAM
		b = binary.BigEndian.AppendUint16(b, 12)
		cr, sr := c.As4(), s.As4()
		b = append(append(b, cr[:]...), sr[:]...)
	} else {
		b = append(b, 0x22) // AF_INET6, DGRAM
		b = binary.BigEndian.AppendUint16(b, 36)
		cr, sr := c.As16(), s.As16()
		b = append(append(b, cr[:]...), sr[:]...)
	}
	b = binary.BigEndian.AppendUint16(b, client.Port())
	return binary.BigEndian.AppendUint16(b, server.Port())
}

func (s *Server) trusted(from netip.AddrPort) bool {
	a := from.Addr().Unmap()
	for _, p := range s.cfg.TrustedProxies {
		if p.Contains(a) {
			return true
		}
	}
	return false
}

// A proxy socket's client, from its header (under s.mu).
type flow struct {
	client netip.AddrPort
	seen   time.Time
}

// unwrapProxy reads a datagram that starts like a PROXY header: from a
// trusted proxy only, well formed, PROXY (not LOCAL). A header alone notes
// its flow and is not answered; one with a payload (an older agent) gives
// the payload and its client. ok false: drop it.
func (s *Server) unwrapProxy(from netip.AddrPort, b []byte, now time.Time) (payload []byte, client netip.AddrPort, ok bool) {
	// Data (an older agent's): only a registered client's socket forwards,
	// and only a trusted proxy's header registered it, so the header is
	// skipped unread.
	if size := proxySize(b); size > 0 && len(b) >= size+headerSize+2 && b[size+4] == byte(typeData) {
		return b[size:], client, true
	}
	if !s.trusted(from) {
		s.Metrics.dropped(dropUntrusted)
		return nil, client, false
	}
	client, size, local, err := parseProxyV2(b)
	if err != nil {
		s.Metrics.dropped(dropProxyHeader)
		return nil, client, false
	}
	if local {
		return nil, client, false
	}
	payload = b[size:]
	// Data comes from a registered client, which knows its own: only the rest
	// notes (or refreshes) the flow.
	if len(payload) < headerSize || payload[4] != byte(typeData) {
		s.noteFlow(from, client, now)
	}
	return payload, client, len(payload) > 0
}

func (s *Server) flowCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.flows)
}

func (s *Server) noteFlow(from, client netip.AddrPort, now time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, known := s.flows[from]; !known && len(s.flows) >= s.cfg.MaxFlows {
		return
	}
	s.flows[from] = flow{client, now}
}

// clientOf is the real client behind a datagram without a header: its
// flow's, or none (ok false) when the flow began before we did (a relay
// restarted under a running proxy: the flow is served by its proxy
// socket, but gets no Observed, no introduction, and the proxy's shared
// lookup budget).
func (s *Server) clientOf(from netip.AddrPort, now time.Time) (netip.AddrPort, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	f, ok := s.flows[from]
	if ok {
		f.seen = now
		s.flows[from] = f
	}
	return f.client, ok
}
