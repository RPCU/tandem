#pragma once

// What a run exercised (docs/architecture/sim.md, "Coverage"): which message
// types crossed the wire each way, and which feature paths the models took
// ("npc.knockout", "v.sandevistan"...), against the list of everything
// there is, so a run says what it never touched.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::sim {
class Coverage {
public:
  // A feature path that exists (so a run that never takes it says so).
  void Declare(std::string_view feature) { m_features.try_emplace(std::string(feature), 0); }
  // ...and one that was taken.
  void Hit(std::string_view feature) { ++m_features[std::string(feature)]; }
  std::uint64_t Hits(std::string_view feature) const;
  // Messages by proto::Message alternative (Referee::SentCounts...), added up.
  void Messages(const std::vector<std::uint64_t> &sent, const std::vector<std::uint64_t> &received);
  void Merge(const Coverage &other);

  std::size_t FeaturesHit() const;
  std::size_t Features() const { return m_features.size(); }
  std::vector<std::string> Missed() const;
  // Message types never sent, never received, and seen neither way.
  std::vector<std::string> NeverSeen() const;

  // The report, one "coverage: ..." line each.
  std::vector<std::string> Report() const;

private:
  std::map<std::string, std::uint64_t, std::less<>> m_features;
  std::vector<std::uint64_t> m_sent, m_received;
};
} // namespace tandem::sim
