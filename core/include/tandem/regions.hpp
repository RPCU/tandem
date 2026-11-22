#pragma once

// Per-player regions (docs/architecture/regions.md): each game runs the NPCs
// around its own V; players whose Vs are near each other are linked and see
// each other's NPCs as proxies; an NPC changes owner when its owner leaves
// it with another linked player near, or when its owner is gone.
//
// Pure rules, no network: the host's endpoint applies them to the positions
// it relays; tests and the mock player use them directly.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace tandem::regions {
// Metres. kTake < kRadius < kKeep < kLose: an NPC between two players does
// not bounce, and a hand-over is asked for before its owner would drop it.
inline constexpr float kRadius = 60.f;   // a region: what a game shares around its V
inline constexpr float kLinkIn = 150.f;  // two Vs closer than this: linked
inline constexpr float kLinkOut = 190.f; // linked Vs farther than this: unlinked
inline constexpr float kKeep = 80.f;     // an owner keeps its NPC while its V is within this
inline constexpr float kTake = 55.f;     // another player takes it over when this close to it
inline constexpr float kLose = 90.f;     // an owner stops sharing an NPC farther than this
// A lower-ranked game's own crowd is off while its V is this deep in a
// higher-ranked linked player's region, and back on beyond the radius.
inline constexpr float kQuietIn = 45.f;
// An owner linked with nobody still tells the host where its NPCs and its
// vehicles are, this often (microseconds): the record a link shows them from.
inline constexpr std::uint64_t kApartStatesUs = 1'000'000;

// An NPC's session id: the announcing game's player id minus one in the top
// 8 bits, that game's own counter below. The host's ids are below 2^24.
inline constexpr std::uint32_t kNpcCounterBits = 24;
inline constexpr proto::NpcId MakeNpcId(proto::PlayerId announcer, std::uint32_t counter) {
  return (((announcer - 1) & 0xFFu) << kNpcCounterBits) | (counter & 0xFFFFFFu);
}
// The game that announced an NPC: its owner until the host says otherwise.
inline constexpr proto::PlayerId Announcer(proto::NpcId npc) {
  return (npc >> kNpcCounterBits) + 1;
}

// Where regions overlap the host ranks first, then the lowest player id.
inline constexpr bool Outranks(proto::PlayerId a, proto::PlayerId b, proto::PlayerId host = 1) {
  if (a == b)
    return false;
  if (a == host || b == host)
    return a == host;
  return a < b;
}

// The players that hold their region: a quest of their own world runs where
// their V stands (proto::RegionHold). A holder ranks before anyone who holds
// nothing, the host too; among holders, and among the rest, the rule above.
using Holders = std::set<proto::PlayerId>;
inline bool Outranks(proto::PlayerId a, proto::PlayerId b, proto::PlayerId host,
                     const Holders &holders) {
  const bool holdsA = holders.contains(a), holdsB = holders.contains(b);
  if (holdsA != holdsB)
    return holdsA;
  return Outranks(a, b, host);
}

float Distance(const proto::Vec3 &a, const proto::Vec3 &b);

// Where a V is, when a state of it has been seen.
struct Place {
  proto::Vec3 position;
  bool known{};
};
using Places = std::map<proto::PlayerId, Place>;

// Which players are linked. Two players are linked until both their places
// are known and more than kLinkOut apart, and again once closer than kLinkIn.
class Links {
public:
  // a != b. Players never heard of are linked.
  bool Linked(proto::PlayerId a, proto::PlayerId b) const;
  // The host: from where everyone is now (the keys are the players in the
  // session). True when a link formed or broke.
  bool Update(const Places &places);
  // A guest: as the host said (Pairs).
  void Set(const proto::Regions &regions);
  // What the host sends: every player of `players` paired with itself (so a
  // player linked with nobody is named too), then each linked pair, lower
  // id first.
  std::vector<proto::PlayerId> Pairs(const std::set<proto::PlayerId> &players) const;
  void Forget(proto::PlayerId player);
  void Clear();

private:
  using Pair = std::pair<proto::PlayerId, proto::PlayerId>;
  static Pair Key(proto::PlayerId a, proto::PlayerId b) { return a < b ? Pair{a, b} : Pair{b, a}; }
  // Only what differs from the default: the pairs that are apart.
  std::set<Pair> m_apart;
  // A guest's: the players the host's last Regions named (others: linked).
  std::set<proto::PlayerId> m_named;
  bool m_fromWire{};
};

// The player that should take this NPC over from its owner now (kNoPlayer:
// it stays): the owner's V is beyond kKeep of it and a player linked with
// the owner has its V within kTake; the highest-ranked such player. Never
// from an owner that holds its region.
proto::PlayerId Taker(const proto::Vec3 &npc, proto::PlayerId owner, const Places &places,
                      const Links &links, proto::PlayerId host = 1, const Holders &holders = {});

