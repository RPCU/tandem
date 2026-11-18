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
//
// Data: the host addresses a guest by its channel; a guest's channel field is
// ignored and the relay stamps the guest's channel before handing it to the
// host. Registration requests are padded so no answer is ever larger than the
// request that caused it (no amplification).
package relay

import (
	"encoding/binary"
	"errors"
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
)

// ErrorCode tells a client why it was refused.
type ErrorCode byte

const (
	ErrBadToken       ErrorCode = 1
	ErrNoSession      ErrorCode = 2
	ErrSessionFull    ErrorCode = 3
	ErrTooManySession ErrorCode = 4
	ErrSessionTaken   ErrorCode = 5
)

var errMalformed = errors.New("malformed datagram")

type datagram struct {
	kind    msgType
	secret  [SecretSize]byte
	token   [TokenSize]byte
	channel uint16
	payload []byte
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

func registered(channel uint16) []byte {
	return binary.BigEndian.AppendUint16(header(typeRegistered, 2), channel)
}

func errorReply(code ErrorCode) []byte {
	return append(header(typeError, 1), byte(code))
}

// Reply is what a client reads back from the relay.
type Reply struct {
	Registered bool
	Error      ErrorCode
	Channel    uint16
	Payload    []byte
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
	}
	return Reply{}, errMalformed
}
