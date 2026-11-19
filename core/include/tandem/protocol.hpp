#pragma once

// The wire protocol. Every message is one alternative of `Message`; the
// variant index is the message id, so new messages are only ever appended.
// Bump kProtocolVersion on any change to an existing message.

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
inline constexpr std::uint16_t kProtocolVersion = 6;
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

// Guest -> host, first message.
struct Hello {
  std::uint32_t magic{kMagic};
  std::uint16_t version{kProtocolVersion};
  std::string name;
  std::string gameVersion;
  Body body{};
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
struct NpcHealth { // Event channel; kept by the host for late joiners
  NpcId npc{};
  float health{}; // fraction of its maximum, 0..1
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

// Quickhacks (docs/ARCHITECTURE.md, "Quickhacks from two players on one
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
// Bits of StoryState::flags (free from 16 up: add new ones there, e.g. other
// worlds such as cyberspace).
inline constexpr std::uint8_t kStoryBraindance = 1, kStoryReplacer = 2, kStoryInCall = 4,
                              kStoryPrivate = 8; // in a private scene: not to be watched
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

// A V's time ability (docs/ARCHITECTURE.md, slowed and stopped time): what
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
// slow time, Focus, Deadeye, Reflex Tuner...); scanning stays on its owner's
// screen.
inline constexpr std::uint8_t kTimeBubbleAbilities =
    kTimeSandevistan | kTimeKerenzikov | kTimeSlowMo;
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

// Loot (docs/ARCHITECTURE.md, "Loot"). Corpses and containers are per
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

// Quest and important items are every player's (docs/ARCHITECTURE.md,
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

// ---- Down and out (milestone 8, docs/ARCHITECTURE.md "Down, revive, respawn") ----

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

// ---- Hit outcomes (docs/ARCHITECTURE.md, "Game rules hold for every hit") ----

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

// ---- Consistency (docs/ARCHITECTURE.md, "Consistency") ----
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
inline constexpr std::uint8_t kEpochJoin = 0, kEpochLoading = 1, kEpochFastTravel = 2;
struct Epoch {
  PlayerId player{};           // whose world: set by the host from the connection
  std::uint32_t epoch{};       // that game's world epoch (1 at the start of a session)
  std::uint32_t seenHost{};    // a guest's: the host epoch it has seen
  std::uint8_t reason{};       // kEpoch*
  std::uint32_t firstNpcSeq{}; // the host's: NpcStates older than this are the old world's
  friend bool operator==(const Epoch &, const Epoch &) = default;
};

// A V in another world (docs/ARCHITECTURE.md, other worlds; core
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

// A ride a player's own game runs for its V (docs/ARCHITECTURE.md,
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
  float tbh{};            // the AI's time-between-hits coefficient for this shot
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

using Message =
    std::variant<Hello, Welcome, Refused, Ping, Pong, Roster, PlayerState, Bye, PoseState,
                 Appearance, Held, Shot, Seat, VehicleState, World, NpcSpawn, NpcStates, NpcGone,
                 NpcHit, NpcHealth, NpcEffect, Pause, VehicleRadio, PlayerHit, NpcAwareness,
                 Stealth, Noise, NpcHack, NpcHackState, NpcStatus, PlayerHack, VehicleHack, Journal,
                 Tracked, PhoneCall, PhoneAction, PhoneReply, Subtitle, ChoiceHub, ChoiceCursor,
                 StoryState, QuestReward, TimeEffect, StoryEffect, NpcAction, NpcActionReply,
                 NpcHold, NpcCue, Regroup, RegroupAnswer, WorldItem, WorldItemTaken, SharedItem,
                 Vitals, Revive, GameOver, NpcAnims, NpcShot, NpcStrike, NpcWound, NpcPoise,
                 Difficulty, AreaHit, Digest, ResyncRequest, NpcSet, Epoch, Elsewhere, Transit,
                 NpcSeat, NpcShotAt, ShotHit, DeviceState, DeviceUse, DeviceSet, DeviceDigest>;

// A story message (Journal … QuestReward, StoryEffect): endpoint.hpp relays these
// through one path.
bool IsStory(const Message &message);
// A hit outcome message (NpcStrike … AreaHit): one path in endpoint.hpp too.
bool IsCombat(const Message &message);

std::vector<std::byte> Encode(const Message &message);
std::optional<Message> Decode(std::span<const std::byte> bytes);

// The channel a message travels on, and whether ENet must deliver it.
Channel ChannelOf(const Message &message);
bool IsReliable(const Message &message);

const char *NameOf(const Message &message);
} // namespace tandem::proto
