// Physics props (docs/architecture/props.md): the pieces (packing, who
// speaks for a moving prop, the record of rests, the drawn track, the
// budget), then real endpoints over loopback UDP: the area owner's moving
// props reach the players linked with it, another game's push reaches the
// owner, one game speaks for a prop at a time, and every record of rests is
// the host's, late joiners' too.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/props.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;

namespace {
using test::NextPort;

constexpr Timing kQuick{
    .timeout = 1000ms, .takeover = 400ms, .digest = 100ms, .resyncCooldown = 200ms};

constexpr std::uint64_t kBin = 0xB1B1'0000'0000'0001ULL, kCone = 0xC0C0'0000'0000'0002ULL;

proto::PropEntry Entry(std::uint64_t entity, float x) {
  return props::Pack({.entity = entity, .position = {x, 0.f, 0.f}, .velocity = {1.f, 0.f, 0.f}});
}

// What one game hears of props.
struct Game {
  Endpoint *endpoint{};
  std::vector<proto::PropStates> moved;
  std::vector<proto::PropRest> rested;
  std::vector<proto::PropPush> pushed;

  void See(const std::vector<SessionEvent> &events) {
    for (const auto &event : events) {
      if (const auto *m = std::get_if<events::PropsMoved>(&event))
        moved.push_back(m->states);
      else if (const auto *r = std::get_if<events::PropRested>(&event))
        rested.push_back(r->rest);
      else if (const auto *p = std::get_if<events::PropPushed>(&event))
        pushed.push_back(p->push);
    }
  }
};

struct Session {
  std::unique_ptr<Endpoint> host, a, b;
  Game h, ga, gb;

  std::vector<Game *> Games() {
    std::vector<Game *> out{&h};
    if (a)
      out.push_back(&ga);
    if (b)
      out.push_back(&gb);
    return out;
  }
  bool Until(const std::function<bool()> &done, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + test::Patience(limit);
    while (std::chrono::steady_clock::now() < deadline) {
      for (auto *game : Games())
        game->See(game->endpoint->Update(1ms));
      if (done())
        return true;
    }
    return false;
  }
  void For(std::chrono::milliseconds time) {
    const auto deadline = std::chrono::steady_clock::now() + time;
    while (std::chrono::steady_clock::now() < deadline)
      for (auto *game : Games())
        game->See(game->endpoint->Update(1ms));
  }
};

void Place(Endpoint &endpoint, float x) { endpoint.SendLocalState({.position = {x, 0.f, 0.f}}); }

std::unique_ptr<Session> Start(bool three = false) {
  const auto port = NextPort();
  std::string error;
  auto s = std::make_unique<Session>();
  s->host = Endpoint::Host({"host"}, port, error, kQuick);
  s->a = Endpoint::Join({"a"}, "127.0.0.1", port, error, kQuick);
  REQUIRE((s->host && s->a));
  if (three) {
    s->b = Endpoint::Join({"b"}, "127.0.0.1", port, error, kQuick);
    REQUIRE(s->b);
  }
  s->h.endpoint = s->host.get();
  s->ga.endpoint = s->a.get();
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] {
    return s->a->Players().size() == (three ? 3u : 2u) && (!three || s->b->Players().size() == 3u);
  }));
  return s;
}
} // namespace

TEST_CASE("a moving prop packs into 33 bytes, its pose within a hundredth of a degree") {
  const props::Prop prop{.entity = kBin,
                         .part = 2,
                         .position = {10.f, -4.f, 1.5f},
                         .orientation = {0.f, 0.f, std::sin(0.4f), std::cos(0.4f)},
                         .velocity = {3.25f, -1.f, 0.5f}};
  const auto entry = props::Pack(prop);
  const auto back = props::Unpack(entry);
  CHECK(back.entity == kBin);
  CHECK(back.part == 2);
  CHECK(back.position == prop.position);
  CHECK(back.orientation.k == doctest::Approx(prop.orientation.k).epsilon(1e-3));
  CHECK(back.velocity.x == doctest::Approx(3.25f).epsilon(1e-3));
  const auto bytes = proto::Encode(proto::PropStates{.owner = 1, .props = {entry}});
  CHECK(bytes.size() < 64);
}

TEST_CASE("one game speaks for a moving prop: the first word, until 2 s after its last") {
  props::Speakers speakers;
  const props::Key bin{kBin, 0};
  // A game that does not run its area never starts one.
  CHECK_FALSE(speakers.Claim(bin, 2, 1'000'000, false));
  CHECK(speakers.Claim(bin, 1, 1'000'000, true));
  CHECK(speakers.Of(bin, 1'500'000) == std::optional<proto::PlayerId>{1});
  // Another area owner's word on the same prop meanwhile: not passed on.
  CHECK_FALSE(speakers.Claim(bin, 3, 1'500'000, true));
  // Its own game finishes it even once its area went to another.
  CHECK(speakers.Claim(bin, 1, 2'000'000, false));
  // Silent 2 s: anyone who runs its area may speak for it.
  CHECK_FALSE(speakers.Of(bin, 4'100'000).has_value());
  CHECK(speakers.Claim(bin, 3, 4'100'000, true));
  speakers.Forget(3);
  CHECK_FALSE(speakers.Of(bin, 4'200'000).has_value());
}

