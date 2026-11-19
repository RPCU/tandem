#pragma once

// Enemy scaling (docs/architecture/npcs.md, "Enemy scaling"): a hostile NPC
// is tougher for each extra player near it, so a fight with two, three or
// four Vs is not a quarter as long. The NPC's owner (the host, or the game
// that runs it under regions) counts the players near it (Counter) and puts
// one Multiplier stat modifier per stat on it (For): health, poise, the hit
// reaction thresholds, armour. Damage it deals is never scaled: each V takes
// vanilla hits. Every proxy of it carries the same modifiers (proto::NpcScale
// says the count), so a guest's hit is computed against the same armour.
//
// Plain arithmetic and a small state machine, kept here so the plugin and
// the mock player agree and the rules are tested on Linux.

#include "tandem/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace tandem::scaling {
// ---- the defaults (tandem.ini enemy_scaling, enemy_scaling_health) ----

// Health ×(1 + 0.6·(n−1)): 1.6 for 2 players, 2.2 for 3, 2.8 for 4. Less
// than ×n: the players' damage does not simply add up (they share targets,
// one revives the other, the NPCs split their fire), and the fight should
// stay as short as alone, not get n times longer.
inline constexpr float kHealthPerPlayer = 0.6f;
// The highest enemy_scaling_health tandem.ini may set (proto::kMaxEnemyHealth).
inline constexpr float kMaxHealthPerPlayer = 2.5f;
// Poise (the guard a melee build breaks) and the hit reaction thresholds
// (how much one hit must take to stagger or knock it down) ×(1 + 0.25·(n−1)):
// 1.25, 1.5, 1.75. Each V's hits still stagger, a little less often.
inline constexpr float kPoisePerPlayer = 0.25f;
// Armour +10 % per extra player, at most +20 %: armour divides every hit's
// damage on top of the health, so it compounds; capped low.
inline constexpr float kArmorPerPlayer = 0.10f;
inline constexpr float kArmorCap = 1.2f;
// A session's size: a count above it is taken as it.
inline constexpr std::uint8_t kMaxPlayers = 4;

// ---- who counts (Counter) ----

// A V counts for an NPC from within kNearIn of it, and stops beyond
// kNearOut: 100 m is past the range most fights are fought at (shotguns,
// SMGs, rifles), well inside the 150 m at which players are linked (so a
// counted V always sees the NPC), and 130 m still inside it. A V fighting
// it (in combat with it, its target, or who hit it) counts from anywhere,
// and for kEngagedHold after that ends: a sniper beyond 130 m counts while
// it shoots.
inline constexpr float kNearIn = 100.f;
inline constexpr float kNearOut = 130.f;
inline constexpr double kEngagedHold = 5.0; // seconds

// The host's rule, as proto::Rules carries it.
struct Rule {
  bool on{};
  float health{kHealthPerPlayer}; // per extra player
};
Rule RuleOf(const proto::Rules &rules);
// `rules` with the scaling rule set (the health per extra player clamped to
// 0..kMaxHealthPerPlayer, carried in whole percent).
proto::Rules WithRule(proto::Rules rules, bool on, float health);

// What an NPC carries for `players` (counted 1..kMaxPlayers; 0 is 1).
// Every one is 1 with the rule off, or alone.
struct Multipliers {
  float health{1.f};
  float poise{1.f}; // the Poise stat and the hit reaction thresholds
  float armor{1.f};
  bool None() const { return health == 1.f && poise == 1.f && armor == 1.f; }
  friend bool operator==(const Multipliers &, const Multipliers &) = default;
};
Multipliers For(std::uint8_t players, const Rule &rule);

// A stat pool (health, poise) at `value` of `oldMax` once its maximum is
// `newMax`: the same share, so a count that changes mid-fight neither heals
// nor hurts. (The plugin keeps it by reading and setting the pool in percent.)
float KeepFraction(float value, float oldMax, float newMax);

// The multiplier for the hit reaction thresholds (StaggerDamageThreshold,
// KnockdownDamageThreshold): the game reads them as a percentage of the
// NPC's total health as its HitReactionComponent last read it
// (`componentHealth`), which may or may not include our health modifier;
// the threshold in points should be the unscaled health's (`baseHealth`)
// times `poise`.
float ThresholdFactor(float poise, float baseHealth, float componentHealth);

// One player as the NPC's owner sees it this round.
struct Seen {
  proto::PlayerId player{};
  float distance{-1.f}; // to the NPC, m (< 0: not placed here: no puppet, no view)
  bool engaged{};       // in combat with it, its target, or hit it a moment ago
};

// Whether a player counts this round: it counted last round (`was`), until
// when its engagement holds it (`heldUntil`).
bool Counts(bool was, const Seen &seen, double heldUntil, double now);

// The count of every NPC its game owns, with hysteresis. A player left out
// of `players` (gone from the session, vanished) stops counting at once; an
// NPC forgotten (dead, gone, handed over) keeps nothing.
class Counter {
public:
  // This round's count for `npc` (1..kMaxPlayers: its owner's V is one,
  // wherever it stands).
  std::uint8_t Count(proto::NpcId npc, std::span<const Seen> players, double now);
  // The last count (1: never counted).
  std::uint8_t CountOf(proto::NpcId npc) const;
  void Forget(proto::NpcId npc);
  void Clear() { m_npcs.clear(); }
  std::size_t Size() const { return m_npcs.size(); }

private:
  struct Npc {
    std::map<proto::PlayerId, double> counted; // who counts, held until when
    std::uint8_t count{1};
  };
  std::map<proto::NpcId, Npc> m_npcs;
};
} // namespace tandem::scaling
