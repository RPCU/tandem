// Activities outside combat and quests (docs/architecture/activities.md):
// what a received Activity may say, the host's time skip arbiter, and both
// over loopback UDP with a real host and two guests.

#include "ports.hpp"
#include "slowdown.hpp"
#include "tandem/activity.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <limits>

using namespace std::chrono_literals;
using namespace tandem;

TEST_CASE("a received activity says only what it can mean") {
  using proto::ActivityKind;
  // An unknown kind is nothing, and nothing has no fields.
  auto odd = activity::Sanitized({.player = 2,
                                  .kind = static_cast<ActivityKind>(42),
                                  .flags = 7,
                                  .workspot = 5,
                                  .anchor = {1, 2, 3}});
  CHECK(odd == proto::Activity{.player = 2});
  // Unknown flags go; an anchor that is not a place goes with its flag.
  auto broken = activity::Sanitized({.kind = ActivityKind::Workspot,
                                     .flags = 0xFF,
                                     .workspot = 9,
                                     .entry = 3,
                                     .anchor = {std::numeric_limits<float>::quiet_NaN(), 0, 0}});
  CHECK(broken.flags == 0);
  CHECK(broken.anchor == proto::Vec3{});
  CHECK(broken.workspot == 9);
  // A body lies only in a bed or a workspot, and only where it is held.
  auto lying = activity::Sanitized(
      {.kind = ActivityKind::Sleep, .flags = proto::kActivityLying | proto::kActivityAnchored});
  CHECK((lying.flags & proto::kActivityLying) != 0);
  auto standing =
      activity::Sanitized({.kind = ActivityKind::Sleep, .flags = proto::kActivityLying});
  CHECK((standing.flags & proto::kActivityLying) == 0);
  auto screen = activity::Sanitized(
      {.kind = ActivityKind::Photo, .flags = proto::kActivityLying | proto::kActivityAnchored});
  CHECK((screen.flags & proto::kActivityLying) == 0);
  // Photo mode names no workspot; a workspot game runs (never frozen).
  auto photo = activity::Sanitized(
      {.kind = ActivityKind::Photo, .flags = proto::kActivityFrozen, .workspot = 4, .entry = 2});
  CHECK(photo.workspot == 0);
  CHECK(photo.entry == 0);
  CHECK(activity::Frozen(photo));
  auto seat = activity::Sanitized({.kind = ActivityKind::Workspot,
                                   .flags = proto::kActivityFrozen | proto::kActivityAnchored,
                                   .workspot = 4});
  CHECK_FALSE(activity::Frozen(seat));
  CHECK(activity::Anchored(seat));
  // No workspot: no entry or animation of one.
  auto bare = activity::Sanitized({.kind = ActivityKind::Screen, .entry = 3, .anim = 8});
  CHECK(bare.entry == 0);
  CHECK(bare.anim == 0);
  // A contest keeps its sort (in `entry`) and its opponent (in `device`),
  // names no workspot and holds nobody; an unknown sort is some other one.
  auto duel = activity::Sanitized({.player = 2,
                                   .kind = ActivityKind::Contest,
                                   .flags = proto::kActivityAnchored | proto::kActivityFrozen,
                                   .device = 7,
                                   .workspot = 4,
                                   .entry = static_cast<std::uint32_t>(proto::ContestSort::Duel),
                                   .anim = 8,
                                   .anchor = {1, 2, 3},
                                   .yaw = 1.f});
  CHECK(duel == proto::Activity{.player = 2,
                                .kind = ActivityKind::Contest,
                                .device = 7,
                                .entry = static_cast<std::uint32_t>(proto::ContestSort::Duel)});
  CHECK(activity::Sanitized({.kind = ActivityKind::Contest, .entry = 99}).entry ==
        static_cast<std::uint32_t>(proto::ContestSort::Other));
  CHECK(activity::Sanitized({.kind = ActivityKind::Contest, .entry = 3}).entry == 3);
}

