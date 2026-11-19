#include "tandem/sim/coverage.hpp"

#include "tandem/sim/referee.hpp"

#include <algorithm>
#include <format>

namespace tandem::sim {
namespace {
std::string Joined(const std::vector<std::string> &names) {
  std::string out;
  for (const auto &name : names)
    out += (out.empty() ? "" : " ") + name;
  return out.empty() ? "-" : out;
}

void AddUp(std::vector<std::uint64_t> &into, const std::vector<std::uint64_t> &from) {
  if (into.size() < from.size())
    into.resize(from.size());
  for (std::size_t i = 0; i < from.size(); ++i)
    into[i] += from[i];
}
} // namespace

std::uint64_t Coverage::Hits(std::string_view feature) const {
  const auto it = m_features.find(feature);
  return it == m_features.end() ? 0 : it->second;
}

void Coverage::Messages(const std::vector<std::uint64_t> &sent,
                        const std::vector<std::uint64_t> &received) {
  AddUp(m_sent, sent);
  AddUp(m_received, received);
}

void Coverage::Merge(const Coverage &other) {
  for (const auto &[feature, hits] : other.m_features)
    m_features[feature] += hits;
  Messages(other.m_sent, other.m_received);
}

std::size_t Coverage::FeaturesHit() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(m_features, [](const auto &entry) { return entry.second > 0; }));
}

std::vector<std::string> Coverage::Missed() const {
  std::vector<std::string> out;
  for (const auto &[feature, hits] : m_features)
    if (hits == 0)
      out.push_back(feature);
  return out;
}

std::vector<std::string> Coverage::NeverSeen() const {
  std::vector<std::string> out;
  const auto types = std::variant_size_v<proto::Message>;
  for (std::size_t i = 0; i < types; ++i)
    if ((i >= m_sent.size() || m_sent[i] == 0) && (i >= m_received.size() || m_received[i] == 0))
      out.emplace_back(MessageName(i));
  return out;
}

std::vector<std::string> Coverage::Report() const {
  const auto types = std::variant_size_v<proto::Message>;
  std::vector<std::string> sent, received, neverSent, neverReceived;
  std::uint64_t sentTotal = 0, receivedTotal = 0;
  for (std::size_t i = 0; i < types; ++i) {
    const auto out = i < m_sent.size() ? m_sent[i] : 0;
    const auto in = i < m_received.size() ? m_received[i] : 0;
    sentTotal += out;
    receivedTotal += in;
    if (out)
      sent.push_back(std::format("{}={}", MessageName(i), out));
    if (in)
      received.push_back(std::format("{}={}", MessageName(i), in));
  }
  const auto never = NeverSeen();
  std::vector<std::string> lines;
  lines.push_back(std::format("coverage: {} of {} message types sent ({} messages): {}",
                              sent.size(), types, sentTotal, Joined(sent)));
  lines.push_back(std::format("coverage: {} of {} message types received ({} messages): {}",
                              received.size(), types, receivedTotal, Joined(received)));
  lines.push_back(std::format("coverage: {} message types never crossed either way: {}",
                              never.size(), Joined(never)));
  std::vector<std::string> taken;
  for (const auto &[feature, hits] : m_features)
    if (hits)
      taken.push_back(std::format("{}={}", feature, hits));
  const auto missed = Missed();
  lines.push_back(std::format("coverage: {} of {} feature paths taken: {}", taken.size(),
                              m_features.size(), Joined(taken)));
  lines.push_back(
      std::format("coverage: {} feature paths never taken: {}", missed.size(), Joined(missed)));
  return lines;
}
} // namespace tandem::sim
