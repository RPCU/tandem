#pragma once

// State shared by the game thread (scripts), the network thread and the
// engine's animation thread. Each piece says which threads touch it.

#include "config.hpp"

#include "tandem/compat.hpp"
#include "tandem/consistency.hpp"
#include "tandem/elsewhere.hpp"
#include "tandem/hosts.hpp"
#include "tandem/interact.hpp"
#include "tandem/interpolation.hpp"
#include "tandem/lagcomp.hpp"
#include "tandem/pose.hpp"
#include "tandem/presence.hpp"
#include "tandem/protocol.hpp"
#include "tandem/rigpose.hpp"
#include "tandem/session.hpp"
#include "tandem/timebubble.hpp"
#include "tandem/traffic.hpp"
#include "tandem/vehicle.hpp"
#include "tandem/vehicle_combat.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tandem::plugin {
class NetThread;

// What scripts learn from the session (game thread reads, net thread writes).
// A host NPC for the guest's scripts: spawn a proxy, or remove it.
// Host: a guest hit one of its NPCs. Guest: an NPC's health changed.
struct NpcEvent {
  // Effect (host): a guest's hit put the status effect spawn.record on it.
  // Seat (guest): it sits in player's vehicle, in slot spawn.appearance (a
  // CName hash; player kNoPlayer: it got out).
  enum class Kind : std::int32_t { Spawn = 1, Gone = 2, Hit = 3, Health = 4, Effect = 5, Seat = 6 };
  Kind kind{};
  proto::NpcSpawn spawn; // npc set for every kind
  std::uint8_t reason{};
  proto::PlayerId player{}; // Hit, Effect: who; Gone: the killer
  float amount{};           // Hit: damage (health points); Health: fraction 0..1
  std::uint8_t flags{};     // Hit: proto::kHit*; Health: proto::kNpcProtected
  proto::Vec3 direction;    // Hit: its way (spawn.position: where it landed)
};

// Quickhacks for the scripts (scripts/tandem/Hacks.reds), net -> game thread.
struct HackEvent {
  enum class Kind : std::int32_t {
    Upload = 1,     // host: a guest started an upload on one of our NPCs
    State = 2,      // guest: an upload on a host NPC started or ended
    Status = 3,     // guest: a quickhack effect came onto (state 1) or left a host NPC
    PlayerHack = 4, // guest: a host NPC netrunner hacks our V
    Car = 5,        // the driver: someone hacked the car we drive
    Traffic = 6,    // someone hacked a shared vehicle (npc: its TrafficId; flags 1: ours)
  };
  Kind kind{};
  proto::NpcId npc{};
  proto::PlayerId player{}; // the uploader, the effect's V, the hacker
  std::uint64_t action{};   // ObjectAction record (Status: the status effect record)
  std::int32_t state{};     // State: proto::HackStep; Status: applied; Car, Traffic: proto::CarHack
  std::int32_t flags{}; // Upload: proto::kUpload*; PlayerHack: proto::kPlayerHack*; Traffic: 1 ours
  float seconds{};      // upload time; Car: how long it lasts
  std::uint64_t atUs{}; // when it arrived (steady clock): a PlayerHack goes stale
};

// Grapples, takedowns, finishers and carried bodies (Takedowns.reds), for
// the scripts: a guest's request (host), the host's answer to ours, who holds
// an NPC, our half of an NPC's reaction (guests).
struct ActionEvent {
  enum class Kind : std::int32_t { Request = 1, Reply = 2, Hold = 3, Cue = 4 };
  Kind kind{};
  proto::NpcId npc{};
  proto::PlayerId player{}; // Request: who asks; Hold: the holder; Cue: us
  std::uint8_t action{};    // Request, Reply: proto::NpcActionKind; Hold: NpcHoldKind; Cue: cue
  std::uint64_t name{};
  std::uint64_t item{};
  float amount{};
  proto::Vec3 direction;
  bool accepted{};
  std::uint8_t reason{};
};

// Fast travel together (scripts/tandem/Travel.reds): an offer to come to
// another player, or an answer to ours, for the scripts.
struct RegroupEvent {
  enum class Kind : std::int32_t { Offer = 1, Answer = 2 };
  Kind kind{};
  proto::Regroup offer;        // Offer
  proto::RegroupAnswer answer; // Answer
};

// World vehicles and police for the scripts (scripts/tandem/Traffic.reds,
// Police.reds), net -> game thread: a shared vehicle appeared, went or
// changed; a guest hit one of ours (its owner); the area's owner saw our
// puppet commit a crime; a hand-over: give one of ours to `player` (Give),
// we run this one now (Take: the giver's state), whoever runs one changed
// (Owner: `player` now, `reason` was).
struct StreetEvent {
  enum class Kind : std::int32_t {
    Spawn = 1,
    Gone = 2,
    Status = 3,
    Hit = 4,
    Crime = 5,
    Give = 6,
    Take = 7,
    Owner = 8
  };
  Kind kind{};
  proto::TrafficSpawn spawn; // id set for every vehicle kind; Hit, Crime: position
  std::int32_t flags{};      // Spawn, Status: kTraffic*; Hit: kTrafficHit*; Crime: kCrime*
  std::int32_t reason{};     // Gone: kTraffic{Away,Taken,Removed}; Crime: proto::CrimeKind
  proto::PlayerId player{};  // Gone taken: whose car; Hit: who; Give: to whom; Take: from whom
  float amount{};            // Status, Take: health 0..1; Hit: damage; Crime: share of health
  proto::Vec3 vector;        // Hit: the push, or the way it went; Take: its velocity
  proto::NpcId npc{};        // Crime: the victim or the witness
  std::int32_t attack{};     // Crime: gamedataAttackType
  std::uint64_t atUs{};      // when it arrived (steady clock): hits and crimes go stale
};

// Vehicle combat for the scripts (scripts/tandem/Wheels.reds), net -> game
// thread (docs/architecture/vehicle-combat.md). The latest damage of each
// vehicle, gunner seats, aims and weapons in vehicles are kept as state for
// the scripts to read; these say what happened.
struct WheelsEvent {
  enum class Kind : std::int32_t {
    Damage = 1, // a vehicle another game owns changed; `a`: vehiclecombat::Destruction
    Hit = 2,    // a hit on one of our vehicles: `amount` damage, `a` flags, `b` tyre, `c` window
    Exit = 3,   // a player left a vehicle the hard way: `a` ExitKind, `b` kExit* flags
    Combat = 4, // a player's weapons in a vehicle: `a` SeatCombat, `id` weapon
    Fire = 5,   // a vehicle weapon fired: `id` weapon, `id2` slot, `position` its target
    Gunner = 6, // a gunner seat changed hands: `player` the gunner (0: free), `id` the seat
    Ram = 7,    // area owner: a player's car ran into our NPC `npc`: `vector` its velocity
    Chase = 8,  // vehicle `vehicle` (a TrafficId) chases `player` (0: nobody), `a` flags
  };
  Kind kind{};
  proto::PlayerId player{};
  std::uint32_t vehicle{}; // a proto::VehicleRef (Chase: a TrafficId)
  std::int32_t a{}, b{}, c{};
  std::uint64_t id{}, id2{};
  float amount{};
  proto::NpcId npc{};
  proto::Vec3 position, vector;
  std::uint64_t atUs{}; // when it arrived (steady clock): hits, exits, shots go stale
};

