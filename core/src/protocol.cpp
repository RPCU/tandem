#include "tandem/protocol.hpp"

#include <zpp_bits.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>

namespace tandem::proto {
// The wire form is zpp_bits' for the variant: the index in one byte, then
// the alternative. Written out here, without zpp_bits' variant support: its
// compile-time checks that the ids are unique grow with the square of the
// alternatives (they needed raised constexpr limits), and reading into the
// variant compiles std::variant's emplace for every alternative.
static_assert(std::variant_size_v<MessageVariant> <= 256, "a message's id is one byte");

bool Fits(const CarPaint &paint) {
  if ((paint.flags & ~kPaintFlags) != 0 || !std::isfinite(paint.lightsHue))
    return false;
  // A unique template is a record and nothing else; colours name none.
  if ((paint.flags & kPaintUnique) != 0 ? paint.flags != kPaintUnique || paint.unique == 0
                                        : paint.unique != 0 || paint.model != 0)
    return false;
  // A second colour is the two-tone of a first.
  if ((paint.flags & kPaintSecondary) != 0 && (paint.flags & kPaintPrimary) == 0)
    return false;
  if ((paint.flags & kPaintLights) != 0 ? paint.lightsHue < 0.f || paint.lightsHue > 360.f
                                        : paint.lightsHue != 0.f)
    return false;
  if ((paint.flags & kPaintPrimary) == 0 &&
      (paint.primaryR != 0 || paint.primaryG != 0 || paint.primaryB != 0))
    return false;
  if ((paint.flags & kPaintSecondary) == 0 &&
      (paint.secondaryR != 0 || paint.secondaryG != 0 || paint.secondaryB != 0))
    return false;
  return paint.flags == 0 || paint.vehicle != 0;
}

bool Fits(const HitTaken &hit) {
  return hit.by <= HitBy::Player && (hit.by == HitBy::World) == (hit.attacker == 0) &&
         (hit.flags & ~kHitTakenFlags) == 0;
}

bool Fits(const Degraded &degraded) {
  if (degraded.entries.size() > kMaxDegraded)
    return false;
  const std::string *last = nullptr;
  for (const auto &entry : degraded.entries) {
    if (entry.id.empty() || entry.id.size() > kMaxDegradedId || (entry.flags & ~kDegradedFlags))
      return false;
    for (const char c : entry.id)
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-'))
        return false;
    if (last && !(*last < entry.id))
      return false;
    last = &entry.id;
  }
  return true;
}

bool Fits(const CorpsePose &corpse) {
  return corpse.npc != 0 && std::isfinite(corpse.position.x) && std::isfinite(corpse.position.y) &&
         std::isfinite(corpse.position.z) && std::isfinite(corpse.yaw) && !corpse.bodies.empty() &&
         corpse.bodies.size() <= kMaxRagdollBodies;
}

bool Fits(const BossState &boss) {
  if (boss.npc == 0 || !std::isfinite(boss.health) || boss.health < 0.f || boss.health > 1.f ||
      boss.effects.size() > kMaxBossEffects || boss.spots.size() > kMaxBossSpots)
    return false;
  for (std::size_t i = 0; i < boss.effects.size(); ++i)
    if (boss.effects[i] == 0 || (i > 0 && boss.effects[i - 1] >= boss.effects[i]))
      return false;
  return std::ranges::all_of(boss.spots,
                             [](std::uint8_t spot) { return spot <= 100 || spot == kSpotUnknown; });
}

bool Fits(const ScriptedKill &kill) {
  return kill.player != kNoPlayer && kill.player <= kMaxPlayerId && kill.npc != 0 &&
         std::isfinite(kill.position.x) && std::isfinite(kill.position.y) &&
         std::isfinite(kill.position.z);
}

bool Fits(const NpcVoice &voice) {
  return voice.context != 0 && voice.delayMs <= kNpcVoiceMaxDelayMs &&
         (voice.flags & ~kVoiceFlags) == 0;
}

bool Fits(const TrafficLights &lights) {
  if (lights.lights.size() > kMaxLights)
    return false;
  for (const auto &light : lights.lights)
    if (light.entity == 0 || light.color > kLightYellow)
      return false;
  return true;
}

namespace {
// What a bone takes in a pose frame's rotations, by its code.
constexpr std::size_t PoseCodeBytes(std::uint8_t code) {
  return code == kBoneWhole   ? kPoseFrameRotationBytes
         : code == kBoneTurn  ? kPoseTurnBytes
         : code == kBoneNudge ? kPoseNudgeBytes
                              : 0;
}
// A key's codes: every bone whole.
constexpr std::uint8_t kWholeCodes =
    kBoneWhole | kBoneWhole << 2 | kBoneWhole << 4 | kBoneWhole << 6;
static_assert(kPoseBones % 4 == 0, "a key's codes fill their bytes");
// A hit taken's places and ways: a signed byte for -range..range.
std::int8_t GridCode(float value, float range) {
  const float within = std::isfinite(value) ? std::clamp(value / range, -1.f, 1.f) : 0.f;
  return static_cast<std::int8_t>(std::lround(within * 127.f));
}
float GridValue(std::int8_t code, float range) { return static_cast<float>(code) * range / 127.f; }
} // namespace

HitTaken Snapped(HitTaken hit) {
  const auto snap = [](float value, float range) {
    return GridValue(GridCode(value, range), range);
  };
  hit.offset = {snap(hit.offset.x, kHitTakenReach), snap(hit.offset.y, kHitTakenReach),
                snap(hit.offset.z, kHitTakenReach)};
  hit.direction = {snap(hit.direction.x, 1.f), snap(hit.direction.y, 1.f),
                   snap(hit.direction.z, 1.f)};
  return hit;
}

// A hit taken (v18): the player in a byte, its place and its way a signed
// byte an axis (Snapped).
template <class Archive> zpp::bits::errc serialize(Archive &out, const HitTaken &hit) {
  return out(static_cast<std::uint8_t>(hit.player), hit.by, hit.attacker, hit.attack,
             hit.attackType, hit.flags, hit.zone, hit.shape, hit.share,
             GridCode(hit.offset.x, kHitTakenReach), GridCode(hit.offset.y, kHitTakenReach),
             GridCode(hit.offset.z, kHitTakenReach), GridCode(hit.direction.x, 1.f),
             GridCode(hit.direction.y, 1.f), GridCode(hit.direction.z, 1.f));
}

template <class Archive> zpp::bits::errc serialize(Archive &in, HitTaken &hit) {
  std::uint8_t player = 0;
  std::int8_t x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
  const auto result = in(player, hit.by, hit.attacker, hit.attack, hit.attackType, hit.flags,
                         hit.zone, hit.shape, hit.share, x, y, z, dx, dy, dz);
  hit.player = player;
  hit.offset = {GridValue(x, kHitTakenReach), GridValue(y, kHitTakenReach),
                GridValue(z, kHitTakenReach)};
  hit.direction = {GridValue(dx, 1.f), GridValue(dy, 1.f), GridValue(dz, 1.f)};
  return result;
}

// A pose frame's own wire form (v17, the largest stream: netcode.md), which
// zpp_bits finds by the frame's namespace: the player in a byte (there are
// at most kMaxPlayerId), the sequence, how
// many frames back the key is (a varint, 0: a key); a key's time whole,
// another frame's low 32 bits and its codes (a key's are all whole); the
// hips; the rotations, as many bytes as the codes say.
template <class Archive> zpp::bits::errc serialize(Archive &out, const PoseFrame &frame) {
  const bool key = frame.key == frame.seq;
  if (auto result = out(static_cast<std::uint8_t>(frame.player), frame.seq,
                        zpp::bits::vuint32_t{frame.seq - frame.key});
      zpp::bits::failure(result))
    return result;
  if (auto result =
          key ? out(frame.timeUs) : out(static_cast<std::uint32_t>(frame.timeUs), frame.codes);
      zpp::bits::failure(result))
    return result;
  return out(frame.hips, zpp::bits::unsized(frame.rotations));
}

template <class Archive> zpp::bits::errc serialize(Archive &in, PoseFrame &frame) {
  zpp::bits::vuint32_t back;
  std::uint8_t player = 0;
  if (auto result = in(player, frame.seq, back); zpp::bits::failure(result))
    return result;
  frame.player = player;
  frame.key = frame.seq - back.value;
  std::size_t bytes = 0;
  if (back.value == 0) {
    if (auto result = in(frame.timeUs); zpp::bits::failure(result))
      return result;
    frame.codes.fill(kWholeCodes);
    bytes = kPoseBones * kPoseFrameRotationBytes;
  } else {
    std::uint32_t low = 0;
    if (auto result = in(low, frame.codes); zpp::bits::failure(result))
      return result;
    frame.timeUs = low;
    for (std::size_t slot = 0; slot < kPoseBones; ++slot)
      bytes += PoseCodeBytes(frame.Code(slot));
  }
  frame.rotations.resize(bytes);
  return in(frame.hips, zpp::bits::unsized(frame.rotations));
}

// The other streams of every frame and every shot (v17): the player in a
// byte, not four; the rest as zpp_bits would write it.
// The state sent before (v22): its time back, then the rest only when there
// is one (timeBack 0: the stream's first, 2 bytes).
template <class Archive> zpp::bits::errc serialize(Archive &out, const PlayerMotion &motion) {
  if (auto result =
          out(static_cast<std::uint8_t>(motion.player), motion.seq, motion.timeUs, motion.position,
              motion.yaw, motion.velocity, motion.stance, motion.flags, motion.before.timeBack);
      zpp::bits::failure(result) || motion.before.timeBack == 0)
    return result;
  return out(motion.before.offset, motion.before.yaw, motion.before.velocity);
}

template <class Archive> zpp::bits::errc serialize(Archive &in, PlayerMotion &motion) {
  std::uint8_t player = 0;
  const auto result = in(player, motion.seq, motion.timeUs, motion.position, motion.yaw,
                         motion.velocity, motion.stance, motion.flags, motion.before.timeBack);
  motion.player = player;
  if (zpp::bits::failure(result) || motion.before.timeBack == 0)
    return result;
  return in(motion.before.offset, motion.before.yaw, motion.before.velocity);
}

template <class Archive> zpp::bits::errc serialize(Archive &out, const VehicleMotion &motion) {
  if (auto result =
          out(static_cast<std::uint8_t>(motion.player), motion.seq, motion.timeUs, motion.position,
              motion.orientation, motion.velocity, motion.before.timeBack);
      zpp::bits::failure(result) || motion.before.timeBack == 0)
    return result;
  return out(motion.before.offset, motion.before.orientation, motion.before.velocity);
}

template <class Archive> zpp::bits::errc serialize(Archive &in, VehicleMotion &motion) {
  std::uint8_t player = 0;
  const auto result = in(player, motion.seq, motion.timeUs, motion.position, motion.orientation,
                         motion.velocity, motion.before.timeBack);
  motion.player = player;
  if (zpp::bits::failure(result) || motion.before.timeBack == 0)
    return result;
  return in(motion.before.offset, motion.before.orientation, motion.before.velocity);
}

// A cue: the player in a byte; a charge's `at` is its level and its hand, a
// byte each (kChargeLevels steps: ChargeLevel), the other kinds' a place.
template <class Archive> zpp::bits::errc serialize(Archive &out, const Cue &cue) {
  if (auto result = out(static_cast<std::uint8_t>(cue.player), cue.seq, cue.kind, cue.arg);
      zpp::bits::failure(result))
    return result;
  if (cue.kind != CueKind::Charge)
    return out(cue.at);
  return out(static_cast<std::uint8_t>(ChargeLevel(cue.at.x) * kChargeLevels + 0.5f),
             static_cast<std::uint8_t>(cue.at.y == kChargeLeftHand ? 1 : 0));
}

template <class Archive> zpp::bits::errc serialize(Archive &in, Cue &cue) {
  std::uint8_t player = 0, level = 0, hand = 0;
  if (auto result = in(player, cue.seq, cue.kind, cue.arg); zpp::bits::failure(result))
    return result;
  cue.player = player;
  if (cue.kind != CueKind::Charge)
    return in(cue.at);
  const auto result = in(level, hand);
  // Another hand than the two: none (Decode refuses it).
  cue.at = {static_cast<float>(level) / kChargeLevels,
            hand == 0   ? kChargeRightHand
            : hand == 1 ? kChargeLeftHand
                        : -1.f,
            0.f};
  return result;
}

float ChargeLevel(float level) {
  // Not a number: none.
  const float within = level > 0.f ? std::min(level, 1.f) : 0.f;
  return static_cast<float>(static_cast<int>(within * kChargeLevels + 0.5f)) / kChargeLevels;
}

std::vector<std::byte> Encode(const Message &message) {
  std::vector<std::byte> bytes;
  auto out = zpp::bits::out(bytes);
  const auto id = static_cast<std::byte>(message.index());
  if (zpp::bits::failure(
          std::visit([&](const auto &alternative) { return out(id, alternative); }, message)))
    return {};
  return bytes;
}

namespace {
// A message's id (its index in Message), at compile time.
template <class T> constexpr std::size_t kId = tandem::detail::IndexIn<T, MessageVariant>::value;

using Reader = decltype(zpp::bits::in(std::declval<std::span<const std::byte> &>()));

// Reads alternative I of Message.
template <std::size_t I> std::optional<Message> Read(Reader &in) {
  std::variant_alternative_t<I, MessageVariant> alternative{};
  if (zpp::bits::failure(in(alternative)))
    return std::nullopt;
  return Message(std::move(alternative));
}

std::optional<Message> ReadMessage(Reader &in, std::size_t id) {
  static constexpr auto readers = []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array<std::optional<Message> (*)(Reader &), sizeof...(I)>{&Read<I>...};
  }(std::make_index_sequence<std::variant_size_v<MessageVariant>>{});
  if (id >= readers.size())
    return std::nullopt;
  return readers[id](in);
}

// A pose frame's codes name none past the last bone, its rotations are
// exactly what the codes say; a key is whole; another frame's time is its
// low 32 bits.
bool Fits(const PoseFrame &frame) {
  std::size_t bytes = 0;
  const bool key = frame.key == frame.seq;
  if (!key && frame.timeUs > kPoseTimeMask)
    return false;
  for (std::size_t slot = 0; slot < kPoseCodeBytes * 4; ++slot) {
    const auto code = frame.Code(slot);
    if ((slot >= kPoseBones && code != 0) || (key && slot < kPoseBones && code != kBoneWhole))
      return false;
    bytes += PoseCodeBytes(code);
  }
  return frame.rotations.size() == bytes;
}

// A rig's float tracks: each track at most once, in order, four bytes each.
bool Fits(const RigTracks &tracks) {
  if (tracks.part > kRigFace || tracks.tracks == 0 || tracks.tracks > kMaxRigTracks ||
      tracks.data.size() > kMaxRigBytes || tracks.data.size() % kRigTrackEntryBytes != 0)
    return false;
  int last = -1;
  for (std::size_t at = 0; at < tracks.data.size(); at += kRigTrackEntryBytes) {
    const int track = tracks.data[at] | tracks.data[at + 1] << 8;
    if (track <= last || track >= tracks.tracks)
      return false;
    last = track;
  }
  return true;
}

// A rig pose's entries: each bone of the rig at most once, in order, each
// with the bytes of its form and nothing after the last.
bool Fits(const RigPose &pose) {
  if (pose.part > kRigFace || pose.bones == 0 || pose.bones > kMaxRigBones ||
      pose.data.size() > kMaxRigBytes)
    return false;
  constexpr std::size_t kFormBytes[4] = {0, 3, 6, 11};
  std::size_t at = 0;
  int last = -1;
  while (at < pose.data.size()) {
    if (at + 2 > pose.data.size())
      return false;
    const auto head = static_cast<std::uint16_t>(pose.data[at] | pose.data[at + 1] << 8);
    const int bone = head & 0x3FFF;
    if (bone <= last || bone >= pose.bones)
      return false;
    last = bone;
    at += 2 + kFormBytes[head >> 14];
  }
  return at == pose.data.size();
}

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
  auto in = zpp::bits::in(bytes);
  static_assert(std::is_same_v<decltype(in), Reader>);
  std::byte id{};
  if (zpp::bits::failure(in(id)))
    return std::nullopt;
  auto read = ReadMessage(in, std::to_integer<std::size_t>(id));
  if (!read)
    return std::nullopt;
  auto &message = *read;
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
  if (const auto *car = std::get_if<TrafficHack>(&message); car && car->hack > CarHack::Accelerate)
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
  if (const auto *frame = std::get_if<PoseFrame>(&message); frame && !Fits(*frame))
    return std::nullopt;
  if (const auto *actor = std::get_if<ActorFrame>(&message);
      actor && (actor->npc == 0 || !Fits(actor->frame)))
    return std::nullopt;
  if (const auto *rig = std::get_if<RigPose>(&message); rig && (rig->npc == 0 || !Fits(*rig)))
    return std::nullopt;
  if (const auto *tracks = std::get_if<RigTracks>(&message);
      tracks && (tracks->npc == 0 || !Fits(*tracks)))
    return std::nullopt;
  // A view no camera has, or one that is not a number.
  if (const auto *camera = std::get_if<SceneCamera>(&message);
      camera && (!(camera->fov > 0.f && camera->fov <= 180.f) || camera->flags > kCameraCut ||
                 camera->play == 0))
    return std::nullopt;
  if (const auto *prop = std::get_if<SceneProp>(&message);
      prop && (prop->play == 0 || prop->prop == 0 || (prop->flags & ~kPropFlags) != 0 ||
               (prop->holder != 0 && prop->holderPlayer != kNoPlayer)))
    return std::nullopt;
  // A placed prop: of a play, numbered, somewhere real; what to spawn unless it goes.
  if (const auto *place = std::get_if<ScenePlace>(&message);
      place && (place->play == 0 || place->prop == 0 || (place->flags & ~kPropGone) != 0 ||
                (place->entity == 0 && (place->flags & kPropGone) == 0) ||
                !std::isfinite(place->position.x) || !std::isfinite(place->position.y) ||
                !std::isfinite(place->position.z) || !std::isfinite(place->orientation.i) ||
                !std::isfinite(place->orientation.j) || !std::isfinite(place->orientation.k) ||
                !std::isfinite(place->orientation.r)))
    return std::nullopt;
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
  if (const auto *interact = std::get_if<QuestInteract>(&message);
      interact && (interact->verdict > kInteractVerdictMax ||
                   (interact->flags & ~kInteractFlags) != 0 || interact->target == 0))
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
      cue &&
      (cue->kind < CueKind::Trace || cue->kind > CueKind::Charge ||
       (cue->kind == CueKind::Charge ? cue->arg > kChargeMax || (cue->at.y != kChargeRightHand &&
                                                                 cue->at.y != kChargeLeftHand)
                                     : (cue->arg & ~kTraceFlags) != 0)))
    return std::nullopt;
  if (const auto *reaction = std::get_if<NpcReaction>(&message);
      reaction && reaction->type > kReactionMax)
    return std::nullopt;
  if (const auto *install = std::get_if<Install>(&message);
      install &&
      (install->build.size() > kMaxInstallText || install->game.size() > kMaxInstallText))
    return std::nullopt;
  if (const auto *mods = std::get_if<ModList>(&message);
      mods && (mods->kind > kModNames || mods->mods.size() > kMaxModEntries ||
               std::ranges::any_of(
                   mods->mods, [](const ModEntry &mod) { return mod.name.size() > kMaxModName; })))
    return std::nullopt;
  if (const auto *thrown = std::get_if<Throw>(&message); thrown && thrown->kind > ThrowKind::Smart)
    return std::nullopt;
  if (const auto *end = std::get_if<ThrowEnd>(&message); end && end->how > ThrowEndKind::Gone)
    return std::nullopt;
  if (const auto *rules = std::get_if<Rules>(&message);
      rules && ((rules->flags & ~kRuleFlags) != 0 || rules->enemyHealth > kMaxEnemyHealth ||
                rules->levelGap > kMaxLevelGap))
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
  if (const auto *paint = std::get_if<CarPaint>(&message); paint && !Fits(*paint))
    return std::nullopt;
  if (const auto *taken = std::get_if<HitTaken>(&message); taken && !Fits(*taken))
    return std::nullopt;
  if (const auto *degraded = std::get_if<Degraded>(&message); degraded && !Fits(*degraded))
    return std::nullopt;
  if (const auto *voice = std::get_if<NpcVoice>(&message); voice && !Fits(*voice))
    return std::nullopt;
  if (const auto *corpse = std::get_if<CorpsePose>(&message); corpse && !Fits(*corpse))
    return std::nullopt;
  if (const auto *boss = std::get_if<BossState>(&message); boss && !Fits(*boss))
    return std::nullopt;
  if (const auto *kill = std::get_if<ScriptedKill>(&message); kill && !Fits(*kill))
    return std::nullopt;
  if (const auto *lights = std::get_if<TrafficLights>(&message); lights && !Fits(*lights))
    return std::nullopt;
  // A live vendor names its NPC; a dead one only its entity.
  if (const auto *vendor = std::get_if<NpcVendor>(&message);
      vendor && (vendor->entity == 0 || (vendor->flags & ~kVendorDead) != 0 ||
                 ((vendor->flags & kVendorDead) != 0) != (vendor->npc == 0)))
    return std::nullopt;
  // A placed NPC names its NPC and its entity; a death its entity, and either
  // its NPC (from its owner) or the host's stamp, never both.
  if (const auto *placed = std::get_if<NpcPlaced>(&message);
      placed && (placed->npc == 0 || placed->entity == 0))
    return std::nullopt;
  if (const auto *death = std::get_if<NpcDeath>(&message);
      death && (death->entity == 0 || (death->flags & ~kDeathKnockedOut) != 0 ||
                (death->npc != 0) != (death->order == 0) || (death->npc != 0 && death->by != 0)))
    return std::nullopt;
  // A race: a kind and a phase the receiver knows, its lists within their caps.
  if (const auto *race = std::get_if<Race>(&message);
      race &&
      (race->kind < RaceKind::Info || race->kind > RaceKind::Route ||
       (race->kind == RaceKind::Route && race->arg > kRaceRewardsSantoDomingo) ||
       (race->kind == RaceKind::Info && (race->arg < kRaceGrid || race->arg > kRaceAbandoned)) ||
       race->points.size() > kMaxRacePoints || race->entries.size() > kMaxRacers))
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
      waypoint && waypoint->kind > WaypointKind::Objective)
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
  if (const auto *prefabs = std::get_if<WorldPrefabs>(&message);
      prefabs && (prefabs->prefabs.size() > kMaxWorldPrefabs ||
                  std::ranges::adjacent_find(prefabs->prefabs, std::greater_equal<>{}) !=
                      prefabs->prefabs.end() ||
                  std::ranges::find(prefabs->prefabs, std::uint64_t{0}) != prefabs->prefabs.end()))
    return std::nullopt;
  if (const auto *gates = std::get_if<StoryGates>(&message);
      gates && (gates->flags & ~kGateFlags) != 0)
    return std::nullopt;
  if (const auto *visuals = std::get_if<StatusVisuals>(&message);
      visuals && (visuals->records.size() > kMaxStatusVisuals ||
                  std::ranges::adjacent_find(visuals->records, std::greater_equal<>{}) !=
                      visuals->records.end()))
    return std::nullopt;
  // A device's fire: the kinds and targets this version knows.
  if (const auto *fire = std::get_if<DeviceFire>(&message);
      fire && (fire->kind > DeviceFireKind::Lost || !target(fire->target)))
    return std::nullopt;
  if (const auto *hands = std::get_if<TurretHands>(&message);
      hands && (hands->flags & ~kHandsFlags) != 0)
    return std::nullopt;
  if (const auto *health = std::get_if<NpcHealth>(&message);
      health && (health->flags & ~kNpcHealthFlags) != 0)
    return std::nullopt;
  return read;
}

