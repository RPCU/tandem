#include "tandem/protocol.hpp"

#include <zpp_bits.h>

#include <algorithm>
#include <functional>

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
      cue && (cue->cue < NpcCueKind::CombatStart || cue->cue > NpcCueKind::OutOfSight))
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
  if (const auto *frame = std::get_if<PoseFrame>(&message)) {
    // Codes 0-2 only, none past the last bone, the rotations exactly what
    // the codes say; a key whole.
    std::size_t bytes = 0;
    const bool key = frame->key == frame->seq;
    for (std::size_t slot = 0; slot < kPoseCodeBytes * 4; ++slot) {
      const auto code = frame->Code(slot);
      if (code == 3 || (slot >= kPoseBones && code != 0) ||
          (key && slot < kPoseBones && code != kBoneWhole))
        return std::nullopt;
      bytes += code == kBoneTurn    ? kPoseTurnBytes
               : code == kBoneWhole ? kPoseFrameRotationBytes
                                    : 0;
    }
    if (frame->rotations.size() != bytes)
      return std::nullopt;
  }
  if (const auto *motion = std::get_if<NpcMotion>(&message);
      motion && motion->npcs.size() > kMaxNpcsPerState)
    return std::nullopt;
  if (const auto *transit = std::get_if<Transit>(&message);
      transit && transit->kind > TransitKind::Along)
    return std::nullopt;
  if (const auto *cast = std::get_if<SceneCast>(&message);
      cast && cast->actors.size() > kMaxSceneActors)
    return std::nullopt;
  // A rate no clock can follow, or one that is not a number.
  if (const auto *scene = std::get_if<SceneState>(&message);
      scene && !(scene->rate >= 0.f && scene->rate <= kMaxSceneRate))
    return std::nullopt;
  // A device's state uses the known bits and a known status only.
  const auto device = [](std::int8_t status, std::uint16_t flags) {
    return status >= kDeviceDisabled && status <= kDeviceOn && (flags & ~kDeviceStateBits) == 0;
  };
  if (const auto *state = std::get_if<DeviceState>(&message);
      state && (!device(state->status, state->flags) || state->how > kDeviceAsk))
    return std::nullopt;
  if (const auto *use = std::get_if<DeviceUse>(&message); use && !device(use->status, use->flags))
    return std::nullopt;
  if (const auto *regions = std::get_if<Regions>(&message);
      regions && (regions->pairs.size() > 2 * kMaxRegionPairs || regions->pairs.size() % 2 != 0))
    return std::nullopt;
  if (const auto *given = std::get_if<NpcHandoverState>(&message);
      given && given->effects.size() > kMaxNpcEffects)
    return std::nullopt;
  if (const auto *cars = std::get_if<TrafficStates>(&message);
      cars && cars->cars.size() > kMaxTrafficPerState)
    return std::nullopt;
  if (const auto *set = std::get_if<TrafficSet>(&message); set && set->ids.size() > kMaxTrafficSet)
    return std::nullopt;
  if (const auto *gone = std::get_if<TrafficGone>(&message); gone && gone->reason > kTrafficRemoved)
    return std::nullopt;
  if (const auto *heat = std::get_if<Heat>(&message);
      heat && (heat->stage > kMaxHeat || heat->state > HeatState::Blinking))
    return std::nullopt;
  if (const auto *crime = std::get_if<Crime>(&message);
      crime && (crime->kind < CrimeKind::Witness || crime->kind > CrimeKind::Kill))
    return std::nullopt;
  if (const auto *cue = std::get_if<Cue>(&message);
      cue && (cue->kind < CueKind::Trace || cue->kind > CueKind::MeleeHit))
    return std::nullopt;
  if (const auto *reaction = std::get_if<NpcReaction>(&message);
      reaction && reaction->type > kReactionMax)
    return std::nullopt;
  if (const auto *install = std::get_if<Install>(&message);
      install &&
      (install->build.size() > kMaxInstallText || install->game.size() > kMaxInstallText))
    return std::nullopt;
  if (const auto *thrown = std::get_if<Throw>(&message); thrown && thrown->kind > ThrowKind::Smart)
    return std::nullopt;
  if (const auto *end = std::get_if<ThrowEnd>(&message); end && end->how > ThrowEndKind::Gone)
    return std::nullopt;
  if (const auto *rules = std::get_if<Rules>(&message);
      rules && ((rules->flags & ~kRuleFlags) != 0 || rules->enemyHealth > kMaxEnemyHealth))
    return std::nullopt;
  if (const auto *scale = std::get_if<NpcScale>(&message);
      scale && (scale->players == 0 || scale->players > kMaxPlayerId))
    return std::nullopt;
  if (const auto *defended = std::get_if<Defended>(&message);
      defended && (defended->how < DefendKind::Blocked || defended->how > DefendKind::Parried))
    return std::nullopt;
  if (const auto *activity = std::get_if<Activity>(&message);
      activity && activity->kind > ActivityKind::Contest)
    return std::nullopt;
  if (const auto *skip = std::get_if<TimeSkip>(&message); skip && skip->reason > SkipReason::Quest)
    return std::nullopt;
  if (const auto *vote = std::get_if<TimeSkipVote>(&message); vote && vote->why > kSkipWhyMax)
    return std::nullopt;
  if (const auto *result = std::get_if<TimeSkipResult>(&message);
      result && (result->reason > SkipReason::Quest || result->outcome > SkipOutcome::Refused ||
                 result->why > kSkipWhyMax))
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
  if (const auto *lend = std::get_if<CarLend>(&message); lend && lend->lend > Lend::Never)
    return std::nullopt;
  // A live vendor names its NPC; a dead one only its entity.
  if (const auto *vendor = std::get_if<NpcVendor>(&message);
      vendor && (vendor->entity == 0 || (vendor->flags & ~kVendorDead) != 0 ||
                 ((vendor->flags & kVendorDead) != 0) != (vendor->npc == 0)))
    return std::nullopt;
  if (const auto *commit = std::get_if<TradeCommit>(&message);
      commit && (!Fits(commit->first) || !Fits(commit->second)))
    return std::nullopt;
  // Comms: kinds the receiver does not know, lists past their caps.
  const auto target = [](const TargetRef &ref) { return ref.kind <= TargetKind::Player; };
  if (const auto *mark = std::get_if<Mark>(&message);
      mark && (mark->kind > MarkKind::Target || !target(mark->target)))
    return std::nullopt;
  if (const auto *tag = std::get_if<Tag>(&message);
      tag && (tag->reason > TagReason::Reveal || tag->targets.size() > kMaxTagTargets ||
              !std::ranges::all_of(tag->targets, target)))
    return std::nullopt;
  if (const auto *link = std::get_if<Link>(&message);
      link && (link->mode > LinkMode::Everyone || link->players.size() > kMaxLinkPlayers))
    return std::nullopt;
  if (const auto *waypoint = std::get_if<Waypoint>(&message);
      waypoint && waypoint->kind > WaypointKind::Point)
    return std::nullopt;
  if (const auto *damage = std::get_if<VehicleDamage>(&message);
      damage && damage->parts.size() > kMaxDetachedParts)
    return std::nullopt;
  if (const auto *exit = std::get_if<VehicleExit>(&message);
      exit && exit->kind > ExitKind::Exploded)
    return std::nullopt;
  if (const auto *combat = std::get_if<VehicleCombat>(&message);
      combat && combat->mode > SeatCombat::Gunner)
    return std::nullopt;
  // A breach: its daemons within the bound, flags and verdicts this version knows.
  if (const auto *breach = std::get_if<Breach>(&message);
      breach && (breach->programs.size() > kMaxBreachPrograms ||
                 breach->traps.size() > kMaxBreachPrograms || (breach->flags & ~kBreachFlags) != 0))
    return std::nullopt;
  if (const auto *result = std::get_if<BreachResult>(&message);
      result && result->verdict > BreachVerdict::Full)
    return std::nullopt;
  // At most the bound, sorted and without repeats: one set, said one way.
  if (const auto *restrictions = std::get_if<StoryRestrictions>(&message);
      restrictions && (restrictions->effects.size() > kMaxStoryRestrictions ||
                       std::ranges::adjacent_find(restrictions->effects, std::greater_equal<>{}) !=
                           restrictions->effects.end()))
    return std::nullopt;
  return message;
}

