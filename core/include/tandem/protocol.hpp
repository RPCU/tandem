#pragma once

// The wire protocol. Every message is one alternative of `Message`; the
// variant index is the message id, so new messages are only ever appended.
// Bump kProtocolVersion on any change to an existing message.

#include "tandem/closed_variant.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace tandem::proto {
inline constexpr std::uint32_t kMagic = 0x4D444E54; // "TNDM"
// 7: Hello.flags (Phantom Liberty), NpcCueKind::OutOfSight, kTimeScanning
// in kTimeBubbleAbilities (a TimeEffect means more), and the compact wire
// forms of the netcode (PoseFrame, NpcMotion, PlayerMotion, VehicleMotion);
// the record hints of Held, Appearance, Seat and NpcSpawn (mod-compat.md),
// added before protocol 7 left the developers' machines. 9: a Waypoint may
// be a tracked quest objective (WaypointKind::Objective). 10: WorldPrefabs
// and StoryGates appended (a game of 9 cannot read them).
// 11: NpcHealth.flags (kNpcProtected); WeakSpotHit and HackTraced appended.
// 12: StoryState's kStoryNoFastTravel (a game of 11 would let a guest's V
// respawn out of the host's locked quest area); TurretBusy appended.
inline constexpr std::uint16_t kProtocolVersion = 12;
inline constexpr std::uint16_t kDefaultPort = 27450;

// ENet channels. State is unreliable and sequenced (newest wins), events are
// reliable and ordered, bulk carries large reliable transfers (world snapshot)
// so they never hold up events.
enum class Channel : std::uint8_t {
  State = 0,
  Event = 1,
  Bulk = 2,
};
inline constexpr std::size_t kChannelCount = 3;

using PlayerId = std::uint32_t;
inline constexpr PlayerId kNoPlayer = 0;
// Player ids are 1 (the host) to kMaxPlayerId: the host hands guests 2..8
// in turn, skipping the ones in use, so every id is one bit of a byte
// (NpcEntry::detected) however many players came and went.
inline constexpr PlayerId kMaxPlayerId = 8;

// A V's body type: it picks the puppet's rig, so it is known before spawning.
enum class Body : std::uint8_t {
  Unknown = 0,
  Female = 1,
  Male = 2,
};

struct Vec3 {
  float x{}, y{}, z{};
  friend bool operator==(const Vec3 &, const Vec3 &) = default;
};

struct Quat {
  float i{}, j{}, k{}, r{1.f};
  friend bool operator==(const Quat &, const Quat &) = default;
};

// Hello::flags. The game has the Phantom Liberty expansion installed and
// enabled: tandem needs it on every game (tandem/compat.hpp), so a host
// refuses a guest without it.
inline constexpr std::uint8_t kHelloExpansion = 1 << 0;

// Guest -> host, first message. Its first two fields never move: a host of
// any version reads them from a Hello it cannot decode (PeekHelloVersion)
// and refuses it in words.
struct Hello {
  std::uint32_t magic{kMagic};
  std::uint16_t version{kProtocolVersion};
  std::string name;
  // The guest's tandem build (compat::Build, "0.2.0+2fa03ca"): a host that
  // refuses the protocol version can say which release that was.
  std::string gameVersion;
  Body body{};
  std::uint8_t flags{kHelloExpansion}; // kHello*
  friend bool operator==(const Hello &, const Hello &) = default;
};

// Host -> guest, the answer to Hello.
struct Welcome {
  PlayerId you{};
  PlayerId host{};
  std::uint64_t hostTimeUs{};
  friend bool operator==(const Welcome &, const Welcome &) = default;
};

// Host -> guest, when Hello is refused.
struct Refused {
  std::string reason;
  friend bool operator==(const Refused &, const Refused &) = default;
};

// Clock sync: the guest sends Ping, the host answers with its own clock.
struct Ping {
  std::uint64_t sentUs{};
  friend bool operator==(const Ping &, const Ping &) = default;
};

struct Pong {
  std::uint64_t pingSentUs{};
  std::uint64_t hostTimeUs{};
  friend bool operator==(const Pong &, const Pong &) = default;
};

// Who is in the session, sent by the host whenever it changes.
struct PlayerInfo {
  PlayerId id{};
  std::string name;
  Body body{};
  friend bool operator==(const PlayerInfo &, const PlayerInfo &) = default;
};

struct Roster {
  std::vector<PlayerInfo> players;
  friend bool operator==(const Roster &, const Roster &) = default;
};

// A player's own V, owned and sent by that player's game (State channel).
// timeUs is on the host's clock, so every game can interpolate on one timeline.
struct PlayerState {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 position;
  Quat orientation;
  Vec3 velocity;
  std::uint8_t stance{}; // locomotion state, see the plugin
  std::uint16_t flags{}; // how it moves: tandem/movement.hpp (bits 10-14 free)
  friend bool operator==(const PlayerState &, const PlayerState &) = default;
};

struct Bye {
  std::string reason;
  friend bool operator==(const Bye &, const Bye &) = default;
};

// A player's body pose, sent by that player's game next to PlayerState
// (State channel, ~30 Hz). Encoding in tandem/pose.hpp: the bones are the
// third-person body rig's minus Root, Trajectory and reference_joint, each a
// rotation relative to its parent in 6 bytes; the hips position is relative
// to the rig's reference hips height, in units of 1/8192.
inline constexpr std::size_t kPoseBones = 68;
inline constexpr std::size_t kPoseRotationBytes = 6;
inline constexpr float kPoseHipsScale = 8192.f;

struct PoseState {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::array<std::int16_t, 3> hips{};
  std::array<std::uint8_t, kPoseBones * kPoseRotationBytes> rotations{};
  friend bool operator==(const PoseState &, const PoseState &) = default;
};

// What kind of thing a record is, said next to its TweakDB id, so a game
// that does not have the record (a mod it lacks) shows a similar vanilla one
// (tandem/mods.hpp, docs/architecture/mod-compat.md). The game's own enum
// names, as CName hashes: the same in every copy of the game, modded or not.
struct RecordHint {
  std::uint64_t type{}; // gamedataItemType ("Wea_Handgun"), gamedataVehicleType ("Car"),
                        // gamedataNPCType ("Human")
  std::uint64_t tag{};  // an item's gamedataItemCategory ("Weapon"); an NPC's gender ("Female")
  std::uint8_t size{};  // a vehicle's seats
  friend bool operator==(const RecordHint &, const RecordHint &) = default;
};

// One part of a V's look: what the game's appearance changer takes
// (world::EntityAppearanceChangeParameter::Key): a resource path hash and a
// name hash, the same in every copy of the game.
struct AppearanceKey {
  std::uint64_t resource{};
  std::uint64_t definition{};
  friend bool operator==(const AppearanceKey &, const AppearanceKey &) = default;
};

// How a player's V looks, owned and sent by that player's game whenever it
// changes (Event channel). The host keeps the latest of each player and sends
// them to whoever joins later. `items` are the TweakDB ids of the clothes
// shown (the wardrobe's choice), worn as preview items on the puppet.
inline constexpr std::size_t kMaxAppearanceKeys = 64;
inline constexpr std::size_t kMaxAppearanceItems = 32;

struct Appearance {
  PlayerId player{};
  std::uint32_t revision{};
  Body body{};
  std::vector<AppearanceKey> look;
  std::vector<std::uint64_t> items;
  std::vector<RecordHint> hints; // items[i]'s (fewer: the rest have none)
  friend bool operator==(const Appearance &, const Appearance &) = default;
};

// What a player holds (Event channel, on change; the host keeps the latest
// for late joiners): TweakDB ids of the items in the right and left hand
// slots, 0 when empty. The puppet gets the same items in the same slots;
// the pose stream moves the hands.
struct Held {
  PlayerId player{};
  std::uint32_t revision{};
  std::uint64_t right{};
  std::uint64_t left{};
  RecordHint rightHint;
  RecordHint leftHint;
  friend bool operator==(const Held &, const Held &) = default;
};

// One weapon event of a player: a shot, a reload, a melee swing (State
// channel: a late one is worth nothing). Drawn on the puppet as the weapon's own effects (muzzle
// flash, shells, sound), with no projectile: hits are decided elsewhere.
enum class Hand : std::uint8_t { Right = 0, Left = 1 };
struct Shot {
  PlayerId player{};
  std::uint32_t seq{};
  Hand hand{};
  // gamedataFxAction (Shoot 16, SilencedShoot 17, MuzzleBrakeShoot 15,
  // EnterReload 5, ExitReload 11), or tandem's own from 200: 200 = melee swing,
  // 201 = quick melee with a ranged weapon.
  std::uint8_t effect{};
  friend bool operator==(const Shot &, const Shot &) = default;
};

// Where a player sits (Event channel, on change; the host keeps the latest
// for late joiners). `vehicle` is the vehicle's TweakDB record id, 0 when on
// foot; `seat` the slot's CName hash (seat_front_left = the driver's...);
// `owner` the player whose car it is: the sender itself when it drives its
// own car, another player when riding in theirs (a copy of it on this game).
struct Seat {
  PlayerId player{};
  std::uint32_t revision{};
  std::uint64_t vehicle{};
  std::uint64_t seat{};
  PlayerId owner{};
  RecordHint hint; // the vehicle's type and seats
  friend bool operator==(const Seat &, const Seat &) = default;
};

// The car a player drives, sent by the driver's game (State channel, 30 Hz).
// Other games teleport their copy of it to the interpolated transform. Also
// sent for a vehicle the player's game moves while the player does not
// drive it (Transit::Ride, Scene): its game runs it, so it is the owner.
struct VehicleState {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 position;
  Quat orientation;
  Vec3 velocity;
  friend bool operator==(const VehicleState &, const VehicleState &) = default;
};

// The host's world (Event channel, sent by the host only, on change and
// every few seconds; kept for late joiners). Guests follow it.
struct World {
  std::uint32_t revision{};
  std::int32_t gameTimeSeconds{}; // GameTime: seconds since day 0
  std::uint64_t weather{};        // the weather state's CName hash (0: unknown)
  bool timePaused{};
  friend bool operator==(const World &, const World &) = default;
};

// The host's NPCs (milestone 5). Sent by the host only. An NPC gets a
// session id when the host first sees it near a player; guests spawn a
// proxy of it (same record and appearance, brain off) and move it where the
// host says. Ids are never reused within a session.
using NpcId = std::uint32_t;

struct NpcSpawn { // Event channel; kept by the host for late joiners
  NpcId npc{};
  std::uint64_t record{};     // TweakDB id of its character record
  std::uint64_t appearance{}; // CName hash of its appearance
  Vec3 position;
  float yaw{};
  RecordHint hint; // its NPC type and gender
  friend bool operator==(const NpcSpawn &, const NpcSpawn &) = default;
};

struct NpcEntry {
  NpcId npc{};
  Vec3 position;
  float yaw{};
  std::uint8_t detected{}; // bit (player - 1): the NPC has detected that player
  friend bool operator==(const NpcEntry &, const NpcEntry &) = default;
};

inline constexpr std::size_t kMaxNpcsPerState = 64;

struct NpcStates { // State channel, ~10 Hz: every NPC the host shares, in one go
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::vector<NpcEntry> npcs;
  friend bool operator==(const NpcStates &, const NpcStates &) = default;
};

struct NpcGone { // Event channel
  NpcId npc{};
  std::uint8_t reason{}; // kGone*: 0 out of range or despawned, 1 dead, 2 disposed of,
                         // 3 knocked out (unconscious; still a target)
  PlayerId killer{};     // dead: the player whose hit killed it (kNoPlayer: none)
  friend bool operator==(const NpcGone &, const NpcGone &) = default;
};

// A guest's hit on one of the host's NPCs (its proxy): the guest's game
// computes the damage (its weapon, perks, the proxy's armour), the host
// applies it to the real NPC. Guest to host only, never relayed.
struct NpcHit {      // Event channel
  PlayerId player{}; // who hit: set by the host from the connection
  NpcId npc{};
  float damage{};       // health points
  std::uint8_t flags{}; // kHit*
  Vec3 position;        // where it landed and the way it went: the host's
  Vec3 direction;       // NPC reacts to it (hit animation, turning on them)
  friend bool operator==(const NpcHit &, const NpcHit &) = default;
};
inline constexpr std::uint8_t kHitWeakSpot = 1, kHitCritical = 2, kHitMelee = 4, kHitLethal = 8;
// PlayerHit and AreaHit only: the attack's own rules for the V it lands on
// (hitFlag.IgnoreDifficulty, hitFlag.CannotKillPlayer: a brawl leaves 1 HP).
inline constexpr std::uint8_t kHitIgnoreDifficulty = 16, kHitCannotKill = 32;

// An NPC's health as the host's game has it, sent when it changes. Death
// is NpcGone with reason 1.
// NpcHealth::flags. kNpcProtected: the owner's game cannot kill it now (god
// mode Immortal or Invulnerable, or its IsInvulnerable stat): a guest's hit
// that takes the last of its proxy's health is no kill (npcs.md).
inline constexpr std::uint8_t kNpcProtected = 1;
inline constexpr std::uint8_t kNpcHealthFlags = kNpcProtected;
struct NpcHealth { // Event channel; kept by the host for late joiners
  NpcId npc{};
  float health{};       // fraction of its maximum, 0..1
  std::uint8_t flags{}; // kNpc* (since protocol 9)
  friend bool operator==(const NpcHealth &, const NpcHealth &) = default;
};

// Status effects a guest's hit put on the host's NPC (its proxy): the
// guest's game decided them by its rules (application chance, its perks,
// immunities); the host puts them on the real NPC for what they do to it
// (stun, blind, panic, burning) and the others to see. Their damage ticks
// are the guest's: they come as NpcHit. Guest to host, never relayed.
inline constexpr std::size_t kMaxNpcEffects = 8;
struct NpcEffect {   // Event channel
  PlayerId player{}; // who: set by the host from the connection
  NpcId npc{};
  std::vector<std::uint64_t> effects; // TweakDB ids of status effect records
  friend bool operator==(const NpcEffect &, const NpcEffect &) = default;
};

// A player opened or closed the game's pause menu (Event channel). The
// pause menu pauses everyone: each game pauses its world while any other
// player is in theirs. A guest sends its own; the host stamps the player from
// the connection, keeps who is paused (for late joiners; a player who leaves
// is no longer paused) and relays it. Other menus never send this.
struct Pause {
  PlayerId player{};
  bool paused{};
  friend bool operator==(const Pause &, const Pause &) = default;
};

// The radio of the car a player drives, sent by the driver's game when it
// changes and when a passenger gets in (Event channel; the host keeps the
// latest of each player for late joiners). Everyone riding in that car hears
// it: a passenger's game sets its copy of the car to this. On foot each V
// hears only its own pocket radio, which is never sent.
// `station` is the game's ERadioStationList index (0..13), -1 for none;
// `track` the current song's CName hash (0: unknown). The game exposes no
// playback position, so `trackStartUs` is when the driver's game saw this
// song begin on the station, on the host clock (0: unknown, e.g. it was
// already playing when the driver tuned in).
struct VehicleRadio {
  PlayerId player{}; // the driver: set by the host from the connection
  std::uint32_t revision{};
  bool on{};
  std::int8_t station{-1};
  std::uint64_t track{};
  std::uint64_t trackStartUs{};
  friend bool operator==(const VehicleRadio &, const VehicleRadio &) = default;
};

// One of the host's NPCs hit a guest's V (its puppet on the host's game).
// The host's game ran the attacker's side of the hit (the NPC's weapon and
// stats, range, its attack); the target's side (armour, resistances, dodge,
// block and deflect, perks, difficulty, god mode, death) belongs to that
// player's game, which runs the hit on its own V with the NPC's proxy as the
// attacker. Host to that player only, never relayed to anyone else.
struct PlayerHit {   // Event channel
  PlayerId player{}; // whose V was hit
  NpcId npc{};       // the attacker as the host shares it (0: not shared)
  // Damage per type as the attacker's side left it, before the target's.
  float physical{}, thermal{}, electric{}, chemical{};
  std::uint8_t attackType{}; // gamedataAttackType (Ranged 10, Melee 7...)
  std::uint8_t flags{};      // kHit*
  float pierce{};            // the attacker's armour penetration, 0..1
  Vec3 position;             // where it landed, and the way it went
  Vec3 direction;
  std::uint64_t attack{}; // TweakDB id of the attack record (0: unknown)
  std::uint64_t weapon{}; // TweakDB id of the NPC's weapon item (0: none)
  friend bool operator==(const PlayerHit &, const PlayerHit &) = default;
};
// How far the host's NPCs are in noticing each guest: the level of the
// game's detection meter (SenseComponent.GetDetection, 0..100) as 0..255,
// for the guest's stealth meter. Sent by the host only (State channel,
// ~5 Hz, empty when nobody is being noticed). Only pairs above 0 are
// listed, never the host's own V. Who has fully detected whom (and so puts
// that player in combat) is NpcEntry::detected.
struct NpcAwarenessEntry {
  NpcId npc{};
  PlayerId player{};
  std::uint8_t level{}; // 255: detection 100 %
  friend bool operator==(const NpcAwarenessEntry &, const NpcAwarenessEntry &) = default;
};
inline constexpr std::size_t kMaxNpcAwareness = 96;

struct NpcAwareness {
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::vector<NpcAwarenessEntry> entries;
  friend bool operator==(const NpcAwareness &, const NpcAwareness &) = default;
};

// How hard a V is to see, sent by that player's game when it changes (Event
// channel; the host stamps the player, keeps the latest for late joiners
// and relays it). The host puts it on the player's puppet, where its NPCs'
// senses look: camouflaged, the puppet's visible object is off; its
// detection multiplier is the V's Visibility stat; crouched, it is seen at
// crouch height. Every game shows the camo effect on the puppet.
inline constexpr std::uint8_t kStealthCamo = 1;         // optical camo on
inline constexpr std::uint8_t kStealthCamoBlocking = 2; // fully blocking (relic: NPCs drop the V)
inline constexpr std::uint8_t kStealthCrouch = 4;       // crouched or sneaking
struct Stealth {
  PlayerId player{};
  std::uint32_t revision{};
  std::uint8_t flags{};  // kStealth*
  float visibility{1.f}; // the V's Visibility stat: NPCs' detection multiplier
  std::uint64_t camo{};  // TweakDB id of the camo status effect on the V (0: none, unknown)
  friend bool operator==(const Stealth &, const Stealth &) = default;
};

// A stimulus a guest's V gave out (a gunshot, a silenced shot, a ground
// slam, a distraction): the host's NPCs hear it from the guest's puppet,
// which the host broadcasts it from. Guest to host only, never relayed
// (State channel: a late one is worth nothing).
struct Noise {
  PlayerId player{};   // set by the host from the connection
  std::uint8_t stim{}; // gamedataStimType
  float radius{};      // metres (0: the stimulus's own)
  bool propagation{};  // StimBroadcasterComponent's propagationChange
  friend bool operator==(const Noise &, const Noise &) = default;
};

// Quickhacks (docs/architecture/combat.md, "Quickhacks from two players on one
// target"). `action` is always the TweakDB id of an ObjectAction record
// (QuickHack.*, AIQuickHack.*), `effect` that of a status effect record.

// A guest started uploading a quickhack on one of the host's NPCs (its
// proxy): its game runs the panel, the RAM cost, its cooldown and queue and
// the upload bar; the effects it lands come as NpcEffect as for a hit. The
// host runs the upload on the real NPC (being hacked, trace) and its
// reaction to the hack. Guest to host, never relayed.
inline constexpr std::uint8_t kUploadTraceable = 1; // the hack reveals its netrunner
struct NpcHack {                                    // Event channel
  PlayerId player{};                                // who: set by the host from the connection
  NpcId npc{};
  std::uint64_t action{};
  std::uint32_t uploadMs{}; // the upload time the guest's game computed (its perks)
  std::uint8_t flags{};     // kUpload*
  friend bool operator==(const NpcHack &, const NpcHack &) = default;
};