TEST_CASE("a remote view says its sort, its object, where it looks from, and its car") {
  using proto::ActivityKind;
  using proto::RemoteSort;
  const auto car = static_cast<std::uint32_t>(RemoteSort::Vehicle);
  const auto camera = static_cast<std::uint32_t>(RemoteSort::Camera);
  // A car driven remotely: nothing holds the body, no workspot plays.
  auto driven = activity::Sanitized({.player = 3,
                                     .kind = ActivityKind::Remote,
                                     .flags = proto::kActivityAnchored | proto::kActivityFrozen,
                                     .device = 9,
                                     .workspot = 4,
                                     .entry = car,
                                     .anim = 8,
                                     .anchor = {1, 2, 3},
                                     .yaw = 1.f,
                                     .view = {10, 20, 3},
                                     .traffic = 0x02000005u});
  CHECK(driven == proto::Activity{.player = 3,
                                  .kind = ActivityKind::Remote,
                                  .device = 9,
                                  .entry = car,
                                  .view = {10, 20, 3},
                                  .traffic = 0x02000005u});
  CHECK_FALSE(activity::Anchored(driven));
  CHECK_FALSE(activity::Frozen(driven));
  CHECK(activity::ViewOf(driven) == proto::Vec3{10, 20, 3});
  CHECK(activity::RemoteCar(driven) == 0x02000005u);
  // Only a car names a shared vehicle; an unknown sort is some other view.
  auto looking = activity::Sanitized(
      {.kind = ActivityKind::Remote, .entry = camera, .view = {5, 5, 5}, .traffic = 7});
  CHECK(looking.traffic == 0);
  CHECK(activity::RemoteCar(looking) == 0);
  CHECK(activity::Sanitized({.kind = ActivityKind::Remote, .entry = 42}).entry ==
        static_cast<std::uint32_t>(RemoteSort::Other));
  // A view that is no place is none known.
  auto lost = activity::Sanitized({.kind = ActivityKind::Remote,
                                   .entry = camera,
                                   .view = {std::numeric_limits<float>::infinity(), 0, 0}});
  CHECK_FALSE(activity::ViewOf(lost).has_value());
  // Another kind carries neither.
  auto seat = activity::Sanitized(
      {.kind = ActivityKind::Workspot, .workspot = 4, .view = {1, 1, 1}, .traffic = 3});
  CHECK(seat.view == proto::Vec3{});
  CHECK(seat.traffic == 0);
  CHECK_FALSE(activity::ViewOf(seat).has_value());
}

