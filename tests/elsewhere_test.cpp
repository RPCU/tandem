#include "tandem/elsewhere.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace tandem;
using elsewhere::Sample;
using elsewhere::Tracker;
using proto::Place;

namespace {
constexpr std::uint64_t kFrameUs = 33'333;

// Our V walking at 1.5 m/s along X from `from`, a sample per frame.
struct Walk {
  Tracker tracker;
  std::uint64_t nowUs = 1'000'000;
  proto::Vec3 at{100.f, 200.f, 10.f};
  bool settled = false;
  bool mounted = false;
  Place place = Place::City;

  elsewhere::Body Step(float dx = 0.05f) {
    nowUs += kFrameUs;
    at.x += dx;
    return tracker.Feed({.timeUs = nowUs,
                         .position = at,
                         .orientation = {},
                         .mounted = mounted,
                         .settled = settled,
                         .place = place});
  }
  // A teleport: one sample far away.
  elsewhere::Body Jump(proto::Vec3 to) {
    at = to;
    return Step(0.f);
  }
  void For(std::uint64_t us) {
    const auto until = nowUs + us;
    while (nowUs < until)
      Step();
  }
};

const proto::Vec3 kVoid{-9000.f, 9000.f, -500.f};
} // namespace

TEST_CASE("walking in the city holds nothing and says nothing") {
  Walk walk;
  for (int i = 0; i < 100; ++i) {
    const auto body = walk.Step();
    CHECK_FALSE(body.held);
    CHECK(body.position == walk.at);
  }
  CHECK(walk.tracker.State() == proto::Elsewhere{});
}

