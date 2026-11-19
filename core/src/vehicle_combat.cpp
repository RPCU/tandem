#include "tandem/vehicle_combat.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::vehiclecombat {
std::array<std::uint8_t, proto::kDamageGrid> PackGrid(std::span<const float> cells) {
  std::array<std::uint8_t, proto::kDamageGrid> out{};
  for (std::size_t i = 0; i < out.size() && i < cells.size(); ++i) {
    const float cell = std::isfinite(cells[i]) ? std::clamp(cells[i], 0.f, 1.f) : 0.f;
    out[i] = static_cast<std::uint8_t>(std::lround(cell * 255.f));
  }
  return out;
}

float GridCell(const std::array<std::uint8_t, proto::kDamageGrid> &grid, std::size_t cell) {
  return cell < grid.size() ? static_cast<float>(grid[cell]) / 255.f : 0.f;
}

bool Supersedes(const proto::VehicleDamage &kept, const proto::VehicleDamage &incoming) {
  return static_cast<std::int32_t>(incoming.revision - kept.revision) > 0;
}

Destruction OnArrival(const proto::VehicleDamage &incoming,
                      const std::optional<proto::VehicleDamage> &before, std::uint64_t hostNowUs) {
  if ((incoming.flags & proto::kDamageDestroyed) == 0)
    return Destruction::None;
  if (before && (before->flags & proto::kDamageDestroyed) != 0)
    return Destruction::None; // a wreck already: nothing blows up twice
  const bool fresh =
      incoming.explodedUs != 0 &&
      (hostNowUs <= incoming.explodedUs || hostNowUs - incoming.explodedUs <= kFreshExplosionUs);
  return fresh ? Destruction::Explode : Destruction::Wreck;
}

proto::VehicleDamage Apply(proto::VehicleDamage state, const proto::VehicleHit &hit,
                           float maxHealth, std::uint64_t hostNowUs) {
  if ((state.flags & proto::kDamageDestroyed) != 0)
    return state; // a wreck takes nothing more
  ++state.revision;
  if (maxHealth > 0.f && hit.damage > 0.f && std::isfinite(hit.damage))
    state.health = std::max(0.f, state.health - hit.damage / maxHealth);
  if ((hit.flags & proto::kVehicleHitTyre) != 0 && hit.tyre >= 0 && hit.tyre < 8)
    state.tyres = static_cast<std::uint8_t>(state.tyres | (1u << hit.tyre));
  if ((hit.flags & proto::kVehicleHitWindow) != 0 && hit.window >= 0 && hit.window < 8)
    state.windows = static_cast<std::uint8_t>(state.windows | (1u << hit.window));
  // The game's last damage stage burns, then the vehicle blows up.
  if (state.health <= 0.f) {
    state.flags =
        static_cast<std::uint8_t>((state.flags & ~proto::kDamageBurning) | proto::kDamageDestroyed);
    state.explodedUs = hostNowUs;
    state.instigator = hit.player;
  } else if (state.health < 0.1f) {
    state.flags |= proto::kDamageBurning;
  }
  return state;
}

proto::ExitKind Classify(const ExitSeen &seen) {
  using proto::ExitKind;
  if (seen.destroyed && (seen.forced || seen.collision || seen.death))
    return ExitKind::Exploded;
  if (seen.death)
    return ExitKind::Dead;
  if (seen.collision || seen.forced)
    return seen.bike ? ExitKind::Knocked : ExitKind::Thrown;
  if (seen.speed)
    return ExitKind::Bail;
  return ExitKind::Normal;
}

bool Rolls(proto::ExitKind kind, float speed, bool driver) {
  // Only a vehicle nobody drives any more rolls on: a passenger jumping out
  // leaves the driver at the wheel. A wreck stays where it blew up.
  return driver && kind != proto::ExitKind::Normal && kind != proto::ExitKind::Exploded &&
         std::isfinite(speed) && speed >= kRollSpeed;
}

std::uint8_t RamFlags(float relativeSpeed, bool crowd) {
  if (!std::isfinite(relativeSpeed))
    return 0;
  std::uint8_t flags = 0;
  if (relativeSpeed >= kKnockdownSpeed)
    flags |= proto::kRamKnockdown;
  if (relativeSpeed >= (crowd ? kCrowdKillSpeed : kKillSpeed))
    flags |= proto::kRamLethal;
  return flags;
}

void BumpLedger::Contact(proto::PlayerId other, std::uint64_t atUs) {
  auto &at = m_contactUs[other];
  at = std::max(at, atUs);
}

