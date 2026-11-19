#include "config.hpp"
#include "log.hpp"

#include <charconv>
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

Config LoadConfig(const std::filesystem::path &path) {
  Config config;
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
        log::Warn("tandem.ini:{}: role is off, host or join", number);
    } else if (key == "address") {
      config.address = value;
      config.addressSet = !value.empty();
    } else if (key == "relay") {
      if (value.empty())
        config.relay.clear();
      else if (!net::ParseRelayAddress(value, config.relay, config.relayPort))
        log::Warn("tandem.ini:{}: relay is host or host:port, not {}", number, value);
    } else if (key == "relay_token") {
      if (const auto token = net::ParseRelayToken(value))
        config.relayToken = *token;
      else
        log::Warn("tandem.ini:{}: relay_token is 64 hex digits (tandem-relay token)", number);
    } else if (key == "join") {
      if (value.empty())
        config.joinCode.clear();
      else if (const auto code = net::NormalizeJoinCode(value))
        config.joinCode = *code;
      else
        log::Warn("tandem.ini:{}: join is a join code like K7Q-2M9, not {}", number, value);
    } else if (key == "name")
      config.name = value;
    else if (key == "puppet_female")
      config.puppetFemale = value;
    else if (key == "puppet_male")
      config.puppetMale = value;
    else if (key == "probe") {
      if (value == "on" || value == "true")
        config.probe = true;
      else if (value == "off" || value == "false")
        config.probe = false;
      else
        log::Warn("tandem.ini:{}: probe is on or off", number);
    } else if (key == "mirror") {
      config.mirror = value == "on" || value == "true";
    } else if (key == "pose") {
      if (value == "on" || value == "true")
        config.pose = true;
      else if (value == "off" || value == "false")
        config.pose = false;
      else
        log::Warn("tandem.ini:{}: pose is on or off", number);
    } else if (key == "recorder_max_mb" || key == "recorder_segment_minutes") {
      std::uint32_t number32{};
      auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number32);
      if (ec == std::errc{} && end == value.data() + value.size() && number32 != 0) {
        if (key == "recorder_max_mb")
          config.recorderMaxMb = number32;
        else
          config.recorderSegmentMinutes = number32;
      } else
        log::Warn("tandem.ini:{}: bad {} {}", number, key, value);
    } else if (key == "story" || key == "quest_freeze" || key == "recorder" ||
               key == "recorder_pose") {
      auto &flag = key == "story"          ? config.story
                   : key == "quest_freeze" ? config.questFreeze
                   : key == "recorder"     ? config.recorder
                                           : config.recorderPose;
      if (value == "on" || value == "true")
        flag = true;
      else if (value == "off" || value == "false")
        flag = false;
      else
        log::Warn("tandem.ini:{}: {} is on or off", number, key);
    } else if (key == "quest_freeze_allow" || key == "private_actors" || key == "private_scenes" ||
               key == "public_scenes") {
      auto &list = key == "quest_freeze_allow" ? config.questFreezeAllow
                   : key == "private_actors"   ? config.privateActors
                   : key == "private_scenes"   ? config.privateScenes
                                               : config.publicScenes;
      if (key == "private_actors")
        list.clear(); // the file's list replaces the defaults
      for (std::size_t start = 0; start <= value.size();) {
        const auto comma = value.find(',', start);
        const auto path = Trim(
            value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
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
        log::Warn("tandem.ini:{}: scene_privacy is on or off", number);
    } else if (key == "scene_replay") {
      if (value == "on" || value == "true")
        config.sceneReplay = config.scenePoses = true;
      else if (value == "noposes") {
        config.sceneReplay = true;
        config.scenePoses = false;
      } else if (value == "off" || value == "false")
        config.sceneReplay = false;
      else
        log::Warn("tandem.ini:{}: scene_replay is on, noposes or off", number);
    } else if (key == "time_host") {
      if (value == "bubble")
        config.timeHostFull = false;
      else if (value == "full")
        config.timeHostFull = true;
      else
        log::Warn("tandem.ini:{}: time_host is bubble or full", number);
    } else if (key == "trace") {
      config.trace = value == "on" || value == "true";
    } else if (key == "port") {
      std::uint16_t port{};
      auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
      if (ec == std::errc{} && end == value.data() + value.size() && port != 0)
        config.port = port;
      else
        log::Warn("tandem.ini:{}: bad port {}", number, value);
    } else
      log::Warn("tandem.ini:{}: unknown key {}", number, key);
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
