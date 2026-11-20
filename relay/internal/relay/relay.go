package relay

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/binary"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/netip"
	"strings"
	"sync"
	"sync/atomic"
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
	// Guests registered in one session at once: the host's game takes 3
	// players (4 with the host) and keeps 2 spare connections, for a player
	// coming back while its old one is still open and one to tell the
	// session is full. A guest the host refused stays registered here until
	// it is idle, so the relay leaves the limit to the host and counts
	// those too.
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
	// Failed lookups per minute from all sources together, for the whole
	// fleet: each of its len(Peers) replicas takes its share. Past it, every
	// lookup is refused until the minute ends, so many sources together
	// cannot try codes faster either.
	MaxCodeFailuresPerMinute int
	// Sources tracked at once; a new one beyond it is ignored.
	MaxLookupSources int

	// A guest's Addresses are answered (two introductions) at most this often.
	IntroduceInterval time.Duration

	// A fleet of replicas: Peers[i] is the public address of replica i (the
	// owner of the codes shardOf maps to i), and Shard is this one. Empty:
	// a relay on its own, owning every code.
	Peers []netip.AddrPort
	Shard int
	// Several regions, each a fleet of its own: Regions[i] is region i's
	// entrance (its --front, public), and Region is ours. Empty: one region.
	// The fleet-wide failure budget is then the whole world's.
	Regions []netip.AddrPort
	Region  int

	// Behind a tunnel or proxy that says who each client is in a PROXY v2
	// header (playit.gg: proxy.go). Only datagrams from TrustedProxies are
	// taken, and their header's address is the client's for every rule;
	// answers go back to the proxy's socket. MaxFlows bounds the proxy
	// sockets remembered.
	ProxyProtocol  bool
	TrustedProxies []netip.Prefix
	MaxFlows       int

	// Open: anyone may host, with no token (open.go): a proof of work of
	// PowBits per session (its challenge valid ChallengeTTL), at most
	// MaxSessionsPerSource open sessions per source and MaxOpenSessions in
	// all, OpenRate bytes a second (OpenBurst at once) per guest link and
	// direction. A host token bypasses it all. Not open: a token is needed.
	Open                 bool
	PowBits              int
	ChallengeTTL         time.Duration
	MaxOpenSessions      int
	MaxSessionsPerSource int
	OpenRate             int
	OpenBurst            int
}

// DefaultConfig suits a small group's relay.
func DefaultConfig() Config {
	return Config{
		MaxSessions:         256,
		MaxGuests:           5,
		IdleTimeout:         2 * time.Minute,
		MaxPacketsPerSecond: 2000,
		MaxBytesPerSecond:   4 << 20,

		CodeTTL:                  10 * time.Minute,
		CodeRenewInterval:        10 * time.Second,
		CodeLookupBurst:          5,
		CodeLookupInterval:       6 * time.Second,
		MaxCodeFailuresPerMinute: 120,
		MaxLookupSources:         4096,

		IntroduceInterval: 2 * time.Second,

		TrustedProxies: []netip.Prefix{netip.MustParsePrefix("127.0.0.1/32"), netip.MustParsePrefix("::1/128")},
		MaxFlows:       65536,

		Open:                 true,
		PowBits:              17,
		ChallengeTTL:         2 * time.Minute,
		MaxOpenSessions:      192,
		MaxSessionsPerSource: 2,
		// Twice tandem's 64 kbit/s per link, and 2 s of it at once.
		OpenRate:  16_000,
		OpenBurst: 32_000,
	}
}

// Validate checks the fleet's description.
func (c Config) Validate() error {
	if c.Open && (c.PowBits < 0 || c.PowBits > 32 || c.OpenRate <= 0 || c.OpenBurst <= 0) {
		return errors.New("open mode: the proof of work is 0 to 32 bits, the bandwidth positive")
	}
	if c.ProxyProtocol && len(c.TrustedProxies) == 0 {
		return errors.New("the PROXY protocol needs the proxies to trust")
	}
	if len(c.Regions) == 0 && c.Region != 0 {
		return errors.New("a region index needs the regions' entrances")
	}
	if len(c.Regions) > 0 {
		if len(c.Regions) > 16 {
			return errors.New("at most 16 regions")
		}
		if c.Region < 0 || c.Region >= len(c.Regions) {
			return errors.New("the region index is not one of the regions")
		}
		for i, p := range c.Regions {
			if !p.IsValid() || p.Port() == 0 || p.Addr().IsUnspecified() {
				return fmt.Errorf("region %d (%v) is not an address and port", i, p)
			}
		}
	}
	if len(c.Peers) == 0 {
		if c.Shard != 0 {
			return errors.New("a shard index needs the replicas' addresses (peers)")
		}
		return nil
	}
	if len(c.Peers) > 32 {
		return errors.New("at most 32 replicas")
	}
	if c.Shard < 0 || c.Shard >= len(c.Peers) {
		return errors.New("the shard index is not one of the peers")
	}
	for i, p := range c.Peers {
		if !p.IsValid() || p.Port() == 0 || p.Addr().IsUnspecified() {
			return fmt.Errorf("peer %d (%v) is not an address and port", i, p)
		}
	}
	return nil
}

