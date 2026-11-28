#include "tandem/sim/referee.hpp"

#include "tandem/consistency.hpp"
#include "tandem/regions.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace tandem::sim {
namespace {
namespace p = proto;

template <class... Ts> constexpr bool IsAny(const p::Message &message) {
  return (std::holds_alternative<Ts>(message) || ...);
}

// Who may say each message: every alternative of proto::Message has its
// line here, and a message without one does not compile (SpeakerOf).
template <class M> struct SaidBy;
#define TANDEM_SAID_BY(speaker, ...)                                                               \
  template <> struct SaidBy<p::__VA_ARGS__> {                                                      \
    static constexpr Speaker value = Speaker::speaker;                                             \
  };

// What only the host may say: the session, the story, the world, the
// arbitration (docs/architecture/regions.md, "What stays host-only").
TANDEM_SAID_BY(Host, Welcome)
TANDEM_SAID_BY(Host, Refused)
TANDEM_SAID_BY(Host, Pong)
TANDEM_SAID_BY(Host, Roster)
TANDEM_SAID_BY(Host, World)
TANDEM_SAID_BY(Host, Journal)
TANDEM_SAID_BY(Host, Tracked)
TANDEM_SAID_BY(Host, PhoneCall)
TANDEM_SAID_BY(Host, Subtitle)
TANDEM_SAID_BY(Host, ChoiceHub)
TANDEM_SAID_BY(Host, QuestReward)
TANDEM_SAID_BY(Host, StoryEffect)
TANDEM_SAID_BY(Host, GameOver)
TANDEM_SAID_BY(Host, Difficulty)
TANDEM_SAID_BY(Host, Digest)
TANDEM_SAID_BY(Host, Regions)
TANDEM_SAID_BY(Host, NpcHandover)
TANDEM_SAID_BY(Host, OwnedDigest)
TANDEM_SAID_BY(Host, DeviceDigest)
TANDEM_SAID_BY(Host, SceneState)
TANDEM_SAID_BY(Host, SceneCast)
TANDEM_SAID_BY(Host, SceneLine)
TANDEM_SAID_BY(Host, ActorPose)
TANDEM_SAID_BY(Host, ActorFrame)
TANDEM_SAID_BY(Host, RigPose)
TANDEM_SAID_BY(Host, RigTracks)
TANDEM_SAID_BY(Host, SceneCamera)
TANDEM_SAID_BY(Host, SceneProp)
TANDEM_SAID_BY(Host, ScenePlace)
TANDEM_SAID_BY(Host, Rules)
TANDEM_SAID_BY(Host, SessionLevel)
TANDEM_SAID_BY(Host, TradeCommit)
TANDEM_SAID_BY(Host, TimeSkipResult)
TANDEM_SAID_BY(Host, VehicleUnlock)
TANDEM_SAID_BY(Host, StoryRestrictions)
TANDEM_SAID_BY(Host, WorldPrefabs)
TANDEM_SAID_BY(Host, OpenWorldDone)
TANDEM_SAID_BY(Host, BreachResult)
// Who holds a turret is the host's to say (devices::TurretHolds).
TANDEM_SAID_BY(Host, TurretBusy)

// What only a guest sends, and the host never passes on: its hello, its
// questions, its answers to the host's call, a V's own level (the host's
// never travels).
TANDEM_SAID_BY(Guest, Hello)
TANDEM_SAID_BY(Guest, Ping)
TANDEM_SAID_BY(Guest, PhoneAction)
TANDEM_SAID_BY(Guest, PhoneReply)
TANDEM_SAID_BY(Guest, ResyncRequest)
TANDEM_SAID_BY(Guest, DeviceUse)
TANDEM_SAID_BY(Guest, PlayerLevel)

// Either side: a player's word in its own name (SenderOf: the host stamps
// it and relays it), an NPC's or a vehicle's owner's (OwnedBy,
// TrafficOwnedBy), a request to an owner (AsksOwner), or a message with its
// own rule in FromGuest, FromHost or Received (DeviceState, NpcOwner...).
TANDEM_SAID_BY(Either, PlayerState)
TANDEM_SAID_BY(Either, Bye)
TANDEM_SAID_BY(Either, PoseState)
TANDEM_SAID_BY(Either, Appearance)
TANDEM_SAID_BY(Either, Held)
TANDEM_SAID_BY(Either, Shot)
TANDEM_SAID_BY(Either, Seat)
TANDEM_SAID_BY(Either, VehicleState)
TANDEM_SAID_BY(Either, NpcSpawn)
TANDEM_SAID_BY(Either, NpcStates)
TANDEM_SAID_BY(Either, NpcMotion)
TANDEM_SAID_BY(Either, NpcGone)
TANDEM_SAID_BY(Either, NpcHit)
TANDEM_SAID_BY(Either, NpcHealth)
TANDEM_SAID_BY(Either, NpcEffect)
TANDEM_SAID_BY(Either, Pause)
TANDEM_SAID_BY(Either, VehicleRadio)
TANDEM_SAID_BY(Either, PlayerHit)
TANDEM_SAID_BY(Either, NpcAwareness)
TANDEM_SAID_BY(Either, Stealth)
TANDEM_SAID_BY(Either, Noise)
TANDEM_SAID_BY(Either, NpcHack)
TANDEM_SAID_BY(Either, NpcHackState)
TANDEM_SAID_BY(Either, NpcStatus)
TANDEM_SAID_BY(Either, PlayerHack)
TANDEM_SAID_BY(Either, VehicleHack)
TANDEM_SAID_BY(Either, ChoiceCursor)
TANDEM_SAID_BY(Either, StoryState)
TANDEM_SAID_BY(Either, TimeEffect)
TANDEM_SAID_BY(Either, NpcAction)
TANDEM_SAID_BY(Either, NpcActionReply)
TANDEM_SAID_BY(Either, NpcHold)
TANDEM_SAID_BY(Either, NpcCue)
TANDEM_SAID_BY(Either, Regroup)
TANDEM_SAID_BY(Either, RegroupAnswer)
TANDEM_SAID_BY(Either, LinkDecline)
TANDEM_SAID_BY(Either, WorldItem)
TANDEM_SAID_BY(Either, WorldItemTaken)
TANDEM_SAID_BY(Either, SharedItem)
TANDEM_SAID_BY(Either, Vitals)
TANDEM_SAID_BY(Either, Revive)
TANDEM_SAID_BY(Either, NpcAnims)
TANDEM_SAID_BY(Either, NpcShot)
TANDEM_SAID_BY(Either, NpcStrike)
TANDEM_SAID_BY(Either, NpcWound)
TANDEM_SAID_BY(Either, NpcPoise)
TANDEM_SAID_BY(Either, NpcLevel)
TANDEM_SAID_BY(Either, Defended)
TANDEM_SAID_BY(Either, AreaHit)
TANDEM_SAID_BY(Either, NpcSet)
TANDEM_SAID_BY(Either, Epoch)
TANDEM_SAID_BY(Either, Elsewhere)
TANDEM_SAID_BY(Either, Transit)
TANDEM_SAID_BY(Either, NpcSeat)
TANDEM_SAID_BY(Either, NpcShotAt)
TANDEM_SAID_BY(Either, ShotHit)
TANDEM_SAID_BY(Either, DeviceState)
TANDEM_SAID_BY(Either, DeviceSet)
TANDEM_SAID_BY(Either, NpcOwner)
TANDEM_SAID_BY(Either, NpcHandoverState)
TANDEM_SAID_BY(Either, NpcHandoverDone)
TANDEM_SAID_BY(Either, RegionHold)
TANDEM_SAID_BY(Either, TrafficSpawn)
TANDEM_SAID_BY(Either, TrafficStates)
TANDEM_SAID_BY(Either, TrafficGone)
TANDEM_SAID_BY(Either, TrafficStatus)
TANDEM_SAID_BY(Either, TrafficHit)
TANDEM_SAID_BY(Either, TrafficHack)
TANDEM_SAID_BY(Either, TrafficClaim)
TANDEM_SAID_BY(Either, TrafficSet)
TANDEM_SAID_BY(Either, Heat)
TANDEM_SAID_BY(Either, Crime)
TANDEM_SAID_BY(Either, Presence)
TANDEM_SAID_BY(Either, Cue)
TANDEM_SAID_BY(Either, HitTaken)
TANDEM_SAID_BY(Either, NpcReaction)
TANDEM_SAID_BY(Either, Install)
TANDEM_SAID_BY(Either, Throw)
TANDEM_SAID_BY(Either, ThrowEnd)
TANDEM_SAID_BY(Either, Activity)
TANDEM_SAID_BY(Either, TimeSkip)
TANDEM_SAID_BY(Either, TimeSkipVote)
TANDEM_SAID_BY(Either, Trade)
TANDEM_SAID_BY(Either, TradeOffer)
TANDEM_SAID_BY(Either, CarLend)
TANDEM_SAID_BY(Either, CarPaint)
TANDEM_SAID_BY(Either, OwnCar)
TANDEM_SAID_BY(Either, Degraded)
TANDEM_SAID_BY(Either, Mark)
TANDEM_SAID_BY(Either, Tag)
TANDEM_SAID_BY(Either, Link)
TANDEM_SAID_BY(Either, Waypoint)
TANDEM_SAID_BY(Either, VehicleDamage)
TANDEM_SAID_BY(Either, VehicleHit)
TANDEM_SAID_BY(Either, VehicleExit)
TANDEM_SAID_BY(Either, VehicleCombat)
TANDEM_SAID_BY(Either, MountedFire)
TANDEM_SAID_BY(Either, GunnerRequest)
TANDEM_SAID_BY(Either, Gunner)
TANDEM_SAID_BY(Either, GunnerAim)
TANDEM_SAID_BY(Either, VehicleBump)
TANDEM_SAID_BY(Either, NpcRam)
TANDEM_SAID_BY(Either, Chase)
TANDEM_SAID_BY(Either, PoseFrame)
TANDEM_SAID_BY(Either, PlayerMotion)
TANDEM_SAID_BY(Either, VehicleMotion)
TANDEM_SAID_BY(Either, NpcScale)
TANDEM_SAID_BY(Either, Breach)
TANDEM_SAID_BY(Either, NpcVendor)
TANDEM_SAID_BY(Either, StatusVisuals)
TANDEM_SAID_BY(Either, ModList)
TANDEM_SAID_BY(Either, Substitutes)
TANDEM_SAID_BY(Host, TrafficOwnership)
TANDEM_SAID_BY(Host, TrafficHandover)
TANDEM_SAID_BY(Either, TrafficHandoverState)
TANDEM_SAID_BY(Either, TrafficHandoverDone)
TANDEM_SAID_BY(Either, QuestInteract)
TANDEM_SAID_BY(Either, Fracture)
TANDEM_SAID_BY(Either, DeviceFire)
TANDEM_SAID_BY(Either, ThrowPath)
// A placed NPC: its owner's word. A death of the ledger: its owner's to the
// host (unstamped), the host's to everyone (stamped): Received.
TANDEM_SAID_BY(Either, NpcPlaced)
TANDEM_SAID_BY(Either, NpcDeath)
TANDEM_SAID_BY(Either, TurretHands)
// A player's story gates: its own word about its own story (stamped).
TANDEM_SAID_BY(Either, StoryGates)
// A street race: a player's word in its own name; what describes the race,
// seats, orders and pays is its starter's alone (CheckValues).
TANDEM_SAID_BY(Either, Race)
// What a proxy cannot do, asked of the NPC's owner (AsksOwner).
TANDEM_SAID_BY(Either, WeakSpotHit)
TANDEM_SAID_BY(Either, HackTraced)
TANDEM_SAID_BY(Either, TrafficLights)
// An NPC's voice line: its owner's word (OwnedBy), as its reactions.
TANDEM_SAID_BY(Either, NpcVoice)
// A body's rest: its owner's word (the host checks whose, as a wound's).
TANDEM_SAID_BY(Either, CorpsePose)
// Physics props: the game that runs a prop's area says how it moves and
// rests (stamped: owner); a push is a player's own (stamped: player).
TANDEM_SAID_BY(Either, PropStates)
TANDEM_SAID_BY(Either, PropRest)
TANDEM_SAID_BY(Either, PropPush)
#undef TANDEM_SAID_BY

// A new message without its SaidBy line above fails here ("incomplete type").
constexpr auto kSaidBy = []<std::size_t... I>(std::index_sequence<I...>) {
  return std::array<Speaker, sizeof...(I)>{
      SaidBy<std::variant_alternative_t<I, p::Message>>::value...};
}(std::make_index_sequence<std::variant_size_v<p::Message>>{});

bool HostOnly(const p::Message &m) { return kSaidBy[m.index()] == Speaker::Host; }
// (Noise goes to every linked player since regions: each owner's NPCs hear it.)
bool GuestOnly(const p::Message &m) { return kSaidBy[m.index()] == Speaker::Guest; }

// What an NPC's owner says about it (the host, or since regions the guest
// whose game runs it): the NPCs it names.
std::vector<p::NpcId> OwnedBy(const p::Message &m) {
  return std::visit(
      [](const auto &x) -> std::vector<p::NpcId> {
        using M = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<M, p::NpcSpawn> || std::is_same_v<M, p::NpcGone> ||
                      std::is_same_v<M, p::NpcHealth> || std::is_same_v<M, p::NpcHold> ||
                      std::is_same_v<M, p::NpcSeat> || std::is_same_v<M, p::NpcPoise> ||
                      std::is_same_v<M, p::NpcWound> || std::is_same_v<M, p::NpcHackState> ||
                      std::is_same_v<M, p::NpcStatus> || std::is_same_v<M, p::PlayerHit> ||
                      std::is_same_v<M, p::PlayerHack> || std::is_same_v<M, p::NpcCue> ||
                      std::is_same_v<M, p::NpcShotAt> || std::is_same_v<M, p::NpcActionReply> ||
                      std::is_same_v<M, p::NpcReaction> || std::is_same_v<M, p::NpcVoice> ||
                      std::is_same_v<M, p::NpcScale> || std::is_same_v<M, p::NpcLevel> ||
                      std::is_same_v<M, p::NpcPlaced>)
          return {x.npc};
        else if constexpr (std::is_same_v<M, p::NpcDeath>)
          // Its owner's report names its NPC; the host's word names none.
          return x.npc != 0 ? std::vector<p::NpcId>{x.npc} : std::vector<p::NpcId>{};
        else if constexpr (std::is_same_v<M, p::NpcVendor>)
          // A live vendor is its owner's word; a death names no NPC.
          return x.npc != 0 ? std::vector<p::NpcId>{x.npc} : std::vector<p::NpcId>{};
        else if constexpr (std::is_same_v<M, p::SharedItem>)
          return x.kind == p::kSharedOnCorpse ? std::vector<p::NpcId>{x.npc}
                                              : std::vector<p::NpcId>{};
        else if constexpr (std::is_same_v<M, p::NpcAnims>) {
          std::vector<p::NpcId> ids;
          for (const auto &anim : x.npcs)
            ids.push_back(anim.npc);
          return ids;
        } else
          return {};
      },
      m);
}

