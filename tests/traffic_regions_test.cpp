// World vehicles of several owners through real endpoints over loopback UDP
// (docs/architecture/traffic.md, "Who owns an area"): a guest that owns its
// area shares its traffic, the players linked with it show proxies, every
// request goes to the vehicle's owner, and a vehicle is handed over when
// players part, with its state and its driver, never popped or doubled.

#include "ports.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using proto::MakeTrafficId;
using test::NextPort;

constexpr Timing kQuick{
    .timeout = 1000ms, .takeover = 400ms, .digest = 100ms, .resyncCooldown = 200ms};

// What one game does with its endpoint's street events: the proxies it
// shows, the vehicles it runs, and its answers to hand-overs (as Traffic.reds).
struct Game {
  Endpoint *endpoint{};
  std::set<proto::TrafficId> proxies, running;
  std::map<proto::TrafficId, proto::Vec3> where; // the vehicles we run
  std::map<proto::TrafficId, proto::TrafficHandoverState> took;
  std::set<proto::NpcId> npcProxies, npcs;
  std::vector<proto::TrafficId> asked;
  std::vector<proto::TrafficGone> left;
  std::vector<proto::Chase> chases;
  int duplicateSpawns{}, strayGones{}, moved{}, hits{}, vehicleHits{}, statuses{}, lost{};
  bool refuse{}, fail{};

  void Announce(std::uint32_t counter, proto::Vec3 at, std::uint16_t flags = 0) {
    const auto id = MakeTrafficId(endpoint->You(), counter);
    running.insert(id);
    endpoint->SendTrafficSpawn({.id = id, .record = 500 + counter, .position = at, .flags = flags});
    where[id] = at;
  }
  void Drive(proto::TrafficId id, proto::Vec3 at) {
    where[id] = at;
    endpoint->SendTrafficStates({.cars = {{.id = id, .position = at}}});
  }

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *spawn = std::get_if<events::TrafficSpawned>(&event)) {
        duplicateSpawns += proxies.insert(spawn->spawn.id).second ? 0 : 1;
      } else if (const auto *gone = std::get_if<events::TrafficLeft>(&event)) {
        left.push_back(gone->gone);
        const bool known = proxies.erase(gone->gone.id) + running.erase(gone->gone.id) > 0;
        strayGones += known ? 0 : 1;
      } else if (const auto *m = std::get_if<events::TrafficMoved>(&event)) {
        moved += static_cast<int>(m->states.cars.size());
      } else if (std::holds_alternative<events::TrafficChanged>(event)) {
        ++statuses;
      } else if (std::holds_alternative<events::TrafficHitBy>(event)) {
        ++hits;
      } else if (std::holds_alternative<events::VehicleHitBy>(event)) {
        ++vehicleHits;
      } else if (const auto *chase = std::get_if<events::ChaseChanged>(&event)) {
        chases.push_back(chase->chase);
      } else if (const auto *give = std::get_if<events::TrafficGive>(&event)) {
        asked.push_back(give->id);
        if (refuse) {
          endpoint->AnswerTraffic(give->id, false);
          continue;
        }
        // Our body stays as its proxy.
        running.erase(give->id);
        proxies.insert(give->id);
        endpoint->GiveTraffic({.id = give->id,
                               .to = give->to,
                               .position = where[give->id],
                               .velocity = {0.f, 7.f, 0.f},
                               .health = 0.6f,
                               .flags = proto::kTrafficLights});
      } else if (const auto *take = std::get_if<events::TrafficTake>(&event)) {
        if (fail) {
          proxies.erase(take->state.id);
          endpoint->AnswerTraffic(take->state.id, false);
          continue;
        }
        // Our proxy is the real vehicle now.
        proxies.erase(take->state.id);
        running.insert(take->state.id);
        took[take->state.id] = take->state;
        where[take->state.id] = take->state.position;
        endpoint->AnswerTraffic(take->state.id, true);
      } else if (const auto *owner = std::get_if<events::TrafficOwnerChanged>(&event)) {
        if (owner->was == endpoint->You() && running.erase(owner->id) > 0) {
          proxies.insert(owner->id);
          ++lost; // taken from us without our giving it (our world was rebuilt)
        }
      } else if (const auto *spawn = std::get_if<events::HostNpcSpawn>(&event)) {
        npcProxies.insert(spawn->spawn.npc);
      } else if (const auto *take = std::get_if<events::NpcTake>(&event)) {
        npcProxies.erase(take->state.npc);
        npcs.insert(take->state.npc);
        endpoint->AdoptNpc(take->state.npc, true);
      } else if (const auto *owner = std::get_if<events::NpcOwnerChanged>(&event)) {
        if (owner->was == endpoint->You() && npcs.erase(owner->npc) > 0)
          npcProxies.insert(owner->npc);
      }
    }
  }
};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game h, ga, gb;

  std::vector<Game *> Games() {
    std::vector<Game *> out{&h};
    if (a)
      out.push_back(&ga);
    if (b)
      out.push_back(&gb);
    return out;
  }
  bool Until(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
      for (auto *game : Games())
        game->See(game->endpoint->Update(1ms));
      if (done())
        return true;
    }
    return false;
  }
  void For(std::chrono::milliseconds time) {
    Until([] { return false; }, time);
  }
};

