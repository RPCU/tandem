// World vehicles (docs/architecture/traffic.md): the packing of a moving
// vehicle, the budget of a TrafficStates packet, and a real host and guests
// over loopback UDP: late joiners, claims, hits, epochs, resyncs, a vanish.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/traffic.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <map>
#include <set>

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

void Settle(std::vector<Endpoint *> endpoints, std::chrono::milliseconds time = 150ms) {
  Pump(std::move(endpoints), [](std::size_t, const auto &) { return false; }, time);
}

constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

constexpr proto::TrafficId kCar = proto::MakeTrafficId(1, 1);
constexpr proto::TrafficId kParked = proto::MakeTrafficId(1, 2);
constexpr std::uint64_t kDriverSeat = 0xb000b1d029d0cea0ULL;

// What a game would show of the shared vehicles: the proxies its events
// spawn and remove. A second spawn of one it shows would be a second car in
// the same place; a removal of one it never showed, an orphan event.
struct Street {
  std::map<proto::TrafficId, proto::TrafficSpawn> proxies;
  std::map<proto::TrafficId, proto::TrafficStatus> status;
  std::map<proto::TrafficId, proto::Vec3> at;
  std::vector<proto::TrafficGone> left;
  std::vector<proto::NpcSeat> seats;
  int duplicateSpawns{}, strayGones{}, strayStates{}, moves{};

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *spawn = std::get_if<events::TrafficSpawned>(&event)) {
        duplicateSpawns += proxies.contains(spawn->spawn.id);
        proxies[spawn->spawn.id] = spawn->spawn;
      } else if (const auto *moved = std::get_if<events::TrafficMoved>(&event)) {
        ++moves;
        for (const auto &car : moved->states.cars) {
          strayStates += !proxies.contains(car.id);
          at[car.id] = car.position;
        }
      } else if (const auto *gone = std::get_if<events::TrafficLeft>(&event)) {
        strayGones += proxies.erase(gone->gone.id) == 0;
        status.erase(gone->gone.id);
        left.push_back(gone->gone);
      } else if (const auto *changed = std::get_if<events::TrafficChanged>(&event)) {
        status[changed->status.id] = changed->status;
      } else if (const auto *seat = std::get_if<events::HostNpcSeat>(&event)) {
        seats.push_back(seat->seat);
      }
    }
  }
};

proto::TrafficStates States(proto::TrafficId id, proto::Vec3 position) {
  return {.cars = {traffic::Pack({.id = id, .position = position, .velocity = {10, 0, 0}})}};
}

traffic::Car CarAt(std::uint32_t n, float x, float speed = 10.f) {
  return {.id = proto::MakeTrafficId(1, n), .position = {x, 0, 0}, .velocity = {speed, 0, 0}};
}
} // namespace

