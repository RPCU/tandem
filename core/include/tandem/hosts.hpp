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

// Why a typed address is not one to join.
enum class Problem {
  None,
  Empty,
  Port, // not 1 to 65535
  Name, // not a host name nor an IPv4 address
  Ipv6, // an IPv6 address ("[fd7a::1]:27450"): ENet speaks IPv4 only
};

struct Checked {
  std::optional<Target> target;
  Problem problem{Problem::None};
};

// "address" (on defaultPort) or "address:port", spaces around trimmed. The
// address is a host name (labels of letters, digits, '-' and '_', 1 to 63
// characters, no '-' at either end) or a dotted IPv4 address.
Checked Check(std::string_view text, std::uint16_t defaultPort);
// Check's target alone.
std::optional<Target> Parse(std::string_view text, std::uint16_t defaultPort);
// The problem as a word for the scripts: "", "empty", "port", "name", "ipv6".
std::string_view ProblemWord(Problem problem);
std::string Format(const Target &target); // "address:port"

struct Recent {
  Target target;    // joined by address; empty for a join code
  std::string name; // the host's player name, as last seen (may be empty)
  // Joined by this join code ("K7Q2M9", normalized): the host's last one,
  // the same next time only when the relay keeps it; known by its name.
  std::string code;
  // The host's ID on the relay (32 hex digits, its proto::HostCard), once
  // it said it: the Join list asks whether it is online, and joins by it.
  std::string hostId;

  bool operator==(const Recent &) const = default;
};

inline constexpr std::size_t kMaxRecent = 8;

// The file's text: "address:port name" or "code:K7Q2M9 name" per line,
// after "id:HEX " when the host's ID is known; bad lines are skipped.
std::vector<Recent> ParseRecent(std::string_view text);
std::string FormatRecent(const std::vector<Recent> &list);
// `target` first: an older entry for it is dropped (its name kept when the
// new one is empty), and the list is cut to kMaxRecent.
void Remember(std::vector<Recent> &list, const Target &target, std::string name);
// A host joined by a join code, first: known by its name (a code changes from
// session to session), so an older code entry of that name (any case) is
// dropped. No name: nothing to remember it by.
void RememberCode(std::vector<Recent> &list, std::string code, std::string name);
// The host's ID heard: kept on its entry (by address, or a code entry by
// name). False: no entry is that host's.
bool NoteHostId(std::vector<Recent> &list, const std::optional<Target> &target,
                std::string_view name, std::string hostId);
// Joined by the host's ID: its entry first, its name as last seen.
void RememberById(std::vector<Recent> &list, std::string_view hostId, std::string name);
} // namespace tandem::hosts
