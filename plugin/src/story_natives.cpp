// What scripts/tandem/Story.reds can call: `TandemStory` static functions and
// the `TandemStoryEvent` struct. Declared on the script side in
// scripts/tandem/StoryNatives.reds: keep the two in sync (tools/check-natives.py
// reads both pairs). All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "config.hpp"
#include "engine/casts.hpp"
#include "engine/posestream.hpp"
#include "engine/quests.hpp"
#include "engine/scenes.hpp"
#include "log.hpp"
#include "mods.hpp"
#include "runtime.hpp"
#include "story.hpp"

#include "tandem/carryover.hpp"
#include "tandem/interact.hpp"

#include <RedLib.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace Red {
RTTI_MAP_TYPE_NAME(CRUID, "CRUID");
}

namespace tandem::plugin {
// One thing the session told the story script (kinds in StoryNatives.reds).
// Plain fields only (the script VM aligns struct locals to 8).
struct TandemStoryEvent {
  std::int32_t kind{};
  std::uint32_t player{};
  std::uint32_t hash{};
  std::int32_t state{};
  std::int32_t flags{};
  std::int32_t index{};
  std::int32_t extra{};
  Red::CName contact;
  std::uint64_t line{};
  float duration{};
  std::uint32_t npc{};
  Red::TweakDBID reward;
  Red::CString name;
  Red::CString text;
  // Quest interactions (kinds 60, 61): the object, the choice, its detail,
  // the layer, where the asker's V stood.
  Red::EntityID entity;
  Red::CName choice;
  std::uint64_t detail{};
  Red::CName layer;
  float x{}, y{}, z{};
};
static_assert(alignof(TandemStoryEvent) <= 8, "the script VM aligns struct locals to 8");

namespace {
// Kinds, as StoryNatives.reds lists them.
enum Kind : std::int32_t {
  kInSession = 1,
  kEnded = 2,
  kJoined = 3,
  kLeft = 4,
  kSnapshotBegin = 10,
  kJournalEntry = 11,
  kSnapshotEnd = 12,
  kTracked = 14,
  kCall = 20,
  kPhoneAction = 21,
  kReply = 22,
  kLine = 30,
  kHub = 31,
  kHubOption = 32,
  kCursor = 33,
  kState = 40,
  kReward = 41,
  kEffect = 42,
  kVehicle = 43,
  kRestrictions = 44,
  kSceneBegin = 50,
  kSceneChange = 51,
  kSceneEnd = 52,
  kSceneActor = 53,
  kSceneActorGone = 54,
  kSceneVoice = 55,
  kSceneVoiceStop = 56,
  kSceneProp = 57,
  kScenePropGone = 58,
  kInteractAsked = 60,
  kInteractAnswer = 61,
};

// What our V earned in co-op (tandem/carryover.hpp): loaded from the save's
// words by the script, stored back after each change.
carry::Ledger &TheLedger() {
  static carry::Ledger ledger;
  return ledger;
}

// Scene replay: a guest's cue, as StoryNatives.reds lists it.
void Flatten(const scene::Cue &cue, Red::DynArray<TandemStoryEvent> &out) {
  using K = scene::Cue::Kind;
  switch (cue.kind) {
  case K::Begin:
  case K::Change:
  case K::End:
    out.EmplaceBack(TandemStoryEvent{.kind = cue.kind == K::Begin    ? kSceneBegin
                                             : cue.kind == K::Change ? kSceneChange
                                                                     : kSceneEnd,
                                     .hash = cue.state.play,
                                     .state = cue.state.tier,
                                     .flags = cue.state.flags,
                                     .line = cue.state.scene,
                                     .duration = cue.seconds});
    return;
  case K::Actor:
  case K::ActorGone:
    out.EmplaceBack(TandemStoryEvent{.kind = cue.kind == K::Actor ? kSceneActor : kSceneActorGone,
                                     .flags = cue.actor.flags,
                                     .npc = cue.actor.npc});
    return;
  case K::Voice:
  case K::VoiceStop:
    out.EmplaceBack(TandemStoryEvent{.kind = cue.kind == K::Voice ? kSceneVoice : kSceneVoiceStop,
                                     .player = cue.line.speakerPlayer,
                                     .hash = cue.line.play,
                                     .flags = cue.line.flags,
                                     .index = static_cast<std::int32_t>(cue.line.duration * 1000.f),
                                     .line = cue.line.line,
                                     .duration = cue.seconds,
                                     .npc = cue.line.speaker});
    return;
  case K::Prop:
  case K::PropGone: {
    // An item our game lacks (a mod's): nothing in the hand, never another.
    constexpr proto::PlayerId host = 1;
    out.EmplaceBack(TandemStoryEvent{.kind = cue.kind == K::Prop ? kSceneProp : kScenePropGone,
                                     .player = cue.prop.holderPlayer,
                                     .hash = cue.prop.prop,
                                     .flags = cue.prop.flags,
                                     .npc = cue.prop.holder,
                                     .reward = Red::TweakDBID(ModRecords::Get().Resolve(
                                         mods::Kind::Item, cue.prop.record, {}, host))});
    return;
  }
  }
}

bool IsScene(const proto::Message &message) {
  return std::holds_alternative<proto::SceneState>(message) ||
         std::holds_alternative<proto::SceneCast>(message) ||
         std::holds_alternative<proto::SceneLine>(message) ||
         std::holds_alternative<proto::SceneProp>(message);
}

void Flatten(const StoryInbound &in, Red::DynArray<TandemStoryEvent> &out) {
  using K = StoryInbound::Kind;
  switch (in.kind) {
  case K::InSession:
    out.EmplaceBack(TandemStoryEvent{.kind = kInSession, .player = in.player, .hash = in.host});
    return;
  case K::Ended:
    // The host is gone, or we left: its scene stops here first (voices cut,
    // every proxy its own again), then the session's end.
    for (const auto &cue : Story::Get().Replay().Reset())
      Flatten(cue, out);
    Runtime::Get().ForgetActorPoses();
    out.EmplaceBack(TandemStoryEvent{.kind = kEnded});
    return;
  case K::Joined:
    out.EmplaceBack(TandemStoryEvent{.kind = kJoined, .player = in.player});
    return;
  case K::Left:
    out.EmplaceBack(TandemStoryEvent{.kind = kLeft, .player = in.player});
    return;
  case K::Message:
    break;
  }
  const auto &m = in.message;
  if (IsScene(m)) {
    // Scene replay off: the host's scenes are not played here.
    if (!Runtime::Get().config.sceneReplay)
      return;
    for (const auto &cue : Story::Get().Replay().Note(m, Runtime::Get().HostNowUs()))
      Flatten(cue, out);
    return;
  }
  if (const auto *journal = std::get_if<proto::Journal>(&m)) {
    if (journal->snapshot)
      out.EmplaceBack(TandemStoryEvent{
          .kind = kSnapshotBegin, .index = static_cast<std::int32_t>(journal->entries.size())});
    for (const auto &entry : journal->entries)
      out.EmplaceBack(TandemStoryEvent{.kind = kJournalEntry,
                                       .hash = entry.hash,
                                       .state = entry.state,
                                       .flags = entry.notify,
                                       .extra = journal->snapshot ? 1 : 0});
    if (journal->snapshot)
      out.EmplaceBack(TandemStoryEvent{.kind = kSnapshotEnd});
  } else if (const auto *tracked = std::get_if<proto::Tracked>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kTracked, .hash = tracked->hash});
  } else if (const auto *call = std::get_if<proto::PhoneCall>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kCall,
                                     .player = call->answeredBy,
                                     .state = call->phase,
                                     .flags = call->flags,
                                     .index = call->mode,
                                     .extra = call->visuals,
                                     .contact = Red::CName(call->contact)});
  } else if (const auto *action = std::get_if<proto::PhoneAction>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kPhoneAction,
                                     .player = action->player,
                                     .index = action->action,
                                     .contact = Red::CName(action->contact)});
  } else if (const auto *reply = std::get_if<proto::PhoneReply>(&m)) {
    out.EmplaceBack(
        TandemStoryEvent{.kind = kReply, .player = reply->player, .hash = reply->choiceEntry});
  } else if (const auto *line = std::get_if<proto::Subtitle>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kLine,
                                     .player = line->speakerPlayer,
                                     .flags = line->flags,
                                     .index = line->type,
                                     .line = line->line,
                                     .duration = line->duration,
                                     .npc = line->speaker,
                                     .name = Red::CString(line->speakerName.c_str()),
                                     .text = Red::CString(line->text.c_str())});
  } else if (const auto *hub = std::get_if<proto::ChoiceHub>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kHub,
                                     .flags = hub->flags,
                                     .index = static_cast<std::int32_t>(hub->options.size()),
                                     .extra = hub->hub,
                                     .duration = hub->timedSeconds,
                                     .name = Red::CString(hub->title.c_str())});
    for (std::size_t i = 0; i < hub->options.size(); ++i)
      out.EmplaceBack(TandemStoryEvent{.kind = kHubOption,
                                       .flags = hub->options[i].types,
                                       .index = static_cast<std::int32_t>(i),
                                       .extra = hub->hub,
                                       .text = Red::CString(hub->options[i].text.c_str())});
  } else if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kCursor,
                                     .player = cursor->player,
                                     .state = cursor->kind,
                                     .index = cursor->index,
                                     .extra = cursor->hub});
  } else if (const auto *state = std::get_if<proto::StoryState>(&m)) {
    out.EmplaceBack(TandemStoryEvent{
        .kind = kState, .player = state->player, .state = state->tier, .flags = state->flags});
  } else if (const auto *effect = std::get_if<proto::StoryEffect>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kEffect,
                                     .player = effect->player,
                                     .flags = effect->applied ? 1 : 0,
                                     .reward = Red::TweakDBID(effect->effect)});
  } else if (const auto *reward = std::get_if<proto::QuestReward>(&m)) {
    // A reward or a car our game lacks (a modded quest's): never given
    // (none; the script skips it), never replaced by another.
    constexpr proto::PlayerId host = 1; // the story's: the host's (always player 1)
    out.EmplaceBack(TandemStoryEvent{.kind = kReward,
                                     .hash = reward->quest,
                                     .reward = Red::TweakDBID(ModRecords::Get().Resolve(
                                         mods::Kind::Item, reward->reward, {}, host))});
  } else if (const auto *unlock = std::get_if<proto::VehicleUnlock>(&m)) {
    out.EmplaceBack(TandemStoryEvent{.kind = kVehicle,
                                     .hash = unlock->quest,
                                     .reward = Red::TweakDBID(ModRecords::Get().Resolve(
                                         mods::Kind::Item, unlock->vehicle, {}, 1))}); // the host's
  } else if (const auto *qi = std::get_if<proto::QuestInteract>(&m)) {
    auto &story = Story::Get();
    if (qi->verdict == proto::kInteractAsk) {
      // Host: a guest's question, kept for its answer (and the reach check).
      story.InteractAsks()[{qi->player, qi->seq}] = {*qi, Runtime::Get().HostNowUs()};
    } else {
      // Guest: the host's verdict, known for the session.
      story.InteractKnown().Note(interact::KeyOf(*qi), qi->verdict);
    }
    out.EmplaceBack(TandemStoryEvent{.kind = qi->verdict == proto::kInteractAsk ? kInteractAsked
                                                                                : kInteractAnswer,
                                     .player = qi->player,
                                     .hash = qi->seq,
                                     .state = qi->verdict,
                                     .flags = qi->flags,
                                     .entity = Red::EntityID(qi->target),
                                     .choice = Red::CName(qi->choice),
                                     .detail = qi->detail,
                                     .layer = Red::CName(qi->layer),
                                     .x = qi->position.x,
                                     .y = qi->position.y,
                                     .z = qi->position.z});
  } else if (const auto *restrictions = std::get_if<proto::StoryRestrictions>(&m)) {
    // The host's set, kept here for RestrictUpdate; the script hears how
    // many (and acts on its next check).
    Story::Get().Restrictions().Host(*restrictions);
    out.EmplaceBack(
        TandemStoryEvent{.kind = kRestrictions,
                         .player = restrictions->player,
                         .index = static_cast<std::int32_t>(restrictions->effects.size())});
  }
}

