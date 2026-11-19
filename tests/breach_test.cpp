// Breach Protocol between players (docs/architecture/devices.md, "Breach
// Protocol"): the host's record of who breached what on its own, then a
// real host and real guests over loopback UDP, in one process. Each game is
// played by the test: it applies the breaches it hears, as
// scripts/tandem/Breach.reds does.

#include "ports.hpp"
#include "tandem/devices.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr std::uint64_t kAp = 0x0A0B0C0D0E0F1011ULL, kApClass = 0xACCE55ull;
constexpr std::uint64_t kCameraShutdown = 0xC0FFEE01ull, kLowerIce = 0x1CE0002ull,
                        kDataMine = 0xDA7A0003ull, kSquadBuff = 0x7A900004ull;
const devices::Key kApKey{kAp, kApClass};

proto::Breach Jack(std::vector<std::uint64_t> programs = {kCameraShutdown, kLowerIce, kDataMine}) {
  return {.entity = kAp, .cls = kApClass, .programs = std::move(programs)};
}

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

// What a game does with the breach events of its endpoint.
struct Game {
  std::vector<proto::Breach> applied; // breaches it ran on its own world
  std::vector<proto::BreachResult> answers;
  std::map<devices::Key, devices::What> devices; // the host's word on its copies
  int left{};

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *by = std::get_if<events::BreachBy>(&event))
        applied.push_back(by->breach);
      else if (const auto *answer = std::get_if<events::BreachAnswered>(&event))
        answers.push_back(answer->result);
      else if (const auto *word = std::get_if<events::HostDevice>(&event))
        devices[devices::KeyOf(word->state)] = devices::WhatOf(word->state);
      else if (std::holds_alternative<events::PlayerLeft>(event))
        ++left;
    }
  }
};

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game hostGame, aGame, bGame;

  bool Run(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    std::vector<Endpoint *> all{host.get()};
    std::vector<Game *> games{&hostGame};
    if (a) {
      all.push_back(a.get());
      games.push_back(&aGame);
    }
    if (b) {
      all.push_back(b.get());
      games.push_back(&bGame);
    }
    return Pump(
        all,
        [&](std::size_t i, const auto &events) {
          games[i]->See(events);
          return done();
        },
        limit);
  }
  void RunFor(std::chrono::milliseconds time) {
    Run([] { return false; }, time);
  }
};

Session Start(bool two, Timing timing = kQuick) {
  const auto port = NextPort();
  std::string error;
  Session s;
  s.host = Endpoint::Host({"host"}, port, error, timing);
  s.a = Endpoint::Join({"a"}, "127.0.0.1", port, error, timing);
  if (two)
    s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, timing);
  REQUIRE((s.host && s.a && (!two || s.b)));
  REQUIRE(s.Run([&] {
    return s.a->Players().size() == (two ? 3u : 2u) && (!two || s.b->Players().size() == 3u);
  }));
  return s;
}
} // namespace

TEST_CASE("a network is breached once: the first breacher's, whoever asks again") {
  devices::Breaches breaches;
  devices::Ledger record;
  using V = proto::BreachVerdict;
  CHECK(breaches.Judge(kApKey, 2, record) == V::Granted);
  breaches.Note(kApKey, 2);
  CHECK(breaches.Judge(kApKey, 3, record) == V::Breached);
  CHECK(breaches.Judge(kApKey, 2, record) == V::Breached); // not twice either
  CHECK(breaches.By(kApKey, record) == 2);
  // Another network, breached in the host's world before anyone asked.
  const devices::Key other{kAp + 1, kApClass};
  record.Report({.entity = other.entity,
                 .cls = other.cls,
                 .status = proto::kDeviceOn,
                 .flags = proto::kDeviceBreached});
  CHECK(breaches.Judge(other, 2, record) == V::Breached);
  CHECK(breaches.By(other, record) == proto::kNoPlayer);
  // One the breacher's own copy reported first (its DeviceUse came first).
  const devices::Key third{kAp + 2, kApClass};
  record.Report({.entity = third.entity,
                 .cls = third.cls,
                 .flags = proto::kDeviceBreached | proto::kDeviceExposed,
                 .by = 3});
  CHECK(breaches.Judge(third, 3, record) == V::Granted);
  CHECK(breaches.Judge(third, 2, record) == V::Breached);
  // An NPC through the quickhack: once too.
  CHECK(breaches.JudgeNpc(7) == V::Granted);
  breaches.NoteNpc(7, 2);
  CHECK(breaches.JudgeNpc(7) == V::Breached);
  CHECK(breaches.ByNpc(7) == 2);
  // The access point's record after a breach: breached and open, by whom.
  const auto state = devices::BreachedState(kApKey, 2, nullptr);
  CHECK(state.flags == (proto::kDeviceBreached | proto::kDeviceExposed));
  CHECK(state.by == 2);
  CHECK(state.status == proto::kDeviceOn);
}

