// The bandwidth budget (docs/architecture/netcode.md): typical sessions
// played by two (or four) real endpoints over loopback UDP, as the games
// send (30 Hz states and poses, the host's NPCs at 10 Hz and those within
// 35 m of a V at 30 Hz, events as they come), measured on the wire (ENet's
// datagrams, IP and UDP headers
// included), each way. The budget holds for the compact wire; the legacy
// wire (Endpoint::SetCompact(false)) is measured alongside for the record.

#include "motion.hpp"
#include "slowdown.hpp"

#include "tandem/endpoint.hpp"
#include "tandem/net_traffic.hpp"
#include "tandem/pose.hpp"
#include "tandem/presence.hpp"
#include "tandem/traffic.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
// What a link may carry each way in a two-player session; the host's side
// of a fight with 24 NPCs, or of a dense street, may take more (netcode.md,
// Budget).
constexpr double kBudgetKbps = 256.0;
constexpr double kFightDownKbps = 384.0;

enum class Play { OnFoot, Standing, Fight, Driving, Apart, Scene, Crowd, Traffic };

const char *NameOf(Play play) {
  switch (play) {
  case Play::OnFoot:
    return "on foot together";
  case Play::Standing:
    return "standing together";
  case Play::Fight:
    return "fight, 24 NPCs";
  case Play::Driving:
    return "driving, a passenger";
  case Play::Scene:
    return "a scene, 3 actors, camera";
  case Play::Crowd:
    return "a dense street, 96 NPCs";
  case Play::Traffic:
    return "a street, 24 cars";
  default:
    return "apart (500 m)";
  }
}

struct Result {
  double upKbps{}, downKbps{};
  std::string summary;
  std::string table; // every stream (net::Table)
};

