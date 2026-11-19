// tandem-relay forwards co-op traffic between players who cannot reach each
// other directly. See internal/relay for the protocol.
//
//	tandem-relay serve [flags]   run the relay
//	tandem-relay token           print a new host token
package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"expvar"
	"flag"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"net/netip"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/Banh-Canh/tandem/relay/internal/relay"
)

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	switch os.Args[1] {
	case "serve":
		if err := serve(os.Args[2:]); err != nil {
			fmt.Fprintln(os.Stderr, "tandem-relay:", err)
			os.Exit(1)
		}
	case "token":
		var t [relay.TokenSize]byte
		_, _ = rand.Read(t[:])
		fmt.Println(hex.EncodeToString(t[:]))
	default:
		usage()
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: tandem-relay serve [flags] | tandem-relay token")
	os.Exit(2)
}

// env is a flag's default from the environment (Kubernetes sets these).
func env(name, fallback string) string {
	if v, ok := os.LookupEnv(name); ok {
		return v
	}
	return fallback
}

func serve(args []string) error {
	cfg := relay.DefaultConfig()
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	udpAddr := fs.String("udp", ":27460", "UDP address players reach")
	frontAddr := fs.String("front", env("TANDEM_RELAY_FRONT", ""),
		"UDP address of the fleet's shared entrance (only redirects; needs --peers)")
	httpAddr := fs.String("http", ":8080", "HTTP address for /healthz, /readyz, /metrics and /debug/vars")
	tokensFile := fs.String("host-tokens", "/etc/tandem-relay/host-tokens", "file of hex host tokens, one per line")
	peers := fs.String("peers", env("TANDEM_RELAY_PEERS", ""),
		"every replica's public address (ip:port), in shard order, comma-separated")
	shard := fs.String("shard", env("TANDEM_RELAY_SHARD", "0"),
		"this replica's index in --peers, or a name ending in it (a StatefulSet pod's: tandem-relay-1)")
	readers := fs.Int("readers", min(runtime.NumCPU(), 8), "sockets reading the UDP port at once (SO_REUSEPORT)")
	drain := fs.Duration("drain-timeout", 60*time.Second,
		"on SIGTERM, how long running sessions may go on (no new ones) before the relay exits")
	fs.BoolVar(&cfg.AllowAnyHost, "allow-any-host", false, "let any host open a session (local testing only)")
	fs.IntVar(&cfg.MaxSessions, "max-sessions", cfg.MaxSessions, "sessions open at once")
	fs.DurationVar(&cfg.IdleTimeout, "idle", cfg.IdleTimeout, "forget a client silent for this long")
	fs.DurationVar(&cfg.CodeTTL, "code-ttl", cfg.CodeTTL, "how long a join code lives")
	fs.IntVar(&cfg.CodeLookupBurst, "code-lookup-burst", cfg.CodeLookupBurst,
		"join code lookups one address may make at once (then one per 6 s)")
	fs.IntVar(&cfg.MaxCodeFailuresPerMinute, "max-code-failures", cfg.MaxCodeFailuresPerMinute,
		"failed join code lookups per minute, from everyone together, for the whole fleet (each replica takes its share)")
	_ = fs.Parse(args)

	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))

	var err error
	if cfg.Peers, err = parsePeers(*peers); err != nil {
		return err
	}
	if cfg.Shard, err = parseShard(*shard); err != nil {
		return err
	}
	if err := cfg.Validate(); err != nil {
		return err
	}
	if *frontAddr != "" && len(cfg.Peers) == 0 {
		return errors.New("--front sends players to the replicas in --peers: give them")
	}

	if !cfg.AllowAnyHost {
		tokens, err := readTokens(*tokensFile)
		if err != nil {
			return err
		}
		if len(tokens) == 0 {
			return fmt.Errorf("%s has no host token (tandem-relay token makes one)", *tokensFile)
		}
		cfg.HostTokens = tokens
	} else {
		log.Warn("any host may open a session: do not expose this relay")
	}

	direct, err := relay.Listen(*udpAddr, *readers)
	if err != nil {
		return err
	}
	var front []*net.UDPConn
	if *frontAddr != "" {
		if front, err = relay.Listen(*frontAddr, 1); err != nil {
			return err
		}
	}
	server := relay.NewServer(cfg, log)

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { _, _ = w.Write([]byte("ok\n")) })
	// Not ready while draining: the entrance's load balancer stops sending
	// players here.
	mux.HandleFunc("GET /readyz", func(w http.ResponseWriter, _ *http.Request) {
		if server.Draining() {
			http.Error(w, "draining", http.StatusServiceUnavailable)
			return
		}
		_, _ = w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("GET /metrics", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		server.WriteMetrics(w)
	})
	expvar.Publish("tandem_relay", expvar.Func(func() any { return server.Snapshot() }))
	mux.Handle("GET /debug/vars", expvar.Handler())
	listener, err := net.Listen("tcp", *httpAddr)
	if err != nil {
		return err
	}
	web := &http.Server{Handler: mux, ReadHeaderTimeout: 5 * time.Second}

	ctx, stop := context.WithCancel(context.Background())
	defer stop()
	go func() {
		if err := web.Serve(listener); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Error("http", "err", err)
			stop()
		}
	}()

	// SIGTERM (Kubernetes, systemd): drain, then exit once the sessions are
	// gone or the deadline passed. A second signal exits at once.
	signals := make(chan os.Signal, 2)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		select {
		case <-signals:
		case <-ctx.Done():
			return
		}
		server.Drain()
		deadline := time.Now().Add(*drain)
		tick := time.NewTicker(time.Second)
		defer tick.Stop()
		for {
			sessions, _ := server.Counts()
			if sessions == 0 || !time.Now().Before(deadline) {
				log.Info("drained", "sessions_left", sessions)
				stop()
				return
			}
			select {
			case <-signals:
				stop()
				return
			case <-tick.C:
			}
		}
	}()

	attrs := []any{
		"udp", direct[0].LocalAddr().String(), "readers", len(direct), "http", listener.Addr().String(),
		"host_tokens", len(cfg.HostTokens),
	}
	if len(cfg.Peers) > 0 {
		attrs = append(attrs, "shard", cfg.Shard, "replicas", len(cfg.Peers), "self", cfg.Peers[cfg.Shard].String())
	}
	if len(front) > 0 {
		attrs = append(attrs, "front", front[0].LocalAddr().String())
	}
	log.Info("relay up", attrs...)
	err = server.ServeAll(ctx, direct, front)

	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = web.Shutdown(shutdown)
	return err
}

