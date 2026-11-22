// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "access.hpp"
#include "animation.hpp"

namespace tandem::plugin::engine {
namespace {
// Address-library hashes, present in 2.50's list.
constexpr std::uint32_t kDriverAttach = 2624656810;
constexpr std::uint32_t kDriverDetach = 2385973466;
constexpr std::uint32_t kDriverSend = 2132808949;
constexpr std::uint32_t kGetAnimLength = 3750304414;

// The engine's functions, found once (0: not in this game, the feature off).
struct Functions {
  void (*attach)(AnimationDriver *, RED4ext::ent::Entity *);
  void (*detach)(AnimationDriver *);
  void (*send)(AnimationDriver *, AnimationData &);
};
const Functions &Driver() {
  const auto f = feature::kMovement;
  static const Functions s_functions{
      Find<decltype(Functions::attach)>(f, "the animation driver's Attach", kDriverAttach),
      Find<decltype(Functions::detach)>(f, "the animation driver's Detach", kDriverDetach),
      Find<decltype(Functions::send)>(f, "the animation driver's Send", kDriverSend)};
  return s_functions;
}
// (ent::AnimationControllerComponent*, CName)
using LengthFn = float (*)(RED4ext::ISerializable *, RED4ext::CName);
LengthFn Length() {
  static const auto s_length = Find<LengthFn>(
      feature::kClipLengths, "AnimationControllerComponent::GetAnimLength", kGetAnimLength);
  return s_length;
}
} // namespace

bool AnimationDriver::Available() {
  const auto &driver = Driver();
  return driver.attach && driver.detach && driver.send;
}

bool CheckClipLengths() {
  if (!Length())
    return false;
  FeatureOn(feature::kClipLengths);
  return true;
}

AnimationDriver::~AnimationDriver() { Detach(); }

// (A controller only exists once CheckMovement has found the three.)
void AnimationDriver::Attach(RED4ext::ent::Entity *entity) {
  if (!Available())
    return;
  Driver().attach(this, entity);
  attached = true;
}

void AnimationDriver::Detach() {
  if (!attached)
    return;
  Driver().detach(this);
  attached = false;
}

void AnimationDriver::Send(AnimationData &data) {
  if (!attached)
    return;
  Driver().send(this, data);
}

float AnimationDriver::ClipLength(RED4ext::CName clip) const {
  const auto length = Length();
  if (!length)
    return 0.f;
  if (const auto controller = component.Lock())
    return length(controller.GetPtr(), clip);
  return 0.f;
}
} // namespace tandem::plugin::engine