TEST_CASE("a rotation packs into six bytes and a velocity into six") {
  // 30 degrees of yaw, 10 of pitch: any unit quaternion will do.
  const proto::Quat q{.i = 0.0842f, .j = 0.0226f, .k = 0.2578f, .r = 0.9622f};
  const auto back = traffic::UnpackOrientation(traffic::PackOrientation(q));
  CHECK(back.i == doctest::Approx(q.i).epsilon(0.001));
  CHECK(back.j == doctest::Approx(q.j).epsilon(0.001));
  CHECK(back.k == doctest::Approx(q.k).epsilon(0.001));
  CHECK(back.r == doctest::Approx(q.r).epsilon(0.001));
  // q and -q are one rotation: r comes back positive.
  const auto flipped =
      traffic::UnpackOrientation(traffic::PackOrientation({-q.i, -q.j, -q.k, -q.r}));
  CHECK(flipped.k == doctest::Approx(q.k).epsilon(0.001));
  CHECK(flipped.r > 0.f);
  // Not normalized, or nonsense: still a rotation.
  const auto scaled = traffic::UnpackOrientation(traffic::PackOrientation({0, 0, 2.f, 2.f}));
  CHECK(scaled.k == doctest::Approx(0.7071f).epsilon(0.001));
  const auto none = traffic::UnpackOrientation(traffic::PackOrientation({0, 0, 0, 0}));
  CHECK(none.r == doctest::Approx(1.f));
  const float nan = std::nanf("");
  CHECK(traffic::UnpackOrientation(traffic::PackOrientation({nan, 0, 0, 1})).r ==
        doctest::Approx(1.f));

  const auto v = traffic::UnpackVelocity(traffic::PackVelocity({12.345f, -3.21f, 0.004f}));
  CHECK(v.x == doctest::Approx(12.35f).epsilon(0.001));
  CHECK(v.y == doctest::Approx(-3.21f).epsilon(0.001));
  CHECK(v.z == doctest::Approx(0.f));
  // Clamped, never wrapped.
  CHECK(traffic::UnpackVelocity(traffic::PackVelocity({1000.f, -1000.f, nan})).x ==
        doctest::Approx(327.67f));
  CHECK(traffic::UnpackVelocity(traffic::PackVelocity({1000.f, -1000.f, nan})).y ==
        doctest::Approx(-327.67f));
  CHECK(traffic::UnpackVelocity(traffic::PackVelocity({0, 0, nan})).z == 0.f);

  const traffic::Car car{.id = 5, .position = {1, 2, 3}, .orientation = q, .velocity = {4, 5, 6}};
  const auto unpacked = traffic::Unpack(traffic::Pack(car));
  CHECK(unpacked.id == 5);
  CHECK(unpacked.position == car.position);
  CHECK(unpacked.velocity.y == doctest::Approx(5.f));
}

TEST_CASE("a vehicle is shared within range and dropped a little further") {
  CHECK(traffic::InRange(100.f, false));
  CHECK_FALSE(traffic::InRange(traffic::kShareRange + 1.f, false));
  // Shared already: kept until the drop range (no flicker at the edge).
  CHECK(traffic::InRange(traffic::kShareRange + 1.f, true));
  CHECK_FALSE(traffic::InRange(traffic::kDropRange + 1.f, true));
  // The nearest player counts, whoever it is.
  const std::vector<proto::Vec3> viewers{{0, 0, 0}, {500, 0, 0}};
  CHECK(traffic::Nearest({490, 0, 0}, viewers) == doctest::Approx(10.f));
  CHECK(traffic::Nearest({0, 0, 0}, {}) > 1e6f);
}

TEST_CASE("a packet holds the nearest vehicles every round and the others in turn") {
  traffic::Budget budget;
  const std::vector<proto::Vec3> viewers{{0, 0, 0}};
  // Everything fits: everything goes.
  std::vector<traffic::Car> few{CarAt(1, 10), CarAt(2, 20), CarAt(3, 30)};
  CHECK(budget.Pick(few, viewers, 8).size() == 3);

  // Twelve moving cars, a packet of eight: the six nearest always, the six
  // others share the two seats left and each gets its turn.
  budget.Clear();
  std::vector<traffic::Car> many;
  for (std::uint32_t n = 1; n <= 12; ++n)
    many.push_back(CarAt(n, 10.f * static_cast<float>(n)));
  std::map<proto::TrafficId, int> sent;
  for (int round = 0; round < 6; ++round) {
    for (auto &car : many)
      car.position.x += 1.f; // they drive
    const auto picked = budget.Pick(many, viewers, 8);
    CHECK(picked.size() == 8);
    for (const auto &entry : picked)
      ++sent[entry.id];
  }
  for (std::uint32_t n = 1; n <= 6; ++n)
    CHECK(sent[proto::MakeTrafficId(1, n)] == 6);
  for (std::uint32_t n = 7; n <= 12; ++n) {
    CAPTURE(n);
    CHECK(sent[proto::MakeTrafficId(1, n)] == 2); // 6 rounds x 2 seats / 6 cars
  }
}

