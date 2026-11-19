// The simulated second game (docs/architecture/sim.md): its models alone,
// then simulated games against each other over loopback, refereed.

#include "tandem/sim/game.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <iostream>
#include <sstream>

using namespace std::chrono_literals;
using namespace tandem;
using sim::operator+;

namespace {
constexpr std::uint64_t kSecond = 1'000'000;

// A V walking 6 m north at 1.5 m/s, standing 2 s, then running east.
play::Recording Walk() {
  play::Recording recording;
  std::uint64_t t = 0;
  proto::Vec3 at{100.f, 100.f, 10.f};
  const auto add = [&](proto::Vec3 velocity, float yaw, float seconds) {
    for (float s = 0.f; s < seconds; s += 1.f / 30.f, t += 33'333) {
      at = at + proto::Vec3{velocity.x / 30.f, velocity.y / 30.f, 0.f};
      recording.entries.push_back({t, proto::PlayerState{.position = at,
                                                         .orientation = sim::QuatOfYaw(yaw),
                                                         .velocity = velocity}});
    }
  };
  add({0.f, 1.5f, 0.f}, 0.f, 4.f);
  add({}, 0.f, 3.f);
  add({4.f, 0.f, 0.f}, -sim::kPi / 2.f, 4.f);
  return recording;
}

void Report(const sim::PairResult &result, const sim::Coverage &coverage) {
  for (const auto &finding : result.findings)
    MESSAGE(finding);
  for (const auto &line : coverage.Report())
    std::cout << line << '\n';
}
} // namespace

TEST_CASE("sim: motion stitched from a recording heads for its goal") {
  sim::Motion motion;
  REQUIRE(motion.Add(Walk()) >= 3);
  sim::Random random(3);
  sim::Body body{.position = {0.f, 0.f, 0.f}};
  const proto::Vec3 goal{-30.f, 0.f, 0.f};
  float before = sim::Distance(body.position, goal);
  for (int frame = 0; frame < 30 * 8; ++frame)
    motion.Step(body, goal, sim::Gait::Walk, false, 1.f / 30.f, random);
  // Eight seconds of the recorded stride: about 12 m closer, never a jump.
  const float after = sim::Distance(body.position, goal);
  // (A recorded walk stops and starts: clips mix walking and standing.)
  CHECK(after < before - 4.f);
  CHECK(motion.Stitched() >= 2);
  // Within a metre of it, it stands.
  body.position = goal + proto::Vec3{0.5f, 0.f, 0.f};
  before = sim::Distance(body.position, goal);
  motion.Step(body, goal, sim::Gait::Run, false, 0.5f, random);
  CHECK(sim::Distance(body.position, goal) <= before + 1.f);

  // No recording: the plain walk, at the game's speeds and flags.
  sim::Motion plain;
  sim::Body walker{};
  for (int frame = 0; frame < 30; ++frame)
    plain.Step(walker, {0.f, 50.f, 0.f}, sim::Gait::Sprint, false, 1.f / 30.f, random);
  CHECK(walker.position.y == doctest::Approx(6.5f).epsilon(0.05));
  CHECK(walker.stance == 2);
}

TEST_CASE("sim: the host's world answers by the real host's rules") {
  sim::Coverage coverage;
  sim::HostWorld world({.npcs = 3}, sim::Random(5), &coverage);
  sim::Scene scene;
  scene.self = scene.host = 1;
  scene.players[2] = {.id = 2, .heard = true, .position = {40.f, 0.f, 0.f}};
  sim::SeenPlayer self{.id = 1, .heard = true, .position = {0.f, 0.f, 0.f}};
  sim::Outbox out;
  world.Tick(kSecond, scene, self, out);
  REQUIRE(world.Alive() == 3);
  const auto npc = world.Npcs().begin()->first;
  out.clear();
  // A takedown of a dead NPC is refused; a grab of a live one far off too.
  world.OnAction({.player = 2, .npc = npc, .kind = proto::NpcActionKind::Grab}, kSecond, scene,
                 self, out);
  const auto *reply = std::get_if<proto::NpcActionReply>(&out.back().message);
  REQUIRE(reply);
  CHECK(out.back().to == 2);
  CHECK(!reply->accepted);
  CHECK(reply->reason == proto::kRefusedTooFar);
  // Strikes by the game's rules: health falls, then it dies once.
  out.clear();
  for (int i = 0; i < 10; ++i)
    world.OnStrike({.player = 2, .npc = npc, .damage = 30.f}, kSecond, out);
  const auto deaths = std::ranges::count_if(out, [](const auto &o) {
    const auto *gone = std::get_if<proto::NpcGone>(&o.message);
    return gone && gone->reason == proto::kGoneDead;
  });
  CHECK(deaths == 1);
  CHECK(coverage.Hits("npc.death") == 1);
  CHECK(coverage.Hits("npc.hit-ignored") > 0);
}

TEST_CASE("sim: the story waits for a guest's answer") {
  sim::Coverage coverage;
  std::string error;
  std::istringstream in("0 call judy 1\n0 await answer 5\n1 call judy 2\n");
  sim::StoryDirector story(sim::ParseStory(in, "test", error), &coverage);
  sim::Scene scene;
  scene.players[2] = {.id = 2, .heard = true};
  sim::SeenPlayer self{.id = 1, .heard = true};
  sim::Outbox out;
  story.Tick(kSecond, scene, self, out);
  story.Tick(3 * kSecond, scene, self, out);
  CHECK(out.size() == 1); // ringing, and the clock stands still
  story.On(events::Story{proto::PhoneAction{.player = 2, .action = proto::kPhoneAnswer}});
  story.Tick(4 * kSecond, scene, self, out);
  story.Tick(5 * kSecond, scene, self, out);
  CHECK(out.size() == 2);
  CHECK(coverage.Hits("story.call-answered") == 1);
}

// Simulated games against each other, refereed both ways: no rule broken,
// the world and its record in step.
TEST_CASE("sim: simulated games against each other break no rule") {
  const Timing fast{.digest = 1000ms, .resyncCooldown = 2000ms};
  struct Pairing {
    const char *host, *guest;
    std::uint64_t seed;
    double seconds;
  };
  for (const auto &pairing :
       {Pairing{"companion", "companion", 1, 10.}, Pairing{"companion", "chaos", 2, 14.},
        Pairing{"speedrunner", "loner", 3, 6.}}) {
    CAPTURE(pairing.host);
    CAPTURE(pairing.guest);
    sim::Coverage coverage;
    const auto result = sim::RunPair({.hostPersona = pairing.host,
                                      .guestPersona = pairing.guest,
                                      .seed = pairing.seed,
                                      .seconds = pairing.seconds,
                                      .pace = 4.f,
                                      .timing = fast,
                                      .playDir = "no-recordings"},
                                     coverage);
    Report(result, coverage);
    CHECK(result.joined);
    CHECK(result.violations == 0);
    CHECK(result.checkFailures == 0);
    CHECK(coverage.FeaturesHit() > 10);
    CHECK(coverage.Hits("npc.spawn") > 0);
  }
}
