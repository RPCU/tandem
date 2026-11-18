#pragma once

// State shared by the game thread (scripts), the network thread and the
// engine's animation thread. Each piece says which threads touch it.

#include "config.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"
#include "tandem/protocol.hpp"
#include "tandem/session.hpp"
#include "tandem/vehicle.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tandem::plugin {
class NetThread;

// What scripts learn from the session (game thread reads, net thread writes).
// A host NPC for the guest's scripts: spawn a proxy, or remove it.
// Host: a guest hit one of its NPCs. Guest: an NPC's health changed.
struct NpcEvent {
  // Effect (host): a guest's hit put the status effect spawn.record on it.
  enum class Kind : std::int32_t { Spawn = 1, Gone = 2, Hit = 3, Health = 4, Effect = 5 };
  Kind kind{};
  proto::NpcSpawn spawn; // npc set for every kind
  std::uint8_t reason{};
  proto::PlayerId player{}; // Hit, Effect: who; Gone: the killer
  float amount{};           // Hit: damage (health points); Health: fraction 0..1
  std::uint8_t flags{};     // Hit: proto::kHit*
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
  };
  Kind kind{};
  proto::NpcId npc{};
  proto::PlayerId player{}; // the uploader, the effect's V, the hacker
  std::uint64_t action{};   // ObjectAction record (Status: the status effect record)
  std::int32_t state{};     // State: proto::HackStep; Status: applied; Car: proto::CarHack
  std::int32_t flags{};     // Upload: proto::kUpload*; PlayerHack: proto::kPlayerHack*
  float seconds{};          // upload time; Car: how long it lasts
};

// Fast travel together (scripts/tandem/Travel.reds): an offer to come to
// another player, or an answer to ours, for the scripts.
struct RegroupEvent {
  enum class Kind : std::int32_t { Offer = 1, Answer = 2 };
  Kind kind{};
  proto::Regroup offer;        // Offer
  proto::RegroupAnswer answer; // Answer
};

// NPC proxies use the player-puppet tracks and controller under these ids.
inline constexpr proto::PlayerId kNpcPuppetBase = 0x10000;

struct ScriptEvent {
  enum class Kind : std::int32_t {
    PlayerJoined = 1,
    PlayerLeft = 2,
    SessionEnded = 3,
    InSession = 4,
  };
  Kind kind{};
  proto::PlayerId player{};
  std::string name;
  proto::Body body{};
};

class Runtime {
public:
  static Runtime &Get();

  Config config;

  // Game thread: the local V exists; start the session once.
  void Start(proto::Body body);
  void Stop();
  bool Started() const;

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
  proto::PlayerId PoseLoopback() const { return m_poseLoopback.load(); }

  // Net thread writes remote poses; animation job threads read them, drawn
  // with the same delay as the player's position.
  void PushRemotePose(const proto::PoseState &pose);
  bool PoseOf(proto::PlayerId player, pose::Pose &out);
  std::uint32_t PosesReceived(proto::PlayerId player);

  // Game thread -> net thread: our V's look, newest only (the endpoint keeps it
  // and sends it again to whoever joins).
  void SubmitLocalAppearance(proto::Appearance appearance);
  std::optional<proto::Appearance> TakeLocalAppearance();
  // Net thread -> game thread: each player's latest look; `changes` counts
  // the looks received for them (0: none yet).
  void PushRemoteAppearance(const proto::Appearance &appearance);
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

  // The pause menu (docs/ARCHITECTURE.md): ours open or closed, game -> net
  // (newest only); the other players in theirs, net -> game (latest + a
  // count of changes, 0: none yet). Names, from the roster, for the notice.
  void SubmitLocalPaused(bool paused);
  std::optional<bool> TakeLocalPaused();
  void SetRemotePaused(std::vector<proto::PlayerId> players);
  std::vector<proto::PlayerId> RemotePaused(std::uint32_t &changes);
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
  // Host: our NPC hit a guest's puppet, game -> net (queued with the NPCs').
  // Guest: the host's NPCs hit our V, net -> game (queued).
  void SubmitPlayerHit(const proto::PlayerHit &hit);
  void PushPlayerHit(const proto::PlayerHit &hit);
  std::vector<proto::PlayerHit> TakePlayerHits();
  // Guest: which players the host's NPC has detected (bit player - 1).
  std::uint8_t NpcDetected(proto::NpcId npc);
  void SubmitNpcStates(const proto::NpcStates &states);
  std::vector<proto::Message> TakeNpcOutbox();
  std::optional<proto::NpcStates> TakeNpcStates();
  void PushNpcEvent(const NpcEvent &event);
  std::vector<NpcEvent> TakeNpcEvents();
  // Guest: `you` is our player id (whose detection counts for our V).
  void PushNpcStates(const proto::NpcStates &states, proto::PlayerId you);

  // Each player is detected on their own (docs/ARCHITECTURE.md). Guest: how
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

  // Quickhacks: messages for the session, game -> net (queued); what the
  // session says about them, net -> game (queued).
  void SubmitHack(const proto::Message &message);
  std::vector<proto::Message> TakeHackOutbox();
  void PushHackEvent(const HackEvent &event);
  std::vector<HackEvent> TakeHackEvents();

