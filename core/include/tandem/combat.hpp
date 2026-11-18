#pragma once

// Hit outcomes (docs/ARCHITECTURE.md, "Game rules hold for every hit"): how
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
} // namespace tandem::combat
