#include "tandem/rigpose.hpp"

#include "tandem/pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace tandem::rig {
namespace {
constexpr std::size_t kFormBytes[4] = {0, 3, 6, 11};

bool Finite(const Bone &bone) {
  return std::isfinite(bone.translation.x) && std::isfinite(bone.translation.y) &&
         std::isfinite(bone.translation.z) && std::isfinite(bone.rotation.i) &&
         std::isfinite(bone.rotation.j) && std::isfinite(bone.rotation.k) &&
         std::isfinite(bone.rotation.r);
}

float Distance(const proto::Vec3 &a, const proto::Vec3 &b) {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                   (a.z - b.z) * (a.z - b.z));
}

proto::Vec3 Lerp(const proto::Vec3 &a, const proto::Vec3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

std::int16_t WholeSteps(float metres) {
  return static_cast<std::int16_t>(
      std::clamp(std::round(metres / proto::kRigWholeStep), -32767.f, 32767.f));
}

// One bone as it goes on the wire, and where that puts it.
struct Made {
  std::uint8_t form{};
  std::array<std::uint8_t, 11> bytes{};
  std::size_t size{};
  Bone placed;
};

Made Make(const Bone &now, const Bone &reference) {
  Made made;
  std::int8_t turn[3]{};
  const bool smallTurn = pose::TurnOf(reference.rotation, now.rotation, turn);
  const bool noTurn = smallTurn && turn[0] == 0 && turn[1] == 0 && turn[2] == 0;
  const float delta[3] = {now.translation.x - reference.translation.x,
                          now.translation.y - reference.translation.y,
                          now.translation.z - reference.translation.z};
  std::int8_t shift[3]{};
  bool smallShift = true, noShift = true;
  for (int axis = 0; axis < 3; ++axis) {
    const float steps = std::round(delta[axis] / proto::kRigShiftStep);
    if (std::abs(steps) > 127.f)
      smallShift = false;
    else
      shift[axis] = static_cast<std::int8_t>(steps);
    noShift = noShift && steps == 0.f;
  }
  if (smallTurn && noShift) {
    made.form = noTurn ? proto::kRigRest : proto::kRigTurn;
    made.placed = {reference.translation,
                   noTurn ? reference.rotation : pose::Turned(reference.rotation, turn)};
    if (!noTurn)
      for (int axis = 0; axis < 3; ++axis)
        made.bytes[made.size++] = static_cast<std::uint8_t>(turn[axis]);
    return made;
  }
  if (smallTurn && smallShift) {
    made.form = proto::kRigShift;
    for (int axis = 0; axis < 3; ++axis)
      made.bytes[made.size++] = static_cast<std::uint8_t>(shift[axis]);
    for (int axis = 0; axis < 3; ++axis)
      made.bytes[made.size++] = static_cast<std::uint8_t>(turn[axis]);
    made.placed = {{reference.translation.x + static_cast<float>(shift[0]) * proto::kRigShiftStep,
                    reference.translation.y + static_cast<float>(shift[1]) * proto::kRigShiftStep,
                    reference.translation.z + static_cast<float>(shift[2]) * proto::kRigShiftStep},
                   pose::Turned(reference.rotation, turn)};
    return made;
  }
  made.form = proto::kRigWhole;
  const std::int16_t whole[3] = {WholeSteps(now.translation.x), WholeSteps(now.translation.y),
                                 WholeSteps(now.translation.z)};
  for (int axis = 0; axis < 3; ++axis) {
    const auto bits = static_cast<std::uint16_t>(whole[axis]);
    made.bytes[made.size++] = static_cast<std::uint8_t>(bits & 0xFF);
    made.bytes[made.size++] = static_cast<std::uint8_t>(bits >> 8);
  }
  pose::PackQuat5(pose::Normalized(now.rotation), made.bytes.data() + made.size);
  made.placed = {{static_cast<float>(whole[0]) * proto::kRigWholeStep,
                  static_cast<float>(whole[1]) * proto::kRigWholeStep,
                  static_cast<float>(whole[2]) * proto::kRigWholeStep},
                 pose::UnpackQuat5(made.bytes.data() + made.size)};
  made.size += proto::kPoseFrameRotationBytes;
  return made;
}

// How far `now` is from `said`, in tolerances: over 1, it is said again.
float Error(const Bone &now, const Bone &said) {
  return Distance(now.translation, said.translation) / kShiftTolerance +
         pose::Angle(now.rotation, said.rotation) / kTurnTolerance;
}
} // namespace