TEST_CASE("a vehicle at rest leaves the stream, says where it stopped, and is kept alive") {
  traffic::Budget budget;
  const std::vector<proto::Vec3> viewers{{0, 0, 0}};
  std::vector<traffic::Car> cars{CarAt(1, 10)};
  CHECK(budget.Pick(cars, viewers).size() == 1); // moving
  cars[0].velocity = {};
  // Stopped where we last said it was moving: said once more, at rest.
  const auto stop = budget.Pick(cars, viewers);
  REQUIRE(stop.size() == 1);
  CHECK(traffic::Unpack(stop[0]).velocity.x == 0.f);
  // Then nothing, until the keepalive.
  int quiet = 0;
  std::size_t later = 0;
  for (std::uint32_t round = 1; round <= traffic::Budget::kRestEvery; ++round) {
    later = budget.Pick(cars, viewers).size();
    quiet += later == 0;
  }
  CHECK(quiet == static_cast<int>(traffic::Budget::kRestEvery) - 1);
  CHECK(later == 1); // the keepalive
  // It drives off: back in the stream at once.
  cars[0].velocity = {5, 0, 0};
  cars[0].position.x += 0.5f;
  CHECK(budget.Pick(cars, viewers).size() == 1);
  // A parked car pushed along slowly (a ram) moved: it is said.
  cars[0].velocity = {};
  budget.Pick(cars, viewers);
  budget.Pick(cars, viewers);
  cars[0].position.x += 1.f;
  CHECK(budget.Pick(cars, viewers).size() == 1);
}

TEST_CASE("the owner's vehicles reach the guests, a late joiner too, with who sits in them") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  CHECK(host->OwnsArea());
  CHECK_FALSE(a->OwnsArea());
  CHECK(a->AreaOwner(a->You()) == host->You());

  Street atA;
  // A guest owns no area today: what it would announce goes nowhere.
  a->SendTrafficSpawn({.id = proto::MakeTrafficId(2, 1), .record = 0x11});
  host->SendTrafficSpawn({.id = kCar, .record = 0x11, .appearance = 0x22, .position = {1, 2, 3}});
  host->SendTrafficSpawn({.id = kParked,
                          .record = 0x33,
                          .entity = 0xABC,
                          .position = {9, 9, 9},
                          .flags = proto::kTrafficParked});
  host->SendNpcSpawn({.npc = 7, .record = 0x44});
  host->SendNpcSeat({.npc = 7, .owner = proto::TrafficSeatOwner(kCar), .seat = kDriverSeat});
  // States of a vehicle never announced are not sent at all.
  host->SendTrafficStates(States(proto::MakeTrafficId(1, 99), {5, 5, 5}));
  host->SendTrafficStates(States(kCar, {10, 20, 30}));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    return atA.proxies.size() == 2 && atA.moves == 1 && !atA.seats.empty();
  }));
  CHECK(atA.proxies.at(kParked).entity == 0xABC);
  CHECK(atA.proxies.at(kParked).flags == proto::kTrafficParked);
  CHECK(atA.at.at(kCar) == proto::Vec3{10, 20, 30});
  CHECK(atA.seats.front().owner == proto::TrafficSeatOwner(kCar));
  CHECK(atA.strayStates == 0);
  CHECK(host->SharedTraffic().size() == 2);
  CHECK(a->SharedTraffic().size() == 2);

  // It burns, then it is a wreck: said once each, a repeat is not.
  host->SendTrafficStatus({.id = kCar, .health = 0.2f, .flags = proto::kTrafficBurning});
  host->SendTrafficStatus({.id = kCar, .health = 0.2f, .flags = proto::kTrafficBurning});
  host->SendTrafficStatus({.id = kCar, .health = 0.f, .flags = proto::kTrafficDestroyed});
  int changes = 0;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1) {
      for (const auto &event : events)
        changes += std::holds_alternative<events::TrafficChanged>(event);
      atA.See(events);
    }
    return atA.status.contains(kCar) && atA.status.at(kCar).flags == proto::kTrafficDestroyed;
  }));
  Settle({host.get(), a.get()});
  CHECK(changes == 2);

  // A late joiner finds the street as it is now: both vehicles, the wreck
  // where it last was, and the driver in its seat.
  auto late = Endpoint::Join({"late"}, "127.0.0.1", port, error, kQuick);
  REQUIRE(late);
  Street atLate;
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 2)
      atLate.See(events);
    return atLate.proxies.size() == 2 && atLate.status.contains(kCar) && !atLate.seats.empty();
  }));
  CHECK(atLate.proxies.at(kCar).position == proto::Vec3{10, 20, 30});
  CHECK(atLate.status.at(kCar).health == 0.f);
  CHECK(atLate.seats.front() ==
        proto::NpcSeat{.npc = 7, .owner = proto::TrafficSeatOwner(kCar), .seat = kDriverSeat});

  // Out of range: gone for everyone, once; its driver is out of it.
  host->SendTrafficGone({.id = kCar});
  host->SendTrafficGone({.id = kCar});
  REQUIRE(Pump({host.get(), a.get(), late.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    if (i == 2)
      atLate.See(events);
    return atA.left.size() == 1 && atLate.left.size() == 1;
  }));
  Settle({host.get(), a.get(), late.get()});
  CHECK(atA.left.size() == 1);
  CHECK(atA.strayGones == 0);
  CHECK(atA.duplicateSpawns == 0);
  CHECK(atA.seats.back() == proto::NpcSeat{.npc = 7});
  CHECK_FALSE(atA.status.contains(kCar));
  // A state that overtook the removal brings nothing back.
  host->SendTrafficStates(States(kCar, {1, 1, 1}));
  Settle({host.get(), a.get(), late.get()});
  CHECK(a->SharedTraffic().size() == 1);
  // Nothing diverged on the way.
  CHECK(a->Checks().Requests() == 0);
  CHECK(late->Checks().Requests() == 0);
}

