#pragma once

// Synthetic motion for the netcode tests (budget_test.cpp, quality_test.cpp):
// a V walking, turning and stopping, a car on a road, a body's walk cycle.
// Times are host-clock microseconds.

#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"
#include "tandem/protocol.hpp"

#include <cmath>
#include <numbers>
#include <string_view>

namespace tandem::test {
inline constexpr std::uint64_t kEpoch = 1'800'000'000'000'000; // epoch-sized: floats would break
inline constexpr float kPi = std::numbers::pi_v<float>;

inline float Seconds(std::uint64_t us) { return static_cast<float>(us - kEpoch) / 1e6f; }

// A V on foot: walks (1.6 m/s) along a gentle curve, runs (4 m/s) now and
// then, stops for a second every 12 s. Position and velocity at `t`.
struct Walker {
  float Speed(float s) const {
    const float phase = std::fmod(s, 12.f);
    if (phase > 11.f)
      return 0.f; // standing
    return phase > 6.f && phase < 9.f ? 4.f : 1.6f;
  }
  // Distance along the path: the integral of Speed (piecewise).
  float Along(float s) const {
    const float laps = std::floor(s / 12.f);
    const float phase = s - laps * 12.f;
    const float lap = 6.f * 1.6f + 3.f * 4.f + 2.f * 1.6f; // 24.8 m
    float d = laps * lap;
    d += std::min(phase, 6.f) * 1.6f;
    if (phase > 6.f)
      d += (std::min(phase, 9.f) - 6.f) * 4.f;
    if (phase > 9.f)
      d += (std::min(phase, 11.f) - 9.f) * 1.6f;
    return d;
  }
  // A circle of 40 m radius: a gentle, steady turn.
  proto::Vec3 At(float s) const {
    const float a = Along(s) / 40.f;
    return {40.f * std::sin(a), 40.f * (1.f - std::cos(a)), 0.f};
  }
  proto::Vec3 Velocity(float s) const {
    const float a = Along(s) / 40.f, v = Speed(s);
    return {v * std::cos(a), v * std::sin(a), 0.f};
  }
  float Yaw(float s) const { return -Along(s) / 40.f; }
  proto::PlayerState State(std::uint64_t t) const {
    const float s = Seconds(t);
    proto::PlayerState state;
    state.timeUs = t;
    state.position = At(s);
    state.velocity = Velocity(s);
    state.orientation = sync::FromYaw(Yaw(s));
    return state;
  }
};

// A car: 20 m/s round a 60 m circle, braking to 5 m/s and back every 10 s.
struct Car {
  float Speed(float s) const {
    const float phase = std::fmod(s, 10.f);
    if (phase < 7.f)
      return 20.f;
    if (phase < 8.f)
      return 20.f - 15.f * (phase - 7.f); // braking at 15 m/s²
    return 5.f + 7.5f * (phase - 8.f);    // and away again
  }
  float Along(float s) const {
    const float laps = std::floor(s / 10.f);
    const float p = s - laps * 10.f;
    const float lap = 7.f * 20.f + (20.f - 7.5f) + (2.f * 5.f + 7.5f * 2.f); // 177.5 m
    float d = laps * lap + std::min(p, 7.f) * 20.f;
    if (p > 7.f) {
      const float b = std::min(p, 8.f) - 7.f;
      d += 20.f * b - 7.5f * b * b;
    }
    if (p > 8.f) {
      const float c = p - 8.f;
      d += 5.f * c + 3.75f * c * c;
    }
    return d;
  }
  proto::Vec3 At(float s) const {
    const float a = Along(s) / 60.f;
    return {60.f * std::cos(a), 60.f * std::sin(a), 0.f};
  }
  proto::VehicleState State(std::uint64_t t) const {
    const float s = Seconds(t), a = Along(s) / 60.f, v = Speed(s);
    proto::VehicleState state;
    state.timeUs = t;
    state.position = At(s);
    state.velocity = {-v * std::sin(a), v * std::cos(a), 0.f};
    state.orientation = sync::FromYaw(a);
    return state;
  }
};

// A body's walk cycle on the third-person rig: legs, arms, spine and head
// swing (stride 1 Hz), the fingers hold still (a fist, or a gun's grip),
// the eyes dart now and then. `energy` 0: standing (breathing only).
inline pose::Pose WalkCycle(float s, float energy = 1.f) {
  pose::Pose p;
  for (auto &q : p.local)
    q = {0.f, 0.f, 0.f, 1.f};
  const auto swing = [&](std::string_view bone, float amplitude, float phase, int axis) {
    const int index = pose::BoneIndex(bone);
    if (index < 0)
      return;
    const float angle = amplitude * std::sin(2.f * kPi * s + phase);
    proto::Quat q{0.f, 0.f, 0.f, std::cos(angle / 2.f)};
    (axis == 0 ? q.i : axis == 1 ? q.j : q.k) = std::sin(angle / 2.f);
    p.local[static_cast<std::size_t>(index)] = q;
  };
  const float e = energy, b = 0.02f; // breathing
  swing("LeftUpLeg", 0.5f * e, 0.f, 0);
  swing("RightUpLeg", 0.5f * e, kPi, 0);
  swing("LeftLeg", 0.6f * e, 0.5f, 0);
  swing("RightLeg", 0.6f * e, kPi + 0.5f, 0);
  swing("LeftFoot", 0.3f * e, 1.f, 0);
  swing("RightFoot", 0.3f * e, kPi + 1.f, 0);
  swing("LeftToeBase", 0.2f * e, 1.5f, 0);
  swing("RightToeBase", 0.2f * e, kPi + 1.5f, 0);
  swing("LeftArm", 0.3f * e, kPi, 0);
  swing("RightArm", 0.3f * e, 0.f, 0);
  swing("LeftForeArm", 0.3f * e + b, kPi + 0.3f, 0);
  swing("RightForeArm", 0.3f * e + b, 0.3f, 0);
  swing("LeftHand", 0.1f * e, kPi, 2);
  swing("RightHand", 0.1f * e, 0.f, 2);
  swing("LeftShoulder", 0.05f * e + b, 0.f, 1);
  swing("RightShoulder", 0.05f * e + b, kPi, 1);
  swing("Spine", 0.05f * e + b, 0.f, 2);
  swing("Spine1", 0.05f * e + b, 0.2f, 2);
  swing("Spine2", 0.05f * e + b, 0.4f, 2);
  swing("Spine3", 0.05f * e + b, 0.6f, 0);
  swing("Neck", 0.05f * e + b, 0.8f, 2);
  swing("Neck1", 0.03f * e + b, 1.f, 2);
  swing("Head", 0.2f * e + 0.05f, 0.1f, 2); // looks around
  swing("LeftHeel", 0.2f * e, 0.f, 0);
  swing("RightHeel", 0.2f * e, kPi, 0);
  // The eyes: a saccade every second.
  const float dart = std::floor(s) * 0.37f;
  for (const auto *eye : {"LeftEye", "RightEye"})
    if (const int index = pose::BoneIndex(eye); index >= 0)
      p.local[static_cast<std::size_t>(index)] = sync::FromYaw(0.1f * std::sin(dart * 7.f));
  p.hips = {0.f, 0.02f * e * std::sin(4.f * kPi * s), 1.f + 0.02f * e * std::cos(4.f * kPi * s)};
  return p;
}
} // namespace tandem::test
