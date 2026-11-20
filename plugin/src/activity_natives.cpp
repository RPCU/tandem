// What scripts/tandem/Activities.reds can call: `TandemActivity` static
// functions and the `TandemBusyState`, `TandemSkipEvent` structs. Declared
// on the script side in scripts/tandem/ActivityNatives.reds: keep the two in
// sync (tools/check-natives.py reads both). All of these run on the game
// thread; they only touch the queues of activity.hpp.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "activity.hpp"
#include "engine/scenes.hpp"

#include "tandem/activity.hpp"
#include "tandem/interpolation.hpp"

#include <RedLib.hpp>

#include <RED4ext/ResourcePath.hpp>

#include <algorithm>
#include <array>

namespace tandem::plugin {
// A player's activity (proto::Activity; plain fields: the script VM aligns
// struct locals to 8).
struct TandemBusyState {
  std::uint32_t player{};
  std::int32_t kind{};  // proto::ActivityKind
  std::int32_t flags{}; // proto::kActivity*
  std::uint64_t device{};
  std::uint64_t workspot{};
  std::int32_t entry{};
  Red::CName anim{};
  float x{}, y{}, z{}; // kActivityAnchored: where the body is held
  float yaw{};
};
static_assert(alignof(TandemBusyState) <= 8, "the script VM aligns struct locals to 8");

// A time skip, for the scripts (SkipEvent).
struct TandemSkipEvent {
  std::int32_t kind{};   // 1 asked of us, 2 a vote on ours, 3 ended
  std::uint32_t asker{}; // whose request
  std::uint32_t request{};
  std::int32_t reason{};  // proto::SkipReason
  std::int32_t outcome{}; // proto::SkipOutcome (ended)
  std::uint32_t by{};     // the voter (voted), who ended it (ended)
  std::int32_t why{};     // proto::kSkip*
  bool accepted{};        // voted
  std::int32_t fromSeconds{};
  std::int32_t toSeconds{};
};
static_assert(alignof(TandemSkipEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
TandemBusyState ToScript(const proto::Activity &state) {
  TandemBusyState out;
  out.player = state.player;
  out.kind = static_cast<std::int32_t>(state.kind);
  out.flags = state.flags;
  out.device = state.device;
  out.workspot = state.workspot;
  out.entry = static_cast<std::int32_t>(state.entry);
  out.anim = Red::CName(state.anim);
  out.x = state.anchor.x;
  out.y = state.anchor.y;
  out.z = state.anchor.z;
  out.yaw = state.yaw;
  return out;
}

// The scenes V's homes run their interactions in (shower, bed, coffee, the
// cat, the guitar...): docs/architecture/activities.md, "Apartments";
// engine-knowledge §13.85. Read from the game's resource list (Codeware's
// KnownHashes); the four bought apartments' own scenes are not named there.
constexpr const char *kApartmentScenes[] = {
    R"(base\quest\minor_quests\mq000\scenes\mq000_01_apartment.scene)",
    R"(dlc\dlc6_apart\scenes\dlc6_apart_judy_apartment_interactions.scene)",
    R"(base\open_world\scenes\mirrors\mirror_scene_v_penthouse.scene)",
    R"(base\open_world\scenes\mirrors\mirror_scene_judys_apartment.scene)",
    R"(base\open_world\scenes\mirrors\mirror_scene_peralez_penthouse.scene)",
};

TandemSkipEvent ToScript(const SkipEvent &event) {
  TandemSkipEvent out;
  out.kind = static_cast<std::int32_t>(event.kind);
  switch (event.kind) {
  case SkipEvent::Kind::Asked:
    out.asker = event.skip.from;
    out.request = event.skip.request;
    out.reason = static_cast<std::int32_t>(event.skip.reason);
    out.fromSeconds = event.skip.fromSeconds;
    out.toSeconds = event.skip.toSeconds;
    break;
  case SkipEvent::Kind::Voted:
    out.asker = event.vote.asker;
    out.request = event.vote.request;
    out.by = event.vote.from;
    out.why = event.vote.why;
    out.accepted = event.vote.accepted;
    break;
  case SkipEvent::Kind::Ended:
    out.asker = event.result.asker;
    out.request = event.result.request;
    out.reason = static_cast<std::int32_t>(event.result.reason);
    out.outcome = static_cast<std::int32_t>(event.result.outcome);
    out.by = event.result.by;
    out.why = event.result.why;
    out.fromSeconds = event.result.fromSeconds;
    out.toSeconds = event.result.toSeconds;
    break;
  }
  return out;
}
} // namespace

struct TandemActivity : Red::IScriptable {
  // What our V is busy with (kind 0: nothing). Sent when it changes; kept
  // for the next session.
  static void SetOurs(std::int32_t kind, std::int32_t flags, std::uint64_t device,
                      std::uint64_t workspot, std::int32_t entry, Red::CName anim,
                      Red::Vector4 position, Red::Quaternion orientation) {
    proto::Activity state{
        .kind = static_cast<proto::ActivityKind>(std::clamp(kind, 0, 255)),
        .flags = static_cast<std::uint8_t>(std::clamp(flags, 0, 255)),
        .device = device,
        .workspot = workspot,
        .entry = static_cast<std::uint32_t>(std::max(entry, 0)),
        .anim = anim.hash,
        .anchor = {position.X, position.Y, position.Z},
        .yaw = sync::YawOf({orientation.i, orientation.j, orientation.k, orientation.r})};
    Activities::Get().SetLocal(state);
  }

  // The other players busy with something, and how many times that changed.
  static Red::DynArray<TandemBusyState> Others() {
    std::uint32_t changes = 0;
    Red::DynArray<TandemBusyState> out;
    for (const auto &[player, state] : Activities::Get().Remote(changes))
      out.EmplaceBack(ToScript(state));
    return out;
  }
  static std::uint32_t Changes() {
    std::uint32_t changes = 0;
    Activities::Get().Remote(changes);
    return changes;
  }

  // Time skips (docs/architecture/activities.md). Ask the others to move
  // the clock from `fromSeconds` (ours) to `toSeconds` (GameTime seconds);
  // reason 0 the wheel, 1 a bed, 2 the host's story (said, not asked). How
  // it ends comes as an event.
  static void AskSkip(std::int32_t reason, std::int32_t fromSeconds, std::int32_t toSeconds) {
    Activities::Get().Send(
        SkipAsk{.reason = static_cast<proto::SkipReason>(std::clamp(reason, 0, 2)),
                .fromSeconds = fromSeconds,
                .toSeconds = toSeconds});
  }
  static void AnswerSkip(std::uint32_t asker, std::uint32_t request, bool accepted,
                         std::int32_t why) {
    Activities::Get().Send(
        SkipAnswer{.asker = asker,
                   .request = request,
                   .accepted = accepted,
                   .why = static_cast<std::uint8_t>(std::clamp(why, 0, int{proto::kSkipWhyMax}))});
  }
  static void WithdrawSkip() { Activities::Get().Send(SkipWithdraw{}); }
  static Red::DynArray<TandemSkipEvent> TakeSkips() {
    Red::DynArray<TandemSkipEvent> out;
    for (const auto &event : Activities::Get().TakeEvents())
      out.EmplaceBack(ToScript(event));
    return out;
  }

  // The scene our game plays now (its quest node ran in the last 3 s; 0:
  // none, or the scene hook is out): its resource path hash.
  static std::uint64_t OurScene() { return engine::ScenePlaying(3000); }
  // That scene is one of V's homes' interactions.
  static bool ApartmentScene(std::uint64_t scene) {
    static const auto hashes = [] {
      std::array<std::uint64_t, std::size(kApartmentScenes)> out{};
      for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = RED4ext::ResourcePath(kApartmentScenes[i]).hash;
      return out;
    }();
    return scene != 0 && std::ranges::find(hashes, scene) != hashes.end();
  }

  RTTI_IMPL_TYPEINFO(TandemActivity);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemBusyState, "TandemBusyState", {
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(device);
  RTTI_PROPERTY(workspot);
  RTTI_PROPERTY(entry);
  RTTI_PROPERTY(anim);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(yaw);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemSkipEvent, "TandemSkipEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(asker);
  RTTI_PROPERTY(request);
  RTTI_PROPERTY(reason);
  RTTI_PROPERTY(outcome);
  RTTI_PROPERTY(by);
  RTTI_PROPERTY(why);
  RTTI_PROPERTY(accepted);
  RTTI_PROPERTY(fromSeconds);
  RTTI_PROPERTY(toSeconds);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemActivity, "TandemActivity", {
  RTTI_METHOD(SetOurs);
  RTTI_METHOD(Others);
  RTTI_METHOD(Changes);
  RTTI_METHOD(AskSkip);
  RTTI_METHOD(AnswerSkip);
  RTTI_METHOD(WithdrawSkip);
  RTTI_METHOD(TakeSkips);
  RTTI_METHOD(OurScene);
  RTTI_METHOD(ApartmentScene);
});
