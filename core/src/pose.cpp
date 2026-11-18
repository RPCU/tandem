#include "tandem/pose.hpp"

#include <algorithm>
#include <cmath>

namespace tandem::pose {
// Generated from docs/research/data/skeletons-2.50.txt (the male rig; the female
// rig has the same 71 bones and parents).
const std::array<std::string_view, kBones> kBoneNames = {
    "Root",
    "Trajectory",
    "Hips",
    "reference_joint",
    "Spine",
    "LeftUpLeg",
    "RightUpLeg",
    "Spine1",
    "LeftLeg",
    "RightLeg",
    "Spine2",
    "LeftFoot",
    "RightFoot",
    "Spine3",
    "LeftShoulder",
    "RightShoulder",
    "Neck",
    "LeftArm",
    "RightArm",
    "Neck1",
    "LeftForeArm",
    "RightForeArm",
    "Head",
    "LeftHand",
    "RightHand",
    "WeaponLeft",
    "WeaponRight",
    "LeftHeel",
    "RightHeel",
    "LeftEye",
    "RightEye",
    "LeftInHandThumb",
    "LeftInHandIndex",
    "LeftInHandMiddle",
    "LeftInHandRing",
    "LeftInHandPinky",
    "RightInHandThumb",
    "RightInHandIndex",
    "RightInHandMiddle",
    "RightInHandRing",
    "RightInHandPinky",
    "LeftToeBase",
    "RightToeBase",
    "LeftHandThumb1",
    "LeftHandIndex1",
    "LeftHandMiddle1",
    "LeftHandRing1",
    "LeftHandPinky1",
    "RightHandThumb1",
    "RightHandIndex1",
    "RightHandMiddle1",
    "RightHandRing1",
    "RightHandPinky1",
    "LeftHandThumb2",
    "LeftHandIndex2",
    "LeftHandMiddle2",
    "LeftHandRing2",
    "LeftHandPinky2",
    "RightHandThumb2",
    "RightHandIndex2",
    "RightHandMiddle2",
    "RightHandRing2",
    "RightHandPinky2",
    "LeftHandIndex3",
    "LeftHandMiddle3",
    "LeftHandRing3",
    "LeftHandPinky3",
    "RightHandIndex3",
    "RightHandMiddle3",
    "RightHandRing3",
    "RightHandPinky3",
};

const std::array<std::int8_t, kBones> kParents = {
    -1, 0,  0,  0,  2,  2,  2,  4,  5,  6,  7,  8,  9,  10, 13, 13, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 23, 24, 11, 12, 22, 22, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 27, 28, 31, 32, 33, 34, 35,
    36, 37, 38, 39, 40, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 54, 55, 56, 57, 59, 60, 61, 62,
};

namespace {
constexpr float kInvSqrt2 = 0.70710678118654752f;
constexpr int kQuatBits = 15;
constexpr std::uint32_t kQuatMax = (1u << kQuatBits) - 1;

proto::Vec3 Rotate(const proto::Quat &q, const proto::Vec3 &v) {
  const proto::Quat p{v.x, v.y, v.z, 0.f};
  const auto r = Multiply(Multiply(q, p), Conjugate(q));
  return {r.i, r.j, r.k};
}

std::int16_t ToFixed(float value) {
  const float scaled = std::round(value * proto::kPoseHipsScale);
  return static_cast<std::int16_t>(std::clamp(scaled, -32767.f, 32767.f));
}
} // namespace

int BoneIndex(std::string_view name) {
  for (std::size_t i = 0; i < kBones; ++i)
    if (kBoneNames[i] == name)
      return static_cast<int>(i);
  return -1;
}

proto::Quat Normalized(const proto::Quat &q) {
  const float length = std::sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.r * q.r);
  if (!(length > 1e-6f) || !std::isfinite(length))
    return {};
  return {q.i / length, q.j / length, q.k / length, q.r / length};
}

proto::Quat Multiply(const proto::Quat &a, const proto::Quat &b) {
  return {
      a.r * b.i + a.i * b.r + a.j * b.k - a.k * b.j, a.r * b.j - a.i * b.k + a.j * b.r + a.k * b.i,
      a.r * b.k + a.i * b.j - a.j * b.i + a.k * b.r, a.r * b.r - a.i * b.i - a.j * b.j - a.k * b.k};
}

proto::Quat Conjugate(const proto::Quat &q) { return {-q.i, -q.j, -q.k, q.r}; }

proto::Quat Slerp(const proto::Quat &a, const proto::Quat &bIn, float t) {
  auto b = bIn;
  float d = a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r;
  if (d < 0.f) {
    b = {-b.i, -b.j, -b.k, -b.r};
    d = -d;
  }
  float wa = 1.f - t, wb = t;
  if (d < 0.9995f) {
    const float theta = std::acos(std::min(d, 1.f));
    const float s = std::sin(theta);
    wa = std::sin((1.f - t) * theta) / s;
    wb = std::sin(t * theta) / s;
  }
  return Normalized(
      {wa * a.i + wb * b.i, wa * a.j + wb * b.j, wa * a.k + wb * b.k, wa * a.r + wb * b.r});
}

float Angle(const proto::Quat &a, const proto::Quat &b) {
  const float d = std::abs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r);
  return 2.f * std::acos(std::min(d, 1.f));
}

