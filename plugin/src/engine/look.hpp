#pragma once
// A remote V's look: a character customization state (face, body, hair...)
// resolved into appearance keys and scheduled on a puppet by the engine's
// appearance changer, the way the game dresses a player template. V's
// third-person templates carry no meshes of their own: without a look the
// puppet is invisible. Facts: docs/research/engine-knowledge.md §5.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Scripting/Natives/Generated/game/ui/ICharacterCustomizationState.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include "tandem/protocol.hpp"

#include <cstdint>
#include <vector>

namespace tandem::plugin::engine {
// The appearance keys of `state` for a whole third-person V (head, body,
// arms), to send to the others. Empty when the state is unusable (logged).
std::vector<proto::AppearanceKey>
CollectLook(const RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> &state,
            bool male);

// Schedules keys received from another player on `entity`. An entity that
// already wears exactly these keys is left alone (re-applying an identical
// look many times crashed NCO's observers). Answers the number scheduled, 0
// when skipped, -1 on failure (logged).
std::int32_t ScheduleLook(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
                          const std::vector<proto::AppearanceKey> &keys);

// Schedules `state`'s look on `entity` (asynchronous). Answers the number of
// appearance keys scheduled, or -1 when nothing was (logged).
std::int32_t
ApplyLook(const RED4ext::Handle<RED4ext::ent::Entity> &entity,
          const RED4ext::Handle<RED4ext::game::ui::ICharacterCustomizationState> &state, bool male);
} // namespace tandem::plugin::engine
