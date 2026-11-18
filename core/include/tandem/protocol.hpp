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
inline constexpr std::uint16_t kProtocolVersion = 5;
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
  std::uint16_t flags{}; // weapon drawn, aiming, in vehicle...
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
// Other games teleport their copy of it to the interpolated transform.
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
  std::uint8_t reason{}; // 0 out of range or despawned, 1 dead
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
inline constexpr std::uint8_t kStoryBraindance = 1, kStoryReplacer = 2, kStoryInCall = 4;
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

using Message =
    std::variant<Hello, Welcome, Refused, Ping, Pong, Roster, PlayerState, Bye, PoseState,
                 Appearance, Held, Shot, Seat, VehicleState, World, NpcSpawn, NpcStates, NpcGone,
                 NpcHit, NpcHealth, NpcEffect, Pause, Journal, Tracked, PhoneCall, PhoneAction,
                 PhoneReply, Subtitle, ChoiceHub, ChoiceCursor, StoryState, QuestReward>;

// A story message (Journal … QuestReward): endpoint.hpp relays these
// through one path.
bool IsStory(const Message &message);

std::vector<std::byte> Encode(const Message &message);
std::optional<Message> Decode(std::span<const std::byte> bytes);

// The channel a message travels on, and whether ENet must deliver it.
Channel ChannelOf(const Message &message);
bool IsReliable(const Message &message);

const char *NameOf(const Message &message);
} // namespace tandem::proto
