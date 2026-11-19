package relay

import (
	"context"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/netip"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

func benchServer(sessions int) *Server {
	cfg := DefaultConfig()
	cfg.AllowAnyHost = true
	cfg.MaxSessions = max(sessions, cfg.MaxSessions)
	cfg.MaxPacketsPerSecond = 1 << 30
	cfg.MaxBytesPerSecond = 1 << 30
	return NewServer(cfg, slog.New(slog.NewTextHandler(io.Discard, nil)))
}

// The packet path alone: one guest's datagram to its host and the host's
// answer, through process(), no socket. Reports allocations per packet.
func BenchmarkForward(b *testing.B) {
	s := benchServer(1)
	now := time.Now()
	s.handle(hostA, RegisterHost(secret, token), now)
	out, _ := s.handle(guestA, RegisterGuest(secret), now)
	r, _ := ParseReply(out.data)
	up := Data(0, make([]byte, 200))
	down := Data(r.Channel, make([]byte, 200))
	w := newWorker(0)
	b.ReportAllocs()
	b.ResetTimer()
	for i := range b.N {
		if i&1 == 0 {
			s.process(w, portDirect, guestA, up, now)
		} else {
			binaryPutChannel(down, r.Channel)
			s.process(w, portDirect, hostA, down, now)
		}
	}
}

func binaryPutChannel(b []byte, channel uint16) {
	b[headerSize], b[headerSize+1] = byte(channel>>8), byte(channel)
}

// The packet path from many sessions at once, as several reader goroutines
// run it.
func BenchmarkForwardParallel(b *testing.B) {
	s := benchServer(256)
	now := time.Now()
	const sessions = 256
	guests := make([]netip.AddrPort, sessions)
	for i := range sessions {
		host := netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 1, byte(i >> 8), byte(i)}), 5000)
		guests[i] = netip.AddrPortFrom(netip.AddrFrom4([4]byte{10, 2, byte(i >> 8), byte(i)}), 6000)
		sec := [SecretSize]byte{byte(i), byte(i >> 8), 1}
		s.handle(host, RegisterHost(sec, token), now)
		s.handle(guests[i], RegisterGuest(sec), now)
	}
	var next atomic.Int64
	b.ReportAllocs()
	b.ResetTimer()
	b.RunParallel(func(pb *testing.PB) {
		n := int(next.Add(1))
		g := guests[n%sessions]
		w := newWorker(n)
		up := Data(0, make([]byte, 200))
		for pb.Next() {
			s.process(w, portDirect, g, up, now)
		}
	})
}

// Datagrams per second through a real relay on 127.0.0.1: pairs of a host
// and a guest bounce datagrams back and forth, a few in flight each.
func BenchmarkLoopback(b *testing.B) {
	for _, readers := range []int{1, 4} {
		for _, pairs := range []int{1, 8} {
			b.Run(fmt.Sprintf("readers=%d/pairs=%d", readers, pairs), func(b *testing.B) { loopback(b, readers, pairs) })
		}
	}
}

func loopback(b *testing.B, readers, pairs int) {
	s := benchServer(pairs)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	conns, err := Listen("127.0.0.1:0", readers)
	if err != nil {
		b.Fatal(err)
	}
	go s.ServeAll(ctx, conns, nil)
	relayAddr := conns[0].LocalAddr().(*net.UDPAddr)

	dial := func() *net.UDPConn {
		c, err := net.DialUDP("udp", nil, relayAddr)
		if err != nil {
			b.Fatal(err)
		}
		_ = c.SetReadBuffer(1 << 20)
		b.Cleanup(func() { c.Close() })
		return c
	}
	ask := func(c *net.UDPConn, req []byte) Reply {
		buf := make([]byte, 2048)
		for range 20 {
			_, _ = c.Write(req)
			_ = c.SetReadDeadline(time.Now().Add(200 * time.Millisecond))
			if n, err := c.Read(buf); err == nil {
				r, _ := ParseReply(buf[:n])
				return r
			}
		}
		b.Fatal("no answer")
		return Reply{}
	}
	type pair struct {
		host, guest *net.UDPConn
		channel     uint16
	}
	ps := make([]pair, pairs)
	for i := range ps {
		sec := [SecretSize]byte{byte(i), 7}
		ps[i].host, ps[i].guest = dial(), dial()
		ask(ps[i].host, RegisterHost(sec, token))
		ps[i].channel = ask(ps[i].guest, RegisterGuest(sec)).Channel
	}

	const window = 8
	per := b.N/pairs + 1
	payload := make([]byte, 200)
	b.ResetTimer()
	start := time.Now()
	var wg sync.WaitGroup
	for _, p := range ps {
		wg.Add(2)
		// The host answers every datagram it gets.
		go func() {
			defer wg.Done()
			buf := make([]byte, 2048)
			down := Data(p.channel, payload)
			for got := 0; got < per; got++ {
				_ = p.host.SetReadDeadline(time.Now().Add(time.Second))
				if _, err := p.host.Read(buf); err != nil {
					return
				}
				_, _ = p.host.Write(down)
			}
		}()
		// The guest keeps `window` datagrams in flight.
		go func() {
			defer wg.Done()
			buf := make([]byte, 2048)
			up := Data(0, payload)
			for range window {
				_, _ = p.guest.Write(up)
			}
			for got := 0; got < per; got++ {
				_ = p.guest.SetReadDeadline(time.Now().Add(time.Second))
				if _, err := p.guest.Read(buf); err != nil {
					// A datagram lost on loopback: put one back in flight.
					_, _ = p.guest.Write(up)
					continue
				}
				_, _ = p.guest.Write(up)
			}
		}()
	}
	wg.Wait()
	elapsed := time.Since(start)
	b.StopTimer()
	// Each round trip is two forwards.
	b.ReportMetric(float64(2*per*pairs)/elapsed.Seconds(), "forwards/s")
}
