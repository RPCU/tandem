// Consistency (docs/architecture/consistency.md): digests, resyncs, epochs, idempotent
// application, and sessions that converge although messages are lost or
// sent twice. Unit tests, then a real host and guests over loopback UDP.

#include "ports.hpp"
#include "referee_expect.hpp"
#include "tandem/consistency.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/story.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>
#include <set>

using namespace std::chrono_literals;
using namespace tandem;

TEST_CASE("digests: the same state hashes the same, any change shows") {
  std::map<proto::NpcId, proto::NpcSpawn> npcs{{1, {.npc = 1, .record = 10}},
                                               {2, {.npc = 2, .record = 20}}};
  std::map<proto::NpcId, proto::NpcHealth> health{{1, {.npc = 1, .health = 0.5f}}};
  const auto base = consistency::NpcDigest(npcs, health);
  CHECK(consistency::NpcDigest(npcs, health) == base);
  // Positions are state (newest wins): not in the digest.
  npcs[1].position = {5, 5, 5};
  CHECK(consistency::NpcDigest(npcs, health) == base);
  health[1].health = 0.4f;
  CHECK(consistency::NpcDigest(npcs, health) != base);
  health[1].health = 0.5f;
  npcs.erase(2);
  CHECK(consistency::NpcDigest(npcs, health) != base);
  // Health of an NPC no longer shared counts for nothing.
  health[2] = {.npc = 2, .health = 0.1f};
  CHECK(consistency::NpcDigest(npcs, health) == consistency::NpcDigest(npcs, {{1, health[1]}}));

  CHECK(consistency::WorldDigest(std::nullopt) != consistency::WorldDigest(proto::World{}));
  CHECK(consistency::WorldDigest(proto::World{.weather = 1}) !=
        consistency::WorldDigest(proto::World{.weather = 2}));
}

TEST_CASE("the players digest leaves out what is the viewer's own") {
  std::map<proto::PlayerId, proto::PlayerInfo> roster{{1, {1, "host"}}, {2, {2, "guest"}}};
  std::map<proto::PlayerId, proto::Appearance> looks{{1, {.player = 1, .revision = 1}}};
  std::map<proto::PlayerId, proto::Held> held;
  std::map<proto::PlayerId, proto::Seat> seats;
  std::map<proto::PlayerId, proto::VehicleRadio> radios;
  std::map<proto::PlayerId, proto::Stealth> stealth;
  std::set<proto::PlayerId> paused;
  std::map<proto::PlayerId, proto::TimeEffect> time;
  std::map<proto::PlayerId, proto::Vitals> lives;
  std::map<proto::PlayerId, proto::Elsewhere> elsewhere;
  std::map<proto::PlayerId, proto::Heat> heat;
  std::map<proto::PlayerId, proto::Activity> activities;
  const consistency::PlayersView view{roster, looks, held,  seats,     radios, stealth,
                                      paused, time,  lives, elsewhere, heat,   activities};
  const auto before = consistency::PlayersDigest(view, 2);
  // The guest's own look and pause are its game's: the host's record of
  // them does not count in the guest's digest.
  looks[2] = {.player = 2, .revision = 7};
  paused.insert(2);
  CHECK(consistency::PlayersDigest(view, 2) == before);
  CHECK(consistency::PlayersDigest(view, 1) != consistency::PlayersDigest(view, 2));
  // Someone else's counts.
  seats[1] = {.player = 1, .vehicle = 5};
  CHECK(consistency::PlayersDigest(view, 2) != before);
  // Another world: the host's dive counts for the guest, the guest's own not.
  const auto seated = consistency::PlayersDigest(view, 2);
  elsewhere[2] = {.player = 2, .place = proto::Place::Braindance};
  CHECK(consistency::PlayersDigest(view, 2) == seated);
  elsewhere[1] = {.player = 1, .place = proto::Place::Cyberspace, .flags = 1, .anchor = {1, 2, 3}};
  CHECK(consistency::PlayersDigest(view, 2) != seated);
  // Wanted: the host's stars count for the guest, the guest's own do not,
  // and no heat is the same as never heard of.
  const auto away = consistency::PlayersDigest(view, 2);
  heat[2] = {.player = 2, .stage = 3, .state = proto::HeatState::Active};
  heat[1] = {.player = 1};
  CHECK(consistency::PlayersDigest(view, 2) == away);
  heat[1] = {.player = 1, .stage = 2, .state = proto::HeatState::Searching};
  CHECK(consistency::PlayersDigest(view, 2) != away);
  // Busy: the host on a bench counts for the guest, the guest's own not.
  const auto busy = consistency::PlayersDigest(view, 2);
  activities[2] = {.player = 2, .kind = proto::ActivityKind::Photo};
  CHECK(consistency::PlayersDigest(view, 2) == busy);
  activities[1] = {.player = 1,
                   .kind = proto::ActivityKind::Workspot,
                   .flags = proto::kActivityAnchored,
                   .workspot = 9,
                   .anchor = {4, 5, 6}};
  CHECK(consistency::PlayersDigest(view, 2) != busy);

  // Who is linked with whom (regions): a lost Regions is a difference.
  std::vector<proto::PlayerId> pairs{1, 2}, none;
  auto linked = view, apart = view;
  linked.regionPairs = &pairs;
  apart.regionPairs = &none;
  CHECK(consistency::PlayersDigest(linked, 2) != consistency::PlayersDigest(apart, 2));
  CHECK(consistency::PlayersDigest(linked, 2) == consistency::PlayersDigest(linked, 2));
}

