package relay

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/subtle"
	"errors"
	"log/slog"
	"net"
	"net/netip"
	"sync"
	"time"
)

// Config bounds what one relay instance accepts.
type Config struct {
	// Only hosts presenting one of these tokens may open a session, so the
	// relay is not an open bandwidth donor. Guests need only the session secret.
	HostTokens [][TokenSize]byte
	// For local testing only: any host may open a session.
	AllowAnyHost bool

	MaxSessions int
	MaxGuests   int
	IdleTimeout time.Duration
	// Per client, over one-second windows.
	MaxPacketsPerSecond int
	MaxBytesPerSecond   int

	// Join codes live this long (and die with their session).
	CodeTTL time.Duration
	// A host asking for a new code sooner than this gets its current one.
	CodeRenewInterval time.Duration
	// Code lookups per source (an IPv4 address, or an IPv6 /64): a burst,
	// then one per interval. A source over it is told once, then ignored.
	CodeLookupBurst    int
	CodeLookupInterval time.Duration
	// Failed lookups per minute from all sources together: past it, every
	// lookup is refused until the minute ends, so many sources together
	// cannot try codes faster either.
	MaxCodeFailuresPerMinute int
	// Sources tracked at once; a new one beyond it is ignored.
	MaxLookupSources int
}

// DefaultConfig suits a small group's relay.
func DefaultConfig() Config {
	return Config{
		MaxSessions:         256,
		MaxGuests:           3,
		IdleTimeout:         2 * time.Minute,
		MaxPacketsPerSecond: 2000,
		MaxBytesPerSecond:   4 << 20,

		CodeTTL:                  10 * time.Minute,
		CodeRenewInterval:        10 * time.Second,
		CodeLookupBurst:          5,
		CodeLookupInterval:       6 * time.Second,
		MaxCodeFailuresPerMinute: 120,
		MaxLookupSources:         4096,
	}
}

type client struct {
	addr     netip.AddrPort
	session  *session
	channel  uint16 // 0 for the host
	lastSeen time.Time

	window      time.Time
	windowPkts  int
	windowBytes int
}

type session struct {
	secret      [SecretSize]byte
	host        *client
	guests      map[uint16]*client
	nextChannel uint16

	// Its join code ("" none), when it was issued and when it expires.
	code        string
	codeIssued  time.Time
	codeExpires time.Time
}

// A source's code lookups: tokens left, refilled one per interval since last.
type lookupBucket struct {
	tokens int
	last   time.Time
	// Told it is over its limit since the last refill: ignored until then.
	warned bool
}

// Server is one relay. handle() is the whole protocol and does no I/O, so the
// tests drive it directly; Serve() adds the socket.
type Server struct {
	cfg     Config
	log     *slog.Logger
	Metrics Metrics

	mu       sync.Mutex
	clients  map[netip.AddrPort]*client
	sessions map[[SecretSize]byte]*session
	codes    map[string]*session
	lookups  map[netip.Prefix]*lookupBucket
	// Failed code lookups in the minute that started at failWindow.
	failWindow time.Time
	failures   int
}

func NewServer(cfg Config, log *slog.Logger) *Server {
	return &Server{
		cfg:      cfg,
		log:      log,
		clients:  map[netip.AddrPort]*client{},
		sessions: map[[SecretSize]byte]*session{},
		codes:    map[string]*session{},
		lookups:  map[netip.Prefix]*lookupBucket{},
	}
}

type outgoing struct {
	to   netip.AddrPort
	data []byte
}

func (s *Server) hostAllowed(token [TokenSize]byte) bool {
	if s.cfg.AllowAnyHost {
		return true
	}
	ok := 0
	for _, t := range s.cfg.HostTokens {
		ok |= subtle.ConstantTimeCompare(t[:], token[:])
	}
	return ok == 1
}

