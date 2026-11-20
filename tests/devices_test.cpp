// Devices and world objects (docs/architecture/devices.md): the session's
// record on its own, then a real host and real guests over loopback UDP,
// in one process. The "game" of each side is played by the test: it reports
// what its devices are and applies what it hears.

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/devices.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr std::uint64_t kDoor = 0x1122334455667788ULL, kDoorClass = 0xD00Dull;
constexpr std::uint64_t kLift = 0x99AABBCCDDEEFF00ULL, kLiftClass = 0x11F7ull;

proto::DeviceState Door(std::uint16_t flags, proto::PlayerId by = proto::kNoPlayer) {
  return {.entity = kDoor,
          .cls = kDoorClass,
          .status = proto::kDeviceOn,
          .flags = flags,
          .value = proto::kDeviceNoValue,
          .by = by};
}

proto::DeviceUse UseDoor(std::uint16_t flags) {
  return {.entity = kDoor, .cls = kDoorClass, .status = proto::kDeviceOn, .flags = flags};
}

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

// What a game does with the device events of its endpoint. A guest's copy
// of each device follows the host's word; the host's game makes its own
// device what a guest asked (unless `refuse`: the game would not, a sealed
// door) and reports what it is now, as scripts/tandem/Devices.reds does.
struct Game {
  Endpoint *endpoint{};
  std::map<devices::Key, devices::What> world; // our game's own devices
  bool refuse{};
  int words{}, quiet{}, asked{}, requested{}, resets{}, resyncs{};
  proto::PlayerId lastBy{};

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *word = std::get_if<events::HostDevice>(&event)) {
        ++words;
        quiet += word->state.how == proto::kDeviceQuiet ? 1 : 0;
        lastBy = word->state.by;
        world[devices::KeyOf(word->state)] = devices::WhatOf(word->state);
      } else if (const auto *ask = std::get_if<events::DeviceAsked>(&event)) {
        ++asked;
        // Our game has it: this is what it is here.
        const auto key = devices::KeyOf(ask->state);
        if (const auto it = world.find(key); it != world.end())
          endpoint->ReportDevice({.entity = key.entity,
                                  .cls = key.cls,
                                  .status = it->second.status,
                                  .flags = it->second.flags,
                                  .value = it->second.value});
      } else if (const auto *use = std::get_if<events::DeviceRequested>(&event)) {
        ++requested;
        const auto key = devices::KeyOf(use->use);
        if (!refuse)
          world[key] = devices::WhatOf(use->use);
        const auto &now = world[key];
        endpoint->ReportDevice({.entity = key.entity,
                                .cls = key.cls,
                                .status = now.status,
                                .flags = now.flags,
                                .value = now.value,
                                .by = use->use.player});
      } else if (std::holds_alternative<events::DevicesReset>(event)) {
        ++resets;
      } else if (const auto *resync = std::get_if<events::Resync>(&event)) {
        resyncs += (resync->domains & proto::kDomainDevices) ? 1 : 0;
      }
    }
  }
};

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game hostGame, aGame, bGame;

  std::vector<Endpoint *> All() const {
    std::vector<Endpoint *> out{host.get()};
    if (a)
      out.push_back(a.get());
    if (b)
      out.push_back(b.get());
    return out;
  }
  // The endpoints in All()'s order see their events.
  bool Run(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    return Pump(
        All(),
        [&](std::size_t i, const auto &events) {
          Game *games[] = {&hostGame, a ? &aGame : &bGame, &bGame};
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
  s.hostGame.endpoint = s.host.get();
  s.aGame.endpoint = s.a.get();
  s.bGame.endpoint = s.b.get();
  REQUIRE(s.Run([&] {
    return s.a->Players().size() == (two ? 3u : 2u) && (!two || s.b->Players().size() == 3u);
  }));
  return s;
}

const devices::Key kDoorKey{kDoor, kDoorClass};
} // namespace

TEST_CASE("the device record counts changes, not repeats") {
  devices::Ledger ledger;
  const auto first = ledger.Report(Door(0));
  REQUIRE(first);
  CHECK(first->revision == 1);
  CHECK_FALSE(ledger.Report(Door(0)));
  // Who says it again does not make it a change.
  CHECK_FALSE(ledger.Report(Door(0, 3)));
  const auto opened = ledger.Report(Door(proto::kDeviceOpen, 2));
  REQUIRE(opened);
  CHECK(opened->revision == 2);
  CHECK(opened->by == 2);
  CHECK(ledger.Size() == 1);
  REQUIRE(ledger.Find(kDoorKey));
  CHECK(ledger.Find(kDoorKey)->flags == proto::kDeviceOpen);
}

TEST_CASE("a request stands when nobody else changed the device since the asker saw it") {
  devices::Ledger ledger;
  using Verdict = devices::Ledger::Verdict;
  auto use = UseDoor(proto::kDeviceOpen);
  use.player = 2;
  // Unknown to the session: granted, our game says what it is.
  CHECK(ledger.Judge(use) == Verdict::Grant);
  ledger.Report(Door(0)); // revision 1, closed
  use.seen = 1;
  CHECK(ledger.Judge(use) == Verdict::Grant);
  ledger.Report(Door(proto::kDeviceOpen, 2)); // revision 2, by player 2
  CHECK(ledger.Judge(use) == Verdict::Same);
  // Player 3 saw revision 1 and locks it: player 2 was first.
  auto late = UseDoor(proto::kDeviceLocked);
  late.player = 3;
  late.seen = 1;
  CHECK(ledger.Judge(late) == Verdict::Stale);
  // Player 2 closes it again before hearing of its own change: its own.
  auto again = UseDoor(0);
  again.player = 2;
  again.seen = 1;
  CHECK(ledger.Judge(again) == Verdict::Grant);
  late.seen = 2;
  CHECK(ledger.Judge(late) == Verdict::Grant);
}

TEST_CASE("a guest's copy takes the host's word once, and never an older one") {
  devices::Ledger host, guest;
  const auto first = *host.Report(Door(0));
  const auto second = *host.Report(Door(proto::kDeviceOpen, 2));
  CHECK(guest.Accept(second));
  CHECK_FALSE(guest.Accept(second)); // a repeat
  CHECK_FALSE(guest.Accept(first));  // older
  auto quiet = second;
  quiet.how = proto::kDeviceQuiet; // the same record, sent again in a resync
  CHECK_FALSE(guest.Accept(quiet));
  CHECK(guest.Digest() == host.Digest());
  host.Report({.entity = kLift, .cls = kLiftClass, .status = proto::kDeviceOn, .value = 3});
  CHECK(guest.Digest() != host.Digest());
}

TEST_CASE("the record is bounded") {
  devices::Ledger ledger;
  for (std::uint64_t i = 1; i <= devices::kMaxDevices; ++i)
    REQUIRE(ledger.Report({.entity = i, .cls = 1}));
  CHECK_FALSE(ledger.Report({.entity = devices::kMaxDevices + 1, .cls = 1}));
  proto::DeviceUse use{.entity = devices::kMaxDevices + 2, .cls = 1, .flags = proto::kDeviceOpen};
  CHECK(ledger.Judge(use) == devices::Ledger::Verdict::Full);
  // What it knows still changes.
  CHECK(ledger.Report({.entity = 1, .cls = 1, .flags = proto::kDeviceOpen}));
}

TEST_CASE("the device digest check asks after two differences in a row, then waits") {
  devices::DigestCheck check(1'000'000);
  const proto::DeviceDigest host{.count = 2, .hash = 7}, ours{.count = 1, .hash = 3};
  CHECK_FALSE(check.Compare(host, ours, 1'000));
  CHECK_FALSE(check.Compare(host, host, 2'000)); // it was a race
  CHECK_FALSE(check.Compare(host, ours, 3'000));
  CHECK(check.Compare(host, ours, 4'000));
  CHECK_FALSE(check.Compare(host, ours, 5'000));
  CHECK_FALSE(check.Compare(host, ours, 6'000)); // asked: cooling down
  CHECK(check.Compare(host, ours, 1'100'000));
  CHECK(check.Requests() == 2);
}

TEST_CASE("a door the host opens opens for every guest, and for a late joiner quietly") {
  auto s = Start(false);
  s.hostGame.world[kDoorKey] = {proto::kDeviceOn, proto::kDeviceOpen, proto::kDeviceNoValue};
  s.host->ReportDevice(Door(proto::kDeviceOpen, s.host->You()));
  REQUIRE(s.Run([&] { return s.aGame.words == 1; }));
  CHECK(s.aGame.quiet == 0); // it just happened: played the game's way
  CHECK(s.aGame.world[kDoorKey].flags == proto::kDeviceOpen);
  // Said again: nothing new for anyone.
  s.host->ReportDevice(Door(proto::kDeviceOpen, s.host->You()));
  s.RunFor(100ms);
  CHECK(s.aGame.words == 1);

  // Someone joins later: the door as it is, set, not played.
  std::string error;
  s.b = Endpoint::Join({"b"}, "127.0.0.1", s.host->LocalPort(), error, kQuick);
  REQUIRE(s.b);
  s.bGame.endpoint = s.b.get();
  REQUIRE(s.Run([&] { return s.bGame.words == 1; }));
  CHECK(s.bGame.quiet == 1);
  CHECK(s.bGame.world[kDoorKey].flags == proto::kDeviceOpen);
  // Everyone's record is the host's: the digests find nothing to fix.
  s.RunFor(400ms);
  CHECK(s.a->Devices().Digest() == s.host->Devices().Digest());
  CHECK(s.b->Devices().Digest() == s.host->Devices().Digest());
  CHECK(s.a->DeviceChecks().Requests() == 0);
  CHECK(s.b->DeviceChecks().Requests() == 0);
}

TEST_CASE("a guest operates a device: the host's game follows and everyone hears who did it") {
  auto s = Start(true);
  s.a->UseDevice(UseDoor(proto::kDeviceOpen));
  REQUIRE(s.Run([&] { return s.aGame.words == 1 && s.bGame.words == 1; }));
  CHECK(s.hostGame.requested == 1);
  CHECK(s.hostGame.world[kDoorKey].flags == proto::kDeviceOpen);
  CHECK(s.bGame.world[kDoorKey].flags == proto::kDeviceOpen);
  CHECK(s.bGame.lastBy == s.a->You());
  CHECK(s.host->DeviceStale() == 0);
  // What the session has already is not asked again.
  s.b->UseDevice(UseDoor(proto::kDeviceOpen));
  s.RunFor(100ms);
  CHECK(s.hostGame.requested == 1);
}

TEST_CASE("two guests operate one device at once: the first wins, the other gets it back") {
  auto s = Start(true);
  s.hostGame.world[kDoorKey] = {proto::kDeviceOn, 0, proto::kDeviceNoValue};
  s.host->ReportDevice(Door(0));
  REQUIRE(s.Run([&] { return s.aGame.words == 1 && s.bGame.words == 1; }));
  // Both saw revision 1: a opens it, b locks it, within a ping.
  s.a->UseDevice(UseDoor(proto::kDeviceOpen));
  s.b->UseDevice(UseDoor(proto::kDeviceLocked));
  REQUIRE(s.Run(
      [&] { return s.host->DeviceStale() == 1 && s.aGame.words >= 2 && s.bGame.words >= 2; }));
  CHECK(s.hostGame.requested == 1);
  // Whichever reached the host first: the door is that for everyone (the
  // other's own copy changed at once; the host's word puts it back).
  const auto won = s.hostGame.world[kDoorKey].flags;
  CHECK((won == proto::kDeviceOpen || won == proto::kDeviceLocked));
  CHECK(s.aGame.world[kDoorKey].flags == won);
  CHECK(s.bGame.world[kDoorKey].flags == won);
  s.RunFor(100ms);
  CHECK(s.a->Devices().Digest() == s.host->Devices().Digest());
  CHECK(s.b->Devices().Digest() == s.host->Devices().Digest());
  // Having seen it, the next request of either stands.
  s.b->UseDevice(UseDoor(0));
  REQUIRE(s.Run([&] { return s.hostGame.requested == 2 && s.aGame.world[kDoorKey].flags == 0; }));
  CHECK(s.host->DeviceStale() == 1);
}

TEST_CASE("a request the host's game refuses comes back to the asker, though nothing changed") {
  auto s = Start(false);
  s.hostGame.world[kDoorKey] = {proto::kDeviceOn, proto::kDeviceSealed, proto::kDeviceNoValue};
  s.host->ReportDevice(Door(proto::kDeviceSealed));
  REQUIRE(s.Run([&] { return s.aGame.words == 1; }));
  s.hostGame.refuse = true;                           // sealed: the game will not open it
  s.aGame.world[kDoorKey].flags = proto::kDeviceOpen; // the guest's copy did, at once
  s.a->UseDevice(UseDoor(proto::kDeviceOpen));
  REQUIRE(s.Run([&] { return s.aGame.words == 2; }));
  CHECK(s.aGame.world[kDoorKey].flags == proto::kDeviceSealed);
  CHECK(s.host->Devices().Find(kDoorKey)->revision == 1);
}

TEST_CASE("a device that comes into a guest's world: the host says what it is when they differ") {
  auto s = Start(false);
  // The host's save has the lift on floor 3; the guest's has it on 0.
  const devices::Key lift{kLift, kLiftClass};
  s.hostGame.world[lift] = {proto::kDeviceOn, 0, 3};
  s.a->AskDevice({.entity = kLift, .cls = kLiftClass, .status = proto::kDeviceOn, .value = 0});
  REQUIRE(s.Run([&] { return s.aGame.words == 1; }));
  CHECK(s.hostGame.asked == 1);
  CHECK(s.aGame.world[lift].value == 3);
  CHECK(s.aGame.quiet == 1); // it was like that: set, not played
  // Now known: the guest's game is told again without asking anyone (its
  // copy streamed out and in again, back at what its own save had).
  s.a->AskDevice({.entity = kLift, .cls = kLiftClass, .status = proto::kDeviceOn, .value = 0});
  REQUIRE(s.Run([&] { return s.aGame.words == 2; }));
  CHECK(s.aGame.quiet == 2);
  CHECK(s.hostGame.asked == 1);
  // The same as the session's: nothing to say.
  s.a->AskDevice({.entity = kLift, .cls = kLiftClass, .status = proto::kDeviceOn, .value = 3});
  s.RunFor(100ms);
  CHECK(s.aGame.words == 2);
  // A device the host's game does not have: no answer, the guest keeps its own.
  s.a->AskDevice({.entity = 42, .cls = 43, .status = proto::kDeviceOff});
  s.RunFor(100ms);
  CHECK(s.hostGame.asked == 2);
  CHECK(s.aGame.words == 2);
  CHECK(s.host->Devices().Size() == 1);
}

TEST_CASE("a guest converges on the host's devices although a third of them is lost") {
  auto s = Start(false);
  s.host->SetFaults({.drop = 0.33, .dup = 0.33, .seed = 5});
  s.a->SetFaults({.drop = 0.2, .seed = 9});
  for (std::uint64_t i = 1; i <= 60; ++i)
    s.host->ReportDevice({.entity = i, .cls = kDoorClass, .status = proto::kDeviceOn});
  for (std::uint64_t i = 1; i <= 60; i += 2)
    s.host->ReportDevice(
        {.entity = i, .cls = kDoorClass, .status = proto::kDeviceOn, .flags = proto::kDeviceOpen});
  s.RunFor(800ms);
  s.host->SetFaults({});
  s.a->SetFaults({});
  CHECK(s.Run(
      [&] {
        return s.a->Devices().Size() == 60 && s.a->Devices().Digest() == s.host->Devices().Digest();
      },
      5s));
  CHECK(s.aGame.resyncs > 0);
  CHECK(s.aGame.resets > 0);
  // What the guest's game shows is the host's, device by device.
  for (std::uint64_t i = 1; i <= 60; ++i)
    CHECK(s.aGame.world[{i, kDoorClass}].flags == (i % 2 ? proto::kDeviceOpen : 0));
  // Settled: more digests find nothing to fix.
  const auto requests = s.a->DeviceChecks().Requests();
  s.RunFor(500ms);
  CHECK(s.a->DeviceChecks().Requests() == requests);
}

TEST_CASE("a guest vanishes with a request in flight: the device is what the host has") {
  auto s = Start(true, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  s.hostGame.world[kDoorKey] = {proto::kDeviceOn, 0, proto::kDeviceNoValue};
  s.host->ReportDevice(Door(0));
  REQUIRE(s.Run([&] { return s.aGame.words == 1 && s.bGame.words == 1; }));
  // a opens the door and is gone before the host's game answers (no Bye).
  s.hostGame.endpoint = nullptr; // the host's game is busy: no answer yet
  s.a->UseDevice(UseDoor(proto::kDeviceOpen));
  s.a->Update(20ms);
  s.a.reset();
  s.bGame.endpoint = s.b.get();
  bool left = false;
  REQUIRE(Pump({s.host.get(), s.b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      s.bGame.See(events);
    for (const auto &event : events)
      if (i == 0 && std::holds_alternative<events::PlayerLeft>(event))
        left = true;
    return left;
  }));
  // The host's game answers now: the door opened there, for a player who
  // is gone. It is the world's: b sees it open, nothing waits for a.
  s.host->ReportDevice(Door(proto::kDeviceOpen, 2));
  REQUIRE(Pump({s.host.get(), s.b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      s.bGame.See(events);
    return s.bGame.words == 2;
  }));
  CHECK(s.bGame.world[kDoorKey].flags == proto::kDeviceOpen);
  CHECK(s.host->Devices().Find(kDoorKey)->flags == proto::kDeviceOpen);
}

TEST_CASE("the host vanishes: a guest keeps the record to put its own devices back") {
  auto s = Start(false, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  s.host->ReportDevice(Door(proto::kDeviceOpen));
  REQUIRE(s.Run([&] { return s.aGame.words == 1; }));
  s.host.reset();
  bool ended = false;
  REQUIRE(Pump({s.a.get()}, [&](std::size_t, const auto &events) {
    for (const auto &event : events)
      ended = ended || std::holds_alternative<events::Ended>(event);
    return ended;
  }));
  // Nothing more is asked of anyone: requests go nowhere, the copy stays.
  s.a->UseDevice(UseDoor(0));
  s.a->AskDevice(Door(0));
  CHECK(s.a->Update(10ms).empty());
  CHECK(s.a->Devices().Size() == 1);
}
