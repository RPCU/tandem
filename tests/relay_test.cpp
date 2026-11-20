// Sessions through tandem-relay. The "relay" suite needs a real relay
// process: tests/relay_loopback.sh starts one on 127.0.0.1 and runs it with
// TANDEM_RELAY set (without it those cases are skipped). The rest needs none.

#include "ports.hpp"
#include "sha256.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <format>
#include <functional>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

// Runs every endpoint until `done` holds or `limit` passes.
bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline) {
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  }
  return finished;
}

std::optional<std::string> EndOf(Endpoint &endpoint, std::chrono::milliseconds limit) {
  std::optional<std::string> reason;
  Pump(
      {&endpoint},
      [&](std::size_t, const auto &events) {
        for (const auto &event : events)
          if (const auto *ended = std::get_if<events::Ended>(&event))
            reason = ended->reason;
        return reason.has_value();
      },
      limit);
  return reason;
}

// The relay from TANDEM_RELAY, or nothing (the suite is skipped).
std::optional<net::RelayOptions> TestRelay() {
  const char *text = std::getenv("TANDEM_RELAY");
  net::RelayOptions relay;
  if (!text || !net::ParseRelayAddress(text, relay.address, relay.port))
    return std::nullopt;
  return relay;
}

#define NEEDS_RELAY(relay)                                                                         \
  const auto relay = TestRelay();                                                                  \
  if (!relay) {                                                                                    \
    MESSAGE("TANDEM_RELAY unset: skipped (tests/relay_loopback.sh runs it)");                      \
    return;                                                                                        \
  }

// A fleet of two relays and its entrance (tests/relay_loopback.sh):
// TANDEM_RELAY_FLEET="ip:port,ip:port", TANDEM_RELAY_FRONT="ip:port".
struct Fleet {
  net::RelayOptions replicas[2];
  net::RelayOptions front;
};

std::optional<Fleet> TestFleet() {
  const char *fleet = std::getenv("TANDEM_RELAY_FLEET");
  const char *front = std::getenv("TANDEM_RELAY_FRONT");
  if (!fleet || !front)
    return std::nullopt;
  const std::string_view text(fleet);
  const auto comma = text.find(',');
  Fleet out;
  if (comma == std::string_view::npos ||
      !net::ParseRelayAddress(text.substr(0, comma), out.replicas[0].address,
                              out.replicas[0].port) ||
      !net::ParseRelayAddress(text.substr(comma + 1), out.replicas[1].address,
                              out.replicas[1].port) ||
      !net::ParseRelayAddress(front, out.front.address, out.front.port))
    return std::nullopt;
  return out;
}

std::string AddressOf(const net::RelayOptions &relay) {
  return relay.address + ":" + std::to_string(relay.port);
}

// A relay process of our own (TANDEM_RELAY_BIN), to stop and start again.
class RelayProcess {
public:
  RelayProcess(std::string binary, std::uint16_t port)
      : m_binary(std::move(binary)), m_port(port) {}
  ~RelayProcess() { Kill(); }

  bool Start() {
    const auto udp = "127.0.0.1:" + std::to_string(m_port);
    // Every flag its environment could set (TANDEM_RELAY_FRONT is the
    // fleet's, tests/relay_loopback.sh), given here.
    std::vector<std::string> args{
        m_binary,  "serve", "--allow-any-host", "--udp", udp,         "--http", "127.0.0.1:0",
        "--peers", udp,     "--shard",          "0",     "--readers", "2",      "--front="};
    std::vector<char *> argv;
    for (auto &arg : args)
      argv.push_back(arg.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    const bool ok =
        posix_spawn(&m_pid, m_binary.c_str(), &actions, nullptr, argv.data(), environ) == 0;
    posix_spawn_file_actions_destroy(&actions);
    return ok;
  }
  // A crash: no drain, every session forgotten.
  void Kill() {
    if (m_pid <= 0)
      return;
    ::kill(m_pid, SIGKILL);
    waitpid(m_pid, nullptr, 0);
    m_pid = 0;
  }
  net::RelayOptions Options() const { return {.address = "127.0.0.1", .port = m_port}; }

private:
  std::string m_binary;
  std::uint16_t m_port;
  pid_t m_pid{};
};

// The host's join code, once the relay gave it.
std::string CodeOf(Endpoint &host, std::chrono::milliseconds limit = 3s) {
  std::string code;
  Pump(
      {&host},
      [&](std::size_t, const auto &) {
        if (const auto relay = host.Relay())
          code = relay->code;
        return !code.empty();
      },
      limit);
  return code;
}

// Runs the endpoints until every guest is in the session.
bool AllJoined(std::vector<Endpoint *> endpoints, std::chrono::milliseconds limit = 4s) {
  return Pump(
      endpoints,
      [&](std::size_t, const auto &) {
        return std::ranges::all_of(endpoints, [](Endpoint *e) { return e->IsJoined(); });
      },
      limit);
}
} // namespace

