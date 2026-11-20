package relay

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/binary"
	"math/bits"
	"net/netip"
	"time"
)

// An open relay (Config.Open): anyone may host, with no token. What stands
// in for an account:
//
//   - a proof of work per session opened: a stateless challenge (a MAC of
//     the host's address, a stamp and the difficulty, under a key drawn at
//     start), answered once with a nonce such that SHA-256(mac | secret |
//     nonce) starts with PowBits zero bits (~50 ms of a client's CPU at 17);
//   - at most MaxSessionsPerSource open sessions per source (an IPv4
//     address, an IPv6 /64) and MaxOpenSessions in all;
//   - a bandwidth bucket per guest link and direction (OpenRate bytes a
//     second, OpenBurst at once): about twice what tandem sends, the rest
//     dropped;
//   - idle sessions forgotten as any.
//
// A host token bypasses all of it.

// bucket is a token bucket of bytes (nanoseconds since the server's epoch).
type bucket struct {
	tokens int64
	last   int64
}

// take spends n bytes when there are; O(1), no allocation.
func (b *bucket) take(n int, now, rate, burst int64) bool {
	if b.last == 0 {
		b.tokens, b.last = burst, now
	}
	if add := (now - b.last) * rate / int64(time.Second); add > 0 {
		b.tokens += add
		b.last += add * int64(time.Second) / rate
		if b.tokens >= burst {
			b.tokens, b.last = burst, now
		}
	}
	if b.tokens < int64(n) {
		return false
	}
	b.tokens -= int64(n)
	return true
}

// overBandwidth counts a datagram of an open session against its guest
// link's bucket: up (guest to host) or down (host to guest).
func (s *Server) overBandwidth(guest *client, up bool, size int, now int64) bool {
	guest.mu.Lock()
	b := &guest.down
	if up {
		b = &guest.up
	}
	ok := b.take(size, now, int64(s.cfg.OpenRate), int64(s.cfg.OpenBurst))
	guest.mu.Unlock()
	if !ok {
		s.Metrics.dropped(dropBandwidth)
	}
	return !ok
}

func (s *Server) challengeMAC(from netip.AddrPort, stamp uint32, difficulty byte) [ChallengeMACSize]byte {
	var in [16 + 2 + 4 + 1]byte
	a := from.Addr().As16()
	copy(in[:], a[:])
	binary.BigEndian.PutUint16(in[16:], from.Port())
	binary.BigEndian.PutUint32(in[18:], stamp)
	in[22] = difficulty
	m := hmac.New(sha256.New, s.powKey[:])
	m.Write(in[:])
	var mac [ChallengeMACSize]byte
	copy(mac[:], m.Sum(nil))
	return mac
}

// appendChallenge: 26 bytes, for a 64-byte registration.
func (s *Server) appendChallenge(out []byte, from netip.AddrPort, now time.Time) []byte {
	stamp := uint32(now.Unix())
	difficulty := byte(s.cfg.PowBits)
	mac := s.challengeMAC(from, stamp, difficulty)
	s.Metrics.challenged()
	out = binary.BigEndian.AppendUint32(appendHeader(out, typeChallenge), stamp)
	return append(append(out, difficulty), mac[:]...)
}

// powHolds: SHA-256(mac | secret | nonce) starts with `difficulty` zero bits.
func powHolds(mac [ChallengeMACSize]byte, secret [SecretSize]byte, nonce uint64, difficulty byte) bool {
	var in [ChallengeMACSize + SecretSize + 8]byte
	copy(in[:], mac[:])
	copy(in[ChallengeMACSize:], secret[:])
	binary.BigEndian.PutUint64(in[ChallengeMACSize+SecretSize:], nonce)
	h := sha256.Sum256(in[:])
	zeros := 0
	for _, x := range h {
		if x != 0 {
			zeros += bits.LeadingZeros8(x)
			break
		}
		zeros += 8
	}
	return zeros >= int(difficulty)
}

// proofHolds checks a RegisterHostOpen: our challenge to this address, not
// stale, as hard as we ask, solved for this secret, and not used before.
// Under s.mu.
func (s *Server) proofHolds(from netip.AddrPort, d datagram, now time.Time) bool {
	age := now.Unix() - int64(d.stamp)
	if age < -5 || age > int64(s.cfg.ChallengeTTL/time.Second) || int(d.bits) < s.cfg.PowBits {
		return false
	}
	mac := s.challengeMAC(from, d.stamp, d.bits)
	if subtle.ConstantTimeCompare(mac[:], d.mac[:]) != 1 || !powHolds(d.mac, d.secret, d.powNonce, d.bits) {
		return false
	}
	if _, used := s.usedChallenges[d.mac]; used || len(s.usedChallenges) >= 1<<16 {
		s.Metrics.replayed()
		return false
	}
	return true
}

func (s *Server) useChallenge(mac [ChallengeMACSize]byte, now time.Time) {
	s.usedChallenges[mac] = now
}

func (s *Server) openCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.openSessions
}

func newPowKey() (k [32]byte) {
	_, _ = rand.Read(k[:])
	return k
}