std::optional<std::uint16_t> PeekHelloVersion(std::span<const std::byte> bytes) {
  // The variant's index (Hello is the first alternative), then Hello's
  // magic and version, little-endian.
  const auto at = [&](std::size_t i) { return std::to_integer<std::uint32_t>(bytes[i]); };
  if (bytes.size() < 7 || at(0) != kId<Hello>)
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
  // A device's shot at a V is fired for real by that V's game: it must
  // arrive. Its shots at anyone else are drawn only, many, and of the moment.
  if (const auto *fire = std::get_if<DeviceFire>(&message))
    return fire->kind == DeviceFireKind::Shot && fire->target.kind != TargetKind::Player
               ? Channel::State
               : Channel::Event;
  return std::holds_alternative<PlayerState>(message) || std::holds_alternative<Cue>(message) ||
                 std::holds_alternative<HitTaken>(message) ||
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
                 std::holds_alternative<ActorFrame>(message) ||
                 std::holds_alternative<RigPose>(message) ||
                 std::holds_alternative<RigTracks>(message) ||
                 std::holds_alternative<SceneCamera>(message) ||
                 std::holds_alternative<PoseFrame>(message) ||
                 std::holds_alternative<NpcMotion>(message) ||
                 std::holds_alternative<PlayerMotion>(message) ||
                 std::holds_alternative<VehicleMotion>(message) ||
                 std::holds_alternative<ThrowPath>(message) ||
                 std::holds_alternative<TurretHands>(message)
             ? Channel::State
             // Everything else is reliable and ordered, the consistency
             // messages too (Digest, ResyncRequest, NpcSet, Epoch): a digest
             // is compared where it falls in the reliable stream.
             : Channel::Event;
}

