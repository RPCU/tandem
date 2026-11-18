// What scripts/tandem/Story.reds can call: `TandemStory` static functions and
// the `TandemStoryEvent` struct. Declared on the script side in
// scripts/tandem/StoryNatives.reds: keep the two in sync (tools/check-natives.py
// reads both pairs). All of these run on the game thread.

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "config.hpp"
#include "engine/quests.hpp"
#include "engine/scenes.hpp"
#include "log.hpp"
#include "runtime.hpp"
#include "story.hpp"

#include <RedLib.hpp>

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
};

void Flatten(const StoryInbound &in, Red::DynArray<TandemStoryEvent> &out) {
  using K = StoryInbound::Kind;
  switch (in.kind) {
  case K::InSession:
    out.EmplaceBack(TandemStoryEvent{.kind = kInSession, .player = in.player, .hash = in.host});
    return;
  case K::Ended:
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
    out.EmplaceBack(TandemStoryEvent{
        .kind = kReward, .hash = reward->quest, .reward = Red::TweakDBID(reward->reward)});
  }
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

  // Host: a story status effect came onto our V (applied) or went.
  static void SendEffect(Red::TweakDBID effect, bool applied) {
    Story::Get().Send(proto::StoryEffect{.effect = effect.value, .applied = applied});
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

  // A dialogue line's id as a number and back (the script cannot build one).
  static Red::CRUID LineId(std::uint64_t line) {
    Red::CRUID id{};
    id.unk00 = static_cast<std::int64_t>(line);
    return id;
  }
  static std::uint64_t LineValue(Red::CRUID id) { return static_cast<std::uint64_t>(id.unk00); }

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
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemStory, "TandemStory", {
  RTTI_METHOD(StoryOn);
  RTTI_METHOD(FreezeAvailable);
  RTTI_METHOD(Freeze);
  RTTI_METHOD(Frozen);
  RTTI_METHOD(FreezeHeld);
  RTTI_METHOD(FreezeLost);
  RTTI_METHOD(FreezeReport);
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
  RTTI_METHOD(SendEffect);
  RTTI_METHOD(SceneClass);
  RTTI_METHOD(SetPrivate);
  RTTI_METHOD(PausePose);
  RTTI_METHOD(LineId);
  RTTI_METHOD(LineValue);
});
