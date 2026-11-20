// Enemy scaling (core/include/tandem/scaling.hpp): the multipliers for a
// count of players, the rule as Rules carries it, the share of health kept
// when the count changes, the hit reaction thresholds, and who counts.

#include "tandem/consistency.hpp"
#include "tandem/scaling.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace tandem;
using namespace tandem::scaling;

namespace {
constexpr Rule kOn{.on = true};
} // namespace

TEST_CASE("enemy scaling: health, poise and armour for each count of players") {
  // Alone (or not counted yet): vanilla.
  CHECK(For(1, kOn).None());
  CHECK(For(0, kOn).None());
  // 2, 3, 4 players.
  CHECK(For(2, kOn).health == doctest::Approx(1.6f));
  CHECK(For(3, kOn).health == doctest::Approx(2.2f));
  CHECK(For(4, kOn).health == doctest::Approx(2.8f));
  CHECK(For(2, kOn).poise == doctest::Approx(1.25f));
  CHECK(For(3, kOn).poise == doctest::Approx(1.5f));
  CHECK(For(4, kOn).poise == doctest::Approx(1.75f));
  CHECK(For(2, kOn).armor == doctest::Approx(1.1f));
  CHECK(For(3, kOn).armor == doctest::Approx(1.2f));
  CHECK(For(4, kOn).armor == doctest::Approx(1.2f)); // capped
  // More than a session holds: as four.
  CHECK(For(8, kOn) == For(4, kOn));
  // The rule off: nothing, whoever is there.
  CHECK(For(4, Rule{}).None());
  // The host's own health per player (enemy_scaling_health); poise and
  // armour keep theirs.
  const Rule custom{.on = true, .health = 1.f};
  CHECK(For(3, custom).health == doctest::Approx(3.f));
  CHECK(For(3, custom).poise == doctest::Approx(1.5f));
  CHECK(For(2, Rule{.on = true, .health = 0.f}).health == 1.f);
  CHECK(For(2, Rule{.on = true, .health = 99.f}).health ==
        doctest::Approx(1.f + kMaxHealthPerPlayer));
}

TEST_CASE("enemy scaling: the rule as the session's Rules carry it") {
  // The default Rules carry the default health but no scaling.
  CHECK_FALSE(RuleOf(proto::Rules{}).on);
  CHECK(RuleOf(proto::Rules{}).health == doctest::Approx(kHealthPerPlayer));
  auto rules = WithRule(proto::Rules{.flags = proto::kRuleFriendlyFire}, true, 0.6f);
  CHECK(rules.flags == (proto::kRuleFriendlyFire | proto::kRuleEnemyScaling));
  CHECK(rules.enemyHealth == 60);
  CHECK(RuleOf(rules).on);
  CHECK(RuleOf(rules).health == doctest::Approx(0.6f));
  // Off keeps the other flags; whole percent, clamped.
  rules = WithRule(rules, false, 0.333f);
  CHECK(rules.flags == proto::kRuleFriendlyFire);
  CHECK(rules.enemyHealth == 33);
  CHECK(WithRule({}, true, 10.f).enemyHealth == proto::kMaxEnemyHealth);
  CHECK(WithRule({}, true, -1.f).enemyHealth == 0);
  // The world digest follows the health too.
  CHECK(consistency::WorldDigest(std::nullopt, std::nullopt, WithRule({}, true, 0.6f)) !=
        consistency::WorldDigest(std::nullopt, std::nullopt, WithRule({}, true, 0.7f)));
}

