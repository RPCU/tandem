#include "tandem/interpolation.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace tandem;
using sync::Track;

namespace {
constexpr std::uint64_t kStart = 1'800'000'000'000'000; // an epoch-sized time: floats would break
constexpr std::uint64_t kStep = 33'333;                 // 30 Hz

proto::PlayerState StateAt(std::uint32_t seq, float x, float yaw = 0.f, float vx = 0.f) {
  proto::PlayerState s;
  s.seq = seq;
  s.timeUs = kStart + seq * kStep;
  s.position = {x, 0.f, 0.f};
  s.orientation = sync::FromYaw(yaw);
  s.velocity = {vx, 0.f, 0.f};
  return s;
}

// Walking along +X at 3 m/s, states arriving after `transit`.
Track Walk(std::uint32_t count, std::uint64_t transit = 20'000) {
  Track track;
  for (std::uint32_t i = 1; i <= count; ++i) {
    const auto s = StateAt(i, 3.f * i * kStep / 1e6f, 0.f, 3.f);
    track.Push(s, s.timeUs + transit);
  }
  return track;
}
} // namespace

TEST_CASE("yaw survives a quaternion round trip") {
  for (float yaw : {0.f, 0.5f, -1.2f, 3.f}) {
    CHECK(sync::YawOf(sync::FromYaw(yaw)) == doctest::Approx(yaw).epsilon(1e-4));
  }
}

TEST_CASE("an empty track has nothing to draw") {
  Track track;
  CHECK_FALSE(track.At(kStart).valid);
}

TEST_CASE("the body is drawn between two states, in the past by the delay") {
  auto track = Walk(30);
  const auto now = kStart + 20 * kStep;
  const auto view = track.At(now);
  REQUIRE(view.valid);
  CHECK_FALSE(view.extrapolated);
  const float expected = 3.f * static_cast<float>(now - track.DelayUs() - kStart) / 1e6f;
  CHECK(view.position.x == doctest::Approx(expected).epsilon(1e-3));
  // The delay stays near the 65 ms base on a clean link.
  CHECK(track.DelayUs() >= 65'000);
  CHECK(track.DelayUs() < 120'000);
}

TEST_CASE("smoothed speed settles on the walking speed") {
  auto track = Walk(60);
  sync::View view;
  for (std::uint64_t t = kStart + 10 * kStep; t < kStart + 55 * kStep; t += 16'000)
    view = track.At(t);
  CHECK(view.speed == doctest::Approx(3.f).epsilon(0.05));
}

TEST_CASE("past the last state it extrapolates briefly, then holds") {
  auto track = Walk(10);
  const auto lastTime = kStart + 10 * kStep;
  const auto lastX = 3.f * 10 * kStep / 1e6f;

  const auto soon = track.At(lastTime + track.DelayUs() + 100'000);
  CHECK(soon.extrapolated);
  CHECK(soon.position.x == doctest::Approx(lastX + 0.3f).epsilon(1e-3));

  const auto later = track.At(lastTime + track.DelayUs() + 2'000'000);
  CHECK(later.position.x ==
        doctest::Approx(lastX + 3.f * Track::kExtrapolateUs / 1e6f).epsilon(1e-3));
}

TEST_CASE("late and duplicate states are dropped") {
  Track track;
  track.Push(StateAt(5, 5.f), kStart + 5 * kStep);
  track.Push(StateAt(4, 4.f), kStart + 5 * kStep); // older
  track.Push(StateAt(5, 5.f), kStart + 5 * kStep); // duplicate
  proto::PlayerState never = StateAt(6, 6.f);
  never.timeUs = 0; // "never moved"
  track.Push(never, kStart + 6 * kStep);
  CHECK(track.Stats().received == 1);
  CHECK(track.Stats().dropped == 3);
}

TEST_CASE("jitter widens the delay, within the cap") {
  Track track;
  for (std::uint32_t i = 1; i <= 120; ++i) {
    const auto s = StateAt(i, static_cast<float>(i));
    const std::uint64_t transit = (i % 2) ? 20'000 : 90'000; // 70 ms of swing
    track.Push(s, s.timeUs + transit);
  }
  CHECK(track.DelayUs() > 120'000);
  CHECK(track.DelayUs() <= Track::kMaxDelayUs);
}

TEST_CASE("turning across ±pi takes the short way") {
  Track track;
  constexpr float pi = std::numbers::pi_v<float>;
  track.Push(StateAt(1, 0.f, pi - 0.1f), kStart + kStep);
  track.Push(StateAt(2, 0.f, -pi + 0.1f), kStart + 2 * kStep);
  const auto mid = kStart + kStep + kStep / 2 + track.DelayUs();
  const float yaw = track.At(mid).yaw;
  CHECK(std::abs(std::abs(yaw) - pi) < 0.05f);
}

TEST_CASE("a jump puts the body there at once, standing still") {
  auto track = Walk(20);
  CHECK(track.At(kStart + 20 * kStep).jumps == 0);
  // Fast travel: 2 km away on the next state, with the sender's speed across it.
  auto far = StateAt(21, 2000.f, 0.f, 60000.f);
  track.Push(far, far.timeUs + 20'000);
  const auto view = track.At(far.timeUs + 30'000);
  REQUIRE(view.valid);
  CHECK(view.jumps == 1);
  CHECK(view.position.x == doctest::Approx(2000.f));
  // No sprint across the map, and no extrapolation along that speed.
  CHECK(view.speed == doctest::Approx(0.f));
  const auto later = track.At(far.timeUs + track.DelayUs() + 200'000);
  CHECK(later.position.x == doctest::Approx(2000.f));
  // It walks on from there.
  for (std::uint32_t i = 22; i <= 40; ++i) {
    const auto s = StateAt(i, 2000.f + 3.f * static_cast<float>(i - 21) * kStep / 1e6f, 0.f, 3.f);
    track.Push(s, s.timeUs + 20'000);
  }
  const auto walking = track.At(kStart + 35 * kStep);
  CHECK(walking.jumps == 1);
  CHECK(walking.position.x > 2000.f);
}

TEST_CASE("a fast vehicle or a fast-forwarded train is not a jump") {
  Track track;
  // 250 m/s (a train at ten times its speed): 8 m between two states.
  for (std::uint32_t i = 1; i <= 30; ++i) {
    const auto s = StateAt(i, 250.f * static_cast<float>(i) * kStep / 1e6f, 0.f, 250.f);
    track.Push(s, s.timeUs + 20'000);
  }
  CHECK(track.At(kStart + 25 * kStep).jumps == 0);
  // A car at 40 m/s, then two seconds of silence: 80 m, not a jump.
  auto after = StateAt(91, 250.f * 30 * kStep / 1e6f + 80.f, 0.f, 40.f);
  track.Push(after, after.timeUs + 20'000);
  CHECK(track.At(after.timeUs).jumps == 0);
}
