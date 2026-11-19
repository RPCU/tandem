#pragma once

// What a session puts on the wire (docs/architecture/netcode.md): bytes and
// messages per message type, per channel and per direction, and the
// datagrams ENet itself sent and received (its headers, acknowledgements,
// pings and resends included). net::Session counts; snapshots taken a few
// seconds apart give rates, for the plugin's log and tandem-peer's report.

#include "tandem/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace tandem::net {
inline constexpr std::size_t kMessageKinds = std::variant_size_v<proto::Message>;
// IPv4 and UDP headers: what every datagram costs on top of ENet's bytes.
inline constexpr std::size_t kUdpIpBytes = 28;

enum Direction : std::size_t { kSent = 0, kReceived = 1 };

struct Totals {
  std::uint64_t bytes{};
  std::uint64_t messages{};
  friend bool operator==(const Totals &, const Totals &) = default;
};

struct Traffic {
  // Encoded message bytes (proto::Encode), by Message index and direction.
  std::array<std::array<Totals, 2>, kMessageKinds> kinds{};
  std::array<std::array<Totals, 2>, proto::kChannelCount> channels{};
  // UDP payload bytes and datagrams, as ENet sent and received them.
  std::array<Totals, 2> wire{};

  void Count(std::size_t kind, proto::Channel channel, Direction direction, std::size_t bytes);
  // Bits on the wire (IP and UDP headers included) one way.
  std::uint64_t WireBits(Direction direction) const {
    return (wire[direction].bytes + kUdpIpBytes * wire[direction].messages) * 8;
  }
  Traffic operator-(const Traffic &earlier) const;
  Traffic &operator+=(const Traffic &other);
};

// One message type's share over a while.
struct Rate {
  std::size_t kind{};
  Direction direction{};
  double bytesPerSecond{};
  double messagesPerSecond{};
};

// Message types by bytes per second, largest first (`limit` 0: all).
std::vector<Rate> TopRates(const Traffic &delta, double seconds, std::size_t limit = 0);
// kbit/s on the wire one way (headers included).
double WireKbps(const Traffic &delta, Direction direction, double seconds);

// One line: "up 41.2 kbit/s (31 datagrams/s), down 38.0 kbit/s (...); top
// up: PoseFrame 21.3 kbit/s, ...".
std::string Summary(const Traffic &delta, double seconds, std::size_t top = 3);
// A table: one row per message type and direction, then the channels and
// the wire (bytes/s, messages/s, mean size).
std::string Table(const Traffic &delta, double seconds);
} // namespace tandem::net