Red::DynArray<Red::TweakDBID> Ids(const std::vector<std::uint64_t> &effects) {
  Red::DynArray<Red::TweakDBID> out;
  for (const auto effect : effects)
    out.EmplaceBack(Red::TweakDBID(effect));
  return out;
}

std::uint8_t Byte(std::int32_t value) { return static_cast<std::uint8_t>(value); }
} // namespace

struct TandemStory : Red::IScriptable {
  static bool StoryOn() {
    // The chosen session (tandem.ini's or the co-op menu's).
    return Runtime::Get().config.story && Runtime::Get().ChosenRole() != Config::Role::Off;
  }

  // The quest freeze (engine/quests.cpp): there (tandem.ini quest_freeze =
  // on, layouts as measured), on or off, what it holds.
  static bool FreezeAvailable() { return engine::QuestFreezeAvailable(); }
  static bool Freeze(bool on) { return engine::SetQuestFreeze(on); }
  static bool Frozen() { return engine::QuestFrozen(); }
  static std::uint32_t FreezeHeld() { return engine::QuestFreezeHeld(); }
  static bool FreezeLost() { return engine::QuestFreezeOverflowed(); }
  // Ownership of gigs: grouped with the host (the default) or apart.
  static void Grouped(bool grouped) { engine::SetQuestGrouped(grouped); }
  static bool IsGrouped() { return engine::QuestGrouped(); }
  // Spike C2: log phases and their journal nodes.
  static void QuestTrace(bool on) { engine::SetQuestTrace(on); }
  static Red::CString FreezeReport() { return Red::CString(engine::QuestFreezeReport().c_str()); }