TEST_CASE("the first player to get into a shared vehicle has it; everyone hears whose it is") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a && b));
  std::vector<Endpoint *> all{host.get(), a.get(), b.get()};
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->IsJoined() && b->IsJoined(); }));
  host->SendTrafficSpawn({.id = kCar, .record = 0x11});
  host->SendTrafficSpawn({.id = kParked, .record = 0x33, .flags = proto::kTrafficParked});
  host->SendNpcSpawn({.npc = 7, .record = 0x44});
  host->SendNpcSeat({.npc = 7, .owner = proto::TrafficSeatOwner(kCar), .seat = kDriverSeat});
  std::array<Street, 3> streets;
  const auto see = [&](std::size_t i, const auto &events) { streets[i].See(events); };
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    see(i, events);
    return streets[1].proxies.size() == 2 && streets[2].proxies.size() == 2;
  }));

  // Both guests pull the driver out at once: one claim wins.
  a->ClaimTraffic(kCar, kDriverSeat);
  b->ClaimTraffic(kCar, kDriverSeat);
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    see(i, events);
    return !streets[0].left.empty() && !streets[1].left.empty() && !streets[2].left.empty();
  }));
  Settle(all);
  const auto winner = streets[0].left.front().player;
  CHECK((winner == a->You() || winner == b->You()));
  for (const auto &street : streets) {
    REQUIRE(street.left.size() == 1); // the loser's claim is answered by the same news
    CHECK(street.left.front().reason == proto::kTrafficTaken);
    CHECK(street.left.front().player == winner);
  }
  // It is a player's car now: no longer among the shared ones, and whoever
  // sat in it is out.
  CHECK_FALSE(host->SharedTraffic().contains(kCar));
  CHECK_FALSE(a->SharedTraffic().contains(kCar));
  CHECK(streets[1].seats.back() == proto::NpcSeat{.npc = 7});

  // The host takes the parked one itself: the same news, its own game too.
  host->ClaimTraffic(kParked, kDriverSeat);
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    see(i, events);
    return streets[0].left.size() == 2 && streets[1].left.size() == 2 &&
           streets[2].left.size() == 2;
  }));
  CHECK(streets[0].left.back().player == host->You());
  CHECK(streets[2].left.back().player == host->You());
  // A claim on what is gone goes nowhere.
  a->ClaimTraffic(kParked, kDriverSeat);
  Settle(all);
  CHECK(streets[0].left.size() == 2);
  // (The owner's game is told of no spawn: its vehicles are its own.)
  CHECK(streets[1].strayGones + streets[2].strayGones == 0);
}