type client struct {
	// Where its datagrams come from and its answers go (behind a proxy: the
	// proxy's socket for it), and who it is (its own public address: the
	// PROXY header's; invalid for a proxied flow older than us).
	addr    netip.AddrPort
	public  netip.AddrPort
	session *session
	channel uint16 // 0 for the host

	// Nanoseconds since the server's epoch.
	lastSeen atomic.Int64

	// Its one-second rate window. A client's datagrams come in on one reader
	// (SO_REUSEPORT hashes by address), so this lock is not contended.
	mu          sync.Mutex
	window      int64
	windowPkts  int
	windowBytes int

	// Under Server.mu: what it told us for direct paths (Addresses), and
	// when it was last introduced.
	lan        netip.AddrPort
	flags      byte
	introduced time.Time

	// A guest of an open session: its link's bandwidth, each way (under mu).
	up, down bucket
}

type session struct {
	secret [SecretSize]byte
	host   *client

	// The data path reads guests under the read lock; registrations change
	// them under Server.mu and the write lock.
	mu          sync.RWMutex
	guests      map[uint16]*client
	nextChannel uint16

	// Under Server.mu: its join code ("" none), when it was issued and when
	// it expires.
	code        string
	codeIssued  time.Time
	codeExpires time.Time
	// A host that asked for its kept code (RegisterHostKeep): its token and
	// salt, and whether `code` is that code (another session of the same
	// token may hold it: this one then has a code of its own).
	keep  bool
	token [TokenSize]byte
	salt  uint32
	kept  bool
	// Opened with no token (open.go): its source's count, and its guests'
	// bandwidth, are kept. Immutable.
	open   bool
	source netip.Prefix
}

func (sess *session) guest(channel uint16) *client {
	sess.mu.RLock()
	g := sess.guests[channel]
	sess.mu.RUnlock()
	return g
}

// A source's code lookups: tokens left, refilled one per interval since last.
type lookupBucket struct {
	tokens int
	last   time.Time
	// Told it is over its limit since the last refill: ignored until then.
	warned bool
}

// clients by address, in shards so the readers of the data path do not
// queue on one lock.
const clientShards = 64

type clientTable struct {
	shards [clientShards]struct {
		mu sync.RWMutex
		m  map[netip.AddrPort]*client
		_  [40]byte // a cache line each
	}
}

func shardIndex(a netip.AddrPort) int {
	raw := a.Addr().As16()
	h := binary.LittleEndian.Uint64(raw[8:]) ^ binary.LittleEndian.Uint64(raw[:8])*0x9E3779B97F4A7C15
	h ^= uint64(a.Port()) * 0xBF58476D1CE4E5B9
	return int((h ^ h>>29) % clientShards)
}

func (t *clientTable) init() {
	for i := range t.shards {
		t.shards[i].m = map[netip.AddrPort]*client{}
	}
}

func (t *clientTable) get(a netip.AddrPort) *client {
	sh := &t.shards[shardIndex(a)]
	sh.mu.RLock()
	c := sh.m[a]
	sh.mu.RUnlock()
	return c
}

func (t *clientTable) put(c *client) {
	sh := &t.shards[shardIndex(c.addr)]
	sh.mu.Lock()
	sh.m[c.addr] = c
	sh.mu.Unlock()
}

func (t *clientTable) del(a netip.AddrPort) {
	sh := &t.shards[shardIndex(a)]
	sh.mu.Lock()
	delete(sh.m, a)
	sh.mu.Unlock()
}

func (t *clientTable) count() int {
	n := 0
	for i := range t.shards {
		sh := &t.shards[i]
		sh.mu.RLock()
		n += len(sh.m)
		sh.mu.RUnlock()
	}
	return n
}

// idle lists the clients silent since before `since`.
func (t *clientTable) idle(since int64) []netip.AddrPort {
	var out []netip.AddrPort
	for i := range t.shards {
		sh := &t.shards[i]
		sh.mu.RLock()
		for a, c := range sh.m {
			if c.lastSeen.Load() < since {
				out = append(out, a)
			}
		}
		sh.mu.RUnlock()
	}
	return out
}

