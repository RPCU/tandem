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
