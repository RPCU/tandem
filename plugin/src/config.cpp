#include "config.hpp"
#include "log.hpp"

#include "tandem/hosts.hpp"
#include "tandem/names.hpp"

#include <charconv>
#include <format>
#include <fstream>

namespace tandem::plugin {
namespace {
std::string Trim(std::string text) {
  const auto first = text.find_first_not_of(" \t\r");
  if (first == std::string::npos)
    return {};
  const auto last = text.find_last_not_of(" \t\r");
  return text.substr(first, last - first + 1);
}
} // namespace

bool ApplySetting(Config &config, const std::string &key, const std::string &value, int number) {
  const auto Bad = [number]<typename... Args>(std::format_string<Args...> what, Args &&...args) {
    const auto text = std::format(what, std::forward<Args>(args)...);
    if (number > 0)
      log::Warn("tandem.ini:{}: {}", number, text);
    else
      log::Warn("setting: {}", text);
    return false;
  };
  if (key == "role") {
    if (value == "off")
      config.role = Config::Role::Off;
    else if (value == "host")
      config.role = Config::Role::Host;
    else if (value == "join")
      config.role = Config::Role::Join;
    else if (value == "record") {
      // Solo with the recorder on (the recorder is its own key now).
      config.role = Config::Role::Off;
      config.recorder = true;
    } else
      return Bad("role is off, host or join");
  } else if (key == "address") {
    config.address = value;
    config.addressSet = !value.empty();
  } else if (key == "relay") {
    // One relay, or a list: a host registers on the nearest, a guest asks
    // each in turn for its code.
    config.relay.clear();
    config.relayMore.clear();
    for (std::size_t start = 0; !value.empty() && start <= value.size();) {
      const auto comma = value.find(',', start);
      const auto field =
          Trim(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
      std::string name;
      std::uint16_t port{};
      if (!net::ParseRelayAddress(field, name, port))
        return Bad("relay is host or host:port (a list: comma-separated), not {}", value);
      if (config.relay.empty()) {
        config.relay = name;
        config.relayPort = port;
      } else {
        config.relayMore.push_back(field);
      }
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
  } else if (key == "code") {
    if (value == "keep")
      config.keepCode = true;
    else if (value == "new")
      config.keepCode = false;
    else
      return Bad("code is keep or new");
  } else if (key == "code_salt") {
    std::uint32_t salt{};
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), salt);
    if (ec != std::errc{} || end != value.data() + value.size())
      return Bad("code_salt is a number");
    config.codeSalt = salt;
  } else if (key == "relay_token") {
    if (value.empty())
      config.relayToken = {};
    else if (const auto token = net::ParseRelayToken(value))
      config.relayToken = *token;
    else
      return Bad("relay_token is 64 hex digits (tandem-relay token)");
  } else if (key == "join") {
    if (value.empty())
      config.joinCode.clear();
    else if (const auto code = net::NormalizeJoinCode(value))
      config.joinCode = *code;
    else
      return Bad("join is a join code like K7Q-2M9, not {}", value);
  } else if (key == "name") {
    const auto name = names::Clean(value);
    if (!name)
      return Bad("name is 1 to {} characters, no control characters, ; or # ({}: {})",
                 names::kMaxLength, names::Problem(value), value);
    config.name = *name;
  } else if (key == "puppet_female")
    config.puppetFemale = value;
  else if (key == "puppet_male")
    config.puppetMale = value;
  else if (key == "probe") {
    if (value == "on" || value == "true")
      config.probe = true;
    else if (value == "off" || value == "false")
      config.probe = false;
    else
      return Bad("probe is on or off");
  } else if (key == "mirror") {
    config.mirror = value == "on" || value == "true";
  } else if (key == "pose") {
    if (value == "on" || value == "true")
      config.pose = true;
    else if (value == "off" || value == "false")
      config.pose = false;
    else
      return Bad("pose is on or off");
  } else if (key == "enemy_scaling_health") {
    float health{};
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), health);
    if (ec != std::errc{} || end != value.data() + value.size() || !(health >= 0.f) ||
        health > scaling::kMaxHealthPerPlayer)
      return Bad("enemy_scaling_health is 0 to {} (the health added per extra player), not {}",
                 scaling::kMaxHealthPerPlayer, value);
    config.enemyScalingHealth = health;
  } else if (key == "recorder_max_mb" || key == "recorder_segment_minutes") {
    std::uint32_t number32{};
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number32);
    if (ec == std::errc{} && end == value.data() + value.size() && number32 != 0) {
      if (key == "recorder_max_mb")
        config.recorderMaxMb = number32;
      else
        config.recorderSegmentMinutes = number32;
    } else
      return Bad("bad {} {}", key, value);
  } else if (key == "story" || key == "quest_freeze" || key == "quest_interact" ||
             key == "recorder" || key == "recorder_pose" || key == "friendly_fire" ||
             key == "markers" || key == "party_hud" || key == "enemy_scaling" ||
             key == "death_ledger" || key == "death_bodies" || key == "overlap_despawn") {
    auto &flag = key == "story"             ? config.story
                 : key == "quest_freeze"    ? config.questFreeze
                 : key == "quest_interact"  ? config.questInteract
                 : key == "recorder"        ? config.recorder
                 : key == "friendly_fire"   ? config.friendlyFire
                 : key == "enemy_scaling"   ? config.enemyScaling
                 : key == "markers"         ? config.markers
                 : key == "party_hud"       ? config.partyHud
                 : key == "death_ledger"    ? config.deathLedger
                 : key == "death_bodies"    ? config.deathBodies
                 : key == "overlap_despawn" ? config.overlapDespawn
                                            : config.recorderPose;
    if (value == "on" || value == "true")
      flag = true;
    else if (value == "off" || value == "false")
      flag = false;
    else
      return Bad("{} is on or off", key);
  } else if (key == "quest_freeze_allow" || key == "quest_freeze_gigs" || key == "private_actors" ||
             key == "private_scenes" || key == "public_scenes" ||
             key == "quest_interact_hostonly" || key == "quest_interact_allow") {
    auto &list = key == "quest_freeze_allow"        ? config.questFreezeAllow
                 : key == "quest_freeze_gigs"       ? config.questFreezeGigs
                 : key == "quest_interact_hostonly" ? config.questInteractHostOnly
                 : key == "quest_interact_allow"    ? config.questInteractAllow
                 : key == "private_actors"          ? config.privateActors
                 : key == "private_scenes"          ? config.privateScenes
                                                    : config.publicScenes;
    if (key == "private_actors")
      list.clear(); // the file's list replaces the defaults
    for (std::size_t start = 0; start <= value.size();) {
      const auto comma = value.find(',', start);
      const auto path =
          Trim(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
      if (!path.empty())
        list.push_back(path);
      if (comma == std::string::npos)
        break;
      start = comma + 1;
    }
  } else if (key == "scene_privacy") {
    if (value == "on" || value == "true")
      config.scenePrivacy = true;
    else if (value == "off" || value == "false")
      config.scenePrivacy = false;
    else
      return Bad("scene_privacy is on or off");
  } else if (key == "scene_replay") {
    if (value == "on" || value == "true")
      config.sceneReplay = config.scenePoses = true;
    else if (value == "noposes") {
      config.sceneReplay = true;
      config.scenePoses = false;
    } else if (value == "off" || value == "false")
      config.sceneReplay = false;
    else
      return Bad("scene_replay is on, noposes or off");
  } else if (key == "scene_rigs") {
    if (value == "on" || value == "true")
      config.sceneRigs = true;
    else if (value == "off" || value == "false")
      config.sceneRigs = false;
    else
      return Bad("scene_rigs is on or off");
  } else if (key == "scene_camera") {
    if (value == "on" || value == "true")
      config.sceneCamera = true;
    else if (value == "off" || value == "false")
      config.sceneCamera = false;
    else
      return Bad("scene_camera is on or off");
  } else if (key == "scene_voice_gender") {
    if (value == "on" || value == "true")
      config.sceneVoiceGender = 1;
    else if (value == "swapped")
      config.sceneVoiceGender = 2;
    else if (value == "off" || value == "false")
      config.sceneVoiceGender = 0;
    else
      return Bad("scene_voice_gender is on, swapped or off");
  } else if (key == "scene_kbps") {
    // What a scene's streams may take on a guest's link (8 to 2000 kbit/s).
    int kbps = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), kbps);
    if (error != std::errc{} || end != value.data() + value.size() || kbps < 8 || kbps > 2000)
      return Bad("scene_kbps is a number from 8 to 2000");
    config.sceneKbps = static_cast<std::uint32_t>(kbps);
  } else if (key == "time_host") {
    if (value == "bubble")
      config.timeHostFull = false;
    else if (value == "full")
      config.timeHostFull = true;
    else
      return Bad("time_host is bubble or full");
  } else if (key == "trade") {
    if (value == "on" || value == "true")
      config.trade = true;
    else if (value == "off" || value == "false")
      config.trade = false;
    else
      return Bad("trade is on or off");
  } else if (key == "lend") {
    if (value == "crew")
      config.lend = proto::Lend::Crew;
    else if (value == "owner")
      config.lend = proto::Lend::WithOwner;
    else if (value == "never")
      config.lend = proto::Lend::Never;
    else
      return Bad("lend is crew, owner or never");
  } else if (key == "vehicle_rewards") {
    if (value == "keep")
      config.vehicleRewardsKept = true;
    else if (value == "credit")
      config.vehicleRewardsKept = false;
    else
      return Bad("vehicle_rewards is credit or keep");
  } else if (key == "leave") {
    if (value == "stay")
      config.leave = progress::LeaveMode::Stay;
    else if (value == "save")
      config.leave = progress::LeaveMode::Save;
    else if (value == "reload")
      config.leave = progress::LeaveMode::Reload;
    else
      return Bad("leave is stay, save or reload");
  } else if (key == "link") {
    if (value == "auto")
      config.link = Config::Link::Auto;
    else if (value == "ask")
      config.link = Config::Link::Ask;
    else if (value == "off")
      config.link = Config::Link::Off;
    else
      return Bad("link is auto, ask or off");
  } else if (key == "direct") {
    if (value == "auto" || value == "on")
      config.direct = true;
    else if (value == "off")
      config.direct = false;
    else
      return Bad("direct is auto or off");
  } else if (key == "pings") {
    if (value == "on" || value == "true")
      config.pings = true;
    else if (value == "off" || value == "false")
      config.pings = false;
    else
      return Bad("pings is on or off");
  } else if (key == "last_address") {
    if (value.empty())
      config.lastAddress.clear();
    else if (const auto target = hosts::Parse(value, config.port))
      config.lastAddress = hosts::Format(*target);
    else
      return Bad("last_address is host or host:port, not {}", value);
  } else if (key == "log") {
    if (value == "warn" || value == "quiet")
      config.logLevel = 1;
    else if (value == "info")
      config.logLevel = 2;
    else if (value == "debug")
      config.logLevel = 3;
    else
      return Bad("log is warn, info or debug");
  } else if (key == "compat") {
    // Developers only: off lets a session start whatever the startup check
    // found (another game version, an older Codeware or TweakXL).
    if (value == "on" || value == "true")
      config.compat = true;
    else if (value == "off" || value == "false")
      config.compat = false;
    else
      return Bad("compat is on or off");
  } else if (key == "overlap") {
    if (value == "density" || value == "area" || value == "off")
      config.overlap = std::string(value);
    else
      return Bad("overlap is density, area or off");
  } else if (key == "nameplates") {
    // on, true: what they were before near and always (tandem.ini files of then).
    if (value == "near" || value == "on" || value == "true")
      config.nameplates = presence::Nameplates::Near;
    else if (value == "always")
      config.nameplates = presence::Nameplates::Always;
    else if (value == "off" || value == "false")
      config.nameplates = presence::Nameplates::Off;
    else
      return Bad("nameplates is near, always or off");
  } else if (key == "notices") {
    if (value == "all" || value == "important" || value == "minimal")
      config.notices = value;
    else
      return Bad("notices is all, important or minimal");
  } else if (key == "trace") {
    config.trace = value == "on" || value == "true";
  } else if (key == "port") {
    std::uint16_t port{};
    auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (ec == std::errc{} && end == value.data() + value.size() && port != 0)
      config.port = port;
    else
      return Bad("bad port {}", value);
  } else
    return Bad("unknown key {}", key);
  return true;
}