  // What the session said since the last call, oldest first.
  static Red::DynArray<TandemStoryEvent> TakeEvents() {
    Red::DynArray<TandemStoryEvent> out;
    auto &story = Story::Get();
    for (const auto &inbound : story.TakeInbound()) {
      story.Note(inbound);
      Flatten(inbound, out);
    }
    return out;
  }

  static std::uint32_t You() { return Story::Get().You(); }
  static std::uint32_t HostId() { return Story::Get().Host(); }

  // Host: journal states (a snapshot: every entry that is not Inactive).
  static void SendJournal(bool snapshot, Red::DynArray<std::uint32_t> hashes,
                          Red::DynArray<std::int32_t> states, Red::DynArray<bool> notify) {
    proto::Journal journal{.snapshot = snapshot};
    for (std::uint32_t i = 0; i < hashes.Size() && i < states.Size() &&
                              journal.entries.size() < proto::kMaxJournalEntries;
         ++i)
      journal.entries.push_back(
          {hashes[i], Byte(states[i]), static_cast<std::uint8_t>(i < notify.Size() && notify[i])});
    if (snapshot)
      log::Info("journal: snapshot of {} entries sent", journal.entries.size());
    Story::Get().Send(std::move(journal));
  }

  static void SendTracked(std::uint32_t hash) { Story::Get().Send(proto::Tracked{hash}); }

  // Host: its PhoneSystem's call (flags: proto::kCall*).
  static void SendCall(Red::CName contact, std::int32_t phase, std::int32_t mode,
                       std::int32_t visuals, std::int32_t flags, std::uint32_t answeredBy) {
    Story::Get().Send(proto::PhoneCall{.contact = contact.hash,
                                       .phase = Byte(phase),
                                       .mode = Byte(mode),
                                       .visuals = Byte(visuals),
                                       .flags = Byte(flags),
                                       .answeredBy = answeredBy});
  }

  // Guest: answer (1), reject (2) or dial (3).
  static void SendPhoneAction(std::int32_t action, Red::CName contact) {
    Story::Get().Send(proto::PhoneAction{.action = Byte(action), .contact = contact.hash});
  }

  // Guest: a text reply (the choice entry's journal hash).
  static void SendReply(std::uint32_t entry) {
    Story::Get().Send(proto::PhoneReply{.choiceEntry = entry});
  }

  // Host: a dialogue line shown (flags proto::kLine*; kLineHide: hidden).
  static void SendLine(std::uint64_t line, std::int32_t type, std::int32_t flags, float duration,
                       std::uint32_t speakerPlayer, Red::CString speakerName, Red::CString text) {
    Story::Get().Send(proto::Subtitle{.line = line,
                                      .type = Byte(type),
                                      .flags = Byte(flags),
                                      .duration = duration,
                                      .speakerPlayer = speakerPlayer,
                                      .speakerName = speakerName.c_str(),
                                      .text = text.c_str()});
  }

  // Host: its active dialogue hub (no options: closed).
  static void SendHub(std::int32_t hub, std::int32_t flags, Red::CString title,
                      Red::DynArray<Red::CString> texts, Red::DynArray<std::int32_t> types,
                      float timed) {
    proto::ChoiceHub message{.hub = hub,
                             .flags = Byte(flags),
                             .title = title.c_str(),
                             .timedSeconds = timed,
                             .timedStartUs = timed > 0.f ? Runtime::Get().HostNowUs() : 0};
    for (std::uint32_t i = 0; i < texts.Size() && message.options.size() < proto::kMaxChoiceOptions;
         ++i)
      message.options.push_back(
          {texts[i].c_str(), static_cast<std::uint16_t>(i < types.Size() ? types[i] : 0)});
    Story::Get().Send(std::move(message));
  }

  // Either: our cursor on the hub (kind proto::kCursor*).
  static void SendCursor(std::int32_t hub, std::int32_t index, std::int32_t kind) {
    Story::Get().Send(proto::ChoiceCursor{
        .hub = hub, .index = static_cast<std::int8_t>(index), .kind = Byte(kind)});
  }