TEST_CASE("cyberspace through a teleport: the body stays, settled, until V is back") {
  Walk walk;
  walk.settled = true; // V sat in the netrunner chair
  walk.For(500'000);
  const auto chair = walk.at;
  const auto chairTime = walk.nowUs;
  // Jacked in: the place and the teleport into the void together.
  walk.place = Place::Cyberspace;
  auto body = walk.Jump(kVoid);
  CHECK(body.held);
  CHECK(body.position == chair);
  REQUIRE(body.holdPoseFrom);
  CHECK(*body.holdPoseFrom == chairTime);
  const auto &state = walk.tracker.State();
  CHECK(state.place == Place::Cyberspace);
  CHECK(state.flags == (proto::kElsewhereMoved | proto::kElsewhereSettled));
  CHECK(state.anchor == chair);

  // Walking around cyberspace (and teleporting inside it) keeps the body.
  walk.settled = false;
  walk.For(2'000'000);
  body = walk.Jump({kVoid.x + 400.f, kVoid.y, kVoid.z});
  CHECK(body.held);
  CHECK(body.position == chair);
  CHECK_FALSE(body.holdPoseFrom);
  CHECK(walk.tracker.State().flags == (proto::kElsewhereMoved | proto::kElsewhereSettled));

  // The world ends first, the way back a moment later: still held between.
  walk.place = Place::City;
  body = walk.Step();
  CHECK(body.held);
  CHECK(walk.tracker.State().place == Place::Cyberspace);
  body = walk.Jump({chair.x + 1.f, chair.y, chair.z});
  CHECK_FALSE(body.held);
  CHECK(body.releasePose);
  CHECK(walk.tracker.State() == proto::Elsewhere{});
}

TEST_CASE("a teleport with no world is a teleport in the city") {
  Walk walk;
  walk.For(300'000);
  const auto before = walk.at;
  auto body = walk.Jump({before.x + 2000.f, before.y, before.z});
  CHECK(body.held); // not yet explained
  CHECK(body.position == before);
  CHECK(walk.tracker.State() == proto::Elsewhere{}); // nothing told
  walk.For(elsewhere::kTeleportUs + kFrameUs);
  body = walk.Step();
  CHECK_FALSE(body.held);
  CHECK(body.position == walk.at);
  CHECK(walk.tracker.State() == proto::Elsewhere{});
}

TEST_CASE("a world that starts just after its teleport still keeps the body") {
  Walk walk;
  walk.For(300'000);
  const auto before = walk.at;
  walk.Jump(kVoid);
  walk.For(elsewhere::kTeleportUs + 2 * kFrameUs); // let go as a city teleport
  CHECK_FALSE(walk.Step().held);
  walk.place = Place::Johnny; // the flashback's replacer comes in late
  const auto body = walk.Step();
  CHECK(body.held);
  CHECK(body.position == before);
  CHECK(body.holdPoseFrom);
  CHECK(walk.tracker.State().place == Place::Johnny);
  CHECK(walk.tracker.State().flags == proto::kElsewhereMoved); // V stood: hidden
}

TEST_CASE("a world in place moves nothing; a teleport in it does") {
  Walk walk;
  walk.place = Place::Johnny; // Johnny takes over in the city
  walk.For(1'000'000);
  CHECK(walk.tracker.State() == proto::Elsewhere{.place = Place::Johnny});
  CHECK_FALSE(walk.Step().held);
  // The scene then takes him somewhere else: the body is where he was.
  const auto before = walk.at;
  const auto body = walk.Jump(kVoid);
  CHECK(body.held);
  CHECK(body.position == before);
  CHECK(walk.tracker.State().flags == proto::kElsewhereMoved);
}

TEST_CASE("a vehicle's jumps are not teleports of V") {
  Walk walk;
  walk.mounted = true;
  walk.place = Place::Johnny; // "Johnny drives"
  walk.For(200'000);
  CHECK_FALSE(walk.Jump(kVoid).held);
  CHECK(walk.tracker.State() == proto::Elsewhere{.place = Place::Johnny});
}

TEST_CASE("a braindance in place: the body stays where it began, V flies off as its camera") {
  Walk walk;
  walk.settled = true; // the braindance chair
  walk.For(300'000);
  walk.place = Place::Braindance;
  auto body = walk.Step(0.f);
  const auto chair = walk.at;
  CHECK(body.held);
  CHECK(body.position == chair);
  CHECK(body.holdPoseFrom);
  CHECK(walk.tracker.State().flags == (proto::kElsewhereMoved | proto::kElsewhereSettled));
  // Flying around the scene (no jump): the body stays.
  walk.settled = false;
  for (int i = 0; i < 60; ++i)
    CHECK(walk.Step(0.2f).position == chair);
  // Out of it, back in the chair: let go at once.
  walk.place = Place::City;
  walk.at = chair;
  body = walk.Step(0.f);
  CHECK_FALSE(body.held);
  CHECK(body.releasePose);
  CHECK(walk.tracker.State() == proto::Elsewhere{});
}

TEST_CASE("back from a world far from the body: let go after the grace") {
  Walk walk;
  walk.For(200'000);
  walk.place = Place::Cyberspace;
  walk.Jump(kVoid);
  walk.place = Place::City; // the world ends, V stays put out there
  walk.For(elsewhere::kReturnUs - 3 * kFrameUs);
  CHECK(walk.Step().held);
  walk.For(3 * kFrameUs);
  const auto body = walk.Step();
  CHECK_FALSE(body.held);
  CHECK(walk.tracker.State() == proto::Elsewhere{});
}

TEST_CASE("received states are made safe") {
  using proto::Elsewhere;
  CHECK(elsewhere::Sanitized({.place = Place::City, .flags = 3, .anchor = {1, 2, 3}, .yaw = 1}) ==
        Elsewhere{});
  CHECK(elsewhere::Sanitized({.place = static_cast<Place>(9), .flags = 1}) == Elsewhere{});
  // Settled only with moved; no anchor when not moved.
  CHECK(elsewhere::Sanitized(
            {.place = Place::Braindance, .flags = proto::kElsewhereSettled, .anchor = {1, 2, 3}}) ==
        Elsewhere{.place = Place::Braindance});
  CHECK(elsewhere::Sanitized({.place = Place::Cyberspace, .flags = 0xFF, .anchor = {1, 2, 3}}) ==
        Elsewhere{.place = Place::Cyberspace, .flags = 3, .anchor = {1, 2, 3}});
  // A broken anchor: no place to show a body at.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(elsewhere::Sanitized({.place = Place::Johnny, .flags = 3, .anchor = {nan, 0, 0}}) ==
        Elsewhere{.place = Place::Johnny, .flags = proto::kElsewhereMoved});
}

TEST_CASE("the pose history gives the body's pose as it was") {
  elsewhere::PoseHistory history;
  CHECK_FALSE(history.At(5));
  for (std::uint32_t i = 0; i < 300; ++i) {
    proto::PoseState pose;
    pose.seq = i;
    history.Push(1'000'000 + i * kFrameUs, pose);
  }
  // Only the last kPoseHistoryUs are kept.
  CHECK(history.Size() <= elsewhere::kPoseHistoryUs / kFrameUs + 2);
  const auto newest = 1'000'000 + 299 * kFrameUs;
  CHECK(history.At(newest)->seq == 299);
  CHECK(history.At(newest - 10 * kFrameUs - 1)->seq == 288);
  // Older than kept: the oldest.
  CHECK(history.At(0)->seq == 300 - history.Size());
}
