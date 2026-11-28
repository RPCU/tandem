#pragma once

// A map pin's phase set the engine's way (docs/architecture/story.md,
// "Open-world activities"; engine facts: docs/research/engine/story.md
// §13.162, game 2.50): the host's open-world activities shown on a guest's
// own pins, done or open as the host's world has them, for the session only.
// The game's own setter of a point-of-interest mappin's phase, by its
// address-library hash (Codeware's Raw::PointOfInterestMappin::SetPhase,
// MIT); the map, the minimap and the world pins read that phase back
// themselves (IMappin.GetPhase).

#include <cstdint>

namespace RED4ext::game::mappins {
struct IMappin;
}

namespace tandem::plugin::engine {
// The startup check (engine/access.cpp): gamemappinsPointOfInterestMappin
// laid out as measured and the setter's hash in this game's library; the
// feature "activity marks" is on or off after it.
bool CheckActivityMarks();

// Game thread. Sets `mappin` (a PointOfInterestMappin) to `phase`
// (gamedataMappinPhase 0..3). False when the feature is off, the pin is not
// a point of interest, or the engine refused.
bool SetPoiPhase(RED4ext::game::mappins::IMappin *mappin, std::uint8_t phase);
} // namespace tandem::plugin::engine