// A world vehicle its owner speaks for (traffic.md: the announcer's id is in
// it, a hand-over moves it): what it is, how it stands, that it is gone, its
// damage, whom it chases.
std::optional<p::TrafficId> TrafficOwnedBy(const p::Message &m) {
  if (const auto *spawn = std::get_if<p::TrafficSpawn>(&m))
    return spawn->id;
  if (const auto *status = std::get_if<p::TrafficStatus>(&m))
    return status->id;
  if (const auto *gone = std::get_if<p::TrafficGone>(&m))
    return gone->id;
  if (const auto *chase = std::get_if<p::Chase>(&m))
    return chase->vehicle;
  if (const auto *damage = std::get_if<p::VehicleDamage>(&m);
      damage && p::IsStreetCar(damage->vehicle))
    return p::TrafficOfSeat(damage->vehicle);
  return std::nullopt;
}
std::optional<p::TrafficId> TrafficAsked(const p::Message &m) {
  if (const auto *hit = std::get_if<p::TrafficHit>(&m))
    return hit->id;
  if (const auto *claim = std::get_if<p::TrafficClaim>(&m))
    return claim->id;
  return std::nullopt;
}

// A request to an NPC's owner (through the host, which stamps the sender).
bool AsksOwner(const p::Message &m) {
  return IsAny<p::NpcHit, p::NpcStrike, p::NpcEffect, p::NpcHack, p::NpcAction, p::ShotHit,
               p::Defended, p::WeakSpotHit, p::HackTraced>(m);
}
p::NpcId NpcOf(const p::Message &m) {
  return std::visit(
      [](const auto &x) -> p::NpcId {
        if constexpr (requires { x.npc; })
          return static_cast<p::NpcId>(x.npc);
        else
          return 0;
      },
      m);
}

// The field of a message a game sends in its own name (nullptr: none).
p::PlayerId *SenderOf(p::Message &message) {
  return std::visit(
      [](auto &m) -> p::PlayerId * {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, p::AreaHit> || std::is_same_v<M, p::Regroup> ||
                      std::is_same_v<M, p::RegroupAnswer> || std::is_same_v<M, p::LinkDecline> ||
                      std::is_same_v<M, p::NpcHandoverState> ||
                      std::is_same_v<M, p::TrafficHandoverState> || std::is_same_v<M, p::Mark> ||
                      std::is_same_v<M, p::Tag> || std::is_same_v<M, p::TimeSkip> ||
                      std::is_same_v<M, p::TimeSkipVote> || std::is_same_v<M, p::Trade>)
          return &m.from;
        else if constexpr (std::is_same_v<M, p::TrafficLights> ||
                           std::is_same_v<M, p::PropStates> || std::is_same_v<M, p::PropRest>)
          return &m.owner;
        else if constexpr (std::is_same_v<M, p::PropPush>)
          return &m.player;
        else if constexpr (std::is_same_v<M, p::WorldItem>)
          return &m.dropper;
        else if constexpr (std::is_same_v<M, p::WorldItemTaken>)
          return &m.taker;
        else if constexpr (std::is_same_v<M, p::PlayerState> || std::is_same_v<M, p::PoseState> ||
                           std::is_same_v<M, p::Appearance> || std::is_same_v<M, p::Held> ||
                           std::is_same_v<M, p::Shot> || std::is_same_v<M, p::Seat> ||
                           std::is_same_v<M, p::VehicleState> ||
                           std::is_same_v<M, p::VehicleRadio> || std::is_same_v<M, p::Pause> ||
                           std::is_same_v<M, p::Stealth> || std::is_same_v<M, p::TimeEffect> ||
                           std::is_same_v<M, p::Elsewhere> || std::is_same_v<M, p::Transit> ||
                           std::is_same_v<M, p::Vitals> || std::is_same_v<M, p::Revive> ||
                           std::is_same_v<M, p::NpcHit> || std::is_same_v<M, p::NpcEffect> ||
                           std::is_same_v<M, p::NpcHack> || std::is_same_v<M, p::NpcAction> ||
                           std::is_same_v<M, p::NpcStrike> || std::is_same_v<M, p::Noise> ||
                           std::is_same_v<M, p::ShotHit> || std::is_same_v<M, p::VehicleHack> ||
                           std::is_same_v<M, p::SharedItem> || std::is_same_v<M, p::Epoch> ||
                           std::is_same_v<M, p::PhoneAction> || std::is_same_v<M, p::PhoneReply> ||
                           std::is_same_v<M, p::ChoiceCursor> || std::is_same_v<M, p::StoryState> ||
                           std::is_same_v<M, p::RegionHold> || std::is_same_v<M, p::DeviceUse> ||
                           std::is_same_v<M, p::Heat> || std::is_same_v<M, p::Presence> ||
                           std::is_same_v<M, p::Cue> || std::is_same_v<M, p::TrafficHit> ||
                           std::is_same_v<M, p::HitTaken> || std::is_same_v<M, p::TrafficHack> ||
                           std::is_same_v<M, p::TrafficClaim> || std::is_same_v<M, p::PoseFrame> ||
                           std::is_same_v<M, p::PlayerMotion> ||
                           std::is_same_v<M, p::VehicleMotion> || std::is_same_v<M, p::Throw> ||
                           std::is_same_v<M, p::ThrowEnd> || std::is_same_v<M, p::Activity> ||
                           std::is_same_v<M, p::CarLend> || std::is_same_v<M, p::CarPaint> ||
                           std::is_same_v<M, p::OwnCar> || std::is_same_v<M, p::Degraded> ||
                           std::is_same_v<M, p::Link> || std::is_same_v<M, p::Waypoint> ||
                           std::is_same_v<M, p::Defended> || std::is_same_v<M, p::TradeOffer> ||
                           std::is_same_v<M, p::Breach> || std::is_same_v<M, p::StatusVisuals> ||
                           std::is_same_v<M, p::QuestInteract> || std::is_same_v<M, p::Fracture> ||
                           std::is_same_v<M, p::DeviceFire> || std::is_same_v<M, p::ThrowPath> ||
                           std::is_same_v<M, p::TurretHands> || std::is_same_v<M, p::Race> ||
                           std::is_same_v<M, p::StoryGates> || std::is_same_v<M, p::WeakSpotHit> ||
                           std::is_same_v<M, p::HackTraced>)
          return &m.player;
        else
          return nullptr;
      },
      message);
}

bool Finite(const p::Vec3 &v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Finite(const p::Quat &q) {
  return std::isfinite(q.i) && std::isfinite(q.j) && std::isfinite(q.k) && std::isfinite(q.r);
}

// A sequence number that is not newer than the last (wrapping).
bool NotNewer(std::uint32_t seq, std::uint32_t last) {
  return static_cast<std::int32_t>(seq - last) <= 0;
}

bool Fires(const p::Shot &shot) {
  return shot.effect == 15 || shot.effect == 16 || shot.effect == 17 || shot.effect >= 200;
}

constexpr std::uint64_t kAgainUs = 10'000'000; // a rate finding repeats this seldom
constexpr p::PlayerId kTakenByUnknown = 0xFFFFFFFF;
} // namespace

Speaker SpeakerOf(std::size_t index) { return kSaidBy.at(index); }

const char *MessageName(std::size_t index) { return p::NameOfKind(index); }

std::string Finding::Line() const {
  return std::format("{}: [{}] {}", hint ? "referee-hint" : "referee", rule, text);
}

Referee::Referee(bool host, Options options)
    : m_host(host), m_options(options), m_received(std::variant_size_v<p::Message>),
      m_sent(std::variant_size_v<p::Message>) {
  if (host) {
    m_self = m_hostPlayer = 1;
    m_players = m_everPlayers = {1};
    m_welcomed = true;
  }
}

void Referee::Attach(Endpoint &endpoint) {
  m_endpoint = &endpoint;
  const auto us = [](std::chrono::milliseconds ms) {
    return static_cast<std::uint64_t>(ms.count()) * 1000;
  };
  const auto &timing = endpoint.Timings();
  m_options.limits.digestGraceUs =
      2 * us(timing.digest) + 2 * us(timing.resyncCooldown) + 4'000'000;
  m_options.limits.digestSilenceUs = 6 * us(timing.digest) + 2'000'000;
  // Digests at the endpoint's own pace (tests shorten it), with room.
  if (timing.digest.count() > 0) {
    m_options.limits.digestHz =
        std::max(m_options.limits.digestHz, 2.f * 1e6f / static_cast<float>(us(timing.digest)));
    // A guest asks at most once per digest it hears (consistency::Checker,
    // each domain once per cooldown): its requests follow the digests' pace,
    // with the same room. 6 in 10 s at the game's 3 s.
    m_options.limits.resyncsPer10s =
        std::max(m_options.limits.resyncsPer10s,
                 static_cast<std::uint32_t>(2 * 10'000'000 / us(timing.digest)));
  }
  // A guest that vanished owes nothing once the host has noticed it is gone.
  m_options.limits.epochAckUs =
      std::max(m_options.limits.epochAckUs, us(timing.timeout) + 2'000'000);
  endpoint.SetWireTap(
      [this, &endpoint](Endpoint::Wire wire, p::PlayerId peer, const p::Message &message) {
        const auto now = net::NowUs();
        if (wire == Endpoint::Wire::Out) {
          Sent(peer, message, now);
        } else if (!m_host && std::holds_alternative<p::Digest>(message)) {
          const auto ours = endpoint.LocalDigest();
          Received(peer, message, now, endpoint.HostNowUs(), &ours);
        } else {
          Received(peer, message, now, endpoint.HostNowUs());
        }
      });
}

void Referee::Tick() { Tick(net::NowUs()); }

void Referee::Add(std::string rule, std::string text, bool hint) {
  ++(hint ? m_hints : m_violations);
  if (m_onFinding)
    m_onFinding({rule, text, hint, m_nowUs});
  // The first few of each rule are told; the rest are only counted.
  if (++m_counts[rule] <= 12)
    m_new.push_back({std::move(rule), std::move(text), hint, m_nowUs});
}

std::vector<Finding> Referee::Take() { return std::exchange(m_new, {}); }