bool IsStory(const Message &message) {
  return (message.index() >= kId<Journal> && message.index() <= kId<QuestReward>) ||
         std::holds_alternative<StoryEffect>(message) ||
         std::holds_alternative<StoryRestrictions>(message) ||
         std::holds_alternative<QuestInteract>(message) ||
         std::holds_alternative<SceneProp>(message) ||
         std::holds_alternative<ScenePlace>(message) ||
         std::holds_alternative<WorldPrefabs>(message) ||
         std::holds_alternative<StoryGates>(message) ||
         (message.index() >= kId<SceneState> && message.index() <= kId<SceneLine>) ||
         std::holds_alternative<VehicleUnlock>(message);
}

bool IsCombat(const Message &message) {
  return (message.index() >= kId<NpcStrike> && message.index() <= kId<AreaHit>) ||
         (message.index() >= kId<Throw> && message.index() <= kId<Defended>) ||
         (message.index() >= kId<PlayerLevel> && message.index() <= kId<NpcLevel>) ||
         std::holds_alternative<CarLend>(message) || std::holds_alternative<CarPaint>(message) ||
         std::holds_alternative<Degraded>(message) || std::holds_alternative<NpcScale>(message) ||
         std::holds_alternative<NpcVendor>(message) || std::holds_alternative<ThrowPath>(message) ||
         std::holds_alternative<NpcPlaced>(message) || std::holds_alternative<NpcDeath>(message) ||
         std::holds_alternative<Race>(message) || std::holds_alternative<WeakSpotHit>(message) ||
         std::holds_alternative<HackTraced>(message) ||
         std::holds_alternative<TrafficLights>(message) ||
         std::holds_alternative<CorpsePose>(message) ||
         std::holds_alternative<BossState>(message) ||
         std::holds_alternative<ScriptedKill>(message);
}

