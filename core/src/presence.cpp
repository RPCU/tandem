#include "tandem/presence.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace tandem::presence {
namespace {
using movement::Detailed;
using movement::Placement;

bool IsJump(Detailed detailed) {
  return detailed == Detailed::Jump || detailed == Detailed::DoubleJump ||
         detailed == Detailed::ChargeJump || detailed == Detailed::HoverJump ||
         detailed == Detailed::LadderJump;
}

// A landing by the state machine's own word, else by how fast it fell.
std::uint8_t LandStrength(Detailed detailed, float fall) {
  switch (detailed) {
  case Detailed::RegularLand:
    return 0;
  case Detailed::HardLand:
    return 1;
  case Detailed::VeryHardLand:
  case Detailed::DeathLand:
  case Detailed::SuperheroLand:
  case Detailed::SuperheroLandRecovery:
    return 2;
  default:
    return fall >= Observer::kVeryHardFall ? 2 : fall >= Observer::kHardFall ? 1 : 0;
  }
}
} // namespace

void Observer::Move(const proto::Vec3 &position, std::uint16_t flags, bool crouched, float dt) {
  m_sinceHurt += dt;
  const auto move = movement::Unpack(flags);
  const auto placement = movement::PlacementOf(move);
  if (!m_moved) {
    m_moved = true;
    m_position = position;
    m_placement = placement;
    m_detailed = move.detailed;
    return;
  }
  const float dx = position.x - m_position.x, dy = position.y - m_position.y,
              dz = position.z - m_position.z;
  const float flat = std::sqrt(dx * dx + dy * dy);
  m_position = position;
  const auto before = m_placement;
  const auto was = m_detailed;
  m_placement = placement;
  m_detailed = move.detailed;

  // What the state machine entered: the game's scripts post a sound or an
  // effort for each of these (engine/presence.md).
  if (move.known && move.detailed != was) {
    if (move.detailed == Detailed::Climb)
      Emit(Kind::Climb);
    else if (move.detailed == Detailed::Vault)
      Emit(Kind::Vault);
    else if (move.detailed == Detailed::Dodge || move.detailed == Detailed::DodgeAir)
      Emit(Kind::Dash, move.detailed == Detailed::DodgeAir ? 1 : 0);
    else if (was == Detailed::Slide && placement == Placement::Ground)
      Emit(Kind::SlideEnd);
  }

  // Put somewhere else (a teleport, a fast travel): nothing was walked.
  if (flat > kTeleport || std::abs(dz) > kTeleport || dt <= 0.f) {
    m_walked = 0.f;
    m_fall = 0.f;
    m_speed = 0.f;
    return;
  }

  // Off the ground and back.
  if (placement == Placement::Air) {
    m_fall = std::max(m_fall, -dz / dt);
    // A second jump in the air is a jump too.
    if (IsJump(move.detailed) && (before != Placement::Air || (was != move.detailed)))
      Emit(Kind::Jump, move.detailed == Detailed::Jump ? 0 : 1);
    m_walked = 0.f;
    return;
  }
  if (before == Placement::Air && placement == Placement::Ground) {
    Emit(Kind::Land, LandStrength(move.detailed, m_fall));
    m_fall = 0.f;
    // Both feet are down: the next plant is half a stride away.
    m_walked = 0.f;
    return;
  }
  m_fall = 0.f;

  if (placement == Placement::Ladder) {
    m_walked += std::abs(dz);
    if (m_walked >= kRung) {
      m_walked -= kRung;
      Emit(Kind::Step, static_cast<std::uint8_t>(static_cast<std::uint8_t>(Gait::Walk) |
                                                 kStepLadder | (m_left ? kStepLeft : 0)));
      m_left = !m_left;
    }
    return;
  }
  if (placement != Placement::Ground || move.detailed == Detailed::Slide ||
      move.detailed == Detailed::Dodge) {
    // Swimming, climbing, carried, sliding, dashing: no foot is planted.
    m_walked = 0.f;
    return;
  }

  // On its feet. The speed is smoothed over ≈0.15 s: one late frame of
  // the interpolation must not turn a walk into a sprint.
  const float blend = std::min(1.f, dt / 0.15f);
  m_speed += (flat / dt - m_speed) * blend;
  if (m_speed < kIdleSpeed) {
    // Standing: the first step after it comes half a stride in.
    m_walked = kStride[static_cast<std::size_t>(m_gait)] * 0.5f;
    return;
  }
  m_gait = crouched                 ? Gait::Sneak
           : m_speed < kRunSpeed    ? Gait::Walk
           : m_speed < kSprintSpeed ? Gait::Run
                                    : Gait::Sprint;
  m_walked += flat;
  const float stride = kStride[static_cast<std::size_t>(m_gait)];
  if (m_walked >= stride) {
    // One plant per frame at most: a hitch does not rattle.
    m_walked = std::min(m_walked - stride, stride * 0.5f);
    Emit(Kind::Step,
         static_cast<std::uint8_t>(static_cast<std::uint8_t>(m_gait) | (m_left ? kStepLeft : 0)));
    m_left = !m_left;
  }
}