TEST_CASE("the traffic digest follows the set, the records and how each vehicle stands") {
  std::map<proto::TrafficId, proto::TrafficSpawn> cars{{7, {.id = 7, .record = 0x11}}};
  std::map<proto::TrafficId, proto::TrafficStatus> status;
  const auto one = consistency::TrafficDigest(cars, status);
  CHECK(one == consistency::TrafficDigest(cars, status));
  // Where it is does not count: positions are state.
  cars[7].position = {5, 6, 7};
  CHECK(consistency::TrafficDigest(cars, status) == one);
  cars[8] = {.id = 8, .record = 0x11, .flags = proto::kTrafficParked};
  const auto two = consistency::TrafficDigest(cars, status);
  CHECK(two != one);
  // A wreck is not the car it was.
  status[8] = {.id = 8, .health = 0.f, .flags = proto::kTrafficDestroyed};
  CHECK(consistency::TrafficDigest(cars, status) != two);
  // What was said of a car that is gone is nobody's business.
  cars.erase(8);
  CHECK(consistency::TrafficDigest(cars, status) == one);
}

TEST_CASE("the story record says when a message changes nothing, and hashes what lasts") {
  story::Record host, guest;
  const proto::Journal snapshot{.snapshot = true, .entries = {{1, 2, 0}, {2, 3, 0}}};
  CHECK(host.Note(snapshot));
  CHECK(guest.Note(snapshot));
  CHECK(host.Digest() == guest.Digest());
  // The same snapshot again (a resync): nothing to apply.
  CHECK_FALSE(guest.Note(snapshot));
  CHECK(guest.Note(proto::Tracked{.hash = 1}));
  CHECK_FALSE(guest.Note(proto::Tracked{.hash = 1}));
  CHECK(host.Digest() != guest.Digest());
  CHECK(host.Note(proto::Tracked{.hash = 1}));
  CHECK(host.Digest() == guest.Digest());
  // A lost change shows.
  CHECK(host.Note(proto::Journal{.entries = {{3, 2, 1}}}));
  CHECK(host.Digest() != guest.Digest());
  // Effects: once on, again changes nothing; off once.
  CHECK(guest.Note(proto::StoryEffect{.player = 1, .effect = 9, .applied = true}));
  CHECK_FALSE(guest.Note(proto::StoryEffect{.player = 1, .effect = 9, .applied = true}));
  CHECK(guest.Note(proto::StoryEffect{.player = 1, .effect = 9, .applied = false}));
  CHECK_FALSE(guest.Note(proto::StoryEffect{.player = 1, .effect = 9, .applied = false}));
}