void Referee::Forget(p::PlayerId player) {
  m_lives.erase(player);
  m_wasDownUs.erase(player);
  m_time.erase(player);
  m_seats.erase(player);
  m_epochs.erase(player);
  m_epochOwed.erase(player);
  std::erase_if(m_seqs, [&](const auto &entry) { return entry.first.second == player; });
  std::erase_if(m_rates, [&](const auto &entry) { return entry.first.second == player; });
  std::erase_if(m_revives, [&](const auto &entry) {
    return entry.first.first == player || entry.first.second == player;
  });
  std::erase_if(m_dropTokens, [&](const auto &entry) { return entry.first == player; });
  std::erase_if(m_resyncs, [&](const auto &entry) { return entry.first.first == player; });
  std::erase_if(m_turretRefusals, [&](const auto &entry) { return entry.first.first == player; });
}

void Referee::NeedNpc(p::NpcId npc, const char *what) {
  if (!m_npcs.contains(npc))
    Add("ids.unknown-npc",
        std::format("{} names npc {}, which the host never announced", what, npc));
}

void Referee::NeedShown(p::NpcId npc, const char *what) {
  // Out of our view (it went "away": a link broke, its region's edge, a
  // resync's set), and not back: its owner's word on it is for the games
  // that show it (regions.md, "What travels where").
  const auto known = m_npcs.find(npc);
  if (known == m_npcs.end() || known->second.live || known->second.dead ||
      known->second.goneReason != p::kGoneAway ||
      m_nowUs - known->second.goneUs < m_options.limits.notSharedGraceUs)
    return;
  Add("authority.not-shared",
      std::format("{} names npc {}, which the host no longer shares with us ({:.1f} s ago)", what,
                  npc, static_cast<double>(m_nowUs - known->second.goneUs) / 1e6));
}

void Referee::NeedPlayer(p::PlayerId player, const char *what) {
  if (player != p::kNoPlayer && !m_everPlayers.contains(player))
    Add("ids.unknown-player",
        std::format("{} names player {}, who was never in the session", what, player));
}

p::PlayerId Referee::OwnerOf(p::NpcId npc) const {
  const auto it = m_owners.find(npc);
  return it == m_owners.end() ? regions::Announcer(npc) : it->second;
}

p::PlayerId Referee::CarOwnerOf(p::TrafficId car) const {
  const auto it = m_carOwners.find(car);
  return it == m_carOwners.end() ? p::TrafficOwner(car) : it->second;
}

void Referee::Sent(p::PlayerId to, const p::Message &message, std::uint64_t nowUs) {
  m_nowUs = nowUs;
  ++m_sent[message.index()];
  // Our goodbye: what still comes on this connection is moot.
  if (std::holds_alternative<p::Bye>(message))
    m_left = true;
  if (const auto *welcome = std::get_if<p::Welcome>(&message)) {
    // A player joins, or is back from a crash under its old id: a fresh game.
    Forget(welcome->you);
    m_everPlayers.insert(welcome->you);
    return;
  }
  if (const auto *epoch = std::get_if<p::Epoch>(&message)) {
    // A host also passes the guests' own on: only ours is our world's.
    if (m_host && epoch->player != m_self)
      return;
    m_ourEpoch = epoch->epoch;
    // Our world was rebuilt: every guest owes us its word that it saw it.
    if (m_host && to == p::kNoPlayer && epoch->reason != p::kEpochJoin)
      for (const auto player : m_players)
        if (player != m_self)
          m_epochOwed[player] = {nowUs};
    return;
  }
  if (const auto *busy = std::get_if<p::TurretBusy>(&message); busy && m_host) {
    // Host: a guest's claim on a turret another holds, refused. Its game
    // lets go at once; refused again and again (once a second while it
    // claims) it never did.
    auto &run = m_turretRefusals[{to, busy->entity}];
    if (run.count == 0 || nowUs > run.lastUs + 1'500'000) {
      run = TurretRefusals{.firstUs = nowUs, .lastUs = nowUs, .count = 1};
    } else {
      run.lastUs = nowUs;
      if (++run.count == 3)
        Add("authority.turret-taken",
            std::format("player {} kept claiming turret {:x} for {:.1f} s after we refused it "
                        "(player {} holds it): its game did not let go",
                        to, busy->entity, static_cast<double>(nowUs - run.firstUs) / 1e6,
                        busy->holder));
    }
  }
  if (!m_host) {
    // Our claims on turrets: the host may refuse them.
    if (const auto *hands = std::get_if<p::TurretHands>(&message);
        hands && !(hands->flags & p::kHandsLetGo))
      m_turretsClaimed.insert(hands->entity);
    // Our breach: the host answers it (unless the minigame was lost).
    if (const auto *breach = std::get_if<p::Breach>(&message);
        breach && !(breach->flags & p::kBreachFailed))
      ++m_breachesAsked[{breach->entity, breach->cls, breach->npc}];
    if (const auto *action = std::get_if<p::NpcAction>(&message)) {
      const std::pair key{action->npc, action->kind};
      ++m_asked[key];
      const bool needsLife = action->kind == p::NpcActionKind::Grab ||
                             action->kind == p::NpcActionKind::Takedown ||
                             action->kind == p::NpcActionKind::Knockout;
      if (const auto it = m_npcs.find(action->npc); needsLife && it != m_npcs.end() &&
                                                    it->second.dead &&
                                                    nowUs - it->second.goneUs > 1'000'000)
        m_askedOnDead.insert(key);
    } else if (const auto *taken = std::get_if<p::WorldItemTaken>(&message)) {
      m_claimed.insert({taken->id, taken->stack});
    } else if (const auto *hack = std::get_if<p::NpcHack>(&message)) {
      m_uploads.insert({hack->npc, hack->action});
    } else if (const auto *ping = std::get_if<p::Ping>(&message)) {
      m_pings.push_back(ping->sentUs);
      if (m_pings.size() > 64)
        m_pings.pop_front();
    } else if (const auto *strike = std::get_if<p::NpcStrike>(&message)) {
      ++m_hitsAndDrops[strike->npc].first;
    } else if (const auto *hit = std::get_if<p::NpcHit>(&message)) {
      ++m_hitsAndDrops[hit->npc].first;
    }
  }
  Note(message, true);
}

void Referee::Received(p::PlayerId from, const p::Message &message, std::uint64_t nowUs,
                       std::uint64_t hostUs, const p::Digest *ours) {
  m_nowUs = nowUs;
  ++m_received[message.index()];
  if (m_left)
    return;
  const auto bytes = p::Encode(message);
  const char *name = p::NameOf(message);

  if (m_host) {
    if (HostOnly(message) ||
        (std::holds_alternative<p::DeviceState>(message) &&
         std::get<p::DeviceState>(message).how != p::kDeviceAsk) ||
        // A quest interaction's verdict is the host's game's (a guest asks).
        (std::holds_alternative<p::QuestInteract>(message) &&
         std::get<p::QuestInteract>(message).verdict != p::kInteractAsk) ||
        // What a device turns to and shoots at is the host's turret's doing
        // (a guest says only that its copy sees its own V).
        (std::holds_alternative<p::DeviceFire>(message) &&
         std::get<p::DeviceFire>(message).kind <= p::DeviceFireKind::Shot) ||
        (std::holds_alternative<p::NpcOwner>(message) &&
         !(std::get<p::NpcOwner>(message).flags & p::kNpcPinned)) ||
        // A death of the ledger is stamped by the host alone.
        (std::holds_alternative<p::NpcDeath>(message) &&
         std::get<p::NpcDeath>(message).order != 0)) {
      Add("authority.host-only",
          std::format("player {} sent {}: only the host says that", from, name));
      return;
    }
    // A guest speaks for the NPCs its game runs, and no others. (States may
    // lag a hand-over by a datagram: the endpoint drops those.)
    if (p::ChannelOf(message) != p::Channel::State)
      for (const auto npc : OwnedBy(message)) {
        // A spawn: of its own range, or one it took over (its set again).
        const bool spawn = std::holds_alternative<p::NpcSpawn>(message);
        const auto owner = OwnerOf(npc);
        if (owner != from && !(spawn && regions::Announcer(npc) == from)) {
          Add("authority.not-owner",
              std::format("player {} sent {} about npc {}, which {}", from, name, npc,
                          spawn ? "is not in its range of ids"
                                : std::format("player {} runs", owner)));
          break;
        }
      }
    // ...and for the shared vehicles its game runs: a new one of its own
    // range, else one it runs now (a hand-over moves it). A gone that is
    // a claim it granted is its word too.
    if (const auto car = TrafficOwnedBy(message); car) {
      const bool spawn = std::holds_alternative<p::TrafficSpawn>(message);
      const auto owner = spawn ? p::TrafficOwner(*car) : CarOwnerOf(*car);
      if (owner != from)
        Add("authority.not-owner", std::format("player {} sent {} about vehicle {:#x}, which "
                                               "player {} runs",
                                               from, name, *car, owner));
    }
    // A hand-over's state only from the vehicle's owner.
    // (Dropped by the endpoint: it moves nothing, here either.)
    if (const auto *given = std::get_if<p::TrafficHandoverState>(&message);
        given && CarOwnerOf(given->id) != from) {
      Add("authority.not-owner",
          std::format("player {} gave vehicle {:#x} away, which player {} runs", from, given->id,
                      CarOwnerOf(given->id)));
      return;
    }
    // A guest speaks for props where its game runs the area, or for one it
    // speaks for already (its rest after the area changed hands). Views of
    // the area lag by a link: a hint, not a failure.
    if (m_endpoint && (std::holds_alternative<p::PropStates>(message) ||
                       std::holds_alternative<p::PropRest>(message))) {
      std::uint64_t entity = 0;
      std::uint8_t part = 0;
      if (const auto *states = std::get_if<p::PropStates>(&message);
          states && !states->props.empty()) {
        entity = states->props.front().entity;
        part = states->props.front().part;
      } else if (const auto *rest = std::get_if<p::PropRest>(&message)) {
        entity = rest->entity;
        part = rest->part;
      }
      if (entity != 0 && m_endpoint->AreaOwner(from) != from &&
          m_endpoint->PropSpeaker(entity, part) != std::optional<p::PlayerId>{from})
        Hint("authority.not-owner",
             std::format("player {} sent {} about prop {:#x} in an area player {} runs", from, name,
                         entity, m_endpoint->AreaOwner(from)));
    }
    // A guest speaks in its own name only.
    auto stamped = message;
    if (auto *sender = SenderOf(stamped)) {
      if (*sender != p::kNoPlayer && *sender != from)
        Add("authority.not-yours",
            std::format("player {} sent {} in the name of player {}", from, name, *sender));
      *sender = from;
    }
    CheckValues(stamped, hostUs);
    CheckRate(from, stamped, bytes.size());
    CheckSequence(from, stamped);
    CheckOneShot(from, stamped, bytes);
    FromGuest(from, stamped);
    Note(stamped, false);
    return;
  }

  if (const auto *welcome = std::get_if<p::Welcome>(&message)) {
    if (m_welcomed)
      Add("oneshot.welcome-twice", "the host welcomed us twice on one connection");
    m_welcomed = true;
    m_self = welcome->you;
    m_hostPlayer = welcome->host;
    m_everPlayers.insert({m_self, m_hostPlayer});
    m_lastDigestUs = nowUs;
    return;
  }
  if (GuestOnly(message) ||
      (std::holds_alternative<p::DeviceState>(message) &&
       std::get<p::DeviceState>(message).how == p::kDeviceAsk) ||
      (std::holds_alternative<p::QuestInteract>(message) &&
       std::get<p::QuestInteract>(message).verdict == p::kInteractAsk) ||
      (std::holds_alternative<p::DeviceFire>(message) &&
       std::get<p::DeviceFire>(message).kind > p::DeviceFireKind::Shot) ||
      // A death not stamped is its owner's report, for the host.
      (std::holds_alternative<p::NpcDeath>(message) && std::get<p::NpcDeath>(message).order == 0)) {
    Add("authority.guest-only", std::format("the host sent {}: only a guest sends that", name));
    return;
  }
  // A request reaches us only for an NPC our game runs; what is said of an
  // NPC is said by its owner, never of ours by another.
  if (AsksOwner(message) && OwnerOf(NpcOf(message)) != m_self)
    Add("authority.not-owner", std::format("the host passed us {} for npc {}, which player {} runs",
                                           name, NpcOf(message), OwnerOf(NpcOf(message))));
  // (The host's record letting go of one of ours, NpcGone "away": nobody
  // could take it over, or our set named one it no longer holds.)
  const auto *letGo = std::get_if<p::NpcGone>(&message);
  if (p::ChannelOf(message) != p::Channel::State && !(letGo && letGo->reason == p::kGoneAway))
    for (const auto npc : OwnedBy(message))
      if (OwnerOf(npc) == m_self && !std::holds_alternative<p::NpcSpawn>(message)) {
        Add("authority.not-owner",
            std::format("the host sent {} about npc {}, which our game runs", name, npc));
        break;
      }
  if (const auto car = TrafficAsked(message); car && CarOwnerOf(*car) != m_self)
    Add("authority.not-owner", std::format("the host passed us {} for vehicle {:#x}, which player "
                                           "{} runs",
                                           name, *car, CarOwnerOf(*car)));
  // (A spawn of ours sent back is a repeat the endpoint ignores.)
  if (const auto car = TrafficOwnedBy(message);
      car && CarOwnerOf(*car) == m_self && !std::holds_alternative<p::TrafficSpawn>(message))
    Add("authority.not-owner",
        std::format("the host sent {} about vehicle {:#x}, which our game runs", name, *car));
  if (const auto *handover = std::get_if<p::TrafficHandover>(&message);
      handover && CarOwnerOf(handover->id) != m_self)
    Add("authority.not-owner", std::format("the host asks us to hand over vehicle {:#x}, which "
                                           "player {} runs",
                                           handover->id, CarOwnerOf(handover->id)));
  if (const auto *given = std::get_if<p::TrafficHandoverState>(&message);
      given && given->to != m_self)
    Add("authority.misdelivered", std::format("the host gave us the state of vehicle {:#x}, which "
                                              "goes to player {}",
                                              given->id, given->to));
  if (const auto *crime = std::get_if<p::Crime>(&message); crime && crime->player != m_self)
    Add("authority.misdelivered",
        std::format("a crime of player {} came to us (player {})", crime->player, m_self));
  if (const auto *handover = std::get_if<p::NpcHandover>(&message);
      handover && OwnerOf(handover->npc) != m_self)
    Add("authority.not-owner", std::format("the host asks us to hand over npc {}, which player {} "
                                           "runs",
                                           handover->npc, OwnerOf(handover->npc)));
  from = m_hostPlayer;
  CheckValues(message, hostUs);
  CheckRate(from, message, bytes.size());
  CheckSequence(from, message);
  CheckOneShot(from, message, bytes);
  FromHost(message, ours);
  Note(message, false);
}

