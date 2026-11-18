#pragma once
// The pose scan (a measuring tool): see posescan.cpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/Scripting/Natives/entEntity.hpp>

namespace tandem::plugin::engine {
// Mark hips-like transforms reachable from the entity (diff = false), or
// report the marked ones that moved since (diff = true).
void PoseMark(const RED4ext::Handle<RED4ext::ent::Entity> &entity, bool diff);
void PoseBones(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
void PoseTree(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
void PoseScan(const RED4ext::Handle<RED4ext::ent::Entity> &entity);
} // namespace tandem::plugin::engine