  // Either: our scene tier and story flags (proto::kStory*).
  static void SendState(std::int32_t tier, std::int32_t flags) {
    Story::Get().Send(proto::StoryState{.tier = Byte(tier), .flags = Byte(flags)});
  }

  // Host: a quest's journal moved (the gameJournalQuest's hash), our V here.
  static void QuestMoved(std::uint32_t quest, float x, float y, float z) {
    Story::Get().QuestMoved(quest, {x, y, z});
  }

  // Host: our V got a quest reward: the guests that took part get it too.
  // Answers how many.
  static std::int32_t Reward(Red::TweakDBID reward, float x, float y, float z) {
    const auto award = Story::Get().Reward({x, y, z});
    for (const auto player : award.players)
      Story::Get().Send(proto::QuestReward{.reward = reward.value, .quest = award.quest}, player);
    log::Info("reward: {:#x} (quest {:#x}) to {} guest(s)", reward.value, award.quest,
              award.players.size());
    return static_cast<std::int32_t>(award.players.size());
  }

  // Host: a vehicle came into our garage (a quest's, a gig's, a fixer's):
  // the guests that took part are told, by the reward rule. Answers how many.
  static std::int32_t VehicleUnlocked(Red::TweakDBID vehicle, float x, float y, float z) {
    const auto award = Story::Get().Reward({x, y, z});
    for (const auto player : award.players)
      Story::Get().Send(proto::VehicleUnlock{.vehicle = vehicle.value, .quest = award.quest},
                        player);
    log::Info("garage: vehicle {:#x} (quest {:#x}) to {} guest(s)", vehicle.value, award.quest,
              award.players.size());
    return static_cast<std::int32_t>(award.players.size());
  }
  // tandem.ini vehicle_rewards = keep: a vehicle earned with a host is
  // unlocked in our own garage too (credit: only noted in the ledger).
  static bool VehicleRewardsKept() { return Runtime::Get().config.vehicleRewardsKept; }

  // The ledger of what our V earned in co-op (docs/architecture/
  // quest-carryover.md): the save's words in, the words to save out.
  static void CarryLoad(Red::DynArray<std::uint64_t> words) {
    auto &ledger = TheLedger();
    ledger = {};
    const std::vector<std::uint64_t> saved(words.begin(), words.end());
    ledger.Unpack(saved);
    log::Info("ledger: {} words from the save: {} quests, {} vehicles", saved.size(),
              ledger.Entries().size(), ledger.Vehicles().size());
  }
  static Red::DynArray<std::uint64_t> CarryWords() {
    Red::DynArray<std::uint64_t> out;
    for (const auto word : TheLedger().Pack())
      out.EmplaceBack(word);
    return out;
  }
  // A vehicle earned with a host (owned: our own garage had it already).
  // True the first time.
  static bool CarryVehicle(Red::TweakDBID vehicle, std::uint32_t quest, bool owned) {
    return TheLedger().NoteVehicle(vehicle.value, quest, owned);
  }
  // It was unlocked in our own garage.
  static void CarryVehicleKept(Red::TweakDBID vehicle) { TheLedger().KeepVehicle(vehicle.value); }
  // Every vehicle credited; whether one was kept.
  static Red::DynArray<Red::TweakDBID> CarryVehicles() {
    Red::DynArray<Red::TweakDBID> out;
    for (const auto &vehicle : TheLedger().Vehicles())
      out.EmplaceBack(Red::TweakDBID(vehicle.record));
    return out;
  }
  static bool CarryVehicleIsKept(Red::TweakDBID vehicle) {
    const auto *found = TheLedger().FindVehicle(vehicle.value);
    return found && found->kept;
  }

  // Guest: we took part in a step of the host's quest (kind: its
  // gameJournalQuestType; own: our own journal's state of it before the
  // mirror, gameJournalEntryState: 1 not started, 2 active, 3 done, 4 failed).
  // The first note stands.
  static void CarryQuest(std::uint32_t quest, std::int32_t kind, std::int32_t own,
                         Red::CString host) {
    const auto state =
        own >= 1 && own <= 4 ? static_cast<carry::OwnState>(own) : carry::OwnState::Unknown;
    const auto type =
        kind >= 0 && kind <= 9 ? static_cast<carry::QuestKind>(kind) : carry::QuestKind::Unknown;
    TheLedger().NoteQuest(quest, type, state, carry::HostHash(host.c_str()));
  }
  // The host's quest reached its end with us.
  static void CarryCompleted(std::uint32_t quest) { TheLedger().NoteCompleted(quest); }
  // Guest: the host's reward for our V. 0 pay it (recorded now), 1 paid
  // already in co-op, 2 we finished that quest alone before.
  static std::int32_t CarryPay(std::uint32_t quest, Red::TweakDBID reward) {
    return static_cast<std::int32_t>(TheLedger().PayReward(quest, reward.value));
  }
  // Our own story pays `reward` for `quest`: false when co-op paid it.
  static bool CarryOwnPays(std::uint32_t quest, Red::TweakDBID reward) {
    return TheLedger().PayInOwnStory(quest, reward.value);
  }
  // "done with NAME": the quest was played with a host of that name.
  static bool CarryWith(std::uint32_t quest, Red::CString host) {
    const auto *entry = TheLedger().Find(quest);
    return entry && entry->host == carry::HostHash(host.c_str());
  }
  // One line for tools/game.sh cmd carry.
  static Red::CString CarryReport() {
    const auto &ledger = TheLedger();
    std::size_t completed = 0, rewards = 0;
    for (const auto &entry : ledger.Entries()) {
      completed += entry.completed ? 1 : 0;
      rewards += entry.rewards;
    }
    const auto line = std::format(
        "ledger: {} quests taken part in ({} completed, {} rewards paid), {} pending for our "
        "world, {} vehicles",
        ledger.Entries().size(), completed, rewards, ledger.Pending().size(),
        ledger.Vehicles().size());
    return Red::CString(line.c_str());
  }

