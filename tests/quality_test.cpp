// How smooth remote things look on real connections (docs/architecture/
// netcode.md, "Smoothness"): a walking V, a car and an NPC sent through the
// link model at 30 ms / 1 %, 80 ms / 3 % and 150 ms / 5 % (each with loss
// bursts, jitter and reordering), drawn at 60 frames a second, and measured
// against where they truly were at the moment drawn. Numbers are printed
// (MESSAGE) and bound by the checks.

#include "motion.hpp"

#include "tandem/interpolation.hpp"
#include "tandem/shaping.hpp"
#include "tandem/vehicle.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <vector>

using namespace tandem;
using test::kEpoch;

namespace {
struct LinkCase {
  const char *name;
  net::LinkShape shape;
};
// Each with bursts of lost datagrams and some overtaken.
const LinkCase kLinks[] = {
    {"30 ms 1%", {.latencyMs = 30, .jitterMs = 10, .loss = 0.01f, .reorder = 0.01f, .burst = 2}},
    {"80 ms 3%", {.latencyMs = 80, .jitterMs = 30, .loss = 0.03f, .reorder = 0.02f, .burst = 3}},
    {"150 ms 5%", {.latencyMs = 150, .jitterMs = 50, .loss = 0.05f, .reorder = 0.03f, .burst = 4}},
};

struct Quality {
  float p95Error{}, maxError{}; // m from the truth at the moment drawn
  float maxPop{};               // m: a frame's step off the true step (a snap)
  float worstBack{};            // m: drawn moving backwards along the motion
  double extrapolated{};        // share of frames past the newest state
  std::uint64_t delayMs{};      // drawn this far behind (mean)
  std::string Line(const char *what, const char *link) const {
    return std::format("{:<6} {:<10} error p95 {:.3f} max {:.3f} m, pop {:.3f} m, back {:.3f} m, "
                       "{:.1f}% extrapolated, {} ms behind",
                       what, link, p95Error, maxError, maxPop, worstBack, extrapolated * 100,
                       delayMs);
  }
};

constexpr std::uint64_t kFrameUs = 16'667;     // drawn at 60 fps
constexpr std::uint64_t kWarmUpUs = 3'000'000; // the link measured first
constexpr std::uint64_t kRunUs = 60'000'000;

// Draws a stream sent every `intervalUs` through `link`; `truth(t)` is where
// it was at t, `push(i, t, arrival)` hands the receiver state i sent at t,
// `draw(now, renderUs&, extrapolated&)` where it is drawn.
template <class Truth, class Push, class Draw>
Quality Measure(const net::LinkShape &shape, std::uint64_t seed, std::uint64_t intervalUs,
                Truth truth, Push push, Draw draw) {
  net::LinkModel link(shape, seed);
  std::multimap<std::uint64_t, std::pair<std::uint32_t, std::uint64_t>> arrivals;
  std::uint32_t seq = 0;
  for (std::uint64_t t = kEpoch; t < kEpoch + kRunUs; t += intervalUs) {
    ++seq;
    if (const auto at = link.Release(t))
      arrivals.emplace(*at, std::pair{seq, t});
  }
  Quality q;
  std::vector<float> errors;
  auto next = arrivals.begin();
  proto::Vec3 lastDrawn{}, lastTruth{};
  bool have = false;
  std::uint64_t delaySum = 0, frames = 0, extrapolated = 0;
  for (auto now = kEpoch + 200'000; now < kEpoch + kRunUs - 500'000; now += kFrameUs) {
    for (; next != arrivals.end() && next->first <= now; ++next)
      push(next->second.first, next->second.second, next->first);
    std::uint64_t render = 0;
    bool extra = false;
    const auto drawn = draw(now, render, extra);
    const auto real = truth(render);
    if (now >= kEpoch + kWarmUpUs) {
      const auto d = [](const proto::Vec3 &a, const proto::Vec3 &b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
      };
      errors.push_back(d(drawn, real));
      if (have) {
        const proto::Vec3 step{drawn.x - lastDrawn.x, drawn.y - lastDrawn.y, drawn.z - lastDrawn.z};
        const proto::Vec3 should{real.x - lastTruth.x, real.y - lastTruth.y, real.z - lastTruth.z};
        q.maxPop = std::max(q.maxPop, d(step, should));
        const float len = std::hypot(should.x, should.y, should.z);
        if (len > 1e-4f)
          q.worstBack = std::max(
              q.worstBack, -(step.x * should.x + step.y * should.y + step.z * should.z) / len);
      }
      ++frames;
      extrapolated += extra;
      delaySum += now - render;
    }
    lastDrawn = drawn;
    lastTruth = real;
    have = now >= kEpoch + kWarmUpUs;
  }
  std::ranges::sort(errors);
  q.p95Error = errors[errors.size() * 95 / 100];
  q.maxError = errors.back();
  q.extrapolated = static_cast<double>(extrapolated) / static_cast<double>(frames);
  q.delayMs = delaySum / frames / 1000;
  return q;
}

// A V's state carries its game's velocity; an NPC's none before protocol 21
// (the receiver's own guess between two states), its owner's read between
// its last two samples since (netcode::NpcVelocities).
enum class Pace { Game, None, Sampled };

Quality Walker(const net::LinkShape &shape, std::uint64_t seed, std::uint64_t intervalUs,
               Pace pace = Pace::Game) {
  test::Walker walker;
  sync::Track track(static_cast<std::uint32_t>(1'000'000 / intervalUs));
  return Measure(
      shape, seed, intervalUs, [&](std::uint64_t t) { return walker.At(test::Seconds(t)); },
      [&](std::uint32_t seq, std::uint64_t t, std::uint64_t arrival) {
        auto state = walker.State(t);
        state.seq = seq;
        if (pace == Pace::None)
          state.velocity = {};
        if (pace == Pace::Sampled) {
          const auto before = walker.At(test::Seconds(t - intervalUs));
          const float span = static_cast<float>(intervalUs) / 1e6f;
          state.velocity = {(state.position.x - before.x) / span,
                            (state.position.y - before.y) / span,
                            (state.position.z - before.z) / span};
        }
        track.Push(state, arrival);
      },
      [&](std::uint64_t now, std::uint64_t &render, bool &extra) {
        const auto view = track.At(now);
        render = now - track.DelayUs();
        extra = view.extrapolated;
        return view.position;
      });
}

Quality Car(const net::LinkShape &shape, std::uint64_t seed) {
  test::Car car;
  sync::Track body;
  sync::VehicleTrack track;
  return Measure(
      shape, seed, 33'333, [&](std::uint64_t t) { return car.At(test::Seconds(t)); },
      [&](std::uint32_t seq, std::uint64_t t, std::uint64_t arrival) {
        auto state = car.State(t);
        state.seq = seq;
        track.Push(state);
        body.Push({.seq = seq, .timeUs = t, .position = state.position}, arrival);
      },
      [&](std::uint64_t now, std::uint64_t &render, bool &extra) {
        body.At(now); // the driver's body sets the delay, as in the plugin
        render = now - body.DelayUs();
        const auto view = track.At(render);
        extra = view.extrapolated;
        return view.position;
      });
}
} // namespace