Pose FromModel(std::span<const proto::Quat, kBones> model, proto::Vec3 hipsPosition,
               float referenceHipsHeight) {
  Pose pose;
  for (std::size_t bone = 0; bone < kBones; ++bone) {
    const int parent = kParents[bone];
    const auto q = Normalized(model[bone]);
    pose.local[bone] =
        parent < 0 ? q : Normalized(Multiply(Conjugate(Normalized(model[parent])), q));
  }
  // The hips hang from Root: their position in Root's frame.
  const auto root = Normalized(model[kRoot]);
  auto hips = Rotate(Conjugate(root), hipsPosition);
  const float scale = referenceHipsHeight > 0.1f ? 1.f / referenceHipsHeight : 1.f;
  pose.hips = {hips.x * scale, hips.y * scale, hips.z * scale};
  return pose;
}

std::array<proto::Quat, kBones> ToModel(const Pose &pose) {
  std::array<proto::Quat, kBones> model{};
  for (std::size_t bone = 0; bone < kBones; ++bone) {
    const int parent = kParents[bone];
    model[bone] =
        parent < 0 ? pose.local[bone] : Normalized(Multiply(model[parent], pose.local[bone]));
  }
  return model;
}

void PackQuat(const proto::Quat &qIn, std::uint8_t *out) {
  auto q = Normalized(qIn);
  const float c[4] = {q.i, q.j, q.k, q.r};
  int largest = 0;
  for (int i = 1; i < 4; ++i)
    if (std::abs(c[i]) > std::abs(c[largest]))
      largest = i;
  // q and -q are the same rotation: make the dropped component positive.
  const float sign = c[largest] < 0.f ? -1.f : 1.f;
  std::uint64_t bits = static_cast<std::uint64_t>(largest);
  int shift = 2;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    // [-1/sqrt2, 1/sqrt2] -> [0, kQuatMax]
    const float unit = std::clamp((c[i] * sign / kInvSqrt2 + 1.f) * 0.5f, 0.f, 1.f);
    bits |= static_cast<std::uint64_t>(std::lround(unit * kQuatMax)) << shift;
    shift += kQuatBits;
  }
  for (std::size_t byte = 0; byte < proto::kPoseRotationBytes; ++byte)
    out[byte] = static_cast<std::uint8_t>(bits >> (8 * byte));
}

proto::Quat UnpackQuat(const std::uint8_t *in) {
  std::uint64_t bits = 0;
  for (std::size_t byte = 0; byte < proto::kPoseRotationBytes; ++byte)
    bits |= static_cast<std::uint64_t>(in[byte]) << (8 * byte);
  const int largest = static_cast<int>(bits & 3);
  float c[4]{};
  float sum = 0.f;
  int shift = 2;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    const auto value = static_cast<float>((bits >> shift) & kQuatMax) / kQuatMax;
    c[i] = (value * 2.f - 1.f) * kInvSqrt2;
    sum += c[i] * c[i];
    shift += kQuatBits;
  }
  c[largest] = std::sqrt(std::max(0.f, 1.f - sum));
  return Normalized({c[0], c[1], c[2], c[3]});
}

proto::PoseState Pack(const Pose &pose) {
  proto::PoseState state;
  state.hips = {ToFixed(pose.hips.x), ToFixed(pose.hips.y), ToFixed(pose.hips.z)};
  std::size_t slot = 0;
  for (std::size_t bone = 0; bone < kBones; ++bone) {
    if (!IsSent(bone))
      continue;
    PackQuat(pose.local[bone], state.rotations.data() + slot * proto::kPoseRotationBytes);
    ++slot;
  }
  return state;
}

std::optional<Pose> Unpack(const proto::PoseState &state) {
  Pose pose;
  pose.hips = {state.hips[0] / proto::kPoseHipsScale, state.hips[1] / proto::kPoseHipsScale,
               state.hips[2] / proto::kPoseHipsScale};
  std::size_t slot = 0;
  for (std::size_t bone = 0; bone < kBones; ++bone) {
    if (!IsSent(bone))
      continue; // identity
    const auto q = UnpackQuat(state.rotations.data() + slot * proto::kPoseRotationBytes);
    if (!std::isfinite(q.r))
      return std::nullopt;
    pose.local[bone] = q;
    ++slot;
  }
  return pose;
}

void PoseTrack::Push(const proto::PoseState &state) {
  // Newest wins; a late or repeated pose is dropped (32-bit wrap-safe).
  if (m_haveSeq && static_cast<std::int32_t>(state.seq - m_lastSeq) <= 0)
    return;
  auto pose = Unpack(state);
  if (!pose)
    return;
  m_lastSeq = state.seq;
  m_haveSeq = true;
  ++m_received;
  if (!m_samples.empty() && state.timeUs <= m_samples.back().timeUs)
    m_samples.clear(); // the sender's clock jumped: start over
  m_samples.push_back({state.timeUs, *pose});
  while (m_samples.size() > kMaxSamples)
    m_samples.pop_front();
}

bool PoseTrack::At(std::uint64_t renderUs, Pose &out) const {
  if (m_samples.empty())
    return false;
  if (renderUs <= m_samples.front().timeUs) {
    out = m_samples.front().pose;
    return true;
  }
  for (std::size_t i = 1; i < m_samples.size(); ++i) {
    const auto &b = m_samples[i];
    if (renderUs > b.timeUs)
      continue;
    const auto &a = m_samples[i - 1];
    const float t =
        static_cast<float>(renderUs - a.timeUs) / static_cast<float>(b.timeUs - a.timeUs);
    for (std::size_t bone = 0; bone < kBones; ++bone)
      out.local[bone] = Slerp(a.pose.local[bone], b.pose.local[bone], t);
    out.hips = {a.pose.hips.x + (b.pose.hips.x - a.pose.hips.x) * t,
                a.pose.hips.y + (b.pose.hips.y - a.pose.hips.y) * t,
                a.pose.hips.z + (b.pose.hips.z - a.pose.hips.z) * t};
    return true;
  }
  out = m_samples.back().pose; // past the newest: hold
  return true;
}
} // namespace tandem::pose
