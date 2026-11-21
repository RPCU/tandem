// The host's quest prefabs in a guest's world and every player's story
// gates, without the game (docs/architecture/story.md, "Quest prefabs",
// "Story gates"): the guest's mirror (what it turns on, and the only ones it
// turns off), the gates' rules, the record, and both messages through real
// endpoints over loopback UDP (a late joiner, a change, the explicit none of
// a resync, a guest's gates passed on and never sent back to it).

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/prefabs.hpp"
#include "tandem/story.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};
constexpr std::uint64_t kKonpeki = 0x1001, kAllFoods = 0x1002, kOwn = 0x1003;

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

// A guest's world as the plugin sees it: each prefab's mode, and which our
// own quests hold.
struct World {
  std::map<std::uint64_t, bool> on;
  std::set<std::uint64_t> held;
  prefabs::Local operator()(std::uint64_t prefab) const {
    const auto it = on.find(prefab);
    return {.on = it != on.end() && it->second, .held = held.contains(prefab)};
  }
  void Apply(const std::vector<prefabs::Step> &steps) {
    for (const auto &step : steps)
      on[step.prefab] = step.on;
  }
};

const proto::WorldPrefabs *PrefabsIn(const std::vector<SessionEvent> &events) {
  for (const auto &event : events)
    if (const auto *story = std::get_if<events::Story>(&event))
      if (const auto *set = std::get_if<proto::WorldPrefabs>(&story->message))
        return set;
  return nullptr;
}

const proto::StoryGates *GatesIn(const std::vector<SessionEvent> &events) {
  for (const auto &event : events)
    if (const auto *story = std::get_if<events::Story>(&event))
      if (const auto *gates = std::get_if<proto::StoryGates>(&story->message))
        return gates;
  return nullptr;
}
} // namespace

TEST_CASE("a guest's mirror turns the host's prefabs on, and only its own off") {
  World world;
  world.on[kOwn] = true; // our own quest's interior, on before the session
  world.held.insert(kOwn);
  prefabs::Mirror mirror;
  const auto query = [&](std::uint64_t prefab) { return world(prefab); };

  mirror.Host(proto::WorldPrefabs{.player = 1, .prefabs = {kKonpeki, kAllFoods, kOwn}});
  auto steps = mirror.Update(true, query);
  CHECK(steps == std::vector<prefabs::Step>{{kKonpeki, true}, {kAllFoods, true}});
  world.Apply(steps);
  CHECK(mirror.Ours() == std::set<std::uint64_t>{kKonpeki, kAllFoods});
  // Nothing to do again.
  CHECK(mirror.Update(true, query).empty());

  // The host's quest moves on: All Foods goes; ours, so off here too.
  mirror.Host(proto::WorldPrefabs{.player = 1, .prefabs = {kKonpeki, kOwn}});
  steps = mirror.Update(true, query);
  CHECK(steps == std::vector<prefabs::Step>{{kAllFoods, false}});
  world.Apply(steps);

  // Our own quest turned Konpeki off (its phase ended): on again, counted.
  world.on[kKonpeki] = false;
  steps = mirror.Update(true, query);
  CHECK(steps == std::vector<prefabs::Step>{{kKonpeki, true}});
  world.Apply(steps);
  CHECK(mirror.Reapplied() == 1);

  // Apart: ours off, our own quest's interior stays.
  steps = mirror.Update(false, query);
  CHECK(steps == std::vector<prefabs::Step>{{kKonpeki, false}});
  world.Apply(steps);
  CHECK(world.on[kOwn]);
  CHECK(mirror.Ours().empty());

  // Grouped again: back on. Then our own quest asks for it too: no longer
  // ours, and the leave leaves it on.
  world.Apply(mirror.Update(true, query));
  CHECK(world.on[kKonpeki]);
  world.held.insert(kKonpeki);
  CHECK(mirror.Update(true, query).empty());
  CHECK(mirror.Ours().empty());
  CHECK(mirror.Lift(query).empty());
  CHECK(world.on[kKonpeki]);
  CHECK(mirror.HostSet().empty());

  // A leave with ours on turns them off; the host's set is forgotten.
  prefabs::Mirror leaving;
  leaving.Host(proto::WorldPrefabs{.player = 1, .prefabs = {kAllFoods}});
  world.Apply(leaving.Update(true, query));
  CHECK(world.on[kAllFoods]);
  steps = leaving.Lift(query);
  CHECK(steps == std::vector<prefabs::Step>{{kAllFoods, false}});
  CHECK(leaving.Update(true, query).empty());
}

