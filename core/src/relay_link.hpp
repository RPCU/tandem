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
  // Direct paths on or off (tandem.ini's `direct`, changed in a session):
  // off, every path goes back through the relay at once.
  void SetDirect(bool on);
  // Host: another join code now (a kept one: the next salt; Status() has it).
  void NewCode();

private:
  // Choosing: a host probing its relays for the nearest.
  enum class Phase { Choosing, Resolving, Registering, Ready, Failed };
  // A direct path to the other side (host: one per guest; guest: to the host).
  struct Direct {
    std::uint32_t nonce{}; // from the relay's introduction; 0: none
    ENetAddress candidates[2]{};
    int count{};
    enet_uint32 punchUntilMs{};
    enet_uint32 punchedMs{}; // last punches sent
    ENetAddress to{};        // the candidate that answered
    bool answered{};         // `to` is known
    bool up{};               // our punch came back: data goes direct
    enet_uint32 heardMs{};   // last datagram on it
    enet_uint32 sentMs{};    // last datagram sent on it
  };
  struct Channel {
    ENetSocket socket{ENET_SOCKET_NULL};
    enet_uint32 lastMs{};
    std::uint16_t port{}; // its local port: ENet's peer address
    Direct direct;
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
  // Direct paths.
  void SendAddresses(enet_uint32 now);
  void OnIntroduce(const std::uint8_t *body, std::size_t length, enet_uint32 now);
  void OnDirect(const ENetAddress &from, std::uint8_t *data, std::size_t size, enet_uint32 now);
  void DirectTimers(Direct &direct, std::uint16_t channel, enet_uint32 now);
  // Sends an ENet datagram wrapped in `buffer` (kDataHeader + size) to the
  // other side: direct when that path is up, else through the relay.
  void Forward(Direct &direct, std::uint8_t *buffer, std::size_t size, enet_uint32 now);
  // Probes: the nearest relay, and the connection check.
  void Probe(const ENetAddress &to);
  bool OnObserved(const ENetAddress &from, const std::uint8_t *data, std::size_t size,
                  enet_uint32 now);
  void Choose(enet_uint32 now);
  NatMapping Mapping() const;
  // Guest: the code led nowhere here; true when there is another relay to ask.
  bool NextHome(enet_uint32 now);

  Role m_role;
  RelayOptions m_options;
  std::uint32_t m_timeoutMs;
  Phase m_phase{Phase::Registering};
  RelayStatus m_status;

  // The relay in use, and the one configured (they differ after a redirect).
  ENetAddress m_relay{};
  ENetAddress m_home{};
  // Redirects followed since the request went out: one within a network,
  // one more to cross regions.
  int m_redirects{};
  // Every relay configured (m_home is one of them), and the one a guest
  // asks (it goes down the list when a code leads nowhere).
  std::vector<ENetAddress> m_homes;
  std::size_t m_homeIndex{};
  // Host, Choosing: each relay's round trip (0: no answer yet).
  std::vector<enet_uint32> m_rttMs;
  enet_uint32 m_chooseMs{}, m_probedMs{};
  std::uint32_t m_probeNonce{};
  // The connection check: what each relay port saw of us, and the second
  // port asked (the fleet's entrance, or the relay's port + 1).
  struct Seen {
    ENetAddress relay, us;
  };
  std::vector<Seen> m_seen;
  ENetAddress m_checkAlt{};
  int m_checkTries{};
  enet_uint32 m_checkMs{};
  // Host: a kept code (asked for, and given).
  bool m_keepFailed{};
  bool m_codeKept{};
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

  // Direct paths: allowed, the guest's to its host, when our addresses
  // went to the relay last (guest: when to ask for an introduction again).
  bool m_directOn{true};
  Direct m_direct;
  enet_uint32 m_addressesMs{};
  bool m_addressesSent{};
  enet_uint32 m_introduceAgainMs{};
};
} // namespace tandem::net
