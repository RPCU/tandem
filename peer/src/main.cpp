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
// grenade" at every other V (AreaHit).
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
// the time abilities it hears are logged.
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
// A bad network: --latency, --jitter, --loss and --reorder put a shaped link
// (tandem/shaping.hpp) between the mock player and the others, both ways.

#include "tandem/combat.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/interpolation.hpp"
#include "tandem/movement.hpp"
#include "tandem/play.hpp"
#include "tandem/pose.hpp"
#include "tandem/scene.hpp"
#include "tandem/shaping.hpp"

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
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using tandem::proto::PlayerState;
using tandem::proto::Vec3;

volatile std::sig_atomic_t g_stop = 0;

enum class Mode { Mirror, Ghost, Front, Drive, Course, Path, Metro, Watch, Idle };

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
  int driveSeconds{0}; // drive: get out after this long (0: never)
  // drive: the car's radio (ERadioStationList index, -1 off); unset: none sent
  std::optional<int> radioStation;
  int hitNpcsSeconds{0};  // join: hit one of the host's NPCs this often (0: never)
  int hitYouSeconds{0};   // host: one of its NPCs hits each guest's V this often (0: never)
  int shootYouSeconds{0}; // host: one of its NPCs fires at each guest's V this often
  std::string shotWeapon{"Items.Preset_Lexington_Default"}; // --shoot-you: its weapon
  // --hit-npcs: what each hit carries besides its damage (plain, nonlethal,
  // cap, poise, head, blade).
  std::string strike{"plain"};
  std::optional<int> difficulty; // host: the session's gameDifficulty
  int areaHitSeconds{0};         // our "grenade" hits every other V this often (0: never)
  // host: the world the game follows (time of day, weather name); unset: none sent
  std::optional<int> timeOfDayMinutes;
  std::string weather;
  std::string recordFile;   // join: save the host's NPC messages here
  std::string replayFile;   // host: send a recording's NPC messages again
  std::string animsFile;    // host: NPC animation states and shots to send (with --replay)
  int pauseAtSeconds{-1};   // open the pause menu this long into the session (-1: never)
  int pauseForSeconds{3};   // ...for this long
  std::string npcRecord;    // host: one NPC of this record in front of the guest
  int detectAtSeconds{-1};  // host: its NPCs detect the guests this long in (-1: never)
  int detectForSeconds{10}; // ...for this long
  int camoAtSeconds{-1};    // optical camo on this long into the session, for 5 s
  int shootSeconds{0};      // join: a gunshot (shot + noise) this often (0: never)
  // Quickhacks: join: upload --hack-action on a host NPC every N s, then land
  // --hack-effect; host: one of our NPCs hacks the guest (--npc-hack) every
  // N s; either: hack the car another player drives every N s.
  int hackNpcsSeconds{0};
  std::string hackAction{"QuickHack.BlindHack"};
  std::string hackEffect{"BaseStatusEffect.Blind"};
  int hackPlayerSeconds{0};
  std::string npcHack{"AIQuickHack.HackRevealPosition"};
  int hackCarSeconds{0};
  std::string storyFile;         // host: a scripted story (tools/scenarios/story-host.story)
  int answerSeconds{-1};         // join: answer the host's ringing call after this long (-1: never)
  std::uint32_t replyEntry{};    // join: reply to the text with this choice entry hash
  int suggestIndex{-1};          // join: suggest this option of every hub (-1: never)
  int sandevistanAtSeconds{-1};  // switch a Sandevistan on this long into the session (-1: never)
  int sandevistanForSeconds{5};  // ...for this long
  float sandevistanScale{0.25f}; // ...asking for this world speed
  int grabNpcsSeconds{0};        // join: grab and take down one NPC this often (0: never)
  int carryNpcsSeconds{0};       // join: pick up, carry and throw a body this often
  int holdNpcsSeconds{0};        // host: our V grabs and takes down a replayed NPC this often
  std::string playFile;          // --play: a real V's play (tandem/play.hpp)
  bool playLoop{};               // ...again from the start at its end
  Vec3 playOffset{};             // ...moved by this
  std::string savePlayFile;      // record our own play there, as the plugin does
  std::string pathFile;          // path: the path to replay
  std::string savePathFile;      // save the states heard (your V's path) here
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
  enum class Answer { Yes, No, Ignore } regroupAnswer{Answer::Yes};
  tandem::net::LinkShape link; // a bad network, both ways (none: direct)
  // Loot: drop one of this record in front of the other V (empty: never);
  // pick up the others' world items this long after they appear (-1: never);
  // host of a replay: dead NPCs' bodies go after this long (-1: never).
  std::string dropItem;
  std::string lootQuest; // "our V" loots one of this record (a quest item), once
  int takeItemsSeconds{-1};
  int removeCorpsesSeconds{-1};
  bool check{}; // parse the options, then exit
  // Down and out: our V goes down (revivable) or out N s after another player
  // is there (-1: never); a downed other V is revived after N s (-1: never).
  int goDownSeconds{-1};
  int goOutSeconds{-1};
  int reviveSeconds{-1};
  // Faults on what we send (docs/architecture/consistency.md, Consistency).
  float drop{}, dup{};
  int vanishAtSeconds{-1}; // end without a goodbye this long into the session (-1: never)
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
         "       tandem-peer play-info FILE   what a gameplay recording holds, where it starts\n\n"
         "  --port N           UDP port (default 27450)\n"
         "  --relay HOST[:PORT] tandem-relay (default port 27460): host, also reachable\n"
         "                     through it (its join code is logged); join, used when\n"
         "                     ADDRESS does not answer, or with --code only\n"
         "  --relay-token HEX  host: one of the relay's host tokens (tandem-relay token)\n"
         "  --code CODE        join: the host's join code (needs --relay)\n"
         "  --mode M           mirror | ghost | front | drive | course | path | metro |\n"
         "                     watch | idle (default mirror)\n"
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
         "  --radio STATION    drive: the car's radio plays STATION (0-13 or its name,\n"
         "                     radio_station_09_downtempo...; off): sent when seated and\n"
         "                     again when a passenger gets in\n"
         "  --hit-npcs N       join: hit one of the host's NPCs (35 health) every N seconds\n"
         "  --shoot-you N      host: an NPC (a replayed one) fires at each guest's V every N\n"
         "                     seconds: the guest's game fires it and decides the hit\n"
         "  --shot-weapon REC  --shoot-you: the NPC's weapon (default\n"
         "                     Items.Preset_Lexington_Default)\n"
         "  --strike KIND      --hit-npcs: what each hit carries: plain (35 health), nonlethal\n"
         "                     (a blunt hit: knocks out), cap (leave 1 HP), poise (10 health,\n"
         "                     40 poise), head (a headshot), blade (a katana on the left arm)\n"
         "  --hit-you N        host: an NPC (a replayed one if any) hits each guest's V for\n"
         "                     10 physical every N seconds\n"
         "  --difficulty D     host: the session's difficulty: easy | hard | veryhard | story\n"
         "                     (or 0-3); guests play at it\n"
         "  --area-hit N       every N seconds our \"grenade\" hits every other player's V for\n"
         "                     20 physical (an explosion, applied by their game)\n"
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
         "  --shoot N          join: fire a gunshot (shot and noise) every N seconds\n"
         "  --hack-npcs N      join: upload a quickhack on one of the host's NPCs every N\n"
         "                     seconds (1.5 s upload), then land its effect\n"
         "  --hack-action REC  --hack-npcs: the quickhack (default QuickHack.BlindHack)\n"
         "  --hack-effect REC  --hack-npcs: the effect it lands (default BaseStatusEffect.Blind)\n"
         "  --hack-player N    host: one of our (replayed) NPCs hacks the guest's V every N\n"
         "                     seconds\n"
         "  --npc-hack REC     --hack-player: the NPC's hack (default\n"
         "                     AIQuickHack.HackRevealPosition, a trace)\n"
         "  --hack-car N       hack the car another player drives (brakes, 3 s) every N\n"
         "                     seconds\n"
         "  --story FILE       host: play a scripted story (journal, calls, lines, hubs,\n"
         "                     rewards; format in tools/scenarios/story-host.story)\n"
         "  --answer N         join: answer the host's ringing call after N seconds\n"
         "  --reply HASH       join: reply to a text with this choice entry (journal hash)\n"
         "  --suggest N        join: suggest option N of every choice hub the host shows\n"
         "  --sandevistan-at N switch a Sandevistan on N seconds after another player is in\n"
         "                     the session: the host slows the NPCs around this V\n"
         "  --sandevistan-for N  --sandevistan-at: switch it off after N seconds (default 5)\n"
         "  --sandevistan-scale S  --sandevistan-at: the world speed it asks for (default 0.25)\n"
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
         "  --latency MS       a bad network: MS added each way\n"
         "  --jitter MS        up to MS more at random, each way (the order kept)\n"
         "  --loss PERCENT     PERCENT of the datagrams lost, each way\n"
         "  --reorder PERCENT  PERCENT of the datagrams held back behind later ones\n"
         "  --loot-quest REC   5 s after another player is there, \"our V\" loots one REC (a\n"
         "                     quest item): every other player gets it too\n"
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
         "                     (Transit Along), carried with its car, out beside it at the end\n";
  std::exit(error.empty() ? 0 : 2);
}

