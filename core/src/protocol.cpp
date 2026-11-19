#include "tandem/protocol.hpp"

#include <zpp_bits.h>

namespace tandem::proto {
std::vector<std::byte> Encode(const Message &message) {
  std::vector<std::byte> bytes;
  auto out = zpp::bits::out(bytes);
  if (zpp::bits::failure(out(message)))
    return {};
  return bytes;
}

namespace {
// A trade offer within the wire's limits (what it may hold: trade::Valid).
bool Fits(const TradeOffer &offer) {
  if (offer.items.size() > kMaxTradeItems)
    return false;
  for (const auto &item : offer.items)
    if (item.stats.size() > kMaxTradeStats || item.parts.size() > kMaxTradeParts)
      return false;
  return true;
}
} // namespace

std::optional<Message> Decode(std::span<const std::byte> bytes) {
  Message message;
  auto in = zpp::bits::in(bytes);
  if (zpp::bits::failure(in(message)))
    return std::nullopt;
  // A message must be read whole: trailing bytes mean the two sides disagree.
  if (in.position() != bytes.size())
    return std::nullopt;
  if (const auto *appearance = std::get_if<Appearance>(&message);
      appearance && (appearance->look.size() > kMaxAppearanceKeys ||
                     appearance->items.size() > kMaxAppearanceItems))
    return std::nullopt;
  if (const auto *npcs = std::get_if<NpcStates>(&message);
      npcs && npcs->npcs.size() > kMaxNpcsPerState)
    return std::nullopt;
  if (const auto *effect = std::get_if<NpcEffect>(&message);
      effect && effect->effects.size() > kMaxNpcEffects)
    return std::nullopt;
  if (const auto *state = std::get_if<NpcHackState>(&message);
      state && state->step > HackStep::Failed)
    return std::nullopt;
  if (const auto *car = std::get_if<VehicleHack>(&message); car && car->hack > CarHack::Accelerate)
    return std::nullopt;
  if (const auto *away = std::get_if<Elsewhere>(&message); away && away->place > Place::Other)
    return std::nullopt;
  if (const auto *awareness = std::get_if<NpcAwareness>(&message);
      awareness && awareness->entries.size() > kMaxNpcAwareness)
    return std::nullopt;
  if (const auto *journal = std::get_if<Journal>(&message);
      journal && journal->entries.size() > kMaxJournalEntries)
    return std::nullopt;
  if (const auto *hub = std::get_if<ChoiceHub>(&message);
      hub && hub->options.size() > kMaxChoiceOptions)
    return std::nullopt;
  if (const auto *set = std::get_if<NpcSet>(&message); set && set->npcs.size() > kMaxNpcSet)
    return std::nullopt;
  if (const auto *resync = std::get_if<ResyncRequest>(&message);
      resync && (resync->domains & ~kAllDomains) != 0)
    return std::nullopt;
  // Kinds the receiver does not know mean the two sides disagree.
  const auto known = [](NpcActionKind kind) {
    return kind >= NpcActionKind::Grab && kind <= NpcActionKind::Dispose;
  };
  if (const auto *action = std::get_if<NpcAction>(&message); action && !known(action->kind))
    return std::nullopt;
  if (const auto *reply = std::get_if<NpcActionReply>(&message); reply && !known(reply->kind))
    return std::nullopt;
  if (const auto *hold = std::get_if<NpcHold>(&message); hold && hold->how > NpcHoldKind::Finisher)
    return std::nullopt;
  if (const auto *cue = std::get_if<NpcCue>(&message);
      cue && (cue->cue < NpcCueKind::CombatStart || cue->cue > NpcCueKind::PoiseBroken))
    return std::nullopt;
  if (const auto *regroup = std::get_if<Regroup>(&message);
      regroup && regroup->label.size() > kMaxRegroupLabel)
    return std::nullopt;
  if (const auto *item = std::get_if<WorldItem>(&message);
      item && item->stacks.size() > kMaxWorldItemStacks)
    return std::nullopt;
  if (const auto *vitals = std::get_if<Vitals>(&message); vitals && vitals->life > Life::Out)
    return std::nullopt;
  if (const auto *revive = std::get_if<Revive>(&message); revive && revive->step > ReviveStep::Done)
    return std::nullopt;
  if (const auto *anims = std::get_if<NpcAnims>(&message);
      anims && anims->npcs.size() > kMaxNpcAnims)
    return std::nullopt;
  if (const auto *wound = std::get_if<NpcWound>(&message);
      wound && (wound->part > 5 || wound->wound > 2))
    return std::nullopt;
  if (const auto *difficulty = std::get_if<Difficulty>(&message);
      difficulty && difficulty->level > 3)
    return std::nullopt;
  if (const auto *transit = std::get_if<Transit>(&message);
      transit && transit->kind > TransitKind::Along)
    return std::nullopt;
  if (const auto *level = std::get_if<PlayerLevel>(&message);
      level && (level->level == 0 || level->level > kMaxLevel))
    return std::nullopt;
  if (const auto *level = std::get_if<SessionLevel>(&message);
      level && (level->level == 0 || level->level > kMaxLevel || level->players == 0))
    return std::nullopt;
  if (const auto *level = std::get_if<NpcLevel>(&message);
      level && !(level->level >= 0.f && level->level <= 2.f * kMaxLevel))
    return std::nullopt;
  if (const auto *trade = std::get_if<Trade>(&message);
      trade && (trade->verb > TradeVerb::Applied || trade->reason > TradeReason::Off))
    return std::nullopt;
  if (const auto *offer = std::get_if<TradeOffer>(&message); offer && !Fits(*offer))
    return std::nullopt;
  if (const auto *commit = std::get_if<TradeCommit>(&message);
      commit && (!Fits(commit->first) || !Fits(commit->second)))
    return std::nullopt;
  return message;
}

Channel ChannelOf(const Message &message) {
  if (const auto *journal = std::get_if<Journal>(&message); journal && journal->snapshot)
    return Channel::Bulk;
  return std::holds_alternative<PlayerState>(message) ||
                 std::holds_alternative<PoseState>(message) ||
                 std::holds_alternative<Shot>(message) ||
                 std::holds_alternative<VehicleState>(message) ||
                 std::holds_alternative<NpcStates>(message) ||
                 std::holds_alternative<NpcAwareness>(message) ||
                 std::holds_alternative<Noise>(message) || std::holds_alternative<NpcShot>(message)
             ? Channel::State
             // Everything else is reliable and ordered, the consistency
             // messages too (Digest, ResyncRequest, NpcSet, Epoch): a digest
             // is compared where it falls in the reliable stream.
             : Channel::Event;
}

bool IsStory(const Message &message) {
  return (message.index() >= Message(Journal{}).index() &&
          message.index() <= Message(QuestReward{}).index()) ||
         std::holds_alternative<StoryEffect>(message);
}

bool IsCombat(const Message &message) {
  return (message.index() >= Message(NpcStrike{}).index() &&
          message.index() <= Message(AreaHit{}).index()) ||
         (message.index() >= Message(PlayerLevel{}).index() &&
          message.index() <= Message(NpcLevel{}).index());
}

bool IsTrade(const Message &message) {
  return std::holds_alternative<Trade>(message) || std::holds_alternative<TradeOffer>(message) ||
         std::holds_alternative<TradeCommit>(message);
}

bool IsReliable(const Message &message) { return ChannelOf(message) != Channel::State; }

const char *NameOf(const Message &message) {
  constexpr const char *names[] = {
      "Hello",          "Welcome",        "Refused",      "Ping",         "Pong",
      "Roster",         "PlayerState",    "Bye",          "PoseState",    "Appearance",
      "Held",           "Shot",           "Seat",         "VehicleState", "World",
      "NpcSpawn",       "NpcStates",      "NpcGone",      "NpcHit",       "NpcHealth",
      "NpcEffect",      "Pause",          "VehicleRadio", "PlayerHit",    "NpcAwareness",
      "Stealth",        "Noise",          "NpcHack",      "NpcHackState", "NpcStatus",
      "PlayerHack",     "VehicleHack",    "Journal",      "Tracked",      "PhoneCall",
      "PhoneAction",    "PhoneReply",     "Subtitle",     "ChoiceHub",    "ChoiceCursor",
      "StoryState",     "QuestReward",    "TimeEffect",   "StoryEffect",  "NpcAction",
      "NpcActionReply", "NpcHold",        "NpcCue",       "Regroup",      "RegroupAnswer",
      "WorldItem",      "WorldItemTaken", "SharedItem",   "Vitals",       "Revive",
      "GameOver",       "NpcAnims",       "NpcShot",      "NpcStrike",    "NpcWound",
      "NpcPoise",       "Difficulty",     "AreaHit",      "Digest",       "ResyncRequest",
      "NpcSet",         "Epoch",          "Elsewhere",    "Transit",      "NpcSeat",
      "NpcShotAt",      "ShotHit",        "PlayerLevel",  "SessionLevel", "NpcLevel",
      "Trade",          "TradeOffer",     "TradeCommit"};
  static_assert(std::size(names) == std::variant_size_v<Message>);
  return names[message.index()];
}
} // namespace tandem::proto