void Referee::CheckValues(const p::Message &message, std::uint64_t hostUs) {
  const char *name = p::NameOf(message);
  bool finite = true, inRange = true;
  std::uint64_t timeUs = 0;
  std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, p::PlayerState> || std::is_same_v<M, p::VehicleState>) {
          finite = Finite(m.position) && Finite(m.orientation) && Finite(m.velocity);
          timeUs = m.timeUs;
        } else if constexpr (std::is_same_v<M, p::PoseState>) {
          timeUs = m.timeUs;
        } else if constexpr (std::is_same_v<M, p::NpcSpawn> || std::is_same_v<M, p::NpcDeath>) {
          finite = Finite(m.position) && std::isfinite(m.yaw);
        } else if constexpr (std::is_same_v<M, p::NpcStates>) {
          timeUs = m.timeUs;
          for (const auto &entry : m.npcs)
            finite = finite && Finite(entry.position) && std::isfinite(entry.yaw);
        } else if constexpr (std::is_same_v<M, p::NpcAwareness>) {
          timeUs = m.timeUs;
        } else if constexpr (std::is_same_v<M, p::NpcShotAt>) {
          timeUs = m.timeUs;
          finite = std::isfinite(m.tbh) && std::isfinite(m.factor);
          inRange = (!finite || (m.factor > 0.f && m.factor <= 1.001f)) &&
                    (m.flags & ~p::kShotAtFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::Cue>) {
          // A shot's end, a bounce, a swing's landing: a real place; a
          // charge: one of its steps, how charged (0..1), a hand (v16).
          finite = Finite(m.at);
          inRange = m.kind == p::CueKind::Charge
                        ? m.arg <= p::kChargeMax &&
                              (!finite ||
                               (m.at.x >= 0.f && m.at.x <= 1.001f &&
                                (m.at.y == p::kChargeRightHand || m.at.y == p::kChargeLeftHand)))
                        : (m.arg & ~p::kTraceFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::HitTaken>) {
          // A hit a V took: on its body (within reach of its position),
          // a real way; a known kind of attacker, named when there is
          // one; known flags.
          finite = Finite(m.offset) && Finite(m.direction);
          inRange = p::Fits(m) && (!finite || (std::abs(m.offset.x) <= p::kHitTakenReach &&
                                               std::abs(m.offset.y) <= p::kHitTakenReach &&
                                               std::abs(m.offset.z) <= p::kHitTakenReach));
        } else if constexpr (std::is_same_v<M, p::NpcHit>) {
          finite = std::isfinite(m.damage) && Finite(m.position) && Finite(m.direction);
          inRange = !finite || m.damage >= 0.f;
        } else if constexpr (std::is_same_v<M, p::NpcStrike>) {
          finite = std::isfinite(m.damage) && std::isfinite(m.poise) &&
                   std::isfinite(m.minHealth) && Finite(m.position) && Finite(m.direction);
          // Known flags only (kStrikeLeftHand, v16: the off hand's hit is a
          // strike like the right's, never another message).
          inRange =
              (!finite || (m.damage >= 0.f && m.poise >= 0.f)) && (m.flags & ~p::kStrikeFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::PlayerHit> || std::is_same_v<M, p::AreaHit>) {
          finite = std::isfinite(m.physical) && std::isfinite(m.thermal) &&
                   std::isfinite(m.electric) && std::isfinite(m.chemical) && Finite(m.position) &&
                   Finite(m.direction);
          inRange = !finite || (m.physical >= 0.f && m.thermal >= 0.f && m.electric >= 0.f &&
                                m.chemical >= 0.f);
        } else if constexpr (std::is_same_v<M, p::NpcHealth>) {
          finite = std::isfinite(m.health);
          inRange = (!finite || (m.health >= 0.f && m.health <= 1.001f)) &&
                    (m.flags & ~p::kNpcHealthFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::WeakSpotHit>) {
          // A share of a named weak spot's health, never nothing.
          finite = std::isfinite(m.drain);
          inRange = m.record != 0 && (!finite || (m.drain > 0.f && m.drain <= 100.001f));
        } else if constexpr (std::is_same_v<M, p::NpcPoise>) {
          finite = std::isfinite(m.poise);
          inRange = !finite || (m.poise >= 0.f && m.poise <= 1.001f);
        } else if constexpr (std::is_same_v<M, p::Regroup>) {
          finite = Finite(m.position) && std::isfinite(m.yaw);
        } else if constexpr (std::is_same_v<M, p::WorldItem>) {
          finite = Finite(m.position);
        } else if constexpr (std::is_same_v<M, p::Elsewhere>) {
          finite = Finite(m.anchor) && std::isfinite(m.yaw);
        } else if constexpr (std::is_same_v<M, p::TimeEffect>) {
          finite = std::isfinite(m.scale);
        } else if constexpr (std::is_same_v<M, p::Stealth>) {
          finite = std::isfinite(m.visibility);
        } else if constexpr (std::is_same_v<M, p::NpcWound> || std::is_same_v<M, p::ShotHit>) {
          finite = Finite(m.position);
        } else if constexpr (std::is_same_v<M, p::PhoneAction>) {
          inRange = m.action >= p::kPhoneAnswer && m.action <= p::kPhoneDial;
        } else if constexpr (std::is_same_v<M, p::StoryState>) {
          inRange = m.tier <= 5;
        } else if constexpr (std::is_same_v<M, p::StoryGates>) {
          inRange = (m.flags & ~p::kGateFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::WorldPrefabs>) {
          inRange = m.prefabs.size() <= p::kMaxWorldPrefabs;
        } else if constexpr (std::is_same_v<M, p::OpenWorldDone>) {
          inRange = m.markers.size() <= p::kMaxOpenWorldDone;
        } else if constexpr (std::is_same_v<M, p::SceneState>) {
          finite = std::isfinite(m.rate) && Finite(m.anchor) && std::isfinite(m.yaw) &&
                   std::isfinite(m.radius);
          inRange = !finite || (m.rate >= 0.f && m.rate <= p::kMaxSceneRate && m.tier <= 5);
        } else if constexpr (std::is_same_v<M, p::TrafficHandoverState>) {
          finite = Finite(m.position) && Finite(m.orientation) && Finite(m.velocity) &&
                   std::isfinite(m.health);
          inRange = !finite || (m.health >= 0.f && m.health <= 1.001f);
        } else if constexpr (std::is_same_v<M, p::NpcHandoverState>) {
          finite = Finite(m.position) && std::isfinite(m.yaw) && std::isfinite(m.health) &&
                   std::isfinite(m.poise);
          inRange = !finite || (m.health <= 1.001f && m.poise <= 1.001f &&
                                m.effects.size() <= p::kMaxNpcEffects);
        } else if constexpr (std::is_same_v<M, p::SceneCast>) {
          inRange = m.actors.size() <= p::kMaxSceneActors;
        } else if constexpr (std::is_same_v<M, p::ActorPose> ||
                             std::is_same_v<M, p::TrafficStates> || std::is_same_v<M, p::RigPose> ||
                             std::is_same_v<M, p::RigTracks>) {
          timeUs = m.timeUs;
        } else if constexpr (std::is_same_v<M, p::SceneCamera>) {
          timeUs = m.timeUs;
          finite = Finite(m.position) && std::isfinite(m.fov);
          inRange = !finite || (m.fov > 0.f && m.fov <= 180.f);
        } else if constexpr (std::is_same_v<M, p::SceneProp>) {
          inRange = m.prop != 0 && (m.flags & ~p::kPropFlags) == 0;
        } else if constexpr (std::is_same_v<M, p::ScenePlace>) {
          finite = Finite(m.position) && Finite(m.orientation);
          inRange = !finite || (m.prop != 0 && (m.flags & ~p::kPropGone) == 0);
        } else if constexpr (std::is_same_v<M, p::LinkDecline>) {
          // A known reason, to a player.
          inRange = m.reason >= p::kLinkDeclineOther && m.reason <= p::kLinkDeclineMissed &&
                    m.to != p::kNoPlayer;
        } else if constexpr (std::is_same_v<M, p::TrafficStatus>) {
          finite = std::isfinite(m.health);
          inRange = !finite || (m.health >= 0.f && m.health <= 1.001f);
        } else if constexpr (std::is_same_v<M, p::TrafficHit>) {
          finite = std::isfinite(m.damage) && Finite(m.position) && Finite(m.impulse);
          inRange = !finite || m.damage >= 0.f;
        } else if constexpr (std::is_same_v<M, p::TrafficHack>) {
          // A named vehicle, a hack this version knows, a length.
          finite = std::isfinite(m.seconds);
          inRange = m.id != 0 && m.hack <= p::CarHack::Accelerate && (!finite || m.seconds >= 0.f);
        } else if constexpr (std::is_same_v<M, p::CarPaint>) {
          // Flags this version knows, a template or colours, a hue in degrees.
          finite = std::isfinite(m.lightsHue);
          inRange = !finite || p::Fits(m);
        } else if constexpr (std::is_same_v<M, p::OwnCar>) {
          // Flags this version knows; no car is nothing else.
          finite = Finite(m.position) && Finite(m.orientation);
          inRange = !finite || p::Fits(m);
        } else if constexpr (std::is_same_v<M, p::Degraded>) {
          // Ids as the registry writes them, sorted, each once; known flags.
          inRange = p::Fits(m);
        } else if constexpr (std::is_same_v<M, p::TrafficLights>) {
          // Lights by their entity, colours the game has, a bounded list.
          inRange = p::Fits(m);
        } else if constexpr (std::is_same_v<M, p::PropStates>) {
          // Placed props, a bounded list, places that are places (props.md).
          timeUs = m.timeUs;
          inRange = p::Fits(m);
          for (const auto &prop : m.props)
            finite = finite && Finite(prop.position);
        } else if constexpr (std::is_same_v<M, p::PropRest>) {
          finite = Finite(m.position) && Finite(m.orientation);
          inRange = m.entity != 0;
        } else if constexpr (std::is_same_v<M, p::PropPush>) {
          finite = Finite(m.position) && Finite(m.velocity);
          timeUs = m.timeUs;
          inRange = m.entity != 0;
        } else if constexpr (std::is_same_v<M, p::TrafficSpawn>) {
          finite = Finite(m.position) && Finite(m.orientation);
        } else if constexpr (std::is_same_v<M, p::Crime>) {
          finite = std::isfinite(m.amount) && Finite(m.position);
          inRange = !finite || (m.amount >= 0.f && m.amount <= 1.001f);
        } else if constexpr (std::is_same_v<M, p::Heat>) {
          inRange = m.stage <= p::kMaxHeat;
        } else if constexpr (std::is_same_v<M, p::NpcReaction>) {
          finite = std::isfinite(m.angle) && Finite(m.way);
          inRange = m.type <= p::kReactionMax;
        } else if constexpr (std::is_same_v<M, p::NpcVoice>) {
          // A context, a delay within its bound, flags this version knows.
          inRange = p::Fits(m);
        } else if constexpr (std::is_same_v<M, p::CorpsePose>) {
          // An NPC named, a place and a heading, its ragdoll's bodies (1 to
          // kMaxRagdollBodies), each turned by a unit quaternion.
          finite = Finite(m.position) && std::isfinite(m.yaw);
          inRange = m.npc != 0 && !m.bodies.empty() && m.bodies.size() <= p::kMaxRagdollBodies &&
                    std::ranges::all_of(m.bodies, [](const p::RagdollBody &body) {
                      double sum = 0;
                      for (const auto c : body.rotation)
                        sum += double(c) * c;
                      const double unit = 32767.0 * 32767.0;
                      return sum > 0.9 * unit && sum < 1.1 * unit;
                    });
        } else if constexpr (std::is_same_v<M, p::Waypoint>) {
          // A place on the map, of a kind this version knows (Objective: v9).
          finite = Finite(m.position);
          inRange = m.kind <= p::WaypointKind::Objective;
        } else if constexpr (std::is_same_v<M, p::Fracture>) {
          finite = Finite(m.at);
        } else if constexpr (std::is_same_v<M, p::DeviceFire>) {
          finite = Finite(m.at);
          timeUs = m.timeUs;
          // A device is named; a shot is stamped (the host clock).
          inRange = m.entity != 0 && (m.kind != p::DeviceFireKind::Shot || m.timeUs != 0);
        } else if constexpr (std::is_same_v<M, p::TurretHands>) {
          finite = Finite(m.at);
          inRange = m.entity != 0;
        } else if constexpr (std::is_same_v<M, p::TurretBusy>) {
          // A named turret, held by a player.
          inRange = m.entity != 0 && m.holder != p::kNoPlayer;
        } else if constexpr (std::is_same_v<M, p::ThrowPath>) {
          // Nothing thrown flies faster than a launcher's round.
          finite = Finite(m.position) && Finite(m.velocity);
          inRange = !finite || (std::abs(m.velocity.x) <= 400.f &&
                                std::abs(m.velocity.y) <= 400.f && std::abs(m.velocity.z) <= 400.f);
        } else if constexpr (std::is_same_v<M, p::Noise>) {
          finite = std::isfinite(m.radius);
          inRange = !finite || (m.radius >= 0.f && m.stim < 65);
        } else if constexpr (std::is_same_v<M, p::Race>) {
          finite = Finite(m.at) && std::isfinite(m.heading) && std::isfinite(m.metres) &&
                   std::ranges::all_of(m.points, [](const p::Vec3 &v) { return Finite(v); });
          // The race's description, its seats, order and rewards: its starter's word.
          const bool starters = m.kind == p::RaceKind::Info || m.kind == p::RaceKind::Seat ||
                                m.kind == p::RaceKind::Standings || m.kind == p::RaceKind::Reward ||
                                m.kind == p::RaceKind::Route;
          inRange = !finite || (m.metres >= 0.f && m.starter != p::kNoPlayer &&
                                (!starters || m.player == m.starter) &&
                                (m.kind != p::RaceKind::Join || m.player != m.starter) &&
                                // A route is whole or none.
                                (m.kind != p::RaceKind::Route || m.points.empty() ||
                                 m.points.size() == m.checkpoints));
        }
      },
      message);
  if (!finite)
    Add("value.not-finite", std::format("{} carries a number that is not finite", name));
  if (!inRange)
    Add("value.range", std::format("{} carries a value out of its range", name));
  // One clock, the host's: a state stamped far from it was made on another.
  if (timeUs != 0 && hostUs != 0) {
    const auto skew = timeUs > hostUs ? timeUs - hostUs : hostUs - timeUs;
    auto &rate = m_rates[{message.index() + 1000, p::kNoPlayer}];
    if (skew > m_options.limits.clockSkewUs &&
        (rate.flaggedUs == 0 || m_nowUs - rate.flaggedUs > kAgainUs)) {
      rate.flaggedUs = m_nowUs;
      Add("value.clock",
          std::format("{} is stamped {:.1f} s {} the host clock", name,
                      static_cast<double>(skew) / 1e6, timeUs > hostUs ? "ahead of" : "behind"));
    }
  }
}

void Referee::CheckRate(p::PlayerId from, const p::Message &message, std::size_t bytes) {
  const auto &limits = m_options.limits;
  const char *name = p::NameOf(message);
  const double window = static_cast<double>(limits.rateWindowUs) / 1e6;
  const auto count = [&](Rate &rate) {
    rate.times.push_back(m_nowUs);
    while (m_nowUs - rate.times.front() > limits.rateWindowUs)
      rate.times.pop_front();
    return static_cast<double>(rate.times.size());
  };
  const auto due = [&](Rate &rate) {
    if (rate.flaggedUs != 0 && m_nowUs - rate.flaggedUs < kAgainUs)
      return false;
    rate.flaggedUs = m_nowUs;
    return true;
  };

  float budget = 0.f;
  if (std::holds_alternative<p::PlayerState>(message))
    budget = limits.playerStateHz;
  else if (std::holds_alternative<p::PoseState>(message))
    budget = limits.poseHz;
  else if (std::holds_alternative<p::VehicleState>(message))
    budget = limits.vehicleHz;
  else if (std::holds_alternative<p::Shot>(message))
    budget = limits.shotHz;
  else if (std::holds_alternative<p::NpcStates>(message) ||
           std::holds_alternative<p::NpcMotion>(message))
    budget = limits.npcStatesHz;
  else if (std::holds_alternative<p::NpcAwareness>(message))
    budget = limits.awarenessHz;
  else if (std::holds_alternative<p::Digest>(message))
    budget = limits.digestHz;
  else if (std::holds_alternative<p::Ping>(message))
    budget = limits.pingHz;
  else if (std::holds_alternative<p::Noise>(message))
    budget = limits.noiseHz;
  else if (std::holds_alternative<p::ThrowPath>(message))
    budget = limits.throwPathHz;
  else if (std::holds_alternative<p::TurretHands>(message))
    budget = limits.turretHandsHz;
  else if (std::holds_alternative<p::NpcVoice>(message))
    budget = limits.npcVoiceHz;
  else if (std::holds_alternative<p::PropStates>(message))
    budget = limits.propStatesHz;
  if (budget > 0.f) {
    // A relayed player's states count against that player.
    auto copy = message;
    const auto *sender = SenderOf(copy);
    auto &rate = m_rates[{message.index(), sender ? *sender : from}];
    if (const auto n = count(rate); n > budget * window && due(rate))
      Add("rate.over-budget", std::format("{} at {:.0f} a second from player {} (budget {:.0f})",
                                          name, n / window, sender ? *sender : from, budget));
  }
  if (p::IsReliable(message)) {
    auto &rate = m_events[from];
    if (const auto n = count(rate); n > limits.eventsHz * window && due(rate))
      Hint("rate.events",
           std::format("{:.0f} reliable messages a second from player {}", n / window, from));
  }
  m_bytes.emplace_back(m_nowUs, bytes);
  m_bytesInWindow += bytes;
  while (m_nowUs - m_bytes.front().first > limits.rateWindowUs) {
    m_bytesInWindow -= m_bytes.front().second;
    m_bytes.pop_front();
  }
  if (static_cast<double>(m_bytesInWindow) > limits.bytesPerSecond * window &&
      (m_bytesFlaggedUs == 0 || m_nowUs - m_bytesFlaggedUs > kAgainUs)) {
    m_bytesFlaggedUs = m_nowUs;
    Hint("rate.bytes", std::format("{:.0f} KB a second received",
                                   static_cast<double>(m_bytesInWindow) / window / 1e3));
  }

  if (std::holds_alternative<p::PlayerState>(message) && bytes > limits.playerStateBytes)
    Add("size.over-budget",
        std::format("PlayerState is {} bytes (budget {})", bytes, limits.playerStateBytes));
  if (!p::IsReliable(message) && bytes > limits.stateBytes)
    Hint("size.state-fragmented",
         std::format("{} is {} bytes: over a datagram, ENet sends it in reliable fragments", name,
                     bytes));
}

void Referee::CheckSequence(p::PlayerId from, const p::Message &message) {
  std::optional<std::pair<p::PlayerId, std::uint32_t>> seq;
  if (const auto *state = std::get_if<p::PlayerState>(&message))
    seq = {state->player, state->seq};
  else if (const auto *pose = std::get_if<p::PoseState>(&message))
    seq = {pose->player, pose->seq};
  else if (const auto *vehicle = std::get_if<p::VehicleState>(&message))
    seq = {vehicle->player, vehicle->seq};
  else if (const auto *shot = std::get_if<p::Shot>(&message))
    seq = {shot->player, shot->seq};
  else if (const auto *cue = std::get_if<p::Cue>(&message))
    seq = {cue->player, cue->seq};
  else if (const auto *cars = std::get_if<p::TrafficStates>(&message))
    seq = {from, cars->seq};
  // Each owner numbers its NPCs' states (a host its own and those it relays).
  else if (const auto *motion = std::get_if<p::NpcMotion>(&message))
    seq = {from, motion->seq};
  else if (const auto *npcs = std::get_if<p::NpcStates>(&message))
    seq = {from, npcs->seq};
  else if (const auto *awareness = std::get_if<p::NpcAwareness>(&message))
    seq = {from, awareness->seq};
  if (!seq)
    return;
  const TypeAndPlayer key{message.index(), seq->first};
  const auto last = m_seqs.find(key);
  // Sequenced by ENet: an older one never arrives, so a repeat was sent twice.
  if (last != m_seqs.end() && NotNewer(seq->second, last->second))
    Add("oneshot.seq-repeated",
        std::format("{} {} of player {} after {}: sent twice, or numbered backwards",
                    p::NameOf(message), seq->second, seq->first, last->second));
  m_seqs[key] = seq->second;
}

void Referee::CheckOneShot(p::PlayerId from, const p::Message &message,
                           const std::vector<std::byte> &bytes) {
  if (!IsAny<p::NpcStrike, p::NpcHit, p::AreaHit, p::PlayerHit, p::ShotHit, p::NpcWound>(message))
    return;
  auto &last = m_lastOneShot[{message.index(), from}];
  if (last.first == bytes && m_nowUs - last.second < 30'000)
    Hint("oneshot.duplicate", std::format("{} from player {} twice, byte for byte, within 30 ms",
                                          p::NameOf(message), from));
  last = {bytes, m_nowUs};
}

void Referee::FromGuest(p::PlayerId from, const p::Message &message) {
  const auto &limits = m_options.limits;
  const char *name = p::NameOf(message);
  const auto life = m_lives.find(from);
  const bool lying = life != m_lives.end() && life->second.life != p::Life::Up &&
                     m_nowUs - life->second.sinceUs > limits.downGraceUs;
  const auto whileDown = [&](const char *what) {
    if (lying)
      Add("authority.acting-while-down",
          std::format("player {} {} while its V is {}", from, what,
                      life->second.life == p::Life::Down ? "down" : "out"));
  };

  if (const auto *hit = std::get_if<p::NpcHit>(&message)) {
    NeedNpc(hit->npc, name);
  } else if (const auto *state = std::get_if<p::StoryState>(&message);
             state && (state->flags & (p::kStoryMoment | p::kStoryWatch))) {
    // The story's moments are the host's V's (story.md, "Spectating").
    Add("authority.host-only",
        std::format("player {} says its V is in a scripted moment: only the host's is", from));
  } else if (const auto *state = std::get_if<p::StoryState>(&message);
             state && (state->flags & p::kStoryNoFastTravel)) {
    // The quest's locks are the host's (a guest's own story is frozen).
    Add("authority.host-only",
        std::format("player {} says its quest blocks fast travel: only the host's quest counts",
                    from));
  } else if (const auto *breach = std::get_if<p::Breach>(&message)) {
    // Granting a breach and passing it on is the host's word.
    if (breach->flags & p::kBreachApply)
      Add("authority.host-only",
          std::format("player {} sent a granted breach: only the host passes breaches on", from));
    if (breach->npc != 0)
      NeedNpc(breach->npc, name);
    if (breach->entity == 0 && breach->npc == 0)
      Add("value.range", std::format("player {} breached nothing (no network, no NPC)", from));
    whileDown("breaches a network");
  } else if (const auto *strike = std::get_if<p::NpcStrike>(&message)) {
    NeedNpc(strike->npc, name);
  } else if (const auto *effect = std::get_if<p::NpcEffect>(&message)) {
    NeedNpc(effect->npc, name);
  } else if (const auto *shotHit = std::get_if<p::ShotHit>(&message)) {
    NeedNpc(shotHit->npc, name);
  } else if (const auto *hack = std::get_if<p::NpcHack>(&message)) {
    NeedNpc(hack->npc, name);
    whileDown("uploads a quickhack");
  } else if (const auto *action = std::get_if<p::NpcAction>(&message)) {
    NeedNpc(action->npc, name);
    if (action->kind == p::NpcActionKind::Grab || action->kind == p::NpcActionKind::PickUp ||
        action->kind == p::NpcActionKind::Finisher)
      whileDown("grabs an NPC");
  } else if (const auto *shot = std::get_if<p::Shot>(&message)) {
    if (Fires(*shot))
      whileDown("fires");
  } else if (const auto *revive = std::get_if<p::Revive>(&message)) {
    NeedPlayer(revive->target, name);
    if (revive->target == from) {
      Add("authority.revive-self", std::format("player {} revives its own V", from));
    } else if (revive->step == p::ReviveStep::Start) {
      whileDown("revives someone");
      const auto target = m_lives.find(revive->target);
      const bool down = target != m_lives.end() && target->second.life == p::Life::Down;
      const auto was = m_wasDownUs.find(revive->target);
      if (!down && (was == m_wasDownUs.end() || m_nowUs - was->second > 5'000'000) &&
          m_everPlayers.contains(revive->target))
        Add("authority.revive-not-down",
            std::format("player {} revives player {}, whose V is not down", from, revive->target));
    }
  } else if (const auto *carHack = std::get_if<p::VehicleHack>(&message)) {
    NeedPlayer(carHack->driver, name);
  } else if (const auto *area = std::get_if<p::AreaHit>(&message)) {
    NeedPlayer(area->player, name);
  } else if (const auto *offer = std::get_if<p::Regroup>(&message)) {
    NeedPlayer(offer->to, name);
  } else if (const auto *decline = std::get_if<p::LinkDecline>(&message)) {
    NeedPlayer(decline->to, name);
  } else if (const auto *answer = std::get_if<p::RegroupAnswer>(&message)) {
    NeedPlayer(answer->to, name);
    if (answer->to == m_self && answer->offer > m_offers)
      Add("ids.unknown-offer", std::format("player {} answers our regroup offer {}, and we made {}",
                                           from, answer->offer, m_offers));
  } else if (const auto *seat = std::get_if<p::Seat>(&message)) {
    NeedPlayer(seat->owner, name);
  } else if (const auto *item = std::get_if<p::WorldItem>(&message)) {
    if (!m_dropTokens.insert({from, item->token}).second)
      Add("oneshot.drop-twice",
          std::format("player {} dropped its bag {} twice", from, item->token));
  } else if (const auto *taken = std::get_if<p::WorldItemTaken>(&message)) {
    const auto known = m_items.find(taken->id);
    if (known == m_items.end() || taken->stack >= known->second.size())
      Add("ids.unknown-item", std::format("player {} claims stack {} of world item {}, which "
                                          "never lay in the world",
                                          from, taken->stack, taken->id));
  } else if (const auto *resync = std::get_if<p::ResyncRequest>(&message)) {
    for (std::uint8_t bit = 0; bit < 4; ++bit) {
      if (!(resync->domains & (1u << bit)))
        continue;
      auto &times = m_resyncs[{from, bit}];
      times.push_back(m_nowUs);
      while (m_nowUs - times.front() > 3 * limits.digestGraceUs / 2)
        times.pop_front();
      // The same domain again and again: the resync does not heal it.
      if (times.size() == 4 && !m_options.faulty)
        Add("digest.resync-loop",
            std::format("player {} asked for domain {} four times in {:.0f} s: our resync does "
                        "not bring it back in step",
                        from, 1u << bit, static_cast<double>(m_nowUs - times.front()) / 1e6));
    }
    auto &all = m_resyncs[{from, 0xFF}];
    all.push_back(m_nowUs);
    while (m_nowUs - all.front() > 10'000'000)
      all.pop_front();
    if (all.size() == limits.resyncsPer10s + 1)
      Add("rate.over-budget",
          std::format("ResyncRequest {} times in 10 s from player {}", all.size(), from));
  } else if (const auto *epoch = std::get_if<p::Epoch>(&message)) {
    if (epoch->seenHost > m_ourEpoch)
      Add("epoch.unknown", std::format("player {} has seen our epoch {}, and we are in {}", from,
                                       epoch->seenHost, m_ourEpoch));
    if (epoch->seenHost >= m_ourEpoch)
      m_epochOwed.erase(from);
    auto &last = m_epochs[from];
    if (epoch->epoch < last && epoch->reason != p::kEpochJoin)
      Add("epoch.backwards",
          std::format("player {}'s epoch went from {} back to {}", from, last, epoch->epoch));
    last = epoch->epoch;
  }
}

void Referee::FromHost(const p::Message &message, const p::Digest *ours) {
  const auto &limits = m_options.limits;
  const char *name = p::NameOf(message);
  const auto notOurs = [&](p::PlayerId subject) {
    if (subject == m_self)
      Add("authority.own-state-back",
          std::format("the host sent us our own {}: our game is its owner", name));
    else
      NeedPlayer(subject, name);
  };
  const auto forUs = [&](p::PlayerId target) {
    if (target != m_self)
      Add("authority.misdelivered",
          std::format("{} for player {} came to us (player {})", name, target, m_self));
  };

  std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, p::Breach>) {
          // Another player's breach, granted, for the NPCs our game runs:
          // of its network, or the one it breached (ours).
          notOurs(m.player);
          if (!(m.flags & p::kBreachApply))
            Add("breach.ungranted",
                std::format("the host passed on player {}'s breach without granting it", m.player));
          if (m.entity == 0 && m.npc != 0 && OwnerOf(m.npc) != m_self)
            Add("authority.not-owner",
                std::format("the host passed us a breach of npc {}, which player {} runs", m.npc,
                            OwnerOf(m.npc)));
          if (m.entity != 0 && !(m.flags & p::kBreachFailed)) {
            const auto [first, fresh] = m_breached.emplace(std::pair{m.entity, m.cls}, m.player);
            if (!fresh && first->second != m.player)
              Add("oneshot.breach-twice",
                  std::format("network {:x} breached by player {} and by player {}", m.entity,
                              first->second, m.player));
          }
        } else if constexpr (std::is_same_v<M, p::NpcDeath>) {
          // The ledger: one death per placed NPC, the host's order for it
          // never changing (a resync says the same again), never more than
          // the cap in play. (By whom: maybe a player that left before we
          // joined.)
          const auto [known, fresh] = m_deaths.emplace(m.entity, m.order);
          if (!fresh && known->second != m.order)
            Add("ledger.changed",
                std::format("the death of placed npc {:x} was number {} and is now number {}",
                            m.entity, known->second, m.order));
          if (fresh && m_deaths.size() > 2 * p::kMaxDeaths)
            Add("ledger.unbounded", std::format("the host told us {} deaths: its ledger keeps {}",
                                                m_deaths.size(), p::kMaxDeaths));
        } else if constexpr (std::is_same_v<M, p::BreachResult>) {
          const std::tuple key{m.entity, m.cls, m.npc};
          auto asked = m_breachesAsked.find(key);
          if (asked == m_breachesAsked.end() || asked->second == 0) {
            Add("reply.unasked", std::format("the host answered a breach of {:x} (npc {}) that we "
                                             "never made",
                                             m.entity, m.npc));
            return;
          }
          if (--asked->second == 0)
            m_breachesAsked.erase(asked);
          if (m.verdict == p::BreachVerdict::Granted && m.by != m_self)
            Add("authority.not-yours",
                std::format("the host granted our breach in the name of player {}", m.by));
          if (m.verdict == p::BreachVerdict::Granted && m.entity != 0) {
            const auto [first, fresh] = m_breached.emplace(std::pair{m.entity, m.cls}, m_self);
            if (!fresh && first->second != m_self)
              Add("oneshot.breach-twice",
                  std::format("network {:x} breached by player {} and by us", m.entity,
                              first->second));
          }
        } else if constexpr (std::is_same_v<M, p::Fracture>) {
          // What broke in a player's game, stamped by the host. Not checked:
          // our own comes back for the session's record, and a late joiner
          // hears breaks by players who left before it came (devices.md,
          // "Glass and breakable scenery").
        } else if constexpr (std::is_same_v<M, p::TurretHands>) {
          // A turret in another player's hands; never our own back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::HitTaken>) {
          // A hit another player's V took; never our own back. (Its NPC
          // may overtake its spawn on the State channel: not checked.)
          notOurs(m.player);
          if (m.by == p::HitBy::Player && m.attacker != m_self)
            NeedPlayer(m.attacker, name);
        } else if constexpr (std::is_same_v<M, p::TurretBusy>) {
          // Our claim refused: another player holds a turret we claimed.
          notOurs(m.holder);
          if (!m_turretsClaimed.contains(m.entity))
            Add("reply.unasked",
                std::format("the host refused us turret {:x}, which we never claimed", m.entity));
        } else if constexpr (std::is_same_v<M, p::DeviceFire>) {
          // The host's turret's target: a player of the session (an NPC's
          // shot may overtake its spawn on the State channel: not checked).
          if (m.target.kind == p::TargetKind::Player)
            NeedPlayer(static_cast<p::PlayerId>(m.target.id), name);
        } else if constexpr (std::is_same_v<M, p::Roster>) {
          std::set<p::PlayerId> ids;
          for (const auto &player : m.players)
            if (!ids.insert(player.id).second)
              Add("roster.broken", std::format("player {} is twice in the roster", player.id));
          if (!ids.contains(m_self) || !ids.contains(m_hostPlayer)) {
            std::string listed;
            for (const auto id : ids)
              listed += std::format(" {}", id);
            Add("roster.broken", std::format("a roster of players{} without us (player {}) or the "
                                             "host (player {})",
                                             listed, m_self, m_hostPlayer));
          }
          if (ids.size() > 1 + kMaxGuests)
            Add("roster.broken", std::format("{} players in the roster", ids.size()));
        } else if constexpr (std::is_same_v<M, p::PlayerState> || std::is_same_v<M, p::PoseState> ||
                             std::is_same_v<M, p::Shot>) {
          // State channel: it may overtake the roster that brings its player.
          if (m.player == m_self)
            notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::VehicleState>) {
          if (m.player == m_self)
            notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::Appearance> || std::is_same_v<M, p::Held> ||
                             std::is_same_v<M, p::VehicleRadio> || std::is_same_v<M, p::Stealth> ||
                             std::is_same_v<M, p::Transit> || std::is_same_v<M, p::ChoiceCursor>) {
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::StoryGates>) {
          // Every player's own word, passed on: never ours sent back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::WorldPrefabs>) {
          // The host's world: what its own quests loaded.
          if (m.player != m_hostPlayer)
            Add("authority.host-only",
                std::format("world prefabs said to be player {}'s: only the host's world has them",
                            m.player));
        } else if constexpr (std::is_same_v<M, p::OpenWorldDone>) {
          // The host's world: the activities its own game finished.
          if (m.player != m_hostPlayer)
            Add("authority.host-only",
                std::format("open-world activities said to be player {}'s: only the host's world "
                            "decides them",
                            m.player));
        } else if constexpr (std::is_same_v<M, p::StoryState>) {
          notOurs(m.player);
          // A moment is the host's V's; a spectator is a guest watching it.
          if ((m.flags & (p::kStoryMoment | p::kStoryWatch)) && m.player != m_hostPlayer)
            Add("authority.host-only",
                std::format("player {} is said to be in a scripted moment: only the host's V is",
                            m.player));
          if ((m.flags & p::kStorySpectating) && m.player == m_hostPlayer)
            Add("value.range", "the host is said to spectate its own moment");
          if ((m.flags & p::kStoryWatch) && !(m.flags & p::kStoryMoment))
            Add("value.range",
                std::format("player {} is said to be in a moment to watch, not in a moment",
                            m.player));
          // Fast travel blocked by the quest: the host's game's word.
          if ((m.flags & p::kStoryNoFastTravel) && m.player != m_hostPlayer)
            Add("authority.host-only",
                std::format("player {}'s quest is said to block fast travel: only the host's does",
                            m.player));
        } else if constexpr (std::is_same_v<M, p::Seat>) {
          notOurs(m.player);
          NeedPlayer(m.owner, name);
        } else if constexpr (std::is_same_v<M, p::Vitals>) {
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::Pause>) {
          // "No longer" is said to everyone when a player comes back.
          if (m.paused || m.player != m_self)
            notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::TimeEffect>) {
          if (m.abilities != 0 || m.player != m_self)
            notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::Elsewhere>) {
          if (m.place != p::Place::City || m.player != m_self)
            notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::PlayerHit>) {
          forUs(m.player);
          if (m.npc != 0) {
            NeedNpc(m.npc, name);
            NeedShown(m.npc, name);
          }
        } else if constexpr (std::is_same_v<M, p::QuestInteract>) {
          // The host's verdict on our own question, to us alone.
          forUs(m.player);
        } else if constexpr (std::is_same_v<M, p::PlayerHack>) {
          forUs(m.player);
          NeedNpc(m.npc, name);
          NeedShown(m.npc, name);
        } else if constexpr (std::is_same_v<M, p::NpcCue>) {
          forUs(m.player);
          NeedNpc(m.npc, name);
          NeedShown(m.npc, name);
        } else if constexpr (std::is_same_v<M, p::NpcShotAt>) {
          forUs(m.player);
          NeedNpc(m.npc, name);
          NeedShown(m.npc, name);
          if (!m_shotsAt.insert({m.npc, m.timeUs}).second)
            Add("oneshot.duplicate",
                std::format("npc {}'s shot at us of {} came twice", m.npc, m.timeUs));
        } else if constexpr (std::is_same_v<M, p::AreaHit>) {
          forUs(m.player);
          NeedPlayer(m.from, name);
        } else if constexpr (std::is_same_v<M, p::VehicleHack>) {
          forUs(m.driver);
          NeedPlayer(m.player, name);
        } else if constexpr (std::is_same_v<M, p::TrafficHack>) {
          // Another player's quickhack on a shared vehicle; never ours back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::CarPaint>) {
          // Another player's car's paint; never ours back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::OwnCar>) {
          // Another player's summoned car; never ours back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::Degraded>) {
          // What another player's game runs without; never ours back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::TrafficLights>) {
          // Another game's traffic lights; never ours back.
          notOurs(m.owner);
        } else if constexpr (std::is_same_v<M, p::PropStates>) {
          // Another game's moving props; never ours back.
          notOurs(m.owner);
        } else if constexpr (std::is_same_v<M, p::PropRest>) {
          // A rest of the session's record: our own comes back for it, and
          // a late joiner hears rests by players who left (props.md).
        } else if constexpr (std::is_same_v<M, p::PropPush>) {
          // Another V's push on a prop our game runs; never ours back.
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::Revive>) {
          forUs(m.target);
          NeedPlayer(m.player, name);
        } else if constexpr (std::is_same_v<M, p::Regroup>) {
          if (m.to != p::kNoPlayer)
            forUs(m.to);
          notOurs(m.from);
        } else if constexpr (std::is_same_v<M, p::LinkDecline>) {
          forUs(m.to);
          notOurs(m.from);
        } else if constexpr (std::is_same_v<M, p::RegroupAnswer>) {
          forUs(m.to);
          notOurs(m.from);
          if (m.to == m_self && m.offer > m_offers)
            Add("ids.unknown-offer",
                std::format("player {} answers our regroup offer {}, and we made {}", m.from,
                            m.offer, m_offers));
        } else if constexpr (std::is_same_v<M, p::NpcSpawn>) {
          const auto known = m_npcs.find(m.npc);
          if (known == m_npcs.end())
            return;
          const bool same =
              known->second.record == m.record && known->second.appearance == m.appearance;
          if (!same)
            Add("ids.reused", std::format("npc {} announced again as another NPC: ids are never "
                                          "reused in a session",
                                          m.npc));
          // Out of our view and back (a link broke and formed again: regions)
          // is the same NPC; one that died or was disposed of never returns.
          else if (!known->second.live && known->second.dead)
            Add("ids.reused",
                std::format("npc {} announced again after it {}", m.npc,
                            known->second.goneReason == p::kGoneDisposed ? "was disposed of"
                                                                         : "died"));
        } else if constexpr (std::is_same_v<M, p::NpcGone>) {
          NeedPlayer(m.killer, name);
          const auto known = m_npcs.find(m.npc);
          if (known != m_npcs.end() && known->second.dead && m.reason == p::kGoneDead)
            Add("oneshot.death-twice", std::format("npc {} died twice", m.npc));
        } else if constexpr (std::is_same_v<M, p::NpcStates>) {
          for (const auto &entry : m.npcs) {
            const auto known = m_npcs.find(entry.npc);
            if (known == m_npcs.end() || known->second.live || known->second.lateFlagged ||
                known->second.goneReason == p::kGoneKnockedOut ||
                m_nowUs - known->second.goneUs < limits.goneGraceUs)
              continue;
            known->second.lateFlagged = true;
            Hint("authority.state-after-gone",
                 std::format("npc {} is still in the host's states {:.0f} s after it went",
                             entry.npc, static_cast<double>(m_nowUs - known->second.goneUs) / 1e6));
          }
        } else if constexpr (std::is_same_v<M, p::NpcHealth>) {
          NeedNpc(m.npc, name);
          auto &health = m_health.try_emplace(m.npc, 1.f).first->second;
          auto &[hits, drops] = m_hitsAndDrops[m.npc];
          if (m.health < health - 1e-4f && ++drops == hits + 3 &&
              m_nowUs - m_othersFoughtUs > 5'000'000)
            Hint("oneshot.hit-twice",
                 std::format("npc {} lost health {} times for {} hits of ours, nobody else "
                             "fighting",
                             m.npc, drops, hits));
          health = m.health;
        } else if constexpr (std::is_same_v<M, p::NpcPoise>) {
          NeedNpc(m.npc, name);
        } else if constexpr (std::is_same_v<M, p::SceneCast>) {
          for (const auto &actor : m.actors)
            NeedNpc(actor.npc, name);
        } else if constexpr (std::is_same_v<M, p::SceneLine>) {
          if (m.speaker != 0)
            NeedNpc(m.speaker, name);
          NeedPlayer(m.speakerPlayer, name);
        } else if constexpr (std::is_same_v<M, p::SceneProp>) {
          // Held by an actor the host shares, or by a V of the session.
          if (m.holder != 0)
            NeedNpc(m.holder, name);
          NeedPlayer(m.holderPlayer, name);
        } else if constexpr (std::is_same_v<M, p::NpcHandoverState>) {
          forUs(m.to);
          NeedNpc(m.npc, name);
        } else if constexpr (std::is_same_v<M, p::RegionHold>) {
          notOurs(m.player);
        } else if constexpr (std::is_same_v<M, p::NpcAnims>) {
          for (const auto &anim : m.npcs) {
            NeedNpc(anim.npc, name);
            NeedShown(anim.npc, name);
          }
        } else if constexpr (std::is_same_v<M, p::NpcVoice>) {
          // A line of an NPC we were told of (its spawn goes first, on the
          // same channel), and only while it is shown to us.
          NeedNpc(m.npc, name);
          NeedShown(m.npc, name);
        } else if constexpr (std::is_same_v<M, p::CorpsePose>) {
          // The body of an NPC we were told of, and never one of ours back.
          NeedNpc(m.npc, name);
          if (OwnerOf(m.npc) == m_self)
            Add("authority.own-state-back",
                std::format("the host sent us npc {}'s body, which our game runs", m.npc));
        } else if constexpr (std::is_same_v<M, p::NpcSeat>) {
          if (m.owner != p::kNoPlayer)
            NeedNpc(m.npc, name);
          // A shared world vehicle's seat names the vehicle (kSeatInTraffic):
          // its id carries its area owner.
          NeedPlayer(p::IsTrafficSeat(m.owner) ? p::TrafficOwner(p::TrafficOfSeat(m.owner))
                                               : m.owner,
                     name);
        } else if constexpr (std::is_same_v<M, p::NpcHold>) {
          NeedPlayer(m.player, name);
          if (m.player != p::kNoPlayer && !m_npcs.contains(m.npc))
            Hint("ids.unknown-npc",
                 std::format("NpcHold names npc {}, which the host never announced to us", m.npc));
        } else if constexpr (std::is_same_v<M, p::NpcStatus> || std::is_same_v<M, p::NpcWound>) {
          NeedPlayer(m.player, name);
        } else if constexpr (std::is_same_v<M, p::NpcHackState>) {
          NeedPlayer(m.player, name);
          if (m.step == p::HackStep::Started) {
            NeedNpc(m.npc, name);
            if (m.player == m_self && !m_uploads.contains({m.npc, m.action}))
              Add("reply.unasked",
                  std::format("the host started an upload of ours on npc {} that we never asked",
                              m.npc));
          }
        } else if constexpr (std::is_same_v<M, p::NpcActionReply>) {
          const std::pair key{m.npc, m.kind};
          auto asked = m_asked.find(key);
          if (asked == m_asked.end() || asked->second == 0) {
            Add("reply.unasked",
                std::format("the host answered an action {} on npc {} that we never asked",
                            static_cast<int>(m.kind), m.npc));
            return;
          }
          --asked->second;
          if (m_askedOnDead.erase(key) > 0 && m.accepted)
            Add("authority.accepted-on-dead",
                std::format("the host accepted action {} on npc {}, which it had said dead",
                            static_cast<int>(m.kind), m.npc));
        } else if constexpr (std::is_same_v<M, p::Pong>) {
          if (std::ranges::find(m_pings, m.pingSentUs) == m_pings.end())
            Add("reply.unasked", "a Pong for a Ping we never sent");
        } else if constexpr (std::is_same_v<M, p::WorldItem>) {
          NeedPlayer(m.dropper, name);
          if (m.id == 0)
            Add("ids.unknown-item", "a world item with no id from the host");
        } else if constexpr (std::is_same_v<M, p::WorldItemTaken>) {
          NeedPlayer(m.taker, name);
          const bool claimed = m_claimed.erase({m.id, m.stack}) > 0;
          if (m.taker == m_self && !claimed)
            Add("reply.unasked", std::format("the host gave us stack {} of world item {}, which "
                                             "we never claimed",
                                             m.stack, m.id));
          const auto known = m_items.find(m.id);
          if (known == m_items.end()) {
            // A refusal names nobody when the bag is gone; a grant needs a bag.
            if (m.taker != p::kNoPlayer)
              Add("ids.unknown-item",
                  std::format("player {} took a stack of world item {}, which the host never "
                              "announced",
                              m.taker, m.id));
          } else if (m.stack < known->second.size()) {
            const auto before = known->second[m.stack];
            if (before != p::kNoPlayer && before != kTakenByUnknown && m.taker != p::kNoPlayer &&
                before != m.taker)
              Add("oneshot.item-taken-twice",
                  std::format("stack {} of world item {} went to player {} and to player {}",
                              m.stack, m.id, before, m.taker));
          }
        } else if constexpr (std::is_same_v<M, p::SharedItem>) {
          notOurs(m.player);
          if (m.kind == p::kSharedOnCorpse)
            return;
          if (m.item.seed != 0 &&
              !m_sharedItems.insert({m.player, m.item.record, m.item.seed}).second)
            Add("oneshot.item-twice",
                std::format("player {} got the one item {:#x} twice", m.player, m.item.record));
        } else if constexpr (std::is_same_v<M, p::QuestReward>) {
          if (m.quest != 0 && !m_rewards.insert({m.reward, m.quest}).second)
            Add("oneshot.reward-twice",
                std::format("reward {:#x} of quest {} paid twice", m.reward, m.quest));
        } else if constexpr (std::is_same_v<M, p::GameOver>) {
          if (!m_gameOvers.insert(m.round).second)
            Add("oneshot.gameover-twice", std::format("loss {} declared twice", m.round));
        } else if constexpr (std::is_same_v<M, p::PhoneCall>) {
          NeedPlayer(m.answeredBy, name);
        } else if constexpr (std::is_same_v<M, p::Subtitle>) {
          NeedPlayer(m.speakerPlayer, name);
        } else if constexpr (std::is_same_v<M, p::StoryEffect>) {
          NeedPlayer(m.player, name);
        } else if constexpr (std::is_same_v<M, p::Epoch>) {
          NeedPlayer(m.player, name);
          if (m.player == m_hostPlayer) {
            if (m.epoch < m_hostEpoch)
              Add("epoch.backwards",
                  std::format("the host's epoch went from {} back to {}", m_hostEpoch, m.epoch));
            m_hostEpoch = std::max(m_hostEpoch, m.epoch);
          } else {
            auto &last = m_epochs[m.player];
            if (m.epoch < last && m.reason != p::kEpochJoin)
              Add("epoch.backwards", std::format("player {}'s epoch went from {} back to {}",
                                                 m.player, last, m.epoch));
            last = m.epoch;
          }
        } else if constexpr (std::is_same_v<M, p::Digest>) {
          m_lastDigestUs = m_nowUs;
          m_digestSilent = false;
          if (m.epoch < m_hostEpoch)
            Add("epoch.backwards", std::format("a digest of epoch {} after the host's epoch {}",
                                               m.epoch, m_hostEpoch));
          m_hostEpoch = std::max(m_hostEpoch, m.epoch);
          if (!ours)
            return;
          const auto differs = consistency::Differences(m, *ours);
          constexpr const char *domains[] = {"world", "npcs", "story", "players"};
          for (std::size_t bit = 0; bit < 4; ++bit) {
            auto &since = m_differs[bit];
            if (!(differs & (1u << bit))) {
              since = {};
              continue;
            }
            if (since.sinceUs == 0)
              since.sinceUs = m_nowUs;
            if (!since.flagged && !m_options.faulty &&
                m_nowUs - since.sinceUs > limits.digestGraceUs) {
              since.flagged = true;
              Add("digest.diverged",
                  std::format("our {} differ from the host's for {:.0f} s: longer than a resync "
                              "takes",
                              domains[bit], static_cast<double>(m_nowUs - since.sinceUs) / 1e6));
            }
          }
        }
      },
      message);
}

