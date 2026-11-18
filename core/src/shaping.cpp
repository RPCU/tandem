#include "tandem/shaping.hpp"
#include "tandem/session.hpp"

#include "enet_util.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <queue>
#include <thread>
#include <vector>

namespace tandem::net {
LinkModel::LinkModel(LinkShape shape, std::uint64_t seed) : m_shape(shape), m_rng(seed) {
  m_shape.loss = std::clamp(m_shape.loss, 0.f, 1.f);
  m_shape.reorder = std::clamp(m_shape.reorder, 0.f, 1.f);
}

std::optional<std::uint64_t> LinkModel::Release(std::uint64_t nowUs) {
  std::uniform_real_distribution<float> chance(0.f, 1.f);
  if (m_shape.loss > 0.f && chance(m_rng) < m_shape.loss) {
    ++m_lost;
    return std::nullopt;
  }
  ++m_passed;
  const auto jitterUs = static_cast<std::uint64_t>(m_shape.jitterMs) * 1000;
  std::uniform_int_distribution<std::uint64_t> spread(0, jitterUs);
  auto at = nowUs + static_cast<std::uint64_t>(m_shape.latencyMs) * 1000 + spread(m_rng);
  if (m_shape.reorder > 0.f && chance(m_rng) < m_shape.reorder) {
    // Held back: the ones after it overtake it. The queue's order is not
    // touched, so it is the only one out of place.
    ++m_reordered;
    return at + kReorderHoldUs + jitterUs;
  }
  // Jitter alone keeps the order, as a real queue does.
  at = (std::max)(at, m_lastUs); // (): windows.h has a max macro
  m_lastUs = at;
  return at;
}

namespace {
constexpr std::size_t kMaxDatagram = 65'536;
// A sender heard from no more than this long ago keeps its upstream socket.
constexpr std::uint64_t kForgetUs = 60'000'000;

ENetSocket OpenSocket(std::uint16_t port) {
  const auto socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (socket == ENET_SOCKET_NULL)
    return socket;
  ENetAddress any{};
  any.host = ENET_HOST_ANY;
  any.port = port;
  if (enet_socket_bind(socket, &any) < 0 ||
      enet_socket_set_option(socket, ENET_SOCKOPT_NONBLOCK, 1) < 0) {
    enet_socket_destroy(socket);
    return ENET_SOCKET_NULL;
  }
  enet_socket_set_option(socket, ENET_SOCKOPT_RCVBUF, 1 << 20);
  enet_socket_set_option(socket, ENET_SOCKOPT_SNDBUF, 1 << 20);
  return socket;
}

bool Same(const ENetAddress &a, const ENetAddress &b) {
  return a.host == b.host && a.port == b.port;
}
} // namespace

struct Shaper::Impl {
  struct Datagram {
    std::uint64_t at{};
    std::uint64_t order{}; // ties leave in arrival order
    ENetSocket from{};
    ENetAddress to{};
    std::vector<std::uint8_t> bytes;
    bool operator>(const Datagram &other) const {
      return at != other.at ? at > other.at : order > other.order;
    }
  };
  struct Client {
    ENetAddress address{};
    ENetSocket upstream{ENET_SOCKET_NULL};
    std::uint64_t lastUs{};
  };

  ENetSocket listen{ENET_SOCKET_NULL};
  ENetAddress target{};
  LinkModel up, down; // towards the target, back from it
  std::vector<Client> clients;
  std::priority_queue<Datagram, std::vector<Datagram>, std::greater<>> queue;
  std::uint64_t order{};
  std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(kMaxDatagram);
  std::atomic<std::uint64_t> passed{}, lost{}, reordered{};
  std::jthread thread;

  Impl(LinkShape shape, std::uint64_t seed) : up(shape, seed), down(shape, seed * 7919 + 1) {}

  ~Impl() {
    thread = {}; // stop and join first: it uses everything below
    for (auto &client : clients)
      enet_socket_destroy(client.upstream);
    if (listen != ENET_SOCKET_NULL)
      enet_socket_destroy(listen);
    ReleaseEnet();
  }

  void Publish() {
    passed = up.Passed() + down.Passed();
    lost = up.Lost() + down.Lost();
    reordered = up.Reordered() + down.Reordered();
  }

