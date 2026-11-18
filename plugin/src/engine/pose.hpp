#pragma once
// The pose hook (docs/research/pose-streaming.md): see pose.cpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <string>

namespace tandem::plugin::engine {
// Remember what the hook needs; nothing is hooked until InstallPoseHook.
void SetPoseHookContext(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
// Game thread, once RTTI holds the game's classes. False (and logged) when the
// engine does not look like the one the hook was measured on.
bool InstallPoseHook();
void RemovePoseHook();

// (measure) Look for pointers to this entity or its components in the
// arguments of the next sample calls whose pose has the entity's bone count.
void PoseWatch(const RED4ext::Handle<RED4ext::ent::Entity> &entity, const std::string &label,
               int depth);
// (experiment) The source's pose is written on every pose with the target's
// bone count (the target and every body sharing its rig). Null: off.
void PoseCopy(const RED4ext::Handle<RED4ext::ent::Entity> &source,
              const RED4ext::Handle<RED4ext::ent::Entity> &target);
// Log what the hook saw since the last report.
void PoseReport();
} // namespace tandem::plugin::engine
