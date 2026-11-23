// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "../log.hpp"
#include "../observer.hpp"
#include "../perf.hpp"
#include "../runtime.hpp"
#include "controller.hpp"

#include "tandem/interpolation.hpp"

#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>

#include <cmath>
#include <numbers>

namespace tandem::plugin::engine {
namespace {
using Allocator = RED4ext::Memory::AI_MovementAllocator;

// Gait bands in m/s, with hysteresis so a body near a threshold doesn't
// flicker between two gaits. V walks ~1.6 m/s, runs ~3.5, sprints ~6.
constexpr float kWalkOn = 0.6f, kWalkOff = 0.3f;
constexpr float kJogOn = 2.4f, kJogOff = 2.0f;
constexpr float kSprintOn = 5.4f, kSprintOff = 4.8f;
// gamePSMLocomotionStates values that mean "sprinting", whatever the speed says.
constexpr std::uint8_t kPsmSprint = 2, kPsmCrouchSprint = 11;

float Wrap(float a) {
  constexpr float pi = std::numbers::pi_v<float>;
  while (a > pi)
    a -= 2 * pi;
  while (a < -pi)
    a += 2 * pi;
  return a;
}

proto::Quat ToProto(const RED4ext::Quaternion &q) { return {q.i, q.j, q.k, q.r}; }
} // namespace

PuppetController::PuppetController(proto::PlayerId aPlayer) : player(aPlayer) {}

void *PuppetController::operator new(std::size_t size) {
  return Allocator::Get()
      ->AllocAligned(static_cast<std::uint32_t>(size), alignof(PuppetController))
      .memory;
}

void PuppetController::operator delete(void *pointer) { Allocator::Get()->Free(pointer); }

RED4ext::Memory::PoolAI_Movement *PuppetController::GetMemoryPool() {
  return RED4ext::Memory::PoolAI_Movement::Get();
}

PuppetController::~PuppetController() = default;

void PuppetController::PreTick(float) {}

void PuppetController::Tick(float delta) {
  // On a ladder, in the water, climbing: no walk cycle under the streamed pose.
  ChooseGait(movement::Walks(m_placement) ? m_speed : 0.f, m_stance);
  if (m_gait.clip > 0.f)
    m_gait.time = std::fmod(m_gait.time + delta, m_gait.clip);
  else
    m_gait.time = 0.f;
}

void PuppetController::GetDeltaTransform(RED4ext::Vector4 &position,
                                         RED4ext::Quaternion &rotation) {
  position = {0.f, 0.f, 0.f, 0.f};
  rotation = {0.f, 0.f, 0.f, 1.f};

  if (canary != kCanary) {
    if (!std::exchange(m_loggedCanary, true))
      log::Error("puppet controller memory overwritten by the engine (canary {:x}): stopped",
                 canary);
    return;
  }

  // Mounting reparents the entity: the placement chain can be gone this frame.
  auto *owner = component ? component->owner : nullptr;
  auto *placed = owner ? owner->transformComponent : nullptr;
  if (!placed) {
    if (!std::exchange(m_loggedNoPlacement, true))
      log::Warn("puppet {}: no placement this frame (mounted?)", player);
    return;
  }

  const Perf::Scope timed(Work::Controller);
  // In a vehicle the mount places the body (seat of a car copy that follows
  // the driver): the controller keeps still. On an NCART train (seated in a
  // train of our world, or hidden and placed by the scripts), or riding
  // along in a full scene vehicle (hidden): scripts/tandem/Transit.reds.
  // Never walked along the line or the road.
  const auto placedBy = Runtime::Get().PlacementOf(player);
  if (placedBy.seated || placedBy.riding)
    return;
  // Placed by something else for now: a grapple's workspot, a carrier's
  // shoulder (Takedowns.reds), an NPC proxy's bench or wall (NpcAnims.reds).
  // The gait idles meanwhile.
  // Placement starts afresh once it is let go (no stale timers or landing).
  if (placedBy.held) {
    m_speed = 0.f;
    m_placer = movement::Placer{};
    m_placement = movement::Placement::Ground;
    m_lastPlaceUs = 0;
    return;
  }

  const auto view = Runtime::Get().ViewOf(player);
  if (!view.valid)
    return;

  const auto current = placed->worldTransform.Position.AsVector3();
  const float currentYaw = sync::YawOf(ToProto(placed->worldTransform.Orientation));

  // On the ground as before; in the air, on a ladder, in the water exactly
  // where the stream says, easing back onto the ground on landing; a body
  // left behind (a ledge, a refused move) is teleported by the game thread.
  const auto now = Runtime::Get().HostNowUs();
  const float dt =
      m_lastPlaceUs && now > m_lastPlaceUs ? static_cast<float>(now - m_lastPlaceUs) / 1e6f : 0.f;
  m_lastPlaceUs = now;
  const auto placement = movement::PlacementOf(view.flags);
  const auto step =
      m_placer.Update({current.X, current.Y, current.Z}, view.position, placement, dt);
  position = {step.delta.x, step.delta.y, step.delta.z, 0.f};
  const auto turn = sync::FromYaw(Wrap(view.yaw - currentYaw));
  rotation = {turn.i, turn.j, turn.k, turn.r};

  m_speed = view.speed;
  m_stance = view.stance;
  m_placement = placement;
  if (const auto bit = 1u << static_cast<unsigned>(placement);
      step.changed && !(m_placementsSeen & bit)) {
    m_placementsSeen |= bit;
    log::Debug("puppet {}: first {} placement{}", player, movement::NameOf(placement),
               movement::FollowsFreely(placement) ? " (follows the stream freely)" : "");
  }
  // (NPC proxies send no movement: they stay on the ground and their
  // scripts place them.)
  if (step.snap && player < kNpcPuppetBase) {
    Runtime::Get().RequestSnap(player);
    if (m_placer.Snaps() <= 10 || m_placer.Snaps() % 10 == 0)
      log::Debug("puppet {}: body {:.2f} m from target ({}, {:+.2f} m up): teleport #{}", player,
                 std::hypot(view.position.x - current.X, view.position.y - current.Y,
                            view.position.z - current.Z),
                 movement::NameOf(placement), view.position.z - current.Z, m_placer.Snaps());
  }

  if (!std::exchange(m_loggedDelta, true))
    log::Debug("puppet {}: moving, body ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, {:.1f}, {:.1f})",
               player, current.X, current.Y, current.Z, view.position.x, view.position.y,
               view.position.z);

  // The observer (observer.hpp): a body that stays far from where its
  // owner says for a second (a snap brings a stray body back sooner).
  if (Observer::On() && player < kNpcPuppetBase) {
    const float gap = std::hypot(view.position.x - current.X, view.position.y - current.Y,
                                 view.position.z - current.Z);
    if (gap < Observer::Get().DriftMetres()) {
      m_driftSinceUs = 0;
      m_driftTold = false;
    } else if (!m_driftSinceUs) {
      m_driftSinceUs = now;
    } else if (!m_driftTold && now - m_driftSinceUs >= 1'000'000) {
      m_driftTold = true;
      Observer::Get().Drift(player, gap, {current.X, current.Y, current.Z}, view.position);
    }
  }

  // Health line every ~5 s: is the body where the network says?
  ++m_frames;
  if (now - m_lastHealthUs >= 5'000'000) {
    const float gap = std::hypot(view.position.x - current.X, view.position.y - current.Y,
                                 view.position.z - current.Z);
    log::Debug("puppet {}: {} frames, body ({:.1f}, {:.1f}, {:.1f}) {:.2f} m from target, "
               "speed {:.1f} m/s, gait {}, {}, {} teleports{}",
               player, m_frames, current.X, current.Y, current.Z, gap, view.speed,
               static_cast<int>(m_gait.style), movement::NameOf(placement), m_placer.Snaps(),
               view.extrapolated ? ", extrapolating" : "");
    m_lastHealthUs = now;
    m_frames = 0;
  }
}

void PuppetController::Unknown05(bool &out) { out = false; }
void PuppetController::Unknown06(RED4ext::Vector4 &, RED4ext::Quaternion &) {}
void PuppetController::Unknown07(RED4ext::Vector4 &, RED4ext::Quaternion &, bool &, bool &) {}

void PuppetController::LoadClips() {
  m_walkClip = m_driver.ClipLength("walk_0");
  m_jogClip = m_driver.ClipLength("jog_0");
  m_sprintClip = m_driver.ClipLength("sprint_0");
  m_gait = {};
}

void PuppetController::SendAnimationParameters(float, RED4ext::Vector4 &, RED4ext::Quaternion &) {
  if (m_sendsUntilClips && --m_sendsUntilClips == 0) {
    LoadClips();
    log::Debug("puppet {}: clips walk {:.2f} s, jog {:.2f} s, sprint {:.2f} s", player, m_walkClip,
               m_jogClip, m_sprintClip);
  }
  AnimationData data;
  data.controller = GetName();
  GetAnimationParameters(data);
  m_driver.Send(data);
  if (!std::exchange(m_loggedSend, true)) {
    log::Debug("puppet {}: animating (clips walk {:.2f} s, jog {:.2f} s, sprint {:.2f} s)", player,
               m_walkClip, m_jogClip, m_sprintClip);
    // Which of the reserved bytes the engine wrote: its real footprint.
    std::string written;
    for (std::size_t k = 0; k < sizeof(engineScratch); ++k)
      if (engineScratch[k])
        written += std::format(" +0x{:X}={:02X}", 0x20 + k, engineScratch[k]);
    log::Debug("puppet {}: engine wrote{}", player, written.empty() ? " nothing" : written);
  }
}

void PuppetController::Unknown09(RED4ext::Vector4 &, RED4ext::Quaternion &, RED4ext::Vector4 &,
                                 RED4ext::Quaternion &) {}
void PuppetController::Unknown10(RED4ext::Vector4 &, RED4ext::Quaternion &, float,
                                 RED4ext::Vector4 &, RED4ext::Quaternion &) {}

RED4ext::CName PuppetController::GetName() const { return RED4ext::CName("tandem"); }

bool PuppetController::Unknown12() { return true; }
void PuppetController::Unknown13() {}
void PuppetController::Unknown14() {}
void PuppetController::Mount(RED4ext::move::Component &,
                             const RED4ext::Handle<RED4ext::ent::Entity> &) {}

void PuppetController::Attach(RED4ext::move::Component &aComponent) {
  if (!aComponent.owner || !aComponent.owner->transformComponent) {
    log::Warn("puppet {}: attach without an entity, skipped", player);
    return;
  }
  component = &aComponent;
  m_driver.Attach(aComponent.owner);
  m_walkClip = m_jogClip = m_sprintClip = 0.f;
  // Our puppets wear V's rig, loaded with V; an NPC's may still be streaming.
  m_sendsUntilClips = player >= kNpcPuppetBase ? 60 : 0;
  if (!m_sendsUntilClips)
    LoadClips();
  m_gait = {};
  m_placer = movement::Placer{};
  m_placement = movement::Placement::Ground;
  m_lastPlaceUs = 0;
  if (player >= kNpcPuppetBase)
    log::Debug("puppet {}: controller attached", player);
  else
    log::Info("puppet {}: controller attached", player);
}

void PuppetController::Detach(RED4ext::move::Component &) {
  m_driver.Detach();
  component = nullptr;
  if (player >= kNpcPuppetBase)
    log::Debug("puppet {}: controller detached", player);
  else
    log::Info("puppet {}: controller detached", player);
}

void PuppetController::GetAnimationParameters(AnimationData &data) {
  // The stance and state wrappers scripts set (inCrouch, combatLocomotion…)
  // only reach the locomotion when it says so (AnimFeature_Locomotion field).
  data.wrappersUnlocked = true;
  if (m_gait.style == LocomotionStyle::Idle || m_gait.clip <= 0.f) {
    // No clip for this gait on this rig: idle pose rather than a stalled graph.
    data.action = MotionAction::None;
    data.style = LocomotionStyle::Idle;
    return;
  }
  data.action = MotionAction::Move;
  data.style = m_gait.style;
  data.time = m_gait.time;
  data.speed = m_speed;
}

void PuppetController::ChooseGait(float speed, std::uint8_t stance) {
  const bool sprintStance = stance == kPsmSprint || stance == kPsmCrouchSprint;
  auto style = m_gait.style;

  switch (style) {
  case LocomotionStyle::Sprint:
    if (speed < kSprintOff && !sprintStance)
      style = speed >= kJogOff ? LocomotionStyle::Jog : LocomotionStyle::Walk;
    if (speed < kWalkOff)
      style = LocomotionStyle::Idle;
    break;
  case LocomotionStyle::Jog:
    if (speed >= kSprintOn || (sprintStance && speed >= kJogOff))
      style = LocomotionStyle::Sprint;
    else if (speed < kJogOff)
      style = speed >= kWalkOff ? LocomotionStyle::Walk : LocomotionStyle::Idle;
    break;
  case LocomotionStyle::Walk:
    if (speed >= kJogOn)
      style = sprintStance || speed >= kSprintOn ? LocomotionStyle::Sprint : LocomotionStyle::Jog;
    else if (speed < kWalkOff)
      style = LocomotionStyle::Idle;
    break;
  default:
    if (speed >= kSprintOn || (sprintStance && speed >= kJogOn))
      style = LocomotionStyle::Sprint;
    else if (speed >= kJogOn)
      style = LocomotionStyle::Jog;
    else if (speed >= kWalkOn)
      style = LocomotionStyle::Walk;
    break;
  }

  if (style == m_gait.style)
    return;

  // A rig without the clip borrows the nearest one it has.
  float clip = 0.f;
  switch (style) {
  case LocomotionStyle::Walk:
    clip = m_walkClip > 0.f ? m_walkClip : m_jogClip;
    if (m_walkClip <= 0.f && m_jogClip > 0.f)
      style = LocomotionStyle::Jog;
    break;
  case LocomotionStyle::Jog:
    clip = m_jogClip > 0.f ? m_jogClip : m_sprintClip;
    if (m_jogClip <= 0.f && m_sprintClip > 0.f)
      style = LocomotionStyle::Sprint;
    break;
  case LocomotionStyle::Sprint:
    clip = m_sprintClip > 0.f ? m_sprintClip : m_jogClip;
    if (m_sprintClip <= 0.f && m_jogClip > 0.f)
      style = LocomotionStyle::Jog;
    break;
  default:
    break;
  }
  m_gait = {style, clip, 0.f};
}
} // namespace tandem::plugin::engine