// Runs `play` for `seconds` with `guests` guests; the link measured is the
// first guest's (up: guest to host).
Result Run(Play play, bool compact, int guests = 1, double seconds = 4.0) {
  std::string error;
  auto host = Endpoint::Host({"host", proto::Body::Female}, 0, error);
  REQUIRE_MESSAGE(host, error);
  std::vector<std::unique_ptr<Endpoint>> joined;
  for (int i = 0; i < guests; ++i) {
    joined.push_back(Endpoint::Join({std::format("guest{}", i), proto::Body::Male}, "127.0.0.1",
                                    host->LocalPort(), error));
    REQUIRE_MESSAGE(joined.back(), error);
  }
  std::vector<Endpoint *> all{host.get()};
  for (auto &guest : joined)
    all.push_back(guest.get());
  for (auto *endpoint : all)
    endpoint->SetCompact(compact);
  const auto pump = [&](std::chrono::milliseconds time, const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < deadline && !done())
      for (auto *endpoint : all)
        endpoint->Update(0ms);
    return done();
  };
  REQUIRE(pump(test::Patience(5s), [&] {
    for (auto &guest : joined)
      if (!guest->IsJoined())
        return false;
    return host->Players().size() == all.size();
  }));

  test::Walker walker;
  test::Car car;
  const auto start = net::NowUs();
  const auto warmUp = start + 1'000'000;
  const auto end = warmUp + static_cast<std::uint64_t>(seconds * 1e6);
  std::uint64_t nextFrame = start, nextNpcs = start, nextEvent = start;
  std::uint32_t frame = 0, sceneFrame = 0, npcFrame = 0, trafficFrame = 0;
  std::uint64_t nextTraffic = start;
  traffic::Budget streetBudget;
  std::optional<net::Traffic> before;
  std::uint64_t measuredFrom = 0;
  for (std::size_t i = 0; i < all.size(); ++i)
    all[i]->SetLocalAppearance({.body = proto::Body::Female,
                                .look = std::vector<proto::AppearanceKey>(24, {1, 2}),
                                .items = std::vector<std::uint64_t>(12, 3)});
  while (net::NowUs() < end) {
    const auto now = net::NowUs();
    if (!before && now >= warmUp) {
      before = joined.front()->Traffic();
      measuredFrom = now;
    }
    if (now >= nextFrame) {
      nextFrame += 33'333;
      ++frame;
      const float s = static_cast<float>(now - start) / 1e6f;
      const auto t = test::kEpoch + (now - start);
      for (std::size_t i = 0; i < all.size(); ++i) {
        auto state = walker.State(t);
        const float apart = play == Play::Apart && i > 0 ? 500.f : 3.f * static_cast<float>(i);
        state.position.x += apart;
        float energy = 1.f;
        if (play == Play::Standing || play == Play::Driving || play == Play::Scene ||
            play == Play::Traffic) {
          state.position = {apart, 0.f, 0.f};
          state.velocity = {};
          state.orientation = sync::FromYaw(0.f);
          energy = 0.f;
        }
        if (play == Play::Driving) {
          auto vehicle = car.State(t);
          state.position = vehicle.position;
          state.velocity = vehicle.velocity;
          if (i == 1)
            all[i]->SendLocalVehicle(vehicle); // the guest drives, the host rides
        }
        all[i]->SendLocalState(state);
        all[i]->SendLocalPose(
            pose::Pack(test::WalkCycle(s + 0.3f * static_cast<float>(i), energy)));
        // On foot: each V's footsteps as noise for the other's NPCs, four a
        // second (presence::NoiseGate), and a grenade of its own in flight
        // half the time, its path said four times a second (ThrowPath).
        if (play == Play::OnFoot && frame % 8 == 0)
          all[i]->SendNoise({.stim = presence::kStimFootStepRegular});
        if (play == Play::OnFoot && frame % 8 == 4 && (frame / 60) % 2 == 0)
          all[i]->SendCombat(proto::ThrowPath{
              .id = 1 + frame / 120, .position = state.position, .velocity = {0.f, 8.f, 2.f}});
      }
    }
    if (play == Play::Scene && sceneFrame != frame) {
      // The host's story scene, watched (scene replay): three actors whose
      // bodies its game reads every frame, and a cutscene's camera. What
      // goes is the scene budget's (scene::Pacer).
      sceneFrame = frame;
      const float s = static_cast<float>(now - start) / 1e6f;
      for (proto::NpcId npc = 1; npc <= 3; ++npc) {
        if (frame < 5)
          host->SendNpcSpawn({.npc = npc, .record = 0x1234 + npc, .appearance = 0x55});
        host->SendActorPose(pose::ForActor(
            npc, pose::Pack(test::WalkCycle(s + 0.7f * static_cast<float>(npc), 0.4f))));
      }
      host->SendSceneCamera(scene::PackCamera(1, {{0.5f * s, -3.f, 1.8f}, {}, 50.f}, false));
      // The first actor speaks: its face's float tracks (427, a few dozen
      // moving: blinks, wrinkles, the lipsync envelope).
      const std::vector<float> rest(427, 0.f);
      std::vector<float> face(rest);
      for (std::size_t track = 0; track < 40; ++track)
        face[track * 10] = 0.5f + 0.5f * std::sin(3.f * s + static_cast<float>(track));
      host->SendRigTracks(1, proto::kRigFace, 0x77, rest, face);
    }
    // The host's NPCs as Npcs.reds sends them: every one 10 times a second,
    // those within 35 m of a V on the frames between (netcode.md, NPC
    // motion). A fight: 24 around the Vs, half walking; a dense street: 96
    // over 150 m by 9 (45 near), half walking.
    if ((play == Play::Fight || play == Play::Crowd) && npcFrame != frame) {
      npcFrame = frame;
      const bool whole = now >= nextNpcs;
      if (whole)
        nextNpcs += 100'000;
      const float s = static_cast<float>(now - start) / 1e6f;
      const proto::NpcId count = play == Play::Fight ? 24 : 96;
      proto::NpcStates states;
      for (proto::NpcId npc = 1; npc <= count; ++npc) {
        if (frame < 5)
          host->SendNpcSpawn({.npc = npc, .record = 0x1234 + npc, .appearance = 0x55});
        const float walk = npc % 2 ? 1.5f * s : 0.f;
        const proto::Vec3 at = play == Play::Fight
                                   ? proto::Vec3{10.f + static_cast<float>(npc) + walk, 20.f, 0.f}
                                   : proto::Vec3{-75.f + static_cast<float>(npc % 32) * 4.7f + walk,
                                                 -4.5f + static_cast<float>(npc / 32) * 4.5f, 0.f};
        if (!whole && std::hypot(at.x, at.y) > 35.f)
          continue;
        states.npcs.push_back({.npc = npc,
                               .position = at,
                               .yaw = 0.1f * static_cast<float>(npc) + (npc % 2 ? 0.2f * s : 0.f),
                               .detected = static_cast<std::uint8_t>(npc % 5 == 0 ? 2 : 0)});
      }
      host->SendNpcStates(states, !whole);
      if (whole && play == Play::Fight)
        host->SendNpcAwareness({.entries = {{1, 2, 100}, {2, 2, 120}, {3, 2, 40}, {4, 2, 255}}});
    }
    // The host's street as Traffic.reds sends it: a round of the budget's
    // pick 10 times a second, the moving cars within 50 m of a V on the
    // frames between (traffic.md, Near either V). 24 cars driving at 10 m/s
    // on two lanes 150 m long, 12 of them near.
    if (play == Play::Traffic && trafficFrame != frame) {
      trafficFrame = frame;
      const bool round = now >= nextTraffic;
      if (round)
        nextTraffic += 100'000;
      const float s = static_cast<float>(now - start) / 1e6f;
      std::vector<traffic::Car> cars;
      for (std::uint32_t n = 1; n <= 24; ++n) {
        const auto id = proto::MakeTrafficId(1, n);
        const float lane = n % 2 ? 1.f : -1.f;
        const float x = std::fmod(6.25f * static_cast<float>(n) + 10.f * s, 150.f) - 75.f;
        cars.push_back({.id = id,
                        .position = {lane * x, 6.f * lane, 0.f},
                        .orientation = sync::FromYaw(lane > 0 ? -1.5708f : 1.5708f),
                        .velocity = {10.f * lane, 0.f, 0.f}});
        if (frame < 5)
          host->SendTrafficSpawn({.id = id,
                                  .record = 0x4321 + n,
                                  .appearance = 0x66,
                                  .position = cars.back().position,
                                  .orientation = cars.back().orientation});
      }
      const std::vector<proto::Vec3> viewers{{0.f, 0.f, 0.f}, {3.f, 0.f, 0.f}};
      proto::TrafficStates states;
      states.cars = round ? streetBudget.Pick(cars, viewers) : traffic::PickNear(cars, viewers);
      host->SendTrafficStates(states);
    }
    if (play == Play::Fight && now >= nextEvent) {
      // Twice a second: a guest's strike, the NPC's health and animation,
      // shots both ways with their tracers' ends, a ricochet, a charge.
      nextEvent += 500'000;
      const auto npc = static_cast<proto::NpcId>(1 + frame % 24);
      joined.front()->SendCombat(proto::NpcStrike{.npc = npc, .damage = 10.f, .poise = 5.f});
      host->SendNpcHealth({.npc = npc, .health = 0.5f});
      host->SendNpcAnims({.npcs = {{.npc = npc, .highLevel = 2, .upperBody = 9}}});
      for (int k = 0; k < 3; ++k) {
        host->SendNpcShot({.npc = npc, .effect = 16});
        joined.front()->SendLocalShot({.effect = 16});
        host->SendLocalShot({.effect = 16});
        // Each V's shot says where its bullet ended (the others' tracer).
        joined.front()->SendLocalCue({.kind = proto::CueKind::Trace, .at = {1.f, 2.f, 3.f}});
        host->SendLocalCue({.kind = proto::CueKind::Trace, .at = {1.f, 2.f, 3.f}});
      }
      // Each V takes a hit (HitTaken, for the others' puppet of it).
      joined.front()->SendLocalHitTaken(
          {.by = proto::HitBy::Npc, .attacker = npc, .attackType = 10, .zone = 7});
      host->SendLocalHitTaken(
          {.by = proto::HitBy::Npc, .attacker = npc, .attackType = 10, .zone = 7});
      // A bounce of one of them, and a tech weapon's charge said again
      // (every 0.25 s while it charges: twice here, both ways).
      joined.front()->SendLocalCue({.kind = proto::CueKind::Ricochet, .at = {4.f, 2.f, 3.f}});
      for (int k = 0; k < 2; ++k) {
        joined.front()->SendLocalCue(
            {.kind = proto::CueKind::Charge, .arg = proto::kChargeReady, .at = {0.6f, 0.f, 0.f}});
        host->SendLocalCue({.kind = proto::CueKind::Charge,
                            .arg = proto::kChargeCharging,
                            .at = {0.3f, 0.f, 0.f}});
      }
    }
    for (auto *endpoint : all)
      endpoint->Update(0ms);
  }
  const auto delta = joined.front()->Traffic() - *before;
  const double s = static_cast<double>(net::NowUs() - measuredFrom) / 1e6;
  return {net::WireKbps(delta, net::kSent, s), net::WireKbps(delta, net::kReceived, s),
          net::Summary(delta, s, 4), net::Table(delta, s)};
}
} // namespace