  // Host: a story status effect came onto our V (applied) or went.
  static void SendEffect(Red::TweakDBID effect, bool applied) {
    Story::Get().Send(proto::StoryEffect{.effect = effect.value, .applied = applied});
  }

  // ---- story restrictions (docs/architecture/story.md, "Restrictions") ----

  // Host: the gameplay restrictions our story has on our V now (any order).
  // Sent when the set changed; true then.
  static bool SendRestrictions(Red::DynArray<Red::TweakDBID> effects) {
    std::vector<std::uint64_t> set;
    for (const auto &effect : effects)
      set.push_back(effect.value);
    std::ranges::sort(set);
    set.erase(std::unique(set.begin(), set.end()), set.end());
    if (set.size() > proto::kMaxStoryRestrictions)
      set.resize(proto::kMaxStoryRestrictions);
    auto &story = Story::Get();
    if (set == story.Sent())
      return false;
    story.Sent() = set;
    Story::Get().Send(proto::StoryRestrictions{.effects = std::move(set)});
    return true;
  }

  // ---- quest interactions (docs/architecture/story.md, "Quest interactions") ----

  // tandem.ini quest_interact (story on).
  static bool InteractOn() { return StoryOn() && Runtime::Get().config.questInteract; }
  // A CName as the number the session names it by.
  static std::uint64_t NameKey(Red::CName name) { return name.hash; }
  // Guest: our V chose `choice` on the host's object `entity` (placed in
  // the world: its id is the same in every game), standing at x, y, z. The question's number (its
  // answer names it, TakeEvents kind 61).
  static std::uint32_t SendInteract(Red::EntityID entity, Red::CName choice, std::uint64_t detail,
                                    Red::CName layer, std::int32_t flags, float x, float y,
                                    float z) {
    auto &story = Story::Get();
    const auto seq = story.NextInteractSeq();
    story.Send(
        proto::QuestInteract{.target = entity.hash,
                             .choice = choice.hash,
                             .detail = detail,
                             .layer = layer.hash,
                             .seq = seq,
                             .position = {x, y, z},
                             .timeUs = Runtime::Get().HostNowUs(),
                             .verdict = proto::kInteractAsk,
                             .flags = static_cast<std::uint8_t>(flags & proto::kInteractFlags)});
    return seq;
  }
  // Guest: what the host answered of this choice on this object this
  // session (proto::kInteract*), -1 never asked.
  static std::int32_t InteractKnown(Red::EntityID entity, Red::CName choice, std::uint64_t detail) {
    const auto known = Story::Get().InteractKnown().Get({entity.hash, choice.hash, detail});
    return known ? static_cast<std::int32_t>(*known) : -1;
  }
  // Host: our game's verdict on guest `player`'s question `seq`.
  static void AnswerInteract(std::uint32_t player, std::uint32_t seq, std::int32_t verdict) {
    auto &asks = Story::Get().InteractAsks();
    const auto it = asks.find({player, seq});
    if (it == asks.end() || verdict <= proto::kInteractAsk || verdict > proto::kInteractVerdictMax)
      return;
    auto answer = it->second.ask;
    asks.erase(it);
    answer.verdict = static_cast<std::uint8_t>(verdict);
    log::Info("interact: player {}'s {:#x} on {:#x}: {}", player, answer.choice, answer.target,
              interact::VerdictName(answer.verdict));
    Story::Get().Send(answer, player);
  }
  // Host: could guest `player`'s V reach our object at x, y, z when it
  // asked question `seq`? (Lag compensated: tandem/interact.hpp.)
  static bool InteractInReach(std::uint32_t player, std::uint32_t seq, float x, float y, float z) {
    const auto &asks = Story::Get().InteractAsks();
    const auto it = asks.find({player, seq});
    if (it == asks.end())
      return false;
    const auto &ask = it->second.ask;
    const auto reach = Runtime::Get().ReachOfObject(player, ask.position, {x, y, z}, ask.timeUs,
                                                    it->second.arrivalUs);
    log::Info("interact: player {} {:.1f} m from the object ({:.1f} m off its track): {}", player,
              reach.distance, reach.claimOff, reach.ok ? "in reach" : "too far");
    return reach.ok;
  }
  // The safety valve (tandem.ini): 1 the record is in quest_interact_hostonly,
  // 2 in quest_interact_allow, 0 neither (the default for everything).
  static std::int32_t InteractOverride(Red::TweakDBID record) {
    const auto &config = Runtime::Get().config;
    const auto in = [&](const std::vector<std::string> &names) {
      return std::ranges::any_of(
          names, [&](const std::string &name) { return Red::TweakDBID(name) == record; });
    };
    return in(config.questInteractHostOnly) ? 1 : in(config.questInteractAllow) ? 2 : 0;
  }
  // Would the action of class `action`, on an entity of class `entity` with
  // state `state`, cast its doer in a workspot (engine/casts.hpp)? 1 yes,
  // 0 no, -1 unknown.
  static std::int32_t ActionCasts(Red::CName entity, Red::CName state, Red::CName action) {
    std::string why;
    return static_cast<std::int32_t>(
        engine::ActionCastsDoer(entity.hash, state.hash, action.hash, why));
  }

  // Host: `npc` acts in the scene we announce (scene replay's cast).
  static bool SceneCasts(std::uint32_t npc) { return Story::Get().Stage().Casts(npc); }

  // Host: the backstop's word on a guest's act on an NPC (story::GuestAct: 0
  // a strike, 1 a takedown, 2 a quickhack), with `moment` whether our V is
  // in a restricted moment and what the NPC is (story::SceneActor): 0 let
  // through, 1 refused, 2 refused and to be said in the log (once a second
  // for each kind of act).
  static std::int32_t BackstopRefuses(bool moment, bool cast, bool questNpc, bool kept,
                                      bool inScene, std::int32_t act) {
    const auto verdict = Story::Get().Backstop().Judge(
        moment, {.cast = cast, .quest = questNpc, .kept = kept, .inScene = inScene},
        static_cast<story::GuestAct>(std::clamp(act, 0, 2)), Runtime::Get().HostNowUs());
    return verdict.refuse ? (verdict.log ? 2 : 1) : 0;
  }

