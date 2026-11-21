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

// Something an actor (or a V) holds in the scene, as the host's game sees it.
// `key` is the host's own name for it (the item's entity, or the holder and
// its hand): the same thing keeps its prop number while it changes hands.
struct PropSample {
  std::uint64_t key{};
  std::uint64_t record{};
  proto::NpcId holder{};
  proto::PlayerId holderPlayer{};
  bool leftHand{};
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
  std::vector<PropSample> props;
};

// A staging area that moved less than this, and not longer ago than
// kStageEveryUs, is not said again.
inline constexpr float kStageMoveMetres = 0.75f;
inline constexpr std::uint64_t kStageEveryUs = 500'000;
// A line hidden this long before its end was cut; later, it just ended.
inline constexpr float kCutSeconds = 0.4f;
// A speaker's face is streamed this long after its line.
inline constexpr std::uint64_t kSpeakerTailUs = 500'000;

class Stage {
public:
  // What to send for this sample (SceneState, SceneCast, SceneProp), in order.
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
  // The NPCs that speak a line of the scene now, or did until
  // kSpeakerTailUs ago (their faces are streamed that long).
  std::vector<proto::NpcId> Speakers(std::uint64_t hostNowUs) const;

private:
  bool Speaking(std::uint64_t hostNowUs) const;

  proto::SceneState m_state;
  proto::SceneCast m_cast;
  std::uint32_t m_play{};
  std::uint64_t m_stagedUs{};
  bool m_private{};
  std::map<std::uint64_t, proto::SceneLine> m_lines; // spoken now
  std::map<std::uint64_t, proto::SceneProp> m_props; // by the sample's key, as last said
  std::uint32_t m_propCount{};
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
    Prop,      // someone holds a prop, or it changed hands (prop)
    PropGone,  // it is no longer there (prop)
  };
  Kind kind{};
  proto::SceneState state;
  proto::SceneActor actor;
  proto::SceneLine line;
  proto::SceneProp prop;
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
  std::size_t Props() const { return m_props.size(); }
  // An actor (or a V, with npc 0) holds a prop now.
  bool Holds(proto::NpcId npc, proto::PlayerId player) const;

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
  std::map<std::uint32_t, proto::SceneProp> m_props; // held now, by prop number
  std::uint32_t m_stale{};
};

// ---- the scene's streams share one budget (host) ----

// What the scene's streams (each actor's body, a speaker's face, a body on
// another rig, the camera) may put on a guest's link together, bytes a
// second, each message's own header included (they ride in the datagrams
// the host's V already sends): 22 kbit/s of the 64 a link has
// (docs/architecture/netcode.md, Budget). tandem.ini scene_kbps changes it.
inline constexpr std::uint32_t kSceneBytesPerSecond = 2750;
inline constexpr std::size_t kStreamOverhead = 12;
// No stream goes faster than this, and one that would go slower than
// kSlowestUs is not sent at all (its actor keeps its own animation, as an
// actor nobody streams does): the others get its share.
inline constexpr std::uint64_t kFastestUs = 33'000, kSlowestUs = 500'000;
// A stream nobody offered a frame of for this long has ended.
inline constexpr std::uint64_t kStreamIdleUs = 1'500'000;

// A stream's name: the NPC and which of its rigs (proto::kRig*), or the
// camera.
constexpr std::uint64_t StreamOf(proto::NpcId npc, std::uint8_t part) {
  return std::uint64_t{npc} << 8 | part;
}
// The stream of a rig's float tracks, apart from its bones'.
constexpr std::uint64_t TracksOf(proto::NpcId npc, std::uint8_t part) {
  return std::uint64_t{npc} << 8 | 0x80 | part;
}
inline constexpr std::uint64_t kCameraStream = ~std::uint64_t{0};
// The camera is the picture: it matters more than any one body.
inline constexpr float kCameraWeight = 3.f;
// A speaker's face is where one looks: before a body.
inline constexpr float kFaceWeight = 2.f;
// Its float tracks (wrinkles, blinks): half of it.
inline constexpr float kTracksWeight = 1.f;

// Splits the budget between the streams that are alive: each is served at
// least every kSlowestUs (the heaviest-weighted first, while the budget
// lasts), and what is left over goes by weight. Whatever the number of
// streams and the size of their frames, what Due() lets through stays
// within the budget.
class Pacer {
public:
  explicit Pacer(std::uint32_t bytesPerSecond = kSceneBytesPerSecond) : m_budget(bytesPerSecond) {}
  // A frame of `stream` is there to send at `nowUs`: may it go? `weight`:
  // how much the stream matters (1: an actor near a watcher; less far away,
  // more for a speaker's face and the camera).
  bool Due(std::uint64_t stream, float weight, std::uint64_t nowUs);
  // It went, `bytes` long (the message as encoded).
  void Sent(std::uint64_t stream, std::size_t bytes, std::uint64_t nowUs);
  // The stream's share now, bytes a second (0: not served).
  float Share(std::uint64_t stream) const;
  std::size_t Streams() const { return m_streams.size(); }
  std::size_t Served() const;
  void Clear() { m_streams.clear(); }

private:
  struct Stream {
    float weight{1.f};
    float bytes{160.f}; // a frame of it, smoothed
    float share{};      // bytes a second (0: not served)
    std::uint64_t nextUs{};
    std::uint64_t offeredUs{};
  };
  void Split(std::uint64_t nowUs);

  std::uint32_t m_budget;
  std::map<std::uint64_t, Stream> m_streams;
  std::uint64_t m_splitUs{};
  std::size_t m_splitCount{};
};

// ---- the camera of a cutscene ----

struct CameraView {
  proto::Vec3 position;
  proto::Quat orientation;
  float fov{60.f}; // degrees
};

// Host: is going from `from` to `to` in `seconds` a cut (another shot), not
// a camera that moves?
bool IsCut(const CameraView &from, const CameraView &to, float seconds);
proto::SceneCamera PackCamera(std::uint32_t play, const CameraView &view, bool cut);
CameraView UnpackCamera(const proto::SceneCamera &camera);

// Guest: the host's recent views, drawn a little in the past between the two
// around that moment (a cut is not blended: the view jumps when its time
// comes). The stream stopped: nothing after kStaleUs, the guest's own camera
// comes back.
class CameraTrack {
public:
  static constexpr std::size_t kMaxSamples = 24;
  static constexpr std::uint64_t kStaleUs = 1'000'000;

  // False: older than the newest, or of an earlier play.
  bool Push(const proto::SceneCamera &camera);
  // The view to show at host clock `nowUs`. False: none, or the stream
  // stopped.
  bool At(std::uint64_t nowUs, CameraView &out) const;
  // How far in the past it is drawn: a frame and a half of the stream.
  std::uint64_t DelayUs() const;
  std::uint32_t Play() const { return m_play; }
  std::uint32_t Received() const { return m_received; }
  void Clear();

private:
  struct Sample {
    std::uint64_t timeUs{};
    CameraView view;
    bool cut{};
  };
  std::vector<Sample> m_samples; // oldest first
  std::uint32_t m_play{};
  std::uint32_t m_lastSeq{};
  std::uint32_t m_received{};
  double m_intervalUs{66'000};
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
