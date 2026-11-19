#include "tandem/session.hpp"
#include "relay_link.hpp"
#include "resolve.hpp"

#include "enet_util.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace tandem::net {
namespace {
std::mutex g_enetLock;
int g_enetUsers = 0;
} // namespace

bool AcquireEnet() {
  std::lock_guard _(g_enetLock);
  if (g_enetUsers == 0 && enet_initialize() != 0)
    return false;
  ++g_enetUsers;
  return true;
}

void ReleaseEnet() {
  std::lock_guard _(g_enetLock);
  if (--g_enetUsers == 0)
    enet_deinitialize();
}

namespace {
// ENet's millisecond clock wraps; its differences do not.
enet_uint32 Since(enet_uint32 now, enet_uint32 then) { return now - then; }
bool Reached(enet_uint32 now, enet_uint32 when) {
  return static_cast<std::int32_t>(now - when) >= 0;
}
} // namespace

std::uint64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Session::Impl {
  ENetHost *host{};
  enet_uint32 timeoutMs{};
  // Guest: the host while the connection is being set up, since when, and
  // for how long at most.
  ENetPeer *connecting{};
  enet_uint32 connectStartMs{};
  enet_uint32 connectTimeoutMs{};
  // Through a relay: the proxy ENet talks to on 127.0.0.1.
  std::unique_ptr<RelayLink> relay;
  // Our own stalls (a Poll() long after the last) make every peer look
  // silent: it is judged again only after a moment to hear from it.
  enet_uint32 lastPollMs{};
  std::optional<enet_uint32> judgeFromMs;
  // The relay link was holding its peers at the last Poll().
  bool relayHeld{};

  ~Impl() {
    relay.reset();
    if (host)
      enet_host_destroy(host);
    ReleaseEnet();
  }

  PeerId IdOf(const ENetPeer *peer) const { return static_cast<PeerId>(peer - host->peers) + 1; }

  ENetPeer *PeerOf(PeerId id) const {
    if (id == 0 || id > host->peerCount)
      return nullptr;
    auto *peer = &host->peers[id - 1];
    return peer->state == ENET_PEER_STATE_CONNECTED ? peer : nullptr;
  }

  // ENet's own timeout only fires when a reliable resend is due, and resends
  // back off exponentially: a dead peer took 5 to 30 s to go. Poll() drops a
  // silent one at `timeoutMs`; ENet's stays as a backstop. A peer reached
  // through the relay outlives a relay that went silent and comes back
  // (RelayLink::kHoldMs): its backstop is past the hold.
  void Watch(ENetPeer *peer) const {
    enet_peer_ping_interval(peer, std::max<enet_uint32>(timeoutMs / 10, 1));
    if (Relayed(peer)) {
      const auto backstop = RelayLink::kHoldMs + 2 * timeoutMs;
      enet_peer_timeout(peer, 0, backstop, backstop);
    } else {
      enet_peer_timeout(peer, 0, timeoutMs, 2 * timeoutMs);
    }
  }

  bool Relayed(const ENetPeer *peer) const { return relay && relay->Carries(peer->address); }

  static enet_uint32 QuietMs(const ENetPeer *peer) {
    // lastReceiveTime is when the peer last acknowledged something.
    return peer->lastReceiveTime == 0 ? 0 : Since(enet_time_get(), peer->lastReceiveTime);
  }

  void OnEvent(ENetEvent &event, std::vector<Event> &events) {
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT:
      if (event.peer == connecting)
        connecting = nullptr;
      Watch(event.peer);
      events.push_back({Event::Kind::Connected, IdOf(event.peer), std::nullopt});
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      if (event.peer == connecting)
        connecting = nullptr;
      events.push_back({Event::Kind::Disconnected, IdOf(event.peer), std::nullopt});
      break;
    case ENET_EVENT_TYPE_RECEIVE: {
      const std::span bytes(reinterpret_cast<const std::byte *>(event.packet->data),
                            event.packet->dataLength);
      auto message = proto::Decode(bytes);
      const auto kind = message ? Event::Kind::Received : Event::Kind::Malformed;
      events.push_back({kind, IdOf(event.peer), std::move(message)});
      if (kind == Event::Kind::Malformed)
        events.back().helloVersion = proto::PeekHelloVersion(bytes);
      enet_packet_destroy(event.packet);
      break;
    }
    default:
      break;
    }
  }

  static ENetPacket *PacketOf(const proto::Message &message) {
    const auto bytes = proto::Encode(message);
    if (bytes.empty())
      return nullptr;
    const auto flags = proto::IsReliable(message) ? ENET_PACKET_FLAG_RELIABLE : 0u;
    return enet_packet_create(bytes.data(), bytes.size(), flags);
  }
};

Session::Session(std::unique_ptr<Impl> impl, bool isHost)
    : m_impl(std::move(impl)), m_isHost(isHost) {}