void Place(Endpoint &endpoint, float x) { endpoint.SendLocalState({.position = {x, 0.f, 0.f}}); }

std::unique_ptr<Session> Start(bool three = false) {
  const auto port = NextPort();
  std::string error;
  auto s = std::make_unique<Session>();
  s->host = Endpoint::Host({"host"}, port, error, kQuick);
  s->a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((s->host && s->a));
  if (three) {
    s->b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kQuick);
    REQUIRE(s->b);
  }
  s->h.endpoint = s->host.get();
  s->ga.endpoint = s->a.get();
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] {
    return s->a->Players().size() == (three ? 3u : 2u) && (!three || s->b->Players().size() == 3u);
  }));
  return s;
}

std::set<proto::TrafficId> Keys(const std::map<proto::TrafficId, proto::TrafficSpawn> &cars) {
  std::set<proto::TrafficId> out;
  for (const auto &[id, spawn] : cars)
    out.insert(id);
  return out;
}
} // namespace

TEST_CASE("a guest that owns its area shares its vehicles with the players linked with it") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 600);
  Place(*s->b, 0);
  // Apart: a runs its own street; the host keeps the record, nobody shows it.
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && s->a->Linked().empty(); }));
  s->ga.Announce(1, {600.f, 10.f, 0.f});
  s->ga.Announce(2, {610.f, 0.f, 0.f}, proto::kTrafficParked);
  const auto one = MakeTrafficId(s->a->You(), 1), two = MakeTrafficId(s->a->You(), 2);
  REQUIRE(s->Until([&] { return s->host->SharedTraffic().size() == 2; }));
  s->ga.Drive(one, {600.f, 12.f, 0.f});
  s->For(300ms);
  // Where it is, all the same (once a second): a link shows it from there.
  CHECK(s->host->SharedTraffic().at(one).position.y == doctest::Approx(12.f));
  CHECK(s->h.proxies.empty());
  CHECK(s->gb.proxies.empty());
  CHECK(s->host->ProxiedTraffic().empty());
  CHECK(s->host->TrafficOwnerOf(one) == s->a->You());
  CHECK(s->a->OwnsTraffic(one));

  // a comes within 150 m: the host and b show both, with a's states.
  Place(*s->a, 120);
  REQUIRE(s->Until([&] { return s->h.proxies.size() == 2 && s->gb.proxies.size() == 2; }));
  CHECK(Keys(s->host->ProxiedTraffic()) == std::set<proto::TrafficId>{one, two});
  CHECK(Keys(s->b->ProxiedTraffic()) == std::set<proto::TrafficId>{one, two});
  CHECK(s->a->ProxiedTraffic().empty());
  s->ga.Drive(one, {118.f, 12.f, 0.f});
  s->a->SendTrafficStatus({.id = one, .health = 0.5f, .flags = proto::kTrafficLights});
  REQUIRE(s->Until([&] { return s->h.moved > 0 && s->gb.moved > 0 && s->gb.statuses > 0; }));
  CHECK(s->ga.moved == 0); // never its own back
  CHECK(s->host->TrafficStatuses().at(one).health == doctest::Approx(0.5f));

  // Nobody else speaks for a's vehicles.
  s->b->SendTrafficSpawn({.id = MakeTrafficId(s->a->You(), 9), .record = 1});
  s->b->SendTrafficStatus({.id = one, .health = 0.f, .flags = proto::kTrafficDestroyed});
  s->b->SendTrafficGone({.id = two});
  s->host->SendTrafficGone({.id = two});
  s->For(300ms);
  CHECK(s->host->SharedTraffic().size() == 2);
  CHECK(s->host->TrafficStatuses().at(one).health == doctest::Approx(0.5f));

  // Deep in the host's region a's game no longer announces new vehicles
  // (the host's does), but still speaks for its own.
  Place(*s->a, 20);
  REQUIRE(s->Until([&] { return !s->a->OwnsArea(); }));
  s->ga.Announce(3, {20.f, 5.f, 0.f});
  s->a->SendTrafficGone({.id = two});
  s->For(300ms);
  CHECK_FALSE(s->host->SharedTraffic().contains(MakeTrafficId(s->a->You(), 3)));
  CHECK_FALSE(s->host->SharedTraffic().contains(two));
  CHECK_FALSE(s->h.proxies.contains(two));
  CHECK(s->h.duplicateSpawns + s->gb.duplicateSpawns == 0);
  CHECK(s->h.strayGones + s->gb.strayGones == 0);

  // Apart again: their proxies go.
  Place(*s->a, 600);
  REQUIRE(s->Until([&] { return s->h.proxies.empty() && s->gb.proxies.empty(); }));
  CHECK(s->host->SharedTraffic().contains(one)); // still a's, in the record
}

