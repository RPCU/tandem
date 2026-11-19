// Vehicle combat (docs/architecture/vehicle-combat.md): the rules in
// vehicle_combat.hpp, and a real host and guests over loopback UDP: damage
// states and late joiners, hits routed to a vehicle's owner, the gunner seat
// race, aims, bumps, rams, chases, digests, duplicates and vanishes.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/vehicle_combat.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;
namespace vc = tandem::vehiclecombat;

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

void Settle(std::vector<Endpoint *> endpoints, std::chrono::milliseconds time = 150ms) {
  Pump(std::move(endpoints), [](std::size_t, const auto &) { return false; }, time);
}

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
constexpr std::uint64_t kTurret = 0x1234'5678'9abc'def0ULL;
constexpr std::uint64_t kBasilisk = 0x22'0011'2233ULL;

// What each game was told, by kind.
struct Heard {
  std::map<proto::VehicleRef, proto::VehicleDamage> damage;
  std::map<proto::VehicleRef, proto::PlayerId> gunners;
  std::vector<proto::VehicleHit> hits;
  std::vector<proto::VehicleExit> exits;
  std::vector<proto::MountedFire> fires;
  std::vector<proto::GunnerAim> aims;
  std::vector<proto::VehicleBump> bumps;
  std::vector<proto::NpcRam> rams;
  std::map<proto::TrafficId, proto::PlayerId> chases;
  std::map<proto::PlayerId, proto::SeatCombat> combat;
  int damageEvents{};

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *d = std::get_if<events::RemoteVehicleDamage>(&event)) {
        ++damageEvents;
        damage[d->damage.vehicle] = d->damage;
      } else if (const auto *g = std::get_if<events::GunnerChanged>(&event)) {
        if (g->gunner.gunner == proto::kNoPlayer)
          gunners.erase(g->gunner.vehicle);
        else
          gunners[g->gunner.vehicle] = g->gunner.gunner;
      } else if (const auto *h = std::get_if<events::VehicleHitBy>(&event)) {
        hits.push_back(h->hit);
      } else if (const auto *e = std::get_if<events::RemoteVehicleExit>(&event)) {
        exits.push_back(e->exit);
      } else if (const auto *f = std::get_if<events::RemoteMountedFire>(&event)) {
        fires.push_back(f->fire);
      } else if (const auto *a = std::get_if<events::RemoteGunnerAim>(&event)) {
        aims.push_back(a->aim);
      } else if (const auto *b = std::get_if<events::VehicleBumpBy>(&event)) {
        bumps.push_back(b->bump);
      } else if (const auto *r = std::get_if<events::NpcRamBy>(&event)) {
        rams.push_back(r->ram);
      } else if (const auto *c = std::get_if<events::ChaseChanged>(&event)) {
        if (c->chase.target == proto::kNoPlayer)
          chases.erase(c->chase.vehicle);
        else
          chases[c->chase.vehicle] = c->chase.target;
      } else if (const auto *m = std::get_if<events::RemoteVehicleCombat>(&event)) {
        if (m->combat.mode == proto::SeatCombat::None)
          combat.erase(m->combat.player);
        else
          combat[m->combat.player] = m->combat.mode;
      }
    }
  }
};

// A host and `guests` guests, all in.
struct Session {
  std::unique_ptr<Endpoint> host;
  std::vector<std::unique_ptr<Endpoint>> guests;
  std::vector<Heard> heard;

  Session(int count, Timing timing = kQuick) {
    const auto port = NextPort();
    std::string error;
    host = Endpoint::Host({"host"}, port, error, timing);
    REQUIRE(host);
    for (int i = 0; i < count; ++i) {
      guests.push_back(Endpoint::Join({"g" + std::to_string(i)}, "127.0.0.1", port, error, timing));
      REQUIRE(guests.back());
    }
    heard.resize(count + 1);
    REQUIRE(Run([&] {
      for (const auto &guest : guests)
        if (!guest->IsJoined() || guest->Players().size() != guests.size() + 1)
          return false;
      return true;
    }));
  }

  std::vector<Endpoint *> All() {
    std::vector<Endpoint *> all{host.get()};
    for (const auto &guest : guests)
      if (guest)
        all.push_back(guest.get());
    return all;
  }