// NPC proxies use the player-puppet tracks and controller under these ids.
inline constexpr proto::PlayerId kNpcPuppetBase = 0x10000;

struct ScriptEvent {
  enum class Kind : std::int32_t {
    PlayerJoined = 1,
    PlayerLeft = 2,
    SessionEnded = 3,
    InSession = 4,
    // Something serious to tell the player (name: the text), at most once
    // in a while (net_thread.cpp): a resync, a lost host, a reconnection.
    Notice = 5,
    // The session shows us under another name (name: it), another player
    // having ours.
    Renamed = 6,
  };
  Kind kind{};
  proto::PlayerId player{};
  std::string name;
  proto::Body body{};
};

// The session the next Start runs: tandem.ini's until the co-op menu picks
// another (scripts/tandem/Coop.reds).
struct SessionChoice {
  Config::Role role{Config::Role::Off};
  std::string address; // join: the host
  std::uint16_t port{};
  // join: the host's join code (through tandem.ini's relay; "" by address),
  // and whether the address is tried too (Config::addressSet).
  std::string joinCode;
  bool addressSet{};
  bool fromMenu{};
  // join, chosen in the co-op menu: at the first welcome our V goes beside
  // the host (Travel.reds), once (TakeArrival). Never carried by a copy of
  // the choice made after it was taken (a checkpoint's rejoin).
  bool arrive{};
};

// How the session is doing, for the co-op menu (net thread writes, game
// thread reads).
struct SessionStatus {
  enum class State : std::int32_t {
    Off = 0,        // no session
    Starting = 1,   // chosen, the net thread is setting up
    Connecting = 2, // guest: waiting for the host's welcome
    InSession = 3,  // hosting, or welcomed by the host
    Retrying = 4,   // guest: the session ended or the host was not reached; again in 5 s
    Failed = 5,     // host: could not open the port (the thread gave up)
  };
  struct Player {
    proto::PlayerId id{};
    std::string name;
    std::int32_t pingMs{-1}; // -1: unknown, or ourselves
  };
  State state{State::Off};
  std::string detail; // why it ended or failed
  proto::PlayerId you{}, host{};
  std::vector<Player> players; // us included
};

class Runtime {
public:
  static Runtime &Get();

  // tandem.ini, as loaded (the defaults; the choice below overrides the role).
  Config config;
  // `direct` as the settings page last set it, for the net thread (applied
  // to a running session at once).
  std::atomic<bool> directOn{true};
  // `code_salt` likewise (the New join code action moves it on).
  std::atomic<std::uint32_t> codeSalt{0};

  // What is installed around us (tandem/compat.hpp), judged once when the
  // plugin loads (main.cpp) and only read afterwards, from any thread.
  struct Installation {
    std::filesystem::path game; // the game's folder
    compat::Facts facts;
    compat::Startup startup;
    proto::Install install; // what the other players are told
  };
  Installation installation;
  // Phantom Liberty, as the game's menus report it (scripts:
  // TandemCoop.ReportExpansion -> TandemNative.SetExpansion; game thread).
  // Unknown until the first menu. Without it a chosen session is dropped.
  void SetExpansion(compat::Expansion state);
  compat::Expansion Expansion() const { return static_cast<compat::Expansion>(m_expansion.load()); }
  // Co-op stays off: a startup problem (the game's version, a missing or
  // old mod) or no Phantom Liberty, unless tandem.ini says compat = off. No
  // session is chosen or started, and the co-op menu says why
  // (TandemNative.CompatProblems).
  bool Blocked() const { return config.compat && !Problems().empty(); }
  // Why, one sentence each for the player (whatever compat says): a startup
  // problem, Phantom Liberty, or a core feature that is off in this game
  // (degradations.hpp: no session without one, degraded::CoreProblem).
  std::vector<std::string> Problems() const;
  // Game thread, when what the game runs without changed: a session chosen
  // (tandem.ini, the menu) and not started yet is dropped when co-op is off
  // now. A running one is the scripts' to leave, with words (Degraded.reds).
  void RecheckChoice();
  // True once after a chosen session was dropped because a core feature is
  // off (the scripts tell the player why nothing starts).
  bool TakeCoreRefusal() { return m_coreRefused.exchange(false); }
  // What differs between our game and another player's (net thread writes,
  // one line each; kept for the menu and the report until the next session).
  void NoteDifference(std::string line);
  void ClearDifferences();
  std::vector<std::string> Differences() const;
  // The bug report (tandem/report.hpp), into reports/ next to the DLL. The
  // zip's path; "" with `error` when nothing was written.
  std::string WriteReport(std::string &error);

  // Game thread: the local V exists; start the chosen session once.
  void Start(proto::Body body);
  void Stop();
  bool Started() const;

  // Game thread (co-op menu): what the next session is. A running session
  // stops first; scripts start the new one (Start) when a game is loaded.
  void Choose(SessionChoice choice);
  // Back to tandem.ini's choice (the main menu shows: a menu choice was for
  // the game the player left).
  void ResetChoice();
  SessionChoice Choice() const;
  Config::Role ChosenRole() const;
  // Game thread (co-op menu): the chosen join arrives beside the host
  // (SessionChoice::arrive); whether it still does; taken once, at the welcome.
  void ChooseArrival();
  bool Arriving() const;
  bool TakeArrival();

  // Net thread -> game thread: how the session is doing.
  void SetStatus(SessionStatus status);
  SessionStatus Status() const;

  // The hosts this game joined, newest first (config.recentFile). Read on
  // the game thread, written by the net thread when a host welcomes us.
  std::vector<hosts::Recent> RecentHosts() const;
  void RememberHost(const hosts::Target &target, std::string name);

