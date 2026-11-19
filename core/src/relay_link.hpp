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

  // How long a relay that went silent is waited for, the relayed players
  // held (their connections not judged): a relay restarting in Kubernetes
  // is back within ~10 s. Past it a guest's link fails, a host's starts over
  // from the configured address.
  static constexpr enet_uint32 kHoldMs = 20'000;

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
  // ENet's peer at `address` is reached through the relay (host: one of the
  // relayed guests' local sockets; guest: the proxy).
  bool Carries(const ENetAddress &address) const;
  // The relay went silent while registered: relayed peers are being held.
  bool Holding() const { return m_phase == Phase::Ready && m_lost; }

private:
  enum class Phase { Resolving, Registering, Ready, Failed };
  struct Channel {
    ENetSocket socket{ENET_SOCKET_NULL};
    enet_uint32 lastMs{};
    std::uint16_t port{}; // its local port: ENet's peer address
  };

  void Fail(std::string error);
  void SendRequest(enet_uint32 now);
  void ToRelay(const std::vector<std::uint8_t> &bytes, enet_uint32 now);
  void OnRelay(const std::uint8_t *data, std::size_t size, enet_uint32 now);
  void OnError(std::uint8_t code);
  void OnRedirect(const std::uint8_t *body, std::size_t length, enet_uint32 now);
  // Host: registering anew at the configured address (after a failure, or
  // a relay that stayed silent).
  void StartOver(enet_uint32 now);
  void RequestCode(enet_uint32 now);
  Channel *ChannelFor(std::uint16_t channel, enet_uint32 now);
  void Timers(enet_uint32 now);

  Role m_role;
  RelayOptions m_options;
  std::uint32_t m_timeoutMs;
  Phase m_phase{Phase::Registering};
  RelayStatus m_status;

  // The relay in use, and the one configured (they differ after a redirect).
  ENetAddress m_relay{};
  ENetAddress m_home{};
  bool m_redirected{};
  ENetSocket m_outer{ENET_SOCKET_NULL};
  RelaySecret m_secret{};
  bool m_haveSecret{};
  // Guest: the secret came from the options (not from the code).
  bool m_secretGiven{};
  // Guest: the channel the relay gave us, asked for again after a restart.
  std::uint16_t m_channel{};

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
  enet_uint32 m_heardMs{};       // last datagram from the relay
  enet_uint32 m_codeRequestMs{}; // host: last code request
  enet_uint32 m_codeExpiresMs{}; // host: when its code expires
  // Registered, but the relay went silent at m_lostMs.
  bool m_lost{};
  enet_uint32 m_lostMs{};
};
} // namespace tandem::net