  // Guest, a few times a second: whether our V is in the host's story moment
  // (core/story.hpp InMoment, with the staged scene when scene replay says
  // where it is), and what to put on our V now: the host's set while in it,
  // plus the scene tier's `holds`. What to take off: RestrictRemovals.
  static Red::DynArray<Red::TweakDBID> RestrictUpdate(bool grouped, bool away, bool hostKnown,
                                                      float hostX, float hostY, float hostZ,
                                                      float x, float y, float z, std::int32_t tier,
                                                      Red::DynArray<Red::TweakDBID> holds) {
    auto &kept = Story::Get();
    const auto &scene = kept.Replay().State();
    const bool staged = scene.scene != 0 && (scene.flags & proto::kSceneStaged);
    story::MomentInput in{.grouped = grouped,
                          .away = away,
                          .hostKnown = hostKnown,
                          .host = {hostX, hostY, hostZ},
                          .guest = {x, y, z},
                          .tier = Byte(tier),
                          .restricted = !kept.Restrictions().HostSet().empty(),
                          .staged = staged,
                          .anchor = scene.anchor,
                          .radius = staged ? scene.radius : -1.f};
    const bool was = kept.InMoment();
    kept.InMoment() = story::InMoment(in, was);
    if (kept.InMoment() != was)
      log::Info("story: {} the host's story moment (tier {}, {} restriction(s))",
                kept.InMoment() ? "in" : "out of", tier, kept.Restrictions().HostSet().size());
    std::vector<std::uint64_t> wanted;
    for (const auto &hold : holds)
      wanted.push_back(hold.value);
    auto change = kept.Restrictions().Update(kept.InMoment(), wanted);
    kept.Removals().insert(kept.Removals().end(), change.remove.begin(), change.remove.end());
    return Ids(change.apply);
  }
  // Guest: what to take off our V now (ours, no longer wanted), once.
  static Red::DynArray<Red::TweakDBID> RestrictRemovals() {
    return Ids(std::exchange(Story::Get().Removals(), {}));
  }
  // Guest: the session is over or the host gone: all of ours to take off
  // (RestrictRemovals), the host's set forgotten.
  static void RestrictLift() {
    auto &story = Story::Get();
    const auto change = story.Restrictions().Lift();
    story.Removals().insert(story.Removals().end(), change.remove.begin(), change.remove.end());
    story.InMoment() = false;
  }
  // Guest: our V had it already, or its own game put it on again: theirs.
  static void RestrictTheirs(Red::TweakDBID effect) {
    Story::Get().Restrictions().Theirs(effect.value);
  }
  // Guest: our V lost it, not through us.
  static void RestrictGone(Red::TweakDBID effect) {
    Story::Get().Restrictions().Gone(effect.value);
  }
  static bool RestrictHolds(Red::TweakDBID effect) {
    return Story::Get().Restrictions().Holds(effect.value);
  }
  static bool RestrictOurs(Red::TweakDBID effect) {
    return Story::Get().Restrictions().Ours(effect.value);
  }
  static bool RestrictInMoment() { return Story::Get().InMoment(); }
  static Red::DynArray<Red::TweakDBID> RestrictHostSet() {
    return Ids(Story::Get().Restrictions().HostSet());
  }

  // The scene our game plays now (its node ran in the last 3 s): 0 none or
  // not known yet, 1 important, 2 private (minor), 3 private (romance).
  static std::int32_t SceneClass() {
    std::string why;
    return static_cast<std::int32_t>(engine::ClassifyRecentScene(3000, why));
  }

  // Our V is in a private scene: held where it began for the others, its
  // pose not sent.
  static void SetPrivate(bool on) {
    Story::Get().SetPrivate(on);
    log::Info("story: our private scene {}", on ? "begins: held for the others" : "is over");
  }

  // Another player is in a private scene: its puppet idles on its own.
  static void PausePose(std::uint32_t player, bool paused) {
    Story::Get().PausePose(player, paused);
  }

  // ---- scene replay (docs/architecture/scene-replay.md) ----

  // 0: off (tandem.ini scene_replay), 1: on, 2: with the actors' bodies
  // (the pose hooks are in).
  static std::int32_t SceneReplay() {
    const auto &config = Runtime::Get().config;
    if (!config.sceneReplay)
      return 0;
    return config.scenePoses && config.pose ? 2 : 1;
  }

  // Host: one actor of the scene our game plays, for the next SceneSample
  // (flags proto::kActor*).
  static void SceneActor(std::uint32_t npc, float x, float y, float z, std::int32_t flags) {
    auto &actors = Story::Get().Actors();
    if (actors.size() < proto::kMaxSceneActors)
      actors.push_back({npc, {x, y, z}, Byte(flags)});
  }

  // Host: something an actor of our scene holds, for the next SceneSample.
  // `key`: our own name for it (the same while it changes hands); holder:
  // the actor's NpcId, or a player with npc 0.
  static void SceneProp(std::uint64_t key, Red::TweakDBID record, std::uint32_t holderNpc,
                        std::uint32_t holderPlayer, bool leftHand) {
    auto &props = Story::Get().Props();
    if (props.size() < proto::kMaxSceneProps)
      props.push_back({key, record.value, holderNpc, holderPlayer, leftHand});
  }
  // Guest: the host's scene has a prop in this actor's (or, npc 0, this
  // player's) hands.
  static bool SceneHolds(std::uint32_t npc, std::uint32_t player) {
    return Story::Get().Replay().Holds(npc, player);
  }