  // tandem-relay, net thread -> game thread: the host's join code to show
  // ("K7Q-2M9", "" none yet or no relay) and what the link is doing (""
  // no relay, "connecting", "ready", else what went wrong).
  void SetRelay(std::string code, std::string status);
  std::string RelayCode() const;
  std::string RelayStatus() const;
  // The rest of what the co-op menu shows of the relay link (net thread
  // sets, game thread reads): the code is kept, the connection check
  // (net::DirectOutlook), the direct paths up.
  struct RelayView {
    bool kept{};
    std::string check;
    std::int32_t direct{};
  };
  void SetRelayDetails(RelayView view);
  RelayView RelayDetails() const;
  // "New code" asked by the co-op menu (counted; the net thread acts on a
  // change).
  std::atomic<std::uint32_t> newCodeRequests{0};
  // Game thread (the settings page): change a tandem.ini key
  // (settings::IsEditable) and save it there. Applied at once, or, for what
  // a running session was set up with (name, relay, story, time_host), when
  // it ends. False: not a key the page changes, or not a value of it.
  bool ChangeSetting(const std::string &key, const std::string &value);
  // Its value as saved (a change waiting for the session's end included).
  std::string SavedSetting(std::string_view key) const;
  // Game thread (the co-op menu): the name is still the default (V) and the
  // menu has not asked for one since the game started (names.hpp).
  bool NameToAsk() const;
  // The menu asked (whatever the answer); false asks again (tests).
  void NameAsked(bool asked) { m_nameAsked = asked; }

  // Game thread: be the guest of the session `code` names, through
  // tandem.ini's relay. A running session is left for it at once; else it
  // starts with the next loaded game. False: not a code, or no relay.
  bool JoinByCode(std::string_view code);

  // Game thread -> net thread: our V, newest only.
  void SubmitLocal(const proto::PlayerState &state);
  std::optional<proto::PlayerState> TakeLocal();

  // Net thread -> game thread.
  void PushEvent(ScriptEvent event);
  std::vector<ScriptEvent> TakeEvents();

  // Animation job thread -> net thread: our V's body pose, newest only. With a
  // loopback player set, it is also drawn on that player's puppet, with no
  // network (a test of the read and write sides together).
  void SubmitLocalPose(const proto::PoseState &pose);
  std::optional<proto::PoseState> TakeLocalPose();
  void SetPoseLoopback(proto::PlayerId player) { m_poseLoopback.store(player); }
  // Our V away from its body (scripts/tandem/Elsewhere.reds): the body's
  // pose as it was at `timeUs` (net::NowUs) is sent in place of the live
  // one, ten times a second, until released.
  void HoldLocalPose(std::uint64_t timeUs);
  void ReleaseLocalPose();
  bool LocalPoseHeld() const;
  proto::PlayerId PoseLoopback() const { return m_poseLoopback.load(); }

  // Net thread writes remote poses; animation job threads read them, drawn
  // with the same delay as the player's position.
  void PushRemotePose(const proto::PoseState &pose);
  bool PoseOf(proto::PlayerId player, pose::Pose &out);
  std::uint32_t PosesReceived(proto::PlayerId player);

  // Scene replay (docs/architecture/scene-replay.md): the body poses of a
  // scene's actors. Host: animation job threads -> net thread, the newest of
  // each NPC. Guest: the net thread writes, animation job threads read, drawn
  // as far in the past as the proxy's position; nothing once the stream has
  // stopped for kActorPoseStaleUs (the proxy's own animation shows again).
  static constexpr std::uint64_t kActorPoseStaleUs = 1'000'000;
  void SubmitActorPose(proto::NpcId npc, const proto::PoseState &pose);
  std::vector<proto::ActorPose> TakeActorPoses();
  void PushActorPose(const proto::ActorPose &pose);
  bool ActorPoseOf(proto::NpcId npc, pose::Pose &out);
  void ForgetActorPoses();
  std::uint32_t ActorPosesReceived();
  // Scene replay, other rigs (a face, a body that is not the humanoid one;
  // engine/posestream.cpp). Host: animation job threads submit a rig's pose
  // (the newest of each NPC's rig is kept), the net thread takes them.
  // Guest: the net thread pushes what the host said, the job thread that
  // commits the proxy's rig writes it over the rig's own pose (answers how
  // many bones; 0: no stream, a stream stopped for kActorPoseStaleUs, or
  // another rig). ForgetActorPoses forgets these too.
  struct RigSample {
    proto::NpcId npc{};
    std::uint8_t part{};
    std::uint32_t hash{}; // rig::RigHash of its bone names
    std::shared_ptr<const std::vector<rig::Bone>> reference;
    std::vector<rig::Bone> pose;
    // Its float tracks (a face's), when the layout check let them be read.
    std::uint32_t trackHash{}; // rig::RigHash of its track names
    std::shared_ptr<const std::vector<float>> trackReference;
    std::vector<float> tracks;
  };
  void SubmitRigPose(RigSample sample);
  std::vector<RigSample> TakeRigPoses();
  void PushRigPose(const proto::RigPose &pose);
  std::size_t ApplyRigPose(proto::NpcId npc, std::uint8_t part, std::uint32_t rigHash,
                           std::span<const rig::Bone> reference, std::span<rig::Bone> pose);
  std::uint32_t RigPosesReceived();
  // The same for a rig's float tracks (RigTracks).
  void PushRigTracks(const proto::RigTracks &tracks);
  std::size_t ApplyRigTracks(proto::NpcId npc, std::uint8_t part, std::uint32_t trackHash,
                             std::span<const float> reference, std::span<float> values);

  // Game thread -> net thread: our V's look, newest only (the endpoint keeps it
  // and sends it again to whoever joins).
  void SubmitLocalAppearance(proto::Appearance appearance);
  std::optional<proto::Appearance> TakeLocalAppearance();
  // Net thread -> game thread: each player's latest look; `changes` counts
  // the looks received for them (0: none yet).
  void PushRemoteAppearance(const proto::Appearance &appearance);
  // A whole V's look of this body (head, body, arms) for a puppet whose own
  // keys are missing or failed: the last real one seen this game (ours or
  // another player's of that body). Empty when none was.
  std::vector<proto::AppearanceKey> StandInLook(proto::Body body) const;
  std::optional<proto::Appearance> RemoteAppearance(proto::PlayerId player, std::uint32_t &changes);

  // What our V holds (newest only) and its shots (queued), game -> net thread;
  // the others' held items (latest + count) and shots, net -> game thread.
  void SubmitLocalHeld(const proto::Held &held);
  std::optional<proto::Held> TakeLocalHeld();
  void SubmitLocalShot(const proto::Shot &shot);
  std::vector<proto::Shot> TakeLocalShots();
  void PushRemoteHeld(const proto::Held &held);
  std::optional<proto::Held> RemoteHeld(proto::PlayerId player, std::uint32_t &changes);
  void PushRemoteShot(const proto::Shot &shot);
  std::vector<proto::Shot> TakeRemoteShots();

