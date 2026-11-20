#pragma once
// Would an interaction cast its doer? (docs/architecture/story.md, "Quest
// interactions"; engine facts: research/engine/story.md §13.65.)
//
// A device action reaches the device's entity as an event: its persistent
// state handles it first, then the entity's own callback for that event's
// class runs (the class's event listeners name it, by the event's type id).
// That callback, or what it calls, is where the game puts the doer in a
// workspot: a personal link, a computer's engineering spot, a turret taken
// apart, a door forced open with V's own hands. This reads the game's own
// compiled scripts at run time: from the callbacks of the entity's class
// (and its parents, and the state's) for the action's class, it follows
// the functions they call, a few levels deep, and says whether any of them
// calls the workspot system's PlayInDevice / PlayInDeviceSimple or the
// device's EnterWorkspot / EnterWorkspotWithIK. No list of devices or
// actions: the same code is read for every device in the game.

#include <cstdint>
#include <string>

namespace tandem::plugin::engine {
enum class Casts : std::int32_t {
  Unknown = -1, // a class it names is not in the RTTI, or the layout is not as measured
  No = 0,
  Yes = 1,
};

// Game thread. Cached per (entity class, action class). `why`: the call
// chain that led to a workspot, or why the answer is unknown.
Casts ActionCastsDoer(std::uint64_t entityClass, std::uint64_t stateClass,
                      std::uint64_t actionClass, std::string &why);
} // namespace tandem::plugin::engine