TEST_CASE("the time skip arbiter: every other player says yes, or nothing moves") {
  activity::SkipArbiter arbiter;
  const proto::TimeSkip skip{.from = 2,
                             .request = 1,
                             .reason = proto::SkipReason::Wait,
                             .fromSeconds = 10 * 3600,
                             .toSeconds = 14 * 3600};
  const std::uint64_t t0 = 1'000'000;

  SUBCASE("everyone accepts: applied, with the clocks it names") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    CHECK(arbiter.Waiting() == std::set<proto::PlayerId>{1, 3});
    CHECK_FALSE(arbiter.Vote({.from = 1, .asker = 2, .request = 1, .accepted = true}));
    // A repeat, the asker's own yes, a vote about another request: nothing.
    CHECK_FALSE(arbiter.Vote({.from = 1, .asker = 2, .request = 1, .accepted = true}));
    CHECK_FALSE(arbiter.Vote({.from = 2, .asker = 2, .request = 1, .accepted = true}));
    CHECK_FALSE(arbiter.Vote({.from = 3, .asker = 2, .request = 9, .accepted = true}));
    const auto result = arbiter.Vote({.from = 3, .asker = 2, .request = 1, .accepted = true});
    REQUIRE(result);
    CHECK(result->outcome == proto::SkipOutcome::Applied);
    CHECK(result->fromSeconds == skip.fromSeconds);
    CHECK(result->toSeconds == skip.toSeconds);
    CHECK_FALSE(arbiter.Open());
  }
  SUBCASE("one no is enough, and says why") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    const auto result = arbiter.Vote(
        {.from = 3, .asker = 2, .request = 1, .accepted = false, .why = proto::kSkipCombat});
    REQUIRE(result);
    CHECK(result->outcome == proto::SkipOutcome::Declined);
    CHECK(result->by == 3);
    CHECK(result->why == proto::kSkipCombat);
  }
  SUBCASE("unanswered, it lapses; the asker can take it back") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2}, {}, t0));
    CHECK_FALSE(arbiter.Tick(t0 + activity::kSkipAnswerUs - 1));
    const auto lapsed = arbiter.Tick(t0 + activity::kSkipAnswerUs);
    REQUIRE(lapsed);
    CHECK(lapsed->outcome == proto::SkipOutcome::Lapsed);
    CHECK(lapsed->by == 1);
    REQUIRE_FALSE(arbiter.Ask({.from = 2,
                               .request = 2,
                               .reason = proto::SkipReason::Sleep,
                               .fromSeconds = 0,
                               .toSeconds = 3600},
                              false, {1, 2}, {}, t0));
    const auto withdrawn = arbiter.Vote({.from = 2, .asker = 2, .request = 2, .accepted = false});
    REQUIRE(withdrawn);
    CHECK(withdrawn->outcome == proto::SkipOutcome::Withdrawn);
  }
  SUBCASE("refused at once: one at a time, forward by a day at most, nobody blocking") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    auto other = skip;
    other.from = 3;
    const auto busy = arbiter.Ask(other, false, {1, 2, 3}, {}, t0);
    REQUIRE(busy);
    CHECK(busy->outcome == proto::SkipOutcome::Refused);
    CHECK(busy->why == proto::kSkipBusy);
    CHECK(busy->asker == 3);
    CHECK(arbiter.Open()->from == 2); // the open one is untouched
    arbiter.Reset();
    auto back = skip;
    back.toSeconds = skip.fromSeconds - 60;
    CHECK(arbiter.Ask(back, false, {1, 2}, {}, t0)->outcome == proto::SkipOutcome::Refused);
    auto far = skip;
    far.toSeconds = skip.fromSeconds + activity::kMaxSkipSeconds + 1;
    CHECK(arbiter.Ask(far, false, {1, 2}, {}, t0)->outcome == proto::SkipOutcome::Refused);
    const auto down = arbiter.Ask(skip, false, {1, 2}, {.why = proto::kSkipDown, .by = 1}, t0);
    REQUIRE(down);
    CHECK(down->why == proto::kSkipDown);
    CHECK(down->by == 1);
    CHECK_FALSE(arbiter.Open());
  }
  SUBCASE("two beds at once: a sleeper still to answer joins the open one") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    proto::TimeSkip bed{.from = 3,
                        .request = 7,
                        .reason = proto::SkipReason::Sleep,
                        .fromSeconds = 10 * 3600,
                        .toSeconds = 18 * 3600};
    CHECK(arbiter.Joins(bed));
    auto wheel = bed;
    wheel.reason = proto::SkipReason::Wait;
    CHECK_FALSE(arbiter.Joins(wheel)); // a wheel waits for its own hours
    auto own = bed;
    own.from = 2;
    CHECK_FALSE(arbiter.Joins(own)); // the asker has no vote
    auto stranger = bed;
    stranger.from = 9;
    CHECK_FALSE(arbiter.Joins(stranger)); // never asked (a late joiner)
    // Its own request is refused, the open one is untouched until it votes.
    const auto busy = arbiter.Ask(bed, false, {1, 2, 3}, {}, t0);
    REQUIRE(busy);
    CHECK(busy->why == proto::kSkipBusy);
    CHECK(arbiter.Open()->from == 2);
    CHECK_FALSE(arbiter.Vote({.from = 3, .asker = 2, .request = 1, .accepted = true}));
    CHECK_FALSE(arbiter.Joins(bed)); // answered already
    arbiter.Reset();
    CHECK_FALSE(arbiter.Joins(bed)); // nothing open: a request of its own
  }
  SUBCASE("something gets in the way while it is open") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2}, {}, t0));
    CHECK_FALSE(arbiter.Block({}));
    const auto blocked = arbiter.Block({.why = proto::kSkipTimeAbility, .by = 1});
    REQUIRE(blocked);
    CHECK(blocked->outcome == proto::SkipOutcome::Refused);
    CHECK(blocked->why == proto::kSkipTimeAbility);
  }
  SUBCASE("a player who goes: the asker withdraws it; a voter has no say any more") {
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    CHECK_FALSE(arbiter.Left(3));
    CHECK_FALSE(arbiter.Left(9)); // never asked
    const auto applied = arbiter.Vote({.from = 1, .asker = 2, .request = 1, .accepted = true});
    REQUIRE(applied);
    CHECK(applied->outcome == proto::SkipOutcome::Applied);
    REQUIRE_FALSE(arbiter.Ask(skip, false, {1, 2, 3}, {}, t0));
    const auto gone = arbiter.Left(2);
    REQUIRE(gone);
    CHECK(gone->outcome == proto::SkipOutcome::Withdrawn);
  }
  SUBCASE("alone, a skip needs nobody's word; the host's story only says so") {
    const auto alone = arbiter.Ask(skip, false, {2}, {}, t0);
    REQUIRE(alone);
    CHECK(alone->outcome == proto::SkipOutcome::Applied);
    const proto::TimeSkip quest{.from = 1,
                                .request = 5,
                                .reason = proto::SkipReason::Quest,
                                .fromSeconds = 100,
                                .toSeconds = 7300};
    const auto said = arbiter.Ask(quest, true, {1, 2}, {.why = proto::kSkipDown, .by = 2}, t0);
    REQUIRE(said);
    CHECK(said->outcome == proto::SkipOutcome::Applied);
    const auto guest = arbiter.Ask(quest, false, {1, 2}, {}, t0);
    REQUIRE(guest);
    CHECK(guest->outcome == proto::SkipOutcome::Refused);
    CHECK(guest->why == proto::kSkipNotHost);
  }
}

