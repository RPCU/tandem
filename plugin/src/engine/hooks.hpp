#pragma once

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Handle.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <cstdint>

namespace tandem::plugin::engine {
// At plugin load: the idle-controller hook that spots our puppets.
bool InstallHooks(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
void RemoveHooks(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);

// Game thread: give a puppet our movement controller. False (and logged) when
// the entity has no move component or the engine layouts don't match.
bool AttachController(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity);
} // namespace tandem::plugin::engine
