#pragma once

// tandem.ini, next to tandem.dll (or where TANDEM_CONFIG says; TANDEM_PORT
// overrides its port: tandem/instance.hpp, for several games on a machine):
//
//   role = join          ; off (solo, the default) | host | join; the co-op menu
//                        ; can pick another. Solo: tandem does nothing to the game.
//   recorder = off       ; on: record what this game plays, solo or in a session
//                        ; (tandem/recorder.hpp), in recordings/auto/
//   recorder_max_mb = 2048        ; the oldest segments go beyond this
//   recorder_segment_minutes = 10
//   recorder_pose = on   ; off: no body poses in the recordings
//   address = 127.0.0.1  ; join: the host's address
//   port = 27450
//   name = V             ; the name the others see: 1 to 16 characters, no ; or #
//                        ; (tandem/names.hpp); the co-op menu asks for one while
//                        ; it is V
//   relay = relay.example.org:27460  ; tandem-relay: NAT fallback, join codes
//   relay_token = <64 hex>           ; host: one of the relay's host tokens
//   join = K7Q-2M9       ; join: the host's join code (through the relay;
//                        ; `address`, when written, is tried first)
//   puppet_female = Character.TandemPuppetWa
//   puppet_male = Character.TandemPuppetMa
//   probe = off          ; on: spawn test bodies and log their animation setup
//   trace = off          ; on: log every distinct anim feature the engine applies
//   mirror = off         ; on (experiment): puppets get the local V's anim features
//   pose = on            ; off: no body pose streaming (the hooks stay out)
//   story = on           ; off: no story sync (journal, calls, lines, hubs, rewards)
//   quest_freeze = off   ; on: a guest's own story phases wait while it is in a
//                        ; session (engine/quests.cpp); off: the hook stays out
//   scene_privacy = on   ; off: every scene is watched (no private scenes)
//   scene_replay = on    ; on: a host's story scenes play for its guests (their
//                        ; cast, voices and, with pose = on, the actors' bodies:
//                        ; docs/architecture/scene-replay.md); poses: only the
//                        ; actors' bodies are left out
//   scene_camera = on    ; with scene_replay: off: a guest keeps its own eyes in
//                        ; the host's cutscenes (no watch camera)
//   scene_rigs = on      ; with scene_replay = on: on: a speaking actor's face and
//                        ; the bodies of drones and robots are streamed too
//   spectate = on        ; guest: off: in the host's scripted moments (scenes,
//                        ; quest rides, set pieces) you keep playing your own V
//                        ; instead of watching through the host's camera
//   scene_kbps = 22      ; with scene_replay, host: what a scene's streams (bodies,
//                        ; the camera) may take on a guest's link, 8 to 2000
//   scene_voice_gender = on ; with scene_replay, guest: a V's line plays the
//                        ; recording of that V's body (on: the line's gender
//                        ; field as locVoiceTagGender, male 1, female 2;
//                        ; swapped: female 1, male 2; off: the game picks)
//   private_actors = judy, panam, river, kerry, meredith  ; romance partners
//   private_scenes = , public_scenes =  ; scene resource paths, comma-separated
//   quest_freeze_gigs =  ; gig phase resources: they wait only while grouped
//   quest_freeze_allow = ; more open-world phase resources that keep running
//                        ; on a frozen guest (resource paths, comma-separated)
//   quest_interact = on  ; off: a guest's use of the host's objects stays in its
//                        ; own world (docs/architecture/story.md, "Quest
//                        ; interactions")
//   quest_interact_hostonly = , quest_interact_allow =  ; TweakDB records of
//                        ; objects whose choices the host must do, or may always
//                        ; do for a guest: a safety valve, keep them empty
//   log = info           ; warn | info | debug (log.hpp): info is quiet (the
//                        ; session's life, a minute's traffic and frame cost);
//                        ; debug adds every puppet, proxy, hit and shot
//   time_host = full     ; host's Sandevistan (scripts/tandem/Time.reds): full keeps
//                        ; the single-player slow motion whole on its own game (the
//                        ; NPCs it shares follow the bubbles' rules); bubble puts its
//                        ; world back to normal speed and slows only the NPCs
//   compat = on          ; off (developers): start sessions whatever the startup
//                        ; check found (tandem/compat.hpp)
//   friendly_fire = off  ; host: on, the Vs' hits (shots, blades, explosions) hurt
//                        ; each other (proto::Rules, every game follows the host's)
//   enemy_scaling = on   ; host: hostile NPCs are tougher for each extra player near
//                        ; them (health, poise, armour; tandem/scaling.hpp, Rules)
//   enemy_scaling_health = 0.6 ; host, advanced (ini only): the health added per
//                        ; extra player, 0..2.5 (0.6: x1.6 for two, x2.2 for three)
//   notices = all        ; important: no news (rides, calls, pauses...); minimal: only
//                        ; what asks for an answer or went wrong
//   markers = on         ; off: no marker for the other players (HUD, minimap, map)
//   nameplates = near    ; the name over the other players: near (to 12 m, gone at
//                        ; 20 m), always (any distance in view, smaller and fainter
//                        ; far away), off (on: near)
//   party_hud = on       ; off: no party list (linked partners' health) on the HUD
//   race_car = own       ; a partner's street race: own, your garage's active car
//                        ; on the grid; copy, a copy of the starter's (also when
//                        ; you have no car)
//   overlap = density    ; our own crowd and traffic where a higher-ranked linked
//                        ; player's region covers ours: density (thinned by how much
//                        ; is covered), area (a crowd null area over that region:
//                        ; untested), off (only off deep inside it) (ini only)
//   overlap_despawn = off ; on: our own crowd cars left inside such a region are
//                        ; asked to despawn (a probe, untested) (ini only)
//   death_ledger = on    ; off: placed NPCs another player killed in the session
//                        ; stay alive in our game (ini only)
//   death_bodies = on    ; off: no body where a placed NPC another player
//                        ; killed in the last ten minutes fell (ini only)
// The settings page (scripts/tandem/Coop.reds) changes some of them while the
// game runs and saves them here (TandemNative.SetSetting, tandem/settings.hpp).
//   leave = stay         ; a guest out of a session, once its own world is back
//                        ; (docs/architecture/progression.md): stay goes on in
//                        ; place; save also makes one quick save; reload then
//                        ; loads that save (only when you chose to leave)
//   trade = off          ; on: trade items and eddies with the player beside you
//                        ; (docs/architecture/trade.md); off: requests are refused
//   link = auto          ; who sees your scanner tags and Ping reveals
//                        ; (docs/architecture/comms.md): auto, every partner
//                        ; (until you break it); ask, the partners you link with
//                        ; (a request they accept); off, nobody
//   pings = on           ; off: you neither send nor see pings
//   last_address =       ; the co-op menu's Join by address: the last one joined
//                        ; ("host:port", an empty answer joins it again)
//   relay = a, b, c      ; several relays: a host registers on the nearest (its
//                        ; probes' round trip), a guest asks each for the code
//   code = new           ; keep: the host's join code is the relay's kept one for
//                        ; its token, the same every session (code_salt picks
//                        ; which; the co-op menu's New code moves it on)
//   direct = auto        ; through a relay: try a direct path to the other side
//                        ; (hole punching, docs/architecture/relay.md), the relay
//                        ; kept as the fallback; off: always through the relay