Session::~Session() = default;

std::unique_ptr<Session> Session::Listen(std::uint16_t port, std::size_t maxConnections,
                                         std::string &error, std::chrono::milliseconds timeout,
                                         const RelayOptions *relay) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->timeoutMs = static_cast<enet_uint32>(timeout.count());

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  impl->host = enet_host_create(&address, maxConnections, proto::kChannelCount, 0, 0);
  if (!impl->host) {
    error = "could not listen on UDP port " + std::to_string(port);
    return nullptr;
  }
  if (relay)
    impl->relay = std::make_unique<RelayLink>(RelayLink::Role::Host, *relay, port, ENetAddress{},
                                              impl->timeoutMs);
  return std::unique_ptr<Session>(new Session(std::move(impl), true));
}

std::unique_ptr<Session> Session::Connect(const std::string &hostName, std::uint16_t port,
                                          std::string &error, std::chrono::milliseconds timeout,
                                          std::chrono::milliseconds connectTimeout) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->timeoutMs = static_cast<enet_uint32>(timeout.count());

  impl->host = enet_host_create(nullptr, 1, proto::kChannelCount, 0, 0);
  if (!impl->host) {
    error = "could not create a UDP socket";
    return nullptr;
  }

  ENetAddress address{};
  if (!ResolveIpv4(hostName, address)) {
    error = "unknown host " + hostName;
    return nullptr;
  }
  address.port = port;
  impl->connecting = enet_host_connect(impl->host, &address, proto::kChannelCount, proto::kMagic);
  if (!impl->connecting) {
    error = "enet_host_connect failed";
    return nullptr;
  }
  impl->Watch(impl->connecting);
  impl->connectStartMs = enet_time_get();
  impl->connectTimeoutMs = connectTimeout.count() > 0
                               ? static_cast<enet_uint32>(connectTimeout.count())
                               : impl->timeoutMs;
  return std::unique_ptr<Session>(new Session(std::move(impl), false));
}

std::unique_ptr<Session> Session::ConnectRelay(const RelayOptions &relay, std::string &error,
                                               std::chrono::milliseconds timeout) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->timeoutMs = static_cast<enet_uint32>(timeout.count());

  // Bound now (not at the first send) so the proxy knows whom to listen to.
  ENetAddress local{};
  local.host = ENET_HOST_TO_NET_32(0x7F000001);
  impl->host = enet_host_create(&local, 1, proto::kChannelCount, 0, 0);
  if (!impl->host || enet_socket_get_address(impl->host->socket, &local) < 0) {
    error = "could not create a UDP socket";
    return nullptr;
  }
  local.host = ENET_HOST_TO_NET_32(0x7F000001);
  impl->relay =
      std::make_unique<RelayLink>(RelayLink::Role::Guest, relay, 0, local, impl->timeoutMs);
  if (const auto status = impl->relay->Status(); status.state == RelayStatus::State::Failed) {
    error = status.error;
    return nullptr;
  }
  const auto target = impl->relay->Target();
  impl->connecting = enet_host_connect(impl->host, &target, proto::kChannelCount, proto::kMagic);
  if (!impl->connecting) {
    error = "enet_host_connect failed";
    return nullptr;
  }
  impl->Watch(impl->connecting);
  impl->connectStartMs = enet_time_get();
  // The relay's answer, then the host's.
  impl->connectTimeoutMs = 2 * impl->timeoutMs;
  return std::unique_ptr<Session>(new Session(std::move(impl), false));
}

