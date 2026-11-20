// Package relay forwards a co-op session's UDP datagrams between its host and
// its guests when they cannot reach each other directly (NAT, CGNAT).
//
// The relay never reads the game's traffic: it forwards opaque ENet datagrams
// by channel. A session is a 16-byte secret the host picks and shares with its
// guests in the invite; the relay keeps no state beyond a few minutes of
// silence.
//
// Wire format (all integers big-endian), every datagram:
//
//	magic "TNR1" (4) | type (1) | body
//
//	RegisterHost  (1)  secret (16) | host token (32) | zero padding to 64 bytes
//	RegisterGuest (2)  secret (16) | zero padding to 64 bytes
//	Registered    (3)  channel (2)                 relay -> client
//	Data          (4)  channel (2) | payload
//	Keepalive     (5)  -                           client -> relay
//	Error         (6)  code (1)                    relay -> client
//	RequestCode   (7)  secret (16) | renew (1) | zero padding to 64 bytes
//	Code          (8)  code (6) | seconds left (2)  relay -> host
//	ResolveCode   (9)  code (6) | zero padding to 64 bytes
//	Secret        (10) secret (16)                 relay -> guest
//	Redirect      (11) family (1) | address (4 or 16) | port (2)  relay -> client
//	RejoinGuest   (12) secret (16) | channel (2) | zero padding to 64 bytes
//	ReclaimCode   (13) secret (16) | code (6) | seconds left (2) | zero padding to 64 bytes
//	Addresses     (14) flags (1) | count (1) | count x (IPv4 (4) | port (2)) | zero padding to 64 bytes
//	Introduce     (15) channel (2) | nonce (4) | flags (1) | public IPv4 (4) | port (2) | LAN IPv4 (4) | port (2)  relay -> client
//	Punch         (16) nonce (4) | channel (2) | ack (1)  client <-> client, never to the relay
//	Probe         (17) nonce (4) | zero padding to 64 bytes
//	Observed      (18) nonce (4) | family (1) | address (4 or 16) | port (2)  relay -> client
//	RegisterHostKeep (19) secret (16) | host token (32) | salt (4) | zero padding to 64 bytes
//
// Kept codes: RegisterHostKeep registers like RegisterHost and gives the
// session its host token's kept code (keptCode: derived from the token and
// the salt, so the same every session until the host asks for another by
// changing the salt). It lives while its host is registered (Code's
// seconds left 0xFFFF) and dies with the session. Every kept code of a
// token belongs to one replica (keepOwner): the others redirect there.
// Sent again with another salt by the registered host, it changes the code
// at once.
//
// Probe is answered to anyone, on every port (at most 28 bytes for 64): a
// client measures its round trip to each relay it may host on, and learns
// its public address and port as each relay port sees them (the connection
// check: one mapping for every destination, or one per destination).
//
// Regions (Config.Regions): independent fleets, sharing nothing but each
// other's entrance addresses. A code names its region too (shardOf); a
// lookup in the wrong region is redirected to the right region's entrance,
// which redirects to the owner: two redirects at most.
//
// Direct paths (hole punching): a registered client tells the relay its LAN
// addresses (Addresses: up to 3, private or loopback only) and its NAT's
// flags (FlagMapping*). A guest's Addresses is answered with two
// Introductions, one to it and one to its host, each naming the other's
// public address as the relay sees it, its first LAN address and a nonce
// drawn for the pair: 24 bytes each, 48 in all for a 64-byte request, sent
// only to members of the guest's own session, at most once per
// IntroduceInterval. The two then send each other Punch datagrams from the
// socket they use for the relay; once one is answered (ack 1), they send
// Data to each other directly, falling back to the relay when it goes
// silent.
//
// Data: the host addresses a guest by its channel; a guest's channel field is
// ignored and the relay stamps the guest's channel before handing it to the
// host. Registration requests are padded so no answer is ever larger than the
// request that caused it (no amplification), and they are the only requests
// an unregistered address gets an answer to.
//
// Join codes: a registered host asks for a short code naming its session
// (RequestCode, from the host's own address, with its secret; renew 1 asks
// for a new one). A guest trades the code for the session's secret
// (ResolveCode -> Secret), then registers with the secret as usual, and
// joins again with it later, after the code expired. A code is 6 characters
// of Crockford's base32 (ASCII, upper case; see NormalizeCode), lives
// CodeTTL, and dies with its session.
//
// Replicas (see Config.Peers): each relay of a fleet owns its sessions, and a
// code names its owner (shardOf). A request for another replica's session,
// or a new session on a replica that is draining, is answered with a
// Redirect to the replica that takes it; a client follows two at most (one
// within a fleet, one more across regions).
// RejoinGuest is RegisterGuest asking for the channel the guest had, and
// ReclaimCode a registered host asking for the code it had, both after the
// relay restarted and forgot them (granted when free).
package relay