  // Where our V sits and the car it drives (newest only), game -> net;
  // the others' seats (latest + count) and cars, net -> game.
  void SubmitLocalSeat(const proto::Seat &seat);
  std::optional<proto::Seat> TakeLocalSeat();
  void SubmitLocalVehicle(const proto::VehicleState &vehicle);
  std::optional<proto::VehicleState> TakeLocalVehicle();
  void PushRemoteSeat(const proto::Seat &seat);
  std::optional<proto::Seat> RemoteSeat(proto::PlayerId player, std::uint32_t &changes);
  void PushRemoteVehicle(const proto::VehicleState &vehicle);
  // Where to draw the player's car now (the same delay as their body).
  sync::VehicleView VehicleOf(proto::PlayerId player);
  // Our V's ride (scripts/tandem/Transit.reds), game -> net (newest only);
  // the others' rides, net -> game (latest + count; kind None: none).
  void SubmitLocalTransit(const proto::Transit &transit);
  std::optional<proto::Transit> TakeLocalTransit();
  void PushRemoteTransit(const proto::Transit &transit);
  // Gameplay recording (tandem/play.hpp), game thread -> net thread: start
  // one (recordings/NAME.tplay next to the DLL; one already going stops
  // first), or stop it (""). The net thread writes the file.
  void RequestRecording(std::string name);
  std::optional<std::string> TakeRecordingRequest();
  std::optional<proto::Transit> RemoteTransit(proto::PlayerId player, std::uint32_t &changes);

  // The radio of the car we drive, game -> net (each one sent: a repeat is
  // for a passenger getting in). `trackStarted`: the song just changed on
  // the same station, so its start is now (else unknown). The others' car
  // radios, net -> game (latest + count).
  void SubmitLocalRadio(proto::VehicleRadio radio, bool trackStarted);
  std::vector<proto::VehicleRadio> TakeLocalRadios();
  void PushRemoteRadio(const proto::VehicleRadio &radio);
  std::optional<proto::VehicleRadio> RemoteRadio(proto::PlayerId player, std::uint32_t &changes);

  // Host: the world as our game has it, game -> net (newest only).
  // Guest: the host's world, net -> game (latest + count).
  void SubmitWorld(const proto::World &world);
  std::optional<proto::World> TakeWorld();
  void PushHostWorld(const proto::World &world);
  std::optional<proto::World> HostWorld(std::uint32_t &changes);

  // The pause menu (docs/architecture/time.md): ours open or closed, game -> net
  // (newest only); the other players in theirs, net -> game (latest + a
  // count of changes, 0: none yet). Names, from the roster, for the notice.
  void SubmitLocalPaused(bool paused);
  std::optional<bool> TakeLocalPaused();
  void SetRemotePaused(std::vector<proto::PlayerId> players);
  std::vector<proto::PlayerId> RemotePaused(std::uint32_t &changes);
  // Time abilities (scripts/tandem/Time.reds): ours, game -> net (newest
  // only); the other players' active ones, net -> game (latest + a count of
  // changes, 0: none yet).
  void SubmitLocalTime(const proto::TimeEffect &effect);
  std::optional<proto::TimeEffect> TakeLocalTime();
  void PushRemoteTime(const proto::TimeEffect &effect);
  std::map<proto::PlayerId, proto::TimeEffect> RemoteTimes(std::uint32_t &changes);
  // The NPCs slowed around the Vs in a Sandevistan. Game thread only (the
  // scripts that apply them).
  sync::TimeBubbles &Bubbles() { return m_bubbles; }
  // Other worlds (tandem/elsewhere.hpp): ours, game -> net (newest only);
  // the other players not in the city, net -> game (latest + a count of
  // changes, 0: none yet). The tracker of our V: game thread only.
  void SubmitLocalElsewhere(const proto::Elsewhere &state);
  std::optional<proto::Elsewhere> TakeLocalElsewhere();
  void PushRemoteElsewhere(const proto::Elsewhere &state);
  void ClearRemoteElsewhere();
  std::map<proto::PlayerId, proto::Elsewhere> RemoteElsewhere(std::uint32_t &changes);
  elsewhere::Tracker &Away() { return m_away; }
  void NotePlayerName(proto::PlayerId player, std::string name);
  std::string PlayerName(proto::PlayerId player);

  // Regroup offers and answers: ours, game -> net (queued; offers numbered
  // here); the others', net -> game (queued).
  std::uint32_t SubmitRegroup(proto::Regroup offer);
  void SubmitRegroupAnswer(const proto::RegroupAnswer &answer);
  std::vector<proto::Message> TakeRegroupOutbox();
  void PushRegroupEvent(RegroupEvent event);
  std::vector<RegroupEvent> TakeRegroupEvents();

