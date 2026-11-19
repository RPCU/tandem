// proxyv2-forward: the test forwarder of package proxyfwd, for
// tests/relay_loopback.sh.
//
//	proxyv2-forward LISTEN TARGET [every] [from=ADDR]
package main

import (
	"context"
	"fmt"
	"net/netip"
	"os"

	"github.com/Banh-Canh/tandem/relay/internal/proxyfwd"
)

func main() {
	if len(os.Args) < 3 {
		fmt.Fprintln(os.Stderr, "usage: proxyv2-forward LISTEN TARGET [every]")
		os.Exit(2)
	}
	target, err := netip.ParseAddrPort(os.Args[2])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	f, err := proxyfwd.Listen(os.Args[1], target)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	// [every] [from=ADDR]: a header on every datagram; the address the
	// sockets toward the relay are bound to.
	for _, arg := range os.Args[3:] {
		switch {
		case arg == "every":
			f.EveryDatagram = true
		case len(arg) > 5 && arg[:5] == "from=":
			if f.From, err = netip.ParseAddr(arg[5:]); err != nil {
				fmt.Fprintln(os.Stderr, err)
				os.Exit(2)
			}
		}
	}
	fmt.Printf("forwarding %v to %v\n", f.Addr(), target)
	f.Serve(context.Background())
}