TEST_CASE("join codes are read the way players type them") {
  CHECK(net::NormalizeJoinCode("k7q-2m9") == "K7Q2M9");
  CHECK(net::NormalizeJoinCode(" O1L I00 ") == "011100");
  CHECK_FALSE(net::NormalizeJoinCode("K7Q2M"));
  CHECK_FALSE(net::NormalizeJoinCode("K7Q2M9X"));
  CHECK_FALSE(net::NormalizeJoinCode("K7Q2MU")); // no U in Crockford's base32
  CHECK_FALSE(net::NormalizeJoinCode(""));
  CHECK(net::DisplayJoinCode("K7Q2M9") == "K7Q-2M9");
}

TEST_CASE("SHA-256, and what an open relay's proof of work costs here") {
  const auto hex = [](const net::Sha256Digest &digest) {
    std::string out;
    for (const auto byte : digest)
      out += std::format("{:02x}", byte);
    return out;
  };
  const auto of = [&](std::string_view text) {
    return hex(
        net::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size())));
  };
  // FIPS 180-2's examples: one block, two blocks (the padding spills over).
  CHECK(of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(net::LeadingZeroBits(net::Sha256Digest{0, 0, 0x1F}) == 19);

  // The relay's default difficulty (17 bits, relay.DefaultConfig), solved as
  // RelayLink does: the mean of a few, for the record.
  const auto start = std::chrono::steady_clock::now();
  constexpr int kProofs = 4;
  for (int proof = 0; proof < kProofs; ++proof) {
    std::uint8_t message[40] = {static_cast<std::uint8_t>(proof)};
    for (std::uint64_t nonce = 0;; ++nonce) {
      for (int j = 0; j < 8; ++j)
        message[32 + j] = static_cast<std::uint8_t>(nonce >> (56 - 8 * j));
      if (net::LeadingZeroBits(net::Sha256(message)) >= 17)
        break;
    }
  }
  const auto took =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
  MESSAGE("17-bit proof of work: " << took.count() / kProofs << " ms on average");
}

TEST_CASE("relay addresses and host tokens") {
  std::string name;
  std::uint16_t port{};
  CHECK(net::ParseRelayAddress("relay.example.org", name, port));
  CHECK(name == "relay.example.org");
  CHECK(port == net::kDefaultRelayPort);
  CHECK(net::ParseRelayAddress("10.0.0.1:4000", name, port));
  CHECK(name == "10.0.0.1");
  CHECK(port == 4000);
  CHECK_FALSE(net::ParseRelayAddress(":4000", name, port));
  CHECK_FALSE(net::ParseRelayAddress("x:0", name, port));
  CHECK_FALSE(net::ParseRelayAddress("x:port", name, port));

  const auto token =
      net::ParseRelayToken("00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF");
  REQUIRE(token);
  CHECK((*token)[1] == 0x11);
  CHECK((*token)[31] == 0xFF);
  CHECK_FALSE(net::ParseRelayToken("0011"));
  CHECK_FALSE(
      net::ParseRelayToken("zz112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"));
}

TEST_CASE("a relay that does not answer leaves direct guests be") {
  const auto port = NextPort();
  const auto deadRelay = NextPort(); // nobody listens there
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast,
                             net::RelayOptions{.address = "127.0.0.1", .port = deadRelay});
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kFast);
  REQUIRE_MESSAGE(guest, error);
  REQUIRE(AllJoined({host.get(), guest.get()}));
  CHECK_FALSE(guest->ThroughRelay());

  std::optional<net::RelayStatus> relay;
  Pump({host.get(), guest.get()}, [&](std::size_t, const auto &) {
    relay = host->Relay();
    return relay && relay->state == net::RelayStatus::State::Failed;
  });
  REQUIRE(relay);
  CHECK(relay->state == net::RelayStatus::State::Failed);
  CHECK(relay->error == "the relay did not answer");
  CHECK(relay->code.empty());
  CHECK(host->Players().size() == 2);
}

