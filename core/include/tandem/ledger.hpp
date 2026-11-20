#pragma once

// The death ledger (docs/architecture/regions.md §14): the placed NPCs that
// died in the session, so that the same dead stay dead in every game, also
// where its player was not there to see it. An NPC is in it by its entity's
// static id, the same in every game (proto::NpcPlaced); the random crowd
// never is.
//
//   Deaths   the ledger as one endpoint has it: the host's is the record
//            (it stamps and orders every death), a guest's what it heard.
//            Bounded: the newest proto::kMaxDeaths by the host's order.
//   Body     whether a game shows a body where an NPC of the ledger fell.
//
// It lasts the session: nothing of it is written to a save (the games keep
// their own copy of each dead NPC hidden, they never kill it).

#include "tandem/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace tandem::ledger {
// A body is shown where a placed NPC fell for this long after its death
// (host clock): after it the place is empty, as the game leaves it once its
// player has been away.
inline constexpr std::uint64_t kBodyForUs = 600'000'000;
// ...to a V within kBodyIn of it, until that V is beyond kBodyOut; at most
// kMaxBodies at a time (the nearest).
inline constexpr float kBodyIn = 70.f;
inline constexpr float kBodyOut = 100.f;
inline constexpr std::size_t kMaxBodies = 8;

class Deaths {
public:
  // The host: a death its owner reported (its NPC's), stamped and kept.
  // None: that entity is in the ledger already, or names none.
  std::optional<proto::NpcDeath> Record(proto::NpcDeath death, proto::PlayerId by,
                                        std::uint64_t hostUs);
  // Anyone: the host's word (stamped). True when it is news.
  bool Note(const proto::NpcDeath &death);

  bool Has(std::uint64_t entity) const { return m_deaths.contains(entity); }
  const proto::NpcDeath *Find(std::uint64_t entity) const;
  // Oldest first, as a late joiner is told.
  std::vector<proto::NpcDeath> InOrder() const;
  // The same on the host and on a guest that heard everything.
  std::uint32_t Digest() const;
  std::size_t Size() const { return m_deaths.size(); }
  bool Empty() const { return m_deaths.empty(); }
  void Clear();

private:
  // Beyond the cap the oldest goes (the lowest order).
  void Trim();

  std::map<std::uint64_t, proto::NpcDeath> m_deaths; // by entity
  std::uint32_t m_next{1};
  // The highest order ever dropped: nothing that old comes back.
  std::uint32_t m_floor{};
};

// Whether the game of player `you` shows a body for this death now: not its
// own kill's (its world has the real one), only while it is recent.
bool ShowsBody(const proto::NpcDeath &death, proto::PlayerId you, std::uint64_t hostUs);

// The deaths whose body the game of `you` shows with its V at `at`: those
// ShowsBody accepts within kBodyIn (kBodyOut for the ones in `shown`, which
// it shows already), the nearest kMaxBodies.
std::vector<proto::NpcDeath> Bodies(const std::vector<proto::NpcDeath> &deaths, proto::PlayerId you,
                                    std::uint64_t hostUs, const proto::Vec3 &at,
                                    const std::vector<std::uint64_t> &shown);
} // namespace tandem::ledger