std::uint32_t RigHash(std::span<const std::uint64_t> boneNames) {
  // FNV-1a over the names' bytes, the count first.
  std::uint32_t hash = 2166136261u;
  const auto add = [&](std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= static_cast<std::uint8_t>(value >> (8 * byte));
      hash *= 16777619u;
    }
  };
  add(boneNames.size());
  for (const auto name : boneNames)
    add(name);
  return hash;
}

std::optional<std::vector<Entry>> Read(std::span<const std::uint8_t> data, std::uint16_t bones) {
  std::vector<Entry> entries;
  std::size_t at = 0;
  int last = -1;
  while (at < data.size()) {
    if (at + 2 > data.size())
      return std::nullopt;
    const auto head = static_cast<std::uint16_t>(data[at] | data[at + 1] << 8);
    Entry entry{.bone = static_cast<std::uint16_t>(head & 0x3FFF),
                .form = static_cast<std::uint8_t>(head >> 14)};
    at += 2;
    if (static_cast<int>(entry.bone) <= last || entry.bone >= bones ||
        at + kFormBytes[entry.form] > data.size())
      return std::nullopt;
    last = entry.bone;
    const auto *bytes = data.data() + at;
    const auto signedByte = [&](std::size_t i) { return static_cast<std::int8_t>(bytes[i]); };
    if (entry.form == proto::kRigTurn) {
      const std::int8_t turn[3] = {signedByte(0), signedByte(1), signedByte(2)};
      entry.turn = pose::Turned({}, turn);
    } else if (entry.form == proto::kRigShift) {
      entry.shift = {static_cast<float>(signedByte(0)) * proto::kRigShiftStep,
                     static_cast<float>(signedByte(1)) * proto::kRigShiftStep,
                     static_cast<float>(signedByte(2)) * proto::kRigShiftStep};
      const std::int8_t turn[3] = {signedByte(3), signedByte(4), signedByte(5)};
      entry.turn = pose::Turned({}, turn);
    } else if (entry.form == proto::kRigWhole) {
      const auto whole = [&](std::size_t i) {
        return static_cast<float>(static_cast<std::int16_t>(
                   static_cast<std::uint16_t>(bytes[i] | bytes[i + 1] << 8))) *
               proto::kRigWholeStep;
      };
      entry.shift = {whole(0), whole(2), whole(4)};
      entry.turn = pose::UnpackQuat5(bytes + 6);
    }
    at += kFormBytes[entry.form];
    entries.push_back(entry);
  }
  return entries;
}

Bone Placed(const Entry &entry, const Bone &reference) {
  switch (entry.form) {
  case proto::kRigTurn:
    return {reference.translation,
            pose::Normalized(pose::Multiply(reference.rotation, entry.turn))};
  case proto::kRigShift:
    return {{reference.translation.x + entry.shift.x, reference.translation.y + entry.shift.y,
             reference.translation.z + entry.shift.z},
            pose::Normalized(pose::Multiply(reference.rotation, entry.turn))};
  case proto::kRigWhole:
    return {entry.shift, entry.turn};
  default:
    return reference;
  }
}

// ---- host ----

void Encoder::Reset(std::span<const Bone> reference) {
  m_reference.assign(reference.begin(), reference.end());
  m_said = m_reference;
  m_skip.assign(m_reference.size(), 0);
  m_next = 0;
}

void Encoder::Skip(std::size_t bone) {
  if (bone < m_skip.size())
    m_skip[bone] = 1;
}

std::size_t Encoder::Moved() const {
  std::size_t moved = 0;
  for (std::size_t bone = 0; bone < m_said.size(); ++bone)
    moved += Error(m_said[bone], m_reference[bone]) > 0.f ? 1 : 0;
  return moved;
}

