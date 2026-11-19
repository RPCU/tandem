#pragma once

// Bandwidth (docs/architecture/netcode.md): how often each stream about a V
// goes out (relevance by distance, a still V less often), and the compact
// form of the host's NPC positions. Endpoint uses them on both sides; the
// game and the mock player never see them.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace tandem::netcode {
// Distance from our V to the nearest other V (metres) to the shortest gap
// between two sends of a stream (0: every frame). Far away a puppet is a
// marker on the map, or streamed out: a few updates a second do.
std::uint64_t PoseIntervalUs(float nearest);  // 30 m: every frame; 80: 15 Hz; 200: 5; then 1
std::uint64_t StateIntervalUs(float nearest); // 80 m: every frame; 200: 15 Hz; then 5
// Unknown distance (nobody else placed yet): as if near.
inline constexpr float kNearest = 0.f;

// Lets a stream through at most once per interval (a little early is fine:
// frames do not fall on the interval).
class Gate {
public:
  bool Due(std::uint64_t nowUs, std::uint64_t intervalUs);

private:
  std::uint64_t m_lastUs{};
  bool m_any{};
};

// A V or a car that stands still is sent 5 times a second instead of every
// frame. When it moves again, the last state held back goes first, with its
// own time, so the receiver sees it leave from where and when it stood
// (not a slow slide over the gap).
template <class State> class StillGate {
public:
  static constexpr std::uint64_t kStillIntervalUs = 200'000;

  // `state` stamped (timeUs); what to send now, oldest first (0 to 2).
  std::vector<State> Offer(const State &state, std::uint64_t relevanceUs);
  std::uint64_t Held() const { return m_heldCount; }

private:
  std::optional<State> m_last; // the last one sent
  std::optional<State> m_held; // a still one not sent
  std::uint64_t m_heldCount{};
};

bool IsStill(const proto::PlayerState &state, const proto::PlayerState &last);
bool IsStill(const proto::VehicleState &state, const proto::VehicleState &last);

extern template class StillGate<proto::PlayerState>;
extern template class StillGate<proto::VehicleState>;

// Host: its NPC states as NpcMotion, with only the NPCs that changed (more
// than kMoveM, kTurnRad, or what they detect) plus each one at least every
// kRefreshFrames states (a lost one heals within that), and those no longer
// listed flagged once. Nullopt: positions out of the quantized range, send
// the states whole (NpcStates).
inline constexpr std::uint8_t kNpcUnlisted = 1; // NpcMove::flags bit: not in the host's list now
class NpcCoder {
public:
  static constexpr float kMoveM = 0.02f;
  static constexpr float kTurnRad = 0.01f;
  static constexpr std::uint32_t kRefreshFrames = 5;
  static constexpr float kStep = 1.f / 128.f; // metres

  std::optional<proto::NpcMotion> Encode(const proto::NpcStates &states);
  void Forget(proto::NpcId npc) { m_sent.erase(npc); }
  void Reset() { m_sent.clear(); }

private:
  struct Sent {
    proto::Vec3 position;
    float yaw{};
    std::uint8_t detected{};
    std::uint32_t seq{};
  };
  std::map<proto::NpcId, Sent> m_sent;
};

// Guest: what the host last said of each NPC, so a motion comes out as the
// whole list of NPCs it shares (NpcStates), as it was always sent.
class NpcBook {
public:
  // Only NPCs in `known` (the shared set) are listed.
  proto::NpcStates Apply(const proto::NpcMotion &motion,
                         const std::map<proto::NpcId, proto::NpcSpawn> &known);
  // A whole list (NpcStates): it replaces what is listed.
  void Note(const proto::NpcStates &states);
  // A spawn: where it stands until it moves.
  void Seed(const proto::NpcSpawn &spawn);
  void Forget(proto::NpcId npc) { m_npcs.erase(npc); }
  void Reset() { m_npcs.clear(); }

private:
  std::map<proto::NpcId, proto::NpcEntry> m_npcs;
};

// A V's and a car's state, compact on the wire (PlayerMotion,
// VehicleMotion), and back. A V's orientation is its yaw.
proto::PlayerMotion Compact(const proto::PlayerState &state);
proto::PlayerState Whole(const proto::PlayerMotion &motion);
proto::VehicleMotion Compact(const proto::VehicleState &state);
proto::VehicleState Whole(const proto::VehicleMotion &motion);

// One move as the guest reads it back (tests).
proto::NpcEntry Dequantize(const proto::NpcMotion &motion, const proto::NpcMove &move);
} // namespace tandem::netcode
