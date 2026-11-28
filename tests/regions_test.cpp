// Per-player regions, the rules alone (docs/architecture/regions.md): links
// with hysteresis, rank, who takes an NPC over and who inherits it.

#include "tandem/regions.hpp"

#include <doctest/doctest.h>

using namespace tandem;
using namespace tandem::regions;

namespace {
Place At(float x, float y = 0.f) { return {.position = {x, y, 0.f}, .known = true}; }
} // namespace

TEST_CASE("npc ids name the game that announced them; the host's are as they always were") {
  CHECK(MakeNpcId(1, 7) == 7u);
  CHECK(Announcer(7) == 1u);
  CHECK(MakeNpcId(3, 7) == 0x02000007u);
  CHECK(Announcer(MakeNpcId(3, 7)) == 3u);
  CHECK(Announcer(MakeNpcId(4, 0xFFFFFF)) == 4u);
  CHECK(MakeNpcId(2, 1) != MakeNpcId(3, 1));
}

TEST_CASE("the host ranks first, then the lowest player id") {
  CHECK(Outranks(1, 2));
  CHECK_FALSE(Outranks(2, 1));
  CHECK(Outranks(2, 3));
  CHECK_FALSE(Outranks(3, 2));
  CHECK_FALSE(Outranks(2, 2));
  // Whoever hosts ranks first, whatever its id.
  CHECK(Outranks(5, 2, 5));
}

TEST_CASE("players are linked until they are known to be apart, with hysteresis") {
  Links links;
  Places places{{1, {}}, {2, {}}};
  // Nobody placed yet: together.
  CHECK_FALSE(links.Update(places));
  CHECK(links.Linked(1, 2));
  CHECK_FALSE(links.Linked(1, 1));

  places[1] = At(0);
  CHECK_FALSE(links.Update(places)); // one unknown: still together
  places[2] = At(1000);
  CHECK(links.Update(places));
  CHECK_FALSE(links.Linked(1, 2));
  CHECK_FALSE(links.Linked(2, 1));

  // Coming closer: linked only inside kLinkIn...
  places[2] = At(kLinkIn + 10);
  CHECK_FALSE(links.Update(places));
  CHECK_FALSE(links.Linked(1, 2));
  places[2] = At(kLinkIn - 1);
  CHECK(links.Update(places));
  CHECK(links.Linked(1, 2));
  // ...and apart again only beyond kLinkOut.
  places[2] = At(kLinkOut - 1);
  CHECK_FALSE(links.Update(places));
  CHECK(links.Linked(1, 2));
  places[2] = At(kLinkOut + 1);
  CHECK(links.Update(places));
  CHECK_FALSE(links.Linked(1, 2));

  // A player that is no longer placed (it left, a new one took the id).
  places[2] = {};
  CHECK(links.Update(places));
  CHECK(links.Linked(1, 2));
}

TEST_CASE("a remote view links its player where it looks from, with the same hysteresis") {
  Links links;
  Places places{{1, At(0)}, {2, At(kLinkOut + 100)}};
  CHECK(links.Update(places));
  CHECK_FALSE(links.Linked(1, 2));
  // Player 1 looks through a camera beside player 2: linked as if there.
  Views views{{1, {kLinkOut + 60, 0, 0}}};
  CHECK(Apart(1, places[1], 2, places[2], views) == doctest::Approx(40.f));
  CHECK(Apart(2, places[2], 1, places[1], views) == doctest::Approx(40.f));
  CHECK(links.Update(places, views));
  CHECK(links.Linked(1, 2));
  // Two views: the nearest of the four places.
  views[2] = {kLinkOut + 55, 0, 0};
  CHECK(Apart(1, places[1], 2, places[2], views) == doctest::Approx(5.f));
  // Out of the camera, still near enough: kLinkOut is the edge as for a V.
  views.erase(2);
  views[1] = {kLinkOut + 100 - (kLinkOut - 1), 0, 0};
  CHECK_FALSE(links.Update(places, views));
  CHECK(links.Linked(1, 2));
  views.clear();
  CHECK(links.Update(places, views));
  CHECK_FALSE(links.Linked(1, 2));
}

