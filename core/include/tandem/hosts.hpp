#pragma once

// Where to join: what a player types in the co-op menu ("host" or
// "host:port") and the hosts recently joined, which the plugin keeps in a
// file next to the DLL (one per line, newest first).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tandem::hosts {
struct Target {
  std::string address; // an IPv4 address or a host name, lower case
  std::uint16_t port{};

  bool operator==(const Target &) const = default;
};

// "address" (on defaultPort) or "address:port", spaces around trimmed.
// nullopt: empty, characters a host name cannot have, or a bad port.
std::optional<Target> Parse(std::string_view text, std::uint16_t defaultPort);
std::string Format(const Target &target); // "address:port"

struct Recent {
  Target target;
  std::string name; // the host's player name, as last seen (may be empty)

  bool operator==(const Recent &) const = default;
};

inline constexpr std::size_t kMaxRecent = 8;

// The file's text: "address:port name" per line; bad lines are skipped.
std::vector<Recent> ParseRecent(std::string_view text);
std::string FormatRecent(const std::vector<Recent> &list);
// `target` first: an older entry for it is dropped (its name kept when the
// new one is empty), and the list is cut to kMaxRecent.
void Remember(std::vector<Recent> &list, const Target &target, std::string name);
} // namespace tandem::hosts
