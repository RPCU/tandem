// Police (docs/architecture/police.md): heat is per player. A real host and
// guests over loopback UDP: who hears whose stars, late joiners, a wanted
// player that leaves, vanishes or comes back, crimes told to the offender
// alone, and digests that heal a lost change.

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

// The stars each game shows for the others, from its events alone.
struct Stars {
  std::map<proto::PlayerId, proto::Heat> of;
  int events{};
  std::vector<proto::Crime> crimes;

  void See(const std::vector<SessionEvent> &all) {
    for (const auto &event : all) {
      if (const auto *heat = std::get_if<events::RemoteHeat>(&event)) {
        ++events;
        if (heat->heat.stage == 0)
          of.erase(heat->heat.player);
        else
          of[heat->heat.player] = heat->heat;
      } else if (const auto *crime = std::get_if<events::CrimeReported>(&event)) {
        crimes.push_back(crime->crime);
      }
    }
  }
  std::uint8_t Stage(proto::PlayerId player) const {
    const auto it = of.find(player);
    return it == of.end() ? std::uint8_t{0} : it->second.stage;
  }
};

struct Three {
  std::unique_ptr<Endpoint> host, a, b;
  std::array<Stars, 3> stars;
  std::vector<Endpoint *> All() const {
    std::vector<Endpoint *> out{host.get()};
    if (a)
      out.push_back(a.get());
    if (b)
      out.push_back(b.get());
    return out;
  }
  bool Until(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    // Indexes follow All(): with a gone, b is second.
    const bool noA = !a;
    return Pump(
        All(),
        [&](std::size_t i, const auto &events) {
          stars[i == 1 && noA ? 2 : i].See(events);
          return done();
        },
        limit);
  }
};

Three Session(const Timing &timing) {
  const auto port = NextPort();
  std::string error;
  Three s;
  s.host = Endpoint::Host({"host"}, port, error, timing);
  s.a = Endpoint::Join({"a"}, "127.0.0.1", port, error, timing);
  s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, timing);
  REQUIRE((s.host && s.a && s.b));
  REQUIRE(s.Until([&] { return s.a->Players().size() == 3 && s.b->Players().size() == 3; }));
  return s;
}
} // namespace

TEST_CASE("heat is per player: only the offender is wanted, and everyone knows who is") {
  auto s = Session(kQuick);
  const auto a = s.a->You(), b = s.b->You();
  // The guest commits a crime: two stars, the police see it.
  s.a->SetLocalHeat({.player = 9, .stage = 2, .state = proto::HeatState::Active});
  REQUIRE(s.Until([&] { return s.stars[0].Stage(a) == 2 && s.stars[2].Stage(a) == 2; }));
  // Its own stars are its game's: never told back, and nobody else is wanted.
  CHECK(s.stars[1].events == 0);
  CHECK(s.stars[0].of.size() == 1);
  CHECK(s.stars[0].of.at(a).state == proto::HeatState::Active);
  CHECK(s.host->Heats().size() == 1);
  CHECK(s.b->Heats().contains(a));
  CHECK(s.a->Heats().contains(a)); // us included
  CHECK_FALSE(s.host->Heats().contains(s.host->You()));

  // The same level again is not news; the police losing sight is.
  const auto heard = s.stars[0].events;
  s.a->SetLocalHeat({.stage = 2, .state = proto::HeatState::Active});
  s.Until([] { return false; }, 150ms);
  CHECK(s.stars[0].events == heard);
  s.a->SetLocalHeat({.stage = 2, .state = proto::HeatState::Searching});
  REQUIRE(s.Until([&] { return s.stars[2].of.at(a).state == proto::HeatState::Searching; }));

  // The host shoots a cop too: its own stars, next to the guest's.
  s.host->SetLocalHeat({.stage = 1, .state = proto::HeatState::Active});
  REQUIRE(s.Until([&] { return s.stars[1].Stage(1) == 1 && s.stars[2].Stage(1) == 1; }));
  CHECK(s.stars[2].Stage(a) == 2);
  CHECK(s.stars[0].Stage(b) == 0);

  // A late joiner hears who is wanted now.
  std::string error;
  auto late = Endpoint::Join({"late"}, "127.0.0.1", s.host->LocalPort(), error, kQuick);
  REQUIRE(late);
  Stars atLate;
  const std::vector<Endpoint *> four{s.host.get(), s.a.get(), s.b.get(), late.get()};
  REQUIRE(Pump(four, [&](std::size_t i, const auto &events) {
    (i < 3 ? s.stars[i] : atLate).See(events);
    return atLate.Stage(1) == 1 && atLate.Stage(a) == 2;
  }));
  CHECK(atLate.of.at(a).state == proto::HeatState::Searching);
  CHECK(atLate.Stage(b) == 0);

  // The guest shakes them off: no longer wanted, for everyone.
  s.a->SetLocalHeat({});
  REQUIRE(Pump(four, [&](std::size_t i, const auto &events) {
    (i < 3 ? s.stars[i] : atLate).See(events);
    return atLate.Stage(a) == 0 && s.stars[0].Stage(a) == 0 && s.stars[2].Stage(a) == 0;
  }));
  CHECK(atLate.Stage(1) == 1);
  CHECK_FALSE(s.host->Heats().contains(a));
  CHECK(late->Checks().Requests() == 0);
  CHECK(s.a->Checks().Requests() == 0);
}