TEST_CASE("a joiner is linked with nobody until its first place, then only when near") {
  Links links;
  Places places{{1, At(0)}, {2, At(50)}};
  links.Update(places);
  REQUIRE(links.Linked(1, 2));

  // 3 joins: apart from everyone before any pass, and through passes while
  // it is not placed; the host names it alone.
  links.Wait(3);
  places[3] = {};
  CHECK_FALSE(links.Linked(1, 3));
  CHECK_FALSE(links.Linked(3, 2));
  CHECK_FALSE(links.Update(places));
  CHECK_FALSE(links.Linked(1, 3));
  CHECK(links.Pairs({1, 2, 3}) == std::vector<proto::PlayerId>{1, 1, 1, 2, 2, 2, 3, 3});
  // Its first place, 400 m away: apart still, nothing changed.
  places[3] = At(400);
  CHECK_FALSE(links.Update(places));
  CHECK_FALSE(links.Waiting(3));
  CHECK_FALSE(links.Linked(1, 3));
  // Between kLinkIn and kLinkOut it stays apart; inside kLinkIn, linked.
  places[3] = At(-(kLinkIn + 10));
  CHECK_FALSE(links.Update(places));
  CHECK_FALSE(links.Linked(1, 3));
  places[3] = At(20);
  CHECK(links.Update(places));
  CHECK(links.Linked(1, 3));
  CHECK(links.Linked(2, 3));

  // Placed before the first pass: near, linked at once; between the two
  // distances, not (no link is guessed).
  links.Wait(4);
  places[4] = At(10);
  links.Wait(5);
  places[5] = At(-(kLinkIn + 10));
  CHECK(links.Update(places));
  CHECK(links.Linked(1, 4));
  CHECK_FALSE(links.Linked(1, 5));

  // A joiner that never says where it is, once released: linked as any
  // player whose place is unknown.
  links.Wait(6);
  places[6] = {};
  CHECK_FALSE(links.Update(places));
  CHECK_FALSE(links.Linked(1, 6));
  links.Release(6);
  CHECK(links.Update(places));
  CHECK(links.Linked(1, 6));
  // A joiner that leaves waits no more.
  links.Wait(7);
  places[7] = {};
  links.Update(places);
  places.erase(7);
  links.Update(places);
  CHECK_FALSE(links.Waiting(7));
}

TEST_CASE("links are pairwise: three in a row") {
  Links links;
  const Places places{{1, At(0)}, {2, At(120)}, {3, At(240)}};
  links.Update(places);
  CHECK(links.Linked(1, 2));
  CHECK(links.Linked(2, 3));
  CHECK_FALSE(links.Linked(1, 3));
}

TEST_CASE("the links as sent and as a guest reads them") {
  Links host;
  const Places places{{1, At(0)}, {2, At(1000)}, {3, At(20)}};
  host.Update(places);
  const proto::Regions wire{.revision = 1, .pairs = host.Pairs({1, 2, 3})};
  Links guest;
  CHECK(guest.Linked(1, 2)); // before the host said anything
  guest.Set(wire);
  CHECK(guest.Linked(1, 3));
  CHECK_FALSE(guest.Linked(1, 2));
  CHECK_FALSE(guest.Linked(2, 3));
  // A player the host has not named yet (it just joined) is with everyone.
  CHECK(guest.Linked(2, 4));
  // A player linked with nobody is still named.
  Links alone;
  alone.Set({.pairs = host.Pairs({1, 2})});
  CHECK_FALSE(alone.Linked(1, 2));
}

