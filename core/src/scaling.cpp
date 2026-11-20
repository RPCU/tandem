#include "tandem/scaling.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::scaling {
Rule RuleOf(const proto::Rules &rules) {
  return {.on = (rules.flags & proto::kRuleEnemyScaling) != 0,
          .health = static_cast<float>(rules.enemyHealth) / 100.f};
}

proto::Rules WithRule(proto::Rules rules, bool on, float health) {
  if (on)
    rules.flags |= proto::kRuleEnemyScaling;
  else
    rules.flags &= static_cast<std::uint8_t>(~proto::kRuleEnemyScaling);
  const float clamped = std::isfinite(health) ? std::clamp(health, 0.f, kMaxHealthPerPlayer) : 0.f;
  rules.enemyHealth = static_cast<std::uint8_t>(std::lround(clamped * 100.f));
  return rules;
}

Multipliers For(std::uint8_t players, const Rule &rule) {
  if (!rule.on)
    return {};
  const auto extra = static_cast<float>(std::clamp<std::uint8_t>(players, 1, kMaxPlayers) - 1);
  if (extra <= 0.f)
    return {};
  const float health = std::clamp(rule.health, 0.f, kMaxHealthPerPlayer);
  return {.health = 1.f + health * extra,
          .poise = 1.f + kPoisePerPlayer * extra,
          .armor = std::min(1.f + kArmorPerPlayer * extra, kArmorCap)};
}

float KeepFraction(float value, float oldMax, float newMax) {
  if (newMax <= 0.f)
    return 0.f;
  if (oldMax <= 0.f)
    return newMax;
  return std::clamp(value / oldMax, 0.f, 1.f) * newMax;
}

float ThresholdFactor(float poise, float baseHealth, float componentHealth) {
  if (baseHealth <= 0.f || componentHealth <= 0.f)
    return poise;
  return std::clamp(poise * baseHealth / componentHealth, 0.1f, 10.f);
}

bool Counts(bool was, const Seen &seen, double heldUntil, double now) {
  if (seen.engaged)
    return true;
  const bool placed = seen.distance >= 0.f;
  if (placed && seen.distance <= kNearIn)
    return true;
  return was && ((placed && seen.distance <= kNearOut) || now < heldUntil);
}

std::uint8_t Counter::Count(proto::NpcId npc, std::span<const Seen> players, double now) {
  auto &state = m_npcs[npc];
  std::map<proto::PlayerId, double> counted;
  for (const auto &seen : players) {
    if (seen.player == proto::kNoPlayer || counted.contains(seen.player))
      continue;
    const auto was = state.counted.find(seen.player);
    const double held = was == state.counted.end() ? 0.0 : was->second;
    if (!Counts(was != state.counted.end(), seen, held, now))
      continue;
    counted[seen.player] = seen.engaged ? now + kEngagedHold : held;
  }
  state.counted = std::move(counted);
  state.count =
      static_cast<std::uint8_t>(std::clamp<std::size_t>(state.counted.size(), 1, kMaxPlayers));
  return state.count;
}

std::uint8_t Counter::CountOf(proto::NpcId npc) const {
  const auto it = m_npcs.find(npc);
  return it == m_npcs.end() ? 1 : it->second.count;
}

void Counter::Forget(proto::NpcId npc) { m_npcs.erase(npc); }
} // namespace tandem::scaling