std::vector<Event> Session::Poll(std::chrono::milliseconds timeout) {
  std::vector<Event> events;
  ENetEvent event;
  const auto service = [&](enet_uint32 wait) {
    while (enet_host_service(m_impl->host, &event, wait) > 0) {
      wait = 0;
      m_impl->OnEvent(event, events);
    }
  };
  const auto wait = static_cast<enet_uint32>(timeout.count());
  if (auto *relay = m_impl->relay.get(); !relay) {
    service(wait);
  } else {
    // The proxy's sockets and ENet's, waited on together; each service is
    // followed by a pump that forwards what ENet just sent.
    relay->Pump();
    service(0);
    relay->Pump();
    if (events.empty() && wait > 0) {
      relay->Wait(m_impl->host->socket, wait);
      relay->Pump();
      service(0);
      relay->Pump();
    }
  }

  // Silent peers, and a host that never answered.
  auto *host = m_impl->host;
  const auto now = enet_time_get();
  // The relay went silent: its peers are held, not judged, while the link
  // asks it again. Once it answers, they get a moment to be heard (a ping
  // each, at once: ENet's own resends have backed off meanwhile).
  const bool held = m_impl->relay && m_impl->relay->Holding();
  if (m_impl->relayHeld && !held) {
    m_impl->judgeFromMs = now + m_impl->timeoutMs;
    for (std::size_t i = 0; i < host->peerCount; ++i)
      if (host->peers[i].state == ENET_PEER_STATE_CONNECTED && m_impl->Relayed(&host->peers[i]))
        enet_peer_ping(&host->peers[i]);
  }
  m_impl->relayHeld = held;
  if (m_impl->lastPollMs && Since(now, m_impl->lastPollMs) > m_impl->timeoutMs / 4)
    m_impl->judgeFromMs = now + m_impl->timeoutMs / 4;
  else if (m_impl->judgeFromMs && Reached(now, *m_impl->judgeFromMs))
    m_impl->judgeFromMs.reset();
  m_impl->lastPollMs = now;
  for (std::size_t i = 0; i < host->peerCount && !m_impl->judgeFromMs; ++i) {
    auto *peer = &host->peers[i];
    const bool open = peer->state == ENET_PEER_STATE_CONNECTED ||
                      peer->state == ENET_PEER_STATE_DISCONNECT_LATER ||
                      peer->state == ENET_PEER_STATE_DISCONNECTING;
    if (held && m_impl->Relayed(peer))
      continue;
    if (open && Impl::QuietMs(peer) >= m_impl->timeoutMs) {
      events.push_back({Event::Kind::Disconnected, m_impl->IdOf(peer), std::nullopt});
      enet_peer_disconnect_now(peer, 0);
    }
  }
  if (auto *peer = m_impl->connecting) {
    std::string reason;
    if (m_impl->relay && m_impl->relay->Status().state == RelayStatus::State::Failed)
      reason = m_impl->relay->Status().error;
    if (!reason.empty() || Since(now, m_impl->connectStartMs) >= m_impl->connectTimeoutMs) {
      m_impl->connecting = nullptr;
      events.push_back(
          {Event::Kind::Disconnected, m_impl->IdOf(peer), std::nullopt, std::move(reason)});
      enet_peer_reset(peer);
    }
  }
  return events;
}

bool Session::Send(PeerId id, const proto::Message &message) {
  auto *peer = m_impl->PeerOf(id);
  auto *packet = peer ? Impl::PacketOf(message) : nullptr;
  if (!packet)
    return false;
  if (enet_peer_send(peer, static_cast<enet_uint8>(proto::ChannelOf(message)), packet) != 0) {
    enet_packet_destroy(packet);
    return false;
  }
  return true;
}

bool Session::SendBytes(PeerId id, std::span<const std::byte> bytes) {
  auto *peer = m_impl->PeerOf(id);
  auto *packet =
      peer ? enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE) : nullptr;
  if (!packet)
    return false;
  if (enet_peer_send(peer, static_cast<enet_uint8>(proto::Channel::Event), packet) != 0) {
    enet_packet_destroy(packet);
    return false;
  }
  return true;
}

void Session::Broadcast(const proto::Message &message, PeerId except) {
  for (auto id : Peers()) {
    if (id != except)
      Send(id, message);
  }
}

void Session::Disconnect(PeerId id) {
  // Later: a goodbye sent together with what came before it is reset away
  // on arrival, with the packets still queued (a Refused, a Bye).
  if (auto *peer = m_impl->PeerOf(id))
    enet_peer_disconnect_later(peer, 0);
}

void Session::Drop(PeerId id) {
  if (auto *peer = m_impl->PeerOf(id))
    enet_peer_disconnect_now(peer, 0);
}

void Session::Flush() { enet_host_flush(m_impl->host); }

std::optional<Stats> Session::StatsOf(PeerId id) const {
  auto *peer = m_impl->PeerOf(id);
  if (!peer)
    return std::nullopt;
  return Stats{
      .rttMs = peer->roundTripTime,
      .rttVarianceMs = peer->roundTripTimeVariance,
      .packetLoss =
          static_cast<float>(peer->packetLoss) / static_cast<float>(ENET_PEER_PACKET_LOSS_SCALE),
      .quietMs = Impl::QuietMs(peer),
  };
}

std::optional<RelayStatus> Session::Relay() const {
  if (!m_impl->relay)
    return std::nullopt;
  return m_impl->relay->Status();
}

std::optional<RelaySecret> Session::RelaySessionSecret() const {
  return m_impl->relay ? m_impl->relay->Secret() : std::nullopt;
}

std::uint16_t Session::LocalPort() const {
  ENetAddress address{};
  if (enet_socket_get_address(m_impl->host->socket, &address) != 0)
    return 0;
  return address.port;
}

std::vector<PeerId> Session::Peers() const {
  std::vector<PeerId> ids;
  for (std::size_t i = 0; i < m_impl->host->peerCount; ++i) {
    if (m_impl->host->peers[i].state == ENET_PEER_STATE_CONNECTED)
      ids.push_back(static_cast<PeerId>(i) + 1);
  }
  return ids;
}
} // namespace tandem::net