  // Net thread writes remote states; the animation thread and scripts read views.
  void PushRemote(const proto::PlayerState &state);
  void ForgetRemote(proto::PlayerId player);
  sync::View ViewOf(proto::PlayerId player);
  std::optional<sync::TrackStats> StatsOf(proto::PlayerId player);

  // The session clock, set by the net thread (host clock = steady clock + offset).
  void SetClockOffset(std::int64_t offsetUs) { m_clockOffsetUs.store(offsetUs); }
  std::uint64_t HostNowUs() const;

  // Watchdog: the game thread stamps each frame (TakeEvents, every tick);
  // the net thread reports a game thread that went silent. Under Proton a
  // crash looks like a freeze (the crash handler waits): this says so in
  // the log, for tools/game.sh and the scenarios.
  void NoteGameFrame() { m_lastFrameUs.store(net::NowUs(), std::memory_order_relaxed); }
  std::uint64_t LastGameFrameUs() const { return m_lastFrameUs.load(std::memory_order_relaxed); }

  // Puppets: which entity draws which player. Read lock-free on the animation
  // thread (by entity hash only), written on the game thread.
  static constexpr std::size_t kMaxPuppets = 64;
  void RegisterPuppet(proto::PlayerId player, std::uint64_t entityHash);
  void UnregisterPuppet(proto::PlayerId player);
  proto::PlayerId PuppetPlayer(std::uint64_t entityHash) const;
  // The animation thread saw a puppet whose movement needs our controller.
  void RequestController(std::uint64_t entityHash);
  bool TakeControllerRequest(proto::PlayerId player);

private:
  struct PuppetSlot {
    std::atomic<std::uint64_t> entity{0};
    std::atomic<proto::PlayerId> player{0};
    std::atomic<bool> wantsController{false};
  };

  mutable std::mutex m_lock; // session lifecycle, local outbox, events
  std::unique_ptr<NetThread> m_net;
  std::optional<proto::PlayerState> m_local;
  std::vector<ScriptEvent> m_events;

  std::optional<proto::PoseState> m_localPose;                                       // under m_lock
  std::optional<proto::Appearance> m_localAppearance;                                // under m_lock
  std::optional<proto::Held> m_localHeld;                                            // under m_lock
  std::vector<proto::Shot> m_localShots, m_remoteShots;                              // under m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Held>> m_held;           // m_lock
  std::optional<proto::Seat> m_localSeat;                                            // m_lock
  std::optional<proto::VehicleState> m_localVehicle;                                 // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Seat>> m_seats;          // m_lock
  std::map<proto::PlayerId, sync::VehicleTrack> m_vehicles;                          // m_tracksLock
  std::vector<proto::VehicleRadio> m_localRadios;                                    // m_lock
  std::uint64_t m_localTrack{}, m_localTrackStartUs{};                               // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::VehicleRadio>> m_radios; // m_lock
  std::optional<proto::World> m_localWorld;                                          // m_lock
  std::optional<proto::World> m_hostWorld;                                           // m_lock
  std::uint32_t m_hostWorldChanges{};                                                // m_lock
  std::vector<proto::Message> m_npcOutbox;                                           // m_lock
  std::optional<proto::NpcStates> m_npcStates;                                       // m_lock
  std::vector<NpcEvent> m_npcEvents;                                                 // m_lock
  std::vector<proto::PlayerHit> m_playerHits;                                        // m_lock
  std::vector<proto::Message> m_hackOutbox;                                          // m_lock
  std::vector<HackEvent> m_hackEvents;                                               // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Appearance>> m_appearances; // m_lock
  std::atomic<proto::PlayerId> m_poseLoopback{0};
  std::uint32_t m_loopSeq{};
  mutable std::mutex m_posesLock;
  std::map<proto::PlayerId, pose::PoseTrack> m_poses;

  mutable std::mutex m_tracksLock;
  std::map<proto::PlayerId, sync::Track> m_tracks;

  std::atomic<std::int64_t> m_clockOffsetUs{0};
  std::atomic<std::uint64_t> m_lastFrameUs{0};
  std::map<proto::NpcId, std::uint8_t> m_npcDetected;  // under m_lock
  std::int32_t m_detectingUs{};                        // m_lock
  std::uint64_t m_npcStatesAtUs{};                     // m_lock (host clock)
  std::map<proto::NpcId, float> m_awarenessOfUs;       // m_lock
  std::uint64_t m_awarenessAtUs{};                     // m_lock (host clock)
  std::optional<proto::NpcAwareness> m_localAwareness; // m_lock
  std::optional<proto::Stealth> m_localStealth;        // m_lock
  std::map<proto::PlayerId, std::pair<std::uint32_t, proto::Stealth>> m_stealth; // m_lock
  std::vector<proto::Noise> m_localNoises, m_remoteNoises;                       // m_lock
  std::optional<bool> m_localPaused;                                             // m_lock
  std::vector<proto::PlayerId> m_remotePaused;                                   // m_lock
  std::uint32_t m_remotePausedChanges{};                                         // m_lock
  std::map<proto::PlayerId, std::string> m_names;                                // m_lock
  std::vector<proto::Message> m_regroupOutbox;                                   // m_lock
  std::vector<RegroupEvent> m_regroupEvents;                                     // m_lock
  std::uint32_t m_regroupOffers{};                                               // m_lock
  std::array<PuppetSlot, kMaxPuppets> m_puppets;
};
} // namespace tandem::plugin
