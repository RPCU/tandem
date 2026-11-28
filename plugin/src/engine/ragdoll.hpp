#pragma once

// A ragdoll's bodies, read and set the way the game's cut multiplayer
// replicated them (docs/research/engine/presence.md §13.162, measured on
// 2.50): entRagdollComponent writes its physics ragdoll's body transforms
// into an entRagdollComponentReplicatedState (vtable slot 52) and applies
// one to its ragdoll (slot 53): every body set where the state says, at
// rest when the state sleeps. Nothing is called through the component's
// vtable: the two physics functions those slots call are, by hash.

#include <cstdint>
#include <string>
#include <vector>

namespace RED4ext::ent {
struct Entity;
}

namespace tandem::plugin::engine {
// One body of a ragdoll, in world space (a Transform: position, then the
// orientation's quaternion).
struct RagdollBody {
  float x = 0.f, y = 0.f, z = 0.f;
  float i = 0.f, j = 0.f, k = 0.f, r = 1.f;
};

// The startup check (engine/access.cpp): the functions' code as measured;
// the feature "ragdoll state" is on or off after it.
bool CheckRagdoll();

// Game thread. The bodies of the entity's ragdoll (its entRagdollComponent's
// physics ragdoll), in the ragdoll's own order; empty when it has none (or
// the feature is off), `why` says why.
std::vector<RagdollBody> ReadRagdoll(RED4ext::ent::Entity *entity, std::string &why);

// Game thread. Sets every body of the entity's ragdoll (the same count, the
// same order as ReadRagdoll's), its velocities cleared: the bodies lie
// there at rest. "" when done, else why not (nothing set then).
std::string WriteRagdoll(RED4ext::ent::Entity *entity, const std::vector<RagdollBody> &bodies);
} // namespace tandem::plugin::engine
