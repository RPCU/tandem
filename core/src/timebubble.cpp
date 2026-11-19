#include "tandem/timebubble.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::sync {
namespace {
bool Near(const proto::Vec3 &a, const proto::Vec3 &b, float reach = TimeBubbles::kRadius) {
  return std::hypot(a.x - b.x, a.y - b.y) <= reach;
}
} // namespace

std::vector<TimeBubbles::Change> TimeBubbles::Update(std::span<const Owner> owners,
                                                     std::span<const Npc> npcs) {
  // Bubbles whose V's ability ended, or whose scale is no slow-down.
  std::erase_if(m_bubbles, [&](const auto &entry) {
    return std::ranges::none_of(owners, [&](const Owner &owner) {
      return owner.player == entry.first && owner.scale < 1.f;
    });
  });
  std::set<proto::NpcId> listed;
  for (const auto &npc : npcs)
    listed.insert(npc.npc);
  for (const auto &owner : owners) {
    if (!(owner.scale < 1.f))
      continue;
    auto &bubble = m_bubbles[owner.player];
    bubble.scale = std::clamp(owner.scale, 0.05f, 1.f);
    std::erase_if(bubble.members, [&](proto::NpcId npc) { return !listed.contains(npc); });
    for (const auto &npc : npcs)
      if (Near(npc.position, owner.position))
        bubble.members.insert(npc.npc);
  }

  // Each NPC's speed: the strongest bubble it is in.
  std::map<proto::NpcId, float> next;
  for (const auto &[player, bubble] : m_bubbles)
    for (const auto npc : bubble.members) {
      const auto [it, added] = next.try_emplace(npc, bubble.scale);
      if (!added)
        it->second = std::min(it->second, bubble.scale);
    }

  std::vector<Change> changes;
  for (const auto &[npc, factor] : m_factors)
    if (!next.contains(npc))
      changes.push_back({npc, 1.f});
  for (const auto &[npc, factor] : next)
    if (const auto it = m_factors.find(npc); it == m_factors.end() || it->second != factor)
      changes.push_back({npc, factor});
  m_factors = std::move(next);
  return changes;
}

std::vector<TimeBubbles::Change> TimeBubbles::Clear() {
  std::vector<Change> changes;
  for (const auto &[npc, factor] : m_factors)
    changes.push_back({npc, 1.f});
  m_factors.clear();
  m_bubbles.clear();
  return changes;
}

TimeBubbles::CatchUp TimeBubbles::ShotCatchUp(std::uint64_t firedUs, std::uint64_t nowUs) {
  // Over this window the projectile covers its own normal way plus the lag's.
  constexpr float kWindow = 0.25f;
  if (nowUs <= firedUs)
    return {};
  const float lag = static_cast<float>(nowUs - firedUs) / 1e6f;
  if (lag < 0.005f || lag > 1.f)
    return {};
  return {.boost = 1.f + lag / kWindow, .seconds = kWindow};
}

float TimeBubbles::PlayerFactor(proto::PlayerId player, float own, const proto::Vec3 &position,
                                std::span<const Owner> owners, bool inside) {
  const float reach = kRadius + (inside ? kMargin : 0.f);
  float strongest = 1.f;
  for (const auto &owner : owners)
    if (owner.player != player && owner.scale < 1.f && Near(owner.position, position, reach))
      strongest = std::min(strongest, std::clamp(owner.scale, 0.05f, 1.f));
  own = std::isfinite(own) ? std::clamp(own, 0.05f, 1.f) : 1.f;
  return std::min(1.f, strongest / own);
}

proto::PlayerId TimeBubbles::SlowedBy(proto::PlayerId player, float own,
                                      const proto::Vec3 &position, std::span<const Owner> owners,
                                      bool inside) {
  if (!(PlayerFactor(player, own, position, owners, inside) < 1.f))
    return proto::kNoPlayer;
  const float reach = kRadius + (inside ? kMargin : 0.f);
  proto::PlayerId by = proto::kNoPlayer;
  float strongest = 1.f, nearest = 0.f;
  for (const auto &owner : owners) {
    if (owner.player == player || !(owner.scale < 1.f) || !Near(owner.position, position, reach))
      continue;
    const float scale = std::clamp(owner.scale, 0.05f, 1.f);
    const float away = std::hypot(owner.position.x - position.x, owner.position.y - position.y);
    if (by == proto::kNoPlayer || scale < strongest || (scale == strongest && away < nearest)) {
      by = owner.player;
      strongest = scale;
      nearest = away;
    }
  }
  return by;
}

float TimeBubbles::BubbleScale(std::uint8_t abilities, float scale) {
  if ((abilities & proto::kTimeBubbleAbilities) == 0 || !std::isfinite(scale) || !(scale < 1.f))
    return 1.f;
  return std::clamp(scale, 0.05f, 1.f);
}

float TimeBubbles::FactorOf(proto::NpcId npc) const {
  const auto it = m_factors.find(npc);
  return it == m_factors.end() ? 1.f : it->second;
}

std::size_t TimeBubbles::MembersOf(proto::PlayerId owner) const {
  const auto it = m_bubbles.find(owner);
  return it == m_bubbles.end() ? 0 : it->second.members.size();
}
} // namespace tandem::sync
