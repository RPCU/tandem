// tandem-peer: a stand-in second player on Linux. It speaks the real protocol
// through the same core as the game plugin, so one copy of the game can be
// tested against it.
//
//   tandem-peer host [options]          the game joins it
//   tandem-peer join ADDRESS[:PORT]     it joins the game
//   tandem-peer join --code CODE --relay RELAY   it joins by a join code
//
// Relay (--relay HOST[:PORT]): a host is also reachable through tandem-relay
// and logs its join code; a guest joins by --code through it, after trying
// ADDRESS directly when one is given.
//
// Modes (--mode):
//   mirror  copies your V back as a second player, offset and delayed: you see
//           your own movements on a puppet, through the whole network path
//   ghost   circles around your V
//   front   stands 3 m in front of your V, facing it, stepping side to side:
//           always in view, for checking how a puppet looks and moves
//   drive   drives a car in circles around your V (--vehicle RECORD): tests
//           the copy of a remote player's car, its driver and its motion;
//           --radio STATION plays that station on the car's radio, for
//           whoever rides in it; --seat SLOT rides in it instead while "an
//           AI" drives (a ride: Delamain, a quest car), --scene makes it a
//           story scene's (the host's: partners near it get a seat),
//           --driver-npc RECORD (host) puts an NPC at the wheel
//   metro   stands beside your V, then rides the NCART (--metro-at,
//           --metro-for): the train is never shared, so your game shows the
//           puppet hidden with a notice (or in your train if you ride one
//           there), skipping halfway, and back where the ride ends
//   region  a player with a region of its own (docs/architecture/regions.md):
//           stands --region-far metres from your V with --region-npcs NPCs it
//           runs itself (announced, moved, hurt and killed by this process),
//           walks up to your V --region-walk-at seconds in (its NPCs with
//           it), and off again at --region-leave-at (or --region-leave-linked
//           seconds after it is up by you and knows it is linked), leaving them with you:
//           the host then hands them over. It gives and takes NPCs as a game
//           would, and logs every step ("region: ...")
//   course  runs a scripted course ahead of your V, every way a V moves:
//           jumps, a double jump, a ledge climb, a drop, a ladder, swimming
//           and diving, an elevator, a slide and a dodge, then back to the
//           start (a teleport); heights relative to where your V stood, so
//           the puppet floats where the ground differs: placement, not looks
//   path    replays a path saved with --save-path (--path FILE) in a loop,
//           moved by --offset: your own jumps, ladders and swims, on the
//           puppet, over the same ground
//   idle    only connects and reports
//
// Looks, held weapons, shots, seats and cars: mirror and front echo yours
// back (the car moved by --offset, so the copy does not sit in yours).
// Pause menu: --pause-at opens "its pause menu" that many seconds into the
// session (once another player is there) for --pause-for seconds; the pause
// states it hears are logged.
// Takedowns: --grab-npcs grabs one of the host's NPCs, then takes it down
// (lethal and non-lethal in turn); --carry-npcs picks up a body (one that
// died), carries it and throws it; as host, it answers the game's requests
// (refusing a dead or held NPC) and --hold-npcs shows its own V grabbing and
// taking down a replayed NPC, with an NPC cue for the game.
// Fights: --hit-npcs (join) hits the host's NPCs, with the outcome --strike
// names (non-lethal, 1 HP cap, poise, headshot, blade); --hit-you (host) has
// one of its NPCs hit every guest's V; a guest logs the NPC hits it receives.
// As the host of a replay it applies strikes by the game's rules (core
// combat.hpp) and answers with health, poise, wounds, knock-outs and deaths.
// --difficulty (host) sets the session's difficulty; --area-hit throws "a
// grenade" at every other V (AreaHit; --friendly-fire on, host, lets it
// through). --throw and --smart send our V's throws and smart rounds for
// the others' copies (Throw, ThrowEnd); --defend (join) blocks and parries
// the host's NPCs' attacks (Defended).
// Stealth: as host, --npc puts an NPC in front of the guest and --detect-at
// makes the NPCs it sends notice, detect and lose the guests (NpcAwareness,
// NpcEntry::detected); --camo turns "its optical camo" on, --shoot (join)
// fires gunshots its host's NPCs hear. Stealth and noises heard are logged.
// Quickhacks: --hack-npcs (as a guest) uploads on the host's NPCs and lands
// the effect; --hack-player (as the host of a replay) has an NPC hack the
// guest's V; --hack-car brakes the car another player drives.
// Sandevistan: --sandevistan-at switches "its Sandevistan" on that many
// seconds into the session for --sandevistan-for seconds, at
// --sandevistan-scale: the host slows the NPCs around its V (time bubble);
// --scan-at does the same with "its scanner" (the scanner's slow motion is
// a bubble too). The time abilities it hears are logged.
// Devices (docs/architecture/devices.md): --device ENTITY:CLASS:WORDS[@S]
// operates one by its id (a guest asks the host; a host says what its own
// is), --device-answer WORDS changes back, --device-after seconds later,
// every device another player just changed; not what the host made it
// right after our answer (a door answered "friendly", which it cannot be:
// h11's two doors went back and forth, devices-camera, 2026-11-21). A host
// has no world of its
// own: a guest's request is taken as said. What it hears is logged.
// Breach Protocol (devices.md, "Breach Protocol"): --breach SPEC breaches
// an access point (ENTITY[:CLASS]) or an NPC (npc=N) with --programs, as a
// won minigame would; --breach-failed loses it instead. What it hears (the
// host's answer, other players' breaches it would apply) is logged.
// Quest interactions (docs/architecture/story.md, "Quest interactions"):
// --interact ENTITY:CHOICE[:DETAIL][@S] has a guest's V use the host's
// object (asks the host; the verdict is logged, and a second ask of a
// known one is answered at once); --interact-answer VERDICT is what a
// host's game answers every guest (local by default: no world of ours;
// performed, host-only, missing, busy, too-far or none, no answer).
// Loot: --loot-quest has "its V" loot a quest item (everyone gets it);
// --drop-item drops a shared world item in front of your V; --take-items
// picks up what the others drop; as the host of a replay, --remove-corpses
// takes the bodies of NPCs a guest killed away (their proxies' loot is left
// as a bag). The world items and pickups it hears are logged.
// NPCs: join --record saves the host's NPC messages (spawns, positions,
// animation states, shots, health, gone); host --replay sends them again,
// and --anims FILE adds animation states and shots from a text script
// (NpcAnimScript below) to the replayed NPCs: aim, crouch, shoot, a workspot.
// Body pose: mirror and front echo your V's pose; --pose FILE (a .tpose made by
// tools/re/pose_export.py from a recording) plays a recorded pose instead, in
// a loop, with any mode.
// Story (milestone 6): --story FILE plays a scripted host story to the game
// as a guest (journal, tracked quest, calls, lines, hubs, tier, rewards; the
// format is in tools/scenarios/story-host.story); as a guest, --answer,
// --reply and --suggest act on the host's calls, texts and hubs, and every
// story message heard is logged (and recorded with --record).
// Fast travel together (milestone 4): --travel-at N "fast travels" the mock
// player N seconds into the session (it stands at --travel-to from then on,
// until another V comes to it) and offers the others to join it, as a game
// does on arriving; --offer-regroup N asks them to come to it; the offers it
// gets are answered as --regroup-answer says (yes: it goes there).
// Activities (docs/architecture/activities.md): --ask-skip N asks the others
// to skip --skip-hours (as the time skip wheel or a bed does), each request
// heard is answered as --skip-answer says, and --activity KIND makes the
// mock player busy (a workspot, photo mode...) for --activity-for seconds.
// A bad network: --latency, --jitter, --loss, --burst and --reorder put a
// shaped link (tandem/shaping.hpp) between the mock player and the others,
// both ways.
// Traffic (docs/architecture/netcode.md): every report says what went over
// the wire, each way, and the largest message types; --traffic-report adds the
// whole table; --legacy-wire sends as before the netcode work (every state
// every frame, whole poses and NPC lists), to compare.
// tandem-peer traffic FILE...: the bandwidth a recording (.tplay, .tsession
// or an NPC recording) takes on the wire, legacy and compact.

#include "tandem/combat.hpp"
#include "tandem/comms.hpp"
#include "tandem/degraded.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/interpolation.hpp"
#include "tandem/mods.hpp"
#include "tandem/movement.hpp"
#include "tandem/netcode.hpp"
#include "tandem/play.hpp"
#include "tandem/pose.hpp"
#include "tandem/presence.hpp"
#include "tandem/rigpose.hpp"
#include "tandem/scene.hpp"
#include "tandem/shaping.hpp"
#include "tandem/sim/game.hpp"
#include "tandem/sim/looks.hpp"
#include "tandem/sim/referee.hpp"
#include "tandem/traffic.hpp"
#include "tandem/vehicle_combat.hpp"

#include "tandem/net_traffic.hpp"
#include "tandem/regions.hpp"
#include "tandem/scaling.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;
using tandem::proto::PlayerState;
using tandem::proto::Vec3;

volatile std::sig_atomic_t g_stop = 0;

// --corpse-npcs: a body's rest as a real ragdoll's bodies lie (a Tyger
// Claw's 22 bodies, thrown and settled on instance a, 2026-11-28: cm from
// the entity, quaternions × 32767): a game's copy of a humanoid takes it.
std::vector<tandem::proto::RagdollBody> RestingBodies() {
  return {{.offset = {0, 0, 0}, .rotation = {-12450, 15844, 10516, -23602}},
          {.offset = {0, 0, 0}, .rotation = {3811, -29970, -11405, 5559}},
          {.offset = {0, 0, 0}, .rotation = {-11338, -4019, -4561, -30136}},
          {.offset = {3, -8, 5}, .rotation = {-10441, 15359, 9280, -25351}},
          {.offset = {-7, -3, 2}, .rotation = {10394, 30164, 1530, 7311}},
          {.offset = {7, 3, -1}, .rotation = {-3932, -14292, -15215, -24949}},
          {.offset = {6, -14, 9}, .rotation = {-12408, 13275, 13018, -23959}},
          {.offset = {-38, 24, -15}, .rotation = {-22668, 16522, -13382, 10381}},
          {.offset = {16, 39, -25}, .rotation = {12038, -8282, 18692, -22600}},
          {.offset = {9, -22, 11}, .rotation = {-13005, 11868, 16180, -22403}},
          {.offset = {13, -34, 12}, .rotation = {3447, -19427, -21280, 15217}},
          {.offset = {13, -34, 12}, .rotation = {-19874, 2626, 9769, -24007}},
          {.offset = {13, -34, 12}, .rotation = {-10048, 10629, 13148, -26208}},
          {.offset = {-1, -52, 22}, .rotation = {-2431, 9423, 30265, 7937}},
          {.offset = {33, -47, 6}, .rotation = {-24309, -20256, 5873, -6162}},
          {.offset = {24, -54, 19}, .rotation = {-15464, 2136, 17977, -22512}},
          {.offset = {-26, -40, 14}, .rotation = {-1779, 9408, 30726, 6159}},
          {.offset = {38, -22, -8}, .rotation = {-25906, -18224, 5333, -6479}},
          {.offset = {-32, -18, -4}, .rotation = {-15847, 21790, -16790, 8115}},
          {.offset = {25, -2, -22}, .rotation = {10801, -6258, 16131, -25645}},
          {.offset = {-51, -31, 9}, .rotation = {-7801, 2154, 29100, 12703}},
          {.offset = {47, 0, -21}, .rotation = {-19429, -23581, 11825, 568}}};
}

enum class Mode { Mirror, Ghost, Front, Drive, Course, Path, Metro, Watch, Region, Idle };

struct Options {
  bool host{};
  std::string address{"127.0.0.1"};
  std::uint16_t port{tandem::proto::kDefaultPort};
  Mode mode{Mode::Mirror};
  std::string name{"tandem-peer"};
  tandem::proto::Body body{tandem::proto::Body::Female};
  Vec3 offset{2.f, 0.f, 0.f};
  bool offsetGiven{};            // --offset on the command line (watch: its own default)
  std::string recordSessionFile; // join: what the host sends us, for --play-session
  std::string playSessionFile;   // host: a real host's recorded session, replayed
  std::chrono::milliseconds delay{0};
  int rateHz{30};
  std::string poseFile;
  std::string vehicle{"Vehicle.v_standard2_archer_hella_player"};
  // Our own look: random clothes (so tests see a V of its own, not a copy
  // of the other player), or an echo of theirs.
  bool echoLook{};
  std::string lookKeys; // --look-keys DIR ("": the default)
  std::optional<unsigned> lookSeed;
  int driveSeconds{0};     // drive: get out after this long (0: never)
  float driveRadius{12.f}; // drive: the circle's radius round your V
  // drive: the car's radio (ERadioStationList index, -1 off); unset: none sent
  std::optional<int> radioStation;
  // drive: our car's custom paint (unset: none said), and when it goes back
  // to stock (seconds after taking the seat; 0: never).
  std::optional<tandem::proto::CarPaint> paint;
  int paintOffSeconds{0};
  // drive: our garage's car is summoned S seconds after we are together and
  // drives in on its own (-1: we sit in it from the start); we get in S
  // seconds after it parked (-1: never); it is summoned again S seconds
  // after it parked (-1: never).
  int summonSeconds{-1};
  int summonBoardSeconds{-1};
  int summonAgainSeconds{-1};
  int hitNpcsSeconds{0};   // join: hit one of the host's NPCs this often (0: never)
  int hitYouSeconds{0};    // host: one of its NPCs hits each guest's V this often (0: never)
  bool hitYouMelee{false}; // --hit-you: a melee hit (blockable, held for the proxy's swing)
  int shootYouSeconds{0};  // host: one of its NPCs fires at each guest's V this often
  std::string shotWeapon{"Items.Preset_Lexington_Default"}; // --shoot-you: its weapon
  bool shotSmart{}; // --shoot-you: a smart gun's guided rounds (kShotAtTracked)
  // --hit-npcs: what each hit carries besides its damage (plain, nonlethal,
  // cap, poise, head, blade).
  std::string strike{"plain"};
  std::optional<int> difficulty;    // host: the session's gameDifficulty
  int areaHitSeconds{0};            // our "grenade" hits every other V this often (0: never)
  std::optional<bool> friendlyFire; // host: the session's rule (unset: none sent)
  // host: enemy scaling, the session's rule (unset: none sent), and its health
  // per extra player; join: stand beside one of the host's NPCs.
  std::optional<bool> enemyScaling;
  float enemyScalingHealth{tandem::scaling::kHealthPerPlayer};
  bool nearNpc{};
  int throwSeconds{0};                               // our V throws a grenade this often (0: never)
  std::string throwItem{"Items.GrenadeFragRegular"}; // --throw: what
  // --throw: each grenade "bounces" a second in, 3 m off its arc (its path
  // said as it flies, ThrowPath), and explodes 3 m to the side.
  bool throwBounce{};
  // Our V's footsteps as noise for the others' NPCs (Noise), as its game
  // gives them out: a run's, a sprint's, none walking slowly or crouched.
  bool footsteps{};
  int smartSeconds{0};  // our V fires a smart round at an NPC this often (0: never)
  int defendSeconds{0}; // join: we parry, then block, an NPC's attack this often
  // host: the world the game follows (time of day, weather name); unset: none sent
  std::optional<int> timeOfDayMinutes;
  std::string weather;
  std::string recordFile; // join: save the host's NPC messages here
  std::string replayFile; // host: send a recording's NPC messages again
  std::string animsFile;  // host: NPC animation states and shots to send (with --replay)
  int pauseAtSeconds{-1}; // open the pause menu this long into the session (-1: never)
  int pauseForSeconds{3}; // ...for this long
  std::string npcRecord;  // host: one NPC of this record in front of the guest
  // Other mods (docs/architecture/mod-compat.md): the hints sent with our
  // held item, car, extra look pieces and NPC (--held-record, --vehicle-record,
  // --look-piece, --npc-record NAME:TYPE...: records a mod would add), and
  // the records this mock player's "game" lacks (--unknown), resolved as a
  // game would and logged ("mods: unknown ...").
  tandem::proto::RecordHint holdHint, vehicleHint, npcHint;
  std::vector<std::pair<std::string, tandem::proto::RecordHint>> lookPieces;
  std::set<std::uint64_t> unknown;
  int detectAtSeconds{-1};  // host: its NPCs detect the guests this long in (-1: never)
  int detectForSeconds{10}; // ...for this long
  int camoAtSeconds{-1};    // optical camo on this long into the session, for 5 s
  std::string camoRecord;   // ...its status effect record (Stealth::camo; none: 0)
  // Status effects that show on our V (StatusVisuals): REC@FROM-TO[,...], in
  // seconds after another player is in (TO 0 or missing: kept).
  std::string statusVisuals;
  int shootSeconds{0};  // join: a gunshot (shot + noise) this often (0: never)
  bool ricochet{};      // --shoot: each bullet ricochets on (a Ricochet cue)
  int chargeSeconds{0}; // a tech weapon's charged shot this often (0: never)
  // Quickhacks: join: upload --hack-action on a host NPC every N s, then land
  // --hack-effect; host: one of our NPCs hacks the guest (--npc-hack) every
  // N s; either: hack the car another player drives every N s.
  int hackNpcsSeconds{0};
  std::string hackAction{"QuickHack.BlindHack"};
  std::string hackEffect{"BaseStatusEffect.Blind"};
  int hackPlayerSeconds{0};
  std::string npcHack{"AIQuickHack.HackRevealPosition"};
  // --hack-player: its netrunner is interrupted this long after each hack
  // (AIQuickHackStatusEffect.HackingInterrupted on it for 2 s; -1: never).
  double hackInterruptSeconds{-1.0};
  int hackCarSeconds{0};
  std::string storyFile;         // host: a scripted story (tools/scenarios/story-host.story)
  std::string restrictions;      // host: story restrictions on its V (REC[,REC]), as steps
  double restrictAt{2.0};        // ...this long after a guest is in
  double restrictFor{0.0};       // ...lifted after this long (0: never)
  std::string prefabs;           // host: its quests' world prefabs (REF[,REF], hex), as a step
  int gates{-1};                 // our story gates (proto::kGate* bits), said once in session
  bool noFastTravel{};           // host: its quest blocks fast travel (kStoryNoFastTravel)
  int answerSeconds{-1};         // join: answer the host's ringing call after this long (-1: never)
  std::uint32_t replyEntry{};    // join: reply to the text with this choice entry hash
  int suggestIndex{-1};          // join: suggest this option of every hub (-1: never)
  int hoverIndex{-1};            // join: move our cursor to this option of every hub (-1: never)
  int sandevistanAtSeconds{-1};  // switch a Sandevistan on this long into the session (-1: never)
  int sandevistanForSeconds{5};  // ...for this long
  float sandevistanScale{0.25f}; // ...asking for this world speed
  std::uint8_t timeAbility{tandem::proto::kTimeSandevistan}; // ...as this ability (--scan-at)
  int grabNpcsSeconds{0};   // join: grab and take down one NPC this often (0: never)
  int carryNpcsSeconds{0};  // join: pick up, carry and throw a body this often
  int holdNpcsSeconds{0};   // host: our V grabs and takes down a replayed NPC this often
  std::string playFile;     // --play: a real V's play (tandem/play.hpp)
  bool playLoop{};          // ...again from the start at its end
  Vec3 playOffset{};        // ...moved by this
  std::string savePlayFile; // record our own play there, as the plugin does
  std::string pathFile;     // path: the path to replay
  std::string savePathFile; // save the states heard (your V's path) here
  // tandem-relay: its address, a host token (host), the session's code (join)
  std::optional<tandem::net::RelayOptions> relay;
  std::string code;
  // metro: board this long into the session, ride this long.
  int metroAtSeconds{5};
  int metroForSeconds{40};
  // drive: our seat (not the driver's: an AI drives), a story scene, an NPC
  // driver (host).
  std::string seat{"seat_front_left"};
  bool scene{};
  std::string driverNpc;
  // Answer a scene ride with seats (the host's) by riding along: hidden,
  // carried with the car, out beside it when it ends.
  bool rideAlong{};
  // Fast travel together: when to travel (-1: never), where to, the fast
  // travel point arrived at (a record name; empty: none), when to ask the
  // others to come (-1: never), how to answer their offers.
  int travelAtSeconds{-1};
  Vec3 travelTo{-915.f, 1879.5f, 36.3f}; // the game's "teleport home" spot
  std::string travelPoint;
  int offerRegroupSeconds{-1};
  // Sleep: --skip-answer only, go to bed too (a sleep of our own, to the
  // same hour: the host counts it as our yes).
  enum class Answer { Yes, No, Ignore, Sleep } regroupAnswer{Answer::Yes};
  // Activities: when to ask for a time skip (-1: never), of how long, why,
  // how to answer the others' requests; what to be busy with, from when, for
  // how long (0: until the end).
  int askSkipSeconds{-1};
  int skipHours{3};
  // Comms (docs/architecture/comms.md): our link consent (auto: everyone;
  // ask: nobody until asked, and every request is accepted; off: never), a
  // request to every other player N s in (-1: never); a ping every N s (0:
  // never), our scan tag or reveal of shared NPCs every N s, our waypoint.
  enum class LinkAnswer { Auto, Ask, Off } linkConsent{LinkAnswer::Auto};
  int linkAtSeconds{-1};
  int unlinkAtSeconds{-1};
  // --link ask: a request is accepted this long after it came (a person
  // picking up a call; 0: at once).
  int linkAnswerSeconds{0};
  // --link-outcome: what a person does with a call that rings (--link ask):
  // answer it, decline it (after --link-answer-after), be busy (declined at
  // once), or let it ring out (declined as missed 15 s after, as a game does).
  enum class CallOutcome { Answer, Decline, Busy, Ignore } callOutcome{CallOutcome::Answer};
  int pingSeconds{0};
  int tagSeconds{0};
  int revealSeconds{0};
  // --reveal-device: the device our "Ping quickhack" reaches (0: none), when.
  std::uint64_t revealDevice{};
  int revealDeviceAtSeconds{5};
  std::optional<Vec3> waypoint;
  tandem::proto::WaypointKind waypointKind{tandem::proto::WaypointKind::Pin};
  int waypointClearSeconds{-1};
  // A team target (the "Mark target" quickhack) 5 s in: a shared NPC's id,
  // or "nearest" (empty: never).
  std::string markTarget;
  // host: our V "quickhacks" a shared NPC (an id, or "nearest") every 20 s:
  // --hack-action's upload for 3 s, then --hack-effect on it for 10 s, as
  // the host's game says it (NpcHackState, NpcStatus). Empty: never.
  std::string hackNpc;
  tandem::proto::SkipReason skipReason{tandem::proto::SkipReason::Wait};
  Answer skipAnswer{Answer::Yes};
  std::string activity;
  int activityAtSeconds{3};
  int activityForSeconds{15};
  // --activity contest: what it is (proto::ContestSort; a duel names the
  // first NPC we know as its opponent).
  tandem::proto::ContestSort contest{tandem::proto::ContestSort::Other};
  tandem::net::LinkShape link; // a bad network, both ways (none: direct)
  bool traffic{};              // the whole traffic table in each report
  bool legacyWire{};           // send as before netcode.md (Endpoint::SetCompact(false))
  // Loot: drop one of this record in front of the other V (empty: never);
  // pick up the others' world items this long after they appear (-1: never);
  // host of a replay: dead NPCs' bodies go after this long (-1: never).
  std::string dropItem;
  // Progression (docs/architecture/progression.md): our V's level (0: none
  // said), and (host) the power level we say our NPCs have (0: none said).
  int level{0};
  float npcLevel{0.f};
  // Garages (docs/architecture/garage.md): who may drive our cars (-1: not
  // said), and (host) a vehicle our quest "gives" while the guests take part.
  int lend{-1};
  std::string unlockVehicle;
  // Vendors (docs/architecture/progression.md): (host) one vendor NPC of
  // this record standing there, announced with that vendor's id (0: none),
  // whether it is a placed NPC too (a stall's seller is the crowd's: not),
  // and when it dies after a guest is there (-1: never).
  std::string vendorRecord;
  Vec3 vendorAt{};
  std::uint64_t vendorEntity{};
  bool vendorPlaced{true};
  int vendorDiesSeconds{-1};
  // Trade (docs/architecture/trade.md): ask the first other player (again
  // every 20 s until one trade is done); accept whoever asks; what we put
  // on the table (RECORD[:N],...), the eddies we give and ask for. We agree
  // once the other side does.
  bool tradeOffer{false};
  bool tradeAccept{false};
  std::string tradeGive;
  int tradeMoney{0};
  int tradeAsks{0};
  std::string lootQuest; // "our V" loots one of this record (a quest item), once
  int takeItemsSeconds{-1};
  int removeCorpsesSeconds{-1};
  // region: how far from the other V we start, how many NPCs of which
  // record we run, when we walk up to it and when we leave them there.
  float regionFar{1000.f};
  std::optional<Vec3> regionAt;
  int regionNpcs{3};
  std::string regionRecord{"Character.kab_tyger_claws_biker1_ranged1_saratoga_ma"};
  int regionWalkAtSeconds{10};
  int regionLeaveAtSeconds{-1};
  // ...or this long after our own endpoint says we are linked (-1: never).
  int regionLeaveLinkedSeconds{-1};
  float regionSpeed{25.f};
  // region: world vehicles of our own (docs/architecture/traffic.md, "Who
  // owns an area"): this many cars round our V, the first one driven by our
  // last NPC (--region-driver), chasing the first player we meet
  // (--region-chase); left where they are when we walk off, like the NPCs.
  int regionCars{0};
  // region: our NPCs are placed in the world (regions.md §14): their static
  // entity ids, from this one up (0: they are not); our first NPC dies this
  // long in, by our own V (-1: never): the session's ledger remembers it.
  std::uint64_t regionPlaced{};
  int regionKillAtSeconds{-1};
  // region: our NPCs cannot die (a quest's immortal): their health floors
  // at 1 %, and "said" tells the others so (NpcHealth kNpcProtected),
  // "silent" does not ("": mortal).
  std::string regionImmortal;
  bool regionDriver{};
  bool regionChase{};
  bool check{}; // parse the options, then exit
  // Down and out: our V goes down (revivable) or out N s after another player
  // is there (-1: never); a downed other V is revived after N s (-1: never).
  int goDownSeconds{-1};
  int goOutSeconds{-1};
  int reviveSeconds{-1};
  // Devices: the ones to operate by id (--device), and what every device
  // another player changes is made this long after (--device-answer).
  struct DeviceOrder {
    std::uint64_t entity{}, cls{};
    std::string words;
    int atSeconds{5};
    bool done{};
  };
  std::vector<DeviceOrder> devices;
  std::string deviceAnswer;
  int deviceAfterSeconds{3};
  // Glass and breakable scenery: what "breaks in our game", and when
  // (--fracture).
  struct FractureOrder {
    tandem::proto::Fracture fracture;
    int atSeconds{5};
    bool done{};
  };
  std::vector<FractureOrder> fractures;
  // A device's fire: as a guest, "our copy of the turret has our V"
  // (--turret-seen); as a host every turret a guest says so of fires at
  // that guest (--turret-shots each time; 0: it says nothing of it).
  std::uint64_t turretSeen{};
  int turretSeenAtSeconds{5};
  // A turret in our V's hands (--turret-hands): which, where it points,
  // when we take it (it fires from 2 s to 4 s in, let go at 6 s, or after
  // --turret-hands-for seconds).
  std::uint64_t turretHands{};
  Vec3 turretHandsAt;
  int turretHandsAtSeconds{5};
  int turretHandsForSeconds{6};
  int turretShots{10};
  // Breach Protocol: what to breach and when (--breach), with which daemons.
  struct BreachOrder {
    std::uint64_t entity{}, cls{};
    std::uint32_t npc{};
    int atSeconds{5};
    bool done{};
  };
  std::optional<BreachOrder> breach;
  std::vector<std::uint64_t> breachPrograms;
  bool breachFailed{};
  // Quest interactions: the uses to ask about (--interact), and what our
  // game answers as a host (--interact-answer; -1: no answer at all).
  struct InteractOrder {
    std::uint64_t entity{}, choice{}, detail{};
    int atSeconds{5};
    bool done{};
  };
  std::vector<InteractOrder> interacts;
  int interactAnswer{tandem::proto::kInteractLocal};
  // Faults on what we send (docs/architecture/consistency.md, Consistency).
  float drop{}, dup{};
  std::uint32_t faultSeed{}; // 0: a new one each run (logged)
  int vanishAtSeconds{-1};   // end without a goodbye this long into the session (-1: never)
  // The street (docs/architecture/traffic.md). Host: this many cars driving
  // round the guest, one more parked, of this record; the first one burns
  // and blows up this long in (-1: never). Guest: ram a shared vehicle this
  // often (0: never); get into the nearest one this long in (-1: never).
  int trafficCars{0};
  // ... announced this long after the guest is with us (a guest already
  // riding in our car by then: its proxies spawn during the ride).
  int trafficAtSeconds{0};
  std::string trafficVehicle{"Vehicle.v_standard2_archer_hella"};
  int wreckAtSeconds{-1};
  int ramTrafficSeconds{0};
  // Guest: quickhack the nearest shared vehicle this long in, once (-1: never).
  int hackTrafficAtSeconds{-1};
  int stealAtSeconds{-1};
  bool stealDriven{}; // --steal-at takes the nearest car with a driver (not parked)
  // Police (docs/architecture/police.md): "our V" is wanted at this level
  // (0: never) from heatAt for heatFor seconds; host: tell the guest's game
  // its V was seen committing a crime this long in (-1: never).
  int heat{0};
  int heatAtSeconds{5};
  int heatForSeconds{20};
  int crimeAtSeconds{-1};
  // Presence (docs/architecture/presence.md): our V is hurt this often (0:
  // never), swings a melee weapon this often, is in a menu N s in, for 5 s
  // (-1: never); host: one of our NPCs plays a hit reaction this often.
  int hurtSeconds{0};
  // --hurt-hit: each of those hits told as the hit it was (HitTaken), by
  // whom: world, npc or you (empty: not told, the health's fall alone).
  std::string hurtHit;
  int meleeSeconds{0};
  int menuAtSeconds{-1};
  int reactNpcsSeconds{0};
  int voiceNpcsSeconds{0};
  int corpseNpcsSeconds{0};
  std::string holdRecord;     // what our V holds in its right hand ("": nothing said)
  std::string holdLeftRecord; // ...and in its left (--hold-left: dual wielding)
  int holsterAtSeconds{-1};   // put it away N s in, for 5 s (-1: never)
  // Vehicle combat (docs/architecture/vehicle-combat.md). drive: our car is
  // hit this long after we sit (dents, a flat tyre, a part off), burns 4 s
  // later and blows up 4 s after that (-1: never); we leave it this long
  // after we sit, the hard way (--exit-kind; -1: never); we push the car
  // another player drives this often (0: never). Any mode: shoot that car
  // this often (0: never); ask for the gunner seat of another player's car
  // this long in, aim and fire from it for 20 s (-1: never). join: run into
  // one of the host's NPCs this often (0: never). host --traffic: the first
  // car chases the guest this long in, for 20 s (-1: never).
  int carDamageSeconds{-1};
  int exitAtSeconds{-1};
  tandem::proto::ExitKind exitKind{tandem::proto::ExitKind::Bail};
  int bumpSeconds{0};
  int shootCarsSeconds{0};
  int gunnerSeconds{-1};
  std::string gunnerSeat{"seat_back_left"};
  int ramNpcsSeconds{0};
  int chaseSeconds{-1};
};

// TweakDBID of a record name: CRC32 of the name, its length in the top byte.
std::uint64_t TweakDBID(std::string_view name) {
  std::uint32_t crc = 0xFFFFFFFF;
  for (const unsigned char c : name) {
    crc ^= c;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
  }
  return static_cast<std::uint64_t>(~crc) | (static_cast<std::uint64_t>(name.size()) << 32);
}

// CName hash: FNV-1a 64.
std::uint64_t CName(std::string_view name) {
  std::uint64_t hash = 0xCBF29CE484222325;
  for (const unsigned char c : name)
    hash = (hash ^ c) * 0x100000001B3;
  return hash;
}

// The game's radio stations, by ERadioStationList index (final.reds,
// RadioStationDataProvider.GetStationName).
constexpr std::string_view kStations[] = {
    "radio_station_02_aggro_ind",    "radio_station_03_elec_ind",  "radio_station_04_hiphop",
    "radio_station_07_aggro_techno", "radio_station_09_downtempo", "radio_station_01_att_rock",
    "radio_station_05_pop",          "radio_station_10_latino",    "radio_station_11_metal",
    "radio_station_06_minim_techno", "radio_station_08_jazz",      "radio_station_12_growl_fm",
    "radio_station_13_dark_star",    "radio_station_14_impulse_fm"};

std::string_view StationName(int station) {
  return station >= 0 && station < static_cast<int>(std::size(kStations)) ? kStations[station]
                                                                          : "off";
}

template <class... Args> void Log(std::format_string<Args...> format, Args &&...args) {
  std::cout << std::format("[{:>8.3f}] ", tandem::net::NowUs() % 100'000'000 / 1e6)
            << std::format(format, std::forward<Args>(args)...) << '\n'
            << std::flush;
}

[[noreturn]] void Usage(std::string_view error = {}) {
  if (!error.empty())
    std::cerr << "tandem-peer: " << error << "\n\n";
  std::cerr
      << "usage: tandem-peer host [options]\n"
         "       tandem-peer join ADDRESS[:PORT] [options]\n"
         "       tandem-peer join --code CODE --relay HOST[:PORT] [options]\n"
         "       tandem-peer play-info FILE   what a gameplay recording holds, where it starts\n"
         "       tandem-peer sim --role host|join ...  a simulated second game (sim --help)\n"
         "       tandem-peer sim-fuzz ...     simulated games against each other, seed by seed\n\n"
         "       tandem-peer traffic FILE...  the bandwidth a recording takes, legacy and "
         "compact\n\n"
         "  --port N           UDP port (default 27450)\n"
         "  --relay HOST[:PORT] tandem-relay (default port 27460): host, also reachable\n"
         "                     through it (its join code is logged); join, used when\n"
         "                     ADDRESS does not answer, or with --code only\n"
         "  --relay-token HEX  host: one of the relay's host tokens (tandem-relay token)\n"
         "  --code CODE        join: the host's join code (needs --relay)\n"
         "  --mode M           mirror | ghost | front | drive | course | path | metro |\n"
         "                     watch | region | idle (default mirror)\n"
         "  --region-far M     region: start this far from the other V (default 1000)\n"
         "  --region-at X,Y,Z  region: around this point instead of where the other V\n"
         "                     first stood (so two mock players can meet)\n"
         "  --region-npcs N    region: run this many NPCs of our own (default 3)\n"
         "  --region-record R  region: their character record\n"
         "  --region-walk-at S region: walk up to the other V this long in (default 10;\n"
         "                     -1: stay away)\n"
         "  --region-leave-at S region: walk off again, leaving the NPCs (default -1: never)\n"
         "  --region-leave-linked S region: walk off S seconds after our endpoint says we\n"
         "                     are linked (the host's Regions reached us: our NPCs' states\n"
         "                     flow), whatever the clock says\n"
         "  --region-speed V   region: how fast it walks, m/s (default 25)\n"
         "  --region-cars N    region: run N world vehicles of our own round our V (the\n"
         "                     --traffic-vehicle record), left behind with the NPCs\n"
         "  --region-placed HEX region: our NPCs are placed in the world, their static\n"
         "                     entity ids HEX, HEX+1... (NpcPlaced): twins by identity, and\n"
         "                     their deaths go into the session's ledger (NpcDeath)\n"
         "  --region-kill-at S region: our first NPC dies S seconds in, by our own V\n"
         "  --region-immortal said|silent region: our NPCs cannot die (health floors at 1 %);\n"
         "                     said: NpcHealth says so (kNpcProtected); silent: it does not,\n"
         "                     and a hit at the floor sends nothing\n"
         "  --region-driver    region: our last NPC drives our first car (NpcSeat)\n"
         "  --region-chase     region: our first car chases the first player we meet\n"
         "  --name NAME        player name (default tandem-peer)\n"
         "  --body B           female | male: the puppet rig others see (default female)\n"
         "  --look L           random | echo: random clothes of its own (default), on a\n"
         "                     real V body of its --body (the keys in --look-keys),\n"
         "                     or a copy of the other player's look (mirror, front)\n"
         "  --look-keys DIR    where female.keys / male.keys are kept (default\n"
         "                     ~/.cache/tandem/looks): the body keys of a real V,\n"
         "                     saved from every look heard with keys\n"
         "  --seed N           --look random: the same outfit every time for N\n"
         "  --offset X,Y,Z     mirror: metres added to your position (default 2,0,0)\n"
         "  --delay MS         mirror: extra delay before copying (default 0)\n"
         "  --rate HZ          states sent per second (default 30)\n"
         "  --pose FILE        play a recorded body pose (.tpose) in a loop\n"
         "  --play FILE        replay a real V's play (.tplay: tools/game.sh fetch-play) as\n"
         "                     our player, at its recorded positions, instead of --mode's\n"
         "                     motion; without FILE, --mode runs\n"
         "  --play-loop        --play, --play-session: start again at the end (default: stop\n"
         "                     there)\n"
         "  --record-session FILE  join: record everything the host sends us (its V, NPCs,\n"
         "                     story, world, hits) to FILE (.tsession), for --play-session;\n"
         "                     with --mode watch, unseen, 30 m under the host's V\n"
         "  --play-session FILE  host: be the real host recorded in FILE for the guest that\n"
         "                     joins (its V, NPCs, story, world), its own --mode motion off;\n"
         "                     without FILE, --mode and --replay run\n"
         "  --play-offset X,Y,Z  --play: metres added to every position (default 0,0,0)\n"
         "  --save-play FILE   record what we send about our own V, as the game does, for\n"
         "                     --play (tests/peer_play.sh)\n"
         "  --vehicle RECORD   drive: the car's record (default "
         "Vehicle.v_standard2_archer_hella_player)\n"
         "  --drive-seconds N  drive: get out after N seconds and stand by the car\n"
         "  --drive-radius M   drive: the circle round your V, in metres (default 12; 60:\n"
         "                     a ride that leaves a guest on foot behind)\n"
         "  --radio STATION    drive: the car's radio plays STATION (0-13 or its name,\n"
         "                     radio_station_09_downtempo...; off): sent when seated and\n"
         "                     again when a passenger gets in\n"
         "  --paint RRGGBB[,RRGGBB][@HUE] drive: the car carries a custom paint (CarPaint):\n"
         "                     its colour, a second one, its lights' hue in degrees\n"
         "  --paint-off-at S   --paint: back to stock paint S seconds after taking the seat\n"
         "  --summon S         drive: our car is summoned S seconds after we meet, drives in\n"
         "                     on its own (OwnCar, its states) and parks 4 m from us, on foot\n"
         "  --summon-board S   --summon: we get in S seconds after it parked, then drive\n"
         "  --summon-again S   --summon: summoned again S seconds after it parked (the same\n"
         "                     car, from the other side)\n"
         "  --hit-npcs N       join: hit one of the host's NPCs (35 health) every N seconds\n"
         "  --shoot-you N      host: an NPC (a replayed one) fires at each guest's V every N\n"
         "                     seconds: the guest's game fires it and decides the hit\n"
         "  --shot-weapon REC  --shoot-you: the NPC's weapon (default\n"
         "                     Items.Preset_Lexington_Default)\n"
         "  --shot-smart       --shoot-you: a smart gun's rounds, locked on the V: guided\n"
         "                     there (give a smart --shot-weapon: Items.Preset_Yukimura_Default)\n"
         "  --strike KIND      --hit-npcs: what each hit carries: plain (35 health), nonlethal\n"
         "                     (a blunt hit: knocks out), cap (leave 1 HP), poise (10 health,\n"
         "                     40 poise), head (a headshot), blade (a katana on the left arm),\n"
         "                     offhand (the left hand's weapon's: --hold-left)\n"
         "  --hit-you N        host: an NPC (a replayed one if any) hits each guest's V for\n"
         "                     10 physical every N seconds\n"
         "  --hit-you-melee X  --hit-you: on, the hits are melee (blockable; the guest\n"
         "                     waits for its proxy's swing)\n"
         "  --difficulty D     host: the session's difficulty: easy | hard | veryhard | story\n"
         "                     (or 0-3); guests play at it\n"
         "  --area-hit N       every N seconds our \"grenade\" hits every other player's V for\n"
         "                     20 physical (an explosion, applied by their game)\n"
         "  --friendly-fire X  host: the session's rule, on | off (the Vs hurt each other)\n"
         "  --enemy-scaling X  host: the session's rule, on | off (hostile NPCs tougher for\n"
         "                     each extra player near them)\n"
         "  --enemy-scaling-health F  host: --enemy-scaling's health per extra player (0.6)\n"
         "  --near-npc         join: stand 4 m from one of the host's NPCs (the one heard\n"
         "                     first that is still there): it counts us near it\n"
         "  --throw N          every N seconds our V throws a grenade (--throw-item) 10 m\n"
         "                     ahead; it explodes 2 s later (the others launch a copy)\n"
         "  --throw-item REC   --throw: the grenade's record (Items.GrenadeFragRegular)\n"
         "  --throw-bounce     --throw: a second in, the grenade is 3 m off its arc (its\n"
         "                     path is said as it flies) and it explodes 3 m to the side\n"
         "  --footsteps        our V's footsteps as noise for the others' NPCs: a run's\n"
         "                     (stim 25), a sprint's (26), 4 a second; none slow or crouched\n"
         "  --smart N          every N seconds our V fires a smart round at an NPC we see\n"
         "  --defend N         join: every N seconds we parry, then block, an NPC's attack\n"
         "  --time HH:MM       host: the time of day the game must follow\n"
         "  --weather NAME     host: the weather it must follow (24h_weather_rain...)\n"
         "  --record FILE      join: save the host's NPCs (spawns, states, gone) to FILE\n"
         "  --replay FILE      host: send a recording's NPCs again, at its pace (moved by\n"
         "                     --offset)\n"
         "  --anims FILE       host: with --replay, NPC animation states and shots from\n"
         "                     FILE (lines: SECONDS NPC|* HIGH UPPER STANCE\n"
         "                     [weapon=RECORD] [target=PLAYER] [shoot=N]\n"
         "                     [workspot=HASH:ENTRY:ANIM])\n"
         "  --pause-at N       open the pause menu N seconds after another player is in the\n"
         "                     session: every game pauses\n"
         "  --pause-for N      --pause-at: close it again after N seconds (default 3)\n"
         "  --npc RECORD       host: one NPC of RECORD standing 6 m in front of the guest\n"
         "  --detect-at N      host: N seconds after a guest is there, the NPCs it sends\n"
         "                     notice it (meter rising over 2 s), then have detected it\n"
         "  --detect-for N     --detect-at: and lose it again after N seconds (default 10)\n"
         "  --camo N           turn optical camo on N seconds after another player is in the\n"
         "                     session, for 5 seconds\n"
         "  --camo-record REC  --camo: its status effect record, as a V's camo sends it\n"
         "                     (BaseStatusEffect.OpticalCamoCoolPerkShort)\n"
         "  --status REC@FROM-TO[,REC@FROM-TO]  status effects that show on our V, each\n"
         "                     from FROM to TO seconds after another player is in (no TO:\n"
         "                     kept): the others' puppet of it plays their visuals\n"
         "  --shoot N          fire a gunshot every N seconds: the shot, where its bullet\n"
         "                     ended (a cue: 6 m past the other V), and as a guest its noise\n"
         "  --ricochet         --shoot: each bullet ricochets on, 3 m aside and 1.5 m up (a cue)\n"
         "  --charge N         every N seconds our V charges its weapon (a tech one: --hold)\n"
         "                     for 2.5 s, said as it goes (charging, ready, full), and fires\n"
         "  --hurt N           our V is hit every N seconds: its health falls a quarter (the\n"
         "                     others' puppet of it grunts and flinches), then is full again\n"
         "  --hurt-hit WHO     --hurt: each hit is also told as the hit it was (a bullet\n"
         "                     on its left leg, from its left), by WHO: world (nobody the\n"
         "                     others know) | npc (the first shared NPC: as host, --npc's)\n"
         "                     | you (the other V)\n"
         "  --melee N          swing a melee weapon every N seconds, and land it (a cue)\n"
         "  --menu-at N        our V is in a menu N seconds after another player is in the\n"
         "                     session, for 5 seconds\n"
         "  --hold RECORD      our V holds this item in its right hand (a weapon for --shoot\n"
         "                     and --melee to be seen and heard: Items.Preset_Lexington_Default)\n"
         "  --hold-left RECORD our V also holds this item in its left hand (dual wielding:\n"
         "                     two pistols); --shoot, --charge and --smart then alternate\n"
         "                     hands, the left hand's said as the left's\n"
         "  --holster-at N     --hold: put it away N seconds after another player is in the\n"
         "                     session, and draw it again 5 seconds later\n"
         "  --held-record NAME:TYPE[:CATEGORY], --vehicle-record NAME:TYPE[:SEATS],\n"
         "  --look-piece NAME:TYPE (again for more), --npc-record NAME:TYPE[:GENDER]\n"
         "                     a record a mod would add, with its hint (the game's enum names:\n"
         "                     Wea_Handgun Weapon, Car 2, Clo_Legs, Human Female): held, driven\n"
         "                     (--vehicle), worn, or the host's NPC (--npc)\n"
         "  --unknown NAME,... records our game lacks: the others' are resolved as a game\n"
         "                     does (\"mods: unknown ... shown as ...\") and told\n"
         "  --react-npcs N     host: every N seconds one of the NPCs it sends plays a hit\n"
         "                     reaction (twitch, impact, stagger, knockdown, ragdoll, its end)\n"
         "  --voice-npcs N     host: every N seconds one of the NPCs it sends says a voice\n"
         "                     line (a callout, a pain cry, a fear bark, a death cry: NpcVoice)\n"
         "  --corpse-npcs N    host: every N seconds one of the NPCs it sends dies, and 1 s\n"
         "                     later its body's rest is said where it stood (CorpsePose)\n"
         "  --hack-npcs N      join: upload a quickhack on one of the host's NPCs every N\n"
         "                     seconds (1.5 s upload), then land its effect\n"
         "  --hack-action REC  --hack-npcs: the quickhack (default QuickHack.BlindHack)\n"
         "  --hack-effect REC  --hack-npcs: the effect it lands (default BaseStatusEffect.Blind)\n"
         "  --hack-player N    host: one of our (replayed) NPCs hacks the guest's V every N\n"
         "                     seconds\n"
         "  --npc-hack REC     --hack-player: the NPC's hack (default\n"
         "                     AIQuickHack.HackRevealPosition, a trace)\n"
         "  --hack-interrupt S --hack-player: its netrunner is interrupted S seconds after\n"
         "                     each hack (HackingInterrupted on it for 2 s, NpcStatus)\n"
         "  --hack-car N       hack the car another player drives (brakes, 3 s) every N\n"
         "                     seconds\n"
         "  --story FILE       host: play a scripted story (journal, calls, lines, hubs,\n"
         "                     rewards; format in tools/scenarios/story-host.story)\n"
         "  --restrict REC[,REC]  host: its story puts these gameplay restrictions on its V\n"
         "                     (GameplayRestriction.NoCombat,...): guests in its moment wear\n"
         "                     them; join: logs the host's set as heard\n"
         "  --restrict-at S    --restrict: S seconds after a guest is in (default 2)\n"
         "  --restrict-for S   --restrict: lifted after S seconds (default 0: kept)\n"
         "  --prefabs REF[,REF]  host: its quests have these world prefabs loaded (node\n"
         "                     refs, hex), 2 s after a guest is in; join: logs the host's\n"
         "                     set as heard\n"
         "  --no-fast-travel   host: its quest blocks fast travel (StoryState's flag), 2 s\n"
         "                     after a guest is in: a guest's V never respawns far away\n"
         "  --gates N          our story gates (1 Watson lockdown lifted, 2 Dogtown open,\n"
         "                     4 our V in Watson, 8 in Dogtown), said once in the session\n"
         "  --answer N         join: answer the host's ringing call after N seconds\n"
         "  --reply HASH       join: reply to a text with this choice entry (journal hash)\n"
         "  --suggest N        join: suggest option N of every choice hub the host shows\n"
         "  --hover N          join: move our cursor to option N of every choice hub the\n"
         "                     host shows (the host's hub marks it)\n"
         "  --sandevistan-at N switch a Sandevistan on N seconds after another player is in\n"
         "                     the session: the host slows the NPCs around this V\n"
         "  --sandevistan-for N  --sandevistan-at: switch it off after N seconds (default 5)\n"
         "  --sandevistan-scale S  --sandevistan-at: the world speed it asks for (default 0.25)\n"
         "  --scan-at N        as --sandevistan-at, with its scanner's slow motion instead\n"
         "                     (--sandevistan-for and --sandevistan-scale apply)\n"
         "  --grab-npcs N      join: every N seconds grab one of the host's NPCs, then take it\n"
         "                     down (lethal, then non-lethal, in turn)\n"
         "  --carry-npcs N     join: every N seconds pick up a dead NPC, carry it, throw it\n"
         "  --hold-npcs N      host: every N seconds our V grabs a replayed NPC and takes it\n"
         "                     down; the game gets a combat cue for it\n"
         "  --travel-at N      fast travel N seconds after another player is in the session:\n"
         "                     stand at --travel-to and offer everyone to join\n"
         "  --travel-to X,Y,Z  --travel-at: where to (default -915,1879.5,36.3, the game's\n"
         "                     teleport home spot)\n"
         "  --travel-point REC --travel-at: the fast travel point record arrived at\n"
         "  --offer-regroup N  N seconds after another player is in the session, ask them\n"
         "                     to come to us\n"
         "  --regroup-answer A yes | no | ignore: how to answer an offer to join (default\n"
         "                     yes: go there)\n"
         "  --link L           auto | ask | off: whom we share tags with (auto: everyone,\n"
         "                     the default; ask: nobody until asked, every request\n"
         "                     accepted; off: nobody, requests ignored)\n"
         "  --link-at N        N seconds after another player is in, ask everyone to link\n"
         "  --link-answer-after N  with --link ask: accept a request N seconds after it\n"
         "                     came (default 0: at once)\n"
         "  --link-outcome O   with --link ask, a call that rings: answer (default),\n"
         "                     decline (after --link-answer-after), busy (declined at\n"
         "                     once), ignore (missed: declined 15 s after) (LinkDecline)\n"
         "  --unlink-at N      N seconds after another player is in, break every link\n"
         "  --ping N           every N seconds a ping: in front of the other V, then the\n"
         "                     nearest shared NPC (enemy), the same again (danger), a\n"
         "                     withdrawal, round again\n"
         "  --tag N            every N seconds tag (or untag) the nearest shared NPC, as\n"
         "                     the scanner's tag (seen by linked players only)\n"
         "  --reveal N         every N seconds reveal up to 4 shared NPCs for 10 s, as\n"
         "                     the Ping quickhack (linked players only)\n"
         "  --reveal-device E[@S] our Ping quickhack reaches the device E (its id in hex) S\n"
         "                     seconds after another player is in (5): the linked pulse its\n"
         "                     network from their copy and draw its beams\n"
         "  --waypoint X,Y,Z   our waypoint (a map pin) there, once another player is in\n"
         "  --waypoint-kind K  what that waypoint is: pin (default), point (a tracked map\n"
         "                     point) or objective (our tracked quest objective)\n"
         "  --waypoint-clear N take the waypoint away N seconds after setting it\n"
         "  --mark-target ID   5 seconds after another player is in, make the shared NPC\n"
         "                     ID (or \"nearest\") our team target, as the Mark target\n"
         "                     quickhack does\n"
         "  --hack-npc ID      host: every 20 s our V quickhacks the shared NPC ID (or\n"
         "                     \"nearest\"): --hack-action uploads for 3 s, then\n"
         "                     --hack-effect is on it for 10 s, as a host's game says\n"
         "  --ask-skip N       N seconds after another player is in the session, ask to\n"
         "                     skip --skip-hours of game time\n"
         "  --skip-hours H     --ask-skip: how many hours (default 3)\n"
         "  --skip-reason R    wait | sleep | quest (host only: said, never asked)\n"
         "  --skip-answer A    yes | no | ignore | sleep: how to answer a time skip (default\n"
         "                     yes; sleep: ask a sleep of our own, two beds at once)\n"
         "  --activity KIND    busy for a while: workspot, sleep, screen, minigame, shop,\n"
         "                     menu, photo, contest\n"
         "  --contest SORT     --activity contest of a sort: duel (a fist fight with the\n"
         "                     first NPC we know: the others' hits on it are dropped),\n"
         "                     race or range\n"
         "  --activity-at N    --activity: N s after another player is there (default 3)\n"
         "  --activity-for S   --activity: for S s (default 15; 0: for good)\n"
         "  --latency MS       a bad network: MS added each way\n"
         "  --jitter MS        up to MS more at random, each way (the order kept)\n"
         "  --loss PERCENT     PERCENT of the datagrams lost, each way\n"
         "  --reorder PERCENT  PERCENT of the datagrams held back behind later ones\n"
         "  --burst N          losses come in bursts of N datagrams on average (--loss)\n"
         "  --device SPEC      operate a device by its id, SPEC = ENTITY:CLASS:WORDS[@SECONDS]:\n"
         "                     ENTITY in hex (tools/game.sh cmd device near), CLASS its state's\n"
         "                     class (DoorControllerPS...), WORDS a comma list of on off\n"
         "                     unpowered disabled open closed locked unlocked sealed unsealed\n"
         "                     broken exploded hacked friendly hostile exposed breached\n"
         "                     glitching value=N floor=N station=N; SECONDS into the session\n"
         "                     (5). A guest asks the host; a host says its own device is that\n"
         "  --device-answer W  every device another player changes is made WORDS (as above)\n"
         "                     --device-after seconds later (not the host's reply to it)\n"
         "  --device-after N   see --device-answer (3)\n"
         "  --fracture SPEC    something breaks in our game (glass, breakable scenery), SPEC =\n"
         "                     [ENTITY:COMPONENT:]X,Y,Z[@SECONDS]: where, and for a part of a\n"
         "                     placed entity its id in hex and its destruction component's\n"
         "                     name; SECONDS into the session (5). The breaks it hears are\n"
         "                     logged\n"
         "  --turret-seen E[@S] join: our copy of the turret E (its id in hex) locks our V S\n"
         "                     seconds into the session (5), and loses it 6 s later\n"
         "  --turret-hands E[:X,Y,Z][@S] our V holds the turret E (its id in hex) S seconds\n"
         "                     into the session (5), points it at X,Y,Z, fires it from 2 s\n"
         "                     to 4 s and lets go at 6 s. Turrets in others' hands, and\n"
         "                     our claims the host refused (another player holds it), are\n"
         "                     logged\n"
         "  --turret-hands-for D our V holds the --turret-hands turret D seconds (6): the\n"
         "                     others' claims on it are refused meanwhile\n"
         "  --turret-shots N   host: a turret a guest's copy has its V with turns to that V\n"
         "                     and fires N shots at it (10; 0: silent, as for a turret that\n"
         "                     is not in our world). What it hears of turrets is logged\n"
         "  --breach SPEC      Breach Protocol won, SPEC = ENTITY[:CLASS][@SECONDS] (an access\n"
         "                     point by its id in hex; CLASS AccessPointControllerPS) or\n"
         "                     npc=N[@SECONDS] (the quickhack on NPC N); SECONDS into the\n"
         "                     session (5)\n"
         "  --programs LIST    --breach: the daemons uploaded, MinigameAction records by name\n"
         "                     or hex, comma separated (NetworkCameraShutdown,...)\n"
         "  --breach-failed    --breach: the minigame is lost (the network is alerted)\n"
         "  --interact SPEC    a guest's V uses the host's object, SPEC =\n"
         "                     ENTITY:CHOICE[:DETAIL][@SECONDS]: ENTITY in hex, CHOICE the\n"
         "                     action's class (or loot action) by name or 0x hash, DETAIL\n"
         "                     its name or the item's record hash; SECONDS into the session\n"
         "                     (5). Twice the same SPEC: the second is answered at once when\n"
         "                     the first was host-only or local\n"
         "  --interact-answer V a host's verdict on every guest's use: local (default: no world\n"
         "                     of ours, the guest's copy is all), performed, host-only,\n"
         "                     missing, busy, too-far, or none (silent)\n"
         "  --traffic-report   each report has the whole traffic table (by message type)\n"
         "  --legacy-wire      send as before the netcode work: every state every frame,\n"
         "                     whole poses and NPC lists (to compare the bandwidth)\n"
         "  --loot-quest REC   5 s after another player is there, \"our V\" loots one REC (a\n"
         "                     quest item): every other player gets it too\n"
         "  --level N          our V's level (1-100): the host scales its NPCs to the mean\n"
         "                     of the players' levels and tells everyone (logged)\n"
         "  --npc-level L      host: say every NPC we share has power level L: the guest's\n"
         "                     proxies take it\n"
         "  --lend WHO         who may drive our cars: crew (anyone, the default in game),\n"
         "                     owner (only with us aboard), never\n"
         "  --unlock-vehicle REC host: 5 s after a guest is there, our garage \"gets\" REC:\n"
         "                     each guest is told it earned it with us (VehicleUnlock)\n"
         "  --vendor REC@X,Y,Z#ENTITY host: a vendor NPC of REC standing at X,Y,Z, said to be\n"
         "                     the placed vendor ENTITY (hex, as `vendor near` logs it):\n"
         "                     a guest opens its own game's vendor of it (NpcVendor)\n"
         "  --vendor-crowd     host, --vendor: the vendor is the crowd's, not a placed NPC\n"
         "                     (no NpcPlaced: its twin is known by record and place,\n"
         "                     its death is not the ledger's)\n"
         "  --vendor-dies N    host, --vendor: it dies N s after a guest is there (every\n"
         "                     game hears that vendor died)\n"
         "  --trade-offer      ask the first other player to trade, every 20 s until one\n"
         "                     trade is done; we agree once they do\n"
         "  --trade-accept     accept whoever asks us to trade; we agree once they do\n"
         "  --trade-give LIST  what we put on the table: RECORD[:N],... (Items.CommonMaterial1:2)\n"
         "  --trade-money N    the eddies we give\n"
         "  --trade-asks N     the eddies we ask for in return (a sale)\n"
         "  --drop-item RECORD drop one RECORD (Items.FirstAidWhiffV0...) as a shared world\n"
         "                     item in front of the other player, 5 s after they are there\n"
         "  --take-items N     pick up every world item the others drop, N seconds after it\n"
         "                     appears (the host gives it to the first)\n"
         "  --remove-corpses N host of a replay: an NPC a guest killed is taken away N\n"
         "                     seconds after its death (its body gone: NpcGone reason 0)\n"
         "  --go-down N        our V goes down N seconds after another player is there: it\n"
         "                     stays where it is until a player revives it\n"
         "  --go-out N         our V is out N seconds after another player is there (not\n"
         "                     revivable), and gets up again 10 s later\n"
         "  --revive N         another player's V that goes down is revived N seconds later:\n"
         "                     ours walks to it and holds the revive for 10 s\n"
         "  --save-path FILE   save the states heard (your V's moves) to FILE, for --path\n"
         "  --path FILE        path: the saved path to replay in a loop\n"
         "  --vanish-at N      N seconds after another player is in the session, vanish: the\n"
         "                     process ends with no goodbye (as Alt-F4, a crash, a cut cable)\n"
         "  --drop P           never send a message with probability P (0..1), reliable ones\n"
         "                     too, as a bug would lose them: digests and resyncs must heal it\n"
         "  --dup P            send a state message twice with probability P (0..1): it must\n"
         "                     be applied once\n"
         "  --fault-seed N     the seed of --drop and --dup (default: a new one, logged)\n"
         "  --metro-at N       metro: board the NCART N seconds after another player is in\n"
         "                     (default 5): it stands beside your V until then\n"
         "  --metro-for N      metro: ride for N seconds (default 40), skipping (ten times\n"
         "                     faster) for 3 s halfway, then get off where the ride ends\n"
         "  --seat SLOT        drive: sit in SLOT (seat_front_right...) while an AI drives:\n"
         "                     a ride its game runs (Transit Ride)\n"
         "  --scene            drive --seat: the ride is a story scene (host: partners near\n"
         "                     its copy get a free seat)\n"
         "  --driver-npc REC   host, drive --seat: an NPC of REC at the wheel (NpcSeat)\n"
         "  --ride-along       a player's scene ride with seats (Transit Scene): ride along\n"
         "                     (Transit Along), carried with its car, out beside it at the end\n"
         "  --traffic N        host: N cars drive round where the guest was first heard, one\n"
         "                     more is parked there (shared world vehicles); --npc REC puts\n"
         "                     a driver in the first; hits wear them down to wrecks\n"
         "  --traffic-at N     host --traffic: the cars are announced N seconds after the guest\n"
         "                     is with us (default 0), round where it is then\n"
         "  --traffic-vehicle REC  the cars' record (Vehicle.v_standard2_archer_hella)\n"
         "  --wreck-at N       host --traffic: the first car burns N seconds in, then blows up\n"
         "  --ram-traffic N    join: ram a shared vehicle every N seconds (TrafficHit)\n"
         "  --hack-traffic S   join: quickhack the nearest shared vehicle S seconds into the\n"
         "                     session, once (brakes for 8 s: TrafficHack)\n"
         "  --steal-at N       join: N seconds in, get into the nearest shared vehicle and sit\n"
         "                     at its wheel for --drive-seconds (default 15): it is ours\n"
         "  --steal-driven     --steal-at takes the nearest car with a driver, not a parked\n"
         "                     one (its game puts the driver out)\n"
         "  --heat N           our V is wanted at N stars (police heat is per player)\n"
         "  --heat-at N        ...from N seconds in (default 5)\n"
         "  --heat-for N       ...for N seconds, the last 5 unseen (default 20)\n"
         "  --crime-at N       host: N seconds in, a witness calls the police on the guest's\n"
         "                     V (Crime): its own game gives it the heat\n"
         "  --car-damage N     drive: N seconds after we sit our car is hit (dents, a flat\n"
         "                     tyre, the front bumper off), burns 4 s later, blows up 4 s\n"
         "                     after that (VehicleDamage: every copy the same)\n"
         "  --exit-at N        drive: leave the car N seconds after we sit, the hard way\n"
         "  --exit-kind K      --exit-at: bail | thrown | knocked | dead | exploded (default\n"
         "                     bail: jump out on the move; the car rolls on to a stop)\n"
         "  --bump N           drive: push the car another player drives every N seconds\n"
         "                     (VehicleBump; their game applies it unless it felt it)\n"
         "  --shoot-cars N     shoot the car another player drives every N seconds\n"
         "                     (VehicleHit to its driver's game; every other one a tyre)\n"
         "  --gunner N         N seconds after another player is there, ask for the gunner\n"
         "                     seat of their car; aim and fire from it for 20 s, then leave\n"
         "  --gunner-seat SLOT --gunner: the seat asked for (default seat_back_left)\n"
         "  --ram-npcs N       join: run into one of the host's NPCs every N seconds at\n"
         "                     12 m/s (NpcRam, and its hit as NpcStrike)\n"
         "  --chase N          host --traffic: the first car chases the first guest N seconds\n"
         "                     in, for 20 s (Chase)\n";
  std::exit(error.empty() ? 0 : 2);
}

template <class T> T Number(std::string_view text) {
  T value{};
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    Usage(std::format("not a number: {}", text));
  return value;
}

// A device's state changed by words ("open", "locked,off", "floor=3"):
// false for a word nobody knows.
bool DeviceWords(std::string_view words, tandem::devices::What &what) {
  using namespace tandem::proto;
  const auto set = [&](std::uint16_t bit, bool on) {
    what.flags = static_cast<std::uint16_t>(on ? what.flags | bit : what.flags & ~bit);
  };
  while (!words.empty()) {
    const auto comma = words.find(',');
    const auto word = words.substr(0, comma);
    words = comma == std::string_view::npos ? std::string_view{} : words.substr(comma + 1);
    if (word == "on")
      what.status = kDeviceOn;
    else if (word == "off")
      what.status = kDeviceOff;
    else if (word == "unpowered")
      what.status = kDeviceUnpowered;
    else if (word == "disabled")
      what.status = kDeviceDisabled;
    else if (word == "open") {
      set(kDeviceOpen, true);
      set(kDeviceLocked | kDeviceSealed, false);
    } else if (word == "closed")
      set(kDeviceOpen, false);
    else if (word == "locked") {
      set(kDeviceLocked, true);
      set(kDeviceOpen, false);
    } else if (word == "unlocked")
      set(kDeviceLocked, false);
    else if (word == "sealed") {
      set(kDeviceSealed, true);
      set(kDeviceOpen, false);
    } else if (word == "unsealed")
      set(kDeviceSealed, false);
    else if (word == "broken")
      set(kDeviceBroken, true);
    else if (word == "exploded")
      set(kDeviceExploded, true);
    else if (word == "hacked")
      set(kDeviceHacked, true);
    else if (word == "friendly")
      set(kDeviceFriendly, true);
    else if (word == "hostile")
      set(kDeviceFriendly, false);
    else if (word == "exposed")
      set(kDeviceExposed, true);
    else if (word == "breached")
      set(kDeviceBreached | kDeviceExposed, true);
    else if (word == "glitching")
      set(kDeviceGlitching, true);
    else if (word.starts_with("value=") || word.starts_with("floor=") ||
             word.starts_with("station="))
      what.value = Number<std::int32_t>(word.substr(word.find('=') + 1));
    else
      return false;
  }
  return true;
}

// --device ENTITY:CLASS:WORDS[@SECONDS]
Options::DeviceOrder DeviceOrderOf(std::string_view spec) {
  Options::DeviceOrder order;
  const auto first = spec.find(':');
  const auto second = first == std::string_view::npos ? first : spec.find(':', first + 1);
  if (second == std::string_view::npos)
    Usage("--device is ENTITY:CLASS:WORDS[@SECONDS]");
  auto entity = spec.substr(0, first);
  if (entity.starts_with("0x"))
    entity.remove_prefix(2);
  if (std::from_chars(entity.data(), entity.data() + entity.size(), order.entity, 16).ptr !=
          entity.data() + entity.size() ||
      order.entity == 0)
    Usage(std::format("--device: {} is not an entity id in hex", entity));
  const auto cls = spec.substr(first + 1, second - first - 1);
  // A class by name, or by its hash as the logs print it.
  if (cls.starts_with("0x")) {
    if (std::from_chars(cls.data() + 2, cls.data() + cls.size(), order.cls, 16).ptr !=
        cls.data() + cls.size())
      Usage(std::format("--device: {} is not a class hash", cls));
  } else {
    order.cls = CName(cls);
  }
  auto words = spec.substr(second + 1);
  if (const auto at = words.find('@'); at != std::string_view::npos) {
    order.atSeconds = Number<int>(words.substr(at + 1));
    words = words.substr(0, at);
  }
  order.words = std::string(words);
  tandem::devices::What probe;
  if (order.words.empty() || !DeviceWords(order.words, probe))
    Usage(std::format("--device: unknown state in {}", words));
  return order;
}

Vec3 Point(std::string_view flag, std::string_view value);

// --fracture [ENTITY:COMPONENT:]X,Y,Z[@SECONDS]
Options::FractureOrder FractureOrderOf(std::string_view spec) {
  Options::FractureOrder order;
  if (const auto at = spec.find('@'); at != std::string_view::npos) {
    order.atSeconds = Number<int>(spec.substr(at + 1));
    spec = spec.substr(0, at);
  }
  if (const auto place = spec.rfind(':'); place != std::string_view::npos) {
    const auto part = spec.substr(0, place);
    const auto colon = part.find(':');
    if (colon == std::string_view::npos)
      Usage("--fracture is [ENTITY:COMPONENT:]X,Y,Z[@SECONDS]");
    auto entity = part.substr(0, colon);
    if (entity.starts_with("0x"))
      entity.remove_prefix(2);
    if (std::from_chars(entity.data(), entity.data() + entity.size(), order.fracture.entity, 16)
                .ptr != entity.data() + entity.size() ||
        order.fracture.entity == 0)
      Usage(std::format("--fracture: {} is not an entity id in hex", entity));
    order.fracture.component = CName(part.substr(colon + 1));
    spec = spec.substr(place + 1);
  }
  order.fracture.at = Point("--fracture", spec);
  return order;
}

// --breach ENTITY[:CLASS][@SECONDS] | npc=N[@SECONDS]
Options::BreachOrder BreachOrderOf(std::string_view spec) {
  Options::BreachOrder order;
  if (const auto at = spec.find('@'); at != std::string_view::npos) {
    order.atSeconds = Number<int>(spec.substr(at + 1));
    spec = spec.substr(0, at);
  }
  if (spec.starts_with("npc=")) {
    order.npc = Number<std::uint32_t>(spec.substr(4));
    if (order.npc == 0)
      Usage("--breach: npc=N names an NPC by its session id");
    return order;
  }
  const auto colon = spec.find(':');
  auto entity = spec.substr(0, colon);
  if (entity.starts_with("0x"))
    entity.remove_prefix(2);
  if (std::from_chars(entity.data(), entity.data() + entity.size(), order.entity, 16).ptr !=
          entity.data() + entity.size() ||
      order.entity == 0)
    Usage(std::format("--breach: {} is not an entity id in hex", entity));
  order.cls = CName(colon == std::string_view::npos ? std::string_view("AccessPointControllerPS")
                                                    : spec.substr(colon + 1));
  return order;
}

// --programs A,B: records by name (MinigameAction. optional) or in hex.
std::vector<std::uint64_t> ProgramsOf(std::string_view list) {
  std::vector<std::uint64_t> programs;
  std::size_t start = 0;
  while (start <= list.size()) {
    const auto comma = list.find(',', start);
    auto name = list.substr(start, comma == std::string_view::npos ? comma : comma - start);
    if (!name.empty()) {
      std::uint64_t id{};
      if (name.starts_with("0x")) {
        if (std::from_chars(name.data() + 2, name.data() + name.size(), id, 16).ptr !=
            name.data() + name.size())
          Usage(std::format("--programs: {} is not a record id", name));
      } else {
        id = TweakDBID(name.find('.') == std::string_view::npos
                           ? std::format("MinigameAction.{}", name)
                           : std::string(name));
      }
      programs.push_back(id);
    }
    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  if (programs.size() > tandem::proto::kMaxBreachPrograms)
    Usage(std::format("--programs: at most {} daemons", tandem::proto::kMaxBreachPrograms));
  return programs;
}

// A CName by name, or by its hash as the logs print it (0x...).
std::uint64_t NameOrHash(std::string_view flag, std::string_view text) {
  if (text.starts_with("0x")) {
    std::uint64_t hash{};
    if (std::from_chars(text.data() + 2, text.data() + text.size(), hash, 16).ptr !=
        text.data() + text.size())
      Usage(std::format("{}: {} is not a hash", flag, text));
    return hash;
  }
  return CName(text);
}

// --interact ENTITY:CHOICE[:DETAIL][@SECONDS]
Options::InteractOrder InteractOrderOf(std::string_view spec) {
  Options::InteractOrder order;
  if (const auto at = spec.find('@'); at != std::string_view::npos) {
    order.atSeconds = Number<int>(spec.substr(at + 1));
    spec = spec.substr(0, at);
  }
  const auto first = spec.find(':');
  if (first == std::string_view::npos)
    Usage("--interact is ENTITY:CHOICE[:DETAIL][@SECONDS]");
  auto entity = spec.substr(0, first);
  if (entity.starts_with("0x"))
    entity.remove_prefix(2);
  if (std::from_chars(entity.data(), entity.data() + entity.size(), order.entity, 16).ptr !=
          entity.data() + entity.size() ||
      order.entity == 0)
    Usage(std::format("--interact: {} is not an entity id in hex", entity));
  auto rest = spec.substr(first + 1);
  const auto second = rest.find(':');
  order.choice = NameOrHash("--interact", rest.substr(0, second));
  if (second != std::string_view::npos)
    order.detail = NameOrHash("--interact", rest.substr(second + 1));
  if (order.choice == 0)
    Usage("--interact: no choice");
  return order;
}

int InteractVerdictOf(std::string_view word) {
  namespace proto = tandem::proto;
  if (word == "performed")
    return proto::kInteractPerformed;
  if (word == "host-only")
    return proto::kInteractHostOnly;
  if (word == "local")
    return proto::kInteractLocal;
  if (word == "missing")
    return proto::kInteractMissing;
  if (word == "busy")
    return proto::kInteractBusy;
  if (word == "too-far")
    return proto::kInteractTooFar;
  if (word == "none")
    return -1;
  Usage(std::format("--interact-answer: {} is not performed, host-only, local, missing, busy, "
                    "too-far or none",
                    word));
}

Vec3 Point(std::string_view flag, std::string_view value) {
  float xyz[3]{};
  std::size_t start = 0;
  for (int axis = 0; axis < 3; ++axis) {
    const auto comma = value.find(',', start);
    if ((axis < 2) == (comma == std::string_view::npos))
      Usage(std::format("{} is X,Y,Z", flag));
    xyz[axis] = Number<float>(value.substr(start, comma - start));
    start = comma + 1;
  }
  return {xyz[0], xyz[1], xyz[2]};
}

float Percent(std::string_view flag, std::string_view value) {
  const auto percent = Number<float>(value);
  if (percent < 0.f || percent > 100.f)
    Usage(std::format("{} is a percentage, 0 to 100", flag));
  return percent / 100.f;
}

std::optional<tandem::proto::ActivityKind> ActivityNamed(const std::string &name) {
  using tandem::proto::ActivityKind;
  static const std::pair<const char *, ActivityKind> kinds[] = {
      {"none", ActivityKind::None},         {"workspot", ActivityKind::Workspot},
      {"sleep", ActivityKind::Sleep},       {"screen", ActivityKind::Screen},
      {"minigame", ActivityKind::Minigame}, {"shop", ActivityKind::Shop},
      {"menu", ActivityKind::Menu},         {"photo", ActivityKind::Photo},
      {"contest", ActivityKind::Contest}};
  for (const auto &[n, kind] : kinds)
    if (name == n)
      return kind;
  return std::nullopt;
}

const char *ActivityName(tandem::proto::ActivityKind kind) {
  static const char *names[] = {"nothing", "a workspot", "a bed",      "a screen", "a minigame",
                                "a shop",  "a menu",     "photo mode", "a contest"};
  const auto i = static_cast<std::size_t>(kind);
  return i < std::size(names) ? names[i] : "?";
}

std::optional<tandem::proto::SkipReason> SkipReasonNamed(std::string_view name) {
  using tandem::proto::SkipReason;
  if (name == "wait")
    return SkipReason::Wait;
  if (name == "sleep")
    return SkipReason::Sleep;
  if (name == "quest")
    return SkipReason::Quest;
  return std::nullopt;
}

const char *SkipOutcomeName(tandem::proto::SkipOutcome outcome) {
  static const char *names[] = {"applied", "declined", "lapsed", "withdrawn", "refused"};
  const auto i = static_cast<std::size_t>(outcome);
  return i < std::size(names) ? names[i] : "?";
}

Options Parse(int argc, char **argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "--help" || args[0] == "-h")
    Usage();

  Options options;
  std::size_t i = 1;
  if (args[0] == "host")
    options.host = true;
  else if (args[0] == "join") {
    if (args.size() < 2)
      Usage("join needs an address or --code");
    if (args[1].starts_with("--")) {
      options.address.clear(); // by --code, through the relay only
    } else {
      auto target = args[1];
      if (auto colon = target.rfind(':'); colon != std::string_view::npos) {
        options.port = Number<std::uint16_t>(target.substr(colon + 1));
        target = target.substr(0, colon);
      }
      options.address = std::string(target);
      i = 2;
    }
  } else
    Usage(std::format("unknown command {}", args[0]));

  for (; i < args.size(); ++i) {
    const auto flag = args[i];
    if (flag == "--check") { // parse only (tests/peer_flags.sh)
      options.check = true;
      continue;
    }
    if (flag == "--scene") {
      options.scene = true;
      continue;
    }
    if (flag == "--near-npc") {
      options.nearNpc = true;
      continue;
    }
    if (flag == "--vendor-crowd") {
      options.vendorPlaced = false;
      continue;
    }
    if (flag == "--trade-offer") {
      options.tradeOffer = true;
      continue;
    }
    if (flag == "--trade-accept") {
      options.tradeAccept = true;
      continue;
    }
    if (flag == "--breach-failed") {
      options.breachFailed = true;
      continue;
    }
    if (flag == "--no-fast-travel") {
      options.noFastTravel = true;
      continue;
    }
    if (flag == "--traffic-report") {
      options.traffic = true;
      continue;
    }
    if (flag == "--legacy-wire") {
      options.legacyWire = true;
      continue;
    }
    if (flag == "--ride-along") {
      options.rideAlong = true;
      continue;
    }
    if (flag == "--region-driver") {
      options.regionDriver = true;
      continue;
    }
    if (flag == "--region-chase") {
      options.regionChase = true;
      continue;
    }
    if (flag == "--play-loop") {
      options.playLoop = true;
      continue;
    }
    // (No value: they used to take the next argument as theirs.)
    if (flag == "--throw-bounce") {
      options.throwBounce = true;
      continue;
    }
    if (flag == "--footsteps") {
      options.footsteps = true;
      continue;
    }
    if (flag == "--shot-smart") {
      options.shotSmart = true;
      continue;
    }
    if (flag == "--ricochet") {
      options.ricochet = true;
      continue;
    }
    if (flag == "--steal-driven") {
      options.stealDriven = true;
      continue;
    }
    if (i + 1 >= args.size())
      Usage(std::format("{} needs a value", flag));
    const auto value = args[++i];
    if (flag == "--port")
      options.port = Number<std::uint16_t>(value);
    else if (flag == "--name")
      options.name = std::string(value);
    else if (flag == "--body") {
      if (value == "female")
        options.body = tandem::proto::Body::Female;
      else if (value == "male")
        options.body = tandem::proto::Body::Male;
      else
        Usage(std::format("unknown body {}", value));
    } else if (flag == "--delay")
      options.delay = std::chrono::milliseconds(Number<int>(value));
    else if (flag == "--time") {
      const auto colon = value.find(':');
      if (colon == std::string_view::npos)
        Usage("--time is HH:MM");
      options.timeOfDayMinutes =
          Number<int>(value.substr(0, colon)) * 60 + Number<int>(value.substr(colon + 1));
    } else if (flag == "--weather")
      options.weather = std::string(value);
    else if (flag == "--pause-at")
      options.pauseAtSeconds = Number<int>(value);
    else if (flag == "--pause-for")
      options.pauseForSeconds = Number<int>(value);
    else if (flag == "--vanish-at")
      options.vanishAtSeconds = Number<int>(value);
    else if (flag == "--drop" || flag == "--dup") {
      const auto p = Number<float>(value);
      if (!(p >= 0.f && p <= 1.f))
        Usage(std::format("{} is a probability (0..1), not {}", flag, value));
      (flag == "--drop" ? options.drop : options.dup) = p;
    } else if (flag == "--fault-seed")
      options.faultSeed = Number<std::uint32_t>(value);
    else if (flag == "--hack-npcs")
      options.hackNpcsSeconds = Number<int>(value);
    else if (flag == "--hack-action")
      options.hackAction = std::string(value);
    else if (flag == "--hack-effect")
      options.hackEffect = std::string(value);
    else if (flag == "--hack-player")
      options.hackPlayerSeconds = Number<int>(value);
    else if (flag == "--hack-interrupt")
      options.hackInterruptSeconds = Number<double>(value);
    else if (flag == "--npc-hack")
      options.npcHack = std::string(value);
    else if (flag == "--hack-car")
      options.hackCarSeconds = Number<int>(value);
    else if (flag == "--sandevistan-at")
      options.sandevistanAtSeconds = Number<int>(value);
    else if (flag == "--sandevistan-for")
      options.sandevistanForSeconds = Number<int>(value);
    else if (flag == "--sandevistan-scale")
      options.sandevistanScale = Number<float>(value);
    else if (flag == "--device")
      options.devices.push_back(DeviceOrderOf(value));
    else if (flag == "--device-answer") {
      tandem::devices::What probe;
      if (!DeviceWords(value, probe))
        Usage(std::format("--device-answer: unknown state in {}", value));
      options.deviceAnswer = std::string(value);
    } else if (flag == "--device-after")
      options.deviceAfterSeconds = Number<int>(value);
    else if (flag == "--turret-seen") {
      std::string_view entity = value;
      if (const auto at = entity.find('@'); at != std::string_view::npos) {
        options.turretSeenAtSeconds = Number<int>(entity.substr(at + 1));
        entity = entity.substr(0, at);
      }
      if (entity.starts_with("0x"))
        entity.remove_prefix(2);
      if (std::from_chars(entity.data(), entity.data() + entity.size(), options.turretSeen, 16)
                  .ptr != entity.data() + entity.size() ||
          options.turretSeen == 0)
        Usage(std::format("--turret-seen: {} is not an entity id in hex", entity));
    } else if (flag == "--turret-hands") {
      std::string_view entity = value;
      if (const auto at = entity.find('@'); at != std::string_view::npos) {
        options.turretHandsAtSeconds = Number<int>(entity.substr(at + 1));
        entity = entity.substr(0, at);
      }
      if (const auto colon = entity.find(':'); colon != std::string_view::npos) {
        options.turretHandsAt = Point("--turret-hands", entity.substr(colon + 1));
        entity = entity.substr(0, colon);
      }
      if (entity.starts_with("0x"))
        entity.remove_prefix(2);
      if (std::from_chars(entity.data(), entity.data() + entity.size(), options.turretHands, 16)
                  .ptr != entity.data() + entity.size() ||
          options.turretHands == 0)
        Usage(std::format("--turret-hands: {} is not an entity id in hex", entity));
    } else if (flag == "--turret-hands-for") {
      options.turretHandsForSeconds = Number<int>(value);
      if (options.turretHandsForSeconds < 1)
        Usage("--turret-hands-for: at least 1 second");
    } else if (flag == "--turret-shots")
      options.turretShots = Number<int>(value);
    else if (flag == "--fracture")
      options.fractures.push_back(FractureOrderOf(value));
    else if (flag == "--breach")
      options.breach = BreachOrderOf(value);
    else if (flag == "--programs")
      options.breachPrograms = ProgramsOf(value);
    else if (flag == "--interact")
      options.interacts.push_back(InteractOrderOf(value));
    else if (flag == "--interact-answer")
      options.interactAnswer = InteractVerdictOf(value);
    else if (flag == "--scan-at") {
      options.sandevistanAtSeconds = Number<int>(value);
      options.timeAbility = tandem::proto::kTimeScanning;
    } else if (flag == "--level") {
      options.level = Number<int>(value);
      if (options.level < 1 || options.level > tandem::proto::kMaxLevel)
        Usage("--level is 1 to 100");
    } else if (flag == "--npc-level")
      options.npcLevel = Number<float>(value);
    else if (flag == "--lend") {
      constexpr std::string_view names[] = {"crew", "owner", "never"};
      const auto *named = std::ranges::find(names, value);
      if (named == std::end(names))
        Usage("--lend is crew, owner or never");
      options.lend = static_cast<int>(named - std::begin(names));
    } else if (flag == "--unlock-vehicle")
      options.unlockVehicle = std::string(value);
    else if (flag == "--vendor") {
      // REC@X,Y,Z#ENTITY
      const auto at = value.find('@');
      const auto hash = value.find('#');
      if (at == std::string_view::npos || hash == std::string_view::npos || hash < at || at == 0)
        Usage("--vendor is REC@X,Y,Z#ENTITY");
      options.vendorRecord = std::string(value.substr(0, at));
      options.vendorAt = Point(flag, value.substr(at + 1, hash - at - 1));
      auto entity = value.substr(hash + 1);
      if (entity.starts_with("0x"))
        entity.remove_prefix(2);
      if (std::from_chars(entity.data(), entity.data() + entity.size(), options.vendorEntity, 16)
                  .ptr != entity.data() + entity.size() ||
          options.vendorEntity == 0)
        Usage(std::format("--vendor: {} is not an entity id in hex", entity));
    } else if (flag == "--vendor-dies")
      options.vendorDiesSeconds = Number<int>(value);
    else if (flag == "--trade-give")
      options.tradeGive = std::string(value);
    else if (flag == "--trade-money")
      options.tradeMoney = Number<int>(value);
    else if (flag == "--trade-asks")
      options.tradeAsks = Number<int>(value);
    else if (flag == "--drop-item")
      options.dropItem = std::string(value);
    else if (flag == "--loot-quest")
      options.lootQuest = std::string(value);
    else if (flag == "--take-items")
      options.takeItemsSeconds = Number<int>(value);
    else if (flag == "--remove-corpses")
      options.removeCorpsesSeconds = Number<int>(value);
    else if (flag == "--hit-npcs")
      options.hitNpcsSeconds = Number<int>(value);
    else if (flag == "--grab-npcs")
      options.grabNpcsSeconds = Number<int>(value);
    else if (flag == "--carry-npcs")
      options.carryNpcsSeconds = Number<int>(value);
    else if (flag == "--hold-npcs")
      options.holdNpcsSeconds = Number<int>(value);
    else if (flag == "--hit-you")
      options.hitYouSeconds = Number<int>(value);
    else if (flag == "--hit-you-melee")
      options.hitYouMelee = value == "on";
    else if (flag == "--shoot-you")
      options.shootYouSeconds = Number<int>(value);
    else if (flag == "--shot-weapon")
      options.shotWeapon = std::string(value);
    else if (flag == "--strike") {
      constexpr std::string_view kinds[] = {"plain", "nonlethal", "cap",    "poise",
                                            "head",  "blade",     "offhand"};
      if (std::ranges::find(kinds, value) == std::end(kinds))
        Usage(std::format("unknown strike {}", value));
      options.strike = std::string(value);
    } else if (flag == "--difficulty") {
      constexpr std::string_view levels[] = {"easy", "hard", "veryhard", "story"};
      if (const auto *named = std::ranges::find(levels, value); named != std::end(levels))
        options.difficulty = static_cast<int>(named - std::begin(levels));
      else if (const auto level = Number<int>(value); level >= 0 && level <= 3)
        options.difficulty = level;
      else
        Usage(std::format("--difficulty: no level {}", value));
    } else if (flag == "--area-hit")
      options.areaHitSeconds = Number<int>(value);
    else if (flag == "--friendly-fire") {
      if (value == "on")
        options.friendlyFire = true;
      else if (value == "off")
        options.friendlyFire = false;
      else
        Usage(std::format("--friendly-fire: on or off, not {}", value));
    } else if (flag == "--enemy-scaling") {
      if (value == "on")
        options.enemyScaling = true;
      else if (value == "off")
        options.enemyScaling = false;
      else
        Usage(std::format("--enemy-scaling: on or off, not {}", value));
    } else if (flag == "--enemy-scaling-health") {
      options.enemyScalingHealth = Number<float>(value);
      if (!(options.enemyScalingHealth >= 0.f) ||
          options.enemyScalingHealth > tandem::scaling::kMaxHealthPerPlayer)
        Usage("--enemy-scaling-health is 0 to 2.5");
    } else if (flag == "--throw")
      options.throwSeconds = Number<int>(value);
    else if (flag == "--throw-item")
      options.throwItem = std::string(value);
    else if (flag == "--smart")
      options.smartSeconds = Number<int>(value);
    else if (flag == "--defend")
      options.defendSeconds = Number<int>(value);
    else if (flag == "--npc")
      options.npcRecord = std::string(value);
    else if (flag == "--held-record" || flag == "--vehicle-record" || flag == "--look-piece" ||
             flag == "--npc-record") {
      // NAME[:TYPE[:TAG or SEATS]]: a record and its hint (the game's enum
      // names: Wea_Handgun, Car, Clo_Legs, Human; Weapon, 4, Female).
      std::vector<std::string> parts;
      for (const auto part : std::views::split(value, ':'))
        parts.emplace_back(std::string_view(part.begin(), part.end()));
      tandem::proto::RecordHint hint;
      if (parts.size() > 1)
        hint.type = CName(parts[1]);
      if (parts.size() > 2 && flag == "--vehicle-record")
        hint.size = static_cast<std::uint8_t>(Number<int>(parts[2]));
      else if (parts.size() > 2)
        hint.tag = CName(parts[2]);
      if (flag == "--held-record") {
        options.holdRecord = parts[0];
        options.holdHint = hint;
      } else if (flag == "--vehicle-record") {
        options.vehicle = parts[0];
        options.vehicleHint = hint;
      } else if (flag == "--look-piece") {
        options.lookPieces.emplace_back(parts[0], hint);
      } else {
        options.npcRecord = parts[0];
        options.npcHint = hint;
      }
    } else if (flag == "--unknown") {
      for (const auto part : std::views::split(value, ','))
        options.unknown.insert(TweakDBID(std::string_view(part.begin(), part.end())));
    } else if (flag == "--region-far")
      options.regionFar = Number<float>(value);
    else if (flag == "--region-at")
      options.regionAt = Point(flag, value);
    else if (flag == "--region-npcs")
      options.regionNpcs = std::clamp(Number<int>(value), 0, 32);
    else if (flag == "--region-record")
      options.regionRecord = std::string(value);
    else if (flag == "--region-walk-at")
      options.regionWalkAtSeconds = Number<int>(value);
    else if (flag == "--region-leave-at")
      options.regionLeaveAtSeconds = Number<int>(value);
    else if (flag == "--region-leave-linked")
      options.regionLeaveLinkedSeconds = Number<int>(value);
    else if (flag == "--region-speed")
      options.regionSpeed = std::max(0.1f, Number<float>(value));
    else if (flag == "--region-cars")
      options.regionCars = std::clamp(Number<int>(value), 0, 16);
    else if (flag == "--region-placed") {
      auto entity = value;
      if (entity.starts_with("0x"))
        entity.remove_prefix(2);
      if (std::from_chars(entity.data(), entity.data() + entity.size(), options.regionPlaced, 16)
                  .ptr != entity.data() + entity.size() ||
          options.regionPlaced == 0)
        Usage(std::format("--region-placed: {} is not an entity id in hex", entity));
    } else if (flag == "--region-kill-at")
      options.regionKillAtSeconds = Number<int>(value);
    else if (flag == "--region-immortal") {
      if (value != "said" && value != "silent")
        Usage(std::format("--region-immortal: said or silent, not {}", value));
      options.regionImmortal = std::string(value);
    } else if (flag == "--detect-at")
      options.detectAtSeconds = Number<int>(value);
    else if (flag == "--detect-for")
      options.detectForSeconds = Number<int>(value);
    else if (flag == "--camo")
      options.camoAtSeconds = Number<int>(value);
    else if (flag == "--camo-record")
      options.camoRecord = value;
    else if (flag == "--status")
      options.statusVisuals = value;
    else if (flag == "--shoot")
      options.shootSeconds = std::max(1, Number<int>(value));
    else if (flag == "--charge")
      options.chargeSeconds = std::max(4, Number<int>(value));
    else if (flag == "--hurt")
      options.hurtSeconds = std::max(1, Number<int>(value));
    else if (flag == "--hurt-hit") {
      if (value != "world" && value != "npc" && value != "you")
        Usage(std::format("--hurt-hit: world, npc or you, not {}", value));
      options.hurtHit = std::string(value);
    } else if (flag == "--melee")
      options.meleeSeconds = std::max(1, Number<int>(value));
    else if (flag == "--menu-at")
      options.menuAtSeconds = Number<int>(value);
    else if (flag == "--react-npcs")
      options.reactNpcsSeconds = std::max(1, Number<int>(value));
    else if (flag == "--voice-npcs")
      options.voiceNpcsSeconds = std::max(1, Number<int>(value));
    else if (flag == "--corpse-npcs")
      options.corpseNpcsSeconds = std::max(2, Number<int>(value));
    else if (flag == "--hold")
      options.holdRecord = value;
    else if (flag == "--hold-left")
      options.holdLeftRecord = value;
    else if (flag == "--holster-at")
      options.holsterAtSeconds = Number<int>(value);
    else if (flag == "--drive-seconds")
      options.driveSeconds = Number<int>(value);
    else if (flag == "--drive-radius")
      options.driveRadius = std::clamp(Number<float>(value), 3.f, 400.f);
    else if (flag == "--radio") {
      if (value == "off")
        options.radioStation = -1;
      else if (const auto *named = std::ranges::find(kStations, value);
               named != std::end(kStations))
        options.radioStation = static_cast<int>(named - std::begin(kStations));
      else if (const auto station = Number<int>(value); station >= 0 && station < 14)
        options.radioStation = station;
      else
        Usage(std::format("--radio: no station {}", value));
    } else if (flag == "--paint") {
      namespace proto = tandem::proto;
      proto::CarPaint paint{.flags = proto::kPaintPrimary};
      auto text = value;
      if (const auto at = text.find('@'); at != std::string_view::npos) {
        paint.flags = static_cast<std::uint8_t>(paint.flags | proto::kPaintLights);
        paint.lightsHue = Number<float>(text.substr(at + 1));
        text = text.substr(0, at);
      }
      const auto colour = [](std::string_view hex, std::uint8_t &r, std::uint8_t &g,
                             std::uint8_t &b) {
        unsigned rgb = 0;
        if (hex.size() != 6 || std::from_chars(hex.data(), hex.data() + hex.size(), rgb, 16).ptr !=
                                   hex.data() + hex.size())
          Usage("--paint is RRGGBB[,RRGGBB][@HUE]: hex colours, the lights' hue in degrees");
        r = static_cast<std::uint8_t>(rgb >> 16 & 0xFF);
        g = static_cast<std::uint8_t>(rgb >> 8 & 0xFF);
        b = static_cast<std::uint8_t>(rgb & 0xFF);
      };
      const auto comma = text.find(',');
      colour(text.substr(0, comma), paint.primaryR, paint.primaryG, paint.primaryB);
      if (comma != std::string_view::npos) {
        paint.flags = static_cast<std::uint8_t>(paint.flags | proto::kPaintSecondary);
        colour(text.substr(comma + 1), paint.secondaryR, paint.secondaryG, paint.secondaryB);
      }
      if (!(paint.lightsHue >= 0.f && paint.lightsHue <= 360.f))
        Usage("--paint: the lights' hue is 0 to 360 degrees");
      options.paint = paint;
    } else if (flag == "--paint-off-at") {
      options.paintOffSeconds = Number<int>(value);
    } else if (flag == "--summon") {
      options.summonSeconds = Number<int>(value);
    } else if (flag == "--summon-board") {
      options.summonBoardSeconds = Number<int>(value);
    } else if (flag == "--summon-again") {
      options.summonAgainSeconds = Number<int>(value);
    } else if (flag == "--look-keys") {
      options.lookKeys = value;
    } else if (flag == "--look") {
      if (value == "echo")
        options.echoLook = true;
      else if (value == "random")
        options.echoLook = false;
      else
        Usage(std::format("unknown look {}", value));
    } else if (flag == "--seed")
      options.lookSeed = Number<unsigned>(value);
    else if (flag == "--vehicle")
      options.vehicle = std::string(value);
    else if (flag == "--pose")
      options.poseFile = std::string(value);
    else if (flag == "--rate")
      options.rateHz = std::max(1, Number<int>(value));
    else if (flag == "--mode") {
      if (value == "mirror")
        options.mode = Mode::Mirror;
      else if (value == "ghost")
        options.mode = Mode::Ghost;
      else if (value == "front")
        options.mode = Mode::Front;
      else if (value == "idle")
        options.mode = Mode::Idle;
      else if (value == "drive")
        options.mode = Mode::Drive;
      else if (value == "course")
        options.mode = Mode::Course;
      else if (value == "path")
        options.mode = Mode::Path;
      else if (value == "metro")
        options.mode = Mode::Metro;
      else if (value == "watch")
        options.mode = Mode::Watch;
      else if (value == "region")
        options.mode = Mode::Region;
      else
        Usage(std::format("unknown mode {}", value));
    } else if (flag == "--metro-at")
      options.metroAtSeconds = std::max(0, Number<int>(value));
    else if (flag == "--metro-for")
      options.metroForSeconds = std::max(8, Number<int>(value));
    else if (flag == "--seat")
      options.seat = std::string(value);
    else if (flag == "--driver-npc")
      options.driverNpc = std::string(value);
    else if (flag == "--offset") {
      options.offset = Point(flag, value);
      options.offsetGiven = true;
    } else if (flag == "--play")
      options.playFile = std::string(value);
    else if (flag == "--record-session")
      options.recordSessionFile = std::string(value);
    else if (flag == "--play-session")
      options.playSessionFile = std::string(value);
    else if (flag == "--play-offset")
      options.playOffset = Point(flag, value);
    else if (flag == "--save-play")
      options.savePlayFile = std::string(value);
    else if (flag == "--travel-at")
      options.travelAtSeconds = Number<int>(value);
    else if (flag == "--travel-to")
      options.travelTo = Point(flag, value);
    else if (flag == "--travel-point")
      options.travelPoint = std::string(value);
    else if (flag == "--offer-regroup")
      options.offerRegroupSeconds = Number<int>(value);
    else if (flag == "--link") {
      if (value == "auto")
        options.linkConsent = Options::LinkAnswer::Auto;
      else if (value == "ask")
        options.linkConsent = Options::LinkAnswer::Ask;
      else if (value == "off")
        options.linkConsent = Options::LinkAnswer::Off;
      else
        Usage(std::format("--link: auto, ask or off, not {}", value));
    } else if (flag == "--link-at")
      options.linkAtSeconds = Number<int>(value);
    else if (flag == "--link-answer-after")
      options.linkAnswerSeconds = Number<int>(value);
    else if (flag == "--link-outcome") {
      if (value == "answer")
        options.callOutcome = Options::CallOutcome::Answer;
      else if (value == "decline")
        options.callOutcome = Options::CallOutcome::Decline;
      else if (value == "busy")
        options.callOutcome = Options::CallOutcome::Busy;
      else if (value == "ignore")
        options.callOutcome = Options::CallOutcome::Ignore;
      else
        Usage(std::format("--link-outcome: answer, decline, busy or ignore, not {}", value));
    } else if (flag == "--unlink-at")
      options.unlinkAtSeconds = Number<int>(value);
    else if (flag == "--ping")
      options.pingSeconds = Number<int>(value);
    else if (flag == "--tag")
      options.tagSeconds = Number<int>(value);
    else if (flag == "--reveal-device") {
      std::string_view entity = value;
      if (const auto at = entity.find('@'); at != std::string_view::npos) {
        options.revealDeviceAtSeconds = Number<int>(entity.substr(at + 1));
        entity = entity.substr(0, at);
      }
      if (entity.starts_with("0x"))
        entity.remove_prefix(2);
      if (std::from_chars(entity.data(), entity.data() + entity.size(), options.revealDevice, 16)
                  .ptr != entity.data() + entity.size() ||
          options.revealDevice == 0)
        Usage(std::format("--reveal-device: {} is not an entity id in hex", entity));
    } else if (flag == "--reveal")
      options.revealSeconds = Number<int>(value);
    else if (flag == "--waypoint")
      options.waypoint = Point(flag, value);
    else if (flag == "--waypoint-kind") {
      using tandem::proto::WaypointKind;
      if (value == "pin")
        options.waypointKind = WaypointKind::Pin;
      else if (value == "point")
        options.waypointKind = WaypointKind::Point;
      else if (value == "objective")
        options.waypointKind = WaypointKind::Objective;
      else
        Usage(std::format("--waypoint-kind: {} is not pin, point or objective", value));
    } else if (flag == "--waypoint-clear")
      options.waypointClearSeconds = Number<int>(value);
    else if (flag == "--hack-npc") {
      if (value != "nearest")
        Number<std::uint32_t>(value);
      options.hackNpc = std::string(value);
    } else if (flag == "--mark-target") {
      if (value != "nearest")
        Number<std::uint32_t>(value); // a number, or the usage
      options.markTarget = std::string(value);
    } else if (flag == "--regroup-answer") {
      if (value == "yes")
        options.regroupAnswer = Options::Answer::Yes;
      else if (value == "no")
        options.regroupAnswer = Options::Answer::No;
      else if (value == "ignore")
        options.regroupAnswer = Options::Answer::Ignore;
      else
        Usage(std::format("--regroup-answer: yes, no or ignore, not {}", value));
    } else if (flag == "--ask-skip")
      options.askSkipSeconds = Number<int>(value);
    else if (flag == "--skip-hours")
      options.skipHours = Number<int>(value);
    else if (flag == "--skip-reason") {
      if (const auto reason = SkipReasonNamed(value))
        options.skipReason = *reason;
      else
        Usage(std::format("--skip-reason: wait, sleep or quest, not {}", value));
    } else if (flag == "--skip-answer") {
      if (value == "yes")
        options.skipAnswer = Options::Answer::Yes;
      else if (value == "no")
        options.skipAnswer = Options::Answer::No;
      else if (value == "ignore")
        options.skipAnswer = Options::Answer::Ignore;
      else if (value == "sleep")
        options.skipAnswer = Options::Answer::Sleep;
      else
        Usage(std::format("--skip-answer: yes, no, ignore or sleep, not {}", value));
    } else if (flag == "--activity") {
      if (!ActivityNamed(std::string(value)))
        Usage(std::format("--activity: workspot, sleep, screen, minigame, shop, menu, photo or "
                          "contest, not {}",
                          value));
      options.activity = std::string(value);
    } else if (flag == "--activity-at")
      options.activityAtSeconds = Number<int>(value);
    else if (flag == "--activity-for")
      options.activityForSeconds = Number<int>(value);
    else if (flag == "--contest") {
      if (value == "duel")
        options.contest = tandem::proto::ContestSort::Duel;
      else if (value == "race")
        options.contest = tandem::proto::ContestSort::Race;
      else if (value == "range")
        options.contest = tandem::proto::ContestSort::Range;
      else
        Usage(std::format("--contest: duel, race or range, not {}", value));
      options.activity = "contest";
    } else if (flag == "--latency")
      options.link.latencyMs = Number<std::uint32_t>(value);
    else if (flag == "--jitter")
      options.link.jitterMs = Number<std::uint32_t>(value);
    else if (flag == "--loss")
      options.link.loss = Percent(flag, value);
    else if (flag == "--reorder")
      options.link.reorder = Percent(flag, value);
    else if (flag == "--burst")
      options.link.burst = Number<float>(value);
    else if (flag == "--record") {
      options.recordFile = value;
    } else if (flag == "--replay") {
      options.replayFile = value;
    } else if (flag == "--relay") {
      if (!options.relay)
        options.relay.emplace();
      if (!tandem::net::ParseRelayAddress(value, options.relay->address, options.relay->port))
        Usage(std::format("--relay is HOST[:PORT], not {}", value));
    } else if (flag == "--relay-token") {
      const auto token = tandem::net::ParseRelayToken(value);
      if (!token)
        Usage("--relay-token is 64 hex digits (tandem-relay token)");
      if (!options.relay)
        options.relay.emplace();
      options.relay->token = *token;
    } else if (flag == "--code") {
      const auto code = tandem::net::NormalizeJoinCode(value);
      if (!code)
        Usage(std::format("not a join code: {}", value));
      options.code = *code;
    } else if (flag == "--story") {
      options.storyFile = value;
    } else if (flag == "--restrict") {
      options.restrictions = value;
    } else if (flag == "--restrict-at") {
      options.restrictAt = Number<double>(value);
    } else if (flag == "--restrict-for") {
      options.restrictFor = Number<double>(value);
    } else if (flag == "--prefabs") {
      options.prefabs = value;
    } else if (flag == "--gates") {
      options.gates = Number<int>(value);
    } else if (flag == "--go-down") {
      options.goDownSeconds = Number<int>(value);
    } else if (flag == "--go-out") {
      options.goOutSeconds = Number<int>(value);
    } else if (flag == "--revive") {
      options.reviveSeconds = Number<int>(value);
    } else if (flag == "--answer") {
      options.answerSeconds = Number<int>(value);
    } else if (flag == "--reply") {
      options.replyEntry = Number<std::uint32_t>(value);
    } else if (flag == "--suggest") {
      options.suggestIndex = Number<int>(value);
    } else if (flag == "--hover") {
      options.hoverIndex = Number<int>(value);
    } else if (flag == "--anims") {
      options.animsFile = value;
    } else if (flag == "--path") {
      options.pathFile = value;
    } else if (flag == "--save-path") {
      options.savePathFile = value;
    } else if (flag == "--traffic") {
      options.trafficCars = Number<int>(value);
      if (options.trafficCars < 0 || options.trafficCars > 64)
        Usage("--traffic is 0 to 64");
    } else if (flag == "--traffic-at") {
      options.trafficAtSeconds = Number<int>(value);
    } else if (flag == "--traffic-vehicle") {
      options.trafficVehicle = std::string(value);
    } else if (flag == "--wreck-at") {
      options.wreckAtSeconds = Number<int>(value);
    } else if (flag == "--ram-traffic") {
      options.ramTrafficSeconds = Number<int>(value);
    } else if (flag == "--hack-traffic") {
      options.hackTrafficAtSeconds = Number<int>(value);
    } else if (flag == "--steal-at") {
      options.stealAtSeconds = Number<int>(value);
    } else if (flag == "--heat") {
      options.heat = Number<int>(value);
      if (options.heat < 0 || options.heat > 5)
        Usage("--heat is 0 to 5");
    } else if (flag == "--heat-at") {
      options.heatAtSeconds = Number<int>(value);
    } else if (flag == "--heat-for") {
      options.heatForSeconds = Number<int>(value);
    } else if (flag == "--crime-at") {
      options.crimeAtSeconds = Number<int>(value);
    } else if (flag == "--car-damage") {
      options.carDamageSeconds = Number<int>(value);
    } else if (flag == "--exit-at") {
      options.exitAtSeconds = Number<int>(value);
    } else if (flag == "--exit-kind") {
      using tandem::proto::ExitKind;
      const std::map<std::string_view, ExitKind> kinds{{"bail", ExitKind::Bail},
                                                       {"thrown", ExitKind::Thrown},
                                                       {"knocked", ExitKind::Knocked},
                                                       {"dead", ExitKind::Dead},
                                                       {"exploded", ExitKind::Exploded}};
      const auto it = kinds.find(value);
      if (it == kinds.end())
        Usage("--exit-kind is bail, thrown, knocked, dead or exploded");
      options.exitKind = it->second;
    } else if (flag == "--bump") {
      options.bumpSeconds = Number<int>(value);
    } else if (flag == "--shoot-cars") {
      options.shootCarsSeconds = Number<int>(value);
    } else if (flag == "--gunner") {
      options.gunnerSeconds = Number<int>(value);
    } else if (flag == "--gunner-seat") {
      options.gunnerSeat = std::string(value);
    } else if (flag == "--ram-npcs") {
      options.ramNpcsSeconds = Number<int>(value);
    } else if (flag == "--chase") {
      options.chaseSeconds = Number<int>(value);
    } else
      Usage(std::format("unknown option {}", flag));
  }
  if (options.mode == Mode::Path && options.pathFile.empty() && !options.check)
    Usage("--mode path needs --path FILE");
  if (!options.host && !options.check) {
    if (options.address.empty() && options.code.empty())
      Usage("join needs an address or --code");
    if (!options.code.empty() && (!options.relay || options.relay->address.empty()))
      Usage("--code needs --relay");
  }
  if (options.relay && options.relay->address.empty() && !options.check)
    Usage("--relay-token needs --relay");
  if (options.relay)
    options.relay->code = options.code;
  return options;
}

// A recorded pose, played in a loop (tools/re/pose_export.py's format).
class PoseClip {
public:
  bool Load(const std::string &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    char magic[4]{};
    std::uint32_t version = 0, frames = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char *>(&version), 4);
    in.read(reinterpret_cast<char *>(&frames), 4);
    in.read(reinterpret_cast<char *>(&m_referenceHips), 4);
    if (!in || std::string_view(magic, 4) != "TPOS" || version != 1 || frames == 0) {
      error = std::format("{}: not a pose file", path);
      return false;
    }
    m_frames.resize(frames);
    for (auto &frame : m_frames) {
      in.read(reinterpret_cast<char *>(&frame.time), 4);
      in.read(reinterpret_cast<char *>(&frame.hips), 12);
      in.read(reinterpret_cast<char *>(frame.model.data()), sizeof(frame.model));
    }
    if (!in) {
      error = std::format("{}: truncated", path);
      return false;
    }
    return true;
  }

  // The pose `nowUs` into the loop.
  tandem::proto::PoseState At(std::uint64_t nowUs) const {
    const float span = m_frames.back().time - m_frames.front().time;
    const float t = m_frames.front().time + std::fmod(static_cast<float>(nowUs / 1e6), span);
    std::size_t i = 0;
    while (i + 1 < m_frames.size() && m_frames[i + 1].time <= t)
      ++i;
    const auto &frame = m_frames[i];
    return tandem::pose::Pack(tandem::pose::FromModel(frame.model, frame.hips, m_referenceHips));
  }

  std::size_t Frames() const { return m_frames.size(); }
  float Seconds() const { return m_frames.back().time - m_frames.front().time; }

private:
  struct Frame {
    float time{};
    Vec3 hips;
    std::array<tandem::proto::Quat, tandem::pose::kBones> model{};
  };
  std::vector<Frame> m_frames;
  float m_referenceHips{1.f};
};

// Messages with their time, for --record / --replay (NPC messages) and
// --save-path / --path (player states): per message u64 microseconds since
// the first, u32 length, the encoded message.
class Recording {
public:
  // Written beside the file (FILE.part) and put in its place by Finish, so
  // a recording without a single NPC (an empty street) never replaces a
  // good one: replaying it gives a guest no proxies at all.
  bool Open(const std::string &path) {
    m_path = path;
    m_out.open(path + ".part", std::ios::binary);
    return bool(m_out);
  }
  // Keeps the recording if it holds NPCs; false (and the old file) if not.
  bool Finish() {
    if (m_path.empty())
      return false;
    m_out.close();
    std::error_code error;
    if (m_spawns == 0) {
      std::filesystem::remove(m_path + ".part", error);
      return false;
    }
    std::filesystem::rename(m_path + ".part", m_path, error);
    return !error;
  }
  std::size_t Spawns() const { return m_spawns; }
  void Add(const tandem::proto::Message &message) {
    if (std::holds_alternative<tandem::proto::NpcSpawn>(message))
      ++m_spawns;
    const auto now = tandem::net::NowUs();
    if (!m_start)
      m_start = now;
    const auto bytes = tandem::proto::Encode(message);
    const std::uint64_t at = now - *m_start;
    const auto size = static_cast<std::uint32_t>(bytes.size());
    m_out.write(reinterpret_cast<const char *>(&at), 8);
    m_out.write(reinterpret_cast<const char *>(&size), 4);
    m_out.write(reinterpret_cast<const char *>(bytes.data()), size);
    m_out.flush();
    ++m_count;
  }
  std::size_t Count() const { return m_count; }

  struct Entry {
    std::uint64_t atUs;
    tandem::proto::Message message;
  };
  static std::vector<Entry> Load(const std::string &path) {
    std::vector<Entry> out;
    std::ifstream in(path, std::ios::binary);
    for (;;) {
      std::uint64_t at = 0;
      std::uint32_t size = 0;
      if (!in.read(reinterpret_cast<char *>(&at), 8) ||
          !in.read(reinterpret_cast<char *>(&size), 4))
        break;
      std::vector<std::byte> bytes(size);
      if (!in.read(reinterpret_cast<char *>(bytes.data()), size))
        break;
      if (auto message = tandem::proto::Decode(bytes))
        out.push_back({at, std::move(*message)});
    }
    return out;
  }

  // NPC spawns in a loaded recording (none: nothing to replay).
  static std::size_t SpawnsIn(const std::vector<Entry> &entries) {
    return static_cast<std::size_t>(std::ranges::count_if(entries, [](const Entry &entry) {
      return std::holds_alternative<tandem::proto::NpcSpawn>(entry.message);
    }));
  }

private:
  std::ofstream m_out;
  std::string m_path;
  std::optional<std::uint64_t> m_start;
  std::size_t m_count{};
  std::size_t m_spawns{};
};

// The course mode's script: where a V is `t` seconds in, in metres along
// the course (x ahead, z up) and how it moves, as its game would say it.
struct CoursePoint {
  float ahead{}, up{};
  tandem::movement::Movement movement{.known = true};
  std::uint8_t stance{}; // gamePSMLocomotionStates
  const char *what{};
};

class Course {
public:
  static constexpr float kGravity = 9.81f;

  // One lap, then it starts again from the start.
  float Seconds() const { return m_end; }

  CoursePoint At(float t) const {
    using D = tandem::movement::Detailed;
    using S = tandem::movement::Swim;
    t = std::fmod(t, m_end);
    float x = 0.f; // where the segment starts
    CoursePoint p;
    const auto segment = [&](float length, const char *what) {
      if (t < length) {
        p.what = what;
        return true;
      }
      t -= length;
      return false;
    };
    const auto ground = [&](D d) { p.movement.detailed = d; };
    // Walking 3 s at 1.6 m/s.
    if (segment(3.f, "walk")) {
      ground(D::Stand);
      p.ahead = x + 1.6f * t;
      return p;
    }
    x += 4.8f;
    // A jump, 1 m high, 0.9 s, 3 m/s ahead; then landing.
    if (segment(0.9f, "jump")) {
      const float v0 = 4.43f;
      p.movement.detailed = t < v0 / kGravity ? D::Jump : D::Fall;
      p.ahead = x + 3.f * t;
      p.up = std::max(0.f, v0 * t - 0.5f * kGravity * t * t);
      return p;
    }
    x += 2.7f;
    if (segment(0.3f, "land")) {
      ground(D::RegularLand);
      p.ahead = x;
      return p;
    }
    // A double jump: up, a second push at the top, down.
    if (segment(1.6f, "double jump")) {
      const float v0 = 4.43f, t1 = v0 / kGravity, h1 = 0.5f * v0 * t1;
      p.ahead = x + 2.f * t;
      if (t < t1) {
        p.movement.detailed = D::Jump;
        p.up = v0 * t - 0.5f * kGravity * t * t;
      } else {
        const float u = t - t1;
        const float v1 = 4.f;
        p.movement.detailed = u < v1 / kGravity ? D::DoubleJump : D::Fall;
        p.up = std::max(0.f, h1 + v1 * u - 0.5f * kGravity * u * u);
      }
      return p;
    }
    x += 3.2f;
    if (segment(0.3f, "land")) {
      ground(D::HardLand);
      p.ahead = x;
      return p;
    }
    // Mantling a 1.5 m ledge, then walking on top of it.
    if (segment(0.8f, "climb")) {
      p.movement.detailed = D::Climb;
      p.ahead = x + 0.6f * t / 0.8f;
      p.up = 1.5f * std::min(1.f, t / 0.6f);
      return p;
    }
    x += 0.6f;
    if (segment(2.f, "on the ledge")) {
      ground(D::Stand);
      p.ahead = x + 1.5f * t;
      p.up = 1.5f;
      return p;
    }
    x += 3.f;
    // Off the ledge: a fall and a hard landing.
    if (segment(0.55f, "drop")) {
      p.movement.detailed = D::Fall;
      p.ahead = x + 1.f * t;
      p.up = std::max(0.f, 1.5f - 0.5f * kGravity * t * t);
      return p;
    }
    x += 0.55f;
    if (segment(0.4f, "land")) {
      ground(D::HardLand);
      p.ahead = x;
      return p;
    }
    // A 4 m ladder: up at 1 m/s, a slide down.
    if (segment(4.f, "ladder up")) {
      p.movement.detailed = D::Ladder;
      p.ahead = x;
      p.up = 1.f * t;
      return p;
    }
    if (segment(1.5f, "ladder slide")) {
      p.movement.detailed = D::LadderSlide;
      p.ahead = x;
      p.up = 4.f - 4.f * t / 1.5f;
      return p;
    }
    // Swimming on the surface, diving 2.5 m, climbing out.
    if (segment(4.f, "swim")) {
      p.movement.swim = S::Surface;
      p.ahead = x + 1.2f * t;
      return p;
    }
    x += 4.8f;
    if (segment(5.f, "dive")) {
      p.movement.swim = S::Diving;
      p.ahead = x + 1.f * t;
      p.up = -2.5f * std::sin(std::numbers::pi_v<float> * t / 5.f);
      return p;
    }
    x += 5.f;
    // Back up before climbing out, as a V must (the surfacing has its sound).
    if (segment(1.f, "surface")) {
      p.movement.swim = S::Surface;
      p.ahead = x + 0.5f * t;
      return p;
    }
    x += 0.5f;
    if (segment(1.f, "out of the water")) {
      p.movement.swim = S::Climbing;
      p.ahead = x + 0.5f * t;
      p.up = 0.5f * std::sin(std::numbers::pi_v<float> * t);
      return p;
    }
    x += 0.5f;
    // An elevator: 6 m up, 6 m down, standing.
    if (segment(8.f, "elevator")) {
      ground(D::Stand);
      p.movement.elevator = t > 0.5f && t < 7.5f;
      p.ahead = x;
      p.up = t < 4.f ? 6.f * std::clamp((t - 0.5f) / 3.f, 0.f, 1.f)
                     : 6.f * std::clamp((7.5f - t) / 3.f, 0.f, 1.f);
      return p;
    }
    // A sprint into a slide, then a dodge.
    if (segment(1.5f, "sprint")) {
      ground(D::Sprint);
      p.stance = 2;
      p.ahead = x + 6.f * t;
      return p;
    }
    x += 9.f;
    if (segment(1.f, "slide")) {
      ground(D::Slide);
      p.stance = 9;
      p.ahead = x + 6.f * t - 2.5f * t * t;
      return p;
    }
    x += 3.5f;
    if (segment(0.3f, "dodge")) {
      ground(D::Dodge);
      p.stance = 6;
      p.ahead = x + 8.f * t;
      return p;
    }
    x += 2.4f;
    // Standing a moment at the end, then (next lap) back at the start.
    segment(m_end, "rest");
    ground(D::Stand);
    p.ahead = x;
    return p;
  }

private:
  float m_end{3.f + 0.9f + 0.3f + 1.6f + 0.3f + 0.8f + 2.f + 0.55f + 0.4f + 4.f + 1.5f + 4.f + 5.f +
              1.f + 8.f + 1.5f + 1.f + 0.3f + 2.f};
};

// A scripted host story (--story FILE): one step per line,
//   SECONDS VERB ARGS...    seconds after a guest is in the session
// Verbs (hashes are journal path hashes as the game logs them, `cmd story
// dump`; names are CNames or TweakDB records by name):
//   snapshot HASH:STATE ...      the journal as a joiner gets it
//   journal HASH STATE [notify]  one change (notify: the toast and sound)
//   tracked HASH                 the tracked entry
//   call CONTACT PHASE           the host's call: 1 ringing, 2 started, 3 ended
//   line TYPE SPEAKER TEXT...    a dialogue line (type 1 regular, 2 holocall;
//                                SPEAKER: host for our V, or a name)
//   hide                         the last line goes
//   hub ID TITLE|OPTION|...      a choice hub (no options: closed)
//   pick ID INDEX                the host picks (every hub closes)
//   hover ID INDEX               the host's cursor on an option (marked on
//                                the guests' hubs)
//   tier N                       the host's scene tier (1..5)
//   reward RECORD [QUESTHASH]    a quest reward for every guest
//   prefabs REF[,REF]|off        the world prefabs the host's quests have
//                                loaded (node refs, hex); off: none
//   gates N                      the host's story gates (proto::kGate* bits)
//   restrict REC[,REC]|off       the gameplay restrictions the host's story
//                                has on its V now (GameplayRestriction.NoCombat:
//                                weapons away); off: lifted
//   effect RECORD on|off         a story status effect on the host's V
//                                (a relic glitch: BaseStatusEffect.JohnnySicknessLow)
//   replacer on|off              the host plays as Johnny (StoryState flag)
//   fasttravel on|off            off: the host's quest blocks fast travel
//                                (StoryState's kStoryNoFastTravel); on: lifted
//   private on|off               the host is in a private scene (romance,
//                                a minor one-to-one chat: StoryState flag)
//   scene NAME [TIER]            a story scene plays on the host (scene
//                                replay; NAME: its resource path or a
//                                number; TIER: the host's, 4 by default);
//                                scene off: it is over; scene private: it
//                                is one nobody watches
//   cast NPC[:ghost][:hidden] ...  its actors among the NPCs the host shares
//                                (own: the --npc NPC), standing in front of
//                                the mock player; with --pose or in mirror
//                                and front modes, each takes the pose the
//                                mock player sends (ActorPose)
//   prop N RECORD HOLDER [left]  an actor of the scene holds something (N: our
//                                number for it; RECORD: an item's TweakDB
//                                name; HOLDER: host, an NPC id or own; left:
//                                in its left hand); again with another
//                                holder: handed over; prop N off: put away
//   face NPC on|off              that actor speaks: its face (a 40-joint
//                                stand-in whose jaw opens and closes) is sent
//                                as rig poses (RigPose) while it lasts
//   moment on [watch] [TIER]|off the host's V is in a scripted moment (its
//                                StoryState says kStoryMoment, its tier TIER,
//                                else the scene's): a guest in it plays it from its own
//                                V; watch (kStoryWatch: a cutscene at tier 5,
//                                another world, the quest's camera): it
//                                watches through its camera (camera on sends
//                                the views, scene or not)
//   camera on [FOV]|cut|off      the host is in a cutscene: its camera's view
//                                (behind and above the mock player) is sent
//                                every frame (SceneCamera), in a scene, a
//                                moment or neither (a real host's view while
//                                a guest watches it: spectate-missing); cut:
//                                the next view is another shot; off: no more
//                                views
//   skip on|off                  the host fast-forwards the scene
//   voice ID SPEAKER [SECONDS] [call]  a voiced line begins (ID: its string
//                                id; SPEAKER: host, an NPC id or own)
//   cut ID                       it is cut before its end
//   world PLACE [moved] [settled] the host's V in another world (Elsewhere):
//                                cyberspace, braindance, johnny, other, or
//                                city to come back; moved: the scene took it
//                                away from its body, which stays where the
//                                mock player was (settled: sitting, lying)
//   activity KIND [anchored] [lying] [frozen]  the host's V busy
//                                (docs/architecture/activities.md): workspot,
//                                sleep, screen, minigame, shop, menu, photo,
//                                contest, or none; anchored where it stands
//   skip HOURS [wait|sleep|quest] the host asks the others to skip HOURS
//                                (quest: its story moved the clock: said)
// Lines starting with # are comments.
struct StoryStep {
  double at{};
  std::string verb;
  std::vector<std::string> args;
  std::string rest; // everything after the verb
};

std::vector<StoryStep> LoadStory(const std::string &path, std::string &error) {
  std::vector<StoryStep> steps;
  std::ifstream in(path);
  if (!in) {
    error = std::format("cannot read {}", path);
    return steps;
  }
  std::string line;
  for (int number = 1; std::getline(in, line); ++number) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream words(line);
    StoryStep step;
    if (!(words >> step.at >> step.verb)) {
      error = std::format("{}:{}: expected SECONDS VERB", path, number);
      return {};
    }
    std::getline(words, step.rest);
    if (const auto first = step.rest.find_first_not_of(' '); first != std::string::npos)
      step.rest = step.rest.substr(first);
    std::istringstream rest(step.rest);
    for (std::string word; rest >> word;)
      step.args.push_back(word);
    steps.push_back(std::move(step));
  }
  return steps;
}

std::uint32_t Hash32(const std::string &text) {
  return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
}

tandem::proto::Place PlaceNamed(const std::string &name) {
  using tandem::proto::Place;
  if (name == "cyberspace")
    return Place::Cyberspace;
  if (name == "braindance")
    return Place::Braindance;
  if (name == "johnny")
    return Place::Johnny;
  if (name == "other")
    return Place::Other;
  return Place::City;
}

const char *PlaceName(tandem::proto::Place place) {
  constexpr const char *names[] = {"the city", "cyberspace", "a braindance", "Johnny",
                                   "another body"};
  const auto index = static_cast<std::size_t>(place);
  return index < std::size(names) ? names[index] : "?";
}

// The scene the scripted host plays (scene replay): what its game would
// sample, said through the same Stage as the game's (core/scene.hpp).
struct SceneScript {
  tandem::scene::Stage stage;
  tandem::scene::HostSample sample;
  tandem::proto::NpcId own{}; // "own" in a .story file: the --npc NPC
  // camera on: the view sent every frame; which side it looks from (a cut
  // changes it).
  // face NPC on: the actors whose faces are sent.
  std::vector<tandem::proto::NpcId> faces;
  bool camera{}, cameraCut{}, cameraSide{};
  float cameraFov{50.f};
  // moment on: the host's V in a scripted moment (kStoryMoment); its camera
  // views go out with camera on, scene or not (story.md, "Spectating").
  bool moment{};
};

// A scene step (scene, cast, skip, voice, cut): what the host sends for it.
// nullopt: not a scene step.
std::optional<std::vector<tandem::proto::Message>>
SceneMessages(const StoryStep &step, SceneScript &script, const tandem::proto::Vec3 &host,
              float hostYaw, tandem::proto::PlayerId hostPlayer, std::uint64_t hostNowUs) {
  namespace proto = tandem::proto;
  const auto &a = step.args;
  const auto npcOf = [&](const std::string &word) -> proto::NpcId {
    return word == "own" ? script.own : static_cast<proto::NpcId>(std::stoul(word, nullptr, 0));
  };
  try {
    if (step.verb == "scene" && !a.empty()) {
      script.sample.host = host;
      script.sample.hostYaw = hostYaw;
      script.sample.isPrivate = a[0] == "private";
      if (a[0] == "off") {
        script.sample = {};
      } else {
        // A number, or a resource path hashed as the game hashes it (FNV-1a).
        const bool number = a[0].find_first_not_of("0123456789abcdefxABCDEFX") == std::string::npos;
        script.sample.scene = number ? std::stoull(a[0], nullptr, 0) : CName(a[0]);
        script.sample.tier = static_cast<std::uint8_t>(a.size() > 1 ? std::stoi(a[1]) : 4);
        script.sample.rate = 1.f;
      }
      return script.stage.Update(script.sample, hostNowUs);
    }
    if (step.verb == "cast") {
      script.sample.host = host;
      script.sample.hostYaw = hostYaw;
      script.sample.actors.clear();
      // In an arc 2.5 m in front of the mock player, facing it.
      const float spread = 0.7f;
      for (std::size_t i = 0; i < a.size(); ++i) {
        tandem::scene::ActorSample actor;
        auto word = a[i];
        actor.flags = proto::kActorPosed;
        for (auto colon = word.rfind(':'); colon != std::string::npos; colon = word.rfind(':')) {
          const auto flag = word.substr(colon + 1);
          if (flag == "ghost")
            actor.flags |= proto::kActorGhost;
          else if (flag == "hidden")
            actor.flags |= proto::kActorHidden;
          word.resize(colon);
        }
        actor.npc = npcOf(word);
        const float angle =
            hostYaw + (static_cast<float>(i) - static_cast<float>(a.size() - 1) / 2.f) * spread;
        actor.position = {host.x - std::sin(angle) * 2.5f, host.y + std::cos(angle) * 2.5f, host.z};
        script.sample.actors.push_back(actor);
      }
      return script.stage.Update(script.sample, hostNowUs);
    }
    if (step.verb == "face" && a.size() >= 2) {
      const auto npc = npcOf(a[0]);
      std::erase(script.faces, npc);
      if (a[1] == "on")
        script.faces.push_back(npc);
      return std::vector<proto::Message>{};
    }
    if (step.verb == "moment" && !a.empty()) {
      script.moment = a[0] == "on";
      bool watch = false;
      int tier = script.stage.On() ? std::max<int>(script.sample.tier, 2) : 1;
      for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] == "watch")
          watch = script.moment;
        else
          tier = std::stoi(a[i]);
      }
      std::vector<proto::Message> out;
      out.push_back(proto::StoryState{
          .tier = static_cast<std::uint8_t>(tier),
          .flags = static_cast<std::uint8_t>((script.moment ? proto::kStoryMoment : 0) |
                                             (watch ? proto::kStoryWatch : 0))});
      return out;
    }
    if (step.verb == "camera" && !a.empty()) {
      if (a[0] == "cut") {
        script.cameraCut = true;
        script.cameraSide = !script.cameraSide;
      } else {
        script.camera = a[0] == "on";
        if (a.size() > 1)
          script.cameraFov = std::stof(a[1]);
      }
      return std::vector<proto::Message>{};
    }
    if (step.verb == "prop" && a.size() >= 2) {
      const auto key = std::stoull(a[0], nullptr, 0);
      std::erase_if(script.sample.props, [&](const auto &prop) { return prop.key == key; });
      if (a[1] != "off" && a.size() >= 3) {
        const bool byHost = a[2] == "host";
        script.sample.props.push_back({.key = key,
                                       .record = TweakDBID(a[1]),
                                       .holder = byHost ? 0 : npcOf(a[2]),
                                       .holderPlayer = byHost ? hostPlayer : proto::kNoPlayer,
                                       .leftHand = a.size() > 3 && a[3] == "left"});
      }
      return script.stage.Update(script.sample, hostNowUs);
    }
    if (step.verb == "skip" && !a.empty()) {
      script.sample.rate = a[0] == "on" ? 4.f : 1.f;
      return script.stage.Update(script.sample, hostNowUs);
    }
    if (step.verb == "voice" && a.size() >= 2) {
      std::uint8_t flags = 0;
      float seconds = 4.f;
      for (std::size_t i = 2; i < a.size(); ++i) {
        if (a[i] == "call")
          flags |= proto::kVoiceHolocall;
        else
          seconds = std::stof(a[i]);
      }
      const bool byHost = a[1] == "host";
      std::vector<proto::Message> out;
      if (auto line =
              script.stage.Line(std::stoull(a[0], nullptr, 0), byHost ? 0 : npcOf(a[1]),
                                byHost ? hostPlayer : proto::kNoPlayer, seconds, flags, hostNowUs))
        out.push_back(*line);
      return out;
    }
    if (step.verb == "cut" && !a.empty()) {
      std::vector<proto::Message> out;
      if (auto line = script.stage.Hide(std::stoull(a[0], nullptr, 0), hostNowUs))
        out.push_back(*line);
      return out;
    }
  } catch (const std::exception &) {
    return std::vector<proto::Message>{};
  }
  return std::nullopt;
}

// One story step as the host's game would send it.
std::optional<tandem::proto::Message> StoryMessage(const StoryStep &step, std::uint64_t &lastLine,
                                                   tandem::proto::PlayerId host) {
  namespace proto = tandem::proto;
  const auto &a = step.args;
  try {
    if (step.verb == "snapshot") {
      proto::Journal journal{.snapshot = true};
      for (const auto &pair : a) {
        const auto colon = pair.find(':');
        journal.entries.push_back({Hash32(pair.substr(0, colon)),
                                   static_cast<std::uint8_t>(std::stoi(pair.substr(colon + 1))),
                                   0});
      }
      return journal;
    }
    if (step.verb == "journal" && a.size() >= 2)
      return proto::Journal{.entries = {{Hash32(a[0]), static_cast<std::uint8_t>(std::stoi(a[1])),
                                         static_cast<std::uint8_t>(a.size() > 2)}}};
    if (step.verb == "tracked" && !a.empty())
      return proto::Tracked{Hash32(a[0])};
    if (step.verb == "call" && a.size() >= 2)
      return proto::PhoneCall{.contact = CName(a[0]),
                              .phase = static_cast<std::uint8_t>(std::stoi(a[1])),
                              .mode = 1,
                              .flags = proto::kCallRejectable | proto::kCallShowAvatar};
    if (step.verb == "line" && a.size() >= 3) {
      proto::Subtitle line{
          .line = ++lastLine, .type = static_cast<std::uint8_t>(std::stoi(a[0])), .duration = 4.f};
      if (a[1] == "host")
        line.speakerPlayer = host;
      else
        line.speakerName = a[1];
      const auto text = step.rest.find(a[1]);
      line.text = step.rest.substr(text + a[1].size() + 1);
      return line;
    }
    if (step.verb == "hide")
      return proto::Subtitle{.line = lastLine, .flags = proto::kLineHide};
    if (step.verb == "hub" && !a.empty()) {
      proto::ChoiceHub hub{.hub = std::stoi(a[0])};
      const auto rest = step.rest.substr(step.rest.find(a[0]) + a[0].size());
      if (const auto bar = rest.find('|'); bar != std::string::npos) {
        hub.title = rest.substr(rest.find_first_not_of(' '), bar - rest.find_first_not_of(' '));
        std::size_t start = bar + 1;
        for (;;) {
          const auto next = rest.find('|', start);
          hub.options.push_back({rest.substr(start, next - start), 0});
          if (next == std::string::npos)
            break;
          start = next + 1;
        }
      }
      return hub;
    }
    if (step.verb == "pick" && a.size() >= 2)
      return proto::ChoiceCursor{.hub = std::stoi(a[0]),
                                 .index = static_cast<std::int8_t>(std::stoi(a[1])),
                                 .kind = proto::kCursorPicked};
    if (step.verb == "hover" && a.size() >= 2)
      return proto::ChoiceCursor{.hub = std::stoi(a[0]),
                                 .index = static_cast<std::int8_t>(std::stoi(a[1])),
                                 .kind = proto::kCursorHover};
    if (step.verb == "tier" && !a.empty())
      return proto::StoryState{.tier = static_cast<std::uint8_t>(std::stoi(a[0]))};
    if (step.verb == "private" && !a.empty())
      return proto::StoryState{.tier = static_cast<std::uint8_t>(a[0] == "on" ? 3 : 1),
                               .flags = a[0] == "on" ? proto::kStoryPrivate : std::uint8_t{0}};
    if (step.verb == "replacer" && !a.empty())
      return proto::StoryState{.tier = 1,
                               .flags = a[0] == "on" ? proto::kStoryReplacer : std::uint8_t{0}};
    if (step.verb == "fasttravel" && !a.empty())
      return proto::StoryState{
          .tier = 1, .flags = a[0] == "off" ? proto::kStoryNoFastTravel : std::uint8_t{0}};
    if (step.verb == "restrict" && !a.empty()) {
      proto::StoryRestrictions set;
      if (a[0] != "off")
        for (std::size_t start = 0; start <= a[0].size();) {
          const auto comma = std::min(a[0].find(',', start), a[0].size());
          if (comma > start)
            set.effects.push_back(TweakDBID(a[0].substr(start, comma - start)));
          start = comma + 1;
        }
      return set;
    }
    if (step.verb == "prefabs" && !a.empty()) {
      proto::WorldPrefabs set;
      if (a[0] != "off")
        for (std::size_t start = 0; start <= a[0].size();) {
          const auto comma = std::min(a[0].find(',', start), a[0].size());
          if (comma > start)
            set.prefabs.push_back(std::stoull(a[0].substr(start, comma - start), nullptr, 16));
          start = comma + 1;
        }
      return set;
    }
    if (step.verb == "gates" && !a.empty())
      return proto::StoryGates{.flags = static_cast<std::uint8_t>(std::stoi(a[0]))};
    if (step.verb == "effect" && a.size() >= 2)
      return proto::StoryEffect{.effect = TweakDBID(a[0]), .applied = a[1] == "on"};
    if (step.verb == "world" && !a.empty()) {
      proto::Elsewhere away{.place = PlaceNamed(a[0])};
      for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] == "moved")
          away.flags |= proto::kElsewhereMoved;
        else if (a[i] == "settled")
          away.flags |= proto::kElsewhereSettled;
      }
      return away;
    }
    if (step.verb == "activity" && !a.empty()) {
      const auto kind = ActivityNamed(a[0]);
      if (!kind)
        return std::nullopt;
      proto::Activity busy{.kind = *kind};
      for (std::size_t i = 1; i < a.size(); ++i) {
        if (a[i] == "anchored")
          busy.flags |= proto::kActivityAnchored;
        else if (a[i] == "lying")
          busy.flags |= proto::kActivityLying;
        else if (a[i] == "frozen")
          busy.flags |= proto::kActivityFrozen;
      }
      return busy;
    }
    if (step.verb == "skip" && !a.empty()) {
      const auto reason = a.size() > 1 ? SkipReasonNamed(a[1]) : proto::SkipReason::Wait;
      if (!reason)
        return std::nullopt;
      return proto::TimeSkip{.reason = *reason, .toSeconds = std::stoi(a[0]) * 3600};
    }
    if (step.verb == "reward" && !a.empty())
      return proto::QuestReward{.reward = TweakDBID(a[0]),
                                .quest = a.size() > 1 ? Hash32(a[1]) : 0};
  } catch (const std::exception &) {
  }
  return std::nullopt;
}

// A story message heard, in one line (for the scenarios' expect_peer).
std::string Describe(const tandem::proto::Message &message) {
  namespace proto = tandem::proto;
  if (const auto *journal = std::get_if<proto::Journal>(&message))
    return std::format("journal: {} {} entries", journal->snapshot ? "snapshot of" : "change of",
                       journal->entries.size());
  if (const auto *tracked = std::get_if<proto::Tracked>(&message))
    return std::format("tracked: {}", tracked->hash);
  if (const auto *call = std::get_if<proto::PhoneCall>(&message))
    return std::format("call: contact {:#x} phase {} answered by {}{}", call->contact, call->phase,
                       call->answeredBy, call->flags & proto::kCallRejected ? " (rejected)" : "");
  if (const auto *action = std::get_if<proto::PhoneAction>(&message))
    return std::format("phone: action {} on {:#x} by player {}", action->action, action->contact,
                       action->player);
  if (const auto *reply = std::get_if<proto::PhoneReply>(&message))
    return std::format("phone: reply {} by player {}", reply->choiceEntry, reply->player);
  if (const auto *line = std::get_if<proto::Subtitle>(&message))
    return (line->flags & proto::kLineHide)
               ? std::format("line: {} hidden", line->line)
               : std::format("line: type {} from {}: {}", line->type,
                             line->speakerPlayer ? std::format("player {}", line->speakerPlayer)
                                                 : line->speakerName,
                             line->text);
  if (const auto *hub = std::get_if<proto::ChoiceHub>(&message))
    return hub->options.empty() ? std::format("hub: {} closed", hub->hub)
                                : std::format("hub: {} '{}' with {} options", hub->hub, hub->title,
                                              hub->options.size());
  if (const auto *cursor = std::get_if<proto::ChoiceCursor>(&message))
    return std::format("cursor: player {} hub {} index {} kind {}", cursor->player, cursor->hub,
                       cursor->index, cursor->kind);
  if (const auto *state = std::get_if<proto::StoryState>(&message))
    return std::format("story state: player {} tier {} flags {}", state->player, state->tier,
                       state->flags);
  if (const auto *reward = std::get_if<proto::QuestReward>(&message))
    return std::format("reward: {:#x} (quest {})", reward->reward, reward->quest);
  if (const auto *effect = std::get_if<proto::StoryEffect>(&message))
    return std::format("story effect: {:#x} {} on player {}", effect->effect,
                       effect->applied ? "on" : "off", effect->player);
  if (const auto *set = std::get_if<proto::WorldPrefabs>(&message)) {
    std::string refs;
    for (const auto prefab : set->prefabs)
      refs += std::format(" {:#x}", prefab);
    return set->prefabs.empty() ? std::format("world prefabs: none of player {}", set->player)
                                : std::format("world prefabs: {} of player {}:{}",
                                              set->prefabs.size(), set->player, refs);
  }
  if (const auto *gates = std::get_if<proto::StoryGates>(&message))
    return std::format("story gates: player {} flags {}", gates->player, gates->flags);
  if (const auto *set = std::get_if<proto::StoryRestrictions>(&message)) {
    std::string effects;
    for (const auto effect : set->effects)
      effects += std::format(" {:#x}", effect);
    return set->effects.empty() ? std::format("story restrictions: none on player {}", set->player)
                                : std::format("story restrictions: {} on player {}:{}",
                                              set->effects.size(), set->player, effects);
  }
  if (const auto *scene = std::get_if<proto::SceneState>(&message))
    return scene->scene == 0
               ? std::format("scene: play {} over{}", scene->play,
                             scene->flags & proto::kSceneInterrupted ? " (interrupted)" : "")
               : std::format("scene: {:#x} play {} tier {} rate {:.1f} flags {:#x}{}", scene->scene,
                             scene->play, scene->tier, scene->rate, scene->flags,
                             scene->flags & proto::kSceneStaged
                                 ? std::format(" staged at {:.1f} {:.1f} {:.1f} ({:.1f} m)",
                                               scene->anchor.x, scene->anchor.y, scene->anchor.z,
                                               scene->radius)
                                 : "");
  if (const auto *cast = std::get_if<proto::SceneCast>(&message)) {
    std::string who;
    for (const auto &actor : cast->actors)
      who += std::format(" {}{}", actor.npc,
                         actor.flags & proto::kActorGhost
                             ? (actor.flags & proto::kActorHidden ? " (ghost, hidden)" : " (ghost)")
                             : "");
    return std::format("scene cast: play {}, {} actor(s):{}", cast->play, cast->actors.size(), who);
  }
  if (const auto *line = std::get_if<proto::SceneLine>(&message))
    return std::format("scene line: {} {} by {} ({:.1f} s, play {})", line->line,
                       line->flags & proto::kVoiceStop ? "cut" : "voiced",
                       line->speakerPlayer ? std::format("player {}", line->speakerPlayer)
                                           : std::format("npc {}", line->speaker),
                       line->duration, line->play);
  if (const auto *prop = std::get_if<proto::SceneProp>(&message))
    return std::format("scene prop: {} ({:#x}) {} by {}{} (play {})", prop->prop, prop->record,
                       prop->flags & proto::kPropGone ? "gone, last held" : "held",
                       prop->holderPlayer ? std::format("player {}", prop->holderPlayer)
                                          : std::format("npc {}", prop->holder),
                       prop->flags & proto::kPropLeftHand ? ", left hand" : "", prop->play);
  if (const auto *place = std::get_if<proto::ScenePlace>(&message))
    return std::format("scene prop: {} ({:#x}) {} at ({:.2f}, {:.2f}, {:.2f}) (play {})",
                       place->prop, place->entity,
                       place->flags & proto::kPropGone ? "gone" : "set down", place->position.x,
                       place->position.y, place->position.z, place->play);
  if (const auto *use = std::get_if<proto::QuestInteract>(&message))
    return std::format("quest interaction: player {} on {:x} ({:#x}/{:#x}), question {}: {}",
                       use->player, use->target, use->choice, use->detail, use->seq,
                       tandem::interact::VerdictName(use->verdict));
  return "story: ?";
}

// --anims: NPC animation states and shots by time, for the replayed NPCs.
// One line per change, `#` comments:
//   SECONDS NPC HIGH UPPER STANCE [weapon=RECORD] [target=PLAYER] [shoot=N]
//           [workspot=HASH:ENTRY:ANIM]
// SECONDS count from the replay's start. NPC is a replayed NPC's id or *
// (every replayed NPC out then); HIGH, UPPER and STANCE are the game's state
// names (gamedataNPCHighLevelState, gamedataNPCUpperBodyState,
// gamedataNPCStanceState) or numbers; shoot=N fires N shots, five a second;
// workspot takes the resource path hash (as the game logs it, 0x… or
// decimal), the tree entry and the animation name. With a script, the
// recording's own animation states and shots are not sent: the script alone
// decides, whichever NPCs were recorded.
class NpcAnimScript {
public:
  struct Line {
    std::uint64_t atUs{};
    std::optional<tandem::proto::NpcId> npc; // none: every NPC
    tandem::proto::NpcAnim anim;
    int shots{};
  };

  bool Load(const std::string &path, std::string &error) {
    std::ifstream in(path);
    if (!in) {
      error = std::format("cannot read {}", path);
      return false;
    }
    std::string text;
    for (int number = 1; std::getline(in, text); ++number) {
      if (const auto hash = text.find('#'); hash != std::string::npos)
        text.resize(hash);
      std::vector<std::string> words;
      for (std::size_t at = 0; at < text.size();) {
        const auto start = text.find_first_not_of(" \t\r", at);
        if (start == std::string::npos)
          break;
        const auto end = text.find_first_of(" \t\r", start);
        words.push_back(text.substr(start, end - start));
        at = end == std::string::npos ? text.size() : end;
      }
      if (words.empty())
        continue;
      Line line;
      if (words.size() < 5 || !ParseLine(words, line)) {
        error =
            std::format("{}:{}: not SECONDS NPC HIGH UPPER STANCE [key=value...]", path, number);
        return false;
      }
      m_lines.push_back(line);
    }
    std::ranges::stable_sort(m_lines, {}, &Line::atUs);
    return true;
  }

  std::size_t Size() const { return m_lines.size(); }
  const std::vector<Line> &Lines() const { return m_lines; }

private:
  static std::optional<int> Number(std::string_view text) {
    int value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size())
      return std::nullopt;
    return value;
  }
  // A state by its enum value's name or number.
  static std::optional<std::uint8_t> State(std::string_view text,
                                           std::initializer_list<std::string_view> names) {
    if (const auto number = Number(text))
      return static_cast<std::uint8_t>(*number);
    std::uint8_t index = 0;
    for (const auto name : names) {
      if (name == text)
        return index;
      ++index;
    }
    return std::nullopt;
  }

  static bool ParseLine(const std::vector<std::string> &words, Line &line) {
    double seconds = 0;
    if (std::from_chars(words[0].data(), words[0].data() + words[0].size(), seconds).ec !=
        std::errc{})
      return false;
    line.atUs = static_cast<std::uint64_t>(seconds * 1e6);
    if (words[1] != "*") {
      const auto npc = Number(words[1]);
      if (!npc)
        return false;
      line.npc = static_cast<tandem::proto::NpcId>(*npc);
    }
    const auto high = State(words[2], {"Alerted", "Any", "Combat", "Dead", "Fear", "Relaxed",
                                       "Stealth", "Unconscious", "Wounded"});
    const auto upper = State(words[3], {"Aim", "Any", "Attack", "ChargedAttack", "Defend", "Equip",
                                        "Normal", "Parry", "Reload", "Shoot", "Taunt"});
    const auto stance =
        State(words[4], {"Any", "Cover", "Crouch", "Stand", "Swim", "Vehicle", "VehicleWindow"});
    if (!high || !upper || !stance)
      return false;
    line.anim.highLevel = *high;
    line.anim.upperBody = *upper;
    line.anim.stance = *stance;
    for (std::size_t i = 5; i < words.size(); ++i) {
      const auto equals = words[i].find('=');
      if (equals == std::string::npos)
        return false;
      const std::string_view key(words[i].data(), equals);
      const std::string_view value(words[i].data() + equals + 1, words[i].size() - equals - 1);
      if (key == "weapon") {
        line.anim.weapon = TweakDBID(value);
        line.anim.flags |= tandem::proto::kNpcAnimWeaponDrawn;
      } else if (key == "target") {
        const auto player = Number(value);
        if (!player)
          return false;
        line.anim.target = static_cast<std::uint32_t>(*player);
      } else if (key == "shoot") {
        const auto shots = Number(value);
        if (!shots)
          return false;
        line.shots = *shots;
      } else if (key == "workspot") {
        // HASH:ENTRY:ANIM
        const auto first = value.find(':');
        if (first == std::string_view::npos)
          return false;
        const auto second = value.find(':', first + 1);
        if (second == std::string_view::npos)
          return false;
        const auto hashText = value.substr(0, first);
        const bool hex = hashText.starts_with("0x");
        const auto digits = hex ? hashText.substr(2) : hashText;
        std::uint64_t hash = 0;
        if (std::from_chars(digits.data(), digits.data() + digits.size(), hash, hex ? 16 : 10).ec !=
            std::errc{})
          return false;
        const auto entry = Number(value.substr(first + 1, second - first - 1));
        if (!entry)
          return false;
        line.anim.workspot = hash;
        line.anim.workspotEntry = static_cast<std::uint32_t>(*entry);
        line.anim.workspotAnim = CName(value.substr(second + 1));
        line.anim.flags |= tandem::proto::kNpcAnimInWorkspot;
      } else
        return false;
    }
    return true;
  }

  std::vector<Line> m_lines;
};

// --mode region: the NPCs this mock player runs itself, as a game does for the
// NPCs around its V (docs/architecture/regions.md): announced once in the
// session, moved ten times a second, hurt and killed by the others' strikes,
// given away when the host asks and taken over when it says so.
class RegionNpcs {
public:
  explicit RegionNpcs(const Options &options) : m_options(options) {}

  void Tick(tandem::Endpoint &endpoint, std::uint64_t nowUs, const Vec3 &at, bool follow) {
    namespace proto = tandem::proto;
    if (!endpoint.IsJoined())
      return;
    if (!m_announced) {
      m_announced = true;
      for (int i = 0; i < m_options.regionNpcs; ++i) {
        const auto id =
            tandem::regions::MakeNpcId(endpoint.You(), static_cast<std::uint32_t>(i + 1));
        m_npcs[id] = {.position = Around(at, i), .slot = i};
        endpoint.SendNpcSpawn({.npc = id,
                               .record = TweakDBID(m_options.regionRecord),
                               .position = m_npcs[id].position});
        endpoint.SendNpcHealth({.npc = id, .health = 1.f, .flags = ProtectedFlag()});
        if (m_options.regionPlaced != 0)
          endpoint.SendCombat(proto::NpcPlaced{
              .npc = id, .entity = m_options.regionPlaced + static_cast<std::uint64_t>(i)});
      }
      m_startUs = nowUs;
      Log("region: running {} NPC(s) of our own ({}), ids from {}{}", m_options.regionNpcs,
          m_options.regionRecord, tandem::regions::MakeNpcId(endpoint.You(), 1),
          m_options.regionPlaced != 0
              ? std::format(", placed (entities from {:x})", m_options.regionPlaced)
              : "");
    }
    // --region-kill-at: our own V kills our first NPC, alone or not.
    if (m_options.regionKillAtSeconds >= 0 && !m_killed &&
        nowUs - m_startUs >=
            static_cast<std::uint64_t>(m_options.regionKillAtSeconds) * 1'000'000) {
      m_killed = true;
      const auto first = tandem::regions::MakeNpcId(endpoint.You(), 1);
      if (m_npcs.erase(first) > 0) {
        endpoint.SendNpcGone({.npc = first, .reason = proto::kGoneDead, .killer = endpoint.You()});
        Log("region: our npc {} died (killer: player {}) (--region-kill-at)", first,
            endpoint.You());
      }
    }
    // Our street: cars round us, the first one driven by our first NPC; a
    // game announces vehicles only where it runs the area round its V.
    if (!m_carsAnnounced && m_options.regionCars > 0 && endpoint.OwnsArea()) {
      m_carsAnnounced = true;
      for (int i = 0; i < m_options.regionCars; ++i) {
        const auto id = proto::MakeTrafficId(endpoint.You(), static_cast<std::uint32_t>(i + 1));
        const bool driven = i == 0 && m_options.regionDriver && m_options.regionNpcs > 0;
        m_cars[id] = {.position = CarAround(at, i),
                      .slot = i,
                      .flags = driven ? proto::kTrafficLights : proto::kTrafficParked};
        endpoint.SendTrafficSpawn({.id = id,
                                   .record = TweakDBID(m_options.trafficVehicle),
                                   .position = m_cars[id].position,
                                   .flags = m_cars[id].flags});
        if (driven)
          endpoint.SendNpcSeat(
              {.npc = tandem::regions::MakeNpcId(endpoint.You(),
                                                 static_cast<std::uint32_t>(m_options.regionNpcs)),
               .owner = proto::TrafficSeatOwner(id),
               .seat = CName("seat_front_left")});
      }
      Log("street: running {} vehicle(s) of our own, ids from {:#x}{}", m_options.regionCars,
          proto::MakeTrafficId(endpoint.You(), 1),
          m_options.regionDriver ? ", our last npc at the wheel of the first" : "");
    }
    // --region-chase: our first car goes after the first player we meet.
    if (m_options.regionChase && !m_chased && !m_cars.empty()) {
      const auto first = m_cars.begin()->first;
      if (const auto linked = endpoint.Linked(); !linked.empty() && endpoint.OwnsTraffic(first)) {
        m_chased = true;
        endpoint.SendChase(
            {.vehicle = first, .target = linked.front(), .flags = proto::kChaseRamming});
        Log("street: our vehicle {:#x} chases player {}", first, linked.front());
      }
    }
    if (nowUs - m_lastStatesUs < 100'000)
      return;
    m_lastStatesUs = nowUs;
    proto::NpcStates states;
    for (auto &[id, npc] : m_npcs) {
      // With us while we walk up; where they are once we leave them, and
      // the ones we took over stay where they were handed to us.
      if (follow && npc.slot >= 0)
        npc.position = Around(at, npc.slot);
      states.npcs.push_back({.npc = id, .position = npc.position});
    }
    endpoint.SendNpcStates(std::move(states));
    // Our cars move with us (and say so) until we leave them.
    proto::TrafficStates cars;
    for (auto &[id, car] : m_cars) {
      if (!follow || car.slot < 0)
        continue;
      const auto was = car.position;
      car.position = CarAround(at, car.slot);
      const Vec3 velocity{(car.position.x - was.x) * 10.f, (car.position.y - was.y) * 10.f, 0.f};
      cars.cars.push_back(tandem::traffic::Pack(
          {.id = id, .position = car.position, .orientation = {}, .velocity = velocity}));
    }
    if (!cars.cars.empty())
      endpoint.SendTrafficStates(std::move(cars));
  }

  // True: the event was about the NPCs we run, and is dealt with.
  bool On(const tandem::SessionEvent &event, tandem::Endpoint &endpoint) {
    namespace proto = tandem::proto;
    namespace events = tandem::events;
    if (const auto *linked = std::get_if<events::RegionsChanged>(&event)) {
      std::string who;
      for (const auto player : linked->linked)
        who += (who.empty() ? "" : ", ") + std::to_string(player);
      Log("region: linked with {}", who.empty() ? "nobody" : "player " + who);
      return true;
    }
    if (const auto *give = std::get_if<events::NpcGive>(&event)) {
      const auto it = m_npcs.find(give->npc);
      if (it == m_npcs.end()) {
        endpoint.RefuseNpc(give->npc);
        return true;
      }
      endpoint.GiveNpc({.npc = give->npc,
                        .to = give->to,
                        .position = it->second.position,
                        .health = it->second.health,
                        .flags = proto::kHandoverCombat,
                        .target = give->to,
                        .hostileTo = static_cast<std::uint8_t>(1u << ((give->to - 1) & 7)),
                        .highLevel = 2});
      Log("region: handed npc {} over to player {} (health {:.2f})", give->npc, give->to,
          it->second.health);
      m_npcs.erase(it);
      return true;
    }
    if (const auto *take = std::get_if<events::NpcTake>(&event)) {
      const auto &state = take->state;
      auto &npc = m_npcs[state.npc];
      npc.slot = -1;
      if ((state.flags & proto::kHandoverBlind) == 0)
        npc.position = state.position;
      else if (const auto known = endpoint.SharedNpcs().find(state.npc);
               known != endpoint.SharedNpcs().end())
        npc.position = known->second.position;
      npc.health = state.health >= 0.f ? state.health : 1.f;
      endpoint.AdoptNpc(state.npc, true);
      Log("region: took npc {} over from player {} (health {:.2f}, {} effect(s), target "
          "player {}{})",
          state.npc, state.from, npc.health, state.effects.size(), state.target,
          state.flags & proto::kHandoverInherited ? ", inherited" : "");
      return true;
    }
    // Our cars: given away, taken over, hit (traffic.md, "Who owns an area").
    if (const auto *give = std::get_if<events::TrafficGive>(&event)) {
      const auto it = m_cars.find(give->id);
      if (it == m_cars.end()) {
        endpoint.AnswerTraffic(give->id, false);
        return true;
      }
      endpoint.GiveTraffic({.id = give->id,
                            .to = give->to,
                            .position = it->second.position,
                            .health = it->second.health,
                            .flags = it->second.flags});
      Log("street: handed vehicle {:#x} over to player {} (health {:.2f})", give->id, give->to,
          it->second.health);
      m_cars.erase(it);
      return true;
    }
    if (const auto *take = std::get_if<events::TrafficTake>(&event)) {
      const auto &state = take->state;
      m_cars[state.id] = {
          .position = state.position, .health = state.health, .slot = -1, .flags = state.flags};
      endpoint.AnswerTraffic(state.id, true);
      Log("street: took vehicle {:#x} over from player {} (health {:.2f}, flags {:#x}{})", state.id,
          state.from, state.health, state.flags,
          state.handover & proto::kHandoverInherited ? ", inherited" : "");
      return true;
    }
    if (const auto *owner = std::get_if<events::TrafficOwnerChanged>(&event)) {
      if (owner->was == endpoint.You() && m_cars.erase(owner->id) > 0)
        Log("street: vehicle {:#x} is no longer ours (now player {}'s)", owner->id, owner->owner);
      else
        Log("street: vehicle {:#x} is now run by player {} (was {})", owner->id, owner->owner,
            owner->was);
      return true;
    }
    if (const auto *hit = std::get_if<events::TrafficHitBy>(&event)) {
      const auto it = m_cars.find(hit->hit.id);
      if (it == m_cars.end())
        return false;
      it->second.health = std::max(0.f, it->second.health - hit->hit.damage / 1000.f);
      if (it->second.health <= 0.f)
        it->second.flags |= proto::kTrafficDestroyed;
      endpoint.SendTrafficStatus(
          {.id = hit->hit.id, .health = it->second.health, .flags = it->second.flags});
      Log("street: player {} hit our vehicle {:#x} for {:.0f}: health {:.2f}", hit->hit.player,
          hit->hit.id, hit->hit.damage, it->second.health);
      return true;
    }
    if (const auto *left = std::get_if<events::TrafficLeft>(&event);
        left && m_cars.erase(left->gone.id) > 0) {
      Log("street: our vehicle {:#x} gone (reason {}, player {})", left->gone.id, left->gone.reason,
          left->gone.player);
      return true;
    }
    if (const auto *owner = std::get_if<events::NpcOwnerChanged>(&event)) {
      if (owner->was == endpoint.You() && m_npcs.erase(owner->npc) > 0)
        Log("region: npc {} is no longer ours (now player {}'s)", owner->npc, owner->owner);
      else
        Log("region: npc {} is now run by player {} (was {})", owner->npc, owner->owner,
            owner->was);
      return true;
    }
    // Strikes and hits on the NPCs we run: health down, dead at zero.
    proto::NpcId hit = 0;
    proto::PlayerId by = 0;
    float damage = 0.f;
    if (const auto *plain = std::get_if<events::NpcHitBy>(&event)) {
      hit = plain->hit.npc;
      by = plain->hit.player;
      damage = plain->hit.damage;
    } else if (const auto *combat = std::get_if<events::Combat>(&event)) {
      if (const auto *strike = std::get_if<proto::NpcStrike>(&combat->message)) {
        hit = strike->npc;
        by = strike->player;
        damage = strike->damage;
      }
    }
    const auto it = m_npcs.find(hit);
    if (hit == 0 || it == m_npcs.end())
      return false;
    if (!m_options.regionImmortal.empty()) {
      // --region-immortal: at 1 % it stays; silent, a hit there says nothing.
      const auto was = it->second.health;
      it->second.health = std::max(0.01f, it->second.health - damage / 100.f);
      Log("region: player {} hit our npc {}: {:.0f} damage, health {:.2f}, it cannot die ({})", by,
          hit, damage, it->second.health, m_options.regionImmortal);
      if (ProtectedFlag() != 0 || it->second.health != was)
        endpoint.SendNpcHealth({.npc = hit, .health = it->second.health, .flags = ProtectedFlag()});
      return true;
    }
    it->second.health = std::max(0.f, it->second.health - damage / 100.f);
    Log("region: player {} hit our npc {}: {:.0f} damage, health {:.2f}", by, hit, damage,
        it->second.health);
    if (it->second.health <= 0.f) {
      endpoint.SendNpcGone({.npc = hit, .reason = proto::kGoneDead, .killer = by});
      Log("region: our npc {} died (killer: player {})", hit, by);
      m_npcs.erase(it);
    } else {
      endpoint.SendNpcHealth({.npc = hit, .health = it->second.health});
    }
    return true;
  }

  std::size_t Running() const { return m_npcs.size(); }

  std::uint8_t ProtectedFlag() const {
    return m_options.regionImmortal == "said" ? tandem::proto::kNpcProtected : std::uint8_t{0};
  }

private:
  struct Npc {
    Vec3 position;
    float health{1.f};
    int slot{-1}; // where it stands around us (-1: taken over, it stays put)
  };
  struct Car {
    Vec3 position;
    float health{1.f};
    int slot{-1}; // where it stands around us (-1: taken over, it stays put)
    std::uint16_t flags{};
  };
  // A ring 6 m around our V, off the NPCs' ring.
  static Vec3 CarAround(const Vec3 &at, int slot) {
    const float angle = 0.5f + 1.1f * static_cast<float>(slot);
    return {at.x + 6.f * std::cos(angle), at.y + 6.f * std::sin(angle), at.z};
  }
  std::map<tandem::proto::TrafficId, Car> m_cars;
  bool m_chased{}, m_carsAnnounced{};
  // A ring 3 m around our V.
  static Vec3 Around(const Vec3 &at, int slot) {
    const float angle = 0.9f * static_cast<float>(slot);
    return {at.x + 3.f * std::cos(angle), at.y + 3.f * std::sin(angle), at.z};
  }

  const Options &m_options;
  std::map<tandem::proto::NpcId, Npc> m_npcs;
  bool m_announced{};
  bool m_killed{};
  std::uint64_t m_startUs{};
  std::uint64_t m_lastStatesUs{};
};

// What the mock player does with the states it hears.
class Puppeteer {
public:
  explicit Puppeteer(const Options &options) : m_options(options) {}

  void Heard(const PlayerState &state) {
    m_heard.push_back({tandem::net::NowUs(), state});
    while (m_heard.size() > 512)
      m_heard.pop_front();
  }

  void HeardPose(const tandem::proto::PoseState &pose) {
    m_poses.push_back({tandem::net::NowUs(), pose});
    while (m_poses.size() > 256)
      m_poses.pop_front();
  }

  // The pose to send: the clip's, else (mirror, front) the newest heard at
  // least `delay` old.
  std::optional<tandem::proto::PoseState> NextPose(std::uint64_t nowUs, const PoseClip *clip) {
    if (clip)
      return clip->At(nowUs);
    // mirror and front show your own pose back (front: in view, facing you).
    if (m_options.mode != Mode::Mirror && m_options.mode != Mode::Front)
      return std::nullopt;
    const auto delayUs = static_cast<std::uint64_t>(m_options.delay.count()) * 1000;
    const tandem::proto::PoseState *pick = nullptr;
    for (const auto &[at, pose] : m_poses)
      if (at + delayUs <= nowUs)
        pick = &pose;
    if (!pick)
      return std::nullopt;
    return *pick;
  }

  // After a fast travel: stand there instead of the mode's motion, until
  // cleared (another V came to us).
  void StandAt(std::optional<Vec3> at) { m_station = at; }
  const std::optional<Vec3> &Station() const { return m_station; }
  // Where the other player's V was last heard (nullopt: not yet).
  std::optional<Vec3> HeardAt() const {
    if (m_heard.empty())
      return std::nullopt;
    return m_heard.back().second.position;
  }
  std::optional<Vec3> m_regionAnchor;
  std::uint64_t m_regionStartUs{}, m_regionLastUs{};
  float m_regionX{};
  bool m_regionLeaving{};
  std::uint64_t m_regionLinkedUs{}, m_regionArrivedUs{};
  // --path: the saved states to replay.
  void SetPath(std::vector<PlayerState> path) { m_path = std::move(path); }

  std::optional<PlayerState> Next(std::uint64_t nowUs) {
    // Standing by an NPC it grabs or carries (--grab-npcs, --carry-npcs).
    if (m_beside) {
      PlayerState out;
      out.position = {m_beside->x + 0.8f, m_beside->y, m_beside->z};
      out.orientation = tandem::sync::FromYaw(std::numbers::pi_v<float> / 2.f); // facing -X
      return out;
    }
    if (m_station) {
      PlayerState out;
      out.position = *m_station;
      return out;
    }
    switch (m_options.mode) {
    case Mode::Idle:
      return std::nullopt;
    case Mode::Course:
      return CourseState(nowUs);
    case Mode::Path:
      return PathState(nowUs);
    case Mode::Mirror:
      return Mirror(nowUs);
    case Mode::Ghost:
      return Ghost(nowUs);
    case Mode::Front:
      return Front(nowUs);
    case Mode::Drive:
      if (m_parkedAt)
        return Parked();
      return Drive(nowUs).first;
    case Mode::Metro:
      return Metro(nowUs);
    case Mode::Watch:
      return Watch();
    case Mode::Region:
      return Region(nowUs);
    }
    return std::nullopt;
  }

  // region: far from the other V (where it first stood), then up to it, then
  // off again. A straight walk along X at --region-speed.
  std::optional<PlayerState> Region(std::uint64_t nowUs) {
    if (m_heard.empty() && !m_options.regionAt)
      return std::nullopt;
    if (!m_regionAnchor) {
      m_regionAnchor = m_options.regionAt ? *m_options.regionAt : m_heard.back().second.position;
      m_regionStartUs = m_regionLastUs = nowUs;
      m_regionX = m_options.regionFar;
    }
    const auto seconds = static_cast<double>(nowUs - m_regionStartUs) / 1e6;
    // Once leaving, for good (walking off unlinks us).
    const bool wasLeaving = m_regionLeaving;
    m_regionLeaving =
        m_regionLeaving ||
        (m_options.regionLeaveAtSeconds >= 0 && seconds >= m_options.regionLeaveAtSeconds) ||
        (m_options.regionLeaveLinkedSeconds >= 0 && m_regionLinkedUs != 0 &&
         m_regionArrivedUs != 0 &&
         nowUs - std::max(m_regionLinkedUs, m_regionArrivedUs) >=
             static_cast<std::uint64_t>(m_options.regionLeaveLinkedSeconds) * 1'000'000);
    if (m_regionLeaving && !wasLeaving)
      Log("region: walking off, leaving our NPCs where they are");
    const bool near = !m_regionLeaving && m_options.regionWalkAtSeconds >= 0 &&
                      seconds >= m_options.regionWalkAtSeconds;
    const float goal = near ? std::min(8.f, m_options.regionFar) : m_options.regionFar;
    const float step = m_options.regionSpeed * static_cast<float>(nowUs - m_regionLastUs) / 1e6f;
    m_regionLastUs = nowUs;
    const float before = m_regionX;
    if (m_regionX > goal)
      m_regionX = std::max(goal, m_regionX - step);
    else
      m_regionX = std::min(goal, m_regionX + step);
    // Up by the other V (--region-leave-linked counts from then).
    if (near && m_regionX == goal && m_regionArrivedUs == 0)
      m_regionArrivedUs = nowUs;
    PlayerState out;
    out.position = {m_regionAnchor->x + m_regionX, m_regionAnchor->y, m_regionAnchor->z};
    if (m_regionX != before)
      out.velocity = {m_regionX < before ? -m_options.regionSpeed : m_options.regionSpeed, 0.f,
                      0.f};
    // Facing the other V (-X).
    out.orientation = tandem::sync::FromYaw(std::numbers::pi_v<float> / 2.f);
    return out;
  }
  // region: we are walking off (our NPCs stay where they are).
  bool RegionLeaving() const { return m_regionLeaving; }
  // region: our endpoint says we are linked with someone (or no longer).
  void SetRegionLinked(bool linked, std::uint64_t nowUs) {
    if (!linked)
      m_regionLinkedUs = 0;
    else if (m_regionLinkedUs == 0)
      m_regionLinkedUs = nowUs;
  }

  // watch: where the other V is, 30 m under it (or --offset): out of
  // sight, out of every NPC's reach, never in anyone's way, while the host
  // sends us everything a guest gets (--record-session).
  std::optional<PlayerState> Watch() {
    if (m_heard.empty())
      return std::nullopt;
    auto out = m_heard.back().second;
    const Vec3 by = m_options.offsetGiven ? m_options.offset : Vec3{0.f, 0.f, -30.f};
    out.position = {out.position.x + by.x, out.position.y + by.y, out.position.z + by.z};
    out.stance = 0;
    out.flags = 0;
    return out;
  }

  // metro: on the train from now on (once).
  void Board(std::uint64_t nowUs) {
    if (!m_boardUs)
      m_boardUs = nowUs;
  }

  // metro: 0 waiting on the platform, 1 riding, 2 skipping (ten times
  // faster), 3 off at the exit station; `secondHalf` past the skip.
  int MetroStep(std::uint64_t nowUs, bool *secondHalf = nullptr) const {
    if (!m_boardUs || nowUs < *m_boardUs)
      return 0;
    const float t = static_cast<float>(nowUs - *m_boardUs) / 1e6f;
    const float ride = static_cast<float>(m_options.metroForSeconds), half = ride / 2.f;
    if (secondHalf)
      *secondHalf = t >= half + kSkipSeconds;
    if (t >= ride)
      return 3;
    if (t >= half && t < half + kSkipSeconds)
      return 2;
    return 1;
  }

private:
  std::optional<PlayerState> Mirror(std::uint64_t nowUs) {
    // The newest state at least `delay` old.
    const auto delayUs = static_cast<std::uint64_t>(m_options.delay.count()) * 1000;
    const PlayerState *pick = nullptr;
    for (const auto &[at, state] : m_heard)
      if (at + delayUs <= nowUs)
        pick = &state;
    if (!pick)
      return std::nullopt;
    auto out = *pick;
    out.position.x += m_options.offset.x;
    out.position.y += m_options.offset.y;
    out.position.z += m_options.offset.z;
    return out;
  }

  std::optional<PlayerState> Ghost(std::uint64_t nowUs) {
    constexpr float kRadius = 3.f;
    constexpr float kSpeed = 2.f; // m/s along the circle
    const Vec3 center = m_heard.empty() ? Vec3{} : m_heard.back().second.position;
    const float angle = static_cast<float>(nowUs / 1e6) * kSpeed / kRadius;

    PlayerState out;
    out.position = {center.x + kRadius * std::cos(angle), center.y + kRadius * std::sin(angle),
                    center.z};
    out.velocity = {-kSpeed * std::sin(angle), kSpeed * std::cos(angle), 0.f};
    // Facing along the circle: yaw around Z (the game's up axis).
    const float yaw = angle + std::numbers::pi_v<float> / 2.f;
    out.orientation = {0.f, 0.f, std::sin(yaw / 2.f), std::cos(yaw / 2.f)};
    return out;
  }

public:
  // A car going round a 12 m circle (--drive-radius) at 8 m/s around where
  // your V was first heard (fixed, so you can watch it pass): the driver's
  // state, and the car's.
  std::pair<std::optional<PlayerState>, std::optional<tandem::proto::VehicleState>>
  Drive(std::uint64_t nowUs) {
    if (m_heard.empty())
      return {};
    if (!m_center)
      m_center = m_heard.back().second.position;
    const float kRadius = m_options.driveRadius;
    constexpr float kSpeed = 8.f;
    const float angle = static_cast<float>(nowUs / 1e6) * kSpeed / kRadius;
    const Vec3 at{m_center->x + kRadius * std::cos(angle), m_center->y + kRadius * std::sin(angle),
                  m_center->z};
    const Vec3 velocity{-kSpeed * std::sin(angle), kSpeed * std::cos(angle), 0.f};
    // The game's forward is +Y turned by yaw: facing along the circle.
    const auto facing = tandem::sync::FromYaw(angle);
    PlayerState body;
    body.position = at;
    body.velocity = velocity;
    body.orientation = facing;
    tandem::proto::VehicleState car;
    car.position = at;
    car.orientation = facing;
    car.velocity = velocity;
    return {body, car};
  }

  void Park(const Vec3 &at) { m_parkedAt = at; }
  void Unpark() { m_parkedAt.reset(); }
  // drive: the circle's centre (where your V was first heard; none yet).
  std::optional<Vec3> Center() {
    if (!m_center && !m_heard.empty())
      m_center = m_heard.back().second.position;
    return m_center;
  }
  void Beside(std::optional<Vec3> at) { m_beside = at; }

private:
  std::optional<Vec3> m_center;
  std::optional<Vec3> m_parkedAt;
  std::optional<Vec3> m_beside;
  std::optional<Vec3> m_station;

  // metro: a straight line from the platform (3 m to the right of where
  // your V was first heard, along its facing), 8 m up while riding, at
  // 20 m/s, 200 m/s while skipping.
  static constexpr float kMetroSpeed = 20.f, kSkipSeconds = 3.f, kSkipFactor = 10.f, kMetroUp = 8.f;
  std::optional<std::uint64_t> m_boardUs;
  std::optional<Vec3> m_platform;
  float m_lineYaw{};

  float MetroDistance(float t) const {
    const float ride = static_cast<float>(m_options.metroForSeconds), half = ride / 2.f;
    t = std::clamp(t, 0.f, ride);
    if (t < half)
      return kMetroSpeed * t;
    const float skip = std::min(t - half, kSkipSeconds);
    return kMetroSpeed * half + kMetroSpeed * kSkipFactor * skip +
           kMetroSpeed * std::max(0.f, t - half - kSkipSeconds);
  }

  std::optional<PlayerState> Metro(std::uint64_t nowUs) {
    if (m_heard.empty())
      return std::nullopt;
    if (!m_platform) {
      const auto &them = m_heard.back().second;
      m_lineYaw = tandem::sync::YawOf(them.orientation);
      // The game's right is (cos, sin) of the yaw.
      m_platform = Vec3{them.position.x + 3.f * std::cos(m_lineYaw),
                        them.position.y + 3.f * std::sin(m_lineYaw), them.position.z};
    }
    const int step = MetroStep(nowUs);
    const float t =
        m_boardUs && nowUs > *m_boardUs ? static_cast<float>(nowUs - *m_boardUs) / 1e6f : 0.f;
    const float along = step == 0 ? 0.f : MetroDistance(t);
    // The game's forward is (-sin, cos) of the yaw.
    const float fx = -std::sin(m_lineYaw), fy = std::cos(m_lineYaw);
    const float speed = step == 1 ? kMetroSpeed : step == 2 ? kMetroSpeed * kSkipFactor : 0.f;
    PlayerState out;
    out.position = {m_platform->x + fx * along, m_platform->y + fy * along,
                    m_platform->z + (step == 1 || step == 2 ? kMetroUp : 0.f)};
    out.velocity = {fx * speed, fy * speed, 0.f};
    out.orientation = tandem::sync::FromYaw(m_lineYaw);
    return out;
  }

  std::optional<PlayerState> Parked() const {
    PlayerState out;
    out.position = {m_parkedAt->x + 2.f, m_parkedAt->y, m_parkedAt->z};
    return out;
  }

  // The course, laid out ahead of where your V first stood, facing the way it faced.
  std::optional<PlayerState> CourseState(std::uint64_t nowUs) {
    if (m_heard.empty())
      return std::nullopt;
    if (!m_courseStartUs) {
      const auto &them = m_heard.back().second;
      m_courseStartUs = nowUs;
      m_courseYaw = tandem::sync::YawOf(them.orientation);
      m_courseOrigin = {them.position.x + m_options.offset.x, them.position.y + m_options.offset.y,
                        them.position.z + m_options.offset.z};
    }
    const float t = static_cast<float>(nowUs - *m_courseStartUs) / 1e6f;
    // The game's forward is +Y turned by yaw: (-sin, cos). 3 m ahead of your V.
    const float fx = -std::sin(m_courseYaw), fy = std::cos(m_courseYaw);
    const auto place = [&](const CoursePoint &p) {
      const float ahead = 3.f + p.ahead;
      return Vec3{m_courseOrigin.x + fx * ahead, m_courseOrigin.y + fy * ahead,
                  m_courseOrigin.z + p.up};
    };
    const auto point = m_course.At(t);
    const auto at = place(point);
    const auto next = place(m_course.At(t + 0.02f));
    if (point.what != m_courseWhat) {
      m_courseWhat = point.what;
      Log("course: {} ({})", point.what,
          tandem::movement::NameOf(tandem::movement::PlacementOf(point.movement)));
    }
    PlayerState out;
    out.position = at;
    // (the lap's end jumps back to the start: no velocity across it)
    if (std::hypot(next.x - at.x, next.y - at.y, next.z - at.z) < 1.f)
      out.velocity = {(next.x - at.x) / 0.02f, (next.y - at.y) / 0.02f, (next.z - at.z) / 0.02f};
    out.orientation = tandem::sync::FromYaw(m_courseYaw);
    out.stance = point.stance;
    out.flags = tandem::movement::Pack(point.movement);
    return out;
  }

  // A saved path at its own pace, in a loop, moved by --offset.
  std::optional<PlayerState> PathState(std::uint64_t nowUs) {
    if (m_path.size() < 2)
      return std::nullopt;
    if (!m_pathStartUs)
      m_pathStartUs = nowUs;
    const auto first = m_path.front().timeUs, span = m_path.back().timeUs - first;
    const auto at = first + (nowUs - *m_pathStartUs) % std::max<std::uint64_t>(span, 1);
    while (m_pathNext + 1 < m_path.size() && m_path[m_pathNext + 1].timeUs <= at)
      ++m_pathNext;
    if (m_path[m_pathNext].timeUs > at)
      m_pathNext = 0; // a new lap
    auto out = m_path[m_pathNext];
    out.position.x += m_options.offset.x;
    out.position.y += m_options.offset.y;
    out.position.z += m_options.offset.z;
    if (const auto placement = tandem::movement::PlacementOf(out.flags);
        placement != m_pathPlacement) {
      m_pathPlacement = placement;
      Log("path: {}", tandem::movement::NameOf(placement));
    }
    return out;
  }

  std::optional<PlayerState> Front(std::uint64_t nowUs) {
    if (m_heard.empty())
      return std::nullopt;
    constexpr float kAhead = 3.f, kSway = 2.f, kRate = 0.6f; // m, m, rad/s
    const auto &them = m_heard.back().second;
    const float yaw = tandem::sync::YawOf(them.orientation);
    // The game's forward is +Y turned by yaw: (-sin, cos); right is (cos, sin).
    const float fx = -std::sin(yaw), fy = std::cos(yaw);
    const float rx = std::cos(yaw), ry = std::sin(yaw);
    const float t = static_cast<float>(nowUs % 600'000'000ULL) / 1e6f;
    const float side = kSway * std::sin(t * kRate);
    const float sideSpeed = kSway * kRate * std::cos(t * kRate);

    PlayerState out;
    out.position = {them.position.x + fx * kAhead + rx * side,
                    them.position.y + fy * kAhead + ry * side, them.position.z};
    out.velocity = {rx * sideSpeed, ry * sideSpeed, 0.f};
    out.orientation = tandem::sync::FromYaw(yaw + std::numbers::pi_v<float>); // facing them
    // Crouched 4 s out of every 8 (gamePSMLocomotionStates::Crouch = 1).
    out.stance = (static_cast<int>(t) % 8) < 4 ? 1 : 0;
    return out;
  }

  const Options &m_options;
  Course m_course;
  std::optional<std::uint64_t> m_courseStartUs;
  float m_courseYaw{};
  Vec3 m_courseOrigin;
  const char *m_courseWhat{};
  std::vector<PlayerState> m_path;
  std::optional<std::uint64_t> m_pathStartUs;
  std::size_t m_pathNext{};
  tandem::movement::Placement m_pathPlacement{};
  std::deque<std::pair<std::uint64_t, PlayerState>> m_heard;
  std::deque<std::pair<std::uint64_t, tandem::proto::PoseState>> m_poses;
};
} // namespace

using tandem::sim::LoadLookKeys;
using tandem::sim::LookKeysDir;
using tandem::sim::LookKeysFile;

void SaveLookKeys(const std::filesystem::path &file,
                  const std::vector<tandem::proto::AppearanceKey> &keys) {
  if (keys.empty() || LoadLookKeys(file) == keys)
    return;
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  std::ofstream out(file, std::ios::trunc);
  for (const auto &key : keys)
    out << std::format("{:016x} {:016x}\n", key.resource, key.definition);
  if (out)
    Log("look: {} body keys saved to {}", keys.size(), file.string());
}

// A look of our own (tandem/sim/looks.hpp), logged.
tandem::proto::Appearance
RandomLook(tandem::proto::Body body, unsigned seed, std::vector<tandem::proto::AppearanceKey> keys,
           const std::vector<std::pair<std::string, tandem::proto::RecordHint>> &extra = {}) {
  std::string picked;
  auto look = tandem::sim::RandomLook(body, seed, std::move(keys), extra, &picked);
  Log("our look: random (seed {}), {} body keys:{}", seed, look.look.size(), picked);
  return look;
}

// --strike: one hit on the host's NPC, as the game sends it (Outcomes.reds).
tandem::proto::NpcStrike StrikeOf(std::string_view kind, tandem::proto::NpcId npc) {
  namespace proto = tandem::proto;
  proto::NpcStrike strike{.npc = npc,
                          .damage = 35.f,
                          .attackType = 10, // Ranged
                          .zone = 1,        // ChestLeft
                          .shape = 0,       // Flesh
                          .direction = {0.f, 1.f, 0.f}};
  if (kind == "nonlethal") {
    strike.flags = proto::kStrikeNonlethal | proto::kStrikeMelee;
    strike.attackType = 7; // Melee
    strike.zone = 7;       // Abdomen
  } else if (kind == "cap") {
    strike.flags = proto::kStrikeLeaveOneHp;
    strike.damage = 34.f;
  } else if (kind == "poise") {
    strike.flags = proto::kStrikeMelee;
    strike.attackType = 12; // StrongMelee
    strike.damage = 10.f;
    strike.poise = 40.f;
  } else if (kind == "head") {
    strike.flags = proto::kStrikeWeakSpot | proto::kStrikeHeadshot | proto::kStrikeCritical;
    strike.zone = 0; // Head
    strike.part = 4; // HEAD
  } else if (kind == "blade") {
    strike.flags = proto::kStrikeMelee | proto::kStrikeForceDismember;
    strike.attackType = 7;
    strike.zone = 2; // ArmLeft
    strike.part = 0; // LEFT_ARM
  } else if (kind == "offhand") {
    // A dual wielding V's left hand's shot: a plain hit, its hand said.
    strike.flags = proto::kStrikeLeftHand;
  }
  return strike;
}

// "primary ff0000, secondary 00ff00, lights hue 120", "stock".
std::string PaintText(const tandem::proto::CarPaint &paint) {
  namespace proto = tandem::proto;
  if (paint.flags == 0)
    return "stock";
  if ((paint.flags & proto::kPaintUnique) != 0)
    return std::format("unique template {:#x} (model {:#x})", paint.unique, paint.model);
  std::string out;
  const auto add = [&](std::string part) { out += (out.empty() ? "" : ", ") + part; };
  if ((paint.flags & proto::kPaintPrimary) != 0)
    add(std::format("primary {:02x}{:02x}{:02x}", paint.primaryR, paint.primaryG, paint.primaryB));
  if ((paint.flags & proto::kPaintSecondary) != 0)
    add(std::format("secondary {:02x}{:02x}{:02x}", paint.secondaryR, paint.secondaryG,
                    paint.secondaryB));
  if ((paint.flags & proto::kPaintLights) != 0)
    add(std::format("lights hue {:.0f}", paint.lightsHue));
  return out;
}

std::string DescribeCombat(const tandem::proto::Message &message) {
  namespace proto = tandem::proto;
  // (tools/scenario.sh reads this line: a game that runs without something
  // a scenario did not declare fails it.)
  if (const auto *degraded = std::get_if<proto::Degraded>(&message))
    return std::format("degraded: player {}'s game runs without: {}", degraded->player,
                       tandem::degraded::Describe(*degraded));
  if (const auto *paint = std::get_if<proto::CarPaint>(&message))
    return std::format("player {}'s car is painted: {} (car {:#x})", paint->player,
                       PaintText(*paint), paint->vehicle);
  if (const auto *car = std::get_if<proto::OwnCar>(&message))
    return car->vehicle == 0
               ? std::format("player {}'s summoned car: none out", car->player)
               : std::format("player {}'s summoned car {:#x}: {} at ({:.1f}, {:.1f}, {:.1f})",
                             car->player, car->vehicle,
                             (car->flags & proto::kOwnCarMoving) ? "driving in" : "parked",
                             car->position.x, car->position.y, car->position.z);
  if (const auto *s = std::get_if<proto::NpcStrike>(&message))
    return std::format("npc {} struck by player {}: {:.0f} health, {:.0f} poise, flags {:#x}, "
                       "zone {}, part {}",
                       s->npc, s->player, s->damage, s->poise, s->flags, s->zone, s->part);
  if (const auto *w = std::get_if<proto::NpcWound>(&message))
    return std::format("npc {} loses part {} (wound {}, player {}'s hit)", w->npc, w->part,
                       w->wound, w->player);
  if (const auto *p = std::get_if<proto::NpcPoise>(&message))
    return std::format("npc {} poise {:.0f}%", p->npc, p->poise * 100.f);
  if (const auto *rest = std::get_if<proto::CorpsePose>(&message))
    return std::format("npc {}'s body at rest at ({:.1f}, {:.1f}, {:.1f}), {} bodies", rest->npc,
                       rest->position.x, rest->position.y, rest->position.z, rest->bodies.size());
  if (const auto *d = std::get_if<proto::Difficulty>(&message))
    return std::format("the session plays at difficulty {}", d->level);
  if (const auto *a = std::get_if<proto::AreaHit>(&message))
    return std::format("player {}'s area effect hits us: {:.0f} physical, {:.0f} thermal (type {})",
                       a->from, a->physical, a->thermal, a->attackType);
  if (const auto *t = std::get_if<proto::Throw>(&message))
    return std::format("player {} throws {} (kind {}, flags {:#x}, npc {}, at player {})",
                       t->player, t->id, static_cast<int>(t->kind), t->flags, t->npc, t->at);
  if (const auto *path = std::get_if<proto::ThrowPath>(&message))
    return std::format("player {}'s throw {} is at ({:.1f} {:.1f} {:.1f}), {:.1f} m/s",
                       path->player, path->id, path->position.x, path->position.y, path->position.z,
                       std::hypot(path->velocity.x, path->velocity.y, path->velocity.z));
  if (const auto *e = std::get_if<proto::ThrowEnd>(&message))
    return std::format("player {}'s throw {} ends ({})", e->player, e->id,
                       static_cast<int>(e->how));
  if (const auto *r = std::get_if<proto::Rules>(&message))
    return std::format("the session's rules: friendly fire {}, enemy scaling {} (health +{} % "
                       "per extra player), level gap {}",
                       (r->flags & proto::kRuleFriendlyFire) ? "on" : "off",
                       (r->flags & proto::kRuleEnemyScaling) ? "on" : "off", r->enemyHealth,
                       r->levelGap);
  if (const auto *s = std::get_if<proto::NpcScale>(&message))
    return std::format("npc {} is scaled for {} players", s->npc, s->players);
  if (const auto *d = std::get_if<proto::Defended>(&message))
    return std::format("player {} {} npc {}'s attack", d->player,
                       d->how == proto::DefendKind::Parried ? "parried" : "blocked", d->npc);
  if (const auto *w = std::get_if<proto::WeakSpotHit>(&message))
    return std::format("player {}'s fire took {:.0f}% of weak spot {} ({:#x}) of npc {}", w->player,
                       w->drain, w->index, w->record, w->npc);
  if (const auto *t = std::get_if<proto::HackTraced>(&message))
    return std::format("npc {}'s trace of player {} ran to its end: its squad is set on them",
                       t->npc, t->player);
  if (const auto *s = std::get_if<proto::SessionLevel>(&message))
    return std::format("the session's level is {} ({} players)", s->level, s->players);
  if (const auto *n = std::get_if<proto::NpcLevel>(&message))
    return std::format("npc {} is level {:.1f}", n->npc, n->level);
  if (const auto *v = std::get_if<proto::NpcVendor>(&message))
    return (v->flags & proto::kVendorDead) != 0
               ? std::format("the vendor {:x} died in the session", v->entity)
               : std::format("npc {} is the placed vendor {:x}", v->npc, v->entity);
  if (const auto *placed = std::get_if<proto::NpcPlaced>(&message))
    return std::format("npc {} is the placed npc {:x}", placed->npc, placed->entity);
  if (const auto *death = std::get_if<proto::NpcDeath>(&message))
    return std::format(
        "ledger: placed npc {:x} {} in player {}'s game at ({:.1f}, {:.1f}, {:.1f}), "
        "death {} of the session",
        death->entity, (death->flags & proto::kDeathKnockedOut) != 0 ? "was knocked out" : "died",
        death->by, death->position.x, death->position.y, death->position.z, death->order);
  if (const auto *l = std::get_if<proto::CarLend>(&message)) {
    constexpr const char *names[] = {"crew", "owner", "never"};
    return std::format("player {} lends its cars: {}", l->player,
                       names[static_cast<std::size_t>(l->lend) % 3]);
  }
  return proto::NameOf(message);
}

// tandem-peer play-info FILE: what a recording holds, where it starts, and
// the teleport that puts a V beside its start (tools/scenario.sh play).
int PlayInfo(const std::string &path) {
  std::string error;
  const auto recording = tandem::play::Load(path, error);
  if (!recording) {
    std::cerr << "tandem-peer: " << error << '\n';
    return 1;
  }
  std::map<std::string, int> counts;
  for (const auto &entry : recording->entries)
    ++counts[tandem::proto::NameOf(entry.message)];
  std::string what;
  for (const auto &[name, count] : counts)
    what += std::format("{}{} {}", what.empty() ? "" : ", ", count, name);
  const auto &h = recording->header;
  if (h.kind == tandem::play::Kind::Session)
    std::cout << std::format("session: host {}, recorded by player {}\n", h.host, h.player);
  std::cout << std::format(
      "play: {:.1f} s, {} messages ({}), player {}, body {}{}\n",
      static_cast<double>(recording->DurationUs()) / 1e6, recording->entries.size(), what, h.player,
      h.body == tandem::proto::Body::Male ? "male" : "female",
      recording->skipped ? std::format(", {} of another protocol skipped", recording->skipped)
                         : "");
  // A session's start is its host's V.
  std::optional<tandem::proto::PlayerState> first;
  for (const auto &entry : recording->entries)
    if (const auto *state = std::get_if<tandem::proto::PlayerState>(&entry.message);
        state && (h.kind == tandem::play::Kind::Play || state->player == h.host)) {
      first = *state;
      break;
    }
  if (!first)
    return 0;
  constexpr float kDegrees = 180.f / std::numbers::pi_v<float>;
  const float yaw = tandem::sync::YawOf(first->orientation);
  std::cout << std::format("start: {:.2f} {:.2f} {:.2f} {:.1f}\n", first->position.x,
                           first->position.y, first->position.z, yaw * kDegrees);
  // 3 m ahead of where it starts (the game's forward: +Y turned by yaw),
  // facing it: the replayed V comes into view at once.
  const float ahead = 3.f;
  std::cout << std::format(
      "beside: teleport {:.2f} {:.2f} {:.2f} {:.1f}\n", first->position.x - std::sin(yaw) * ahead,
      first->position.y + std::cos(yaw) * ahead, first->position.z, yaw * kDegrees + 180.f);
  return 0;
}

// tandem-peer mods PATH: what tandem makes of a game's other mods
// (docs/architecture/mod-compat.md): a game folder (names and contents, as
// the plugin reads them at startup) or a list of file names (a .tsv,
// tests/data/mods-sample.tsv: names alone). A table, one mod a row.
int Mods(std::string_view path) {
  namespace mods = tandem::mods;
  std::vector<mods::Entry> entries;
  if (std::filesystem::is_directory(path)) {
    entries = mods::ScanEntries(path);
  } else {
    std::ifstream in{std::string(path)};
    if (!in) {
      std::cerr << std::format("mods: cannot read {}\n", path);
      return 1;
    }
    std::stringstream text;
    text << in.rdbuf();
    entries = mods::EntriesOfList(text.str());
  }
  const auto all = mods::Group(entries);
  std::array<int, mods::kCategoryOrder.size()> by{};
  std::cout << "| Mod | Counted as | Also |\n| --- | --- | --- |\n";
  for (const auto &mod : all) {
    const auto primary = mods::Primary(mod.categories);
    std::string also;
    for (const auto category : mods::kCategoryOrder)
      if ((mod.categories & category) && category != primary)
        also += std::format("{}{}", also.empty() ? "" : ", ", mods::CategoryName(category));
    std::cout << std::format("| {} | {} | {} |\n", mod.name, mods::CategoryName(primary), also);
    for (std::size_t i = 0; i < by.size(); ++i)
      by[i] += mods::kCategoryOrder[i] == primary;
  }
  std::cout << std::format("\n{} mods from {} entries:", all.size(), entries.size());
  for (std::size_t i = 0; i < by.size(); ++i)
    if (by[i])
      std::cout << std::format(" {} {}", by[i], mods::CategoryName(mods::kCategoryOrder[i]));
  std::cout << "\n";
  return 0;
}

// tandem-peer traffic FILE...: what a recording would take on the wire, as
// its messages were (legacy) and as the netcode sends them now (compact:
// delta-coded poses, NPC changes, compact states, a still V 5 times a
// second; no relevance: the other player is unknown). Datagrams
// are what goes within 4 ms (ENet sends what is queued at each service),
// each with IP, UDP and ENet headers (32 bytes) and 8 bytes a message.
int Traffic(const std::vector<std::string> &paths) {
  namespace proto = tandem::proto;
  constexpr double kDatagramBytes = 32, kCommandBytes = 8;
  for (const auto &path : paths) {
    std::string error;
    std::vector<std::pair<std::uint64_t, proto::Message>> entries;
    if (auto recording = tandem::play::Load(path, error))
      for (auto &entry : recording->entries)
        entries.emplace_back(entry.atUs, std::move(entry.message));
    else
      for (auto &entry : Recording::Load(path))
        entries.emplace_back(entry.atUs, std::move(entry.message));
    if (entries.size() < 2) {
      std::cerr << "tandem-peer: " << path << ": nothing to measure " << error << '\n';
      continue;
    }
    const double seconds = static_cast<double>(entries.back().first - entries.front().first) / 1e6;
    struct Way {
      std::map<std::size_t, std::pair<double, double>> kinds; // bytes, messages
      double datagrams{}, bytes{};
      std::uint64_t lastUs{};
      bool any{};
      void Add(std::uint64_t at, const proto::Message &message) {
        const auto size = static_cast<double>(proto::Encode(message).size());
        auto &kind = kinds[message.index()];
        kind.first += size;
        kind.second += 1;
        bytes += size + kCommandBytes;
        if (!any || at - lastUs > 4'000) {
          datagrams += 1;
          lastUs = at;
          any = true;
        }
      }
      double Kbps(double seconds) const {
        return (bytes + datagrams * kDatagramBytes) * 8 / 1000 / seconds;
      }
    } legacy, compact;
    tandem::pose::PoseEncoder poses;
    tandem::netcode::NpcCoder npcs;
    tandem::netcode::StillGate<proto::PlayerState> states;
    tandem::netcode::StillGate<proto::VehicleState> cars;
    std::uint32_t poseSeq = 0, npcSeq = 0;
    constexpr std::uint64_t kBase = 1'800'000'000'000'000;
    for (const auto &[at, message] : entries) {
      legacy.Add(at, message);
      if (auto pose = std::get_if<proto::PoseState>(&message)) {
        auto stamped = *pose;
        stamped.seq = ++poseSeq;
        stamped.timeUs = kBase + at;
        compact.Add(at, poses.Encode(stamped));
      } else if (auto list = std::get_if<proto::NpcStates>(&message)) {
        auto stamped = *list;
        stamped.seq = ++npcSeq;
        stamped.timeUs = kBase + at;
        if (const auto motion = npcs.Encode(stamped))
          compact.Add(at, *motion);
        else
          compact.Add(at, stamped);
      } else if (auto state = std::get_if<proto::PlayerState>(&message)) {
        auto stamped = *state;
        stamped.timeUs = kBase + at;
        for (const auto &send : states.Offer(stamped, 0))
          compact.Add(at, tandem::netcode::Compact(send));
      } else if (auto car = std::get_if<proto::VehicleState>(&message)) {
        auto stamped = *car;
        stamped.timeUs = kBase + at;
        for (const auto &send : cars.Offer(stamped, 0))
          compact.Add(at, tandem::netcode::Compact(send));
      } else {
        compact.Add(at, message);
      }
    }
    std::cout << std::format(
        "{}: {:.1f} s, {} messages: legacy {:.1f} kbit/s ({:.0f} datagrams/s), "
        "compact {:.1f} kbit/s ({:.0f}/s)\n",
        path, seconds, entries.size(), legacy.Kbps(seconds), legacy.datagrams / seconds,
        compact.Kbps(seconds), compact.datagrams / seconds);
    std::map<std::string, std::pair<double, double>> rows; // legacy, compact kbit/s
    for (const auto &[kind, totals] : legacy.kinds)
      rows[proto::NameOfKind(kind)].first += totals.first * 8 / 1000 / seconds;
    for (const auto &[kind, totals] : compact.kinds)
      rows[proto::NameOfKind(kind)].second += totals.first * 8 / 1000 / seconds;
    for (const auto &[name, kbps] : rows)
      if (kbps.first + kbps.second >= 0.05)
        std::cout << std::format("  {:<14} legacy {:7.2f}  compact {:7.2f} kbit/s\n", name,
                                 kbps.first, kbps.second);
    std::cout << std::format("  pose: {} keys of {} frames\n", poses.Keys(), poses.Frames());
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 3 && std::string_view(argv[1]) == "play-info")
    return PlayInfo(argv[2]);
  // The simulated second game (docs/architecture/sim.md).
  if (argc >= 2 && (std::string_view(argv[1]) == "sim" || std::string_view(argv[1]) == "sim-fuzz"))
    return tandem::sim::SimMain(argv[1], std::vector<std::string_view>(argv + 2, argv + argc));
  if (argc >= 3 && std::string_view(argv[1]) == "traffic")
    return Traffic(std::vector<std::string>(argv + 2, argv + argc));
  if (argc >= 3 && std::string_view(argv[1]) == "mods")
    return Mods(argv[2]);
  auto options = Parse(argc, argv);
  if (options.check) {
    std::cout << "options ok\n";
    return 0;
  }
  // --play-session: a real host's session instead of the mode's motion and
  // --replay (its V's body: the rig the guest gives our puppet).
  std::optional<tandem::play::Player> sessionPlay;
  if (!options.playSessionFile.empty()) {
    std::string sessionError;
    auto recording = tandem::play::Load(options.playSessionFile, sessionError);
    if (recording && recording->header.kind != tandem::play::Kind::Session) {
      sessionError = options.playSessionFile + ": a play, not a session (--play)";
      recording.reset();
    }
    if (recording && options.host) {
      for (const auto &entry : recording->entries)
        if (const auto *look = std::get_if<tandem::proto::Appearance>(&entry.message);
            look && look->player == recording->header.host &&
            look->body != tandem::proto::Body::Unknown) {
          options.body = look->body;
          break;
        }
      Log("session: {} ({:.1f} s, {} messages{}){}", options.playSessionFile,
          static_cast<double>(recording->DurationUs()) / 1e6, recording->entries.size(),
          recording->skipped ? std::format(", {} skipped", recording->skipped) : "",
          options.playLoop ? ", in a loop" : "");
      options.mode = Mode::Idle;
      options.replayFile.clear();
      sessionPlay.emplace(std::move(*recording), options.playLoop);
    } else {
      Log("session: {}: the mode's own host instead",
          recording ? "--play-session is for a host" : sessionError);
    }
  }
  // --play: a real V's play instead of the mode's motion (its body, its look).
  std::optional<tandem::play::Player> playback;
  if (!options.playFile.empty()) {
    std::string playError;
    if (auto recording = tandem::play::Load(options.playFile, playError)) {
      Log("play: {} ({:.1f} s, {} messages{}){}", options.playFile,
          static_cast<double>(recording->DurationUs()) / 1e6, recording->entries.size(),
          recording->skipped ? std::format(", {} skipped", recording->skipped) : "",
          options.playLoop ? ", in a loop" : "");
      if (recording->header.body != tandem::proto::Body::Unknown)
        options.body = recording->header.body;
      options.mode = Mode::Idle;
      playback.emplace(std::move(*recording), options.playLoop);
    } else {
      Log("play: {}: the mode's own motion instead", playError);
    }
  }
  std::signal(SIGINT, [](int) { g_stop = 1; });
  std::signal(SIGTERM, [](int) { g_stop = 1; });

  std::string error;
  // A guest with a code but no address goes through the relay only.
  const tandem::JoinTarget target{.address = options.address,
                                  .port = options.port,
                                  .relay = options.code.empty() ? std::nullopt : options.relay};
  // A bad network: the shaper takes the public side of the direct link
  // (host: the port the others join; guest: a local port the endpoint joins),
  // the endpoint sits behind it on loopback. The relay path is not shaped.
  std::unique_ptr<tandem::net::Shaper> shaper;
  std::unique_ptr<tandem::Endpoint> endpoint;
  if (options.link.Active() && (options.host || !target.address.empty())) {
    if (options.host) {
      endpoint = tandem::Endpoint::Host({options.name, options.body}, 0, error, {}, options.relay);
      if (endpoint)
        shaper = tandem::net::Shaper::Start(options.port, "127.0.0.1", endpoint->LocalPort(),
                                            options.link, error);
    } else {
      shaper = tandem::net::Shaper::Start(0, target.address, target.port, options.link, error);
      if (shaper) {
        auto shaped = target;
        shaped.address = "127.0.0.1";
        shaped.port = shaper->Port();
        endpoint = tandem::Endpoint::Join({options.name, options.body}, shaped, error);
      }
    }
    if (!shaper)
      endpoint.reset();
  } else {
    endpoint = options.host ? tandem::Endpoint::Host({options.name, options.body}, options.port,
                                                     error, {}, options.relay)
                            : tandem::Endpoint::Join({options.name, options.body}, target, error);
  }
  if (!endpoint) {
    std::cerr << "tandem-peer: " << error << '\n';
    return 1;
  }
  // Comms: our consent, from the start (the plugin does the same).
  tandem::proto::Link ownLink{
      .mode = options.linkConsent == Options::LinkAnswer::Auto  ? tandem::proto::LinkMode::Everyone
              : options.linkConsent == Options::LinkAnswer::Ask ? tandem::proto::LinkMode::Listed
                                                                : tandem::proto::LinkMode::Off};
  endpoint->SetLocalLink(ownLink);
  // --link-answer-after: the requests we will accept, and when.
  std::vector<std::pair<tandem::proto::PlayerId, std::uint64_t>> linkAnswers;
  // --link-outcome: the calls we will decline, when and why.
  struct CallDecline {
    tandem::proto::PlayerId player{};
    std::uint64_t dueUs{};
    std::uint8_t reason{};
  };
  std::vector<CallDecline> linkDeclines;
  if (options.legacyWire) {
    endpoint->SetCompact(false);
    Log("legacy wire: every state every frame, whole poses and NPC lists");
  }
  if (options.drop > 0.f || options.dup > 0.f) {
    const auto seed = options.faultSeed != 0 ? options.faultSeed : std::random_device{}();
    endpoint->SetFaults({.drop = options.drop, .dup = options.dup, .seed = seed});
    Log("faults: {:.0f}% of what we send is lost, {:.0f}% of our state sent twice (seed {})",
        options.drop * 100, options.dup * 100, seed);
  }
  // The referee (docs/architecture/sim.md), in every mode: what the other
  // games send us, checked against the protocol and the authority table;
  // each breach is a `referee:` line, which scenarios reject.
  tandem::sim::Referee referee(options.host, {.faulty = options.drop > 0.f || options.dup > 0.f});
  referee.Attach(*endpoint);
  // --look random: the same clothes all session; its body keys from the
  // cache, or as soon as a look of its body is heard.
  const auto lookKeysDir = LookKeysDir(options.lookKeys);
  const auto lookSeed = options.lookSeed.value_or(std::random_device{}());
  auto ownKeys = LoadLookKeys(LookKeysFile(lookKeysDir, options.body));
  if (!options.echoLook && !playback && !sessionPlay)
    endpoint->SetLocalAppearance(RandomLook(options.body, lookSeed, ownKeys, options.lookPieces));
  // --record-session: what the host sends us, from its welcome on (kept
  // until we know who we are, then written with a header that says it).
  tandem::play::Writer sessionOut;
  std::vector<tandem::proto::Message> sessionEarly;
  if (!options.recordSessionFile.empty() && !options.host)
    endpoint->SetReceivedTap([&](const tandem::proto::Message &message) {
      if (!tandem::play::KeepsSession(message))
        return;
      if (sessionOut.IsOpen())
        sessionOut.Add(endpoint->HostNowUs(), message);
      else if (sessionEarly.size() < 100'000)
        sessionEarly.push_back(message);
    });
  // --hold, --hold-left: what our V holds (a hand not named is empty).
  const auto ourHeld = [&] {
    tandem::proto::Held held;
    if (!options.holdRecord.empty()) {
      held.right = TweakDBID(options.holdRecord);
      held.rightHint = options.holdHint;
    }
    if (!options.holdLeftRecord.empty())
      held.left = TweakDBID(options.holdLeftRecord);
    return held;
  };
  // Dual wielding: the hand of the next shot, charge and smart round (the
  // right first, then the other each time).
  const bool akimbo = !options.holdLeftRecord.empty();
  bool leftShot = false, leftCharge = true, leftSmart = false;
  if (!options.holdRecord.empty() || akimbo) {
    endpoint->SetLocalHeld(ourHeld());
    if (akimbo)
      Log("holding {} / {} (dual wielding)", options.holdRecord, options.holdLeftRecord);
    else
      Log("holding {}", options.holdRecord);
  }
  // --strike blade: our puppet holds a katana, so the host's NPC reacts to
  // (and is cut by) a blade: its hit reaction reads the attacker's weapon.
  if (options.strike == "blade") {
    endpoint->SetLocalHeld({.right = TweakDBID("Items.Preset_Katana_Default")});
    Log("holding Items.Preset_Katana_Default");
  }
  if (options.host)
    Log("hosting on UDP {} as {}{}", options.port, options.name,
        options.relay
            ? std::format(", and on relay {}:{}", options.relay->address, options.relay->port)
            : "");
  else if (options.address.empty())
    Log("joining code {} through relay {}:{} as {}", tandem::net::DisplayJoinCode(options.code),
        options.relay->address, options.relay->port, options.name);
  else
    Log("joining {}:{} as {}{}", options.address, options.port, options.name,
        target.relay ? " (the relay if it does not answer)" : "");
  if (shaper)
    Log("bad network, each way: +{} ms, jitter {} ms, {:.0f}% lost, {:.0f}% reordered",
        options.link.latencyMs, options.link.jitterMs, options.link.loss * 100.f,
        options.link.reorder * 100.f);
  // The relay as last logged: its state, the host's code, the last error.
  std::optional<tandem::net::RelayStatus> relayShown;

  PoseClip clip;
  if (!options.poseFile.empty()) {
    if (!clip.Load(options.poseFile, error)) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("pose: {} frames, {:.0f} s, in a loop", clip.Frames(), clip.Seconds());
  }
  const PoseClip *poseClip = options.poseFile.empty() ? nullptr : &clip;

  Recording recording;
  if (!options.recordFile.empty() && !recording.Open(options.recordFile)) {
    std::cerr << "tandem-peer: cannot write " << options.recordFile << '\n';
    return 1;
  }
  std::set<tandem::proto::NpcId> liveNpcs;
  std::map<tandem::proto::NpcId, float> npcHealth; // host: replayed NPCs hit by guests
  std::map<tandem::proto::NpcId, float> npcPoise;  // ...and their poise
  bool difficultySent = false;
  bool rulesSent = false;
  // --level, --npc-level: said once (the endpoint keeps ours; an NPC's is
  // said when we share it).
  bool levelSent = false;
  std::set<tandem::proto::NpcId> levelledNpcs;
  // --lend: said once (the endpoint keeps it); --unlock-vehicle: when, and
  // the guests told.
  bool lendSent = false;
  std::optional<std::chrono::steady_clock::time_point> unlockAt;
  std::set<tandem::proto::PlayerId> unlockedFor;
  // --vendor: when a guest was first there, whether it is shared and dead,
  // and its next standing still (the guest's proxy follows it).
  constexpr tandem::proto::NpcId kVendorNpc = 50'003;
  std::optional<std::chrono::steady_clock::time_point> vendorSince;
  bool vendorShared = false, vendorDead = false;
  auto nextVendorState = std::chrono::steady_clock::now();
  // Trade: our seat at the table, when we ask next, the trades done.
  tandem::trade::Seat tradeSeat;
  auto nextTradeAsk = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  int tradesDone = 0;
  const auto tradeSend = [&](const std::vector<tandem::proto::Message> &messages) {
    for (const auto &message : messages)
      endpoint->SendTrade(message);
  };
  // What --trade-give, --trade-money and --trade-asks put on the table.
  const auto tradePut = [&] {
    std::vector<tandem::proto::TradeItem> items;
    std::string_view rest = options.tradeGive;
    while (!rest.empty()) {
      const auto comma = rest.find(',');
      auto one = rest.substr(0, comma);
      rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
      std::uint32_t quantity = 1;
      if (const auto colon = one.rfind(':'); colon != std::string_view::npos) {
        quantity = Number<std::uint32_t>(one.substr(colon + 1));
        one = one.substr(0, colon);
      }
      if (!one.empty() && quantity > 0)
        items.push_back({.item = {.record = TweakDBID(std::string(one)), .quantity = quantity}});
    }
    if (items.empty() && options.tradeMoney == 0 && options.tradeAsks == 0)
      return;
    const auto count = items.size();
    tradeSend(tradeSeat.Offer(std::move(items), static_cast<std::uint32_t>(options.tradeMoney),
                              static_cast<std::uint32_t>(options.tradeAsks)));
    Log("trade: our side: {} stacks, {} eddies, asks {}", count, options.tradeMoney,
        options.tradeAsks);
  };
  auto nextAreaHit =
      std::chrono::steady_clock::now() + std::chrono::seconds(options.areaHitSeconds);
  auto nextThrow = std::chrono::steady_clock::now() + std::chrono::seconds(options.throwSeconds);
  auto nextSmart = std::chrono::steady_clock::now() + std::chrono::seconds(options.smartSeconds);
  auto nextDefend = std::chrono::steady_clock::now() + std::chrono::seconds(options.defendSeconds);
  std::uint32_t throwId = 0;
  bool parryNext = true;
  // --throw: our grenades in flight (id, when it explodes, where).
  struct Thrown {
    std::uint32_t id;
    std::chrono::steady_clock::time_point at;
    tandem::proto::Vec3 where;
    // --throw-bounce: when its path is said, and where it is then.
    std::optional<std::chrono::steady_clock::time_point> bounceAt{};
    tandem::proto::Vec3 bounce{};
  };
  tandem::presence::NoiseGate footstepGate; // --footsteps
  std::uint32_t footstepsSaid = 0;
  std::vector<Thrown> inFlight;
  std::set<tandem::proto::NpcId> deadNpcs;
  std::map<tandem::proto::NpcId, Vec3> npcAt; // join: where the host's NPCs are
  // join: when each was last in the host's states (dead and far NPCs are
  // not), and the NPCs whose grab the host refused (not asked again).
  std::map<tandem::proto::NpcId, std::chrono::steady_clock::time_point> npcSeen;
  // States heard of each NPC since the last report (netcode.md, NPC motion:
  // those near a V 30 a second, the others 10).
  std::map<tandem::proto::NpcId, std::uint32_t> npcStatesHeard;
  std::optional<tandem::proto::NpcId> nearNpc; // join, --near-npc: the NPC we stand by
  std::set<tandem::proto::NpcId> grabRefused;
  std::set<tandem::proto::NpcId> bodies; // join: the host's NPCs that died, still lying there
  std::optional<tandem::proto::NpcId> hitTarget;
  // --hit-npcs: strikes on each NPC since its health last changed; NPCs
  // that never change (invulnerable) are left alone.
  std::map<tandem::proto::NpcId, int> unanswered;
  std::set<tandem::proto::NpcId> unhurt;
  std::set<tandem::proto::NpcId> burnt;
  std::map<tandem::proto::NpcId, std::uint8_t> detection;
  // Stealth: --npc's NPC (where the guest was first heard), the NPCs last
  // sent (host), how far each NPC notices us (join), --camo, --shoot.
  constexpr tandem::proto::NpcId kOwnNpc = 50'000;
  std::optional<PlayerState> firstRemote;
  std::optional<tandem::proto::NpcEntry> ownNpc;
  std::vector<tandem::proto::NpcId> sentNpcs;
  std::map<tandem::proto::NpcId, int> noticedStep;
  int detectPhase = 0; // 0 nothing, 1 noticing, 2 detected, 3 lost
  auto nextNpcState = std::chrono::steady_clock::now();
  auto nextAwareness = std::chrono::steady_clock::now();
  int camoStep = 0; // 0 waiting, 1 on, 2 done
  // --status: each record's window, and the set last sent.
  struct StatusWindow {
    std::string record;
    double from{}, to{};
  };
  std::vector<StatusWindow> statusWindows;
  for (std::size_t start = 0; start < options.statusVisuals.size();) {
    const auto comma =
        std::min(options.statusVisuals.find(',', start), options.statusVisuals.size());
    const auto item = options.statusVisuals.substr(start, comma - start);
    start = comma + 1;
    const auto at = item.find('@');
    if (item.empty() || at == std::string::npos) {
      std::cerr << "tandem-peer: --status wants REC@FROM-TO, not '" << item << "'\n";
      return 1;
    }
    const auto times = item.substr(at + 1);
    const auto dash = times.find('-');
    StatusWindow window{.record = item.substr(0, at)};
    try {
      window.from = std::stod(times.substr(0, dash));
      window.to = dash == std::string::npos ? 0.0 : std::stod(times.substr(dash + 1));
    } catch (const std::exception &) {
      std::cerr << "tandem-peer: --status: bad times in '" << item << "'\n";
      return 1;
    }
    statusWindows.push_back(window);
  }
  std::vector<std::uint64_t> statusShown;
  auto nextShot = std::chrono::steady_clock::now();
  // Presence: --melee, --hurt, --menu-at, --react-npcs, --voice-npcs.
  auto nextMelee = nextShot, nextHurt = nextShot, nextReaction = nextShot, nextVoice = nextShot,
       nextCorpse = nextShot;
  std::size_t voiceStep = 0;
  // --corpse-npcs: the bodies whose rest is due, and when.
  std::vector<std::pair<tandem::proto::CorpsePose, std::chrono::steady_clock::time_point>>
      corpsesDue;
  // --charge: when the current charge began, the step last said and when.
  auto nextCharge = nextShot, chargeSaid = nextShot;
  std::optional<std::chrono::steady_clock::time_point> chargeFrom;
  int chargeStep = -1;
  float ourHealth = 1.f;
  bool ourMenu = false, presenceSent = false;
  std::size_t reactionStep = 0;
  int holsterStep = 0;
  std::optional<std::chrono::steady_clock::time_point> sharedSince;
  // --detect-at: while detected, every guest's bit (player - 1).
  const auto detectedBits = [&]() -> std::uint8_t {
    std::uint8_t bits = 0;
    if (detectPhase == 2)
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You() && id >= 1 && id <= 8)
          bits |= static_cast<std::uint8_t>(1u << (id - 1));
    return bits;
  };
  auto nextHit = std::chrono::steady_clock::now();
  // Takedowns: what we ask (join), what we answer and hold (host).
  using tandem::proto::NpcActionKind;
  using tandem::proto::NpcHoldKind;
  struct TakedownStep {
    tandem::proto::NpcId npc{};
    int step{}; // 0 idle, then the steps of each test
    std::chrono::steady_clock::time_point at{};
  } grab, carry, hold;
  int takedowns = 0;
  std::map<tandem::proto::NpcId, tandem::proto::PlayerId> held; // host: who holds what
  auto nextGrab = std::chrono::steady_clock::now() + 5s;
  auto nextCarry = nextGrab;
  auto nextHold = nextGrab;
  auto nextHitYou = std::chrono::steady_clock::now() + std::chrono::seconds(options.hitYouSeconds);
  auto nextShootYou =
      std::chrono::steady_clock::now() + std::chrono::seconds(options.shootYouSeconds);
  std::set<tandem::proto::NpcId> replayedNpcs; // host: the replay's live NPCs
  // host: what each NPC holds as its animation state last said (--shoot-you)
  std::map<tandem::proto::NpcId, std::uint64_t> npcWeapons;
  // Quickhacks: our upload's effect to land, the host's uploads to finish,
  // the NPCs a replay spawned, who drives a car.
  struct Pending {
    std::chrono::steady_clock::time_point at;
    tandem::proto::NpcHackState state;
    std::uint64_t effect{};
  };
  std::vector<Pending> pendingHacks;
  std::map<tandem::proto::PlayerId, bool> driving;
  auto nextHack = std::chrono::steady_clock::now() + 5s;
  auto nextPlayerHack = std::chrono::steady_clock::now() + 5s;
  // --hack-interrupt: the netrunner whose hack is to be cut, and when its
  // interruption comes and goes (nullopt: none pending).
  tandem::proto::NpcId interruptNpc = 0;
  std::optional<std::chrono::steady_clock::time_point> interruptAt, interruptEnd;
  auto nextCarHack = std::chrono::steady_clock::now() + 5s;
  std::vector<Recording::Entry> replay;
  std::size_t replayNext = 0;
  std::optional<std::uint64_t> replayStartUs;
  if (!options.replayFile.empty()) {
    replay = Recording::Load(options.replayFile);
    Log("replay: {} NPC messages from {}", replay.size(), options.replayFile);
    if (Recording::SpawnsIn(replay) == 0)
      Log("replay: no NPC spawns in {}: its guest gets no proxies (recorded on an empty street?)",
          options.replayFile);
  }
  NpcAnimScript anims;
  if (!options.animsFile.empty()) {
    std::string error;
    if (!anims.Load(options.animsFile, error)) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("anims: {} NPC animation changes from {}", anims.Size(), options.animsFile);
  }
  std::size_t animsNext = 0;
  // --anims shoot=N: shots still to fire, per NPC, and when the next is due.
  struct Burst {
    int left{};
    std::uint64_t nextUs{};
  };
  std::map<tandem::proto::NpcId, Burst> bursts;

  std::vector<StoryStep> story;
  std::size_t storyNext = 0;
  std::optional<std::chrono::steady_clock::time_point> storyStart;
  std::uint64_t storyLine = 0;
  SceneScript sceneScript;
  sceneScript.own = kOwnNpc;
  std::uint64_t actorPoses = 0, cameraViews = 0, rigPoses = 0;
  if (!options.storyFile.empty()) {
    story = LoadStory(options.storyFile, error);
    if (!error.empty()) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("story: {} steps from {}", story.size(), options.storyFile);
  }
  // --restrict: two more steps of the host's story, in time order.
  if (!options.restrictions.empty()) {
    story.push_back({.at = options.restrictAt,
                     .verb = "restrict",
                     .args = {options.restrictions},
                     .rest = options.restrictions});
    if (options.restrictFor > 0.0)
      story.push_back({.at = options.restrictAt + options.restrictFor,
                       .verb = "restrict",
                       .args = {"off"},
                       .rest = "off"});
    std::ranges::stable_sort(story, {}, &StoryStep::at);
  }
  // --no-fast-travel: one more step of the host's story.
  if (options.noFastTravel) {
    story.push_back({.at = 2.0, .verb = "fasttravel", .args = {"off"}, .rest = "off"});
    std::ranges::stable_sort(story, {}, &StoryStep::at);
  }
  // --prefabs: one more step of the host's story.
  if (!options.prefabs.empty()) {
    story.push_back(
        {.at = 2.0, .verb = "prefabs", .args = {options.prefabs}, .rest = options.prefabs});
    std::ranges::stable_sort(story, {}, &StoryStep::at);
  }
  bool gatesSaid = false;
  // join: the call to answer, and when.
  std::optional<std::pair<std::uint64_t, std::chrono::steady_clock::time_point>> toAnswer;
  bool replied = false;
  std::optional<std::chrono::steady_clock::time_point> joinedAt;

  Puppeteer puppeteer(options);
  std::optional<RegionNpcs> regionNpcs;
  if (options.mode == Mode::Region)
    regionNpcs.emplace(options);
  // --play: when it began, recorded NPCs as this session's, where our V is.
  std::optional<std::uint64_t> playStartUs;
  std::map<tandem::proto::NpcId, tandem::proto::NpcId> playNpcs;
  Vec3 playedAt;
  std::uint64_t playSent = 0, playDropped = 0;
  // --save-play: our own play, from the moment we are in a session.
  tandem::play::Writer savedPlay;
  // Down and out: our V's life (0 up, 1 down, 2 out) and where it lies;
  // since when another player is with us; the downed V we revive, the
  // others' latest positions.
  int ourLife = 0;
  bool downDone = false;
  std::optional<PlayerState> lying;
  std::optional<std::chrono::steady_clock::time_point> withSince, outSince;
  struct Rescue {
    tandem::proto::PlayerId target{};
    std::chrono::steady_clock::time_point at;
    bool started{};
  };
  std::optional<Rescue> rescue;
  std::map<tandem::proto::PlayerId, PlayerState> lastOf;
  const auto setLife = [&](int life, std::string_view why) {
    ourLife = life;
    endpoint->SetLocalVitals({.life = static_cast<tandem::proto::Life>(life)});
    constexpr const char *kLives[] = {"up", "down", "out"};
    Log("our V is {} ({})", kLives[life], why);
  };
  if (options.mode == Mode::Path) {
    std::vector<PlayerState> path;
    for (auto &entry : Recording::Load(options.pathFile))
      if (const auto *state = std::get_if<PlayerState>(&entry.message))
        path.push_back(*state);
    if (path.size() < 2) {
      std::cerr << "tandem-peer: " << options.pathFile << ": no saved path\n";
      return 1;
    }
    Log("path: {} states, {:.1f} s, in a loop", path.size(),
        static_cast<double>(path.back().timeUs - path.front().timeUs) / 1e6);
    puppeteer.SetPath(std::move(path));
  }
  Recording savedPath;
  if (!options.savePathFile.empty() && !savedPath.Open(options.savePathFile)) {
    std::cerr << "tandem-peer: cannot write " << options.savePathFile << '\n';
    return 1;
  }
  const auto startUs = tandem::net::NowUs();
  auto nextWorld = std::chrono::steady_clock::now();
  bool seated = false, parked = false, paintOff = false;
  // --summon: our summoned car's arrival.
  struct {
    bool standing{}, boarded{};
    int round{};
    Vec3 stop, from;
    float direction{-1.f};
    tandem::proto::Quat facing;
    std::optional<std::uint64_t> arrivingUs;
    std::uint64_t sentUs{};
    std::optional<std::chrono::steady_clock::time_point> parkedAt;
  } summon;
  std::uint64_t seatedAtUs = 0;
  std::optional<tandem::proto::VehicleState> lastCar;
  // The last state sent, and (world ... moved) the body we hold there.
  std::optional<PlayerState> lastState, heldBody;
  // --radio: the radio of the car we drive, as a driver's game sends it.
  const auto sendRadio = [&](std::string_view why) {
    const int station = *options.radioStation;
    endpoint->SetLocalVehicleRadio(
        {.on = station >= 0, .station = static_cast<std::int8_t>(station)});
    Log("car radio sent ({}): {}", why, StationName(station));
  };
  const auto interval = std::chrono::microseconds(1'000'000 / options.rateHz);
  auto nextSend = std::chrono::steady_clock::now();
  auto nextReport = nextSend + 5s;
  auto lastTraffic = endpoint->Traffic();
  auto lastTrafficUs = tandem::net::NowUs();
  int resyncsAsked = 0;
  std::uint64_t heard = 0, heardPoses = 0, heardShots = 0, heardVehicles = 0;
  // --pause-at: when someone else was first there, and how far the pause got.
  std::optional<std::chrono::steady_clock::time_point> togetherSince;
  // --ride-along: whose scene ride we ride along in, the cars heard (by
  // player), where we were carried last, out of it yet.
  std::optional<tandem::proto::PlayerId> alongWith;
  std::map<tandem::proto::PlayerId, tandem::proto::VehicleState> carOf;
  std::optional<PlayerState> lastCarried;
  bool gotOut = false;
  // metro: the ride's step as last sent.
  int metroStep = 0;
  bool metroSecondHalf = false;
  // drive --driver-npc: the NPC at the wheel (host), its states every 100 ms.
  constexpr tandem::proto::NpcId kDriverNpc = 50'001;
  constexpr tandem::proto::NpcId kStreetDriver = 50'002;
  auto nextDriverState = std::chrono::steady_clock::now();
  // An NCART train's seat and id, made up: the game only uses them to find
  // the same train in its own world.
  const std::uint64_t kTrainSeat = CName("seat_front_right");
  constexpr std::uint64_t kTrain = 0x4E43415254ULL; // "NCART"
  enum class PauseStep { Waiting, Paused, Done } pauseStep = PauseStep::Waiting;
  PauseStep sandevistanStep = PauseStep::Waiting;
  // Fast travel together: done yet, our offers so far, where we last stood.
  bool travelled = false, offeredRegroup = false;
  std::uint32_t offers = 0;
  // Activities: our time skip asked yet; the game time skipped so far (host:
  // added to the clock it sends); the host's clock as last heard (guest);
  // our activity's start and end.
  bool askedSkip = false;
  std::int64_t skippedSeconds = 0;
  std::optional<std::int32_t> hostClock;
  // When it was heard (real time): how fast the host's world ran between
  // two (time-menus: a host's menu slows nothing).
  std::optional<std::chrono::steady_clock::time_point> hostClockAt;
  bool busySet = false, busyDone = false;
  std::optional<std::chrono::steady_clock::time_point> busySince;
  std::optional<Vec3> lastSent;
  const auto distance = [](const Vec3 &a, const Vec3 &b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
  };
  // Devices: when the session began for us (--device counts from it), and
  // the devices to change back (--device-answer: when, from what).
  std::optional<std::chrono::steady_clock::time_point> devicesFrom;
  // A device's fire: the bursts our "turrets" still owe (host), and where
  // --turret-seen stands (0 not yet, 1 said, 2 lost again).
  struct TurretBurst {
    std::uint64_t entity{};
    tandem::proto::PlayerId player{};
    Vec3 at;
    int shots{};
    std::chrono::steady_clock::time_point next;
  };
  std::vector<TurretBurst> turretBursts;
  int turretSeenStep = 0;
  // --turret-hands: when it was last said (0: not yet), whether let go.
  std::chrono::steady_clock::time_point turretHandsSaid{};
  bool turretHandsDone = false;
  // Turrets in others' hands: their flags as last logged.
  std::map<std::uint64_t, std::uint8_t> turretsHeld;
  // --breach: a won one pays once the host grants it (devices.md).
  tandem::devices::BreachHolds breachHolds;
  const auto breachSettled = [](const tandem::devices::BreachHolds::Decision &decision) {
    if (decision.paid)
      Log("our breach pays ({})", decision.answered ? "granted" : "nobody answered");
    else
      Log("our breach pays nothing: player {} was first", decision.by);
  };
  std::uint32_t interactSeq = 0; // --interact: our questions' numbers
  std::map<tandem::devices::Key,
           std::pair<std::chrono::steady_clock::time_point, tandem::devices::What>>
      deviceAnswers;
  // When we last answered each device: what the host says of it within
  // kDeviceReply after is its reply, not a change to answer.
  std::map<tandem::devices::Key, std::chrono::steady_clock::time_point> deviceAnsweredAt;
  constexpr auto kDeviceReply = std::chrono::seconds(5);
  // "device 1a2b (0x3c4d): on, open+hacked, value -1", as the game's log has it.
  const auto describeDevice = [](const tandem::devices::Key &key,
                                 const tandem::devices::What &what) {
    using namespace tandem::proto;
    constexpr std::pair<std::uint16_t, std::string_view> kBits[] = {
        {kDeviceOpen, "open"},          {kDeviceLocked, "locked"},
        {kDeviceSealed, "sealed"},      {kDeviceBroken, "broken"},
        {kDeviceExploded, "exploded"},  {kDeviceHacked, "hacked"},
        {kDeviceFriendly, "friendly"},  {kDeviceExposed, "exposed"},
        {kDeviceBreached, "breached"},  {kDeviceMoving, "moving"},
        {kDeviceGlitching, "glitching"}};
    std::string bits;
    for (const auto &[bit, word] : kBits)
      if (what.flags & bit)
        bits += (bits.empty() ? "" : "+") + std::string(word);
    constexpr std::string_view kStatus[] = {"disabled", "unpowered", "off", "on"};
    return std::format("device {:x} ({:#x}): {}, {}, value {}", key.entity, key.cls,
                       kStatus[std::clamp<int>(what.status + 2, 0, 3)],
                       bits.empty() ? "plain" : bits, what.value);
  };
  // Makes a device something: a guest asks the host, a host says its own is.
  const auto operateDevice = [&](const tandem::devices::Key &key, const tandem::devices::What &what,
                                 std::string_view why) {
    if (endpoint->IsHost())
      endpoint->ReportDevice({.entity = key.entity,
                              .cls = key.cls,
                              .status = what.status,
                              .flags = what.flags,
                              .value = what.value,
                              .by = endpoint->You()});
    else
      endpoint->UseDevice({.entity = key.entity,
                           .cls = key.cls,
                           .status = what.status,
                           .flags = what.flags,
                           .value = what.value});
    Log("{} {} ({})", endpoint->IsHost() ? "our" : "asked the host for", describeDevice(key, what),
        why);
  };
  const auto answerDevice = [&](const tandem::devices::Key &key, tandem::devices::What what) {
    if (options.deviceAnswer.empty() || !DeviceWords(options.deviceAnswer, what))
      return;
    const auto now = std::chrono::steady_clock::now();
    if (const auto last = deviceAnsweredAt.find(key);
        last != deviceAnsweredAt.end() && now < last->second + kDeviceReply)
      return;
    deviceAnswers[key] = {
        std::chrono::steady_clock::now() + std::chrono::seconds(options.deviceAfterSeconds), what};
  };
  // Loot: our drop (--drop-item), the others' world items to pick up
  // (--take-items: when), and dead NPCs' bodies to take away (--remove-corpses).
  bool dropped = false;
  std::optional<std::chrono::steady_clock::time_point> dropAt;
  bool lootedQuest = false;
  std::optional<std::chrono::steady_clock::time_point> lootQuestAt;
  std::map<tandem::proto::WorldItemId,
           std::pair<std::chrono::steady_clock::time_point, tandem::proto::WorldItem>>
      toTake;
  std::map<tandem::proto::NpcId, std::chrono::steady_clock::time_point> corpses;
  std::optional<PlayerState> otherV; // the last state heard of the player we follow
  // The one other player mirror and front copy, and otherV is of: the
  // lowest id but ours (a guest follows the host), so a second mock player
  // in the session is not copied too.
  const auto followed = [&] {
    for (const auto &[id, info] : endpoint->Players())
      if (id != endpoint->You())
        return id;
    return tandem::proto::kNoPlayer;
  };
  // Comms: our pings (the game's own rules for them), their timers, the
  // last target pinged, our tag's state, our waypoint's.
  tandem::comms::Pinger pinger;
  int pingStep = 0;
  tandem::proto::TargetRef lastPing;
  Vec3 lastPingAt{};
  bool tagOn = false, linkAsked = false, unlinked = false, waypointSet = false,
       waypointCleared = false, targetMarked = false;
  int hackStep = 0;
  tandem::proto::NpcId hackedNpc = 0;
  auto nextHackNpc = std::chrono::steady_clock::now();
  auto nextPing = std::chrono::steady_clock::now(), nextTag = nextPing, nextReveal = nextPing,
       waypointSetAt = nextPing;

  // The NPC world of a replay (--replay) or a recorded session
  // (--play-session) as this host sends it: an NPC a guest killed is out of
  // the rest; --npc and --detect-at on top; `shifted`: moved by --offset.
  const auto replayNpcWorld = [&](tandem::proto::Message message, bool shifted) {
    // --offset moves the replayed NPCs, to tell them from the game's own.
    const auto shift = [&](Vec3 &p) {
      if (!shifted)
        return;
      p.x += options.offset.x;
      p.y += options.offset.y;
      p.z += options.offset.z;
    };
    // An NPC a guest killed is out of the rest of the recording.
    if (auto *spawn = std::get_if<tandem::proto::NpcSpawn>(&message)) {
      shift(spawn->position);
      endpoint->SendNpcSpawn(*spawn);
      replayedNpcs.insert(spawn->npc);
    } else if (auto *states = std::get_if<tandem::proto::NpcStates>(&message)) {
      std::erase_if(states->npcs, [&](const auto &entry) { return deadNpcs.contains(entry.npc); });
      sentNpcs.clear();
      for (auto &entry : states->npcs) {
        shift(entry.position);
        entry.detected |= detectedBits();
        sentNpcs.push_back(entry.npc);
      }
      if (ownNpc)
        states->npcs.push_back(*ownNpc);
      endpoint->SendNpcStates(*states);
    } else if (const auto *gone = std::get_if<tandem::proto::NpcGone>(&message)) {
      replayedNpcs.erase(gone->npc);
      if (!deadNpcs.contains(gone->npc)) {
        endpoint->SendNpcGone(*gone);
        held.erase(gone->npc);
      }
    } else if (const auto *health = std::get_if<tandem::proto::NpcHealth>(&message))
      endpoint->SendNpcHealth(*health);
    // The recording's own animation states and shots, unless --anims
    // scripts them: then the script alone says how the NPCs animate.
    else if (auto *recorded = std::get_if<tandem::proto::NpcAnims>(&message)) {
      if (anims.Size() == 0) {
        std::erase_if(recorded->npcs,
                      [&](const auto &anim) { return deadNpcs.contains(anim.npc); });
        for (const auto &anim : recorded->npcs)
          npcWeapons[anim.npc] = anim.weapon;
        endpoint->SendNpcAnims(*recorded);
      }
    } else if (const auto *shot = std::get_if<tandem::proto::NpcShot>(&message)) {
      if (anims.Size() == 0 && !deadNpcs.contains(shot->npc))
        endpoint->SendNpcShot(*shot);
    }
  };
  // --play-session: when it began, and what it sent.
  std::optional<std::uint64_t> sessionStartUs;
  std::uint64_t sessionSent = 0, sessionDropped = 0, sessionNpcs = 0;

  // The street. Host (--traffic): its cars, round a centre; guest: the
  // vehicles shared with us and where each was last said to be.
  struct StreetCar {
    tandem::proto::TrafficId id{};
    float angle{}, radius{}, speed{}; // radians, metres, metres per second (0: parked)
    float health{1.f};
    std::uint16_t flags{};
    bool gone{};
  };
  std::vector<StreetCar> street;
  std::optional<Vec3> streetCentre;
  tandem::traffic::Budget streetBudget;
  auto nextStreetState = std::chrono::steady_clock::now();
  int wreckStep = 0;
  std::map<tandem::proto::TrafficId, tandem::proto::TrafficSpawn> sharedCars;
  auto nextRam = std::chrono::steady_clock::now() + 5s;
  bool hackedTraffic = false;
  std::optional<tandem::proto::TrafficSpawn> stolen;
  int stealStep = 0;
  double stolenAt = 0.0;
  int heatStep = 0;
  bool crimeSaid = false;

  // Vehicle combat. Our car's damage as its owner (hits on it, --car-damage),
  // a car rolling on after we left it, pushes from others' cars, the gunner
  // seat we asked for, what we heard of the others' vehicles.
  namespace vc = tandem::vehiclecombat;
  std::optional<tandem::proto::VehicleDamage> ourDamage;
  const auto ourCarDamage = [&]() -> tandem::proto::VehicleDamage & {
    if (!ourDamage)
      ourDamage = tandem::proto::VehicleDamage{.vehicle = tandem::proto::PlayerCar(endpoint->You()),
                                               .record = TweakDBID(options.vehicle)};
    return *ourDamage;
  };
  int carDamageStep = 0;
  std::optional<tandem::proto::VehicleState> rolling;
  std::uint64_t rollingSinceUs = 0, rolledAtUs = 0;
  vc::BumpLedger bumps;
  auto nextBump = std::chrono::steady_clock::now() + 3s;
  auto nextCarShot = std::chrono::steady_clock::now() + 3s;
  int carShots = 0;
  auto nextNpcRam = std::chrono::steady_clock::now() + 3s;
  int gunnerStep = 0;
  tandem::proto::VehicleRef gunnerCar = 0;
  std::optional<std::chrono::steady_clock::time_point> gunnerSince;
  auto nextAim = std::chrono::steady_clock::now();
  auto nextMountedShot = std::chrono::steady_clock::now();
  std::map<tandem::proto::VehicleRef, tandem::proto::VehicleDamage> heardDamage;
  std::map<tandem::proto::PlayerId, std::chrono::steady_clock::time_point> aimLoggedAt;
  int chaseStep = 0;
  // --unknown: the records our "game" lacks. The others' records resolved as
  // a game does (tandem/mods.hpp, plugin/src/mods.cpp): logged, told.
  tandem::mods::Resolver missing(
      [&](tandem::mods::Kind, std::uint64_t record) { return !options.unknown.contains(record); });
  const auto nameOf = [&](tandem::proto::PlayerId id) {
    const auto found = endpoint->Players().find(id);
    return found != endpoint->Players().end() ? found->second.name : std::format("player {}", id);
  };
  const auto resolve = [&](tandem::mods::Kind kind, std::uint64_t record,
                           const tandem::proto::RecordHint &hint, tandem::proto::PlayerId from) {
    if (!options.unknown.empty())
      missing.Resolve(kind, record, hint, from, nameOf(from), tandem::net::NowUs());
  };

  while (!g_stop) {
    for (const auto &line : missing.TakeLog())
      Log("{}", line);
    for (const auto &notice : missing.TakeNotices(tandem::net::NowUs()))
      Log("mods notice: {}'s {} x{} shown otherwise (outcome {})", notice.who,
          tandem::mods::KindName(notice.kind), notice.count, static_cast<int>(notice.outcome));
    for (const auto &told : missing.TakeTold())
      endpoint->SendSubstitutes(told);
    for (const auto &event : endpoint->Update(5ms)) {
      // region: what concerns the NPCs we run.
      if (const auto *linked = std::get_if<tandem::events::RegionsChanged>(&event))
        puppeteer.SetRegionLinked(!linked->linked.empty(), tandem::net::NowUs());
      if (regionNpcs && regionNpcs->On(event, *endpoint))
        continue;
      std::visit(
          [&](const auto &e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, tandem::events::Joined>) {
              Log("in the session: player {}, host {}", e.you, e.host);
              if (!options.recordSessionFile.empty() && !sessionOut.IsOpen()) {
                std::string saveError;
                if (sessionOut.Open(options.recordSessionFile,
                                    {.startHostUs = endpoint->HostNowUs(),
                                     .player = e.you,
                                     .body = options.body,
                                     .kind = tandem::play::Kind::Session,
                                     .host = e.host},
                                    saveError)) {
                  for (const auto &early : sessionEarly)
                    sessionOut.Add(endpoint->HostNowUs(), early);
                  sessionEarly.clear();
                  Log("recording the session to {}", options.recordSessionFile);
                } else {
                  Log("--record-session: {}", saveError);
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::PlayerJoined>)
              Log("+ player {} ({})", e.player.id, e.player.name);
            else if constexpr (std::is_same_v<E, tandem::events::PlayerLeft>)
              Log("- player {}", e.player);
            else if constexpr (std::is_same_v<E, tandem::events::PlayerReturned>)
              Log("= player {} ({}) is back", e.player.id, e.player.name);
            else if constexpr (std::is_same_v<E, tandem::events::NamedAs>)
              Log("the session shows us as {} ({} is taken)", e.shown, e.asked);
            else if constexpr (std::is_same_v<E, tandem::events::RemoteState>) {
              ++heard;
              if (e.state.player == followed()) {
                puppeteer.Heard(e.state);
                otherV = e.state;
              }
              if (!options.savePathFile.empty())
                savedPath.Add(e.state);
              if (!firstRemote)
                firstRemote = e.state;
              lastOf[e.state.player] = e.state;
            } else if constexpr (std::is_same_v<E, tandem::events::RemotePresence>) {
              Log("player {} presence: health {:.0f} %, flags {:#x}", e.presence.player,
                  tandem::presence::HealthFraction(e.presence.health) * 100.f, e.presence.flags);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteStatusVisuals>) {
              Log("player {} status visuals: {} record(s)", e.visuals.player,
                  e.visuals.records.size());
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteCue>) {
              using Kind = tandem::proto::CueKind;
              const char *kind = e.cue.kind == Kind::Trace      ? "bullet"
                                 : e.cue.kind == Kind::Ricochet ? "ricochet"
                                 : e.cue.kind == Kind::Charge   ? "charge"
                                                                : "melee hit";
              Log("player {} cue: {} to ({:.1f}, {:.1f}, {:.1f}), flags {:#x}", e.cue.player, kind,
                  e.cue.at.x, e.cue.at.y, e.cue.at.z, e.cue.arg);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteHitTaken>) {
              const auto &hit = e.hit;
              Log("player {} took a hit: by {} {}, attack {:#x} (type {}), zone {}, {:.0f} % of "
                  "its health, flags {:#x}, from ({:.2f}, {:.2f}, {:.2f})",
                  hit.player,
                  hit.by == tandem::proto::HitBy::Npc      ? "npc"
                  : hit.by == tandem::proto::HitBy::Player ? "player"
                                                           : "nobody known,",
                  hit.attacker, hit.attack, hit.attackType, hit.zone,
                  static_cast<float>(hit.share) / 2.55f, hit.flags, hit.direction.x,
                  hit.direction.y, hit.direction.z);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcReaction>) {
              // Twitches come by the dozen under fire: the others are said.
              if (e.reaction.type != tandem::proto::kReactionTwitch)
                Log("npc {} reacts: type {} (direction {}, intensity {}, part {}), by player {}",
                    e.reaction.npc, e.reaction.type, e.reaction.direction, e.reaction.intensity,
                    e.reaction.bodyPart, e.reaction.by);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcVoice>) {
              Log("npc {} says voice context {:#x} (after {} ms)", e.voice.npc, e.voice.context,
                  e.voice.delayMs);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteStealth>) {
              Log("player {} stealth: flags {:#x}, visibility {:.2f}, camo {:#x}", e.stealth.player,
                  e.stealth.flags, e.stealth.visibility, e.stealth.camo);
            } else if constexpr (std::is_same_v<E, tandem::events::NoiseBy>) {
              Log("player {} noise: stim {}, radius {:.0f}", e.noise.player, e.noise.stim,
                  e.noise.radius);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcAwareness>) {
              // How far each NPC notices us, in quarters, when it changes.
              std::map<tandem::proto::NpcId, int> now;
              for (const auto &entry : e.awareness.entries)
                if (entry.player == endpoint->You())
                  now[entry.npc] = entry.level * 4 / 255;
              for (const auto &[npc, step] : now)
                if (noticedStep[npc] != step)
                  Log("npc {} notices us: {}%", npc, step * 25);
              for (const auto &[npc, step] : noticedStep)
                if (step && !now.contains(npc))
                  Log("npc {} notices us: 0%", npc);
              noticedStep = std::move(now);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteAppearance>) {
              Log("look from player {}: {} keys, {} items", e.appearance.player,
                  e.appearance.look.size(), e.appearance.items.size());
              for (std::size_t i = 0; i < e.appearance.items.size(); ++i)
                resolve(tandem::mods::Kind::Clothing, e.appearance.items[i],
                        i < e.appearance.hints.size() ? e.appearance.hints[i]
                                                      : tandem::proto::RecordHint{},
                        e.appearance.player);
              // A real V's body keys: kept for random looks of that body.
              if (!e.appearance.look.empty() && e.appearance.body != tandem::proto::Body::Unknown) {
                SaveLookKeys(LookKeysFile(lookKeysDir, e.appearance.body), e.appearance.look);
                if (!options.echoLook && !playback && !sessionPlay && ownKeys.empty() &&
                    e.appearance.body == options.body) {
                  ownKeys = e.appearance.look;
                  endpoint->SetLocalAppearance(
                      RandomLook(options.body, lookSeed, ownKeys, options.lookPieces));
                }
              }
              if (options.echoLook &&
                  (options.mode == Mode::Mirror || options.mode == Mode::Front)) {
                auto echo = e.appearance;
                echo.body = options.body;
                endpoint->SetLocalAppearance(std::move(echo));
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteHeld>) {
              Log("player {} holds {:#x} / {:#x}", e.held.player, e.held.right, e.held.left);
              resolve(tandem::mods::Kind::Weapon, e.held.right, e.held.rightHint, e.held.player);
              resolve(tandem::mods::Kind::Weapon, e.held.left, e.held.leftHint, e.held.player);
              // Echoed, unless we hold our own (--hold, --held-record, --strike
              // blade): the host's empty hands, heard at the join, replaced it.
              if ((options.mode == Mode::Mirror || options.mode == Mode::Front) &&
                  options.holdRecord.empty() && options.holdLeftRecord.empty() &&
                  options.strike != "blade")
                endpoint->SetLocalHeld(e.held);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcSpawn>) {
              liveNpcs.insert(e.spawn.npc);
              resolve(tandem::mods::Kind::Npc, e.spawn.record, e.spawn.hint,
                      tandem::regions::Announcer(e.spawn.npc));
              if (!options.recordFile.empty()) {
                recording.Add(e.spawn);
                Log("recorded the spawn of npc {}", e.spawn.npc);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcStates>) {
              if (!options.recordFile.empty())
                recording.Add(e.states);
              // Who each NPC has detected, when it changes.
              for (const auto &entry : e.states.npcs) {
                npcAt[entry.npc] = entry.position;
                npcSeen[entry.npc] = std::chrono::steady_clock::now();
                ++npcStatesHeard[entry.npc];
              }
              for (const auto &entry : e.states.npcs)
                if (auto &seen = detection[entry.npc]; seen != entry.detected) {
                  Log("npc {} has detected players {:#x}", entry.npc, entry.detected);
                  seen = entry.detected;
                }
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcAnims>) {
              if (!options.recordFile.empty())
                recording.Add(e.anims);
              for (const auto &anim : e.anims.npcs)
                Log("npc {} animates: high {}, upper {}, stance {}, weapon {:#x}, target {}, "
                    "workspot {:#x} entry {}",
                    anim.npc, anim.highLevel, anim.upperBody, anim.stance, anim.weapon, anim.target,
                    anim.workspot, anim.workspotEntry);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcShot>) {
              if (!options.recordFile.empty())
                recording.Add(e.shot);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcGone>) {
              liveNpcs.erase(e.gone.npc);
              if (e.gone.reason == tandem::proto::kGoneDead) {
                Log("npc {} died", e.gone.npc);
                bodies.insert(e.gone.npc);
              } else {
                bodies.erase(e.gone.npc);
                if (e.gone.reason == tandem::proto::kGoneKnockedOut)
                  Log("npc {} knocked out (by player {})", e.gone.npc, e.gone.killer);
              }
              if (!options.recordFile.empty())
                recording.Add(e.gone);
            } else if constexpr (std::is_same_v<E, tandem::events::NpcEffectBy>) {
              for (const auto effect : e.effect.effects)
                Log("npc {} gets effect {:#x} from player {}", e.effect.npc, effect,
                    e.effect.player);
            } else if constexpr (std::is_same_v<E, tandem::events::NpcHitBy>) {
              // As the host of a replay: the NPC takes it (100 health each).
              auto &health = npcHealth.try_emplace(e.hit.npc, 1.f).first->second;
              health -= (e.hit.flags & tandem::proto::kHitLethal) ? health : e.hit.damage / 100.f;
              Log("npc {} hit by player {} for {:.0f} (flags {}), health {:.0f}%", e.hit.npc,
                  e.hit.player, e.hit.damage, e.hit.flags, std::max(health, 0.f) * 100.f);
              if (health <= 0.f) {
                deadNpcs.insert(e.hit.npc);
                if (options.removeCorpsesSeconds >= 0)
                  corpses[e.hit.npc] = std::chrono::steady_clock::now() +
                                       std::chrono::seconds(options.removeCorpsesSeconds);
                endpoint->SendNpcGone({.npc = e.hit.npc, .reason = 1, .killer = e.hit.player});
                Log("npc {} died", e.hit.npc);
              } else
                endpoint->SendNpcHealth({.npc = e.hit.npc, .health = health});
            } else if constexpr (std::is_same_v<E, tandem::events::Combat>) {
              Log("{}", DescribeCombat(e.message));
              namespace proto = tandem::proto;
              // As the host of a replay: the strike by the game's rules
              // (100 health, 100 poise each), and what the guests see of it.
              if (const auto *strike = std::get_if<proto::NpcStrike>(&e.message);
                  strike && options.host && !deadNpcs.contains(strike->npc)) {
                auto &health = npcHealth.try_emplace(strike->npc, 1.f).first->second;
                health -= tandem::combat::StrikeLoss(*strike, health * 100.f, 100.f, false) / 100.f;
                if (strike->poise > 0.f) {
                  auto &poise = npcPoise.try_emplace(strike->npc, 1.f).first->second;
                  poise = std::max(poise - strike->poise / 100.f, 0.f);
                  endpoint->SendCombat(proto::NpcPoise{.npc = strike->npc, .poise = poise});
                  Log("npc {} poise {:.0f}%{}", strike->npc, poise * 100.f,
                      poise <= 0.f ? ": guard broken" : "");
                }
                if (health > 0.f) {
                  endpoint->SendNpcHealth({.npc = strike->npc, .health = health});
                  Log("npc {} health {:.0f}%", strike->npc, health * 100.f);
                } else if (strike->flags & proto::kStrikeNonlethal) {
                  endpoint->SendNpcHealth({.npc = strike->npc, .health = 0.001f});
                  endpoint->SendNpcGone({.npc = strike->npc,
                                         .reason = tandem::proto::kGoneKnockedOut,
                                         .killer = strike->player});
                  Log("npc {} knocked out by player {}", strike->npc, strike->player);
                } else {
                  if (strike->part >= 0) {
                    const bool clean = strike->flags & proto::kStrikeMelee;
                    endpoint->SendCombat(
                        proto::NpcWound{.npc = strike->npc,
                                        .player = strike->player,
                                        .part = static_cast<std::uint8_t>(strike->part),
                                        .wound = static_cast<std::uint8_t>(clean ? 0 : 1),
                                        .position = strike->position});
                    Log("npc {} loses part {}", strike->npc, strike->part);
                  }
                  deadNpcs.insert(strike->npc);
                  // --remove-corpses: as for a hit (the game's kills come as
                  // strikes now).
                  if (options.removeCorpsesSeconds >= 0)
                    corpses[strike->npc] = std::chrono::steady_clock::now() +
                                           std::chrono::seconds(options.removeCorpsesSeconds);
                  endpoint->SendNpcGone(
                      {.npc = strike->npc, .reason = 1, .killer = strike->player});
                  Log("npc {} died", strike->npc);
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::NpcActionBy>) {
              // As the host of a replay: the NPC is ours; refuse what cannot be.
              const auto &a = e.action;
              const bool alive = replayedNpcs.contains(a.npc) && !deadNpcs.contains(a.npc);
              const bool body = a.kind == NpcActionKind::PickUp || a.kind == NpcActionKind::Drop ||
                                a.kind == NpcActionKind::Throw || a.kind == NpcActionKind::Dispose;
              const auto holder = held.contains(a.npc) ? held[a.npc] : tandem::proto::kNoPlayer;
              std::uint8_t reason = 0;
              if (!alive && !body)
                reason = tandem::proto::kRefusedGone;
              else if (holder != tandem::proto::kNoPlayer && holder != a.player)
                reason = tandem::proto::kRefusedHeld;
              endpoint->SendNpcActionReply(
                  a.player,
                  {.npc = a.npc, .kind = a.kind, .accepted = reason == 0, .reason = reason});
              Log("npc {}: player {} asks action {}: {}", a.npc, a.player, static_cast<int>(a.kind),
                  reason ? std::format("refused ({})", reason) : std::string("accepted"));
              if (reason == 0) {
                if (a.kind == NpcActionKind::Grab || a.kind == NpcActionKind::PickUp) {
                  held[a.npc] = a.player;
                  endpoint->SendNpcHold({.npc = a.npc,
                                         .player = a.player,
                                         .how = a.kind == NpcActionKind::Grab
                                                    ? NpcHoldKind::Grappled
                                                    : NpcHoldKind::Carried,
                                         .name = a.name});
                } else {
                  held.erase(a.npc);
                  endpoint->SendNpcHold({.npc = a.npc});
                  const bool kills =
                      a.kind == NpcActionKind::Takedown || a.kind == NpcActionKind::Knockout ||
                      a.kind == NpcActionKind::Finisher || a.kind == NpcActionKind::Throw;
                  if (a.kind == NpcActionKind::Dispose) {
                    deadNpcs.insert(a.npc);
                    endpoint->SendNpcGone(
                        {.npc = a.npc, .reason = tandem::proto::kGoneDisposed, .killer = a.player});
                    Log("npc {} disposed of", a.npc);
                  } else if (kills && alive) {
                    deadNpcs.insert(a.npc);
                    endpoint->SendNpcGone(
                        {.npc = a.npc, .reason = tandem::proto::kGoneDead, .killer = a.player});
                    Log("npc {} died", a.npc);
                  }
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::NpcActionAnswered>) {
              Log("npc {}: action {} {}", e.reply.npc, static_cast<int>(e.reply.kind),
                  e.reply.accepted ? std::string("accepted")
                                   : std::format("refused ({})", e.reply.reason));
              if (!e.reply.accepted) {
                if (grab.npc == e.reply.npc) {
                  grab.step = 0;
                  grabRefused.insert(grab.npc);
                }
                if (carry.npc == e.reply.npc)
                  carry.step = 0;
                puppeteer.Beside(std::nullopt);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHold>) {
              Log("npc {} held by player {} (how {})", e.hold.npc, e.hold.player,
                  static_cast<int>(e.hold.how));
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcCue>) {
              Log("npc {}: cue {} for us", e.cue.npc, static_cast<int>(e.cue.cue));
            } else if constexpr (std::is_same_v<E, tandem::events::ShotAtUs>) {
              Log("npc {} fires at us (weapon {:#x}, speed {:.2f}{})", e.shot.npc, e.shot.weapon,
                  e.shot.factor, e.shot.tbh == 0.f ? ", may land" : "");
            } else if constexpr (std::is_same_v<E, tandem::events::ShotHitBy>) {
              Log("player {} reports npc {}'s shot hit their V", e.hit.player, e.hit.npc);
            } else if constexpr (std::is_same_v<E, tandem::events::HitByNpc>) {
              Log("hit by npc {}: {:.1f} physical, {:.1f} thermal, {:.1f} electric, {:.1f} "
                  "chemical (type {}, flags {}, attack {:#x})",
                  e.hit.npc, e.hit.physical, e.hit.thermal, e.hit.electric, e.hit.chemical,
                  e.hit.attackType, e.hit.flags, e.hit.attack);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHealth>) {
              unanswered[e.health.npc] = 0;
              if (options.hitNpcsSeconds)
                Log("npc {} health {:.0f}%", e.health.npc, e.health.health * 100.f);
              if (!options.recordFile.empty())
                recording.Add(e.health);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteSeat>) {
              Log("player {} sits in {:#x} (seat {:#x}, car of player {})", e.seat.player,
                  e.seat.vehicle, e.seat.seat, e.seat.owner);
              resolve(tandem::mods::Kind::Vehicle, e.seat.vehicle, e.seat.hint, e.seat.player);
              driving[e.seat.player] = e.seat.vehicle != 0 &&
                                       e.seat.seat == CName("seat_front_left") &&
                                       e.seat.owner == e.seat.player;
              // A passenger in the car we drive: our radio again, for them.
              if (options.mode == Mode::Drive && options.radioStation && seated && !parked &&
                  e.seat.vehicle != 0 && e.seat.owner == endpoint->You())
                sendRadio(std::format("player {} got in", e.seat.player));
              // Echo: the same seat in our own copy of the car.
              if (options.mode == Mode::Mirror || options.mode == Mode::Front) {
                auto echo = e.seat;
                echo.owner = tandem::proto::kNoPlayer;
                endpoint->SetLocalSeat(echo);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicle>) {
              ++heardVehicles;
              carOf[e.vehicle.player] = e.vehicle;
              if (options.mode == Mode::Mirror || options.mode == Mode::Front) {
                auto echo = e.vehicle;
                echo.position.x += options.offset.x;
                echo.position.y += options.offset.y;
                echo.position.z += options.offset.z;
                endpoint->SendLocalVehicle(echo);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicleRadio>) {
              Log("player {}'s car radio: {} (track {:#x})", e.radio.player,
                  StationName(e.radio.on ? e.radio.station : -1), e.radio.track);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteShot>) {
              ++heardShots;
              Log("player {}: weapon event {} ({} hand)", e.shot.player, e.shot.effect,
                  e.shot.hand == tandem::proto::Hand::Left ? "left" : "right");
              if ((options.mode == Mode::Mirror || options.mode == Mode::Front) &&
                  e.shot.player == followed())
                endpoint->SendLocalShot(e.shot);
            } else if constexpr (std::is_same_v<E, tandem::events::RemotePose>) {
              ++heardPoses;
              if (e.pose.player == followed())
                puppeteer.HeardPose(e.pose);
            } else if constexpr (std::is_same_v<E, tandem::events::NpcHackBy>) {
              // As the host of a replay: the NPC is being hacked for that long.
              Log("player {} uploads {:#x} on npc {} ({} ms)", e.hack.player, e.hack.action,
                  e.hack.npc, e.hack.uploadMs);
              tandem::proto::NpcHackState state{.npc = e.hack.npc,
                                                .player = e.hack.player,
                                                .action = e.hack.action,
                                                .uploadMs = e.hack.uploadMs};
              endpoint->SendNpcHackState(state);
              state.step = tandem::proto::HackStep::Done;
              pendingHacks.push_back(
                  {std::chrono::steady_clock::now() + std::chrono::milliseconds(e.hack.uploadMs),
                   state});
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcHackState>) {
              Log("npc {}: upload {:#x} of player {} step {}", e.state.npc, e.state.action,
                  e.state.player, static_cast<int>(e.state.step));
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcStatus>) {
              Log("npc {} {} effect {:#x} (player {})", e.status.npc,
                  e.status.applied ? "has" : "lost", e.status.effect, e.status.player);
            } else if constexpr (std::is_same_v<E, tandem::events::HostPlayerHack>) {
              Log("npc {} hacks us with {:#x} (flags {})", e.hack.npc, e.hack.action, e.hack.flags);
            } else if constexpr (std::is_same_v<E, tandem::events::VehicleHackOn>) {
              Log("player {} hacked our car: {} ({:.1f} s)", e.hack.player,
                  static_cast<int>(e.hack.hack), e.hack.seconds);
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficHacked>) {
              Log("street: player {} hacked vehicle {:#x}: {} ({:.1f} s){}", e.hack.player,
                  e.hack.id, static_cast<int>(e.hack.hack), e.hack.seconds, e.ours ? ", ours" : "");
            } else if constexpr (std::is_same_v<E, tandem::events::PauseChanged>) {
              Log("player {} {} (pause menu)", e.pause.player,
                  e.pause.paused ? "paused" : "resumed");
            } else if constexpr (std::is_same_v<E, tandem::events::Trade>) {
              using Kind = tandem::trade::Seat::Event::Kind;
              tradeSeat.SetYou(endpoint->You());
              auto heard = tradeSeat.On(e.message, tandem::net::NowUs());
              tradeSend(heard.send);
              for (const auto &event : heard.events) {
                switch (event.kind) {
                case Kind::Asked:
                  Log("trade: player {} asks us to trade", event.with);
                  if (options.tradeAccept) {
                    tradeSend(tradeSeat.Answer(true, tandem::net::NowUs()));
                    Log("trade: accepted: table open with player {}", event.with);
                    tradePut();
                  } else {
                    tradeSend(tradeSeat.Answer(false, tandem::net::NowUs()));
                    Log("trade: declined (--trade-accept to accept)");
                  }
                  break;
                case Kind::Opened:
                  Log("trade: table open with player {}", event.with);
                  tradePut();
                  break;
                case Kind::Offer:
                  Log("trade: their side: {} stacks, {} eddies, asks {}",
                      tradeSeat.Theirs().items.size(), tradeSeat.Theirs().money,
                      tradeSeat.Theirs().asks);
                  break;
                case Kind::Confirmed:
                  // They agree: so do we.
                  Log("trade: player {} agrees: so do we", event.with);
                  tradeSend(tradeSeat.Confirm());
                  break;
                case Kind::Withdrawn:
                  Log("trade: player {} no longer agrees", event.with);
                  break;
                case Kind::Unconfirmed:
                  Log("trade: our agreement did not count (reason {})",
                      static_cast<int>(event.reason));
                  break;
                case Kind::Closed:
                  Log("trade: closed with player {} (reason {}): nothing changed hands", event.with,
                      static_cast<int>(event.reason));
                  break;
                case Kind::Commit:
                  ++tradesDone;
                  Log("trade: applied with player {}: gave {} stacks and {} eddies, took {} "
                      "stacks and {} eddies",
                      event.with, tradeSeat.Give().items.size(), tradeSeat.Give().money,
                      tradeSeat.Take().items.size(), tradeSeat.Take().money);
                  break;
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RegroupOffered>) {
              const auto &o = e.offer;
              Log("player {} offers to join them (offer {}, reason {}, point {:#x}{}) at ({:.1f}, "
                  "{:.1f}, {:.1f})",
                  o.from, o.offer, static_cast<int>(o.reason), o.point,
                  o.label.empty() ? "" : ", " + o.label, o.position.x, o.position.y, o.position.z);
              if (options.regroupAnswer == Options::Answer::Ignore) {
                Log("offer {} of player {}: not answered", o.offer, o.from);
              } else {
                const bool yes = options.regroupAnswer == Options::Answer::Yes;
                endpoint->SendRegroupAnswer({.to = o.from, .offer = o.offer, .accepted = yes});
                if (yes) {
                  // There, beside them, as a game lands next to whom it joins.
                  puppeteer.StandAt(Vec3{o.position.x + 1.5f, o.position.y, o.position.z});
                  Log("joining player {} at ({:.1f}, {:.1f}, {:.1f})", o.from, o.position.x,
                      o.position.y, o.position.z);
                } else
                  Log("offer {} of player {}: declined", o.offer, o.from);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::MarkFrom>) {
              const auto &m = e.mark;
              Log("player {} pinged: mark {} kind {} target {}:{:#x} at ({:.1f}, {:.1f}, {:.1f}) "
                  "for {} s",
                  m.from, m.id, static_cast<int>(m.kind), static_cast<int>(m.target.kind),
                  m.target.id, m.position.x, m.position.y, m.position.z, m.seconds);
            } else if constexpr (std::is_same_v<E, tandem::events::TagFrom>) {
              Log("player {} {} {} target(s) ({}, {} s)", e.tag.from,
                  e.tag.on ? "tagged" : "untagged", e.tag.targets.size(),
                  e.tag.reason == tandem::proto::TagReason::Reveal ? "reveal" : "scan",
                  e.tag.seconds);
              // A reveal of a placed entity: the Ping quickhack reached a device.
              for (const auto &target : e.tag.targets)
                if (e.tag.on && e.tag.reason == tandem::proto::TagReason::Reveal &&
                    target.kind == tandem::proto::TargetKind::Entity)
                  Log("player {} pulsed the network from device {:x} ({} s)", e.tag.from, target.id,
                      e.tag.seconds);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteLink>) {
              const auto &l = e.link;
              const bool theyAsk = tandem::comms::Consents(&l, endpoint->You());
              const bool weDo = tandem::comms::Consents(&ownLink, l.player);
              Log("player {}'s link: mode {}, {} listed: {}", l.player, static_cast<int>(l.mode),
                  l.players.size(),
                  theyAsk && weDo ? "linked"
                  : theyAsk       ? "they ask us"
                  : weDo          ? "we ask them"
                                  : "not linked");
              // --link ask: every request is accepted.
              if (theyAsk && !weDo && options.linkConsent == Options::LinkAnswer::Ask &&
                  options.callOutcome != Options::CallOutcome::Answer) {
                const bool waiting = std::ranges::any_of(
                    linkDeclines, [&](const auto &d) { return d.player == l.player; });
                if (!waiting) {
                  using O = Options::CallOutcome;
                  const auto seconds = options.callOutcome == O::Busy ? 0
                                       : options.callOutcome == O::Ignore
                                           ? 15
                                           : options.linkAnswerSeconds;
                  const auto reason =
                      options.callOutcome == O::Busy     ? tandem::proto::kLinkDeclineBusy
                      : options.callOutcome == O::Ignore ? tandem::proto::kLinkDeclineMissed
                                                         : tandem::proto::kLinkDeclineRefused;
                  linkDeclines.push_back(
                      {l.player,
                       tandem::net::NowUs() + static_cast<std::uint64_t>(seconds) * 1'000'000,
                       reason});
                  Log("player {}'s link request: declined (reason {}) in {} s", l.player, reason,
                      seconds);
                }
              } else if (theyAsk && !weDo && options.linkConsent == Options::LinkAnswer::Ask) {
                if (options.linkAnswerSeconds > 0) {
                  const bool waiting = std::ranges::any_of(
                      linkAnswers, [&](const auto &answer) { return answer.first == l.player; });
                  if (!waiting) {
                    linkAnswers.emplace_back(
                        l.player,
                        tandem::net::NowUs() +
                            static_cast<std::uint64_t>(options.linkAnswerSeconds) * 1'000'000);
                    Log("player {}'s link request: answered in {} s", l.player,
                        options.linkAnswerSeconds);
                  }
                } else {
                  ownLink = tandem::comms::WithConsent(ownLink, l.player, true);
                  endpoint->SetLocalLink(ownLink);
                  Log("accepted player {}'s link request", l.player);
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::LinkDeclined>) {
              Log("player {} did not take our link request (reason {})", e.decline.from,
                  e.decline.reason);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteWaypoint>) {
              const auto &w = e.waypoint;
              if (w.kind == tandem::proto::WaypointKind::None)
                Log("player {}'s waypoint: none", w.player);
              else if (w.kind == tandem::proto::WaypointKind::Objective)
                Log("player {}'s waypoint: objective at ({:.1f}, {:.1f}, {:.1f})", w.player,
                    w.position.x, w.position.y, w.position.z);
              else
                Log("player {}'s waypoint: kind {} at ({:.1f}, {:.1f}, {:.1f})", w.player,
                    static_cast<int>(w.kind), w.position.x, w.position.y, w.position.z);
            } else if constexpr (std::is_same_v<E, tandem::events::RegroupAnswered>) {
              Log("player {} {} (offer {})", e.answer.from,
                  e.answer.accepted ? (e.answer.offer ? "accepted: on the way" : "is on the way")
                                    : "declined: stays",
                  e.answer.offer);
            } else if constexpr (std::is_same_v<E, tandem::events::HostActorPose>) {
              // Scene replay: the host's actors' bodies, counted.
              if (++actorPoses % 100 == 1)
                Log("scene: {} actor pose(s) heard (npc {})", actorPoses, e.pose.npc);
            } else if constexpr (std::is_same_v<E, tandem::events::Story>) {
              namespace proto = tandem::proto;
              // So is a face or a body on another rig.
              if (const auto *said = std::get_if<proto::RigPose>(&e.message)) {
                if (++rigPoses % 30 == 1)
                  Log("scene rig: {} pose(s) heard (npc {}, {}, {} bytes of a rig of {} bones)",
                      rigPoses, said->npc, said->part == proto::kRigFace ? "face" : "body",
                      said->data.size(), said->bones);
                return;
              }
              // The host's camera is a stream: counted, not listed.
              if (const auto *camera = std::get_if<proto::SceneCamera>(&e.message)) {
                if (++cameraViews % 60 == 1 || (camera->flags & proto::kCameraCut))
                  Log("scene camera: {} view(s) heard (play {}, fov {:.0f}{})", cameraViews,
                      camera->play, camera->fov,
                      camera->flags & proto::kCameraCut ? ", a cut" : "");
                return;
              }
              Log("{}", Describe(e.message));
              if (!options.recordFile.empty())
                recording.Add(e.message);
              if (const auto *call = std::get_if<proto::PhoneCall>(&e.message);
                  call && options.answerSeconds >= 0 && call->phase == 1 &&
                  call->answeredBy == proto::kNoPlayer)
                toAnswer = {call->contact, std::chrono::steady_clock::now() +
                                               std::chrono::seconds(options.answerSeconds)};
              if (const auto *hub = std::get_if<proto::ChoiceHub>(&e.message);
                  hub && options.hoverIndex >= 0 && !hub->options.empty() && !options.host) {
                endpoint->SendStory(
                    proto::ChoiceCursor{.hub = hub->hub,
                                        .index = static_cast<std::int8_t>(options.hoverIndex),
                                        .kind = proto::kCursorHover});
                Log("hover option {} of hub {}", options.hoverIndex, hub->hub);
              }
              if (const auto *hub = std::get_if<proto::ChoiceHub>(&e.message);
                  hub && options.suggestIndex >= 0 && !hub->options.empty() && !options.host) {
                endpoint->SendStory(
                    proto::ChoiceCursor{.hub = hub->hub,
                                        .index = static_cast<std::int8_t>(options.suggestIndex),
                                        .kind = proto::kCursorSuggest});
                Log("suggest option {} of hub {}", options.suggestIndex, hub->hub);
              }
              // Quest interactions: as a host (no world of ours), a guest's
              // use answered as --interact-answer says; as a guest, the verdict.
              if (const auto *use = std::get_if<proto::QuestInteract>(&e.message)) {
                if (use->verdict == proto::kInteractAsk && options.host &&
                    options.interactAnswer >= 0) {
                  auto answer = *use;
                  answer.verdict = static_cast<std::uint8_t>(options.interactAnswer);
                  endpoint->SendStory(answer, use->player);
                  Log("interact: player {}'s use of {:x}: {}", use->player, use->target,
                      tandem::interact::VerdictName(answer.verdict));
                } else if (use->verdict != proto::kInteractAsk) {
                  Log("interact: the host on our use of {:x} (question {}): {}", use->target,
                      use->seq, tandem::interact::VerdictName(use->verdict));
                }
              }
              // As a scripted host: a guest's answer that counted.
              if (const auto *action = std::get_if<proto::PhoneAction>(&e.message);
                  action && options.host)
                Log("phone: {} by player {}",
                    action->action == proto::kPhoneReject ? "rejected" : "answered",
                    action->player);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteTransit>) {
              constexpr const char *kKinds[] = {"none", "metro", "ride", "scene", "along"};
              const auto kind = static_cast<std::size_t>(e.transit.kind);
              Log("player {} ride: {} (flags {:#x}, station {}, next {}, seat {:#x}, carrier {})",
                  e.transit.player, kind < std::size(kKinds) ? kKinds[kind] : "?", e.transit.flags,
                  e.transit.at, e.transit.next, e.transit.seat, e.transit.carrier);
              namespace proto = tandem::proto;
              const bool offered = e.transit.kind == proto::TransitKind::Scene &&
                                   (e.transit.flags & proto::kTransitSeats) != 0;
              if (options.rideAlong && offered && !alongWith) {
                alongWith = e.transit.player;
                endpoint->SetLocalTransit(
                    {.kind = proto::TransitKind::Along, .carrier = e.transit.player});
                Log("riding along with player {}", e.transit.player);
              } else if (alongWith && *alongWith == e.transit.player && !offered) {
                alongWith.reset();
                endpoint->SetLocalTransit({});
                gotOut = true;
                Log("out of player {}'s ride along, beside the car", e.transit.player);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcSeat>) {
              Log("npc {} {} (seat {:#x})", e.seat.npc,
                  e.seat.owner ? std::format("sits in player {}'s vehicle", e.seat.owner)
                               : std::string("got out"),
                  e.seat.seat);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteTimeEffect>) {
              Log("player {}: time abilities {:#x} (world speed {:.2f})", e.effect.player,
                  e.effect.abilities, e.effect.scale);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteElsewhere>) {
              const auto &away = e.state;
              if (away.place == tandem::proto::Place::City)
                Log("player {} is back in the city", away.player);
              else
                Log("player {} is in {}{}", away.player, PlaceName(away.place),
                    away.flags & tandem::proto::kElsewhereMoved
                        ? std::format(", body {} at ({:.1f}, {:.1f}, {:.1f})",
                                      away.flags & tandem::proto::kElsewhereSettled ? "settled"
                                                                                    : "left",
                                      away.anchor.x, away.anchor.y, away.anchor.z)
                        : std::string(" (in place)"));
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteActivity>) {
              const auto &busy = e.state;
              if (busy.kind == tandem::proto::ActivityKind::None)
                Log("player {} is free again", busy.player);
              else
                Log("player {} is busy: {} (flags {:#x}, workspot {:#x}, entry {}, device "
                    "{:#x}){}",
                    busy.player, ActivityName(busy.kind), busy.flags, busy.workspot, busy.entry,
                    busy.device,
                    busy.flags & tandem::proto::kActivityAnchored
                        ? std::format(" at ({:.1f}, {:.1f}, {:.1f})", busy.anchor.x, busy.anchor.y,
                                      busy.anchor.z)
                        : std::string());
            } else if constexpr (std::is_same_v<E, tandem::events::TimeSkipAsked>) {
              const auto &k = e.skip;
              const auto hours = (k.toSeconds - k.fromSeconds) / 3600.0;
              Log("player {} asks to skip {:.1f} h (request {}, reason {})", k.from, hours,
                  k.request, static_cast<int>(k.reason));
              if (options.skipAnswer == Options::Answer::Ignore) {
                Log("time skip {} of player {}: not answered", k.request, k.from);
              } else if (options.skipAnswer == Options::Answer::Sleep) {
                // Two beds at once: our own sleep, which the host counts as
                // our yes to theirs.
                const auto ours = endpoint->AskTimeSkip(tandem::proto::SkipReason::Sleep,
                                                        k.fromSeconds, k.toSeconds);
                Log("time skip {} of player {}: we go to bed too (request {})", k.request, k.from,
                    ours);
              } else {
                const bool yes = options.skipAnswer == Options::Answer::Yes;
                endpoint->AnswerTimeSkip(k.from, k.request, yes);
                Log("time skip {} of player {}: {}", k.request, k.from,
                    yes ? "accepted" : "declined");
              }
            } else if constexpr (std::is_same_v<E, tandem::events::TimeSkipVoted>) {
              Log("player {} {} our time skip {}", e.vote.from,
                  e.vote.accepted ? "accepted" : "declined", e.vote.request);
            } else if constexpr (std::is_same_v<E, tandem::events::TimeSkipEnded>) {
              const auto &r = e.result;
              Log("time skip {} of player {}: {} (by {}, why {}), {:.1f} h", r.request, r.asker,
                  SkipOutcomeName(r.outcome), r.by, r.why, (r.toSeconds - r.fromSeconds) / 3600.0);
              if (r.outcome == tandem::proto::SkipOutcome::Applied && options.host &&
                  r.reason != tandem::proto::SkipReason::Quest) {
                // Our clock moves by what was asked; the World goes out now.
                skippedSeconds += r.toSeconds - r.fromSeconds;
                nextWorld = std::chrono::steady_clock::now();
              }
            } else if constexpr (std::is_same_v<E, tandem::events::HostWorld>) {
              const auto heardAt = std::chrono::steady_clock::now();
              if (hostClock && hostClockAt) {
                const double real = std::chrono::duration<double>(heardAt - *hostClockAt).count();
                if (real > 1.0)
                  Log("host clock {} s: {} game s in {:.1f} s (rate {:.2f})",
                      e.world.gameTimeSeconds, e.world.gameTimeSeconds - *hostClock, real,
                      (e.world.gameTimeSeconds - *hostClock) / real);
              }
              hostClock = e.world.gameTimeSeconds;
              hostClockAt = heardAt;
            } else if constexpr (std::is_same_v<E, tandem::events::WorldItemAppeared>) {
              const auto &item = e.item;
              Log("world item {} from player {}: {} stack(s), first {:#x} x{} (token {})", item.id,
                  item.dropper, item.stacks.size(), item.stacks.empty() ? 0 : item.stacks[0].record,
                  item.stacks.empty() ? 0 : item.stacks[0].quantity, item.token);
              if (options.takeItemsSeconds >= 0 && item.dropper != endpoint->You())
                toTake[item.id] = {std::chrono::steady_clock::now() +
                                       std::chrono::seconds(options.takeItemsSeconds),
                                   item};
            } else if constexpr (std::is_same_v<E, tandem::events::SharedItemFrom>) {
              Log("shared item {:#x} x{} from player {} ({}, npc {})", e.item.item.record,
                  e.item.item.quantity, e.item.player,
                  e.item.kind == tandem::proto::kSharedOnCorpse ? "on a corpse" : "looted",
                  e.item.npc);
            } else if constexpr (std::is_same_v<E, tandem::events::WorldItemTakenBy>) {
              Log("world item {} stack {} taken by player {}", e.taken.id, e.taken.stack,
                  e.taken.taker);
              // Someone was first: nothing left of it for us to claim.
              if (auto it = toTake.find(e.taken.id); it != toTake.end()) {
                auto &stacks = it->second.second.stacks;
                if (e.taken.stack < stacks.size())
                  stacks[e.taken.stack].quantity = 0;
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVitals>) {
              constexpr const char *kLives[] = {"up", "down", "out"};
              Log("player {} is {} (flags {:#x})", e.vitals.player,
                  kLives[static_cast<int>(e.vitals.life)], e.vitals.flags);
              if (e.vitals.life == tandem::proto::Life::Down && options.reviveSeconds >= 0 &&
                  ourLife == 0 && !rescue)
                rescue = Rescue{e.vitals.player, std::chrono::steady_clock::now() +
                                                     std::chrono::seconds(options.reviveSeconds)};
              if (e.vitals.life != tandem::proto::Life::Down && rescue &&
                  rescue->target == e.vitals.player) {
                Log("revive of player {}: {}", e.vitals.player,
                    rescue->started ? "they are up" : "no longer needed");
                rescue.reset();
              }
            } else if constexpr (std::is_same_v<E, tandem::events::ReviveBy>) {
              constexpr const char *kSteps[] = {"starts reviving us", "stopped reviving us",
                                                "revived us"};
              Log("player {} {}", e.revive.player, kSteps[static_cast<int>(e.revive.step)]);
              if (e.revive.step == tandem::proto::ReviveStep::Done && ourLife == 1) {
                lying.reset();
                setLife(0, std::format("revived by player {}", e.revive.player));
              }
            } else if constexpr (std::is_same_v<E, tandem::events::SessionLost>) {
              Log("session lost: no V is up (loss {})", e.over.round);
            } else if constexpr (std::is_same_v<E, tandem::events::HostDevice>) {
              const auto key = tandem::devices::KeyOf(e.state);
              const auto what = tandem::devices::WhatOf(e.state);
              Log("{} by player {}, {}", describeDevice(key, what), e.state.by,
                  e.state.how == tandem::proto::kDeviceQuiet ? "set" : "played");
              // Another player's doing, just now: ours to change back.
              if (e.state.how == tandem::proto::kDeviceLive &&
                  e.state.by != tandem::proto::kNoPlayer && e.state.by != endpoint->You())
                answerDevice(key, what);
            } else if constexpr (std::is_same_v<E, tandem::events::DeviceAsked>) {
              Log("{} asked by player {}: no world of ours, no answer",
                  describeDevice(tandem::devices::KeyOf(e.state), tandem::devices::WhatOf(e.state)),
                  e.state.by);
            } else if constexpr (std::is_same_v<E, tandem::events::DeviceRequested>) {
              // No world of ours: the device is what the guest made it.
              const auto key = tandem::devices::KeyOf(e.use);
              const auto what = tandem::devices::WhatOf(e.use);
              endpoint->ReportDevice({.entity = key.entity,
                                      .cls = key.cls,
                                      .status = what.status,
                                      .flags = what.flags,
                                      .value = what.value,
                                      .by = e.use.player});
              Log("{} operated by player {}: taken as said", describeDevice(key, what),
                  e.use.player);
              answerDevice(key, what);
            } else if constexpr (std::is_same_v<E, tandem::events::BreachBy>) {
              // No world of ours: the daemons are taken as run.
              const auto &b = e.breach;
              Log("breach by player {}: network {:x} npc {}, {} daemon(s), {} trap(s){}{}: {}",
                  b.player, b.entity, b.npc, b.programs.size(), b.traps.size(),
                  (b.flags & tandem::proto::kBreachRemote) ? ", remote" : "",
                  (b.flags & tandem::proto::kBreachFailed) ? ", lost" : "",
                  endpoint->IsHost() ? "applied to its network and our NPCs"
                                     : "applied to our NPCs");
            } else if constexpr (std::is_same_v<E, tandem::events::BreachAnswered>) {
              const auto &r = e.result;
              constexpr const char *kVerdicts[] = {"granted", "refused (breached already)",
                                                   "refused (the record is full)"};
              Log("our breach of network {:x} npc {}: {}, by player {}", r.entity, r.npc,
                  kVerdicts[static_cast<int>(r.verdict)], r.by);
              if (const auto decision = breachHolds.Answer(r))
                breachSettled(*decision);
            } else if constexpr (std::is_same_v<E, tandem::events::DeviceFired>) {
              using Kind = tandem::proto::DeviceFireKind;
              const auto &f = e.fire;
              constexpr const char *kKinds[] = {"follows nobody", "turns to", "fires at",
                                                "has the V of", "lost the V of"};
              if (f.kind == Kind::Seen || f.kind == Kind::Lost) {
                Log("turret {:x}: player {}'s copy {} player {}", f.entity, f.player,
                    kKinds[static_cast<int>(f.kind)], f.player);
                std::erase_if(turretBursts, [&](const TurretBurst &burst) {
                  return burst.entity == f.entity && burst.player == f.player;
                });
                // No world of ours: the turret is where the guest says, and
                // takes that guest's V at once.
                if (f.kind == Kind::Seen && options.turretShots > 0) {
                  const tandem::proto::TargetRef target{tandem::proto::TargetKind::Player,
                                                        f.player};
                  endpoint->SendDeviceFire(
                      {.entity = f.entity, .kind = Kind::Aim, .target = target, .at = f.at});
                  turretBursts.push_back({f.entity, f.player, f.at, options.turretShots,
                                          std::chrono::steady_clock::now() + 500ms});
                }
              } else {
                Log("turret {:x}: the host's {} target kind {} id {} at ({:.1f} {:.1f} {:.1f})",
                    f.entity, kKinds[static_cast<int>(f.kind)], static_cast<int>(f.target.kind),
                    f.target.id, f.at.x, f.at.y, f.at.z);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::TurretHeld>) {
              const auto &h = e.hands;
              const auto [it, fresh] = turretsHeld.try_emplace(h.entity, h.flags);
              if (fresh || it->second != h.flags) {
                it->second = h.flags;
                Log("turret {:x}: in player {}'s hands, {} ({:.1f} {:.1f} {:.1f})", h.entity,
                    h.player,
                    (h.flags & tandem::proto::kHandsLetGo) != 0    ? "let go"
                    : (h.flags & tandem::proto::kHandsFiring) != 0 ? "firing at"
                                                                   : "pointing at",
                    h.at.x, h.at.y, h.at.z);
              }
              if ((h.flags & tandem::proto::kHandsLetGo) != 0)
                turretsHeld.erase(h.entity);
            } else if constexpr (std::is_same_v<E, tandem::events::TurretRefused>) {
              Log("turret {:x}: our claim refused, player {} holds it", e.busy.entity,
                  e.busy.holder);
            } else if constexpr (std::is_same_v<E, tandem::events::Fractured>) {
              const auto &f = e.fracture;
              Log("player {} broke {} at ({:.1f} {:.1f} {:.1f})", f.player,
                  f.entity != 0 ? std::format("part {:#x} of {:x}", f.component, f.entity)
                                : std::string("a piece of the world"),
                  f.at.x, f.at.y, f.at.z);
            } else if constexpr (std::is_same_v<E, tandem::events::DevicesReset>) {
              Log("devices: the host sends its record again");
            } else if constexpr (std::is_same_v<E, tandem::events::Resync>) {
              if (e.player == endpoint->You())
                Log("resync: asked the host for domains {:#x}", e.domains);
              else {
                ++resyncsAsked;
                Log("resync: player {} asked for domains {:#x}", e.player, e.domains);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::EpochChanged>) {
              Log("epoch: player {}'s world was rebuilt (reason {}, epoch {})", e.epoch.player,
                  e.epoch.reason, e.epoch.epoch);
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficSpawned>) {
              sharedCars[e.spawn.id] = e.spawn;
              Log("street: vehicle {:#x} ({:#x}, flags {:#x}) at ({:.1f}, {:.1f}, {:.1f}), {} "
                  "shared",
                  e.spawn.id, e.spawn.record, e.spawn.flags, e.spawn.position.x, e.spawn.position.y,
                  e.spawn.position.z, sharedCars.size());
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficMoved>) {
              for (const auto &car : e.states.cars)
                if (const auto it = sharedCars.find(car.id); it != sharedCars.end())
                  it->second.position = car.position;
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficLeft>) {
              sharedCars.erase(e.gone.id);
              for (auto &car : street)
                if (car.id == e.gone.id)
                  car.gone = true;
              if (e.gone.reason == tandem::proto::kTrafficTaken)
                Log("street: vehicle {:#x} taken: it is player {}'s car now{}", e.gone.id,
                    e.gone.player, e.gone.player == endpoint->You() ? " (ours)" : "");
              else
                Log("street: vehicle {:#x} gone (reason {})", e.gone.id, e.gone.reason);
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficChanged>) {
              if (const auto it = sharedCars.find(e.status.id); it != sharedCars.end())
                it->second.flags = static_cast<std::uint16_t>(
                    (it->second.flags & ~tandem::proto::kTrafficStatusMask) | e.status.flags);
              Log("street: vehicle {:#x} health {:.2f}, flags {:#x}{}", e.status.id,
                  e.status.health, e.status.flags,
                  e.status.flags & tandem::proto::kTrafficDestroyed ? " (a wreck)" : "");
            } else if constexpr (std::is_same_v<E, tandem::events::TrafficHitBy>) {
              // Ours to apply: 1000 health points a car, a wreck at none.
              for (auto &car : street) {
                if (car.id != e.hit.id || car.gone)
                  continue;
                car.health = std::max(0.f, car.health - e.hit.damage / 1000.f);
                if (car.health <= 0.f) {
                  car.flags |= tandem::proto::kTrafficDestroyed;
                  car.speed = 0.f;
                }
                endpoint->SendTrafficStatus(
                    {.id = car.id, .health = car.health, .flags = car.flags});
                Log("street: player {} hit vehicle {:#x} for {:.0f}{}: health {:.2f}", e.hit.player,
                    e.hit.id, e.hit.damage,
                    e.hit.flags & tandem::proto::kTrafficHitCollision ? " (a ram)" : "",
                    car.health);
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteHeat>) {
              Log("police: player {} wanted level {} (state {})", e.heat.player, e.heat.stage,
                  static_cast<int>(e.heat.state));
            } else if constexpr (std::is_same_v<E, tandem::events::CrimeReported>) {
              Log("police: the area's owner saw our V commit a crime (kind {}, npc {})",
                  static_cast<int>(e.crime.kind), e.crime.npc);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteInstall>) {
              Log("{} (player {}) runs tandem {}", e.name, e.player, e.install.build);
              for (const auto &line : e.differences)
                Log("notice: {}", line);
            } else if constexpr (std::is_same_v<E, tandem::events::SubstitutesFrom>) {
              Log("mods: {} sees substitutes for {} of ours (kinds {:#x})", e.name,
                  e.substitutes.count, e.substitutes.kinds);
            } else if constexpr (std::is_same_v<E, tandem::events::JoinRefused>) {
              Log("notice: {}", e.notice);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicleDamage>) {
              namespace proto = tandem::proto;
              const auto &d = e.damage;
              const auto it = heardDamage.find(d.vehicle);
              const auto before = it == heardDamage.end()
                                      ? std::nullopt
                                      : std::optional<proto::VehicleDamage>(it->second);
              const auto end = vc::OnArrival(d, before, endpoint->HostNowUs());
              heardDamage[d.vehicle] = d;
              Log("vehicle {:#x} (of player {}): health {:.2f}, flags {:#x}, tyres {:#x}, {} "
                  "part(s) "
                  "off{}",
                  d.vehicle, proto::OwnerOfVehicle(d.vehicle), d.health, d.flags, d.tyres,
                  d.parts.size(),
                  end == vc::Destruction::Explode ? ": it blows up"
                  : end == vc::Destruction::Wreck ? ": a wreck"
                                                  : "");
            } else if constexpr (std::is_same_v<E, tandem::events::VehicleHitBy>) {
              namespace proto = tandem::proto;
              const auto &hit = e.hit;
              if (hit.vehicle == proto::PlayerCar(endpoint->You())) {
                // Ours to apply: 1000 health points a car.
                auto &car = ourCarDamage();
                car = vc::Apply(car, hit, 1000.f, endpoint->HostNowUs());
                endpoint->SetVehicleDamage(car);
                Log("our car: player {} hit it for {:.0f}{}: health {:.2f}{}", hit.player,
                    hit.damage,
                    hit.flags & proto::kVehicleHitTyre ? std::format(", tyre {} flat", hit.tyre)
                                                       : "",
                    car.health, car.flags & proto::kDamageDestroyed ? " (a wreck)" : "");
              } else if (proto::IsStreetCar(hit.vehicle)) {
                Log("street: player {} hit vehicle {:#x}{}", hit.player,
                    proto::TrafficOfSeat(hit.vehicle),
                    hit.flags & proto::kVehicleHitTyre ? std::format(": tyre {} flat", hit.tyre)
                                                       : "");
                if (hit.flags & proto::kVehicleHitTyre) {
                  auto damage = endpoint->VehicleDamages().contains(hit.vehicle)
                                    ? endpoint->VehicleDamages().at(hit.vehicle)
                                    : proto::VehicleDamage{.vehicle = hit.vehicle};
                  damage = vc::Apply(damage, {.flags = hit.flags, .tyre = hit.tyre}, 1000.f,
                                     endpoint->HostNowUs());
                  endpoint->SetVehicleDamage(damage);
                }
              }
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicleExit>) {
              constexpr const char *kKinds[] = {"got out",           "bailed out",
                                                "was thrown out",    "was knocked off",
                                                "died at the wheel", "was blown out"};
              Log("player {} {} of vehicle {:#x} at {:.1f} m/s{}", e.exit.player,
                  kKinds[static_cast<int>(e.exit.kind)], e.exit.vehicle,
                  std::hypot(e.exit.velocity.x, e.exit.velocity.y, e.exit.velocity.z),
                  e.exit.flags & tandem::proto::kExitRolling ? ": it rolls on" : "");
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteVehicleCombat>) {
              constexpr const char *kModes[] = {"none", "driver combat", "passenger combat",
                                                "mounted weapons", "gunner"};
              Log("player {} in vehicle {:#x}: {} (weapon {:#x})", e.combat.player,
                  e.combat.vehicle, kModes[static_cast<int>(e.combat.mode)], e.combat.weapon);
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteMountedFire>) {
              Log("player {} fires vehicle {:#x}'s weapon {:#x}", e.fire.player, e.fire.vehicle,
                  e.fire.weapon);
            } else if constexpr (std::is_same_v<E, tandem::events::GunnerChanged>) {
              Log("vehicle {:#x}: gunner {}{}", e.gunner.vehicle, e.gunner.gunner,
                  e.gunner.gunner == tandem::proto::kNoPlayer ? " (free)"
                  : e.gunner.gunner == endpoint->You()        ? " (us)"
                                                              : "");
            } else if constexpr (std::is_same_v<E, tandem::events::RemoteGunnerAim>) {
              auto &at = aimLoggedAt[e.aim.player];
              if (std::chrono::steady_clock::now() - at > 2s) {
                at = std::chrono::steady_clock::now();
                Log("player {} aims vehicle {:#x}'s gun: yaw {:.2f}, pitch {:.2f}{}", e.aim.player,
                    e.aim.vehicle, e.aim.yaw, e.aim.pitch,
                    e.aim.flags & tandem::proto::kAimFiring ? ", firing" : "");
              }
            } else if constexpr (std::is_same_v<E, tandem::events::VehicleBumpBy>) {
              bumps.Receive(e.bump, endpoint->HostNowUs());
            } else if constexpr (std::is_same_v<E, tandem::events::NpcRamBy>) {
              Log("player {} ran into npc {} at {:.1f} m/s{}", e.ram.player, e.ram.npc,
                  std::hypot(e.ram.velocity.x, e.ram.velocity.y, e.ram.velocity.z),
                  e.ram.flags & tandem::proto::kRamLethal      ? " (lethal)"
                  : e.ram.flags & tandem::proto::kRamKnockdown ? " (knocked down)"
                                                               : "");
            } else if constexpr (std::is_same_v<E, tandem::events::ChaseChanged>) {
              if (e.chase.target == tandem::proto::kNoPlayer)
                Log("street: vehicle {:#x} chases nobody now", e.chase.vehicle);
              else
                Log("street: vehicle {:#x} chases player {}{}", e.chase.vehicle, e.chase.target,
                    e.chase.target == endpoint->You() ? " (us)" : "");
            } else if constexpr (std::is_same_v<E, tandem::events::Ended>) {
              Log("session ended: {}", e.reason);
              referee.End();
              g_stop = 1;
            }
          },
          event);
    }
    referee.Tick();
    for (const auto &finding : referee.Take())
      Log("{}", finding.Line());
    if (auto relay = endpoint->Relay()) {
      relay->codeSecondsLeft = 0; // not a change worth a line
      if (relay != relayShown) {
        using State = tandem::net::RelayStatus::State;
        if (!relay->code.empty() && (!relayShown || relayShown->code != relay->code))
          Log("join code {} (relay {}:{})", tandem::net::DisplayJoinCode(relay->code),
              options.relay->address, options.relay->port);
        if (relay->state == State::Ready && (!relayShown || relayShown->state != State::Ready))
          Log("relay: registered{}", endpoint->ThroughRelay() ? ", joining through it" : "");
        if (!relay->error.empty() && (!relayShown || relayShown->error != relay->error))
          Log("relay: {}", relay->error);
        relayShown = relay;
      }
    }

    if (!options.savePlayFile.empty() && !savedPlay.IsOpen() && endpoint->IsJoined()) {
      std::string saveError;
      if (savedPlay.Open(options.savePlayFile,
                         {.startHostUs = endpoint->HostNowUs(),
                          .player = endpoint->You(),
                          .body = options.body},
                         saveError)) {
        for (const auto &message : endpoint->LocalSnapshot())
          savedPlay.Add(endpoint->HostNowUs(), message);
        endpoint->SetTap([&](const tandem::proto::Message &message) {
          savedPlay.Add(endpoint->HostNowUs(), message);
        });
        Log("saving our play to {}", options.savePlayFile);
      } else {
        Log("--save-play: {}", saveError);
        options.savePlayFile.clear();
      }
    }

    // --play: the recording's messages as their time comes, once someone
    // else is there to see them.
    if (playback && endpoint->IsJoined() && !playback->Done()) {
      const auto nowUs = tandem::net::NowUs();
      if (!playStartUs && endpoint->Players().size() > 1) {
        playStartUs = nowUs;
        Log("play: started");
      }
      if (playStartUs) {
        const auto &header = playback->Of().header;
        tandem::play::Mapping mapping{.self = header.player, .offset = options.playOffset};
        for (const auto &[id, info] : endpoint->Players())
          if (id != endpoint->You()) {
            mapping.other = id;
            break;
          }
        // A recorded NPC: the live one nearest where the hit landed (else
        // nearest our V), within 30 m, then that one for the rest.
        mapping.npc = [&](tandem::proto::NpcId recorded,
                          const Vec3 *near) -> std::optional<tandem::proto::NpcId> {
          if (const auto it = playNpcs.find(recorded);
              it != playNpcs.end() && liveNpcs.contains(it->second))
            return it->second;
          const Vec3 at = near ? *near : playedAt;
          std::optional<tandem::proto::NpcId> best;
          float bestDistance = 30.f;
          for (const auto npc : liveNpcs)
            if (const auto where = npcAt.find(npc); where != npcAt.end()) {
              const float d = std::hypot(where->second.x - at.x, where->second.y - at.y,
                                         where->second.z - at.z);
              if (d < bestDistance) {
                bestDistance = d;
                best = npc;
              }
            }
          if (best) {
            playNpcs[recorded] = *best;
            Log("play: npc {} of the recording is npc {} here ({:.1f} m)", recorded, *best,
                bestDistance);
          }
          return best;
        };
        const auto lap = playback->Lap();
        for (const auto *entry : playback->Due(nowUs - *playStartUs)) {
          const auto shift = static_cast<std::int64_t>(endpoint->HostNowUs()) -
                             static_cast<std::int64_t>(header.startHostUs + entry->atUs);
          auto message = tandem::play::Adapt(entry->message, mapping, shift);
          if (!message) {
            ++playDropped;
            continue;
          }
          if (const auto *state = std::get_if<PlayerState>(&*message))
            playedAt = state->position;
          else if (const auto *seat = std::get_if<tandem::proto::Seat>(&*message))
            Log("play: {}", seat->vehicle ? std::format("seated in {:#x}", seat->vehicle)
                                          : std::string("on foot"));
          else if (!std::holds_alternative<tandem::proto::PoseState>(*message) &&
                   !std::holds_alternative<tandem::proto::VehicleState>(*message))
            Log("play: {}", tandem::proto::NameOf(*message));
          tandem::play::Apply(*endpoint, *message);
          ++playSent;
        }
        if (playback->Lap() != lap)
          Log("play: lap {}", playback->Lap() + 1);
        if (playback->Done())
          Log("play: done, {} messages sent, {} not for this session", playSent, playDropped);
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= nextSend) {
      nextSend += interval;
      if (nextSend < now)
        nextSend = now + interval;
      if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
        togetherSince = now;
      auto state = puppeteer.Next(tandem::net::NowUs());
      // Down or out: where it fell, still. Reviving: beside the downed V.
      if (ourLife != 0) {
        if (!lying)
          lying = state.value_or(PlayerState{});
        lying->velocity = {};
        state = lying;
      } else if (rescue && rescue->started && lastOf.contains(rescue->target)) {
        auto beside = lastOf[rescue->target];
        beside.position.x += 1.f;
        beside.velocity = {};
        state = beside;
      }
      // Riding along: where the car is; out of it, beside where it stopped.
      const auto car = alongWith ? carOf.find(*alongWith) : carOf.end();
      if (car != carOf.end()) {
        PlayerState carried;
        carried.position = car->second.position;
        carried.orientation = car->second.orientation;
        carried.velocity = car->second.velocity;
        state = carried;
        lastCarried = carried;
      } else if (gotOut && lastCarried) {
        auto beside = *lastCarried;
        beside.position.x += 2.5f;
        beside.velocity = {};
        state = beside;
      }
      // Our V away from its body (world ... moved): the body stays put.
      if (heldBody)
        state = heldBody;
      else if (state)
        lastState = *state;
      if (state) {
        endpoint->SendLocalState(*state);
        lastSent = state->position;
        // --footsteps: what its game's state machine broadcasts as it
        // moves (StandEvents above 2.5 m/s, SprintEvents; a crouched V
        // none), through the gate the plugin sends them through.
        if (options.footsteps && endpoint->IsJoined()) {
          const float speed = std::hypot(state->velocity.x, state->velocity.y);
          const bool crouched = state->stance == 1 || state->stance == 11 || state->stance == 12;
          const std::uint8_t stim = speed > 5.8f   ? tandem::presence::kStimFootStepSprint
                                    : speed > 2.5f ? tandem::presence::kStimFootStepRegular
                                                   : std::uint8_t{0};
          if (stim != 0 && !crouched && footstepGate.ShouldSend(stim, tandem::net::NowUs())) {
            endpoint->SendNoise({.stim = stim});
            if (++footstepsSaid == 1 || footstepsSaid % 20 == 0)
              Log("footsteps: noise {} sent (stim {}, {:.1f} m/s)", footstepsSaid, stim, speed);
          }
        }
        if (regionNpcs)
          regionNpcs->Tick(*endpoint, tandem::net::NowUs(), state->position,
                           !puppeteer.RegionLeaving());
      }
      // face NPC on: a face of 40 joints at rest but its jaw (joint 5), which
      // opens and closes four times a second, and a lip (joint 6).
      if (options.host && sceneScript.stage.On() && !sceneScript.faces.empty()) {
        static const std::vector<tandem::rig::Bone> reference(40);
        static const std::uint32_t rigHash = [] {
          std::vector<std::uint64_t> names(40);
          for (std::size_t i = 0; i < names.size(); ++i)
            names[i] = 0x1000 + i;
          return tandem::rig::RigHash(names);
        }();
        const float beat = static_cast<float>(tandem::net::NowUs() % 1'000'000) / 1e6f * 25.f;
        const float open = 0.15f * (1.f + std::sin(beat));
        auto face = reference;
        face[5].rotation = {std::sin(open / 2.f), 0.f, 0.f, std::cos(open / 2.f)};
        face[6].translation.z = 0.003f * std::sin(beat);
        for (const auto npc : sceneScript.faces)
          endpoint->SendRigPose(npc, tandem::proto::kRigFace, rigHash, reference, face);
      }
      // camera on: the host's view of its cutscene, from behind and above
      // its V, on one side or the other. Out of a scene and a moment too: a
      // real host sends its view while a guest watches through its eyes
      // (Spectate.reds, view wanted), as with interiors the guest lacks.
      if (options.host && sceneScript.camera && state) {
        const float side = sceneScript.cameraSide ? -2.f : 2.f;
        const tandem::scene::CameraView view{
            {state->position.x + side, state->position.y - 3.f, state->position.z + 1.8f},
            state->orientation,
            sceneScript.cameraFov};
        endpoint->SendSceneCamera(
            tandem::scene::PackCamera(std::max<std::uint32_t>(1, sceneScript.stage.Play()), view,
                                      std::exchange(sceneScript.cameraCut, false)));
      }
      if (auto pose = puppeteer.NextPose(tandem::net::NowUs(), poseClip)) {
        endpoint->SendLocalPose(*pose);
        // --story: the scene's actors take the same pose (ActorPose), so a
        // game shows the stream on its proxies.
        if (options.host && sceneScript.stage.On())
          for (const auto &actor : sceneScript.sample.actors)
            if (actor.flags & tandem::proto::kActorPosed) {
              endpoint->SendActorPose(tandem::pose::ForActor(actor.npc, *pose));
              if (++actorPoses % 300 == 1)
                Log("scene: {} actor pose(s) sent", actorPoses);
            }
      }
      // metro: board, skip halfway, get off; the ride as the game sends it
      // (no seat: the train is never shared).
      if (options.mode == Mode::Metro && endpoint->IsJoined()) {
        const auto nowUs = tandem::net::NowUs();
        if (togetherSince && now >= *togetherSince + std::chrono::seconds(options.metroAtSeconds))
          puppeteer.Board(nowUs);
        bool secondHalf = false;
        const int step = puppeteer.MetroStep(nowUs, &secondHalf);
        if (step != metroStep || secondHalf != metroSecondHalf) {
          namespace proto = tandem::proto;
          proto::Transit ride{.kind = proto::TransitKind::Metro,
                              .at = static_cast<std::uint8_t>(secondHalf ? 6 : 5),
                              .next = static_cast<std::uint8_t>(secondHalf ? 7 : 6),
                              .vehicle = kTrain,
                              .seat = kTrainSeat};
          if (step == 2)
            ride.flags = proto::kTransitSkipping;
          if (step == 3)
            ride = {};
          endpoint->SetLocalTransit(ride);
          constexpr const char *kSteps[] = {"on the platform", "riding the NCART", "skipping",
                                            "off the NCART"};
          Log("metro: {} (station {}, next {})", kSteps[step], ride.at, ride.next);
          metroStep = step;
          metroSecondHalf = secondHalf;
        }
      }
      // --summon: our garage's car drives in on its own and parks by us,
      // we on foot (OwnCar; our VehicleState stream is it while it moves);
      // --summon-again: summoned again (the same car, moved); --summon-board:
      // we get in (our Seat: the others' copy of it is our car's).
      if (options.mode == Mode::Drive && options.summonSeconds >= 0 && !summon.boarded &&
          endpoint->IsJoined()) {
        namespace proto = tandem::proto;
        const auto nowUs = tandem::net::NowUs();
        const auto center = puppeteer.Center();
        if (center && !summon.standing) {
          summon.stop = {center->x + options.driveRadius, center->y + 4.f, center->z};
          puppeteer.Park({summon.stop.x - 2.f, summon.stop.y - 4.f, summon.stop.z});
          summon.standing = true;
        }
        const auto after = [&](std::optional<std::chrono::steady_clock::time_point> from,
                               int seconds) {
          return from && seconds >= 0 && now >= *from + std::chrono::seconds(seconds);
        };
        const auto said = [&](std::uint8_t flags, const Vec3 &at, const proto::Quat &facing) {
          endpoint->SendCombat(proto::OwnCar{.vehicle = TweakDBID(options.vehicle),
                                             .flags = flags,
                                             .position = at,
                                             .orientation = facing,
                                             .hint = options.vehicleHint});
        };
        constexpr float kSummonFrom = 50.f, kSummonSpeed = 6.f;
        const bool due = summon.round == 0 ? after(togetherSince, options.summonSeconds)
                                           : summon.round == 1 &&
                                                 after(summon.parkedAt, options.summonAgainSeconds);
        if (summon.standing && !summon.arrivingUs && due) {
          // From ahead the first time, from behind when summoned again.
          summon.direction = summon.round == 0 ? -1.f : 1.f;
          summon.from = {summon.stop.x, summon.stop.y - summon.direction * kSummonFrom,
                         summon.stop.z};
          summon.facing =
              tandem::sync::FromYaw(summon.direction < 0 ? std::numbers::pi_v<float> : 0.f);
          summon.arrivingUs = nowUs;
          ++summon.round;
          said(proto::kOwnCarMoving, summon.from, summon.facing);
          Log("summon: our car {} drives in from {:.0f} m{}", options.vehicle, kSummonFrom,
              summon.round > 1 ? " (summoned again)" : "");
        }
        if (summon.arrivingUs) {
          const float t = static_cast<float>(nowUs - *summon.arrivingUs) / 1e6f;
          const float along = std::min(t * kSummonSpeed, kSummonFrom);
          proto::VehicleState car{
              .position = {summon.from.x, summon.from.y + summon.direction * along, summon.from.z},
              .orientation = summon.facing,
              .velocity = {0.f, summon.direction * kSummonSpeed, 0.f}};
          if (along >= kSummonFrom) {
            car.velocity = {};
            endpoint->SendLocalVehicle(car);
            said(0, car.position, summon.facing);
            summon.arrivingUs.reset();
            summon.parkedAt = now;
            Log("summon: our car parked by us at ({:.1f}, {:.1f}, {:.1f})", car.position.x,
                car.position.y, car.position.z);
          } else if (nowUs - summon.sentUs >= 33'000) {
            summon.sentUs = nowUs;
            endpoint->SendLocalVehicle(car);
          }
        }
        if (!summon.arrivingUs && after(summon.parkedAt, options.summonBoardSeconds) &&
            (options.summonAgainSeconds < 0 || summon.round > 1)) {
          summon.boarded = true;
          puppeteer.Unpark();
          Log("summon: we get in our car");
        }
      }
      if (options.mode == Mode::Drive && !parked && (options.summonSeconds < 0 || summon.boarded)) {
        const auto nowUs = tandem::net::NowUs();
        const bool driving = options.seat == "seat_front_left";
        const auto car = puppeteer.Drive(nowUs).second;
        if (!seated && endpoint->IsJoined() && car) {
          endpoint->SetLocalSeat({.vehicle = TweakDBID(options.vehicle),
                                  .seat = CName(options.seat),
                                  .hint = options.vehicleHint});
          Log("{} {} ({:#x}) in {}", driving ? "driving" : "riding", options.vehicle,
              TweakDBID(options.vehicle), options.seat);
          seated = true;
          seatedAtUs = nowUs;
          if (options.radioStation)
            sendRadio("at the wheel");
          // --paint: the car's custom paint, as its owner's game says it.
          if (options.paint) {
            auto paint = *options.paint;
            paint.vehicle = TweakDBID(options.vehicle);
            endpoint->SendCombat(paint);
            Log("our car's paint sent: {}", PaintText(paint));
          }
          // Not at the wheel: a ride our game runs (an AI or a scene drives).
          if (!driving) {
            namespace proto = tandem::proto;
            endpoint->SetLocalTransit(
                {.kind = options.scene ? proto::TransitKind::Scene : proto::TransitKind::Ride,
                 .flags = options.scene && options.host ? proto::kTransitSeats : std::uint8_t{0},
                 .seat = CName(options.seat)});
            Log("a {} ride{}", options.scene ? "scene" : "plain",
                options.scene && options.host ? ": partners near it get a seat" : "");
          }
          if (!driving && options.host && !options.driverNpc.empty()) {
            endpoint->SendNpcSpawn({.npc = kDriverNpc,
                                    .record = TweakDBID(options.driverNpc),
                                    .position = car->position,
                                    .yaw = tandem::sync::YawOf(car->orientation)});
            endpoint->SendNpcSeat(
                {.npc = kDriverNpc, .owner = endpoint->You(), .seat = CName("seat_front_left")});
            Log("npc {}: {} at the wheel", kDriverNpc, options.driverNpc);
          }
        }
        if (car) {
          endpoint->SendLocalVehicle(*car);
          lastCar = car;
          // The NPC at the wheel goes where the car goes (10 Hz).
          if (!driving && options.host && !options.driverNpc.empty() && now >= nextDriverState) {
            nextDriverState = now + 100ms;
            endpoint->SendNpcStates({.npcs = {{.npc = kDriverNpc,
                                               .position = car->position,
                                               .yaw = tandem::sync::YawOf(car->orientation)}}});
          }
        }
        // --car-damage: dents, a flat tyre, the bumper off; fire; a wreck.
        if (seated && options.carDamageSeconds >= 0 && carDamageStep < 3 &&
            nowUs - seatedAtUs >
                static_cast<std::uint64_t>(options.carDamageSeconds + 4 * carDamageStep) *
                    1'000'000) {
          namespace proto = tandem::proto;
          auto &car = ourCarDamage();
          if (carDamageStep == 0) {
            const float dents[] = {0.6f, 0.4f, 0.f, 0.f, 0.f,  0.f,  0.f,  0.f,
                                   0.f,  0.f,  0.f, 0.f, 0.3f, 0.8f, 0.5f, 0.f};
            car.health = 0.55f;
            car.tyres |= 1;
            car.grid = vc::PackGrid(dents);
            car.parts = {CName("BumperFront")};
            Log("our car: hit: dents, the front left tyre flat, the front bumper off");
          } else if (carDamageStep == 1) {
            car.health = 0.08f;
            car.flags |= proto::kDamageBurning;
            Log("our car burns");
          } else {
            car.health = 0.f;
            car.flags = proto::kDamageDestroyed;
            car.explodedUs = endpoint->HostNowUs();
            Log("our car blew up");
          }
          ++carDamageStep;
          endpoint->SetVehicleDamage(car);
        }
        // --exit-at: out of the car the hard way.
        if (seated && options.exitAtSeconds >= 0 && lastCar &&
            nowUs - seatedAtUs > static_cast<std::uint64_t>(options.exitAtSeconds) * 1'000'000) {
          namespace proto = tandem::proto;
          const bool driver = options.seat == "seat_front_left";
          const auto &v = lastCar->velocity;
          const float speed = std::hypot(v.x, v.y, v.z);
          const bool rolls = vc::Rolls(options.exitKind, speed, driver);
          endpoint->SendVehicleExit(
              {.vehicle = proto::PlayerCar(endpoint->You()),
               .kind = options.exitKind,
               .flags = static_cast<std::uint8_t>((rolls ? proto::kExitRolling : 0) |
                                                  (driver ? proto::kExitDriver : 0)),
               .velocity = v});
          endpoint->SetLocalSeat({});
          endpoint->SetLocalTransit({});
          parked = true;
          puppeteer.Park(lastCar->position);
          if (rolls) {
            rolling = lastCar;
            rollingSinceUs = rolledAtUs = nowUs;
          }
          Log("out of the car the hard way (kind {}) at {:.1f} m/s{}",
              static_cast<int>(options.exitKind), speed, rolls ? ": it rolls on" : "");
        }
        // --paint-off-at: the custom paint switched off (stock paint again).
        if (seated && options.paint && options.paintOffSeconds > 0 && !paintOff &&
            nowUs - seatedAtUs > static_cast<std::uint64_t>(options.paintOffSeconds) * 1'000'000) {
          paintOff = true;
          endpoint->SendCombat(tandem::proto::CarPaint{});
          Log("our car's paint sent: stock");
        }
        if (seated && !parked && options.driveSeconds > 0 &&
            nowUs - seatedAtUs > static_cast<std::uint64_t>(options.driveSeconds) * 1'000'000) {
          endpoint->SetLocalSeat({}); // on foot, beside where the car stopped
          endpoint->SetLocalTransit({});
          parked = true;
          if (lastCar)
            puppeteer.Park(lastCar->position);
          Log("out of the car");
        }
      }
    }
    // The car we left on the move rolls on, slowing, until it stops: our
    // game simulates it, so its state keeps coming from us.
    if (rolling) {
      const auto nowUs = tandem::net::NowUs();
      const float dt = static_cast<float>(nowUs - rolledAtUs) / 1e6f;
      if (dt >= 1.f / 30.f) {
        rolledAtUs = nowUs;
        auto &car = *rolling;
        const float keep = std::max(0.f, 1.f - 0.6f * dt);
        car.velocity = {car.velocity.x * keep, car.velocity.y * keep, car.velocity.z * keep};
        car.position = {car.position.x + car.velocity.x * dt, car.position.y + car.velocity.y * dt,
                        car.position.z + car.velocity.z * dt};
        const float speed = std::hypot(car.velocity.x, car.velocity.y, car.velocity.z);
        if (speed < vc::kRestSpeed || nowUs - rollingSinceUs > vc::kRollUs) {
          car.velocity = {};
          endpoint->SendLocalVehicle(car);
          Log("our car rolled to a stop at ({:.1f}, {:.1f}, {:.1f})", car.position.x,
              car.position.y, car.position.z);
          rolling.reset();
        } else {
          endpoint->SendLocalVehicle(car);
        }
      }
    }
    // Pushes from the others' cars, once our game had the time to feel
    // the contact itself.
    for (const auto &push : bumps.Due(endpoint->HostNowUs()))
      Log("our car: pushed by player {}'s car ({:.1f}, {:.1f}, {:.1f}) m/s", push.from,
          push.impulse.x, push.impulse.y, push.impulse.z);
    {
      namespace proto = tandem::proto;
      // The first other player who drives (or, with nobody driving, any).
      const auto driverOther = [&](bool anyone) -> proto::PlayerId {
        for (const auto &[player, drives] : driving)
          if (drives && player != endpoint->You())
            return player;
        if (anyone)
          for (const auto &[id, info] : endpoint->Players())
            if (id != endpoint->You())
              return id;
        return proto::kNoPlayer;
      };
      // --bump: our car pushes theirs.
      if (options.bumpSeconds && options.mode == Mode::Drive && seated && !parked && lastCar &&
          std::chrono::steady_clock::now() >= nextBump) {
        nextBump = std::chrono::steady_clock::now() + std::chrono::seconds(options.bumpSeconds);
        if (const auto other = driverOther(false)) {
          endpoint->SendVehicleBump(
              {.other = other, .impulse = {0.f, 2.5f, 0.f}, .position = lastCar->position});
          Log("our car pushed player {}'s car", other);
        }
      }
      // --shoot-cars: our shots land on their car (every other one a tyre).
      if (options.shootCarsSeconds && endpoint->IsJoined() &&
          std::chrono::steady_clock::now() >= nextCarShot) {
        nextCarShot =
            std::chrono::steady_clock::now() + std::chrono::seconds(options.shootCarsSeconds);
        if (const auto other = driverOther(false)) {
          const bool tyre = carShots++ % 2 == 1;
          endpoint->SendVehicleHit({.vehicle = proto::PlayerCar(other),
                                    .damage = 150.f,
                                    .flags = tyre ? proto::kVehicleHitTyre : std::uint8_t{0},
                                    .tyre = static_cast<std::int8_t>(tyre ? 1 : -1),
                                    .direction = {0.f, 1.f, 0.f}});
          Log("shot player {}'s car for 150{}", other, tyre ? ", tyre 1" : "");
        }
      }
      // --ram-npcs: our car runs into one of the host's NPCs at 12 m/s.
      if (options.ramNpcsSeconds && !options.host && endpoint->IsJoined() &&
          !endpoint->SharedNpcs().empty() && std::chrono::steady_clock::now() >= nextNpcRam) {
        nextNpcRam =
            std::chrono::steady_clock::now() + std::chrono::seconds(options.ramNpcsSeconds);
        const auto &[npc, spawn] = *endpoint->SharedNpcs().begin();
        const proto::Vec3 velocity{12.f, 0.f, 0.f};
        endpoint->SendCombat(proto::NpcStrike{.npc = npc,
                                              .damage = 60.f,
                                              .attackType = 1, // the collision's (Direct)
                                              .position = spawn.position,
                                              .direction = {1.f, 0.f, 0.f}});
        endpoint->SendNpcRam({.npc = npc,
                              .velocity = velocity,
                              .position = spawn.position,
                              .flags = vc::RamFlags(12.f, false)});
        Log("our car ran into npc {} at 12 m/s", npc);
      }
      // --gunner: ask for their car's gun, aim and fire, leave.
      if (options.gunnerSeconds >= 0 && endpoint->IsJoined() && togetherSince &&
          now >= *togetherSince + std::chrono::seconds(options.gunnerSeconds)) {
        if (gunnerStep == 0) {
          if (const auto other = driverOther(true)) {
            gunnerCar = proto::PlayerCar(other);
            endpoint->AskGunner(gunnerCar, CName(options.gunnerSeat));
            gunnerStep = 1;
            gunnerSince = now;
            Log("gunner: asking for the gun of player {}'s vehicle ({})", other,
                options.gunnerSeat);
          }
        } else if (gunnerStep == 1) {
          const auto &seats = endpoint->Gunners();
          if (const auto it = seats.find(gunnerCar); it != seats.end()) {
            if (it->second.gunner == endpoint->You()) {
              gunnerStep = 2;
              gunnerSince = now;
              endpoint->SetLocalVehicleCombat({.mode = proto::SeatCombat::Gunner,
                                               .vehicle = gunnerCar,
                                               .weapon = TweakDBID("Items.Panzer_Cannon")});
              Log("gunner: the seat is ours");
            } else {
              gunnerStep = 4;
              Log("gunner: the seat is player {}'s", it->second.gunner);
            }
          } else if (now - *gunnerSince > 5s) {
            gunnerStep = 4;
            Log("gunner: no answer");
          }
        } else if (gunnerStep == 2) {
          const double t = std::chrono::duration<double>(now - *gunnerSince).count();
          if (now >= nextAim) {
            nextAim = now + 50ms;
            endpoint->SendGunnerAim({.vehicle = gunnerCar,
                                     .yaw = static_cast<float>(std::sin(t * 0.8)),
                                     .pitch = 0.1f,
                                     .flags = proto::kAimFiring});
          }
          if (now >= nextMountedShot) {
            nextMountedShot = now + 2s;
            // 30 m ahead of the vehicle's driver (its game fires its real
            // guns there: vehicle-combat.md, "A guest on someone else's gun").
            const Vec3 at = otherV ? otherV->position : Vec3{};
            endpoint->SendMountedFire({.vehicle = gunnerCar,
                                       .weapon = TweakDBID("Items.Panzer_Cannon"),
                                       .target = {at.x, at.y + 30.f, at.z + 1.f}});
            Log("gunner: fire");
          }
          if (t > 20.0) {
            endpoint->AskGunner(gunnerCar, CName(options.gunnerSeat), true);
            endpoint->SetLocalVehicleCombat({});
            gunnerStep = 3;
            Log("gunner: left the seat");
          }
        }
      }
      // --chase: the first street car goes after the first guest.
      if (options.host && options.chaseSeconds >= 0 && !street.empty() && firstRemote &&
          togetherSince) {
        const double in = std::chrono::duration<double>(now - *togetherSince).count();
        const int step = in < options.chaseSeconds ? 0 : in < options.chaseSeconds + 20 ? 1 : 2;
        if (step != chaseStep && !street.front().gone) {
          chaseStep = step;
          endpoint->SendChase({.vehicle = street.front().id,
                               .target = step == 1 ? firstRemote->player : proto::kNoPlayer,
                               .flags = proto::kChaseRamming});
          Log("street: vehicle {:#x} {}", street.front().id,
              step == 1 ? std::format("chases player {}", firstRemote->player)
                        : "gives up the chase");
        }
      }
    }
    if (options.host && (options.timeOfDayMinutes || !options.weather.empty()) &&
        now >= nextWorld) {
      nextWorld = now + 5s;
      // Day 1 at the given time, running about 8 game seconds a second.
      const auto elapsed = static_cast<int>((tandem::net::NowUs() - startUs) / 1'000'000);
      tandem::proto::World world;
      world.gameTimeSeconds = static_cast<std::int32_t>(
          86400 + options.timeOfDayMinutes.value_or(12 * 60) * 60 + elapsed * 8 + skippedSeconds);
      world.weather = options.weather.empty() ? 0 : CName(options.weather);
      endpoint->SetWorld(world);
    }
    if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
      togetherSince = now;
    const auto together = [&](int seconds) {
      return seconds >= 0 && togetherSince && now >= *togetherSince + std::chrono::seconds(seconds);
    };
    // --travel-at: arrive somewhere else, as from a fast travel, and offer
    // the others to join (as a game does once its loading screen is over).
    if (!travelled && together(options.travelAtSeconds)) {
      travelled = true;
      puppeteer.StandAt(options.travelTo);
      const auto &to = options.travelTo;
      endpoint->SendRegroup(
          {.offer = ++offers,
           .reason = tandem::proto::RegroupReason::FastTravel,
           .point = options.travelPoint.empty() ? 0 : TweakDBID(options.travelPoint),
           .position = to});
      Log("fast travelled to ({:.1f}, {:.1f}, {:.1f}){}: offered everyone to join (offer {})", to.x,
          to.y, to.z, options.travelPoint.empty() ? "" : " at " + options.travelPoint, offers);
    }
    // Comms: link requests, pings, tags and reveals, our waypoint.
    std::erase_if(linkDeclines, [&](const auto &d) {
      if (tandem::net::NowUs() < d.dueUs)
        return false;
      endpoint->SendLinkDecline({.to = d.player, .reason = d.reason});
      Log("declined player {}'s link request (reason {})", d.player, d.reason);
      return true;
    });
    std::erase_if(linkAnswers, [&](const auto &answer) {
      if (tandem::net::NowUs() < answer.second)
        return false;
      ownLink = tandem::comms::WithConsent(ownLink, answer.first, true);
      endpoint->SetLocalLink(ownLink);
      Log("accepted player {}'s link request", answer.first);
      return true;
    });
    if (!linkAsked && together(options.linkAtSeconds)) {
      linkAsked = true;
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You())
          ownLink = tandem::comms::WithConsent(ownLink, id, true);
      endpoint->SetLocalLink(ownLink);
      Log("asked everyone to link ({} listed)", ownLink.players.size());
    }
    if (!unlinked && together(options.unlinkAtSeconds)) {
      unlinked = true;
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You())
          ownLink = tandem::comms::WithConsent(ownLink, id, false);
      endpoint->SetLocalLink(ownLink);
      Log("broke every link (mode {}, {} listed)", static_cast<int>(ownLink.mode),
          ownLink.players.size());
    }
    const auto nearestNpc = [&]() -> std::optional<tandem::proto::NpcSpawn> {
      std::optional<tandem::proto::NpcSpawn> best;
      const Vec3 from = otherV ? otherV->position : lastSent.value_or(Vec3{});
      for (const auto &[id, spawn] : endpoint->SharedNpcs())
        if (!best || distance(spawn.position, from) < distance(best->position, from))
          best = spawn;
      return best;
    };
    if (options.pingSeconds > 0 && endpoint->IsJoined() && togetherSince && now >= nextPing) {
      nextPing = now + std::chrono::seconds(options.pingSeconds);
      // A spot in front of the other V, an enemy (or the spot again), the
      // same at once (danger), then taken back: the four things a ping does.
      const auto nowUs = tandem::net::NowUs();
      std::optional<tandem::proto::Mark> mark;
      const auto npc = nearestNpc();
      Vec3 spot = lastSent.value_or(Vec3{});
      if (otherV) {
        const float yaw = tandem::sync::YawOf(otherV->orientation);
        spot = {otherV->position.x - std::sin(yaw) * 4.f, otherV->position.y + std::cos(yaw) * 4.f,
                otherV->position.z};
      }
      switch (pingStep++ % 4) {
      case 0:
        mark = pinger.Ping(tandem::proto::MarkKind::Spot, {}, spot, nowUs);
        break;
      case 1:
        lastPing = npc ? tandem::proto::TargetRef{tandem::proto::TargetKind::Npc, npc->npc}
                       : tandem::proto::TargetRef{};
        lastPingAt = npc ? npc->position : Vec3{spot.x + 5.f, spot.y, spot.z};
        mark = pinger.Ping(npc ? tandem::proto::MarkKind::Enemy : tandem::proto::MarkKind::Spot,
                           lastPing, lastPingAt, nowUs);
        // Twice in a row: careful (the danger window is 1.5 s).
        if (mark) {
          endpoint->SendMark(*mark);
          Log("ping: mark {} kind {} at ({:.1f}, {:.1f}, {:.1f})", mark->id,
              static_cast<int>(mark->kind), mark->position.x, mark->position.y, mark->position.z);
        }
        mark = pinger.Ping(npc ? tandem::proto::MarkKind::Enemy : tandem::proto::MarkKind::Spot,
                           lastPing, lastPingAt, nowUs + 1);
        break;
      case 2:
        mark = pinger.Ping(tandem::proto::MarkKind::Spot, lastPing, lastPingAt, nowUs);
        break;
      default:
        break; // a rest
      }
      if (mark) {
        endpoint->SendMark(*mark);
        Log("ping: mark {} kind {} at ({:.1f}, {:.1f}, {:.1f})", mark->id,
            static_cast<int>(mark->kind), mark->position.x, mark->position.y, mark->position.z);
      }
    }
    if (options.tagSeconds > 0 && endpoint->IsJoined() && togetherSince && now >= nextTag) {
      nextTag = now + std::chrono::seconds(options.tagSeconds);
      if (const auto npc = nearestNpc()) {
        tagOn = !tagOn;
        endpoint->SendTag({.reason = tandem::proto::TagReason::Scan,
                           .on = tagOn,
                           .targets = {{tandem::proto::TargetKind::Npc, npc->npc}}});
        Log("{} npc {} (scan)", tagOn ? "tagged" : "untagged", npc->npc);
      }
    }
    if (options.revealSeconds > 0 && endpoint->IsJoined() && togetherSince && now >= nextReveal) {
      nextReveal = now + std::chrono::seconds(options.revealSeconds);
      tandem::proto::Tag reveal{
          .reason = tandem::proto::TagReason::Reveal, .on = true, .seconds = 10};
      for (const auto &[id, spawn] : endpoint->SharedNpcs())
        if (reveal.targets.size() < 4)
          reveal.targets.push_back({tandem::proto::TargetKind::Npc, id});
      if (!reveal.targets.empty()) {
        Log("revealed {} npc(s) for 10 s (ping quickhack)", reveal.targets.size());
        endpoint->SendTag(std::move(reveal));
      }
    }
    if (options.revealDevice != 0 && together(options.revealDeviceAtSeconds)) {
      endpoint->SendTag({.reason = tandem::proto::TagReason::Reveal,
                         .on = true,
                         .seconds = 10,
                         .targets = {{tandem::proto::TargetKind::Entity, options.revealDevice}}});
      Log("pulsed the network from device {:x} for 10 s (ping quickhack)", options.revealDevice);
      options.revealDevice = 0;
    }
    if (!options.markTarget.empty() && !targetMarked && together(5)) {
      targetMarked = true;
      std::optional<tandem::proto::NpcSpawn> npc;
      if (options.markTarget == "nearest")
        npc = nearestNpc();
      else if (const auto it =
                   endpoint->SharedNpcs().find(Number<std::uint32_t>(options.markTarget));
               it != endpoint->SharedNpcs().end())
        npc = it->second;
      const tandem::proto::NpcId id =
          npc ? npc->npc
              : (options.markTarget == "nearest" ? 0 : Number<std::uint32_t>(options.markTarget));
      if (id == 0) {
        Log("team target: no shared NPC to mark");
      } else if (const auto mark =
                     pinger.Target({tandem::proto::TargetKind::Npc, id},
                                   npc ? npc->position : Vec3{}, tandem::net::NowUs())) {
        endpoint->SendMark(*mark);
        Log("team target: mark {} on npc {}", mark->id, id);
      }
    }
    if (!options.hackNpc.empty() && options.host && togetherSince && now >= nextHackNpc) {
      namespace proto = tandem::proto;
      const auto t = now - *togetherSince;
      const auto step = static_cast<int>((t % 20s) / 1s);
      if (hackStep == 0 && step < 3) {
        hackStep = 1;
        hackedNpc = 0;
        if (options.hackNpc == "nearest") {
          if (const auto npc = nearestNpc())
            hackedNpc = npc->npc;
        } else {
          hackedNpc = Number<std::uint32_t>(options.hackNpc);
        }
        if (hackedNpc != 0) {
          endpoint->SendNpcHackState({.npc = hackedNpc,
                                      .player = endpoint->You(),
                                      .action = TweakDBID(options.hackAction),
                                      .step = proto::HackStep::Started,
                                      .uploadMs = 3000});
          Log("hack: uploading {} on npc {} (3 s)", options.hackAction, hackedNpc);
        }
      } else if (hackStep == 1 && step >= 3) {
        hackStep = 2;
        if (hackedNpc != 0) {
          endpoint->SendNpcHackState({.npc = hackedNpc,
                                      .player = endpoint->You(),
                                      .action = TweakDBID(options.hackAction),
                                      .step = proto::HackStep::Done});
          endpoint->SendNpcStatus({.npc = hackedNpc,
                                   .player = endpoint->You(),
                                   .effect = TweakDBID(options.hackEffect),
                                   .applied = true});
          Log("hack: {} on npc {} (10 s)", options.hackEffect, hackedNpc);
        }
      } else if (hackStep == 2 && step >= 13) {
        hackStep = 0;
        if (hackedNpc != 0) {
          endpoint->SendNpcStatus(
              {.npc = hackedNpc, .effect = TweakDBID(options.hackEffect), .applied = false});
          Log("hack: {} off npc {}", options.hackEffect, hackedNpc);
        }
        nextHackNpc = now + 2s; // the rest of the round
      }
    }
    if (options.waypoint && !waypointSet && endpoint->IsJoined() && togetherSince) {
      waypointSet = true;
      waypointSetAt = now;
      endpoint->SetLocalWaypoint({.kind = options.waypointKind, .position = *options.waypoint});
      Log("waypoint (kind {}) set at ({:.1f}, {:.1f}, {:.1f})",
          static_cast<int>(options.waypointKind), options.waypoint->x, options.waypoint->y,
          options.waypoint->z);
    }
    if (waypointSet && !waypointCleared && options.waypointClearSeconds >= 0 &&
        now >= waypointSetAt + std::chrono::seconds(options.waypointClearSeconds)) {
      waypointCleared = true;
      endpoint->SetLocalWaypoint({});
      Log("waypoint taken away");
    }
    // --offer-regroup: ask the others to come.
    if (!offeredRegroup && together(options.offerRegroupSeconds)) {
      offeredRegroup = true;
      const auto at = lastSent.value_or(Vec3{});
      endpoint->SendRegroup(
          {.offer = ++offers, .reason = tandem::proto::RegroupReason::Asked, .position = at});
      Log("asked the others to come to ({:.1f}, {:.1f}, {:.1f}) (offer {})", at.x, at.y, at.z,
          offers);
    }
    // --ask-skip: ask the others to let the hours pass, on our clock (the
    // host's: the one we send; a guest's: the host's as last heard).
    if (!askedSkip && together(options.askSkipSeconds)) {
      askedSkip = true;
      const auto elapsed = static_cast<std::int64_t>((tandem::net::NowUs() - startUs) / 1'000'000);
      const auto clock =
          options.host
              ? static_cast<std::int32_t>(86400 + options.timeOfDayMinutes.value_or(12 * 60) * 60 +
                                          elapsed * 8 + skippedSeconds)
              : hostClock.value_or(86400 + 12 * 3600);
      const auto request =
          endpoint->AskTimeSkip(options.skipReason, clock, clock + options.skipHours * 3600);
      Log("asked to skip {} h (request {}, reason {})", options.skipHours, request,
          static_cast<int>(options.skipReason));
      if (options.host && options.skipReason == tandem::proto::SkipReason::Quest) {
        skippedSeconds += options.skipHours * 3600;
        nextWorld = now;
      }
    }
    // --activity: busy for a while, standing where we are.
    if (!options.activity.empty() && !busySet && together(options.activityAtSeconds)) {
      busySet = true;
      busySince = now;
      const auto kind = *ActivityNamed(options.activity);
      tandem::proto::Activity busy{.kind = kind};
      if (kind == tandem::proto::ActivityKind::Contest) {
        // Its sort, and a duel's opponent; nothing holds a contestant.
        busy.entry = static_cast<std::uint32_t>(options.contest);
        if (options.contest == tandem::proto::ContestSort::Duel) {
          // (A guest fights one of the host's NPCs, the host of a replay
          // one of its own.)
          if (!liveNpcs.empty())
            busy.device = *liveNpcs.begin();
          if (options.host)
            for (const auto npc : replayedNpcs)
              if (!deadNpcs.contains(npc)) {
                busy.device = npc;
                break;
              }
        }
        Log("contest: sort {}, opponent npc {}", busy.entry, busy.device);
      } else if (lastSent && kind != tandem::proto::ActivityKind::Photo &&
                 kind != tandem::proto::ActivityKind::Menu) {
        busy.flags |= tandem::proto::kActivityAnchored;
        busy.anchor = *lastSent;
      }
      if (kind == tandem::proto::ActivityKind::Sleep)
        busy.flags |= tandem::proto::kActivityLying;
      if (kind == tandem::proto::ActivityKind::Photo || kind == tandem::proto::ActivityKind::Menu)
        busy.flags |= tandem::proto::kActivityFrozen;
      endpoint->SetLocalActivity(busy);
      Log("busy: {} (flags {:#x})", ActivityName(kind), busy.flags);
    }
    if (busySet && !busyDone && options.activityForSeconds > 0 && busySince &&
        now >= *busySince + std::chrono::seconds(options.activityForSeconds)) {
      busyDone = true;
      endpoint->SetLocalActivity({});
      Log("free again");
    }
    // Someone came to where we stand: back to the mode's motion.
    if (const auto station = puppeteer.Station()) {
      if (const auto them = puppeteer.HeardAt(); them && distance(*them, *station) < 10.f) {
        Log("another V is with us again ({:.1f} m)", distance(*them, *station));
        puppeteer.StandAt(std::nullopt);
      }
    }
    // --pause-at: open the pause menu, then close it.
    if (options.pauseAtSeconds >= 0 && pauseStep != PauseStep::Done) {
      const auto pauseAt = togetherSince
                               ? *togetherSince + std::chrono::seconds(options.pauseAtSeconds)
                               : std::chrono::steady_clock::time_point::max();
      if (pauseStep == PauseStep::Waiting && now >= pauseAt) {
        endpoint->SetLocalPaused(true);
        pauseStep = PauseStep::Paused;
        Log("pause menu open: pausing everyone for {} s", options.pauseForSeconds);
      } else if (pauseStep == PauseStep::Paused &&
                 now >= pauseAt + std::chrono::seconds(options.pauseForSeconds)) {
        endpoint->SetLocalPaused(false);
        pauseStep = PauseStep::Done;
        Log("pause menu closed");
      }
    }
    // --sandevistan-at (or --scan-at): on, then off.
    if (options.sandevistanAtSeconds >= 0 && sandevistanStep != PauseStep::Done) {
      const char *ability =
          options.timeAbility == tandem::proto::kTimeScanning ? "scanning" : "sandevistan";
      if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
        togetherSince = now;
      const auto at = togetherSince
                          ? *togetherSince + std::chrono::seconds(options.sandevistanAtSeconds)
                          : std::chrono::steady_clock::time_point::max();
      if (sandevistanStep == PauseStep::Waiting && now >= at) {
        endpoint->SetLocalTimeEffect(
            {.abilities = options.timeAbility, .scale = options.sandevistanScale});
        sandevistanStep = PauseStep::Paused;
        Log("{} on: world speed {:.2f} around us for {} s", ability, options.sandevistanScale,
            options.sandevistanForSeconds);
      } else if (sandevistanStep == PauseStep::Paused &&
                 now >= at + std::chrono::seconds(options.sandevistanForSeconds)) {
        endpoint->SetLocalTimeEffect({});
        sandevistanStep = PauseStep::Done;
        Log("{} off", ability);
      }
    }
    // --loot-quest: "our V" loots a quest item, once.
    if (!options.lootQuest.empty() && !lootedQuest && endpoint->IsJoined() &&
        endpoint->Players().size() > 1) {
      if (!lootQuestAt)
        lootQuestAt = now + 5s;
      if (now >= *lootQuestAt) {
        lootedQuest = true;
        endpoint->SendSharedItem({.kind = tandem::proto::kSharedLooted,
                                  .item = {.record = TweakDBID(options.lootQuest), .quantity = 1}});
        Log("looted quest item {}: shared", options.lootQuest);
      }
    }
    // --device: each one at its time into the session. --device-answer:
    // what another player changed, changed back when its time comes.
    if (endpoint->IsJoined() && !devicesFrom)
      devicesFrom = now;
    for (auto &order : options.devices) {
      if (order.done || !devicesFrom || now < *devicesFrom + std::chrono::seconds(order.atSeconds))
        continue;
      order.done = true;
      const tandem::devices::Key key{order.entity, order.cls};
      // From what the session has of it (unknown: on, closed).
      tandem::devices::What what{.status = tandem::proto::kDeviceOn};
      if (const auto *record = endpoint->Devices().Find(key))
        what = tandem::devices::WhatOf(*record);
      DeviceWords(order.words, what);
      operateDevice(key, what, "--device");
    }
    // A device's fire. Host: the bursts owed, five shots a second, then
    // the turret follows nobody. Guest: --turret-seen, then lost 6 s later.
    for (auto &burst : turretBursts) {
      if (now < burst.next)
        continue;
      burst.next = now + 200ms;
      const bool last = --burst.shots < 0;
      if (last) {
        endpoint->SendDeviceFire(
            {.entity = burst.entity, .kind = tandem::proto::DeviceFireKind::Idle});
        Log("turret {:x}: follows nobody (its burst at player {} is over)", burst.entity,
            burst.player);
      } else {
        endpoint->SendDeviceFire({.entity = burst.entity,
                                  .kind = tandem::proto::DeviceFireKind::Shot,
                                  .target = {tandem::proto::TargetKind::Player, burst.player},
                                  .at = burst.at});
      }
    }
    std::erase_if(turretBursts, [](const TurretBurst &burst) { return burst.shots < 0; });
    if (options.turretSeen != 0 && !options.host && devicesFrom) {
      const auto seenAt = *devicesFrom + std::chrono::seconds(options.turretSeenAtSeconds);
      if (turretSeenStep == 0 && now >= seenAt) {
        turretSeenStep = 1;
        endpoint->SendDeviceFire(
            {.entity = options.turretSeen, .kind = tandem::proto::DeviceFireKind::Seen});
        Log("turret {:x}: our copy has our V (--turret-seen)", options.turretSeen);
      } else if (turretSeenStep == 1 && now >= seenAt + 6s) {
        turretSeenStep = 2;
        endpoint->SendDeviceFire(
            {.entity = options.turretSeen, .kind = tandem::proto::DeviceFireKind::Lost});
        Log("turret {:x}: our copy lost our V", options.turretSeen);
      }
    }
    // --turret-hands: five words a second while held, then let go.
    if (options.turretHands != 0 && !turretHandsDone && devicesFrom) {
      const auto from = *devicesFrom + std::chrono::seconds(options.turretHandsAtSeconds);
      if (now >= from + std::chrono::seconds(options.turretHandsForSeconds)) {
        turretHandsDone = true;
        endpoint->SendTurretHands(
            {.entity = options.turretHands, .flags = tandem::proto::kHandsLetGo});
        Log("turret {:x}: our V lets go of it (--turret-hands)", options.turretHands);
      } else if (now >= from && now - turretHandsSaid >= 200ms) {
        if (turretHandsSaid == std::chrono::steady_clock::time_point{})
          Log("turret {:x}: in our V's hands, pointing at ({:.1f} {:.1f} {:.1f}) "
              "(--turret-hands)",
              options.turretHands, options.turretHandsAt.x, options.turretHandsAt.y,
              options.turretHandsAt.z);
        turretHandsSaid = now;
        const bool firing = now >= from + 2s && now < from + 4s;
        endpoint->SendTurretHands({.entity = options.turretHands,
                                   .at = options.turretHandsAt,
                                   .flags = firing ? tandem::proto::kHandsFiring : std::uint8_t{}});
      }
    }
    // --fracture: each one at its time into the session.
    for (auto &order : options.fractures) {
      if (order.done || !devicesFrom || now < *devicesFrom + std::chrono::seconds(order.atSeconds))
        continue;
      order.done = true;
      endpoint->SendFracture(order.fracture);
      Log("something broke here at ({:.1f} {:.1f} {:.1f}), entity {:x} (--fracture)",
          order.fracture.at.x, order.fracture.at.y, order.fracture.at.z, order.fracture.entity);
    }
    // --breach: its outcome, once, at its time into the session.
    if (options.breach && !options.breach->done && devicesFrom &&
        now >= *devicesFrom + std::chrono::seconds(options.breach->atSeconds)) {
      options.breach->done = true;
      const auto &order = *options.breach;
      tandem::proto::Breach breach{.entity = order.entity,
                                   .cls = order.cls,
                                   .npc = order.npc,
                                   .programs = options.breachPrograms};
      if (order.npc != 0)
        breach.flags |= tandem::proto::kBreachRemote;
      if (options.breachFailed) {
        breach.flags |= tandem::proto::kBreachFailed;
        breach.programs.clear();
      }
      endpoint->SendBreach(breach);
      if (!options.breachFailed)
        breachHolds.Hold(1, {{order.entity, order.cls}, order.npc}, tandem::net::NowUs());
      Log("breached network {:x} npc {} with {} daemon(s){} (--breach)", order.entity, order.npc,
          breach.programs.size(), options.breachFailed ? ", lost" : "");
    }
    for (const auto &decision : breachHolds.Expire(tandem::net::NowUs()))
      breachSettled(decision);
    // --interact: each use at its time into the session (a guest asks).
    for (auto &order : options.interacts) {
      if (order.done || !devicesFrom || now < *devicesFrom + std::chrono::seconds(order.atSeconds))
        continue;
      order.done = true;
      if (options.host) {
        Log("--interact: a host does its own objects itself (nothing asked)");
        continue;
      }
      endpoint->SendStory(tandem::proto::QuestInteract{.target = order.entity,
                                                       .choice = order.choice,
                                                       .detail = order.detail,
                                                       .seq = ++interactSeq,
                                                       .position = lastSent.value_or(Vec3{}),
                                                       .timeUs = endpoint->HostNowUs()});
      Log("interact: our V uses {:x} ({:#x}), question {}", order.entity, order.choice,
          interactSeq);
    }
    for (auto it = deviceAnswers.begin(); it != deviceAnswers.end();) {
      if (now < it->second.first) {
        ++it;
        continue;
      }
      operateDevice(it->first, it->second.second, "--device-answer");
      deviceAnsweredAt[it->first] = now;
      it = deviceAnswers.erase(it);
    }
    // --drop-item: one world item 1.5 m in front of the other V, once.
    if (!options.dropItem.empty() && !dropped && endpoint->IsJoined() && otherV) {
      if (!dropAt)
        dropAt = now + 5s;
      if (now >= *dropAt) {
        dropped = true;
        const float yaw = tandem::sync::YawOf(otherV->orientation);
        const Vec3 at{otherV->position.x - std::sin(yaw) * 1.5f,
                      otherV->position.y + std::cos(yaw) * 1.5f, otherV->position.z};
        endpoint->DropWorldItem(
            {.token = 1,
             .position = at,
             .stacks = {{.record = TweakDBID(options.dropItem), .quantity = 1}}});
        Log("dropped world item {} x1 at ({:.1f}, {:.1f}, {:.1f})", options.dropItem, at.x, at.y,
            at.z);
      }
    }
    // --take-items: every stack nobody took yet, once its time comes.
    for (auto it = toTake.begin(); it != toTake.end();) {
      if (now < it->second.first) {
        ++it;
        continue;
      }
      const auto &item = it->second.second;
      for (std::size_t i = 0; i < item.stacks.size(); ++i)
        if (item.stacks[i].quantity > 0) {
          endpoint->TakeWorldItem(item.id, static_cast<std::uint8_t>(i));
          Log("claimed world item {} stack {}", item.id, i);
        }
      it = toTake.erase(it);
    }
    // --remove-corpses: the bodies of NPCs a guest killed go.
    for (auto it = corpses.begin(); it != corpses.end();) {
      if (now < it->second) {
        ++it;
        continue;
      }
      endpoint->SendNpcGone({.npc = it->first, .reason = 0});
      Log("npc {}'s body taken away", it->first);
      it = corpses.erase(it);
    }
    // --hit-npcs: one NPC at a time until it dies.
    if (options.hitNpcsSeconds && !options.host && endpoint->IsJoined() && !liveNpcs.empty() &&
        now >= nextHit) {
      nextHit = now + std::chrono::seconds(options.hitNpcsSeconds);
      // An NPC whose health no strike moves (a quest NPC, invulnerable) is
      // given up after a few; the newest NPCs first (a scenario's spawns).
      if (hitTarget && unanswered[*hitTarget] >= 4 && unhurt.insert(*hitTarget).second)
        Log("npc {}: no health change after 4 hits, trying another", *hitTarget);
      if (!hitTarget || !liveNpcs.contains(*hitTarget) || unhurt.contains(*hitTarget)) {
        hitTarget.reset();
        for (auto it = liveNpcs.rbegin(); it != liveNpcs.rend() && !hitTarget; ++it)
          if (!unhurt.contains(*it))
            hitTarget = *it;
        if (!hitTarget) {
          unhurt.clear(); // all tried: start over
          hitTarget = *liveNpcs.rbegin();
        }
        unanswered[*hitTarget] = 0;
      }
      ++unanswered[*hitTarget];
      const auto strike = StrikeOf(options.strike, *hitTarget);
      endpoint->SendCombat(strike);
      Log("hit npc {} ({}): {:.0f} health, {:.0f} poise, flags {:#x}", *hitTarget, options.strike,
          strike.damage, strike.poise, strike.flags);
      // The first hit on each NPC also sets it on fire (a status effect our
      // hit applied, for the host to put on the real NPC).
      if (burnt.insert(*hitTarget).second) {
        endpoint->SendNpcEffect(
            {.npc = *hitTarget, .effects = {TweakDBID("BaseStatusEffect.Burning")}});
        Log("set npc {} on fire", *hitTarget);
      }
    }
    // --grab-npcs: walk up to one NPC, grab it two seconds later (the other
    // games must see us beside it first), take it down two seconds after.
    if (options.grabNpcsSeconds && !options.host && endpoint->IsJoined()) {
      // The target: an NPC the host shares where it stands now (in its last
      // second of states: alive and near), not one it refused already. The
      // first in id order was a body far away, refused forever (2026-11-18).
      std::optional<tandem::proto::NpcId> target;
      for (const auto npc : liveNpcs)
        if (!target && !grabRefused.contains(npc) && npcSeen.contains(npc) &&
            now - npcSeen[npc] < 1s)
          target = npc;
      if (!target && !grabRefused.empty() && grab.step == 0 && now >= nextGrab) {
        Log("every NPC in view refused a grab: trying them again");
        grabRefused.clear();
      }
      if (grab.step == 0 && now >= nextGrab && target) {
        nextGrab = now + std::chrono::seconds(options.grabNpcsSeconds);
        grab = {*target, 1, now + 2s};
        Log("walk up to npc {}", grab.npc);
      } else if (grab.step == 1 && now >= grab.at) {
        grab.step = 2;
        grab.at = now + 2s;
        endpoint->SendNpcAction(
            {.npc = grab.npc, .kind = NpcActionKind::Grab, .name = CName("GrappleTarget")});
        Log("grab npc {}", grab.npc);
      } else if (grab.step == 2 && now >= grab.at) {
        const bool lethal = takedowns++ % 2 == 0;
        endpoint->SendNpcAction({.npc = grab.npc,
                                 .kind = lethal ? NpcActionKind::Takedown : NpcActionKind::Knockout,
                                 .name = CName(lethal ? "kill" : "setUnconscious")});
        Log("take down npc {} ({})", grab.npc, lethal ? "lethal" : "non-lethal");
        grab.step = 0;
        puppeteer.Beside(std::nullopt);
      }
    }
    // --carry-npcs: pick up a body, carry it three seconds, throw it.
    if (options.carryNpcsSeconds && !options.host && endpoint->IsJoined()) {
      if (carry.step == 0 && now >= nextCarry && !bodies.empty()) {
        nextCarry = now + std::chrono::seconds(options.carryNpcsSeconds);
        carry = {*bodies.begin(), 1, now + 2s};
        Log("walk up to the body of npc {}", carry.npc);
      } else if (carry.step == 1 && now >= carry.at) {
        carry.step = 2;
        carry.at = now + 3s;
        endpoint->SendNpcAction({.npc = carry.npc, .kind = NpcActionKind::PickUp});
        Log("pick up npc {}", carry.npc);
      } else if (carry.step == 2 && now >= carry.at) {
        endpoint->SendNpcAction(
            {.npc = carry.npc, .kind = NpcActionKind::Throw, .direction = {0.f, 100.f, 20.f}});
        Log("throw npc {}", carry.npc);
        bodies.erase(carry.npc);
        carry.step = 0;
        puppeteer.Beside(std::nullopt);
      }
    }
    // Beside the NPC we are busy with, wherever it walks (grab before carry).
    if (grab.step && npcAt.contains(grab.npc))
      puppeteer.Beside(npcAt[grab.npc]);
    else if (carry.step && npcAt.contains(carry.npc))
      puppeteer.Beside(npcAt[carry.npc]);
    else if (options.grabNpcsSeconds || options.carryNpcsSeconds)
      puppeteer.Beside(std::nullopt);
    // --near-npc: 4 m from one of the host's NPCs (the first heard that is
    // still there, alive and placed in the last second), wherever it walks.
    if (options.nearNpc && !options.host && endpoint->IsJoined() && !grab.step && !carry.step) {
      if (!nearNpc || !liveNpcs.contains(*nearNpc) || !npcSeen.contains(*nearNpc) ||
          now - npcSeen[*nearNpc] > 1s) {
        const auto before = nearNpc;
        nearNpc.reset();
        for (const auto npc : liveNpcs)
          if (!nearNpc && npcSeen.contains(npc) && now - npcSeen[npc] < 1s)
            nearNpc = npc;
        if (nearNpc && nearNpc != before)
          Log("we stand by npc {}", *nearNpc);
      }
      if (nearNpc && npcAt.contains(*nearNpc)) {
        auto at = npcAt[*nearNpc];
        at.x += 3.2f; // Beside adds 0.8 m
        puppeteer.Beside(at);
      } else {
        puppeteer.Beside(std::nullopt);
      }
    }
    // --hold-npcs (host): our V grabs a replayed NPC, then takes it down.
    if (options.holdNpcsSeconds && options.host && endpoint->Players().size() > 1) {
      if (hold.step == 0 && now >= nextHold) {
        nextHold = now + std::chrono::seconds(options.holdNpcsSeconds);
        for (const auto npc : replayedNpcs)
          if (!deadNpcs.contains(npc) && !held.contains(npc)) {
            hold = {npc, 1, now + 3s};
            held[npc] = endpoint->You();
            endpoint->SendNpcHold({.npc = npc,
                                   .player = endpoint->You(),
                                   .how = NpcHoldKind::Grappled,
                                   .name = CName("GrappleTarget")});
            for (const auto &[id, info] : endpoint->Players())
              if (id != endpoint->You())
                endpoint->SendNpcCue(
                    {.npc = npc, .player = id, .cue = tandem::proto::NpcCueKind::CombatStart});
            Log("our V grabs npc {}", npc);
            break;
          }
      } else if (hold.step == 1 && now >= hold.at) {
        endpoint->SendNpcHold({.npc = hold.npc,
                               .player = endpoint->You(),
                               .how = NpcHoldKind::Takedown,
                               .name = CName("kill")});
        hold.step = 2;
        hold.at = now + 3s;
        Log("our V takes npc {} down", hold.npc);
      } else if (hold.step == 2 && now >= hold.at) {
        held.erase(hold.npc);
        endpoint->SendNpcHold({.npc = hold.npc});
        deadNpcs.insert(hold.npc);
        endpoint->SendNpcGone(
            {.npc = hold.npc, .reason = tandem::proto::kGoneDead, .killer = endpoint->You()});
        Log("npc {} died", hold.npc);
        hold.step = 0;
      }
    }
    // --level: our V's (the endpoint keeps it and says it once we are in).
    if (options.level > 0 && !levelSent) {
      levelSent = true;
      endpoint->SendCombat(
          tandem::proto::PlayerLevel{.level = static_cast<std::uint8_t>(options.level)});
      Log("our V is level {}", options.level);
    }
    // --lend: who may drive our cars.
    if (options.lend >= 0 && !lendSent) {
      lendSent = true;
      endpoint->SendCombat(
          tandem::proto::CarLend{.lend = static_cast<tandem::proto::Lend>(options.lend)});
      Log("our cars: lent to {}", options.lend == 0   ? "the crew"
                                  : options.lend == 1 ? "the crew with us aboard"
                                                      : "nobody");
    }
    // --unlock-vehicle: our "quest" gives a vehicle; each guest earned it.
    if (!options.unlockVehicle.empty() && options.host && endpoint->IsJoined() &&
        endpoint->Players().size() > 1) {
      if (!unlockAt)
        unlockAt = now + 5s;
      if (now >= *unlockAt)
        for (const auto &[id, info] : endpoint->Players())
          if (id != endpoint->You() && unlockedFor.insert(id).second) {
            endpoint->SendStory(
                tandem::proto::VehicleUnlock{.vehicle = TweakDBID(options.unlockVehicle)}, id);
            Log("vehicle {} unlocked in our garage: player {} earned it with us",
                options.unlockVehicle, id);
          }
    }
    // --vendor: shared once a guest is there, standing still; --vendor-dies.
    if (!options.vendorRecord.empty() && options.host && endpoint->IsJoined() &&
        endpoint->Players().size() > 1 && !vendorDead) {
      namespace proto = tandem::proto;
      if (!vendorSince)
        vendorSince = now;
      if (!vendorShared) {
        endpoint->SendNpcSpawn({.npc = kVendorNpc,
                                .record = TweakDBID(options.vendorRecord),
                                .position = options.vendorAt});
        vendorShared = endpoint->SendCombat(
            proto::NpcVendor{.npc = kVendorNpc, .entity = options.vendorEntity});
        // A placed vendor: its twin in the other games is known by its
        // entity, and its death is the ledger's too (regions.md §14). The
        // crowd's (--vendor-crowd): by record and place, as the game says it.
        if (options.vendorPlaced)
          endpoint->SendCombat(proto::NpcPlaced{.npc = kVendorNpc, .entity = options.vendorEntity});
        Log("vendor npc {}: {} at {:.1f} {:.1f} {:.1f}, the {} vendor {:x}{}", kVendorNpc,
            options.vendorRecord, options.vendorAt.x, options.vendorAt.y, options.vendorAt.z,
            options.vendorPlaced ? "placed" : "crowd's", options.vendorEntity,
            vendorShared ? "" : " (not said: nobody to tell)");
        vendorShared = true;
      }
      if (now >= nextVendorState) {
        nextVendorState = now + 500ms;
        endpoint->SendNpcStates({.npcs = {{.npc = kVendorNpc, .position = options.vendorAt}}});
      }
      if (options.vendorDiesSeconds >= 0 &&
          now - *vendorSince >= std::chrono::seconds(options.vendorDiesSeconds)) {
        endpoint->SendNpcGone(
            {.npc = kVendorNpc, .reason = proto::kGoneDead, .killer = endpoint->You()});
        vendorDead = true;
        Log("vendor npc {} died (--vendor-dies)", kVendorNpc);
      }
    }
    // --npc-level: every NPC we share, once.
    if (options.npcLevel > 0.f && options.host && endpoint->IsJoined())
      for (const auto &[npc, spawn] : endpoint->SharedNpcs())
        if (!levelledNpcs.contains(npc) &&
            endpoint->SendCombat(tandem::proto::NpcLevel{.npc = npc, .level = options.npcLevel}))
          levelledNpcs.insert(npc);
    // Trade: our seat's own clock, and --trade-offer's asking.
    if (endpoint->IsJoined()) {
      tradeSeat.SetYou(endpoint->You());
      auto heard = tradeSeat.Tick(tandem::net::NowUs());
      tradeSend(heard.send);
      for (const auto &event : heard.events)
        if (event.kind == tandem::trade::Seat::Event::Kind::Closed)
          Log("trade: closed with player {} (reason {}): nothing changed hands", event.with,
              static_cast<int>(event.reason));
      if (options.tradeOffer && tradesDone == 0 &&
          tradeSeat.Is() == tandem::trade::Seat::State::None && now >= nextTradeAsk) {
        nextTradeAsk = now + std::chrono::seconds(20);
        for (const auto &[id, info] : endpoint->Players())
          if (id != endpoint->You() && tradeSeat.Is() == tandem::trade::Seat::State::None) {
            tradeSend(tradeSeat.Ask(id, tandem::net::NowUs()));
            Log("trade: asked player {} to trade", id);
          }
      }
    }
    // --difficulty: the session's, kept by the endpoint for whoever joins.
    if (options.difficulty && options.host && !difficultySent && endpoint->IsJoined()) {
      difficultySent = endpoint->SendCombat(
          tandem::proto::Difficulty{.level = static_cast<std::uint8_t>(*options.difficulty)});
      Log("the session plays at difficulty {}", *options.difficulty);
    }
    // --friendly-fire, --enemy-scaling: the session's rules, kept by the
    // endpoint for whoever joins.
    if ((options.friendlyFire || options.enemyScaling) && options.host && !rulesSent &&
        endpoint->IsJoined()) {
      const bool friendly = options.friendlyFire.value_or(false);
      // (The level gap the mock host's endpoint has always used: the plain
      // mean.)
      const auto rules = tandem::scaling::WithRule(
          {.flags = friendly ? tandem::proto::kRuleFriendlyFire : std::uint8_t{0}, .levelGap = 0},
          options.enemyScaling.value_or(false), options.enemyScalingHealth);
      rulesSent = endpoint->SendCombat(rules);
      Log("the session's rules: friendly fire {}, enemy scaling {} (health +{} % per extra "
          "player), level gap {}",
          friendly ? "on" : "off", options.enemyScaling.value_or(false) ? "on" : "off",
          rules.enemyHealth, rules.levelGap);
    }
    // --throw: a grenade 10 m ahead of our V (+Y), exploding 2 s later.
    if (options.throwSeconds && endpoint->IsJoined() && now >= nextThrow) {
      nextThrow = now + std::chrono::seconds(options.throwSeconds);
      tandem::proto::Vec3 from = lastSent.value_or(tandem::proto::Vec3{});
      from.z += 1.5f;
      const tandem::proto::Throw thrown{.id = ++throwId,
                                        .kind = tandem::proto::ThrowKind::Grenade,
                                        .item = TweakDBID(options.throwItem),
                                        .origin = from,
                                        .velocity = {0.f, 7.f, 6.f}};
      if (endpoint->SendCombat(thrown)) {
        Log("we throw grenade {} ({})", thrown.id, options.throwItem);
        Thrown flight{
            thrown.id, now + std::chrono::seconds(2), {from.x, from.y + 10.f, from.z - 1.5f}};
        if (options.throwBounce) {
          // A second in it "bounced off something" to the right: 3 m off
          // the arc a copy flies, and it ends 3 m to the side.
          flight.bounceAt = now + std::chrono::seconds(1);
          flight.bounce = {from.x + 3.f, from.y + 6.f, from.z - 0.5f};
          flight.where.x += 3.f;
        }
        inFlight.push_back(flight);
      }
    }
    for (auto &flight : inFlight)
      if (flight.bounceAt && now >= *flight.bounceAt) {
        flight.bounceAt.reset();
        if (endpoint->SendCombat(tandem::proto::ThrowPath{
                .id = flight.id, .position = flight.bounce, .velocity = {0.f, 4.f, -1.f}}))
          Log("our grenade {} bounced: its path is said", flight.id);
      }
    for (auto it = inFlight.begin(); it != inFlight.end();)
      if (now >= it->at) {
        endpoint->SendCombat(tandem::proto::ThrowEnd{
            .id = it->id, .how = tandem::proto::ThrowEndKind::Exploded, .position = it->where});
        Log("our grenade {} explodes", it->id);
        it = inFlight.erase(it);
      } else
        ++it;
    // --smart: a guided round at an NPC we know.
    // (A guest aims at the host's NPCs, the host of a replay at its own.)
    tandem::proto::NpcId smartAt = liveNpcs.empty() ? 0 : *liveNpcs.begin();
    if (options.host)
      for (const auto npc : replayedNpcs)
        if (!deadNpcs.contains(npc)) {
          smartAt = npc;
          break;
        }
    if (options.smartSeconds && endpoint->IsJoined() && smartAt != 0 && now >= nextSmart) {
      nextSmart = now + std::chrono::seconds(options.smartSeconds);
      tandem::proto::Vec3 from = lastSent.value_or(tandem::proto::Vec3{});
      from.z += 1.4f;
      const auto npc = smartAt;
      // --hold-left: every other round from the left hand's weapon.
      const bool left = akimbo && std::exchange(leftSmart, !leftSmart);
      const auto flags = static_cast<std::uint8_t>(tandem::proto::kThrowHoming |
                                                   (left ? tandem::proto::kThrowLeftHand : 0));
      if (endpoint->SendCombat(
              tandem::proto::Throw{.id = ++throwId,
                                   .kind = tandem::proto::ThrowKind::Smart,
                                   .flags = flags,
                                   .item = TweakDBID("Items.Preset_Yukimura_Default"),
                                   .origin = from,
                                   .velocity = {0.f, 1.f, 0.f},
                                   .npc = npc}))
        Log("our smart round at npc {}{}", npc, left ? " (left hand)" : "");
    }
    // --defend: a parry, then a block, of an NPC's attack on our V.
    if (options.defendSeconds && !options.host && endpoint->IsJoined() && !liveNpcs.empty() &&
        now >= nextDefend) {
      nextDefend = now + std::chrono::seconds(options.defendSeconds);
      const auto npc = *liveNpcs.begin();
      const auto how =
          parryNext ? tandem::proto::DefendKind::Parried : tandem::proto::DefendKind::Blocked;
      parryNext = !parryNext;
      if (endpoint->SendCombat(tandem::proto::Defended{.npc = npc, .how = how}))
        Log("we {} npc {}'s attack", how == tandem::proto::DefendKind::Parried ? "parry" : "block",
            npc);
    }
    // --area-hit: our "grenade" on every other V, for their game to apply.
    if (options.areaHitSeconds && endpoint->IsJoined() && now >= nextAreaHit) {
      nextAreaHit = now + std::chrono::seconds(options.areaHitSeconds);
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You() &&
            endpoint->SendCombat(tandem::proto::AreaHit{.player = id,
                                                        .physical = 20.f,
                                                        .attackType = 3, // Explosion
                                                        .direction = {0.f, 0.f, -1.f}}))
          Log("our grenade hit player {} for 20 physical", id);
    }
    // --story: the scripted host's steps, once a guest is in the session.
    if (options.host && storyNext < story.size() && endpoint->Players().size() > 1) {
      if (!storyStart)
        storyStart = now;
      while (storyNext < story.size() &&
             now >= *storyStart + std::chrono::milliseconds(
                                      static_cast<std::int64_t>(story[storyNext].at * 1000))) {
        const auto &step = story[storyNext++];
        const auto here = lastState.value_or(PlayerState{});
        if (auto scene = SceneMessages(step, sceneScript, here.position,
                                       tandem::sync::YawOf(here.orientation), endpoint->You(),
                                       endpoint->HostNowUs())) {
          for (const auto &message : *scene) {
            endpoint->SendStory(message);
            Log("{}", Describe(message));
          }
          Log("story step: {} {}", step.verb, step.rest);
        } else if (auto message = StoryMessage(step, storyLine, endpoint->You())) {
          // Another world is ours, not the story's: our own state.
          if (auto *away = std::get_if<tandem::proto::Elsewhere>(&*message)) {
            const bool moved = away->flags & tandem::proto::kElsewhereMoved;
            if (moved && !heldBody)
              heldBody = lastState.value_or(PlayerState{});
            if (!moved || away->place == tandem::proto::Place::City)
              heldBody.reset();
            if (heldBody) {
              heldBody->velocity = {};
              away->anchor = heldBody->position;
              away->yaw = tandem::sync::YawOf(heldBody->orientation);
            }
            endpoint->SetLocalElsewhere(*away);
          } else if (auto *busy = std::get_if<tandem::proto::Activity>(&*message)) {
            // Our V busy: our own state too, where it stands.
            if ((busy->flags & tandem::proto::kActivityAnchored) && lastState) {
              busy->anchor = lastState->position;
              busy->yaw = tandem::sync::YawOf(lastState->orientation);
            }
            endpoint->SetLocalActivity(*busy);
          } else if (auto *skip = std::get_if<tandem::proto::TimeSkip>(&*message)) {
            const auto elapsed =
                static_cast<std::int64_t>((tandem::net::NowUs() - startUs) / 1'000'000);
            const auto clock =
                static_cast<std::int32_t>(86400 + options.timeOfDayMinutes.value_or(12 * 60) * 60 +
                                          elapsed * 8 + skippedSeconds);
            endpoint->AskTimeSkip(skip->reason, clock, clock + skip->toSeconds);
            if (skip->reason == tandem::proto::SkipReason::Quest) {
              skippedSeconds += skip->toSeconds;
              nextWorld = now;
            }
          } else if (std::holds_alternative<tandem::proto::QuestReward>(*message)) {
            // A reward goes to every guest, one by one.
            for (const auto &[id, info] : endpoint->Players())
              if (id != endpoint->You())
                endpoint->SendStory(*message, id);
          } else
            endpoint->SendStory(*message);
          Log("story step: {} {}", step.verb, step.rest);
        } else
          Log("story step not understood: {} {}", step.verb, step.rest);
      }
      if (storyNext == story.size())
        Log("story: done");
    }
    // --gates: our story gates, once someone else is in the session.
    if (options.gates >= 0 && !gatesSaid && endpoint->IsJoined() &&
        endpoint->Players().size() > 1) {
      gatesSaid = true;
      endpoint->SendStory(
          tandem::proto::StoryGates{.flags = static_cast<std::uint8_t>(options.gates)});
      Log("our story gates: {}", options.gates);
    }
    // --answer / --reply: a guest acting on the host's calls and texts.
    if (!options.host && endpoint->IsJoined()) {
      if (!joinedAt)
        joinedAt = now;
      if (toAnswer && now >= toAnswer->second) {
        endpoint->SendStory(tandem::proto::PhoneAction{.action = tandem::proto::kPhoneAnswer,
                                                       .contact = toAnswer->first});
        Log("answer the call from {:#x}", toAnswer->first);
        toAnswer.reset();
      }
      if (options.replyEntry && !replied && *joinedAt + 3s <= now) {
        endpoint->SendStory(tandem::proto::PhoneReply{.choiceEntry = options.replyEntry});
        Log("reply {}", options.replyEntry);
        replied = true;
      }
    }
    // Quickhacks. Uploads whose time is up: the host's word that they are
    // done, our own effect landing.
    for (auto it = pendingHacks.begin(); it != pendingHacks.end();) {
      if (now < it->at) {
        ++it;
        continue;
      }
      if (it->effect) {
        endpoint->SendNpcEffect({.npc = it->state.npc, .effects = {it->effect}});
        Log("upload on npc {} done: effect {}", it->state.npc, options.hackEffect);
      } else {
        endpoint->SendNpcHackState(it->state);
        Log("npc {}: player {}'s upload done", it->state.npc, it->state.player);
      }
      it = pendingHacks.erase(it);
    }
    // --hack-npcs: upload on one of the host's NPCs, as the game would send it.
    if (options.hackNpcsSeconds && !options.host && endpoint->IsJoined() && !liveNpcs.empty() &&
        now >= nextHack) {
      nextHack = now + std::chrono::seconds(options.hackNpcsSeconds);
      const auto npc = *liveNpcs.begin();
      endpoint->SendNpcHack(
          {.npc = npc, .action = TweakDBID(options.hackAction), .uploadMs = 1500, .flags = 0});
      pendingHacks.push_back({now + 1500ms, {.npc = npc}, TweakDBID(options.hackEffect)});
      Log("upload {} on npc {} (1.5 s)", options.hackAction, npc);
    }
    // --hack-player: one of our NPCs hacks the first guest.
    if (options.hackPlayerSeconds && options.host && !replayedNpcs.empty() &&
        endpoint->Players().size() > 1 && now >= nextPlayerHack) {
      nextPlayerHack = now + std::chrono::seconds(options.hackPlayerSeconds);
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You()) {
          const auto npc = *replayedNpcs.begin();
          const bool trace = options.npcHack == "AIQuickHack.HackRevealPosition";
          endpoint->SendPlayerHack(
              {.player = id,
               .npc = npc,
               .action = TweakDBID(options.npcHack),
               .flags = trace ? tandem::proto::kPlayerHackReveal : std::uint8_t{0}});
          Log("npc {} hacks player {} with {}", npc, id, options.npcHack);
          if (options.hackInterruptSeconds >= 0.0) {
            interruptNpc = npc;
            interruptAt = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                    std::chrono::duration<double>(options.hackInterruptSeconds));
          }
          break;
        }
    }
    // --hack-interrupt: what stops its upload, on the netrunner (its owner's word).
    if (interruptAt && now >= *interruptAt) {
      interruptAt.reset();
      interruptEnd = now + 2s;
      endpoint->SendNpcStatus({.npc = interruptNpc,
                               .effect = TweakDBID("AIQuickHackStatusEffect.HackingInterrupted"),
                               .applied = true});
      Log("npc {}'s hack interrupted (AIQuickHackStatusEffect.HackingInterrupted)", interruptNpc);
    }
    if (interruptEnd && now >= *interruptEnd) {
      interruptEnd.reset();
      endpoint->SendNpcStatus({.npc = interruptNpc,
                               .effect = TweakDBID("AIQuickHackStatusEffect.HackingInterrupted"),
                               .applied = false});
    }
    // --hack-car: brake the car someone else drives.
    if (options.hackCarSeconds && endpoint->IsJoined() && now >= nextCarHack) {
      nextCarHack = now + std::chrono::seconds(options.hackCarSeconds);
      for (const auto &[player, drives] : driving)
        if (drives && player != endpoint->You()) {
          endpoint->SendVehicleHack(
              {.driver = player, .hack = tandem::proto::CarHack::ForceBrakes, .seconds = 3.f});
          Log("hack player {}'s car: brakes for 3 s", player);
        }
    }
    // --hit-you: one of our NPCs (a live replayed one, else none) shoots each
    // guest: an ordinary ranged hit (gamedataAttackType.Ranged = 10), no
    // attack record (the game falls back to a plain bullet).
    if (options.hitYouSeconds && options.host && now >= nextHitYou) {
      nextHitYou = now + std::chrono::seconds(options.hitYouSeconds);
      tandem::proto::NpcId shooter = 0;
      for (const auto npc : replayedNpcs)
        if (!deadNpcs.contains(npc)) {
          shooter = npc;
          break;
        }
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You() &&
            endpoint->SendPlayerHit(
                {.player = id,
                 .npc = shooter,
                 .physical = 10.f,
                 .attackType = static_cast<std::uint8_t>(options.hitYouMelee ? 7 : 10),
                 .flags = options.hitYouMelee ? tandem::proto::kHitMelee : std::uint8_t{0},
                 .direction = {0.f, 1.f, 0.f}}))
          Log("npc {} hit player {} for 10 physical{}", shooter, id,
              options.hitYouMelee ? " (melee)" : "");
    }
    // --shoot-you: one of our (replayed) NPCs fires at each guest; the
    // guest's game fires the shot from its proxy and decides the hit.
    if (options.shootYouSeconds && options.host && now >= nextShootYou) {
      nextShootYou = now + std::chrono::seconds(options.shootYouSeconds);
      // A live NPC with a weapon drawn, as a real host's shooter has; else
      // the first live one, made to draw --shot-weapon first (its animation
      // state says so, as a real host's would: the guest's proxy is armed
      // from it, NpcAnims.reds).
      tandem::proto::NpcId shooter = 0;
      for (const auto npc : replayedNpcs)
        if (!deadNpcs.contains(npc) && npcWeapons[npc] != 0) {
          shooter = npc;
          break;
        }
      if (!shooter)
        for (const auto npc : replayedNpcs)
          if (!deadNpcs.contains(npc)) {
            shooter = npc;
            npcWeapons[npc] = TweakDBID(options.shotWeapon);
            endpoint->SendNpcAnims({.npcs = {{.npc = npc,
                                              .highLevel = 2, // Combat
                                              .upperBody = 0, // Aim
                                              .flags = tandem::proto::kNpcAnimWeaponDrawn,
                                              .weapon = npcWeapons[npc]}}});
            Log("npc {} draws {} to fire", npc, options.shotWeapon);
            break;
          }
      // The host's game decides whether a shot may land (tbh 0, the time
      // between hits passed on its puppet); here every other one does.
      static unsigned shotsAtYou = 0;
      const bool lands = shooter && ++shotsAtYou % 2 == 0;
      for (const auto &[id, info] : endpoint->Players())
        if (shooter && id != endpoint->You() &&
            endpoint->SendNpcShotAt({.player = id,
                                     .npc = shooter,
                                     .weapon = npcWeapons[shooter],
                                     .tbh = lands ? 0.f : 1.f,
                                     .trigger = 4, // SemiAuto
                                     .factor = 1.f,
                                     .flags = options.shotSmart ? static_cast<std::uint8_t>(
                                                                      tandem::proto::kShotAtSmart |
                                                                      tandem::proto::kShotAtTracked)
                                                                : std::uint8_t{0}}))
          Log("npc {} fires at player {}{}{}", shooter, id, options.shotSmart ? " (guided)" : "",
              lands ? " (may land)" : "");
    }
    // Seconds since another player was first in the session with us.
    if (!sharedSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
      sharedSince = now;
    const double shared =
        sharedSince ? std::chrono::duration<double>(now - *sharedSince).count() : -1.0;
    // --traffic: cars round where the guest was first heard, one parked.
    if (options.host && options.trafficCars > 0 && firstRemote &&
        shared >= static_cast<double>(options.trafficAtSeconds)) {
      namespace proto = tandem::proto;
      const auto place = [&](const StreetCar &car) {
        tandem::traffic::Car at{.id = car.id};
        at.position = {streetCentre->x + std::cos(car.angle) * car.radius,
                       streetCentre->y + std::sin(car.angle) * car.radius, streetCentre->z};
        // Along the circle, anticlockwise: yaw 0 looks along +Y.
        at.orientation = tandem::sync::FromYaw(car.angle);
        at.velocity = {-std::sin(car.angle) * car.speed, std::cos(car.angle) * car.speed, 0.f};
        return at;
      };
      if (!streetCentre) {
        streetCentre = firstRemote->position;
        for (int n = 0; n <= options.trafficCars; ++n) {
          const bool parked = n == options.trafficCars;
          StreetCar car{
              .id = proto::MakeTrafficId(endpoint->You(), static_cast<std::uint32_t>(n + 1)),
              .angle =
                  6.2832f * static_cast<float>(n) / static_cast<float>(options.trafficCars + 1),
              .radius = parked ? 8.f : 14.f + 4.f * static_cast<float>(n % 3),
              .speed = parked ? 0.f : 7.f,
              .flags = parked ? proto::kTrafficParked : proto::kTrafficLights};
          const auto at = place(car);
          endpoint->SendTrafficSpawn({.id = car.id,
                                      .record = TweakDBID(options.trafficVehicle),
                                      .position = at.position,
                                      .orientation = at.orientation,
                                      .flags = car.flags});
          street.push_back(car);
        }
        Log("street: {} cars of {} round player {}, one parked", options.trafficCars,
            options.trafficVehicle, firstRemote->player);
        // A driver in the first one (--npc): an NPC like any other, seated.
        if (!options.npcRecord.empty()) {
          endpoint->SendNpcSpawn({.npc = kStreetDriver,
                                  .record = TweakDBID(options.npcRecord),
                                  .position = place(street.front()).position});
          endpoint->SendNpcSeat({.npc = kStreetDriver,
                                 .owner = proto::TrafficSeatOwner(street.front().id),
                                 .seat = CName("seat_front_left")});
          Log("street: npc {} at the wheel of vehicle {:#x}", kStreetDriver, street.front().id);
        }
      }
      if (now >= nextStreetState) {
        nextStreetState = now + 100ms;
        std::vector<tandem::traffic::Car> cars;
        for (auto &car : street) {
          if (car.gone)
            continue;
          car.angle += car.speed / car.radius * 0.1f;
          cars.push_back(place(car));
        }
        std::vector<Vec3> viewers;
        if (otherV)
          viewers.push_back(otherV->position);
        endpoint->SendTrafficStates({.cars = streetBudget.Pick(cars, viewers)});
      }
      // --wreck-at: the first car burns, then blows up 4 s later.
      if (options.wreckAtSeconds >= 0 && !street.front().gone) {
        auto &car = street.front();
        if (wreckStep == 0 && shared >= options.wreckAtSeconds) {
          wreckStep = 1;
          car.flags |= proto::kTrafficBurning;
          car.health = 0.15f;
          endpoint->SendTrafficStatus({.id = car.id, .health = car.health, .flags = car.flags});
          Log("street: vehicle {:#x} burns", car.id);
        } else if (wreckStep == 1 && shared >= options.wreckAtSeconds + 4) {
          wreckStep = 2;
          car.flags = static_cast<std::uint16_t>((car.flags & ~proto::kTrafficBurning) |
                                                 proto::kTrafficDestroyed);
          car.health = 0.f;
          car.speed = 0.f;
          endpoint->SendTrafficStatus({.id = car.id, .health = 0.f, .flags = car.flags});
          Log("street: vehicle {:#x} blew up: a wreck", car.id);
        }
      }
    }
    // --ram-traffic: our car hits a shared vehicle (what our game would
    // send when it rams the proxy: the damage, and the push it gave).
    if (options.ramTrafficSeconds && !options.host && endpoint->IsJoined() && !sharedCars.empty() &&
        now >= nextRam) {
      nextRam = now + std::chrono::seconds(options.ramTrafficSeconds);
      const auto &[id, spawn] = *sharedCars.begin();
      endpoint->SendTrafficHit({.id = id,
                                .damage = 300.f,
                                .flags = tandem::proto::kTrafficHitCollision,
                                .position = spawn.position,
                                .impulse = {0.f, 6.f, 0.f}});
      Log("street: rammed vehicle {:#x} for 300", id);
    }
    // --hack-traffic: our V quickhacks the nearest shared vehicle that still
    // drives, once: its owner applies it, every game marks it.
    if (options.hackTrafficAtSeconds >= 0 && !hackedTraffic && !options.host &&
        endpoint->IsJoined() && shared >= options.hackTrafficAtSeconds && !sharedCars.empty()) {
      hackedTraffic = true;
      const Vec3 from = lastState ? lastState->position : Vec3{};
      const auto nearest = std::ranges::min_element(sharedCars, {}, [&](const auto &entry) {
        const auto &p = entry.second.position;
        const float wreck = entry.second.flags & tandem::proto::kTrafficDestroyed ? 1e12f : 0.f;
        return wreck + (p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y);
      });
      endpoint->SendTrafficHack(
          {.id = nearest->first, .hack = tandem::proto::CarHack::ForceBrakes, .seconds = 8.f});
      Log("street: hacked vehicle {:#x} (brakes, 8 s)", nearest->first);
    }
    // --steal-at: into the nearest shared vehicle, at its wheel.
    if (options.stealAtSeconds >= 0 && !options.host && shared >= options.stealAtSeconds) {
      const bool candidate =
          !options.stealDriven || std::ranges::any_of(sharedCars, [](const auto &entry) {
            return !(entry.second.flags & tandem::proto::kTrafficParked);
          });
      if (stealStep == 0 && !sharedCars.empty() && candidate) {
        // The nearest one that still drives: nobody gets into a wreck (with
        // --steal-driven, nor into a parked car: one with a driver).
        const Vec3 from = lastState ? lastState->position : Vec3{};
        const auto nearest = std::ranges::min_element(sharedCars, {}, [&](const auto &entry) {
          const auto &p = entry.second.position;
          const auto flags = entry.second.flags;
          const bool skip = (flags & tandem::proto::kTrafficDestroyed) ||
                            (options.stealDriven && (flags & tandem::proto::kTrafficParked));
          return (skip ? 1e12f : 0.f) + (p.x - from.x) * (p.x - from.x) +
                 (p.y - from.y) * (p.y - from.y);
        });
        stolen = nearest->second;
        endpoint->ClaimTraffic(stolen->id, CName("seat_front_left"));
        stealStep = 1;
        stolenAt = shared;
        Log("street: getting into vehicle {:#x}{}", stolen->id,
            stolen->flags & tandem::proto::kTrafficParked ? " (parked)" : " (driven)");
      } else if (stealStep == 1 && !sharedCars.contains(stolen->id)) {
        // The claim came back (TrafficLeft above said whose it is): if it
        // is ours, we sit at its wheel where it stands.
        stealStep = 2;
        endpoint->SetLocalSeat({.vehicle = stolen->record, .seat = CName("seat_front_left")});
      } else if (stealStep == 2) {
        endpoint->SendLocalVehicle(
            {.position = stolen->position, .orientation = stolen->orientation});
        if (shared - stolenAt > (options.driveSeconds > 0 ? options.driveSeconds : 15)) {
          endpoint->SetLocalSeat({});
          stealStep = 3;
          Log("street: out of the car we took");
        }
      }
    }
    // --heat: our V is wanted, seen until the last 5 s, then clean again.
    if (options.heat > 0 && shared >= 0.0) {
      using tandem::proto::HeatState;
      const double at = options.heatAtSeconds, end = at + options.heatForSeconds;
      const int step = shared < at ? 0 : shared < end - 5.0 ? 1 : shared < end ? 2 : 3;
      if (step != heatStep) {
        heatStep = step;
        const auto stage = static_cast<std::uint8_t>(step == 1 || step == 2 ? options.heat : 0);
        endpoint->SetLocalHeat(
            {.stage = stage, .state = step == 1 ? HeatState::Active : HeatState::Searching});
        Log("police: our wanted level {}{}", stage,
            step == 1   ? " (seen)"
            : step == 2 ? " (lost)"
                        : "");
      }
    }
    // --crime-at: a witness of ours called the police on the guest's V.
    if (options.host && options.crimeAtSeconds >= 0 && !crimeSaid && firstRemote &&
        shared >= options.crimeAtSeconds) {
      crimeSaid = true;
      const bool sent = endpoint->SendCrime({.player = firstRemote->player,
                                             .kind = tandem::proto::CrimeKind::Witness,
                                             .position = firstRemote->position});
      Log("police: crime of player {} reported to its game{}", firstRemote->player,
          sent ? "" : " (not in the session)");
    }
    // --detect-at: the host's NPCs notice the guests (2 s), have detected
    // them (--detect-for s), then lose them.
    if (options.host && options.detectAtSeconds >= 0 && shared >= 0.0) {
      const double at = options.detectAtSeconds;
      const int phase = shared < at                                    ? 0
                        : shared < at + 2.0                            ? 1
                        : shared < at + 2.0 + options.detectForSeconds ? 2
                                                                       : 3;
      if (phase != detectPhase) {
        detectPhase = phase;
        constexpr const char *kPhases[] = {"", "notice", "have detected", "lost"};
        Log("npcs {} the guests", kPhases[phase]);
      }
      if (now >= nextAwareness) {
        nextAwareness = now + 200ms;
        const auto level = phase == 1   ? static_cast<std::uint8_t>((shared - at) / 2.0 * 255.0)
                           : phase == 2 ? std::uint8_t{255}
                                        : std::uint8_t{0};
        tandem::proto::NpcAwareness awareness;
        auto npcs = sentNpcs;
        if (ownNpc)
          npcs.push_back(ownNpc->npc);
        for (const auto npc : npcs)
          for (const auto &[id, info] : endpoint->Players())
            if (id != endpoint->You() && level > 0 &&
                awareness.entries.size() < tandem::proto::kMaxNpcAwareness)
              awareness.entries.push_back({.npc = npc, .player = id, .level = level});
        endpoint->SendNpcAwareness(std::move(awareness));
      }
    }
    // --npc: one NPC standing 6 m in front of where the guest was first
    // heard, facing it, its states at 10 Hz (with the replay's, if any).
    if (options.host && !options.npcRecord.empty() && firstRemote && shared >= 0.0) {
      if (!ownNpc) {
        const float yaw = tandem::sync::YawOf(firstRemote->orientation);
        const Vec3 at{firstRemote->position.x - std::sin(yaw) * 6.f,
                      firstRemote->position.y + std::cos(yaw) * 6.f, firstRemote->position.z};
        ownNpc = tandem::proto::NpcEntry{
            .npc = kOwnNpc, .position = at, .yaw = yaw + std::numbers::pi_v<float>};
        endpoint->SendNpcSpawn({.npc = kOwnNpc,
                                .record = TweakDBID(options.npcRecord),
                                .position = at,
                                .yaw = ownNpc->yaw,
                                .hint = options.npcHint});
        Log("npc {}: {} in front of player {}", kOwnNpc, options.npcRecord, firstRemote->player);
      }
      ownNpc->detected = detectedBits();
      if (replay.empty() && now >= nextNpcState) {
        nextNpcState = now + 100ms;
        endpoint->SendNpcStates({.npcs = {*ownNpc}});
      }
    }
    // --camo: our optical camo on for 5 s.
    if (options.camoAtSeconds >= 0 && shared >= 0.0 && camoStep < 2) {
      if (camoStep == 0 && shared >= options.camoAtSeconds) {
        endpoint->SetLocalStealth(
            {.flags = tandem::proto::kStealthCamo,
             .visibility = 0.2f,
             .camo = options.camoRecord.empty() ? 0 : TweakDBID(options.camoRecord)});
        camoStep = 1;
        Log("optical camo on{}", options.camoRecord.empty() ? "" : " (" + options.camoRecord + ")");
      } else if (camoStep == 1 && shared >= options.camoAtSeconds + 5) {
        endpoint->SetLocalStealth({.flags = 0, .visibility = 1.f});
        camoStep = 2;
        Log("optical camo off");
      }
    }
    // --status: the status effects that show on our V now, as one set.
    if (!statusWindows.empty() && shared >= 0.0) {
      std::vector<std::uint64_t> on;
      std::string names;
      for (const auto &window : statusWindows)
        if (shared >= window.from && (window.to <= window.from || shared < window.to)) {
          on.push_back(TweakDBID(window.record));
          names += (names.empty() ? "" : ", ") + window.record;
        }
      auto set = tandem::presence::StatusSet(on);
      if (set != statusShown) {
        statusShown = set;
        endpoint->SetLocalStatusVisuals(set);
        Log("status visuals on our V: {}", names.empty() ? "none" : names);
      }
    }
    // Where a shot or a swing of ours lands: 6 m past the other V at chest
    // height, a step to its side, so the bullet is seen going by.
    const auto beyond = [&]() -> std::optional<tandem::proto::Vec3> {
      if (lastOf.empty())
        return std::nullopt;
      const auto &other = lastOf.begin()->second;
      const float yaw = tandem::sync::YawOf(other.orientation);
      return tandem::proto::Vec3{other.position.x + std::sin(yaw) * 6.f + std::cos(yaw) * 1.f,
                                 other.position.y - std::cos(yaw) * 6.f + std::sin(yaw) * 1.f,
                                 other.position.z + 1.3f};
    };
    // --shoot: a gunshot, seen (Shot, and where its bullet ended: a Cue) and,
    // from a guest, heard by the host's NPCs (Noise, gamedataStimType.Gunshot 28).
    if (options.shootSeconds && endpoint->IsJoined() && now >= nextShot) {
      nextShot = now + std::chrono::seconds(options.shootSeconds);
      // --hold-left: each hand in turn, its bullet and its bounce the
      // same hand's (kTraceLeftHand).
      const bool left = akimbo && std::exchange(leftShot, !leftShot);
      const std::uint8_t hand = left ? tandem::proto::kTraceLeftHand : std::uint8_t{0};
      endpoint->SendLocalShot(
          {.hand = left ? tandem::proto::Hand::Left : tandem::proto::Hand::Right, .effect = 16});
      if (const auto at = beyond()) {
        endpoint->SendLocalCue({.kind = tandem::proto::CueKind::Trace, .arg = hand, .at = *at});
        // --ricochet: it bounced off where it ended and hit something there.
        if (options.ricochet) {
          endpoint->SendLocalCue({.kind = tandem::proto::CueKind::Ricochet,
                                  .arg = hand,
                                  .at = {at->x + 3.f, at->y, at->z + 1.5f}});
          Log("ricochet");
        }
      }
      if (!options.host)
        endpoint->SendNoise({.stim = 28, .radius = 25.f});
      Log("gunshot{}", left ? " (left hand)" : "");
    }
    // --charge: charging for 1 s, ready for 0.5 s, full for 1 s, said on
    // each change and every 0.25 s with how charged; then the shot and the
    // charge's end.
    if (options.chargeSeconds && endpoint->IsJoined()) {
      if (!chargeFrom && now >= nextCharge) {
        chargeFrom = now;
        // --hold-left: every other charge is the left hand's weapon's (the
        // right's first).
        leftCharge = akimbo && !leftCharge;
      }
      if (chargeFrom) {
        namespace proto = tandem::proto;
        const double in = std::chrono::duration<double>(now - *chargeFrom).count();
        const int step = in < 1.0   ? proto::kChargeCharging
                         : in < 1.5 ? proto::kChargeReady
                         : in < 2.5 ? proto::kChargeFull
                                    : proto::kChargeIdle;
        if (step != chargeStep || now - chargeSaid >= std::chrono::milliseconds(250)) {
          if (step != chargeStep)
            Log("charge step {}{}", step, leftCharge ? " (left hand)" : "");
          chargeStep = step;
          chargeSaid = now;
          const float charged =
              step == proto::kChargeIdle ? 0.f : std::min(1.f, static_cast<float>(in) / 1.5f);
          endpoint->SendLocalCue(
              {.kind = proto::CueKind::Charge,
               .arg = static_cast<std::uint8_t>(step),
               .at = {charged, leftCharge ? proto::kChargeLeftHand : proto::kChargeRightHand,
                      0.f}});
        }
        if (step == proto::kChargeIdle) {
          endpoint->SendLocalShot(
              {.hand = leftCharge ? proto::Hand::Left : proto::Hand::Right, .effect = 16});
          if (const auto at = beyond())
            endpoint->SendLocalCue({.kind = proto::CueKind::Trace,
                                    .arg = leftCharge ? proto::kTraceLeftHand : std::uint8_t{0},
                                    .at = *at});
          Log("charged shot");
          chargeFrom.reset();
          chargeStep = -1;
          nextCharge = now + std::chrono::seconds(options.chargeSeconds);
        }
      }
    }
    // --melee: a swing (Shot 200), and it lands (a Cue).
    if (options.meleeSeconds && endpoint->IsJoined() && now >= nextMelee) {
      nextMelee = now + std::chrono::seconds(options.meleeSeconds);
      endpoint->SendLocalShot({.hand = tandem::proto::Hand::Right, .effect = 200});
      if (const auto at = beyond())
        endpoint->SendLocalCue({.kind = tandem::proto::CueKind::MeleeHit, .at = *at});
      Log("melee swing");
    }
    // --hurt: our V's health, a quarter down each time, then full again.
    // --menu-at: in a menu for 5 s. Both are our Presence.
    if (endpoint->IsJoined() && (options.hurtSeconds || options.menuAtSeconds >= 0)) {
      bool changed = !presenceSent;
      if (options.hurtSeconds && now >= nextHurt) {
        nextHurt = now + std::chrono::seconds(options.hurtSeconds);
        if (presenceSent) {
          const bool hit = ourHealth > 0.3f;
          ourHealth = hit ? ourHealth - 0.25f : 1.f;
          changed = true;
          // --hurt-hit: the hit it was, for the others' puppet of our V: a
          // bullet (gamedataAttackType.Ranged 10) on its left leg
          // (EHitReactionZone.LegLeft 8, flesh), going east.
          if (hit && !options.hurtHit.empty()) {
            tandem::proto::HitTaken taken{.attackType = 10,
                                          .zone = 8,
                                          .shape = 0,
                                          .share = 64,
                                          .offset = {-0.15f, 0.f, 0.5f},
                                          .direction = {1.f, 0.f, 0.f}};
            if (options.hurtHit == "npc") {
              const tandem::proto::NpcId npc = options.host
                                                   ? (options.npcRecord.empty() ? 0 : kOwnNpc)
                                                   : (liveNpcs.empty() ? 0 : *liveNpcs.begin());
              if (npc != 0) {
                taken.by = tandem::proto::HitBy::Npc;
                taken.attacker = npc;
              }
            } else if (options.hurtHit == "you" && !lastOf.empty()) {
              taken.by = tandem::proto::HitBy::Player;
              taken.attacker = lastOf.begin()->first;
            }
            endpoint->SendLocalHitTaken(taken);
            Log("our V took a hit on its left leg: by {} {}",
                taken.by == tandem::proto::HitBy::Npc      ? "npc"
                : taken.by == tandem::proto::HitBy::Player ? "player"
                                                           : "nobody known,",
                taken.attacker);
          }
        }
      }
      const bool inMenu = options.menuAtSeconds >= 0 && shared >= options.menuAtSeconds &&
                          shared < options.menuAtSeconds + 5;
      changed |= inMenu != ourMenu;
      ourMenu = inMenu;
      if (changed) {
        presenceSent = true;
        endpoint->SetLocalPresence(
            {.health = tandem::presence::HealthCode(ourHealth),
             .flags = static_cast<std::uint8_t>(ourMenu ? tandem::proto::kPresenceMenu : 0)});
        Log("our V: health {:.0f} %{}", ourHealth * 100.f, ourMenu ? ", in a menu" : "");
      }
    }
    // --holster-at: the weapon away, then drawn again.
    if (options.holsterAtSeconds >= 0 && (!options.holdRecord.empty() || akimbo) &&
        holsterStep < 2) {
      if (holsterStep == 0 && shared >= options.holsterAtSeconds) {
        endpoint->SetLocalHeld({});
        holsterStep = 1;
        Log("weapon holstered");
      } else if (holsterStep == 1 && shared >= options.holsterAtSeconds + 5) {
        endpoint->SetLocalHeld(ourHeld());
        holsterStep = 2;
        Log("weapon drawn");
      }
    }
    // --react-npcs: one of the NPCs we share plays the next hit reaction.
    if (options.reactNpcsSeconds && options.host && endpoint->Players().size() > 1 &&
        !endpoint->SharedNpcs().empty() && now >= nextReaction) {
      nextReaction = now + std::chrono::seconds(options.reactNpcsSeconds);
      constexpr std::uint8_t kCycle[] = {1, 2, 3, 5, 6, 0};
      const auto type = kCycle[reactionStep++ % std::size(kCycle)];
      const auto npc = endpoint->SharedNpcs().begin()->first;
      endpoint->SendNpcReaction({.npc = npc,
                                 .type = type,
                                 .direction = 3,
                                 .intensity = 1,
                                 .bodyPart = 4,
                                 .way = {0.f, -3.f, 1.f}});
      Log("npc {} plays hit reaction {}", npc, type);
    }
    // --voice-npcs: one of the NPCs we share says the next voice line (the
    // game's own contexts: GameObject.PlayVoiceOver's callers).
    if (options.voiceNpcsSeconds && options.host && endpoint->Players().size() > 1 &&
        !endpoint->SharedNpcs().empty() && now >= nextVoice) {
      nextVoice = now + std::chrono::seconds(options.voiceNpcsSeconds);
      constexpr std::string_view kLines[] = {"start_combat",      "enemy_warning",
                                             "vo_any_damage_hit", "hit_reaction_heavy",
                                             "fear_beg",          "stlh_death"};
      const auto line = kLines[voiceStep++ % std::size(kLines)];
      const auto npc = endpoint->SharedNpcs().begin()->first;
      endpoint->SendNpcVoice({.npc = npc, .context = tandem::mods::NameHash(line)});
      Log("npc {} says {}", npc, line);
    }
    // --corpse-npcs: one of the NPCs we share dies; its body's rest (a
    // neutral pose where it stood) 1 s later, as a game's settled body.
    if (options.corpseNpcsSeconds && options.host && endpoint->Players().size() > 1 &&
        !endpoint->SharedNpcs().empty() && now >= nextCorpse) {
      nextCorpse = now + std::chrono::seconds(options.corpseNpcsSeconds);
      const auto [npc, spawn] = *endpoint->SharedNpcs().begin();
      endpoint->SendNpcGone(
          {.npc = npc, .reason = tandem::proto::kGoneDead, .killer = endpoint->You()});
      Log("npc {} died (--corpse-npcs)", npc);
      corpsesDue.push_back({{.npc = npc, .position = spawn.position, .bodies = RestingBodies()},
                            now + std::chrono::seconds(1)});
    }
    for (auto it = corpsesDue.begin(); it != corpsesDue.end();)
      if (now >= it->second) {
        if (endpoint->SendCombat(it->first))
          Log("npc {} at rest: its pose sent", it->first.npc);
        it = corpsesDue.erase(it);
      } else {
        ++it;
      }
    // Replay, once someone is there to see it, at the recording's pace.
    if (options.host && replayNext < replay.size() && endpoint->Players().size() > 1) {
      const auto nowUs = tandem::net::NowUs();
      if (!replayStartUs)
        replayStartUs = nowUs;
      while (replayNext < replay.size() && replay[replayNext].atUs <= nowUs - *replayStartUs)
        replayNpcWorld(replay[replayNext++].message, true);
      if (replayNext == replay.size())
        Log("replay: done");
    }
    // --play-session: the recorded host's messages as their time comes, once
    // a guest is in, as this host's own.
    if (sessionPlay && options.host && !sessionPlay->Done() && endpoint->Players().size() > 1) {
      const auto nowUs = tandem::net::NowUs();
      if (!sessionStartUs) {
        sessionStartUs = nowUs;
        Log("session: started");
      }
      const auto &header = sessionPlay->Of().header;
      tandem::play::SessionMapping mapping{
          .host = header.host, .you = header.player, .self = endpoint->You()};
      for (const auto &[id, info] : endpoint->Players())
        if (id != endpoint->You()) {
          mapping.guest = id;
          break;
        }
      const auto lap = sessionPlay->Lap();
      for (const auto *entry : sessionPlay->Due(nowUs - *sessionStartUs)) {
        const auto shift = static_cast<std::int64_t>(endpoint->HostNowUs()) -
                           static_cast<std::int64_t>(header.startHostUs + entry->atUs);
        auto message = tandem::play::AdaptSession(entry->message, mapping, shift);
        if (!message) {
          ++sessionDropped;
          continue;
        }
        ++sessionSent;
        if (std::holds_alternative<tandem::proto::NpcSpawn>(*message) ||
            std::holds_alternative<tandem::proto::NpcStates>(*message) ||
            std::holds_alternative<tandem::proto::NpcGone>(*message) ||
            std::holds_alternative<tandem::proto::NpcHealth>(*message) ||
            std::holds_alternative<tandem::proto::NpcAnims>(*message) ||
            std::holds_alternative<tandem::proto::NpcShot>(*message)) {
          if (const auto *spawn = std::get_if<tandem::proto::NpcSpawn>(&*message);
              spawn && !replayedNpcs.contains(spawn->npc))
            ++sessionNpcs;
          replayNpcWorld(std::move(*message), false);
          continue;
        }
        if (!std::holds_alternative<PlayerState>(*message) &&
            !std::holds_alternative<tandem::proto::PoseState>(*message) &&
            !std::holds_alternative<tandem::proto::VehicleState>(*message) &&
            !std::holds_alternative<tandem::proto::NpcAwareness>(*message))
          Log("session: {}", tandem::proto::NameOf(*message));
        tandem::play::ApplyHost(*endpoint, *message, mapping.guest);
      }
      if (sessionPlay->Lap() != lap)
        Log("session: lap {}", sessionPlay->Lap() + 1);
      if (sessionPlay->Done())
        Log("session: done, {} messages sent ({} NPCs), {} not for this session", sessionSent,
            sessionNpcs, sessionDropped);
    }
    // --go-down / --go-out / --revive: down and out.
    if (!withSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
      withSince = now;
    // Gone like a killed game: no Bye, no ENet disconnect; the others find
    // out by the silence (5 s).
    if (withSince && options.vanishAtSeconds >= 0 &&
        now >= *withSince + std::chrono::seconds(options.vanishAtSeconds)) {
      Log("vanishing (--vanish-at): no goodbye");
      std::_Exit(0);
    }
    if (withSince && !downDone && ourLife == 0) {
      if (options.goDownSeconds >= 0 &&
          now >= *withSince + std::chrono::seconds(options.goDownSeconds)) {
        downDone = true;
        setLife(1, "--go-down");
      } else if (options.goOutSeconds >= 0 &&
                 now >= *withSince + std::chrono::seconds(options.goOutSeconds)) {
        downDone = true;
        outSince = now;
        setLife(2, "--go-out");
      }
    }
    if (ourLife == 2 && outSince && now >= *outSince + 10s) {
      outSince.reset();
      lying.reset();
      setLife(0, "up again beside the others");
    }
    if (rescue && ourLife == 0 && now >= rescue->at) {
      if (!rescue->started) {
        endpoint->SendRevive({.target = rescue->target, .step = tandem::proto::ReviveStep::Start});
        rescue->started = true;
        rescue->at = now + 10s;
        Log("reviving player {} (10 s)", rescue->target);
      } else {
        endpoint->SendRevive({.target = rescue->target, .step = tandem::proto::ReviveStep::Done});
        Log("revived player {}", rescue->target);
        rescue.reset();
      }
    }
    if (options.host && replayStartUs && anims.Size() > 0) {
      const auto nowUs = tandem::net::NowUs();
      // --anims: the script's changes as their time comes, on the NPCs the
      // replay has out (all of them for *), and their shots.
      const auto elapsedUs = nowUs - *replayStartUs;
      for (; animsNext < anims.Size() && anims.Lines()[animsNext].atUs <= elapsedUs; ++animsNext) {
        const auto &line = anims.Lines()[animsNext];
        tandem::proto::NpcAnims batch;
        for (const auto npc : replayedNpcs) {
          if ((line.npc && *line.npc != npc) || deadNpcs.contains(npc) ||
              batch.npcs.size() == tandem::proto::kMaxNpcAnims)
            continue;
          auto anim = line.anim;
          anim.npc = npc;
          batch.npcs.push_back(anim);
          npcWeapons[npc] = anim.weapon;
          if (line.shots > 0)
            bursts[npc] = {line.shots, nowUs};
        }
        endpoint->SendNpcAnims(batch);
        Log("anims: line {} for {} NPC(s): high {}, upper {}, stance {}{}", animsNext + 1,
            batch.npcs.size(), line.anim.highLevel, line.anim.upperBody, line.anim.stance,
            line.shots ? std::format(", {} shots", line.shots) : "");
      }
      for (auto &[npc, burst] : bursts)
        if (burst.left > 0 && nowUs >= burst.nextUs && !deadNpcs.contains(npc)) {
          endpoint->SendNpcShot({.npc = npc, .effect = 16});
          --burst.left;
          burst.nextUs = nowUs + 200'000;
        }
    }
    if (now >= nextReport) {
      nextReport = now + 5s;
      if (!options.recordFile.empty())
        Log("recorded {} NPC messages", recording.Count());
      if (!options.savePathFile.empty())
        Log("saved {} states of the path", savedPath.Count());
      const auto stats = endpoint->HostLinkStats();
      Log("{} player(s), {} states, {} poses, {} shots, {} car states heard{}",
          endpoint->Players().size(), heard, heardPoses, heardShots, heardVehicles,
          stats ? std::format(", rtt {} ms, loss {:.1f}%", stats->rttMs, stats->packetLoss * 100)
                : "");
      if (shaper) {
        const auto link = shaper->Stats();
        Log("bad network: {} datagrams through, {} lost, {} held back", link.passed, link.lost,
            link.reordered);
      }
      {
        const auto traffic = endpoint->Traffic();
        const auto delta = traffic - lastTraffic;
        const auto nowUs = tandem::net::NowUs();
        const double seconds = static_cast<double>(nowUs - lastTrafficUs) / 1e6;
        lastTraffic = traffic;
        lastTrafficUs = nowUs;
        Log("traffic: {}", tandem::net::Summary(delta, seconds));
        if (!npcStatesHeard.empty() && seconds > 0) {
          std::uint32_t most = 0, all = 0;
          for (const auto &[npc, count] : npcStatesHeard) {
            most = std::max(most, count);
            all += count;
          }
          Log("npc states: {} NPCs, {:.0f} a second for the most heard, {:.1f} on average",
              npcStatesHeard.size(), most / seconds,
              all / seconds / static_cast<double>(npcStatesHeard.size()));
          npcStatesHeard.clear();
        }
        if (options.traffic)
          std::cout << tandem::net::Table(delta, seconds) << std::flush;
      }
      if (const auto &faults = endpoint->FaultStats(); faults.Active())
        Log("faults: {} message(s) lost, {} sent twice; {} resync(s) asked of us so far",
            faults.Dropped(), faults.Duplicated(), resyncsAsked);
    }
  }

  if (sessionOut.IsOpen()) {
    Log("session saved: {} messages, {:.1f} s", sessionOut.Count(),
        static_cast<double>(sessionOut.LastUs()) / 1e6);
    sessionOut.Close();
  }
  if (savedPlay.IsOpen()) {
    endpoint->SetTap({});
    Log("play saved: {} messages, {:.1f} s", savedPlay.Count(),
        static_cast<double>(savedPlay.LastUs()) / 1e6);
    savedPlay.Close();
  }
  {
    const auto whole = endpoint->Traffic();
    Log("traffic over the whole run:\n{}", tandem::net::Table(whole, 1.0));
  }
  referee.End();
  endpoint->Leave("tandem-peer stopped");
  Log("referee summary: {} violation(s), {} hint(s)", referee.Violations(), referee.Hints());
  if (!options.recordFile.empty()) {
    if (recording.Finish())
      Log("recording saved: {} NPC messages, {} spawns, in {}", recording.Count(),
          recording.Spawns(), options.recordFile);
    else
      Log("recording not saved: no NPC spawns ({} kept as it was)", options.recordFile);
  }
  return 0;
}
