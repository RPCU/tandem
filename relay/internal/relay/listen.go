package relay

import (
	"context"
	"net"
	"syscall"
)

// Listen opens up to n UDP sockets on one address. Where the system has
// SO_REUSEPORT (Linux), each is its own socket on the same port and the
// kernel spreads the clients over them by address, so n reader goroutines
// run the data path on n cores with a client always on the same one.
// Elsewhere it opens one. A port of 0 is picked once, for all of them.
func Listen(address string, n int) ([]*net.UDPConn, error) {
	if n < 1 || !reusePortSupported {
		n = 1
	}
	lc := net.ListenConfig{}
	if n > 1 {
		lc.Control = func(_, _ string, c syscall.RawConn) error {
			var err error
			if cerr := c.Control(func(fd uintptr) { err = setReusePort(fd) }); cerr != nil {
				return cerr
			}
			return err
		}
	}
	var conns []*net.UDPConn
	for range n {
		pc, err := lc.ListenPacket(context.Background(), "udp", address)
		if err != nil {
			for _, c := range conns {
				_ = c.Close()
			}
			return nil, err
		}
		conn := pc.(*net.UDPConn)
		// The kernel's default receive buffer (~200 KiB) drops bursts.
		_ = conn.SetReadBuffer(4 << 20)
		_ = conn.SetWriteBuffer(4 << 20)
		conns = append(conns, conn)
		address = conn.LocalAddr().String()
	}
	return conns, nil
}