TEST_CASE("hits, claims, tyres, rams and chases of a guest's vehicle reach that guest") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 300);
  Place(*s->b, 280);
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && s->a->Linked().size() == 1; }));
  s->ga.Announce(1, {290.f, 0.f, 0.f});
  s->ga.Announce(2, {295.f, 0.f, 0.f});
  const auto one = MakeTrafficId(s->a->You(), 1), two = MakeTrafficId(s->a->You(), 2);
  REQUIRE(s->Until([&] { return s->gb.proxies.size() == 2; }));
  CHECK(s->h.proxies.empty()); // the host is 300 m away: not linked

  // b's ram and its shot at a tyre: to a, not the host.
  s->b->SendTrafficHit({.id = one, .damage = 200.f, .flags = proto::kTrafficHitCollision});
  s->b->SendVehicleHit(
      {.vehicle = proto::StreetCar(one), .flags = proto::kVehicleHitTyre, .tyre = 1});
  REQUIRE(s->Until([&] { return s->ga.hits == 1 && s->ga.vehicleHits == 1; }));
  CHECK(s->h.hits + s->h.vehicleHits == 0);
  // a's own hit on its vehicle is its game's.
  s->a->SendTrafficHit({.id = one, .damage = 5.f});
  s->For(200ms);
  CHECK(s->ga.hits == 1);

  // a's chase after b: b hears it; b cannot say a's vehicle chases.
  s->a->SendChase({.vehicle = one, .target = s->b->You(), .flags = proto::kChaseRamming});
  REQUIRE(s->Until([&] { return !s->gb.chases.empty(); }));
  CHECK(s->gb.chases.back().target == s->b->You());
  CHECK(s->b->Chases().contains(one));
  s->b->SendChase({.vehicle = one, .target = s->a->You()});
  s->For(200ms);
  CHECK(s->host->Chases().at(one).target == s->b->You());

  // b gets into a's second car: a decides, everyone hears whose it is.
  s->b->ClaimTraffic(two, 1);
  REQUIRE(s->Until([&] {
    return !s->ga.left.empty() && !s->gb.left.empty() && !s->host->SharedTraffic().contains(two);
  }));
  CHECK(s->ga.left.back().reason == proto::kTrafficTaken);
  CHECK(s->ga.left.back().player == s->b->You());
  CHECK(s->gb.left.back().player == s->b->You());
  // A second claim on it finds nothing.
  s->host->ClaimTraffic(two, 1);
  s->For(200ms);
  CHECK(s->ga.left.size() == 1);

  // The chase ends with its target.
  s->a->SendChase({.vehicle = one, .target = proto::kNoPlayer});
  REQUIRE(s->Until([&] { return s->gb.chases.back().target == proto::kNoPlayer; }));
}

