#pragma once

// One simulated game (docs/architecture/sim.md): an Endpoint with a referee
// on its wire, a simulated V with a persona, and as host its world (NPCs,
// time, weather) and a scripted story, all from one seed. It plays against
// the real game (tandem-peer sim) or against another simulated game in the
// same process (RunPair: task test, task sim:fuzz).

#include "tandem/endpoint.hpp"
#include "tandem/shaping.hpp"
#include "tandem/sim/coverage.hpp"
#include "tandem/sim/motion.hpp"
#include "tandem/sim/player.hpp"
#include "tandem/sim/referee.hpp"
#include "tandem/sim/story.hpp"
#include "tandem/sim/world.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::sim {
struct GameOptions {
  bool host{};
  std::string persona{"companion"};
  std::uint64_t seed{1};
  std::string name; // "": PERSONA-SEED
  std::string address{"127.0.0.1"};
  std::uint16_t port{proto::kDefaultPort}; // host: 0, any free one (Game::Port)
  Timing timing{};
  std::string storyFile; // host: "" the default story (story.hpp)
  std::filesystem::path playDir{"recordings/play"};
  float pace{1.f};  // goals and leaves this many times faster
  bool shape{true}; // the persona's own link, through net::Shaper
  // Every line worth a log: findings, goals, the session.
  std::function<void(const std::string &)> log;
};

class Game {
public:
  Game(GameOptions options, Coverage &coverage);
  ~Game();
  Game(const Game &) = delete;
  Game &operator=(const Game &) = delete;

  bool Start(std::string &error);
  // One frame; `wait`: how long the endpoint may block for traffic.
  void Step(std::chrono::milliseconds wait = std::chrono::milliseconds{0});
  // Leaves with a goodbye; the run's message counts go to the coverage.
  void Stop(const std::string &reason = "sim stopped");

  bool Joined() const { return m_endpoint && m_endpoint->IsJoined(); }
  std::uint16_t Port() const;
  bool Others() const { return !m_scene.players.empty(); }
  // What the referees found (every connection of the run), and the sim's
  // own consistency checks (sim-check: lines) that failed.
  std::uint32_t Violations() const;
  std::uint32_t Hints() const;
  std::uint32_t CheckFailures() const { return m_checkFailures; }
  std::map<std::string, std::uint32_t> Rules() const;
  std::vector<std::string> Findings() const { return m_findings; } // violations, the first 50
  const SimV &V() const { return m_v; }
  const HostWorld *World() const { return m_world.get(); }
  const Endpoint *Wire() const { return m_endpoint.get(); }
  std::uint32_t Sessions() const { return m_sessions; }
  std::string Summary() const;

private:
  bool Connect(std::string &error);
  void Drop(bool goodbye, const std::string &reason, std::uint64_t nowUs);
  void Route(Outbox &out, std::uint64_t nowUs);
  void OnEvent(const SessionEvent &event, std::uint64_t nowUs, Outbox &out);
  void Check(std::uint64_t nowUs);
  void Flag(const std::string &what, std::uint64_t nowUs);
  void Say(const std::string &line) const {
    if (m_options.log)
      m_options.log(line);
  }
  void Retire(); // the current referee's counts into the totals

  GameOptions m_options;
  Coverage &m_coverage;
  const Persona &m_persona;
  Random m_random;
  Motion m_motion;
  SimV m_v;
  std::unique_ptr<HostWorld> m_world;
  std::unique_ptr<StoryDirector> m_story;
  std::unique_ptr<net::Shaper> m_shaper;
  std::unique_ptr<Endpoint> m_endpoint;
  std::unique_ptr<Referee> m_referee;
  Scene m_scene;
  std::vector<proto::PlayerHit> m_ownHits; // our world's NPCs on our V
  // Totals of the referees retired so far.
  std::uint32_t m_violations{}, m_hints{};
  std::map<std::string, std::uint32_t> m_rules;
  std::vector<std::uint64_t> m_sent, m_received;
  std::vector<std::string> m_findings;
  std::uint32_t m_checkFailures{}, m_sessions{};
  std::map<std::string, std::uint64_t> m_flagged;
  std::map<std::size_t, bool> m_undeliverable;
  std::optional<std::uint64_t> m_npcsApartUs;
  std::uint64_t m_rejoinAtUs{}, m_leaveAtUs{}, m_nextCheckUs{};
  Goal m_shownGoal{Goal::Count};
  bool m_stopped{};
};

// A simulated host and a simulated guest in one process, over loopback.
struct PairOptions {
  std::string hostPersona{"companion"}, guestPersona{"companion"};
  std::uint64_t seed{1};
  double seconds{20.};
  float pace{1.f};
  Timing timing{};
  std::filesystem::path playDir{"recordings/play"};
  std::string storyFile;
  std::function<void(const std::string &)> log; // lines prefixed "host: ", "guest: "
};
struct PairResult {
  bool joined{};
  std::uint32_t violations{}, hints{}, checkFailures{}, guestSessions{};
  std::map<std::string, std::uint32_t> rules;
  std::vector<std::string> findings;
};
PairResult RunPair(const PairOptions &options, Coverage &coverage);

// tandem-peer sim ... and sim-fuzz ... (args after the command word).
int SimMain(std::string_view command, const std::vector<std::string_view> &args);
} // namespace tandem::sim