TEST_CASE("the record of rests: the newest rest of each prop, bounded, its digest the same for "
          "the same rests") {
  props::Record host, guest;
  const proto::PropRest bin{.owner = 1, .entity = kBin, .position = {1.f, 2.f, 3.f}};
  const proto::PropRest cone{.owner = 2, .entity = kCone, .position = {4.f, 5.f, 6.f}};
  CHECK(host.Note(bin));
  CHECK(host.Note(cone));
  // The same place again, by anyone: nothing new.
  CHECK_FALSE(host.Note({.owner = 2, .entity = kBin, .position = {1.f, 2.f, 3.f}}));
  // Moved again: the newest.
  auto again = bin;
  again.position.x = 7.f;
  CHECK(host.Note(again));
  CHECK(host.Find({kBin, 0})->position.x == 7.f);
  CHECK(host.InOrder().front().entity == kBin);
  CHECK(host.Size() == 2);
  // A guest that heard the same: the same digest; one rest short: another.
  CHECK(guest.Note(bin));
  CHECK(guest.Note(cone));
  CHECK(guest.Digest() != host.Digest());
  CHECK(guest.Note(again));
  CHECK(guest.Digest() == host.Digest());
  CHECK(host.Around({0.f, 0.f, 0.f}, 8.f, 8).size() == 1);
  // Full: a new prop is not kept; a known one still moves.
  props::Record full;
  for (std::size_t i = 0; i < props::kMaxRecord; ++i)
    full.Note({.entity = 100 + i, .position = {static_cast<float>(i), 0.f, 0.f}});
  CHECK(full.Full());
  CHECK_FALSE(full.Note({.entity = 1, .position = {-5.f, 0.f, 0.f}}));
  CHECK(full.Refused() == 1);
  CHECK(full.Note({.entity = 100, .position = {-5.f, 0.f, 0.f}}));
}

TEST_CASE("another game's prop is drawn between its states, carried on a little, then held") {
  props::Track track;
  CHECK(track.Add(1'000'000, {.entity = kBin, .position = {0.f, 0.f, 0.f}}));
  CHECK(track.Add(1'100'000,
                  {.entity = kBin, .position = {1.f, 0.f, 0.f}, .velocity = {10.f, 0.f, 0.f}}));
  // Older than the newest (the State channel reorders): dropped.
  CHECK_FALSE(track.Add(1'050'000, {.entity = kBin}));
  CHECK(track.At(1'050'000)->position.x == doctest::Approx(0.5f));
  CHECK(track.At(900'000)->position.x == doctest::Approx(0.f));
  // 100 ms past the last: along its velocity; long past: held 250 ms out.
  CHECK(track.At(1'200'000)->position.x == doctest::Approx(2.f));
  CHECK(track.At(9'000'000)->position.x == doctest::Approx(3.5f));
}

TEST_CASE("a round's props: the nearest 16, those left out first in the rounds after") {
  std::vector<props::Prop> moving;
  for (std::uint64_t i = 0; i < 20; ++i)
    moving.push_back({.entity = 1 + i, .position = {static_cast<float>(i), 0.f, 0.f}});
  const std::vector<proto::Vec3> viewers{{0.f, 0.f, 0.f}};
  props::Budget budget;
  const auto first = budget.Pick(moving, viewers);
  REQUIRE(first.size() == proto::kMaxPropsPerState);
  CHECK(first.front().entity == 1);
  const auto second = budget.Pick(moving, viewers);
  // The four left out are 20 m nearer now: in, ahead of most.
  CHECK(std::ranges::any_of(second, [](const auto &e) { return e.entity == 20; }));
  CHECK(props::Near({50.f, 0.f, 0.f}, viewers));
  CHECK_FALSE(props::Near({150.f, 0.f, 0.f}, viewers));
}

TEST_CASE("the area owner's moving props reach the players linked with it; a guest in its area "
          "asks it with a push") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 3);
  REQUIRE(s->Until([&] { return s->a->AreaOwner(s->a->You()) == s->host->You(); }));
  // The host's game runs the area: its bin flies, a shows it.
  CHECK(s->host->SendPropStates({.props = {Entry(kBin, 1.f)}}));
  REQUIRE(s->Until([&] { return !s->ga.moved.empty(); }));
  CHECK(s->ga.moved.front().owner == s->host->You());
  CHECK(s->ga.moved.front().props.front().entity == kBin);
  CHECK(s->a->PropSpeaker(kBin, 0) == std::optional<proto::PlayerId>{s->host->You()});
  // a's game does not run its area: it says nothing of props, it asks.
  CHECK_FALSE(s->a->SendPropStates({.props = {Entry(kCone, 2.f)}}));
  CHECK_FALSE(s->a->SendPropRest({.entity = kCone, .position = {2.f, 0.f, 0.f}}));
  s->a->SendPropPush({.entity = kCone, .position = {2.f, 0.f, 0.f}, .velocity = {0.f, 3.f, 0.f}});
  REQUIRE(s->Until([&] { return !s->h.pushed.empty(); }));
  CHECK(s->h.pushed.front().player == s->a->You());
  CHECK(s->h.pushed.front().velocity.y == 3.f);
  // The bin comes to rest: the session's record, a's too, the same.
  CHECK(s->host->SendPropRest({.entity = kBin, .position = {1.5f, 0.f, 0.f}}));
  REQUIRE(s->Until([&] { return !s->ga.rested.empty(); }));
  CHECK(s->ga.rested.front().owner == s->host->You());
  CHECK(s->a->Props().Digest() == s->host->Props().Digest());
  // Never its own back.
  s->For(100ms);
  CHECK(s->h.moved.empty());
  CHECK(s->h.rested.empty());
  CHECK(s->ga.pushed.empty());
}