TEST_CASE("players part: a vehicle goes to the one beside it with its state and its driver") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 70);
  Place(*s->b, 400);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  s->ga.Announce(1, {30.f, 0.f, 0.f}, proto::kTrafficLights);
  const auto car = MakeTrafficId(s->a->You(), 1);
  // Its driver: a's NPC, seated in it.
  const auto driver = regions::MakeNpcId(s->a->You(), 1);
  s->a->SendNpcSpawn({.npc = driver, .record = 9, .position = {30.f, 0.f, 0.f}});
  s->a->SendNpcSeat({.npc = driver, .owner = proto::TrafficSeatOwner(car), .seat = 7});
  s->ga.npcs.insert(driver);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(car) && s->h.npcProxies.contains(driver); }));
  // Meeting moves nothing: both keep what they run.
  s->For(500ms);
  CHECK(s->ga.asked.empty());
  CHECK(s->a->OwnsTraffic(car));

  // a walks off; the host stands 30 m from the car: the host runs it now.
  Place(*s->a, 140);
  REQUIRE(s->Until([&] { return s->h.running.contains(car) && s->host->OwnsTraffic(car); }));
  REQUIRE(s->Until([&] { return s->host->TrafficHandovers() == 1; }));
  // The state came with it.
  const auto &state = s->h.took.at(car);
  CHECK(state.from == s->a->You());
  CHECK(state.health == doctest::Approx(0.6f));
  CHECK(state.velocity.y == doctest::Approx(7.f));
  CHECK((state.handover & proto::kHandoverInherited) == 0);
  // No pop: the giver's body is a proxy, the receiver's proxy runs.
  CHECK(s->ga.proxies == std::set<proto::TrafficId>{car});
  CHECK(s->ga.running.empty());
  CHECK(s->h.proxies.empty());
  // Its driver went with it, still in its seat.
  REQUIRE(s->Until([&] { return s->host->NpcOwner(driver) == 1u && s->h.npcs.contains(driver); }));
  CHECK(s->a->NpcOwner(driver) == 1u);
  REQUIRE(s->Until([&] { return s->a->TrafficOwnerOf(car) == 1u; }));
  // The record says how it stood when given.
  CHECK(s->host->TrafficStatuses().at(car).health == doctest::Approx(0.6f));

  // The new owner's word counts, the old owner's no longer.
  s->h.Drive(car, {32.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->ga.moved > 0; }));
  s->a->SendTrafficGone({.id = car});
  s->a->SendTrafficStatus({.id = car, .health = 0.f});
  s->For(200ms);
  CHECK(s->host->SharedTraffic().contains(car));
  // A hit on it from a goes to its new owner.
  s->a->SendTrafficHit({.id = car, .damage = 50.f});
  REQUIRE(s->Until([&] { return s->h.hits == 1; }));

  // And back: the host walks off, a comes back to it.
  Place(*s->host, -60);
  Place(*s->a, 45); // 13 m from the car: not on top of it (regions::ClearOf)
  REQUIRE(s->Until([&] { return s->ga.running.contains(car) && s->a->OwnsTraffic(car); }));
  REQUIRE(s->Until([&] { return s->host->TrafficOwnerOf(car) == s->a->You(); }));
  CHECK(s->h.proxies == std::set<proto::TrafficId>{car});
  // b, far off all along, never saw any of it.
  CHECK(s->gb.proxies.empty());
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns + s->gb.duplicateSpawns == 0);
  CHECK(s->h.strayGones + s->ga.strayGones + s->gb.strayGones == 0);
}

TEST_CASE("a vehicle on top of a player's V is not handed to that player until it is clear") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 70);
  Place(*s->b, 400);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  // a's car, 2 m from the host's V: it ran into it, or carries it.
  s->ga.Announce(1, {2.f, 0.f, 0.f});
  const auto car = MakeTrafficId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(car); }));
  // a walks off: any other car would be the host's now. This one stays a's.
  Place(*s->a, 140);
  s->For(1500ms);
  CHECK(s->ga.asked.empty());
  CHECK(s->a->OwnsTraffic(car));
  CHECK(s->host->TrafficHandovers() == 0);
  // It drives clear of the host's V: handed over like any other.
  s->ga.Drive(car, {30.f, 0.f, 0.f});
  REQUIRE(s->Until([&] { return s->h.running.contains(car) && s->host->OwnsTraffic(car); }));
}