  // Host: its NPCs, game -> net (spawns and removals queued, states newest).
  // Guest: the host's NPCs, net -> game (events queued; states feed the
  // tracks of kNpcPuppetBase + npc).
  void SubmitNpcSpawn(const proto::NpcSpawn &spawn);
  void SubmitNpcGone(const proto::NpcGone &gone);
  void SubmitNpcHealth(const proto::NpcHealth &health); // host
  void SubmitNpcHit(const proto::NpcHit &hit);          // guest
  void SubmitNpcEffect(const proto::NpcEffect &effect); // guest
  void SubmitNpcSeat(const proto::NpcSeat &seat);       // host
  // Host: our NPC hit a guest's puppet, game -> net (queued with the NPCs').
  // Guest: the host's NPCs hit our V, net -> game (queued).
  void SubmitPlayerHit(const proto::PlayerHit &hit);
  void PushPlayerHit(const proto::PlayerHit &hit);
  std::vector<proto::PlayerHit> TakePlayerHits();
  // NPC shots at a guest's V: the host's go out on the NPC outbox; a guest
  // takes the ones at it (net -> game), and its hit reports go out on the
  // outbox too; the host takes the reports (net -> game).
  void SubmitShotAt(const proto::NpcShotAt &shot);
  void PushShotAt(const proto::NpcShotAt &shot);
  std::vector<proto::NpcShotAt> TakeShotsAt();
  void SubmitShotHit(const proto::ShotHit &hit);
  void PushShotHit(const proto::ShotHit &hit);
  std::vector<proto::ShotHit> TakeShotHits();
  // Guest: which players the host's NPC has detected (bit player - 1).
  std::uint8_t NpcDetected(proto::NpcId npc);
  void SubmitNpcStates(const proto::NpcStates &states);
  // World vehicles and police (docs/architecture/traffic.md, police.md).
  // game -> net: what the area's owner says of its vehicles (TrafficSpawn,
  // TrafficGone, TrafficStatus, Crime), and anyone's TrafficHit and
  // TrafficClaim. The owner's moving vehicles go through the packet budget
  // here (game thread) and leave as one TrafficStates (newest only).
  void SubmitStreet(proto::Message message);
  // Traffic lights (scripts/tandem/Lights.reds, traffic.md "Traffic
  // lights"). net -> game: the other games' TrafficLights (ours leave
  // through Combat). Any thread (a light's own event hook) -> game: a
  // colour our game gave a light; game -> any thread: the lights that show
  // another game's colours now (the hook drops our game's own change).
  void PushLights(proto::TrafficLights lights);
  std::vector<proto::TrafficLights> TakeLights();
  void NoteLightSaid(std::uint64_t entity, std::uint8_t color);
  std::vector<proto::TrafficLight> TakeLightsSaid();
  void FollowLights(std::vector<std::uint64_t> entities);
  bool FollowsLight(std::uint64_t entity) const;
  std::vector<proto::Message> TakeStreetOutbox();
  void SubmitTrafficCars(std::span<const traffic::Car> cars, std::span<const proto::Vec3> viewers);
  std::optional<proto::TrafficStates> TakeTrafficStates();
  // net -> game: events for the scripts, and each vehicle's track.
  void PushStreetEvent(StreetEvent event);
  std::vector<StreetEvent> TakeStreetEvents();
  void PushTrafficStates(const proto::TrafficStates &states);
  void ForgetTraffic(proto::TrafficId id);
  // Where to draw a shared vehicle now (game thread).
  sync::VehicleView TrafficOf(proto::TrafficId id);
  // The session's record of the shared vehicles, for the scripts' checks.
  // The session's record of the shared vehicles (net thread, when it
  // changed): every one we know, the ones we show as proxies, who runs each.
  void SetSharedTraffic(const std::map<proto::TrafficId, proto::TrafficSpawn> &traffic,
                        const std::map<proto::TrafficId, proto::TrafficStatus> &status,
                        const std::map<proto::TrafficId, proto::TrafficSpawn> &shown,
                        std::map<proto::TrafficId, proto::PlayerId> owners);
  // The proxies our game must show, no more, no less.
  std::vector<std::pair<proto::TrafficSpawn, proto::TrafficStatus>> SharedTraffic();
  bool IsTrafficShared(proto::TrafficId id);
  // Who runs a shared vehicle (0: not one the session knows).
  proto::PlayerId TrafficOwnerOf(proto::TrafficId id);
  // Our V's wanted level, game -> net (newest only); everyone else's.
  void SubmitLocalHeat(const proto::Heat &heat);
  std::optional<proto::Heat> TakeLocalHeat();
  void PushRemoteHeat(const proto::Heat &heat);
  proto::Heat HeatOf(proto::PlayerId player);
  // Vehicle combat (docs/architecture/vehicle-combat.md). game -> net: what
  // our game says (VehicleDamage of our vehicles, hits, exits, weapons in a
  // vehicle, mounted shots, gunner requests, aims, bumps, rams, chases).
  void SubmitWheels(proto::Message message);
  std::vector<proto::Message> TakeWheelsOutbox();
  // Our own vehicles' damage as the scripts build it: the grid and the
  // parts are noted as the game reports them, the rest with the send.
  void NoteOwnGrid(proto::VehicleRef vehicle, std::span<const float> cells);
  void NoteOwnPart(proto::VehicleRef vehicle, std::uint64_t part);
  proto::VehicleDamage OwnDamage(proto::VehicleRef vehicle);
  void SetOwnDamage(const proto::VehicleDamage &damage);
  void ForgetOwnDamage(proto::VehicleRef vehicle);
  // net -> game: events, and the state the scripts read.
  void PushWheelsEvent(WheelsEvent event);
  std::vector<WheelsEvent> TakeWheelsEvents();
  // A vehicle another game owns is damaged so: the event says what a copy
  // must do (blow up now, be a wreck, or show its health).
  void PushVehicleDamage(const proto::VehicleDamage &damage);
  std::optional<proto::VehicleDamage> DamageOf(proto::VehicleRef vehicle);
  void SetGunner(const proto::Gunner &gunner);
  proto::Gunner GunnerOf(proto::VehicleRef vehicle);
  void PushGunnerAim(const proto::GunnerAim &aim);
  std::optional<proto::GunnerAim> AimOf(proto::VehicleRef vehicle);
  void SetSeatCombat(const proto::VehicleCombat &combat);
  proto::VehicleCombat SeatCombatOf(proto::PlayerId player);
  // Pushes between players' cars (vehiclecombat::BumpLedger): our car
  // touched `player`'s copy; another's report of a push; the pushes due.
  void NoteContact(proto::PlayerId player);
  void PushBump(const proto::VehicleBump &bump);
  std::vector<vehiclecombat::BumpLedger::Push> TakeDuePushes();
  std::vector<proto::Message> TakeNpcOutbox();
  std::optional<proto::NpcStates> TakeNpcStates();
  void PushNpcEvent(const NpcEvent &event);
  std::vector<NpcEvent> TakeNpcEvents();
  // Guest: `you` is our player id (whose detection counts for our V).
  void PushNpcStates(const proto::NpcStates &states, proto::PlayerId you);

  // Each player is detected on their own (docs/architecture/stealth.md). Guest: how
  // many of the host's NPCs have detected our V (0 once the host's states
  // are 3 s old), and how far an NPC is in noticing it (0..1; 0 once 1 s
  // old). Host: its NPCs' awareness of the guests, game -> net (newest).
  std::int32_t NpcsDetectingUs();
  float NpcAwarenessOfUs(proto::NpcId npc);
  void PushNpcAwareness(const proto::NpcAwareness &awareness, proto::PlayerId you);
  void SubmitNpcAwareness(proto::NpcAwareness awareness);
  std::optional<proto::NpcAwareness> TakeNpcAwareness();

  // How hard our V is to see, game -> net (newest only); the others',
  // net -> game (latest + count). Our V's noises (queued, guest -> host);
  // a guest's noises for our NPCs (queued, host).
  void SubmitLocalStealth(const proto::Stealth &stealth);
  std::optional<proto::Stealth> TakeLocalStealth();
  void PushRemoteStealth(const proto::Stealth &stealth);
  std::optional<proto::Stealth> RemoteStealth(proto::PlayerId player, std::uint32_t &changes);
  void SubmitLocalNoise(const proto::Noise &noise);
  std::vector<proto::Noise> TakeLocalNoises();
  void PushRemoteNoise(const proto::Noise &noise);
  std::vector<proto::Noise> TakeRemoteNoises();

  // Down and out (scripts/tandem/Revive.reds): our V's life, game -> net
  // (newest only); every other player in the session with its life (up
  // unless it said otherwise), net -> game (a snapshot + a count of
  // changes); revives, ours to send and those for our V (queued); how many
  // times the session was lost (no V up).
  void SubmitLocalVitals(const proto::Vitals &vitals);
  std::optional<proto::Vitals> TakeLocalVitals();
  void SetOthersLives(std::vector<proto::Vitals> others);
  std::vector<proto::Vitals> OthersLives(std::uint32_t &changes);
  void SubmitRevive(const proto::Revive &revive);
  std::vector<proto::Revive> TakeLocalRevives();
  void PushRevive(const proto::Revive &revive);
  std::vector<proto::Revive> TakeRevives();
  void NoteSessionLost();
  std::uint32_t SessionLosses();