namespace {
using test::NextPort;

bool Pump(std::vector<Endpoint *> endpoints,
          const std::function<bool(std::size_t, const std::vector<SessionEvent> &)> &done,
          std::chrono::milliseconds limit = 3s) {
  const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
  bool finished = false;
  while (!finished && std::chrono::steady_clock::now() < deadline)
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      finished = done(i, endpoints[i]->Update(1ms)) || finished;
  return finished;
}

// Liveness and the answer time scaled down.
constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms, .skipAnswer = 2000ms};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  std::vector<Endpoint *> All() const {
    std::vector<Endpoint *> out{host.get()};
    if (a)
      out.push_back(a.get());
    if (b)
      out.push_back(b.get());
    return out;
  }
};

Session Three() {
  const auto port = NextPort();
  std::string error;
  Session s;
  s.host = Endpoint::Host({"host"}, port, error, kFast);
  s.a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
  s.b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
  REQUIRE((s.host && s.a && s.b));
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) {
    return s.a->Players().size() == 3 && s.b->Players().size() == 3;
  }));
  return s;
}

// What each endpoint heard of time skips.
struct Skips {
  std::vector<proto::TimeSkip> asked;
  std::vector<proto::TimeSkipVote> votes;
  std::vector<proto::TimeSkipResult> ended;
};

std::function<bool(std::size_t, const std::vector<SessionEvent> &)>
Collect(std::vector<Skips> &heard, const std::function<bool()> &done) {
  heard.resize(3);
  return [&heard, done](std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *asked = std::get_if<events::TimeSkipAsked>(&event))
        heard[i].asked.push_back(asked->skip);
      else if (const auto *vote = std::get_if<events::TimeSkipVoted>(&event))
        heard[i].votes.push_back(vote->vote);
      else if (const auto *ended = std::get_if<events::TimeSkipEnded>(&event))
        heard[i].ended.push_back(ended->result);
    }
    return done();
  };
}
} // namespace

TEST_CASE("a player's activity reaches everyone, stamped, and late joiners get it") {
  auto s = Three();
  s.a->SetLocalActivity({.player = 9, // never in another's name
                         .kind = proto::ActivityKind::Workspot,
                         .flags = proto::kActivityAnchored,
                         .workspot = 0xABC,
                         .entry = 2,
                         .anchor = {1.f, 2.f, 3.f},
                         .yaw = 0.5f});
  proto::Activity seen;
  REQUIRE(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::RemoteActivity>(&event); e && i == 2)
        seen = e->state;
    return seen.kind == proto::ActivityKind::Workspot && s.host->Activities().contains(2);
  }));
  CHECK(seen.player == s.a->You());
  CHECK(seen.workspot == 0xABC);
  CHECK(s.a->Activities().contains(s.a->You())); // ours, as we know it

  // A late joiner hears it from the host.
  std::string error;
  auto late = Endpoint::Join({"late"}, "127.0.0.1", s.host->LocalPort(), error, kFast);
  REQUIRE(late);
  auto all = s.All();
  all.push_back(late.get());
  REQUIRE(Pump(all,
               [&](std::size_t, const auto &) { return late->Activities().contains(s.a->You()); }));
  CHECK(late->Activities().at(s.a->You()).entry == 2);

  // Up again: gone from everyone's record.
  s.a->SetLocalActivity({});
  REQUIRE(Pump(all, [&](std::size_t, const auto &) {
    return !late->Activities().contains(s.a->You()) && !s.b->Activities().contains(s.a->You()) &&
           !s.host->Activities().contains(s.a->You());
  }));
}