template <class T> T Number(std::string_view text) {
  T value{};
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    Usage(std::format("not a number: {}", text));
  return value;
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
    if (flag == "--ride-along") {
      options.rideAlong = true;
      continue;
    }
    if (flag == "--play-loop") {
      options.playLoop = true;
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
    } else if (flag == "--hack-npcs")
      options.hackNpcsSeconds = Number<int>(value);
    else if (flag == "--hack-action")
      options.hackAction = std::string(value);
    else if (flag == "--hack-effect")
      options.hackEffect = std::string(value);
    else if (flag == "--hack-player")
      options.hackPlayerSeconds = Number<int>(value);
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
    else if (flag == "--shoot-you")
      options.shootYouSeconds = Number<int>(value);
    else if (flag == "--shot-weapon")
      options.shotWeapon = std::string(value);
    else if (flag == "--strike") {
      constexpr std::string_view kinds[] = {"plain", "nonlethal", "cap", "poise", "head", "blade"};
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
    else if (flag == "--npc")
      options.npcRecord = std::string(value);
    else if (flag == "--detect-at")
      options.detectAtSeconds = Number<int>(value);
    else if (flag == "--detect-for")
      options.detectForSeconds = Number<int>(value);
    else if (flag == "--camo")
      options.camoAtSeconds = Number<int>(value);
    else if (flag == "--shoot")
      options.shootSeconds = std::max(1, Number<int>(value));
    else if (flag == "--drive-seconds")
      options.driveSeconds = Number<int>(value);
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
    else if (flag == "--regroup-answer") {
      if (value == "yes")
        options.regroupAnswer = Options::Answer::Yes;
      else if (value == "no")
        options.regroupAnswer = Options::Answer::No;
      else if (value == "ignore")
        options.regroupAnswer = Options::Answer::Ignore;
      else
        Usage(std::format("--regroup-answer: yes, no or ignore, not {}", value));
    } else if (flag == "--latency")
      options.link.latencyMs = Number<std::uint32_t>(value);
    else if (flag == "--jitter")
      options.link.jitterMs = Number<std::uint32_t>(value);
    else if (flag == "--loss")
      options.link.loss = Percent(flag, value);
    else if (flag == "--reorder")
      options.link.reorder = Percent(flag, value);
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
    } else if (flag == "--anims") {
      options.animsFile = value;
    } else if (flag == "--path") {
      options.pathFile = value;
    } else if (flag == "--save-path") {
      options.savePathFile = value;
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
//   tier N                       the host's scene tier (1..5)
//   reward RECORD [QUESTHASH]    a quest reward for every guest
//   effect RECORD on|off         a story status effect on the host's V
//                                (a relic glitch: BaseStatusEffect.JohnnySicknessLow)
//   replacer on|off              the host plays as Johnny (StoryState flag)
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
//   skip on|off                  the host fast-forwards the scene
//   voice ID SPEAKER [SECONDS] [call]  a voiced line begins (ID: its string
//                                id; SPEAKER: host, an NPC id or own)
//   cut ID                       it is cut before its end
//   world PLACE [moved] [settled] the host's V in another world (Elsewhere):
//                                cyberspace, braindance, johnny, other, or
//                                city to come back; moved: the scene took it
//                                away from its body, which stays where the
//                                mock player was (settled: sitting, lying)
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
    if (step.verb == "tier" && !a.empty())
      return proto::StoryState{.tier = static_cast<std::uint8_t>(std::stoi(a[0]))};
    if (step.verb == "private" && !a.empty())
      return proto::StoryState{.tier = static_cast<std::uint8_t>(a[0] == "on" ? 3 : 1),
                               .flags = a[0] == "on" ? proto::kStoryPrivate : std::uint8_t{0}};
    if (step.verb == "replacer" && !a.empty())
      return proto::StoryState{.tier = 1,
                               .flags = a[0] == "on" ? proto::kStoryReplacer : std::uint8_t{0}};
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
    }
    return std::nullopt;
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
  // A car going round a 12 m circle at 8 m/s around where your V was first
  // heard (fixed, so you can watch it pass): the driver's state, and the car's.
  std::pair<std::optional<PlayerState>, std::optional<tandem::proto::VehicleState>>
  Drive(std::uint64_t nowUs) {
    if (m_heard.empty())
      return {};
    if (!m_center)
      m_center = m_heard.back().second.position;
    constexpr float kRadius = 12.f, kSpeed = 8.f;
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

// The body keys of a real V (face, body, arms: the game's appearance keys,
// a resource path hash and a name hash each), one file per body type:
// "RESOURCE DEFINITION" in hex, one key a line. Saved from every look heard
// with keys (a game's own V: 15 of them), so a random look wears a real
// body; a file may also be written by hand.
std::filesystem::path LookKeysDir(const std::string &option) {
  if (!option.empty())
    return option;
  if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache && *cache)
    return std::filesystem::path(cache) / "tandem" / "looks";
  if (const char *home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache" / "tandem" / "looks";
  return "looks";
}

std::filesystem::path LookKeysFile(const std::filesystem::path &dir, tandem::proto::Body body) {
  return dir / (body == tandem::proto::Body::Male ? "male.keys" : "female.keys");
}

std::vector<tandem::proto::AppearanceKey> LoadLookKeys(const std::filesystem::path &file) {
  std::vector<tandem::proto::AppearanceKey> keys;
  std::ifstream in(file);
  for (std::string resource, definition; in >> resource >> definition;) {
    try {
      keys.push_back({std::stoull(resource, nullptr, 16), std::stoull(definition, nullptr, 16)});
    } catch (const std::exception &) {
      return {};
    }
  }
  if (keys.size() > tandem::proto::kMaxAppearanceKeys)
    keys.resize(tandem::proto::kMaxAppearanceKeys);
  return keys;
}

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

// A look of our own: a real V body of ours when its keys are known (else
// none: the game puts a stand-in body on the puppet) and one random piece
// per slot from clothing records the game's own scripts name (their
// crafting specs end in _Crafting; the item is the same name without it).
tandem::proto::Appearance RandomLook(tandem::proto::Body body, unsigned seed,
                                     std::vector<tandem::proto::AppearanceKey> keys) {
  static const std::vector<std::vector<std::string_view>> slots = {
      {"Items.FormalShirt_01_rich_06", "Items.FormalShirt_02_basic_03",
       "Items.LooseShirt_01_rich_02", "Items.Fixer_01_Set_TShirt"},
      {"Items.Jacket_19_basic_04", "Items.Jacket_20_basic_01", "Items.Jacket_01_rich_01",
       "Items.Jacket_17_rich_06", "Items.Coat_01_basic_03", "Items.Coat_04_rich_02",
       "Items.FormalJacket_02_basic_02", "Items.FormalJacket_04_basic_02"},
      {"Items.Pants_02_basic_01", "Items.Pants_04_basic_01", "Items.Pants_08_rich_01",
       "Items.FormalPants_02_rich_01", "Items.FormalPants_03_rich_02",
       "Items.FormalSkirt_01_rich_01", "Items.FormalSkirt_02_rich_03"},
      {"Items.Boots_03_basic_02", "Items.Boots_07_basic_01", "Items.Boots_09_rich_01",
       "Items.CasualShoes_01_basic_01", "Items.CasualShoes_02_basic_01",
       "Items.CasualShoes_05_basic_03", "Items.FormalShoes_01_rich_02",
       "Items.FormalShoes_03_rich_01"},
      // Head and face: sometimes nothing.
      {"", "", "Items.Cap_02_basic_01", "Items.Cap_03_rich_02", "Items.Hat_02_rich_02",
       "Items.Hat_03_basic_02"},
      {"", "", "Items.Glasses_01_basic_03", "Items.Glasses_03_basic_06"},
  };
  std::mt19937 random(seed);
  tandem::proto::Appearance look{.body = body, .look = std::move(keys)};
  std::string picked;
  for (const auto &slot : slots) {
    const auto name = slot[random() % slot.size()];
    if (name.empty())
      continue;
    look.items.push_back(TweakDBID(name));
    picked += std::format(" {}", name.substr(6));
  }
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
  }
  return strike;
}

std::string DescribeCombat(const tandem::proto::Message &message) {
  namespace proto = tandem::proto;
  if (const auto *s = std::get_if<proto::NpcStrike>(&message))
    return std::format("npc {} struck by player {}: {:.0f} health, {:.0f} poise, flags {:#x}, "
                       "zone {}, part {}",
                       s->npc, s->player, s->damage, s->poise, s->flags, s->zone, s->part);
  if (const auto *w = std::get_if<proto::NpcWound>(&message))
    return std::format("npc {} loses part {} (wound {}, player {}'s hit)", w->npc, w->part,
                       w->wound, w->player);
  if (const auto *p = std::get_if<proto::NpcPoise>(&message))
    return std::format("npc {} poise {:.0f}%", p->npc, p->poise * 100.f);
  if (const auto *d = std::get_if<proto::Difficulty>(&message))
    return std::format("the session plays at difficulty {}", d->level);
  if (const auto *a = std::get_if<proto::AreaHit>(&message))
    return std::format("player {}'s area effect hits us: {:.0f} physical, {:.0f} thermal (type {})",
                       a->from, a->physical, a->thermal, a->attackType);
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

int main(int argc, char **argv) {
  if (argc >= 3 && std::string_view(argv[1]) == "play-info")
    return PlayInfo(argv[2]);
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
  if (options.drop > 0.f || options.dup > 0.f) {
    endpoint->SetFaults({.drop = options.drop, .dup = options.dup, .seed = std::random_device{}()});
    Log("faults: {:.0f}% of what we send is lost, {:.0f}% of our state sent twice",
        options.drop * 100, options.dup * 100);
  }
  // --look random: the same clothes all session; its body keys from the
  // cache, or as soon as a look of its body is heard.
  const auto lookKeysDir = LookKeysDir(options.lookKeys);
  const auto lookSeed = options.lookSeed.value_or(std::random_device{}());
  auto ownKeys = LoadLookKeys(LookKeysFile(lookKeysDir, options.body));
  if (!options.echoLook && !playback && !sessionPlay)
    endpoint->SetLocalAppearance(RandomLook(options.body, lookSeed, ownKeys));
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
  auto nextAreaHit =
      std::chrono::steady_clock::now() + std::chrono::seconds(options.areaHitSeconds);
  std::set<tandem::proto::NpcId> deadNpcs;
  std::map<tandem::proto::NpcId, Vec3> npcAt; // join: where the host's NPCs are
  // join: when each was last in the host's states (dead and far NPCs are
  // not), and the NPCs whose grab the host refused (not asked again).
  std::map<tandem::proto::NpcId, std::chrono::steady_clock::time_point> npcSeen;
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
  auto nextShot = std::chrono::steady_clock::now();
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
  std::uint64_t actorPoses = 0;
  if (!options.storyFile.empty()) {
    story = LoadStory(options.storyFile, error);
    if (!error.empty()) {
      std::cerr << "tandem-peer: " << error << '\n';
      return 1;
    }
    Log("story: {} steps from {}", story.size(), options.storyFile);
  }
  // join: the call to answer, and when.
  std::optional<std::pair<std::uint64_t, std::chrono::steady_clock::time_point>> toAnswer;
  bool replied = false;
  std::optional<std::chrono::steady_clock::time_point> joinedAt;

  Puppeteer puppeteer(options);
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
  bool seated = false, parked = false;
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
  std::optional<Vec3> lastSent;
  const auto distance = [](const Vec3 &a, const Vec3 &b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
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

  while (!g_stop) {
    for (const auto &event : endpoint->Update(5ms)) {
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
              // A real V's body keys: kept for random looks of that body.
              if (!e.appearance.look.empty() && e.appearance.body != tandem::proto::Body::Unknown) {
                SaveLookKeys(LookKeysFile(lookKeysDir, e.appearance.body), e.appearance.look);
                if (!options.echoLook && !playback && !sessionPlay && ownKeys.empty() &&
                    e.appearance.body == options.body) {
                  ownKeys = e.appearance.look;
                  endpoint->SetLocalAppearance(RandomLook(options.body, lookSeed, ownKeys));
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
              if (options.mode == Mode::Mirror || options.mode == Mode::Front)
                endpoint->SetLocalHeld(e.held);
            } else if constexpr (std::is_same_v<E, tandem::events::HostNpcSpawn>) {
              liveNpcs.insert(e.spawn.npc);
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
              Log("npc {} fires at us (weapon {:#x}, speed {:.2f})", e.shot.npc, e.shot.weapon,
                  e.shot.factor);
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
              Log("player {}: weapon event {}", e.shot.player, e.shot.effect);
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
            } else if constexpr (std::is_same_v<E, tandem::events::PauseChanged>) {
              Log("player {} {} (pause menu)", e.pause.player,
                  e.pause.paused ? "paused" : "resumed");
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
              Log("{}", Describe(e.message));
              if (!options.recordFile.empty())
                recording.Add(e.message);
              namespace proto = tandem::proto;
              if (const auto *call = std::get_if<proto::PhoneCall>(&e.message);
                  call && options.answerSeconds >= 0 && call->phase == 1 &&
                  call->answeredBy == proto::kNoPlayer)
                toAnswer = {call->contact, std::chrono::steady_clock::now() +
                                               std::chrono::seconds(options.answerSeconds)};
              if (const auto *hub = std::get_if<proto::ChoiceHub>(&e.message);
                  hub && options.suggestIndex >= 0 && !hub->options.empty() && !options.host) {
                endpoint->SendStory(
                    proto::ChoiceCursor{.hub = hub->hub,
                                        .index = static_cast<std::int8_t>(options.suggestIndex),
                                        .kind = proto::kCursorSuggest});
                Log("suggest option {} of hub {}", options.suggestIndex, hub->hub);
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
            } else if constexpr (std::is_same_v<E, tandem::events::Ended>) {
              Log("session ended: {}", e.reason);
              g_stop = 1;
            }
          },
          event);
    }
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
      if (options.mode == Mode::Drive && !parked) {
        const auto nowUs = tandem::net::NowUs();
        const bool driving = options.seat == "seat_front_left";
        const auto car = puppeteer.Drive(nowUs).second;
        if (!seated && endpoint->IsJoined() && car) {
          endpoint->SetLocalSeat(
              {.vehicle = TweakDBID(options.vehicle), .seat = CName(options.seat)});
          Log("{} {} ({:#x}) in {}", driving ? "driving" : "riding", options.vehicle,
              TweakDBID(options.vehicle), options.seat);
          seated = true;
          seatedAtUs = nowUs;
          if (options.radioStation)
            sendRadio("at the wheel");
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
        if (seated && options.driveSeconds > 0 &&
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
    if (options.host && (options.timeOfDayMinutes || !options.weather.empty()) &&
        now >= nextWorld) {
      nextWorld = now + 5s;
      // Day 1 at the given time, running about 8 game seconds a second.
      const auto elapsed = static_cast<int>((tandem::net::NowUs() - startUs) / 1'000'000);
      tandem::proto::World world;
      world.gameTimeSeconds = 86400 + options.timeOfDayMinutes.value_or(12 * 60) * 60 + elapsed * 8;
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
    // --offer-regroup: ask the others to come.
    if (!offeredRegroup && together(options.offerRegroupSeconds)) {
      offeredRegroup = true;
      const auto at = lastSent.value_or(Vec3{});
      endpoint->SendRegroup(
          {.offer = ++offers, .reason = tandem::proto::RegroupReason::Asked, .position = at});
      Log("asked the others to come to ({:.1f}, {:.1f}, {:.1f}) (offer {})", at.x, at.y, at.z,
          offers);
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
    // --sandevistan-at: on, then off.
    if (options.sandevistanAtSeconds >= 0 && sandevistanStep != PauseStep::Done) {
      if (!togetherSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
        togetherSince = now;
      const auto at = togetherSince
                          ? *togetherSince + std::chrono::seconds(options.sandevistanAtSeconds)
                          : std::chrono::steady_clock::time_point::max();
      if (sandevistanStep == PauseStep::Waiting && now >= at) {
        endpoint->SetLocalTimeEffect(
            {.abilities = tandem::proto::kTimeSandevistan, .scale = options.sandevistanScale});
        sandevistanStep = PauseStep::Paused;
        Log("sandevistan on: world speed {:.2f} around us for {} s", options.sandevistanScale,
            options.sandevistanForSeconds);
      } else if (sandevistanStep == PauseStep::Paused &&
                 now >= at + std::chrono::seconds(options.sandevistanForSeconds)) {
        endpoint->SetLocalTimeEffect({});
        sandevistanStep = PauseStep::Done;
        Log("sandevistan off");
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
    // --difficulty: the session's, kept by the endpoint for whoever joins.
    if (options.difficulty && options.host && !difficultySent && endpoint->IsJoined()) {
      difficultySent = endpoint->SendCombat(
          tandem::proto::Difficulty{.level = static_cast<std::uint8_t>(*options.difficulty)});
      Log("the session plays at difficulty {}", *options.difficulty);
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
          break;
        }
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
        if (id != endpoint->You() && endpoint->SendPlayerHit({.player = id,
                                                              .npc = shooter,
                                                              .physical = 10.f,
                                                              .attackType = 10,
                                                              .direction = {0.f, 1.f, 0.f}}))
          Log("npc {} hit player {} for 10 physical", shooter, id);
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
      for (const auto &[id, info] : endpoint->Players())
        if (shooter && id != endpoint->You() &&
            endpoint->SendNpcShotAt({.player = id,
                                     .npc = shooter,
                                     .weapon = npcWeapons[shooter],
                                     .tbh = 1.f,
                                     .trigger = 4, // SemiAuto
                                     .factor = 1.f}))
          Log("npc {} fires at player {}", shooter, id);
    }
    // Seconds since another player was first in the session with us.
    if (!sharedSince && endpoint->IsJoined() && endpoint->Players().size() > 1)
      sharedSince = now;
    const double shared =
        sharedSince ? std::chrono::duration<double>(now - *sharedSince).count() : -1.0;
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
                                .yaw = ownNpc->yaw});
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
        endpoint->SetLocalStealth({.flags = tandem::proto::kStealthCamo, .visibility = 0.2f});
        camoStep = 1;
        Log("optical camo on");
      } else if (camoStep == 1 && shared >= options.camoAtSeconds + 5) {
        endpoint->SetLocalStealth({.flags = 0, .visibility = 1.f});
        camoStep = 2;
        Log("optical camo off");
      }
    }
    // --shoot: a gunshot, seen (Shot) and heard (Noise, gamedataStimType.Gunshot 28).
    if (options.shootSeconds && !options.host && endpoint->IsJoined() && now >= nextShot) {
      nextShot = now + std::chrono::seconds(options.shootSeconds);
      endpoint->SendLocalShot({.hand = tandem::proto::Hand::Right, .effect = 16});
      endpoint->SendNoise({.stim = 28, .radius = 25.f});
      Log("gunshot");
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
  endpoint->Leave("tandem-peer stopped");
  if (!options.recordFile.empty()) {
    if (recording.Finish())
      Log("recording saved: {} NPC messages, {} spawns, in {}", recording.Count(),
          recording.Spawns(), options.recordFile);
    else
      Log("recording not saved: no NPC spawns ({} kept as it was)", options.recordFile);
  }
  return 0;
}