TEST_CASE("story gates: behind at the join, met in play once per crossing") {
  using gates::Gate;
  constexpr std::uint8_t open = proto::kGateWatsonOpen | proto::kGateDogtownOpen;
  // The host further: both gates behind for a guest in Act 1.
  CHECK(gates::Behind(open, proto::kGateInWatson) ==
        std::vector<Gate>{Gate::Watson, Gate::Dogtown});
  CHECK(gates::Behind(open, proto::kGateWatsonOpen) == std::vector<Gate>{Gate::Dogtown});
  CHECK(gates::Behind(proto::kGateWatsonOpen, open).empty()); // the guest further: nothing
  CHECK(gates::Behind(0, 0).empty());

  // Watson: the guest locked in, the host just out, 100 m away.
  const std::uint8_t hostOut = open, guestIn = proto::kGateInWatson;
  CHECK(gates::Met(hostOut, guestIn, true, 100.f) == Gate::Watson);
  CHECK_FALSE(gates::Met(hostOut, guestIn, true, 400.f).has_value()); // too far: not at the barrier
  CHECK_FALSE(gates::Met(hostOut, guestIn, false, 10.f).has_value()); // the host not drawn
  CHECK_FALSE(
      gates::Met(open | proto::kGateInWatson, guestIn, true, 10.f).has_value()); // both in Watson
  CHECK_FALSE(
      gates::Met(hostOut, guestIn | proto::kGateWatsonOpen, true, 10.f).has_value()); // lifted
  // Dogtown: the guest outside its closed gate, the host inside.
  const std::uint8_t guestOut = proto::kGateWatsonOpen;
  CHECK(gates::Met(open | proto::kGateInDogtown, guestOut, true, 50.f) == Gate::Dogtown);
  CHECK_FALSE(gates::Met(open, guestOut, true, 50.f).has_value()); // the host outside too

  gates::Watcher watcher;
  CHECK(watcher.Update(hostOut, guestIn, true, 100.f) == Gate::Watson);
  CHECK_FALSE(watcher.Update(hostOut, guestIn, true, 120.f).has_value());
  // Walking off and back is the same crossing.
  CHECK_FALSE(watcher.Update(hostOut, guestIn, true, 600.f).has_value());
  CHECK_FALSE(watcher.Update(hostOut, guestIn, true, 100.f).has_value());
  // The host back in Watson, out again: told again.
  CHECK_FALSE(watcher.Update(hostOut | proto::kGateInWatson, guestIn, true, 5.f).has_value());
  CHECK(watcher.Update(hostOut, guestIn, true, 100.f) == Gate::Watson);
}

