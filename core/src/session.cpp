#include "tandem/session.hpp"

#include <enet/enet.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

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

constexpr std::uint32_t kTimeoutMinMs = 5'000;
constexpr std::uint32_t kTimeoutMaxMs = 15'000;
} // namespace

std::uint64_t NowUs() {
  using namespace std::chrono;
  return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

struct Session::Impl {
  ENetHost *host{};

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

std::unique_ptr<Session> Session::Listen(std::uint16_t port, std::size_t maxGuests,
                                         std::string &error) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();

  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  impl->host = enet_host_create(&address, maxGuests, proto::kChannelCount, 0, 0);
  if (!impl->host) {
    error = "could not listen on UDP port " + std::to_string(port);
    return nullptr;
  }
  return std::unique_ptr<Session>(new Session(std::move(impl), true));
}

std::unique_ptr<Session> Session::Connect(const std::string &hostName, std::uint16_t port,
                                          std::string &error) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();

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
  if (!enet_host_connect(impl->host, &address, proto::kChannelCount, proto::kMagic)) {
    error = "enet_host_connect failed";
    return nullptr;
  }
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
      enet_peer_timeout(event.peer, 0, kTimeoutMinMs, kTimeoutMaxMs);
      events.push_back({Event::Kind::Connected, m_impl->IdOf(event.peer), std::nullopt});
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
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
  if (auto *peer = m_impl->PeerOf(id))
    enet_peer_disconnect(peer, 0);
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
