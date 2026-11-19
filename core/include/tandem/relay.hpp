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
//
// A relay may be one replica of a fleet (docs/relay.md): it then sends a
// client on to the replica holding its session or its code (one redirect at
// most). A relay that goes silent (restarted, or the path to it broke) is
// asked again every half second while the session holds its relayed players
// (RelayLink::kHoldMs): the host gets its session and code back, the guests
// their channels, and play goes on where it stopped.
//
// Direct paths (hole punching): once a guest is in through the relay, the
// relay tells it and its host each other's public address (as it sees
// them) and LAN address; both send punches from the socket they use for
// the relay, and once one is answered the proxy sends to the other side
// directly. ENet never knows: it still talks to the proxy on 127.0.0.1.
// The relay path stays registered, and takes the traffic back as soon as
// the direct one goes silent.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
  // More relays ("name[:port]", tandem.ini's `relay = a, b, c`): a host
  // registers on the one that answers its probe the soonest, a guest asks
  // each in turn for its code (the relays of one network send it on
  // themselves, across regions too).
  std::vector<std::string> more;
  // Host: one of the relay's host tokens (zeros for a relay open to any host).
  RelayToken token{};
  // Guest: the session's secret when known (joined before), else its code.
  // With both, a secret the relay no longer knows falls back to the code.
  std::optional<RelaySecret> secret;
  std::string code;
  // A direct path between host and guest, set up through the relay (hole
  // punching; tandem.ini's `direct = auto`): the relay path stays as the
  // fallback. False: always through the relay.
  bool direct{true};
  // Host: the code the relay derives from our token and `codeSalt` (the
  // same every session, while we are registered; tandem.ini's `code =
  // keep`), rather than a new one each session.
  bool keepCode{};
  std::uint32_t codeSalt{};
};

// What the relays saw of us (the connection check): how our NAT maps the
// socket we use for them.
enum class NatMapping : std::uint8_t {
  Unknown, // fewer than two relay ports answered
  Stable,  // the same public port for each: direct paths should work
  Varies,  // another port per destination (symmetric): relay only
};

struct RelayStatus {
  enum class State {
    Connecting, // registering (guest: resolving the code first; both: the
                // relay went silent and is asked again, `error` says so)
    Ready,      // registered: traffic flows
    Failed,     // `error` says why (a host keeps trying)
  };
  State state{State::Connecting};
  // Host: its session's join code ("" until the relay gave one).
  std::string code;
  std::uint32_t codeSecondsLeft{};
  std::string error;
  // The relay replica in use, "ip:port" (another than the one configured
  // after a redirect): a guest joins again there by the session's secret.
  std::string at;
  // Direct paths up (guest: 0 or 1, to the host; host: its guests'): their
  // traffic skips the relay.
  std::uint32_t direct{};
  // Host: `code` is our kept one (it lives while we are registered), and
  // the salt it came from (the "new code" action changes it: save it).
  bool codeKept{};
  std::uint32_t codeSalt{};
  // The connection check: our public address as the relay sees it ("ip:port"),
  // whether its port is our socket's own, and our NAT's mapping.
  std::string publicAddress;
  bool portKept{};
  NatMapping nat{NatMapping::Unknown};

  bool operator==(const RelayStatus &) const = default;
};

// What the connection check says of direct paths: "direct" (likely to
// work), "relay" (our NAT maps each destination apart: relay only), ""
// (not known yet).
std::string_view DirectOutlook(const RelayStatus &status);
} // namespace tandem::net