TEST_CASE("two games that run their areas: the first to speak for a prop has it, the other "
          "follows") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 120);
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && !s->a->Linked().empty(); }));
  // The cone between them: a's game moves it first.
  CHECK(s->a->SendPropStates({.props = {Entry(kCone, 60.f)}}));
  REQUIRE(s->Until([&] { return !s->h.moved.empty(); }));
  CHECK(s->host->PropSpeaker(kCone, 0) == std::optional<proto::PlayerId>{s->a->You()});
  // The host's own word on it meanwhile goes nowhere; a's still does.
  const auto refused = s->host->PropsRefused();
  s->host->SendPropStates({.props = {Entry(kCone, 61.f)}});
  CHECK(s->host->PropsRefused() == refused + 1);
  CHECK_FALSE(s->host->SendPropRest({.entity = kCone, .position = {61.f, 0.f, 0.f}}));
  CHECK(s->a->SendPropRest({.entity = kCone, .position = {62.f, 0.f, 0.f}}));
  REQUIRE(s->Until([&] { return !s->h.rested.empty() && s->a->Props().Size() == 1; }));
  CHECK(s->host->Props().Find({kCone, 0})->position.x == 62.f);
  s->For(100ms);
  CHECK(s->ga.moved.empty());
  CHECK(s->ga.rested.empty());
}

TEST_CASE("props apart: no motion to who is not linked, every rest to everyone, and the record to "
          "a late joiner") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 600);
  REQUIRE(s->Until([&] { return s->a->OwnsArea() && s->a->Linked().empty(); }));
  CHECK(s->a->SendPropStates({.props = {Entry(kBin, 600.f)}}));
  CHECK(s->a->SendPropRest({.entity = kBin, .position = {601.f, 0.f, 0.f}}));
  REQUIRE(s->Until([&] { return !s->h.rested.empty(); }));
  // The host's game is far: no motion shown, the rest kept for when it comes.
  CHECK(s->h.moved.empty());
  CHECK(s->h.rested.front().entity == kBin);
  s->host->SendPropRest({.entity = kCone, .position = {2.f, 0.f, 0.f}});
  REQUIRE(s->Until([&] { return s->a->Props().Size() == 2; }));

  std::string error;
  s->b = Endpoint::Join({"b"}, "127.0.0.1", s->host->LocalPort(), error, kQuick);
  REQUIRE(s->b);
  s->gb.endpoint = s->b.get();
  REQUIRE(s->Until([&] { return s->gb.rested.size() == 2; }));
  CHECK(s->b->Props().Digest() == s->host->Props().Digest());
  CHECK(s->a->Props().Digest() == s->host->Props().Digest());
  s->For(300ms);
  CHECK(s->gb.rested.size() == 2);
}

TEST_CASE("a push on a prop the pusher's own game runs is nobody's to give") {
  auto s = Start();
  Place(*s->host, 0);
  Place(*s->a, 3);
  REQUIRE(s->Until([&] { return s->a->AreaOwner(s->a->You()) == s->host->You(); }));
  const auto refused = s->host->PropsRefused();
  s->host->SendPropPush({.entity = kBin, .velocity = {1.f, 0.f, 0.f}});
  // The host's own push on a prop its own game runs: nobody else's to give.
  CHECK(s->host->PropsRefused() == refused + 1);
}
