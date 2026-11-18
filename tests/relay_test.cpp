// Sessions through tandem-relay. The "relay" suite needs a real relay
// process: tests/relay_loopback.sh starts one on 127.0.0.1 and runs it with
// TANDEM_RELAY set (without it those cases are skipped). The rest needs none.

#include "ports.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>

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

// The host's join code, once the relay gave it.
std::string CodeOf(Endpoint &host) {
  std::string code;
  Pump({&host}, [&](std::size_t, const auto &) {
    if (const auto relay = host.Relay())
      code = relay->code;
    return !code.empty();
  });
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
}
