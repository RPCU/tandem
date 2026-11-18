#pragma once

// tandem.ini, next to tandem.dll:
//
//   role = join          ; off | host | join
//   address = 127.0.0.1  ; join: the host's address
//   port = 27450
//   name = V
//   puppet_female = Character.TandemPuppetWa
//   puppet_male = Character.TandemPuppetMa
//   probe = off          ; on: spawn test bodies and log their animation setup
//   trace = off          ; on: log every distinct anim feature the engine applies
//   mirror = off         ; on (experiment): puppets get the local V's anim features
//   pose = on            ; off: no body pose streaming (the hooks stay out)
//   time_host = bubble   ; host's Sandevistan (scripts/tandem/Time.reds): bubble keeps
//                        ; its world at normal speed and slows only the NPCs around
//                        ; its V; full keeps the single-player slow motion on its own
//                        ; screen, shared NPCs and puppets exempted (experimental)

#include <cstdint>
#include <filesystem>
#include <string>

namespace tandem::plugin {
struct Config {
  enum class Role { Off, Host, Join };

  Role role{Role::Off};
  std::string address{"127.0.0.1"};
  std::uint16_t port{27450};
  std::string name{"V"};
  std::string puppetFemale{"Character.TandemPuppetWa"};
  std::string puppetMale{"Character.TandemPuppetMa"};
  bool probe{false};
  bool trace{false};
  bool mirror{false};
  bool pose{true};
  // How the host's own time dilations are played (Time.reds): its world in
  // bubbles only (false), or the whole single-player slow motion with what
  // is shared exempted (true).
  bool timeHostFull{false};
  // Not from the file: tandem.cmd next to it, a command for the running game.
  std::filesystem::path commandFile;
};

// Missing file: the defaults (off). Unknown keys and bad values are logged.
Config LoadConfig(const std::filesystem::path &path);
const char *NameOf(Config::Role role);
} // namespace tandem::plugin