TEST_CASE("enemy scaling: the share of health is kept when the count changes") {
  // 300 HP NPC at half health; a second player arrives (×1.6).
  CHECK(KeepFraction(150.f, 300.f, 480.f) == doctest::Approx(240.f));
  // And leaves again: back to half of 300, no heal, no drop.
  CHECK(KeepFraction(240.f, 480.f, 300.f) == doctest::Approx(150.f));
  // Full stays full, a scratch stays a scratch.
  CHECK(KeepFraction(300.f, 300.f, 840.f) == doctest::Approx(840.f));
  CHECK(KeepFraction(3.f, 300.f, 840.f) == doctest::Approx(8.4f));
  // Dead stays dead; nothing known of the old maximum: full.
  CHECK(KeepFraction(0.f, 300.f, 480.f) == 0.f);
  CHECK(KeepFraction(10.f, 0.f, 480.f) == 480.f);
  CHECK(KeepFraction(10.f, 300.f, 0.f) == 0.f);
  // Never above the maximum.
  CHECK(KeepFraction(400.f, 300.f, 480.f) == doctest::Approx(480.f));
}

TEST_CASE("enemy scaling: the hit reaction thresholds are the poise multiplier, once") {
  // The hit reaction component read the health before our modifier (the
  // usual case: it reads it once, when the NPC is attached): ×poise.
  CHECK(ThresholdFactor(1.5f, 300.f, 300.f) == doctest::Approx(1.5f));
  // It read it with our health modifier on (×2.2): the percentage is cut so
  // the threshold in points is still the unscaled health's ×1.5.
  CHECK(ThresholdFactor(1.5f, 300.f, 660.f) == doctest::Approx(1.5f * 300.f / 660.f));
  // Nothing to compare with: ×poise.
  CHECK(ThresholdFactor(1.25f, 0.f, 300.f) == 1.25f);
  CHECK(ThresholdFactor(1.25f, 300.f, 0.f) == 1.25f);
  // Never absurd.
  CHECK(ThresholdFactor(1.5f, 1.f, 1000.f) == doctest::Approx(0.1f));
}

TEST_CASE("enemy scaling: who counts for an NPC, with hysteresis") {
  // Within 100 m: counts. Between 100 and 130: only if it counted already.
  CHECK(Counts(false, {.player = 2, .distance = 99.f}, 0.0, 10.0));
  CHECK_FALSE(Counts(false, {.player = 2, .distance = 110.f}, 0.0, 10.0));
  CHECK(Counts(true, {.player = 2, .distance = 110.f}, 0.0, 10.0));
  CHECK(Counts(true, {.player = 2, .distance = 130.f}, 0.0, 10.0));
  CHECK_FALSE(Counts(true, {.player = 2, .distance = 131.f}, 0.0, 10.0));
  // Fighting it: from anywhere, and for a moment after.
  CHECK(Counts(false, {.player = 2, .distance = 400.f, .engaged = true}, 0.0, 10.0));
  CHECK(Counts(true, {.player = 2, .distance = 400.f}, 12.0, 10.0));
  CHECK_FALSE(Counts(true, {.player = 2, .distance = 400.f}, 9.0, 10.0));
  // Not placed here (no puppet, no view): only by fighting it.
  CHECK_FALSE(Counts(false, {.player = 2}, 0.0, 10.0));
  CHECK(Counts(false, {.player = 2, .engaged = true}, 0.0, 10.0));
}

