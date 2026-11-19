#pragma once

// Hit outcomes (docs/architecture/combat.md, "Game rules hold for every hit"): how
// the host applies a guest's hit on its NPC by the rules the guest's game
// measured it with. Platform-independent, so the plugin and the mock player
// apply a strike the same way and the rules are tested on Linux.

#include "tandem/protocol.hpp"

namespace tandem::combat {
// The health points a guest's strike takes from the host's NPC, which has
// `health` of `maxHealth` points now. As the game's StatPoolsManager does:
//   - an instant kill (finisher, the Kill flag) takes everything;
//   - a hit the attacker's game capped to leave 1 HP (finisher grace, the
//     bleeding perk) leaves 1 HP of the host's health, whatever the proxy
//     had (it trails the host's by a round trip);
//   - the attack's minimum health percentage (AttackData
//     .GetMinimumHealthPercent) is never gone under, and an immortal NPC
//     (god mode Immortal) keeps 1 %;
//   - never more than it has.
// Non-lethal hits take their damage too: the NPC's defeat mechanic knocks
// it out instead of killing it (the host's game decides, from the flag).
float StrikeLoss(const proto::NpcStrike &strike, float health, float maxHealth, bool immortal);

// A guest's NpcHit (the older, health-only message) as a strike.
proto::NpcStrike FromHit(const proto::NpcHit &hit);

// Guest: how long a host NPC's melee hit on our V waits before our game runs
// it (block, parry, dodge, armour: the target's side), so it lands when our
// proxy of that NPC is drawn landing it, not before (docs/architecture/
// combat-coverage.md, "NPC melee at a guest"). The proxy is drawn
// `proxyDelayUs` behind the host's clock; the hit was decided on the host
// and took `transitUs` to arrive. Never negative, never more than
// kMaxMeleeHoldUs (a late link must not turn a swing into a delayed curse).
inline constexpr std::uint64_t kMaxMeleeHoldUs = 300'000;
std::uint64_t MeleeHoldUs(std::uint64_t proxyDelayUs, std::uint64_t transitUs);

// A throw's copy is launched only if the throw is this fresh: one held up
// behind a stall or a loading screen would fly long after the real one
// landed (its end, when it comes, finds no copy).
inline constexpr std::uint64_t kThrowStaleUs = 1'000'000;

// The session's rules (proto::Rules).
inline bool FriendlyFire(const proto::Rules &rules) {
  return (rules.flags & proto::kRuleFriendlyFire) != 0;
}
} // namespace tandem::combat
