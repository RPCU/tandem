#pragma once

// A bad network, on purpose: added latency, jitter, loss and reordering, to
// test the smoothing (interpolation, poses, cars) and the reliable channels
// without a bad link at hand (docs/architecture/testing.md, "A bad
// network").
//
// LinkModel decides what happens to each datagram (seeded: repeatable in
// tests). Shaper is a UDP relay that puts one LinkModel on each direction:
// it listens on a port and forwards everything to a target, and the answers
// back, so ENet itself (acknowledgements, resends, pings) goes through the
// bad link exactly as the game's packets would. The mock player puts one in
// front of its endpoint (tandem-peer --latency/--jitter/--loss/--reorder).

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>

namespace tandem::net {
struct LinkShape {
  std::uint32_t latencyMs{}; // added to every datagram, one way
  std::uint32_t jitterMs{};  // up to this much more, at random (the order kept)
  float loss{};              // 0..1: the chance a datagram is lost
  float reorder{};           // 0..1: the chance one is held back behind later ones
  // Losses come in bursts of this many datagrams on average (0 or 1: each
  // one on its own). The share lost stays `loss` (a Gilbert-Elliott model:
  // a bad state that loses everything, entered and left at random).
  float burst{};
  bool Active() const { return latencyMs || jitterMs || loss > 0.f || reorder > 0.f; }
};

class LinkModel {
public:
  // A held-back datagram leaves this much after it would have (plus jitter).
  static constexpr std::uint64_t kReorderHoldUs = 30'000;

  explicit LinkModel(LinkShape shape, std::uint64_t seed = 1);

  // When a datagram that enters the link at `nowUs` comes out of it;
  // nullopt: lost. Without reordering, datagrams leave in the order they came.
  std::optional<std::uint64_t> Release(std::uint64_t nowUs);

  const LinkShape &Shape() const { return m_shape; }
  std::uint64_t Passed() const { return m_passed; }
  std::uint64_t Lost() const { return m_lost; }
  std::uint64_t Reordered() const { return m_reordered; }

private:
  LinkShape m_shape;
  std::mt19937_64 m_rng;
  bool m_bad{}; // in a loss burst
  std::uint64_t m_lastUs{};
  std::uint64_t m_passed{}, m_lost{}, m_reordered{};
};

class Shaper {
public:
  struct Counters {
    std::uint64_t passed{}, lost{}, reordered{};
    // UDP payload bytes that entered the link, towards the target and back.
    std::uint64_t bytesUp{}, bytesDown{};
  };

  // Listens on UDP `listenPort` (0: any free port) and relays every datagram
  // to `targetHost:targetPort`, and the target's answers back to whoever sent
  // them (one upstream socket per sender), each way through its own model.
  // Runs on a thread of its own until destroyed. `seed` 0: a random one.
  static std::unique_ptr<Shaper> Start(std::uint16_t listenPort, const std::string &targetHost,
                                       std::uint16_t targetPort, LinkShape shape,
                                       std::string &error, std::uint64_t seed = 0);
  ~Shaper();
  Shaper(const Shaper &) = delete;
  Shaper &operator=(const Shaper &) = delete;

  std::uint16_t Port() const;
  // Both directions together.
  Counters Stats() const;
  // An outage (a cable pulled, a Wi-Fi drop): while on, every datagram both
  // ways is lost, whatever the shape says.
  void SetOutage(bool on);

private:
  struct Impl;
  explicit Shaper(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> m_impl;
};
} // namespace tandem::net