// Server is one relay. process() is the whole protocol and does no I/O, so
// the tests drive it directly; Serve() adds the sockets.
type Server struct {
	cfg     Config
	log     *slog.Logger
	Metrics Metrics

	epoch    time.Time
	started  time.Time
	draining atomic.Bool
	// Our share of the fleet's failed-lookup budget.
	failCap int

	clients clientTable

	// Everything but the data path: registrations, codes, lookups.
	mu       sync.Mutex
	sessions map[[SecretSize]byte]*session
	codes    map[string]*session
	lookups  map[netip.Prefix]*lookupBucket
	// Failed code lookups in the minute that started at failWindow.
	failWindow time.Time
	failures   int
	// Behind a proxy: each of its sockets' client (proxy.go).
	flows map[netip.AddrPort]flow
	// Open mode (open.go): the challenges' key, those used (until stale),
	// and the open sessions, per source and in all.
	powKey         [32]byte
	usedChallenges map[[ChallengeMACSize]byte]time.Time
	openBySource   map[netip.Prefix]int
	openSessions   int
}

func NewServer(cfg Config, log *slog.Logger) *Server {
	// Our share of the budget: the world's, over its regions and ours over
	// its replicas.
	shares := max(len(cfg.Peers), 1) * max(len(cfg.Regions), 1)
	now := time.Now()
	s := &Server{
		cfg:      cfg,
		log:      log,
		epoch:    now,
		started:  now,
		failCap:  (cfg.MaxCodeFailuresPerMinute + shares - 1) / shares,
		sessions: map[[SecretSize]byte]*session{},
		codes:    map[string]*session{},
		lookups:  map[netip.Prefix]*lookupBucket{},
		flows:    map[netip.AddrPort]flow{},

		powKey:         newPowKey(),
		usedChallenges: map[[ChallengeMACSize]byte]time.Time{},
		openBySource:   map[netip.Prefix]int{},
	}
	s.clients.init()
	return s
}

func (s *Server) mono(now time.Time) int64 { return int64(now.Sub(s.epoch)) }

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

// Which socket a datagram came in on.
type port int

const (
	// Players' sessions: registrations, data, codes.
	portDirect port = iota
	// The fleet's shared entrance (a load balancer over every replica):
	// registration-type requests only, each answered with a redirect to the
	// replica that takes it (or a refusal). Its datagrams never carry a
	// session, so a load balancer that moves flows between replicas breaks
	// nothing.
	portFront
)

// worker is one reader's scratch space: its answers are built in out, and a
// second datagram (an introduction to the other side) in extra.
type worker struct {
	slot    int
	out     []byte
	extra   []byte
	extraTo netip.AddrPort
}

func newWorker(slot int) *worker {
	return &worker{slot: slot, out: make([]byte, 0, 64), extra: make([]byte, 0, 64)}
}

// process handles one datagram and answers where to send what (ok false:
// nothing). Data is rewritten in place and sent on as b; any other answer
// is in w.out.
func (s *Server) process(w *worker, p port, from netip.AddrPort, b []byte, now time.Time) (to netip.AddrPort, reply []byte, ok bool) {
	// Who sent it: `from`, or behind a proxy the client its header names
	// (`who` invalid: not known yet).
	who := from
	if s.cfg.ProxyProtocol {
		who = netip.AddrPort{}
		if len(b) > 0 && b[0] == proxySignature[0] {
			if b, who, ok = s.unwrapProxy(from, b, now); !ok {
				return to, nil, false
			}
		}
	}
	// The data path first, with no lock but the client table's and the
	// session's read locks. Behind a proxy, a client is known by the proxy's
	// socket for it, which only a trusted proxy's header registered.
	if len(b) >= headerSize+2 && b[4] == byte(typeData) && string(b[:4]) == magic && p == portDirect {
		return s.forward(w, from, b, now)
	}
	if s.cfg.ProxyProtocol && !who.IsValid() {
		if !s.trusted(from) {
			s.Metrics.dropped(dropUntrusted)
			return to, nil, false
		}
		who = s.publicOf(from, now)
	}
	d, err := parse(b)
	if err != nil {
		s.Metrics.dropped(dropMalformed)
		return to, nil, false
	}
	out := w.out[:0]
	w.extra, w.extraTo = w.extra[:0], netip.AddrPort{}
	switch {
	case d.kind == typeProbe:
		// Anyone, either port: what we see of it, smaller than its probe
		// (behind a proxy, only a client we know the address of).
		if !who.IsValid() {
			return to, nil, false
		}
		s.Metrics.probed()
		out, ok = appendObserved(out, d.nonce, who), true
	case p == portFront:
		out, ok = s.front(out, from, who, d, now)
	case d.kind == typeAddresses:
		out, ok = s.addresses(w, out, from, d, len(b), now)
	default:
		out, ok = s.control(out, from, who, d, len(b), now)
	}
	w.out = out
	return from, out, ok
}

// publicOf is the address behind a proxy socket: its registered client's,
// else its flow's (invalid when neither is known).
func (s *Server) publicOf(from netip.AddrPort, now time.Time) netip.AddrPort {
	if c := s.clients.get(from); c != nil && c.public.IsValid() {
		return c.public
	}
	who, _ := s.clientOf(from, now)
	return who
}