std::string SettingOf(const Config &config, std::string_view key) {
  const auto OnOff = [](bool on) { return std::string(on ? "on" : "off"); };
  if (key == "name")
    return config.name;
  if (key == "recorder")
    return OnOff(config.recorder);
  if (key == "friendly_fire")
    return OnOff(config.friendlyFire);
  if (key == "enemy_scaling")
    return OnOff(config.enemyScaling);
  if (key == "enemy_scaling_health")
    return std::format("{:.2f}", config.enemyScalingHealth);
  if (key == "notices")
    return config.notices;
  if (key == "markers")
    return OnOff(config.markers);
  if (key == "party_hud")
    return OnOff(config.partyHud);
  if (key == "death_ledger")
    return OnOff(config.deathLedger);
  if (key == "death_bodies")
    return OnOff(config.deathBodies);
  if (key == "overlap")
    return config.overlap;
  if (key == "overlap_despawn")
    return OnOff(config.overlapDespawn);
  if (key == "nameplates")
    return config.nameplates == presence::Nameplates::Always ? "always"
           : config.nameplates == presence::Nameplates::Off  ? "off"
                                                             : "near";
  if (key == "relay") {
    auto text = config.relay.empty() ? std::string()
                : config.relayPort == net::kDefaultRelayPort
                    ? config.relay
                    : std::format("{}:{}", config.relay, config.relayPort);
    for (const auto &more : config.relayMore)
      text += ", " + more;
    return text;
  }
  if (key == "code")
    return config.keepCode ? "keep" : "new";
  if (key == "relay_token") // never shown: set or not
    return config.relayToken == net::RelayToken{} ? std::string() : std::string("set");
  if (key == "story")
    return OnOff(config.story);
  if (key == "quest_freeze")
    return OnOff(config.questFreeze);
  if (key == "scene_privacy")
    return OnOff(config.scenePrivacy);
  if (key == "time_host")
    return config.timeHostFull ? "full" : "bubble";
  if (key == "direct")
    return config.direct ? "auto" : "off";
  if (key == "link")
    return config.link == Config::Link::Ask   ? "ask"
           : config.link == Config::Link::Off ? "off"
                                              : "auto";
  if (key == "pings")
    return OnOff(config.pings);
  if (key == "last_address")
    return config.lastAddress;
  return {};
}

Config LoadConfig(const std::filesystem::path &path) {
  Config config;
  config.file = path;
  std::ifstream file(path);
  if (!file) {
    log::Info("no {}, staying offline", path.filename().string());
    return config;
  }

  std::string line;
  for (int number = 1; std::getline(file, line); ++number) {
    if (const auto comment = line.find_first_of(";#"); comment != std::string::npos)
      line.resize(comment);
    line = Trim(line);
    if (line.empty())
      continue;
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
      log::Warn("tandem.ini:{}: expected key = value", number);
      continue;
    }
    const auto key = Trim(line.substr(0, equals));
    const auto value = Trim(line.substr(equals + 1));
    ApplySetting(config, key, value, number);
  }
  return config;
}

const char *NameOf(Config::Role role) {
  switch (role) {
  case Config::Role::Off:
    return "off";
  case Config::Role::Host:
    return "host";
  case Config::Role::Join:
    return "join";
  }
  return "?";
}
} // namespace tandem::plugin
