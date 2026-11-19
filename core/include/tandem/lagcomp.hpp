#pragma once

// Fairness under latency (docs/architecture/netcode.md, "Fairness"): the
// owner of an NPC (the host) checks a guest's request against the world as
// the guest saw it when it acted, not as the host has it when the request
// lands. A guest's own V is where its game said (its states, stamped on the
// host clock); the NPC is where the host's own NPC was when the guest drew
// it (a round trip and the guest's view delay ago). The rewind is bounded,
// so a slow link buys at most kMaxRewindUs of grace and nothing more.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace tandem::lagcomp {
inline constexpr std::uint64_t kMaxRewindUs = 400'000;
inline constexpr std::uint64_t kKeepUs = 1'500'000;
// How far off our estimate of the guest's moment may be (metres): a
// fraction of a step either way.
inline constexpr float kToleranceM = 0.5f;

// Where one thing was lately, on the host clock.
class History {
public:
  void Push(std::uint64_t timeUs, const proto::Vec3 &position);
  // Interpolated; before the oldest, the oldest; after the newest, the newest.
  std::optional<proto::Vec3> At(std::uint64_t timeUs) const;
  bool Empty() const { return m_samples.empty(); }

private:
  struct Sample {
    std::uint64_t timeUs;
    proto::Vec3 position;
  };
  std::deque<Sample> m_samples;
};

// When a guest saw the world it acted on: its request took half the round
// trip to come, and our state of the NPC took another half to reach it,
// drawn `viewDelayUs` behind. Never further back than kMaxRewindUs.
std::uint64_t SeenAtUs(std::uint64_t arrivalUs, std::uint32_t rttMs, std::uint64_t viewDelayUs);
// A guest's view delay of our NPCs when it does not say: the NPC stream's
// interval (10 Hz) and a margin (its tracks hold the next state).
std::uint64_t NpcViewDelayUs(std::uint32_t rttMs);

struct Reach {
  float now{};  // metres apart as we have them now
  float seen{}; // as the guest saw them (or now, when no history)
  bool ok{};
};

// Is `actor` (a guest's V) within `limit` of `target` (our NPC)? `actorNow`
// and `targetNow` are where we have them now (its puppet, the NPC); the
// histories rewind both to the guest's moment.
Reach Check(const History *actor, const History *target, const proto::Vec3 &actorNow,
            const proto::Vec3 &targetNow, std::uint64_t arrivalUs, std::uint32_t rttMs,
            float limit);

// The host's record: guests' Vs and its own NPCs, by an id of the caller's.
class Book {
public:
  void Push(std::uint32_t id, std::uint64_t timeUs, const proto::Vec3 &position);
  const History *Of(std::uint32_t id) const;
  void Forget(std::uint32_t id) { m_histories.erase(id); }
  void Clear() { m_histories.clear(); }

private:
  std::map<std::uint32_t, History> m_histories;
};
} // namespace tandem::lagcomp
