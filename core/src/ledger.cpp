#include "tandem/ledger.hpp"

#include "tandem/consistency.hpp"
#include "tandem/regions.hpp"

#include <algorithm>

namespace tandem::ledger {
std::optional<proto::NpcDeath> Deaths::Record(proto::NpcDeath death, proto::PlayerId by,
                                              std::uint64_t hostUs) {
  if (death.entity == 0 || m_deaths.contains(death.entity))
    return std::nullopt;
  death.npc = 0;
  death.by = by;
  death.order = m_next++;
  death.timeUs = hostUs;
  m_deaths[death.entity] = death;
  Trim();
  return death;
}

bool Deaths::Note(const proto::NpcDeath &death) {
  if (death.entity == 0 || death.order == 0 || death.order <= m_floor ||
      m_deaths.contains(death.entity))
    return false;
  m_deaths[death.entity] = death;
  m_next = std::max(m_next, death.order + 1);
  Trim();
  return m_deaths.contains(death.entity);
}

const proto::NpcDeath *Deaths::Find(std::uint64_t entity) const {
  const auto it = m_deaths.find(entity);
  return it == m_deaths.end() ? nullptr : &it->second;
}

std::vector<proto::NpcDeath> Deaths::InOrder() const {
  std::vector<proto::NpcDeath> out;
  out.reserve(m_deaths.size());
  for (const auto &[entity, death] : m_deaths)
    out.push_back(death);
  std::ranges::sort(out, {}, &proto::NpcDeath::order);
  return out;
}

std::uint32_t Deaths::Digest() const {
  consistency::Hasher hash;
  for (const auto &[entity, death] : m_deaths)
    hash.Add(entity).Add(std::uint64_t{death.order}).Add(std::uint64_t{death.flags});
  return hash.Value();
}

void Deaths::Clear() {
  m_deaths.clear();
  m_next = 1;
  m_floor = 0;
}

void Deaths::Trim() {
  while (m_deaths.size() > proto::kMaxDeaths) {
    const auto oldest = std::ranges::min_element(
        m_deaths, {}, [](const auto &entry) { return entry.second.order; });
    m_floor = std::max(m_floor, oldest->second.order);
    m_deaths.erase(oldest);
  }
}

bool ShowsBody(const proto::NpcDeath &death, proto::PlayerId you, std::uint64_t hostUs) {
  if (death.by == you || death.order == 0)
    return false;
  // (A death stamped after our clock's reading of the host's is recent.)
  return hostUs <= death.timeUs || hostUs - death.timeUs < kBodyForUs;
}

std::vector<proto::NpcDeath> Bodies(const std::vector<proto::NpcDeath> &deaths, proto::PlayerId you,
                                    std::uint64_t hostUs, const proto::Vec3 &at,
                                    const std::vector<std::uint64_t> &shown) {
  std::vector<std::pair<float, proto::NpcDeath>> close;
  for (const auto &death : deaths) {
    if (!ShowsBody(death, you, hostUs))
      continue;
    const bool has = std::ranges::find(shown, death.entity) != shown.end();
    const float distance = regions::Distance(at, death.position);
    if (distance < (has ? kBodyOut : kBodyIn))
      close.emplace_back(distance, death);
  }
  std::ranges::sort(close, [](const auto &a, const auto &b) {
    return a.first != b.first ? a.first < b.first : a.second.order < b.second.order;
  });
  if (close.size() > kMaxBodies)
    close.resize(kMaxBodies);
  std::vector<proto::NpcDeath> out;
  for (auto &entry : close)
    out.push_back(std::move(entry.second));
  return out;
}
} // namespace tandem::ledger