void Observer::Health(std::uint8_t code) {
  if (m_health && code < *m_health && m_sinceHurt >= kHurtGapSeconds) {
    const int lost = *m_health - code;
    // Lighter than 1 % is a tick of an effect, not a hit worth a grunt.
    if (lost >= 3) {
      Emit(Kind::Hurt, lost >= 77 ? 2 : lost >= 26 ? 1 : 0); // 30 %, 10 %
      m_sinceHurt = 0.f;
    }
  }
  m_health = code;
}

void Observer::Held(std::uint64_t right) {
  if (m_right && *m_right != right) {
    if (right != 0)
      Emit(Kind::Draw); // from nothing, or one weapon for another
    else
      Emit(Kind::Holster);
  }
  m_right = right;
}

void Observer::Life(proto::Life life) {
  if (m_life && *m_life != life) {
    if (*m_life == proto::Life::Up)
      Emit(Kind::Down);
    else if (life == proto::Life::Up)
      Emit(Kind::Up);
  }
  m_life = life;
}

void Observer::Abilities(std::uint8_t bits) {
  if (m_abilities && *m_abilities != bits) {
    if (const auto on = static_cast<std::uint8_t>(bits & ~*m_abilities))
      Emit(Kind::AbilityOn, on);
    if (const auto off = static_cast<std::uint8_t>(*m_abilities & ~bits))
      Emit(Kind::AbilityOff, off);
  }
  m_abilities = bits;
}

std::vector<Event> Observer::Take() { return std::exchange(m_events, {}); }

std::uint8_t HealthCode(float fraction) {
  if (!(fraction > 0.f))
    return 0;
  const auto code = static_cast<int>(std::lround(std::min(fraction, 1.f) * 255.f));
  return static_cast<std::uint8_t>(std::max(code, 1));
}

float HealthFraction(std::uint8_t code) { return static_cast<float>(code) / 255.f; }

bool Gate::ShouldSend(std::uint8_t health, std::uint8_t flags, std::uint64_t nowUs) {
  bool send = !m_sent || flags != m_flags;
  if (!send && health != m_health) {
    const bool lostNow = health + kLossNow <= m_health || health == 0;
    send = lostNow || nowUs - m_atUs >= kGapUs;
  }
  if (send) {
    m_sent = true;
    m_health = health;
    m_flags = flags;
    m_atUs = nowUs;
  }
  return send;
}

const char *NameOf(Status status) {
  constexpr const char *names[] = {"up", "down", "out", "elsewhere", "scene", "menu"};
  return names[static_cast<std::size_t>(status)];
}

