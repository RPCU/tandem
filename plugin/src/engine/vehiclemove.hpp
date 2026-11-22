#pragma once

// The engine's own ways to move a vehicle it does not simulate
// (docs/research/engine/vehicles.md §13.141, §13.155; game 2.50).

#include <string_view>

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
// The startup check (engine/access.cpp): the class size, the move's address
// and, for the kinematic path, the physics state's setter and offset; the
// feature "vehicle move" is on or off after it.
bool CheckVehicleMove();
// How tandem.ini asks the vehicles other games run to be moved
// (vehicle_move: teleport, kinematic, velocity); before the first frame's
// check. Teleport: the feature is "not asked for".
void AskVehicleMove(std::string_view mode);
bool MoveVehicle(RED4ext::ent::Entity *entity, float x, float y, float z, float i, float j, float k,
                 float r, float seconds);

// The engine's own physics state for a vehicle driven kinematically (bit
// 0x100 of vehicleBaseObject's physics state, which a quest's DriveToPoint
// sets before it moves a car with MoveVehicle every frame): its physics
// proxy is kinematic (our world does not push it, gravity and suspension do
// not act on it) and the move puts it where told, with the velocity of the
// motion on its rigid body (what the game reads for the speed). `on` false
// gives its physics back. False when the class or the setter is not the
// one measured (CanHoldVehicles). Game thread.
bool CanHoldVehicles();
bool HoldVehicle(RED4ext::ent::Entity *entity, bool on);
// The state bit is set now.
bool VehicleHeld(RED4ext::ent::Entity *entity);
} // namespace tandem::plugin::engine
