#pragma once

// The co-op session on top of the transport: handshake, roster, clock sync and
// player state relay. The same code runs in the game plugin and in the mock
// player, as host or as guest.
//
// Topology: one host, up to kMaxGuests guests, star-shaped. Guests only talk to
// the host; the host relays what one guest sends to the others. Each player's
// game owns its own V (PlayerState); the host's game owns the shared world.

#include "tandem/activity.hpp"
#include "tandem/comms.hpp"
#include "tandem/consistency.hpp"
#include "tandem/devices.hpp"
#include "tandem/interact.hpp"
#include "tandem/ledger.hpp"
#include "tandem/mods.hpp"
#include "tandem/netcode.hpp"
#include "tandem/pose.hpp"
#include "tandem/protocol.hpp"
#include "tandem/regions.hpp"
#include "tandem/rigpose.hpp"
#include "tandem/scene.hpp"
#include "tandem/session.hpp"
#include "tandem/story.hpp"
#include "tandem/trade.hpp"
#include "tandem/vehicle_combat.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tandem {
// The supported session: the host and three guests (4 players).
inline constexpr std::size_t kMaxGuests = 3;
inline constexpr std::size_t kMaxPlayers = kMaxGuests + 1;
static_assert(kMaxPlayers < proto::kMaxPlayerId, "ids to spare: a freed id is not reused at once");
// The reason a guest past kMaxGuests is refused with (the scripts translate it).
inline constexpr std::string_view kSessionFull = "the session is full (4 players at most)";
static_assert(kMaxPlayers == 4, "kSessionFull names the maximum");
// Connections beyond kMaxGuests: a player coming back while its old
// connection is still open, or one to tell that the session is full.
inline constexpr std::size_t kSpareConnections = 2;

