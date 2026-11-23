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

// An owner's NPC states as NpcMotion, with only the NPCs that changed (more
// than kMoveM, kTurnRad, kSpeedMs of velocity, or what they detect) plus
// each one at least every kRefreshFrames messages (a lost one heals within
// that). A partial list (the NPCs near a V, between two whole ones) leaves
// the others as they were. Nullopt: positions out of the quantized range,
// send the states whole (NpcStates). Unbounded: Split cuts it.
class NpcCoder {
public:
  static constexpr float kMoveM = 0.02f;
  static constexpr float kTurnRad = 0.01f;
  static constexpr float kSpeedMs = 0.1f;
  static constexpr std::uint32_t kRefreshFrames = 5;
  static constexpr float kStep = 1.f / 128.f; // metres

  std::optional<proto::NpcMotion> Encode(const proto::NpcStates &states, bool partial = false);
  void Forget(proto::NpcId npc) { m_sent.erase(npc); }
  void Reset() { m_sent.clear(); }

private:
  struct Sent {
    proto::Vec3 position;
    float yaw{};
    proto::Vec3 velocity;
    std::uint8_t detected{};
    std::uint32_t frame{};
  };
  std::map<proto::NpcId, Sent> m_sent;
  std::uint32_t m_frame{};
};

// The NPCs a motion lists, as states (each move read on its own).
proto::NpcStates Read(const proto::NpcMotion &motion);

// What one datagram carries (a little over 1 kB): an owner's list in as
// many messages as it takes.
inline constexpr std::size_t kNpcsPerMotion = 48; // 19 bytes a move
inline constexpr std::size_t kNpcsPerStates = 32; // 33 bytes an entry
std::vector<proto::NpcMotion> Split(proto::NpcMotion motion);
std::vector<proto::NpcStates> Split(proto::NpcStates states);

// An owner's NPC velocities, from its own samples of them (where each was
// when it was read): the receiver carries a late state on along it.
class NpcVelocities {
public:
  // Fills each entry's velocity (states.timeUs: when they were read).
  void Fill(proto::NpcStates &states);
  void Forget(proto::NpcId npc) { m_last.erase(npc); }
  void Reset() { m_last.clear(); }

private:
  struct Last {
    proto::Vec3 position;
    std::uint64_t timeUs{};
  };
  std::map<proto::NpcId, Last> m_last;
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