  // Host, a few times a second: what our game plays now. `playing`: our V
  // is in a scene (its tier, an actor in a scene); the scene is the one
  // whose quest node ran last (engine/scenes.cpp). Says what changed to the
  // guests and answers the play's number (0: no scene announced).
  static std::int32_t SceneSample(bool playing, bool isPrivate, std::int32_t tier, float rate,
                                  bool held, float x, float y, float z, float yaw) {
    auto &story = Story::Get();
    scene::HostSample sample{.isPrivate = isPrivate,
                             .tier = Byte(tier),
                             .rate = rate,
                             .held = held,
                             .host = {x, y, z},
                             .hostYaw = yaw,
                             .actors = std::move(story.Actors()),
                             .props = std::move(story.Props())};
    story.Actors().clear();
    story.Props().clear();
    if (playing && Runtime::Get().config.sceneReplay) {
      // No node seen (the hook is out, or the scene is not a quest's): a
      // scene all the same, with no name.
      sample.scene = engine::ScenePlaying(3000);
      if (sample.scene == 0)
        sample.scene = 1;
    }
    const auto before = story.Stage().On() ? story.Stage().Play() : 0u;
    for (auto &message : story.Stage().Update(sample, Runtime::Get().HostNowUs())) {
      if (const auto *state = std::get_if<proto::SceneState>(&message)) {
        if (state->scene == 0)
          log::Info("scene: play {} is over{}", state->play,
                    state->flags & proto::kSceneInterrupted ? " (interrupted)" : "");
        else if (state->play != before)
          log::Info("scene: {:#x} plays (play {}, tier {})", state->scene, state->play,
                    state->tier);
      } else if (const auto *cast = std::get_if<proto::SceneCast>(&message))
        log::Info("scene: play {} has {} actor(s)", cast->play, cast->actors.size());
      else if (const auto *prop = std::get_if<proto::SceneProp>(&message))
        log::Info("scene: prop {} ({:#x}) {} (npc {}, player {}{})", prop->prop, prop->record,
                  prop->flags & proto::kPropGone ? "gone" : "held", prop->holder,
                  prop->holderPlayer, prop->flags & proto::kPropLeftHand ? ", left hand" : "");
      story.Send(std::move(message));
    }
    return story.Stage().On() ? static_cast<std::int32_t>(story.Stage().Play()) : 0;
  }

  // Host: a voiced line begins on our game (flags proto::kVoice*), or its
  // subtitle went. True when the guests were told.
  static bool SceneVoice(std::uint64_t line, std::uint32_t speakerNpc, std::uint32_t speakerPlayer,
                         float duration, std::int32_t flags) {
    if (!Runtime::Get().config.sceneReplay)
      return false;
    auto spoken = Story::Get().Stage().Line(line, speakerNpc, speakerPlayer, duration, Byte(flags),
                                            Runtime::Get().HostNowUs());
    if (spoken)
      Story::Get().Send(*spoken);
    return spoken.has_value();
  }
  static bool SceneVoiceHide(std::uint64_t line) {
    auto cut = Story::Get().Stage().Hide(line, Runtime::Get().HostNowUs());
    if (cut)
      Story::Get().Send(*cut);
    return cut.has_value();
  }

  // Guest: the scene the host plays, as heard (0: none); where it is staged
  // (W: how far from the middle its actors stand; W < 0: not staged) and the
  // way the host's V faces it; how long it has played.
  static std::uint64_t ScenePlaying() { return Story::Get().Replay().State().scene; }
  static Red::Vector4 SceneAnchor() {
    const auto &state = Story::Get().Replay().State();
    if (state.scene == 0 || !(state.flags & proto::kSceneStaged))
      return {0.f, 0.f, 0.f, -1.f};
    return {state.anchor.x, state.anchor.y, state.anchor.z, state.radius};
  }
  static float SceneYaw() { return Story::Get().Replay().State().yaw; }
  static float SceneSeconds() { return Story::Get().Replay().Seconds(Runtime::Get().HostNowUs()); }
  static bool SceneActs(std::uint32_t npc) { return Story::Get().Replay().Acts(npc); }

  // Guest: the bodies around the scene (actors, the host's V, the other
  // watchers), listed again before each question below.
  static void SceneBodies() { Story::Get().Bodies().clear(); }
  static void SceneBody(float x, float y, float z) {
    auto &bodies = Story::Get().Bodies();
    if (bodies.size() < 64)
      bodies.push_back({x, y, z});
  }
  // A place to watch the scene from (core/scene.hpp): X Y Z and, in W, the
  // yaw that faces the scene; W > 100: none.
  static Red::Vector4 WatchSpot(float hostX, float hostY, float hostZ, float x, float y, float z) {
    const auto spot = scene::WatchSpot(Story::Get().Replay().State(), Story::Get().Bodies(),
                                       {hostX, hostY, hostZ}, {x, y, z});
    if (!spot.found)
      return {0.f, 0.f, 0.f, 1000.f};
    return {spot.position.x, spot.position.y, spot.position.z, spot.yaw};
  }
  // The step that takes our V out of an actor it stands in (W: its length;
  // 0: clear).
  static Red::Vector4 KeepOut(float x, float y, float z, float radius, float maxStep) {
    const auto step = scene::KeepOut({x, y, z}, Story::Get().Bodies(), radius, maxStep);
    return {step.x, step.y, 0.f, std::hypot(step.x, step.y)};
  }

  // The actors' bodies (engine/posestream.cpp). Host: the NPC read as actor
  // `npc` (null: no longer). Guest: the proxy drawn in its pose (null: its
  // own animation again). Nothing without SceneReplay() == 2.
  static void ActorSource(std::uint32_t npc, Red::Handle<Red::ent::Entity> entity) {
    if (SceneReplay() == 2 || !entity)
      engine::SetPoseActorSource(npc, entity);
  }
  static void ActorBind(std::uint32_t npc, Red::Handle<Red::ent::Entity> entity) {
    if (SceneReplay() == 2 || !entity)
      engine::BindPoseActor(npc, entity);
  }
  static void ActorsClear() {
    engine::ClearPoseActors();
    Runtime::Get().ForgetActorPoses();
  }
  static Red::CString SceneReport() {
    const auto &replay = Story::Get().Replay();
    const auto &stage = Story::Get().Stage();
    return Red::CString(
        std::format("scene: host play {} ({}), heard {:#x} play {} with {} actor(s), {} line(s) "
                    "spoken, {} too late, {} prop(s); {}",
                    stage.Play(), stage.On() ? "on" : "off", replay.State().scene,
                    replay.State().play, replay.Cast().size(), replay.Speaking(), replay.Stale(),
                    replay.Props(), engine::PoseActorsReport())
            .c_str());
  }

