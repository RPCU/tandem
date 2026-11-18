#include "tandem/timebubble.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using tandem::sync::TimeBubbles;
using Change = TimeBubbles::Change;
using Owner = TimeBubbles::Owner;
using Npc = TimeBubbles::Npc;

namespace {
bool Has(const std::vector<Change> &changes, Change change) {
  return std::ranges::find(changes, change) != changes.end();
}
} // namespace

TEST_CASE("a Sandevistan slows the NPCs around its V, and only them") {
  TimeBubbles bubbles;
  const std::vector<Owner> v{{.player = 2, .scale = 0.25f, .position = {0, 0, 0}}};
  std::vector<Npc> npcs{{1, {30, 0, 0}}, {2, {0, 147, 9}}, {3, {240, 0, 0}}};
  const auto start = bubbles.Update(v, npcs);
  CHECK(start.size() == 2);
  CHECK(Has(start, {1, 0.25f}));
  CHECK(Has(start, {2, 0.25f}));
  CHECK(bubbles.FactorOf(3) == 1.f); // out of reach: normal speed
  CHECK(bubbles.MembersOf(2) == 2);

  // Nothing moved: nothing to change.
  CHECK(bubbles.Update(v, npcs).empty());

  // Sticky: an NPC that runs out stays slowed; one that comes in joins.
  npcs[0].position = {600, 0, 0};
  npcs[2].position = {90, 0, 0};
  const auto moved = bubbles.Update(v, npcs);
  CHECK(moved == std::vector<Change>{{3, 0.25f}});
  CHECK(bubbles.FactorOf(1) == 0.25f);

  // The ability ends: every NPC goes back together.
  const auto end = bubbles.Update({}, npcs);
  CHECK(end.size() == 3);
  for (const auto npc : {1u, 2u, 3u})
    CHECK(Has(end, {npc, 1.f}));
  CHECK(bubbles.Bubbles() == 0);
}

TEST_CASE("two Vs in a Sandevistan: the strongest wins, never the product") {
  TimeBubbles bubbles;
  // Player 1 (a 0.5 implant) at 0, player 2 (0.2) 180 m away: NPC 2 is in both.
  std::vector<Owner> both{{.player = 1, .scale = 0.5f, .position = {0, 0, 0}},
                          {.player = 2, .scale = 0.2f, .position = {180, 0, 0}}};
  const std::vector<Npc> npcs{{1, {-30, 0, 0}}, {2, {90, 0, 0}}, {3, {300, 0, 0}}};
  bubbles.Update(both, npcs);
  CHECK(bubbles.FactorOf(1) == 0.5f);
  CHECK(bubbles.FactorOf(2) == doctest::Approx(0.2f)); // min, not 0.5 * 0.2
  CHECK(bubbles.FactorOf(3) == doctest::Approx(0.2f));

  // Player 2's ends first: what only it held goes back, what both held
  // takes player 1's.
  const std::vector<Owner> first{both[0]};
  const auto changes = bubbles.Update(first, npcs);
  CHECK(changes.size() == 2);
  CHECK(Has(changes, {2, 0.5f}));
  CHECK(Has(changes, {3, 1.f}));
  CHECK(bubbles.FactorOf(1) == 0.5f);

  // Then player 1's: all at normal speed.
  const auto last = bubbles.Update({}, npcs);
  CHECK(last.size() == 2);
  CHECK(Has(last, {1, 1.f}));
  CHECK(Has(last, {2, 1.f}));
}

TEST_CASE("a stronger bubble starting later takes over, and gives back on its end") {
  TimeBubbles bubbles;
  const std::vector<Npc> npcs{{7, {15, 15, 0}}};
  const Owner kerenzikov{.player = 1, .scale = 0.6f, .position = {0, 0, 0}};
  const Owner sandevistan{.player = 3, .scale = 0.1f, .position = {30, 0, 0}};
  bubbles.Update(std::vector{kerenzikov}, npcs);
  CHECK(bubbles.Update(std::vector{kerenzikov, sandevistan}, npcs) ==
        std::vector<Change>{{7, 0.1f}});
  CHECK(bubbles.Update(std::vector{kerenzikov}, npcs) == std::vector<Change>{{7, 0.6f}});
}