std::optional<std::uint16_t> PeekHelloVersion(std::span<const std::byte> bytes) {
  // The variant's index (Hello is the first alternative), then Hello's
  // magic and version, little-endian.
  const auto at = [&](std::size_t i) { return std::to_integer<std::uint32_t>(bytes[i]); };
  if (bytes.size() < 7 || at(0) != Message(Hello{}).index())
    return std::nullopt;
  if ((at(1) | at(2) << 8 | at(3) << 16 | at(4) << 24) != kMagic)
    return std::nullopt;
  return static_cast<std::uint16_t>(at(5) | at(6) << 8);
}

Channel ChannelOf(const Message &message) {
  if (const auto *journal = std::get_if<Journal>(&message); journal && journal->snapshot)
    return Channel::Bulk;
  // A twitch is one of many; the reactions that change how an NPC stands
  // (a knockdown, a death, a ragdoll) must arrive.
  if (const auto *reaction = std::get_if<NpcReaction>(&message))
    return reaction->type == kReactionTwitch ? Channel::State : Channel::Event;
  // A smart weapon's guided round, one per shot: like the shot itself.
  if (const auto *thrown = std::get_if<Throw>(&message); thrown && thrown->kind == ThrowKind::Smart)
    return Channel::State;
  return std::holds_alternative<PlayerState>(message) || std::holds_alternative<Cue>(message) ||
                 std::holds_alternative<PoseState>(message) ||
                 std::holds_alternative<Shot>(message) ||
                 std::holds_alternative<VehicleState>(message) ||
                 std::holds_alternative<NpcStates>(message) ||
                 std::holds_alternative<NpcAwareness>(message) ||
                 std::holds_alternative<Noise>(message) ||
                 std::holds_alternative<NpcShot>(message) ||
                 std::holds_alternative<TrafficStates>(message) ||
                 std::holds_alternative<MountedFire>(message) ||
                 std::holds_alternative<GunnerAim>(message) ||
                 std::holds_alternative<ActorPose>(message) ||
                 std::holds_alternative<PoseFrame>(message) ||
                 std::holds_alternative<NpcMotion>(message) ||
                 std::holds_alternative<PlayerMotion>(message) ||
                 std::holds_alternative<VehicleMotion>(message)
             ? Channel::State
             // Everything else is reliable and ordered, the consistency
             // messages too (Digest, ResyncRequest, NpcSet, Epoch): a digest
             // is compared where it falls in the reliable stream.
             : Channel::Event;
}

