#include "tandem/endpoint.hpp"
#include "tandem/vehicle.hpp"

#include <doctest/doctest.h>

using namespace tandem;

namespace {
proto::VehicleState At(std::uint32_t seq, std::uint64_t timeUs, float x, float vx = 0.f) {
  return {.seq = seq, .timeUs = timeUs, .position = {x, 0.f, 0.f}, .velocity = {vx, 0.f, 0.f}};
}
} // namespace

TEST_CASE("a car is drawn between its states, then along its velocity, then held") {
  sync::VehicleTrack track;
  CHECK_FALSE(track.At(0).valid);
  track.Push(At(1, 1'000'000, 0.f, 10.f));
  track.Push(At(2, 1'100'000, 1.f, 10.f));
  track.Push(At(1, 1'000'000, 0.f)); // late: dropped

  auto view = track.At(1'050'000);
  REQUIRE(view.valid);
  CHECK(view.position.x == doctest::Approx(0.5f));
  CHECK_FALSE(view.extrapolated);

  view = track.At(1'300'000); // 200 ms past the newest, at 10 m/s
  CHECK(view.extrapolated);
  CHECK(view.position.x == doctest::Approx(3.f));

  view = track.At(5'000'000); // long gone: held 500 ms ahead
  CHECK(view.position.x == doctest::Approx(6.f));
  CHECK(view.velocity.x == 0.f);
}

TEST_CASE("where a player sits reaches a late joiner") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 41901, error);
  REQUIRE_MESSAGE(host, error);
  host->SetLocalSeat({.vehicle = 77, .seat = 5});
  auto late = Endpoint::Join({"late"}, "127.0.0.1", 41901, error);
  REQUIRE_MESSAGE(late, error);
  std::optional<proto::Seat> seat;
  for (int i = 0; i < 400 && !seat; ++i) {
    host->Update(std::chrono::milliseconds(1));
    for (const auto &event : late->Update(std::chrono::milliseconds(1)))
      if (const auto *s = std::get_if<events::RemoteSeat>(&event))
        seat = s->seat;
  }
  REQUIRE(seat);
  CHECK(seat->vehicle == 77);
  CHECK(seat->owner == 1); // the host drives its own car
}

TEST_CASE("the host's world reaches guests, late joiners included; a guest cannot set it") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 41902, error);
  REQUIRE_MESSAGE(host, error);
  host->SetWorld({.gameTimeSeconds = 64800, .weather = 99});
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", 41902, error);
  REQUIRE_MESSAGE(guest, error);
  std::optional<proto::World> world;
  bool hostHeard = false;
  for (int i = 0; i < 400 && !world; ++i) {
    for (const auto &event : host->Update(std::chrono::milliseconds(1)))
      hostHeard |= std::holds_alternative<events::HostWorld>(event);
    for (const auto &event : guest->Update(std::chrono::milliseconds(1)))
      if (const auto *w = std::get_if<events::HostWorld>(&event))
        world = w->world;
  }
  REQUIRE(world);
  CHECK(world->gameTimeSeconds == 64800);
  CHECK(world->weather == 99);
  guest->SetWorld({.gameTimeSeconds = 1}); // ignored: guests follow
  for (int i = 0; i < 50; ++i) {
    guest->Update(std::chrono::milliseconds(1));
    for (const auto &event : host->Update(std::chrono::milliseconds(1)))
      hostHeard |= std::holds_alternative<events::HostWorld>(event);
  }
  CHECK_FALSE(hostHeard);
}

TEST_CASE("a late joiner gets the host's NPCs where they are now, not the gone ones") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 41903, error);
  REQUIRE_MESSAGE(host, error);
  host->SendNpcSpawn({.npc = 1, .record = 10, .position = {0, 0, 0}});
  host->SendNpcSpawn({.npc = 2, .record = 20});
  host->SendNpcStates({.npcs = {{.npc = 1, .position = {5, 0, 0}}}});
  host->SendNpcGone({.npc = 2});
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", 41903, error);
  REQUIRE_MESSAGE(guest, error);
  std::vector<proto::NpcSpawn> spawns;
  for (int i = 0; i < 400 && spawns.empty(); ++i) {
    host->Update(std::chrono::milliseconds(1));
    for (const auto &event : guest->Update(std::chrono::milliseconds(1)))
      if (const auto *s = std::get_if<events::HostNpcSpawn>(&event))
        spawns.push_back(s->spawn);
  }
  for (int i = 0; i < 20; ++i) {
    host->Update(std::chrono::milliseconds(1));
    for (const auto &event : guest->Update(std::chrono::milliseconds(1)))
      if (const auto *s = std::get_if<events::HostNpcSpawn>(&event))
        spawns.push_back(s->spawn);
  }
  REQUIRE(spawns.size() == 1);
  CHECK(spawns[0].npc == 1);
  CHECK(spawns[0].position.x == 5.f);
}
