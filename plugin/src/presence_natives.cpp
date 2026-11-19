// What scripts/tandem/Presence.reds, PartyHud.reds and Reactions use:
// `TandemPresence` static functions and its structs. Declared on the script
// side in scripts/tandem/PresenceNatives.reds: keep the two in sync
// (tools/check-natives.py reads both). All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "presence.hpp"
#include "runtime.hpp"

#include "tandem/presence.hpp"
#include "tandem/session.hpp"

#include <RedLib.hpp>

#include <algorithm>

namespace tandem::plugin {
// Something a remote V was heard doing (presence::Event). Plain fields: the
// script VM aligns struct locals to 8.
struct TandemHeard {
  std::int32_t kind{}; // presence::Kind
  std::int32_t arg{};
};
static_assert(alignof(TandemHeard) <= 8, "the script VM aligns struct locals to 8");

// A cue of another player's V (proto::Cue).
struct TandemCue {
  std::uint32_t player{};
  std::int32_t kind{}; // proto::CueKind
  std::int32_t arg{};
  float x{}, y{}, z{};
};
static_assert(alignof(TandemCue) <= 8, "the script VM aligns struct locals to 8");

// A host NPC's hit reaction (proto::NpcReaction).
struct TandemNpcReaction {
  std::uint32_t npc{};
  std::uint32_t by{};
  std::int32_t type{};
  std::int32_t direction{};
  std::int32_t intensity{};
  std::int32_t bodyPart{};
  std::int32_t variation{};
  std::int32_t source{};
  std::int32_t stance{};
  std::int32_t moveSpeed{};
  std::int32_t moveDirection{};
  bool turns{};
  float angle{};
  float x{}, y{}, z{};
};
static_assert(alignof(TandemNpcReaction) <= 8, "the script VM aligns struct locals to 8");

// Where a partner is from our camera (presence::Bearing).
struct TandemBearing {
  float distance{};
  float angle{};
  float pitch{};
  bool inView{};
  std::int32_t side{};
};
static_assert(alignof(TandemBearing) <= 8, "the script VM aligns struct locals to 8");

// Where a point of the world is on the screen (presence::ScreenPoint).
struct TandemScreenPoint {
  float x{}, y{};
  bool visible{};
};
static_assert(alignof(TandemScreenPoint) <= 8, "the script VM aligns struct locals to 8");

namespace {
proto::Vec3 Vec(const Red::Vector4 &v) { return {v.X, v.Y, v.Z}; }

template <class T> T Clamped(std::int32_t value, std::int32_t low, std::int32_t high) {
  return static_cast<T>(std::clamp(value, low, high));
}
} // namespace

struct TandemPresence : Red::IScriptable {
  // Our V's health (0..1 of its pool) and proto::kPresence* flags, every
  // frame: sent when it is worth it (presence::Gate). True when it was.
  static bool Send(float health, std::int32_t flags) {
    return PresenceState::Get().SubmitLocal(presence::HealthCode(health),
                                            Clamped<std::uint8_t>(flags, 0, 255), net::NowUs());
  }

  // Another player's health as last heard, 0..1 (-1: never heard).
  static float Health(std::uint32_t player) {
    const auto presence = PresenceState::Get().Remote(player);
    return presence ? presence::HealthFraction(presence->health) : -1.f;
  }
  static std::int32_t Flags(std::uint32_t player) {
    const auto presence = PresenceState::Get().Remote(player);
    return presence ? presence->flags : 0;
  }

  // A cue of our V for the others' puppets of it (proto::CueKind, kTrace*).
  static void SendCue(std::int32_t kind, std::int32_t arg, Red::Vector4 at) {
    PresenceState::Get().SubmitCue(
        {.kind = static_cast<proto::CueKind>(Clamped<std::uint8_t>(kind, 0, 255)),
         .arg = Clamped<std::uint8_t>(arg, 0, 255),
         .at = Vec(at)});
  }
  static Red::DynArray<TandemCue> TakeCues() {
    Red::DynArray<TandemCue> out;
    for (const auto &cue : PresenceState::Get().TakeCues(net::NowUs()))
      out.EmplaceBack(TandemCue{cue.player, static_cast<std::int32_t>(cue.kind), cue.arg, cue.at.x,
                                cue.at.y, cue.at.z});
    return out;
  }