TEST_CASE("a crime seen by the area's owner is told to the offender alone") {
  auto s = Session(kQuick);
  const auto a = s.a->You();
  const proto::Crime witnessed{
      .player = a, .kind = proto::CrimeKind::Witness, .npc = 7, .position = {1, 2, 3}};
  CHECK(s.host->SendCrime(witnessed));
  // The host's own V's crimes are its game's; a player not here gets none;
  // a guest owns no area today and reports nothing.
  CHECK_FALSE(s.host->SendCrime({.player = 1, .kind = proto::CrimeKind::Witness}));
  CHECK_FALSE(s.host->SendCrime({.player = 9, .kind = proto::CrimeKind::Witness}));
  CHECK_FALSE(s.b->SendCrime({.player = a, .kind = proto::CrimeKind::Kill}));
  REQUIRE(s.Until([&] { return !s.stars[1].crimes.empty(); }));
  s.Until([] { return false; }, 150ms);
  REQUIRE(s.stars[1].crimes.size() == 1);
  CHECK(s.stars[1].crimes.front() == witnessed);
  CHECK(s.stars[2].crimes.empty());
  CHECK(s.stars[0].crimes.empty());
  // A crime counts once however bad the link: it is never sent twice.
  CHECK(consistency::IsOneShot(proto::Crime{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Heat{}));
}

TEST_CASE("a wanted player that vanishes is no longer wanted; one that comes back starts clean") {
  auto s = Session(kFast);
  const auto a = s.a->You();
  s.a->SetLocalHeat({.stage = 4, .state = proto::HeatState::Active});
  s.host->SetLocalHeat({.stage = 1});
  REQUIRE(s.Until([&] { return s.stars[2].Stage(a) == 4 && s.stars[2].Stage(1) == 1; }));

  // Alt-F4 in the middle of a chase: no Bye. The host and b stop showing
  // its stars on the timeout alone; the host's own stay.
  s.a.reset();
  REQUIRE(s.Until([&] { return s.stars[0].Stage(a) == 0 && s.stars[2].Stage(a) == 0; }, 4s));
  CHECK_FALSE(s.host->Heats().contains(a));
  CHECK_FALSE(s.b->Heats().contains(a));
  CHECK(s.stars[2].Stage(1) == 1);
  CHECK(s.b->Players().size() == 2);

  // b drops its link and is back before the timeout under the same name: a
  // fresh game, not wanted until it says so.
  s.b->SetLocalHeat({.stage = 3, .state = proto::HeatState::Active});
  const auto b = s.b->You();
  REQUIRE(s.Until([&] { return s.stars[0].Stage(b) == 3; }));
  const auto port = s.host->LocalPort();
  s.b.reset();
  Pump({s.host.get()}, [](std::size_t, const auto &) { return false; }, 500ms); // past takeover
  std::string error;
  s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(s.b);
  bool returned = false;
  REQUIRE(Pump({s.host.get(), s.b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0) {
      s.stars[0].See(events);
      for (const auto &event : events)
        returned |= std::holds_alternative<events::PlayerReturned>(event);
    }
    return returned && s.b->IsJoined() && s.stars[0].Stage(b) == 0;
  }));
  CHECK(s.b->You() == b);
  CHECK_FALSE(s.host->Heats().contains(b));
}

TEST_CASE("a guest that missed a change of heat is healed by the digests") {
  auto s = Session(kQuick);
  const auto a = s.a->You();
  s.a->SetLocalHeat({.stage = 3, .state = proto::HeatState::Active});
  REQUIRE(s.Until([&] { return s.stars[2].Stage(a) == 3; }));
  // The host relays nothing for a while: b misses that a is clean again and
  // that the host is wanted.
  s.host->SetFaults({.drop = 1.0});
  s.a->SetLocalHeat({});
  s.host->SetLocalHeat({.stage = 2, .state = proto::HeatState::Active});
  s.Until([] { return false; }, 150ms);
  CHECK(s.stars[2].Stage(a) == 3);
  s.host->SetFaults({});
  CHECK(s.Until([&] { return s.stars[2].Stage(a) == 0 && s.stars[2].Stage(1) == 2; }));
  CHECK(s.b->Checks().Requests() >= 1);
  CHECK(s.stars[1].Stage(1) == 2); // a too
}