import (
	"encoding/binary"
	"errors"
	"net/netip"
	"strings"
)

const (
	magic      = "TNR1"
	headerSize = 5

	SecretSize   = 16
	TokenSize    = 32
	registerSize = 64

	// Larger than any ENet datagram (ENet's default MTU is 1392).
	MaxPayload = 1400
)

type msgType byte

const (
	typeRegisterHost     msgType = 1
	typeRegisterGuest    msgType = 2
	typeRegistered       msgType = 3
	typeData             msgType = 4
	typeKeepalive        msgType = 5
	typeError            msgType = 6
	typeRequestCode      msgType = 7
	typeCode             msgType = 8
	typeResolveCode      msgType = 9
	typeSecret           msgType = 10
	typeRedirect         msgType = 11
	typeRejoinGuest      msgType = 12
	typeReclaimCode      msgType = 13
	typeAddresses        msgType = 14
	typeIntroduce        msgType = 15
	typePunch            msgType = 16
	typeProbe            msgType = 17
	typeObserved         msgType = 18
	typeRegisterHostKeep msgType = 19
)

// KeptCodeLeft is a kept code's seconds left in a Code: it lives while its
// host is registered.
const KeptCodeLeft = 0xFFFF

// Addresses' flags: what the client learnt of its NAT (the connection
// check): how its public port is chosen.
const (
	FlagMappingUnknown byte = 0
	// The same public port whichever relay port it sends to: punching works.
	FlagMappingStable byte = 1
	// Another public port per destination (a symmetric NAT): punching
	// cannot work from both sides at once.
	FlagMappingVaries byte = 2
	flagMappingMask   byte = 3
)

// MaxLAN is the LAN addresses an Addresses message carries.
const MaxLAN = 3

// ErrorCode tells a client why it was refused.
type ErrorCode byte

const (
	ErrBadToken       ErrorCode = 1
	ErrNoSession      ErrorCode = 2
	ErrSessionFull    ErrorCode = 3
	ErrTooManySession ErrorCode = 4
	ErrSessionTaken   ErrorCode = 5
	// The join code is wrong or expired.
	ErrNoCode ErrorCode = 6
	// Too many code lookups from this address, or from everyone: wait.
	ErrSlowDown ErrorCode = 7
	// The relay is shutting down and has no other replica to send a new
	// session to: try again in a moment.
	ErrDraining ErrorCode = 8
)

// CodeSize is a join code's length, in characters of codeAlphabet.
const CodeSize = 6

// Crockford's base32: no I, L, O or U, so a code read aloud or copied by
// hand is not mistaken. 32 symbols: a random byte's low 5 bits pick one
// without bias.
const codeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

// NormalizeCode turns what a player typed into a code: case, dashes and
// spaces do not matter, O reads as 0, I and L as 1. ok is false when it is
// not a code.
func NormalizeCode(text string) (code string, ok bool) {
	out := make([]byte, 0, CodeSize)
	for i := 0; i < len(text); i++ {
		c := text[i]
		switch {
		case c == '-' || c == ' ':
			continue
		case c >= 'a' && c <= 'z':
			c -= 'a' - 'A'
		}
		switch c {
		case 'O':
			c = '0'
		case 'I', 'L':
			c = '1'
		}
		if !strings.ContainsRune(codeAlphabet, rune(c)) || len(out) == CodeSize {
			return "", false
		}
		out = append(out, c)
	}
	if len(out) != CodeSize {
		return "", false
	}
	return string(out), true
}

var errMalformed = errors.New("malformed datagram")

type datagram struct {
	kind    msgType
	secret  [SecretSize]byte
	token   [TokenSize]byte
	channel uint16
	payload []byte
	renew   bool
	code    [CodeSize]byte
	// ReclaimCode: the code's seconds left.
	left uint16
	// Addresses.
	flags byte
	lan   [MaxLAN]netip.AddrPort
	// Probe.
	nonce uint32
	// RegisterHostKeep.
	salt uint32
}

