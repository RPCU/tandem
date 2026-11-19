#include "tandem/lagcomp.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::lagcomp {
namespace {
float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
} // namespace

void History::Push(std::uint64_t timeUs, const proto::Vec3 &position) {
  if (!m_samples.empty() && timeUs <= m_samples.back().timeUs)
    return; // late or repeated: what we have is newer
  m_samples.push_back({timeUs, position});
  while (m_samples.size() > 2 && timeUs - m_samples.front().timeUs > kKeepUs)
    m_samples.pop_front();
}

std::optional<proto::Vec3> History::At(std::uint64_t timeUs) const {
  if (m_samples.empty())
    return std::nullopt;
  if (timeUs <= m_samples.front().timeUs)
    return m_samples.front().position;
  for (std::size_t i = 1; i < m_samples.size(); ++i) {
    const auto &b = m_samples[i];
    if (timeUs > b.timeUs)
      continue;
    const auto &a = m_samples[i - 1];
    const float t = static_cast<float>(timeUs - a.timeUs) / static_cast<float>(b.timeUs - a.timeUs);
    return proto::Vec3{a.position.x + (b.position.x - a.position.x) * t,
                       a.position.y + (b.position.y - a.position.y) * t,
                       a.position.z + (b.position.z - a.position.z) * t};
  }
  return m_samples.back().position;
}

std::uint64_t SeenAtUs(std::uint64_t arrivalUs, std::uint32_t rttMs, std::uint64_t viewDelayUs) {
  const auto back =
      std::min<std::uint64_t>(static_cast<std::uint64_t>(rttMs) * 1000 + viewDelayUs, kMaxRewindUs);
  return arrivalUs > back ? arrivalUs - back : 0;
}

std::uint64_t NpcViewDelayUs(std::uint32_t rttMs) {
  // Its NPC tracks hold transit (half the round trip, already in SeenAtUs)
  // plus an interval of 100 ms and jitter: the margin covers the jitter.
  (void)rttMs;
  return 100'000 + 15'000;
}

Reach Check(const History *actor, const History *target, const proto::Vec3 &actorNow,
            const proto::Vec3 &targetNow, std::uint64_t arrivalUs, std::uint32_t rttMs,
            float limit) {
  Reach reach;
  reach.now = Distance(actorNow, targetNow);
  reach.seen = reach.now;
  // The guest's V when it asked (its own state, half a round trip ago); our
  // NPC when it saw it.
  const auto asked = SeenAtUs(arrivalUs, rttMs / 2, 0);
  const auto seen = SeenAtUs(arrivalUs, rttMs, NpcViewDelayUs(rttMs));
  const auto actorThen = actor ? actor->At(asked) : std::nullopt;
  const auto targetThen = target ? target->At(seen) : std::nullopt;
  if (actorThen || targetThen)
    reach.seen = Distance(actorThen.value_or(actorNow), targetThen.value_or(targetNow));
  reach.ok = std::min(reach.now, reach.seen) <= limit + kToleranceM;
  return reach;
}

void Book::Push(std::uint32_t id, std::uint64_t timeUs, const proto::Vec3 &position) {
  m_histories[id].Push(timeUs, position);
}

const History *Book::Of(std::uint32_t id) const {
  const auto it = m_histories.find(id);
  return it == m_histories.end() ? nullptr : &it->second;
}
} // namespace tandem::lagcomp
