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
      else
        log::Warn("tandem.ini:{}: role is off, host or join", number);
    } else if (key == "address")
      config.address = value;
    else if (key == "name")
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