  // Every datagram waiting on `socket`, through `model`, to go out of `from`.
  template <class To> void Drain(ENetSocket socket, LinkModel &model, To &&destination) {
    for (;;) {
      ENetAddress sender{};
      ENetBuffer chunk; // field order differs between platforms: no braces
      chunk.data = buffer.data();
      chunk.dataLength = buffer.size();
      const int size = enet_socket_receive(socket, &sender, &chunk, 1);
      if (size <= 0)
        return; // nothing more (0), or an error for this one datagram
      const auto now = NowUs();
      ENetSocket from{};
      ENetAddress to{};
      if (!destination(sender, from, to))
        continue;
      if (const auto at = model.Release(now))
        queue.push({*at, order++, from, to,
                    std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + size)});
    }
  }

  Client *ClientOf(const ENetAddress &address, std::uint64_t now) {
    for (auto &client : clients)
      if (Same(client.address, address)) {
        client.lastUs = now;
        return &client;
      }
    const auto upstream = OpenSocket(0);
    if (upstream == ENET_SOCKET_NULL)
      return nullptr;
    clients.push_back({address, upstream, now});
    return &clients.back();
  }

  void Run(std::stop_token stop) {
    while (!stop.stop_requested()) {
      auto now = NowUs();
      while (!queue.empty() && queue.top().at <= now) {
        const auto &datagram = queue.top();
        ENetBuffer chunk;
        chunk.data = const_cast<std::uint8_t *>(datagram.bytes.data());
        chunk.dataLength = datagram.bytes.size();
        enet_socket_send(datagram.from, &datagram.to, &chunk, 1);
        queue.pop();
      }

      // Wait for input, or until the next datagram is due (2 ms at most).
      enet_uint32 waitMs = 2;
      if (!queue.empty())
        waitMs = static_cast<enet_uint32>(
            std::min<std::uint64_t>(queue.top().at > now ? (queue.top().at - now) / 1000 : 0, 2));
      ENetSocketSet ready;
      ENET_SOCKETSET_EMPTY(ready);
      ENET_SOCKETSET_ADD(ready, listen);
      ENetSocket highest = listen;
      for (const auto &client : clients) {
        ENET_SOCKETSET_ADD(ready, client.upstream);
        highest = (std::max)(highest, client.upstream);
      }
      if (enet_socketset_select(highest, &ready, nullptr, waitMs) <= 0)
        continue;

      if (ENET_SOCKETSET_CHECK(ready, listen))
        Drain(listen, up, [&](const ENetAddress &sender, ENetSocket &from, ENetAddress &to) {
          const auto *client = ClientOf(sender, NowUs());
          if (!client)
            return false;
          from = client->upstream;
          to = target;
          return true;
        });
      // Copies: ClientOf may grow the list while a drain runs.
      const auto known = clients;
      for (const auto &client : known)
        if (ENET_SOCKETSET_CHECK(ready, client.upstream))
          Drain(client.upstream, down, [&](const ENetAddress &, ENetSocket &from, ENetAddress &to) {
            from = listen;
            to = client.address;
            return true;
          });

      now = NowUs();
      std::erase_if(clients, [&](const Client &client) {
        if (now - client.lastUs < kForgetUs)
          return false;
        enet_socket_destroy(client.upstream);
        return true;
      });
      Publish();
    }
  }
};

Shaper::Shaper(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Shaper::~Shaper() = default;

std::unique_ptr<Shaper> Shaper::Start(std::uint16_t listenPort, const std::string &targetHost,
                                      std::uint16_t targetPort, LinkShape shape, std::string &error,
                                      std::uint64_t seed) {
  if (!AcquireEnet()) {
    error = "enet_initialize failed";
    return nullptr;
  }
  if (seed == 0)
    seed = std::random_device{}() | 1;
  auto impl = std::make_unique<Impl>(shape, seed);
  if (!ResolveIpv4(targetHost, impl->target)) {
    error = "unknown host " + targetHost;
    return nullptr;
  }
  impl->target.port = targetPort;
  impl->listen = OpenSocket(listenPort);
  if (impl->listen == ENET_SOCKET_NULL) {
    error = "could not listen on UDP port " + std::to_string(listenPort);
    return nullptr;
  }
  impl->thread = std::jthread([raw = impl.get()](std::stop_token stop) { raw->Run(stop); });
  return std::unique_ptr<Shaper>(new Shaper(std::move(impl)));
}

std::uint16_t Shaper::Port() const {
  ENetAddress address{};
  if (enet_socket_get_address(m_impl->listen, &address) != 0)
    return 0;
  return address.port;
}

Shaper::Counters Shaper::Stats() const {
  return {m_impl->passed.load(), m_impl->lost.load(), m_impl->reordered.load()};
}
} // namespace tandem::net