TEST_CASE("a guest asks for a domain that differs twice in a row, then waits out a cooldown") {
  consistency::Checker checker(1'000'000);
  const proto::Digest host{.world = 1, .npcs = 2, .story = 3, .players = 4};
  auto ours = host;
  CHECK(checker.Compare(host, ours, 10) == 0);
  ours.npcs = 9;
  // Once: may be a transfer still on its way.
  CHECK(checker.Compare(host, ours, 20) == 0);
  CHECK(checker.Compare(host, ours, 30) == proto::kDomainNpcs);
  // Asked: counted afresh, and not again within the cooldown.
  CHECK(checker.Compare(host, ours, 40) == 0);
  CHECK(checker.Compare(host, ours, 50) == 0);
  // Still different once the cooldown is over: asked again at once.
  CHECK(checker.Compare(host, ours, 2'000'000) == proto::kDomainNpcs);
  // Healed: nothing more.
  CHECK(checker.Compare(host, host, 3'000'000) == 0);
  CHECK(checker.Compare(host, host, 4'000'000) == 0);
  CHECK(checker.Requests() == 2);
  // A one-off difference never asks.
  ours = host;
  ours.world = 7;
  CHECK(checker.Compare(host, ours, 5'000'000) == 0);
  CHECK(checker.Compare(host, host, 5'000'010) == 0);
}

TEST_CASE("notices: at most one per key and gap") {
  consistency::NoticeGate gate;
  CHECK(gate.Allow("resync", 0, 60'000'000));
  CHECK_FALSE(gate.Allow("resync", 1'000'000, 60'000'000));
  CHECK(gate.Allow("lost", 1'000'000, 60'000'000));
  CHECK(gate.Allow("resync", 61'000'000, 60'000'000));
}

TEST_CASE("stale time-sensitive events are dropped, the rest kept in order") {
  std::vector<consistency::Timed<int>> hits{{1, 100}, {2, 2'000'000}, {3, 2'900'000}};
  CHECK(consistency::DropStale(hits, 3'000'000, 1'500'000) == 1);
  REQUIRE(hits.size() == 2);
  CHECK(hits[0].value == 2);
  CHECK(hits[1].value == 3);
  // A stamp a little in the future (clock offsets) is young.
  hits.push_back({4, 3'100'000});
  CHECK(consistency::DropStale(hits, 3'000'000, 1'500'000) == 0);
}

