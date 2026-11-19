#include "tandem/presence.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace tandem;
using movement::Detailed;
using presence::Event;
using presence::Gait;
using presence::Kind;
using presence::Observer;
using presence::Status;

namespace {
constexpr float kFrame = 1.f / 60.f;
constexpr float kPi = std::numbers::pi_v<float>;

std::uint16_t Flags(Detailed detailed, movement::Swim swim = movement::Swim::None) {
  return movement::Pack({.known = true, .detailed = detailed, .swim = swim});
}

// A V walked along +Y at `speed` for `seconds`; what was heard.
struct Walk {
  Observer observer;
  proto::Vec3 at;
  std::vector<Event> heard;

  void For(float seconds, float speed, std::uint16_t flags, bool crouched = false,
           float climb = 0.f) {
    for (float t = 0.f; t < seconds; t += kFrame) {
      at.y += speed * kFrame;
      at.z += climb * kFrame;
      observer.Move(at, flags, crouched, kFrame);
      for (const auto &event : observer.Take())
        heard.push_back(event);
    }
  }
  int Count(Kind kind) const {
    int n = 0;
    for (const auto &event : heard)
      n += event.kind == kind;
    return n;
  }
};

Gait GaitOf(const Event &event) { return static_cast<Gait>(event.arg & presence::kStepGaitMask); }
} // namespace