// handle processes one datagram and returns what to send, if anything.
func (s *Server) handle(from netip.AddrPort, b []byte, now time.Time) (outgoing, bool) {
	d, err := parse(b)
	if err != nil {
		s.Metrics.dropped(dropMalformed)
		return outgoing{}, false
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	switch d.kind {
	case typeRegisterHost:
		return s.registerHost(from, d, now), true
	case typeRegisterGuest:
		return s.registerGuest(from, d, now), true
	case typeKeepalive:
		if c := s.clients[from]; c != nil {
			c.lastSeen = now
		}
		return outgoing{}, false
	case typeData:
		return s.forward(from, d, len(b), now)
	case typeRequestCode:
		return s.requestCode(from, d, len(b), now)
	case typeResolveCode:
		return s.resolveCode(from, d, now)
	}
	return outgoing{}, false
}

// overRate counts a datagram against its client's one-second window.
func (s *Server) overRate(c *client, size int, now time.Time) bool {
	if now.Sub(c.window) >= time.Second {
		c.window, c.windowPkts, c.windowBytes = now, 0, 0
	}
	c.windowPkts++
	c.windowBytes += size
	if c.windowPkts > s.cfg.MaxPacketsPerSecond || c.windowBytes > s.cfg.MaxBytesPerSecond {
		s.Metrics.dropped(dropRate)
		return true
	}
	return false
}

// requestCode answers a session's host only (its address and its secret):
// anyone else gets nothing.
func (s *Server) requestCode(from netip.AddrPort, d datagram, size int, now time.Time) (outgoing, bool) {
	c := s.clients[from]
	if c == nil || c != c.session.host || subtle.ConstantTimeCompare(c.session.secret[:], d.secret[:]) != 1 {
		s.Metrics.dropped(dropUnknown)
		return outgoing{}, false
	}
	if s.overRate(c, size, now) {
		return outgoing{}, false
	}
	c.lastSeen = now
	sess := c.session
	live := sess.code != "" && now.Before(sess.codeExpires)
	if !live || (d.renew && now.Sub(sess.codeIssued) >= s.cfg.CodeRenewInterval) {
		s.issueCode(sess, now)
	}
	left := min(sess.codeExpires.Sub(now)/time.Second, 0xFFFF)
	return outgoing{from, codeReply(sess.code, uint16(left))}, true
}

func (s *Server) issueCode(sess *session, now time.Time) {
	if sess.code != "" {
		delete(s.codes, sess.code)
	}
	var code string
	for {
		var raw [CodeSize]byte
		_, _ = rand.Read(raw[:])
		for i := range raw {
			raw[i] = codeAlphabet[raw[i]&31]
		}
		if code = string(raw[:]); s.codes[code] == nil {
			break
		}
	}
	sess.code, sess.codeIssued, sess.codeExpires = code, now, now.Add(s.cfg.CodeTTL)
	s.codes[code] = sess
	s.Metrics.codeIssued()
	s.log.Info("join code issued", "host", sess.host.addr, "ttl", s.cfg.CodeTTL)
}

// sourceOf groups addresses one sender can easily hold many of: an IPv6
// /64 counts as one source.
func sourceOf(addr netip.AddrPort) netip.Prefix {
	a := addr.Addr().Unmap()
	bits := 32
	if a.Is6() {
		bits = 64
	}
	p, _ := a.Prefix(bits)
	return p
}

// allowLookup spends one of the source's lookups. answer: tell it it is
// over its limit (once per refill; after that it is ignored).
func (s *Server) allowLookup(from netip.AddrPort, now time.Time) (allowed, answer bool) {
	key := sourceOf(from)
	b := s.lookups[key]
	if b == nil {
		if len(s.lookups) >= s.cfg.MaxLookupSources {
			return false, false
		}
		b = &lookupBucket{tokens: s.cfg.CodeLookupBurst, last: now}
		s.lookups[key] = b
	}
	if n := int(now.Sub(b.last) / s.cfg.CodeLookupInterval); n > 0 {
		b.tokens = min(s.cfg.CodeLookupBurst, b.tokens+n)
		b.last = b.last.Add(time.Duration(n) * s.cfg.CodeLookupInterval)
		b.warned = false
	}
	if now.Sub(s.failWindow) >= time.Minute {
		s.failWindow, s.failures = now, 0
	}
	if b.tokens == 0 || s.failures >= s.cfg.MaxCodeFailuresPerMinute {
		answer = !b.warned
		b.warned = true
		return false, answer
	}
	b.tokens--
	return true, true
}

// resolveCode trades a live join code for its session's secret. Like the
// registrations it may answer an unregistered address: its request is
// padded to 64 bytes, every answer is at most 21.
func (s *Server) resolveCode(from netip.AddrPort, d datagram, now time.Time) (outgoing, bool) {
	allowed, answer := s.allowLookup(from, now)
	if !allowed {
		s.Metrics.lookup(lookupLimited)
		if answer {
			return outgoing{from, errorReply(ErrSlowDown)}, true
		}
		return outgoing{}, false
	}
	var sess *session
	if code, ok := NormalizeCode(string(bytes.TrimRight(d.code[:], "\x00"))); ok {
		sess = s.codes[code]
	}
	if sess == nil || !now.Before(sess.codeExpires) {
		s.failures++
		s.Metrics.lookup(lookupUnknown)
		return outgoing{from, errorReply(ErrNoCode)}, true
	}
	s.Metrics.lookup(lookupFound)
	return outgoing{from, secretReply(sess.secret)}, true
}

func (s *Server) registerHost(from netip.AddrPort, d datagram, now time.Time) outgoing {
	if !s.hostAllowed(d.token) {
		s.Metrics.dropped(dropBadToken)
		return outgoing{from, errorReply(ErrBadToken)}
	}
	if sess := s.sessions[d.secret]; sess != nil {
		if sess.host.addr != from {
			return outgoing{from, errorReply(ErrSessionTaken)}
		}
		sess.host.lastSeen = now // a retry
		return outgoing{from, registered(0)}
	}
	if len(s.sessions) >= s.cfg.MaxSessions {
		return outgoing{from, errorReply(ErrTooManySession)}
	}
	s.forget(from)

	sess := &session{secret: d.secret, guests: map[uint16]*client{}, nextChannel: 1}
	host := &client{addr: from, session: sess, lastSeen: now}
	sess.host = host
	s.sessions[d.secret] = sess
	s.clients[from] = host
	s.log.Info("session opened", "host", from, "sessions", len(s.sessions))
	return outgoing{from, registered(0)}
}

func (s *Server) registerGuest(from netip.AddrPort, d datagram, now time.Time) outgoing {
	sess := s.sessions[d.secret]
	if sess == nil {
		return outgoing{from, errorReply(ErrNoSession)}
	}
	if c := s.clients[from]; c != nil && c.session == sess && c.channel != 0 {
		c.lastSeen = now // a retry
		return outgoing{from, registered(c.channel)}
	}
	if len(sess.guests) >= s.cfg.MaxGuests {
		return outgoing{from, errorReply(ErrSessionFull)}
	}
	s.forget(from)

	channel := sess.nextChannel
	for channel == 0 || sess.guests[channel] != nil {
		channel++
	}
	sess.nextChannel = channel + 1
	guest := &client{addr: from, session: sess, channel: channel, lastSeen: now}
	sess.guests[channel] = guest
	s.clients[from] = guest
	s.log.Info("guest joined", "guest", from, "channel", channel, "host", sess.host.addr)
	return outgoing{from, registered(channel)}
}

func (s *Server) forward(from netip.AddrPort, d datagram, size int, now time.Time) (outgoing, bool) {
	c := s.clients[from]
	if c == nil {
		// Never answer an unknown address: no reflection.
		s.Metrics.dropped(dropUnknown)
		return outgoing{}, false
	}
	if s.overRate(c, size, now) {
		return outgoing{}, false
	}
	c.lastSeen = now

	sess := c.session
	if c == sess.host {
		guest := sess.guests[d.channel]
		if guest == nil {
			s.Metrics.dropped(dropNoChannel)
			return outgoing{}, false
		}
		s.Metrics.forwarded(len(d.payload))
		return outgoing{guest.addr, Data(0, d.payload)}, true
	}
	s.Metrics.forwarded(len(d.payload))
	return outgoing{sess.host.addr, Data(c.channel, d.payload)}, true
}

// forget drops whatever this address was registered as.
func (s *Server) forget(addr netip.AddrPort) {
	c := s.clients[addr]
	if c == nil {
		return
	}
	delete(s.clients, addr)
	sess := c.session
	if c == sess.host {
		for _, g := range sess.guests {
			delete(s.clients, g.addr)
		}
		if sess.code != "" {
			delete(s.codes, sess.code)
		}
		delete(s.sessions, sess.secret)
		s.log.Info("session closed", "host", addr, "sessions", len(s.sessions))
		return
	}
	delete(sess.guests, c.channel)
}

// sweep forgets clients silent for longer than the idle timeout, expired
// codes, and lookup sources back to a full burst.
func (s *Server) sweep(now time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for addr, c := range s.clients {
		if now.Sub(c.lastSeen) > s.cfg.IdleTimeout {
			s.forget(addr)
		}
	}
	for code, sess := range s.codes {
		if !now.Before(sess.codeExpires) {
			delete(s.codes, code)
			sess.code = ""
		}
	}
	full := time.Duration(s.cfg.CodeLookupBurst) * s.cfg.CodeLookupInterval
	for key, b := range s.lookups {
		if now.Sub(b.last) >= full {
			delete(s.lookups, key)
		}
	}
}

// Counts returns the live sessions and clients.
func (s *Server) Counts() (sessions, clients int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return len(s.sessions), len(s.clients)
}

// Serve runs the relay on conn until ctx ends.
func (s *Server) Serve(ctx context.Context, conn *net.UDPConn) error {
	go func() {
		<-ctx.Done()
		_ = conn.Close()
	}()
	go func() {
		tick := time.NewTicker(10 * time.Second)
		defer tick.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case now := <-tick.C:
				s.sweep(now)
			}
		}
	}()

	buf := make([]byte, headerSize+2+MaxPayload+1)
	for {
		n, from, err := conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				return nil
			}
			s.log.Warn("read", "err", err)
			continue
		}
		out, ok := s.handle(from, buf[:n], time.Now())
		if !ok {
			continue
		}
		if _, err := conn.WriteToUDPAddrPort(out.data, out.to); err != nil {
			s.log.Debug("write", "to", out.to, "err", err)
		}
	}
}