TEST_CASE("a vehicle driven remotely is its driver's game's, and stays so while it drives") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 70);
  Place(*s->b, 400);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  // The host's car, 40 m from the host's V and 30 m from a's: a's quickhack
  // takes control of it (its game drives its proxy, its V stays put).
  s->h.Announce(1, {40.f, 0.f, 0.f});
  const auto car = MakeTrafficId(s->host->You(), 1);
  REQUIRE(s->Until([&] { return s->ga.proxies.contains(car); }));
  const auto vehicle = static_cast<std::uint32_t>(proto::RemoteSort::Vehicle);
  s->a->SetLocalActivity({.kind = proto::ActivityKind::Remote,
                          .device = 77,
                          .entry = vehicle,
                          .view = {40.f, 0.f, 0.f},
                          .traffic = car});
  // Handed to a at once (no distance rule), with its state.
  REQUIRE(s->Until([&] { return s->ga.running.contains(car) && s->a->OwnsTraffic(car); }));
  REQUIRE(s->Until([&] { return s->host->TrafficOwnerOf(car) == s->a->You(); }));
  CHECK(s->h.proxies.contains(car));
  // a drives it beside the host's V and away from its own: the rule that
  // gives a car to the V beside it does not take it from its driver.
  s->ga.Drive(car, {10.f, 0.f, 0.f});
  Place(*s->a, 140);
  s->For(1500ms);
  CHECK(s->a->OwnsTraffic(car));
  CHECK(s->h.asked.empty());
  CHECK(s->ga.asked.empty());
  // b's takeover of it meanwhile asks nothing of a (its game refuses it).
  s->b->SetLocalActivity({.kind = proto::ActivityKind::Remote, .entry = vehicle, .traffic = car});
  s->For(500ms);
  CHECK(s->a->OwnsTraffic(car));
  CHECK(s->ga.asked.empty());
  s->b->SetLocalActivity({});
  // Out of the car: a car like any other, the host's again by the usual rule.
  s->a->SetLocalActivity({});
  REQUIRE(s->Until([&] { return s->h.running.contains(car) && s->host->OwnsTraffic(car); }));
  CHECK(s->h.duplicateSpawns + s->ga.duplicateSpawns + s->gb.duplicateSpawns == 0);
}

TEST_CASE("a vehicle handed to a third player: the others show it from its new owner") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, -40);
  Place(*s->b, 60);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 2 && s->b->OwnsArea(); }));
  s->gb.Announce(1, {50.f, 0.f, 0.f});
  const auto car = MakeTrafficId(s->b->You(), 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(car) && s->ga.proxies.contains(car); }));
  // b leaves; the host outranks a: the host takes it, a sees the new owner.
  Place(*s->b, 140);
  REQUIRE(s->Until([&] { return s->h.running.contains(car); }));
  REQUIRE(s->Until([&] { return s->a->TrafficOwnerOf(car) == 1u; }));
  CHECK(s->ga.proxies.contains(car));
  CHECK(s->ga.duplicateSpawns == 0);
}

TEST_CASE("an owner may refuse; a receiver that cannot run it lets it go for everyone") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 70);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  s->ga.Announce(1, {30.f, 0.f, 0.f});
  const auto car = MakeTrafficId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(car); }));
  // a refuses (its V is getting in): asked again only after a while.
  s->ga.refuse = true;
  Place(*s->a, 140);
  REQUIRE(s->Until([&] { return !s->ga.asked.empty(); }));
  s->For(1500ms);
  CHECK(s->ga.asked.size() == 1);
  CHECK(s->a->OwnsTraffic(car));
  // a gives it, the host cannot run it: it goes.
  s->ga.refuse = false;
  s->h.fail = true;
  REQUIRE(s->Until([&] { return !s->host->SharedTraffic().contains(car); }, 8s));
  REQUIRE(s->Until([&] { return !s->a->SharedTraffic().contains(car); }));
  CHECK(s->h.proxies.empty());
}