  // What a remote V did since the last frame, heard from what its game
  // already sends (presence::Observer): its puppet's body is at `position`
  // now, `dt` seconds after the last call.
  static Red::DynArray<TandemHeard> Observe(std::uint32_t player, Red::Vector4 position, float dt) {
    auto &runtime = Runtime::Get();
    auto &observer = PresenceState::Get().ObserverOf(player);
    const auto view = runtime.ViewOf(player);
    // gamePSMLocomotionStates: Crouch 1, CrouchSprint 11, CrouchDodge 12.
    const bool crouched = view.stance == 1 || view.stance == 11 || view.stance == 12;
    observer.Move(Vec(position), view.flags, crouched, dt);
    if (const auto presence = PresenceState::Get().Remote(player))
      observer.Health(presence->health);
    std::uint32_t changes = 0;
    if (const auto held = runtime.RemoteHeld(player, changes))
      observer.Held(held->right);
    for (const auto &vitals : runtime.OthersLives(changes))
      if (vitals.player == player)
        observer.Life(vitals.life);
    const auto times = runtime.RemoteTimes(changes);
    const auto time = times.find(player);
    observer.Abilities(time == times.end() ? 0 : time->second.abilities);
    Red::DynArray<TandemHeard> out;
    for (const auto &event : observer.Take())
      out.EmplaceBack(TandemHeard{static_cast<std::int32_t>(event.kind), event.arg});
    return out;
  }
  // Its puppet is gone: a later one of that player starts afresh.
  static void Forget(std::uint32_t player) { PresenceState::Get().ObserverOf(player) = {}; }

  // A puppet was hit: its upper body flinches the way the hit went, laid
  // over its streamed pose (it never moves the body). `strength` 0..1.
  static void Flinch(std::uint32_t player, float strength, Red::Vector4 direction,
                     Red::Vector4 forward) {
    PresenceState::Get().Flinch(player, strength, Vec(direction), Vec(forward),
                                Runtime::Get().HostNowUs());
  }

  // Host: one of our shared NPCs starts a hit reaction (proto::NpcReaction).
  static void SendNpcReaction(std::uint32_t npc, std::uint32_t by, std::int32_t type,
                              std::int32_t direction, std::int32_t intensity, std::int32_t bodyPart,
                              std::int32_t variation, std::int32_t source, std::int32_t stance,
                              std::int32_t moveSpeed, std::int32_t moveDirection, bool turns,
                              float angle, Red::Vector4 way) {
    Runtime::Get().SubmitNpcReaction(
        {.npc = npc,
         .by = by,
         .type = Clamped<std::uint8_t>(type, 0, proto::kReactionMax),
         .direction = Clamped<std::int8_t>(direction, -128, 127),
         .intensity = Clamped<std::int8_t>(intensity, -128, 127),
         .bodyPart = Clamped<std::int8_t>(bodyPart, -128, 127),
         .variation = Clamped<std::int8_t>(variation, -128, 127),
         .source = Clamped<std::int8_t>(source, -128, 127),
         .stance = Clamped<std::uint8_t>(stance, 0, 255),
         .moveSpeed = Clamped<std::uint8_t>(moveSpeed, 0, 255),
         .moveDirection = Clamped<std::uint8_t>(moveDirection, 0, 255),
         .flags = static_cast<std::uint8_t>(turns ? proto::kReactionTurns : 0),
         .angle = angle,
         .way = Vec(way)});
  }
  // Guest: the host's NPCs' reactions since the last ask.
  static Red::DynArray<TandemNpcReaction> TakeNpcReactions() {
    Red::DynArray<TandemNpcReaction> out;
    for (const auto &r : PresenceState::Get().TakeReactions())
      out.EmplaceBack(TandemNpcReaction{r.npc, r.by, r.type, r.direction, r.intensity, r.bodyPart,
                                        r.variation, r.source, r.stance, r.moveSpeed,
                                        r.moveDirection, (r.flags & proto::kReactionTurns) != 0,
                                        r.angle, r.way.x, r.way.y, r.way.z});
    return out;
  }