// A vehicle is not handed to a player whose V it is on top of: one it has
// just run into, carries on its hood or is about to hit (kHandoverClear from
// the vehicle's origin: half a car and a couple of metres). A car handed
// over wakes up as a real one of the receiver's traffic and drives on, its
// own V on it (a host's V carried 750 m, 2026-11-22). A place not known is
// clear.
inline constexpr float kHandoverClear = 4.f;
inline bool ClearOf(const proto::Vec3 &vehicle, proto::PlayerId player, const Places &places) {
  const auto place = places.find(player);
  return place == places.end() || !place->second.known ||
         Distance(vehicle, place->second.position) > kHandoverClear;
}

// The player that inherits an NPC whose owner is gone (kNoPlayer: nobody is
// near, it goes): the highest-ranked of `candidates` (the players that were
// linked with the owner) whose V is known to be within kKeep of it.
proto::PlayerId Heir(const proto::Vec3 &npc, const Places &places,
                     const std::set<proto::PlayerId> &candidates, proto::PlayerId host = 1,
                     const Holders &holders = {});

// A player's group: itself and every player of `players` linked with it,
// directly or through others. Alone in it: the player is apart.
std::set<proto::PlayerId> GroupOf(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                                  const Links &links);
// Whose world's quests are played in a group: its highest-ranked player (a
// holder first, then the host, then the lowest id).
proto::PlayerId Leader(const std::set<proto::PlayerId> &group, proto::PlayerId host = 1,
                       const Holders &holders = {});

// The game that runs the world around `player` (its new NPCs, its crowd, its
// traffic and police: docs/architecture/traffic.md, "Who owns an area"): the
// highest-ranked player of `players` linked with it that outranks it and
// whose V is within kQuietIn of its V (kRadius for `current`, the area's
// owner until now: the same hysteresis as the crowd), else `player`'s own
// game. A linked player that outranks it while either place is unknown
// covers it (as a guest's crowd was off before regions).
proto::PlayerId AreaOwner(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                          const Places &places, const Links &links, proto::PlayerId host = 1,
                          const Holders &holders = {}, proto::PlayerId current = proto::kNoPlayer);

// ---- The overlap: what a game holds back (regions.md §6) ----

// The region of a linked player that outranks us: its game populates the
// world there (its crowd, its traffic), ours holds its own back.
struct Cover {
  proto::PlayerId by{};
  proto::Vec3 center;
  friend bool operator==(const Cover &, const Cover &) = default;
};
// The covers over `player`'s world: every player of `players` linked with
// it that outranks it and whose place is known, highest rank first.
std::vector<Cover> Covers(proto::PlayerId player, const std::set<proto::PlayerId> &players,
                          const Places &places, const Links &links, proto::PlayerId host = 1,
                          const Holders &holders = {});
// The highest-ranked cover whose V is within `radius` of this point
// (kNoPlayer: none): the game that populates it.
proto::PlayerId CoveredBy(const proto::Vec3 &point, const std::vector<Cover> &covers,
                          float radius = kRadius);

// The share of its crowd a game keeps (the crowd system's density modifier)
// when it cannot keep it out of the covers by place: all of it alone (1),
// the part of its region no cover overlaps as its V nears one, none (0)
// once its V is within kQuietIn of a cover's (`quiet`: it is at none now,
// and stays there until beyond kRadius: the crowd's hysteresis). In steps of
// a tenth, so that a walk does not change it every pass.
float CrowdShare(const proto::Vec3 &self, const std::vector<Cover> &covers, bool quiet);

// Null areas: boxes the crowd system spawns nothing in (pedestrians and
// traffic), one over each cover, kept where that V is. An area is placed
// with a lifetime and placed again before it runs out, or when its V has
// moved a step: it ends by itself if we stop (a crash, a session's end
// nobody tidied up).
inline constexpr float kNullHalf = kRadius; // half the box's side, metres
inline constexpr float kNullHeight = 40.f;  // half its height
inline constexpr float kNullStep = 15.f;    // placed again when its V is this far from its centre
inline constexpr float kNullLife = 12.f;    // seconds an area lasts
inline constexpr float kNullRenew = 8.f;    // placed again after this long
struct NullStep {
  proto::PlayerId by{};
  bool place{}; // true: put the area at `center` (the old one of `by`, if any, goes); false: remove
                // it
  proto::Vec3 center;
  friend bool operator==(const NullStep &, const NullStep &) = default;
};
class NullAreas {
public:
  // What to change now, given the covers and the time (seconds).
  std::vector<NullStep> Update(const std::vector<Cover> &covers, double now);
  // Every area goes (the session ended, the feature was turned off).
  std::vector<NullStep> Clear();
  std::size_t Size() const { return m_areas.size(); }

private:
  struct Area {
    proto::Vec3 center;
    double at{};
  };
  std::map<proto::PlayerId, Area> m_areas;
};
} // namespace tandem::regions