TEST_CASE("a guest's hit on a shared vehicle reaches its owner, unless its world was rebuilt") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  std::vector<Endpoint *> all{host.get(), a.get()};
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendTrafficSpawn({.id = kCar, .record = 0x11});
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->SharedTraffic().contains(kCar); }));

  std::vector<proto::TrafficHit> hits;
  const auto collect = [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *hit = std::get_if<events::TrafficHitBy>(&event); hit && i == 0)
        hits.push_back(hit->hit);
  };
  // A ram: the guest's car gave it 6 m/s sideways.
  a->SendTrafficHit({.player = 9,
                     .id = kCar,
                     .damage = 80.f,
                     .flags = proto::kTrafficHitCollision,
                     .position = {1, 2, 3},
                     .impulse = {0, 6, 0}});
  // A vehicle the guest does not know is not hit.
  a->SendTrafficHit({.id = kParked, .damage = 10.f});
  // The owner's own hits are its game's.
  host->SendTrafficHit({.id = kCar, .damage = 10.f});
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return !hits.empty();
  }));
  Settle(all);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].player == a->You()); // stamped: a guest cannot hit in another's name
  CHECK(hits[0].impulse == proto::Vec3{0, 6, 0});
  CHECK((hits[0].flags & proto::kTrafficHitCollision) != 0);

  // The host fast travels: a hit and a claim that left before the guest
  // heard of it aim at a street that is gone.
  host->BumpEpoch(proto::kEpochFastTravel);
  a->SendTrafficHit({.id = kCar, .damage = 10.f});
  a->ClaimTraffic(kCar, kDriverSeat);
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    collect(i, events);
    return a->HostEpoch() == host->WorldEpoch() && host->Fenced() == 2;
  }));
  CHECK(hits.size() == 1);
  CHECK(host->SharedTraffic().contains(kCar));
  // The new world's states count; they share the NPC states' numbering, so
  // the same fence covers both.
  Street atA;
  host->SendNpcStates({});
  host->SendTrafficStates(States(kCar, {7, 7, 7}));
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    if (i == 1)
      for (const auto &event : events)
        if (const auto *moved = std::get_if<events::TrafficMoved>(&event))
          atA.at[kCar] = moved->states.cars.front().position;
    return atA.at.contains(kCar);
  }));
}

TEST_CASE("a guest that missed spawns and removals is healed by the digests") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  std::vector<Endpoint *> all{host.get(), a.get()};
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  Street atA;
  host->SendTrafficSpawn({.id = kCar, .record = 0x11});
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    return atA.proxies.contains(kCar);
  }));

  // Everything is lost for a while: one car goes, another comes, a third
  // becomes a wreck.
  host->SetFaults({.drop = 1.0});
  host->SendTrafficGone({.id = kCar});
  host->SendTrafficSpawn({.id = kParked, .record = 0x33, .flags = proto::kTrafficParked});
  host->SendTrafficStatus({.id = kParked, .health = 0.f, .flags = proto::kTrafficDestroyed});
  Settle(all, 100ms);
  CHECK(atA.proxies.contains(kCar));
  host->SetFaults({});
  CHECK(Pump(all, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    return !atA.proxies.contains(kCar) && atA.proxies.contains(kParked) &&
           atA.status.contains(kParked);
  }));
  CHECK(atA.status.at(kParked).flags == proto::kTrafficDestroyed);
  CHECK(atA.duplicateSpawns == 0);
  CHECK(atA.strayGones == 0);
  CHECK(a->Checks().Requests() >= 1);
  // And it stays healed: the same state hashes the same on both sides.
  const auto requests = a->Checks().Requests();
  Settle(all, 500ms);
  CHECK(a->Checks().Requests() == requests);
}

