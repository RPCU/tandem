// Package proxyfwd is a UDP forwarder that tells what is behind it in PROXY
// v2 headers, as playit.gg's agent does: one socket toward the relay per
// client, the header sent alone first on each (agent 0.16 on), or in front
// of every datagram (EveryDatagram: older agents). For tests only
// (tests/relay_loopback.sh, the relay's own); not shipped.
package proxyfwd

import (
	"context"
	"net"
	"net/netip"
	"sync"
	"time"

	"github.com/Banh-Canh/tandem/relay/internal/relay"
)

type Forwarder struct {
	public        *net.UDPConn
	target        netip.AddrPort
	EveryDatagram bool
	// The address its sockets toward the relay are bound to (zero: any).
	From netip.Addr

	mu    sync.Mutex
	flows map[netip.AddrPort]*net.UDPConn
}

// Listen opens the forwarder's public socket on addr, toward target.
func Listen(addr string, target netip.AddrPort) (*Forwarder, error) {
	pc, err := net.ListenPacket("udp", addr)
	if err != nil {
		return nil, err
	}
	return &Forwarder{public: pc.(*net.UDPConn), target: target, flows: map[netip.AddrPort]*net.UDPConn{}}, nil
}

func (f *Forwarder) Addr() netip.AddrPort { return f.public.LocalAddr().(*net.UDPAddr).AddrPort() }

// Serve forwards until ctx ends.
func (f *Forwarder) Serve(ctx context.Context) {
	go func() {
		<-ctx.Done()
		_ = f.public.Close()
		f.mu.Lock()
		for _, c := range f.flows {
			_ = c.Close()
		}
		f.mu.Unlock()
	}()
	buf := make([]byte, 2048)
	for {
		n, client, err := f.public.ReadFromUDPAddrPort(buf)
		if err != nil {
			return
		}
		flow, header, err := f.flow(client)
		if err != nil {
			continue
		}
		out := buf[:n]
		if f.EveryDatagram {
			out = append(header, out...)
		}
		_, _ = flow.Write(out)
	}
}

// flow is the client's socket toward the relay, opened (and its header sent
// alone) on its first datagram.
func (f *Forwarder) flow(client netip.AddrPort) (*net.UDPConn, []byte, error) {
	header := relay.AppendProxyV2(nil, client, f.target)
	f.mu.Lock()
	defer f.mu.Unlock()
	if c := f.flows[client]; c != nil {
		return c, header, nil
	}
	var local *net.UDPAddr
	if f.From.IsValid() {
		local = net.UDPAddrFromAddrPort(netip.AddrPortFrom(f.From, 0))
	}
	c, err := net.DialUDP("udp", local, net.UDPAddrFromAddrPort(f.target))
	if err != nil {
		return nil, nil, err
	}
	f.flows[client] = c
	if !f.EveryDatagram {
		_, _ = c.Write(header)
	}
	// Answers come back on the flow's socket, with no header, and go to
	// its client.
	go func() {
		buf := make([]byte, 2048)
		for {
			_ = c.SetReadDeadline(time.Now().Add(10 * time.Minute))
			n, err := c.Read(buf)
			if err != nil {
				return
			}
			_, _ = f.public.WriteToUDPAddrPort(buf[:n], client)
		}
	}()
	return c, header, nil
}
