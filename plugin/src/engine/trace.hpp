#pragma once
// The anim feature trace (tandem.ini trace = on): see trace.cpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Scripting/Natives/entEntity.hpp>

namespace tandem::plugin::engine {
bool InstallTrace(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle, bool logging);
// (experiment, tandem.ini mirror = on) the local player's anim features
// applied to this entity's animation controller as well.
void MirrorPlayerTo(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
void ClearMirrors();
void RemoveTrace(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
} // namespace tandem::plugin::engine