// What is being uploaded to a host NPC, by whom (host to everyone, the
// uploader included: its game learns the host's order). Each game shows the
// others' uploads on its proxy.
enum class HackStep : std::uint8_t { Started = 0, Done = 1, Failed = 2 };
struct NpcHackState { // Event channel
  NpcId npc{};
  PlayerId player{}; // the uploader (the host's own V too)
  std::uint64_t action{};
  HackStep step{};
  std::uint32_t uploadMs{};
  friend bool operator==(const NpcHackState &, const NpcHackState &) = default;
};

// A quickhack's status effect came onto, or left, a host NPC (host to
// everyone). Guests put it on their proxy, so its look and the game's own
// "already affected" lockouts in their quickhack menu are true; the game
// whose V put it there (`player`) has it already.
struct NpcStatus { // Event channel
  NpcId npc{};
  PlayerId player{}; // whose hack it is (kNoPlayer: an NPC's, or unknown)
  std::uint64_t effect{};
  bool applied{}; // false: removed
  friend bool operator==(const NpcStatus &, const NpcStatus &) = default;
};

// One of the host's NPC netrunners hacks a guest's V (host to that guest
// only). On the host the hack would land on the puppet; the guest's game
// runs it on its own V instead, with its proxy of the netrunner as the
// hacker: its ICE and cyberdeck defences, the upload bar, the effect.
inline constexpr std::uint8_t kPlayerHackReveal = 1;      // trace: reveals the V's position
inline constexpr std::uint8_t kPlayerHackDirectional = 2; // show the attack's direction
inline constexpr std::uint8_t kPlayerHackSkipBeingHacked = 4;
struct PlayerHack {  // Event channel
  PlayerId player{}; // the V hacked
  NpcId npc{};       // the netrunner
  std::uint64_t action{};
  std::uint8_t flags{}; // kPlayerHack*
  friend bool operator==(const PlayerHack &, const PlayerHack &) = default;
};

// A V quickhacked the car another V drives (its copy in the hacker's game).
// The driver's game owns that car and applies the hack to it; the others
// see the result through the car's usual sync. Sent to the host, which
// stamps the hacker and passes it to the driver (or applies it, when the
// host drives).
enum class CarHack : std::uint8_t { ForceBrakes = 0, Explode = 1, Accelerate = 2 };
struct VehicleHack { // Event channel
  PlayerId player{}; // the hacker: set by the host from the connection
  PlayerId driver{};
  CarHack hack{};
  float seconds{}; // how long it lasts (brakes, acceleration), as the hacker's game has it
  friend bool operator==(const VehicleHack &, const VehicleHack &) = default;
};

// ---- Story (milestone 6, docs/research/coop-quests-design.md §6 step 1) ----
// Host to guests unless noted; all reliable. The host keeps what a late
// joiner needs (story.hpp, StoryRecord): the journal, the tracked entry, a
// call that is up, an open choice hub, each player's StoryState.

// One journal entry's state (gameJournalEntryState) by its path hash
// (JournalManager.GetEntryHash).
struct JournalChange {
  std::uint32_t hash{};
  std::uint8_t state{};
  std::uint8_t notify{}; // 1: the host's change notified (toast, sound)
  friend bool operator==(const JournalChange &, const JournalChange &) = default;
};
inline constexpr std::size_t kMaxJournalEntries = 32768;

// A snapshot (Bulk channel: every entry that is not Inactive; on a guest,
// entries not in it are Inactive) or changes (Event channel).
struct Journal {
  bool snapshot{};
  std::vector<JournalChange> entries;
  friend bool operator==(const Journal &, const Journal &) = default;
};

// The host's tracked journal entry (0: none). A guest applies it on join
// and when the host changes it, and may then track something else.
struct Tracked {
  std::uint32_t hash{};
  friend bool operator==(const Tracked &, const Tracked &) = default;
};

// The host's phone call, on every phase change (PhoneSystem); kept by the
// host while it is up. Guests present it on their own HUD.
inline constexpr std::uint8_t kCallRejectable = 1, kCallShowAvatar = 2, kCallPlayerCalling = 4,
                              kCallPlayerTriggered = 8, kCallRejected = 16;
struct PhoneCall {
  std::uint64_t contact{}; // CName hash of the contact (its journal id)
  std::uint8_t phase{};    // questPhoneCallPhase: 1 incoming, 2 started, 3 ended
  std::uint8_t mode{};     // questPhoneCallMode
  std::uint8_t visuals{};  // questPhoneCallVisuals
  std::uint8_t flags{};    // kCall*
  PlayerId answeredBy{};   // kNoPlayer until picked up (or rejected, kCallRejected)
  friend bool operator==(const PhoneCall &, const PhoneCall &) = default;
};

// Guest to host: an answer to the ringing call (first one wins), or a
// call the guest dials. Never relayed.
inline constexpr std::uint8_t kPhoneAnswer = 1, kPhoneReject = 2, kPhoneDial = 3;
struct PhoneAction {
  PlayerId player{}; // set by the host from the connection
  std::uint8_t action{};
  std::uint64_t contact{};
  friend bool operator==(const PhoneAction &, const PhoneAction &) = default;
};

// Guest to host: a text reply picked (first one wins). Never relayed.
struct PhoneReply {
  PlayerId player{};
  std::uint32_t choiceEntry{}; // journal hash of the gameJournalPhoneChoiceEntry
  friend bool operator==(const PhoneReply &, const PhoneReply &) = default;
};

// A dialogue line the host's game shows (UIGameData.ShowDialogLine) or
// hides (kLineHide). Text in the host's language.
inline constexpr std::uint8_t kLineHide = 1, kLinePersistent = 2;
struct Subtitle {
  std::uint64_t line{}; // scnDialogLineData.id (CRUID)
  std::uint8_t type{};  // scnDialogLineType
  std::uint8_t flags{}; // kLine*
  float duration{};
  NpcId speaker{};          // the NPC's session id (0: none, unknown or a player)
  PlayerId speakerPlayer{}; // a V's line: whose
  std::string speakerName;
  std::string text;
  friend bool operator==(const Subtitle &, const Subtitle &) = default;
};

struct ChoiceOption {
  std::string text;
  std::uint16_t types{}; // bit n: gameinteractionsChoiceType n
  friend bool operator==(const ChoiceOption &, const ChoiceOption &) = default;
};
inline constexpr std::size_t kMaxChoiceOptions = 16;

// The host's active dialogue choice hub; no options: closed.
inline constexpr std::uint8_t kHubPhoneLock = 1;
struct ChoiceHub {
  std::int32_t hub{};
  std::uint8_t flags{}; // kHub*
  std::string title;
  std::vector<ChoiceOption> options;
  float timedSeconds{}; // 0: not timed
  std::uint64_t timedStartUs{};
  friend bool operator==(const ChoiceHub &, const ChoiceHub &) = default;
};

// A player's cursor on the hub: where it is, a guest's suggestion, the
// host's pick. Either way; the host stamps and relays.
inline constexpr std::uint8_t kCursorHover = 0, kCursorSuggest = 1, kCursorPicked = 2;
struct ChoiceCursor {
  PlayerId player{};
  std::int32_t hub{};
  std::int8_t index{};
  std::uint8_t kind{}; // kCursor*
  friend bool operator==(const ChoiceCursor &, const ChoiceCursor &) = default;
};

// A player's story state from its own game (either way; the host stamps,
// keeps and relays it).
// Bits of StoryState::flags (all eight taken: another one widens the field,
// a protocol bump).
inline constexpr std::uint8_t kStoryBraindance = 1, kStoryReplacer = 2, kStoryInCall = 4,
                              kStoryPrivate = 8, // in a private scene: not to be watched
    // The host's V is in a scripted moment; its camera's views go out
    // (story::MomentReasons; the host's word only).
    kStoryMoment = 16,
                              // A guest watches the host's moment: its own V is parked (hidden from
                              // the others, not to be seen by their NPCs). A guest's word only.
    kStorySpectating = 32,
                              // The host's moment is one only its view shows (story::kMomentWatch:
                              // a cutscene, another world, the quest's camera). With kStoryMoment;
                              // the host's word only.
    kStoryWatch = 64,
                              // The host's game would not let its V fast travel now: a quest's lock
                              // (Konpeki, Phantom Liberty's missions), not combat, a car or the
                              // police. A guest's V never respawns far away then (revive.md, "Where
                              // a respawn puts you"). The host's word only.
    kStoryNoFastTravel = 128;
struct StoryState {
  PlayerId player{};
  std::uint8_t tier{};  // GameplayTier 1..5
  std::uint8_t flags{}; // kStory*
  friend bool operator==(const StoryState &, const StoryState &) = default;
};

// Host to one guest that took part: a quest reward for its own V
// (RPGManager.GiveReward on the guest, by its own level and perks).
struct QuestReward {
  std::uint64_t reward{}; // TweakDB id of the reward record
  std::uint32_t quest{};  // journal hash of its quest (0: unknown)
  friend bool operator==(const QuestReward &, const QuestReward &) = default;
};

// A V's time ability (docs/architecture/time.md, slowed and stopped time): what
// is active now and the world speed it asks for (Event channel, sent by that
// player's game when it starts, changes or ends). The host stamps the sender
// from the connection, keeps the latest of each player for late joiners
// (a player who leaves has none) and relays it. The owner's own game keeps
// its single-player slow motion; the shared world never slows as a whole:
// the host slows only the NPCs around that V, its time bubble
// (tandem/timebubble.hpp), and every game shows the ability on its puppet.
inline constexpr std::uint8_t kTimeSandevistan = 1, kTimeKerenzikov = 2, kTimeSlowMo = 4,
                              kTimeScanning = 8;
// The abilities that make a bubble (kTimeSlowMo: perks and implants that
// slow time, Focus, Deadeye, Reflex Tuner...; kTimeScanning: the scanner's
// own slow motion, since protocol 7: before, scanning made none).
inline constexpr std::uint8_t kTimeBubbleAbilities =
    kTimeSandevistan | kTimeKerenzikov | kTimeSlowMo | kTimeScanning;
struct TimeEffect {
  PlayerId player{};
  std::uint8_t abilities{}; // kTime* bits (0: none)
  float scale{1.f};         // world speed it asks for, 0..1 (1: no bubble)
  friend bool operator==(const TimeEffect &, const TimeEffect &) = default;
};

// A story status effect on the host's V: Johnny's relic glitches
// (JohnnySickness*), Songbird's overlays, other quest glitch effects
// (Story.reds decides which). Host to guests (Event channel; the host keeps
// the ones on now for late joiners); a guest near the host puts the same
// effect on its own V while the host's lasts.
struct StoryEffect {
  PlayerId player{};      // whose V has it (the host)
  std::uint64_t effect{}; // TweakDB id of the status effect record
  bool applied{};         // false: it went
  friend bool operator==(const StoryEffect &, const StoryEffect &) = default;
};

// NpcGone::reason values. A disposed body (in a container) is gone for good:
// guests delete its proxy instead of leaving a corpse.
inline constexpr std::uint8_t kGoneAway = 0, kGoneDead = 1, kGoneDisposed = 2,
                              kGoneKnockedOut = 3; // a non-lethal hit: the body stays, a later
                                                   // lethal one sends kGoneDead

// A player's action that moves, animates or kills one of the host's NPCs:
// grapples, takedowns, finishers, carrying and throwing bodies. The actor's
// game plays its own side at once (its V's animation, on its proxy); this is
// the request to the NPC's owner, the host, which validates it and plays the
// NPC's side on the real one (scripts/tandem/Takedowns.reds).
enum class NpcActionKind : std::uint8_t {
  Grab = 1,     // grapple start; name: the takedown action (GrappleTarget...)
  Release = 2,  // let go alive (break free, failed grapple, shove); name: why
  Takedown = 3, // lethal takedown; name: the takedown effect tag (kill)
  Knockout = 4, // non-lethal takedown; name: its effect tag (setUnconscious...)
  Animate = 5,  // another takedown animation (aerial, boss); name: its tag
  Finisher = 6, // name: the playFinisher effect; item: weapon record
  PickUp = 7,   // pick up a body (unconscious or dead)
  Drop = 8,     // put it down
  Throw = 9,    // throw it; direction: the ragdoll impulse
  Dispose = 10, // into a container; item: the device's entity id; name: kill or not
};

struct NpcAction {   // Event channel; guest to host, never relayed
  PlayerId player{}; // who: set by the host from the connection
  NpcId npc{};
  NpcActionKind kind{};
  std::uint64_t name{}; // CName hash, see NpcActionKind
  std::uint64_t item{}; // TweakDB id or entity hash, see NpcActionKind
  float amount{};       // finisher: the NPC's health (fraction) as the actor saw it
  Vec3 direction;       // throw
  friend bool operator==(const NpcAction &, const NpcAction &) = default;
};

// Why the host refused an NpcAction.
inline constexpr std::uint8_t kRefusedGone = 1, kRefusedHeld = 2, kRefusedConditions = 3,
                              kRefusedNotYours = 4, kRefusedTooFar = 5;

struct NpcActionReply { // Event channel; host to the player who asked only
  NpcId npc{};
  NpcActionKind kind{};
  bool accepted{};
  std::uint8_t reason{}; // kRefused*, when refused
  friend bool operator==(const NpcActionReply &, const NpcActionReply &) = default;
};

// Who holds or animates one of the host's NPCs, so every game shows it the
// same way: its proxy goes into the grapple, onto the shoulder or into the
// takedown with that player's V (puppet or own). Host to everyone, kept for
// late joiners until released (player kNoPlayer) or gone.
enum class NpcHoldKind : std::uint8_t {
  None = 0,     // free again
  Grappled = 1, // name: the takedown action that started it
  Carried = 2,
  Takedown = 3, // a takedown being played; name: its effect tag
  Finisher = 4, // a finisher being played; name: the playFinisher effect
};

struct NpcHold { // Event channel
  NpcId npc{};
  PlayerId player{}; // the holder; kNoPlayer when released
  NpcHoldKind how{};
  std::uint64_t name{};
  friend bool operator==(const NpcHold &, const NpcHold &) = default;
};

// The player's half of an NPC's reaction that single player gives "the
// player" (on the host: its own V) when another V is the one concerned.
// Host to that player's game only.
enum class NpcCueKind : std::uint8_t {
  CombatStart = 1, // the NPC entered combat with them: second chance, gag
  PoiseBroken = 2, // their hit broke its guard: a finisher is offered
  OutOfSight = 3,  // their dodge got them out of its sight: the memory wipe
                   // they asked for (NpcEffect) is done, the rest is theirs
};

struct NpcCue { // Event channel
  NpcId npc{};
  PlayerId player{}; // whose game it is for
  NpcCueKind cue{};
  friend bool operator==(const NpcCue &, const NpcCue &) = default;
};

// A player offers others to come to them ("regroup", milestone 4): sent by
// that player's game when it arrives from a fast travel, when a story scene
// starts while someone is far (the story work), or when the player asks. The
// others get the game's own style of prompt; accepting brings them there
// (their own fast travel to the same point when they know it, with its
// loading screen; else next to the player). Nobody is ever moved without
// accepting: players stay free to be anywhere.
// Event channel. The host stamps `from` from the connection and delivers it
// to `to` only (kNoPlayer: every other player). Not kept: an offer is of the
// moment; a newer one from the same player replaces the one shown.
enum class RegroupReason : std::uint8_t {
  FastTravel = 0, // `from` fast travelled here
  Asked = 1,      // `from` asks to be joined (a regroup request)
  Story = 2,      // a story scene starts beside `from` (label: what)
  Teleport = 3,   // a quest moved `from` far away
};
inline constexpr std::size_t kMaxRegroupLabel = 96;

struct Regroup {
  PlayerId from{};
  PlayerId to{};
  std::uint32_t offer{}; // the sender's count of offers: an answer names it
  RegroupReason reason{};
  std::uint64_t point{};  // TweakDB id of the fast travel point `from` is at (0: none)
  std::uint64_t marker{}; // that point's marker node (NodeRef hash; 0: none)
  Vec3 position;          // where `from` stands
  float yaw{};
  std::string label; // shown with the prompt (a quest's title...); may be empty
  friend bool operator==(const Regroup &, const Regroup &) = default;
};

// The answer to a Regroup (Event channel; the host stamps `from` and
// delivers it to `to`, the offer's sender). `offer` 0: not an answer but a
// player coming on their own (the regroup action).
struct RegroupAnswer {
  PlayerId from{};
  PlayerId to{};
  std::uint32_t offer{};
  bool accepted{};
  friend bool operator==(const RegroupAnswer &, const RegroupAnswer &) = default;
};

// Loot (docs/architecture/loot.md, "Loot"). Corpses and containers are per
// player: every game rolls its own loot, nothing is sent. What a V drops
// from its inventory on purpose is a shared world item, the host's: it lies
// in every game, and the first player to pick a stack up keeps it.
using WorldItemId = std::uint32_t;
inline constexpr std::size_t kMaxWorldItemStacks = 16;

struct ItemStack {
  std::uint64_t record{};   // TweakDB id of the item's record
  std::uint32_t seed{};     // its ItemID's random seed: the same stats everywhere (0: stackable)
  std::uint32_t quantity{}; // 0: taken
  friend bool operator==(const ItemStack &, const ItemStack &) = default;
};

// A bag of stacks a V dropped (Event channel). The dropper's game sends it
// with id 0 and a token of its own; the host numbers it, keeps it for late
// joiners until every stack is taken, and sends it to everyone, the dropper
// included (its game matches the token to the bag it already has).
struct WorldItem {
  WorldItemId id{};
  PlayerId dropper{};    // set by the host from the connection
  std::uint32_t token{}; // the dropper's own number for it
  Vec3 position;
  std::vector<ItemStack> stacks;
  friend bool operator==(const WorldItem &, const WorldItem &) = default;
};

// A stack of a world item was picked up (Event channel). The picker's game
// sends it to the host: a claim (the host sets `taker` from the connection).
// The host grants the first claim and tells everyone who has it; a later
// claimer hears the winner (kNoPlayer: the item was gone) and gives its own
// pickup back.
struct WorldItemTaken {
  WorldItemId id{};
  std::uint8_t stack{};
  PlayerId taker{};
  friend bool operator==(const WorldItemTaken &, const WorldItemTaken &) = default;
};

// Quest and important items are every player's (docs/architecture/loot.md,
// Loot): an item tagged Quest, a keycard or an iconic item that came into a
// V's inventory (kSharedLooted: every other game gives it to its own V,
// unless it has one; the host's own quests see it), or one on a host NPC's
// corpse (kSharedOnCorpse, host only: guests put it on their proxy corpse
// of `npc`). Event channel; a guest's goes to the host, which stamps it and
// passes it on to the others.
inline constexpr std::uint8_t kSharedLooted = 0, kSharedOnCorpse = 1;
struct SharedItem {
  PlayerId player{}; // who got it: set by the host from the connection
  std::uint8_t kind{};
  NpcId npc{}; // kSharedOnCorpse: whose corpse
  ItemStack item;
  friend bool operator==(const SharedItem &, const SharedItem &) = default;
};

// ---- Down and out (milestone 8, docs/architecture/revive.md "Down, revive, respawn") ----