TEST_CASE("a guest's breach: the host applies its daemons, every game hears the network") {
  auto s = Start(true);
  auto breach = Jack();
  breach.traps = {kSquadBuff};
  s.a->SendBreach(breach);
  REQUIRE(s.Run([&] {
    return !s.aGame.answers.empty() && !s.hostGame.applied.empty() && !s.bGame.applied.empty() &&
           s.bGame.devices.contains(kApKey);
  }));
  // The breacher hears it stands; its own game ran the minigame and its
  // rewards, and is never asked to apply its own breach.
  CHECK(s.aGame.answers[0].verdict == proto::BreachVerdict::Granted);
  CHECK(s.aGame.answers[0].by == s.a->You());
  CHECK(s.aGame.applied.empty());
  // The host's game runs the daemons on the network's devices and its NPCs,
  // from the breacher's puppet; the other guest on the NPCs it runs.
  for (const auto *game : {&s.hostGame, &s.bGame}) {
    REQUIRE(game->applied.size() == 1);
    const auto &applied = game->applied[0];
    CHECK(applied.player == s.a->You());
    CHECK(applied.entity == kAp);
    CHECK(applied.programs == breach.programs);
    CHECK(applied.traps == breach.traps);
    CHECK((applied.flags & proto::kBreachApply) != 0);
  }
  // Breached for everyone: the access point's record says so, by whom.
  const auto *record = s.host->Devices().Find(kApKey);
  REQUIRE(record);
  CHECK((record->flags & proto::kDeviceBreached) != 0);
  CHECK((record->flags & proto::kDeviceExposed) != 0);
  CHECK(record->by == s.a->You());
  CHECK((s.bGame.devices[kApKey].flags & proto::kDeviceBreached) != 0);
  REQUIRE(s.Run([&] { return s.aGame.devices.contains(kApKey); }));
  s.RunFor(300ms);
  CHECK(s.a->Devices().Digest() == s.host->Devices().Digest());
  CHECK(s.b->Devices().Digest() == s.host->Devices().Digest());
  CHECK(s.host->BreachesRefused() == 0);
}

TEST_CASE("a second breach of the same network is refused, and names the first breacher") {
  auto s = Start(true);
  s.a->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.aGame.answers.empty() && !s.hostGame.applied.empty(); }));
  // b jacked in before it heard: refused, its daemons never run anywhere.
  s.b->SendBreach(Jack({kLowerIce}));
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Breached);
  CHECK(s.bGame.answers[0].by == s.a->You());
  s.RunFor(100ms);
  CHECK(s.hostGame.applied.size() == 1);
  CHECK(s.aGame.applied.empty());
  CHECK(s.host->BreachesRefused() == 1);
  // The host's own V cannot either.
  s.host->SendBreach(Jack({kLowerIce}));
  REQUIRE(s.Run([&] { return !s.hostGame.answers.empty(); }));
  CHECK(s.hostGame.answers[0].verdict == proto::BreachVerdict::Breached);
  CHECK(s.host->BreachesRefused() == 2);
}

TEST_CASE("the host's own breach goes to every guest for their NPCs, not to its own game") {
  auto s = Start(true);
  s.host->SendBreach(Jack());
  REQUIRE(s.Run([&] { return s.aGame.applied.size() == 1 && s.bGame.applied.size() == 1; }));
  REQUIRE(s.hostGame.answers.size() == 1);
  CHECK(s.hostGame.answers[0].verdict == proto::BreachVerdict::Granted);
  CHECK(s.hostGame.applied.empty()); // its minigame ran it all here
  CHECK(s.aGame.applied[0].player == s.host->You());
  CHECK((s.host->Devices().Find(kApKey)->flags & proto::kDeviceBreached) != 0);
}

