#pragma once

// The simulated V (docs/architecture/sim.md, "The simulated V"): a player
// with goals, picked by its persona's tastes from what the scene makes
// possible (fight the NPC nearest, ride in the partner's car, revive them
// when they are down...), moving the way a real V moves (motion.hpp), and
// answering what the session does to it the way its game would: hits that
// hurt, shots fired at it that may miss, going down when another V is up and
// out when nobody is, the host's calls and choice hubs, regroup offers.
//
// It writes protocol messages about its own V and its requests to an Outbox
// (scene.hpp); as host, the runner hands its requests on NPCs to the
// world instead (a host's V hits its own NPCs without a message).

#include "tandem/sim/coverage.hpp"
#include "tandem/sim/motion.hpp"
#include "tandem/sim/persona.hpp"
#include "tandem/sim/scene.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace tandem::sim {
class SimV {
public:
  struct Config {
    bool host{};
    float pace{1.f};                            // goals this many times faster (tests)
    proto::Vec3 start{-915.f, 1879.5f, 36.19f}; // the test field
    // The look sent (tandem-peer sim: a real V's body keys and random
    // clothes, looks.hpp); the default: the body alone, a stand-in on it.
    proto::Appearance look{.body = proto::Body::Female};
    std::string car{"Vehicle.v_standard2_archer_hella_player"};
    std::string gun{"Items.Preset_Lexington_Default"};
    std::string blade{"Items.Preset_Katana_Default"};
  };
  SimV(const Persona &persona, Config config, Random random, Coverage *coverage, Motion *motion);

  // One frame: the goal goes on, the V moves, its state goes out.
  void Tick(std::uint64_t nowUs, const Scene &scene, Outbox &out);
  // What the session tells this game (events), and, as host, what our own
  // world says (its replies to our V, its uploads) as if heard.
  void On(const SessionEvent &event, std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void Heard(const proto::Message &message, std::uint64_t nowUs, const Scene &scene, Outbox &out);
  // One of our own world's NPCs hit our V (host: never a message).
  void HitBy(const proto::PlayerHit &hit, std::uint64_t nowUs, const Scene &scene, Outbox &out);
  // A new session (a rejoin): what the old one knew of us is gone.
  void Rejoined(std::uint64_t nowUs);
  // Our V's look, held items, life... for a new connection to send again.
  void Introduce(Outbox &out) const;

  // Our V as the models see it.
  const SeenPlayer &Self() const { return m_self; }
  Goal Doing() const { return m_goal; }
  float Health() const { return m_health; }
  const Persona &Who() const { return m_persona; }
  // Our world was rebuilt around us (host: the world rebuilds too).
  bool Rebuilt() { return std::exchange(m_rebuilt, false); }

private:
  struct Task {
    Goal goal{Goal::Follow};
    std::uint64_t startUs{}, untilUs{}, nextUs{};
    int step{};
    proto::NpcId npc{};
    proto::PlayerId who{};
    proto::Vec3 where;
    bool melee{}, asked{}, accepted{};
  };

  bool Possible(Goal goal, const Scene &scene) const;
  void Pick(std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void Begin(Goal goal, std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void Finish(std::uint64_t nowUs, Outbox &out);
  void Work(std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void Move(float dt, const Scene &scene);
  void Send(std::uint64_t nowUs, Outbox &out);
  void Hurt(float damage, bool cannotKill, std::uint64_t nowUs, const Scene &scene, Outbox &out);
  void SetLife(proto::Life life, std::uint64_t nowUs, Outbox &out);
  void Hold(std::uint64_t right, Outbox &out);
  void Travel(const proto::Vec3 &to, std::uint8_t reason, std::uint64_t nowUs, Outbox &out);
  const SeenNpc *Target(const Scene &scene, bool forTakedown) const;
  std::uint64_t Seconds(float low, float high);
  void Hit(std::string_view feature) {
    if (m_coverage)
      m_coverage->Hit(feature);
  }

  const Persona &m_persona;
  Config m_config;
  Random m_random;
  Coverage *m_coverage{};
  Motion *m_motion{};
  SeenPlayer m_self;
  Body m_body;
  Gait m_gait{Gait::Walk};
  bool m_crouch{};
  proto::Vec3 m_goalAt;
  Goal m_goal{Goal::Follow};
  Task m_task;
  std::uint64_t m_lastUs{}, m_nextStateUs{}, m_pickUs{};
  float m_health{100.f};
  std::uint64_t m_hurtUs{}, m_lifeUs{}, m_aloneDownUs{};
  std::uint32_t m_seq{}, m_vehicleSeq{}, m_shotSeq{}, m_offers{}, m_tokens{}, m_seeds{};
  std::uint64_t m_held{};
  bool m_paused{}, m_rebuilt{};
  // The car we drive, as its body.
  std::optional<Body> m_car;
  float m_carSpeed{};
  std::uint64_t m_brakedUntilUs{};
  // Reactions under way.
  std::optional<std::pair<proto::PlayerId, std::uint64_t>> m_reviving; // whom, done at
  std::uint64_t m_answerUs{}, m_suggestUs{};
  std::uint64_t m_lastCall{};
  std::int32_t m_lastHub{-1};
  std::set<proto::WorldItemId> m_claimed;
  std::map<proto::WorldItemId, std::uint64_t> m_seenItems;
  std::set<proto::NpcId> m_brokenGuard; // their poise broke under our hits
  std::uint64_t m_chaosUs{};
};
} // namespace tandem::sim
