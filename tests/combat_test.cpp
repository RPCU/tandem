// How the host applies a guest's hit on its NPC (core/include/tandem/combat.hpp).

#include "tandem/combat.hpp"

#include <doctest/doctest.h>

#include <limits>

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

TEST_CASE("an NPC's melee hit on a guest waits until its proxy is drawn landing it") {
  // Proxy drawn 120 ms behind the host, the hit 40 ms on the way: 80 ms more.
  CHECK(combat::MeleeHoldUs(120'000, 40'000) == 80'000);
  // A slow link: it arrived after the proxy showed it already, at once.
  CHECK(combat::MeleeHoldUs(100'000, 150'000) == 0);
  CHECK(combat::MeleeHoldUs(100'000, 100'000) == 0);
  // A huge delay never holds a hit for long.
  CHECK(combat::MeleeHoldUs(2'000'000, 10'000) == combat::kMaxMeleeHoldUs);
}

TEST_CASE("friendly fire is the session's rule, off unless the host says so") {
  CHECK_FALSE(combat::FriendlyFire({}));
  CHECK(combat::FriendlyFire({.flags = proto::kRuleFriendlyFire}));
}

TEST_CASE("a copy of a thrown object is put on the real one's path only when it strayed") {
  using combat::FollowPath;
  using combat::PathFix;
  const proto::ThrowPath flying{.position = {10.f, 20.f, 3.f}, .velocity = {0.f, 8.f, 2.f}};
  // The same arc, a frame or two apart: left alone.
  CHECK(FollowPath({10.f, 19.4f, 2.9f}, flying) == PathFix::None);
  CHECK(FollowPath({10.f, 20.f, 3.f}, flying) == PathFix::None);
  // It bounced off something the thrower's game does not have: back on
  // the real one's path, flying on.
  CHECK(FollowPath({13.f, 18.f, 0.2f}, flying) == PathFix::Relaunch);
  // The real one lies still: a copy beside it is left, one that rolled
  // elsewhere is laid where it lies.
  const proto::ThrowPath still{.position = {10.f, 20.f, 0.1f}};
  CHECK(FollowPath({10.2f, 20.1f, 0.1f}, still) == PathFix::None);
  CHECK(FollowPath({11.f, 20.f, 0.1f}, still) == PathFix::Place);
  // Nothing sane to follow.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(FollowPath({nan, 0.f, 0.f}, flying) == PathFix::None);
  CHECK(FollowPath({0.f, 0.f, 0.f}, {.position = {0.f, 0.f, 0.f}, .velocity = {nan, 0.f, 0.f}}) ==
        PathFix::None);
}