void Referee::Moved(p::PlayerId subject) {
  const auto seat = m_seats.find(subject);
  if (seat == m_seats.end())
    return;
  const auto &[where, sinceUs] = seat->second;
  if (where.vehicle == 0 || where.owner == p::kNoPlayer || where.owner == subject ||
      m_nowUs - sinceUs < m_options.limits.seatGraceUs)
    return;
  auto &rate = m_rates[{2000, subject}];
  if (rate.flaggedUs != 0 && m_nowUs - rate.flaggedUs < kAgainUs)
    return;
  rate.flaggedUs = m_nowUs;
  Add("authority.vehicle-not-yours",
      std::format("player {} moves a vehicle while it rides in player {}'s", subject, where.owner));
}

void Referee::Note(const p::Message &message, bool sent) {
  // The host's word on its world: what it sends (host), what we hear (guest).
  const bool hostsWord = sent == m_host;
  std::visit(
      [&](const auto &m) {
        using M = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<M, p::Roster>) {
          if (!hostsWord)
            return;
          std::set<p::PlayerId> next;
          for (const auto &player : m.players)
            next.insert(player.id);
          for (const auto player : m_players)
            if (!next.contains(player)) {
              m_leftUs[player] = m_nowUs;
              m_lives.erase(player);
              std::erase_if(m_revives,
                            [&](const auto &entry) { return entry.first.second == player; });
              m_seats.erase(player);
              m_epochOwed.erase(player);
            }
          for (const auto player : next)
            m_leftUs.erase(player);
          m_players = std::move(next);
          m_everPlayers.insert(m_players.begin(), m_players.end());
        } else if constexpr (std::is_same_v<M, p::NpcSpawn>) {
          // Whoever runs it announces it (the host, or a guest since regions).
          m_npcs[m.npc] = Npc{.record = m.record, .appearance = m.appearance, .live = true};
        } else if constexpr (std::is_same_v<M, p::NpcOwner>) {
          if (!(m.flags & p::kNpcPinned) || m.owner != OwnerOf(m.npc))
            m_owners[m.npc] = m.owner;
        } else if constexpr (std::is_same_v<M, p::NpcHandoverState>) {
          // The giver's state makes the receiver its owner (the receiver
          // learns it from this, not from an NpcOwner).
          if (m.to != p::kNoPlayer)
            m_owners[m.npc] = m.to;
          if (!sent && m.to == m_self)
            m_given.insert(m.npc);
        } else if constexpr (std::is_same_v<M, p::TrafficOwnership>) {
          m_carOwners[m.id] = m.owner;
        } else if constexpr (std::is_same_v<M, p::TrafficHandoverState>) {
          // As for an NPC: the giver's state makes the receiver its owner.
          if (m.to != p::kNoPlayer)
            m_carOwners[m.id] = m.to;
          if (!sent && m.to == m_self)
            m_carsGiven.insert(m.id);
        } else if constexpr (std::is_same_v<M, p::TrafficHandoverDone>) {
          // A receiver that cannot run it gives it up: the host's to end.
          if (sent && !m.accepted && m_carsGiven.erase(m.id) > 0)
            m_carOwners[m.id] = m_hostPlayer;
          else if (sent)
            m_carsGiven.erase(m.id);
        } else if constexpr (std::is_same_v<M, p::TrafficGone>) {
          m_carOwners.erase(m.id);
          m_carsGiven.erase(m.id);
        } else if constexpr (std::is_same_v<M, p::NpcHandoverDone>) {
          // A receiver that cannot run what it was given gives it up: the
          // host speaks for it from then on (its end for everyone). A giver
          // that refuses keeps it.
          if (sent && !m.accepted && m_given.erase(m.npc) > 0)
            m_owners[m.npc] = m_hostPlayer;
          else if (sent)
            m_given.erase(m.npc);
        } else if constexpr (std::is_same_v<M, p::NpcGone>) {
          auto &npc = m_npcs[m.npc];
          npc.live = false;
          npc.dead = npc.dead || m.reason == p::kGoneDead || m.reason == p::kGoneDisposed;
          npc.goneReason = m.reason;
          npc.goneUs = m_nowUs;
          m_holds.erase(m.npc);
        } else if constexpr (std::is_same_v<M, p::NpcSet>) {
          // A guest's record only: the host's set is what it shares with us
          // (ours stay); ours (regions) is the NPCs our game runs. A host's
          // set goes to one guest and changes nothing of the session.
          if (m_host)
            return;
          const std::set<p::NpcId> shared(m.npcs.begin(), m.npcs.end());
          for (auto &[id, npc] : m_npcs)
            if (npc.live && !shared.contains(id) && (OwnerOf(id) == m_self) == sent) {
              npc.live = false;
              npc.goneReason = p::kGoneAway;
              npc.goneUs = m_nowUs;
              m_holds.erase(id);
            }
        } else if constexpr (std::is_same_v<M, p::NpcHold>) {
          if (m.player == p::kNoPlayer) {
            m_holds.erase(m.npc);
          } else if (const auto held = m_holds.find(m.npc); held == m_holds.end() ||
                                                            held->second.player != m.player ||
                                                            held->second.how != m.how) {
            m_holds[m.npc] = {m.player, m.how, m_nowUs};
          }
        } else if constexpr (std::is_same_v<M, p::Vitals>) {
          auto &life = m_lives[m.player];
          if (life.life == p::Life::Down || m.life == p::Life::Down)
            m_wasDownUs[m.player] = m_nowUs;
          if (life.life != m.life || life.sinceUs == 0)
            life = {m.life, m_nowUs};
          // Up, or out: nobody is reviving it any more.
          if (m.life != p::Life::Down)
            std::erase_if(m_revives,
                          [&](const auto &entry) { return entry.first.second == m.player; });
        } else if constexpr (std::is_same_v<M, p::TimeEffect>) {
          if ((m.abilities & p::kTimeBubbleAbilities) == 0)
            m_time.erase(m.player);
          else
            m_time.try_emplace(m.player, Since{m_nowUs});
        } else if constexpr (std::is_same_v<M, p::Seat>) {
          m_seats[m.player] = {m, m_nowUs};
        } else if constexpr (std::is_same_v<M, p::VehicleState>) {
          if (!sent)
            Moved(m.player);
        } else if constexpr (std::is_same_v<M, p::Shot>) {
          if (!sent && Fires(m))
            m_othersFoughtUs = m_nowUs;
        } else if constexpr (std::is_same_v<M, p::Revive>) {
          const std::pair key{m.player, m.target};
          if (m.step == p::ReviveStep::Start)
            m_revives.try_emplace(key, Since{m_nowUs});
          else
            m_revives.erase(key);
        } else if constexpr (std::is_same_v<M, p::PhoneCall>) {
          if (!hostsWord)
            return;
          if (m.phase == 1 && m.answeredBy == p::kNoPlayer && !(m.flags & p::kCallRejected)) {
            if (!m_ringing)
              m_ringing = Since{m_nowUs};
          } else {
            m_ringing.reset();
          }
        } else if constexpr (std::is_same_v<M, p::WorldItem>) {
          if (!hostsWord || m.id == 0)
            return;
          auto &takers = m_items[m.id];
          takers.resize(m.stacks.size(), p::kNoPlayer);
          for (std::size_t i = 0; i < m.stacks.size(); ++i)
            if (m.stacks[i].quantity == 0 && takers[i] == p::kNoPlayer)
              takers[i] = kTakenByUnknown;
        } else if constexpr (std::is_same_v<M, p::WorldItemTaken>) {
          if (!hostsWord || m.taker == p::kNoPlayer)
            return;
          if (const auto known = m_items.find(m.id);
              known != m_items.end() && m.stack < known->second.size())
            known->second[m.stack] = m.taker;
        } else if constexpr (std::is_same_v<M, p::Regroup>) {
          if (sent && m.from == m_self)
            m_offers = std::max(m_offers, m.offer);
        }
      },
      message);
}

