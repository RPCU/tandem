#include "tandem/interpolation.hpp"
#include "tandem/movement.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace tandem;
using movement::Detailed;
using movement::Movement;
using movement::Placement;
using movement::Placer;
using movement::Swim;

namespace {
constexpr float kFrame = 1.f / 60.f;

std::uint16_t Flags(Detailed detailed, Swim swim = Swim::None) {
  return movement::Pack({.known = true, .detailed = detailed, .swim = swim});
}

// Runs a placer for `seconds` on a body the engine moves by `followed` of
// each delta (1: all of it, 0: refuses), toward a fixed target.
struct Run {
  proto::Vec3 body;
  Placer placer;
  int snaps{};
  void For(float seconds, const proto::Vec3 &target, Placement placement, float followed = 1.f) {
    for (float t = 0.f; t < seconds; t += kFrame) {
      const auto step = placer.Update(body, target, placement, kFrame);
      if (step.snap) {
        body = target; // what the game thread's teleport does
        ++snaps;
        continue;
      }
      body.x += step.delta.x * followed;
      body.y += step.delta.y * followed;
      body.z += step.delta.z * followed;
    }
  }
};
} // namespace

TEST_CASE("movement flags round-trip and fit beside an unfilled value") {
  const Movement all{.known = true,
                     .detailed = Detailed::Felled,
                     .swim = Swim::Climbing,
                     .elevator = true,
                     .workspot = true,
                     .felled = true};
  CHECK(movement::Unpack(movement::Pack(all)) == all);
  for (int d = 0; d < 32; ++d)
    for (int s = 0; s < 4; ++s) {
      const Movement m{
          .known = true, .detailed = static_cast<Detailed>(d), .swim = static_cast<Swim>(s)};
      CHECK(movement::Unpack(movement::Pack(m)) == m);
    }
  // A state that fills nothing (NPC states, an older sender) is walking.
  CHECK_FALSE(movement::Unpack(0).known);
  CHECK(movement::PlacementOf(std::uint16_t{0}) == Placement::Ground);
  CHECK(movement::Pack({}) == 0);
}

TEST_CASE("each way of moving has its placement") {
  CHECK(movement::PlacementOf(Flags(Detailed::Stand)) == Placement::Ground);
  CHECK(movement::PlacementOf(Flags(Detailed::Slide)) == Placement::Ground);
  CHECK(movement::PlacementOf(Flags(Detailed::Dodge)) == Placement::Ground);
  CHECK(movement::PlacementOf(Flags(Detailed::HardLand)) == Placement::Ground);
  CHECK(movement::PlacementOf(Flags(Detailed::Jump)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::DoubleJump)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::Fall)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::DodgeAir)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::SlideFall)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::LadderJump)) == Placement::Air);
  CHECK(movement::PlacementOf(Flags(Detailed::Vault)) == Placement::Climb);
  CHECK(movement::PlacementOf(Flags(Detailed::Climb)) == Placement::Climb);
  CHECK(movement::PlacementOf(Flags(Detailed::Ladder)) == Placement::Ladder);
  CHECK(movement::PlacementOf(Flags(Detailed::LadderSlide)) == Placement::Ladder);
  CHECK(movement::PlacementOf(Flags(Detailed::Knockdown)) == Placement::Down);
  // In the water the swimming state wins over a stale LocomotionDetailed.
  CHECK(movement::PlacementOf(Flags(Detailed::Sprint, Swim::Surface)) == Placement::Swim);
  CHECK(movement::PlacementOf(Flags(Detailed::Stand, Swim::Diving)) == Placement::Swim);
  CHECK(movement::PlacementOf(Flags(Detailed::Stand, Swim::Climbing)) == Placement::Climb);
  CHECK(movement::PlacementOf(
            movement::Pack({.known = true, .detailed = Detailed::Stand, .elevator = true})) ==
        Placement::Platform);
  CHECK(movement::PlacementOf(movement::Pack(
            {.known = true, .detailed = Detailed::Stand, .felled = true})) == Placement::Down);
  CHECK(movement::FollowsFreely(Placement::Swim));
  CHECK_FALSE(movement::FollowsFreely(Placement::Ground));
}