func parsePeers(text string) ([]netip.AddrPort, error) {
	var out []netip.AddrPort
	for _, field := range strings.Split(text, ",") {
		if field = strings.TrimSpace(field); field == "" {
			continue
		}
		// An address, not a name: the relay makes no DNS query (no egress).
		p, err := netip.ParseAddrPort(field)
		if err != nil {
			return nil, fmt.Errorf("--peers: %q is not ip:port", field)
		}
		out = append(out, p)
	}
	return out, nil
}

// parseShard reads "2" or a name ending in "-2".
func parseShard(text string) (int, error) {
	if i := strings.LastIndexByte(text, '-'); i >= 0 {
		text = text[i+1:]
	}
	n, err := strconv.Atoi(text)
	if err != nil || n < 0 {
		return 0, fmt.Errorf("--shard: %q is not an index", text)
	}
	return n, nil
}

func readTokens(path string) ([][relay.TokenSize]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var tokens [][relay.TokenSize]byte
	scanner := bufio.NewScanner(f)
	for line := 1; scanner.Scan(); line++ {
		text := strings.TrimSpace(scanner.Text())
		if text == "" || strings.HasPrefix(text, "#") {
			continue
		}
		raw, err := hex.DecodeString(text)
		if err != nil || len(raw) != relay.TokenSize {
			return nil, fmt.Errorf("%s:%d: a token is %d hex bytes", path, line, relay.TokenSize)
		}
		tokens = append(tokens, [relay.TokenSize]byte(raw))
	}
	return tokens, scanner.Err()
}