bool BumpLedger::Felt(const proto::VehicleBump &bump) const {
  const auto it = m_contactUs.find(bump.player);
  if (it == m_contactUs.end())
    return false;
  const auto gap = it->second > bump.atUs ? it->second - bump.atUs : bump.atUs - it->second;
  return gap <= kWindowUs;
}

void BumpLedger::Receive(const proto::VehicleBump &bump, std::uint64_t nowUs) {
  if (nowUs > bump.atUs && nowUs - bump.atUs > kStaleUs)
    return;
  if (m_held.size() < 32)
    m_held.push_back(bump);
}

std::vector<BumpLedger::Push> BumpLedger::Due(std::uint64_t nowUs) {
  std::vector<Push> out;
  std::vector<proto::VehicleBump> keep;
  for (const auto &bump : m_held) {
    if (Felt(bump))
      continue; // our game ran this contact itself
    if (nowUs > bump.atUs && nowUs - bump.atUs > kStaleUs)
      continue; // too old to push anything now
    if (nowUs < bump.atUs + kWindowUs) {
      keep.push_back(bump); // our side of it may still come
      continue;
    }
    auto push = bump.impulse;
    if (!std::isfinite(push.x) || !std::isfinite(push.y) || !std::isfinite(push.z))
      continue;
    const float length = std::sqrt(push.x * push.x + push.y * push.y + push.z * push.z);
    if (length <= 0.f)
      continue;
    if (length > kMaxImpulse) {
      const float scale = kMaxImpulse / length;
      push = {push.x * scale, push.y * scale, push.z * scale};
    }
    // Felt now: a second report of the same contact pushes nothing more.
    Contact(bump.player, bump.atUs);
    out.push_back({.from = bump.player, .impulse = push, .position = bump.position});
  }
  m_held = std::move(keep);
  return out;
}

void BumpLedger::Forget(proto::PlayerId player) {
  m_contactUs.erase(player);
  std::erase_if(m_held, [&](const proto::VehicleBump &bump) { return bump.player == player; });
}

void BumpLedger::Clear() {
  m_contactUs.clear();
  m_held.clear();
}

proto::Gunner Turrets::Change(proto::VehicleRef vehicle, proto::PlayerId gunner,
                              std::uint64_t seat) {
  proto::Gunner state{.vehicle = vehicle, .gunner = gunner, .seat = seat, .revision = ++m_revision};
  if (gunner == proto::kNoPlayer)
    m_seats.erase(vehicle);
  else
    m_seats[vehicle] = state;
  return state;
}

std::vector<proto::Gunner> Turrets::Ask(const proto::GunnerRequest &request) {
  std::vector<proto::Gunner> changed;
  if (request.player == proto::kNoPlayer ||
      request.player == proto::OwnerOfVehicle(request.vehicle))
    return changed;
  const auto it = m_seats.find(request.vehicle);
  if (request.leave) {
    if (it != m_seats.end() && it->second.gunner == request.player)
      changed.push_back(Change(request.vehicle, proto::kNoPlayer, 0));
    return changed;
  }
  if (it != m_seats.end()) {
    if (it->second.gunner != request.player)
      return changed; // taken: the asker hears who has it
    if (it->second.seat == request.seat)
      return changed; // asked twice: it has it already
  }
  // One seat per V: one it held elsewhere is free again.
  std::vector<proto::VehicleRef> held;
  for (const auto &[vehicle, seat] : m_seats)
    if (seat.gunner == request.player && vehicle != request.vehicle)
      held.push_back(vehicle);
  for (const auto vehicle : held)
    changed.push_back(Change(vehicle, proto::kNoPlayer, 0));
  changed.push_back(Change(request.vehicle, request.player, request.seat));
  return changed;
}

std::vector<proto::Gunner> Turrets::Release(proto::PlayerId player) {
  std::vector<proto::VehicleRef> held;
  for (const auto &[vehicle, seat] : m_seats)
    if (seat.gunner == player)
      held.push_back(vehicle);
  std::vector<proto::Gunner> changed;
  for (const auto vehicle : held)
    changed.push_back(Change(vehicle, proto::kNoPlayer, 0));
  return changed;
}

std::optional<proto::Gunner> Turrets::Free(proto::VehicleRef vehicle) {
  if (!m_seats.contains(vehicle))
    return std::nullopt;
  return Change(vehicle, proto::kNoPlayer, 0);
}

void Turrets::Set(const proto::Gunner &gunner) {
  m_revision = std::max(m_revision, gunner.revision);
  if (gunner.gunner == proto::kNoPlayer)
    m_seats.erase(gunner.vehicle);
  else
    m_seats[gunner.vehicle] = gunner;
}
} // namespace tandem::vehiclecombat