TEST_CASE("a guest that vanishes busy is free again for everyone") {
  auto s = Three();
  const auto gone = s.a->You();
  s.a->SetLocalActivity({.kind = proto::ActivityKind::Photo, .flags = proto::kActivityFrozen});
  REQUIRE(Pump(s.All(), [&](std::size_t, const auto &) {
    return s.b->Activities().contains(gone) && s.host->Activities().contains(gone);
  }));
  s.a.reset(); // no goodbye
  bool free = false;
  REQUIRE(Pump(s.All(), [&](std::size_t i, const auto &events) {
    for (const auto &event : events)
      if (const auto *e = std::get_if<events::RemoteActivity>(&event); e && i == 1)
        free |= e->state.player == gone && e->state.kind == proto::ActivityKind::None;
    return free && !s.host->Activities().contains(gone);
  }));
  CHECK(s.b->Activities().empty());
}

TEST_CASE("a time skip is asked of every other player and applied only when all say yes") {
  auto s = Three();
  std::vector<Skips> heard;
  const auto request = s.a->AskTimeSkip(proto::SkipReason::Wait, 3600, 4 * 3600);
  REQUIRE(request != 0);
  REQUIRE(Pump(s.All(),
               Collect(heard, [&] { return !heard[0].asked.empty() && !heard[2].asked.empty(); })));
  CHECK(heard[1].asked.empty()); // never to the asker
  CHECK(heard[0].asked.front().from == s.a->You());
  CHECK(heard[2].asked.front().toSeconds == 4 * 3600);
  REQUIRE(s.host->OpenTimeSkip());

  SUBCASE("both accept") {
    s.host->AnswerTimeSkip(s.a->You(), request, true);
    s.b->AnswerTimeSkip(s.a->You(), request, true);
    REQUIRE(Pump(s.All(), Collect(heard, [&] {
                   return !heard[0].ended.empty() && !heard[1].ended.empty() &&
                          !heard[2].ended.empty();
                 })));
    for (const auto &h : heard) {
      CHECK(h.ended.front().outcome == proto::SkipOutcome::Applied);
      CHECK(h.ended.front().fromSeconds == 3600);
      CHECK(h.ended.front().toSeconds == 4 * 3600);
    }
    CHECK(heard[1].votes.size() == 2); // the asker heard each yes
    CHECK_FALSE(s.host->OpenTimeSkip());
  }
  SUBCASE("one declines: nobody moves, and the asker hears who and why") {
    s.host->AnswerTimeSkip(s.a->You(), request, true);
    s.b->AnswerTimeSkip(s.a->You(), request, false, proto::kSkipCombat);
    REQUIRE(Pump(s.All(), Collect(heard, [&] {
                   return !heard[1].ended.empty() && !heard[0].ended.empty() &&
                          !heard[2].ended.empty();
                 })));
    CHECK(heard[1].ended.front().outcome == proto::SkipOutcome::Declined);
    CHECK(heard[1].ended.front().by == s.b->You());
    CHECK(heard[1].ended.front().why == proto::kSkipCombat);
  }
  SUBCASE("nobody answers: it lapses") {
    REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[1].ended.empty(); }), 5s));
    CHECK(heard[1].ended.front().outcome == proto::SkipOutcome::Lapsed);
  }
  SUBCASE("the asker takes it back") {
    s.a->WithdrawTimeSkip();
    REQUIRE(Pump(s.All(), Collect(heard, [&] {
                   return !heard[0].ended.empty() && !heard[2].ended.empty();
                 })));
    CHECK(heard[2].ended.front().outcome == proto::SkipOutcome::Withdrawn);
  }
  SUBCASE("the asker vanishes: withdrawn for the others") {
    s.a.reset();
    REQUIRE(Pump(s.All(), Collect(heard, [&] {
                   return !heard[0].ended.empty() && !heard[1].ended.empty();
                 })));
    // s.All() has shifted: index 1 is now b.
    CHECK(heard[1].ended.front().outcome == proto::SkipOutcome::Withdrawn);
    CHECK_FALSE(s.host->OpenTimeSkip());
  }
  SUBCASE("a voter vanishes: the ones left decide") {
    s.host->AnswerTimeSkip(s.a->You(), request, true);
    REQUIRE(Pump(s.All(), Collect(heard, [&] { return heard[1].votes.size() == 1; })));
    s.b.reset();
    REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[1].ended.empty(); })));
    CHECK(heard[1].ended.front().outcome == proto::SkipOutcome::Applied);
  }
}

