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

void PackQuat5(const proto::Quat &qIn, std::uint8_t *out) {
  auto q = Normalized(qIn);
  const float c[4] = {q.i, q.j, q.k, q.r};
  int largest = 0;
  for (int i = 1; i < 4; ++i)
    if (std::abs(c[i]) > std::abs(c[largest]))
      largest = i;
  const float sign = c[largest] < 0.f ? -1.f : 1.f;
  constexpr int kBits[3] = {13, 13, 12};
  std::uint64_t bits = static_cast<std::uint64_t>(largest);
  int shift = 2, n = 0;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    const auto max = (1u << kBits[n]) - 1;
    const float unit = std::clamp((c[i] * sign / kInvSqrt2 + 1.f) * 0.5f, 0.f, 1.f);
    bits |= static_cast<std::uint64_t>(std::lround(unit * static_cast<float>(max))) << shift;
    shift += kBits[n++];
  }
  for (std::size_t byte = 0; byte < proto::kPoseFrameRotationBytes; ++byte)
    out[byte] = static_cast<std::uint8_t>(bits >> (8 * byte));
}

proto::Quat UnpackQuat5(const std::uint8_t *in) {
  std::uint64_t bits = 0;
  for (std::size_t byte = 0; byte < proto::kPoseFrameRotationBytes; ++byte)
    bits |= static_cast<std::uint64_t>(in[byte]) << (8 * byte);
  constexpr int kBits[3] = {13, 13, 12};
  const int largest = static_cast<int>(bits & 3);
  float c[4]{};
  float sum = 0.f;
  int shift = 2, n = 0;
  for (int i = 0; i < 4; ++i) {
    if (i == largest)
      continue;
    const auto max = (1u << kBits[n]) - 1;
    const auto value = static_cast<float>((bits >> shift) & max) / static_cast<float>(max);
    c[i] = (value * 2.f - 1.f) * kInvSqrt2;
    sum += c[i] * c[i];
    shift += kBits[n++];
  }
  c[largest] = std::sqrt(std::max(0.f, 1.f - sum));
  return Normalized({c[0], c[1], c[2], c[3]});
}

// A small turn from `from` to `to` as a rotation vector, in kPoseTurnStep
// steps; false when it does not fit a byte each way.
bool TurnOf(const proto::Quat &from, const proto::Quat &to, std::int8_t *out) {
  auto d = Multiply(Conjugate(from), to);
  if (d.r < 0.f)
    d = {-d.i, -d.j, -d.k, -d.r};
  const float s = std::sqrt(d.i * d.i + d.j * d.j + d.k * d.k);
  const float scale = s < 1e-7f ? 2.f : 2.f * std::atan2(s, d.r) / s;
  const float v[3] = {d.i * scale, d.j * scale, d.k * scale};
  for (int axis = 0; axis < 3; ++axis) {
    const float steps = std::round(v[axis] / proto::kPoseTurnStep);
    if (std::abs(steps) > 127.f)
      return false;
    out[axis] = static_cast<std::int8_t>(steps);
  }
  return true;
}

proto::Quat Turned(const proto::Quat &from, const std::int8_t *turn) {
  const float v[3] = {turn[0] * proto::kPoseTurnStep, turn[1] * proto::kPoseTurnStep,
                      turn[2] * proto::kPoseTurnStep};
  const float angle = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (angle < 1e-9f)
    return from;
  const float s = std::sin(angle / 2.f) / angle;
  return Normalized(Multiply(from, {v[0] * s, v[1] * s, v[2] * s, std::cos(angle / 2.f)}));
}

namespace {
void SetCode(proto::PoseFrame &frame, std::size_t slot, std::uint8_t code) {
  frame.codes[slot / 4] |= static_cast<std::uint8_t>(code << (2 * (slot % 4)));
}
} // namespace

proto::PoseFrame PoseEncoder::Encode(const proto::PoseState &pose) {
  proto::PoseFrame frame{
      .player = pose.player, .seq = pose.seq, .timeUs = pose.timeUs, .hips = pose.hips};
  const bool key = !m_haveKey || m_sinceKey + 1 >= m_everyFrames || pose.timeUs < m_keyUs ||
                   pose.timeUs - m_keyUs >= m_everyUs;
  frame.key = key ? pose.seq : m_keySeq;
  frame.rotations.reserve(proto::kPoseBones * proto::kPoseFrameRotationBytes);
  std::uint8_t packed[proto::kPoseFrameRotationBytes];
  std::int8_t turn[3];
  for (std::size_t slot = 0; slot < proto::kPoseBones; ++slot) {
    const auto q = UnpackQuat(pose.rotations.data() + slot * proto::kPoseRotationBytes);
    if (!key && Angle(q, m_key[slot]) <= m_threshold)
      continue; // kBoneKept
    if (!key && TurnOf(m_key[slot], q, turn)) {
      SetCode(frame, slot, proto::kBoneTurn);
      frame.rotations.insert(frame.rotations.end(), reinterpret_cast<std::uint8_t *>(turn),
                             reinterpret_cast<std::uint8_t *>(turn) + proto::kPoseTurnBytes);
      continue;
    }
    PackQuat5(q, packed);
    if (key)
      m_key[slot] = UnpackQuat5(packed); // what the receiver will have
    SetCode(frame, slot, proto::kBoneWhole);
    frame.rotations.insert(frame.rotations.end(), packed, packed + sizeof packed);
  }
  ++m_frames;
  if (key) {
    m_haveKey = true;
    m_keySeq = pose.seq;
    m_keyUs = pose.timeUs;
    m_sinceKey = 0;
    ++m_keys;
  } else {
    ++m_sinceKey;
  }
  return frame;
}