  // A dialogue line's id as a number and back (the script cannot build one).
  static Red::CRUID LineId(std::uint64_t line) {
    Red::CRUID id{};
    id.unk00 = static_cast<std::int64_t>(line);
    return id;
  }
  static std::uint64_t LineValue(Red::CRUID id) { return static_cast<std::uint64_t>(id.unk00); }
  // The id a guest shows the host's line under (never 0, never the line's
  // own: the game may show that one itself, Dialogue.reds).
  static std::uint64_t StandInLine(std::uint64_t line) {
    const auto standIn = line ^ 0x7A6E64656D4C696Eull;
    return standIn == 0 ? 1 : standIn;
  }

  RTTI_IMPL_TYPEINFO(TandemStory);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemStoryEvent, "TandemStoryEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(hash);
  RTTI_PROPERTY(state);
  RTTI_PROPERTY(flags);
  RTTI_PROPERTY(index);
  RTTI_PROPERTY(extra);
  RTTI_PROPERTY(contact);
  RTTI_PROPERTY(line);
  RTTI_PROPERTY(duration);
  RTTI_PROPERTY(npc);
  RTTI_PROPERTY(reward);
  RTTI_PROPERTY(name);
  RTTI_PROPERTY(text);
  RTTI_PROPERTY(entity);
  RTTI_PROPERTY(choice);
  RTTI_PROPERTY(detail);
  RTTI_PROPERTY(layer);
  RTTI_PROPERTY(x);
  RTTI_PROPERTY(y);
  RTTI_PROPERTY(z);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStory, "TandemStory", {
  RTTI_METHOD(StoryOn);
  RTTI_METHOD(FreezeAvailable);
  RTTI_METHOD(Freeze);
  RTTI_METHOD(Frozen);
  RTTI_METHOD(FreezeHeld);
  RTTI_METHOD(FreezeLost);
  RTTI_METHOD(FreezeReport);
  RTTI_METHOD(Grouped);
  RTTI_METHOD(IsGrouped);
  RTTI_METHOD(QuestTrace);
  RTTI_METHOD(TakeEvents);
  RTTI_METHOD(You);
  RTTI_METHOD(HostId);
  RTTI_METHOD(SendJournal);
  RTTI_METHOD(SendTracked);
  RTTI_METHOD(SendCall);
  RTTI_METHOD(SendPhoneAction);
  RTTI_METHOD(SendReply);
  RTTI_METHOD(SendLine);
  RTTI_METHOD(SendHub);
  RTTI_METHOD(SendCursor);
  RTTI_METHOD(SendState);
  RTTI_METHOD(QuestMoved);
  RTTI_METHOD(Reward);
  RTTI_METHOD(VehicleUnlocked);
  RTTI_METHOD(VehicleRewardsKept);
  RTTI_METHOD(CarryLoad);
  RTTI_METHOD(CarryWords);
  RTTI_METHOD(CarryVehicle);
  RTTI_METHOD(CarryVehicleKept);
  RTTI_METHOD(CarryVehicles);
  RTTI_METHOD(CarryVehicleIsKept);
  RTTI_METHOD(CarryQuest);
  RTTI_METHOD(CarryCompleted);
  RTTI_METHOD(CarryPay);
  RTTI_METHOD(CarryOwnPays);
  RTTI_METHOD(CarryWith);
  RTTI_METHOD(CarryReport);
  RTTI_METHOD(SendEffect);
  RTTI_METHOD(SendRestrictions);
  RTTI_METHOD(InteractOn);
  RTTI_METHOD(NameKey);
  RTTI_METHOD(SendInteract);
  RTTI_METHOD(InteractKnown);
  RTTI_METHOD(AnswerInteract);
  RTTI_METHOD(InteractInReach);
  RTTI_METHOD(InteractOverride);
  RTTI_METHOD(ActionCasts);
  RTTI_METHOD(SceneCasts);
  RTTI_METHOD(BackstopRefuses);
  RTTI_METHOD(RestrictUpdate);
  RTTI_METHOD(RestrictRemovals);
  RTTI_METHOD(RestrictLift);
  RTTI_METHOD(RestrictTheirs);
  RTTI_METHOD(RestrictGone);
  RTTI_METHOD(RestrictHolds);
  RTTI_METHOD(RestrictOurs);
  RTTI_METHOD(RestrictInMoment);
  RTTI_METHOD(RestrictHostSet);
  RTTI_METHOD(SceneClass);
  RTTI_METHOD(SetPrivate);
  RTTI_METHOD(PausePose);
  RTTI_METHOD(SceneReplay);
  RTTI_METHOD(SceneActor);
  RTTI_METHOD(SceneProp);
  RTTI_METHOD(SceneHolds);
  RTTI_METHOD(SceneSample);
  RTTI_METHOD(SceneVoice);
  RTTI_METHOD(SceneVoiceHide);
  RTTI_METHOD(ScenePlaying);
  RTTI_METHOD(SceneAnchor);
  RTTI_METHOD(SceneYaw);
  RTTI_METHOD(SceneSeconds);
  RTTI_METHOD(SceneActs);
  RTTI_METHOD(SceneBodies);
  RTTI_METHOD(SceneBody);
  RTTI_METHOD(WatchSpot);
  RTTI_METHOD(KeepOut);
  RTTI_METHOD(ActorSource);
  RTTI_METHOD(ActorBind);
  RTTI_METHOD(ActorsClear);
  RTTI_METHOD(SceneReport);
  RTTI_METHOD(LineId);
  RTTI_METHOD(LineValue);
  RTTI_METHOD(StandInLine);
});
