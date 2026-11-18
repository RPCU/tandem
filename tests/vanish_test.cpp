// A player can vanish at any moment (Alt-F4, a crash, power or network
// gone): no Bye, no disconnect. Whatever it was doing must end cleanly for
// the ones left (docs/ARCHITECTURE.md, Consistency). A real host and guests
// over loopback UDP; "vanish" is the endpoint destroyed without Leave().

#include "ports.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

// The liveness rules scaled down (play: 5 s).
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  std::vector<Endpoint *> All() const {
    std::vector<Endpoint *> out{host.get()};
    if (a)
      out.push_back(a.get());
    if (b)
      out.push_back(b.get());
    return out;
  }
};

Session Three() {
  const auto port = NextPort();
  std::string error;
  Session s;
  s.host = Endpoint::Host({"host"}, port, error, kFast);
  s.a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE((s.host && s.a && s.b));
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) {
    return s.a->Players().size() == 3 && s.b->Players().size() == 3;
  }));
  return s;
}
} // namespace

TEST_CASE("a guest that vanishes paused, in a Sandevistan and holding an NPC frees all three") {
  auto s = Three();
  const auto gone = s.a->You();
  s.a->SetLocalPaused(true);
  s.a->SetLocalTimeEffect({.abilities = proto::kTimeSandevistan, .scale = 0.25f});
  s.host->SendNpcSpawn({.npc = 7, .record = 1});
  s.host->SendNpcHold({.npc = 7, .player = gone, .how = proto::NpcHoldKind::Grappled});
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) {
    return s.b->Paused().contains(gone) && s.b->TimeEffects().contains(gone) &&
           s.b->NpcHolds().contains(7);
  }));

  s.a.reset(); // no goodbye
  bool unpaused = false, timeBack = false, released = false, left = false, hostLeft = false;
  REQUIRE(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events) {
      if (i == 0) {
        hostLeft |= std::holds_alternative<events::PlayerLeft>(event);
        continue;
      }
      if (const auto *pause = std::get_if<events::PauseChanged>(&event))
        unpaused |= pause->pause.player == gone && !pause->pause.paused;
      if (const auto *time = std::get_if<events::RemoteTimeEffect>(&event))
        timeBack |= time->effect.player == gone && time->effect.abilities == 0;
      if (const auto *hold = std::get_if<events::HostNpcHold>(&event))
        released |= hold->hold.npc == 7 && hold->hold.player == proto::kNoPlayer;
      if (const auto *l = std::get_if<events::PlayerLeft>(&event))
        left |= l->player == gone;
    }
    return unpaused && timeBack && released && left && hostLeft;
  }));
  CHECK(s.b->Paused().empty());
  CHECK(s.b->TimeEffects().empty());
  CHECK(s.b->NpcHolds().empty());
  CHECK(s.host->NpcHolds().empty());
}

TEST_CASE("a reviver that vanishes mid-revive: the downed V hears the revive is off") {
  auto s = Three();
  const auto reviver = s.a->You();
  s.b->SetLocalVitals({.life = proto::Life::Down});
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) {
    return s.a->Lives().contains(s.b->You()) && s.host->Lives().contains(s.b->You());
  }));
  s.a->SendRevive({.target = s.b->You(), .step = proto::ReviveStep::Start});
  bool started = false;
  REQUIRE(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *revive = std::get_if<events::ReviveBy>(&event); revive && i == 2)
        started |= revive->revive.step == proto::ReviveStep::Start;
    return started;
  }));

  s.a.reset();
  bool cancelled = false, lost = false;
  REQUIRE(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events) {
      if (const auto *revive = std::get_if<events::ReviveBy>(&event); revive && i == 1)
        cancelled |=
            revive->revive.player == reviver && revive->revive.step == proto::ReviveStep::Cancel;
      lost |= std::holds_alternative<events::SessionLost>(event);
    }
    return cancelled;
  }));
  // The host's V is still up: nobody lost anything.
  CHECK_FALSE(lost);
}

TEST_CASE("the last V up vanishes: no V is up, the session is lost for the ones down") {
  auto s = Three();
  s.host->SetLocalVitals({.life = proto::Life::Down});
  s.b->SetLocalVitals({.life = proto::Life::Down});
  bool lostEarly = false;
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &events) {
    for (const auto &event : events)
      lostEarly |= std::holds_alternative<events::SessionLost>(event);
    return s.host->Lives().contains(s.b->You()) && s.b->Lives().contains(1);
  }));
  CHECK_FALSE(lostEarly); // a is up

  s.a.reset();
  bool hostLost = false, guestLost = false;
  CHECK(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (std::holds_alternative<events::SessionLost>(event))
        (i == 0 ? hostLost : guestLost) = true;
    return hostLost && guestLost;
  }));
}

TEST_CASE("a downed guest that vanishes is forgotten: the others are not lost, its life is gone") {
  auto s = Three();
  const auto gone = s.a->You();
  s.a->SetLocalVitals({.life = proto::Life::Down});
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) { return s.b->Lives().contains(gone); }));
  s.a.reset();
  bool lost = false;
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &events) {
    for (const auto &event : events)
      lost |= std::holds_alternative<events::SessionLost>(event);
    return s.b->Players().size() == 2;
  }));
  CHECK_FALSE(lost);
  CHECK_FALSE(s.b->Lives().contains(gone));
  CHECK_FALSE(s.host->Lives().contains(gone));
}

TEST_CASE("the host vanishes: every guest's session ends within the timeout, whatever it held") {
  auto s = Three();
  s.host->SetLocalPaused(true);
  s.host->SetLocalTimeEffect({.abilities = proto::kTimeSandevistan, .scale = 0.5f});
  s.b->SetLocalVitals({.life = proto::Life::Down});
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) { return s.a->Paused().contains(1); }));
  s.host.reset();
  bool aEnded = false, bEnded = false;
  std::vector<Endpoint *> guests{s.a.get(), s.b.get()};
  REQUIRE(Pump(guests, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (std::holds_alternative<events::Ended>(event))
        (i == 0 ? aEnded : bEnded) = true;
    return aEnded && bEnded;
  }));
}
