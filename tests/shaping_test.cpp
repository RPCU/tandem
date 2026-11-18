// A bad link on purpose (tandem/shaping.hpp): the model itself, the smoothing
// (players, poses, cars) fed through it, and a real session over a shaped
// UDP link: reliable events all arrive, in order, and the session holds.

#include "tandem/endpoint.hpp"
#include "tandem/interpolation.hpp"
#include "tandem/pose.hpp"
#include "tandem/shaping.hpp"
#include "tandem/vehicle.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <functional>
#include <map>
#include <numbers>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
constexpr std::uint64_t kStart = 1'800'000'000'000'000; // epoch-sized: floats would break
constexpr std::uint64_t kStep = 33'333;                 // 30 Hz
// A bad but playable link: 80 ms each way, up to 60 ms of jitter, one
// datagram in ten lost, one in twenty overtaken.
constexpr net::LinkShape kBad{.latencyMs = 80, .jitterMs = 60, .loss = 0.1f, .reorder = 0.05f};

// What a sender sends at kStart + i * kStep, by when it arrives.
template <class T, class Make>
std::multimap<std::uint64_t, T> Through(net::LinkModel &link, std::uint32_t count, Make make) {
  std::multimap<std::uint64_t, T> arrivals;
  for (std::uint32_t i = 1; i <= count; ++i) {
    const auto sent = kStart + i * kStep;
    if (const auto at = link.Release(sent))
      arrivals.emplace(*at, make(i, sent));
  }
  return arrivals;
}
} // namespace