TEST_CASE("an owner that vanishes: its vehicles go to the player near them, the rest go") {
  auto s = Start(true);
  Place(*s->host, 0);
  Place(*s->a, 70);
  Place(*s->b, 60);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 2 && s->a->OwnsArea(); }));
  s->ga.Announce(1, {30.f, 0.f, 0.f});
  s->ga.Announce(2, {40.f, 300.f, 0.f});
  const auto near = MakeTrafficId(s->a->You(), 1), far = MakeTrafficId(s->a->You(), 2);
  REQUIRE(
      s->Until([&] { return s->host->SharedTraffic().size() == 2 && s->gb.proxies.size() == 2; }));
  // a is gone without a goodbye.
  s->a.reset();
  REQUIRE(s->Until([&] { return s->h.running.contains(near); }, 4s));
  CHECK(s->host->OwnsTraffic(near));
  CHECK((s->h.took.at(near).handover & proto::kHandoverInherited) != 0);
  REQUIRE(s->Until([&] { return !s->host->SharedTraffic().contains(far); }));
  REQUIRE(
      s->Until([&] { return s->b->TrafficOwnerOf(near) == 1u && !s->gb.proxies.contains(far); }));
  CHECK(s->gb.proxies == std::set<proto::TrafficId>{near});
  CHECK(s->host->TrafficInherited() == 1);
}

TEST_CASE("an owner whose world is rebuilt leaves its vehicles to who stays") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 70);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  s->ga.Announce(1, {10.f, 0.f, 0.f});
  const auto car = MakeTrafficId(s->a->You(), 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(car); }));
  s->a->BumpEpoch(proto::kEpochFastTravel);
  REQUIRE(s->Until([&] { return s->h.running.contains(car) && s->ga.lost == 1; }));
  CHECK(s->a->TrafficOwnerOf(car) == 1u);
  CHECK(s->ga.proxies.contains(car));
}

TEST_CASE("the host's record of a guest's vehicles is healed when a spawn was lost") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 600);
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && s->a->Linked().empty(); }));
  // Everything a says is lost for a moment: its spawn never arrives.
  s->a->SetFaults({.drop = 1.0});
  s->ga.Announce(1, {600.f, 0.f, 0.f});
  s->For(100ms);
  s->a->SetFaults({});
  CHECK(s->host->SharedTraffic().empty());
  // The owned digest differs twice: a sends its set again.
  REQUIRE(s->Until([&] { return s->host->SharedTraffic().size() == 1; }, 5s));
  // And a vehicle a no longer has (its removal lost) leaves the record.
  s->a->SetFaults({.drop = 1.0});
  s->a->SendTrafficGone({.id = MakeTrafficId(s->a->You(), 1)});
  s->For(100ms);
  s->a->SetFaults({});
  REQUIRE(s->Until([&] { return s->host->SharedTraffic().empty(); }, 5s));
}

TEST_CASE("a late joiner sees a guest's vehicles, and who runs them, when it is near") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 70);
  REQUIRE(s->Until([&] { return s->a->Linked().size() == 1 && s->a->OwnsArea(); }));
  s->ga.Announce(1, {30.f, 0.f, 0.f});
  s->h.Announce(1, {40.f, 10.f, 0.f});
  const auto theirs = MakeTrafficId(s->a->You(), 1), ours = MakeTrafficId(1, 1);
  REQUIRE(s->Until([&] { return s->h.proxies.contains(theirs) && s->ga.proxies.contains(ours); }));
  // The host's own vehicle goes to a when the host leaves it.
  Place(*s->host, -90);
  REQUIRE(s->Until([&] { return s->ga.running.contains(ours); }));
  // b joins near both.
  std::string error;
  s->b = Endpoint::Join({"b"}, "127.0.0.1", s->host->LocalPort(), error, kQuick);
  REQUIRE(s->b);
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] { return s->b->IsJoined(); }));
  Place(*s->b, 35);
  REQUIRE(s->Until([&] {
    return s->gb.proxies == std::set<proto::TrafficId>{theirs, ours} &&
           s->b->TrafficOwnerOf(ours) == s->a->You();
  }));
  CHECK(s->gb.duplicateSpawns == 0);
}