// addresses keeps a registered client's LAN address and NAT flags; a
// guest's also introduces it and its host to each other (two answers, 48
// bytes for its 64, both to members of its session). Anyone else: nothing.
func (s *Server) addresses(w *worker, out []byte, from netip.AddrPort, d datagram, size int, now time.Time) ([]byte, bool) {
	c := s.clients.get(from)
	if c == nil {
		s.Metrics.dropped(dropUnknown)
		return out, false
	}
	if s.overRate(c, size, s.mono(now)) {
		return out, false
	}
	c.lastSeen.Store(s.mono(now))
	s.mu.Lock()
	defer s.mu.Unlock()
	c.lan, c.flags = d.lan[0], d.flags&flagMappingMask
	host := c.session.host
	// Each named by its own public address (behind a proxy, its header's:
	// not known, no introduction), sent to where it is reached.
	if c == host || now.Sub(c.introduced) < s.cfg.IntroduceInterval ||
		!c.public.Addr().Unmap().Is4() || !host.public.Addr().Unmap().Is4() {
		return out, false
	}
	c.introduced = now
	var raw [4]byte
	_, _ = rand.Read(raw[:])
	nonce := binary.BigEndian.Uint32(raw[:])
	s.Metrics.introduced()
	w.extraTo = host.addr
	w.extra = appendIntroduce(w.extra, c.channel, nonce, c.flags, c.public, c.lan)
	return appendIntroduce(out, c.channel, nonce, host.flags, host.public, host.lan), true
}

