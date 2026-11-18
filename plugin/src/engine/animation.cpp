// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "animation.hpp"

#include <RED4ext/Relocation.hpp>

namespace tandem::plugin::engine {
namespace {
// Address-library hashes, present in 2.50's list.
constexpr std::uint32_t kDriverAttach = 2624656810;
constexpr std::uint32_t kDriverDetach = 2385973466;
constexpr std::uint32_t kDriverSend = 2132808949;
constexpr std::uint32_t kGetAnimLength = 3750304414;
} // namespace

AnimationDriver::~AnimationDriver() { Detach(); }

void AnimationDriver::Attach(RED4ext::ent::Entity *entity) {
  static RED4ext::UniversalRelocFunc<void (*)(AnimationDriver *, RED4ext::ent::Entity *)> attach(
      kDriverAttach);
  attach(this, entity);
  attached = true;
}

void AnimationDriver::Detach() {
  if (!attached)
    return;
  static RED4ext::UniversalRelocFunc<void (*)(AnimationDriver *)> detach(kDriverDetach);
  detach(this);
  attached = false;
}

void AnimationDriver::Send(AnimationData &data) {
  if (!attached)
    return;
  static RED4ext::UniversalRelocFunc<void (*)(AnimationDriver *, AnimationData &)> send(
      kDriverSend);
  send(this, data);
}

float AnimationDriver::ClipLength(RED4ext::CName clip) const {
  // (ent::AnimationControllerComponent*, CName)
  static RED4ext::UniversalRelocFunc<float (*)(RED4ext::ISerializable *, RED4ext::CName)> length(
      kGetAnimLength);
  if (const auto controller = component.Lock())
    return length(controller.GetPtr(), clip);
  return 0.f;
}
} // namespace tandem::plugin::engine