void Referee::Tick(std::uint64_t nowUs) {
  if (m_ended)
    return;
  // A clock read before the last message was stamped: no earlier than it.
  nowUs = std::max(nowUs, m_nowUs);
  m_nowUs = nowUs;
  const auto &limits = m_options.limits;
  const auto seconds = [&](std::uint64_t sinceUs) {
    return static_cast<double>(nowUs - sinceUs) / 1e6;
  };
  const auto gone = [&](p::PlayerId player) {
    const auto left = m_leftUs.find(player);
    return left != m_leftUs.end() && nowUs - left->second > limits.holdAfterLeaveUs;
  };

  for (auto &[npc, held] : m_holds) {
    if (held.flagged)
      continue;
    if (gone(held.player)) {
      held.flagged = true;
      Add("stuck.after-leave", std::format("npc {} is still held by player {}, who left {:.0f} s "
                                           "ago",
                                           npc, held.player, seconds(m_leftUs[held.player])));
    } else if ((held.how == p::NpcHoldKind::Takedown || held.how == p::NpcHoldKind::Finisher) &&
               nowUs - held.sinceUs > limits.takedownHoldUs) {
      held.flagged = true;
      Add("stuck.hold", std::format("npc {} has been in player {}'s takedown for {:.0f} s", npc,
                                    held.player, seconds(held.sinceUs)));
    } else if (held.how == p::NpcHoldKind::Grappled &&
               nowUs - held.sinceUs > limits.grappleHoldUs) {
      held.flagged = true;
      Hint("stuck.hold", std::format("npc {} has been grappled by player {} for {:.0f} s", npc,
                                     held.player, seconds(held.sinceUs)));
    }
  }
  for (auto &[player, since] : m_time) {
    if (since.flagged)
      continue;
    if (gone(player)) {
      since.flagged = true;
      Add("stuck.after-leave",
          std::format("player {} left and its time bubble never ended", player));
    } else if (nowUs - since.sinceUs > limits.timeEffectUs) {
      since.flagged = true;
      Add("stuck.time-effect",
          std::format("player {} has slowed time for {:.0f} s", player, seconds(since.sinceUs)));
    }
  }
  // The host calls off a revive whose reviver left (ours: our endpoint did).
  if (m_host)
    std::erase_if(m_revives, [&](const auto &entry) { return gone(entry.first.first); });
  for (auto &[who, since] : m_revives) {
    if (since.flagged)
      continue;
    if (gone(who.first)) {
      since.flagged = true;
      Add("stuck.after-leave",
          std::format("player {} left while reviving player {}, and nobody called it off",
                      who.first, who.second));
    } else if (nowUs - since.sinceUs > limits.reviveUs) {
      since.flagged = true;
      Add("stuck.revive", std::format("player {} has been reviving player {} for {:.0f} s",
                                      who.first, who.second, seconds(since.sinceUs)));
    }
  }
  for (auto &[player, life] : m_lives) {
    if (life.flagged || life.life == p::Life::Up)
      continue;
    const bool down = life.life == p::Life::Down;
    if (nowUs - life.sinceUs > (down ? limits.downUs : limits.outUs)) {
      life.flagged = true;
      Hint("stuck.life", std::format("player {} has been {} for {:.0f} s", player,
                                     down ? "down" : "out", seconds(life.sinceUs)));
    }
  }
  if (m_ringing && !m_ringing->flagged && nowUs - m_ringing->sinceUs > limits.ringingUs) {
    m_ringing->flagged = true;
    Hint("stuck.call",
         std::format("the host's call has been ringing for {:.0f} s", seconds(m_ringing->sinceUs)));
  }
  for (auto &[player, since] : m_epochOwed)
    if (!since.flagged && nowUs - since.sinceUs > limits.epochAckUs) {
      since.flagged = true;
      Add("epoch.unacknowledged",
          std::format("player {} has not said it saw our epoch {} after {:.0f} s", player,
                      m_ourEpoch, seconds(since.sinceUs)));
    }
  if (!m_host && m_welcomed && !m_digestSilent && m_lastDigestUs != 0 &&
      nowUs - m_lastDigestUs > limits.digestSilenceUs) {
    m_digestSilent = true;
    Add("rate.digest-silent",
        std::format("no digest from the host for {:.0f} s", seconds(m_lastDigestUs)));
  }
}
} // namespace tandem::sim