  // The party HUD (presence.hpp): what the partner's entry says
  // (presence::Status: 0 up, 1 down, 2 out, 3 elsewhere, 4 scene, 5 menu).
  static std::int32_t Status(std::int32_t life, std::int32_t place, std::int32_t tier,
                             std::int32_t storyFlags, bool paused, std::int32_t flags) {
    return static_cast<std::int32_t>(presence::StatusOf(
        static_cast<proto::Life>(Clamped<std::uint8_t>(life, 0, 2)),
        static_cast<proto::Place>(Clamped<std::uint8_t>(place, 0, 4)),
        Clamped<std::uint8_t>(tier, 0, 255), Clamped<std::uint8_t>(storyFlags, 0, 255), paused,
        Clamped<std::uint8_t>(flags, 0, 255)));
  }
  // Where `target` is from the camera (`halfFov` in radians).
  static TandemBearing Bearing(Red::Vector4 camera, Red::Vector4 forward, float halfFov,
                               Red::Vector4 target) {
    const auto bearing = presence::BearingOf(Vec(camera), Vec(forward), halfFov, Vec(target));
    return {bearing.distance, bearing.angle, bearing.pitch, bearing.inView, bearing.side};
  }
  // Where `target` falls on the screen (0..1 from the top left corner).
  static TandemScreenPoint Project(Red::Vector4 camera, Red::Vector4 forward, Red::Vector4 right,
                                   float halfFov, bool vertical, float aspect,
                                   Red::Vector4 target) {
    const auto point = presence::ProjectOf(Vec(camera), Vec(forward), Vec(right), halfFov, vertical,
                                           aspect, Vec(target));
    return {point.x, point.y, point.visible};
  }
  static std::int32_t RoundedMetres(float distance) {
    return static_cast<std::int32_t>(presence::RoundedMetres(distance));
  }
  static float NameplateOpacity(float distance) { return presence::NameplateOpacity(distance); }

  RTTI_IMPL_TYPEINFO(TandemPresence);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemHeard, "TandemHeard", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(arg);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemCue, "TandemCue", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(arg);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemNpcReaction, "TandemNpcReaction", {
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(by);
  RTTI_PROPERTY(type);
  RTTI_PROPERTY(direction);
  RTTI_PROPERTY(intensity);
  RTTI_PROPERTY(bodyPart);
  RTTI_PROPERTY(variation);
  RTTI_PROPERTY(source);
  RTTI_PROPERTY(stance);
  RTTI_PROPERTY(moveSpeed);
  RTTI_PROPERTY(moveDirection);
  RTTI_PROPERTY(turns);
  RTTI_PROPERTY(angle);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemBearing, "TandemBearing", {
  RTTI_PROPERTY(distance);
  RTTI_PROPERTY(angle);
  RTTI_PROPERTY(pitch);
  RTTI_PROPERTY(inView);
  RTTI_PROPERTY(side);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemScreenPoint, "TandemScreenPoint", {
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(visible);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemPresence, "TandemPresence", {
  RTTI_METHOD(Send);
  RTTI_METHOD(Health);
  RTTI_METHOD(Flags);
  RTTI_METHOD(SendCue);
  RTTI_METHOD(TakeCues);
  RTTI_METHOD(Observe);
  RTTI_METHOD(Forget);
  RTTI_METHOD(Flinch);
  RTTI_METHOD(SendNpcReaction);
  RTTI_METHOD(TakeNpcReactions);
  RTTI_METHOD(Status);
  RTTI_METHOD(Bearing);
  RTTI_METHOD(Project);
  RTTI_METHOD(RoundedMetres);
  RTTI_METHOD(NameplateOpacity);
});
