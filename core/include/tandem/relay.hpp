#pragma once

// Reaching a session through tandem-relay (relay/, wire format in
// relay/internal/relay/wire.go) when NAT keeps the players apart. ENet never
// knows: the session runs a small UDP proxy on 127.0.0.1 that wraps what ENet
// sends for the relay and unwraps what the relay forwards (core/src/relay.cpp).
//
//   guest ENet -> 127.0.0.1:proxy -> relay -> host proxy, one socket per
//   relayed guest -> 127.0.0.1:port (host ENet, which also takes direct guests)
//
// Join codes: a host on a relay gets a short code naming its session (6
// characters of Crockford's base32, shown as "K7Q-2M9", valid 10 minutes,
// renewed when it expires); a guest trades it for the session's secret and
// joins with that, and joins again with the secret after the code expired.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tandem::net {
inline constexpr std::uint16_t kDefaultRelayPort = 27460;
inline constexpr std::size_t kJoinCodeSize = 6;

using RelaySecret = std::array<std::uint8_t, 16>;
using RelayToken = std::array<std::uint8_t, 32>;

// A join code as a player typed it (any case, dashes and spaces, O for 0, I
// or L for 1) -> its 6 characters; nullopt when it is not a code. The relay
// applies the same rule (relay.NormalizeCode).
std::optional<std::string> NormalizeJoinCode(std::string_view text);
// "K7Q2M9" -> "K7Q-2M9", to show to players.
std::string DisplayJoinCode(std::string_view code);
// A host token as tandem-relay prints it: 64 hex digits.
std::optional<RelayToken> ParseRelayToken(std::string_view hex);
// "name[:port]" (default port kDefaultRelayPort); false when malformed.
bool ParseRelayAddress(std::string_view text, std::string &name, std::uint16_t &port);

struct RelayOptions {
  std::string address;
  std::uint16_t port{kDefaultRelayPort};
  // Host: one of the relay's host tokens (zeros for a relay open to any host).
  RelayToken token{};
  // Guest: the session's secret when known (joined before), else its code.
  // With both, a secret the relay no longer knows falls back to the code.
  std::optional<RelaySecret> secret;
  std::string code;
};

struct RelayStatus {
  enum class State {
    Connecting, // registering (guest: resolving the code first)
    Ready,      // registered: traffic flows
    Failed,     // `error` says why (a host keeps trying)
  };
  State state{State::Connecting};
  // Host: its session's join code ("" until the relay gave one).
  std::string code;
  std::uint32_t codeSecondsLeft{};
  std::string error;

  bool operator==(const RelayStatus &) const = default;
};
} // namespace tandem::net