bool IsTrade(const Message &message) {
  return std::holds_alternative<Trade>(message) || std::holds_alternative<TradeOffer>(message) ||
         std::holds_alternative<TradeCommit>(message);
}

bool IsVehicleCombat(const Message &message) {
  return message.index() >= kId<VehicleDamage> && message.index() <= kId<Chase>;
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
                                   "StatusVisuals",
                                   "ModList",
                                   "Substitutes",
                                   "TrafficOwnership",
                                   "TrafficHandover",
                                   "TrafficHandoverState",
                                   "TrafficHandoverDone",
                                   "QuestInteract",
                                   "ActorFrame",
                                   "RigPose",
                                   "SceneCamera",
                                   "SceneProp",
                                   "Fracture",
                                   "DeviceFire",
                                   "ThrowPath",
                                   "NpcPlaced",
                                   "NpcDeath",
                                   "RigTracks",
                                   "TurretHands",
                                   "Race",
                                   "ScenePlace",
                                   "LinkDecline",
                                   "WorldPrefabs",
                                   "StoryGates",
                                   "WeakSpotHit",
                                   "HackTraced",
                                   "TurretBusy",
                                   "TrafficHack",
                                   "CarPaint",
                                   "HitTaken",
                                   "Degraded",
                                   "TrafficLights",
                                   "NpcVoice",
                                   "CorpsePose",
                                   "BossState",
                                   "ScriptedKill"};
  static_assert(std::size(names) == std::variant_size_v<Message>);
  return kind < std::size(names) ? names[kind] : "?";
}
} // namespace tandem::proto
