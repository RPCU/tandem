#pragma once

// Twins (docs/architecture/regions.md §3): a placed NPC exists in every
// game. When another game's NPC is shown here as a proxy, our own copy of
// the same person gives way to it. Which of our NPCs is that person:
//
//   1. by identity: the owner said its NPC is placed (proto::NpcPlaced, the
//      entity's static id, the same in every game) and one of ours has that
//      id: that one, wherever each of them stands;
//   2. else by record and place, one to one: among the proxies with no
//      identity and our NPCs with none (a dynamic id), of the same record
//      and within kReach of each other, the nearest pairs first. A proxy
//      takes one of ours at most and one of ours goes to one proxy at most,
//      so two identical guards side by side are two pairs, never one of ours
//      taken twice or the wrong one of two.
//
// One of ours with a static id is never matched by place: its twin's owner
// would have named it.

#include "tandem/protocol.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace tandem::twins {
// How far apart two copies of one placed NPC may stand (metres): the same
// post, a step taken in one game and not in the other.
inline constexpr float kReach = 2.5f;

// A proxy that came (theirs), or one of our own NPCs that could be its twin.
struct Body {
  std::uint64_t record{}; // its character record
  proto::Vec3 position;
  std::uint64_t entity{}; // its static id (0: none known, or a dynamic id)
  // Ours only: in a fight, hurt, down: never given up, whatever matches.
  bool busy{};
};

struct Pair {
  std::size_t theirs{}; // index in `theirs`
  std::size_t ours{};   // index in `ours`
  bool identity{};      // matched by static id (rule 1)
  friend bool operator==(const Pair &, const Pair &) = default;
};

// The pairs, in the order of `theirs`.
std::vector<Pair> Match(std::span<const Body> theirs, std::span<const Body> ours,
                        float reach = kReach);
} // namespace tandem::twins