TEST_CASE("an NPC is taken over when its owner leaves it and a linked player is near") {
  Links links;
  Places places{{1, At(0)}, {2, At(50)}};
  links.Update(places);
  const proto::Vec3 npc{40.f, 0.f, 0.f};
  // The owner is near: it stays, whoever else is near.
  CHECK(Taker(npc, 1, places, links) == proto::kNoPlayer);
  // The owner walks off: player 2, 10 m from it, takes it.
  places[1] = At(-45);
  links.Update(places);
  CHECK(Taker(npc, 1, places, links) == 2u);
  // Nobody near: it stays (and leaves the shared world with its owner).
  places[2] = At(100);
  links.Update(places);
  CHECK(Taker(npc, 1, places, links) == proto::kNoPlayer);
  // Between kTake and kKeep nothing moves either way.
  places[1] = At(-30); // 70 m: inside kKeep
  places[2] = At(50);
  CHECK(Taker(npc, 1, places, links) == proto::kNoPlayer);
  CHECK(Taker(npc, 2, places, links) == proto::kNoPlayer);
}

TEST_CASE("only a player linked with the owner takes its NPC; the highest-ranked of several") {
  Links links;
  // 1 far away; 2 and 3 next to the NPC, linked with each other.
  Places places{{1, At(1000)}, {2, At(10)}, {3, At(20)}};
  links.Update(places);
  const proto::Vec3 npc{15.f, 0.f, 0.f};
  CHECK(Taker(npc, 1, places, links) == proto::kNoPlayer); // unlinked: they never saw it
  Places near{{1, At(-70)}, {2, At(10)}, {3, At(20)}};
  Links together;
  together.Update(near);
  CHECK(Taker(npc, 1, near, together) == 2u);
  // An owner that is not placed keeps what it has.
  near[1] = {};
  CHECK(Taker(npc, 1, near, together) == proto::kNoPlayer);
}

TEST_CASE("an NPC whose owner is gone goes to the highest-ranked player near it, or nobody") {
  const Places places{{1, At(0)}, {2, At(30)}, {3, At(500)}};
  const proto::Vec3 npc{20.f, 0.f, 0.f};
  CHECK(Heir(npc, places, {1, 2}) == 1u);
  CHECK(Heir(npc, places, {2}) == 2u);
  CHECK(Heir(npc, places, {3}) == proto::kNoPlayer);
  CHECK(Heir(npc, places, {}) == proto::kNoPlayer);
  // A player that is not in the session any more inherits nothing.
  CHECK(Heir(npc, places, {7}) == proto::kNoPlayer);
}

TEST_CASE("a player that holds its region ranks first, keeps its NPCs and leads its group") {
  const Holders guest{2};
  CHECK(Outranks(2, 1, 1, guest));
  CHECK_FALSE(Outranks(1, 2, 1, guest));
  CHECK(Outranks(1, 3, 1, guest));         // among those that hold nothing: as before
  CHECK(Outranks(1, 2, 1, Holders{1, 2})); // two holders: the host, then the lowest id

  // Nobody takes an NPC from an owner that holds, however far its V is.
  Links links;
  const Places places{{1, At(10)}, {2, At(-90)}};
  links.Update(places);
  const proto::Vec3 npc{15.f, 0.f, 0.f};
  CHECK(Taker(npc, 2, places, links, 1) == 1u);
  CHECK(Taker(npc, 2, places, links, 1, guest) == proto::kNoPlayer);

  // Groups: a chain is one group, its leader the holder; apart, each leads itself.
  Links row;
  const Places three{{1, At(0)}, {2, At(120)}, {3, At(240)}, {4, At(5000)}};
  row.Update(three);
  const std::set<proto::PlayerId> everyone{1, 2, 3, 4};
  CHECK(GroupOf(3, everyone, row) == std::set<proto::PlayerId>{1, 2, 3});
  CHECK(GroupOf(4, everyone, row) == std::set<proto::PlayerId>{4});
  CHECK(Leader(GroupOf(3, everyone, row)) == 1u);
  CHECK(Leader(GroupOf(3, everyone, row), 1, Holders{3}) == 3u);
  CHECK(Leader(GroupOf(4, everyone, row)) == 4u);
}

