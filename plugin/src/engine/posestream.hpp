#pragma once
// Body pose streaming (docs/research/pose-streaming.md, fifth and sixth
// passes): read the local V's pose after the engine animated it, write a
// remote player's pose on their puppet where the graph hands its result over.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <cstdint>

namespace tandem::plugin::engine {
// At plugin load: hook the entity animation job and the pose commit (by
// address-library hash). They do nothing until a source or a puppet is bound.
bool InstallPoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
void RemovePoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);

// Game thread. The local V whose pose is read (null: none). Cheap: call it
// again whenever (the animation objects persist while the entity lives).
void SetPoseSource(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
// Game thread. The puppet that shows `player`'s pose (null entity: unbind).
void BindPosePuppet(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity);
// Log counters and checks since the last report.
void PoseStreamReport();
} // namespace tandem::plugin::engine