func parse(b []byte) (datagram, error) {
	var d datagram
	if len(b) < headerSize || string(b[:4]) != magic {
		return d, errMalformed
	}
	d.kind = msgType(b[4])
	body := b[headerSize:]
	switch d.kind {
	case typeRegisterHost, typeRegisterHostKeep:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
		copy(d.token[:], body[SecretSize:SecretSize+TokenSize])
		d.salt = binary.BigEndian.Uint32(body[SecretSize+TokenSize:])
	case typeRegisterGuest:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
	case typeData:
		if len(body) < 2 || len(body)-2 > MaxPayload {
			return d, errMalformed
		}
		d.channel = binary.BigEndian.Uint16(body)
		d.payload = body[2:]
	case typeKeepalive:
	case typeRequestCode:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
		d.renew = body[SecretSize] == 1
	case typeResolveCode:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.code[:], body[:CodeSize])
	case typeRejoinGuest:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
		d.channel = binary.BigEndian.Uint16(body[SecretSize:])
	case typeReclaimCode:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
		copy(d.code[:], body[SecretSize:SecretSize+CodeSize])
		d.left = binary.BigEndian.Uint16(body[SecretSize+CodeSize:])
	case typeAddresses:
		if len(b) != registerSize || body[1] > MaxLAN {
			return d, errMalformed
		}
		d.flags = body[0]
		for i := range int(body[1]) {
			at := body[2+6*i:]
			a := netip.AddrPortFrom(netip.AddrFrom4([4]byte(at[:4])), binary.BigEndian.Uint16(at[4:]))
			// Only addresses on the client's own network: a client cannot have
			// the other side send punches at a stranger.
			if a.Port() != 0 && (a.Addr().IsPrivate() || a.Addr().IsLoopback()) {
				d.lan[i] = a
			}
		}
	case typeProbe:
		if len(b) != registerSize {
			return d, errMalformed
		}
		d.nonce = binary.BigEndian.Uint32(body)
	default:
		return d, errMalformed
	}
	return d, nil
}

func header(kind msgType, extra int) []byte {
	b := make([]byte, headerSize, headerSize+extra)
	copy(b, magic)
	b[4] = byte(kind)
	return b
}

// RegisterHost builds a host's registration.
func RegisterHost(secret [SecretSize]byte, token [TokenSize]byte) []byte {
	b := header(typeRegisterHost, registerSize)
	b = append(b, secret[:]...)
	b = append(b, token[:]...)
	return append(b, make([]byte, registerSize-len(b))...)
}

// RegisterHostKeep builds a host's registration asking for its kept code.
func RegisterHostKeep(secret [SecretSize]byte, token [TokenSize]byte, salt uint32) []byte {
	b := header(typeRegisterHostKeep, registerSize)
	b = append(b, secret[:]...)
	b = append(b, token[:]...)
	b = binary.BigEndian.AppendUint32(b, salt)
	return append(b, make([]byte, registerSize-len(b))...)
}

// RegisterGuest builds a guest's registration.
func RegisterGuest(secret [SecretSize]byte) []byte {
	b := header(typeRegisterGuest, registerSize)
	b = append(b, secret[:]...)
	return append(b, make([]byte, registerSize-len(b))...)
}

// Data wraps a payload for a channel.
func Data(channel uint16, payload []byte) []byte {
	b := header(typeData, 2+len(payload))
	b = binary.BigEndian.AppendUint16(b, channel)
	return append(b, payload...)
}

// Keepalive keeps a registration alive while nothing else is sent.
func Keepalive() []byte { return header(typeKeepalive, 0) }

// RequestCode builds a host's request for its session's join code; renew
// asks for a new one instead of the current one.
func RequestCode(secret [SecretSize]byte, renew bool) []byte {
	b := header(typeRequestCode, registerSize)
	b = append(b, secret[:]...)
	if renew {
		b = append(b, 1)
	}
	return append(b, make([]byte, registerSize-len(b))...)
}