TEST_CASE("faults never touch the handshake and never send a one-shot twice") {
  consistency::FaultyLink link;
  link.Set({.drop = 0.0, .dup = 1.0});
  CHECK(link.Decide(proto::Hello{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::Welcome{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::NpcHit{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::QuestReward{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::Journal{.snapshot = false}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::NpcSpawn{}) == consistency::FaultyLink::Action::Twice);
  CHECK(link.Decide(proto::Journal{.snapshot = true}) == consistency::FaultyLink::Action::Twice);
  link.Set({.drop = 1.0});
  CHECK(link.Decide(proto::Bye{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::NpcHit{}) == consistency::FaultyLink::Action::Drop);
  CHECK(link.Dropped() == 1);
  CHECK(consistency::IsOneShot(proto::PlayerHit{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Appearance{}));
  // A shot at a V and its hit: doubling either would be a second shot.
  CHECK(consistency::IsOneShot(proto::NpcShotAt{}));
  CHECK(consistency::IsOneShot(proto::ShotHit{}));
  // A breach runs its daemons once; its answer is said once.
  CHECK(consistency::IsOneShot(proto::Breach{}));
  CHECK(consistency::IsOneShot(proto::BreachResult{}));
  // A time skip's request, answers and result: each one counts.
  CHECK(consistency::IsOneShot(proto::TimeSkip{}));
  CHECK(consistency::IsOneShot(proto::TimeSkipVote{}));
  CHECK(consistency::IsOneShot(proto::TimeSkipResult{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Activity{}));
  link.Set({.dup = 1.0});
  CHECK(link.Decide(proto::NpcShotAt{}) == consistency::FaultyLink::Action::Send);
  // A throw doubled is a second grenade; a defence doubled, a second stagger.
  CHECK(consistency::IsOneShot(proto::Throw{}));
  CHECK(consistency::IsOneShot(proto::ThrowEnd{}));
  CHECK(consistency::IsOneShot(proto::Defended{}));
  CHECK_FALSE(consistency::IsOneShot(proto::Rules{}));
  CHECK(link.Decide(proto::Throw{}) == consistency::FaultyLink::Action::Send);
  CHECK(link.Decide(proto::Rules{}) == consistency::FaultyLink::Action::Twice);
}

TEST_CASE("the session's rules are part of the world digest") {
  const proto::World world{.revision = 1};
  const auto none = consistency::WorldDigest(world, std::nullopt, std::nullopt);
  const auto off = consistency::WorldDigest(world, std::nullopt, proto::Rules{});
  const auto on = consistency::WorldDigest(world, std::nullopt,
                                           proto::Rules{.flags = proto::kRuleFriendlyFire});
  CHECK(none != off);
  CHECK(off != on);
  CHECK(on == consistency::WorldDigest(world, std::nullopt,
                                       proto::Rules{.flags = proto::kRuleFriendlyFire}));
}

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

// What a guest's game would show of the host's NPCs: the proxies its
// events spawn and remove. A spawn of one it shows already would be a
// duplicate proxy; a removal of one it never showed, an orphan event.
struct Game {
  std::set<proto::NpcId> proxies, corpses;
  int duplicateSpawns{}, strayGones{}, resyncs{}, worlds{};
  std::map<proto::PlayerId, int> looks;

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *spawn = std::get_if<events::HostNpcSpawn>(&event))
        duplicateSpawns += proxies.insert(spawn->spawn.npc).second ? 0 : 1;
      else if (const auto *gone = std::get_if<events::HostNpcGone>(&event)) {
        // Dead: a corpse until the host's goes (reason 0).
        const bool live = proxies.erase(gone->gone.npc) > 0;
        if (live && gone->gone.reason == 1)
          corpses.insert(gone->gone.npc);
        else if (!live && (gone->gone.reason == 1 || corpses.erase(gone->gone.npc) == 0))
          ++strayGones;
      } else if (std::holds_alternative<events::Resync>(event))
        ++resyncs;
      else if (std::holds_alternative<events::HostWorld>(event))
        ++worlds;
      else if (const auto *look = std::get_if<events::RemoteAppearance>(&event))
        ++looks[look->appearance.player];
    }
  }
};

std::set<proto::NpcId> Keys(const std::map<proto::NpcId, proto::NpcSpawn> &npcs) {
  std::set<proto::NpcId> out;
  for (const auto &[id, spawn] : npcs)
    out.insert(id);
  return out;
}

// Quick digests and resyncs: the same rules, scaled down.
constexpr Timing kQuick{.digest = 100ms, .resyncCooldown = 200ms};
} // namespace

TEST_CASE("a guest converges on the host's world although a third of what the host sends is lost") {
  // Lost and doubled on purpose: an NPC's word whose spawn was lost, a state twice.
  test::ExpectReferee({"ids.unknown-npc", "oneshot.seq-repeated", "digest.resync-loop"});
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  REQUIRE_MESSAGE(host, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kQuick);
  REQUIRE_MESSAGE(guest, error);
  Game game;
  REQUIRE(Pump({host.get(), guest.get()}, [&](std::size_t i, const auto &events) {
    if (i == 1)
      game.See(events);
    return guest->IsJoined();
  }));
  // The guest's own sends are lossy too (its requests for a resync).
  guest->SetFaults({.drop = 0.2, .seed = 11});

  // A third of everything the host sends never leaves (even reliable
  // messages: as if a bug lost them), and a third of its state goes twice.
  host->SetFaults({.drop = 0.33, .dup = 0.33, .seed = 7});
  host->SetLocalAppearance({.body = proto::Body::Male, .look = {{1, 2}}});
  host->SetWorld({.gameTimeSeconds = 3600, .weather = 0x77});
  host->SendStory(proto::Journal{.snapshot = true, .entries = {{1, 2, 0}, {2, 3, 0}}});
  for (proto::NpcId npc = 1; npc <= 30; ++npc) {
    host->SendNpcSpawn({.npc = npc, .record = 100 + npc});
    host->SendNpcHealth({.npc = npc, .health = 1.f});
  }
  for (proto::NpcId npc = 1; npc <= 30; npc += 3)
    host->SendNpcGone({.npc = npc, .reason = 0});
  host->SendNpcHealth({.npc = 2, .health = 0.25f});
  host->SendStory(proto::Journal{.entries = {{3, 2, 1}}});
  host->SendStory(proto::Tracked{.hash = 3});
  host->SetWorld({.gameTimeSeconds = 7200, .weather = 0x78});

  // Lossy for a while, then clean: within a few digests the guest's copy
  // and what its game shows are the host's.
  Pump(
      {host.get(), guest.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          game.See(events);
        return false;
      },
      1500ms);
  host->SetFaults({});
  guest->SetFaults({});
  CHECK(Pump(
      {host.get(), guest.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          game.See(events);
        return Keys(guest->SharedNpcs()) == Keys(host->SharedNpcs()) &&
               game.proxies == Keys(host->SharedNpcs()) && guest->Checks().Mismatches() > 0;
      },
      5s));
  // Settled: a few more digests find nothing to fix.
  const auto requests = guest->Checks().Requests();
  Pump(
      {host.get(), guest.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          game.See(events);
        return false;
      },
      600ms);
  CHECK(guest->Checks().Requests() == requests);
  CHECK(game.resyncs > 0);
  CHECK(game.duplicateSpawns == 0);
  CHECK(game.strayGones == 0);
  CHECK(game.proxies.size() == 20);
  CHECK(game.looks[1] == 1); // one look, applied once
}

TEST_CASE("state sent twice is applied once") {
  // Everything the host sends goes twice, on purpose.
  test::ExpectReferee({"oneshot.death-twice"});
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  Game game;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SetFaults({.dup = 1.0});
  host->SetWorld({.gameTimeSeconds = 60, .weather = 1});
  host->SetLocalAppearance({.look = {{1, 2}}});
  host->SendNpcSpawn({.npc = 5, .record = 1});
  host->SendNpcGone({.npc = 5, .reason = 1});
  host->SendNpcGone({.npc = 5, .reason = 0}); // the corpse goes, once
  Pump(
      {host.get(), a.get()},
      [&](std::size_t i, const auto &events) {
        if (i == 1)
          game.See(events);
        return false;
      },
      500ms);
  CHECK(game.worlds == 1);
  CHECK(game.looks[1] == 1);
  CHECK(game.duplicateSpawns == 0);
  CHECK(game.strayGones == 0);
  CHECK(game.proxies.empty());
  CHECK(game.corpses.empty());
  CHECK(a->Checks().Requests() == 0); // nothing diverged
}

TEST_CASE("after the host's world is rebuilt, hits aimed at the old one are dropped") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a && b));
  REQUIRE(Pump({host.get(), a.get(), b.get()},
               [&](std::size_t, const auto &) { return a->IsJoined() && b->IsJoined(); }));
  host->SendNpcSpawn({.npc = 1, .record = 1});
  REQUIRE(Pump({host.get(), a.get(), b.get()},
               [&](std::size_t, const auto &) { return a->SharedNpcs().contains(1); }));

  // The host fast travels; a's hit left before a heard of it.
  host->BumpEpoch(proto::kEpochFastTravel);
  a->SendNpcHit({.npc = 1, .damage = 10.f});
  int hitsAtHost = 0;
  bool aSawEpoch = false;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events) {
      hitsAtHost += i == 0 && std::holds_alternative<events::NpcHitBy>(event);
      if (const auto *e = std::get_if<events::EpochChanged>(&event); e && i == 1)
        aSawEpoch = e->epoch.player == 1 && e->epoch.reason == proto::kEpochFastTravel;
    }
    return aSawEpoch && a->HostEpoch() == host->WorldEpoch() && host->Fenced() == 1;
  }));
  CHECK(hitsAtHost == 0);
  // Now a knows: its hits count again.
  Pump({host.get(), a.get(), b.get()}, [&](std::size_t, const auto &) { return false; }, 200ms);
  a->SendNpcHit({.npc = 1, .damage = 10.f});
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      hitsAtHost += i == 0 && std::holds_alternative<events::NpcHitBy>(event);
    return hitsAtHost == 1;
  }));

  // a's own world is rebuilt (its loading screen): the host and b hear it.
  a->BumpEpoch(proto::kEpochLoading);
  bool hostHeard = false, bHeard = false;
  REQUIRE(Pump({host.get(), a.get(), b.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::EpochChanged>(&event);
          e && e->epoch.player == a->You()) {
        hostHeard |= i == 0;
        bHeard |= i == 2;
      }
    return hostHeard && bHeard;
  }));
}