  // Quickhacks: messages for the session, game -> net (queued); what the
  // session says about them, net -> game (queued).
  void SubmitHack(const proto::Message &message);
  std::vector<proto::Message> TakeHackOutbox();
  void PushHackEvent(const HackEvent &event);
  std::vector<HackEvent> TakeHackEvents();
  // NPC animation (scripts/tandem/NpcAnims.reds). Host, game -> net: states
  // that changed (batched into NpcAnims in the NPC outbox, in order with the
  // spawns) and shots. Guest, net -> game: both queued for the scripts.
  void SubmitNpcAnim(const proto::NpcAnim &anim);
  void SubmitNpcShot(const proto::NpcShot &shot);
  // Host, game -> net: a shared NPC's hit reaction, in order with the rest
  // of the NPC outbox (presence.hpp keeps the guests' side).
  void SubmitNpcReaction(const proto::NpcReaction &reaction);
  void PushNpcAnims(const proto::NpcAnims &anims);
  std::vector<proto::NpcAnim> TakeNpcAnims();
  void PushNpcShot(const proto::NpcShot &shot);
  std::vector<proto::NpcShot> TakeNpcShots();

  // Grapples, takedowns, carried bodies: requests (guest) and holds and
  // cues (host) go out with the NPC outbox; answers to one player separately.
  void SubmitNpcAction(const proto::NpcAction &action);                                  // guest
  void SubmitNpcHold(const proto::NpcHold &hold);                                        // host
  void SubmitNpcCue(const proto::NpcCue &cue);                                           // host
  void SubmitNpcActionReply(proto::PlayerId player, const proto::NpcActionReply &reply); // host
  std::vector<std::pair<proto::PlayerId, proto::NpcActionReply>> TakeNpcReplies();
  void PushActionEvent(const ActionEvent &event);
  std::vector<ActionEvent> TakeActionEvents();

  // A puppet (player or NPC proxy) the controller must leave where it is:
  // something else places it (a grapple workspot, a carrier's shoulder).
  // Game thread writes, the movement controller reads.
  void HoldPuppet(proto::PlayerId player, bool hold);
  bool IsPuppetHeld(proto::PlayerId player) const;
  // A puppet's controller, every frame: whether something else places it
  // (seated in a car, on a train or riding along, held), in one go.
  struct Placement {
    bool seated{}, riding{}, held{};
  };
  Placement PlacementOf(proto::PlayerId player) const;
  // Loot (scripts/tandem/Loot.reds): our drops and pickup claims (WorldItem,
  // WorldItemTaken), game -> net; world items appearing and stacks taken,
  // net -> game. Both queued, bounded.
  void SubmitLoot(proto::Message message);
  std::vector<proto::Message> TakeLootOutbox();
  void PushLoot(proto::Message message);
  std::vector<proto::Message> TakeLoot();

  // Devices (scripts/tandem/Devices.reds). Game -> net: what a device of
  // ours is now (host: DeviceState), what our V made one (guest: DeviceUse),
  // one that came into our world (guest: DeviceState, kDeviceAsk). Net ->
  // game: the host's word (DeviceState), a guest's question (DeviceState,
  // kDeviceAsk) or request (DeviceUse), the record sent again (DeviceSet).
  // Both queued, bounded.
  void SubmitDevice(proto::Message message);
  std::vector<proto::Message> TakeDeviceOutbox();
  void PushDevice(proto::Message message);
  std::vector<proto::Message> TakeDevices();

  // Breach Protocol (scripts/tandem/Breach.reds). Game -> net: our V's
  // breach (proto::Breach). Net -> game: another player's breach to apply
  // (Breach), the host's answer to ours (BreachResult). Both bounded.
  void SubmitBreach(proto::Breach breach);
  std::vector<proto::Breach> TakeBreachOutbox();
  void PushBreach(proto::Message message);
  std::vector<proto::Message> TakeBreaches();

  // The world's objects (scripts/tandem/Objects.reds): glass and breakable
  // scenery (proto::Fracture), a device's fire (proto::DeviceFire). Game ->
  // net: ours. Net -> game: the others'. Both bounded.
  void SubmitObject(proto::Message message);
  std::vector<proto::Message> TakeObjectOutbox();
  void PushObject(proto::Message message);
  std::vector<proto::Message> TakeObjects();

  // Our player id while in a session (0: none), set by the net thread.
  void SetYou(proto::PlayerId you) { m_you.store(you); }
  proto::PlayerId You() const { return m_you.load(); }

  // Consistency (docs/architecture/consistency.md). Net thread -> game thread: the NPCs
  // the host shares as our session's record has them (guest: what our game
  // must show as proxies, no more, no less) and the players in the session
  // (roster), so scripts can find orphans and what is missing.
  void SetShared(const std::map<proto::NpcId, proto::NpcSpawn> &npcs,
                 const std::map<proto::PlayerId, proto::PlayerInfo> &players);
  bool IsNpcShared(proto::NpcId npc);
  std::vector<proto::NpcSpawn> SharedNpcs();
  bool InRoster(proto::PlayerId player);
  std::vector<proto::PlayerInfo> RosterPlayers();
  // Game -> net thread: our world was rebuilt around our V (proto::kEpoch*:
  // a fast travel the scripts saw), newest only.
  void RequestEpoch(std::uint8_t reason);
  std::optional<std::uint8_t> TakeEpochRequest();
  // Net thread: a session starts or ends. What was queued for or from the
  // last one goes: its NPC ids, hits and uploads mean nothing in the next
  // (a restarted host numbers its NPCs from 1 again), and NPC tracks go so
  // the next session's states are not refused as older.
  void ResetSession();
  // Events dropped because a queue was full (the game was not taking them,
  // e.g. behind a long loading screen); the scripts' checks then put the
  // proxies right. Read and cleared by the net thread.
  std::uint32_t TakeOverflows();

  // Frame cost (perf.hpp): the deepest each queue was when taken, since the
  // last report (and reset); a report asked for (tools/game.sh cmd perf),
  // said by the net thread with the traffic, or at once when solo.
  static constexpr std::size_t kQueues = 15;
  std::string QueueReport();
  void RequestPerfReport();
  bool TakePerfReport() { return m_perfReport.exchange(false); }