// ResolveCode builds a guest's request for the session a code names: the
// code normalized (NormalizeCode; the relay applies it again), or as typed
// when it is not a code, cut or zero-padded to CodeSize.
func ResolveCode(code string) []byte {
	if normal, ok := NormalizeCode(code); ok {
		code = normal
	}
	b := header(typeResolveCode, registerSize)
	var raw [CodeSize]byte
	copy(raw[:], code)
	b = append(b, raw[:]...)
	return append(b, make([]byte, registerSize-len(b))...)
}

// RejoinGuest builds a guest's registration asking for the channel it had.
func RejoinGuest(secret [SecretSize]byte, channel uint16) []byte {
	b := header(typeRejoinGuest, registerSize)
	b = append(b, secret[:]...)
	b = binary.BigEndian.AppendUint16(b, channel)
	return append(b, make([]byte, registerSize-len(b))...)
}

// ReclaimCode builds a registered host's request for the code it had, with
// its seconds left.
func ReclaimCode(secret [SecretSize]byte, code string, left uint16) []byte {
	b := header(typeReclaimCode, registerSize)
	b = append(b, secret[:]...)
	var raw [CodeSize]byte
	copy(raw[:], code)
	b = append(b, raw[:]...)
	b = binary.BigEndian.AppendUint16(b, left)
	return append(b, make([]byte, registerSize-len(b))...)
}

// Addresses builds a registered client's LAN addresses (IPv4, MaxLAN at
// most) and NAT flags.
func Addresses(flags byte, lan []netip.AddrPort) []byte {
	b := header(typeAddresses, registerSize)
	lan = lan[:min(len(lan), MaxLAN)]
	b = append(b, flags, byte(len(lan)))
	for _, a := range lan {
		raw := a.Addr().Unmap().As4()
		b = binary.BigEndian.AppendUint16(append(b, raw[:]...), a.Port())
	}
	return append(b, make([]byte, registerSize-len(b))...)
}

// Probe builds a client's probe: answered with what the relay sees of it.
func Probe(nonce uint32) []byte {
	b := binary.BigEndian.AppendUint32(header(typeProbe, registerSize), nonce)
	return append(b, make([]byte, registerSize-len(b))...)
}

// At most 28 bytes (an IPv6 address).
func appendObserved(b []byte, nonce uint32, seen netip.AddrPort) []byte {
	b = binary.BigEndian.AppendUint32(appendHeader(b, typeObserved), nonce)
	if a := seen.Addr().Unmap(); a.Is4() {
		raw := a.As4()
		b = append(append(b, 4), raw[:]...)
	} else {
		raw := a.As16()
		b = append(append(b, 6), raw[:]...)
	}
	return binary.BigEndian.AppendUint16(b, seen.Port())
}

// Punch builds a punch datagram (client to client).
func Punch(nonce uint32, channel uint16, ack bool) []byte {
	b := binary.BigEndian.AppendUint32(header(typePunch, 7), nonce)
	b = binary.BigEndian.AppendUint16(b, channel)
	if ack {
		return append(b, 1)
	}
	return append(b, 0)
}

// The relay's answers are appended to a buffer it reuses: no allocation.

// introduceSize: what one Introduce takes (two answer one Addresses).
const introduceSize = headerSize + 2 + 4 + 1 + 6 + 6

func appendIntroduce(b []byte, channel uint16, nonce uint32, flags byte, public, lan netip.AddrPort) []byte {
	b = binary.BigEndian.AppendUint16(appendHeader(b, typeIntroduce), channel)
	b = append(binary.BigEndian.AppendUint32(b, nonce), flags)
	for _, a := range []netip.AddrPort{public, lan} {
		var raw [4]byte
		if a.Addr().Unmap().Is4() {
			raw = a.Addr().Unmap().As4()
		}
		b = binary.BigEndian.AppendUint16(append(b, raw[:]...), a.Port())
	}
	return b
}

func appendHeader(b []byte, kind msgType) []byte {
	return append(append(b, magic...), byte(kind))
}

func appendCode(b []byte, code string, left uint16) []byte {
	b = append(appendHeader(b, typeCode), code...)
	return binary.BigEndian.AppendUint16(b, left)
}

func appendSecret(b []byte, secret [SecretSize]byte) []byte {
	return append(appendHeader(b, typeSecret), secret[:]...)
}

func appendRegistered(b []byte, channel uint16) []byte {
	return binary.BigEndian.AppendUint16(appendHeader(b, typeRegistered), channel)
}