bool IsStory(const Message &message) {
  return (message.index() >= Message(Journal{}).index() &&
          message.index() <= Message(QuestReward{}).index()) ||
         std::holds_alternative<StoryEffect>(message) ||
         std::holds_alternative<StoryRestrictions>(message) ||
         (message.index() >= Message(SceneState{}).index() &&
          message.index() <= Message(SceneLine{}).index()) ||
         std::holds_alternative<VehicleUnlock>(message);
}

bool IsCombat(const Message &message) {
  return (message.index() >= Message(NpcStrike{}).index() &&
          message.index() <= Message(AreaHit{}).index()) ||
         (message.index() >= Message(Throw{}).index() &&
          message.index() <= Message(Defended{}).index()) ||
         (message.index() >= Message(PlayerLevel{}).index() &&
          message.index() <= Message(NpcLevel{}).index()) ||
         std::holds_alternative<CarLend>(message) || std::holds_alternative<NpcScale>(message) ||
         std::holds_alternative<NpcVendor>(message);
}

bool IsTrade(const Message &message) {
  return std::holds_alternative<Trade>(message) || std::holds_alternative<TradeOffer>(message) ||
         std::holds_alternative<TradeCommit>(message);
}

bool IsVehicleCombat(const Message &message) {
  return message.index() >= Message(VehicleDamage{}).index() &&
         message.index() <= Message(Chase{}).index();
}

bool IsReliable(const Message &message) { return ChannelOf(message) != Channel::State; }

const char *NameOf(const Message &message) { return NameOfKind(message.index()); }