TEST_CASE("NPCs that go, scales that slow nothing, and the end of a session") {
  TimeBubbles bubbles;
  const std::vector<Owner> v{{.player = 2, .scale = 0.3f, .position = {0, 0, 0}}};
  bubbles.Update(v, std::vector<Npc>{{1, {3, 0, 0}}, {2, {6, 0, 0}}});
  // NPC 1 is no longer listed (dead, gone): released.
  CHECK(bubbles.Update(v, std::vector<Npc>{{2, {6, 0, 0}}}) == std::vector<Change>{{1, 1.f}});
  CHECK(bubbles.MembersOf(2) == 1);

  // A scale of 1 (scanning, no bubble ability) makes no bubble.
  TimeBubbles none;
  CHECK(
      none.Update(std::vector<Owner>{{.player = 4, .scale = 1.f}}, std::vector<Npc>{{1, {0, 0, 0}}})
          .empty());
  CHECK(none.Bubbles() == 0);

  // The session ends: everything slowed is given back.
  CHECK(bubbles.Clear() == std::vector<Change>{{2, 1.f}});
  CHECK(bubbles.FactorOf(2) == 1.f);
}

TEST_CASE("a bubble slows the other Vs in it by the ratio of their own ability") {
  const std::vector<Owner> owners{{.player = 1, .scale = 0.25f, .position = {0, 0, 0}},
                                  {.player = 2, .scale = 0.5f, .position = {60, 0, 0}},
                                  {.player = 3, .scale = 0.1f, .position = {1500, 0, 0}}};
  struct Case {
    tandem::proto::PlayerId player;
    float own;
    tandem::proto::Vec3 at;
    float slowed;
  };
  const Case cases[] = {
      {4, 1.f, {30, 0, 0}, 0.25f},   // no ability, in 1's and 2's: the strongest
      {4, 1.f, {195, 0, 0}, 0.5f},   // only in 2's
      {4, 1.f, {600, 0, 0}, 1.f},    // in nobody's
      {4, 1.f, {1440, 0, 0}, 0.1f},  // in 3's
      {1, 0.25f, {0, 0, 0}, 1.f},    // an owner is never slowed by its own, nor by a weaker one
      {2, 0.5f, {60, 0, 0}, 0.5f},   // a weaker ability: slowed by the ratio 0.25 / 0.5
      {4, 0.25f, {30, 0, 0}, 1.f},   // the same strength: an equal footing
      {4, 0.1f, {30, 0, 0}, 1.f},    // a stronger one: not slowed, never sped up
      {4, 0.5f, {1440, 0, 0}, 0.2f}, // 0.1 / 0.5
      {4, -7.f, {30, 0, 0}, 1.f},    // nonsense in: clamped (0.25 / 0.05 is above 1)
  };
  for (const auto &c : cases)
    CHECK(TimeBubbles::PlayerFactor(c.player, c.own, c.at, owners) == doctest::Approx(c.slowed));
  // Three bubbles on one spot: the strongest.
  const std::vector<Owner> three{
      {.player = 1, .scale = 0.6f}, {.player = 2, .scale = 0.3f}, {.player = 3, .scale = 0.4f}};
  CHECK(TimeBubbles::PlayerFactor(4, 1.f, {3, 3, 0}, three) == doctest::Approx(0.3f));
  CHECK(TimeBubbles::PlayerFactor(2, 0.3f, {3, 3, 0}, three) == doctest::Approx(1.f));
  CHECK(TimeBubbles::PlayerFactor(1, 0.6f, {3, 3, 0}, three) == doctest::Approx(0.5f));
  CHECK(TimeBubbles::PlayerFactor(4, 1.f, {3, 3, 0}, {}) == 1.f);
}

TEST_CASE("an implant's factor that changes mid-effect reaches its NPCs") {
  TimeBubbles bubbles;
  const std::vector<Npc> npcs{{1, {9, 0, 0}}};
  bubbles.Update(std::vector<Owner>{{.player = 2, .scale = 0.5f}}, npcs);
  CHECK(bubbles.Update(std::vector<Owner>{{.player = 2, .scale = 0.25f}}, npcs) ==
        std::vector<Change>{{1, 0.25f}});
  CHECK(bubbles.Update(std::vector<Owner>{{.player = 2, .scale = 0.4f}}, npcs) ==
        std::vector<Change>{{1, 0.4f}});
}

TEST_CASE("a V at a bubble's edge does not flicker: a margin on the way out") {
  const std::vector<Owner> owner{{.player = 1, .scale = 0.25f, .position = {0, 0, 0}}};
  const float edge = TimeBubbles::kRadius, margin = TimeBubbles::kMargin;
  // Coming in: slowed from the edge on.
  CHECK(TimeBubbles::PlayerFactor(2, 1.f, {edge + 1.f, 0, 0}, owner, false) == 1.f);
  CHECK(TimeBubbles::PlayerFactor(2, 1.f, {edge - 1.f, 0, 0}, owner, false) == 0.25f);
  // Going out: still slowed within the margin, free past it.
  CHECK(TimeBubbles::PlayerFactor(2, 1.f, {edge + 1.f, 0, 0}, owner, true) == 0.25f);
  CHECK(TimeBubbles::PlayerFactor(2, 1.f, {edge + margin + 1.f, 0, 0}, owner, true) == 1.f);
}
