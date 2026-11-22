#pragma once

// The engine's own kinematic move of a vehicle
// (docs/research/engine/vehicles.md §13.141, game 2.50).

namespace RED4ext::ent {
struct Entity;
}

namespace tandem::plugin::engine {
// Moves a vehicle's body to a transform over `seconds`, with the velocity
// that takes it there (farther than 100 m/s away: put there, at rest). False
// when the entity is not a vehicle with a body, or the vehicle class is not
// the one measured (logged once): the caller teleports it then. Game thread.
// The vehicle class is the one measured (checked once; a mismatch is logged
// once): MoveVehicle can be used.
bool CanMoveVehicles();
bool MoveVehicle(RED4ext::ent::Entity *entity, float x, float y, float z, float i, float j, float k,
                 float r, float seconds);
} // namespace tandem::plugin::engine