TEST_SUITE("presence") {
  TEST_CASE("a walking V plants a foot every stride, left then right") {
    Walk walk;
    walk.For(10.f, 1.5f, Flags(Detailed::Stand));
    // 15 m at 0.72 m a step.
    CHECK(walk.Count(Kind::Step) >= 19);
    CHECK(walk.Count(Kind::Step) <= 22);
    REQUIRE(walk.heard.size() >= 2);
    CHECK(GaitOf(walk.heard[0]) == Gait::Walk);
    CHECK((walk.heard[0].arg & presence::kStepLeft) != (walk.heard[1].arg & presence::kStepLeft));
  }

  TEST_CASE("the gait follows the speed and the stance") {
    Walk run;
    run.For(4.f, 4.f, Flags(Detailed::Stand));
    REQUIRE(!run.heard.empty());
    CHECK(GaitOf(run.heard.back()) == Gait::Run);

    Walk sprint;
    sprint.For(4.f, 7.f, Flags(Detailed::Sprint));
    REQUIRE(!sprint.heard.empty());
    CHECK(GaitOf(sprint.heard.back()) == Gait::Sprint);
    // Longer strides: fewer steps per metre than a walk.
    CHECK(sprint.Count(Kind::Step) < 28.f / Observer::kStride[1]);

    Walk sneak;
    sneak.For(4.f, 1.2f, Flags(Detailed::Crouch), true);
    REQUIRE(!sneak.heard.empty());
    CHECK(GaitOf(sneak.heard.back()) == Gait::Sneak);
  }

  TEST_CASE("a mock player that fills no flags still walks audibly") {
    Walk walk;
    walk.For(5.f, 1.5f, 0);
    CHECK(walk.Count(Kind::Step) >= 8);
  }

  TEST_CASE("standing still, swimming, sliding and teleports are silent") {
    Walk still;
    still.For(5.f, 0.f, Flags(Detailed::Stand));
    CHECK(still.heard.empty());

    Walk swim;
    swim.For(5.f, 2.f, Flags(Detailed::Stand, movement::Swim::Surface));
    CHECK(swim.Count(Kind::Step) == 0);

    Walk slide;
    slide.For(1.f, 6.f, Flags(Detailed::Slide));
    CHECK(slide.Count(Kind::Step) == 0);

    // In a car (the body moves at the car's speed): nobody walks.
    Walk driven;
    driven.For(5.f, 25.f, Flags(Detailed::Stand));
    CHECK(driven.heard.empty());

    Walk put;
    put.For(1.f, 0.f, Flags(Detailed::Stand));
    put.at = {500.f, 500.f, 20.f};
    put.For(1.f, 0.f, Flags(Detailed::Stand));
    CHECK(put.heard.empty());
  }

  TEST_CASE("the first step after standing comes within a stride") {
    Walk walk;
    walk.For(1.f, 0.f, Flags(Detailed::Stand));
    walk.For(0.45f, 1.5f, Flags(Detailed::Stand)); // 0.67 m
    CHECK(walk.Count(Kind::Step) == 1);
  }

  TEST_CASE("a jump is heard once, and its landing by how hard it is") {
    Walk walk;
    walk.For(0.5f, 3.f, Flags(Detailed::Stand));
    walk.heard.clear();
    walk.For(0.3f, 3.f, Flags(Detailed::Jump), false, 3.f);
    walk.For(0.3f, 3.f, Flags(Detailed::Fall), false, -3.f);
    CHECK(walk.Count(Kind::Jump) == 1);
    CHECK(walk.Count(Kind::Step) == 0);
    CHECK(walk.Count(Kind::Land) == 0);
    walk.For(0.2f, 3.f, Flags(Detailed::RegularLand));
    REQUIRE(walk.Count(Kind::Land) == 1);
    CHECK(walk.heard.back() == Event{Kind::Land, 0});

    // A double jump is a second jump.
    walk.heard.clear();
    walk.For(0.2f, 3.f, Flags(Detailed::Jump), false, 3.f);
    walk.For(0.2f, 3.f, Flags(Detailed::DoubleJump), false, 3.f);
    CHECK(walk.Count(Kind::Jump) == 2);
    CHECK(walk.heard.back() == Event{Kind::Jump, 1});
    walk.For(0.5f, 0.f, Flags(Detailed::Fall), false, -12.f);
    walk.heard.clear();
    walk.For(0.1f, 0.f, Flags(Detailed::HardLand));
    CHECK(walk.heard.front() == Event{Kind::Land, 1});
  }

  TEST_CASE("climbs, vaults, dashes and the end of a slide are heard once each") {
    Walk walk;
    walk.For(0.2f, 3.f, Flags(Detailed::Sprint));
    walk.heard.clear();
    walk.For(0.3f, 1.f, Flags(Detailed::Climb), false, 2.f);
    walk.For(0.2f, 3.f, Flags(Detailed::Stand));
    walk.For(0.3f, 3.f, Flags(Detailed::Vault));
    walk.For(0.2f, 3.f, Flags(Detailed::Stand));
    walk.For(0.2f, 8.f, Flags(Detailed::Dodge));
    walk.For(0.2f, 3.f, Flags(Detailed::Sprint));
    walk.For(0.5f, 6.f, Flags(Detailed::Slide));
    walk.For(0.2f, 3.f, Flags(Detailed::Crouch), true);
    CHECK(walk.Count(Kind::Climb) == 1);
    CHECK(walk.Count(Kind::Vault) == 1);
    CHECK(walk.Count(Kind::Dash) == 1);
    CHECK(walk.Count(Kind::SlideEnd) == 1);
    // Climbing onto a ledge is not a jump, and coming off it not a landing.
    CHECK(walk.Count(Kind::Jump) == 0);
    CHECK(walk.Count(Kind::Land) == 0);
  }

  TEST_CASE("a fall with no landing state is judged by its speed") {
    Walk walk;
    walk.For(0.2f, 0.f, Flags(Detailed::Stand));
    walk.For(1.f, 0.f, Flags(Detailed::Fall), false, -15.f);
    walk.heard.clear();
    walk.For(0.1f, 0.f, Flags(Detailed::Stand));
    REQUIRE(!walk.heard.empty());
    CHECK(walk.heard.front() == Event{Kind::Land, 2});
  }

  TEST_CASE("a ladder is climbed rung by rung") {
    Walk walk;
    walk.For(3.f, 0.f, Flags(Detailed::Ladder), false, 1.5f); // 4.5 m up
    CHECK(walk.Count(Kind::Step) >= 9);
    CHECK(walk.Count(Kind::Step) <= 11);
    CHECK((walk.heard.front().arg & presence::kStepLadder) != 0);
  }

  TEST_CASE("pain is heard from a fall of health, not from a gain or a tick") {
    Observer observer;
    observer.Health(255); // where it starts: nothing
    CHECK(observer.Take().empty());
    observer.Move({}, 0, false, 1.f);
    observer.Move({}, 0, false, 1.f);
    observer.Health(254); // a tick
    CHECK(observer.Take().empty());
    observer.Health(240);
    CHECK(observer.Take() == std::vector<Event>{{Kind::Hurt, 0}});
    // Another hit right behind it: one grunt.
    observer.Health(230);
    CHECK(observer.Take().empty());
    observer.Move({}, 0, false, 0.5f);
    observer.Health(150);
    CHECK(observer.Take() == std::vector<Event>{{Kind::Hurt, 2}});
    observer.Move({}, 0, false, 0.5f);
    observer.Health(255); // healed
    CHECK(observer.Take().empty());
    observer.Move({}, 0, false, 0.5f);
    observer.Health(220);
    CHECK(observer.Take() == std::vector<Event>{{Kind::Hurt, 1}});
  }

  TEST_CASE("draws, holsters, going down and abilities are heard on change only") {
    Observer observer;
    observer.Held(42); // drawn before we came: silent
    observer.Life(proto::Life::Up);
    observer.Abilities(0);
    CHECK(observer.Take().empty());
    observer.Held(42);
    CHECK(observer.Take().empty());
    observer.Held(0);
    observer.Held(7);
    observer.Held(8);
    CHECK(observer.Take() ==
          std::vector<Event>{{Kind::Holster, 0}, {Kind::Draw, 0}, {Kind::Draw, 0}});
    observer.Life(proto::Life::Down);
    observer.Life(proto::Life::Out); // still on the ground: no second cry
    observer.Life(proto::Life::Up);
    CHECK(observer.Take() == std::vector<Event>{{Kind::Down, 0}, {Kind::Up, 0}});
    observer.Abilities(proto::kTimeSandevistan);
    observer.Abilities(proto::kTimeSandevistan);
    observer.Abilities(0);
    CHECK(observer.Take() == std::vector<Event>{{Kind::AbilityOn, proto::kTimeSandevistan},
                                                {Kind::AbilityOff, proto::kTimeSandevistan}});
  }

  TEST_CASE("health travels in a byte and a sliver is never dead") {
    CHECK(presence::HealthCode(1.f) == 255);
    CHECK(presence::HealthCode(1.7f) == 255);
    CHECK(presence::HealthCode(0.f) == 0);
    CHECK(presence::HealthCode(-1.f) == 0);
    CHECK(presence::HealthCode(0.0005f) == 1);
    CHECK(presence::HealthFraction(presence::HealthCode(0.5f)) ==
          doctest::Approx(0.5f).epsilon(0.01));
  }

  TEST_CASE("presence is sent at once for flags and losses, else five times a second") {
    presence::Gate gate;
    CHECK(gate.ShouldSend(255, 0, 0));
    CHECK(!gate.ShouldSend(255, 0, 10'000));
    CHECK(gate.ShouldSend(255, proto::kPresenceMenu, 20'000));
    CHECK(gate.ShouldSend(200, proto::kPresenceMenu, 30'000));  // hit
    CHECK(!gate.ShouldSend(201, proto::kPresenceMenu, 40'000)); // regenerating
    CHECK(!gate.ShouldSend(203, proto::kPresenceMenu, 100'000));
    CHECK(gate.ShouldSend(205, proto::kPresenceMenu, 240'000));
    CHECK(!gate.ShouldSend(204, proto::kPresenceMenu, 250'000)); // a tick
    CHECK(gate.ShouldSend(0, proto::kPresenceMenu, 260'000));    // the last of it
  }

  TEST_CASE("the HUD says the most important thing about the partner") {
    using proto::Life;
    using proto::Place;
    CHECK(presence::StatusOf(Life::Up, Place::City, 1, 0, false, 0) == Status::Up);
    CHECK(presence::StatusOf(Life::Up, Place::City, 1, 0, false, proto::kPresenceMenu) ==
          Status::Menu);
    CHECK(presence::StatusOf(Life::Up, Place::City, 1, 0, true, 0) == Status::Menu);
    CHECK(presence::StatusOf(Life::Up, Place::City, 3, 0, true, 0) == Status::Scene);
    CHECK(presence::StatusOf(Life::Up, Place::City, 2, 0, false, 0) == Status::Up);
    CHECK(presence::StatusOf(Life::Up, Place::Cyberspace, 4, 0, false, 0) == Status::Elsewhere);
    CHECK(presence::StatusOf(Life::Up, Place::City, 1, proto::kStoryBraindance, false, 0) ==
          Status::Elsewhere);
    CHECK(presence::StatusOf(Life::Down, Place::Braindance, 4, 0, true, proto::kPresenceMenu) ==
          Status::Down);
    CHECK(presence::StatusOf(Life::Out, Place::City, 1, 0, false, 0) == Status::Out);
    CHECK(std::string_view(presence::NameOf(Status::Scene)) == "scene");
  }

  TEST_CASE("the partner's bearing from the camera") {
    const proto::Vec3 camera{10.f, 10.f, 2.f};
    const proto::Vec3 north{0.f, 1.f, 0.f};
    const float half = 40.f * kPi / 180.f;

    auto ahead = presence::BearingOf(camera, north, half, {10.f, 30.f, 2.f});
    CHECK(ahead.distance == doctest::Approx(20.f));
    CHECK(ahead.angle == doctest::Approx(0.f));
    CHECK(ahead.inView);
    CHECK(ahead.side == 0);

    auto right = presence::BearingOf(camera, north, half, {40.f, 10.f, 2.f});
    CHECK(right.angle == doctest::Approx(kPi / 2.f));
    CHECK(!right.inView);
    CHECK(right.side == 1);

    auto left = presence::BearingOf(camera, north, half, {-20.f, 12.f, 2.f});
    CHECK(left.angle < 0.f);
    CHECK(left.side == 3);

    auto behind = presence::BearingOf(camera, north, half, {11.f, -50.f, 2.f});
    CHECK(!behind.inView);
    CHECK(behind.side == 2);

    // Ahead but far above the frame: not in view, and the arrow points ahead.
    auto above = presence::BearingOf(camera, north, half, {10.f, 20.f, 40.f});
    CHECK(!above.inView);
    CHECK(above.side == 0);
    CHECK(above.pitch > 1.f);

    // The camera's length and tilt do not matter to the heading.
    auto tilted = presence::BearingOf(camera, {0.f, 5.f, -2.f}, half, {40.f, 10.f, 2.f});
    CHECK(tilted.angle == doctest::Approx(kPi / 2.f));
  }

  TEST_CASE("a point of the world on the screen, for the nameplate") {
    const proto::Vec3 camera{0.f, 0.f, 1.7f};
    const proto::Vec3 north{0.f, 2.f, 0.f}, east{3.f, 0.f, 0.f};
    const float half = 45.f * kPi / 180.f; // horizontal: the frame's edge is at 45°
    const float aspect = 16.f / 9.f;

    const auto centre =
        presence::ProjectOf(camera, north, east, half, false, aspect, {0.f, 10.f, 1.7f});
    CHECK(centre.visible);
    CHECK(centre.x == doctest::Approx(0.5f));
    CHECK(centre.y == doctest::Approx(0.5f));

    // To the right and above: right of the centre, and nearer the top.
    const auto upRight =
        presence::ProjectOf(camera, north, east, half, false, aspect, {5.f, 10.f, 3.7f});
    CHECK(upRight.visible);
    CHECK(upRight.x == doctest::Approx(0.75f));
    CHECK(upRight.y < 0.5f);
    // The right edge of the frame is 45° off the axis.
    CHECK(presence::ProjectOf(camera, north, east, half, false, aspect, {10.f, 10.f, 1.7f}).x ==
          doctest::Approx(1.f));
    CHECK_FALSE(
        presence::ProjectOf(camera, north, east, half, false, aspect, {11.f, 10.f, 1.7f}).visible);
    // Behind the camera: never drawn (no mirrored plate).
    CHECK_FALSE(
        presence::ProjectOf(camera, north, east, half, false, aspect, {0.f, -10.f, 1.7f}).visible);

    // The same half angle read as vertical makes the frame wider.
    const auto wide =
        presence::ProjectOf(camera, north, east, half, true, aspect, {5.f, 10.f, 1.7f});
    CHECK(wide.x < 0.75f);
    CHECK(wide.x > 0.5f);
    // The top edge of a vertical 45° half angle.
    CHECK(presence::ProjectOf(camera, north, east, half, true, aspect, {0.f, 10.f, 11.7f}).y ==
          doctest::Approx(0.f).epsilon(0.001));

    // A camera with no direction draws nothing.
    CHECK_FALSE(
        presence::ProjectOf(camera, {}, east, half, false, aspect, {0.f, 10.f, 1.7f}).visible);
  }

  TEST_CASE("distances and the nameplate's fade") {
    CHECK(presence::RoundedMetres(12.4f) == 12);
    CHECK(presence::RoundedMetres(999.4f) == 999);
    CHECK(presence::RoundedMetres(1234.f) == 1200);
    CHECK(presence::RoundedMetres(-3.f) == 0);

    CHECK(presence::NameplateOpacity(3.f) == 1.f);
    CHECK(presence::NameplateOpacity(16.f) == doctest::Approx(0.5f));
    CHECK(presence::NameplateOpacity(25.f) == 0.f);
  }

  TEST_CASE("a hit puppet flinches the way the hit went and comes back to rest") {
    const proto::Vec3 north{0.f, 1.f, 0.f};
    presence::Flinch flinch;
    CHECK(!flinch.Active(0));
    CHECK(flinch.At(123) == presence::Lean{});

    // Facing +Y, shot from the front: the bullet travels −Y.
    flinch.Hit(1.f, {0.f, -1.f, 0.f}, north, 1'000'000);
    CHECK(flinch.Active(1'000'000));
    const auto first = flinch.At(1'010'000);
    CHECK(first.pitch < -0.15f); // knocked back
    CHECK(std::abs(first.roll) < 1e-3f);
    for (std::uint64_t t = 1'000'000; t < 1'450'000; t += 16'000)
      CHECK(std::abs(flinch.At(t).pitch) <= presence::Flinch::kMaxAngle);
    // Nearly at rest before it ends: no pop.
    CHECK(std::abs(flinch.At(1'440'000).pitch) < 0.002f);
    CHECK(!flinch.Active(1'450'000));
    CHECK(flinch.At(1'450'000) == presence::Lean{});

    // From its left side (travelling +X, to its right): it rolls right.
    flinch.Hit(0.5f, {1.f, 0.f, 0.f}, north, 2'000'000);
    const auto side = flinch.At(2'010'000);
    CHECK(side.roll > 0.05f);
    CHECK(std::abs(side.pitch) < 1e-3f);

    // The same hit on a body facing −X: now from the front.
    flinch.Hit(0.5f, {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, 3'000'000);
    CHECK(flinch.At(3'010'000).pitch < -0.05f);

    // No direction known: from the front.
    flinch.Hit(0.5f, {}, north, 4'000'000);
    CHECK(flinch.At(4'010'000).pitch < -0.05f);

    // A light hit shows less than a heavy one, but shows.
    presence::Flinch light, heavy;
    light.Hit(0.f, {0.f, -1.f, 0.f}, north, 0);
    heavy.Hit(1.f, {0.f, -1.f, 0.f}, north, 0);
    const auto l = light.At(10'000), h = heavy.At(10'000);
    CHECK(std::abs(l.pitch) > 0.05f);
    CHECK(std::abs(l.pitch) < std::abs(h.pitch));
  }
}
