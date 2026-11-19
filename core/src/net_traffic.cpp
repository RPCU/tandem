#include "tandem/net_traffic.hpp"

#include <algorithm>
#include <format>

namespace tandem::net {
namespace {
double Kbps(double bytesPerSecond) { return bytesPerSecond * 8.0 / 1000.0; }

const char *ChannelName(std::size_t channel) {
  switch (channel) {
  case 0:
    return "state";
  case 1:
    return "event";
  default:
    return "bulk";
  }
}
} // namespace

void Traffic::Count(std::size_t kind, proto::Channel channel, Direction direction,
                    std::size_t bytes) {
  if (kind >= kMessageKinds)
    return;
  auto &k = kinds[kind][direction];
  k.bytes += bytes;
  ++k.messages;
  auto &c = channels[static_cast<std::size_t>(channel) % proto::kChannelCount][direction];
  c.bytes += bytes;
  ++c.messages;
}

Traffic Traffic::operator-(const Traffic &earlier) const {
  Traffic out = *this;
  const auto minus = [](Totals &a, const Totals &b) {
    a.bytes = a.bytes >= b.bytes ? a.bytes - b.bytes : 0;
    a.messages = a.messages >= b.messages ? a.messages - b.messages : 0;
  };
  for (std::size_t i = 0; i < kMessageKinds; ++i)
    for (std::size_t d = 0; d < 2; ++d)
      minus(out.kinds[i][d], earlier.kinds[i][d]);
  for (std::size_t c = 0; c < proto::kChannelCount; ++c)
    for (std::size_t d = 0; d < 2; ++d)
      minus(out.channels[c][d], earlier.channels[c][d]);
  for (std::size_t d = 0; d < 2; ++d)
    minus(out.wire[d], earlier.wire[d]);
  return out;
}

Traffic &Traffic::operator+=(const Traffic &other) {
  const auto plus = [](Totals &a, const Totals &b) {
    a.bytes += b.bytes;
    a.messages += b.messages;
  };
  for (std::size_t i = 0; i < kMessageKinds; ++i)
    for (std::size_t d = 0; d < 2; ++d)
      plus(kinds[i][d], other.kinds[i][d]);
  for (std::size_t c = 0; c < proto::kChannelCount; ++c)
    for (std::size_t d = 0; d < 2; ++d)
      plus(channels[c][d], other.channels[c][d]);
  for (std::size_t d = 0; d < 2; ++d)
    plus(wire[d], other.wire[d]);
  return *this;
}

std::vector<Rate> TopRates(const Traffic &delta, double seconds, std::size_t limit) {
  std::vector<Rate> rates;
  const double s = seconds > 0 ? seconds : 1.0;
  for (std::size_t i = 0; i < kMessageKinds; ++i)
    for (std::size_t d = 0; d < 2; ++d)
      if (const auto &t = delta.kinds[i][d]; t.messages)
        rates.push_back({i, static_cast<Direction>(d), static_cast<double>(t.bytes) / s,
                         static_cast<double>(t.messages) / s});
  std::ranges::sort(rates, [](const Rate &a, const Rate &b) {
    return a.bytesPerSecond != b.bytesPerSecond ? a.bytesPerSecond > b.bytesPerSecond
                                                : a.kind < b.kind;
  });
  if (limit && rates.size() > limit)
    rates.resize(limit);
  return rates;
}

double WireKbps(const Traffic &delta, Direction direction, double seconds) {
  return seconds > 0 ? static_cast<double>(delta.WireBits(direction)) / 1000.0 / seconds : 0.0;
}

std::string Summary(const Traffic &delta, double seconds, std::size_t top) {
  const double s = seconds > 0 ? seconds : 1.0;
  auto out = std::format(
      "up {:.1f} kbit/s ({:.0f} datagrams/s), down {:.1f} kbit/s ({:.0f}/s)",
      WireKbps(delta, kSent, s), static_cast<double>(delta.wire[kSent].messages) / s,
      WireKbps(delta, kReceived, s), static_cast<double>(delta.wire[kReceived].messages) / s);
  for (const auto direction : {kSent, kReceived}) {
    std::string list;
    std::size_t n = 0;
    for (const auto &rate : TopRates(delta, s)) {
      if (rate.direction != direction)
        continue;
      if (n++ == top)
        break;
      list += std::format("{}{} {:.1f}", list.empty() ? "" : ", ", proto::NameOfKind(rate.kind),
                          Kbps(rate.bytesPerSecond));
    }
    if (!list.empty())
      out += std::format("; {}: {}", direction == kSent ? "up" : "down", list);
  }
  return out;
}

std::string Table(const Traffic &delta, double seconds) {
  const double s = seconds > 0 ? seconds : 1.0;
  std::string out =
      std::format("{:<16} {:<4} {:>9} {:>8} {:>7}\n", "message", "way", "kbit/s", "msg/s", "bytes");
  for (const auto &rate : TopRates(delta, s))
    out += std::format("{:<16} {:<4} {:>9.2f} {:>8.1f} {:>7.0f}\n", proto::NameOfKind(rate.kind),
                       rate.direction == kSent ? "up" : "down", Kbps(rate.bytesPerSecond),
                       rate.messagesPerSecond, rate.bytesPerSecond / rate.messagesPerSecond);
  for (std::size_t c = 0; c < proto::kChannelCount; ++c)
    for (const auto d : {kSent, kReceived})
      if (const auto &t = delta.channels[c][d]; t.messages)
        out += std::format("{:<16} {:<4} {:>9.2f} {:>8.1f}\n",
                           std::string("channel ") + ChannelName(c), d == kSent ? "up" : "down",
                           Kbps(static_cast<double>(t.bytes) / s),
                           static_cast<double>(t.messages) / s);
  for (const auto d : {kSent, kReceived})
    out += std::format("{:<16} {:<4} {:>9.2f} {:>8.1f}\n", "wire (+IP/UDP)",
                       d == kSent ? "up" : "down", WireKbps(delta, d, s),
                       static_cast<double>(delta.wire[d].messages) / s);
  return out;
}
} // namespace tandem::net
