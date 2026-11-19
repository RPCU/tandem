#pragma once

// Time bubbles (docs/architecture/time.md, slowed and stopped time).
//
// In single player a Sandevistan, a Kerenzikov, a slow-motion perk or the
// scanner slows the whole world. In a shared world the whole world would
// slow for every player, so instead the NPCs around the V using it are
// slowed, for everyone, by the factor the ability asks for: that V's
// bubble. The host applies it to its real NPCs (per-entity time dilation,
// the one vanilla gives a single NPC); each guest applies the same factors
// to its proxies so they animate at the speed they move. Everything
// outside every bubble, and every V, keeps normal speed.
//
// The rules, in one place so every game agrees:
// - one bubble per V whose ability is on (scale < 1); it holds the NPCs
//   within kRadius of that V;
// - membership is sticky: an NPC that was in a bubble stays in it until
//   the bubble ends, wherever it goes (no NPC speeding up as it crosses an
//   edge); an NPC that comes within reach joins;
// - an NPC in several bubbles takes the strongest one: the smallest factor,
//   never the product;
// - a bubble ends when its V's ability does (or that player leaves): all
//   its NPCs go back together, except those still in another bubble, which
//   take that one's factor;
// - an NPC that is no longer listed (gone, dead) is released;
// - a bubble slows the other Vs inside it too (PlayerFactor): each V's own
//   game slows its V by the strongest bubble around it that is not its
//   own, divided by its own ability's factor. A V with no ability is as
//   slow as the NPCs and sees the bubble's owner move fast; one whose own
//   ability matches or beats the bubble is not slowed and stands in it on
//   an equal footing; a weaker one is slowed by the ratio. Not sticky: a
//   V that walks out is free again, kMargin past the edge.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace tandem::sync {
class TimeBubbles {
public:
  // Metres around the V, on the ground plane: everything its player can
  // see or fight. In single player the whole world slows; here it is every
  // NPC a game shows (the host shares those within 60 m of its V), with
  // room for players some way apart.
  static constexpr float kRadius = 150.f;
  // A V already slowed by a bubble stays in it this much farther out, so
  // one walking along its edge does not flicker between two speeds.
  static constexpr float kMargin = 15.f;

  struct Owner {
    proto::PlayerId player{};
    float scale{1.f}; // the world speed its ability asks for (< 1: a bubble)
    proto::Vec3 position;
  };
  struct Npc {
    proto::NpcId npc{};
    proto::Vec3 position;
  };
  // An NPC's new speed: its factor, or 1 when it is out of every bubble.
  struct Change {
    proto::NpcId npc{};
    float factor{1.f};
    friend bool operator==(const Change &, const Change &) = default;
  };

  // The Vs with an ability on now and every NPC there is: the NPCs whose
  // speed changed since the last update.
  std::vector<Change> Update(std::span<const Owner> owners, std::span<const Npc> npcs);
  // Every bubble ends (the session is over): the NPCs to give back.
  std::vector<Change> Clear();

  float FactorOf(proto::NpcId npc) const;
  // How much a V is slowed by the others' bubbles (1: not at all): `own` is
  // the world speed its own ability asks for (1: none), `owners` every V
  // with a bubble now (itself too, ignored), `inside` whether it was slowed
  // at the last ask (the edge's margin).
  // A shot fired on the host reaches the guest `lagUs` later (its stamp
  // against the host clock now). The guest's projectile is run faster for a
  // moment, so it then flies where the host's timeline has it: `factor` x
  // `boost` for `seconds`, then `factor`. No boost for a lag under 5 ms or
  // over 1 s (a stale shot is fired as it comes).
  struct CatchUp {
    float boost{1.f};
    float seconds{};
    friend bool operator==(const CatchUp &, const CatchUp &) = default;
  };
  static CatchUp ShotCatchUp(std::uint64_t firedUs, std::uint64_t nowUs);
  static float PlayerFactor(proto::PlayerId player, float own, const proto::Vec3 &position,
                            std::span<const Owner> owners, bool inside = false);
  std::size_t MembersOf(proto::PlayerId owner) const;
  std::size_t Bubbles() const { return m_bubbles.size(); }

private:
  struct Bubble {
    float scale{1.f};
    std::set<proto::NpcId> members;
  };
  std::map<proto::PlayerId, Bubble> m_bubbles;
  std::map<proto::NpcId, float> m_factors; // the NPCs slowed now (factor < 1)
};
} // namespace tandem::sync
