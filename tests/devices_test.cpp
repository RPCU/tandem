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

// ---- Glass and breakable scenery (devices.md) ----

TEST_CASE("fractures: a pane is told once, and what our replay breaks never") {
  devices::Fractures glass;
  const std::uint64_t t = 10'000'000;
  const proto::Fracture pane{.at = {10.f, 20.f, 3.f}};
  CHECK(glass.Ours(pane, t));
  // The same pane's other pieces, moments later.
  CHECK_FALSE(glass.Ours({.at = {10.3f, 20.f, 3.2f}}, t + 100'000));
  // Another window, further along the wall.
  CHECK(glass.Ours({.at = {14.f, 20.f, 3.f}}, t + 200'000));
  // What stood behind the first one goes much later: news again.
  CHECK(glass.Ours(pane, t + 5'000'000));
  CHECK(glass.Told() == 3);
  CHECK(glass.Repeats() == 1);

  // Another game's break is ours to replay, once...
  const proto::Fracture theirs{.player = 2, .at = {40.f, 5.f, 1.f}};
  CHECK(glass.Theirs(theirs, t + 6'000'000));
  CHECK_FALSE(glass.Theirs(theirs, t + 6'010'000));
  CHECK(glass.Replayed() == 1);
  // ...and what our replay breaks here is news for nobody.
  CHECK_FALSE(glass.Ours({.at = {40.9f, 5.f, 1.4f}}, t + 6'050'000));
  CHECK(glass.Echoes() == 1);
  // Later the same place breaks again, by our own doing: told.
  CHECK(glass.Ours({.at = {40.9f, 5.f, 1.4f}}, t + 9'000'000));
  // Both shot one window: ours is told, theirs needs no replay.
  CHECK(glass.Ours({.at = {60.f, 0.f, 0.f}}, t + 10'000'000));
  CHECK_FALSE(glass.Theirs({.player = 2, .at = {60.2f, 0.f, 0.f}}, t + 10'100'000));
}

TEST_CASE("fractures: a named part breaks once, whoever says it") {
  devices::Fractures glass;
  const proto::Fracture part{.entity = 0xABC, .component = 0x11, .at = {1.f, 2.f, 3.f}, .level = 1};
  auto beside = part; // the pane next to it in the same frame, 20 cm away
  beside.component = 0x12;
  beside.at.x += 0.2f;
  CHECK(glass.Theirs(part, 1'000'000));
  CHECK_FALSE(glass.Theirs(part, 1'000'001));
  // Our replay broke it here: our game's own event of it is its echo.
  CHECK_FALSE(glass.Ours(part, 1'050'000));
  CHECK(glass.Echoes() == 1);
  // The pane beside it is ours, although right next to a replay.
  CHECK(glass.Ours(beside, 1'060'000));
  // Broken for good: never again, either way.
  CHECK_FALSE(glass.Ours(beside, 60'000'000));
  CHECK_FALSE(glass.Theirs(beside, 60'000'001));
  CHECK(glass.Repeats() == 3);
  // A new session starts whole.
  glass.Clear();
  CHECK(glass.Ours(beside, 61'000'000));
}

TEST_CASE("fractures: a game is held to its own rate") {
  devices::Fractures glass;
  int told = 0;
  for (int i = 0; i < 100; ++i)
    told += glass.Ours({.at = {static_cast<float>(i) * 5.f, 0.f, 0.f}}, 1'000'000 + i) ? 1 : 0;
  CHECK(told == static_cast<int>(devices::kFractureBurst));
  CHECK(glass.Held() == 100u - static_cast<unsigned>(told));
  // Two seconds later: a whole burst again, no more.
  told = 0;
  for (int i = 0; i < 100; ++i)
    told += glass.Ours({.at = {static_cast<float>(i) * 5.f, 100.f, 0.f}}, 3'100'000 + i) ? 1 : 0;
  CHECK(told == static_cast<int>(devices::kFractureBurst));
}

namespace {
// The fractures and device fire each endpoint of a session hears (host, a, b).
struct Heard {
  std::vector<proto::Fracture> broke[3];
  std::vector<proto::DeviceFire> fired[3];
  bool Run(const std::vector<Endpoint *> &endpoints, const std::function<bool()> &done,
           std::chrono::milliseconds limit = 3s) {
    return Pump(
        endpoints,
        [&](std::size_t i, const auto &events) {
          for (const auto &event : events) {
            if (const auto *glass = std::get_if<events::Fractured>(&event))
              broke[i].push_back(glass->fracture);
            else if (const auto *fire = std::get_if<events::DeviceFired>(&event))
              fired[i].push_back(fire->fire);
          }
          return done();
        },
        limit);
  }
};
} // namespace

TEST_CASE("glass: what breaks in one game breaks in the others, in its player's name, never "
          "its own") {
  auto s = Start(true);
  Heard heard;
  // A guest's shot breaks a window: a part of a placed entity.
  s.a->SendFracture({.entity = 0x77, .component = 0x9, .at = {1.f, 2.f, 3.f}, .level = 2});
  REQUIRE(
      heard.Run(s.All(), [&] { return heard.broke[0].size() == 1 && heard.broke[2].size() == 1; }));
  CHECK(heard.broke[0][0] == proto::Fracture{.player = s.a->You(),
                                             .entity = 0x77,
                                             .component = 0x9,
                                             .at = {1.f, 2.f, 3.f},
                                             .level = 2});
  CHECK(heard.broke[2][0] == heard.broke[0][0]);
  // The host's own, a piece of the world with no name: to every guest.
  s.host->SendFracture({.at = {5.f, 6.f, 7.f}});
  REQUIRE(
      heard.Run(s.All(), [&] { return heard.broke[1].size() == 1 && heard.broke[2].size() == 2; }));
  CHECK(heard.broke[1][0] == proto::Fracture{.player = s.host->You(), .at = {5.f, 6.f, 7.f}});
  // Nobody hears its own again.
  heard.Run(s.All(), [] { return false; }, 100ms);
  CHECK(heard.broke[0].size() == 1);
  CHECK(heard.broke[1].size() == 1);
  CHECK(heard.broke[2].size() == 2);
}

TEST_CASE("glass: the host holds a guest's fractures to a rate") {
  auto s = Start(true);
  Heard heard;
  for (int i = 0; i < 200; ++i)
    s.a->SendFracture({.at = {static_cast<float>(i), 0.f, 0.f}});
  REQUIRE(heard.Run(s.All(), [&] {
    return heard.broke[0].size() + s.host->FracturesDropped() == 200 &&
           heard.broke[2].size() == heard.broke[0].size();
  }));
  CHECK(heard.broke[0].size() >= static_cast<std::size_t>(devices::kHostFractureBurst));
  CHECK(s.host->FracturesDropped() > 0);
}

TEST_CASE("the fracture record keeps a piece once, by its name or its metre, in any order") {
  devices::FractureRecord host, guest;
  const proto::Fracture part{.player = 2, .entity = 0x77, .component = 0x9, .at = {1.f, 2.f, 3.f}};
  const proto::Fracture pane{.player = 1, .at = {10.2f, -3.4f, 1.f}};
  CHECK(host.Note(part));
  CHECK(host.Note(pane));
  // The same part anywhere, by anyone; another piece in the pane's metre.
  CHECK_FALSE(host.Note({.player = 3, .entity = 0x77, .component = 0x9, .at = {9.f, 9.f, 9.f}}));
  CHECK_FALSE(host.Note({.player = 2, .at = {10.9f, -3.1f, 1.5f}}));
  CHECK(host.Note({.at = {11.1f, -3.4f, 1.f}}));
  CHECK(host.Size() == 3);
  CHECK(host.InOrder().front() == part);
  // Heard in another order: the same digest.
  CHECK(guest.Note({.at = {11.1f, -3.4f, 1.f}}));
  CHECK(guest.Digest() != host.Digest());
  CHECK(guest.Note(pane));
  CHECK(guest.Note(part));
  CHECK(guest.Digest() == host.Digest());
  // Bounded: full, a new piece is not kept; a known one is still known.
  devices::FractureRecord full;
  for (std::size_t i = 0; i < devices::kMaxFractureRecord; ++i)
    full.Note({.at = {static_cast<float>(i) * 2.f, 0.f, 0.f}});
  CHECK(full.Full());
  CHECK_FALSE(full.Note({.at = {-50.f, 0.f, 0.f}}));
  CHECK(full.Has({.at = {0.5f, 0.f, 0.f}}));
  CHECK_FALSE(full.Has({.at = {-50.f, 0.f, 0.f}}));
}

TEST_CASE("glass: what broke before a player joined is broken for it, and every record is the "
          "host's") {
  auto s = Start(false);
  Heard heard;
  s.a->SendFracture({.entity = 0x77, .component = 0x9, .at = {1.f, 2.f, 3.f}, .level = 2});
  s.host->SendFracture({.at = {5.f, 6.f, 7.f}});
  REQUIRE(heard.Run(s.All(), [&] {
    return heard.broke[0].size() == 1 && heard.broke[1].size() == 1 && s.a->Fractures().Size() == 2;
  }));
  // Another piece of the host's pane: news to the linked, not to the record.
  s.host->SendFracture({.at = {5.2f, 6.1f, 7.3f}});
  REQUIRE(heard.Run(s.All(), [&] { return heard.broke[1].size() == 2; }));
  CHECK(s.host->Fractures().Size() == 2);

  // b joins later: both pieces, once each, in their players' names.
  std::string error;
  s.b = Endpoint::Join({"b"}, "127.0.0.1", s.host->LocalPort(), error, kQuick);
  REQUIRE(s.b);
  REQUIRE(heard.Run(s.All(), [&] { return heard.broke[2].size() == 2; }));
  // In any order (the record is a set): a's part, and the host's pane.
  const auto &late = heard.broke[2];
  const bool aFirst = late[0].entity == 0x77;
  const auto &part = late[aFirst ? 0 : 1];
  const auto &pane = late[aFirst ? 1 : 0];
  CHECK(part.player == s.a->You());
  CHECK(part.entity == 0x77);
  CHECK(pane.player == s.host->You());
  CHECK(pane.entity == 0);
  heard.Run(s.All(), [] { return false; }, 400ms);
  CHECK(heard.broke[2].size() == 2);
  CHECK(s.a->Fractures().Digest() == s.host->Fractures().Digest());
  CHECK(s.b->Fractures().Digest() == s.host->Fractures().Digest());
}

TEST_CASE("a turret in a player's hands: shown to the others in its name, never to itself") {
  auto s = Start(true);
  std::vector<proto::TurretHands> held[3];
  const auto run = [&](const std::function<bool()> &done) {
    return Pump(
        s.All(),
        [&](std::size_t i, const auto &events) {
          for (const auto &event : events)
            if (const auto *hands = std::get_if<events::TurretHeld>(&event))
              held[i].push_back(hands->hands);
          return done();
        },
        3s);
  };
  s.a->SendTurretHands({.entity = 0x55, .at = {1.f, 2.f, 3.f}, .flags = proto::kHandsFiring});
  REQUIRE(run([&] { return held[0].size() == 1 && held[2].size() == 1; }));
  CHECK(held[0][0] == proto::TurretHands{.player = s.a->You(),
                                         .entity = 0x55,
                                         .at = {1.f, 2.f, 3.f},
                                         .flags = proto::kHandsFiring});
  CHECK(held[2][0] == held[0][0]);
  s.host->SendTurretHands({.entity = 0x66, .flags = proto::kHandsLetGo});
  REQUIRE(run([&] { return held[1].size() == 1 && held[2].size() == 2; }));
  CHECK(held[1][0].player == s.host->You());
  // Nobody hears its own; an unnamed turret goes nowhere.
  s.b->SendTurretHands({.at = {1.f, 1.f, 1.f}});
  run([] { return false; });
  CHECK(held[0].size() == 1);
  CHECK(held[1].size() == 1);
  CHECK(held[2].size() == 2);
}

TEST_CASE("a device's fire: the host's turret's aim and shots reach every guest; what a copy "
          "sees, the host alone") {
  using Kind = proto::DeviceFireKind;
  constexpr std::uint64_t kTurret = 0x7E77E7ull;
  auto s = Start(true);
  Heard heard;
  const auto a = s.a->You();
  // a's copy of the turret locks a's V: the host's game hears it, once.
  s.a->SendDeviceFire({.entity = kTurret, .kind = Kind::Seen, .at = {1.f, 2.f, 3.f}});
  s.a->SendDeviceFire({.entity = kTurret, .kind = Kind::Seen, .at = {1.f, 2.f, 3.f}});
  REQUIRE(heard.Run(s.All(), [&] { return heard.fired[0].size() == 1; }));
  CHECK(heard.fired[0][0].kind == Kind::Seen);
  CHECK(heard.fired[0][0].player == a);
  CHECK(heard.fired[0][0].entity == kTurret);
  CHECK(s.host->DevicesSeeing().at(a).contains(kTurret));
  // The host's turret turns to a's puppet and fires: every guest hears at whom.
  const proto::TargetRef target{proto::TargetKind::Player, a};
  s.host->SendDeviceFire(
      {.entity = kTurret, .kind = Kind::Aim, .target = target, .at = {1.f, 2.f, 3.f}});
  s.host->SendDeviceFire(
      {.entity = kTurret, .kind = Kind::Shot, .target = target, .at = {1.f, 2.f, 3.f}});
  REQUIRE(
      heard.Run(s.All(), [&] { return heard.fired[1].size() == 2 && heard.fired[2].size() == 2; }));
  CHECK(heard.fired[1][0].kind == Kind::Aim);
  CHECK(heard.fired[1][0].timeUs == 0);
  CHECK(heard.fired[1][1].kind == Kind::Shot);
  CHECK(heard.fired[1][1].target == target);
  CHECK(heard.fired[1][1].timeUs != 0); // the host clock, for a shot that comes too late
  CHECK(heard.fired[2][1] == heard.fired[1][1]);
  // Not theirs to say: a guest's "shot", the host's "seen", a turret that
  // never had b's V losing it. Nobody hears any of it.
  s.a->SendDeviceFire({.entity = kTurret, .kind = Kind::Shot, .target = target});
  s.host->SendDeviceFire({.entity = kTurret, .kind = Kind::Seen});
  s.b->SendDeviceFire({.entity = kTurret, .kind = Kind::Lost});
  heard.Run(s.All(), [] { return false; }, 100ms);
  CHECK(heard.fired[0].size() == 1);
  CHECK(heard.fired[1].size() == 2);
  CHECK(heard.fired[2].size() == 2);
  // a's copy loses a's V, then the host's turret follows nobody.
  s.a->SendDeviceFire({.entity = kTurret, .kind = Kind::Lost});
  REQUIRE(heard.Run(s.All(), [&] { return heard.fired[0].size() == 2; }));
  CHECK(heard.fired[0][1].kind == Kind::Lost);
  CHECK(heard.fired[0][1].player == a);
  CHECK(s.host->DevicesSeeing().at(a).empty());
  s.host->SendDeviceFire({.entity = kTurret, .kind = Kind::Idle});
  REQUIRE(
      heard.Run(s.All(), [&] { return heard.fired[1].size() == 3 && heard.fired[2].size() == 3; }));
  CHECK(heard.fired[2][2].kind == Kind::Idle);
}

TEST_CASE("a device's fire: a guest that vanishes is let go by the turrets that had its V") {
  using Kind = proto::DeviceFireKind;
  auto s = Start(true, {.timeout = 1000ms, .takeover = 400ms, .digest = 100ms});
  Heard heard;
  const auto a = s.a->You();
  s.a->SendDeviceFire({.entity = 7, .kind = Kind::Seen});
  s.a->SendDeviceFire({.entity = 8, .kind = Kind::Seen});
  REQUIRE(heard.Run(s.All(), [&] { return heard.fired[0].size() == 2; }));
  s.a.reset(); // no goodbye
  REQUIRE(heard.Run({s.host.get(), s.b.get()}, [&] { return heard.fired[0].size() == 4; }));
  for (std::size_t i = 2; i < 4; ++i) {
    CHECK(heard.fired[0][i].kind == Kind::Lost);
    CHECK(heard.fired[0][i].player == a);
  }
  CHECK(heard.fired[0][2].entity + heard.fired[0][3].entity == 15);
  CHECK_FALSE(s.host->DevicesSeeing().contains(a));
}
