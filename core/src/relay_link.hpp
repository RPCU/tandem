#pragma once

// The session's side of tandem-relay (tandem/relay.hpp): a UDP proxy on
// 127.0.0.1 between ENet and the relay. Single-threaded, non-blocking: the
// session pumps it around each ENet service.

#include "tandem/relay.hpp"

#include <enet/enet.h>

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace tandem::net {
class RelayLink {
public:
  enum class Role { Host, Guest };

  // Host: ENet listens on `enetPort` (all interfaces); relayed guests reach it
  // from 127.0.0.1. Guest: ENet connects to Target() from `enetSource`, the
  // 127.0.0.1 address its own socket is bound to. A link that cannot start
  // (unknown relay, no socket) is Failed from the start.
  RelayLink(Role role, RelayOptions options, std::uint16_t enetPort, ENetAddress enetSource,
            std::uint32_t timeoutMs);
  ~RelayLink();
  RelayLink(const RelayLink &) = delete;
  RelayLink &operator=(const RelayLink &) = delete;

  // Moves what is waiting both ways, resends what went unanswered.
  void Pump();
  // Until one of the link's sockets or `enet` has something, or `ms` passes.
  void Wait(ENetSocket enet, enet_uint32 ms);

  ENetAddress Target() const { return m_innerAddress; }
  RelayStatus Status() const;
  // Guest: the session's secret (resolved from the code, or given).
  std::optional<RelaySecret> Secret() const;

private:
  enum class Phase { Resolving, Registering, Ready, Failed };
  struct Channel {
    ENetSocket socket{ENET_SOCKET_NULL};
    enet_uint32 lastMs{};
  };

  void Fail(std::string error);
  void SendRequest(enet_uint32 now);
  void ToRelay(const std::vector<std::uint8_t> &bytes, enet_uint32 now);
  void OnRelay(const std::uint8_t *data, std::size_t size, enet_uint32 now);
  void OnError(std::uint8_t code);
  Channel *ChannelFor(std::uint16_t channel, enet_uint32 now);
  void Timers(enet_uint32 now);

  Role m_role;
  RelayOptions m_options;
  std::uint32_t m_timeoutMs;
  Phase m_phase{Phase::Registering};
  RelayStatus m_status;

  ENetAddress m_relay{};
  ENetSocket m_outer{ENET_SOCKET_NULL};
  RelaySecret m_secret{};
  bool m_haveSecret{};
  // Guest: the secret came from the options (not from the code).
  bool m_secretGiven{};

  // Guest: ENet talks to m_inner from m_enetSource; what it says before the
  // relay registered us waits (a few datagrams: its connect).
  ENetSocket m_inner{ENET_SOCKET_NULL};
  ENetAddress m_innerAddress{};
  ENetAddress m_enetSource{};
  std::deque<std::vector<std::uint8_t>> m_pending;

  // Host: one socket per relayed guest, talking to ENet's port on 127.0.0.1.
  ENetAddress m_enetListen{};
  std::map<std::uint16_t, Channel> m_channels;

  enet_uint32 m_startMs{};
  enet_uint32 m_requestMs{};     // last registration or resolve sent
  enet_uint32 m_sentMs{};        // last datagram to the relay
  enet_uint32 m_codeRequestMs{}; // host: last code request
  enet_uint32 m_codeExpiresMs{}; // host: when its code expires
};
} // namespace tandem::net
