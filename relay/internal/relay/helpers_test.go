package relay

import (
	"bytes"
	"net"
	"net/netip"
	"testing"
	"time"
)

type outgoing struct {
	to   netip.AddrPort
	data []byte
}

// handle runs one datagram on the players' port, on copies (process rewrites
// data in place and reuses its answer buffer).
func (s *Server) handle(from netip.AddrPort, b []byte, now time.Time) (outgoing, bool) {
	return s.handleOn(portDirect, from, b, now)
}

// handleFront runs one datagram on the fleet's entrance.
func (s *Server) handleFront(from netip.AddrPort, b []byte, now time.Time) (outgoing, bool) {
	return s.handleOn(portFront, from, b, now)
}

func dialTest(t *testing.T, relay *net.UDPAddr) *net.UDPConn {
	t.Helper()
	c, err := net.DialUDP("udp", nil, relay)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { c.Close() })
	_ = c.SetDeadline(time.Now().Add(2 * time.Second))
	return c
}

func read(t *testing.T, c *net.UDPConn) Reply {
	t.Helper()
	buf := make([]byte, 2048)
	n, err := c.Read(buf)
	if err != nil {
		t.Fatal(err)
	}
	r, err := ParseReply(buf[:n])
	if err != nil {
		t.Fatal(err)
	}
	return r
}

func ask(t *testing.T, c *net.UDPConn, req []byte) Reply {
	t.Helper()
	if _, err := c.Write(req); err != nil {
		t.Fatal(err)
	}
	return read(t, c)
}

func (s *Server) handleOn(p port, from netip.AddrPort, b []byte, now time.Time) (outgoing, bool) {
	out, _, ok := s.handleTwo(p, from, b, now)
	return out, ok
}

// handleTwo also returns the second datagram an answer may come with (an
// introduction to the other side); its `to` is invalid when there is none.
func (s *Server) handleTwo(p port, from netip.AddrPort, b []byte, now time.Time) (outgoing, outgoing, bool) {
	w := newWorker(0)
	to, reply, ok := s.process(w, p, from, bytes.Clone(b), now)
	return outgoing{to, bytes.Clone(reply)}, outgoing{w.extraTo, bytes.Clone(w.extra)}, ok
}