TEST_CASE("the story record keeps the host's prefabs and every player's gates for joiners") {
  story::Record record;
  const auto none = record.Digest();
  CHECK(record.Note(proto::WorldPrefabs{.player = 1, .prefabs = {kKonpeki}}));
  CHECK_FALSE(record.Note(proto::WorldPrefabs{.player = 1, .prefabs = {kKonpeki}}));
  CHECK(record.PrefabsOn());
  const auto with = record.Digest();
  CHECK(with != none);
  CHECK(record.Note(proto::StoryGates{.player = 2, .flags = proto::kGateInWatson}));
  CHECK_FALSE(record.Note(proto::StoryGates{.player = 2, .flags = proto::kGateInWatson}));
  CHECK(record.Digest() == with); // a player's own word: not the host's world
  const auto joiner = record.ForJoiner();
  REQUIRE(joiner.size() == 2);
  CHECK(std::get<proto::WorldPrefabs>(joiner[0]).prefabs == std::vector<std::uint64_t>{kKonpeki});
  CHECK(std::get<proto::StoryGates>(joiner[1]).player == 2);
  record.Forget(2);
  CHECK_FALSE(record.GatesOf(2).has_value());
  CHECK(record.Note(proto::WorldPrefabs{.player = 1}));
  CHECK_FALSE(record.PrefabsOn());
  CHECK(record.ForJoiner().empty());
  CHECK(record.Digest() == none);
}

TEST_CASE("world prefabs and story gates through the endpoints") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kFast);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));

  // Unsorted, a repeat and a zero inside: the guest hears one sorted set,
  // stamped with the host. A guest cannot speak for the host's world.
  host->SendStory(proto::WorldPrefabs{.player = 9, .prefabs = {kAllFoods, kKonpeki, 0, kAllFoods}});
  a->SendStory(proto::WorldPrefabs{.prefabs = {kOwn}});
  std::optional<proto::WorldPrefabs> heard;
  std::size_t hostHeard = 0;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1) {
      if (const auto *set = PrefabsIn(events))
        heard = *set;
    } else if (PrefabsIn(events)) {
      ++hostHeard;
    }
    return heard.has_value();
  }));
  CHECK(heard->player == 1);
  CHECK(heard->prefabs == std::vector<std::uint64_t>{kKonpeki, kAllFoods});
  CHECK(hostHeard == 0);

  // The guest's gates reach the host, stamped; never back to the guest.
  a->SendStory(proto::StoryGates{.player = 7, .flags = proto::kGateInWatson});
  std::optional<proto::StoryGates> atHost;
  bool backAtA = false;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    if (i == 0) {
      if (const auto *gates = GatesIn(events))
        atHost = *gates;
    } else if (GatesIn(events)) {
      backAtA = true;
    }
    return atHost.has_value();
  }));
  CHECK(atHost->player == a->You());
  CHECK(atHost->flags == proto::kGateInWatson);
  // The host's own gates go to the guest.
  host->SendStory(proto::StoryGates{.flags = proto::kGateWatsonOpen | proto::kGateDogtownOpen});

  // A late joiner gets the host's prefabs, the host's gates and a's.
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE(b);
  std::optional<proto::WorldPrefabs> atB;
  std::map<proto::PlayerId, std::uint8_t> gatesAtB;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1 && GatesIn(events) && GatesIn(events)->player == a->You())
      backAtA = true;
    if (i == 2) {
      if (const auto *set = PrefabsIn(events))
        atB = *set;
      for (const auto &event : events)
        if (const auto *story = std::get_if<events::Story>(&event))
          if (const auto *gates = std::get_if<proto::StoryGates>(&story->message))
            gatesAtB[gates->player] = gates->flags;
    }
    return atB.has_value() && gatesAtB.size() == 2;
  }));
  CHECK(atB->prefabs == std::vector<std::uint64_t>{kKonpeki, kAllFoods});
  CHECK(gatesAtB[1] == (proto::kGateWatsonOpen | proto::kGateDogtownOpen));
  CHECK(gatesAtB[a->You()] == proto::kGateInWatson);
  CHECK_FALSE(backAtA);

  // The host's quest moves on: none, for everyone.
  host->SendStory(proto::WorldPrefabs{});
  bool emptyAtA = false, emptyAtB = false;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    if (const auto *set = PrefabsIn(events); set && set->prefabs.empty()) {
      if (i == 1)
        emptyAtA = true;
      if (i == 2)
        emptyAtB = true;
    }
    return emptyAtA && emptyAtB;
  }));
}
