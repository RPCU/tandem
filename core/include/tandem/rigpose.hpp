#pragma once

// Some bones of any rig on the wire (proto::RigPose): a scene actor's face
// (lipsync, expressions) and the body of an actor that is not on the
// humanoid body rig (a drone, a robot). docs/architecture/scene-replay.md.
//
// Both games have the same rig resource for the same NPC, so a bone is said
// relative to the rig's reference pose, in local space: most of a face's 290
// joints rest there, and the ones that move stay within millimetres and a
// fraction of a radian of it (a few bytes each). The host says the bones
// that moved most since it last said them, within a byte cap, and a few in
// turn whatever they do: a lost frame heals within seconds. No keys, no
// acknowledgements. A receiver whose body has another rig takes nothing.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tandem::rig {
// One bone relative to its parent (local space).
struct Bone {
  proto::Vec3 translation;
  proto::Quat rotation;
};

// A rig's name on the wire: its bone names' hashes, in order.
std::uint32_t RigHash(std::span<const std::uint64_t> boneNames);

// A bone is said again once it is this far from what was last said of it.
inline constexpr float kShiftTolerance = 0.0004f; // metres
inline constexpr float kTurnTolerance = 0.01f;    // radians
// How many bones a frame refreshes in turn besides those that moved.
inline constexpr std::size_t kRefreshed = 3;

// One entry of a RigPose, read back.
struct Entry {
  std::uint16_t bone{};
  std::uint8_t form{}; // proto::kRig*
  proto::Vec3 shift;   // kRigShift: from the reference; kRigWhole: the translation
  proto::Quat turn;    // kRigTurn, kRigShift: from the reference; kRigWhole: the rotation
};
// The entries of a frame; nullopt when it is malformed for a rig of `bones`.
std::optional<std::vector<Entry>> Read(std::span<const std::uint8_t> data, std::uint16_t bones);
// Where an entry puts its bone, given the rig's reference for it.
Bone Placed(const Entry &entry, const Bone &reference);

// The host's side of one stream.
class Encoder {
public:
  // The rig's reference pose (local), before the first Encode; again when
  // the body is rebuilt on another rig.
  void Reset(std::span<const Bone> reference);
  // A bone never said (the root, a trajectory: the entity is placed by its
  // NPC states).
  void Skip(std::size_t bone);
  bool Ready() const { return !m_reference.empty(); }
  std::size_t Bones() const { return m_reference.size(); }
  // What to say of `pose` (the rig's bones, local) in at most `maxBytes`:
  // the bones furthest from what the receiver has, then a few in turn.
  // Empty: nothing moved and nothing fits.
  std::vector<std::uint8_t> Encode(std::span<const Bone> pose, std::size_t maxBytes);
  // How many bones are off their reference as the receiver has them.
  std::size_t Moved() const;

private:
  std::vector<Bone> m_reference;
  std::vector<Bone> m_said; // as the receiver has each bone
  std::vector<std::uint8_t> m_skip;
  std::size_t m_next{};
};

// The guest's side of one stream: what the host said of each bone, blended
// in over a few frames and written over the rig's own animation.
class Track {
public:
  // How fast a bone goes to where the host put it (the time to cover 63 %).
  static constexpr float kBlendSeconds = 0.06f;

  // A frame (older ones than the newest are dropped). False: not taken.
  bool Push(const proto::RigPose &pose);
  std::uint64_t NewestUs() const { return m_newestUs; }
  std::uint32_t Rig() const { return m_rig; }
  std::size_t Bones() const { return m_told.size(); }
  std::uint32_t Received() const { return m_received; }
  // Bones the host holds off their reference now.
  std::size_t Held() const { return m_active.size(); }
  // Writes the bones the host moved into `pose` (the rig's own output, local
  // space, in the rig's order), at host clock `nowUs`; a bone the host put
  // back at rest goes back to the rig's own value and is let go. Nothing
  // when `rig` or the bone count is not the stream's. Answers how many bones
  // it wrote.
  std::size_t Apply(std::uint64_t nowUs, std::uint32_t rig, std::span<const Bone> reference,
                    std::span<Bone> pose);

private:
  struct Told {
    Entry entry;
    Bone shown;     // what we last wrote
    bool active{};  // written over the rig's own
    bool started{}; // `shown` holds a value
  };
  std::vector<Told> m_told;
  std::vector<std::uint16_t> m_active;
  std::uint32_t m_rig{};
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};
  std::uint64_t m_newestUs{};
  std::uint64_t m_appliedUs{};
  std::uint32_t m_received{};
};
// ---- float tracks (proto::RigTracks): a face's wrinkles, pupils, blinks ----

// A track is said again once it is this far from what was last said of it.
inline constexpr float kTrackTolerance = 0.004f;

// The host's side of one rig's tracks.
class TrackEncoder {
public:
  // The rig's reference values, before the first Encode.
  void Reset(std::span<const float> reference);
  bool Ready() const { return !m_reference.empty(); }
  std::size_t Tracks() const { return m_reference.size(); }
  // What to say of `values` (the rig's tracks, in order) in at most
  // `maxBytes`: the furthest from what the receiver has, then a few in turn.
  std::vector<std::uint8_t> Encode(std::span<const float> values, std::size_t maxBytes);

private:
  std::vector<float> m_reference;
  std::vector<float> m_said;
  std::size_t m_next{};
};

// The guest's side: what the host said of each track, blended in and
// written over the rig's own values.
class TrackSet {
public:
  bool Push(const proto::RigTracks &tracks);
  std::uint64_t NewestUs() const { return m_newestUs; }
  std::uint32_t Rig() const { return m_rig; }
  std::size_t Held() const { return m_active.size(); }
  // Writes the tracks the host moved into `values` (the rig's own, in its
  // order), at host clock `nowUs`; a track the host put back at its
  // reference value goes back to the rig's own and is let go. Nothing when
  // `rig` or the count is not the stream's. Answers how many it wrote.
  std::size_t Apply(std::uint64_t nowUs, std::uint32_t rig, std::span<const float> reference,
                    std::span<float> values);

private:
  struct Told {
    float value{};
    float shown{};
    bool active{};
    bool started{};
  };
  std::vector<Told> m_told;
  std::vector<std::uint16_t> m_active;
  std::uint32_t m_rig{};
  std::uint32_t m_lastSeq{};
  bool m_haveSeq{};
  std::uint64_t m_newestUs{};
  std::uint64_t m_appliedUs{};
};
} // namespace tandem::rig