std::optional<proto::PoseState> PoseDecoder::Decode(const proto::PoseFrame &frame) {
  const Key *base = nullptr;
  const bool key = frame.key == frame.seq;
  if (!key) {
    // Its own key, else the newest we have (by sequence, wrap-safe).
    for (std::size_t i = 0; i < m_count; ++i)
      if (m_keys[i].seq == frame.key)
        base = &m_keys[i];
    if (!base) {
      for (std::size_t i = 0; i < m_count; ++i)
        if (!base || static_cast<std::int32_t>(m_keys[i].seq - base->seq) > 0)
          base = &m_keys[i];
      if (!base) {
        ++m_dropped;
        return std::nullopt;
      }
      ++m_otherKey;
    }
  }
  Key decoded{.seq = frame.seq};
  std::size_t at = 0;
  for (std::size_t slot = 0; slot < proto::kPoseBones; ++slot) {
    const auto code = frame.Code(slot);
    if (code == proto::kBoneWhole) {
      if (at + proto::kPoseFrameRotationBytes > frame.rotations.size())
        return std::nullopt;
      decoded.bones[slot] = UnpackQuat5(frame.rotations.data() + at);
      at += proto::kPoseFrameRotationBytes;
    } else if (!base) {
      return std::nullopt; // a key with a bone missing: not ours to guess
    } else if (code == proto::kBoneTurn) {
      if (at + proto::kPoseTurnBytes > frame.rotations.size())
        return std::nullopt;
      decoded.bones[slot] = Turned(
          base->bones[slot], reinterpret_cast<const std::int8_t *>(frame.rotations.data() + at));
      at += proto::kPoseTurnBytes;
    } else {
      decoded.bones[slot] = base->bones[slot];
    }
  }
  proto::PoseState state{
      .player = frame.player, .seq = frame.seq, .timeUs = frame.timeUs, .hips = frame.hips};
  for (std::size_t slot = 0; slot < proto::kPoseBones; ++slot)
    PackQuat(decoded.bones[slot], state.rotations.data() + slot * proto::kPoseRotationBytes);
  if (key) {
    m_keys[m_next] = decoded;
    m_next = (m_next + 1) % kKeys;
    m_count = std::min(m_count + 1, kKeys);
  }
  return state;
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

proto::ActorPose ForActor(proto::NpcId npc, const proto::PoseState &state) {
  return {.npc = npc,
          .seq = state.seq,
          .timeUs = state.timeUs,
          .hips = state.hips,
          .rotations = state.rotations};
}

proto::PoseState FromActor(const proto::ActorPose &pose) {
  return {.player = proto::kNoPlayer,
          .seq = pose.seq,
          .timeUs = pose.timeUs,
          .hips = pose.hips,
          .rotations = pose.rotations};
}

void LeanSpine(Pose &pose, float pitch, float roll) {
  if (pitch == 0.f && roll == 0.f)
    return;
  static const int spine = BoneIndex("Spine1");
  if (spine < 0)
    return;
  // The parent's rotation in model space: the locals down from the root.
  proto::Quat parent{};
  std::array<int, kBones> chain{};
  std::size_t depth = 0;
  for (int bone = kParents[static_cast<std::size_t>(spine)]; bone >= 0;
       bone = kParents[static_cast<std::size_t>(bone)])
    chain[depth++] = bone;
  while (depth > 0)
    parent = Multiply(parent, pose.local[static_cast<std::size_t>(chain[--depth])]);
  // Forward is a turn about X the other way round (+X takes Y to Z: back);
  // to the right, a turn about +Y (it takes Z to X).
  const proto::Quat aboutX{std::sin(-pitch / 2.f), 0.f, 0.f, std::cos(-pitch / 2.f)};
  const proto::Quat aboutY{0.f, std::sin(roll / 2.f), 0.f, std::cos(roll / 2.f)};
  const auto lean = Multiply(aboutY, aboutX);
  auto &local = pose.local[static_cast<std::size_t>(spine)];
  local = Normalized(Multiply(Multiply(Multiply(Conjugate(parent), lean), parent), local));
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
  if (!m_samples.empty() && state.timeUs - m_samples.back().timeUs < 2'000'000)
    m_intervalUs +=
        (static_cast<double>(state.timeUs - m_samples.back().timeUs) - m_intervalUs) / 8.0;
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