TEST_CASE("state sent twice shows one vehicle, and a hit or a claim is never doubled") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  std::vector<Endpoint *> all{host.get(), a.get()};
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SetFaults({.dup = 1.0});
  a->SetFaults({.dup = 1.0});
  Street atA;
  int hits = 0;
  host->SendTrafficSpawn({.id = kCar, .record = 0x11});
  host->SendTrafficStatus({.id = kCar, .health = 0.5f});
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    return atA.status.contains(kCar);
  }));
  const auto count = [&](std::size_t i, const auto &events) {
    if (i == 1)
      atA.See(events);
    for (const auto &event : events)
      hits += i == 0 && std::holds_alternative<events::TrafficHitBy>(event);
  };
  a->SendTrafficHit({.id = kCar, .damage = 10.f});
  REQUIRE(Pump(all, [&](std::size_t i, const auto &events) {
    count(i, events);
    return hits > 0;
  }));
  host->SendTrafficGone({.id = kCar});
  Pump(
      all,
      [&](std::size_t i, const auto &events) {
        count(i, events);
        return false;
      },
      400ms);
  CHECK(hits == 1);
  CHECK(atA.duplicateSpawns == 0);
  CHECK(atA.strayGones == 0);
  CHECK(atA.left.size() == 1);
  CHECK(consistency::IsOneShot(proto::TrafficHit{}));
  CHECK(consistency::IsOneShot(proto::TrafficClaim{}));
  CHECK_FALSE(consistency::IsOneShot(proto::TrafficSpawn{}));
  CHECK_FALSE(consistency::IsOneShot(proto::TrafficStatus{}));
}

TEST_CASE("a guest that vanishes in a car it took leaves the street as the owner has it") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a && b));
  std::vector<Endpoint *> all{host.get(), a.get(), b.get()};
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->IsJoined() && b->IsJoined(); }));
  host->SendTrafficSpawn({.id = kCar, .record = 0x11});
  host->SendTrafficSpawn({.id = kParked, .record = 0x33, .flags = proto::kTrafficParked});
  REQUIRE(Pump(all, [&](std::size_t, const auto &) { return a->SharedTraffic().size() == 2; }));
  a->ClaimTraffic(kCar, kDriverSeat);
  a->SetLocalSeat({.vehicle = 0x11, .seat = kDriverSeat, .owner = a->You()});
  REQUIRE(Pump(all, [&](std::size_t, const auto &) {
    return !b->SharedTraffic().contains(kCar) && !host->SharedTraffic().contains(kCar);
  }));
  const auto gone = a->You();
  a.reset(); // no Bye: Alt-F4
  bool left = false;
  REQUIRE(Pump(
      {host.get(), b.get()},
      [&](std::size_t i, const auto &events) {
        for (const auto &event : events)
          if (const auto *l = std::get_if<events::PlayerLeft>(&event); l && i == 1)
            left = l->player == gone;
        return left;
      },
      4s));
  // The car it took was its own (it goes with its puppet, the usual rule);
  // the street is untouched and still the same on both sides.
  CHECK(host->SharedTraffic().size() == 1);
  CHECK(b->SharedTraffic().size() == 1);
  CHECK(b->SharedTraffic().contains(kParked));
}