func (s *Server) control(out []byte, from, who netip.AddrPort, d datagram, size int, now time.Time) ([]byte, bool) {
	if d.kind == typeKeepalive {
		if c := s.clients.get(from); c != nil {
			c.lastSeen.Store(s.mono(now))
		}
		return out, false
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	switch d.kind {
	case typeRegisterHost, typeRegisterHostKeep, typeRegisterHostOpen:
		return s.registerHost(out, from, who, d, now), true
	case typeRegisterGuest, typeRejoinGuest:
		return s.registerGuest(out, from, who, d, now), true
	case typeRequestCode, typeReclaimCode:
		return s.requestCode(out, from, d, size, now)
	case typeResolveCode:
		return s.resolveCode(out, from, who, d, false, now)
	}
	return out, false
}

// front answers the fleet's entrance: always a redirect or a refusal, never
// a registration.
func (s *Server) front(out []byte, from, who netip.AddrPort, d datagram, now time.Time) ([]byte, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	switch d.kind {
	case typeRegisterHostKeep:
		// To the replica holding the token's kept codes, unless that is us
		// and we are draining.
		if owner := s.keepOwner(d.token); owner != s.cfg.Shard || !s.draining.Load() {
			s.Metrics.redirect(redirectHost)
			return appendRedirect(out, s.cfg.Peers[owner]), true
		}
		fallthrough
	case typeRegisterHost, typeRegisterHostOpen:
		if to, ok := s.takerOfNewSessions(); ok {
			s.Metrics.redirect(redirectHost)
			return appendRedirect(out, to), true
		}
		s.Metrics.refused(refusedDraining)
		return appendError(out, ErrDraining), true
	case typeRegisterGuest, typeRejoinGuest:
		// The secret does not say which replica holds the session: the guest
		// comes back to the one it was sent to, this is only a guess.
		if s.sessions[d.secret] != nil {
			s.Metrics.redirect(redirectGuest)
			return appendRedirect(out, s.self()), true
		}
		s.Metrics.refused(refusedNoSession)
		return appendError(out, ErrNoSession), true
	case typeResolveCode:
		return s.resolveCode(out, from, who, d, true, now)
	}
	s.Metrics.dropped(dropUnknown)
	return out, false
}

func (s *Server) replicas() int { return max(len(s.cfg.Peers), 1) }

func (s *Server) self() netip.AddrPort {
	if len(s.cfg.Peers) == 0 {
		return netip.AddrPort{}
	}
	return s.cfg.Peers[s.cfg.Shard]
}

// takerOfNewSessions: where a new session goes (ok false: nowhere). Us,
// unless we are draining: then the next replica.
func (s *Server) takerOfNewSessions() (netip.AddrPort, bool) {
	if !s.draining.Load() {
		return s.self(), len(s.cfg.Peers) > 0
	}
	if len(s.cfg.Peers) < 2 {
		return netip.AddrPort{}, false
	}
	return s.cfg.Peers[(s.cfg.Shard+1)%len(s.cfg.Peers)], true
}

func codeValue(code string) int {
	v := 0
	for i := 0; i < len(code); i++ {
		v = v<<5 | strings.IndexByte(codeAlphabet, code[i])
	}
	return v
}

// shardOf is the replica owning a code in a region of one: its 30 bits
// modulo the fleet's size. Replicas hold disjoint sets of codes and every
// one reads the same owner from a code, with nothing shared but the list of
// peers.
func shardOf(code string, replicas int) int { return codeValue(code) % replicas }

// ownerOf is a code's region (its value modulo the regions) and, in it, its
// replica (the rest modulo the region's replicas). One region: shardOf.
func (s *Server) ownerOf(code string) (region, shard int) {
	v, regions := codeValue(code), max(len(s.cfg.Regions), 1)
	return v % regions, (v / regions) % s.replicas()
}

func (s *Server) owns(code string) bool {
	region, shard := s.ownerOf(code)
	return region == s.cfg.Region && shard == s.cfg.Shard
}

// overRate counts a datagram against its client's one-second window.
func (s *Server) overRate(c *client, size int, now int64) bool {
	over, _ := s.overRateAndUp(c, size, now, false)
	return over
}

// overRateAndUp also takes a guest of an open session's datagram from its
// upload bucket, under the same lock (overBandwidth's other half).
func (s *Server) overRateAndUp(c *client, size int, now int64, up bool) (over, overUp bool) {
	c.mu.Lock()
	if now-c.window >= int64(time.Second) {
		c.window, c.windowPkts, c.windowBytes = now, 0, 0
	}
	c.windowPkts++
	c.windowBytes += size
	over = c.windowPkts > s.cfg.MaxPacketsPerSecond || c.windowBytes > s.cfg.MaxBytesPerSecond
	if up && !over {
		overUp = !c.up.take(size, now, int64(s.cfg.OpenRate), int64(s.cfg.OpenBurst))
	}
	c.mu.Unlock()
	if over {
		s.Metrics.dropped(dropRate)
	}
	if overUp {
		s.Metrics.dropped(dropBandwidth)
	}
	return over, overUp
}

// requestCode answers a session's host only (its address and its secret):
// anyone else gets nothing. A ReclaimCode gets the code the host had when
// we may give it back (reclaim), else what a RequestCode would.
func (s *Server) requestCode(out []byte, from netip.AddrPort, d datagram, size int, now time.Time) ([]byte, bool) {
	c := s.clients.get(from)
	if c == nil || c != c.session.host || subtle.ConstantTimeCompare(c.session.secret[:], d.secret[:]) != 1 {
		s.Metrics.dropped(dropUnknown)
		return out, false
	}
	if s.overRate(c, size, s.mono(now)) {
		return out, false
	}
	c.lastSeen.Store(s.mono(now))
	sess := c.session
	live := sess.code != "" && now.Before(sess.codeExpires)
	switch {
	case sess.kept && live:
		// Kept: a new one comes with another salt (RegisterHostKeep).
	case d.kind == typeReclaimCode && !live && s.reclaim(sess, d, now):
	case !live || (d.kind == typeRequestCode && d.renew && now.Sub(sess.codeIssued) >= s.cfg.CodeRenewInterval):
		s.issueCode(sess, now)
	}
	left := min(sess.codeExpires.Sub(now)/time.Second, 0xFFFF)
	return appendCode(out, sess.code, uint16(left)), true
}

// reclaim gives a host back the code it had before we restarted: one of
// ours, held by nobody, with time left, and only within a code's lifetime of
// our start (a code we issued since, we know).
func (s *Server) reclaim(sess *session, d datagram, now time.Time) bool {
	code, ok := NormalizeCode(string(bytes.TrimRight(d.code[:], "\x00")))
	if !ok || d.left == 0 || now.Sub(s.started) >= s.cfg.CodeTTL || !s.owns(code) || s.codes[code] != nil {
		return false
	}
	if sess.code != "" {
		delete(s.codes, sess.code)
	}
	ttl := min(time.Duration(d.left)*time.Second, s.cfg.CodeTTL)
	sess.code, sess.codeIssued, sess.codeExpires = code, now, now.Add(ttl)
	s.codes[code] = sess
	s.Metrics.codeReclaimed()
	s.log.Info("join code reclaimed", "host", sess.host.addr, "ttl", ttl)
	return true
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
		code = string(raw[:])
		if s.owns(code) && s.codes[code] == nil {
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
	if b.tokens == 0 || s.failures >= s.failCap {
		answer = !b.warned
		b.warned = true
		return false, answer
	}
	b.tokens--
	return true, true
}

// resolveCode trades a live join code for its session's secret, or sends
// the guest to the replica owning the code. Like the registrations it may
// answer an unregistered address: its request is padded to 64 bytes, every
// answer is at most 24. A redirect spends the source's lookup here (and the
// owner spends another): a flood across replicas is no cheaper than one at
// the owner. It is not a failure: the owner counts those, and the
// redirect says nothing about whether the code is live.
func (s *Server) resolveCode(out []byte, from, who netip.AddrPort, d datagram, front bool, now time.Time) ([]byte, bool) {
	// Limited by who asks: behind a proxy, its client (one we do not know
	// of yet shares the proxy's budget).
	source := who
	if !source.IsValid() {
		source = from
	}
	allowed, answer := s.allowLookup(source, now)
	if !allowed {
		s.Metrics.lookup(lookupLimited)
		if answer {
			return appendError(out, ErrSlowDown), true
		}
		return out, false
	}
	var sess *session
	code, ok := NormalizeCode(string(bytes.TrimRight(d.code[:], "\x00")))
	if ok {
		// Another region's code: to its entrance (which sends it to the
		// owner); another replica's: to it.
		switch region, shard := s.ownerOf(code); {
		case region != s.cfg.Region:
			s.Metrics.lookup(lookupRedirected)
			return appendRedirect(out, s.cfg.Regions[region]), true
		case shard != s.cfg.Shard:
			s.Metrics.lookup(lookupRedirected)
			return appendRedirect(out, s.cfg.Peers[shard]), true
		}
		sess = s.codes[code]
	}
	if sess == nil || !now.Before(sess.codeExpires) {
		s.failures++
		s.Metrics.lookup(lookupUnknown)
		return appendError(out, ErrNoCode), true
	}
	if front {
		// Ours: the guest registers here, so it asks here.
		s.Metrics.lookup(lookupRedirected)
		return appendRedirect(out, s.self()), true
	}
	s.Metrics.lookup(lookupFound)
	return appendSecret(out, sess.secret), true
}

func (s *Server) registerHost(out []byte, from, who netip.AddrPort, d datagram, now time.Time) []byte {
	keep := d.kind == typeRegisterHostKeep
	// A retry (the host's refreshes and probes) needs only the session's
	// secret, from the host's address: no token, no proof again.
	if sess := s.sessions[d.secret]; sess != nil {
		if sess.host.addr != from {
			s.Metrics.refused(refusedTaken)
			return appendError(out, ErrSessionTaken)
		}
		sess.host.lastSeen.Store(s.mono(now)) // a retry
		// Another salt: the host asked for another kept code, at once.
		if keep && sess.keep && d.salt != sess.salt {
			sess.salt = d.salt
			s.bindKept(sess, now)
		}
		return appendRegistered(out, 0)
	}
	// A new session: a host token, or (open) a proof of work, the open
	// caps, and no kept code (it is a token's).
	open := d.kind == typeRegisterHostOpen || !s.hostAllowed(d.token)
	source := sourceOf(from)
	if who.IsValid() {
		source = sourceOf(who)
	}
	if open {
		if !s.cfg.Open {
			s.Metrics.dropped(dropBadToken)
			return appendError(out, ErrBadToken)
		}
		if d.kind != typeRegisterHostOpen || !s.proofHolds(from, d, now) {
			return s.appendChallenge(out, from, now)
		}
		keep = false
	}
	// A kept code lives on one replica: the host goes there (unless that is
	// us, draining: then it gets a code of its own elsewhere).
	if owner := s.keepOwner(d.token); keep && owner != s.cfg.Shard {
		s.Metrics.redirect(redirectHost)
		return appendRedirect(out, s.cfg.Peers[owner])
	}
	if s.draining.Load() {
		// Running sessions stay; a new one goes to another replica.
		if to, ok := s.takerOfNewSessions(); ok {
			s.Metrics.redirect(redirectHost)
			return appendRedirect(out, to)
		}
		s.Metrics.refused(refusedDraining)
		return appendError(out, ErrDraining)
	}
	if len(s.sessions) >= s.cfg.MaxSessions || (open && s.openSessions >= s.cfg.MaxOpenSessions) {
		s.Metrics.refused(refusedTooMany)
		return appendError(out, ErrTooManySession)
	}
	if open && s.openBySource[source] >= s.cfg.MaxSessionsPerSource {
		s.Metrics.refused(refusedSourceBusy)
		return appendError(out, ErrSourceBusy)
	}
	s.forget(from)

	sess := &session{secret: d.secret, guests: map[uint16]*client{}, nextChannel: 1}
	if open {
		s.useChallenge(d.mac, now)
		sess.open, sess.source = true, source
		s.openBySource[source]++
		s.openSessions++
	}
	host := &client{addr: from, public: who, session: sess}
	host.lastSeen.Store(s.mono(now))
	sess.host = host
	s.sessions[d.secret] = sess
	s.clients.put(host)
	if keep {
		sess.keep, sess.token, sess.salt = true, d.token, d.salt
		s.bindKept(sess, now)
	}
	s.log.Info("session opened", "host", from, "sessions", len(s.sessions), "kept_code", sess.kept)
	return appendRegistered(out, 0)
}

// keepOwner is the replica (of our region) holding a token's kept codes:
// the same whatever the salt, so a new one is given at once.
func (s *Server) keepOwner(token [TokenSize]byte) int {
	h := sha256.Sum256(append([]byte("tandem-kept-owner\x00"), token[:]...))
	return int(binary.BigEndian.Uint32(h[:]) % uint32(s.replicas()))
}

// keptCode is a token's kept code for a salt: 30 bits of a hash of both,
// moved into our region and the token's replica (ownerOf), so as random as
// any other code and in the same space.
func (s *Server) keptCode(token [TokenSize]byte, salt uint32) string {
	in := append([]byte("tandem-kept-code\x00"), token[:]...)
	h := sha256.Sum256(binary.BigEndian.AppendUint32(in, salt))
	const space = 1 << (5 * CodeSize)
	regions := max(len(s.cfg.Regions), 1)
	m := regions * s.replicas()
	v := int(binary.BigEndian.Uint32(h[:]) % space)
	v = v - v%m + s.keepOwner(token)*regions + s.cfg.Region
	if v >= space {
		v -= m
	}
	var raw [CodeSize]byte
	for i := range raw {
		raw[CodeSize-1-i] = codeAlphabet[(v>>(5*i))&31]
	}
	return string(raw[:])
}

// bindKept gives the session its kept code. The newest session of the
// token takes it from an older one (a host back from a crash or a reload
// before its old session idled out; a second game of the same host gets
// the next code it asks for); one of another token (a hash collision)
// keeps it.
func (s *Server) bindKept(sess *session, now time.Time) {
	code := s.keptCode(sess.token, sess.salt)
	if other := s.codes[code]; other != nil && other != sess {
		if !other.keep || subtle.ConstantTimeCompare(other.token[:], sess.token[:]) != 1 {
			sess.kept = false
			if sess.code == "" || !now.Before(sess.codeExpires) {
				s.issueCode(sess, now)
			}
			return
		}
		delete(s.codes, code)
		other.code, other.kept = "", false
	}
	if sess.code != "" && sess.code != code {
		delete(s.codes, sess.code)
	}
	sess.code, sess.codeIssued, sess.codeExpires, sess.kept = code, now, now.Add(100*365*24*time.Hour), true
	s.codes[code] = sess
	s.Metrics.codeIssued()
	s.log.Info("kept join code", "host", sess.host.addr)
}

// registerGuest: a RejoinGuest gets the channel it asks for when it is free
// (a guest back on a relay that restarted keeps its place at the host).
func (s *Server) registerGuest(out []byte, from, who netip.AddrPort, d datagram, now time.Time) []byte {
	sess := s.sessions[d.secret]
	if sess == nil {
		s.Metrics.refused(refusedNoSession)
		return appendError(out, ErrNoSession)
	}
	if c := s.clients.get(from); c != nil && c.session == sess && c.channel != 0 {
		c.lastSeen.Store(s.mono(now)) // a retry
		return appendRegistered(out, c.channel)
	}
	if len(sess.guests) >= s.cfg.MaxGuests {
		s.Metrics.refused(refusedFull)
		return appendError(out, ErrSessionFull)
	}
	s.forget(from)

	sess.mu.Lock()
	channel := sess.nextChannel
	if d.kind == typeRejoinGuest && d.channel != 0 && sess.guests[d.channel] == nil {
		channel = d.channel
	}
	for channel == 0 || sess.guests[channel] != nil {
		channel++
	}
	if channel >= sess.nextChannel {
		sess.nextChannel = channel + 1
	}
	guest := &client{addr: from, public: who, session: sess, channel: channel}
	guest.lastSeen.Store(s.mono(now))
	sess.guests[channel] = guest
	sess.mu.Unlock()
	s.clients.put(guest)
	s.log.Info("guest joined", "guest", from, "channel", channel, "host", sess.host.addr)
	return appendRegistered(out, channel)
}

// forward hands a datagram to the other side of its session, rewritten in
// place (the same size: only the channel changes). No allocation.
func (s *Server) forward(w *worker, from netip.AddrPort, b []byte, now time.Time) (netip.AddrPort, []byte, bool) {
	if len(b)-headerSize-2 > MaxPayload {
		s.Metrics.dropped(dropMalformed)
		return netip.AddrPort{}, nil, false
	}
	c := s.clients.get(from)
	if c == nil {
		// Never answer an unknown address: no reflection.
		s.Metrics.dropped(dropUnknown)
		return netip.AddrPort{}, nil, false
	}
	mono := s.mono(now)
	sess := c.session
	if over, overUp := s.overRateAndUp(c, len(b), mono, sess.open && c.channel != 0); over || overUp {
		return netip.AddrPort{}, nil, false
	}
	c.lastSeen.Store(mono)

	var to netip.AddrPort
	if c.channel == 0 {
		guest := sess.guest(binary.BigEndian.Uint16(b[headerSize:]))
		if guest == nil {
			s.Metrics.dropped(dropNoChannel)
			return netip.AddrPort{}, nil, false
		}
		if sess.open && s.overBandwidth(guest, false, len(b), mono) {
			return netip.AddrPort{}, nil, false
		}
		to = guest.addr
		binary.BigEndian.PutUint16(b[headerSize:], 0)
	} else {
		to = sess.host.addr
		binary.BigEndian.PutUint16(b[headerSize:], c.channel)
	}
	s.Metrics.forwarded(w.slot, len(b)-headerSize-2)
	return to, b, true
}

// forget drops whatever this address was registered as. Under s.mu.
func (s *Server) forget(addr netip.AddrPort) {
	c := s.clients.get(addr)
	if c == nil {
		return
	}
	s.clients.del(addr)
	sess := c.session
	if c == sess.host {
		sess.mu.RLock()
		for _, g := range sess.guests {
			s.clients.del(g.addr)
		}
		sess.mu.RUnlock()
		if sess.code != "" {
			delete(s.codes, sess.code)
		}
		delete(s.sessions, sess.secret)
		if sess.open {
			s.openSessions--
			if s.openBySource[sess.source]--; s.openBySource[sess.source] <= 0 {
				delete(s.openBySource, sess.source)
			}
		}
		s.log.Info("session closed", "host", addr, "sessions", len(s.sessions))
		return
	}
	sess.mu.Lock()
	if sess.guests[c.channel] == c {
		delete(sess.guests, c.channel)
	}
	sess.mu.Unlock()
}

// sweep forgets clients silent for longer than the idle timeout, expired
// codes, and lookup sources back to a full burst.
func (s *Server) sweep(now time.Time) {
	idle := s.clients.idle(s.mono(now) - int64(s.cfg.IdleTimeout))
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, addr := range idle {
		// Seen again since the list was made: kept.
		if c := s.clients.get(addr); c != nil && c.lastSeen.Load() < s.mono(now)-int64(s.cfg.IdleTimeout) {
			s.forget(addr)
		}
	}
	for code, sess := range s.codes {
		if !now.Before(sess.codeExpires) {
			delete(s.codes, code)
			sess.code = ""
		}
	}
	// Used challenges are forgotten once stale (they could not be used again).
	for mac, at := range s.usedChallenges {
		if now.Sub(at) > s.cfg.ChallengeTTL+time.Minute {
			delete(s.usedChallenges, mac)
		}
	}
	// A flow not heard of for that long: its client, if any, keeps its own
	// address (client.public).
	for from, f := range s.flows {
		if now.Sub(f.seen) > s.cfg.IdleTimeout {
			delete(s.flows, from)
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
	sessions = len(s.sessions)
	s.mu.Unlock()
	return sessions, s.clients.count()
}

// Drain stops taking new sessions (they go to another replica, or are
// refused): the running ones stay until they end.
func (s *Server) Drain() {
	if !s.draining.Swap(true) {
		s.log.Info("draining: no new sessions")
	}
}

// Draining is true once Drain was called.
func (s *Server) Draining() bool { return s.draining.Load() }

// Serve runs the relay on one socket until ctx ends.
func (s *Server) Serve(ctx context.Context, conn *net.UDPConn) error {
	return s.ServeAll(ctx, []*net.UDPConn{conn}, nil)
}

// ServeAll runs the relay until ctx ends: one reader goroutine per socket,
// direct ones for the sessions (several bound to one port with SO_REUSEPORT
// spread the work over cores, see Listen) and front ones for the fleet's
// entrance.
func (s *Server) ServeAll(ctx context.Context, direct, front []*net.UDPConn) error {
	conns := append(append([]*net.UDPConn{}, direct...), front...)
	go func() {
		<-ctx.Done()
		for _, c := range conns {
			_ = c.Close()
		}
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
	var wg sync.WaitGroup
	for i, c := range conns {
		p := portDirect
		if i >= len(direct) {
			p = portFront
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			s.read(ctx, c, p, newWorker(i))
		}()
	}
	wg.Wait()
	return nil
}

func (s *Server) read(ctx context.Context, conn *net.UDPConn, p port, w *worker) {
	buf := make([]byte, headerSize+2+MaxPayload+1)
	for {
		n, from, err := conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				return
			}
			s.log.Warn("read", "err", err)
			continue
		}
		to, reply, ok := s.process(w, p, from, buf[:n], time.Now())
		if !ok {
			continue
		}
		if _, err := conn.WriteToUDPAddrPort(reply, to); err != nil {
			s.log.Debug("write", "to", to, "err", err)
		}
		if w.extraTo.IsValid() {
			_, _ = conn.WriteToUDPAddrPort(w.extra, w.extraTo)
			w.extraTo = netip.AddrPort{}
		}
	}
}