TEST_CASE("smoothness on real connections: numbers within bounds") {
  // Bounds per link (index as kLinks): what a player must not notice.
  struct Bound {
    float walkP95, walkPop, carP95, carPop, npcP95;
    std::uint64_t walkDelayMs;
  };
  const Bound bounds[] = {
      {0.05f, 0.03f, 0.10f, 0.06f, 0.08f, 120},
      {0.08f, 0.04f, 0.20f, 0.10f, 0.12f, 220},
      {0.12f, 0.05f, 0.35f, 0.15f, 0.15f, 330},
  };
  for (std::size_t i = 0; i < std::size(kLinks); ++i) {
    const auto &link = kLinks[i];
    const auto &bound = bounds[i];
    CAPTURE(link.name);
    const auto walk = Walker(link.shape, 11 + i, 33'333);
    const auto car = Car(link.shape, 23 + i);
    // An NPC far from every V (10 Hz) and one near one (30 Hz), with the
    // owner's velocity; and as it was sent before protocol 21, for the record.
    const auto npc = Walker(link.shape, 37 + i, 100'000, Pace::Sampled);
    const auto near = Walker(link.shape, 41 + i, 33'333, Pace::Sampled);
    const auto old = Walker(link.shape, 37 + i, 100'000, Pace::None);
    MESSAGE(walk.Line("V", link.name));
    MESSAGE(car.Line("car", link.name));
    MESSAGE(old.Line("NPC v20", link.name));
    MESSAGE(npc.Line("NPC", link.name));
    MESSAGE(near.Line("NPC 30", link.name));
    CHECK(walk.p95Error < bound.walkP95);
    CHECK(walk.maxPop < bound.walkPop);
    CHECK(walk.worstBack < 0.005f);
    CHECK(walk.delayMs < bound.walkDelayMs);
    CHECK(walk.extrapolated < 0.05);
    CHECK(car.p95Error < bound.carP95);
    CHECK(car.maxPop < bound.carPop);
    CHECK(car.worstBack < 0.01f);
    CHECK(npc.p95Error < bound.npcP95);
    CHECK(npc.worstBack < 0.005f);
    CHECK(npc.maxPop < bound.walkPop);
    CHECK(near.p95Error < bound.walkP95);
    CHECK(near.maxError < (i < 2 ? 0.5f : 0.8f)); // within half a metre near a V
    CHECK(near.worstBack < 0.005f);
    CHECK(near.maxPop < bound.walkPop);
  }
}
