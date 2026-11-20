#include "tandem/combat.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::combat {
float StrikeLoss(const proto::NpcStrike &strike, float health, float maxHealth, bool immortal) {
  if (health <= 0.f)
    return 0.f;
  if (strike.flags & proto::kStrikeInstantKill)
    return health;
  float loss = std::max(strike.damage, 0.f);
  if (strike.flags & proto::kStrikeLeaveOneHp)
    loss = std::max(health - 1.f, 0.f);
  float floor = std::max(maxHealth, 0.f) * std::clamp(strike.minHealth, 0.f, 100.f) / 100.f;
  if (immortal)
    floor = std::max(floor, std::max(maxHealth, 0.f) * 0.01f);
  if (floor > 0.f && health - loss < floor)
    loss = std::max(health - floor, 0.f);
  return std::min(loss, health);
}

proto::NpcStrike FromHit(const proto::NpcHit &hit) {
  std::uint16_t flags = 0;
  if (hit.flags & proto::kHitWeakSpot)
    flags |= proto::kStrikeWeakSpot;
  if (hit.flags & proto::kHitCritical)
    flags |= proto::kStrikeCritical;
  if (hit.flags & proto::kHitMelee)
    flags |= proto::kStrikeMelee;
  if (hit.flags & proto::kHitLethal)
    flags |= proto::kStrikeInstantKill;
  return {.player = hit.player,
          .npc = hit.npc,
          .damage = hit.damage,
          .flags = flags,
          .attackType = static_cast<std::uint8_t>((hit.flags & proto::kHitMelee) ? 7 : 10),
          .position = hit.position,
          .direction = hit.direction};
}

std::uint64_t MeleeHoldUs(std::uint64_t proxyDelayUs, std::uint64_t transitUs) {
  if (proxyDelayUs <= transitUs)
    return 0;
  return std::min(proxyDelayUs - transitUs, kMaxMeleeHoldUs);
}

PathFix FollowPath(const proto::Vec3 &copy, const proto::ThrowPath &real) {
  const float dx = copy.x - real.position.x, dy = copy.y - real.position.y,
              dz = copy.z - real.position.z;
  const float off = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float speed =
      std::sqrt(real.velocity.x * real.velocity.x + real.velocity.y * real.velocity.y +
                real.velocity.z * real.velocity.z);
  if (!std::isfinite(off) || !std::isfinite(speed))
    return PathFix::None;
  if (speed < kThrowRestSpeed)
    return off > kThrowRestSlack ? PathFix::Place : PathFix::None;
  return off > kThrowPathSlack ? PathFix::Relaunch : PathFix::None;
}
} // namespace tandem::combat