Status StatusOf(proto::Life life, proto::Place place, std::uint8_t tier, std::uint8_t storyFlags,
                bool paused, std::uint8_t presenceFlags) {
  if (life == proto::Life::Out)
    return Status::Out;
  if (life == proto::Life::Down)
    return Status::Down;
  if (place != proto::Place::City ||
      (storyFlags & (proto::kStoryBraindance | proto::kStoryReplacer)))
    return Status::Elsewhere;
  if (tier >= 3)
    return Status::Scene;
  if (paused || (presenceFlags & proto::kPresenceMenu))
    return Status::Menu;
  return Status::Up;
}

Bearing BearingOf(const proto::Vec3 &camera, const proto::Vec3 &forward, float halfFov,
                  const proto::Vec3 &target) {
  Bearing out;
  const float dx = target.x - camera.x, dy = target.y - camera.y, dz = target.z - camera.z;
  out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  const float flat = std::sqrt(dx * dx + dy * dy);
  const float forwardFlat = std::sqrt(forward.x * forward.x + forward.y * forward.y);
  if (flat < 1e-4f || forwardFlat < 1e-4f) {
    // Right above or below us, or a camera looking straight up or down.
    out.inView = out.distance < 1e-3f;
    return out;
  }
  // Positive to the right: the world is right-handed, Z up, so a target to
  // the right of the view direction has a negative cross product.
  const float cross = forward.x * dy - forward.y * dx;
  const float dot = forward.x * dx + forward.y * dy;
  out.angle = std::atan2(-cross, dot);
  out.pitch = std::atan2(dz, flat) - std::atan2(forward.z, forwardFlat);
  // The vertical field of view of a 16:9 frame.
  const float halfVertical = std::atan(std::tan(halfFov) * 9.f / 16.f);
  out.inView = std::abs(out.angle) <= halfFov && std::abs(out.pitch) <= halfVertical;
  const float quarter = std::numbers::pi_v<float> / 4.f;
  out.side = std::abs(out.angle) <= quarter         ? 0
             : std::abs(out.angle) >= 3.f * quarter ? 2
             : out.angle > 0.f                      ? 1
                                                    : 3;
  return out;
}

std::uint32_t RoundedMetres(float distance) {
  if (!(distance > 0.f))
    return 0;
  const auto metres = static_cast<std::uint32_t>(std::lround(distance));
  // Beyond a kilometre the HUD writes one decimal of it.
  return metres < 1000 ? metres : (metres + 50) / 100 * 100;
}

float NameplateOpacity(float distance) {
  if (distance <= kNameplateFull)
    return 1.f;
  if (distance >= kNameplateGone)
    return 0.f;
  return (kNameplateGone - distance) / (kNameplateGone - kNameplateFull);
}

void Flinch::Hit(float strength, const proto::Vec3 &direction, const proto::Vec3 &forward,
                 std::uint64_t nowUs) {
  strength = std::clamp(strength, 0.f, 1.f);
  // The least hit still shows.
  const float angle = kMaxAngle * (0.3f + 0.7f * strength);
  float along = -1.f, across = 0.f; // no direction: from the front
  const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
  const float facing = std::sqrt(forward.x * forward.x + forward.y * forward.y);
  if (length > 1e-4f && facing > 1e-4f) {
    const float fx = forward.x / facing, fy = forward.y / facing;
    // Its right, in a right-handed world with Z up.
    const float rx = fy, ry = -fx;
    along = (direction.x * fx + direction.y * fy) / length;
    across = (direction.x * rx + direction.y * ry) / length;
  }
  // A hit travelling the way it faces (from behind) bends it forward.
  m_pitch = angle * along;
  m_roll = angle * across;
  m_atUs = nowUs;
  m_hit = true;
}

Lean Flinch::At(std::uint64_t nowUs) const {
  if (!Active(nowUs))
    return {};
  const float t = static_cast<float>(nowUs - m_atUs) / 1e6f;
  const float length = static_cast<float>(kLengthUs) / 1e6f;
  // Knocked at once, then a damped swing back to rest, ending at zero.
  const float envelope = std::exp(-6.f * t / length) * (1.f - t / length);
  const float swing = std::cos(kFrequency * t);
  return {m_pitch * envelope * swing, m_roll * envelope * swing};
}
} // namespace tandem::presence