  // Pumps everyone, recording what each heard, until `done`.
  bool Run(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
      heard[0].See(host->Update(1ms));
      for (std::size_t i = 0; i < guests.size(); ++i)
        if (guests[i])
          heard[i + 1].See(guests[i]->Update(1ms));
      if (done())
        return true;
    }
    return false;
  }
  void Wait(std::chrono::milliseconds time) {
    Run([] { return false; }, time);
  }
};
} // namespace

// ---- the rules ----

TEST_CASE("a destruction grid packs into bytes") {
  const float cells[] = {0.f, 1.f, 0.5f, -1.f, 2.f};
  const auto grid = vc::PackGrid(cells);
  CHECK(grid[0] == 0);
  CHECK(grid[1] == 255);
  CHECK(vc::GridCell(grid, 2) == doctest::Approx(0.5f).epsilon(0.01));
  CHECK(grid[3] == 0);   // clamped
  CHECK(grid[4] == 255); // clamped
  CHECK(grid[15] == 0);  // not given: undamaged
  CHECK(vc::GridCell(grid, 99) == 0.f);
}

TEST_CASE("a damage state is newer by its revision, across the wrap") {
  CHECK(vc::Supersedes({.revision = 4}, {.revision = 5}));
  CHECK_FALSE(vc::Supersedes({.revision = 5}, {.revision = 5}));
  CHECK_FALSE(vc::Supersedes({.revision = 6}, {.revision = 5}));
  CHECK(vc::Supersedes({.revision = 0xFFFFFFF0u}, {.revision = 3}));
}