// A V's life, owned and sent by that player's game when it changes (Event
// channel; the host stamps the player, keeps everyone's latest for late
// joiners and relays it). A V that would die while another V is up goes
// Down (revivable, no bleed-out); one that cannot be revived (down again
// within the cooldown after a revive, or killed outright) is Out until it
// respawns beside a partner. The host decides the session is lost when no V
// is Up (GameOver).
enum class Life : std::uint8_t { Up = 0, Down = 1, Out = 2 };
inline constexpr std::uint8_t kVitalsInCombat = 1; // the V is in combat (a respawn waits for it)
struct Vitals {
  PlayerId player{};
  std::uint32_t revision{};
  Life life{};
  std::uint8_t flags{}; // kVitals*
  friend bool operator==(const Vitals &, const Vitals &) = default;
};

// A player reviving another's downed V (the puppet in the reviver's game,
// held for 10 s within 2 m): started, given up, done. To the downed V's game
// through the host, which stamps the reviver and drops it unless the target
// is Down; the downed V's game decides.
enum class ReviveStep : std::uint8_t { Start = 0, Cancel = 1, Done = 2 };
struct Revive {
  PlayerId player{}; // the reviver: set by the host from the connection
  PlayerId target{};
  ReviveStep step{};
  friend bool operator==(const Revive &, const Revive &) = default;
};

// Host to everyone: no V in the session is Up any more. The host's game runs
// the normal death (death screen, checkpoint reload); guests wait for it.
struct GameOver {
  std::uint32_t round{}; // counts the losses of this session
  friend bool operator==(const GameOver &, const GameOver &) = default;
};

// How one of the host's NPCs animates, in the game's own terms: what its
// animation graph reads (NPCStatesComponent's states and anim features, the
// weapon in its hands, a look-at target, the workspot it plays in), so a
// guest's brain-off proxy is put in the same states and its own graph plays
// them. A separate message from NpcStates (positions, 10 Hz, every NPC):
// these change seldom, so they go only when they change (the host samples
// them 4 times a second), reliably, and the host keeps each NPC's latest for
// late joiners. The values are the game's enums (engine-knowledge §13.21).
inline constexpr std::uint8_t kNpcAnimWeaponDrawn = 1; // something in its right hand
inline constexpr std::uint8_t kNpcAnimInWorkspot = 2;  // workspot below is valid
struct NpcAnim {
  NpcId npc{};
  std::uint8_t highLevel{5}; // gamedataNPCHighLevelState (Relaxed 5, Combat 2, Alerted 0…)
  std::uint8_t upperBody{6}; // gamedataNPCUpperBodyState (Normal 6, Aim 0, Shoot 9, Reload 8…)
  std::uint8_t stance{3};    // gamedataNPCStanceState (Stand 3, Crouch 2, Cover 1…)
  std::uint8_t flags{};      // kNpcAnim*
  std::uint64_t weapon{};    // TweakDB id of the item in its right hand (0: none)
  // Who it looks or aims at: a player id, or 0x10000 + an NpcId (0: nobody).
  std::uint32_t target{};
  // Its workspot (a bench, a wall to lean on, a cigarette): the workspot
  // resource's path hash, the tree entry it plays, the animation's CName hash.
  std::uint64_t workspot{};
  std::uint32_t workspotEntry{};
  std::uint64_t workspotAnim{};
  friend bool operator==(const NpcAnim &, const NpcAnim &) = default;
};

inline constexpr std::size_t kMaxNpcAnims = 64;
struct NpcAnims { // Event channel, host only: the NPCs whose animation state changed
  std::vector<NpcAnim> npcs;
  friend bool operator==(const NpcAnims &, const NpcAnims &) = default;
};

// One shot (or reload) of a host NPC's weapon, drawn on the guests' proxy as
// the weapon's own effects (muzzle flash, report, shells), like a player's
// Shot. State channel: a late one is worth nothing. Host only.
struct NpcShot {
  NpcId npc{};
  std::uint8_t effect{}; // gamedataFxAction, as Shot (Shoot 16, SilencedShoot 17…)
  Vec3 target;           // where the shot went (tracers, later; zero: unknown)
  friend bool operator==(const NpcShot &, const NpcShot &) = default;
};

// ---- Hit outcomes (docs/architecture/combat.md, "Game rules hold for every hit") ----

// A guest's hit on one of the host's NPCs (its proxy) with its whole outcome
// as the guest's game computed it: health and poise lost, the rules that
// bound the loss (non-lethal, leave 1 HP, the attack's minimum health), and
// what the host's NPC needs to react as to a hit of its own (the attack, the
// body part and kind of hit shape it landed on). The host applies it as
// given (combat.hpp StrikeLoss) and plays the real reaction, wounds and
// dismemberment on its NPC. Supersedes NpcHit, which the host still takes.
// Guest to host, never relayed.
inline constexpr std::uint16_t kStrikeWeakSpot = 1, kStrikeCritical = 2, kStrikeMelee = 4,
                               kStrikeInstantKill = 8, // finisher, Kill flag: health to 0
    kStrikeNonlethal = 16,                             // hitFlag.Nonlethal: knocks out
    kStrikeLeaveOneHp = 32,                            // finisher grace, bleeding perk
    kStrikeKillingBlow = 64,                           // hitFlag.WasKillingBlow on the proxy
    kStrikeHeadshot = 128,                             // hitFlag.Headshot
    kStrikeForceDismember = 256;                       // hitFlag.ForceDismember
inline constexpr std::uint8_t kNoZone = 255;
struct NpcStrike {   // Event channel
  PlayerId player{}; // who hit: set by the host from the connection
  NpcId npc{};
  float damage{};             // health points
  float poise{};              // poise points
  float minHealth{};          // AttackData.GetMinimumHealthPercent (0: none)
  std::uint16_t flags{};      // kStrike*
  std::uint8_t attackType{};  // gamedataAttackType
  std::uint8_t zone{kNoZone}; // EHitReactionZone of the hit shape (kNoZone: none)
  std::int8_t part{-1};       // EAIDismembermentBodyPart (-1 none)
  std::int8_t shape{-1};      // EHitShapeType (-1 none)
  std::uint64_t attack{};     // TweakDB id of the attack record (0: unknown)
  Vec3 position;              // where it landed, and the way it went
  Vec3 direction;
  friend bool operator==(const NpcStrike &, const NpcStrike &) = default;
};

// A host NPC lost a limb or got a wound (its hit reaction decided it, by the
// game's rules, from a hit by anyone): every guest's proxy gets the same.
// Host to everyone.
struct NpcWound { // Event channel
  NpcId npc{};
  PlayerId player{};    // whose hit (kNoPlayer: an NPC's, or unknown)
  std::uint8_t part{};  // gameDismBodyPart 0..5
  std::uint8_t wound{}; // gameDismWoundType 0..2
  Vec3 position;
  friend bool operator==(const NpcWound &, const NpcWound &) = default;
};

// A host NPC's poise (guard), as its health: guests' proxies follow it, so
// their own finisher and guard-break checks see the host's. Host to
// everyone, on change; kept for late joiners until the NPC is gone.
struct NpcPoise { // Event channel
  NpcId npc{};
  float poise{}; // fraction of its maximum, 0..1
  friend bool operator==(const NpcPoise &, const NpcPoise &) = default;
};

// The session plays at the host's difficulty (gameDifficulty: Easy 0, Hard 1,
// VeryHard 2, Story 3). Guests switch to it while in the session and back
// when they leave. Host to everyone, kept for late joiners.
struct Difficulty { // Event channel
  std::uint8_t level{};
  friend bool operator==(const Difficulty &, const Difficulty &) = default;
};

// An area effect our game computed (our V's grenade or explosion) reached
// another player's V (its puppet here). The attacker's game ran the
// attacker's side; the victim's runs the target's side on its own V, with
// our puppet as the attacker: never applied twice. To the host, which
// stamps `from` and passes it to `player` (or applies it to its own V).
struct AreaHit {     // Event channel
  PlayerId from{};   // the attacker: set by the host from the connection
  PlayerId player{}; // whose V was hit
  float physical{}, thermal{}, electric{}, chemical{};
  std::uint8_t attackType{}; // gamedataAttackType (Explosion 3, PressureWave...)
  std::uint8_t flags{};      // kHit*
  Vec3 position;
  Vec3 direction;
  std::uint64_t attack{}; // TweakDB id of the attack record
  std::uint64_t weapon{}; // TweakDB id of the item (grenade, weapon; 0: none)
  friend bool operator==(const AreaHit &, const AreaHit &) = default;
};

// ---- Consistency (docs/architecture/consistency.md, "Consistency") ----
// The shared state the host's digests cover, as bits.
inline constexpr std::uint8_t kDomainWorld = 1;   // World: time, weather
inline constexpr std::uint8_t kDomainNpcs = 2;    // the NPCs the host shares, their health
inline constexpr std::uint8_t kDomainStory = 4;   // journal, tracked entry, call, hub, effects
inline constexpr std::uint8_t kDomainPlayers = 8; // roster, looks, held items, seats, radios...
inline constexpr std::uint8_t kAllDomains = 15;
// The devices (DeviceDigest, DeviceSet): a domain of its own messages, named
// here for the reports of a resync only; never a bit of Digest or ResyncRequest.
inline constexpr std::uint8_t kDomainDevices = 16;

// Host to each guest every few seconds (Event channel: it falls in the
// reliable stream after everything the host sent before it, so the guest
// compares at the same point). A hash of each domain as the host's record
// has it for that guest (tandem/consistency.hpp); the guest hashes its own
// copy the same way and asks again for a domain that differs twice in a row.
struct Digest {
  std::uint32_t epoch{}; // the host's world epoch
  std::uint32_t seq{};
  std::uint32_t world{}, npcs{}, story{}, players{};
  std::uint16_t npcCount{};
  friend bool operator==(const Digest &, const Digest &) = default;
};

// Guest to host: send me these domains again (kDomain* bits). Never relayed.
struct ResyncRequest {
  std::uint8_t domains{};
  friend bool operator==(const ResyncRequest &, const ResyncRequest &) = default;
};

// Host to one guest, opening a resync of its NPCs: every NPC the host
// shares now. Their NpcSpawn and NpcHealth follow; the guest drops the rest.
inline constexpr std::size_t kMaxNpcSet = 1024;
struct NpcSet {
  std::vector<NpcId> npcs;
  friend bool operator==(const NpcSet &, const NpcSet &) = default;
};

// A game's world was rebuilt around its V (a loading screen, a fast
// travel): its epoch moves on, and what was in flight about the old one is
// void (Event channel). The host's goes to every guest (and to a newcomer);
// each guest answers with its own, naming the host epoch it has seen, and
// until then the host drops that guest's hits, effects, uploads and noises
// (they aimed at the old world). A guest's own goes to the host, which
// stamps and relays it.
inline constexpr std::uint8_t kEpochJoin = 0, kEpochLoading = 1, kEpochFastTravel = 2,
                              kEpochTimeSkip = 3, // the clock jumped: the crowd is new
    kEpochFrozen = 4;                             // our game stood still (photo mode) and is back
struct Epoch {
  PlayerId player{};           // whose world: set by the host from the connection
  std::uint32_t epoch{};       // that game's world epoch (1 at the start of a session)
  std::uint32_t seenHost{};    // a guest's: the host epoch it has seen
  std::uint8_t reason{};       // kEpoch*
  std::uint32_t firstNpcSeq{}; // the host's: NpcStates older than this are the old world's
  friend bool operator==(const Epoch &, const Epoch &) = default;
};

// A V in another world (docs/architecture/elsewhere.md, other worlds; core
// elsewhere.hpp): cyberspace, a braindance, playing as Johnny or someone
// else. Sent by that player's game when it changes (Event channel); the host
// stamps the sender, keeps everyone's for late joiners (a player who leaves,
// or comes back from a crash, is back in the city) and relays it. While the
// scene has taken the player away from its body (kElsewhereMoved), its game
// sends the body's place (`anchor`) as its PlayerState and holds the body's
// pose as it was; the others show the body there when it was sitting or
// lying (kElsewhereSettled: a chair, a bed, a braindance rig) and hide it
// otherwise. Part of the players domain of the host's digests.
enum class Place : std::uint8_t { City = 0, Cyberspace = 1, Braindance = 2, Johnny = 3, Other = 4 };
inline constexpr std::uint8_t kElsewhereMoved = 1, kElsewhereSettled = 2;
struct Elsewhere {
  PlayerId player{};
  Place place{};
  std::uint8_t flags{}; // kElsewhere*
  Vec3 anchor;          // kElsewhereMoved: where the body is, in the city
  float yaw{};          // its heading (radians, sync::YawOf)
  friend bool operator==(const Elsewhere &, const Elsewhere &) = default;
};

// A ride a player's own game runs for its V (docs/architecture/transit.md,
// "Scripted transit and quest vehicles"), sent by that game when it starts,
// changes or ends (Event channel; the host stamps the sender, keeps the
// latest of each player for late joiners and relays it).
// - Metro: an NCART train (ncartMetroObject, status effect tag MetroRide).
//   Never copied: every game that rides runs its own train. The others
//   seat the rider's puppet in a train of their own world when one is
//   there (the same entity, or the one they ride themselves, near the
//   rider), else hide it with a notice; it comes back at the exit station.
//   No Seat is sent for a train.
// - Ride: a vehicle our game moves for us while we do not drive it (a
//   Delamain cab, a car an NPC drives): Seat and VehicleState as for a car
//   we drive, so the others see its copy with us seated.
// - Scene: the same in a story scene (the host's quest vehicle): a partner
//   near it on foot is offered to come along (kTransitSeats): a free seat
//   of its copy, or, when the car has none (two seats: the host and Panam),
//   a ride along.
// - Along: our V rides along in `carrier`'s scene vehicle with no seat of
//   its own (Transit.reds): carried with its copy here, out of sight, and
//   out of sight everywhere else too (no seat to show it in).
enum class TransitKind : std::uint8_t { None = 0, Metro = 1, Ride = 2, Scene = 3, Along = 4 };
inline constexpr std::uint8_t kTransitSkipping =
    1; // the ride's own skip runs (the world fast-forwards)
inline constexpr std::uint8_t kTransitSeats = 2; // partners near its copy get a free seat
struct Transit {
  PlayerId player{};
  std::uint32_t revision{};
  TransitKind kind{};
  std::uint8_t flags{}; // kTransit*
  // Metro: the station the train is at or last stopped at, and the next one
  // (ENcartStations: 1..19; 0 unknown).
  std::uint8_t at{};
  std::uint8_t next{};
  std::uint64_t vehicle{}; // the vehicle's EntityID hash in the rider's world
  std::uint64_t seat{};    // the rider's seat slot (CName hash; 0 unknown)
  PlayerId carrier{};      // Along: whose vehicle carries us
  friend bool operator==(const Transit &, const Transit &) = default;
};

// One of the host's NPCs sits in the vehicle carrying a player (the vehicle
// that player's game runs: its copy everywhere else), or got out (owner
// kNoPlayer). Host to guests (Event channel; kept while it sits, for late
// joiners). A guest seats its proxy in that copy: Panam at the wheel of the
// quest car the host's V rides in.
struct NpcSeat {
  NpcId npc{};
  PlayerId owner{};     // the player whose vehicle (kNoPlayer: out)
  std::uint64_t seat{}; // the slot's CName hash
  friend bool operator==(const NpcSeat &, const NpcSeat &) = default;
};

// NPC shots at a guest's V (docs/ARCHITECTURE.md, authority table; not
// NpcShot, the cosmetic shot every guest draws on its proxy): the
// host's NPC decides to fire (when, at whom, with which weapon and attack);
// the guest's game fires the shot from its proxy of that NPC at its own V,
// simulates the bullet or projectile and decides the hit, as single player
// would (aim, the target's miss rules, dodging under slow motion). The host
// sends it to that guest only and no longer decides the hit itself.
// Event channel: a lost shot is one never fired, a doubled one two (a
// one-shot for the consistency layer, in no digest: it is not state).
struct NpcShotAt {
  PlayerId player{};      // the V shot at
  NpcId npc{};            // the shooter, as the host shares it
  std::uint64_t timeUs{}; // host clock when it fired
  std::uint64_t attack{}; // TweakDB id of the ranged attack asked for (0: the weapon's)
  std::uint64_t weapon{}; // TweakDB id of the shooter's weapon item
  // The AI's time-between-hits coefficient for this shot, 0 when the
  // host's game lets it land (its time between hits on the target's
  // puppet, from the NPC's own line of sight; v8).
  float tbh{};
  std::uint8_t trigger{}; // gamedataTriggerMode
  float factor{1.f};      // the shooter's time factor (its bubble; 1: none)
  friend bool operator==(const NpcShotAt &, const NpcShotAt &) = default;
};

// A shot the guest's game simulated hit its V (guest to host; the host
// stamps the player). The host shows the hit on that player's puppet (the
// game's own hit reaction, no damage: the damage was the guest's).
struct ShotHit {     // Event channel
  PlayerId player{}; // the V hit: set by the host from the connection
  NpcId npc{};       // the shooter
  Vec3 position;     // where it landed and the way it went
  Vec3 direction;
  friend bool operator==(const ShotHit &, const ShotHit &) = default;
};

// ---- Devices and world objects (docs/architecture/devices.md) ----
//
// A placed device is named by its entity's static id and the class of its
// persistent state (its PersistentID's component is "controller"): the same
// in every game. The host's session keeps one record per device anyone
// touched or asked about: what it is now, and who changed it last.

// What a device is, class by class (the bits a class has no use for stay 0).
inline constexpr std::uint16_t kDeviceOpen = 1;         // door, gate
inline constexpr std::uint16_t kDeviceLocked = 2;       // door
inline constexpr std::uint16_t kDeviceSealed = 4;       // door
inline constexpr std::uint16_t kDeviceBroken = 8;       // durability: broken or destroyed
inline constexpr std::uint16_t kDeviceExploded = 16;    // explosive: barrel, mine, trap
inline constexpr std::uint16_t kDeviceHacked = 32;      // a quickhack was done on it
inline constexpr std::uint16_t kDeviceFriendly = 64;    // camera, turret: turned by a player
inline constexpr std::uint16_t kDeviceExposed = 128;    // its quickhacks are open (a breach)
inline constexpr std::uint16_t kDeviceBreached = 256;   // access point: the network is breached
inline constexpr std::uint16_t kDeviceMoving = 512;     // lift: on its way to `value`
inline constexpr std::uint16_t kDeviceGlitching = 1024; // distracting (a glitch, a malfunction)
inline constexpr std::uint16_t kDeviceStateBits = 2047;
// How a DeviceState travels (not part of what the device is):
inline constexpr std::uint8_t kDeviceLive = 0; // it just changed: shown the game's way
inline constexpr std::uint8_t kDeviceQuiet =
    1; // a record (a join, a resync, an answer): set, not played
inline constexpr std::uint8_t kDeviceAsk = 2; // guest to host: mine is this, what is yours?

// The status of a device: the game's EDeviceStatus.
inline constexpr std::int8_t kDeviceDisabled = -2, kDeviceUnpowered = -1, kDeviceOff = 0,
                             kDeviceOn = 1;
inline constexpr std::int32_t kDeviceNoValue = -1;

// A device as the session has it (Event channel). Host to guests: its word,
// kept for late joiners and hashed in DeviceDigest; a guest applies it to
// its own copy, loaded or not, and remembers what its own save had. Guest
// to host (kDeviceAsk): this device came into my world like this; the host
// answers with its own when they differ.
struct DeviceState {
  std::uint64_t entity{};             // the entity's static id
  std::uint64_t cls{};                // its persistent state's class name
  std::int8_t status{};               // kDeviceDisabled .. kDeviceOn
  std::uint16_t flags{};              // kDevice* state bits
  std::int32_t value{kDeviceNoValue}; // lift: floor; radio, TV: station; security system: state
  std::uint32_t revision{}; // the host's count of its changes (a guest's: the one it has seen)
  PlayerId by{};            // who changed it last (kNoPlayer: the host's world)
  std::uint8_t how{};       // kDeviceLive, kDeviceQuiet, kDeviceAsk
  friend bool operator==(const DeviceState &, const DeviceState &) = default;
};