TEST_CASE("two players in two beds: the second sleep is a yes to the first") {
  auto s = Three();
  std::vector<Skips> heard;
  const auto first = s.a->AskTimeSkip(proto::SkipReason::Sleep, 3600, 9 * 3600);
  REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[2].asked.empty(); })));
  // b goes to bed too: its own request is refused to it alone, and the hours
  // pass once the host agrees.
  const auto second = s.b->AskTimeSkip(proto::SkipReason::Sleep, 3600, 8 * 3600);
  REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[2].ended.empty(); })));
  CHECK(heard[2].ended.front().request == second);
  CHECK(heard[2].ended.front().why == proto::kSkipBusy);
  CHECK(heard[2].ended.front().by == s.a->You());
  REQUIRE(Pump(s.All(), Collect(heard, [&] { return heard[1].votes.size() == 1; })));
  CHECK(heard[1].votes.front().from == s.b->You());
  CHECK(heard[1].votes.front().accepted);
  REQUIRE(s.host->OpenTimeSkip());
  s.host->AnswerTimeSkip(s.a->You(), first, true);
  REQUIRE(Pump(s.All(), Collect(heard, [&] {
                 return !heard[0].ended.empty() && !heard[1].ended.empty() &&
                        heard[2].ended.size() == 2;
               })));
  for (const auto &h : heard) {
    CHECK(h.ended.back().request == first);
    CHECK(h.ended.back().outcome == proto::SkipOutcome::Applied);
    CHECK(h.ended.back().toSeconds == 9 * 3600);
  }
  CHECK(heard[0].ended.size() == 1); // the second request never reached the host's game
}

TEST_CASE("a time skip nobody can take is refused at once, and only the asker hears") {
  auto s = Three();
  std::vector<Skips> heard;
  s.b->SetLocalVitals({.life = proto::Life::Down});
  REQUIRE(Pump(s.All(),
               [&](std::size_t, const auto &) { return s.host->Lives().contains(s.b->You()); }));
  s.a->AskTimeSkip(proto::SkipReason::Sleep, 0, 8 * 3600);
  REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[1].ended.empty(); })));
  CHECK(heard[1].ended.front().outcome == proto::SkipOutcome::Refused);
  CHECK(heard[1].ended.front().why == proto::kSkipDown);
  CHECK(heard[1].ended.front().by == s.b->You());
  Pump(s.All(), Collect(heard, [] { return false; }), 200ms);
  CHECK(heard[0].asked.empty());
  CHECK(heard[2].asked.empty());
  CHECK(heard[2].ended.empty());

  // A guest cannot pass its own story's clock off as the host's.
  heard.assign(3, {});
  s.a->AskTimeSkip(proto::SkipReason::Quest, 0, 3600);
  REQUIRE(Pump(s.All(), Collect(heard, [&] { return !heard[1].ended.empty(); })));
  CHECK(heard[1].ended.front().why == proto::kSkipNotHost);
}

TEST_CASE("the host's story moving the clock is said to everyone, never asked") {
  auto s = Three();
  std::vector<Skips> heard;
  s.host->AskTimeSkip(proto::SkipReason::Quest, 3600, 9 * 3600);
  REQUIRE(Pump(s.All(), Collect(heard, [&] {
                 return !heard[0].ended.empty() && !heard[1].ended.empty() &&
                        !heard[2].ended.empty();
               })));
  for (const auto &h : heard) {
    CHECK(h.asked.empty());
    CHECK(h.ended.front().outcome == proto::SkipOutcome::Applied);
    CHECK(h.ended.front().reason == proto::SkipReason::Quest);
  }
}

TEST_CASE("a look sent again unchanged (a mirror left as it was) redresses nobody") {
  auto s = Three();
  const proto::Appearance look{
      .body = proto::Body::Female, .look = {{.resource = 1, .definition = 2}}, .items = {7}};
  s.a->SetLocalAppearance(look);
  int heard = 0;
  const auto count = [&](std::size_t i, const std::vector<SessionEvent> &events) {
    for (const auto &event : events)
      if (std::holds_alternative<events::RemoteAppearance>(event) && i == 2)
        ++heard;
    return false;
  };
  Pump(s.All(), count, 300ms);
  CHECK(heard == 1);
  s.a->SetLocalAppearance(look);
  Pump(s.All(), count, 300ms);
  CHECK(heard == 1);
  auto changed = look;
  changed.look.push_back({.resource = 3, .definition = 4}); // the mirror changed the hair
  s.a->SetLocalAppearance(changed);
  Pump(s.All(), count, 300ms);
  CHECK(heard == 2);
}
