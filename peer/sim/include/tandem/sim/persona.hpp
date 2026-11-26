#pragma once

// Who the simulated player is (docs/architecture/sim.md, "Personas"): what it
// likes to do, how far it stays from its partner, and what it does to the
// session itself (leaving, vanishing, a bad link).

#include "tandem/shaping.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::sim {
// What a simulated V sets out to do. Each is a feature path of the coverage
// report ("v.fight", "v.takedown"...).
enum class Goal : std::uint8_t {
  Follow,      // stay by the partner
  Wander,      // walk about on its own
  Fight,       // shoot or cut an NPC in a fight
  Hack,        // upload a quickhack, land its effect
  Sandevistan, // slow time around it
  Takedown,    // grab an NPC, take it down or knock it out, carry the body
  Drive,       // drive its car about
  Ride,        // ride in the partner's car
  FastTravel,  // travel far, and offer the others to join
  Regroup,     // ask the others to come
  Metro,       // ride the NCART
  Elsewhere,   // a braindance, cyberspace
  Pause,       // open its pause menu
  Camo,        // optical camo
  DropItem,    // drop a shared world item
  LootQuest,   // loot a quest item (every V gets it)
  GoDown,      // take a fall: down until someone revives it
  CarHack,     // quickhack the car the partner drives
  Grenade,     // an area hit on the partner's V (it cannot kill)
  Dial,        // call someone (a guest's request to the host)
  Count,
};
const char *NameOf(Goal goal);

struct Persona {
  std::string name;
  std::string what;
  // How likely each goal is picked when it is possible (0: never).
  std::array<float, static_cast<std::size_t>(Goal::Count)> weights{};
  float keep{4.f};     // metres it stays from its partner when following
  float region{0.f};   // metres from the partner where it lives (0: with them)
  float tempo{1.f};    // how fast it goes from one thing to the next
  bool helps{true};    // revives its partner, takes their fights
  bool comes{true};    // accepts regroup offers
  bool answers{true};  // answers the host's calls, suggests at hubs
  bool hostile{};      // fights NPCs that are not in a fight
  float givesUpDown{}; // seconds down, unrevived, before it is out (0: waits)
  // Chaos: legal, at the worst time.
  bool worstMoments{};        // acts as others load, as NPCs die, as holds change
  double leaveEverySeconds{}; // leaves and joins again this often (0: never)
  double vanishShare{};       // ...this share of those with no goodbye
  net::LinkShape link;        // its network, each way
  float Weight(Goal goal) const { return weights[static_cast<std::size_t>(goal)]; }
};

// companion, loner, chaos, speedrunner (nullptr: no such persona).
const Persona *PersonaNamed(std::string_view name);
std::vector<std::string> PersonaNames();
} // namespace tandem::sim
