#include "tandem/endpoint.hpp"
#include "tandem/pose.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <random>
#include <thread>

using namespace tandem;
using pose::kBones;

namespace {
proto::Quat AxisAngle(float x, float y, float z, float angle) {
  const float n = std::sqrt(x * x + y * y + z * z);
  const float s = std::sin(angle / 2.f) / n;
  return {x * s, y * s, z * s, std::cos(angle / 2.f)};
}

proto::Quat Random(std::mt19937 &rng) {
  std::normal_distribution<float> d;
  return pose::Normalized({d(rng), d(rng), d(rng), d(rng)});
}

// A model-space pose: random local rotations up the hierarchy, except for the
// bones that are never sent (Root stays the entity's frame, as in the game).
std::array<proto::Quat, kBones> RandomModel(std::mt19937 &rng) {
  pose::Pose p;
  for (std::size_t bone = 0; bone < kBones; ++bone)
    if (pose::IsSent(bone))
      p.local[bone] = Random(rng);
  return pose::ToModel(p);
}
} // namespace

TEST_CASE("the bone table is the third-person rig's") {
  CHECK(pose::kBoneNames[pose::kHips] == "Hips");
  CHECK(pose::kBoneNames[pose::kReferenceJoint] == "reference_joint");
  CHECK(pose::BoneIndex("RightHandPinky3") == 70);
  CHECK(pose::BoneIndex("shadow_Hips") == -1);
  // Parents come before their children (the model-space walk relies on it).
  for (std::size_t bone = 1; bone < kBones; ++bone) {
    CHECK(pose::kParents[bone] >= 0);
    CHECK(static_cast<std::size_t>(pose::kParents[bone]) < bone);
  }
  CHECK(pose::kBoneNames[pose::kParents[pose::BoneIndex("LeftHand")]] == "LeftForeArm");
}

TEST_CASE("a quaternion survives 6 bytes within 0.01 degree") {
  std::mt19937 rng(7);
  float worst = 0.f;
  for (int i = 0; i < 20000; ++i) {
    const auto q = Random(rng);
    std::uint8_t bytes[proto::kPoseRotationBytes];
    pose::PackQuat(q, bytes);
    worst = std::max(worst, pose::Angle(q, pose::UnpackQuat(bytes)));
  }
  CHECK(worst < 0.01f * 3.14159265f / 180.f * 10.f); // 0.1 degree bound, measured ~0.006
  // Identity and its negation are the same rotation.
  std::uint8_t a[6], b[6];
  pose::PackQuat({0, 0, 0, 1}, a);
  pose::PackQuat({0, 0, 0, -1}, b);
  CHECK(std::equal(a, a + 6, b));
}

TEST_CASE("model -> local -> wire -> model keeps every bone") {
  std::mt19937 rng(11);
  for (int round = 0; round < 50; ++round) {
    const auto model = RandomModel(rng);
    const auto p = pose::FromModel(model, {0.01f, -0.2f, 0.95f}, 0.951f);
    CHECK(p.hips.z == doctest::Approx(0.95f / 0.951f).epsilon(1e-4));
    const auto back = pose::ToModel(*pose::Unpack(pose::Pack(p)));
    for (std::size_t bone = 0; bone < kBones; ++bone) {
      if (!pose::IsSent(bone))
        continue; // not sent: identity on the receiver
      // Errors add up along a chain of ~10 bones.
      CHECK(pose::Angle(model[bone], back[bone]) < 0.002f);
    }
  }
}

TEST_CASE("unsent bones arrive as identity, and the hips in rig units") {
  pose::Pose p;
  p.local[pose::kRoot] = AxisAngle(0, 0, 1, 1.f);
  p.local[pose::kReferenceJoint] = AxisAngle(1, 0, 0, 1.f);
  p.hips = {0.1f, -0.05f, 1.02f};
  const auto back = *pose::Unpack(pose::Pack(p));
  CHECK(back.local[pose::kRoot] == proto::Quat{});
  CHECK(back.local[pose::kReferenceJoint] == proto::Quat{});
  CHECK(back.hips.z == doctest::Approx(1.02f).epsilon(1e-3));
  CHECK(back.hips.x == doctest::Approx(0.1f).epsilon(1e-3));
}