TEST_CASE("a guest whose relay does not answer is told so") {
  std::string error;
  auto guest = Endpoint::Join({"guest"},
                              JoinTarget{.relay = net::RelayOptions{.address = "127.0.0.1",
                                                                    .port = NextPort(),
                                                                    .code = "K7Q-2M9"}},
                              error, kFast);
  REQUIRE_MESSAGE(guest, error);
  const auto reason = EndOf(*guest, 3s);
  REQUIRE(reason);
  CHECK(*reason == "the relay did not answer");

  // Not a code: refused before anything is sent.
  CHECK_FALSE(Endpoint::Join(
      {"guest"},
      JoinTarget{.relay =
                     net::RelayOptions{.address = "127.0.0.1", .port = NextPort(), .code = "nope"}},
      error, kFast));
  CHECK(error == "not a join code: nope");
}

TEST_SUITE("relay") {
  TEST_CASE("a guest joins by code through the relay; both see each other's V") {
    NEEDS_RELAY(relay);
    std::string error;
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, relay);
    REQUIRE_MESSAGE(host, error);
    const auto code = CodeOf(*host);
    REQUIRE(code.size() == net::kJoinCodeSize);
    CHECK(host->Relay()->codeSecondsLeft > 0);

    // As a player would type it: lower case, with the dash.
    auto typed = net::DisplayJoinCode(code);
    std::ranges::transform(typed, typed.begin(), [](char c) { return std::tolower(c); });
    auto byCode = *relay;
    byCode.code = typed;
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));
    CHECK(guest->ThroughRelay());
    CHECK(guest->RelaySessionSecret());
    CHECK(host->Players().size() == 2);

    guest->SendLocalState(proto::PlayerState{.position = {1.f, 2.f, 3.f}});
    host->SendLocalState(proto::PlayerState{.position = {7.f, 8.f, 9.f}});
    std::optional<proto::PlayerState> atHost, atGuest;
    REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
      for (const auto &event : events)
        if (const auto *remote = std::get_if<events::RemoteState>(&event))
          (i == 0 ? atHost : atGuest) = remote->state;
      return atHost && atGuest;
    }));
    CHECK(atHost->player == 2);
    CHECK(atHost->position == proto::Vec3{1.f, 2.f, 3.f});
    CHECK(atGuest->position == proto::Vec3{7.f, 8.f, 9.f});

    // The session's end reaches a relayed guest too.
    host->Leave("closing");
    const auto reason = EndOf(*guest, 3s);
    REQUIRE(reason);
    CHECK(*reason == "closing");
  }

  TEST_CASE("a wrong join code ends the join with the relay's reason") {
    NEEDS_RELAY(relay);
    std::string error;
    auto wrong = *relay;
    wrong.code = "ZZZ-ZZZ";
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = wrong}, error, kFast);
    REQUIRE_MESSAGE(guest, error);
    const auto reason = EndOf(*guest, 3s);
    REQUIRE(reason);
    CHECK(*reason == "no session has this join code (wrong, or expired)");
  }

  TEST_CASE("a guest that cannot reach the host directly goes through the relay") {
    NEEDS_RELAY(relay);
    std::string error;
    auto host = Endpoint::Host({"host"}, NextPort(), error, kFast, relay);
    REQUIRE_MESSAGE(host, error);
    auto byCode = *relay;
    byCode.code = CodeOf(*host);
    REQUIRE_FALSE(byCode.code.empty());

    // The address answers nothing, as a host behind NAT would.
    const auto start = std::chrono::steady_clock::now();
    auto guest = Endpoint::Join(
        {"guest"}, JoinTarget{.address = "127.0.0.1", .port = NextPort(), .relay = byCode}, error,
        kFast);
    REQUIRE_MESSAGE(guest, error);
    bool ended = false;
    REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
      if (i == 1)
        for (const auto &event : events)
          ended |= std::holds_alternative<events::Ended>(event);
      return guest->IsJoined() || ended;
    }));
    CHECK_FALSE(ended);
    CHECK(guest->ThroughRelay());
    const auto took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
    MESSAGE("in the session through the relay after " << took.count() << " s");
    CHECK(took < 2s);
  }

  TEST_CASE("direct and relayed guests share a session; the secret joins again") {
    NEEDS_RELAY(relay);
    std::string error;
    const auto port = NextPort();
    auto host = Endpoint::Host({"host"}, port, error, {}, relay);
    REQUIRE_MESSAGE(host, error);
    auto byCode = *relay;
    byCode.code = CodeOf(*host);

    auto direct = Endpoint::Join({"direct"}, "127.0.0.1", port, error);
    REQUIRE_MESSAGE(direct, error);
    auto relayed = Endpoint::Join({"relayed"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(relayed, error);
    REQUIRE(AllJoined({host.get(), direct.get(), relayed.get()}));
    REQUIRE(Pump({host.get(), direct.get(), relayed.get()}, [&](std::size_t, const auto &) {
      return direct->Players().size() == 3 && relayed->Players().size() == 3;
    }));
    CHECK_FALSE(direct->ThroughRelay());
    CHECK(relayed->ThroughRelay());

    // A relayed guest's states reach the direct one through the host.
    relayed->SendLocalState(proto::PlayerState{.position = {4.f, 5.f, 6.f}});
    std::optional<proto::PlayerState> seen;
    REQUIRE(Pump({host.get(), direct.get(), relayed.get()}, [&](std::size_t i, const auto &events) {
      for (const auto &event : events)
        if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 1)
          seen = remote->state;
      return seen.has_value();
    }));
    CHECK(seen->player == relayed->You());

    // Back with the session's secret alone (the code may have expired).
    const auto secret = relayed->RelaySessionSecret();
    REQUIRE(secret);
    relayed->Leave();
    relayed.reset();
    auto bySecret = *relay;
    bySecret.secret = secret;
    auto again = Endpoint::Join({"again"}, JoinTarget{.relay = bySecret}, error);
    REQUIRE_MESSAGE(again, error);
    REQUIRE(AllJoined({host.get(), direct.get(), again.get()}));
    CHECK(again->ThroughRelay());
  }

  TEST_CASE("a fleet of relays: the entrance and a replica send players on to the owner") {
    const auto fleet = TestFleet();
    if (!fleet) {
      MESSAGE("TANDEM_RELAY_FLEET unset: skipped (tests/relay_loopback.sh runs it)");
      return;
    }
    std::string error;
    // A host at the entrance is sent to a replica, which owns its session.
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, fleet->front);
    REQUIRE_MESSAGE(host, error);
    const auto code = CodeOf(*host);
    REQUIRE(code.size() == net::kJoinCodeSize);
    const auto owner = host->Relay()->at;
    const int ownerIndex = owner == AddressOf(fleet->replicas[0]) ? 0 : 1;
    REQUIRE(owner == AddressOf(fleet->replicas[ownerIndex]));

    // A guest asking the other replica is sent to the owner, once.
    auto byCode = fleet->replicas[1 - ownerIndex];
    byCode.code = code;
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));
    CHECK(guest->Relay()->at == owner);

    // And one at the entrance too.
    auto atFront = fleet->front;
    atFront.code = code;
    auto third = Endpoint::Join({"third"}, JoinTarget{.relay = atFront}, error);
    REQUIRE_MESSAGE(third, error);
    REQUIRE(AllJoined({host.get(), guest.get(), third.get()}));
    CHECK(third->Relay()->at == owner);
  }

  TEST_CASE("a relay that restarts within the hold: the session goes on, the code too") {
    const char *binary = std::getenv("TANDEM_RELAY_BIN");
    if (!binary) {
      MESSAGE("TANDEM_RELAY_BIN unset: skipped (tests/relay_loopback.sh runs it)");
      return;
    }
    RelayProcess relay(binary, NextPort());
    REQUIRE(relay.Start());
    std::this_thread::sleep_for(300ms);
    std::string error;
    // All through the relay (a direct path would carry them over the gap).
    auto options = relay.Options();
    options.direct = false;
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, options);
    REQUIRE_MESSAGE(host, error);
    auto byCode = options;
    byCode.code = CodeOf(*host, 10s);
    REQUIRE_FALSE(byCode.code.empty());
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));
    const auto you = guest->You();

    // Down for longer than the session's timeout (5 s), as a pod being
    // replaced: nobody leaves.
    relay.Kill();
    bool ended = false, left = false;
    const auto watch = [&](std::size_t i, const std::vector<SessionEvent> &events) {
      for (const auto &event : events) {
        ended |= std::holds_alternative<events::Ended>(event);
        left |= i == 0 && std::holds_alternative<events::PlayerLeft>(event);
      }
      return ended || left;
    };
    Pump({host.get(), guest.get()}, watch, 8s);
    REQUIRE_FALSE(ended);
    REQUIRE_FALSE(left);
    CHECK(host->Relay()->state == net::RelayStatus::State::Connecting);

    REQUIRE(relay.Start());
    // The host's code is back on the relay, and play goes on.
    std::optional<proto::PlayerState> seen;
    REQUIRE(Pump(
        {host.get(), guest.get()},
        [&](std::size_t i, const auto &events) {
          watch(i, events);
          guest->SendLocalState(proto::PlayerState{.position = {3.f, 2.f, 1.f}});
          for (const auto &event : events)
            if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 0)
              seen = remote->state;
          return seen.has_value() || ended || left;
        },
        10s));
    CHECK_FALSE(ended);
    CHECK_FALSE(left);
    REQUIRE(seen);
    CHECK(seen->player == you);
    CHECK(guest->You() == you);
    CHECK(host->Relay()->state == net::RelayStatus::State::Ready);

    // The code players were given still leads into the session.
    auto late = byCode;
    auto third = Endpoint::Join({"late"}, JoinTarget{.relay = late}, error);
    REQUIRE_MESSAGE(third, error);
    REQUIRE(AllJoined({host.get(), guest.get(), third.get()}));
    CHECK(host->Relay()->code == byCode.code);
  }

  TEST_CASE("through the relay, host and guest find a direct path, and fall back to the relay") {
    NEEDS_RELAY(relay);
    std::string error;
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, relay);
    REQUIRE_MESSAGE(host, error);
    auto byCode = *relay;
    byCode.code = CodeOf(*host);
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));

    // Introduced by the relay, they punch through (on 127.0.0.1, at once).
    REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t, const auto &) {
      return guest->Relay()->direct == 1 && host->Relay()->direct == 1;
    }));

    bool ended = false, left = false;
    // States flow (direct), and keep flowing once the path is gone.
    const auto flows = [&](std::chrono::milliseconds limit) {
      std::optional<proto::PlayerState> seen;
      Pump(
          {host.get(), guest.get()},
          [&](std::size_t i, const std::vector<SessionEvent> &events) {
            guest->SendLocalState(proto::PlayerState{.position = {1.f, 1.f, 1.f}});
            for (const auto &event : events) {
              ended |= std::holds_alternative<events::Ended>(event);
              left |= std::holds_alternative<events::PlayerLeft>(event);
              if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 0)
                seen = remote->state;
            }
            return seen.has_value();
          },
          limit);
      return seen.has_value();
    };
    CHECK(flows(2s));

    // The host turns direct paths off (the settings page): its side stops at
    // once, the guest's goes silent and falls back to the relay, and the
    // session never notices.
    host->SetDirect(false);
    CHECK(host->Relay()->direct == 0);
    REQUIRE(
        Pump({host.get(), guest.get()}, [&](std::size_t, const std::vector<SessionEvent> &events) {
          for (const auto &event : events)
            ended |= std::holds_alternative<events::Ended>(event);
          return guest->Relay()->direct == 0 || ended;
        }));
    CHECK(flows(3s));
    CHECK_FALSE(ended);
    CHECK_FALSE(left);
    CHECK(guest->IsJoined());
  }

  TEST_CASE("a host that does not punch back: the guest stays on the relay, nothing lost") {
    NEEDS_RELAY(relay);
    std::string error;
    auto noDirect = *relay;
    noDirect.direct = false; // as a port that drops every punch
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, noDirect);
    REQUIRE_MESSAGE(host, error);
    auto byCode = *relay;
    byCode.code = CodeOf(*host);
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));

    // The guest punches for 3 s, unanswered; the session goes on meanwhile.
    std::optional<proto::PlayerState> seen;
    bool ended = false;
    Pump(
        {host.get(), guest.get()},
        [&](std::size_t i, const std::vector<SessionEvent> &events) {
          guest->SendLocalState(proto::PlayerState{.position = {2.f, 2.f, 2.f}});
          for (const auto &event : events) {
            ended |= std::holds_alternative<events::Ended>(event);
            if (const auto *remote = std::get_if<events::RemoteState>(&event); remote && i == 0)
              seen = remote->state;
          }
          return false;
        },
        4s);
    CHECK_FALSE(ended);
    REQUIRE(seen);
    CHECK(guest->Relay()->direct == 0);
    CHECK(host->Relay()->direct == 0);
    CHECK(guest->ThroughRelay());
  }

  TEST_CASE("the connection check: what two relay ports saw of us") {
    NEEDS_RELAY(relay);
    std::string error;
    // TANDEM_RELAY's next port is the fleet's other replica: two relay
    // ports answer, and on 127.0.0.1 nothing maps ports.
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, relay);
    REQUIRE_MESSAGE(host, error);
    REQUIRE(Pump({host.get()}, [&](std::size_t, const auto &) {
      return host->Relay()->nat != net::NatMapping::Unknown;
    }));
    const auto status = *host->Relay();
    CHECK(status.nat == net::NatMapping::Stable);
    CHECK(net::DirectOutlook(status) == "direct");
    CHECK(status.portKept);
    CHECK(status.publicAddress.starts_with("127.0.0.1:"));
  }

  TEST_CASE("several relays: a host takes the one that answers, a guest asks each in turn") {
    const auto fleet = TestFleet();
    const char *binary = std::getenv("TANDEM_RELAY_BIN");
    if (!fleet || !binary) {
      MESSAGE("TANDEM_RELAY_FLEET or TANDEM_RELAY_BIN unset: skipped");
      return;
    }
    std::string error;
    // The first relay listed answers nothing: the host registers on the next.
    auto options = net::RelayOptions{.address = "127.0.0.1", .port = NextPort()};
    options.more = {AddressOf(fleet->replicas[1])};
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, options);
    REQUIRE_MESSAGE(host, error);
    const auto code = CodeOf(*host, 5s);
    REQUIRE(code.size() == net::kJoinCodeSize);
    CHECK(host->Relay()->at == AddressOf(fleet->replicas[1]));

    // A relay of another network (it does not know the code), then the fleet.
    RelayProcess other(binary, NextPort());
    REQUIRE(other.Start());
    std::this_thread::sleep_for(300ms);
    auto byCode = other.Options();
    byCode.code = code;
    byCode.more = {AddressOf(fleet->replicas[0])};
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}, 8s));
    CHECK(guest->Relay()->at == AddressOf(fleet->replicas[1]));
  }

  TEST_CASE("a kept code: the same next session, another on asking") {
    NEEDS_RELAY(relay);
    std::string error;
    auto keep = *relay;
    keep.keepCode = true;
    keep.codeSalt = 41;
    // A kept code is a token's (TANDEM_RELAY_TOKEN: one of the relay's).
    if (const char *token = std::getenv("TANDEM_RELAY_TOKEN"))
      if (const auto parsed = net::ParseRelayToken(token))
        keep.token = *parsed;
    std::string kept;
    {
      auto host = Endpoint::Host({"host"}, NextPort(), error, {}, keep);
      REQUIRE_MESSAGE(host, error);
      kept = CodeOf(*host);
      REQUIRE(kept.size() == net::kJoinCodeSize);
      CHECK(host->Relay()->codeKept);
      CHECK(host->Relay()->codeSecondsLeft == 0);
    }
    // The host comes back (its old session still on the relay): its code too.
    auto host = Endpoint::Host({"host"}, NextPort(), error, {}, keep);
    REQUIRE_MESSAGE(host, error);
    CHECK(CodeOf(*host) == kept);
    auto byCode = *relay;
    byCode.code = kept;
    auto guest = Endpoint::Join({"guest"}, JoinTarget{.relay = byCode}, error);
    REQUIRE_MESSAGE(guest, error);
    REQUIRE(AllJoined({host.get(), guest.get()}));

    // New code: the next salt, at once, the session untouched.
    host->NewRelayCode();
    std::string next;
    REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t, const auto &) {
      next = host->Relay()->code;
      return !next.empty() && next != kept;
    }));
    CHECK(host->Relay()->codeKept);
    CHECK(host->Relay()->codeSalt == 42);
    CHECK(guest->IsJoined());
  }
}
