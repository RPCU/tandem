#pragma once

// Time bubbles (docs/ARCHITECTURE.md, slowed and stopped time).
//
// In single player a Sandevistan or a Kerenzikov slows the whole world. In
// a shared world the whole world would slow for every player, so instead
// the NPCs around the V using it are slowed, for everyone, by the factor
// the ability asks for: that V's bubble. The host applies it to its real
// NPCs (per-entity time dilation, the one vanilla gives a single NPC); each
// guest applies the same factors to its proxies so they animate at the
// speed they move. Everything outside every bubble, and every V, keeps
// normal speed.
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
// - an NPC that is no longer listed (gone, dead) is released.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace tandem::sync {
class TimeBubbles {
public:
  // Metres around the V, on the ground plane: a gunfight's reach, within
  // the 60 m around the host's V that it shares NPCs for.
  static constexpr float kRadius = 50.f;

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
