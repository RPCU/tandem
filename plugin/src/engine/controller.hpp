#pragma once

// The movement controller a remote V's puppet runs instead of its NPC brain.
//
// The engine asks a movement controller, on its ANIMATION thread, for the
// body's position change this frame (GetDeltaTransform) and for the
// locomotion animation to play (SendAnimationParameters). Ours answers both
// from the remote player's interpolated state, so the body is exactly where
// the network says, with the engine's own walk/jog/sprint animation. How the
// body is placed (ground, air, ladder, water) and when it must be teleported
// instead is tandem::movement::Placer's.
//
// The class layout and its 19 virtual functions are an ENGINE ABI (the
// idle-controller interface, measured on 2.50, docs/research §3): no base
// class, no new virtual function, and members before `player` stay put.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "animation.hpp"

#include "tandem/movement.hpp"
#include "tandem/protocol.hpp"

#include <RED4ext/Memory/Allocators.hpp>
#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/EntityID.hpp>
#include <RED4ext/Scripting/Natives/moveComponent.hpp>

#include <cstdint>

namespace tandem::plugin::engine {
class PuppetController {
public:
  // Marks our controllers (`type` at +8); the engine's use small values.
  static constexpr std::uint32_t kType = 0x7A6D0000;

  explicit PuppetController(proto::PlayerId player);

  // Engine memory for the engine's object lifetime.
  static void *operator new(std::size_t size);
  static void operator delete(void *pointer);

  // ---- engine ABI: 19 slots, this order ----
  virtual RED4ext::Memory::PoolAI_Movement *GetMemoryPool(); // 00
  virtual ~PuppetController();                               // 01
  virtual void PreTick(float delta);                         // 02
  virtual void Tick(float delta);                            // 03
  virtual void GetDeltaTransform(RED4ext::Vector4 &position, // 04
                                 RED4ext::Quaternion &rotation);
  virtual void Unknown05(bool &out);                                                 // 05
  virtual void Unknown06(RED4ext::Vector4 &, RED4ext::Quaternion &);                 // 06
  virtual void Unknown07(RED4ext::Vector4 &, RED4ext::Quaternion &, bool &, bool &); // 07
  virtual void SendAnimationParameters(float delta, RED4ext::Vector4 &position,      // 08
                                       RED4ext::Quaternion &orientation);
  virtual void Unknown09(RED4ext::Vector4 &, RED4ext::Quaternion &, RED4ext::Vector4 &,
                         RED4ext::Quaternion &); // 09
  virtual void Unknown10(RED4ext::Vector4 &, RED4ext::Quaternion &, float, RED4ext::Vector4 &,
                         RED4ext::Quaternion &); // 10
  virtual RED4ext::CName GetName() const;        // 11
  virtual bool Unknown12();                      // 12
  virtual void Unknown13();                      // 13
  virtual void Unknown14();                      // 14
  // The engine passes a pointer to a handle it owns and releases itself: by
  // reference, never by value (the MSVC ABI makes a by-value callee destroy
  // its argument, which released the mount parent once per call and freed
  // car copies too early: engine-knowledge.md §13.6). No engine object is
  // ever taken by value in this vtable.
  virtual void Mount(RED4ext::move::Component &component, // 15
                     const RED4ext::Handle<RED4ext::ent::Entity> &parent);
  virtual void Attach(RED4ext::move::Component &component); // 16
  virtual void Detach(RED4ext::move::Component &component); // 17
  virtual void GetAnimationParameters(AnimationData &data); // 18

  // ---- engine-visible fields ----
  std::uint32_t type{kType};             // 08
  bool attached{};                       // 0C
  bool highPriority{};                   // 0D
  RED4ext::move::Component *component{}; // 10
  RED4ext::ent::EntityID id{};           // 18

  // The engine writes past the fields above (measured on 2.50: +0x20 was set
  // to 1 right after attach, overwriting what was stored there). Keep our own
  // data clear of it; the canary below catches it if 0x60 bytes is not enough.
  std::uint8_t engineScratch[0x60]{};

  // ---- ours ----
  static constexpr std::uint64_t kCanary = 0x74616E64656D4F4BULL; // "tandemOK"
  std::uint64_t canary{kCanary};
  proto::PlayerId player{};

private:
  struct Gait {
    LocomotionStyle style{LocomotionStyle::Idle};
    float clip{}; // seconds, 0 = the rig has no clip for it
    float time{};
  };

  void ChooseGait(float speed, std::uint8_t stance);

  AnimationDriver m_driver;
  Gait m_gait;
  float m_walkClip{}, m_jogClip{}, m_sprintClip{};
  // Clip lengths wait on the rig's animations: asked before they have
  // streamed in, the engine blocks the game thread for good (a fresh NPC
  // proxy hung the game, 2026-11-18). Asked once the graph has run a while.
  std::uint32_t m_sendsUntilClips{};
  void LoadClips();
  float m_speed{};
  std::uint8_t m_stance{};
  // How the body is placed (tandem/movement.hpp): on the ground, or
  // following the stream freely in the air, on a ladder, in the water...
  movement::Placer m_placer;
  movement::Placement m_placement{movement::Placement::Ground};
  std::uint64_t m_lastPlaceUs{};
  std::uint32_t m_placementsSeen{}; // bit per Placement: logged once each
  bool m_loggedDelta{}, m_loggedSend{}, m_loggedNoPlacement{}, m_loggedCanary{};
  std::uint32_t m_frames{};
  std::uint64_t m_lastHealthUs{};
  // The observer's drift watch: since when the body is far from its target
  // (0: it is not), and whether this stretch was told.
  std::uint64_t m_driftSinceUs{};
  bool m_driftTold{};
};
} // namespace tandem::plugin::engine
