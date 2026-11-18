#pragma once

// The engine's locomotion animation interface, as an NPC movement controller
// feeds it. Layouts and address hashes measured on 2.50 (docs/research/
// engine-knowledge.md §3-4): field order is the ABI, do not reorder.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include <RED4ext/CName.hpp>
#include <RED4ext/Handle.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>

#include <cstddef>
#include <cstdint>

namespace tandem::plugin::engine {
enum class LocomotionStyle : std::int32_t { Idle = 0, Rotation, Walk, Jog, Sprint, Any };
enum class MotionAction : std::int32_t { None = 0, Start = 1, Stop = 2, Move = 3, TurnInPlace = 4 };

// What a movement controller hands the animation graph each frame.
// Engine size 0x3C (this struct pads to 0x40; the engine reads the first 0x3C).
struct AnimationData {
  RED4ext::CName controller;                    // 00
  bool debug{};                                 // 08
  LocomotionStyle style{LocomotionStyle::Idle}; // 0C
  MotionAction action{MotionAction::None};      // 10
  float path{};                                 // 14
  float slope{};                                // 18
  float delta{};                                // 1C
  bool stairs{};                                // 20
  float ground{};                               // 24
  float time{};                                 // 28  clip time
  float footScale{1.f};                         // 2C
  float startAngle{};                           // 30
  bool wrappersUnlocked{};                      // 34  areAnimWrappersUnlocked: inCrouch & co. apply
  float speed{};                                // 38  the graph's blend parameter
};
static_assert(__builtin_offsetof(AnimationData, style) == 0x0C);
static_assert(__builtin_offsetof(AnimationData, time) == 0x28);
static_assert(__builtin_offsetof(AnimationData, speed) == 0x38);

// The engine's animation driver: binds to an entity's animation controller.
// Engine size 0x30. Its functions are the engine's, called by hash.
struct AnimationDriver {
  AnimationDriver() = default;
  ~AnimationDriver();
  AnimationDriver(const AnimationDriver &) = delete;
  AnimationDriver &operator=(const AnimationDriver &) = delete;

  void Attach(RED4ext::ent::Entity *entity);
  void Detach();
  void Send(AnimationData &data);
  float ClipLength(RED4ext::CName clip) const; // 0 when the rig has no such clip

  // Filled by the engine's Attach, zeroed by its Detach; only `component` is
  // read here. (anim::AnimFeature_Locomotion, anim::AnimFeature_CrowdLocomotion,
  // ent::AnimationControllerComponent: base types keep the generated headers out.)
  RED4ext::Handle<RED4ext::ISerializable> locomotion;
  RED4ext::Handle<RED4ext::ISerializable> crowd;
  RED4ext::WeakHandle<RED4ext::ISerializable> component;
  bool attached{};
};
static_assert(__builtin_offsetof(AnimationDriver, component) == 0x20);
} // namespace tandem::plugin::engine