// A guest operated a device (Event channel; guest to host, never relayed):
// it is this in my game now. The guest's own copy changed at once; the host
// brings its own to the same state the game's way and its DeviceState is
// the answer for everyone. `seen` fences it: when someone else changed the
// device since the revision the guest had seen, the request is void and
// the guest gets the host's state back.
struct DeviceUse {
  std::uint64_t entity{};
  std::uint64_t cls{};
  std::int8_t status{};
  std::uint16_t flags{};
  std::int32_t value{kDeviceNoValue};
  std::uint32_t seen{};
  PlayerId player{}; // set by the host from the connection
  friend bool operator==(const DeviceUse &, const DeviceUse &) = default;
};

// Host to one guest, opening a resync of the devices: forget the record,
// `count` DeviceStates follow (what is not among them goes back to what the
// guest's own save had). Guest to host: send me the record again.
struct DeviceSet {
  std::uint32_t count{};
  friend bool operator==(const DeviceSet &, const DeviceSet &) = default;
};

// Host to each guest with its Digest (Event channel): the hash of the
// device record (tandem/devices.hpp). A guest whose copy differs twice in a
// row asks for it again (DeviceSet).
struct DeviceDigest {
  std::uint32_t count{};
  std::uint32_t hash{};
  friend bool operator==(const DeviceDigest &, const DeviceDigest &) = default;
};

// ---- Scene replay (docs/architecture/scene-replay.md) ----
// A story scene of the host's game as its guests watch it. The scene runs
// on the host only (authority table: quests and scenes are the host's); a
// guest never plays its own copy. What travels is what the scene shows: that
// one is on and where (SceneState), which of the host's NPCs act in it
// (SceneCast), each actor's body pose (ActorPose) and each voiced line with
// its start time (SceneLine). All host to guests. A private scene
// (kStoryPrivate) is never announced.

// SceneState::flags.
inline constexpr std::uint8_t kSceneSkipping = 1, // the host fast-forwards (rate > 1)
    kSceneHeld = 2,                               // its time waits (a choice hub, a pause)
    kSceneInterrupted = 4,                        // with scene 0: it ended before its end
    kSceneStaged = 8;                             // anchor, yaw and radius are known
inline constexpr float kMaxSceneRate = 16.f;
// The host's scene, on every change (Event channel; kept while one is on,
// for late joiners: they join at host clock - startUs). `scene` 0: none.
struct SceneState {
  std::uint64_t scene{};   // the scnSceneResource's path hash (0: no scene)
  std::uint32_t play{};    // counts the host's scenes in this session: a scene
                           // played again is another play
  std::uint32_t section{}; // the scnNodeId playing (0: not known)
  std::uint64_t startUs{}; // host clock at the scene's (or the section's) time 0
  float rate{1.f};         // how fast its time runs (fast-forward: > 1; held: 0;
                           // at most kMaxSceneRate)
  std::uint8_t tier{};     // GameplayTier it puts the host's V in
  std::uint8_t flags{};    // kScene*
  Vec3 anchor;             // kSceneStaged: the middle of its actors,
  float yaw{};             // the way the host's V faces them
  float radius{};          // and how far from the middle they stand
  friend bool operator==(const SceneState &, const SceneState &) = default;
};

// SceneActor::flags.
inline constexpr std::uint8_t kActorGhost = 1, // only there through the Relic (Johnny): comes
                                               // and goes with its glitch
    kActorHidden = 2,                          // in the cast, not shown now
    kActorPosed = 4;                           // its pose is streamed (ActorPose)
struct SceneActor {
  NpcId npc{}; // as the host shares it (NpcSpawn)
  std::uint8_t flags{};
  friend bool operator==(const SceneActor &, const SceneActor &) = default;
};
inline constexpr std::size_t kMaxSceneActors = 32;

// Who acts in play `play`, whole on every change (Event channel; kept with
// the SceneState). The host's V is always in it and not listed.
struct SceneCast {
  std::uint32_t play{};
  std::vector<SceneActor> actors;
  friend bool operator==(const SceneCast &, const SceneCast &) = default;
};

// SceneLine::flags.
inline constexpr std::uint8_t kVoiceStop = 1, // the line was cut (a skip, an interruption)
    kVoiceHolocall = 2,                       // heard through the phone: no place in the world
    kVoiceOverhead = 4;                       // a bark or a chatter, not the scene's dialogue
// A voiced line begins (or is cut): the same id as its Subtitle. A guest
// plays the line's voice-over itself, in its own language, on the speaker's
// proxy or puppet, from host clock - startUs (Event channel; kept while it
// lasts, for late joiners).
struct SceneLine {
  std::uint32_t play{};     // 0: outside a scene (a bark)
  std::uint64_t line{};     // the line's string id (scnDialogLineData.id, a CRUID)
  NpcId speaker{};          // the NPC (0: none or a player)
  PlayerId speakerPlayer{}; // a V's line: whose
  std::uint64_t startUs{};  // host clock when it began
  float duration{};         // seconds (0: unknown)
  std::uint8_t flags{};     // kVoice*
  friend bool operator==(const SceneLine &, const SceneLine &) = default;
};

