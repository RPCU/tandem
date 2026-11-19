#pragma once

// The simulated host's world (docs/architecture/sim.md): its NPCs with a
// small behaviour machine, the time of day and the weather, and its answers
// to what guests ask, by the rules the real host applies (combat.hpp
// StrikeLoss, timebubble.hpp, the refusals of scripts/tandem/Takedowns.reds).
// It writes protocol messages; the endpoint keeps the session's record and
// the digests from them, so both always agree with what was sent.

#include "tandem/sim/coverage.hpp"
#include "tandem/sim/names.hpp"
#include "tandem/sim/scene.hpp"
#include "tandem/timebubble.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tandem::sim {
enum class NpcKind : std::uint8_t { Ranged, Melee, Netrunner, Civilian };
enum class NpcMode : std::uint8_t {
  Idle,
  Patrol,
  Alerted, // heard or half-seen something: looking for it
  Combat,
  Cover,
  Staggered, // its guard was broken, or it was stunned
  Flee,
  Held,        // grappled, or in a takedown
  Unconscious, // a body, alive
  Dead,        // a body
};
const char *NameOf(NpcMode mode);

// One kind of NPC the world spawns.
struct NpcSpec {
  NpcKind kind{};
  std::string record; // TweakDB character record
  std::string weapon; // TweakDB item it holds ("": none)
  float health{100.f};
  float poise{100.f};
  // It carries a quest item: everyone gets one from its body (SharedItem).
  std::string questItem;
};

struct SimNpc {
  proto::NpcId id{};
  NpcSpec spec;
  NpcMode mode{};
  proto::Vec3 position, home, goal;
  float yaw{};
  float health{}, poise{};
  float factor{1.f}; // its speed in a time bubble
  // Each player it is noticing (0..1), has detected, and last saw.
  std::map<proto::PlayerId, float> awareness;
  std::map<proto::PlayerId, std::uint64_t> seenUs;
  std::uint8_t detected{};
  proto::PlayerId target{};
  proto::PlayerId holder{};
  proto::NpcHoldKind hold{};
  proto::PlayerId killer{};
  // Status effects on it: the record, whose it is, when it ends.
  struct Effect {
    proto::PlayerId player{};
    std::uint64_t untilUs{};
  };
  std::map<std::uint64_t, Effect> effects;
  std::uint64_t modeUntilUs{}, nextShotUs{}, nextMeleeUs{}, nextCoverUs{}, nextHackUs{};
  std::uint64_t poiseBackUs{}, goneAtUs{}, heldSinceUs{};
  int burst{};
  bool body{};         // lying there (unconscious or dead)
  bool announced{};    // its NpcGone for being a body was sent
  proto::NpcAnim anim; // as last sent
  bool Alive() const { return !body; }
};

class HostWorld {
public:
  struct Config {
    std::size_t npcs{5}; // alive at a time
    float ring{16.f};    // spawned this far from the anchor, give or take
    float leash{85.f};   // an NPC farther than this from our V goes (regions::kLose)
    std::uint64_t corpseUs{40'000'000};
    std::uint64_t waveUs{12'000'000};    // a new group this long after the last fell
    std::uint64_t grappleUs{12'000'000}; // a grappled NPC breaks free after this
    std::uint64_t takedownUs{2'000'000};
    std::uint8_t difficulty{1}; // gameDifficulty (Hard)
    float clockScale{8.f};      // game seconds a second
    std::int32_t startTime{12 * 3600};
    std::uint64_t weatherUs{90'000'000};
    // The NPCs live around our own V (per-player regions: a host shares
    // what is around itself, with the guests linked to it); off, around the
    // partner's, and kept while anyone is near (before regions).
    bool aroundSelf{true};
    std::vector<NpcSpec> cast; // empty: DefaultCast()
  };
  static std::vector<NpcSpec> DefaultCast();

  HostWorld(Config config, Random random, Coverage *coverage);

  // One step: `self` is our own V as it stands (the scene holds the others).
  void Tick(std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self, Outbox &out);