TEST_CASE("a late joiner finds the network breached, with the breacher's name") {
  auto s = Start(false);
  s.a->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.aGame.answers.empty(); }));
  std::string error;
  s.b = Endpoint::Join({"late"}, "127.0.0.1", s.host->LocalPort(), error, kQuick);
  REQUIRE(s.b);
  REQUIRE(s.Run([&] { return s.bGame.devices.contains(kApKey); }));
  CHECK((s.bGame.devices[kApKey].flags & proto::kDeviceBreached) != 0);
  CHECK(s.b->Devices().Find(kApKey)->by == s.a->You());
  // The breach itself is not replayed to it: its NPCs are the host's
  // (statuses) and its devices the record's.
  s.RunFor(200ms);
  CHECK(s.bGame.applied.empty());
  // Nor can it breach the network again.
  s.b->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Breached);
}

TEST_CASE("a breach of the host's NPC through the quickhack goes to its owner, once") {
  auto s = Start(true);
  s.host->SendNpcSpawn({.npc = 7, .record = 1});
  REQUIRE(s.Run([&] { return s.a->SharedNpcs().contains(7); }));
  // No network (the NPC's own squad): the NPC alone, for its owner.
  s.a->SendBreach({.npc = 7, .programs = {kLowerIce}, .flags = proto::kBreachRemote});
  REQUIRE(s.Run([&] { return !s.aGame.answers.empty() && !s.hostGame.applied.empty(); }));
  CHECK(s.aGame.answers[0].verdict == proto::BreachVerdict::Granted);
  CHECK(s.hostGame.applied[0].npc == 7);
  CHECK(s.hostGame.applied[0].player == s.a->You());
  CHECK((s.hostGame.applied[0].flags & proto::kBreachRemote) != 0);
  s.RunFor(100ms);
  CHECK(s.bGame.applied.empty()); // not b's NPC, no network
  // Its network, found by the host's game (the NPC's device link): the
  // host says it for the breacher, and applies it itself.
  s.host->SendBreach(
      {.player = s.a->You(), .entity = kAp, .cls = kApClass, .programs = {kLowerIce}});
  REQUIRE(s.Run([&] { return s.hostGame.applied.size() == 2 && s.bGame.applied.size() == 1; }));
  CHECK(s.hostGame.applied[1].entity == kAp);
  CHECK(s.host->Devices().Find(kApKey)->by == s.a->You());
  CHECK(s.hostGame.answers.empty()); // nobody asked our game
  // b uploads Breach Protocol to the same NPC: it was breached already.
  s.b->SendBreach({.npc = 7, .programs = {kLowerIce}, .flags = proto::kBreachRemote});
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Breached);
  CHECK(s.bGame.answers[0].by == s.a->You());
}

TEST_CASE("a lost minigame alerts the network's NPCs, and breaches nothing") {
  auto s = Start(true);
  s.a->SendBreach({.entity = kAp, .cls = kApClass, .flags = proto::kBreachFailed});
  REQUIRE(s.Run([&] { return s.hostGame.applied.size() == 1 && s.bGame.applied.size() == 1; }));
  CHECK((s.hostGame.applied[0].flags & proto::kBreachFailed) != 0);
  CHECK(s.host->Devices().Find(kApKey) == nullptr);
  CHECK(s.aGame.answers.empty());
  // It can still be breached, by anyone.
  s.b->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Granted);
}

TEST_CASE("a breacher that vanishes mid-breach: the network is the next player's to breach") {
  auto s = Start(true, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  // a sits in the minigame (busy), then is gone without a goodbye: its
  // breach was never won, nothing was sent.
  s.a->SetLocalActivity({.kind = proto::ActivityKind::Minigame});
  REQUIRE(s.Run([&] { return s.b->Activities().contains(s.a->You()); }));
  const auto aId = s.a->You();
  s.a.reset();
  REQUIRE(s.Run([&] { return s.hostGame.left == 1 && s.bGame.left == 1; }));
  CHECK_FALSE(s.b->Activities().contains(aId));
  s.b->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty() && !s.hostGame.applied.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Granted);
}

TEST_CASE("a breacher that vanishes after its breach: the network stays breached") {
  auto s = Start(true, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  s.a->SendBreach(Jack());
  s.a->Update(20ms);
  const auto aId = s.a->You();
  s.a.reset(); // gone before the host's game ran the daemons
  REQUIRE(s.Run([&] { return !s.hostGame.applied.empty() && s.hostGame.left == 1; }));
  // What a vanished player did stays done: the record is the world's.
  CHECK(s.host->Devices().Find(kApKey)->by == aId);
  s.b->SendBreach(Jack());
  REQUIRE(s.Run([&] { return !s.bGame.answers.empty(); }));
  CHECK(s.bGame.answers[0].verdict == proto::BreachVerdict::Breached);
}