  // Fairness (tandem/lagcomp.hpp), host: is a guest's V (`puppetAt`, its
  // puppet now) within `limit` of our NPC (`npcAt`, now), as the guest saw
  // them when it asked? Rewinds both by the guest's round trip and view
  // delay, bounded. Game thread.
  lagcomp::Reach ReachOf(proto::PlayerId player, proto::NpcId npc, const proto::Vec3 &puppetAt,
                         const proto::Vec3 &npcAt, float limit);
  // Quest interactions (tandem/interact.hpp), host: could guest `player`'s
  // V reach our object at `object` when it asked (`claimed` where it says it
  // stood, at `askedUs`; the question came at `arrivalUs`)? Game thread.
  interact::Reach ReachOfObject(proto::PlayerId player, const proto::Vec3 &claimed,
                                const proto::Vec3 &object, std::uint64_t askedUs,
                                std::uint64_t arrivalUs);

  // Net thread writes remote states; the animation thread and scripts read views.
  void PushRemote(const proto::PlayerState &state);
  void ForgetRemote(proto::PlayerId player);
  sync::View ViewOf(proto::PlayerId player);
  std::optional<sync::TrackStats> StatsOf(proto::PlayerId player);

  // The session clock, set by the net thread (host clock = steady clock + offset).
  void SetClockOffset(std::int64_t offsetUs) { m_clockOffsetUs.store(offsetUs); }
  // (test) remote vehicles drawn at a smoothed moment (sync::RenderClock).
  void SetVehicleSmoothing(bool on) { m_vehicleSmoothing.store(on); }
  std::uint64_t HostNowUs() const;

  // Watchdog: the game thread stamps each frame (TakeEvents, every tick);
  // the net thread reports a game thread that went silent. Under Proton a
  // crash looks like a freeze (the crash handler waits): this says so in
  // the log, for tools/game.sh and the scenarios.
  void NoteGameFrame() { m_lastFrameUs.store(net::NowUs(), std::memory_order_relaxed); }
  std::uint64_t LastGameFrameUs() const { return m_lastFrameUs.load(std::memory_order_relaxed); }

  // Puppets: which entity draws which player. Read lock-free on the animation
  // thread (by entity hash only), written on the game thread.
  // 128: the other players and the NPC proxies of the 100 m share (64 an
  // owner; 64 slots were full on a guest at a Kabuki crossing, 2026-11-23).
  static constexpr std::size_t kMaxPuppets = 128;
  void RegisterPuppet(proto::PlayerId player, std::uint64_t entityHash);
  void UnregisterPuppet(proto::PlayerId player);
  proto::PlayerId PuppetPlayer(std::uint64_t entityHash) const;
  // The animation thread saw a puppet whose movement needs our controller.
  void RequestController(std::uint64_t entityHash);
  bool TakeControllerRequest(proto::PlayerId player);
  // Animation thread -> game thread: the controller could not bring this
  // puppet's body to its target (a ledge, a refused move): teleport it there.
  void RequestSnap(proto::PlayerId player);
  bool TakeSnap(proto::PlayerId player);

private:
  struct PuppetSlot {
    std::atomic<std::uint64_t> entity{0};
    std::atomic<proto::PlayerId> player{0};
    std::atomic<bool> wantsController{false};
  };

  mutable std::mutex m_lock; // session lifecycle, local outbox, events
  // Game thread: settings saved during a session, applied as it stops.
  std::vector<std::pair<std::string, std::string>> m_pendingSettings;
  bool m_nameAsked{false};
  std::unique_ptr<NetThread> m_net;
  SessionChoice m_choice; // m_lock
  SessionStatus m_status; // m_lock
  mutable std::mutex m_recentLock;
  mutable std::mutex m_compatLock;
  std::vector<std::string> m_differences; // m_compatLock
  proto::Body m_body{};                   // m_lock: the last Start's
  std::string m_relayCode, m_relayStatus; // m_lock
  RelayView m_relayView;                  // m_lock
  std::optional<proto::PlayerState> m_local;
  std::vector<ScriptEvent> m_events;

  std::optional<proto::PoseState> m_localPose;                                       // under m_lock
  std::optional<proto::Appearance> m_localAppearance;                                // under m_lock
  std::optional<proto::Held> m_localHeld;                                            // under m_lock
  std::vector<proto::Shot> m_localShots;                                             // under m_lock
  std::vector<consistency::Timed<proto::Shot>> m_remoteShots;                        // under m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Held>> m_held;           // m_lock
  std::optional<proto::Seat> m_localSeat;                                            // m_lock
  std::optional<proto::VehicleState> m_localVehicle;                                 // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Seat>> m_seats;          // m_lock
  std::map<proto::PlayerId, sync::VehicleTrack> m_vehicles;                          // m_tracksLock
  std::optional<proto::Transit> m_localTransit;                                      // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Transit>> m_transits;    // m_lock
  std::vector<proto::VehicleRadio> m_localRadios;                                    // m_lock
  std::uint64_t m_localTrack{}, m_localTrackStartUs{};                               // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::VehicleRadio>> m_radios; // m_lock
  std::optional<proto::World> m_localWorld;                                          // m_lock
  std::optional<proto::World> m_hostWorld;                                           // m_lock
  std::uint32_t m_hostWorldChanges{};                                                // m_lock
  std::vector<proto::Message> m_npcOutbox;                                           // m_lock
  std::optional<proto::NpcStates> m_npcStates;                                       // m_lock
  std::vector<proto::Message> m_streetOutbox;                                        // m_lock
  std::optional<proto::TrafficStates> m_trafficStates;                               // m_lock
  std::vector<StreetEvent> m_streetEvents;                                           // m_lock
  std::vector<proto::TrafficLights> m_lightsIn;                                      // m_lock
  mutable std::mutex m_lightsLock;               // a light's hook: never held across an engine call
  std::vector<proto::TrafficLight> m_lightsSaid; // m_lightsLock
  std::vector<std::uint64_t> m_lightsFollowed; // sorted                             // m_lightsLock
  std::map<proto::TrafficId, proto::TrafficSpawn> m_sharedTraffic;             // m_lock
  std::map<proto::TrafficId, proto::TrafficStatus> m_trafficStatus;            // m_lock
  std::set<proto::TrafficId> m_trafficShown;                                   // m_lock
  std::map<proto::TrafficId, proto::PlayerId> m_trafficOwners;                 // m_lock
  std::optional<proto::Heat> m_localHeat;                                      // m_lock
  std::map<proto::PlayerId, proto::Heat> m_heat;                               // m_lock
  traffic::Budget m_trafficBudget;                                             // game thread
  std::map<proto::TrafficId, sync::VehicleTrack> m_trafficTracks;              // m_tracksLock
  std::vector<NpcEvent> m_npcEvents;                                           // m_lock
  std::vector<proto::Message> m_wheelsOutbox;                                  // m_lock
  std::vector<WheelsEvent> m_wheelsEvents;                                     // m_lock
  std::map<proto::VehicleRef, proto::VehicleDamage> m_ownDamage;               // m_lock
  std::map<proto::VehicleRef, proto::VehicleDamage> m_vehicleDamage;           // m_lock
  std::map<proto::VehicleRef, proto::Gunner> m_gunners;                        // m_lock
  std::map<proto::VehicleRef, proto::GunnerAim> m_aims;                        // m_lock
  std::map<proto::PlayerId, proto::VehicleCombat> m_seatCombat;                // m_lock
  vehiclecombat::BumpLedger m_bumps;                                           // m_lock
  std::vector<consistency::Timed<proto::PlayerHit>> m_playerHits;              // m_lock
  std::vector<proto::Message> m_hackOutbox;                                    // m_lock
  std::vector<HackEvent> m_hackEvents;                                         // m_lock
  std::vector<std::pair<proto::PlayerId, proto::NpcActionReply>> m_npcReplies; // m_lock
  std::vector<ActionEvent> m_actionEvents;                                     // m_lock
  mutable std::mutex m_heldLock;
  std::set<proto::PlayerId> m_heldPuppets;                   // m_heldLock
  std::vector<proto::Message> m_lootOutbox, m_lootInbox;     // m_lock
  std::vector<proto::Message> m_deviceOutbox, m_deviceInbox; // m_lock
  std::vector<proto::Breach> m_breachOutbox;                 // m_lock
  std::vector<proto::Message> m_breachInbox;                 // m_lock
  std::vector<proto::Message> m_objectOutbox, m_objectInbox; // m_lock
  std::atomic<proto::PlayerId> m_you{0};
  std::vector<proto::NpcAnim> m_npcAnimsIn;                                             // m_lock
  std::vector<proto::NpcShot> m_npcShotsIn;                                             // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Appearance>> m_appearances; // m_lock
  std::atomic<proto::PlayerId> m_poseLoopback{0};
  std::atomic<std::int32_t> m_expansion{0}; // compat::Expansion
  std::atomic_bool m_coreRefused{false};
  std::uint32_t m_loopSeq{};
  mutable std::mutex m_posesLock;
  std::map<proto::PlayerId, pose::PoseTrack> m_poses;
  std::map<proto::NpcId, pose::PoseTrack> m_actorPoses; // m_posesLock
  std::map<proto::NpcId, proto::PoseState> m_actorOut;  // m_posesLock
  std::uint32_t m_actorPosesIn{};                       // m_posesLock
  std::map<std::uint64_t, RigSample> m_rigOut;          // m_posesLock, by npc and part
  std::map<std::uint64_t, rig::Track> m_rigTracks;      // m_posesLock
  std::uint32_t m_rigPosesIn{};                         // m_posesLock
  std::map<std::uint64_t, rig::TrackSet> m_trackSets;   // m_posesLock