TEST_CASE("in the air the body follows the stream exactly, with no teleport") {
  Run run;
  // A jump: up 1.2 m and forward, frame by frame, the engine following.
  for (int i = 0; i <= 40; ++i) {
    const float t = i * kFrame;
    const proto::Vec3 target{3.f * t, 0.f, 5.f * t - 0.5f * 9.81f * t * t};
    const auto step = run.placer.Update(run.body, target, Placement::Air, kFrame);
    CHECK_FALSE(step.snap);
    CHECK(step.delta.z == doctest::Approx(target.z - run.body.z));
    run.body = {run.body.x + step.delta.x, run.body.y + step.delta.y, run.body.z + step.delta.z};
  }
  CHECK(run.placer.Snaps() == 0);
}

TEST_CASE("a body the engine will not move up is teleported, in the air or on a ladder") {
  for (auto placement : {Placement::Air, Placement::Ladder, Placement::Swim}) {
    Run run;
    run.For(0.1f, {0.f, 0.f, 2.f}, placement, 0.f);
    CHECK(run.snaps == 0); // not yet: a frame or two behind is fine
    run.For(0.3f, {0.f, 0.f, 2.f}, placement, 0.f);
    CHECK(run.snaps == 1);
    CHECK(run.body.z == doctest::Approx(2.f));
  }
}

TEST_CASE("on the ground a ledge the body cannot climb is a teleport") {
  Run run;
  // The remote V mantled a 1.5 m ledge and walks on top; our body stays below.
  run.For(0.2f, {0.5f, 0.f, 1.5f}, Placement::Ground, 0.f);
  CHECK(run.snaps == 0);
  run.For(0.2f, {0.5f, 0.f, 1.5f}, Placement::Ground, 0.f);
  CHECK(run.snaps == 1);
  // Stairs: a step at a time, followed, never teleported.
  Run stairs;
  for (int step = 1; step <= 20; ++step)
    stairs.For(0.25f, {0.3f * step, 0.f, 0.18f * step}, Placement::Ground);
  CHECK(stairs.snaps == 0);
  CHECK(stairs.body.z == doctest::Approx(3.6f).epsilon(1e-3));
}

TEST_CASE("on the ground, a body far behind is teleported; at once when very far") {
  Run behind;
  behind.For(0.4f, {3.f, 0.f, 0.f}, Placement::Ground, 0.f);
  CHECK(behind.snaps == 0);
  behind.For(0.2f, {3.f, 0.f, 0.f}, Placement::Ground, 0.f);
  CHECK(behind.snaps == 1);

  Placer placer;
  CHECK(placer.Update({}, {20.f, 0.f, 0.f}, Placement::Ground, kFrame).snap);
  // ...but not twice in a row: the teleport needs a frame or two to land.
  CHECK_FALSE(placer.Update({}, {20.f, 0.f, 0.f}, Placement::Ground, kFrame).snap);
}

TEST_CASE("landing eases the height back instead of popping") {
  Run run;
  run.For(0.5f, {0.f, 0.f, 0.4f}, Placement::Air);
  CHECK(run.body.z == doctest::Approx(0.4f));
  // Landed: the ground target is 0.4 m lower than where the body hangs.
  const auto first = run.placer.Update(run.body, {0.f, 0.f, 0.f}, Placement::Ground, kFrame);
  CHECK(first.changed);
  CHECK_FALSE(first.snap);
  CHECK(first.delta.z < 0.f);
  CHECK(first.delta.z > -0.4f * 0.3f); // a fraction, not all of it
  run.body.z += first.delta.z;
  run.For(0.4f, {0.f, 0.f, 0.f}, Placement::Ground);
  CHECK(std::abs(run.body.z) < 0.01f);
  // Afterwards the ground follows in full again.
  const auto later =
      run.placer.Update({0.f, 0.f, 0.f}, {0.f, 0.f, 0.2f}, Placement::Ground, kFrame);
  CHECK(later.delta.z == doctest::Approx(0.2f));
}

TEST_CASE("a late state in the air is extrapolated falling") {
  sync::Track track;
  constexpr std::uint64_t start = 1'800'000'000'000'000, step = 33'333;
  for (std::uint32_t i = 1; i <= 10; ++i) {
    proto::PlayerState s;
    s.seq = i;
    s.timeUs = start + i * step;
    s.position = {0.f, 0.f, 10.f};
    s.flags = Flags(Detailed::Fall);
    track.Push(s, s.timeUs + 20'000);
  }
  const auto view = track.At(start + 10 * step + track.DelayUs() + 200'000);
  CHECK(view.extrapolated);
  CHECK(view.position.z == doctest::Approx(10.f - 0.5f * 9.81f * 0.2f * 0.2f).epsilon(1e-3));
  CHECK(movement::PlacementOf(view.flags) == Placement::Air);
}