const char *NameOfKind(std::size_t kind) {
  constexpr const char *names[] = {"Hello",
                                   "Welcome",
                                   "Refused",
                                   "Ping",
                                   "Pong",
                                   "Roster",
                                   "PlayerState",
                                   "Bye",
                                   "PoseState",
                                   "Appearance",
                                   "Held",
                                   "Shot",
                                   "Seat",
                                   "VehicleState",
                                   "World",
                                   "NpcSpawn",
                                   "NpcStates",
                                   "NpcGone",
                                   "NpcHit",
                                   "NpcHealth",
                                   "NpcEffect",
                                   "Pause",
                                   "VehicleRadio",
                                   "PlayerHit",
                                   "NpcAwareness",
                                   "Stealth",
                                   "Noise",
                                   "NpcHack",
                                   "NpcHackState",
                                   "NpcStatus",
                                   "PlayerHack",
                                   "VehicleHack",
                                   "Journal",
                                   "Tracked",
                                   "PhoneCall",
                                   "PhoneAction",
                                   "PhoneReply",
                                   "Subtitle",
                                   "ChoiceHub",
                                   "ChoiceCursor",
                                   "StoryState",
                                   "QuestReward",
                                   "TimeEffect",
                                   "StoryEffect",
                                   "NpcAction",
                                   "NpcActionReply",
                                   "NpcHold",
                                   "NpcCue",
                                   "Regroup",
                                   "RegroupAnswer",
                                   "WorldItem",
                                   "WorldItemTaken",
                                   "SharedItem",
                                   "Vitals",
                                   "Revive",
                                   "GameOver",
                                   "NpcAnims",
                                   "NpcShot",
                                   "NpcStrike",
                                   "NpcWound",
                                   "NpcPoise",
                                   "Difficulty",
                                   "AreaHit",
                                   "Digest",
                                   "ResyncRequest",
                                   "NpcSet",
                                   "Epoch",
                                   "Elsewhere",
                                   "Transit",
                                   "NpcSeat",
                                   "NpcShotAt",
                                   "ShotHit",
                                   "SceneState",
                                   "SceneCast",
                                   "SceneLine",
                                   "ActorPose",
                                   "DeviceState",
                                   "DeviceUse",
                                   "DeviceSet",
                                   "DeviceDigest",
                                   "Regions",
                                   "NpcOwner",
                                   "NpcHandover",
                                   "NpcHandoverState",
                                   "NpcHandoverDone",
                                   "OwnedDigest",
                                   "RegionHold",
                                   "TrafficSpawn",
                                   "TrafficStates",
                                   "TrafficGone",
                                   "TrafficStatus",
                                   "TrafficHit",
                                   "TrafficClaim",
                                   "TrafficSet",
                                   "Heat",
                                   "Crime",
                                   "Presence",
                                   "Cue",
                                   "NpcReaction",
                                   "Install",
                                   "Throw",
                                   "ThrowEnd",
                                   "Rules",
                                   "Defended",
                                   "Activity",
                                   "TimeSkip",
                                   "TimeSkipVote",
                                   "TimeSkipResult",
                                   "PlayerLevel",
                                   "SessionLevel",
                                   "NpcLevel",
                                   "Trade",
                                   "TradeOffer",
                                   "TradeCommit",
                                   "VehicleUnlock",
                                   "CarLend",
                                   "Mark",
                                   "Tag",
                                   "Link",
                                   "Waypoint",
                                   "VehicleDamage",
                                   "VehicleHit",
                                   "VehicleExit",
                                   "VehicleCombat",
                                   "MountedFire",
                                   "GunnerRequest",
                                   "Gunner",
                                   "GunnerAim",
                                   "VehicleBump",
                                   "NpcRam",
                                   "Chase",
                                   "StoryRestrictions",
                                   "PoseFrame",
                                   "NpcMotion",
                                   "PlayerMotion",
                                   "VehicleMotion",
                                   "NpcScale",
                                   "Breach",
                                   "BreachResult",
                                   "NpcVendor",
"TrafficOwnership",
"TrafficHandover",
"TrafficHandoverState",
"TrafficHandoverDone"};
  static_assert(std::size(names) == std::variant_size_v<Message>);
  return kind < std::size(names) ? names[kind] : "?";
}
} // namespace tandem::proto