#include "tandem/presence.hpp"
#include "tandem/progress.hpp"
#include "tandem/relay.hpp"
#include "tandem/scaling.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::plugin {
struct Config {
  enum class Role { Off, Host, Join };

  Role role{Role::Off};
  std::string address{"127.0.0.1"};
  // `address` was in tandem.ini: a guest with a join code tries it first.
  bool addressSet{false};
  std::uint16_t port{27450};
  // tandem-relay ("" none), a host token for it, and (join) the host's code.
  std::string relay;
  std::uint16_t relayPort{net::kDefaultRelayPort};
  net::RelayToken relayToken{};
  std::string joinCode;
  // More relays (`relay = a, b, c`): "name[:port]" each, after the first.
  std::vector<std::string> relayMore;
  // Through the relay: a direct path set up by its introductions (`direct`).
  bool direct{true};
  // Host: the relay's kept code for our token (`code = keep`), and its salt
  // (the "new code" action moves it on and saves it).
  bool keepCode{false};
  std::uint32_t codeSalt{0};
  std::string name{"V"};
  std::string puppetFemale{"Character.TandemPuppetWa"};
  std::string puppetMale{"Character.TandemPuppetMa"};
  bool probe{false};
  bool trace{false};
  bool mirror{false};
  bool pose{true};
  bool story{true};
  // The recorder (docs/ARCHITECTURE.md, "Recorder"): independent of the role.
  bool recorder{false};
  std::uint64_t recorderMaxMb{2048};
  std::uint32_t recorderSegmentMinutes{10};
  bool recorderPose{true};
  bool questFreeze{false};
  std::vector<std::string> questFreezeAllow;
  std::vector<std::string> questFreezeGigs; // gig phase resources (ours while apart)
  // Quest interactions (Interact.reds): a guest's use of the host's objects
  // goes to the host's game. The override lists (TweakDB record names) are
  // empty by default and should stay so: the host's game judges each choice.
  bool questInteract{true};
  std::vector<std::string> questInteractHostOnly, questInteractAllow;
  // Scene privacy (engine/scenes.cpp): private scenes are not watched.
  bool scenePrivacy{true};
  bool sceneReplay{true};
  bool scenePoses{true}; // with sceneReplay
  // With sceneReplay: a guest in the host's moment sees a cutscene through
  // the host's camera; what the scene's streams may take on a link (0: the
  // default, scene::kSceneBytesPerSecond).
  bool sceneCamera{true};
  // A guest in the host's scripted moment watches it through the host's
  // camera, its own V parked (docs/architecture/story.md, "Spectating").
  bool spectate{true};
  // With sceneReplay and scenePoses: speakers' faces and the bodies of
  // actors on other rigs than the humanoid one (engine/posestream.cpp,
  // "Other rigs"): never run in game, so off unless asked for.
  bool sceneRigs{true};
  std::uint32_t sceneKbps{0};
  // A V's line on a guest: 0 the game picks, 1 locVoiceTagGender, 2 swapped.
  std::uint8_t sceneVoiceGender{1};
  std::vector<std::string> privateActors{"judy", "panam", "river", "kerry", "meredith"};
  std::vector<std::string> privateScenes, publicScenes;
  // How the host's own time dilations are played (Time.reds): its world in
  // bubbles only (false), or the whole single-player slow motion with what
  // is shared exempted (true).
  bool timeHostFull{true};
  // The startup check (game version, needed mods) keeps co-op off on a problem.
  bool compat{true};
  // The settings page's own (applied by the scripts as they change).
  // The session's rule when we host (proto::Rules; the settings page edits it):
  // the Vs hurt each other.
  bool friendlyFire{false};
  // Enemy scaling (scripts/tandem/Scaling.reds; the settings page edits the
  // first): on, and the health added per extra player near an NPC.
  bool enemyScaling{true};
  float enemyScalingHealth{scaling::kHealthPerPlayer};
  std::string notices{"all"};
  // A partner's street race (scripts/tandem/Races.reds): "own", our garage's
  // active car on the grid; "copy", a copy of the starter's.
  std::string raceCar{"own"};
  bool markers{true};
  presence::Nameplates nameplates{presence::Nameplates::Near};
  bool partyHud{true};
  // The death ledger (regions.md §14): our own copies of the placed NPCs the
  // session says died are kept out of our world, and a body lies where a
  // recent one fell.
  bool deathLedger{true};
  bool deathBodies{true};
  // The overlap (regions.md §6): how our game keeps its own crowd and traffic
  // out of a higher-ranked linked player's region. "density": the crowd's
  // density follows how much of our region is covered; "area": a crowd null
  // area over each cover (untested on 2.50); "off": as before (density 0
  // only deep inside). And whether our own crowd cars left inside a cover
  // are asked to despawn (a probe, untested).
  std::string overlap{"density"};
  bool overlapDespawn{false};
  // Where it was read from (the settings page writes there).
  std::filesystem::path file;
  // What a guest's game does once it is out of a session (Saves.reds).
  progress::LeaveMode leave{progress::LeaveMode::Stay};
  // Trade between players (Trade.reds): off until checked in game.
  bool trade{false};
  // A vehicle a host's quest gave while we took part (Garage.reds): credited
  // in our ledger (false), or unlocked in our own garage too (true).
  bool vehicleRewardsKept{false};
  // Who may drive our cars (Vehicles.reds, garage.md): any partner (crew),
  // only with us aboard, or nobody.
  proto::Lend lend{proto::Lend::Crew};
  // Comms (docs/architecture/comms.md): the link's default, and pings.
  enum class Link { Auto, Ask, Off };
  Link link{Link::Auto};
  bool pings{true};
  // The co-op menu's Join by address: the last address joined ("host:port").
  std::string lastAddress;
  // The log's level: 1 warn, 2 info, 3 debug (log::Level).
  int logLevel{2};
  // Not from the file: tandem.cmd next to it, a command for the running game;
  // recent.txt, the hosts this game joined (tandem/hosts.hpp).
  std::filesystem::path commandFile;
  // pilot.state next to the command file: what the test pilot reads
  // (pilot_natives.cpp).
  std::filesystem::path pilotStateFile;
  std::filesystem::path recentFile;
};

// Missing file: the defaults (off). Unknown keys and bad values are logged.
Config LoadConfig(const std::filesystem::path &path);
// One key's value into `config` (line: tandem.ini's line, for the log; 0: the
// settings page). False, and logged, when it is not a value of that key.
bool ApplySetting(Config &config, const std::string &key, const std::string &value, int line);
// A settings page key's value as tandem.ini writes it ("" unknown; the relay
// token is only "set" or "").
std::string SettingOf(const Config &config, std::string_view key);
const char *NameOf(Config::Role role);
} // namespace tandem::plugin
