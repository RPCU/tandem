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
#include <string>

namespace tandem::plugin::engine {
// At plugin load: hook the entity animation job and the pose commit (by
// address-library hash). They do nothing until a source or a puppet is bound.
bool InstallPoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
void RemovePoseStream(const RED4ext::v1::Sdk *sdk, RED4ext::v1::PluginHandle handle);
// At the first frame (access.hpp): the animated component's and the rig's
// layouts. Off: nothing is bound, the hooks only call the engine's functions
// (no pose is read or written; bodies keep their locomotion animation).
bool CheckPoseStream();

// Game thread. The local V whose pose is read (null: none). Cheap: call it
// again whenever (the animation objects persist while the entity lives).
void SetPoseSource(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
// Game thread. The puppet that shows `player`'s pose (null entity: unbind).
void BindPosePuppet(std::uint32_t player, const RED4ext::Handle<RED4ext::ent::Entity> &entity);
// Log counters and checks since the last report.
void PoseStreamReport();
// (test) a seated puppet's hips: 0 its player's, 1 their height only.
void PoseSeatedHips(int mode);

// Scene replay (docs/architecture/scene-replay.md), game thread. Host: the
// NPC whose body is read as actor `npc` (null entity: no longer). Guest: the
// proxy that shows actor `npc`'s pose while its stream lasts (null: its own
// animation again). Both need the hooks (tandem.ini pose = on) and the same
// layout checks as a player's pose; without them they do nothing.
void SetPoseActorSource(std::uint32_t npc, const RED4ext::Handle<RED4ext::ent::Entity> &entity);
void BindPoseActor(std::uint32_t npc, const RED4ext::Handle<RED4ext::ent::Entity> &entity);
// Another rig of actor `npc` (part: proto::kRigFace its face, kRigBody a
// body that is not the humanoid one). source: the host's NPC, read at its
// rig's commit (a body only when its skeleton is not the humanoid one);
// else a guest's proxy, written there while the host's stream lasts. Null
// entity: let go. ClearPoseActors lets go of all.
void BindPoseRig(std::uint32_t npc, std::uint8_t part, bool source,
                 const RED4ext::Handle<RED4ext::ent::Entity> &entity);
void ClearPoseActors();
// Counters since the last report, in one line.
std::string PoseActorsReport();
} // namespace tandem::plugin::engine
