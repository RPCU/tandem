#pragma once

// A V's body pose on the wire (docs/research/pose-streaming.md).
//
// The pose is expressed on the third-person body rig: its 71 bones, the same
// names and parents for the male and the female rig, and all present in the
// first-person player rig the sender animates. What travels is each bone's
// rotation relative to its parent (local), so the receiving body keeps its
// own bone lengths, plus the hips' position relative to the rig's reference
// hips height, so the feet stay on the ground on a taller or shorter body.
//
// Root, Trajectory and reference_joint are not sent: the entity is placed by
// its movement controller, and reference_joint carries a rig-specific frame.

#include "tandem/protocol.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string_view>

namespace tandem::pose {
inline constexpr std::size_t kBones = 71;

// The third-person body rig, in its own order (game 2.50, rigs
// 14264052059588999261 and 10037276762472186253).
extern const std::array<std::string_view, kBones> kBoneNames;
extern const std::array<std::int8_t, kBones> kParents; // -1: none

inline constexpr int kRoot = 0, kTrajectory = 1, kHips = 2, kReferenceJoint = 3;

// A bone is sent unless it is one of the three above.
constexpr bool IsSent(std::size_t bone) {
  return bone != kRoot && bone != kTrajectory && bone != kReferenceJoint;
}
inline constexpr std::size_t kSentBones = kBones - 3;
static_assert(kSentBones == proto::kPoseBones);

// Index of a bone name, or -1.
int BoneIndex(std::string_view name);

struct Pose {
  std::array<proto::Quat, kBones> local{}; // rotation relative to the parent bone
  proto::Vec3 hips{};                      // hips position / the rig's reference hips height
};

// From model-space rotations and the hips' model-space position (both in
// kBoneNames order) and the sender rig's reference hips height (metres).
Pose FromModel(std::span<const proto::Quat, kBones> model, proto::Vec3 hipsPosition,
               float referenceHipsHeight);

// Model-space rotations from local ones (for tests and checks).
std::array<proto::Quat, kBones> ToModel(const Pose &pose);

// Wire form. Unpack returns nullopt on a malformed rotation.
proto::PoseState Pack(const Pose &pose);
std::optional<Pose> Unpack(const proto::PoseState &state);

// Smallest-three quaternion in 6 bytes: index of the largest component (2
// bits) and the other three at 15 bits each.
void PackQuat(const proto::Quat &q, std::uint8_t *out);
proto::Quat UnpackQuat(const std::uint8_t *in);

proto::Quat Normalized(const proto::Quat &q);
proto::Quat Multiply(const proto::Quat &a, const proto::Quat &b);
proto::Quat Conjugate(const proto::Quat &q);
proto::Quat Slerp(const proto::Quat &a, const proto::Quat &b, float t);
// Angle between two rotations, radians.
float Angle(const proto::Quat &a, const proto::Quat &b);

// Leans the upper body (from Spine1 up) by `pitch` forward and `roll` to
// its right, radians, about the body's own axes (model space: X right, Y
// forward, Z up), whatever the pose: a hit puppet's flinch (presence.hpp).
// The hips and legs stay where they are.
void LeanSpine(Pose &pose, float pitch, float roll);

// A remote player's recent poses, drawn at a moment in the past (the same
// delay as their position, see interpolation.hpp) between the two around it.
// Past the newest pose it is held.
class PoseTrack {
public:
  static constexpr std::size_t kMaxSamples = 16;

  void Push(const proto::PoseState &state);
  // The pose at `renderUs` (host clock). False before the first pose.
  bool At(std::uint64_t renderUs, Pose &out) const;
  std::uint32_t Received() const { return m_received; }

private:
  struct Sample {
    std::uint64_t timeUs;
    Pose pose;
  };
  std::deque<Sample> m_samples;
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};
  std::uint32_t m_received{};
};
} // namespace tandem::pose