// A scene actor's body pose (State channel, up to 30 Hz, host only): a
// PoseState for one of the host's NPCs, same encoding (tandem/pose.hpp: the
// body rig's bones by name, which every humanoid NPC rig has).
struct ActorPose {
  NpcId npc{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::array<std::int16_t, 3> hips{};
  std::array<std::uint8_t, kPoseBones * kPoseRotationBytes> rotations{};
  friend bool operator==(const ActorPose &, const ActorPose &) = default;
};

// ---- Per-player regions (docs/architecture/regions.md) ----
// Each game runs the NPCs around its own V; players whose Vs are near each
// other are linked and see each other's NPCs as proxies. "The host's NPCs"
// in the messages above reads "the owner's NPCs": the endpoint routes by
// owner (tandem/regions.hpp, Endpoint::NpcOwner).

// Which players are linked (host to everyone, Event channel, on change; kept
// for late joiners): `pairs` holds two player ids per link, lower id first.
inline constexpr std::size_t kMaxRegionPairs = 32;
struct Regions {
  std::uint32_t revision{};
  std::vector<PlayerId> pairs;
  friend bool operator==(const Regions &, const Regions &) = default;
};

// Who runs an NPC, when it is not the game that announced it (host to every
// player that sees the NPC; kept for late joiners). From an owner to the
// host, with kNpcPinned: this NPC of mine never changes owner (a quest or
// story NPC, a police unit).
inline constexpr std::uint8_t kNpcPinned = 1;
struct NpcOwner {
  NpcId npc{};
  PlayerId owner{};
  std::uint8_t flags{}; // kNpc*
  friend bool operator==(const NpcOwner &, const NpcOwner &) = default;
};

// Host to an NPC's owner: hand it over to `to` (its V is leaving the NPC,
// `to`'s is near). The owner answers with NpcHandoverState, or refuses with
// NpcHandoverDone.
struct NpcHandover {
  NpcId npc{};
  PlayerId to{};
  friend bool operator==(const NpcHandover &, const NpcHandover &) = default;
};

struct NpcEffectState {
  std::uint64_t record{}; // TweakDB id of the status effect record
  float remaining{};      // seconds left (0: no duration, or unknown)
  std::uint8_t stacks{1};
  friend bool operator==(const NpcEffectState &, const NpcEffectState &) = default;
};

// The NPC as its owner leaves it (giver to host, host to the new owner): the
// giver has switched its brain off and keeps its body as a proxy; the
// receiver switches its proxy's brain on and applies this. Also what the
// host sends the heir of an owner that vanished, built from the session's
// record (kHandoverBlind: nobody read the NPC, no target).
inline constexpr std::uint8_t kHandoverCombat = 1, kHandoverDefeated = 2, kHandoverUnconscious = 4,
                              kHandoverBlind = 8, kHandoverInherited = 16;
struct NpcHandoverState {
  NpcId npc{};
  PlayerId from{}; // set by the host from the connection
  PlayerId to{};
  Vec3 position;
  float yaw{};
  float health{-1.f};       // fraction of its maximum (negative: unknown, keep the proxy's)
  float poise{-1.f};        // the same for its guard
  std::uint8_t flags{};     // kHandover*
  PlayerId target{};        // the V it fights (kNoPlayer: none)
  std::uint8_t hostileTo{}; // bit (player - 1): hostile to that V
  std::uint8_t highLevel{}; // gamedataNPCHighLevelState
  std::vector<NpcEffectState> effects; // at most kMaxNpcEffects
  friend bool operator==(const NpcHandoverState &, const NpcHandoverState &) = default;
};

// The new owner to the host: I run it (accepted), or I cannot (no body: it
// is gone). The giver to the host, accepted false: it stays mine for now (a
// quest NPC, held, seated, mid-takedown).
struct NpcHandoverDone {
  NpcId npc{};
  bool accepted{};
  friend bool operator==(const NpcHandoverDone &, const NpcHandoverDone &) = default;
};

// Host to one guest, with each round of digests: the NPCs that guest owns as
// the host's record has them (tandem/consistency.hpp, NpcDigest). The guest
// hashes its own list; when they differ twice in a row it sends its set again
// (NpcSet, then each spawn, health and hold).
struct OwnedDigest {
  std::uint32_t seq{};
  std::uint32_t npcs{};
  std::uint16_t count{};
  friend bool operator==(const OwnedDigest &, const OwnedDigest &) = default;
};

// A player's world has a quest of its own running where its V stands (a gig
// it started: docs/architecture/quest-carryover.md, "Whose gig is it"): it
// keeps its region. While held, it ranks first where regions overlap (the
// others' games hold back there, the host's too), none of its NPCs is handed
// over, and it leads its group: its world's quests are the ones played.
// Sent by that player's game when it changes (Event channel); the host
// stamps the player, keeps who holds for late joiners (a player who leaves
// holds nothing) and relays it.
struct RegionHold {
  PlayerId player{};
  bool held{};
  friend bool operator==(const RegionHold &, const RegionHold &) = default;
};

// ---- World vehicles (docs/architecture/traffic.md) ----
//
// The traffic, the parked cars and the police cars around the players are
// run by one game: the owner of that area (per-player regions' area owner,
// Endpoint::AreaOwner: the game that owns the NPCs there). It shares each one as a
// lightweight proxy: one TrafficSpawn, then its transform in TrafficStates
// while it moves. A car a player gets into becomes that player's car under
// the usual rule (Seat, VehicleState): TrafficClaim, TrafficGone{taken}.
//
// An id is unique in the session: the owner's player id in the top 8 bits,
// its own counter below, so several owners never collide.
using TrafficId = std::uint32_t;
constexpr TrafficId MakeTrafficId(PlayerId owner, std::uint32_t counter) {
  return (owner << 24) | (counter & 0xFFFFFFu);
}
constexpr PlayerId TrafficOwner(TrafficId id) { return id >> 24; }

// What a world vehicle is and how it stands (TrafficSpawn::flags; the ones
// under kTrafficStatusMask change and travel in TrafficStatus too).
inline constexpr std::uint16_t kTrafficParked = 1;      // no driver, still: no state stream
inline constexpr std::uint16_t kTrafficPolice = 2;      // a prevention vehicle
inline constexpr std::uint16_t kTrafficDestroyed = 4;   // a wreck
inline constexpr std::uint16_t kTrafficBurning = 8;     // on fire: about to blow
inline constexpr std::uint16_t kTrafficSiren = 16;      // siren and light bar on
inline constexpr std::uint16_t kTrafficLights = 32;     // headlights on
inline constexpr std::uint16_t kTrafficPanic = 64;      // its driver flees
inline constexpr std::uint16_t kTrafficAbandoned = 128; // its driver got out or was pulled out
inline constexpr std::uint16_t kTrafficStatusMask =
    kTrafficParked | kTrafficDestroyed | kTrafficBurning | kTrafficSiren | kTrafficLights |
    kTrafficPanic | kTrafficAbandoned;

struct TrafficSpawn { // Event channel; kept by the owner's side for late joiners
  TrafficId id{};
  std::uint64_t record{};     // TweakDB id of the vehicle record
  std::uint64_t appearance{}; // CName hash of its appearance
  // Its EntityID hash in the owner's world when it is a placed vehicle (the
  // same in every game: the others take their own instance over instead of
  // spawning a copy); 0 for what the owner's traffic spawned.
  std::uint64_t entity{};
  Vec3 position;
  Quat orientation;
  std::uint16_t flags{}; // kTraffic*
  friend bool operator==(const TrafficSpawn &, const TrafficSpawn &) = default;
};

// One moving vehicle in a TrafficStates: 28 bytes. The orientation is the
// quaternion's i, j, k with r >= 0, each in 1/32767; the velocity in cm/s
// (tandem/traffic.hpp packs and unpacks both).
struct TrafficEntry {
  TrafficId id{};
  Vec3 position;
  std::array<std::int16_t, 3> orientation{};
  std::array<std::int16_t, 3> velocity{};
  friend bool operator==(const TrafficEntry &, const TrafficEntry &) = default;
};

// At 10 Hz a full packet is 32 x 28 bytes: under 9 kB/s per guest, whatever
// the street holds (the owner picks the nearest moving ones, traffic.hpp).
inline constexpr std::size_t kMaxTrafficPerState = 32;

struct TrafficStates { // State channel, ~10 Hz: the vehicles that move
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::vector<TrafficEntry> cars;
  friend bool operator==(const TrafficStates &, const TrafficStates &) = default;
};

inline constexpr std::uint8_t kTrafficAway = 0;    // out of range, despawned by its game
inline constexpr std::uint8_t kTrafficTaken = 1;   // `player` got in: it is that player's car now
inline constexpr std::uint8_t kTrafficRemoved = 2; // gone for good (a wreck cleaned up)
struct TrafficGone {                               // Event channel
  TrafficId id{};
  std::uint8_t reason{}; // kTraffic{Away,Taken,Removed}
  PlayerId player{};     // taken: whose car it is now
  friend bool operator==(const TrafficGone &, const TrafficGone &) = default;
};

// How a shared vehicle stands, sent by its owner when it changes (Event
// channel; kept for late joiners until gone): a wreck stays a wreck.
struct TrafficStatus {
  TrafficId id{};
  float health{1.f};     // fraction of its maximum, 0..1
  std::uint16_t flags{}; // kTraffic* under kTrafficStatusMask
  friend bool operator==(const TrafficStatus &, const TrafficStatus &) = default;
};

// A player's game hit a shared vehicle (its proxy): bullets, an explosion,
// or the car it drives ramming it. That game computed the damage and ran its
// own car's side of a collision; the vehicle's owner applies the damage and
// the push to the real one, and the outcome (TrafficStates, TrafficStatus,
// its driver's reaction) comes back to everyone. To the owner through the
// host, never relayed to the others.
inline constexpr std::uint8_t kTrafficHitCollision = 1; // a ram: `impulse` is what our car gave it
inline constexpr std::uint8_t kTrafficHitExplosion = 2;
inline constexpr std::uint8_t kTrafficHitMelee = 4;
struct TrafficHit {  // Event channel
  PlayerId player{}; // who: set by the host from the connection
  TrafficId id{};
  float damage{};       // health points
  std::uint8_t flags{}; // kTrafficHit*
  Vec3 position;        // where it landed (world)
  Vec3 impulse;         // collision: the velocity change given, m/s; else the way it went
  friend bool operator==(const TrafficHit &, const TrafficHit &) = default;
};

// A player gets into a shared vehicle (steals it, or takes an empty one):
// to its owner through the host. The first claim wins: the owner answers
// everyone with TrafficGone{taken, player}; a later claimer hears the same
// and knows it lost. From then on it is that player's car (Seat,
// VehicleState), and whoever sat in it is out (NpcSeat).
struct TrafficClaim { // Event channel
  PlayerId player{};  // who: set by the host from the connection
  TrafficId id{};
  std::uint64_t seat{}; // the slot's CName hash
  friend bool operator==(const TrafficClaim &, const TrafficClaim &) = default;
};

// Opens a resync of the shared vehicles: every one its owner shares now.
// The receiver drops what it has beyond these (kept with the NPC domain).
inline constexpr std::size_t kMaxTrafficSet = 512;
struct TrafficSet {
  std::vector<TrafficId> ids;
  friend bool operator==(const TrafficSet &, const TrafficSet &) = default;
};

// NpcSeat::owner names a shared vehicle instead of a player's car when this
// bit is set: the rest is the TrafficId (drivers and passengers of traffic
// and police cars are NPCs like any other, seated the same way).
inline constexpr PlayerId kSeatInTraffic = 0x80000000u;
constexpr PlayerId TrafficSeatOwner(TrafficId id) { return kSeatInTraffic | id; }
constexpr bool IsTrafficSeat(PlayerId owner) { return (owner & kSeatInTraffic) != 0; }
constexpr TrafficId TrafficOfSeat(PlayerId owner) { return owner & ~kSeatInTraffic; }

// ---- Police (docs/architecture/police.md) ----
//
// Heat is per player: each game's own prevention system scores its own V's
// crimes and owns that V's wanted level. Sent by that game when it changes
// (Event channel; the host stamps the player, keeps the latest for late
// joiners and relays it). The owner of the area the V is in answers a heat
// above 0 with police, hostile to that V alone; a player that leaves is no
// longer wanted.
enum class HeatState : std::uint8_t { // EStarState
  Default = 0,
  Active = 1,    // the police see the V
  Searching = 2, // they lost it: grey stars
  Blinking = 3,  // about to give up
};
inline constexpr std::uint8_t kMaxHeat = 5;
struct Heat {
  PlayerId player{};
  std::uint32_t revision{};
  std::uint8_t stage{}; // EPreventionHeatStage: 0 none, 1..5 stars
  HeatState state{};
  friend bool operator==(const Heat &, const Heat &) = default;
};

// A crime of a remote V that only the game running the witnesses can see:
// one of its NPCs called the police on the V's puppet, or its police saw
// the puppet attack. That game never blames its own V for it: it tells the
// offender's game, whose prevention system scores it as its own. Area owner
// to the offender only, through the host; never relayed to anyone else.
enum class CrimeKind : std::uint8_t {
  Witness = 1, // an NPC saw it and called it in (PreventionCrimeWitnessRequest)
  Combat = 2,  // the police were attacked or drawn on (PreventionCombatStartedRequest)
  Damage = 3,  // damage to a civilian, an officer or a vehicle the offender's game did not see
  Kill = 4,    // the same, deadly
};
inline constexpr std::uint8_t kCrimePolice = 1;  // the victim is police
inline constexpr std::uint8_t kCrimeVehicle = 2; // the victim is a vehicle (`traffic`)
struct Crime {                                   // Event channel
  PlayerId player{};                             // the offender
  CrimeKind kind{};
  std::uint8_t flags{};      // kCrime*
  std::uint8_t attackType{}; // gamedataAttackType (Damage, Kill)
  float amount{};            // Damage: the share of the victim's health, 0..1
  NpcId npc{};               // the victim or the witness, as shared (0: not shared)
  TrafficId traffic{};       // the vehicle hit (kCrimeVehicle)
  Vec3 position;             // where the offender was
  friend bool operator==(const Crime &, const Crime &) = default;
};

// ---- Presence (docs/architecture/presence.md) ----

// What the others' party HUD shows of a V that no other message carries,
// sent by that player's game when it changes (Event channel; the host
// stamps the player, keeps everyone's latest for late joiners and relays
// it). `health` is the share of its health pool, 0..255 (0: none left;
// presence.hpp HealthCode). A fall of it is also how the others' puppets
// know their V was hurt (the grunt, the flinch). Not part of a digest: the
// next change repairs it.
inline constexpr std::uint8_t kPresenceMenu = 1;   // in a full-screen menu (inventory, map…)
inline constexpr std::uint8_t kPresencePhoto = 2;  // in photo mode
inline constexpr std::uint8_t kPresenceCombat = 4; // in combat
struct Presence {
  PlayerId player{};
  std::uint32_t revision{};
  std::uint8_t health{255};
  std::uint8_t flags{}; // kPresence*
  friend bool operator==(const Presence &, const Presence &) = default;
};

// Something a V did at one moment that neither its pose nor another message
// tells the others, for them to see and hear on its puppet (State channel:
// a late one is worth nothing; the host stamps the player and relays it).
// Footsteps, jumps, landings, climbs, dashes, draws and pain are NOT sent:
// every game derives them from PlayerState, Held, Vitals, TimeEffect and
// Presence (presence.hpp Observer).
enum class CueKind : std::uint8_t {
  // A shot's end, right after its Shot: `at` is where the bullet stopped
  // (the tracer's end, the impact and its decal); arg: kTrace*.
  Trace = 1,
  // A melee swing landed: `at` where; arg: kTrace* (what it hit).
  MeleeHit = 2,
};
inline constexpr std::uint8_t kTraceLeftHand = 1;  // from the left hand's weapon
inline constexpr std::uint8_t kTraceCharacter = 2; // it ended on a character (flesh, no decal)
inline constexpr std::uint8_t kTraceNothing = 4;   // it hit nothing: `at` is as far as it flew
struct Cue {
  PlayerId player{};
  std::uint32_t seq{};
  CueKind kind{};
  std::uint8_t arg{};
  Vec3 at;
  friend bool operator==(const Cue &, const Cue &) = default;
};

// How one of the host's NPCs reacts to a hit, in the game's own terms: the
// hit reaction its HitReactionComponent and AI chose (a twitch, an impact, a
// stagger, a knockdown, its death animation, a ragdoll), with the animation
// feature that plays it (AnimFeature_HitReactionsData). A guest's brain-off
// proxy cannot choose one (the reaction tasks are its AI's): it plays the
// host's. Host to guests. A twitch goes on the State channel (many, and a
// late one is worth nothing), the others on the Event channel (a lost
// knockdown or death would leave the proxy standing).
inline constexpr std::uint8_t kReactionRagdollEnd = 0; // it is out of its ragdoll
inline constexpr std::uint8_t kReactionTwitch = 1, kReactionRagdoll = 6, kReactionDeath = 7,
                              kReactionMax = 11;  // animHitReactionType
inline constexpr std::uint8_t kReactionTurns = 1; // useInitialRotation
struct NpcReaction {
  NpcId npc{};
  PlayerId by{};           // whose hit (kNoPlayer: an NPC's, the world's, unknown)
  std::uint8_t type{};     // animHitReactionType (Twitch 1, Impact 2, Stagger 3, Pain 4,
                           // Knockdown 5, Ragdoll 6, Death 7, Block 8, GuardBreak 9, Parry 10,
                           // Bump 11), or kReactionRagdollEnd
  std::int8_t direction{}; // EAIHitDirection (Left 0, Back 1, Right 2, Front 3)
  std::int8_t intensity{}; // EAIHitIntensity (Light 0, Medium 1, Heavy 2, Explosion 3)
  std::int8_t bodyPart{};  // EAIHitBodyPart (Head 1 … RightLeg 6)
  std::int8_t variation{}; // animVariation
  std::int8_t source{};    // EAIHitSource (Ranged 0, MeleeSharp 1, MeleeBlunt 2, QuickMelee 3)
  std::uint8_t stance{};   // 0 standing, 1 crouching
  std::uint8_t moveSpeed{}, moveDirection{}; // npcMovementSpeed, npcMovementDirection
  std::uint8_t flags{};                      // kReaction*
  float angle{};                             // angleToAttack
  Vec3 way;                                  // hitDirectionWs; a ragdoll: the impulse that threw it
  friend bool operator==(const NpcReaction &, const NpcReaction &) = default;
};

// What a game has installed, said once by each side after the Welcome (guest
// to host, host to guest; never relayed): tandem/compat.hpp compares the two
// and warns the players. 0 and "": unknown, never compared.
inline constexpr std::size_t kMaxInstallText = 64;
struct Install {           // Event channel
  std::string build;       // tandem's build, "0.2.0+2fa03ca"
  std::string game;        // the game executable's version, "3.0.88.46235"
  std::uint64_t scripts{}; // r6/scripts/tandem: names and contents
  std::uint64_t tweaks{};  // r6/tweaks/tandem
  std::uint64_t mods{};    // the names of the other mods installed
  std::uint16_t modCount{};
  friend bool operator==(const Install &, const Install &) = default;
};

// ---- Thrown objects, the session's rules, defence (docs/architecture/combat-coverage.md) ----

// Our V threw or launched an object whose flight the others must see: a
// grenade, a throwing knife or axe, a round of the arm launcher, or a smart
// weapon's guided projectile. The thrower's game owns the real one (its
// flight, its hits, its explosion: hits on proxies go as NpcStrike, on
// another V as AreaHit); every other game launches a copy from the
// thrower's puppet whose hits are dropped (a remote V's puppet never deals
// damage on our game) and ends it where the real one did (ThrowEnd). The
// copy's own non-damage effects stay: smoke blinds the host's NPCs, its
// explosion is heard by them. Event channel, a one-shot, except Smart
// (State: one per shot, a late one is worth nothing); the host stamps the
// thrower and relays it to everyone else.
enum class ThrowKind : std::uint8_t { Grenade = 0, Knife = 1, Launcher = 2, Smart = 3 };
inline constexpr std::uint8_t kThrowSticky = 1, kThrowHoming = 2, kThrowCharged = 4,
                              kThrowQuick = 8;
struct Throw {
  PlayerId player{};    // the thrower: set by the host from the connection
  std::uint32_t id{};   // the thrower's number for this object (ThrowEnd names it)
  ThrowKind kind{};     //
  std::uint8_t flags{}; // kThrow*
  std::uint64_t item{}; // TweakDB id: the grenade, the knife, the round, the smart weapon
  Vec3 origin;          // where it left the hand
  Vec3 velocity;        // its launch velocity (m/s): the copy's direction and speed
  NpcId npc{};          // what it homes on: a host NPC (0: none)...
  PlayerId at{};        // ...or a V (kNoPlayer: none)
  friend bool operator==(const Throw &, const Throw &) = default;
};

// The real object of a Throw came to its end: it exploded, stuck (a knife
// in a wall or a body) or is gone (picked up by its owner, despawned). The
// copies end the same way at the same place. Event channel, a one-shot;
// relayed like Throw.
enum class ThrowEndKind : std::uint8_t { Exploded = 0, Stuck = 1, Gone = 2 };
struct ThrowEnd {
  PlayerId player{};
  std::uint32_t id{};
  ThrowEndKind how{};
  Vec3 position;
  friend bool operator==(const ThrowEnd &, const ThrowEnd &) = default;
};

// The session's rules, the host's (tandem.ini): every game plays by them.
// Host to everyone, on change, kept for late joiners, in the world digest.
// kRuleFriendlyFire: a V's hits (shots, blades, explosions) hurt the other
// Vs (AreaHit carries every one); off, they never do (the default: as
// vanilla's companions, a partner is never hurt by V).
// kRuleEnemyScaling: hostile NPCs are tougher for each extra player near
// them (tandem/scaling.hpp, NpcScale; docs/architecture/npcs.md, "Enemy
// scaling"), their health by `enemyHealth` percent per extra player.
// (enemyHealth was added before Rules was released: no version bump.)
inline constexpr std::uint8_t kRuleFriendlyFire = 1, kRuleEnemyScaling = 2;
inline constexpr std::uint8_t kRuleFlags = kRuleFriendlyFire | kRuleEnemyScaling;
inline constexpr std::uint8_t kMaxEnemyHealth = 250; // percent per extra player
struct Rules {
  std::uint8_t flags{};         // kRule*
  std::uint8_t enemyHealth{60}; // kRuleEnemyScaling: health percent per extra player
  friend bool operator==(const Rules &, const Rules &) = default;
};

// A shared NPC's attack on our V, run on our game (a PlayerHit, a shot
// fired from its proxy), was blocked or parried there by our rules: the NPC
// gets, in its owner's game, what vanilla's attacker gets then
// (DamageManager .ProcessDefensiveState: the "attack blocked" or "attack
// parried" animation event, AI event and signal: its stagger, its next
// move). To the NPC's owner (the host, or a guest for an NPC of its own:
// regions.md), a guest's through the host; behind the epoch fence.
enum class DefendKind : std::uint8_t { Blocked = 1, Parried = 2 };
struct Defended {
  PlayerId player{}; // who defended: set by the host from the connection
  NpcId npc{};       // the attacker
  DefendKind how{};
  friend bool operator==(const Defended &, const Defended &) = default;
};

// What a V is busy with outside combat and quests (docs/architecture/
// activities.md; core activity.hpp): a workspot (a chair, a bar stool, a
// bed, a shower, an arcade machine), a screen it reads, a minigame, a shop
// or the ripperdoc's chair, a menu that stops its own game, photo mode. Sent
// by that player's game when it changes (Event channel); the host stamps the
// sender, keeps everyone's for late joiners (a player who leaves, or comes
// back from a crash, does nothing) and relays it. The pose stream still
// carries the body's animation; this says where the body must stay
// (kActivityAnchored: the others hold the puppet exactly there, so it never
// floats or slides off its seat), which workspot it plays (so a puppet with
// no pose stream plays the same one) and whether that game stands still
// (kActivityFrozen: its poses stop mid-step, so the others let the puppet
// idle on its own graph instead). Part of the players domain of the host's
// digests.
enum class ActivityKind : std::uint8_t {
  None = 0,
  Workspot = 1, // sitting, leaning, using a device: `workspot`, `entry`, `anim`
  Sleep = 2,    // lying in a bed (a workspot too)
  Screen = 3,   // reading a computer, a terminal, a shard
  Minigame = 4, // breach protocol, an arcade game
  Shop = 5,     // a vendor, the ripperdoc, the wardrobe, the mirror
  Menu = 6,     // the hub menus, the map, the time skip wheel
  Photo = 7,    // photo mode
  Contest = 8,  // a duel or a contest the others must leave alone (`entry`: its ContestSort)
};
// What a Contest is (Activity::entry, which names no workspot then; engine
// facts: research/engine/story.md §13.92). Read from the game's own data
// on the contestant's V: a fist fight's restriction, a shooting
// competition's, the racing system's word. `device` is the session's number
// of an opponent (an NpcId; 0: none, or more than one).
enum class ContestSort : std::uint8_t {
  Other = 0,
  Duel = 1,  // a fist fight: the others' hits on its opponents are dropped
  Race = 2,  // a street race
  Range = 3, // a shooting competition
};
inline constexpr std::uint8_t kActivityAnchored = 1; // `anchor`, `yaw`: hold the body there
inline constexpr std::uint8_t kActivityLying = 2;    // lying, not sitting or standing
inline constexpr std::uint8_t kActivityFrozen = 4;   // that game stands still meanwhile
inline constexpr std::uint8_t kActivityFlags = 7;
struct Activity {
  PlayerId player{};
  ActivityKind kind{};
  std::uint8_t flags{};     // kActivity*
  std::uint64_t device{};   // the device used: its EntityID hash (0: none)
  std::uint64_t workspot{}; // the workspot resource's path hash (0: unknown)
  std::uint32_t entry{};    // its tree entry (Contest: its ContestSort)
  std::uint64_t anim{};     // the animation playing (CName hash)
  Vec3 anchor;              // kActivityAnchored: where the body is
  float yaw{};              // its heading (radians, sync::YawOf)
  friend bool operator==(const Activity &, const Activity &) = default;
};

// Skipping time (docs/architecture/activities.md, "Time skips"; core
// activity.hpp): the world clock is the host's and shared, so a skip is
// never one player's alone. A game asks (TimeSkip, to the host, which stamps
// `from` and relays it to every other player as a prompt), each other
// player answers (TimeSkipVote, to the host, relayed to the asker), and the
// host says how it ended (TimeSkipResult, to everyone): applied when every
// other player accepted, else nothing moves. One request at a time; it
// lapses unanswered. Event channel, one-shots (not state, in no digest: the
// clock itself is in the world digest).
enum class SkipReason : std::uint8_t {
  Wait = 0,  // the time skip wheel
  Sleep = 1, // a bed
  Quest = 2, // the host's story moved the clock: said, not asked
};
struct TimeSkip {
  PlayerId from{};
  std::uint32_t request{}; // the asker's count of requests: votes and the result name it
  SkipReason reason{};
  std::int32_t fromSeconds{}; // the asker's clock when it asked (GameTime seconds, as World)
  std::int32_t toSeconds{};   // the game time asked for
  friend bool operator==(const TimeSkip &, const TimeSkip &) = default;
};

// Why a skip is declined or refused (TimeSkipVote::why, TimeSkipResult::why).
inline constexpr std::uint8_t kSkipNoReason = 0, kSkipBusy = 1, // another request is open
    kSkipCombat = 2,      // that player's V cannot skip now (the game's own conditions)
    kSkipDown = 3,        // a V is down or out
    kSkipTimeAbility = 4, // a Sandevistan (or the like) is on
    kSkipElsewhere = 5,   // a V is in cyberspace, a braindance, a flashback
    kSkipPaused = 6,      // someone is in the pause menu
    kSkipNotHost = 7,     // only the host's story moves the clock unasked
    kSkipWhyMax = 7;
struct TimeSkipVote {
  PlayerId from{};         // the voter: set by the host from the connection
  PlayerId asker{};        // whose request
  std::uint32_t request{}; // which one
  bool accepted{};         // the asker's own `false` withdraws the request
  std::uint8_t why{};      // kSkip*: why not
  friend bool operator==(const TimeSkipVote &, const TimeSkipVote &) = default;
};

enum class SkipOutcome : std::uint8_t {
  Applied = 0,   // the host's clock is at `toSeconds`: every game fast-forwards its V
  Declined = 1,  // `by` said no
  Lapsed = 2,    // `by` never answered
  Withdrawn = 3, // the asker took it back, or left
  Refused = 4,   // never asked around: `why`
};
struct TimeSkipResult {
  PlayerId asker{};
  std::uint32_t request{};
  SkipReason reason{};
  SkipOutcome outcome{};
  PlayerId by{};              // who declined, never answered or blocks it (kNoPlayer: nobody)
  std::uint8_t why{};         // kSkip*
  std::int32_t fromSeconds{}; // the asker's clock when it asked
  std::int32_t toSeconds{};   // the time asked for: each game moves its clock by the difference
  friend bool operator==(const TimeSkipResult &, const TimeSkipResult &) = default;
};

// ---- Progression (docs/architecture/progression.md) ----
// A V's character level (gamedataStatType.PowerLevel, 1..kMaxLevel), from
// its own game when it changes (Event channel). The host stamps the player
// from the connection and keeps it: enemies are scaled to the mean of the
// session's levels (tandem/progress.hpp). Never relayed: a level is its
// player's own business; guests hear the mean (SessionLevel).
inline constexpr std::uint8_t kMaxLevel = 100;
struct PlayerLevel { // Event channel
  PlayerId player{};
  std::uint8_t level{};
  friend bool operator==(const PlayerLevel &, const PlayerLevel &) = default;
};

// The level the host scales its NPCs to: the mean of the levels of the
// players in the session (the host's included), and how many they are.
// Host to everyone when it changes (a join, a leave, a level-up); kept for
// late joiners.
struct SessionLevel { // Event channel
  std::uint8_t level{};
  std::uint8_t players{};
  friend bool operator==(const SessionLevel &, const SessionLevel &) = default;
};

// A shared NPC's power level as its owner's game has it, scaling included:
// the proxies take the same, so every game computes a hit against the same
// health, armour and damage. The owner (the host, or a guest for an NPC of
// its own: regions.md) to the games that show it, through the host, when it
// is first known and when it changes; kept for late joiners until the NPC
// is gone.
struct NpcLevel { // Event channel
  NpcId npc{};
  float level{};
  friend bool operator==(const NpcLevel &, const NpcLevel &) = default;
};

// ---- Vendors (docs/architecture/progression.md, "Vendors") ----
// A shared NPC is a vendor placed in the world (a shop, a ripperdoc, a
// clothing store): its entity's static id, the same in every game. A player
// whose game shows it as a proxy opens its own game's vendor of that entity,
// as in single player: its own stock, prices, street cred gates and money
// (tandem/vendors.hpp). Its owner to the players linked with it, once per
// NPC, with the NPC's other records (late joiners, a link that forms); kept
// until the NPC is gone.
//
// kVendorDead (npc 0): the host's word that the vendor of that entity died
// in the session, to everyone. Each game keeps its own copy hidden until the
// session ends (the same dead stay dead); kept for the session, for late
// joiners. Never a guest's to say: the host derives it from the owner's
// NpcGone.
inline constexpr std::uint8_t kVendorDead = 1;
struct NpcVendor { // Event channel
  NpcId npc{};
  std::uint64_t entity{}; // EntityID hash (static: placed in the world)
  std::uint8_t flags{};   // kVendorDead
  friend bool operator==(const NpcVendor &, const NpcVendor &) = default;
};

// ---- Trade between two players (docs/architecture/trade.md) ----
// Two players standing together give, swap or sell items and eddies. The
// host arbitrates (tandem/trade.hpp): one asks, the other accepts, each
// puts up an offer, both confirm the same pair of offers, the host commits
// and each game changes its own V's inventory, once. Event channel, every
// message a one-shot named by its trade's id.
using TradeId = std::uint32_t;
// The asker's game numbers its trades: its player id above, its own count
// below (the host checks the id is the sender's to make).
inline constexpr std::uint32_t kTradeIdShift = 20;
inline constexpr TradeId MakeTradeId(PlayerId asker, std::uint32_t count) {
  return (asker << kTradeIdShift) | (count & ((1u << kTradeIdShift) - 1));
}
inline constexpr PlayerId TradeAsker(TradeId id) { return id >> kTradeIdShift; }

inline constexpr std::size_t kMaxTradeItems = 12; // stacks in one offer
inline constexpr std::size_t kMaxTradeStats = 12; // saved stats of one item
inline constexpr std::size_t kMaxTradeParts = 8;  // parts installed in one item
inline constexpr std::uint32_t kMaxTradeMoney = 99'999'999;

// What makes an item more than its record and seed: a stat its game saved
// on it (quality, tier, upgrades, level: gamedataStatType and its value)...
struct TradeStat {
  std::uint16_t stat{};
  float value{};
  friend bool operator==(const TradeStat &, const TradeStat &) = default;
};
// ...and a part the player installed in one of its slots (a mod, a scope).
struct TradePart {
  std::uint64_t slot{};   // TweakDB id of the attachment slot
  std::uint64_t record{}; // TweakDB id of the part's item record
  std::uint32_t seed{};   // its own random seed (0: none)
  friend bool operator==(const TradePart &, const TradePart &) = default;
};
struct TradeItem {
  ItemStack item; // record, seed, quantity (never 0)
  std::vector<TradeStat> stats;
  std::vector<TradePart> parts;
  friend bool operator==(const TradeItem &, const TradeItem &) = default;
};

enum class TradeVerb : std::uint8_t {
  Request,  // `from` asks `to` to trade (answered within 15 s, or it lapses)
  Accept,   // `to` agrees: the trade is open
  Confirm,  // I agree to the offers at revisions (mine, theirs)
  Withdraw, // I no longer do
  Close,    // the trade is over, nothing changed hands (reason)
  Applied,  // I applied the commit (to the host only)
};
enum class TradeReason : std::uint8_t {
  None,
  Declined,  // the one asked said no
  Cancelled, // a party closed it
  Lapsed,    // the request was not answered in time
  Away,      // the two are no longer together
  Combat,    // a party is fighting
  Down,      // a party is down or out
  Gone,      // a party left the session
  Busy,      // a party is in another trade
  Invalid,   // an offer the host refused, or a table that cannot be committed
  Idle,      // nobody touched it for too long
  Over,      // the host knows no such trade (any more)
  Off,       // a party's game does not trade (tandem.ini)
};
struct Trade {
  TradeId id{};
  PlayerId from{}; // who says it: set by the host from the connection
  PlayerId to{};   // the other party
  TradeVerb verb{};
  TradeReason reason{};
  std::uint32_t mine{}, theirs{}; // Confirm: the offers' revisions agreed to
  friend bool operator==(const Trade &, const Trade &) = default;
};

// One party's side of the table, whole, each time it changes (a newer
// revision replaces the older; any change takes both confirmations back).
// `asks`: the eddies wanted in return (a sale): the other side's `money`
// must be at least that for the host to commit.
struct TradeOffer {
  TradeId id{};
  PlayerId player{}; // whose: set by the host from the connection
  std::uint32_t revision{};
  std::uint32_t money{};
  std::uint32_t asks{};
  std::vector<TradeItem> items;
  friend bool operator==(const TradeOffer &, const TradeOffer &) = default;
};

// The host's word: both confirmed these two offers; each party gives what
// its own says and takes what the other's says. Sent again until the party
// answers Applied; a game applies a trade id once.
struct TradeCommit {
  TradeId id{};
  TradeOffer first, second;
  friend bool operator==(const TradeCommit &, const TradeCommit &) = default;
};

// A vehicle came into the host's garage (a quest, a gig or a fixer gave
// it; docs/architecture/garage.md). Host to each guest that took part in
// the quest it belongs to (the reward rule), never to everyone, never from
// a guest: the guest's game credits it in its ledger, once (Event channel,
// with the story's messages).
struct VehicleUnlock {
  std::uint64_t vehicle{}; // TweakDB id of the vehicle record (the garage's)
  std::uint32_t quest{};   // journal hash of its quest (0: unknown)
  friend bool operator==(const VehicleUnlock &, const VehicleUnlock &) = default;
};

// Who may drive a player's own cars (docs/architecture/garage.md): each
// game says its own (tandem.ini `lend`), the host stamps who and keeps
// everyone's for late joiners; a repeat is applied nowhere; forgotten when
// the player goes. With the levels: per-player progression state.
enum class Lend : std::uint8_t {
  Crew = 0,      // any partner, the owner aboard or not (the default)
  WithOwner = 1, // only with the owner aboard
  Never = 2,     // nobody but the owner
};
struct CarLend {
  PlayerId player{};
  Lend lend{Lend::Crew};
  friend bool operator==(const CarLend &, const CarLend &) = default;
};

// ---- Comms (docs/architecture/comms.md): pings, shared tags, waypoints ----

// What a ping or a tag is about, named the same way in every game: one of the
// host's shared NPCs (its proxy on a guest), a placed entity (its static
// EntityID hash: the same in every game), or a player. A receiver that does
// not find it falls back to the position that comes with it.
enum class TargetKind : std::uint8_t { None = 0, Npc = 1, Entity = 2, Player = 3 };
struct TargetRef {
  TargetKind kind{};
  std::uint64_t id{}; // NpcId, EntityID hash or PlayerId
  friend bool operator==(const TargetRef &, const TargetRef &) = default;
};

// A ping: "look at this". Its kind picks the icon, sound and lifetime.
enum class MarkKind : std::uint8_t {
  None = 0, // withdrawn: the sender's mark `id` goes
  Spot = 1, // a place
  Enemy = 2,
  Device = 3,
  Loot = 4,
  Vehicle = 5,
  Danger = 6, // the same place or target, pinged twice in a row: "careful"
  // The team target ("focus this one"): the "Mark target" quickhack of a
  // linked player. One per sender; it replaces that sender's last one.
  Target = 7,
};

// The sender's game sends it to the host, which stamps `from`, holds each
// guest to a rate (comms::Limiter) and passes it to every other player
// (Event channel; a one-shot: never kept, never sent to a late joiner). A
// Mark with the id of one shown replaces it (a ping turned into a danger).
struct Mark {
  PlayerId from{};
  std::uint32_t id{}; // the sender's count of its marks
  MarkKind kind{};
  TargetRef target;
  Vec3 position;          // where it is (the target's place when sent)
  std::uint8_t seconds{}; // how long it shows (0: the kind's own; capped)
  friend bool operator==(const Mark &, const Mark &) = default;
};

// What a player scanned and tagged, or what its Ping quickhack revealed:
// outlined for the players linked with it (comms::Linked), and only those.
// The host stamps `from` and delivers it to them; a one-shot, like Mark.
enum class TagReason : std::uint8_t {
  Scan = 0,   // the scanner's tag, until untagged
  Reveal = 1, // a reveal (the Ping quickhack) for `seconds`
};
inline constexpr std::size_t kMaxTagTargets = 16;
struct Tag {
  PlayerId from{};
  TagReason reason{};
  bool on{};
  std::uint8_t seconds{}; // Reveal: how long (0: until said off)
  std::vector<TargetRef> targets;
  friend bool operator==(const Tag &, const Tag &) = default;
};

// Whom a player shares its tags with (the link, docs/architecture/comms.md):
// its own consent, sent by its game when it changes. Two players are linked
// when each consents to the other. The host stamps `player`, keeps
// everyone's for late joiners (players domain) and relays it.
enum class LinkMode : std::uint8_t {
  Off = 0,      // nobody
  Listed = 1,   // the players in `players`
  Everyone = 2, // everyone but the players in `players` (link = auto)
};
inline constexpr std::size_t kMaxLinkPlayers = 8;
struct Link {
  PlayerId player{};
  std::uint32_t revision{};
  LinkMode mode{};
  std::vector<PlayerId> players;
  friend bool operator==(const Link &, const Link &) = default;
};

// A link request its receiver's game did not take, and why: to the asker
// only (Event channel; a one-shot; the host stamps `from` and delivers it to
// `to`). The asker's consent stands, as for a request left unanswered.
// kLinkDeclineOther: another request came after it and took the call.
inline constexpr std::uint8_t kLinkDeclineOther = 1;
struct LinkDecline {
  PlayerId from{};
  PlayerId to{};
  std::uint8_t reason{}; // kLinkDecline*
  friend bool operator==(const LinkDecline &, const LinkDecline &) = default;
};

// Where a player's own waypoint is (the map pin it placed, or the map point
// it tracks; with neither, its tracked quest objective), shown to the others
// as "NAME's waypoint" ("NAME's objective"). Sent by its game when it
// changes (Event channel; kind None: no waypoint any more); the host stamps
// `player`, keeps everyone's for late joiners (players domain) and relays it.
enum class WaypointKind : std::uint8_t {
  None = 0,
  Pin = 1,       // a map pin (the world map's custom position)
  Point = 2,     // a point of the map it tracks (a vendor, a fast travel point...)
  Objective = 3, // its tracked objective's quest mappin, the nearest (v9)
};
struct Waypoint {
  PlayerId player{};
  std::uint32_t revision{};
  WaypointKind kind{};
  Vec3 position;
  friend bool operator==(const Waypoint &, const Waypoint &) = default;
};

// ---- Vehicle combat (docs/architecture/vehicle-combat.md) ----
//
// One rule for every vehicle: the game that simulates it owns it, its
// damage included. A player's car is its driver's game's (it stays so once
// parked: the last driver's); a shared world vehicle is its area owner's.
// The others show copies and send what they see happen to them (hits,
// bumps) to the owner, which applies it and says how the vehicle stands.
//
// A vehicle named across games: a player's car by that player's id, a
// shared world vehicle by kSeatInTraffic | its TrafficId (the NpcSeat
// encoding, so one number names either everywhere).
using VehicleRef = std::uint32_t;
constexpr VehicleRef PlayerCar(PlayerId player) { return player & ~kSeatInTraffic; }
constexpr VehicleRef StreetCar(TrafficId id) { return TrafficSeatOwner(id); }
constexpr bool IsStreetCar(VehicleRef vehicle) { return IsTrafficSeat(vehicle); }
// The game that owns a vehicle: its player, or its area owner.
constexpr PlayerId OwnerOfVehicle(VehicleRef vehicle) {
  return IsStreetCar(vehicle) ? TrafficOwner(TrafficOfSeat(vehicle)) : vehicle;
}

// How a vehicle is damaged, sent by its owner when it changes (Event
// channel; the host keeps the latest of each vehicle for late joiners until
// it goes, in the digests: player cars with the players, shared ones with
// the NPCs). Every copy is put in the same state: health (the game's damage
// stages: smoke, then fire, follow from it), the destruction grid (dents),
// the parts that came off, flat tyres, broken windows; destroyed with the
// time it blew up, so a copy that hears it at once blows up the game's way
// and one that hears it later (a late joiner, a resync) is put there as a
// wreck. For a shared world vehicle health and the wreck stay
// TrafficStatus's; this adds what TrafficStatus does not say.
inline constexpr std::uint8_t kDamageBurning = 1;   // on fire (health in its last stage)
inline constexpr std::uint8_t kDamageDestroyed = 2; // a wreck
inline constexpr std::uint8_t kDamageSubmerged = 4; // drowned
inline constexpr std::size_t kDamageGrid = 16;      // VehicleGridDestructionEvent.state
inline constexpr std::size_t kMaxDetachedParts = 16;
struct VehicleDamage {
  VehicleRef vehicle{};
  std::uint32_t revision{}; // the owner's count: an older one is never applied
  std::uint64_t record{};   // TweakDB id of the vehicle record (a check: the copy is that car)
  float health{1.f};        // fraction of its maximum, 0..1
  std::uint8_t flags{};     // kDamage*
  std::uint8_t tyres{};     // bit n: tyre n is flat (WheeledObject.ToggleBrokenTire index)
  std::uint8_t windows{};   // bit n: vehicleQuestWindowDestruction n is broken
  std::array<std::uint8_t, kDamageGrid> grid{}; // each cell 0..1 in 1/255
  std::vector<std::uint64_t> parts;             // CName hashes of the parts that came off
  std::uint64_t explodedUs{};                   // host clock when it blew up (0: it did not)
  PlayerId instigator{}; // whose doing its destruction was (kNoPlayer: none, an NPC's)
  friend bool operator==(const VehicleDamage &, const VehicleDamage &) = default;
};

// A player's game hit a vehicle another game owns (its copy or proxy
// there): bullets, a blast, a blade, a quickhack's damage. That game
// computed the damage and where it landed; the owner applies it to the real
// vehicle and answers everyone with VehicleDamage. To the owner through the
// host (which stamps the hitter), never relayed to anyone else. A ram on a
// shared world vehicle stays TrafficHit; this adds tyres and player cars.
inline constexpr std::uint8_t kVehicleHitExplosion = 1;
inline constexpr std::uint8_t kVehicleHitMelee = 2;
inline constexpr std::uint8_t kVehicleHitTyre = 4;   // `tyre` was shot out
inline constexpr std::uint8_t kVehicleHitWindow = 8; // `window` broke
struct VehicleHit {                                  // Event channel
  PlayerId player{}; // the hitter: set by the host from the connection
  VehicleRef vehicle{};
  float damage{};         // health points
  std::uint8_t flags{};   // kVehicleHit*
  std::int8_t tyre{-1};   // the tyre index (kVehicleHitTyre)
  std::int8_t window{-1}; // vehicleQuestWindowDestruction (kVehicleHitWindow)
  Vec3 position;          // where it landed (world)
  Vec3 direction;         // the way it went
  friend bool operator==(const VehicleHit &, const VehicleHit &) = default;
};

// How a V left a vehicle when it was not a plain exit: jumped out of a car
// on the move, thrown out by a crash or off a bike, died in the seat, or the
// vehicle blew up under it. Sent by that V's game (Event channel; the host
// stamps and relays; not kept: it is of the moment), before its Seat says
// it is on foot. With kExitRolling the car went on without a driver: its
// game (the one simulating it) keeps sending its VehicleState until it
// stops, and the others keep their copy following instead of parking it.
enum class ExitKind : std::uint8_t {
  Normal = 0,
  Bail = 1,     // jumped out of a moving car (SpeedExitingEvents)
  Thrown = 2,   // ejected by a crash (CollisionExitingEvents, a car)
  Knocked = 3,  // thrown off a bike
  Dead = 4,     // died in the seat (DeathExitingEvents)
  Exploded = 5, // the vehicle blew up with the V in it
};
inline constexpr std::uint8_t kExitRolling = 1; // the vehicle rolls on: its state keeps coming
inline constexpr std::uint8_t kExitDriver = 2;  // the V was its driver
struct VehicleExit {
  PlayerId player{};
  VehicleRef vehicle{}; // the vehicle left (a player's car, or a shared one)
  ExitKind kind{};
  std::uint8_t flags{}; // kExit*
  Vec3 velocity;        // the V's when it left (the throw, the roll)
  friend bool operator==(const VehicleExit &, const VehicleExit &) = default;
};

// What a V does with weapons in a vehicle (the game's vehicle states:
// driver combat, a passenger leaning out, the vehicle's own weapons, a
// turret). Sent by that V's game when it changes (Event channel; the host
// stamps, keeps the latest of each player for late joiners, a player's goes
// with it, and relays; in the players digest). Its shots are Shot (its
// hands) and MountedFire (the vehicle's weapons); its hits are its own, as
// on foot.
enum class SeatCombat : std::uint8_t {
  None = 0,
  Driver = 1,    // driver combat with a gun (DriverCombatFirearmsEvents)
  Passenger = 2, // a passenger shooting out of the window (vehicle state Combat)
  Mounted = 3,   // the driver on the vehicle's own weapons (DriverCombatMountedWeaponsEvents)
  Gunner = 4,    // a turret or gunner seat (GunnerEvents, the Basilisk's gun)
};
struct VehicleCombat {
  PlayerId player{};
  std::uint32_t revision{};
  SeatCombat mode{};
  VehicleRef vehicle{};   // the vehicle it is in
  std::uint64_t weapon{}; // TweakDB id of the weapon in use (a gun, a mounted weapon; 0: none)
  friend bool operator==(const VehicleCombat &, const VehicleCombat &) = default;
};

// One shot of a vehicle's own weapon (a mounted gun, a missile pod, the
// Basilisk's cannon), fired by the V who uses it (the driver, or the
// gunner of someone else's vehicle on its copy). State channel, as Shot: a
// late one is worth nothing. The others play it on their copy for its
// effects; what it hits is the shooter's game's to say, as for any shot.
struct MountedFire {
  PlayerId player{}; // the shooter: set by the host from the connection
  std::uint32_t seq{};
  VehicleRef vehicle{};
  std::uint64_t weapon{}; // VehicleMountedWeaponShootEvent.weaponID
  std::uint64_t slot{};   // VehicleMountedWeaponShootEvent.slotID
  Vec3 target;            // where it was aimed (world; zero: unknown)
  friend bool operator==(const MountedFire &, const MountedFire &) = default;
};

// A V asks for the gunner seat of another player's vehicle (the turret of
// a Basilisk, a mounted gun): to the vehicle's owner, through the host
// (which stamps the asker). The owner gives the seat to the first who asks
// and answers everyone with Gunner. `leave`: let it go.
struct GunnerRequest { // Event channel
  PlayerId player{};   // the asker: set by the host from the connection
  VehicleRef vehicle{};
  std::uint64_t seat{}; // the slot's CName hash
  bool leave{};
  friend bool operator==(const GunnerRequest &, const GunnerRequest &) = default;
};

// Who holds a vehicle's gunner seat, said by the vehicle's owner (Event
// channel; the host keeps it while held, for late joiners, frees it when
// the gunner or the owner goes, and relays it; in the players digest).
// The gunner's game aims and fires from its copy (GunnerAim, MountedFire)
// and decides its hits; the owner turns its real turret the same way.
struct Gunner {
  VehicleRef vehicle{};
  PlayerId gunner{};    // kNoPlayer: free
  std::uint64_t seat{}; // the slot's CName hash
  std::uint32_t revision{};
  friend bool operator==(const Gunner &, const Gunner &) = default;
};

// Where the gunner aims, relative to the vehicle (State channel, ~20 Hz,
// from the gunner's game only; the host relays it while the seat is that
// player's). Every game turns its copy's turret, the owner its real one.
inline constexpr std::uint8_t kAimFiring = 1; // the trigger is held
struct GunnerAim {
  PlayerId player{}; // set by the host from the connection
  std::uint32_t seq{};
  std::uint64_t timeUs{}; // host clock
  VehicleRef vehicle{};
  float yaw{};   // radians, from the vehicle's forward, left positive
  float pitch{}; // radians, up positive
  std::uint8_t flags{};
  friend bool operator==(const GunnerAim &, const GunnerAim &) = default;
};

// Two players' cars touched. Each driver's game runs its own car's side
// against the other's copy (which stands like a wall for the instant); the
// push our car gave goes to the other driver's game, which applies it to
// its real car unless it felt the same contact itself (vehicle_combat.hpp,
// BumpLedger). To `other` through the host, which stamps `player`.
struct VehicleBump {    // Event channel
  PlayerId player{};    // whose car bumped: set by the host from the connection
  PlayerId other{};     // whose car was hit
  Vec3 impulse;         // the velocity change our car gave theirs, m/s
  Vec3 position;        // where they touched
  std::uint64_t atUs{}; // host clock of the contact
  friend bool operator==(const VehicleBump &, const VehicleBump &) = default;
};

// A player's car ran into one of the area owner's NPCs (its proxy): the
// driver's game ran the hit (the damage, as NpcStrike); this is its
// physical half, for the real NPC: knocked down and thrown along the car's
// velocity, or killed by the game's own rule (a car this fast kills). To
// the area owner through the host; never relayed.
inline constexpr std::uint8_t kRamKnockdown = 1; // fast enough to knock it down
inline constexpr std::uint8_t kRamLethal = 2;    // fast enough to kill it outright
struct NpcRam {                                  // Event channel
  PlayerId player{};                             // the driver: set by the host from the connection
  NpcId npc{};
  Vec3 velocity; // the car's, m/s
  Vec3 position; // where it hit
  std::uint8_t flags{};
  friend bool operator==(const NpcRam &, const NpcRam &) = default;
};

// One of the area owner's vehicles is chasing a player (its NPCs are in
// combat with that V, the game's own InVehicleCombatDecorator drives after
// it). Sent by the owner when the chase starts, changes target or ends
// (kNoPlayer); the host keeps it while it lasts, for late joiners, with the
// NPCs (digest), and it ends when the vehicle or the target goes. The
// target's game treats the proxy as a hostile vehicle.
inline constexpr std::uint8_t kChaseRamming = 1;  // it rams (aggressiveRamming)
inline constexpr std::uint8_t kChaseShooting = 2; // its passengers fire
struct Chase {
  TrafficId vehicle{};
  PlayerId target{}; // kNoPlayer: over
  std::uint8_t flags{};
  friend bool operator==(const Chase &, const Chase &) = default;
};

// ---- Story restrictions (docs/architecture/story.md, "Restrictions") ----
//
// The gameplay restrictions the host's story has on the host's V now: the
// TweakDB ids of its GameplayRestriction status effects (no weapons, no
// sprint, no jump, forced walk...) that a quest put on, not the ones a
// static zone or tandem itself applied (Restrictions.reds decides). Host to
// guests, the whole set on every change (sorted; empty: none), a story
// message: kept by the host for late joiners and in the story digest. A
// guest in the host's story moment (story::InMoment) puts the same effects
// on its own V while the set has them.
inline constexpr std::size_t kMaxStoryRestrictions = 32;
struct StoryRestrictions {
  PlayerId player{}; // whose V (the host): set by the host's endpoint
  std::vector<std::uint64_t> effects;
  friend bool operator==(const StoryRestrictions &, const StoryRestrictions &) = default;
};

// ---- Netcode (docs/architecture/netcode.md): compact forms of the two
// largest streams. Endpoint encodes and decodes them; everything above it
// (events, recordings, the game) sees PoseState and NpcStates as before.

// A body pose, delta coded (tandem/pose.hpp PoseEncoder). A key frame (key
// == seq) has every bone whole; the others say per bone (2 bits of `codes`,
// slot n at bits 2n) whether it is the key's (kBoneKept), a small turn from
// the key's (kBoneTurn: a rotation vector, 3 signed bytes of
// kPoseTurnStep rad: up to 0.5 rad), or whole (kBoneWhole: a smallest-three quaternion in
// 5 bytes), in slot order in `rotations`. State channel.
inline constexpr std::size_t kPoseFrameRotationBytes = 5;
inline constexpr std::size_t kPoseTurnBytes = 3;
inline constexpr float kPoseTurnStep = 1.f / 256.f;
inline constexpr std::size_t kPoseCodeBytes = (kPoseBones * 2 + 7) / 8;
inline constexpr std::uint8_t kBoneKept = 0, kBoneTurn = 1, kBoneWhole = 2;
struct PoseFrame {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::uint32_t key{}; // seq of its key frame (== seq: this is one)
  std::array<std::int16_t, 3> hips{};
  std::array<std::uint8_t, kPoseCodeBytes> codes{};
  std::vector<std::uint8_t> rotations;
  std::uint8_t Code(std::size_t slot) const { return (codes[slot / 4] >> (2 * (slot % 4))) & 3; }
  friend bool operator==(const PoseFrame &, const PoseFrame &) = default;
};

// The host's NPC positions, quantized and only those that changed (NpcCoder
// in tandem/netcode.hpp): each NPC that moved, turned or changed what it
// detects, and every NPC at least twice a second. The guest fills in the
// rest from what it last heard, so NpcStates comes out whole as before.
// Positions are 1/128 m steps from `origin` (±256 m), yaw 1/65536 turn.
struct NpcMove {
  NpcId npc{};
  std::array<std::int16_t, 3> offset{};
  std::uint16_t yaw{};
  std::uint8_t detected{};
  std::uint8_t flags{}; // netcode::kNpcUnlisted: no longer in the host's list
  friend bool operator==(const NpcMove &, const NpcMove &) = default;
};

struct NpcMotion { // State channel, ~10 Hz, host only
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 origin;
  std::vector<NpcMove> npcs;
  friend bool operator==(const NpcMotion &, const NpcMotion &) = default;
};

// PlayerState, compact (40 bytes, not 60): a V's orientation is its yaw
// (1/65536 turn), its velocity in cm/s. Endpoint sends this and gives
// PlayerState to everything above it. State channel.
struct PlayerMotion {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 position;
  std::uint16_t yaw{};
  std::array<std::int16_t, 3> velocity{}; // cm/s
  std::uint8_t stance{};
  std::uint16_t flags{};
  friend bool operator==(const PlayerMotion &, const PlayerMotion &) = default;
};

// VehicleState, compact: the orientation a 5-byte smallest-three
// quaternion, the velocity in cm/s. State channel.
struct VehicleMotion {
  PlayerId player{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 position;
  std::array<std::uint8_t, kPoseFrameRotationBytes> orientation{};
  std::array<std::int16_t, 3> velocity{}; // cm/s
  friend bool operator==(const VehicleMotion &, const VehicleMotion &) = default;
};

// ---- Enemy scaling (docs/architecture/npcs.md, "Enemy scaling") ----
// How many players an NPC's owner counts near it (tandem/scaling.hpp:
// within 100 m, fighting it or its target, with hysteresis): its owner's
// game scales its health, poise and armour by that count under the host's
// Rules, keeping its health and poise fractions; every proxy of it takes
// the same modifiers, so each game computes a hit against the same armour
// and the same health. Owner to everyone who sees it (a guest's through the
// host), when it changes; kept for late joiners until the NPC is gone; in
// the NPC digest.
struct NpcScale { // Event channel
  NpcId npc{};
  std::uint8_t players{}; // 1..kMaxPlayerId
  friend bool operator==(const NpcScale &, const NpcScale &) = default;
};

// ---- Breach Protocol (docs/architecture/devices.md, "Breach Protocol") ----
//
// The minigame is the breacher's own, as in single player (its UI, its
// timer, its rewards); what it did to the world is a request to the owner
// of what it affects. A game's own breach (a jack-in, the quickhack on a
// device or an NPC, or a mod that fakes the outcome) goes to the host:
// the access point's network is the host's (breached once for everyone:
// the record of the access point says so, kDeviceBreached), the daemons'
// targets are their owners'. The host grants the first breach of a network
// and refuses any later one; a granted breach is passed on to every other
// game for the NPCs of that network it runs, and the NPC breached through
// the quickhack is its owner's to apply.
inline constexpr std::size_t kMaxBreachPrograms = 16; // daemons and traps, each
inline constexpr std::uint8_t kBreachRemote = 1; // the Breach Protocol quickhack, not a jack-in
inline constexpr std::uint8_t kBreachFailed = 2; // the minigame was lost: the network is alerted
inline constexpr std::uint8_t kBreachApply = 4;  // host to the other games: granted, apply it
inline constexpr std::uint8_t kBreachFlags = 7;
struct Breach {                        // Event channel
  PlayerId player{};                   // who breached: set by the host from the connection
  std::uint64_t entity{};              // the access point's static id (0: none, an NPC's own squad)
  std::uint64_t cls{};                 // its persistent state's class
  NpcId npc{};                         // the NPC breached through the quickhack (0: none)
  std::vector<std::uint64_t> programs; // the daemons uploaded (MinigameAction records)
  std::vector<std::uint64_t> traps;    // the traps the grid set off (MinigameTraps records)
  std::uint8_t flags{};                // kBreach*
  friend bool operator==(const Breach &, const Breach &) = default;
};

// The host's answer to a guest's breach (Event channel; to the breacher
// only). Refused: the network (or the NPC) was breached already, `by` says
// by whom (kNoPlayer: the host's world had it so).
enum class BreachVerdict : std::uint8_t { Granted = 0, Breached = 1, Full = 2 };
struct BreachResult {
  std::uint64_t entity{};
  std::uint64_t cls{};
  NpcId npc{};
  BreachVerdict verdict{};
  PlayerId by{};
  friend bool operator==(const BreachResult &, const BreachResult &) = default;
};

// ---- Quest interactions (docs/architecture/story.md, "Quest interactions") ----
//
// The quest runs on the host, so a guest's V using one of the host's
// objects (grouped, a placed entity: the same in both games) moves nothing
// of the host's quest by itself. The guest says what it chose (kInteractAsk,
// guest to host); the host's game decides from its own data and answers
// that guest alone with a verdict. Never relayed, a story message
// (tandem/interact.hpp keeps the first-come rule and the verdicts known).
//
// QuestInteract::verdict. Ask: the guest's question. The rest are answers.
inline constexpr std::uint8_t kInteractAsk = 0,
                              kInteractPerformed = 1, // the host's V did it on the host's object
    kInteractHostOnly = 2, // it would cast the doer in a scene or workspot: the host must
    kInteractLocal = 3,    // nothing of the host's quest there: the guest's own copy is all
    kInteractTaken = 4,    // another player's use of it came first
    kInteractTooFar = 5,   // the guest's V was not where it could reach it
    kInteractMissing = 6,  // the host's game has no such object or choice now
    kInteractBusy = 7;     // the host's V cannot do it now (a scene, a workspot, a menu)
inline constexpr std::uint8_t kInteractVerdictMax = kInteractBusy;
// QuestInteract::flags.
inline constexpr std::uint8_t kInteractChecked = 1, // the guest's own game passed the choice's
                                                    // checks (a skill, an item its V holds)
    kInteractCasts = 2, // the guest's own game says the choice casts the doer (not run there)
    kInteractLoot = 4;  // a loot choice: `detail` is the item's TweakDB id
inline constexpr std::uint8_t kInteractFlags = 7;
struct QuestInteract {
  std::uint64_t target{}; // the entity's static id (EntityID hash)
  std::uint64_t choice{}; // what was chosen: the device action's class, or the loot action (CName)
  std::uint64_t detail{}; // the action's name (CName), or the looted item's record (kInteractLoot)
  std::uint64_t layer{};  // the interaction layer it was on (CName; 0: none)
  std::uint32_t seq{};    // the asker's count of its questions: the answer names it
  Vec3 position;          // ask: where the guest's V was when it chose
  std::uint64_t timeUs{}; // ask: when, on the host clock
  PlayerId player{};      // the asker (set by the host from the connection)
  std::uint8_t verdict{}; // kInteract*
  std::uint8_t flags{};   // kInteract* flags
  friend bool operator==(const QuestInteract &, const QuestInteract &) = default;
};

// ---- World vehicles of several owners (docs/architecture/traffic.md, "Who
// owns an area"; regions.md, "Hand-over") ----
//
// A shared world vehicle is run by the game that announced it (its id's
// owner) until the host hands it over, as an NPC is: its owner's V is
// leaving it with another linked player near (regions::Taker), or its owner
// is gone or its world rebuilt (regions::Heir). Both games already have a
// body for it, the real one and the proxy: the giver keeps its body as a
// proxy, the receiver's proxy becomes the real vehicle, with the state that
// came with it. Nothing is spawned or deleted: no pop, no duplicate.

// Who runs a shared vehicle now (host -> every player who sees it, and the
// giver; kept with the NPC domain for late joiners and resyncs).
struct TrafficOwnership {
  TrafficId id{};
  PlayerId owner{};
  friend bool operator==(const TrafficOwnership &, const TrafficOwnership &) = default;
};

// Host -> the vehicle's owner: give it to `to`.
struct TrafficHandover {
  TrafficId id{};
  PlayerId to{};
  friend bool operator==(const TrafficHandover &, const TrafficHandover &) = default;
};

// How the giver leaves it (giver -> host -> receiver), or what the host's
// record says of it when its owner is gone (kHandoverInherited).
struct TrafficHandoverState {
  TrafficId id{};
  PlayerId from{}; // set by the host from the connection
  PlayerId to{};
  Vec3 position;
  Quat orientation;
  Vec3 velocity;           // m/s: a car that drove on drives on
  float health{1.f};       // fraction, 0..1
  std::uint16_t flags{};   // kTraffic* (kTrafficParked: no driver, it stays put)
  std::uint8_t handover{}; // kHandoverInherited: built from the record, nobody read the car
  friend bool operator==(const TrafficHandoverState &, const TrafficHandoverState &) = default;
};

// Receiver: it runs the vehicle now (or cannot: it goes). Giver: it will not
// give it (accepted false: a player is getting in, it is claimed).
struct TrafficHandoverDone {
  TrafficId id{};
  bool accepted{};
  friend bool operator==(const TrafficHandoverDone &, const TrafficHandoverDone &) = default;
};

// ---- Other mods (docs/architecture/mod-compat.md) ----

// A game's other mods, each by the hash of its name and what it adds
// (tandem/mods.hpp), said once by each side after Install (never relayed,
// like Install). Names travel only when asked for: kModNamesAsked carries
// the hashes whose names one side wants, kModNames the answer (names of
// those of the answerer's own mods only). Event channel.
inline constexpr std::uint8_t kModListed = 0, kModNamesAsked = 1, kModNames = 2;
inline constexpr std::size_t kMaxModEntries = 512;
inline constexpr std::size_t kMaxModName = 64;
struct ModEntry {
  std::uint64_t hash{};
  std::uint8_t categories{}; // mods::k*
  std::string name;          // kModNames only
  friend bool operator==(const ModEntry &, const ModEntry &) = default;
};
struct ModList {
  std::uint8_t kind{}; // kMod*
  std::vector<ModEntry> mods;
  friend bool operator==(const ModList &, const ModList &) = default;
};

// A game shows substitutes for some of `owner`'s records (a mod that game
// lacks): one per kind it first replaced, said so `owner`'s player knows
// what their partner sees. The seer's game sends it to the host, which
// stamps `seer` and passes it to `owner` only. Event channel.
struct Substitutes {
  PlayerId owner{};
  PlayerId seer{};
  std::uint16_t kinds{}; // bit (mods::Kind)
  std::uint16_t count{}; // records replaced so far
  friend bool operator==(const Substitutes &, const Substitutes &) = default;
};

// ---- Status visuals (docs/architecture/presence.md, "Status effects") ----
//
// The status effects on a V that show on it (burning, shock, a camo's
// shimmer, a Sandevistan's trail, a heal's glow...): the TweakDB ids of the
// records its game has on it now that name a VFX, a sound or a visual
// effector, the whole set on every change (sorted; empty: none). Sent by
// that player's game (Event channel); the host stamps the player, keeps
// everyone's latest for late joiners and relays it. The others play the
// records' visuals on its puppet, never their gameplay.
inline constexpr std::size_t kMaxStatusVisuals = 16;
struct StatusVisuals {
  PlayerId player{};
  std::uint32_t revision{};
  std::vector<std::uint64_t> records;
  friend bool operator==(const StatusVisuals &, const StatusVisuals &) = default;
};

// ---- Scene replay, second part (docs/architecture/scene-replay.md) ----
// What a scene shows besides bodies and voices: faces, bodies on other rigs
// (drones, robots), the props its actors hold, and the camera of a cutscene.
// All host to guests; the three streams share one budget on the host
// (scene::Pacer), with the actors' bodies.

// A scene actor's body pose, delta coded as a V's is (PoseFrame, PoseEncoder
// with a slower key): what ActorPose travels as. Endpoint encodes and decodes
// it; everything above sees ActorPose. State channel.
struct ActorFrame {
  NpcId npc{};
  PoseFrame frame; // `player` unused; seq and key are numbered with the NPC states
  friend bool operator==(const ActorFrame &, const ActorFrame &) = default;
};

// RigPose::part: which animated component of the NPC's entity.
inline constexpr std::uint8_t kRigBody = 0, // its root rig (a body that is not the humanoid one)
    kRigFace = 1;                           // its face rig (lipsync, expressions)
inline constexpr std::size_t kMaxRigBones = 1024;
inline constexpr std::size_t kMaxRigBytes = 1024;
// RigPose entries (tandem/rigpose.hpp): two bytes (the bone in the low 14
// bits, the form in the high 2), then the form's bytes.
inline constexpr std::uint8_t kRigRest = 0, // back at the rig's reference: the receiver's own
                                            // animation shows again
    kRigTurn = 1,  // a small turn from the reference rotation (3 bytes, as PoseFrame's)
    kRigShift = 2, // a small move from the reference translation (3 signed bytes of
                   // kRigShiftStep m) and a small turn
    kRigWhole = 3; // translation (3 x int16 of kRigWholeStep m) and rotation (5 bytes)
inline constexpr float kRigShiftStep = 0.00025f;
inline constexpr float kRigWholeStep = 1.f / 4096.f;
// Some bones of one rig of one of the host's NPCs, in local space, relative
// to the rig's reference pose (the same resource in every game): the bones
// that moved since the host last said them, and a few in turn so that a lost
// frame heals. A bone stays as last said until said again. `rig` and `bones`
// name the rig: a receiver whose body has another one does not take it.
// State channel, host only, paced.
struct RigPose {
  NpcId npc{};
  std::uint8_t part{}; // kRig*
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::uint32_t rig{};   // hash of the rig's bone names (rig::RigHash)
  std::uint16_t bones{}; // how many bones the rig has
  std::vector<std::uint8_t> data;
  friend bool operator==(const RigPose &, const RigPose &) = default;
};

// SceneCamera::flags.
inline constexpr std::uint8_t kCameraCut = 1; // a cut: jump there, no blend from the last
// The picture of the host's scene: its game's active camera while a cutscene
// has it (tier 5), sampled every frame or so. A guest watching shows the
// same view, smoothed (scene::CameraTrack). State channel, host only, paced;
// never sent in a private scene.
struct SceneCamera {
  std::uint32_t play{};
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  Vec3 position;
  std::array<std::uint8_t, kPoseFrameRotationBytes> orientation{}; // pose::PackQuat5
  float fov{};                                                     // degrees
  std::uint8_t flags{};                                            // kCamera*
  friend bool operator==(const SceneCamera &, const SceneCamera &) = default;
};

// SceneProp::flags.
inline constexpr std::uint8_t kPropGone = 1, // it is no longer there (put away, despawned)
    kPropLeftHand = 2;                       // in the holder's left hand (else its right)
inline constexpr std::uint8_t kPropFlags = 3;
inline constexpr std::size_t kMaxSceneProps = 16;
// Something a scene's actor holds (a glass, a shard, a cigarette): which
// item, who holds it and in which hand; said again when it changes hands,
// and with kPropGone when it goes. `prop` counts the play's props from 1.
// Every prop of a play goes with the play. Event channel, a story message,
// kept with the scene for late joiners.
struct SceneProp {
  std::uint32_t play{};
  std::uint32_t prop{};
  std::uint64_t record{};  // the item's TweakDB id
  NpcId holder{};          // the actor holding it (0: a player, or nobody)
  PlayerId holderPlayer{}; // a V holding it (with holder 0; both 0: put down)
  std::uint8_t flags{};    // kProp*
  friend bool operator==(const SceneProp &, const SceneProp &) = default;
};

// ---- The world's objects (docs/architecture/devices.md, "Glass and
// breakable scenery", "A device's fire") ----

// Something of the world broke in a game (a window, a cover, breakable
// scenery): physics destruction, which has no script state. The game where
// it broke says where and, when the piece is a destruction component of a
// placed entity, which; every game that has the place in its world breaks
// the same piece its own way (the component's own trigger, else a physics
// impulse at the place), hurting nobody. Event channel; the host stamps
// the player, holds each guest to a rate (devices.hpp) and passes it to the
// players linked with it. A break is for good: a repeat changes nothing
// (devices::Fractures drops it).
struct Fracture {
  PlayerId player{};         // whose game saw it break: set by the host from the connection
  std::uint64_t entity{};    // the placed entity it is a part of (0: world geometry)
  std::uint64_t component{}; // its destruction component's name (CName; 0: none)
  Vec3 at;                   // where it broke
  std::uint8_t level{};      // the entity's level of destruction (0: not said)
  friend bool operator==(const Fracture &, const Fracture &) = default;
};

// A device that fires (a security turret) is the host's, as every device
// is: what it turns to and shoots at is the host's turret's doing, and
// every other game draws it on its own copy, which never fires on its own.
// A shot at a V is fired for real by that V's game from its copy (the V's
// game decides the hit, as for an NPC's shot: NpcShotAt); a shot at anyone
// else is drawn and hurts nobody. Sensing stays each game's own (the game's
// sensing code knows the local player only): a guest whose copy locked its
// V says so, and the host's turret takes that V's puppet as a target.
enum class DeviceFireKind : std::uint8_t {
  Idle = 0, // host: it follows nobody any more
  Aim = 1,  // host: it turned to `target` (said again every second while it does)
  Shot = 2, // host: one shot of its weapon at `target`
  Seen = 3, // guest: my copy of it has locked my V
  Lost = 4, // guest: ...and no longer has
};
struct DeviceFire {
  std::uint64_t entity{}; // the device's static id
  DeviceFireKind kind{};
  PlayerId player{};      // Seen, Lost: whose V (set by the host from the connection)
  TargetRef target;       // Aim, Shot: a player's V, a shared NPC, a placed entity (None: `at`)
  Vec3 at;                // Aim, Shot: where the target was on the host
  std::uint64_t timeUs{}; // Shot: host clock when it fired
  friend bool operator==(const DeviceFire &, const DeviceFire &) = default;
};

// ---- A thrown object's path (docs/architecture/combat-coverage.md, "Throws") ----
//
// Where a thrown object of ours (a Throw still in flight: a grenade, a
// knife, a launcher round) is now and how it moves, a few
// times a second while it flies (kThrowPathHz an object). A
// copy that strayed from it (it bounced off something only one game has) is
// put back on the real one's path; one that flies the same arc is left
// alone. A velocity of zero: it lies still. State channel (a late one is
// worth nothing: the next one, or the ThrowEnd, says more); the host stamps
// the thrower and relays it like Throw.
inline constexpr float kThrowPathHz = 4.f;
struct ThrowPath {
  PlayerId player{};
  std::uint32_t id{}; // the Throw it is the path of
  Vec3 position;
  Vec3 velocity; // m/s
  friend bool operator==(const ThrowPath &, const ThrowPath &) = default;
};

// ---- Placed NPCs and the death ledger (docs/architecture/regions.md §14) ----
//
// A shared NPC is placed in the world (a guard at its post, a gang member at
// its hangout, a vendor): its entity's static id, the same in every game.
// Its owner to the players linked with it, once per NPC, with the NPC's
// other records (late joiners, a link that forms); kept until the NPC is
// gone. A game that shows the NPC knows which NPC of its own world is the
// same person (its twin, tandem/twins.hpp), and the owner's endpoint knows
// whose death to report.
struct NpcPlaced { // Event channel
  NpcId npc{};
  std::uint64_t entity{}; // EntityID hash (static: placed in the world)
  friend bool operator==(const NpcPlaced &, const NpcPlaced &) = default;
};

// A placed NPC died in the session, or was knocked out for good
// (kDeathKnockedOut): the same dead stay dead in every game, linked or not,
// until the session ends (tandem/ledger.hpp). Its owner's endpoint says it
// to the host when its game reports the NPC dead (npc: the NPC, still its
// own; by, order and timeUs 0); the host stamps it (npc 0, by: whose game
// ran it, order: its count, from 1, timeUs: its clock), keeps the newest
// kMaxDeaths for the session and tells everyone, late joiners included
// (the world digest covers them). Each game then keeps its own copy of
// that entity out of its world, with a body where it fell while the death
// is recent. Never the random crowd: only an NPC said placed.
inline constexpr std::uint8_t kDeathKnockedOut = 1;
inline constexpr std::size_t kMaxDeaths = 256;
struct NpcDeath { // Event channel
  NpcId npc{};
  std::uint64_t entity{};     // EntityID hash (static)
  std::uint64_t record{};     // its character record
  std::uint64_t appearance{}; // CName hash
  Vec3 position;              // where it fell
  float yaw{};
  std::uint8_t flags{}; // kDeath*
  PlayerId by{};
  std::uint32_t order{};
  std::uint64_t timeUs{}; // host clock
  friend bool operator==(const NpcDeath &, const NpcDeath &) = default;
};

// Some float tracks of one rig of one of the host's NPCs (a face's:
// wrinkles, pupils, blinks, lipsync envelopes), in the rig's track order:
// the tracks that moved since the host last said them, and a few in turn so
// that a lost frame heals. Entries of four bytes, tracks in order: the track
// (uint16), its value (int16 of kRigTrackStep). A track stays as last said;
// one said at the rig's reference value lets the receiver's own animation
// show again. `rig` and `tracks` name the rig's tracks (rig::RigHash of
// their names, their count): a receiver whose rig has others takes nothing.
// State channel, host only, paced with the scene's streams.
inline constexpr std::size_t kMaxRigTracks = 1024;
inline constexpr std::size_t kRigTrackEntryBytes = 4;
inline constexpr float kRigTrackStep = 1.f / 4096.f; // values within ±8
struct RigTracks {
  NpcId npc{};
  std::uint8_t part{}; // kRig*
  std::uint32_t seq{};
  std::uint64_t timeUs{};
  std::uint32_t rig{};    // hash of the rig's track names (rig::RigHash)
  std::uint16_t tracks{}; // how many float tracks the rig has
  std::vector<std::uint8_t> data;
  friend bool operator==(const RigTracks &, const RigTracks &) = default;
};

// ---- A turret in a player's hands (docs/architecture/devices.md, "A
// turret in a player's hands") ----
//
// A player took control of a turret (the takeover quickhack, a device's
// control): its game turns it and fires it, as the game does for one V.
// The controller's game says where it points (a point along its barrels)
// and whether it fires, a few times a second while it holds it
// (kTurretHandsHz), and once that it let go. The host stamps the player and
// passes it to the players linked with it; every other copy turns there and
// draws its fire, which hurts nobody: what the controller's shots hit is
// its game's to decide, as for its own weapon. State channel: the next one
// says more, and a copy unheard of for kTurretHandsHoldS is its game's again.
inline constexpr float kTurretHandsHz = 5.f;
inline constexpr float kTurretHandsHoldS = 1.f;
inline constexpr std::uint8_t kHandsFiring = 1; // its trigger is held
inline constexpr std::uint8_t kHandsLetGo = 2;  // the player let go of it
inline constexpr std::uint8_t kHandsFlags = kHandsFiring | kHandsLetGo;
struct TurretHands {
  PlayerId player{};      // whose hands: set by the host from the connection
  std::uint64_t entity{}; // the turret's static id
  Vec3 at;                // where it points (world)
  std::uint8_t flags{};   // kHands*
  friend bool operator==(const TurretHands &, const TurretHands &) = default;
};

// ---- Street races in co-op (docs/architecture/activities.md, "Races") ----
//
// A race is its starter's: its game runs the race quest. The others may join
// it before its start as new racers; each joiner races the starter's
// checkpoints in its own game and says how far it got. The starter's game
// orders everyone (players and its NPC racers) by those reports and their
// host clock times. One message (Event channel), sent by a player's game in
// its own name: the host stamps `player` and relays it to every other game.
enum class RaceKind : std::uint8_t {
  // Starter: the race as it stands (resent while it lasts): `arg` its phase
  // (kRace*), `at` and `heading` its start line, `record` its car,
  // `checkpoints` how many in all, `points` those known so far (in order),
  // `quest` the race quest's journal hash (0: unknown), `timeUs` its start.
  Info = 1,
  Join = 2,      // a player to `starter`: in (`arg` 1) or out (0)
  Seat = 3,      // starter to `to`: its slot on the grid (`arg`; 0: refused)
  Progress = 4,  // a racer: `checkpoint` passed at `timeUs`, `metres` to the next; `arg` 1 finished
  Standings = 5, // starter: everyone's place, `entries`
  Reward = 6,    // starter to `to`: `record`, a reward its race paid, for the place `arg`
  // Starter: the whole route from its race quest's data, said on the grid
  // (again when a joiner is seated or the route changes): `points` every
  // checkpoint in order, as many as `checkpoints` (none: no route known, the
  // joiners follow Info's), `arg` the race's own rewards (kRaceRewards*; 0
  // none known).
  Route = 7,
};
inline constexpr std::uint8_t kRaceGrid = 1, kRaceRunning = 2, kRaceOver = 3,
                              kRaceAbandoned = 4;                // Info's phases (`arg`)
inline constexpr std::uint8_t kRacerFinished = 1, kRacerOut = 2; // RaceEntry::flags
inline constexpr std::uint32_t kRacerNpc = 0x80000000u; // RaceEntry::who: the starter's NPC racer
// Route's `arg`: which race of The Beast in Me (QuestRewards.sq024_win_*_race,
// sq024_finish_*_race), each joiner paying its own from its own TweakDB.
inline constexpr std::uint8_t kRaceRewardsBadlands = 1, kRaceRewardsBig = 2, kRaceRewardsCity = 3,
                              kRaceRewardsSantoDomingo = 4;
inline constexpr std::size_t kMaxRacePoints = 64, kMaxRacers = 16;
struct RaceEntry {
  std::uint32_t who{};        // a player, or kRacerNpc | the racer's number
  std::uint16_t checkpoint{}; // checkpoints passed
  std::uint8_t place{};       // 1 first
  std::uint8_t flags{};       // kRacer*
  std::uint64_t timeUs{};     // host clock: its finish, else its last checkpoint
  friend bool operator==(const RaceEntry &, const RaceEntry &) = default;
};
struct Race {
  PlayerId player{};    // the sender (the host stamps it)
  PlayerId starter{};   // whose race
  PlayerId to{};        // Seat, Reward: the one player it is for
  std::uint32_t race{}; // the starter's number for that race
  RaceKind kind{};
  std::uint8_t arg{};
  std::uint16_t checkpoint{};
  std::uint16_t checkpoints{};
  float metres{};
  float heading{};        // degrees
  std::uint64_t timeUs{}; // host clock
  std::uint64_t record{}; // TweakDB id: a car, a reward
  std::uint32_t quest{};
  Vec3 at;
  std::vector<Vec3> points;       // at most kMaxRacePoints
  std::vector<RaceEntry> entries; // at most kMaxRacers
  friend bool operator==(const Race &, const Race &) = default;
};

// A prop of the host's scene that nobody holds (set on a table, dropped,
// thrown): an item entity the scene brought in, where it is now. Said when
// it appears, again when it moved (scene::kPlaceMoveMetres, kPlaceTurnRadians;
// at most every scene::kPlaceEveryUs), and with kPropGone when it is no
// longer seen or someone holds it (then a SceneProp). A guest spawns the same
// entity template and appearance there, moves it, and deletes it when it
// goes or the play ends. `prop` counts with the play's SceneProp numbers.
// Event channel, a story message, kept with the scene for late joiners.
struct ScenePlace {
  std::uint32_t play{};
  std::uint32_t prop{};
  std::uint64_t entity{};     // its entity template's resource path hash
  std::uint64_t appearance{}; // its appearance, a CName hash (0: the template's own)
  Vec3 position;
  Quat orientation;
  std::uint8_t flags{}; // kPropGone only
  friend bool operator==(const ScenePlace &, const ScenePlace &) = default;
};

// ---- The host's world prefabs (docs/architecture/story.md, "Quest prefabs") ----
//
// The quest prefabs the host's quests have loaded now (the interiors and
// props a quest phase streams in: Konpeki Plaza, the All Foods plant): their
// world node refs (GlobalNodeRef hashes, the same in every game of the same
// world data), as the host's questQuestPrefabsHandler lists them in
// ForceLoad (engine-knowledge §13.111). Host to guests, the whole set on
// every change (sorted; empty: none), a story message: kept for late
// joiners, in the story digest, an explicit "none" in a story resync. A
// guest grouped with the host loads the same ones in its own world while
// they last, and puts back what it loaded when it goes apart or leaves.
inline constexpr std::size_t kMaxWorldPrefabs = 256;
struct WorldPrefabs {
  PlayerId player{}; // whose world (the host): set by the host's endpoint
  std::vector<std::uint64_t> prefabs;
  friend bool operator==(const WorldPrefabs &, const WorldPrefabs &) = default;
};

// ---- A player's story gates (docs/architecture/story.md, "Story gates") ----
//
// Where its own story lets its V go: the Watson lockdown lifted (fact
// q101_done), Dogtown's gate open (fact ep1_side_content), and which side
// of each its V stands on now. Every player's own word about its game, on
// every change; the host stamps a guest's and passes it on, and keeps each
// player's for late joiners (a story message, as StoryState). A gate open in
// one story and closed in another is told in words at the join, and when a
// guest meets the closed side while the host is past it.
inline constexpr std::uint8_t kGateWatsonOpen = 1;  // the Watson lockdown is lifted
inline constexpr std::uint8_t kGateDogtownOpen = 2; // Dogtown's gate is open
inline constexpr std::uint8_t kGateInWatson = 4;    // its V is in Watson now
inline constexpr std::uint8_t kGateInDogtown = 8;   // its V is in Dogtown now
inline constexpr std::uint8_t kGateFlags = 15;
struct StoryGates {
  PlayerId player{}; // whose story (the host stamps a guest's)
  std::uint8_t flags{};
  friend bool operator==(const StoryGates &, const StoryGates &) = default;
};

// ---- What a player's game did to an NPC's proxy that the NPC's owner
// runs on the real one (docs/architecture/npcs.md, "What a proxy cannot
// do") ----
//
// A weak spot of the proxy (a boss's plate or heat sink: a WeakspotObject)
// lost health to the sender's fire on its game: the owner drains the same
// share from its real weak spot, so what its destruction does (its fact,
// the boss's next phase, the damage to its owner) happens on the game that
// runs the NPC. The sender's copy breaks on its own screen too. Combat path,
// routed to the NPC's owner as NpcStrike; a one-shot.
struct WeakSpotHit {
  PlayerId player{};      // whose fire: set by the host from the connection
  NpcId npc{};            // the weak spot's NPC
  std::uint64_t record{}; // TweakDB id of its Weakspot record
  std::uint8_t index{};   // its place in the NPC's weak spots (several of one record)
  float drain{};          // the share of its health it lost, percent (0..100]
  friend bool operator==(const WeakSpotHit &, const WeakSpotHit &) = default;
};

// An NPC netrunner's trace (AIQuickHack.HackRevealPosition, PlayerHack with
// kPlayerHackReveal) ran to its end on the traced V's game: the owner sets
// the netrunner and its squad on that V, as the game does when a trace
// completes (UploadFromNPCToPlayerListener), which the V's game cannot: its
// proxy has no squad. Combat path, to the NPC's owner; a one-shot.
struct HackTraced {
  PlayerId player{}; // the traced V's: set by the host from the connection
  NpcId npc{};       // the netrunner
  friend bool operator==(const HackTraced &, const HackTraced &) = default;
};

// A turret is in one player's hands at a time (devices.md, "A turret in a
// player's hands"). The host keeps who holds each, first come: from a
// player's first TurretHands until it lets go or is unheard of for
// kTurretHandsHoldS. Another player's claim is not passed on; its game is
// told who holds the turret (at most once per kTurretHandsHoldS while it
// claims), lets go at once and says so in words. Host to that player only
// (Event channel; a one-shot).
struct TurretBusy {
  std::uint64_t entity{}; // the turret's static id
  PlayerId holder{};      // who holds it
  friend bool operator==(const TurretBusy &, const TurretBusy &) = default;
};

// The alternatives, in id order. Code uses Message (below).
using MessageVariant = std::variant<
    Hello, Welcome, Refused, Ping, Pong, Roster, PlayerState, Bye, PoseState, Appearance, Held,
    Shot, Seat, VehicleState, World, NpcSpawn, NpcStates, NpcGone, NpcHit, NpcHealth, NpcEffect,
    Pause, VehicleRadio, PlayerHit, NpcAwareness, Stealth, Noise, NpcHack, NpcHackState, NpcStatus,
    PlayerHack, VehicleHack, Journal, Tracked, PhoneCall, PhoneAction, PhoneReply, Subtitle,
    ChoiceHub, ChoiceCursor, StoryState, QuestReward, TimeEffect, StoryEffect, NpcAction,
    NpcActionReply, NpcHold, NpcCue, Regroup, RegroupAnswer, WorldItem, WorldItemTaken, SharedItem,
    Vitals, Revive, GameOver, NpcAnims, NpcShot, NpcStrike, NpcWound, NpcPoise, Difficulty, AreaHit,
    Digest, ResyncRequest, NpcSet, Epoch, Elsewhere, Transit, NpcSeat, NpcShotAt, ShotHit,
    SceneState, SceneCast, SceneLine, ActorPose, DeviceState, DeviceUse, DeviceSet, DeviceDigest,
    Regions, NpcOwner, NpcHandover, NpcHandoverState, NpcHandoverDone, OwnedDigest, RegionHold,
    TrafficSpawn, TrafficStates, TrafficGone, TrafficStatus, TrafficHit, TrafficClaim, TrafficSet,
    Heat, Crime, Presence, Cue, NpcReaction, Install, Throw, ThrowEnd, Rules, Defended, Activity,
    TimeSkip, TimeSkipVote, TimeSkipResult, PlayerLevel, SessionLevel, NpcLevel, Trade, TradeOffer,
    TradeCommit, VehicleUnlock, CarLend, Mark, Tag, Link, Waypoint, VehicleDamage, VehicleHit,
    VehicleExit, VehicleCombat, MountedFire, GunnerRequest, Gunner, GunnerAim, VehicleBump, NpcRam,
    Chase, StoryRestrictions, PoseFrame, NpcMotion, PlayerMotion, VehicleMotion, NpcScale, Breach,
    BreachResult, NpcVendor, StatusVisuals, ModList, Substitutes, TrafficOwnership, TrafficHandover,
    TrafficHandoverState, TrafficHandoverDone, QuestInteract, ActorFrame, RigPose, SceneCamera,
    SceneProp, Fracture, DeviceFire, ThrowPath, NpcPlaced, NpcDeath, RigTracks, TurretHands, Race,
    ScenePlace, LinkDecline, WorldPrefabs, StoryGates, WeakSpotHit, HackTraced, TurretBusy>;

// A message: MessageVariant, its copies, moves and conversions compiled in
// message.cpp only (tandem/closed_variant.hpp). std::visit, std::get_if,
// std::holds_alternative and index() work on it as on the variant.
using Message = ClosedVariant<MessageVariant>;
bool operator==(const Message &a, const Message &b);

// A story message (Journal … QuestReward, StoryEffect, SceneState … SceneLine,
// VehicleUnlock, StoryRestrictions, QuestInteract, SceneProp, ScenePlace,
// WorldPrefabs, StoryGates):
// endpoint.hpp relays these through one path.
bool IsStory(const Message &message);
// A hit outcome message (NpcStrike … AreaHit, Throw … Defended, ThrowPath),
// a level (PlayerLevel … NpcLevel), a car's lending (CarLend), an NPC's
// scale (NpcScale), a vendor (NpcVendor), a placed NPC (NpcPlaced), a
// death of the ledger (NpcDeath), a street race (Race), or what a proxy
// cannot do (WeakSpotHit, HackTraced): one path in endpoint.hpp too.
bool IsCombat(const Message &message);
// A trade message (Trade, TradeOffer, TradeCommit): one path as well.
bool IsTrade(const Message &message);
// A vehicle combat message (VehicleDamage … Chase): one path in endpoint.hpp too.
bool IsVehicleCombat(const Message &message);

std::vector<std::byte> Encode(const Message &message);
std::optional<Message> Decode(std::span<const std::byte> bytes);
// The protocol version of a packet that is a tandem Hello, also one Decode
// refuses (another version's layout); none: not a Hello.
std::optional<std::uint16_t> PeekHelloVersion(std::span<const std::byte> bytes);

// The channel a message travels on, and whether ENet must deliver it.
Channel ChannelOf(const Message &message);
bool IsReliable(const Message &message);

const char *NameOf(const Message &message);
// The same by Message index ("?" past the last).
const char *NameOfKind(std::size_t kind);
} // namespace tandem::proto

extern template class tandem::ClosedVariant<tandem::proto::MessageVariant>;