TEST_CASE("NPC states never bring back an NPC the guest does not know") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((host && a));
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t, const auto &) { return a->IsJoined(); }));
  host->SendNpcSpawn({.npc = 1, .record = 1});
  REQUIRE(Pump({host.get(), a.get()},
               [&](std::size_t, const auto &) { return a->SharedNpcs().contains(1); }));
  // NPC 2 was never announced (its spawn lost, or it left already).
  host->SendNpcStates({.npcs = {{.npc = 1}, {.npc = 2}}});
  std::optional<proto::NpcStates> states;
  REQUIRE(Pump({host.get(), a.get()}, [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *s = std::get_if<events::HostNpcStates>(&event); s && i == 1)
        states = s->states;
    return states.has_value();
  }));
  REQUIRE(states->npcs.size() == 1);
  CHECK(states->npcs[0].npc == 1);
}

TEST_CASE("a guest that missed everything after its Welcome is healed by the digests") {
  const auto port = NextPort();
  std::string error;
  auto host = Endpoint::Host({"host"}, port, error, kQuick);
  REQUIRE_MESSAGE(host, error);
  host->SetLocalAppearance({.look = {{1, 2}}});
  // Only the Welcome gets through: the roster, the host's epoch and its
  // look are lost.
  host->SetFaults({.drop = 1.0});
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", port, error, kQuick);
  REQUIRE_MESSAGE(guest, error);
  REQUIRE(Pump({host.get(), guest.get()},
               [&](std::size_t, const auto &) { return guest->IsJoined(); }));
  Pump({host.get(), guest.get()}, [&](std::size_t, const auto &) { return false; }, 200ms);
  CHECK(guest->HostEpoch() == 0);
  host->SetFaults({});
  bool sawHost = false, gotLook = false;
  CHECK(Pump(
      {host.get(), guest.get()},
      [&](std::size_t i, const auto &events) {
        for (const auto &event : events)
          if (i == 1) {
            if (const auto *joined = std::get_if<events::PlayerJoined>(&event))
              sawHost |= joined->player.id == 1;
            gotLook |= std::holds_alternative<events::RemoteAppearance>(event);
          }
        return sawHost && gotLook;
      },
      3s));
  CHECK(guest->HostEpoch() == host->WorldEpoch());
}