// How soon a silent connection is given up. The defaults suit play; tests
// shorten them.
struct Timing {
  // Nothing heard for this long: the player, or the host, is gone
  // (net::kDefaultTimeout). A hiccup of a second or two passes.
  std::chrono::milliseconds timeout{net::kDefaultTimeout};
  // Host: a Hello under the name of a player whose connection has been
  // silent this long replaces that connection at once, keeping the player's
  // id, puppet and seat (a game back from a crash or a dropped link). A live
  // player is heard from several times a second, so nobody takes its place.
  std::chrono::milliseconds takeover{2'000};
  // Host: how often each guest gets a digest of the shared state
  // (docs/architecture/consistency.md, Consistency). Guest: how soon the same domain may
  // be asked for again after a resync.
  std::chrono::milliseconds digest{3'000};
  std::chrono::milliseconds resyncCooldown{5'000};
  // Host: how long the others have to answer a time skip
  // (activity::kSkipAnswerUs).
  std::chrono::milliseconds skipAnswer{20'000};
};

namespace events {
// We are in a session (host: listening; guest: welcomed by the host).
struct Joined {
  proto::PlayerId you{};
  proto::PlayerId host{};
};
struct PlayerJoined {
  proto::PlayerInfo player;
};
struct PlayerLeft {
  proto::PlayerId player{};
};
// Host only: a player came back on a new connection before its old one timed
// out; same id, so its puppet and seat stay (no PlayerLeft, no PlayerJoined).
struct PlayerReturned {
  proto::PlayerInfo player;
};
struct RemoteState {
  proto::PlayerState state;
};
struct RemotePose {
  proto::PoseState pose;
};
struct RemoteAppearance {
  proto::Appearance appearance;
};
struct RemoteHeld {
  proto::Held held;
};
struct RemoteShot {
  proto::Shot shot;
};
struct RemoteSeat {
  proto::Seat seat;
};
struct RemoteVehicle {
  proto::VehicleState vehicle;
};
// The host's NPCs (guests only).
struct HostNpcSpawn {
  proto::NpcSpawn spawn;
};
struct HostNpcStates {
  proto::NpcStates states;
};
// A scene actor's body pose from the host (scene replay), for an NPC we know.
struct HostActorPose {
  proto::ActorPose pose;
};
struct HostNpcGone {
  proto::NpcGone gone;
};
struct HostNpcHealth {
  proto::NpcHealth health;
};
struct HostNpcAnims {
  proto::NpcAnims anims;
};
struct HostNpcShot {
  proto::NpcShot shot;
};
// A guest hit one of our NPCs (host only).
struct NpcHitBy {
  proto::NpcHit hit;
};
struct NpcEffectBy {
  proto::NpcEffect effect;
};
// One of the host's NPCs hit our V (guests only; never another player's hit).
struct HitByNpc {
  proto::PlayerHit hit;
};
// Quickhacks. Host: a guest started an upload on one of our NPCs.
struct NpcHackBy {
  proto::NpcHack hack;
};
// Guests: an upload on a host NPC started or ended (anyone's, ours too).
struct HostNpcHackState {
  proto::NpcHackState state;
};
// Guests: a quickhack effect came onto or left a host NPC (never our own).
struct HostNpcStatus {
  proto::NpcStatus status;
};
// Guests: a host NPC netrunner hacks our V.
struct HostPlayerHack {
  proto::PlayerHack hack;
};
// The driver's game (host or guest): someone hacked the car we drive.
struct VehicleHackOn {
  proto::VehicleHack hack;
};
// A guest's grapple, takedown, finisher or carry request (host only), and
// the host's answer to ours (guests only).
struct NpcActionBy {
  proto::NpcAction action;
};
struct NpcActionAnswered {
  proto::NpcActionReply reply;
};
// Who holds or animates one of the host's NPCs (guests only).
struct HostNpcHold {
  proto::NpcHold hold;
};
// Our half of an NPC's reaction to our V (guests only).
struct HostNpcCue {
  proto::NpcCue cue;
};
// The host's NPC fires at our V: our game simulates the shot (guests only).
struct ShotAtUs {
  proto::NpcShotAt shot;
};
// A guest's game says a shot it simulated hit its V (host only).
struct ShotHitBy {
  proto::ShotHit hit;
};
// The host's world changed (guests only).
struct HostWorld {
  proto::World world;
};
// Another player opened (paused) or closed their pause menu, or left while
// in it (paused false). Never reported for our own.
struct PauseChanged {
  proto::Pause pause;
};
// A story message (proto::IsStory) for this game. Host: from a guest,
// stamped with its player (PhoneAction only when it is the answer that
// counts, story::CallArbiter). Guest: from the host.
struct Story {
  proto::Message message;
};
// Another player's time ability started, changed or ended (abilities 0),
// or they left while it was on. Never reported for our own.
struct RemoteTimeEffect {
  proto::TimeEffect effect;
};
// A hit outcome message (proto::IsCombat) for this game. Another player's
// NpcStrike, Defended, WeakSpotHit or HackTraced on an NPC we run,
// stamped; an NPC owner's NpcWound,
// NpcPoise and NpcLevel on one we show; an AreaHit on our V; another
// player's Throw and ThrowEnd. Guest: the host's Difficulty and Rules.
struct Combat {
  proto::Message message;
};
// Trade (tandem/trade.hpp): what the host says to our seat at the table: a
// Trade or a TradeOffer of the trade we are in or asked into, or a
// TradeCommit to apply (reported once per trade, however often it comes).
struct Trade {
  proto::Message message;
};
// Another player went into another world, changed it or came back (place
// City), or left while away. Never reported for our own.
struct RemoteElsewhere {
  proto::Elsewhere state;
};
// The session is over (refused, host gone, timed out).
struct Ended {
  std::string reason;
};
// The radio of the car another player drives changed (or was sent again
// for a passenger getting in). Never reported for our own.
struct RemoteVehicleRadio {
  proto::VehicleRadio radio;
};
// Another player's stealth changed (camo, visibility, crouch).
struct RemoteStealth {
  proto::Stealth stealth;
};
// Another player's health or menu state changed (the party HUD; a fall of
// health is also their puppet's pain).
struct RemotePresence {
  proto::Presence presence;
};
// Something another player's V did at one moment (a shot's end, a melee
// swing landing): seen and heard on its puppet.
struct RemoteCue {
  proto::Cue cue;
};
// Another player's V shows other status effects now (the whole set): its
// puppet plays their visuals.
struct RemoteStatusVisuals {
  proto::StatusVisuals visuals;
};
// How one of the host's NPCs reacts to a hit (guests only): played on its proxy.
struct HostNpcReaction {
  proto::NpcReaction reaction;
};
// How far the host's NPCs are in noticing each guest (guests only).
struct HostNpcAwareness {
  proto::NpcAwareness awareness;
};
// A guest's V made a noise for our NPCs to hear (host only).
struct NoiseBy {
  proto::Noise noise;
};
// Another player offers us to come to them (Regroup), or answers ours. Only
// ever what is meant for us (to us, or to everyone).
struct RegroupOffered {
  proto::Regroup offer;
};
struct RegroupAnswered {
  proto::RegroupAnswer answer;
};
// Loot: a shared world item lies in the world (numbered by the host; ours
// too, with our token), and who picked up one of its stacks (our own
// claims included: taker is us when the host granted it).
struct WorldItemAppeared {
  proto::WorldItem item;
};
struct WorldItemTakenBy {
  proto::WorldItemTaken taken;
};
// Another player got a quest or important item (or, guests: one lies on a
// host NPC's corpse). Never reported for our own.
struct SharedItemFrom {
  proto::SharedItem item;
};
// Down and out. Another player's V went up, down or out (never our own; a
// player who leaves is forgotten without one).
struct RemoteVitals {
  proto::Vitals vitals;
};
// Someone is reviving our downed V (target is us), stamped by the host.
struct ReviveBy {
  proto::Revive revive;
};
// No V is up any more: the session is lost (host: our own arbitration).
struct SessionLost {
  proto::GameOver over;
};
// Consistency. Guest: our copy of these domains (kDomain* bits) differed
// from the host's digest twice in a row; we asked for them again (player:
// us). Host: `player` asked, and was sent them.
struct Resync {
  proto::PlayerId player{};
  std::uint8_t domains{};
};
// Another game's world was rebuilt around its V (loading screen, fast
// travel): its epoch moved on. Never reported for our own, nor for a join.
struct EpochChanged {
  proto::Epoch epoch;
};
// Another player's ride (metro, a ride, a scene vehicle) started, changed
// or ended (kind None). Never reported for our own.
struct RemoteTransit {
  proto::Transit transit;
};
// One of the host's NPCs got into, or out of, a player's vehicle (guests only).
struct HostNpcSeat {
  proto::NpcSeat seat;
};
// Devices (docs/architecture/devices.md). Guest: the host's word on a
// device, new, changed, or the answer to our own request (how: live or
// quiet); our game brings its copy to it.
struct HostDevice {
  proto::DeviceState state;
};
// Host: a guest (`state.by`) asks what a device we have no record of is;
// our game reports it (ReportDevice) if it has it.
struct DeviceAsked {
  proto::DeviceState state;
};
// Host: a guest operated a device and the request stands: our game brings
// its own copy to that state and reports what it became.
struct DeviceRequested {
  proto::DeviceUse use;
};
// Guest: the host is sending its device record again: what it does not
// name again is no longer the session's.
struct DevicesReset {};
// Breach Protocol (docs/architecture/devices.md, "Breach Protocol"). Another
// player's breach for our game to apply: the host, a network's devices and
// our NPCs in it (kBreachApply); any other game, our NPCs in it
// (kBreachApply), or the NPC breached through the quickhack, ours
// (`breach.npc`). Its daemons run from the breacher's puppet; its rewards
// were the breacher's, never ours. Never our own breach back.
struct BreachBy {
  proto::Breach breach;
};
// The host's answer to our own breach (the host's game: at once).
struct BreachAnswered {
  proto::BreachResult result;
};
// The world's objects (docs/architecture/devices.md). Something broke in
// another player's game (glass, breakable scenery): ours breaks the same
// piece. Never our own back.
struct Fractured {
  proto::Fracture fracture;
};
// A device's fire. Guest: the host's turret turned to a target, shot at it,
// or follows nobody any more; our copy shows it (a shot at our own V is
// ours to fire for real). Host: a guest's copy of a turret locked that
// guest's V (`fire.player`), or lost it (also when that guest vanished):
// our turret takes its puppet as a target, or lets it go.
struct DeviceFired {
  proto::DeviceFire fire;
};
// A turret in another player's hands, linked with us: our copy turns where
// it points and draws its fire (devices.md, "A turret in a player's hands").
struct TurretHeld {
  proto::TurretHands hands;
};
// Regions (docs/architecture/regions.md). The players our region is linked
// with changed: we see their NPCs, they see ours.
struct RegionsChanged {
  std::vector<proto::PlayerId> linked;
  // Whose world's quests are played where we are (Endpoint::RegionLeader):
  // us when we are apart, or when we hold our region.
  proto::PlayerId leader{};
};
// The host asks us to hand one of our NPCs over to `to`: answer with
// Endpoint::GiveNpc (its state as we leave it) or RefuseNpc.
struct NpcGive {
  proto::NpcId npc{};
  proto::PlayerId to{};
};
// An NPC we showed as a proxy is ours to run from now: answer with
// Endpoint::AdoptNpc.
struct NpcTake {
  proto::NpcHandoverState state;
};
// An NPC changed owner (never reported for one we take: NpcTake). `was` us:
// it is no longer ours (owner kNoPlayer: it left the session's record, e.g.
// after our world was rebuilt): our game stops running it as shared.
struct NpcOwnerChanged {
  proto::NpcId npc{};
  proto::PlayerId owner{};
  proto::PlayerId was{};
};
// World vehicles (docs/architecture/traffic.md). One the area's owner shares
// appeared, moved, changed (health, wreck, siren) or is no longer shared.
// TrafficLeft with reason taken is for every game, the owner's too: that
// player got in, and it is that player's car from now on.
struct TrafficSpawned {
  proto::TrafficSpawn spawn;
};
struct TrafficMoved {
  proto::TrafficStates states;
};
struct TrafficLeft {
  proto::TrafficGone gone;
};
struct TrafficChanged {
  proto::TrafficStatus status;
};
// Another player's game hit one of the vehicles we own (stamped by the host).
struct TrafficHitBy {
  proto::TrafficHit hit;
};
// Hand-over of a shared vehicle (traffic.md, "Who owns an area"). Give: the
// host asks us to give one of ours to `to` (answer: Endpoint::GiveTraffic,
// or AnswerTraffic(id, false)). Take: we run it from now on: wake its proxy
// up with this state (answer: AnswerTraffic). OwnerChanged: whoever runs it
// changed (`was` us: our body is a proxy from now on; `owner` kNoPlayer: no
// longer shared, our body is a world car again).
struct TrafficGive {
  proto::TrafficId id{};
  proto::PlayerId to{};
};
struct TrafficTake {
  proto::TrafficHandoverState state;
};
struct TrafficOwnerChanged {
  proto::TrafficId id{};
  proto::PlayerId owner{};
  proto::PlayerId was{};
};
// Police (docs/architecture/police.md). Another player's wanted level
// changed (stage 0: no longer wanted, or it left). Never our own.
struct RemoteHeat {
  proto::Heat heat;
};
// The owner of the area our V is in saw our puppet commit a crime: for our
// own prevention system.
struct CrimeReported {
  proto::Crime crime;
};
// What another player's game has installed (host: a guest's; guest: the
// host's), with what differs from ours in words for our player
// (compat::Differences; empty: nothing known differs). Never blocks.
struct RemoteInstall {
  proto::PlayerId player{};
  std::string name;
  proto::Install install;
  std::vector<std::string> differences;
};
// Host only: a game was refused for its protocol version, or because it
// or ours lacks Phantom Liberty, with the words for our player (the guest
// got its own with the refusal).
struct JoinRefused {
  std::string name;
  std::string notice;
};
// Other mods (docs/architecture/mod-compat.md). Another player's mods
// against ours (host: a guest's; guest: the host's), once each side said.
struct RemoteMods {
  proto::PlayerId player{};
  std::string name;
  mods::Comparison comparison;
};
// The names of another player's mods we asked for (AskModNames).
struct ModNamesFrom {
  proto::PlayerId player{};
  std::string name;
  std::vector<proto::ModEntry> mods;
};
// Another player's game shows substitutes for some of our records.
struct SubstitutesFrom {
  proto::Substitutes substitutes;
  std::string name; // the seer's
};
// Another player started, changed or ended what it is busy with (kind None),
// or left while busy (docs/architecture/activities.md). Never our own.
struct RemoteActivity {
  proto::Activity state;
};
// Another player asks to skip time: ours to answer (AnswerTimeSkip).
struct TimeSkipAsked {
  proto::TimeSkip skip;
};
// A player answered our request (a yes is not the end: the others may still
// be thinking).
struct TimeSkipVoted {
  proto::TimeSkipVote vote;
};
// A request ended (ours, or one we were asked about; ours refused at once):
// Applied means the host's clock is at `toSeconds`, so every game
// fast-forwards its own V and a guest sets its clock without waiting for
// the next World.
struct TimeSkipEnded {
  proto::TimeSkipResult result;
};
// Comms (docs/architecture/comms.md). Another player's ping (never our own);
// a tag or reveal of a player linked with us (only then); another player's
// link consent or waypoint, when it changes (never our own).
struct MarkFrom {
  proto::Mark mark;
};
struct TagFrom {
  proto::Tag tag;
};
struct RemoteLink {
  proto::Link link;
};
struct RemoteWaypoint {
  proto::Waypoint waypoint;
};
// A player did not take our link request, and why (only ever ours).
struct LinkDeclined {
  proto::LinkDecline decline;
};
// Vehicle combat (docs/architecture/vehicle-combat.md). How a vehicle
// another game owns is damaged now (never one of ours).
struct RemoteVehicleDamage {
  proto::VehicleDamage damage;
};
// Another player's game hit a vehicle we own (stamped by the host).
struct VehicleHitBy {
  proto::VehicleHit hit;
};
// Another player left a vehicle the hard way (bail, thrown, dead...).
struct RemoteVehicleExit {
  proto::VehicleExit exit;
};
// Another player's weapons in a vehicle changed (mode None: no longer, or it left).
struct RemoteVehicleCombat {
  proto::VehicleCombat combat;
};
// Another player fired a vehicle's own weapon.
struct RemoteMountedFire {
  proto::MountedFire fire;
};
// Who holds a vehicle's gunner seat changed (gunner kNoPlayer: free). For
// every game, the owner's and the gunner's too.
struct GunnerChanged {
  proto::Gunner gunner;
};
// Where the gunner of a vehicle aims (never our own aim).
struct RemoteGunnerAim {
  proto::GunnerAim aim;
};
// Another player's car says it pushed ours (BumpLedger decides).
struct VehicleBumpBy {
  proto::VehicleBump bump;
};
// Area owner: a player's car ran into one of our NPCs.
struct NpcRamBy {
  proto::NpcRam ram;
};
// One of the area owner's vehicles chases a player, or no longer does.
struct ChaseChanged {
  proto::Chase chase;
};
// Guest: the session shows us under another name than ours (another
// player has it: "V (2)"), from the roster, once and again if it changes.
struct NamedAs {
  std::string asked;
  std::string shown;
};
} // namespace events

// Who this game is in the session, and what it has installed (the mock
// player and the tests: the build alone).
struct Identity {
  std::string name;
  proto::Body body{};
  proto::Install install{};
  // Our other mods, names included (mods::Scan): said to the others as
  // hashes and categories, names only when asked. Only a game that read its
  // folder says them (tellsMods): the mock player and the tests do not.
  std::vector<proto::ModEntry> mods;
  bool tellsMods{};
  // The game has Phantom Liberty installed and enabled (the mock player
  // and the tests: yes). A host without it takes nobody; a guest without
  // it is refused (compat::kExpansion*).
  bool expansion{true};
};

// Where a guest finds its host: directly, through a relay, or directly
// first and through the relay when that fails.
struct JoinTarget {
  // The host's address ("": none, through the relay only).
  std::string address;
  std::uint16_t port{proto::kDefaultPort};
  // A relay and the session's join code or secret (tandem/relay.hpp). With
  // an address too, the relay is tried when the host has not answered
  // directly (NAT) within half the timeout; the switch is not reported.
  std::optional<net::RelayOptions> relay;
};

// What Update() reports. Code uses SessionEvent (below).
using SessionEventVariant = std::variant<
    events::Joined, events::PlayerJoined, events::PlayerLeft, events::RemoteState,
    events::RemotePose, events::RemoteAppearance, events::RemoteHeld, events::RemoteShot,
    events::RemoteSeat, events::RemoteVehicle, events::HostWorld, events::HostNpcSpawn,
    events::HostNpcStates, events::HostActorPose, events::HostNpcGone, events::HostNpcHealth,
    events::NpcHitBy, events::NpcEffectBy, events::PauseChanged, events::Ended,
    events::PlayerReturned, events::RemoteVehicleRadio, events::HitByNpc, events::RemoteStealth,
    events::HostNpcAwareness, events::NoiseBy, events::NpcHackBy, events::HostNpcHackState,
    events::HostNpcStatus, events::HostPlayerHack, events::VehicleHackOn, events::Story,
    events::RemoteTimeEffect, events::NpcActionBy, events::NpcActionAnswered, events::HostNpcHold,
    events::HostNpcCue, events::RegroupOffered, events::RegroupAnswered, events::WorldItemAppeared,
    events::WorldItemTakenBy, events::SharedItemFrom, events::RemoteVitals, events::ReviveBy,
    events::SessionLost, events::HostNpcAnims, events::HostNpcShot, events::Combat, events::Resync,
    events::EpochChanged, events::RemoteElsewhere, events::RemoteTransit, events::HostNpcSeat,
    events::ShotAtUs, events::ShotHitBy, events::HostDevice, events::DeviceAsked,
    events::DeviceRequested, events::DevicesReset, events::RegionsChanged, events::NpcGive,
    events::NpcTake, events::NpcOwnerChanged, events::TrafficSpawned, events::TrafficMoved,
    events::TrafficLeft, events::TrafficChanged, events::TrafficHitBy, events::RemoteHeat,
    events::CrimeReported, events::RemotePresence, events::RemoteCue, events::HostNpcReaction,
    events::RemoteInstall, events::JoinRefused, events::RemoteActivity, events::TimeSkipAsked,
    events::TimeSkipVoted, events::TimeSkipEnded, events::Trade, events::MarkFrom, events::TagFrom,
    events::RemoteLink, events::RemoteWaypoint, events::RemoteVehicleDamage, events::VehicleHitBy,
    events::RemoteVehicleExit, events::RemoteVehicleCombat, events::RemoteMountedFire,
    events::GunnerChanged, events::RemoteGunnerAim, events::VehicleBumpBy, events::NpcRamBy,
    events::ChaseChanged, events::NamedAs, events::BreachBy, events::BreachAnswered,
    events::RemoteStatusVisuals, events::RemoteMods, events::ModNamesFrom, events::SubstitutesFrom,
    events::TrafficGive, events::TrafficTake, events::TrafficOwnerChanged, events::Fractured,
    events::DeviceFired, events::TurretHeld, events::LinkDeclined>;
// Its copies, moves and conversions compiled in session_event.cpp only, as
// proto::Message's (tandem/closed_variant.hpp).
using SessionEvent = ClosedVariant<SessionEventVariant>;

class Endpoint {
public:
  // With `relay`, guests can also join through it, by the join code the
  // relay gives (Relay()->code).
  static std::unique_ptr<Endpoint> Host(Identity identity, std::uint16_t port, std::string &error,
                                        Timing timing = {},
                                        std::optional<net::RelayOptions> relay = std::nullopt);
  static std::unique_ptr<Endpoint> Join(Identity identity, const JoinTarget &target,
                                        std::string &error, Timing timing = {});
  static std::unique_ptr<Endpoint> Join(Identity identity, const std::string &host,
                                        std::uint16_t port, std::string &error, Timing timing = {});

  // Services the network; call every frame or tick. Returns what happened.
  std::vector<SessionEvent> Update(std::chrono::milliseconds wait = std::chrono::milliseconds{0});

  // Our own V. `timeUs` is filled with the host clock.
  void SendLocalState(proto::PlayerState state);
  // Our own V's body pose (tandem/pose.hpp). `timeUs` is filled with the host clock.
  void SendLocalPose(proto::PoseState pose);
  // Our own V's look. Kept: sent now if we are in the session, else as soon
  // as we are; the host also hands it to players who join later.
  void SetLocalAppearance(proto::Appearance appearance);
  // What we hold (kept and handed to late joiners like the look), and shots.
  void SetLocalHeld(proto::Held held);
  void SendLocalShot(proto::Shot shot);
  // What the others' party HUD shows of our V (kept and handed to late
  // joiners), and one-off cues of what it does (presence.hpp).
  void SetLocalPresence(proto::Presence presence);
  void SendLocalCue(proto::Cue cue);
  const std::map<proto::PlayerId, proto::Presence> &Presences() const { return m_presences; }
  // The status effects that show on our V (presence::StatusSet makes the
  // set; a repeat of the last is not sent). Kept and handed to late joiners;
  // the others' latest, ours too, by player.
  void SetLocalStatusVisuals(std::vector<std::uint64_t> records);
  const std::map<proto::PlayerId, proto::StatusVisuals> &StatusVisualsOf() const {
    return m_statusVisuals;
  }
  // Where we sit (kept and handed to late joiners), and the car we drive.
  void SetLocalSeat(proto::Seat seat);
  void SendLocalVehicle(proto::VehicleState vehicle);
  // The radio of the car we drive: sent now (each call, even unchanged: a
  // passenger getting in is sent it again), kept and handed to late joiners.
  void SetLocalVehicleRadio(proto::VehicleRadio radio);
  // Host only: the world every guest follows (kept for late joiners).
  void SetWorld(proto::World world);
  // The NPCs our game runs (docs/architecture/regions.md): each call is the
  // owner's word, dropped when the NPC is not ours. The host relays a guest's
  // to the players linked with it and keeps the record for late joiners.
  void SendNpcSpawn(const proto::NpcSpawn &spawn);
  void SendNpcStates(proto::NpcStates states);
  // Host: one scene actor's body pose (State channel; numbered with the NPC
  // states, so the epoch fence that voids the old world's states voids these
  // too). Nothing for an NPC we do not share. Offered as often as the game
  // reads it: the scene's budget decides which go (scene::Pacer, a body near
  // a watcher first), delta coded (ActorFrame); a guest's game hears whole
  // poses.
  void SendActorPose(proto::ActorPose pose);
  // Host: one rig of an NPC we share, as its game animates it now (its
  // face: proto::kRigFace; a body that is not the humanoid one: kRigBody):
  // `pose` and the rig's `reference` pose in local space, in the rig's bone
  // order; `rigHash` names the rig (rig::RigHash). Paced with the scene's other
  // streams; what goes is the bones that moved (rig::Encoder, RigPose). A
  // guest hears the RigPose as events::Story (it is not kept).
  void SendRigPose(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                   std::span<const rig::Bone> reference, std::span<const rig::Bone> pose);
  // Host: the float tracks of that rig (a face's wrinkles, pupils, blinks):
  // `values` and the rig's `reference` values in its track order, `rigHash`
  // the hash of their names. Paced as a stream of its own, half a face's
  // weight; what goes is the tracks that moved (rig::TrackEncoder,
  // RigTracks), heard by a guest as events::Story.
  void SendRigTracks(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                     std::span<const float> reference, std::span<const float> values);
  // Host: the view of its game's camera in a cutscene (play, position,
  // orientation, fov, kCameraCut), as often as its game has one: paced
  // here (scene::Pacer), numbered and stamped, to every guest. A guest hears
  // it as events::Story (it is not kept: a view is only good now).
  void SendSceneCamera(proto::SceneCamera camera);
  // What the scene's streams may take on a link, bytes a second (tandem.ini
  // scene_kbps; scene::kSceneBytesPerSecond by default).
  void SetSceneBudget(std::uint32_t bytesPerSecond) { m_scenePacer = scene::Pacer(bytesPerSecond); }
  void SendNpcGone(const proto::NpcGone &gone);
  // Host only: an NPC's health changed (kept for late joiners until gone).
  void SendNpcHealth(const proto::NpcHealth &health);
  // Host only: NPCs whose animation state changed (each NPC's latest kept
  // for late joiners until gone), and one of their shots.
  void SendNpcAnims(const proto::NpcAnims &anims);
  void SendNpcShot(const proto::NpcShot &shot);
  // Host only: a shared NPC's hit reaction, for the guests' proxies.
  void SendNpcReaction(const proto::NpcReaction &reaction);
  // Guest only: we hit one of the host's NPCs.
  void SendNpcHit(const proto::NpcHit &hit);
  void SendNpcEffect(const proto::NpcEffect &effect);
  // Host only: one of our NPCs hit `hit.player`'s V (its puppet here). Sent
  // to that player alone; false when it is not a guest in the session.
  bool SendPlayerHit(const proto::PlayerHit &hit);
  // Quickhacks (docs/architecture/combat.md). Guest only: we started uploading on a
  // host NPC. Host only: uploads and quickhack effects on our NPCs, to
  // everyone; an NPC netrunner's hack on a guest's V, to that guest only.
  void SendNpcHack(const proto::NpcHack &hack);
  void SendNpcHackState(const proto::NpcHackState &state);
  void SendNpcStatus(const proto::NpcStatus &status);
  void SendPlayerHack(const proto::PlayerHack &hack);
  // We hacked the car `hack.driver` drives: to its driver's game, through
  // the host (which stamps the hacker).
  void SendVehicleHack(proto::VehicleHack hack);
  // Guest only: we grab, take down, finish or carry one of the host's NPCs.
  void SendNpcAction(const proto::NpcAction &action);
  // Host only: the answer to `player`'s NpcAction, to that player alone.
  void SendNpcActionReply(proto::PlayerId player, const proto::NpcActionReply &reply);
  // Other mods: the names of the mods `player` has and we do not (answered
  // as events::ModNamesFrom); that our game shows substitutes for some of
  // `substitutes.owner`'s records (through the host, to that player only).
  void AskModNames(proto::PlayerId player);
  void SendSubstitutes(proto::Substitutes substitutes);
  // Host only: who holds an NPC now (kept for late joiners until released,
  // gone, or the holder leaves).
  void SendNpcHold(const proto::NpcHold &hold);
  // Host only: an NPC's reaction for one player's game (cue.player).
  void SendNpcCue(const proto::NpcCue &cue);
  // Host only: one of its NPCs fires at a guest's V (to that guest only;
  // `timeUs` is filled with the host clock). False: not a guest here.
  bool SendNpcShotAt(proto::NpcShotAt shot);
  // Guest only: a shot our game simulated hit our V.
  void SendShotHit(const proto::ShotHit &hit);
  // The NPCs held now, as this game knows them (host: the session's record).
  const std::map<proto::NpcId, proto::NpcHold> &NpcHolds() const { return m_npcHolds; }
  // Our pause menu is open (paused) or closed. Kept: sent now if we are in
  // the session, else as soon as we are.
  void SetLocalPaused(bool paused);
  // Story (milestone 6, proto::IsStory). Host: to every guest (to: one
  // guest), kept for late joiners (story::Record). Guest: to the host, which
  // relays ChoiceCursor and StoryState. Anything else is dropped.
  void SendStory(proto::Message message, proto::PlayerId to = proto::kNoPlayer);
  // Hit outcomes (proto::IsCombat). NpcStrike, Defended, WeakSpotHit and
  // HackTraced to the NPC's owner (a guest's through the host). An NPC's owner (the host, or a
  // guest for its own NPCs: regions.md): NpcWound, NpcPoise and NpcLevel to the games that show it
  // (poise and level kept for late joiners until the NPC goes). Host: Difficulty, Rules to every
  // guest (kept for late joiners). Either: AreaHit to its victim's game, through the host, only
  // while the session's rules allow friendly fire; Throw and ThrowEnd to every other game, through
  // the host. Levels travel here too (tandem/progress.hpp): PlayerLevel is
  // our V's, kept and said to the host as soon as we are in the session (the
  // host keeps everyone's and tells all the session's level when their mean
  // changes: SessionLevel, never a game's to say). NpcScale is an owner's word on an NPC it runs
  // (tandem/scaling.hpp; one whose spawn has not reached us yet is said right after it), to
  // everyone who sees that NPC, kept for late joiners until it goes. CarLend too: ours, kept and
  // said as soon as we are in; the host stamps who, keeps everyone's (late joiners, resyncs) and
  // tells all. NpcVendor: an owner's word that one of its NPCs is a placed vendor, to the players
  // linked with it, kept with the NPC's records until it goes (one whose spawn is still on its way
  // is said right after it); never a dead one (the host derives those from the owner's NpcGone,
  // tells everyone and keeps them for the session). Race: our word on a street race, in our name,
  // to every other game (the host stamps a guest's and relays it). False when nothing was sent (not
  // ours to say, nobody to send to, refused by the rules).
  bool SendCombat(proto::Message message);
  // The session's rules as this game knows them (the host's own; a guest's,
  // what the host said; none yet: every flag off).
  proto::Rules Rules() const { return m_rules.value_or(proto::Rules{}); }
  // Trade between two players (proto::IsTrade; tandem/trade.hpp). What our
  // seat says (a Trade or a TradeOffer; who we are is stamped here) goes to
  // the host's table; the host's own goes straight into it. A TradeCommit
  // is the table's alone to send. False when nothing was sent.
  bool SendTrade(proto::Message message);
  // Host: the table (who is trading, what was committed).
  const trade::Table &Trades() const { return m_trades; }
  // The level the host scales its NPCs to (none: not said yet), and the
  // host's level for each NPC it shares.
  const std::optional<proto::SessionLevel> &SessionLevel() const { return m_sessionLevel; }
  // Host: tandem.ini `level_gap` (progress::SessionLevelOf; 0, the default
  // here: the plain mean), taken at the next change of anyone's level.
  void SetLevelGap(std::uint8_t gap) { m_levelGap = gap; }
  const std::map<proto::NpcId, proto::NpcLevel> &NpcLevels() const { return m_npcLevels; }
  // How many players each NPC we know of is scaled for, as its owner said.
  const std::map<proto::NpcId, proto::NpcScale> &NpcScales() const { return m_npcScale; }
  // Who may drive each player's cars, as they said (a guest's own is not
  // kept here; the host's is).
  const std::map<proto::PlayerId, proto::CarLend> &CarLends() const { return m_lends; }
  // The shared NPCs that are placed vendors (their entities), and the
  // entities whose vendor died in the session (docs/architecture/progression.md).
  const std::map<proto::NpcId, proto::NpcVendor> &NpcVendors() const { return m_npcVendors; }
  const std::map<std::uint64_t, proto::NpcVendor> &DeadVendors() const { return m_deadVendors; }
  // Placed NPCs and the death ledger (docs/architecture/regions.md §14).
  // NpcPlaced goes through SendCombat: an owner's word that one of its NPCs
  // is placed in the world (its entity's static id), to the players linked
  // with it, kept with the NPC's records until it goes (one whose spawn is
  // still on its way is said right after it). When our game then reports
  // that NPC dead or knocked out (SendNpcGone), its death goes to the host,
  // which records it and tells everyone, linked or not, and every later
  // joiner: reported as events::Combat{NpcDeath}, to our own game too.
  // NpcDeath is never a game's to say.
  const std::map<proto::NpcId, proto::NpcPlaced> &PlacedNpcs() const { return m_npcPlaced; }
  const ledger::Deaths &Deaths() const { return m_deaths; }
  // The players in their pause menu, as this game knows them (us included).
  const std::set<proto::PlayerId> &Paused() const { return m_paused; }
  // Our V's time ability (Sandevistan...). Kept: sent now if we are in the
  // session, else as soon as we are; the host hands everyone's to late joiners.
  void SetLocalTimeEffect(proto::TimeEffect effect);
  // Every active time ability this game knows of (us included), by player.
  const std::map<proto::PlayerId, proto::TimeEffect> &TimeEffects() const { return m_timeEffects; }
  // Our V in another world (tandem/elsewhere.hpp). Kept: sent now if we are
  // in the session, else as soon as we are; the host hands everyone's to
  // late joiners.
  void SetLocalElsewhere(proto::Elsewhere state);
  // Every player not in the city, as this game knows them (us included).
  const std::map<proto::PlayerId, proto::Elsewhere> &Elsewheres() const { return m_elsewheres; }
  // What our V is busy with (tandem/activity.hpp). Kept: sent now if we are
  // in the session, else as soon as we are; the host hands everyone's to
  // late joiners.
  void SetLocalActivity(proto::Activity state);
  // Every busy player, as this game knows them (us included).
  const std::map<proto::PlayerId, proto::Activity> &Activities() const { return m_activities; }
  // Time skips (tandem/activity.hpp): ask to move the shared clock to
  // `toSeconds` (our clock is `fromSeconds`). Returns the request's number
  // (0: not in a session); how it ends comes as TimeSkipEnded. The host's
  // story moving its clock is SkipReason::Quest: said to everyone, never
  // asked.
  std::uint32_t AskTimeSkip(proto::SkipReason reason, std::int32_t fromSeconds,
                            std::int32_t toSeconds);
  // Our answer to another player's request (TimeSkipAsked); `why` is
  // proto::kSkip*, for a no.
  void AnswerTimeSkip(proto::PlayerId asker, std::uint32_t request, bool accepted,
                      std::uint8_t why = proto::kSkipNoReason);
  // Takes our open request back.
  void WithdrawTimeSkip();
  // Host: the open request, if any.
  const std::optional<proto::TimeSkip> &OpenTimeSkip() const { return m_skips.Open(); }
  // How hard our V is to see (kept: sent now if we are in the session, else
  // as soon as we are; the host hands everyone's to late joiners).
  void SetLocalStealth(proto::Stealth stealth);
  // Host only: how far its NPCs are in noticing each guest.
  void SendNpcAwareness(proto::NpcAwareness awareness);
  // Guest only: a noise our V made, for the host's NPCs.
  void SendNoise(const proto::Noise &noise);
  // Our V's ride (kind None: none). Kept: sent now if we are in the session,
  // else as soon as we are; the host hands everyone's to late joiners.
  void SetLocalTransit(proto::Transit transit);
  // Every ride this game knows of, by player (host: the session's record).
  const std::map<proto::PlayerId, proto::Transit> &Transits() const { return m_transits; }
  // Host only: one of our NPCs got into (owner set) or out of a player's
  // vehicle; kept while it sits, for late joiners.
  void SendNpcSeat(const proto::NpcSeat &seat);
  // Who runs the world around a player (its traffic and police too):
  // AreaOwner and OwnsArea, under Regions below.
  // World vehicles (docs/architecture/traffic.md, endpoint_traffic.cpp). A
  // vehicle is run by one game, as an NPC is: the area owner that announced
  // it (ids from proto::MakeTrafficId(You(), n)), until the host hands it
  // over. A new one is announced only while we own our area (OwnsArea); the
  // rest only for the vehicles we run. Spawns and statuses are kept for late
  // joiners until the vehicle is gone; a spawn's position follows the
  // states. A guest's go through the host to the players linked with it.
  void SendTrafficSpawn(const proto::TrafficSpawn &spawn);
  void SendTrafficStates(proto::TrafficStates states);
  void SendTrafficGone(const proto::TrafficGone &gone);
  void SendTrafficStatus(const proto::TrafficStatus &status);
  // We hit a vehicle another game runs (its proxy here): to its owner.
  void SendTrafficHit(const proto::TrafficHit &hit);
  // Our V gets into a shared vehicle: the first claim wins (its owner
  // decides) and comes back to every game as TrafficLeft{taken, player}.
  void ClaimTraffic(proto::TrafficId id, std::uint64_t seat);
  // Our answer to events::TrafficGive: the vehicle as we leave it (our body
  // stays here as its proxy). And to events::TrafficGive (false: we will
  // not give it) or events::TrafficTake (whether we run it now; false: it
  // goes).
  void GiveTraffic(proto::TrafficHandoverState state);
  void AnswerTraffic(proto::TrafficId id, bool accepted);
  // The world vehicles shared now (host: the session's record; guest: what
  // it was shown, and its own) and how each stands.
  const std::map<proto::TrafficId, proto::TrafficSpawn> &SharedTraffic() const { return m_traffic; }
  const std::map<proto::TrafficId, proto::TrafficStatus> &TrafficStatuses() const {
    return m_trafficStatus;
  }
  // Who runs a shared vehicle (kNoPlayer: not one the session knows), and
  // whether that is us.
  proto::PlayerId TrafficOwnerOf(proto::TrafficId id) const;
  bool OwnsTraffic(proto::TrafficId id) const;
  // The vehicles this game must show as proxies: those of the owners we are
  // linked with, never ours.
  std::map<proto::TrafficId, proto::TrafficSpawn> ProxiedTraffic() const;
  // The game that runs a vehicle (vehicle-combat.md): a player's car its
  // player's, a shared one its owner's as the session has it.
  proto::PlayerId VehicleOwner(proto::VehicleRef vehicle) const;
  // Host: vehicles handed over and inherited so far (tests, logs).
  std::uint64_t TrafficHandovers() const { return m_trafficHandoversDone; }
  std::uint64_t TrafficInherited() const { return m_trafficInherited; }
  // Our V's wanted level (stage 0: none). Kept: sent now if we are in the
  // session, else as soon as we are; the host hands everyone's to late
  // joiners. The same level and state again is not sent.
  void SetLocalHeat(proto::Heat heat);
  // Every wanted player this game knows of (us included), by player.
  const std::map<proto::PlayerId, proto::Heat> &Heats() const { return m_heat; }
  // Area owner only: `crime.player`'s puppet committed a crime here. To that
  // player's game alone; false when it is not a guest in the session (our
  // own V's crimes are our game's).
  bool SendCrime(const proto::Crime &crime);
  // Vehicle combat (docs/architecture/vehicle-combat.md). The game that
  // simulates a vehicle owns it: our car (PlayerCar(You())), or a shared
  // world vehicle of our area. How one of ours is damaged now: kept (for
  // late joiners, the digests), sent when it changed; the revision is ours
  // to number. Ignored for a vehicle we do not own.
  void SetVehicleDamage(proto::VehicleDamage damage);
  // We hit a vehicle another game owns: to its owner, through the host.
  void SendVehicleHit(proto::VehicleHit hit);
  // Our V left a vehicle the hard way (sent before its Seat says so).
  void SendVehicleExit(proto::VehicleExit exit);
  // Our V's weapons in a vehicle (mode None: none). Kept: sent now if we
  // are in the session, else as soon as we are; the host hands everyone's
  // to late joiners.
  void SetLocalVehicleCombat(proto::VehicleCombat combat);
  // One shot of a vehicle weapon our V fires.
  void SendMountedFire(proto::MountedFire fire);
  // Our V asks for (or leaves) the gunner seat of another player's
  // vehicle: the owner answers everyone with GunnerChanged.
  void AskGunner(proto::VehicleRef vehicle, std::uint64_t seat, bool leave = false);
  // Owner: the seat of one of our vehicles is free again (our game could
  // not seat the gunner, or the vehicle is gone).
  void FreeGunner(proto::VehicleRef vehicle);
  // Our aim as the gunner of a vehicle (dropped unless the seat is ours).
  void SendGunnerAim(proto::GunnerAim aim);
  // Our car pushed `bump.other`'s: to that driver's game.
  void SendVehicleBump(proto::VehicleBump bump);
  // Our car ran into one of the area owner's NPCs (its proxy here).
  void SendNpcRam(proto::NpcRam ram);
  // Area owner: one of our shared vehicles chases a player (kNoPlayer:
  // over). Kept while it lasts, for late joiners.
  void SendChase(const proto::Chase &chase);
  // As this game knows them (host: the session's record; us included).
  const std::map<proto::VehicleRef, proto::VehicleDamage> &VehicleDamages() const {
    return m_vehicleDamage;
  }
  const std::map<proto::VehicleRef, proto::Gunner> &Gunners() const { return m_gunners; }
  const std::map<proto::PlayerId, proto::VehicleCombat> &SeatCombats() const {
    return m_seatCombat;
  }
  const std::map<proto::TrafficId, proto::Chase> &Chases() const { return m_chases; }
  // Offer `offer.to` (kNoPlayer: everyone else) to come to us, or answer an
  // offer. `from` is filled in; nothing is kept.
  void SendRegroup(proto::Regroup offer);
  void SendRegroupAnswer(proto::RegroupAnswer answer);
  // Loot: our V dropped a bag of stacks (id ignored: the host numbers it;
  // `token` is ours, to know it again), or picked up a stack of a world item
  // (a claim: the host grants the first one). Each comes back as an event.
  void DropWorldItem(proto::WorldItem item);
  void TakeWorldItem(proto::WorldItemId id, std::uint8_t stack);
  // Loot: our V got a quest or important item, or (host) one lies on our
  // NPC's corpse: every other player gets it.
  void SendSharedItem(proto::SharedItem item);
  // Devices (docs/architecture/devices.md). Host: our game says what a
  // device is now (`state.by`: who made it so); everyone hears of a change,
  // and a guest whose request it did not become gets the record back.
  void ReportDevice(proto::DeviceState state);
  // Guest: our V operated a device, it is this in our game now (a request:
  // the host's DeviceState is the answer). `seen` and `player` are filled in.
  void UseDevice(proto::DeviceUse use);
  // Guest: a device came into our world like this. Known to the session and
  // different: our game hears the host's word again; unknown: the host is asked.
  void AskDevice(proto::DeviceState state);
  // Breach Protocol: our V breached a network or an NPC (its minigame won,
  // or lost: kBreachFailed); the programs are what the game's minigame
  // blackboard had. Guest: to the host, which answers (BreachAnswered).
  // Host: judged here, and passed on when it stands. Host only: `player`
  // may name another player, whose breach of an NPC our game resolved to
  // its network (that player's, not ours: our game applies it too).
  void SendBreach(proto::Breach breach);
  // Glass and breakable scenery: something broke in our game (`player` is
  // filled in). A guest's goes through the host, which holds it to
  // devices::kHostFracture*. A piece new to the session's record goes to
  // every player (its teller too, for its record; one away from it keeps
  // it for when it comes), another piece of a known one to the players
  // linked with its teller. Late joiners and a world resync get the record.
  void SendFracture(proto::Fracture fracture);
  // A turret in our V's hands: where it points, whether it fires, or that
  // we let go of it (`player` is filled in). To the players linked with
  // us (a guest's: through the host).
  void SendTurretHands(proto::TurretHands hands);
  // Host: fractures dropped for coming too fast, or past a full record.
  std::uint64_t FracturesDropped() const { return m_fracturesDropped; }
  // What broke in the session (host: kept; guest: as the host sent it).
  const devices::FractureRecord &Fractures() const { return m_fractureRecord; }
  // A device's fire. Host: our turret turned to a target, shot, or went
  // idle (kinds Idle, Aim, Shot), to the guests linked with us; a shot is
  // stamped with the host clock. Guest: our copy of a turret locked our own
  // V, or lost it (Seen, Lost), to the host. Anything else is not ours to
  // say and goes nowhere.
  void SendDeviceFire(proto::DeviceFire fire);
  // Host: the turrets each guest's copy has locked that guest's V with.
  const std::map<proto::PlayerId, std::set<std::uint64_t>> &DevicesSeeing() const {
    return m_deviceSeen;
  }
  // Host: breaches granted, and refused because someone was first.
  const devices::Breaches &BreachRecord() const { return m_breaches; }
  std::uint64_t BreachesRefused() const { return m_breachesRefused; }
  // The session's device record: host, the authority; guest, its copy.
  const devices::Ledger &Devices() const { return m_devices; }
  const devices::DigestCheck &DeviceChecks() const { return m_deviceCheck; }
  // Host: requests refused because someone else changed the device first.
  std::uint64_t DeviceStale() const { return m_deviceStale; }
  // Quest interactions (tandem/interact.hpp; they travel as story messages,
  // SendStory and events::Story). What is known of each object's choices:
  // host, what its game answered (a known host-only or local choice is
  // answered at once, without its game); guest, what the host answered (a
  // known host-only or local choice is not asked again: events::Story
  // brings the known verdict back at once). The host's referee (first come,
  // first served); answers given without the game; asks refused because
  // another player's use came first.
  const interact::Known &InteractKnown() const { return m_interactKnown; }
  const interact::Referee &InteractReferee() const { return m_interactReferee; }
  std::uint64_t InteractInstant() const { return m_interactInstant; }
  std::uint64_t InteractTaken() const { return m_interactTaken; }
  // Host: the world items still lying somewhere (taken stacks at quantity 0).
  const std::map<proto::WorldItemId, proto::WorldItem> &WorldItems() const { return m_worldItems; }
  // Down and out (docs/architecture/revive.md). Our V's life: kept, sent now if we
  // are in the session, else as soon as we are; the host hands everyone's
  // to late joiners and decides when the session is lost (no V up).
  void SetLocalVitals(proto::Vitals vitals);
  // We revive (or stop reviving) `revive.target`'s downed V: to that
  // player's game, through the host.
  void SendRevive(proto::Revive revive);
  // Every player's life as this game knows it (us included; a player with
  // none sent yet is up).
  proto::Life LifeOf(proto::PlayerId player) const;
  const std::map<proto::PlayerId, proto::Vitals> &Lives() const { return m_lives; }

  // Comms (docs/architecture/comms.md). Our ping, to every other player
  // (`from` filled in; the host holds each guest to comms::kHostMark* and
  // drops the rest). False: not sent (not in a session).
  bool SendMark(proto::Mark mark);
  // Our tag or reveal, to the players linked with us only.
  void SendTag(proto::Tag tag);
  // Our link consent and our waypoint (kind None: none). Kept: sent now if
  // we are in the session, else as soon as we are; the host hands
  // everyone's to late joiners.
  void SetLocalLink(proto::Link link);
  void SetLocalWaypoint(proto::Waypoint waypoint);
  // A link request of `decline.to` we did not take, and why (to them only;
  // `from` is filled in; nothing is kept).
  void SendLinkDecline(proto::LinkDecline decline);
  // Everyone's consent and waypoint as this game knows them (us included).
  const std::map<proto::PlayerId, proto::Link> &CommsLinks() const { return m_commsLinks; }
  const std::map<proto::PlayerId, proto::Waypoint> &Waypoints() const { return m_waypoints; }
  // Host: marks and tags it dropped for coming too fast.
  std::uint64_t CommsDropped() const { return m_commsDropped; }

  // Consistency (docs/architecture/consistency.md). Our world was rebuilt around our V
  // (proto::kEpoch* reason): what was in flight about the old one is void.
  // Host: every guest learns it, and their requests aimed at the old world
  // are dropped until they have. Guest: the host learns it.
  void BumpEpoch(std::uint8_t reason);
  std::uint32_t WorldEpoch() const { return m_epoch; }
  // Guest: the host's world epoch as last heard.
  std::uint32_t HostEpoch() const { return m_hostEpoch; }
  // The NPCs the host shares: host, its record; guest, its copy of it (what
  // its game must show as proxies, no more, no less).
  const std::map<proto::NpcId, proto::NpcSpawn> &SharedNpcs() const { return m_npcs; }
  // Guest: its digest checks so far. Host: the guests' requests it dropped
  // because they aimed at an older epoch of its world.
  const consistency::Checker &Checks() const { return m_checker; }
  std::uint64_t Fenced() const { return m_fenced; }
  // Regions (docs/architecture/regions.md). Who runs an NPC (kNoPlayer: not
  // one the session knows), and whether that is us.
  proto::PlayerId NpcOwner(proto::NpcId npc) const;
  bool OwnsNpc(proto::NpcId npc) const;
  // The NPCs this game must show as proxies: the ones of the owners we are
  // linked with, never ours.
  std::map<proto::NpcId, proto::NpcSpawn> ProxiedNpcs() const;
  // Who is linked with whom (host: as it decided; guest: as the host said),
  // and the players linked with us.
  const regions::Links &Links() const { return m_links; }
  bool LinkedWith(proto::PlayerId player) const;
  std::vector<proto::PlayerId> Linked() const;
  // Grouped or apart, for quests (docs/architecture/quest-carryover.md,
  // "Whose gig is it"). Apart: linked with nobody, our world is our own. The
  // leader of our group (us and everyone linked with us, directly or through
  // others) is the player whose world's quests are played there: a player
  // that holds its region, else the host, else the lowest id; us when apart.
  bool Apart() const { return Linked().empty(); }
  proto::PlayerId RegionLeader() const;
  // The game that runs the world around `player`: its new NPCs, its crowd,
  // its traffic and police (docs/architecture/traffic.md, "Who owns an
  // area"; the rule is regions::AreaOwner). A player's own game, unless its V
  // is deep in the region of a linked player that outranks it. Every
  // endpoint decides it from the places it hears, every 250 ms.
  proto::PlayerId AreaOwner(proto::PlayerId player) const;
  bool OwnsArea() const { return IsJoined() && AreaOwner(m_you) == m_you; }
  const std::map<proto::PlayerId, proto::PlayerId> &AreaOwners() const { return m_areaOwners; }
  // A quest of our own world runs where our V stands (a gig we started): we
  // keep our region. Kept: sent now if we are in the session, else as soon
  // as we are.
  void SetRegionHold(bool held);
  const regions::Holders &RegionHolders() const { return m_holders; }
  // One of our NPCs never changes owner (a quest or story NPC, a police unit).
  void PinNpc(proto::NpcId npc);
  // Our answer to events::NpcGive: the NPC's state as we leave it (its body
  // stays here as a proxy), or no (it is held, seated, in a scene).
  void GiveNpc(proto::NpcHandoverState state);
  void RefuseNpc(proto::NpcId npc);
  // Our answer to events::NpcTake: we run it now, or cannot (it goes).
  void AdoptNpc(proto::NpcId npc, bool accepted);
  // Host: hand-overs completed and NPCs inherited so far (tests, logs).
  std::uint64_t Handovers() const { return m_handoversDone; }
  std::uint64_t Inherited() const { return m_inherited; }

  // Faults on what this endpoint sends (tests, tandem-peer --drop/--dup).
  void SetFaults(const consistency::Faults &faults) { m_faults.Set(faults); }
  const consistency::FaultyLink &FaultStats() const { return m_faults; }

  // Time on the host's clock (the session's shared timeline). A better
  // estimate of the offset is slewed in (kClockSlew), never jumped to: every
  // remote thing is drawn on this timeline, and a step would be a hitch.
  std::uint64_t HostNowUs() const;
  static constexpr double kClockSlew = 0.05;            // at most 50 us per ms
  static constexpr std::int64_t kClockJumpUs = 250'000; // further off: jump at once

  // What went over the wire so far (tandem/traffic.hpp).
  net::Traffic Traffic() const { return m_session->TrafficSoFar(); }
  // Netcode counters (netcode.md): our pose stream's key frames and frames,
  // frames decoded over another key, states held back while still.
  struct NetCounters {
    std::uint64_t poseKeys{}, poseFrames{}, poseSkipped{};
    std::uint64_t poseOtherKey{}, poseDropped{};
    std::uint64_t statesHeld{}, statesSkipped{};
    std::uint64_t npcMotions{}, npcWhole{};
  };
  NetCounters Counters() const;
  // Off: the wire as before netcode.md (every state every frame, whole
  // poses and NPC lists), for measuring the difference (budget_test.cpp,
  // tandem-peer --legacy-wire). What we receive is understood either way.
  void SetCompact(bool compact) { m_compact = compact; }

  bool IsHost() const { return m_session->IsHost(); }
  bool IsJoined() const { return m_you != proto::kNoPlayer; }
  proto::PlayerId You() const { return m_you; }
  proto::PlayerId HostPlayer() const { return m_host; } // kNoPlayer until welcomed
  const std::map<proto::PlayerId, proto::PlayerInfo> &Players() const { return m_players; }
  std::optional<net::Stats> HostLinkStats() const;
  // The UDP port we listen on (host) or send from (guest).
  std::uint16_t LocalPort() const { return m_session->LocalPort(); }
  // The link to a player: host, to each guest; guest, to the host only
  // (the others are reached through it). nullopt for ourselves and strangers.
  std::optional<net::Stats> LinkStatsOf(proto::PlayerId player) const;

  // The relay, when the session uses one: host, its join code; guest, how
  // far it got (nullopt while still trying directly).
  std::optional<net::RelayStatus> Relay() const { return m_session->Relay(); }
  // Guest through a relay: the session's secret, to join again without the
  // code (JoinTarget::relay->secret).
  std::optional<net::RelaySecret> RelaySessionSecret() const {
    return m_session->RelaySessionSecret();
  }
  bool ThroughRelay() const { return m_throughRelay; }
  // Direct paths set up through the relay (tandem.ini's `direct`): see
  // net::RelayOptions::direct. Changed in a session, at once.
  void SetDirect(bool on) { m_session->SetDirect(on); }
  // Host on a relay: another join code now (Relay()->codeSalt has a kept
  // one's new salt).
  void NewRelayCode() { m_session->NewRelayCode(); }

  void Leave(const std::string &reason = "left");

  // Gameplay recording (tandem/play.hpp): each call above about our own V
  // hands `tap` the message as given, before anything is sent (whether or
  // not anyone is there to get it). Empty: off, and free.
  using Tap = std::function<void(const proto::Message &)>;
  void SetTap(Tap tap) { m_tap = std::move(tap); }
  // Session recording (tandem/play.hpp): as a guest, every message the host
  // sent us, as it arrives, before it is acted on. Empty: off.
  void SetReceivedTap(Tap tap) { m_receivedTap = std::move(tap); }
  // The referee (docs/architecture/sim.md): every message as it crosses the
  // wire, either way, before anything is filtered, stamped or applied. In:
  // `peer` is who sent it (host: the guest of that connection; guest: the
  // host). Out: whom it goes to (kNoPlayer: everyone, or nobody known yet),
  // whatever the fault layer then does to it. Empty: off, one check a message.
  enum class Wire : std::uint8_t { In, Out };
  using WireTap = std::function<void(Wire, proto::PlayerId peer, const proto::Message &)>;
  void SetWireTap(WireTap tap) { m_wireTap = std::move(tap); }
  // Each domain's hash as this game has it now: what a guest compares with
  // the host's Digest at that point of the stream.
  proto::Digest LocalDigest() const { return DigestFor(m_you); }
  const Timing &Timings() const { return m_timing; }
  // Tests: called with every endpoint made from now on (empty: off), so a
  // whole suite can run under one observer.
  static void SetCreationHook(std::function<void(Endpoint &)> hook);
  // The recorder (tandem/recorder.hpp): every call above that says
  // something (about our V or, as the host, our world) hands `tap` the
  // message as given, before anything is sent. Empty: off, and free.
  void SetRecordTap(Tap tap) { m_recordTap = std::move(tap); }
  // Our V as it stands now (look, held items, seat, radio, stealth, time
  // ability, ride, life, elsewhere): what a recording begins with.
  std::vector<proto::Message> LocalSnapshot() const;
  proto::Body OwnBody() const { return m_identity.body; }

private:
  Endpoint(std::unique_ptr<net::Session> session, Identity identity, Timing timing);

  void OnHostEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnGuestEvent(const net::Event &event, std::vector<SessionEvent> &out);
  void OnHello(net::PeerId peer, const proto::Hello &hello, std::vector<SessionEvent> &out);
  void RemovePlayer(net::PeerId peer, std::vector<SessionEvent> &out);
  // Host: the id for a new guest, the next free one after the last handed
  // out (2..kMaxPlayerId, round), so an id is reused only after the others.
  proto::PlayerId NextFreePlayer();
  void SendSessionTo(net::PeerId peer);
  // Host: these domains (kDomain*) as they are now, to one guest. `resync`:
  // its copy may hold what we no longer have (NpcSet first).
  void SendDomainsTo(net::PeerId peer, std::uint8_t domains, bool resync);
  // Every send goes through these (faults injected here).
  bool Send(net::PeerId peer, const proto::Message &message);
  bool Transmit(net::PeerId peer, const proto::Message &message);
  void Broadcast(const proto::Message &message, net::PeerId except = 0);
  void OnGuestMessage(const proto::Message &message, std::vector<SessionEvent> &out);
  // Each domain's hash as `self` sees it (consistency.hpp).
  proto::Digest DigestFor(proto::PlayerId self) const;
  void SendDigests();
  void BroadcastRoster();

  // ---- Regions (docs/architecture/regions.md) ----
  // Host: `viewer` shows `owner`'s NPCs (linked, and not its own).
  bool Sees(proto::PlayerId viewer, proto::PlayerId owner) const {
    return viewer != owner && m_links.Linked(viewer, owner);
  }
  // Host: `viewer`'s game shows `npc` (in the shared record, its owner linked
  // with it). An owner's word to one player about its NPC (a hit, a cue, a
  // shot, a hack) goes only where this holds, like its states.
  bool SharedWith(proto::NpcId npc, proto::PlayerId viewer) const {
    return m_npcs.contains(npc) && Sees(viewer, NpcOwner(npc));
  }
  // Host: an owner's word on its NPC, to every guest that sees it.
  void ShareNpc(const proto::Message &message, proto::PlayerId owner, net::PeerId except = 0);
  // An owner's word from our own game: to the host, or to whoever sees it.
  void SayNpc(const proto::Message &message);
  // To the owner of `npc` from `from` (a hit, an upload, an action): our own
  // game's (true), or passed on.
  bool RouteToOwner(proto::NpcId npc, const proto::Message &stamped, proto::PlayerId from);
  // To `to`, from an owner: ours (true), or passed on.
  bool RouteToPlayer(proto::PlayerId to, const proto::Message &message, proto::PlayerId from);
  // Host: a guest's messages as the owner of NPCs (false: not one of those).
  bool OnOwnerMessage(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                      std::vector<SessionEvent> &out);
  // Guest: the host's messages about regions and requests for our NPCs.
  bool OnRegionMessage(const proto::Message &message, std::vector<SessionEvent> &out);
  // Host: every 250 ms, links, hand-overs and their retries.
  void Arbitrate(std::vector<SessionEvent> &out);
  void OnLinksChanged(const regions::Links &before, std::vector<SessionEvent> &out);
  void SendRegionsTo(std::optional<net::PeerId> peer);
  // Host: an NPC's records for one guest, or as events for our own game.
  std::vector<proto::Message> NpcRecords(proto::NpcId npc) const;
  void ShowNpcTo(net::PeerId peer, proto::NpcId npc);
  void ShowNpcHere(proto::NpcId npc, std::vector<SessionEvent> &out);
  // Host: `npc` is `now`'s from here on (everyone who saw it or sees it now
  // is told), and `state` goes to `now` until it says it runs it.
  void MoveNpc(proto::NpcId npc, proto::PlayerId now, proto::NpcHandoverState state,
               std::vector<SessionEvent> &out);
  // Host: an NPC leaves the session's record (its owner is told too).
  void DropNpc(proto::NpcId npc, std::vector<SessionEvent> &out);
  void ForgetNpc(proto::NpcId npc);
  // Host: the NPCs of an owner that is gone, or whose world was rebuilt, go
  // to the players near them. The rest leave with an owner that is `gone`,
  // and stay its own otherwise (its game says what became of them).
  void InheritNpcsOf(proto::PlayerId owner, bool gone, std::vector<SessionEvent> &out);
  void OnGiven(proto::PlayerId from, proto::NpcHandoverState state, std::vector<SessionEvent> &out);
  void OnHandoverDone(proto::PlayerId from, const proto::NpcHandoverDone &done,
                      std::vector<SessionEvent> &out);
  // Guest: our whole set again (the host's record of it differed).
  void ResendOwned();
  std::uint32_t OwnedHash(proto::PlayerId owner, std::uint16_t &count) const;

  regions::Links m_links;
  regions::Places m_places; // where every player is, as last heard
  // Who runs the world around each player (AreaOwner), and when it is next decided.
  std::map<proto::PlayerId, proto::PlayerId> m_areaOwners;
  std::uint64_t m_nextAreasUs{};
  void UpdateAreas();
  std::uint32_t m_regionsRevision{};
  std::vector<proto::PlayerId> m_regionsSent; // host: the pairs as last sent
  std::vector<proto::PlayerId> m_linkedShown; // the players linked with us, as last reported
  proto::PlayerId m_leaderShown{};            // ...and our group's leader
  // Who holds its region (host: the session's record), and whether we do.
  regions::Holders m_holders;
  bool m_localHold{};
  void SendRegionHoldNow();
  // Our links or our group's leader changed: said once.
  void NoteGroup(std::vector<SessionEvent> &out);
  // We run this NPC, or its body is the corpse of one we ran.
  bool SpeaksFor(proto::NpcId npc) const;
  // NpcOwner, or for an NPC the record does not hold, its announcer.
  proto::PlayerId NpcOwnerOrAnnouncer(proto::NpcId npc) const {
    const auto owner = NpcOwner(npc);
    return owner != proto::kNoPlayer ? owner : regions::Announcer(npc);
  }
  // A request to an NPC's owner from our own game: to the host, or (host)
  // straight to the guest that owns it.
  void AskOwner(proto::NpcId npc, const proto::Message &stamped);
  std::uint64_t m_nextArbitrationUs{};
  // Who runs each NPC of m_npcs (absent: the game that announced it).
  std::map<proto::NpcId, proto::PlayerId> m_npcOwner;
  // Dead NPCs whose corpse is still there, by the owner they had.
  std::map<proto::NpcId, proto::PlayerId> m_corpseOwner;
  // Host: NPCs that never change owner, and until when a refused one is left alone.
  std::set<proto::NpcId> m_pinned;
  std::map<proto::NpcId, std::uint64_t> m_refusedUntilUs;
  // Host: hand-overs in flight. Without `state`: asked of `from`. With it:
  // `to` owns the NPC and has not said yet that it runs it.
  struct Handover {
    proto::PlayerId from{}, to{};
    std::optional<proto::NpcHandoverState> state;
    std::uint64_t sentUs{};
    int tries{};
  };
  std::map<proto::NpcId, Handover> m_handovers;
  // Host: `npc` is being handed from or to `player`. Until it is done, that
  // player's word on what it runs may not have caught up with our record:
  // its set neither drops nor corrects it, and its owned digest leaves it out.
  bool InHandover(proto::NpcId npc, proto::PlayerId player) const {
    const auto it = m_handovers.find(npc);
    return it != m_handovers.end() && (it->second.from == player || it->second.to == player);
  }
  std::uint64_t m_handoversDone{}, m_inherited{};
  // Giver: what we gave, until we hear who owns it. Receiver: what we took.
  std::map<proto::NpcId, proto::NpcHandoverState> m_given;
  std::set<proto::NpcId> m_took;
  // Host: who asked for each NpcAction passed on to a guest owner, in order.
  std::map<proto::NpcId, std::deque<proto::PlayerId>> m_askers;
  // Guest: the host's last OwnedDigest differed from ours.
  bool m_ownedDiffered{};
  std::uint64_t m_ownedResentUs{};
  // Host: the connection of a welcomed player (nullopt: none, or us).
  std::optional<net::PeerId> PeerOf(proto::PlayerId player) const;
  // Other mods: a ModList or Substitutes from `player` (host: a guest;
  // guest: the host).
  void OnModList(proto::PlayerId player, const proto::ModList &list,
                 std::vector<SessionEvent> &out);
  void OnSubstitutes(proto::PlayerId player, proto::Substitutes substitutes,
                     std::vector<SessionEvent> &out);
  void SendModsTo(net::PeerId peer);
  std::map<proto::PlayerId, std::vector<proto::ModEntry>> m_theirMods;
  // Host: delivers a message meant for `to` (kNoPlayer: everyone but
  // `except`) and tells whether it is for us too.
  template <class M> bool Deliver(const M &message, proto::PlayerId to, net::PeerId except);

  std::unique_ptr<net::Session> m_session;
  Identity m_identity;
  Tap m_tap;
  Tap m_recordTap;
  template <class M> void Record(const M &message) {
    if (m_recordTap)
      m_recordTap(proto::Message(message));
  }
  Tap m_receivedTap;
  WireTap m_wireTap;
  Timing m_timing;

  proto::PlayerId m_you{proto::kNoPlayer};
  proto::PlayerId m_host{proto::kNoPlayer};
  // The roster: each player under the name the session shows (the host
  // makes names unique at join: names::Unique).
  std::map<proto::PlayerId, proto::PlayerInfo> m_players;
  // Host: the name each guest asked for (sanitized), which a returning
  // player is matched by; guest: our own as the roster last showed it.
  std::map<proto::PlayerId, std::string> m_askedNames;
  std::string m_shownName;

  // Host: which connection is which player (only after a valid Hello).
  std::map<net::PeerId, proto::PlayerId> m_playerOfPeer;
  proto::PlayerId m_nextPlayer{2};

  // Guest: the host connection, and the clock offset from the best ping
  // (slewed from `from` at `since` towards `to`, see HostNowUs).
  net::PeerId m_hostPeer{};
  std::int64_t m_offsetFromUs{}, m_offsetToUs{};
  std::uint64_t m_offsetSinceUs{};
  std::int64_t OffsetAt(std::uint64_t nowUs) const;
  void SetClockOffset(std::int64_t offsetUs, bool jump);
  std::uint64_t m_bestRttUs{UINT64_MAX};
  std::uint64_t m_lastPingUs{};
  std::uint32_t m_seq{};
  std::uint32_t m_poseSeq{};
  // Netcode (netcode.md): how often our V's streams go, our pose encoder,
  // the host's re-encoders of each guest's poses, a decoder per sender;
  // where each V was last (relevance); the host's NPC coder, a guest's book.
  bool m_compact{true};
  netcode::Gate m_poseGate;
  pose::PoseEncoder m_poseEncoder;
  std::map<proto::PlayerId, pose::PoseEncoder> m_relayEncoders;
  std::map<proto::PlayerId, pose::PoseDecoder> m_poseDecoders;
  std::uint64_t m_poseSkipped{}, m_statesSkipped{}, m_npcMotions{}, m_npcWhole{};
  netcode::StillGate<proto::PlayerState> m_stateGate;
  netcode::StillGate<proto::VehicleState> m_vehicleGate;
  std::optional<proto::Vec3> m_ownPosition;
  std::map<proto::PlayerId, proto::Vec3> m_positions;
  float NearestOther() const;
  void NotePosition(const proto::PlayerState &state);
  void ForgetStreams(proto::PlayerId player);
  netcode::NpcCoder m_npcCoder;
  netcode::NpcBook m_npcBook;
  void OnRemotePoseFrame(proto::PoseFrame frame, net::PeerId from, std::vector<SessionEvent> &out);
  void OnHostNpcStates(proto::NpcStates states, std::vector<SessionEvent> &out);

  // Our look, and (host) everyone's latest, for late joiners.
  std::optional<proto::Appearance> m_localAppearance;
  std::uint32_t m_appearanceRevision{};
  std::map<proto::PlayerId, proto::Appearance> m_appearances;
  void SendLocalAppearanceNow();
  std::optional<proto::Held> m_localHeld;
  std::uint32_t m_heldRevision{};
  std::map<proto::PlayerId, proto::Held> m_held;
  std::uint32_t m_shotSeq{};
  void SendLocalHeldNow();
  std::optional<proto::Seat> m_localSeat;
  std::uint32_t m_seatRevision{};
  std::map<proto::PlayerId, proto::Seat> m_seats;
  std::uint32_t m_vehicleSeq{};
  void SendLocalSeatNow();
  std::optional<proto::VehicleRadio> m_localRadio;
  std::uint32_t m_radioRevision{};
  std::map<proto::PlayerId, proto::VehicleRadio> m_radios;
  void SendLocalRadioNow();
  std::optional<proto::World> m_world;
  std::uint32_t m_worldRevision{};
  std::map<proto::NpcId, proto::NpcSpawn> m_npcs;
  std::map<proto::NpcId, proto::NpcHealth> m_npcHealth;
  std::map<proto::NpcId, proto::NpcHold> m_npcHolds;
  std::map<proto::NpcId, proto::NpcAnim> m_npcAnims;
  std::uint32_t m_npcSeq{};
  // A guest linked with nobody: when it last told the host where its NPCs
  // and its vehicles are (regions::kApartStatesUs).
  std::uint64_t m_apartNpcStatesUs{}, m_apartTrafficStatesUs{};
  // Scene replay's streams (the camera; the actors' bodies and faces).
  scene::Pacer m_scenePacer;
  std::uint32_t m_cameraSeq{};
  bool m_cameraCut{};
  struct RigStream {
    rig::Encoder encoder;
    std::uint32_t hash{};
  };
  std::map<std::uint64_t, RigStream> m_rigStreams; // host, by scene::StreamOf
  struct TrackStream {
    rig::TrackEncoder encoder;
    std::uint32_t hash{};
  };
  std::map<std::uint64_t, TrackStream> m_trackStreams;       // host, by scene::TracksOf
  std::map<proto::NpcId, pose::PoseEncoder> m_actorEncoders; // host
  std::map<proto::NpcId, pose::PoseDecoder> m_actorDecoders; // guest
  // How much an actor's body matters: by how far the nearest other V is.
  float ActorWeight(proto::NpcId npc) const;
  // Who is in the pause menu (host: the session's record, for late joiners).
  bool m_localPaused{};
  std::set<proto::PlayerId> m_paused;
  void SendLocalPausedNow();
  // Records a player's pause state; false when it did not change.
  bool NotePaused(proto::PlayerId player, bool paused);
  // Active time abilities (host: the session's record, for late joiners).
  proto::TimeEffect m_localTime;
  std::map<proto::PlayerId, proto::TimeEffect> m_timeEffects;
  void SendLocalTimeNow();
  // Records a player's time ability; false when it did not change.
  bool NoteTime(const proto::TimeEffect &effect);
  // A player is gone (or back from a crash): its ability ends for everyone.
  void EndTimeOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // Players in another world (host: the session's record, for late joiners).
  proto::Elsewhere m_localElsewhere;
  std::map<proto::PlayerId, proto::Elsewhere> m_elsewheres;
  void SendLocalElsewhereNow();
  // Records a player's place; false when it did not change.
  bool NoteElsewhere(const proto::Elsewhere &state);
  // A player is gone (or back from a crash): back in the city for everyone.
  void EndElsewhereOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // What players are busy with (host: the session's record, for late joiners).
  proto::Activity m_localActivity;
  std::map<proto::PlayerId, proto::Activity> m_activities;
  void SendLocalActivityNow();
  bool NoteActivity(const proto::Activity &state);
  void EndActivityOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // Time skips: our requests' count; host, the one open request.
  std::uint32_t m_skipRequests{};
  activity::SkipArbiter m_skips;
  std::optional<proto::TimeSkip> m_ourSkip; // guest: ours, until it ends
  void OnTimeSkip(proto::TimeSkip skip, std::vector<SessionEvent> &out);
  void OnTimeSkipVote(const proto::TimeSkipVote &vote, std::vector<SessionEvent> &out);
  // `asked`: the others were asked (they hear how it ended); else only the
  // asker hears (an Applied skip is everyone's news either way).
  void EndTimeSkip(const proto::TimeSkipResult &result, bool asked, std::vector<SessionEvent> &out);
  activity::Blocker SkipBlocker() const;
  // Our stealth, and (host) everyone's latest, for late joiners.
  std::optional<proto::Stealth> m_localStealth;
  std::uint32_t m_stealthRevision{};
  std::map<proto::PlayerId, proto::Stealth> m_stealth;
  void SendLocalStealthNow();
  // Our presence (health, menu), and everyone's latest: the host hands them
  // to late joiners; every game's party HUD reads them.
  std::optional<proto::Presence> m_localPresence;
  std::uint32_t m_presenceRevision{};
  std::map<proto::PlayerId, proto::Presence> m_presences;
  void SendLocalPresenceNow();
  // Our status visuals, and everyone's latest (late joiners, resyncs).
  std::optional<proto::StatusVisuals> m_localStatusVisuals;
  std::uint32_t m_statusVisualsRevision{};
  std::map<proto::PlayerId, proto::StatusVisuals> m_statusVisuals;
  void SendLocalStatusVisualsNow();
  std::uint32_t m_cueSeq{};
  std::uint32_t m_awarenessSeq{};
  // Our ride, and everyone's (host: the session's record, for late joiners;
  // only rides that are on).
  std::optional<proto::Transit> m_localTransit;
  std::uint32_t m_transitRevision{};
  std::map<proto::PlayerId, proto::Transit> m_transits;
  void SendLocalTransitNow();
  void NoteTransit(const proto::Transit &transit);
  // Host: our NPCs seated in a player's vehicle.
  std::map<proto::NpcId, proto::NpcSeat> m_npcSeats;
  // World vehicles shared now and how each stands (owner: its record, for
  // late joiners; others: their copy).
  std::map<proto::TrafficId, proto::TrafficSpawn> m_traffic;
  std::map<proto::TrafficId, proto::TrafficStatus> m_trafficStatus;
  // A shared vehicle is no longer shared: what was kept of it goes.
  void ForgetTraffic(proto::TrafficId id);
  // Owner: `player` gets into a vehicle we run; the first claim wins.
  void TakeTraffic(proto::PlayerId player, proto::TrafficId id, std::vector<SessionEvent> &out);
  // Who runs each vehicle of m_traffic (absent: the game that announced it).
  std::map<proto::TrafficId, proto::PlayerId> m_trafficOwner;
  // Host: hand-overs in flight (as for NPCs: without `state`, asked of
  // `from`; with it, `to` runs it and has not said so yet), and until when a
  // refused one is left alone.
  struct TrafficHandoff {
    proto::PlayerId from{}, to{};
    std::optional<proto::TrafficHandoverState> state;
    std::uint64_t sentUs{};
    int tries{};
  };
  std::map<proto::TrafficId, TrafficHandoff> m_trafficHandoffs;
  // Host: as InHandover, for a vehicle.
  bool TrafficInHandover(proto::TrafficId id, proto::PlayerId player) const {
    const auto it = m_trafficHandoffs.find(id);
    return it != m_trafficHandoffs.end() && (it->second.from == player || it->second.to == player);
  }
  std::map<proto::TrafficId, std::uint64_t> m_trafficRefusedUntilUs;
  std::uint64_t m_trafficHandoversDone{}, m_trafficInherited{};
  // Giver: what we gave, until we hear who runs it. Receiver: what we took.
  std::map<proto::TrafficId, proto::TrafficHandoverState> m_trafficGiven;
  std::set<proto::TrafficId> m_trafficTook;
  // An owner's word on its vehicles: to the host, or (host) to whoever sees
  // that owner. ShareTraffic: host, to the guests that see `owner`.
  void SayTraffic(const proto::Message &message);
  void ShareTraffic(const proto::Message &message, proto::PlayerId owner, net::PeerId except = 0);
  // Host: what a guest says of its vehicles and asks of others' (false: not
  // a traffic message).
  bool OnGuestTraffic(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                      std::vector<SessionEvent> &out);
  // Guest: the host's word on a vehicle's owner, its hand-overs, the
  // requests for the vehicles we run (false: not one of those).
  bool OnHostTraffic(const proto::Message &message, std::vector<SessionEvent> &out);
  // Host: everything a guest needs to show a vehicle, owner first.
  std::vector<proto::Message> TrafficRecords(proto::TrafficId id) const;
  void ShowTrafficTo(net::PeerId peer, proto::TrafficId id);
  void ShowTrafficHere(proto::TrafficId id, std::vector<SessionEvent> &out);
  // Host: the vehicles of these domains a guest sees, to it (late joiners,
  // resyncs), and our own game's view when our links changed.
  void SendTrafficTo(net::PeerId peer, proto::PlayerId them, bool resync);
  void OnTrafficLinksChanged(proto::PlayerId other, bool sees, std::vector<SessionEvent> &out);
  // Host: hand-overs (Arbitrate), their steps, a vehicle moved to `now`
  // (its riders with it), a vehicle no longer shared, and the vehicles of an
  // owner that is gone or whose world was rebuilt.
  void ArbitrateTraffic(std::uint64_t now, std::vector<SessionEvent> &out);
  void OnTrafficGiven(proto::PlayerId from, proto::TrafficHandoverState state,
                      std::vector<SessionEvent> &out);
  void OnTrafficDone(proto::PlayerId from, const proto::TrafficHandoverDone &done,
                     std::vector<SessionEvent> &out);
  void MoveTraffic(proto::TrafficId id, proto::PlayerId now, proto::TrafficHandoverState state,
                   std::vector<SessionEvent> &out);
  void DropTraffic(proto::TrafficId id, std::vector<SessionEvent> &out);
  void InheritTrafficOf(proto::PlayerId owner, bool gone, std::vector<SessionEvent> &out);
  // Guest: our whole set of vehicles again (the host's record differed).
  void ResendOwnedTraffic();
  std::uint32_t OwnedTrafficHash(proto::PlayerId owner, std::uint16_t &count) const;
  std::uint32_t TrafficDigestFor(proto::PlayerId self) const;
  // Our wanted level, and everyone's above 0 (host: the session's record).
  std::optional<proto::Heat> m_localHeat;
  std::uint32_t m_heatRevision{};
  std::map<proto::PlayerId, proto::Heat> m_heat;
  void SendLocalHeatNow();
  // Records a player's wanted level; false when it did not change.
  bool NoteHeat(const proto::Heat &heat);
  // A player is gone (or back from a crash): no longer wanted, for everyone.
  void EndHeatOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast);
  // Vehicle combat (endpoint_vehicles.cpp). Damage of every vehicle (host:
  // the record; guest: what it heard, and its own car's), who holds which
  // gunner seat, everyone's weapons in a vehicle, the chases; the seats of
  // our own vehicles we give out.
  std::map<proto::VehicleRef, proto::VehicleDamage> m_vehicleDamage;
  std::map<proto::VehicleRef, proto::Gunner> m_gunners;
  std::map<proto::PlayerId, proto::VehicleCombat> m_seatCombat;
  std::optional<proto::VehicleCombat> m_localSeatCombat;
  std::uint32_t m_seatCombatRevision{};
  std::uint32_t m_damageRevision{};
  std::uint32_t m_mountedSeq{}, m_aimSeq{};
  std::map<proto::TrafficId, proto::Chase> m_chases;
  vehiclecombat::Turrets m_turrets;
  // Host: a guest's vehicle combat message; guest: the host's.
  void OnGuestVehicles(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                       std::vector<SessionEvent> &out);
  void OnHostVehicles(const proto::Message &message, std::vector<SessionEvent> &out);
  // Records a damage state; false when it is not newer than ours.
  bool NoteDamage(const proto::VehicleDamage &damage);
  bool NoteGunner(const proto::Gunner &gunner);
  // The owner's answer to a gunner request (ours to give, or relayed).
  void GiveGunner(const proto::GunnerRequest &request, std::vector<SessionEvent> &out);
  void SayGunner(const proto::Gunner &gunner, std::vector<SessionEvent> &out);
  void SendLocalVehicleCombatNow();
  // A player is gone (or back from a crash, `returning`): its car's damage,
  // the gunner seats it held or gave, its weapons in a vehicle, the chases
  // after it (not when returning: the chasers still chase its puppet).
  void ForgetVehiclesOf(proto::PlayerId player, std::vector<SessionEvent> &out, bool broadcast,
                        bool returning = false);
  // A shared vehicle is no longer shared: its damage, seat and chase go.
  void ForgetStreetVehicle(proto::TrafficId id);
  // Host: the vehicle combat state of these domains, to one guest.
  void SendVehiclesTo(net::PeerId peer, proto::PlayerId them, std::uint8_t domains, bool resync);
  // The domains' hashes of it (consistency.hpp), as `self` sees them.
  std::uint32_t VehiclePlayersDigest(proto::PlayerId self) const;
  std::uint32_t VehicleStreetDigest(proto::PlayerId self) const;
  // Host: the story for late joiners, and who answered the call first.
  story::Record m_story;
  story::CallArbiter m_calls;
  // Quest interactions (InteractKnown and the rest above).
  interact::Known m_interactKnown;
  interact::Referee m_interactReferee;
  std::uint64_t m_interactInstant{}, m_interactTaken{};
  void OnGuestInteract(net::PeerId peer, proto::PlayerId player, proto::QuestInteract ask,
                       std::vector<SessionEvent> &out);
  void OnGuestStory(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                    std::vector<SessionEvent> &out);
  // Host: the shared world items and who took each stack; events for our
  // own game from calls made between updates (our drops and claims).
  std::map<proto::WorldItemId, proto::WorldItem> m_worldItems;
  std::map<proto::WorldItemId, std::vector<proto::PlayerId>> m_worldTakers;
  proto::WorldItemId m_nextWorldItem{1};
  std::vector<SessionEvent> m_pending;
  void AddWorldItem(proto::PlayerId dropper, proto::WorldItem item, std::vector<SessionEvent> &out);
  void ClaimWorldItem(proto::PlayerId player, proto::WorldItemId id, std::uint8_t stack,
                      std::optional<net::PeerId> peer, std::vector<SessionEvent> &out);
  // Down and out: our V's life, and everyone's (host: the record for late
  // joiners). Host: whether the session is lost now, and how many times.
  std::optional<proto::Vitals> m_localVitals;
  std::uint32_t m_vitalsRevision{};
  std::map<proto::PlayerId, proto::Vitals> m_lives;
  bool m_lost{};
  std::uint32_t m_losses{};
  void SendLocalVitalsNow();
  // Host: every player is down or out: GameOver to all, once per loss.
  void CheckLost(std::vector<SessionEvent> &out);
  // Comms: our link consent and waypoint, and everyone's (host: the record
  // for late joiners); host, each guest's rate of marks and tags.
  std::optional<proto::Link> m_localLink;
  std::uint32_t m_linkRevision{};
  std::map<proto::PlayerId, proto::Link> m_commsLinks;
  std::optional<proto::Waypoint> m_localWaypoint;
  std::uint32_t m_waypointRevision{};
  std::map<proto::PlayerId, proto::Waypoint> m_waypoints;
  std::map<proto::PlayerId, comms::Limiter> m_markRate, m_tagRate;
  std::uint64_t m_commsDropped{};
  void SendLocalLinkNow();
  void SendLocalWaypointNow();
  // `from` and `to` each consent to the other (our own consent is ours).
  bool LinkedPair(proto::PlayerId from, proto::PlayerId to) const;
  // Host: a tag to the players linked with its sender (and our game, when
  // linked: true).
  bool DeliverTag(const proto::Tag &tag, net::PeerId except);
  // A player is gone or back from a crash: its comms state with it.
  void ForgetComms(proto::PlayerId player);
  // Host: a guest's Mark, Tag, Link or Waypoint; a player back from a
  // crash starting with none (told to all but its new connection).
  void OnGuestComms(net::PeerId peer, proto::PlayerId player, const proto::Message &message,
                    std::vector<SessionEvent> &out);
  void EndCommsOf(proto::PlayerId player, net::PeerId peer, std::vector<SessionEvent> &out);
  // Host: the session's difficulty and its NPCs' poise, for late joiners.
  std::optional<proto::Difficulty> m_difficulty;
  // The session's rules (host: its own, for late joiners; guest: the host's).
  std::optional<proto::Rules> m_rules;
  bool FriendlyFire() const { return (Rules().flags & proto::kRuleFriendlyFire) != 0; }
  std::map<proto::NpcId, proto::NpcPoise> m_npcPoise;
  // How many players each NPC is scaled for (NpcScale: its owner's word),
  // as this game knows it; and ours said before their spawn reached us.
  std::map<proto::NpcId, proto::NpcScale> m_npcScale;
  std::map<proto::NpcId, proto::NpcScale> m_npcScaleEarly;
  std::map<proto::NpcId, proto::NpcPoise> m_npcPoiseEarly; // ours, said before its spawn
  // The NPCs of our range we announced (a poise before the first spawn
  // waits for it; after a removal it is said), at most kNpcsSaid.
  std::set<proto::NpcId> m_npcsSaid;
  static constexpr std::size_t kNpcsSaid = 8192;
  void OnGuestCombat(proto::PlayerId player, const proto::Message &message,
                     std::vector<SessionEvent> &out);
  // Levels: ours (kept until we are in the session); host: each player's,
  // their mean as last said, and its NPCs' levels; guest: the last two as
  // the host said them.
  std::optional<std::uint8_t> m_localLevel;
  std::map<proto::PlayerId, std::uint8_t> m_levels;
  std::optional<proto::SessionLevel> m_sessionLevel;
  std::uint8_t m_levelGap{0};
  std::map<proto::NpcId, proto::NpcLevel> m_npcLevels;
  // Host: levels said of NPCs whose spawn has not been sent yet.
  std::map<proto::NpcId, proto::NpcLevel> m_npcLevelsEarly;
  // Vendors: the shared NPCs that are placed vendors (an owner's word, kept
  // with the NPC's records), ours said before their spawn went, and the
  // session's dead vendors by entity (host: derived and told to everyone;
  // guest: as the host said them).
  std::map<proto::NpcId, proto::NpcVendor> m_npcVendors;
  std::map<proto::NpcId, proto::NpcVendor> m_npcVendorsEarly;
  std::map<std::uint64_t, proto::NpcVendor> m_deadVendors;
  // Host: vendors knocked out (their records go; a later death still counts).
  std::map<proto::NpcId, proto::NpcVendor> m_downVendors;
  // Host: an NPC's owner said it is gone (reason: proto::NpcGone's): a vendor
  // that died is the session's to remember. Before the NPC's records go.
  void NoteVendorGone(const proto::NpcGone &gone, std::vector<SessionEvent> *out);
  // Placed NPCs (an owner's word, kept with the NPC's records), ours said
  // before their spawn went, and the session's deaths (host: the record;
  // guest: as the host said them).
  std::map<proto::NpcId, proto::NpcPlaced> m_npcPlaced;
  std::map<proto::NpcId, proto::NpcPlaced> m_npcPlacedEarly;
  ledger::Deaths m_deaths;
  // Our game says one of our NPCs is gone: a placed one that died or was
  // knocked out goes into the ledger (host), or to the host (guest). Before
  // the NPC's records go.
  void NoteDeath(const proto::NpcGone &gone);
  // Host: a death of player `by`'s NPC, into the ledger and to everyone
  // (once per entity). `out`: our own game's events.
  void RecordDeath(const proto::NpcDeath &death, proto::PlayerId by,
                   std::vector<SessionEvent> &out);
  void SendLocalLevelNow();
  void UpdateSessionLevel(std::vector<SessionEvent> &out);
  // Cars: ours (kept until we are in), everyone's (host: what it relays;
  // guest: what the host said). Who said is never the game's to claim.
  std::optional<proto::Lend> m_localLend;
  std::map<proto::PlayerId, proto::CarLend> m_lends;
  void SendLocalLendNow();
  // A player's lending heard (host: from its connection): kept, passed on,
  // given to our game. False: a repeat, nothing done.
  bool NoteLend(const proto::CarLend &lend, std::vector<SessionEvent> &out);
  // Trade. Host: the table, and its word passed on (to our own game: an
  // event). Every game: the trades whose commit it has applied.
  trade::Table m_trades;
  std::set<proto::TradeId> m_tradesApplied;
  void RouteTrade(const std::vector<trade::Out> &outs, std::vector<SessionEvent> &out);
  void HearTrade(const proto::Message &message, std::vector<SessionEvent> &out);
  std::set<proto::PlayerId> Present() const;
  // Consistency: our world epoch; guest, the host's and the first NpcStates
  // of it; host, the host epoch each guest has seen and its own epoch.
  std::uint32_t m_epoch{1};
  std::uint32_t m_hostEpoch{};
  std::uint32_t m_firstNpcSeq{};
  std::map<proto::PlayerId, std::uint32_t> m_seenHost, m_guestEpoch;
  std::uint64_t m_fenced{};
  std::uint64_t m_epochAtUs{};
  // Host: whom each NPC last shot at with an NpcShotAt, and when.
  std::map<proto::NpcId, std::pair<proto::PlayerId, std::uint64_t>> m_shotAt;
  // Host: digests (when the next round is due, guests owed one sooner) and
  // each guest's last resync, for its cooldown.
  std::uint32_t m_digestSeq{};
  std::uint64_t m_nextDigestUs{};
  std::set<proto::PlayerId> m_digestSoon;
  std::map<proto::PlayerId, std::uint64_t> m_resyncAtUs;
  // Guest: its checks, the host's NPCs that died (corpses until the host's
  // goes), and what came before the Welcome (the bulk channel can overtake it).
  consistency::Checker m_checker;
  std::set<proto::NpcId> m_corpses;
  std::vector<proto::Message> m_early;
  // Devices: the record (host: the authority; guest: its copy). Host: the
  // requests granted and not answered by a report yet, and each guest's
  // last resync. Guest: our requests the host has not answered yet.
  devices::Ledger m_devices;
  struct DeviceRequest {
    proto::PlayerId player{};
    devices::What what;
    std::uint64_t atUs{};
    // Who asked for something else while our game had not answered yet.
    std::vector<std::pair<proto::PlayerId, devices::What>> late;
  };
  std::map<devices::Key, DeviceRequest> m_deviceUses;
  std::map<devices::Key, devices::What> m_deviceAsked;
  std::map<proto::PlayerId, std::uint64_t> m_deviceSetAtUs;
  devices::DigestCheck m_deviceCheck;
  std::uint64_t m_deviceStale{};
  void OnDeviceUse(net::PeerId peer, proto::PlayerId player, const proto::DeviceUse &use,
                   std::vector<SessionEvent> &out);
  // Breach Protocol. Host: who breached what; a breach from `player` (us:
  // no peer), judged, answered and passed on. Guest: our breaches the host
  // has not answered yet.
  devices::Breaches m_breaches;
  std::uint64_t m_breachesRefused{};
  void OnBreach(std::optional<net::PeerId> peer, proto::PlayerId player, proto::Breach breach,
                std::vector<SessionEvent> &out);
  void SendDevicesTo(net::PeerId peer, bool resync);
  proto::DeviceDigest DeviceDigestNow() const;
  // The world domain's hash (time, weather, rules, the dead, what broke).
  std::uint32_t WorldDigestNow() const;
  // Glass: each guest's rate (host). A device's fire: the turrets whose
  // copy in a guest's game has that guest's V (host; at most
  // kMaxDevicesSeeing each), let go when the guest is gone.
  std::map<proto::PlayerId, comms::Limiter> m_fractureRate;
  std::uint64_t m_fracturesDropped{};
  devices::FractureRecord m_fractureRecord;
  // Host: a break said by `player` (ours or a guest's, stamped), past the
  // rate: kept and relayed (devices.md).
  void RelayFracture(const proto::Fracture &fracture, net::PeerId from,
                     std::vector<SessionEvent> *out);
  static constexpr std::size_t kMaxDevicesSeeing = 16;
  std::map<proto::PlayerId, std::set<std::uint64_t>> m_deviceSeen;
  void OnDeviceSeen(proto::PlayerId player, proto::DeviceFire fire, std::vector<SessionEvent> &out);
  void ForgetDevicesSeeing(proto::PlayerId player, std::vector<SessionEvent> &out);
  consistency::FaultyLink m_faults;
  bool m_ended{};
  // Guest: the relay to try when the host does not answer directly.
  std::optional<net::RelayOptions> m_fallback;
  bool m_tryRelay{};
  bool m_throughRelay{};
};
} // namespace tandem

extern template class tandem::ClosedVariant<tandem::SessionEventVariant>;