TEST_CASE("a link model loses, delays and reorders as asked") {
  constexpr int kCount = 20'000;
  SUBCASE("latency and jitter keep the order") {
    net::LinkModel link({.latencyMs = 50, .jitterMs = 30}, 7);
    std::uint64_t last = 0;
    for (int i = 0; i < kCount; ++i) {
      const auto sent = kStart + static_cast<std::uint64_t>(i) * 1000;
      const auto at = link.Release(sent);
      REQUIRE(at);
      CHECK(*at >= sent + 50'000);
      CHECK(*at >= last);
      last = *at;
    }
    CHECK(link.Lost() == 0);
    CHECK(link.Reordered() == 0);
  }
  SUBCASE("loss and reordering at the rates asked") {
    net::LinkModel link({.latencyMs = 20, .jitterMs = 10, .loss = 0.2f, .reorder = 0.1f}, 11);
    int lost = 0, overtaken = 0;
    std::uint64_t latest = 0;
    for (int i = 0; i < kCount; ++i) {
      const auto at = link.Release(kStart + static_cast<std::uint64_t>(i) * 1000);
      if (!at) {
        ++lost;
        continue;
      }
      // Out before one sent earlier (that one was held back).
      if (*at < latest)
        ++overtaken;
      latest = std::max(latest, *at);
      CHECK(*at <= kStart + static_cast<std::uint64_t>(i) * 1000 + 20'000 + 10'000 +
                       net::LinkModel::kReorderHoldUs + 10'000);
    }
    CHECK(lost == doctest::Approx(0.2 * kCount).epsilon(0.1));
    CHECK(link.Reordered() == doctest::Approx(0.1 * 0.8 * kCount).epsilon(0.1));
    CHECK(overtaken > 0); // the ones sent after a held-back one, out before it
  }
}

TEST_CASE("a walking player stays smooth through a bad link") {
  // 3 m/s along +X, a state every 33 ms; drawn at 60 frames a second.
  net::LinkModel link(kBad, 3);
  const auto arrivals =
      Through<proto::PlayerState>(link, 300, [](std::uint32_t i, std::uint64_t t) {
        proto::PlayerState s;
        s.seq = i;
        s.timeUs = t;
        s.position = {3.f * static_cast<float>(t - kStart) / 1e6f, 0.f, 0.f};
        s.velocity = {3.f, 0.f, 0.f};
        return s;
      });
  sync::Track track;
  auto next = arrivals.begin();
  float lastX = -1.f, worstStep = 0.f, worstBack = 0.f, worstError = 0.f;
  int extrapolated = 0, frames = 0;
  for (auto now = kStart + 500'000; now < kStart + 300 * kStep; now += 16'667) {
    for (; next != arrivals.end() && next->first <= now; ++next)
      track.Push(next->second, next->first);
    const auto view = track.At(now);
    REQUIRE(view.valid);
    if (now < kStart + 1'500'000) { // a second to measure the link
      lastX = view.position.x;
      continue;
    }
    ++frames;
    extrapolated += view.extrapolated;
    const float truth = 3.f * static_cast<float>(now - track.DelayUs() - kStart) / 1e6f;
    worstError = std::max(worstError, std::abs(view.position.x - truth));
    worstStep = std::max(worstStep, view.position.x - lastX);
    worstBack = std::max(worstBack, lastX - view.position.x);
    lastX = view.position.x;
  }
  const auto stats = track.Stats();
  MESSAGE("delay ", stats.delayUs / 1000, " ms, jitter ", stats.jitterUs / 1000, " ms, ",
          stats.dropped, " out of order, ", extrapolated, "/", frames,
          " frames extrapolated, worst error ", worstError, " m, worst step ", worstStep, " m");
  // Wider than on a clean link, within the cap.
  CHECK(stats.delayUs > 150'000);
  CHECK(stats.delayUs <= sync::Track::kMaxDelayUs);
  CHECK(stats.dropped > 0); // the overtaken ones: newest wins
  // Never backwards, never a pop: at most a little faster than walking while
  // the delay settles (3 m/s is 0.05 m a frame).
  CHECK(worstBack < 0.01f);
  CHECK(worstStep < 0.12f);
  CHECK(worstError < 0.25f);
  CHECK(extrapolated < frames / 10);
}

TEST_CASE("a fast travel puts the body there, it does not fly it across the map") {
  sync::Track track;
  const auto state = [](std::uint32_t seq, float x, float vx) {
    proto::PlayerState s;
    s.seq = seq;
    s.timeUs = kStart + seq * kStep;
    s.position = {x, 0.f, 0.f};
    s.velocity = {vx, 0.f, 0.f};
    return s;
  };
  for (std::uint32_t i = 1; i <= 10; ++i)
    track.Push(state(i, 0.1f * static_cast<float>(i), 3.f), kStart + i * kStep + 20'000);
  // 2 km away in one sample; the sender's velocity says 60 km/s.
  track.Push(state(11, 2000.f, 2000.f / 0.0333f), kStart + 11 * kStep + 20'000);
  const auto delay = track.DelayUs();
  const auto mid = track.At(kStart + 10 * kStep + kStep / 2 + delay);
  CHECK(mid.position.x == doctest::Approx(1.f)); // still where it was
  CHECK(mid.speed < 3.5f);
  const auto after = track.At(kStart + 11 * kStep + 200'000 + delay);
  CHECK(after.position.x == doctest::Approx(2000.f)); // there, held: no extrapolating the jump
}

TEST_CASE("a car stays on its circle through a bad link") {
  // 20 m/s round a 30 m circle; drawn with the driver's body delay.
  constexpr float kRadius = 30.f, kSpeed = 20.f;
  const auto where = [&](std::uint64_t t) {
    const float angle = static_cast<float>(t - kStart) / 1e6f * kSpeed / kRadius;
    return std::pair{proto::Vec3{kRadius * std::cos(angle), kRadius * std::sin(angle), 0.f},
                     proto::Vec3{-kSpeed * std::sin(angle), kSpeed * std::cos(angle), 0.f}};
  };
  net::LinkModel link(kBad, 5);
  std::multimap<std::uint64_t, std::pair<proto::PlayerState, proto::VehicleState>> arrivals;
  for (std::uint32_t i = 1; i <= 300; ++i) {
    const auto t = kStart + i * kStep;
    const auto [at, v] = where(t);
    // Body and car leave together (one datagram each, the same fate is
    // close enough: both are on the same link).
    if (const auto arrival = link.Release(t))
      arrivals.emplace(*arrival,
                       std::pair{proto::PlayerState{.seq = i, .timeUs = t, .position = at},
                                 proto::VehicleState{.seq = i,
                                                     .timeUs = t,
                                                     .position = at,
                                                     .orientation = sync::FromYaw(0.f),
                                                     .velocity = v}});
  }
  sync::Track body;
  sync::VehicleTrack car;
  auto next = arrivals.begin();
  float worst = 0.f;
  for (auto now = kStart + 500'000; now < kStart + 300 * kStep; now += 16'667) {
    for (; next != arrivals.end() && next->first <= now; ++next) {
      body.Push(next->second.first, next->first);
      car.Push(next->second.second);
    }
    const auto render = now - body.DelayUs();
    const auto view = car.At(render);
    REQUIRE(view.valid);
    if (now < kStart + 1'500'000)
      continue;
    const auto truth = where(render).first;
    worst = std::max(worst, std::hypot(view.position.x - truth.x, view.position.y - truth.y));
  }
  MESSAGE("car: worst distance from its path ", worst, " m");
  CHECK(worst < 0.5f);
}

TEST_CASE("a body pose stays smooth through a bad link") {
  // The head bone turning at 0.5 rad/s, a pose every 33 ms (4 s: within ±pi).
  const int head = pose::BoneIndex("Head");
  REQUIRE(head > 0);
  net::LinkModel link(kBad, 9);
  const auto arrivals = Through<proto::PoseState>(link, 120, [&](std::uint32_t i, std::uint64_t t) {
    pose::Pose p;
    for (auto &q : p.local)
      q = {0.f, 0.f, 0.f, 1.f};
    const float angle = 0.5f * static_cast<float>(t - kStart) / 1e6f;
    p.local[static_cast<std::size_t>(head)] = sync::FromYaw(angle);
    auto state = pose::Pack(p);
    state.seq = i;
    state.timeUs = t;
    return state;
  });
  pose::PoseTrack track;
  auto next = arrivals.begin();
  float worst = 0.f, last = 0.f, worstBack = 0.f;
  const std::uint64_t delay = 200'000; // what a body track settles on over this link
  for (auto now = kStart + 500'000; now < kStart + 120 * kStep; now += 16'667) {
    for (; next != arrivals.end() && next->first <= now; ++next)
      track.Push(next->second);
    pose::Pose drawn;
    REQUIRE(track.At(now - delay, drawn));
    const float angle = sync::YawOf(drawn.local[static_cast<std::size_t>(head)]);
    if (now < kStart + 1'000'000) {
      last = angle;
      continue;
    }
    const float truth = 0.5f * static_cast<float>(now - delay - kStart) / 1e6f;
    worst = std::max(worst, std::abs(angle - truth));
    worstBack = std::max(worstBack, last - angle);
    last = angle;
  }
  MESSAGE("pose: worst angle error ", worst, " rad");
  CHECK(worst < 0.03f);
  CHECK(worstBack < 0.002f);
}

TEST_CASE("over a shaped UDP link, reliable events arrive whole and in order") {
  std::string error;
  auto host = Endpoint::Host({"host"}, 0, error);
  REQUIRE_MESSAGE(host, error);
  // The guest's side of the link, both ways: 40 ms ± 20, 15% lost, 5% overtaken.
  auto shaper = net::Shaper::Start(
      0, "127.0.0.1", host->LocalPort(),
      {.latencyMs = 40, .jitterMs = 20, .loss = 0.15f, .reorder = 0.05f}, error, 42);
  REQUIRE_MESSAGE(shaper, error);
  auto guest = Endpoint::Join({"guest"}, "127.0.0.1", shaper->Port(), error);
  REQUIRE_MESSAGE(guest, error);

  bool ended = false;
  std::vector<std::uint32_t> heldAtHost, worldsAtGuest;
  std::uint32_t states = 0, lastSeq = 0, olderStates = 0;
  const auto pump = [&](std::chrono::milliseconds time, const std::function<bool()> &done) {
    const auto deadline = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < deadline && !done()) {
      for (auto *endpoint : {host.get(), guest.get()})
        for (const auto &event : endpoint->Update(1ms)) {
          if (std::holds_alternative<events::Ended>(event))
            ended = true;
          else if (const auto *held = std::get_if<events::RemoteHeld>(&event))
            heldAtHost.push_back(held->held.revision);
          else if (const auto *world = std::get_if<events::HostWorld>(&event))
            worldsAtGuest.push_back(world->world.revision);
          else if (const auto *state = std::get_if<events::RemoteState>(&event)) {
            ++states;
            olderStates += state->state.seq < lastSeq;
            lastSeq = std::max(lastSeq, state->state.seq);
          }
        }
    }
    return done();
  };
  REQUIRE(pump(10s, [&] { return guest->IsJoined() && host->Players().size() == 2; }));

  // 200 changes each way, as fast as they come, with states streaming.
  for (std::uint32_t i = 1; i <= 200; ++i) {
    guest->SetLocalHeld({.right = i});
    host->SetWorld({.gameTimeSeconds = static_cast<std::int32_t>(i)});
    guest->SendLocalState({.position = {static_cast<float>(i), 0.f, 0.f}});
    pump(2ms, [] { return false; });
  }
  REQUIRE(pump(10s, [&] { return heldAtHost.size() >= 200 && worldsAtGuest.size() >= 200; }));
  pump(500ms, [] { return false; });  // nothing more, nothing twice
  pump(1200ms, [] { return false; }); // a few pings more for the clock
  CHECK_FALSE(ended);
  REQUIRE(heldAtHost.size() == 200);
  REQUIRE(worldsAtGuest.size() == 200);
  for (std::uint32_t i = 0; i < 200; ++i) {
    CHECK(heldAtHost[i] == i + 1);
    CHECK(worldsAtGuest[i] == i + 1);
  }
  // States are unreliable: some lost, some overtaken, never repeated.
  CHECK(states > 100);
  CHECK(states < 200);
  const auto link = shaper->Stats();
  MESSAGE("shaper: ", link.passed, " passed, ", link.lost, " lost, ", link.reordered,
          " held back; states ", states, "/200, ", olderStates, " overtaken");
  CHECK(link.lost > 0);
  CHECK(link.reordered > 0);
  // The guest's idea of the host clock: off by no more than the link's
  // asymmetry (both sides share this process's clock). It stayed the
  // Welcome's, a one-way latency off, until the best-ping filter was fixed.
  const auto skew =
      static_cast<std::int64_t>(guest->HostNowUs()) - static_cast<std::int64_t>(host->HostNowUs());
  CHECK(std::abs(skew) < 30'000);
}
