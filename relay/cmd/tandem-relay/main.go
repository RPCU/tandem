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
	"flag"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
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

func serve(args []string) error {
	cfg := relay.DefaultConfig()
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	udpAddr := fs.String("udp", ":27460", "UDP address players reach")
	httpAddr := fs.String("http", ":8080", "HTTP address for /healthz and /metrics")
	tokensFile := fs.String("host-tokens", "/etc/tandem-relay/host-tokens", "file of hex host tokens, one per line")
	fs.BoolVar(&cfg.AllowAnyHost, "allow-any-host", false, "let any host open a session (local testing only)")
	fs.IntVar(&cfg.MaxSessions, "max-sessions", cfg.MaxSessions, "sessions open at once")
	fs.DurationVar(&cfg.IdleTimeout, "idle", cfg.IdleTimeout, "forget a client silent for this long")
	fs.DurationVar(&cfg.CodeTTL, "code-ttl", cfg.CodeTTL, "how long a join code lives")
	fs.IntVar(&cfg.CodeLookupBurst, "code-lookup-burst", cfg.CodeLookupBurst,
		"join code lookups one address may make at once (then one per 6 s)")
	fs.IntVar(&cfg.MaxCodeFailuresPerMinute, "max-code-failures", cfg.MaxCodeFailuresPerMinute,
		"failed join code lookups per minute, from everyone together, before every lookup waits")
	_ = fs.Parse(args)

	log := slog.New(slog.NewJSONHandler(os.Stdout, nil))

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

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	addr, err := net.ResolveUDPAddr("udp", *udpAddr)
	if err != nil {
		return err
	}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return err
	}
	server := relay.NewServer(cfg, log)

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { _, _ = w.Write([]byte("ok\n")) })
	mux.HandleFunc("GET /metrics", func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		sessions, clients := server.Counts()
		server.Metrics.Write(w, sessions, clients)
	})
	web := &http.Server{Addr: *httpAddr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	go func() {
		if err := web.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Error("http", "err", err)
			stop()
		}
	}()

	log.Info("relay up", "udp", conn.LocalAddr().String(), "http", *httpAddr, "host_tokens", len(cfg.HostTokens))
	err = server.Serve(ctx, conn)

	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = web.Shutdown(shutdown)
	return err
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
