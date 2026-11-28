#pragma once

// One game among several on a machine: where the plugin keeps its files and
// which port it uses, when the environment says something else than "next to
// the DLL" (docs/architecture/two-instances.md).
//
//   TANDEM_CONFIG  the settings file instead of tandem.ini (recent.txt, the
//                  hosts this game joined, and campaigns.txt, its co-op
//                  playthroughs, are kept next to it)
//   TANDEM_CMD     the command file instead of tandem.cmd (recordings/ is
//                  kept next to it)
//   TANDEM_PORT    the UDP port, over the settings file's
//   TANDEM_LOG     a log file of tandem's own, besides RED4ext's
//
// Paths are the game's (Windows: under Wine, Z:/ is the Unix root); a
// relative one is taken from the plugin's directory.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace tandem::instance {
inline constexpr const char *kConfigVariable = "TANDEM_CONFIG";
inline constexpr const char *kCommandVariable = "TANDEM_CMD";
inline constexpr const char *kPortVariable = "TANDEM_PORT";
inline constexpr const char *kLogVariable = "TANDEM_LOG";

// The variables' values; empty: not set.
struct Overrides {
  std::filesystem::path config;
  std::filesystem::path command;
  std::filesystem::path log;
};

struct Files {
  std::filesystem::path config;  // tandem.ini
  std::filesystem::path command; // tandem.cmd
  std::filesystem::path recent;  // recent.txt, next to the settings
  // campaigns.txt, next to the settings (tandem/campaigns.hpp)
  std::filesystem::path campaigns;
  std::filesystem::path log; // empty: RED4ext's log only

  bool operator==(const Files &) const = default;
};

Files Resolve(const std::filesystem::path &pluginDirectory, const Overrides &overrides);

// A port from TANDEM_PORT: 1 to 65535, digits only (spaces around trimmed).
std::optional<std::uint16_t> ParsePort(std::string_view text);
} // namespace tandem::instance
