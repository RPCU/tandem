#include "tandem/session.hpp"

#include <enet/enet.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <atomic>
#include <mutex>

namespace tandem::net {
namespace {
std::mutex g_enetLock;
int g_enetUsers = 0;

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

// The name's IPv4 address (ENet speaks IPv4 only). ENet's own lookup missed
// it for a host whose name also resolves to IPv6 addresses (2026-11-17).
bool ResolveIpv4(const std::string &name, ENetAddress &address) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo *found = nullptr;
  if (getaddrinfo(name.c_str(), nullptr, &hints, &found) != 0 || !found)
    return false;
  address.host = reinterpret_cast<const sockaddr_in *>(found->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(found);
  return true;
}

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
  // Guest: the host while the connection is being set up, and since when.
  ENetPeer *connecting{};
  enet_uint32 connectStartMs{};
  // Our own stalls (a Poll() long after the last) make every peer look
  // silent: it is judged again only after a moment to hear from it.
  enet_uint32 lastPollMs{};
  std::optional<enet_uint32> judgeFromMs;

  ~Impl() {
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
  // silent one at `timeoutMs`; ENet's stays as a backstop.
  void Watch(ENetPeer *peer) const {
    enet_peer_ping_interval(peer, std::max<enet_uint32>(timeoutMs / 10, 1));
    enet_peer_timeout(peer, 0, timeoutMs, 2 * timeoutMs);
  }

  static enet_uint32 QuietMs(const ENetPeer *peer) {
    // lastReceiveTime is when the peer last acknowledged something.
    return peer->lastReceiveTime == 0 ? 0 : Since(enet_time_get(), peer->lastReceiveTime);
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
                                         std::string &error, std::chrono::milliseconds timeout) {
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
  return std::unique_ptr<Session>(new Session(std::move(impl), true));
}

std::unique_ptr<Session> Session::Connect(const std::string &hostName, std::uint16_t port,
                                          std::string &error, std::chrono::milliseconds timeout) {
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
  return std::unique_ptr<Session>(new Session(std::move(impl), false));
}

std::vector<Event> Session::Poll(std::chrono::milliseconds timeout) {
  std::vector<Event> events;
  ENetEvent event;
  auto wait = static_cast<enet_uint32>(timeout.count());

  while (enet_host_service(m_impl->host, &event, wait) > 0) {
    wait = 0;
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT:
      if (event.peer == m_impl->connecting)
        m_impl->connecting = nullptr;
      m_impl->Watch(event.peer);
      events.push_back({Event::Kind::Connected, m_impl->IdOf(event.peer), std::nullopt});
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      if (event.peer == m_impl->connecting)
        m_impl->connecting = nullptr;
      events.push_back({Event::Kind::Disconnected, m_impl->IdOf(event.peer), std::nullopt});
      break;
    case ENET_EVENT_TYPE_RECEIVE: {
      const std::span bytes(reinterpret_cast<const std::byte *>(event.packet->data),
                            event.packet->dataLength);
      auto message = proto::Decode(bytes);
      const auto kind = message ? Event::Kind::Received : Event::Kind::Malformed;
      events.push_back({kind, m_impl->IdOf(event.peer), std::move(message)});
      enet_packet_destroy(event.packet);
      break;
    }
    default:
      break;
    }
  }

  // Silent peers, and a host that never answered.
  auto *host = m_impl->host;
  const auto now = enet_time_get();
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
    if (open && Impl::QuietMs(peer) >= m_impl->timeoutMs) {
      events.push_back({Event::Kind::Disconnected, m_impl->IdOf(peer), std::nullopt});
      enet_peer_disconnect_now(peer, 0);
    }
  }
  if (auto *peer = m_impl->connecting;
      peer && Since(now, m_impl->connectStartMs) >= m_impl->timeoutMs) {
    m_impl->connecting = nullptr;
    events.push_back({Event::Kind::Disconnected, m_impl->IdOf(peer), std::nullopt});
    enet_peer_reset(peer);
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

std::vector<PeerId> Session::Peers() const {
  std::vector<PeerId> ids;
  for (std::size_t i = 0; i < m_impl->host->peerCount; ++i) {
    if (m_impl->host->peers[i].state == ENET_PEER_STATE_CONNECTED)
      ids.push_back(static_cast<PeerId>(i) + 1);
  }
  return ids;
}
} // namespace tandem::net
