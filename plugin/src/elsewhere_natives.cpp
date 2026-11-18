// What scripts/tandem/Elsewhere.reds can call: `TandemAway` static functions
// and the `TandemAwayBody`, `TandemAwayState` structs. Declared on the script
// side in scripts/tandem/ElsewhereNatives.reds: keep the two in sync
// (tools/check-natives.py reads both). All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "runtime.hpp"

#include "tandem/elsewhere.hpp"

#include <RedLib.hpp>

namespace tandem::plugin {
// What to send of our V this sample (plain fields: the script VM aligns
// struct locals to 8).
struct TandemAwayBody {
  bool held{}; // the body stays elsewhere: send it (x, y, z, i, j, k, r) and no velocity
  float x{}, y{}, z{};
  float i{}, j{}, k{}, r{1.f};
};
static_assert(alignof(TandemAwayBody) <= 8, "the script VM aligns struct locals to 8");

// A player's place (proto::Elsewhere).
struct TandemAwayState {
  std::uint32_t player{};
  std::int32_t place{}; // proto::Place
  std::int32_t flags{}; // proto::kElsewhere*
  float x{}, y{}, z{};  // the body (moved)
  float yaw{};
};
static_assert(alignof(TandemAwayState) <= 8, "the script VM aligns struct locals to 8");

namespace {
TandemAwayState ToScript(const proto::Elsewhere &state) {
  return {state.player,   static_cast<std::int32_t>(state.place),
          state.flags,    state.anchor.x,
          state.anchor.y, state.anchor.z,
          state.yaw};
}

proto::Place PlaceOf(std::int32_t place) {
  return place > 0 && place <= static_cast<std::int32_t>(proto::Place::Other)
             ? static_cast<proto::Place>(place)
             : proto::Place::City;
}
} // namespace

struct TandemAway : Red::IScriptable {
  // Our V, once per state sent: where it is, what it does, which world the
  // script sees it in. Answers what to send; holds and releases the body's
  // pose, and hands a changed place to the session.
  static TandemAwayBody Sample(Red::Vector4 position, Red::Quaternion orientation, bool mounted,
                               bool settled, std::int32_t place) {
    auto &runtime = Runtime::Get();
    auto &tracker = runtime.Away();
    const auto before = tracker.State();
    const auto body =
        tracker.Feed({.timeUs = net::NowUs(),
                      .position = {position.X, position.Y, position.Z},
                      .orientation = {orientation.i, orientation.j, orientation.k, orientation.r},
                      .mounted = mounted,
                      .settled = settled,
                      .place = PlaceOf(place)});
    if (body.holdPoseFrom) {
      runtime.HoldLocalPose(*body.holdPoseFrom);
      log::Info("elsewhere: our body stays at ({:.1f}, {:.1f}, {:.1f}), its pose held{}",
                body.position.x, body.position.y, body.position.z,
                runtime.LocalPoseHeld() ? "" : " (none yet: live)");
    }
    if (body.releasePose) {
      runtime.ReleaseLocalPose();
      log::Info("elsewhere: back with our body");
    }
    const auto &state = tracker.State();
    if (!(state == before))
      runtime.SubmitLocalElsewhere(state);
    return {body.held,          body.position.x,    body.position.y,    body.position.z,
            body.orientation.i, body.orientation.j, body.orientation.k, body.orientation.r};
  }

  // A new game (a save loaded): our V starts in its body.
  static void Reset() {
    auto &runtime = Runtime::Get();
    runtime.Away().Reset();
    runtime.ReleaseLocalPose();
    runtime.SubmitLocalElsewhere({});
  }

  // Our place as the others are told it.
  static TandemAwayState Ours() { return ToScript(Runtime::Get().Away().State()); }
  // Our body is held where it was (an unexplained teleport included).
  static bool Holding() { return Runtime::Get().Away().Holding(); }

  // The other players not in the city, and how many times that changed.
  static Red::DynArray<TandemAwayState> Others() {
    std::uint32_t changes = 0;
    Red::DynArray<TandemAwayState> out;
    for (const auto &[player, state] : Runtime::Get().RemoteElsewhere(changes))
      out.EmplaceBack(ToScript(state));
    return out;
  }
  static std::uint32_t Changes() {
    std::uint32_t changes = 0;
    Runtime::Get().RemoteElsewhere(changes);
    return changes;
  }

  RTTI_IMPL_TYPEINFO(TandemAway);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemAwayBody, "TandemAwayBody", {
  RTTI_PROPERTY(held);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(i);
  RTTI_PROPERTY(j);
  RTTI_PROPERTY(k);
  RTTI_PROPERTY(r);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemAwayState, "TandemAwayState", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(place);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemAway, "TandemAway", {
  RTTI_METHOD(Sample);
  RTTI_METHOD(Reset);
  RTTI_METHOD(Ours);
  RTTI_METHOD(Holding);
  RTTI_METHOD(Others);
  RTTI_METHOD(Changes);
});