TEST_CASE("a copy blows up only when it hears of it at once, and never twice") {
  const std::uint64_t now = 100'000'000;
  const proto::VehicleDamage fresh{.flags = proto::kDamageDestroyed, .explodedUs = now - 500'000};
  const proto::VehicleDamage old{.flags = proto::kDamageDestroyed, .explodedUs = now - 10'000'000};
  const proto::VehicleDamage unknown{.flags = proto::kDamageDestroyed};
  CHECK(vc::OnArrival(fresh, std::nullopt, now) == vc::Destruction::Explode);
  CHECK(vc::OnArrival(fresh, proto::VehicleDamage{.health = 0.4f}, now) ==
        vc::Destruction::Explode);
  CHECK(vc::OnArrival(old, std::nullopt, now) == vc::Destruction::Wreck);
  CHECK(vc::OnArrival(unknown, std::nullopt, now) == vc::Destruction::Wreck);
  // A clock a little behind the owner's: still the explosion.
  CHECK(vc::OnArrival(fresh, std::nullopt, now - 1'000'000) == vc::Destruction::Explode);
  CHECK(vc::OnArrival(fresh, fresh, now) == vc::Destruction::None);
  CHECK(vc::OnArrival({.health = 0.5f}, std::nullopt, now) == vc::Destruction::None);
}

TEST_CASE("a hit takes health, shoots out a tyre, breaks a window, and ends in a wreck") {
  proto::VehicleDamage car{.vehicle = proto::PlayerCar(2), .revision = 1};
  car = vc::Apply(car,
                  {.player = 3,
                   .damage = 250.f,
                   .flags = proto::kVehicleHitTyre | proto::kVehicleHitWindow,
                   .tyre = 2,
                   .window = 0},
                  1000.f, 5);
  CHECK(car.revision == 2);
  CHECK(car.health == doctest::Approx(0.75f));
  CHECK(car.tyres == 4);
  CHECK(car.windows == 1);
  car = vc::Apply(car, {.player = 3, .damage = 680.f}, 1000.f, 6);
  CHECK((car.flags & proto::kDamageBurning) != 0);
  car = vc::Apply(car, {.player = 3, .damage = 100.f}, 1000.f, 7);
  CHECK((car.flags & proto::kDamageDestroyed) != 0);
  CHECK((car.flags & proto::kDamageBurning) == 0);
  CHECK(car.explodedUs == 7);
  CHECK(car.instigator == 3);
  const auto revision = car.revision;
  car = vc::Apply(car, {.player = 4, .damage = 100.f}, 1000.f, 8);
  CHECK(car.revision == revision); // a wreck takes nothing more
  CHECK(car.instigator == 3);
  // Nonsense indices change nothing.
  const auto same = vc::Apply({}, {.flags = proto::kVehicleHitTyre, .tyre = 12}, 1000.f, 1);
  CHECK(same.tyres == 0);
}

TEST_CASE("how a V left a vehicle, and whether it rolls on") {
  using proto::ExitKind;
  CHECK(vc::Classify({}) == ExitKind::Normal);
  CHECK(vc::Classify({.speed = true}) == ExitKind::Bail);
  CHECK(vc::Classify({.collision = true}) == ExitKind::Thrown);
  CHECK(vc::Classify({.collision = true, .bike = true}) == ExitKind::Knocked);
  CHECK(vc::Classify({.forced = true, .bike = true}) == ExitKind::Knocked);
  CHECK(vc::Classify({.death = true}) == ExitKind::Dead);
  CHECK(vc::Classify({.death = true, .destroyed = true}) == ExitKind::Exploded);
  CHECK(vc::Classify({.forced = true, .destroyed = true}) == ExitKind::Exploded);
  CHECK(vc::Rolls(ExitKind::Bail, 15.f, true));
  CHECK_FALSE(vc::Rolls(ExitKind::Bail, 15.f, false)); // a passenger: the driver drives on
  CHECK_FALSE(vc::Rolls(ExitKind::Bail, 1.f, true));
  CHECK_FALSE(vc::Rolls(ExitKind::Exploded, 15.f, true));
  CHECK_FALSE(vc::Rolls(ExitKind::Normal, 15.f, true));
  CHECK(vc::Rolls(ExitKind::Knocked, 9.f, true));
}

TEST_CASE("a car running into an NPC knocks it down, and kills it by the game's thresholds") {
  CHECK(vc::RamFlags(2.f, false) == 0);
  CHECK(vc::RamFlags(8.f, false) == proto::kRamKnockdown);
  CHECK(vc::RamFlags(21.f, false) == proto::kRamKnockdown);
  CHECK(vc::RamFlags(21.f, true) == (proto::kRamKnockdown | proto::kRamLethal));
  CHECK(vc::RamFlags(30.f, false) == (proto::kRamKnockdown | proto::kRamLethal));
}

TEST_CASE("a push between players' cars is applied once: not when our game felt it") {
  vc::BumpLedger ledger;
  const std::uint64_t t = 50'000'000;
  // Not felt here: held for the window, then given.
  ledger.Receive({.player = 2, .other = 1, .impulse = {3, 0, 0}, .atUs = t}, t + 50'000);
  CHECK(ledger.Due(t + 100'000).empty());
  auto due = ledger.Due(t + vc::BumpLedger::kWindowUs + 1);
  REQUIRE(due.size() == 1);
  CHECK(due[0].from == 2);
  CHECK(due[0].impulse.x == doctest::Approx(3.f));
  // The same contact said again: felt now, nothing more.
  ledger.Receive({.player = 2, .other = 1, .impulse = {3, 0, 0}, .atUs = t}, t + 600'000);
  CHECK(ledger.Due(t + 900'000).empty());

  // Felt here before the report came, or after it (the copy lags): nothing.
  ledger.Contact(3, t + 1'000'000);
  ledger.Receive({.player = 3, .impulse = {0, 4, 0}, .atUs = t + 1'100'000}, t + 1'150'000);
  CHECK(ledger.Due(t + 2'000'000).empty());
  ledger.Receive({.player = 4, .impulse = {0, 4, 0}, .atUs = t + 3'000'000}, t + 3'050'000);
  ledger.Contact(4, t + 3'200'000);
  CHECK(ledger.Due(t + 3'600'000).empty());

  // Too old (behind a loading screen), or absurd: nothing; too strong: clamped.
  ledger.Receive({.player = 5, .impulse = {1, 0, 0}, .atUs = t}, t + 5'000'000);
  ledger.Receive({.player = 6, .impulse = {300, 0, 0}, .atUs = t + 6'000'000}, t + 6'000'000);
  due = ledger.Due(t + 6'500'000);
  REQUIRE(due.size() == 1);
  CHECK(due[0].from == 6);
  CHECK(due[0].impulse.x == doctest::Approx(vc::BumpLedger::kMaxImpulse));
  ledger.Receive({.player = 7, .impulse = {1, 0, 0}, .atUs = t + 7'000'000}, t + 7'000'000);
  ledger.Forget(7);
  CHECK(ledger.Held() == 0);
}

TEST_CASE("a gunner seat goes to the first who asks, one seat a V, never the driver") {
  vc::Turrets turrets;
  const auto car = proto::PlayerCar(1);
  const auto other = proto::PlayerCar(4);
  auto changed = turrets.Ask({.player = 2, .vehicle = car, .seat = kTurret});
  REQUIRE(changed.size() == 1);
  CHECK(changed[0].gunner == 2);
  CHECK(turrets.Ask({.player = 3, .vehicle = car, .seat = kTurret}).empty()); // taken
  CHECK(turrets.Ask({.player = 2, .vehicle = car, .seat = kTurret}).empty()); // twice
  CHECK(turrets.Ask({.player = 1, .vehicle = car, .seat = kTurret}).empty()); // its driver
  // Player 2 moves to another vehicle's seat: the first is free.
  changed = turrets.Ask({.player = 2, .vehicle = other, .seat = kTurret});
  REQUIRE(changed.size() == 2);
  CHECK(changed[0].vehicle == car);
  CHECK(changed[0].gunner == proto::kNoPlayer);
  CHECK(changed[1].gunner == 2);
  CHECK(turrets.Ask({.player = 3, .vehicle = car, .seat = kTurret}).size() == 1);
  CHECK(turrets.Ask({.player = 2, .vehicle = car, .leave = true}).empty()); // not its seat
  CHECK(turrets.Ask({.player = 3, .vehicle = car, .leave = true}).size() == 1);
  CHECK(turrets.Release(2).size() == 1);
  CHECK(turrets.Seats().empty());
  CHECK_FALSE(turrets.Free(car));
}

// ---- in a session ----

TEST_CASE("a car's damage reaches every copy and a late joiner, and is never applied backwards") {
  Session s(1);
  auto &a = *s.guests[0];
  const auto car = proto::PlayerCar(a.You());
  a.SetVehicleDamage({.vehicle = car, .record = kBasilisk, .health = 0.6f, .tyres = 1});
  REQUIRE(s.Run([&] { return s.heard[0].damage.contains(car); }));
  CHECK(s.heard[0].damage[car].health == doctest::Approx(0.6f));
  CHECK(s.heard[0].damage[car].tyres == 1);
  // The same again: nothing new for anyone.
  a.SetVehicleDamage({.vehicle = car, .record = kBasilisk, .health = 0.6f, .tyres = 1});
  s.Wait(100ms);
  CHECK(s.heard[0].damageEvents == 1);
  // Someone else's car is not ours to damage.
  a.SetVehicleDamage({.vehicle = proto::PlayerCar(1), .health = 0.1f});
  // It blows up.
  a.SetVehicleDamage({.vehicle = car,
                      .record = kBasilisk,
                      .health = 0.f,
                      .flags = proto::kDamageDestroyed,
                      .explodedUs = a.HostNowUs(),
                      .instigator = 1});
  REQUIRE(s.Run([&] { return (s.heard[0].damage[car].flags & proto::kDamageDestroyed) != 0; }));
  CHECK(s.heard[0].damage[car].instigator == 1);
  CHECK_FALSE(s.host->VehicleDamages().contains(proto::PlayerCar(1)));

  // A late joiner hears it as it stands: a wreck, put there.
  s.Wait(vc::kFreshExplosionUs * 1ms / 1000 + 200ms);
  const auto port = s.host->LocalPort();
  std::string error;
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error, kQuick);
  REQUIRE(late);
  std::optional<proto::VehicleDamage> wreck;
  REQUIRE(Pump({s.host.get(), &a, late.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *d = std::get_if<events::RemoteVehicleDamage>(&event); d && i == 2)
        wreck = d->damage;
    return wreck.has_value();
  }));
  CHECK(wreck->vehicle == car);
  CHECK(vc::OnArrival(*wreck, std::nullopt, late->HostNowUs()) == vc::Destruction::Wreck);
}

TEST_CASE("a hit on a player's car reaches its driver's game only") {
  Session s(2);
  auto &a = *s.guests[0];
  auto &b = *s.guests[1];
  b.SendVehicleHit({.vehicle = proto::PlayerCar(a.You()),
                    .damage = 120.f,
                    .flags = proto::kVehicleHitTyre,
                    .tyre = 1});
  s.host->SendVehicleHit({.vehicle = proto::PlayerCar(a.You()), .damage = 30.f});
  a.SendVehicleHit({.vehicle = proto::PlayerCar(1), .damage = 50.f});
  a.SendVehicleHit({.vehicle = proto::PlayerCar(a.You()), .damage = 50.f}); // ours: dropped
  REQUIRE(s.Run([&] { return s.heard[1].hits.size() == 2 && s.heard[0].hits.size() == 1; }));
  s.Wait(100ms);
  CHECK(s.heard[1].hits.size() == 2);
  CHECK(s.heard[2].hits.empty());
  CHECK(s.heard[0].hits.size() == 1);
  CHECK(s.heard[0].hits[0].player == a.You());
  bool fromB = false, fromHost = false;
  for (const auto &hit : s.heard[1].hits) {
    fromB = fromB || (hit.player == b.You() && hit.tyre == 1);
    fromHost = fromHost || hit.player == 1;
  }
  CHECK(fromB);
  CHECK(fromHost);
}

TEST_CASE("the gunner seat of the host's car goes to the first guest; freed when it vanishes") {
  Session s(2, kFast);
  auto &a = *s.guests[0];
  auto &b = *s.guests[1];
  const auto car = proto::PlayerCar(1);
  a.AskGunner(car, kTurret);
  b.AskGunner(car, kTurret);
  REQUIRE(s.Run([&] {
    return s.heard[0].gunners.contains(car) && s.heard[1].gunners.contains(car) &&
           s.heard[2].gunners.contains(car);
  }));
  s.Wait(200ms);
  const auto gunner = s.heard[0].gunners[car];
  CHECK((gunner == a.You() || gunner == b.You()));
  CHECK(s.heard[1].gunners[car] == gunner);
  CHECK(s.heard[2].gunners[car] == gunner);
  CHECK(s.host->Gunners().at(car).gunner == gunner);

  // Only the gunner's aim goes round.
  auto &winner = gunner == a.You() ? a : b;
  auto &loser = gunner == a.You() ? b : a;
  winner.SendGunnerAim({.vehicle = car, .yaw = 0.5f, .pitch = 0.1f, .flags = proto::kAimFiring});
  loser.SendGunnerAim({.vehicle = car, .yaw = -1.f});
  winner.SendMountedFire({.vehicle = car, .weapon = 0x77, .target = {1, 2, 3}});
  REQUIRE(s.Run([&] { return !s.heard[0].aims.empty() && !s.heard[0].fires.empty(); }));
  s.Wait(100ms);
  CHECK(s.heard[0].aims.size() == 1);
  CHECK(s.heard[0].aims[0].player == gunner);
  CHECK(s.heard[0].fires[0].player == gunner);
  const std::size_t loserIndex = gunner == a.You() ? 2 : 1;
  CHECK(s.heard[loserIndex].aims.size() == 1);

  // The gunner vanishes: everyone hears the seat is free.
  const bool aWon = gunner == a.You();
  s.guests[aWon ? 0 : 1].reset();
  REQUIRE(s.Run(
      [&] {
        return !s.heard[0].gunners.contains(car) && !s.heard[aWon ? 2 : 1].gunners.contains(car);
      },
      4s));
  CHECK(s.host->Gunners().empty());
}

TEST_CASE("a guest's own vehicle gives its seat: the request goes to that guest's game") {
  Session s(2);
  auto &a = *s.guests[0];
  auto &b = *s.guests[1];
  const auto car = proto::PlayerCar(a.You());
  b.AskGunner(car, kTurret);
  REQUIRE(s.Run([&] {
    return s.heard[1].gunners.contains(car) && s.heard[2].gunners.contains(car) &&
           s.heard[0].gunners.contains(car);
  }));
  CHECK(s.heard[1].gunners[car] == b.You());
  // The host asks too: refused, and told who has it.
  s.host->AskGunner(car, kTurret);
  s.Wait(200ms);
  CHECK(s.host->Gunners().at(car).gunner == b.You());
  // The owner's game could not seat them: free for all.
  a.FreeGunner(car);
  REQUIRE(s.Run(
      [&] { return !s.heard[0].gunners.contains(car) && !s.heard[2].gunners.contains(car); }));
  // And b may leave a seat it holds.
  b.AskGunner(car, kTurret);
  REQUIRE(s.Run([&] { return s.heard[0].gunners.contains(car); }));
  b.AskGunner(car, kTurret, true);
  REQUIRE(s.Run([&] { return !s.heard[0].gunners.contains(car); }));
}

TEST_CASE("weapons in a vehicle, exits and mounted shots reach the others; the state goes with its "
          "player") {
  Session s(2, kFast);
  auto &a = *s.guests[0];
  a.SetLocalVehicleCombat(
      {.mode = proto::SeatCombat::Passenger, .vehicle = proto::PlayerCar(1), .weapon = 0x55});
  a.SendVehicleExit({.vehicle = proto::PlayerCar(a.You()),
                     .kind = proto::ExitKind::Bail,
                     .flags = proto::kExitRolling | proto::kExitDriver,
                     .velocity = {10, 0, 0}});
  REQUIRE(s.Run([&] { return s.heard[2].combat.contains(a.You()) && !s.heard[2].exits.empty(); }));
  CHECK(s.heard[0].combat[a.You()] == proto::SeatCombat::Passenger);
  CHECK(s.heard[2].exits[0].kind == proto::ExitKind::Bail);
  CHECK(s.heard[2].exits[0].player == a.You());
  CHECK(s.heard[1].exits.empty()); // never our own back
  const auto id = a.You();
  s.guests[0].reset();
  REQUIRE(s.Run([&] { return !s.heard[2].combat.contains(id) && !s.heard[0].combat.contains(id); },
                4s));
}

TEST_CASE("a push goes to the other driver; a ram to the NPCs' owner, unless its world moved on") {
  Session s(2);
  auto &a = *s.guests[0];
  auto &b = *s.guests[1];
  a.SendVehicleBump({.other = b.You(), .impulse = {0, 3, 0}, .position = {1, 1, 1}});
  a.SendVehicleBump({.other = 1, .impulse = {0, 2, 0}});
  a.SendVehicleBump({.other = a.You(), .impulse = {0, 2, 0}}); // ourselves: dropped
  s.host->SendNpcSpawn({.npc = 7, .record = 0x11});
  REQUIRE(s.Run([&] { return a.SharedNpcs().contains(7); }));
  a.SendNpcRam({.npc = 7, .velocity = {15, 0, 0}, .flags = proto::kRamKnockdown});
  a.SendNpcRam({.npc = 99, .velocity = {15, 0, 0}}); // not shared: dropped
  REQUIRE(s.Run([&] {
    return s.heard[2].bumps.size() == 1 && s.heard[0].bumps.size() == 1 &&
           s.heard[0].rams.size() == 1;
  }));
  CHECK(s.heard[2].bumps[0].player == a.You());
  CHECK(s.heard[2].bumps[0].atUs != 0);
  CHECK(s.heard[0].rams[0].player == a.You());
  CHECK(s.heard[0].rams[0].npc == 7);

  // The host's world is rebuilt; a ram aimed at the old one is void.
  s.host->BumpEpoch(proto::kEpochFastTravel);
  a.SendNpcRam({.npc = 7, .velocity = {15, 0, 0}});
  s.Wait(300ms);
  CHECK(s.heard[0].rams.size() == 1);
  CHECK(s.host->Fenced() >= 1);
}

TEST_CASE("a chase reaches the guests and ends with its vehicle or its target") {
  Session s(2, kFast);
  auto &a = *s.guests[0];
  const auto car = proto::MakeTrafficId(1, 1);
  const auto other = proto::MakeTrafficId(1, 2);
  s.host->SendTrafficSpawn({.id = car, .record = 0x11});
  s.host->SendTrafficSpawn({.id = other, .record = 0x12});
  s.host->SendChase({.vehicle = car, .target = a.You(), .flags = proto::kChaseRamming});
  s.host->SendChase({.vehicle = other, .target = a.You()});
  s.host->SendChase({.vehicle = proto::MakeTrafficId(1, 9), .target = a.You()}); // not shared
  REQUIRE(s.Run([&] { return s.heard[1].chases.size() == 2 && s.heard[2].chases.size() == 2; }));
  // A shot-out tyre on a shared vehicle travels with its damage.
  s.host->SetVehicleDamage({.vehicle = proto::StreetCar(car), .health = 0.9f, .tyres = 2});
  REQUIRE(s.Run([&] { return s.heard[2].damage.contains(proto::StreetCar(car)); }));
  s.host->SendTrafficGone({.id = car});
  REQUIRE(s.Run([&] { return !s.guests[1]->Chases().contains(car); }));
  CHECK_FALSE(s.guests[1]->VehicleDamages().contains(proto::StreetCar(car)));
  const auto id = a.You();
  s.guests[0].reset();
  REQUIRE(s.Run([&] { return s.guests[1]->Chases().empty() && !s.heard[2].chases.contains(other); },
                4s));
  CHECK(s.host->Chases().empty());
  CHECK_FALSE(s.guests[1]->Players().contains(id));
}

TEST_CASE("lost damage, seats, weapons and chases are healed by the digests") {
  Session s(1);
  auto &a = *s.guests[0];
  const auto car = proto::MakeTrafficId(1, 1);
  s.host->SendTrafficSpawn({.id = car, .record = 0x11});
  REQUIRE(s.Run([&] { return a.SharedTraffic().contains(car); }));
  s.host->SetFaults({.drop = 1.0});
  s.host->SetVehicleDamage({.vehicle = proto::PlayerCar(1), .health = 0.3f, .tyres = 8});
  s.host->SetVehicleDamage({.vehicle = proto::StreetCar(car), .health = 0.5f, .tyres = 1});
  s.host->SendChase({.vehicle = car, .target = a.You()});
  s.host->SetLocalVehicleCombat({.mode = proto::SeatCombat::Mounted, .vehicle = 1, .weapon = 9});
  s.Wait(100ms);
  CHECK(s.heard[1].damage.empty());
  s.host->SetFaults({});
  REQUIRE(s.Run([&] {
    return s.heard[1].damage.contains(proto::PlayerCar(1)) &&
           s.heard[1].damage.contains(proto::StreetCar(car)) && s.heard[1].chases.contains(car) &&
           s.heard[1].combat.contains(1);
  }));
  CHECK(a.Checks().Requests() >= 1);
  const auto requests = a.Checks().Requests();
  s.Wait(500ms);
  CHECK(a.Checks().Requests() == requests);
}

TEST_CASE("sent twice: one damage state, one hit, one push") {
  Session s(1);
  auto &a = *s.guests[0];
  s.host->SetFaults({.dup = 1.0});
  a.SetFaults({.dup = 1.0});
  a.SetVehicleDamage({.vehicle = proto::PlayerCar(a.You()), .health = 0.5f});
  a.SendVehicleHit({.vehicle = proto::PlayerCar(1), .damage = 10.f});
  a.SendVehicleBump({.other = 1, .impulse = {1, 0, 0}});
  s.Wait(400ms);
  CHECK(s.heard[0].damageEvents == 1);
  CHECK(s.heard[0].hits.size() == 1);
  CHECK(s.heard[0].bumps.size() == 1);
  CHECK(consistency::IsOneShot(proto::VehicleHit{}));
  CHECK(consistency::IsOneShot(proto::VehicleBump{}));
  CHECK(consistency::IsOneShot(proto::NpcRam{}));
  CHECK(consistency::IsOneShot(proto::GunnerRequest{}));
  CHECK_FALSE(consistency::IsOneShot(proto::VehicleDamage{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Gunner{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Chase{}));
}
