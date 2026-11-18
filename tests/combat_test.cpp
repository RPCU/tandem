// How the host applies a guest's hit on its NPC (core/include/tandem/combat.hpp).

#include "tandem/combat.hpp"

#include <doctest/doctest.h>

using namespace tandem;
using combat::StrikeLoss;

TEST_CASE("a strike takes its damage, never more than the NPC has") {
  CHECK(StrikeLoss({.damage = 30.f}, 100.f, 100.f, false) == 30.f);
  CHECK(StrikeLoss({.damage = 300.f}, 100.f, 100.f, false) == 100.f);
  CHECK(StrikeLoss({.damage = -5.f}, 100.f, 100.f, false) == 0.f);
  CHECK(StrikeLoss({.damage = 30.f}, 0.f, 100.f, false) == 0.f); // already dead
}

TEST_CASE("an instant kill takes everything") {
  CHECK(StrikeLoss({.damage = 0.f, .flags = proto::kStrikeInstantKill}, 80.f, 100.f, false) ==
        80.f);
}

TEST_CASE("a hit capped to leave 1 HP leaves 1 HP of the host's health") {
  // The proxy had 40 HP, the host's NPC 55 (healed meanwhile): still 1 HP left.
  CHECK(StrikeLoss({.damage = 39.f, .flags = proto::kStrikeLeaveOneHp}, 55.f, 100.f, false) ==
        54.f);
  // The proxy had more than the host: no kill either.
  CHECK(StrikeLoss({.damage = 69.f, .flags = proto::kStrikeLeaveOneHp}, 20.f, 100.f, false) ==
        19.f);
  CHECK(StrikeLoss({.damage = 9.f, .flags = proto::kStrikeLeaveOneHp}, 1.f, 100.f, false) == 0.f);
}

TEST_CASE("the attack's minimum health and immortality hold") {
  // 25 % minimum of 200: 50 HP stay.
  CHECK(StrikeLoss({.damage = 500.f, .minHealth = 25.f}, 120.f, 200.f, false) == 70.f);
  CHECK(StrikeLoss({.damage = 10.f, .minHealth = 25.f}, 120.f, 200.f, false) == 10.f);
  CHECK(StrikeLoss({.damage = 10.f, .minHealth = 25.f}, 40.f, 200.f, false) == 0.f);
  // An immortal NPC keeps 1 %.
  CHECK(StrikeLoss({.damage = 500.f}, 120.f, 200.f, true) == doctest::Approx(118.f));
  // An instant kill is the game's own (RequestSettingStatPoolMinValue on the
  // host, where god mode stops it): the loss is everything.
  CHECK(StrikeLoss({.flags = proto::kStrikeInstantKill}, 50.f, 200.f, true) == 50.f);
}

TEST_CASE("a non-lethal hit takes its damage: the host's defeat mechanic knocks out") {
  CHECK(StrikeLoss({.damage = 300.f, .flags = proto::kStrikeNonlethal}, 100.f, 100.f, false) ==
        100.f);
}

TEST_CASE("an older NpcHit becomes a strike with the same meaning") {
  const auto strike =
      combat::FromHit({.player = 2,
                       .npc = 7,
                       .damage = 35.f,
                       .flags = proto::kHitWeakSpot | proto::kHitMelee | proto::kHitLethal,
                       .position = {1, 2, 3}});
  CHECK(strike.player == 2);
  CHECK(strike.npc == 7);
  CHECK(strike.damage == 35.f);
  CHECK(strike.flags == (proto::kStrikeWeakSpot | proto::kStrikeMelee | proto::kStrikeInstantKill));
  CHECK(strike.attackType == 7);
  CHECK(strike.zone == proto::kNoZone);
  CHECK(strike.position == proto::Vec3{1, 2, 3});
}