TEST_CASE("enemy scaling: the count of an NPC over a fight") {
  Counter counter;
  constexpr proto::NpcId npc = 7;
  // Alone: 1. Never counted: 1.
  CHECK(counter.CountOf(npc) == 1);
  std::vector<Seen> seen{{.player = 1, .distance = 20.f}, {.player = 2, .distance = 300.f}};
  CHECK(counter.Count(npc, seen, 0.0) == 1);
  // The second V walks up: 2 at once.
  seen[1].distance = 95.f;
  CHECK(counter.Count(npc, seen, 1.0) == 2);
  // It wanders at the edge (100..130 m): no flicker.
  for (const float d : {101.f, 99.f, 125.f, 105.f, 129.f}) {
    seen[1].distance = d;
    CHECK(counter.Count(npc, seen, 2.0) == 2);
  }
  // Out past 130 m: 1.
  seen[1].distance = 135.f;
  CHECK(counter.Count(npc, seen, 3.0) == 1);
  // Back at 120 m: not yet (it must come within 100 m again).
  seen[1].distance = 120.f;
  CHECK(counter.Count(npc, seen, 4.0) == 1);
  // A sniper 250 m off shoots it: counts, and 5 s after its last shot.
  seen[1] = {.player = 2, .distance = 250.f, .engaged = true};
  CHECK(counter.Count(npc, seen, 10.0) == 2);
  seen[1].engaged = false;
  CHECK(counter.Count(npc, seen, 12.0) == 2);
  CHECK(counter.Count(npc, seen, 14.9) == 2);
  CHECK(counter.Count(npc, seen, 15.1) == 1);
  // Four Vs, and the same player twice in a list counts once.
  seen = {{.player = 1, .distance = 5.f},
          {.player = 2, .distance = 5.f},
          {.player = 3, .distance = 5.f},
          {.player = 3, .distance = 5.f},
          {.player = 4, .distance = 5.f}};
  CHECK(counter.Count(npc, seen, 20.0) == 4);
  CHECK(counter.CountOf(npc) == 4);
  // A player that vanished is left out of the list: it stops counting at
  // once, whatever its hold.
  seen = {{.player = 1, .distance = 5.f},
          {.player = 2, .distance = 5.f, .engaged = true},
          {.player = 3, .distance = 5.f}};
  CHECK(counter.Count(npc, seen, 21.0) == 3);
  seen.pop_back();
  CHECK(counter.Count(npc, seen, 21.5) == 2);
  // The owner's V is never less than one, wherever it is.
  seen = {{.player = 1, .distance = 500.f}};
  CHECK(counter.Count(npc, seen, 40.0) == 1);
  // Each NPC on its own; a dead one keeps nothing.
  seen = {{.player = 1, .distance = 5.f}, {.player = 2, .distance = 5.f}};
  CHECK(counter.Count(8, seen, 41.0) == 2);
  CHECK(counter.Size() == 2);
  counter.Forget(8);
  CHECK(counter.CountOf(8) == 1);
  CHECK(counter.Size() == 1);
  // Forgotten mid-hold: a new NPC by that id starts afresh.
  seen = {{.player = 1, .distance = 5.f}, {.player = 2, .distance = 300.f, .engaged = true}};
  CHECK(counter.Count(9, seen, 50.0) == 2);
  counter.Forget(9);
  seen[1].engaged = false;
  CHECK(counter.Count(9, seen, 51.0) == 1);
}

TEST_CASE("enemy scaling: the NPC digest follows a scale, not an NPC alone") {
  std::map<proto::NpcId, proto::NpcSpawn> npcs{{1, {.npc = 1, .record = 5}}};
  const std::map<proto::NpcId, proto::NpcHealth> health{{1, {.npc = 1, .health = 0.5f}}};
  std::map<proto::NpcId, proto::NpcScale> scales;
  const auto none = consistency::NpcDigest(npcs, health, {}, {}, {}, nullptr, &scales);
  CHECK(none == consistency::NpcDigest(npcs, health));
  // Said alone: the same as never said.
  scales[1] = {.npc = 1, .players = 1};
  CHECK(consistency::NpcDigest(npcs, health, {}, {}, {}, nullptr, &scales) == none);
  scales[1].players = 3;
  const auto three = consistency::NpcDigest(npcs, health, {}, {}, {}, nullptr, &scales);
  CHECK(three != none);
  scales[1].players = 2;
  CHECK(consistency::NpcDigest(npcs, health, {}, {}, {}, nullptr, &scales) != three);
  // The scale of an NPC that is not shared is nobody's business.
  scales[2] = {.npc = 2, .players = 4};
  scales[1].players = 3;
  CHECK(consistency::NpcDigest(npcs, health, {}, {}, {}, nullptr, &scales) == three);
}