std::vector<std::uint8_t> Encoder::Encode(std::span<const Bone> pose, std::size_t maxBytes) {
  std::vector<std::uint8_t> out;
  const auto count = m_reference.size();
  if (count == 0 || pose.size() != count)
    return out;
  struct Want {
    float error;
    std::uint16_t bone;
  };
  std::vector<Want> wants;
  for (std::size_t bone = 0; bone < count; ++bone) {
    if (m_skip[bone] || !Finite(pose[bone]))
      continue;
    if (const float error = Error(pose[bone], m_said[bone]); error > 1.f)
      wants.push_back({error, static_cast<std::uint16_t>(bone)});
  }
  std::ranges::sort(wants, [](const Want &a, const Want &b) { return a.error > b.error; });

  std::vector<std::pair<std::uint16_t, Made>> chosen;
  std::vector<std::uint8_t> picked(count, 0);
  std::size_t bytes = 0;
  const auto take = [&](std::uint16_t bone) {
    const auto made = Make(pose[bone], m_reference[bone]);
    if (bytes + 2 + made.size > maxBytes)
      return false;
    bytes += 2 + made.size;
    picked[bone] = 1;
    chosen.emplace_back(bone, made);
    return true;
  };
  for (const auto &want : wants) {
    if (bytes + 2 > maxBytes)
      break;
    take(want.bone); // too large for what is left: a smaller one may fit
  }
  // A few in turn, moved or not: what a lost frame left wrong heals.
  for (std::size_t refreshed = 0, tried = 0; refreshed < kRefreshed && tried < count; ++tried) {
    const auto bone = static_cast<std::uint16_t>(m_next);
    m_next = (m_next + 1) % count;
    if (m_skip[bone] || picked[bone] || !Finite(pose[bone]))
      continue;
    if (!take(bone))
      break;
    ++refreshed;
  }
  std::ranges::sort(chosen, [](const auto &a, const auto &b) { return a.first < b.first; });
  out.reserve(bytes);
  for (const auto &[bone, made] : chosen) {
    const auto head = static_cast<std::uint16_t>(bone | made.form << 14);
    out.push_back(static_cast<std::uint8_t>(head & 0xFF));
    out.push_back(static_cast<std::uint8_t>(head >> 8));
    out.insert(out.end(), made.bytes.begin(), made.bytes.begin() + made.size);
    m_said[bone] = made.placed;
  }
  return out;
}

// ---- guest ----

bool Track::Push(const proto::RigPose &pose) {
  if (m_haveSeq && static_cast<std::int32_t>(pose.seq - m_lastSeq) <= 0 && pose.rig == m_rig)
    return false;
  const auto entries = Read(pose.data, pose.bones);
  if (!entries)
    return false;
  if (m_told.size() != pose.bones || m_rig != pose.rig) {
    // Another body: nothing of the old one holds.
    m_told.assign(pose.bones, {});
    m_active.clear();
    m_rig = pose.rig;
    m_appliedUs = 0;
  }
  m_haveSeq = true;
  m_lastSeq = pose.seq;
  m_newestUs = pose.timeUs;
  ++m_received;
  for (const auto &entry : *entries) {
    auto &told = m_told[entry.bone];
    if (!told.active && entry.form == proto::kRigRest)
      continue; // at rest already
    told.entry = entry;
    if (!told.active) {
      told.active = true;
      m_active.push_back(entry.bone);
    }
  }
  return true;
}

std::size_t Track::Apply(std::uint64_t nowUs, std::uint32_t rig, std::span<const Bone> reference,
                         std::span<Bone> pose) {
  if (rig != m_rig || m_told.empty() || reference.size() != m_told.size() ||
      pose.size() != m_told.size())
    return 0;
  // The first write, or one after a pause, goes there at once.
  float seconds = 0.25f;
  if (m_appliedUs != 0 && nowUs > m_appliedUs)
    seconds = std::min(static_cast<float>(static_cast<double>(nowUs - m_appliedUs) / 1e6), 0.25f);
  else if (m_appliedUs != 0)
    seconds = 0.f;
  m_appliedUs = nowUs;
  const float alpha = 1.f - std::exp(-seconds / kBlendSeconds);
  std::size_t written = 0;
  for (std::size_t i = 0; i < m_active.size();) {
    const auto bone = m_active[i];
    auto &told = m_told[bone];
    const bool rest = told.entry.form == proto::kRigRest;
    const Bone target = rest ? pose[bone] : Placed(told.entry, reference[bone]);
    if (!told.started) {
      told.shown = pose[bone];
      told.started = true;
    }
    told.shown = {Lerp(told.shown.translation, target.translation, alpha),
                  pose::Slerp(told.shown.rotation, target.rotation, alpha)};
    if (rest && Distance(told.shown.translation, target.translation) < kShiftTolerance / 2.f &&
        pose::Angle(told.shown.rotation, target.rotation) < kTurnTolerance / 2.f) {
      // Back with the rig's own animation: let go.
      told = {};
      m_active[i] = m_active.back();
      m_active.pop_back();
      continue;
    }
    if (Finite(told.shown)) {
      pose[bone] = told.shown;
      ++written;
    }
    ++i;
  }
  return written;
}
} // namespace tandem::rig