TEST_CASE(
    "the area around a player: its own game's, unless deep in a higher-ranked linked region") {
  const std::set<proto::PlayerId> players{1, 2, 3};
  Links links;
  Places places{{1, At(0)}, {2, At(1000)}, {3, At(2000)}};
  links.Update(places);
  // Apart: everyone runs its own area.
  CHECK(AreaOwner(1, players, places, links) == 1u);
  CHECK(AreaOwner(2, players, places, links) == 2u);
  CHECK(AreaOwner(3, players, places, links) == 3u);

  // 2 comes within 100 m of the host: linked, but its V is not inside the
  // host's region yet: still its own.
  places[2] = At(100);
  links.Update(places);
  REQUIRE(links.Linked(1, 2));
  CHECK(AreaOwner(2, players, places, links) == 2u);
  // Deep inside (45 m): the host's; the host's own area stays the host's.
  places[2] = At(40);
  CHECK(AreaOwner(2, players, places, links) == 1u);
  CHECK(AreaOwner(1, players, places, links) == 1u);
  // Hysteresis: at 50 m it stays the host's, it was before; a fresh look says 2.
  places[2] = At(50);
  CHECK(AreaOwner(2, players, places, links, 1, {}, 1) == 1u);
  CHECK(AreaOwner(2, players, places, links) == 2u);
  // Beyond the radius: its own again.
  places[2] = At(65);
  CHECK(AreaOwner(2, players, places, links, 1, {}, 1) == 2u);

  // A holder ranks first: deep in a holding guest's region, the host's area
  // is that guest's.
  places[2] = At(30);
  CHECK(AreaOwner(1, players, places, links, 1, Holders{2}) == 2u);
  CHECK(AreaOwner(2, players, places, links, 1, Holders{2}) == 2u);

  // A place unknown on either side of a link to one that outranks: covered
  // (as a guest was before regions). 2 is not placed yet: linked with
  // everyone, the host's; 3 is placed, but 2 outranks it and could be next
  // to it. The host, placed, outranks everyone: its own.
  Places unknown{{1, At(0)}, {2, {}}, {3, At(2000)}};
  Links fresh;
  fresh.Update(unknown);
  CHECK(AreaOwner(2, players, unknown, fresh) == 1u);
  CHECK(AreaOwner(3, players, unknown, fresh) == 2u);
  CHECK(AreaOwner(1, players, unknown, fresh) == 1u);
}

TEST_CASE("the covers over a player: the linked players that outrank it, where they are") {
  Links links;
  const std::set<proto::PlayerId> players{1, 2, 3};
  Places places{{1, At(0)}, {2, At(100)}, {3, At(130)}};
  links.Update(places);
  // The host is covered by nobody; player 2 by the host; player 3 by both,
  // the host first.
  CHECK(Covers(1, players, places, links).empty());
  CHECK(Covers(2, players, places, links) == std::vector<Cover>{{1, {0.f, 0.f, 0.f}}});
  CHECK(Covers(3, players, places, links) ==
        std::vector<Cover>{{1, {0.f, 0.f, 0.f}}, {2, {100.f, 0.f, 0.f}}});
  // A holder ranks first; a player whose place is unknown covers nothing yet.
  CHECK(Covers(1, players, places, links, 1, {3}) == std::vector<Cover>{{3, {130.f, 0.f, 0.f}}});
  places[1] = {};
  CHECK(Covers(2, players, places, links).empty());
  // Unlinked: no cover.
  places = {{1, At(0)}, {2, At(500)}};
  links.Update(places);
  CHECK(Covers(2, {1, 2}, places, links).empty());

  const std::vector<Cover> covers{{1, {0.f, 0.f, 0.f}}, {2, {100.f, 0.f, 0.f}}};
  CHECK(CoveredBy({30.f, 0.f, 0.f}, covers) == 1u);
  CHECK(CoveredBy({55.f, 0.f, 0.f}, covers) == 1u); // both: the higher rank
  CHECK(CoveredBy({70.f, 0.f, 0.f}, covers) == 2u);
  CHECK(CoveredBy({50.f, 80.f, 0.f}, covers) == proto::kNoPlayer);
  CHECK(CoveredBy({70.f, 0.f, 0.f}, covers, 20.f) == proto::kNoPlayer);
}