TEST_CASE("typical sessions stay within the bandwidth budget, each way") {
  for (const auto play : {Play::OnFoot, Play::Standing, Play::Fight, Play::Driving, Play::Apart,
                          Play::Scene, Play::Crowd, Play::Traffic}) {
    CAPTURE(NameOf(play));
    const auto legacy = Run(play, false);
    const auto compact = Run(play, true);
    MESSAGE(std::format("{:<22} before: up {:6.1f} down {:6.1f} kbit/s | after: up {:6.1f} down "
                        "{:6.1f} kbit/s | {}",
                        NameOf(play), legacy.upKbps, legacy.downKbps, compact.upKbps,
                        compact.downKbps, compact.summary));
    // The fullest row, stream by stream (the numbers of netcode.md's table).
    const bool npcs = play == Play::Fight || play == Play::Crowd;
    if (npcs)
      MESSAGE(std::format("{}, every stream:\n{}", NameOf(play), compact.table));
    CHECK(compact.upKbps < kBudgetKbps);
    CHECK(compact.downKbps < (npcs ? kFightDownKbps : kBudgetKbps));
    CHECK(compact.upKbps <= legacy.upKbps * 1.05);
  }
}

TEST_CASE("four players on foot: each guest's link") {
  const auto legacy = Run(Play::OnFoot, false, 3);
  const auto compact = Run(Play::OnFoot, true, 3);
  MESSAGE(std::format("4 players on foot      before: up {:6.1f} down {:6.1f} kbit/s | after: up "
                      "{:6.1f} down {:6.1f} kbit/s | {}",
                      legacy.upKbps, legacy.downKbps, compact.upKbps, compact.downKbps,
                      compact.summary));
  // Down carries three Vs (the host's, two relayed): three times one's up.
  CHECK(compact.upKbps < kBudgetKbps);
  CHECK(compact.downKbps < 3 * kBudgetKbps);
}
