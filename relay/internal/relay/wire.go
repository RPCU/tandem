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
// Redirect to the replica that takes it; a client follows one at most.
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
	typeRegisterHost  msgType = 1
	typeRegisterGuest msgType = 2
	typeRegistered    msgType = 3
	typeData          msgType = 4
	typeKeepalive     msgType = 5
	typeError         msgType = 6
	typeRequestCode   msgType = 7
	typeCode          msgType = 8
	typeResolveCode   msgType = 9
	typeSecret        msgType = 10
	typeRedirect      msgType = 11
	typeRejoinGuest   msgType = 12
	typeReclaimCode   msgType = 13
)

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
}

func parse(b []byte) (datagram, error) {
	var d datagram
	if len(b) < headerSize || string(b[:4]) != magic {
		return d, errMalformed
	}
	d.kind = msgType(b[4])
	body := b[headerSize:]
	switch d.kind {
	case typeRegisterHost:
		if len(b) != registerSize {
			return d, errMalformed
		}
		copy(d.secret[:], body[:SecretSize])
		copy(d.token[:], body[SecretSize:SecretSize+TokenSize])
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

// The relay's answers are appended to a buffer it reuses: no allocation.

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
	}
	return Reply{}, errMalformed
}