TEST_CASE("the crowd a game keeps as its V nears a cover: all, the part outside it, none") {
  const std::vector<Cover> covers{{1, {0.f, 0.f, 0.f}}};
  CHECK(CrowdShare({500.f, 0.f, 0.f}, covers, false) == doctest::Approx(1.f));
  CHECK(CrowdShare({120.f, 0.f, 0.f}, covers, false) == doctest::Approx(1.f));
  // Less and less as the two regions overlap more, in tenths.
  const float at100 = CrowdShare({100.f, 0.f, 0.f}, covers, false);
  const float at60 = CrowdShare({60.f, 0.f, 0.f}, covers, false);
  const float at46 = CrowdShare({46.f, 0.f, 0.f}, covers, false);
  CHECK(at100 == doctest::Approx(0.9f));
  CHECK(at60 == doctest::Approx(0.6f));
  CHECK(at46 == doctest::Approx(0.5f));
  // Deep inside: none, and none until back beyond the radius.
  CHECK(CrowdShare({44.f, 0.f, 0.f}, covers, false) == 0.f);
  CHECK(CrowdShare({55.f, 0.f, 0.f}, covers, true) == 0.f);
  CHECK(CrowdShare({61.f, 0.f, 0.f}, covers, true) == doctest::Approx(0.6f));
  // Several covers: the least; none: all of it.
  CHECK(CrowdShare({60.f, 0.f, 0.f}, {{1, {0.f, 0.f, 0.f}}, {2, {160.f, 0.f, 0.f}}}, false) ==
        doctest::Approx(0.6f));
  CHECK(CrowdShare({0.f, 0.f, 0.f}, {}, true) == doctest::Approx(1.f));
}

TEST_CASE("null areas follow the covers in steps, are renewed, and go with them") {
  NullAreas areas;
  std::vector<Cover> covers{{1, {0.f, 0.f, 0.f}}};
  CHECK(areas.Update(covers, 0.0) == std::vector<NullStep>{{1, true, {0.f, 0.f, 0.f}}});
  // Nothing to do while its V stays near and the area is young.
  CHECK(areas.Update(covers, 1.0).empty());
  covers[0].center = {kNullStep - 1.f, 0.f, 0.f};
  CHECK(areas.Update(covers, 2.0).empty());
  // A step away: placed again there.
  covers[0].center = {kNullStep + 1.f, 0.f, 0.f};
  CHECK(areas.Update(covers, 3.0) == std::vector<NullStep>{{1, true, {kNullStep + 1.f, 0.f, 0.f}}});
  // Before it runs out: placed again where it is.
  CHECK(areas.Update(covers, 3.0 + kNullRenew - 0.5).empty());
  CHECK(areas.Update(covers, 3.0 + kNullRenew).size() == 1);
  CHECK(kNullRenew < kNullLife);
  // A second cover comes, the first goes.
  covers = {{2, {50.f, 0.f, 0.f}}};
  const auto steps = areas.Update(covers, 20.0);
  REQUIRE(steps.size() == 2);
  CHECK(steps[0] == NullStep{1, false, {kNullStep + 1.f, 0.f, 0.f}});
  CHECK(steps[1] == NullStep{2, true, {50.f, 0.f, 0.f}});
  CHECK(areas.Size() == 1);
  // The session ends: every area goes.
  CHECK(areas.Clear() == std::vector<NullStep>{{2, false, {50.f, 0.f, 0.f}}});
  CHECK(areas.Size() == 0);
  CHECK(areas.Clear().empty());
}

TEST_CASE(
    "a vehicle is clear of a V a few metres away, of one not placed, of one not in the session") {
  regions::Places places;
  places[1] = {.position = {0.f, 0.f, 0.f}, .known = true};
  places[2] = {.position = {0.f, 0.f, 0.f}, .known = false};
  CHECK_FALSE(regions::ClearOf({2.f, 0.f, 0.f}, 1, places));
  CHECK_FALSE(regions::ClearOf({0.f, 3.f, 1.f}, 1, places));
  CHECK(regions::ClearOf({4.5f, 0.f, 0.f}, 1, places));
  CHECK(regions::ClearOf({2.f, 0.f, 0.f}, 2, places));
  CHECK(regions::ClearOf({2.f, 0.f, 0.f}, 3, places));
}
