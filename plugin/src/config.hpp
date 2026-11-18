#pragma once

// tandem.ini, next to tandem.dll:
//
//   role = join          ; off | host | join
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
//   quest_freeze_allow = ; more open-world phase resources that keep running
//                        ; on a frozen guest (resource paths, comma-separated)

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
  bool questFreeze{false};
  std::vector<std::string> questFreezeAllow;
  // Not from the file: tandem.cmd next to it, a command for the running game.
  std::filesystem::path commandFile;
};

// Missing file: the defaults (off). Unknown keys and bad values are logged.
Config LoadConfig(const std::filesystem::path &path);
const char *NameOf(Config::Role role);
} // namespace tandem::plugin