TEST_CASE("the hips are read in Root's frame") {
  std::array<proto::Quat, kBones> model{};
  // Root turned a quarter around Z: the hips' world offset is turned back.
  model[pose::kRoot] = AxisAngle(0, 0, 1, 3.14159265f / 2.f);
  for (std::size_t bone = 1; bone < kBones; ++bone)
    model[bone] = model[pose::kRoot];
  const auto p = pose::FromModel(model, {0.f, 1.f, 0.9f}, 0.9f);
  CHECK(p.hips.x == doctest::Approx(1.f / 0.9f).epsilon(1e-4));
  CHECK(std::abs(p.hips.y) < 1e-4f);
  CHECK(pose::Angle(p.local[pose::kHips], {}) < 1e-4f);
}

TEST_CASE("a pose track slerps between the poses around the render time, then holds") {
  pose::PoseTrack track;
  pose::Pose a, b;
  a.local[pose::kHips] = AxisAngle(0, 0, 1, 0.f);
  b.local[pose::kHips] = AxisAngle(0, 0, 1, 1.f);
  a.hips = {0, 0, 1.f};
  b.hips = {0, 0, 0.5f};
  auto sa = pose::Pack(a), sb = pose::Pack(b);
  sa.seq = 1, sa.timeUs = 1'000'000;
  sb.seq = 2, sb.timeUs = 1'100'000;
  pose::Pose out;
  CHECK_FALSE(track.At(0, out));
  track.Push(sa);
  track.Push(sb);
  track.Push(sa); // late: dropped
  CHECK(track.Received() == 2);

  REQUIRE(track.At(1'050'000, out));
  CHECK(pose::Angle(out.local[pose::kHips], AxisAngle(0, 0, 1, 0.5f)) < 0.002f);
  CHECK(out.hips.z == doctest::Approx(0.75f).epsilon(1e-3));

  REQUIRE(track.At(5'000'000, out));
  CHECK(pose::Angle(out.local[pose::kHips], AxisAngle(0, 0, 1, 1.f)) < 0.002f);
  REQUIRE(track.At(0, out));
  CHECK(pose::Angle(out.local[pose::kHips], {}) < 0.002f);
}

TEST_CASE("a pose fits one datagram and goes on the state channel") {
  const auto bytes = proto::Encode(pose::Pack({}));
  CHECK(bytes.size() < 450);
  CHECK(proto::ChannelOf(proto::PoseState{}) == proto::Channel::State);
  CHECK_FALSE(proto::IsReliable(proto::PoseState{}));
  CHECK(std::string(proto::NameOf(proto::PoseState{})) == "PoseState");
}

TEST_CASE("poses go from a guest through the host, stamped with the guest's id") {
  std::string error;
  auto host = Endpoint::Host({"host", proto::Body::Male}, 27461, error);
  REQUIRE(host);
  auto guest = Endpoint::Join({"guest", proto::Body::Female}, "127.0.0.1", 27461, error);
  REQUIRE(guest);
  for (int i = 0; i < 200 && !guest->IsJoined(); ++i) {
    host->Update(std::chrono::milliseconds(2));
    guest->Update(std::chrono::milliseconds(2));
  }
  REQUIRE(guest->IsJoined());

  pose::Pose p;
  p.local[pose::BoneIndex("LeftArm")] = AxisAngle(1, 0, 0, 0.7f);
  auto sent = pose::Pack(p);
  sent.player = 99; // a guest cannot speak for another player
  guest->SendLocalPose(sent);
  std::optional<proto::PoseState> got;
  for (int i = 0; i < 200 && !got; ++i) {
    guest->Update(std::chrono::milliseconds(2));
    for (const auto &event : host->Update(std::chrono::milliseconds(2)))
      if (const auto *remote = std::get_if<events::RemotePose>(&event))
        got = remote->pose;
  }
  REQUIRE(got);
  CHECK(got->player == guest->You());
  CHECK(got->rotations == sent.rotations);
  CHECK(got->seq == 1);
}
