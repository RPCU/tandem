// What scripts/tandem/Activities.reds can call: `TandemActivity` static
// functions and the `TandemBusyState`, `TandemSkipEvent` structs. Declared
// on the script side in scripts/tandem/ActivityNatives.reds: keep the two in
// sync (tools/check-natives.py reads both). All of these run on the game
// thread; they only touch the queues of activity.hpp (and combat.hpp's
// outbound one for a race's word). QueueRaceUi and QueueRaceReward may run on
// a job thread (script hooks): they only queue.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "activity.hpp"
#include "combat.hpp"
#include "engine/scenes.hpp"
#include "runtime.hpp"

#include "tandem/activity.hpp"
#include "tandem/interpolation.hpp"

#include <RedLib.hpp>

#include <RED4ext/ResourcePath.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

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

// A street race's note, for the scripts (RaceNote; Races.reds). A heard
// Race comes as a head (part 0) followed by one event per point (part 1:
// x, y, z) and one per standings entry (part 2: who, checkpoint, place,
// flags, ago). Times are `ago`: seconds before now on the host clock (-1:
// none), so no absolute time reaches a script float.
struct TandemRaceEvent {
  std::int32_t kind{}; // 1 heard, 2 our race HUD (arg: vehicleRaceUI), 3 our reward
  std::int32_t part{};
  std::uint32_t player{};
  std::uint32_t starter{};
  std::uint32_t to{};
  std::uint32_t race{};
  std::int32_t raceKind{}; // proto::RaceKind
  std::int32_t arg{};
  std::int32_t checkpoint{};  // our HUD: its maxPosition
  std::int32_t checkpoints{}; // our HUD: its maxCheckpoints
  float metres{};
  float heading{};
  float ago{};
  Red::TweakDBID record;
  std::uint32_t questHash{};
  float x{}, y{}, z{};
  std::uint32_t who{};
  std::int32_t place{};
  std::int32_t flags{};
};
static_assert(alignof(TandemRaceEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
float AgoOf(std::uint64_t timeUs, std::uint64_t nowUs) {
  if (timeUs == 0)
    return -1.f;
  return timeUs >= nowUs ? 0.f : static_cast<float>(static_cast<double>(nowUs - timeUs) / 1e6);
}

std::uint64_t TimeOf(float ago, std::uint64_t nowUs) {
  if (!(ago >= 0.f))
    return 0;
  const auto back = static_cast<std::uint64_t>(static_cast<double>(ago) * 1e6);
  return back >= nowUs ? 1 : nowUs - back;
}

// A race's lists, staged by the scripts right before SendRace (game thread).
struct StagedRace {
  std::vector<proto::Vec3> points;
  std::vector<proto::RaceEntry> entries;
};
StagedRace &Staged() {
  static StagedRace staged;
  return staged;
}

void AddRace(Red::DynArray<TandemRaceEvent> &out, const RaceNote &note, std::uint64_t nowUs) {
  TandemRaceEvent head;
  head.kind = static_cast<std::int32_t>(note.kind);
  if (note.kind == RaceNote::Kind::Ui) {
    head.arg = note.mode;
    head.checkpoint = note.maxPosition;
    head.checkpoints = note.maxCheckpoints;
    out.EmplaceBack(head);
    return;
  }
  if (note.kind == RaceNote::Kind::Reward) {
    head.record = Red::TweakDBID(note.reward);
    out.EmplaceBack(head);
    return;
  }
  const auto &race = note.race;
  head.player = race.player;
  head.starter = race.starter;
  head.to = race.to;
  head.race = race.race;
  head.raceKind = static_cast<std::int32_t>(race.kind);
  head.arg = race.arg;
  head.checkpoint = race.checkpoint;
  head.checkpoints = race.checkpoints;
  head.metres = race.metres;
  head.heading = race.heading;
  head.ago = AgoOf(race.timeUs, nowUs);
  head.record = Red::TweakDBID(race.record);
  head.questHash = race.quest;
  head.x = race.at.x;
  head.y = race.at.y;
  head.z = race.at.z;
  out.EmplaceBack(head);
  for (const auto &point : race.points) {
    TandemRaceEvent item;
    item.kind = head.kind;
    item.part = 1;
    item.x = point.x;
    item.y = point.y;
    item.z = point.z;
    out.EmplaceBack(item);
  }
  for (const auto &entry : race.entries) {
    TandemRaceEvent item;
    item.kind = head.kind;
    item.part = 2;
    item.who = entry.who;
    item.checkpoint = entry.checkpoint;
    item.place = entry.place;
    item.flags = entry.flags;
    item.ago = AgoOf(entry.timeUs, nowUs);
    out.EmplaceBack(item);
  }
}

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

  // Street races (docs/architecture/activities.md, "Races"). Our word on
  // one, in our name, to every other game. `ago`: how long before now its
  // time was (-1: none). The lists go first, staged for the next SendRace:
  // the checkpoints known (Info), the standings as parallel lists
  // (Standings).
  static void StageRacePoints(Red::DynArray<Red::Vector4> points) {
    auto &staged = Staged();
    staged.points.clear();
    for (std::uint32_t n = 0; n < points.Size() && staged.points.size() < proto::kMaxRacePoints;
         ++n)
      staged.points.push_back({points[n].X, points[n].Y, points[n].Z});
  }
  static void StageRaceStandings(Red::DynArray<std::uint32_t> who,
                                 Red::DynArray<std::int32_t> passed,
                                 Red::DynArray<std::int32_t> places,
                                 Red::DynArray<std::int32_t> flags, Red::DynArray<float> agos) {
    auto &staged = Staged();
    staged.entries.clear();
    const auto now = Runtime::Get().HostNowUs();
    for (std::uint32_t n = 0;
         n < who.Size() && n < passed.Size() && n < places.Size() && n < flags.Size() &&
         n < agos.Size() && staged.entries.size() < proto::kMaxRacers;
         ++n)
      staged.entries.push_back(
          {.who = who[n],
           .checkpoint = static_cast<std::uint16_t>(std::clamp(passed[n], 0, 65535)),
           .place = static_cast<std::uint8_t>(std::clamp(places[n], 0, 255)),
           .flags =
               static_cast<std::uint8_t>(flags[n] & (proto::kRacerFinished | proto::kRacerOut)),
           .timeUs = TimeOf(agos[n], now)});
  }
  static void SendRace(std::int32_t kind, std::uint32_t starter, std::uint32_t to,
                       std::uint32_t race, std::int32_t arg, std::int32_t checkpoint,
                       std::int32_t checkpoints, float metres, float heading, float ago,
                       Red::TweakDBID record, std::uint32_t questHash, Red::Vector4 at) {
    auto &staged = Staged();
    auto points = std::exchange(staged.points, {});
    auto entries = std::exchange(staged.entries, {});
    if (kind < static_cast<std::int32_t>(proto::RaceKind::Info) ||
        kind > static_cast<std::int32_t>(proto::RaceKind::Route))
      return;
    const auto now = Runtime::Get().HostNowUs();
    proto::Race message{.starter = starter,
                        .to = to,
                        .race = race,
                        .kind = static_cast<proto::RaceKind>(kind),
                        .arg = static_cast<std::uint8_t>(std::clamp(arg, 0, 255)),
                        .checkpoint = static_cast<std::uint16_t>(std::clamp(checkpoint, 0, 65535)),
                        .checkpoints =
                            static_cast<std::uint16_t>(std::clamp(checkpoints, 0, 65535)),
                        .metres = std::isfinite(metres) ? std::max(metres, 0.f) : 0.f,
                        .heading = std::isfinite(heading) ? heading : 0.f,
                        .timeUs = TimeOf(ago, now),
                        .record = record.value,
                        .quest = questHash,
                        .at = {at.X, at.Y, at.Z},
                        .points = std::move(points),
                        .entries = std::move(entries)};
    Combat::Get().Send(std::move(message));
  }
  // What the session said of races, our race HUD's modes and our rewards
  // since the last call (TandemRaceEvent).
  static Red::DynArray<TandemRaceEvent> TakeRaces() {
    Red::DynArray<TandemRaceEvent> out;
    const auto now = Runtime::Get().HostNowUs();
    for (const auto &note : Activities::Get().TakeRaces())
      AddRace(out, note, now);
    return out;
  }
  // Our race HUD was told a mode (hudCarRaceController's event), or the
  // phone key went down or up while a race is offered (101, 102): queued.
  static void QueueRaceUi(std::int32_t mode, std::int32_t maxPosition,
                          std::int32_t maxCheckpoints) {
    Activities::Get().PushRace({.kind = RaceNote::Kind::Ui,
                                .mode = mode,
                                .maxPosition = maxPosition,
                                .maxCheckpoints = maxCheckpoints});
  }
  // Our V got a reward (PlayerPuppet.OnRewardEvent): queued.
  static void QueueRaceReward(Red::TweakDBID reward) {
    Activities::Get().PushRace({.kind = RaceNote::Kind::Reward, .reward = reward.value});
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

RTTI_DEFINE_CLASS(tandem::plugin::TandemRaceEvent, "TandemRaceEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(part);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(starter);
  RTTI_PROPERTY(to);
  RTTI_PROPERTY(race);
  RTTI_PROPERTY(raceKind);
  RTTI_PROPERTY(arg);
  RTTI_PROPERTY(checkpoint);
  RTTI_PROPERTY(checkpoints);
  RTTI_PROPERTY(metres);
  RTTI_PROPERTY(heading);
  RTTI_PROPERTY(ago);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(questHash);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
  RTTI_PROPERTY(who);
  RTTI_PROPERTY(place);
  RTTI_PROPERTY(flags);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemActivity, "TandemActivity", {
  RTTI_METHOD(SetOurs);
  RTTI_METHOD(Others);
  RTTI_METHOD(Changes);
  RTTI_METHOD(AskSkip);
  RTTI_METHOD(AnswerSkip);
  RTTI_METHOD(WithdrawSkip);
  RTTI_METHOD(TakeSkips);
  RTTI_METHOD(StageRacePoints);
  RTTI_METHOD(StageRaceStandings);
  RTTI_METHOD(SendRace);
  RTTI_METHOD(TakeRaces);
  RTTI_METHOD(QueueRaceUi);
  RTTI_METHOD(QueueRaceReward);
  RTTI_METHOD(OurScene);
  RTTI_METHOD(ApartmentScene);
});