  // What players ask of our NPCs (a guest's, stamped; our own V's the same way).
  void OnStrike(const proto::NpcStrike &strike, std::uint64_t nowUs, Outbox &out);
  void OnEffect(const proto::NpcEffect &effect, std::uint64_t nowUs, Outbox &out);
  void OnHack(const proto::NpcHack &hack, std::uint64_t nowUs, Outbox &out);
  void OnAction(const proto::NpcAction &action, std::uint64_t nowUs, const Scene &scene,
                const SeenPlayer &self, Outbox &out);
  void OnNoise(const proto::Noise &noise, std::uint64_t nowUs, const Scene &scene,
               const SeenPlayer &self);
  void OnShotHit(const proto::ShotHit &hit);
  void OnPlayerLeft(proto::PlayerId player, std::uint64_t nowUs, Outbox &out);
  // Our world was rebuilt around our V (a fast travel): every NPC goes, and
  // new ones come where we are now.
  void Rebuild(std::uint64_t nowUs, Outbox &out);

  // A hit one of our NPCs lands on our own V (never a message).
  std::function<void(const proto::PlayerHit &)> hitOwnV;

  const std::map<proto::NpcId, SimNpc> &Npcs() const { return m_npcs; }
  // The NPCs as our own V sees them.
  void Fill(Scene &scene) const;
  std::int32_t GameTime() const { return m_gameTime; }
  std::uint64_t Weather() const { return m_weather; }
  std::size_t Alive() const;
  // What must hold between this world and the session's record of it
  // (Endpoint::SharedNpcs, NpcHolds): "" when it does.
  std::string Check(const Endpoint &endpoint) const;

private:
  SimNpc *Find(proto::NpcId npc);
  void Spawn(std::uint64_t nowUs, const proto::Vec3 &anchor, Outbox &out, bool fighter);
  void Clock(std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void Think(SimNpc &npc, float dt, std::uint64_t nowUs, const Scene &scene, const SeenPlayer &self,
             Outbox &out);
  void Perceive(SimNpc &npc, float dt, std::uint64_t nowUs, const Scene &scene,
                const SeenPlayer &self, Outbox &out);
  void Detect(SimNpc &npc, proto::PlayerId player, std::uint64_t nowUs, Outbox &out);
  void Fire(SimNpc &npc, const SeenPlayer &target, std::uint64_t nowUs, Outbox &out);
  void Melee(SimNpc &npc, const SeenPlayer &target, Outbox &out);
  void Fall(SimNpc &npc, bool dead, proto::PlayerId by, std::uint64_t nowUs, Outbox &out);
  void Free(SimNpc &npc, Outbox &out);
  void Remove(SimNpc &npc, std::uint8_t reason, Outbox &out);
  void Animate(SimNpc &npc, proto::NpcAnims &changed);
  void Hit(std::string_view feature) {
    if (m_coverage)
      m_coverage->Hit(feature);
  }

  Config m_config;
  Random m_random;
  Coverage *m_coverage{};
  std::map<proto::NpcId, SimNpc> m_npcs;
  proto::NpcId m_nextNpc{1};
  proto::PlayerId m_self{1};
  sync::TimeBubbles m_bubbles;
  std::uint64_t m_lastUs{}, m_nextStatesUs{}, m_nextAwarenessUs{}, m_nextWorldUs{};
  std::uint64_t m_nextWeatherUs{}, m_waveAtUs{}, m_startUs{};
  bool m_started{}, m_awarenessShown{}, m_paused{}, m_everSpawned{};
  std::int32_t m_gameTime{};
  double m_gameSeconds{};
  std::uint64_t m_weather{};
  // Uploads in progress, in the order they came.
  struct Upload {
    proto::NpcHackState state;
    std::uint8_t flags{};
    std::uint64_t doneUs{};
  };
  std::vector<Upload> m_uploads;
  // Takedowns being played: what the NPC becomes at the end.
  struct Ending {
    proto::NpcId npc{};
    std::uint64_t atUs{};
    bool dead{};
    proto::PlayerId by{};
    std::int8_t part{-1};
  };
  std::vector<Ending> m_endings;
};
} // namespace tandem::sim
