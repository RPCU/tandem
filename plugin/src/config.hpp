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
//   name = V
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
//   private_actors = judy, panam, river, kerry, meredith  ; romance partners
//   private_scenes = , public_scenes =  ; scene resource paths, comma-separated
//   quest_freeze_allow = ; more open-world phase resources that keep running
//                        ; on a frozen guest (resource paths, comma-separated)
//   time_host = full     ; host's Sandevistan (scripts/tandem/Time.reds): full keeps
//                        ; the single-player slow motion whole on its own game (the
//                        ; NPCs it shares follow the bubbles' rules); bubble puts its
//                        ; world back to normal speed and slows only the NPCs
//   compat = on          ; off (developers): start sessions whatever the startup
//                        ; check found (tandem/compat.hpp)

#include "tandem/relay.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
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
  // Scene privacy (engine/scenes.cpp): private scenes are not watched.
  bool scenePrivacy{true};
  std::vector<std::string> privateActors{"judy", "panam", "river", "kerry", "meredith"};
  std::vector<std::string> privateScenes, publicScenes;
  // How the host's own time dilations are played (Time.reds): its world in
  // bubbles only (false), or the whole single-player slow motion with what
  // is shared exempted (true).
  bool timeHostFull{true};
  // The startup check (game version, needed mods) keeps co-op off on a problem.
  bool compat{true};
  // Not from the file: tandem.cmd next to it, a command for the running game;
  // recent.txt, the hosts this game joined (tandem/hosts.hpp).
  std::filesystem::path commandFile;
  std::filesystem::path recentFile;
};

// Missing file: the defaults (off). Unknown keys and bad values are logged.
Config LoadConfig(const std::filesystem::path &path);
const char *NameOf(Config::Role role);
} // namespace tandem::plugin
