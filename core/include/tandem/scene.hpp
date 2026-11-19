#pragma once

// Scene replay, the parts that need no game (docs/architecture/scene-replay.md).
//
// The host's game runs every story scene. Its guests never play their own
// copy: they are told that a scene is on and where, which of the host's NPCs
// act in it, how each actor's body is posed, and which line is spoken since
// when. Here: what the host says from what its game samples (Stage), what a
// guest does with what it hears (Replay: cues for the game), and where a
// guest's own V may stand to watch (WatchSpot, KeepOut).

#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace tandem::scene {
// ---- host ----

struct ActorSample {
  proto::NpcId npc{};
  proto::Vec3 position;
  std::uint8_t flags{}; // proto::kActor*
};

// What the host's game knows now (sampled a few times a second).
struct HostSample {
  std::uint64_t scene{}; // the scene resource playing (0: none)
  bool isPrivate{};      // a private scene: never announced
  std::uint8_t tier{};   // the host V's GameplayTier
  float rate{1.f};       // the scene's time against the clock (fast-forward: > 1)
  bool held{};           // its time waits (a choice hub)
  proto::Vec3 host;      // the host's V
  float hostYaw{};
  std::vector<ActorSample> actors;
};

// A staging area that moved less than this, and not longer ago than
// kStageEveryUs, is not said again.
inline constexpr float kStageMoveMetres = 0.75f;
inline constexpr std::uint64_t kStageEveryUs = 500'000;
// A line hidden this long before its end was cut; later, it just ended.
inline constexpr float kCutSeconds = 0.4f;

class Stage {
public:
  // What to send for this sample (SceneState, SceneCast), in order.
  std::vector<proto::Message> Update(const HostSample &sample, std::uint64_t hostNowUs);
  // A voiced line begins. Nothing while no scene is announced and the line
  // is the scene's (a private scene's lines are its own); barks
  // (kVoiceOverhead) and holocalls always go.
  std::optional<proto::SceneLine> Line(std::uint64_t line, proto::NpcId speaker,
                                       proto::PlayerId speakerPlayer, float duration,
                                       std::uint8_t flags, std::uint64_t hostNowUs);
  // Its subtitle went: a cut if that is before its end, else nothing to say.
  std::optional<proto::SceneLine> Hide(std::uint64_t line, std::uint64_t hostNowUs);
  bool On() const { return m_state.scene != 0; }
  std::uint32_t Play() const { return m_play; }
  const proto::SceneState &State() const { return m_state; }
  // `npc` acts in the scene announced now (its cast as last sent).
  bool Casts(proto::NpcId npc) const;

private:
  bool Speaking(std::uint64_t hostNowUs) const;

  proto::SceneState m_state;
  proto::SceneCast m_cast;
  std::uint32_t m_play{};
  std::uint64_t m_stagedUs{};
  bool m_private{};
  std::map<std::uint64_t, proto::SceneLine> m_lines; // spoken now
};

// ---- guest ----

// A line that would begin with less than this left is not played; a line
// begun less than kSeekSeconds ago plays from its start (the network's delay
// is not worth a seek).
inline constexpr float kLateSeconds = 0.35f;
inline constexpr float kSeekSeconds = 0.25f;
// A line of unknown length is forgotten after this.
inline constexpr float kUnknownLineSeconds = 30.f;

struct Cue {
  enum class Kind : std::uint8_t {
    Begin,     // a scene is on (state; seconds: how long already)
    Change,    // its staging, rate, tier or flags changed (state)
    End,       // it is over (state.flags & kSceneInterrupted: before its end)
    Actor,     // an NPC acts in it, or its flags changed (actor)
    ActorGone, // it no longer does: the proxy is its own again (actor)
    Voice,     // play the line on its speaker from `seconds` in (line)
    VoiceStop, // stop it (line)
  };
  Kind kind{};
  proto::SceneState state;
  proto::SceneActor actor;
  proto::SceneLine line;
  float seconds{};
};

class Replay {
public:
  // A scene message from the host, at this host-clock time.
  std::vector<Cue> Note(const proto::Message &message, std::uint64_t hostNowUs);
  // The session is over or the host is gone: everything stops, every proxy
  // is released.
  std::vector<Cue> Reset();
  // Lines whose time is over are forgotten (no cue: the voice ended itself).
  void Tick(std::uint64_t hostNowUs);

  bool On() const { return m_state.scene != 0; }
  const proto::SceneState &State() const { return m_state; }
  const std::vector<proto::SceneActor> &Cast() const { return m_cast; }
  bool Acts(proto::NpcId npc) const;
  std::size_t Speaking() const { return m_lines.size(); }
  // Seconds since the scene's time 0.
  float Seconds(std::uint64_t hostNowUs) const;
  std::uint32_t Stale() const { return m_stale; } // lines too late to play

private:
  void StopVoices(std::vector<Cue> &out, bool sceneOnly);
  void Leave(std::vector<Cue> &out, std::uint8_t flags);

  proto::SceneState m_state;
  std::vector<proto::SceneActor> m_cast;
  struct Spoken {
    proto::SceneLine line;
    std::uint64_t endUs{};
  };
  std::map<std::uint64_t, Spoken> m_lines;
  std::uint32_t m_stale{};
};

// ---- where a guest's own V stands ----

// How close a watcher's spot may be to anyone, and the ring's bounds.
inline constexpr float kSpotClearMetres = 1.1f;
inline constexpr float kSpotMinRing = 2.2f, kSpotMaxRing = 7.f, kSpotBeyondActors = 1.6f;

struct Spot {
  bool found{};
  proto::Vec3 position;
  float yaw{}; // facing the scene's middle
};

// A place to watch a staged scene from: on a ring around its middle, on the
// host's side of it but out of the host's line of sight to the actors, clear
// of every body (actors, the host, the other watchers), the nearest such
// place to where our V stands. Not found: the scene has no staging, or no
// place is clear.
Spot WatchSpot(const proto::SceneState &state, std::span<const proto::Vec3> bodies,
               const proto::Vec3 &host, const proto::Vec3 &own);

// Our V stands inside an actor (the scene walked it there): the step, at
// most maxStep long, that takes it out of every body's radius. Zero when
// clear.
proto::Vec3 KeepOut(const proto::Vec3 &own, std::span<const proto::Vec3> bodies, float radius,
                    float maxStep);
} // namespace tandem::scene