  mutable std::mutex m_reachLock;
  lagcomp::Book m_reach; // m_reachLock: guests' Vs by player id, our NPCs by kNpcPuppetBase + id

  mutable std::mutex m_tracksLock;
  std::map<proto::PlayerId, sync::Track> m_tracks;

  std::atomic<std::int64_t> m_clockOffsetUs{0};
  std::atomic_bool m_vehicleSmoothing{true}; // sync::RenderClock (test: off)
  std::atomic<std::uint64_t> m_lastFrameUs{0};
  // Each NPC's word with when it came (host clock): every owner lists only
  // its own NPCs, so an entry ends by going stale, not by being left out.
  std::map<proto::NpcId, std::pair<std::uint8_t, std::uint64_t>> m_npcDetected;  // m_lock
  proto::PlayerId m_detectedFor{};                                               // m_lock
  std::map<proto::NpcId, std::pair<float, std::uint64_t>> m_awarenessOfUs;       // m_lock
  std::optional<proto::NpcAwareness> m_localAwareness;                           // m_lock
  std::optional<proto::Stealth> m_localStealth;                                  // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Stealth>> m_stealth; // m_lock
  std::vector<proto::Noise> m_localNoises;                                       // m_lock
  presence::NoiseGate m_noiseGate;                                               // m_lock
  std::vector<consistency::Timed<proto::Noise>> m_remoteNoises;                  // m_lock
  // Consistency (m_lock): the session's record for the scripts' checks.
  std::map<proto::NpcId, proto::NpcSpawn> m_sharedNpcs;
  std::map<proto::PlayerId, proto::PlayerInfo> m_roster;
  std::optional<std::uint8_t> m_epochRequest;
  std::uint32_t m_overflows{};
  std::array<std::uint32_t, kQueues> m_queueMax{};      // m_lock
  void NoteDepth(std::size_t queue, std::size_t depth); // under m_lock
  std::atomic<bool> m_perfReport{false};
  std::optional<bool> m_localPaused;                                   // m_lock
  std::vector<proto::PlayerId> m_remotePaused;                         // m_lock
  std::uint32_t m_remotePausedChanges{};                               // m_lock
  std::vector<proto::NpcShotAt> m_shotsAt;                             // m_lock
  std::vector<proto::ShotHit> m_shotHits;                              // m_lock
  std::map<proto::PlayerId, std::string> m_names;                      // m_lock
  std::optional<proto::TimeEffect> m_localTime;                        // m_lock
  std::map<proto::PlayerId, proto::TimeEffect> m_remoteTimes;          // m_lock
  std::uint32_t m_remoteTimeChanges{};                                 // m_lock
  sync::TimeBubbles m_bubbles;                                         // game thread
  elsewhere::Tracker m_away;                                           // game thread
  std::map<proto::Body, std::vector<proto::AppearanceKey>> m_standIns; // m_lock
  void NoteStandIn(const proto::Appearance &appearance);               // under m_lock
  elsewhere::PoseHistory m_poseHistory;                                // m_lock
  std::optional<proto::PoseState> m_poseHold;                          // m_lock
  std::uint64_t m_poseHoldSentUs{};                                    // m_lock
  std::optional<proto::Elsewhere> m_localElsewhere;                    // m_lock
  std::map<proto::PlayerId, proto::Elsewhere> m_remoteElsewhere;       // m_lock
  std::uint32_t m_remoteElsewhereChanges{};                            // m_lock
  std::vector<proto::Message> m_regroupOutbox;                         // m_lock
  std::vector<RegroupEvent> m_regroupEvents;                           // m_lock
  std::uint32_t m_regroupOffers{};                                     // m_lock
  std::optional<proto::Vitals> m_localVitals;                          // m_lock
  std::vector<proto::Vitals> m_othersLives;                            // m_lock
  std::uint32_t m_othersLivesChanges{};                                // m_lock
  std::vector<proto::Revive> m_localRevives, m_revives;                // m_lock
  std::uint32_t m_sessionLosses{};                                     // m_lock
  std::optional<std::string> m_recordingRequest;                       // m_lock
  std::mutex m_snapsLock;
  std::set<proto::PlayerId> m_snaps; // m_snapsLock
  std::array<PuppetSlot, kMaxPuppets> m_puppets;
};
} // namespace tandem::plugin