func appendError(b []byte, code ErrorCode) []byte {
	return append(appendHeader(b, typeError), byte(code))
}

// At most 24 bytes (an IPv6 address): smaller than any request it answers.
func appendRedirect(b []byte, to netip.AddrPort) []byte {
	b = appendHeader(b, typeRedirect)
	if a := to.Addr().Unmap(); a.Is4() {
		raw := a.As4()
		b = append(append(b, 4), raw[:]...)
	} else {
		raw := a.As16()
		b = append(append(b, 6), raw[:]...)
	}
	return binary.BigEndian.AppendUint16(b, to.Port())
}

// Reply is what a client reads back from the relay.
type Reply struct {
	Registered bool
	Error      ErrorCode
	Channel    uint16
	Payload    []byte
	// Code: the host's join code and its seconds left.
	Code     string
	CodeLeft uint16
	// Secret: the session a code names.
	Secret    [SecretSize]byte
	HasSecret bool
	// Redirect: where to ask instead.
	Redirect netip.AddrPort
	// Introduce: the other side of a direct path (Channel: the guest's).
	Introduced bool
	Nonce      uint32
	PeerFlags  byte
	Peer       netip.AddrPort
	PeerLAN    netip.AddrPort
	// Observed: the client's address as the relay saw it (Nonce: the probe's).
	Observed netip.AddrPort
}

// ParseReply decodes a relay -> client datagram.
func ParseReply(b []byte) (Reply, error) {
	if len(b) < headerSize || string(b[:4]) != magic {
		return Reply{}, errMalformed
	}
	body := b[headerSize:]
	switch msgType(b[4]) {
	case typeRegistered:
		if len(body) != 2 {
			return Reply{}, errMalformed
		}
		return Reply{Registered: true, Channel: binary.BigEndian.Uint16(body)}, nil
	case typeError:
		if len(body) != 1 {
			return Reply{}, errMalformed
		}
		return Reply{Error: ErrorCode(body[0])}, nil
	case typeData:
		if len(body) < 2 {
			return Reply{}, errMalformed
		}
		return Reply{Channel: binary.BigEndian.Uint16(body), Payload: body[2:]}, nil
	case typeCode:
		if len(body) != CodeSize+2 {
			return Reply{}, errMalformed
		}
		return Reply{Code: string(body[:CodeSize]), CodeLeft: binary.BigEndian.Uint16(body[CodeSize:])}, nil
	case typeSecret:
		if len(body) != SecretSize {
			return Reply{}, errMalformed
		}
		return Reply{Secret: [SecretSize]byte(body), HasSecret: true}, nil
	case typeRedirect:
		var addr netip.Addr
		switch {
		case len(body) == 1+4+2 && body[0] == 4:
			addr = netip.AddrFrom4([4]byte(body[1:5]))
		case len(body) == 1+16+2 && body[0] == 6:
			addr = netip.AddrFrom16([16]byte(body[1:17]))
		default:
			return Reply{}, errMalformed
		}
		return Reply{Redirect: netip.AddrPortFrom(addr, binary.BigEndian.Uint16(body[len(body)-2:]))}, nil
	case typeObserved:
		var addr netip.Addr
		switch {
		case len(body) == 4+1+4+2 && body[4] == 4:
			addr = netip.AddrFrom4([4]byte(body[5:9]))
		case len(body) == 4+1+16+2 && body[4] == 6:
			addr = netip.AddrFrom16([16]byte(body[5:21]))
		default:
			return Reply{}, errMalformed
		}
		return Reply{
			Nonce:    binary.BigEndian.Uint32(body),
			Observed: netip.AddrPortFrom(addr, binary.BigEndian.Uint16(body[len(body)-2:])),
		}, nil
	case typeIntroduce:
		if len(b) != introduceSize {
			return Reply{}, errMalformed
		}
		at := func(p []byte) netip.AddrPort {
			return netip.AddrPortFrom(netip.AddrFrom4([4]byte(p[:4])), binary.BigEndian.Uint16(p[4:]))
		}
		return Reply{
			Introduced: true, Channel: binary.BigEndian.Uint16(body), Nonce: binary.BigEndian.Uint32(body[2:]),
			PeerFlags: body[6], Peer: at(body[7:]), PeerLAN: at(body[13:]),
		}, nil
	}
	return Reply{}, errMalformed
}
