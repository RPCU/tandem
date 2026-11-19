// Comms (docs/architecture/comms.md): what the games decide about pings,
// links, tags and waypoints (tandem/comms.hpp), and the same over loopback
// UDP through a host: who hears what, stamped by the host, held to a rate,
// and nothing left behind by a player who vanishes.

#include "tandem/comms.hpp"
#include "tandem/endpoint.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

using namespace std::chrono_literals;
using namespace tandem;
using proto::MarkKind;
using proto::TargetKind;

namespace {
constexpr std::uint64_t kSecond = 1'000'000;

proto::Mark MarkOf(proto::PlayerId from, std::uint32_t id, MarkKind kind = MarkKind::Spot,
                   proto::TargetRef target = {}, proto::Vec3 at = {}) {
  return {.from = from, .id = id, .kind = kind, .target = target, .position = at};
}

std::size_t Count(const std::vector<comms::Change> &changes, comms::Change::Kind kind) {
  return static_cast<std::size_t>(std::ranges::count_if(
      changes, [&](const comms::Change &change) { return change.kind == kind; }));
}
} // namespace

TEST_CASE("the rate: a burst, then the refill") {
  comms::Limiter limiter(3, 0.5);
  const std::uint64_t t = 10 * kSecond;
  CHECK(limiter.Allow(t));
  CHECK(limiter.Allow(t));
  CHECK(limiter.Allow(t));
  CHECK_FALSE(limiter.Allow(t + 1));
  CHECK_FALSE(limiter.Allow(t + kSecond)); // half a token
  CHECK(limiter.Allow(t + 2 * kSecond));
  CHECK_FALSE(limiter.Allow(t + 2 * kSecond));
  // Never more than the burst, however long it rested.
  CHECK(limiter.Allow(t + 1000 * kSecond));
  CHECK(limiter.Allow(t + 1000 * kSecond));
  CHECK(limiter.Allow(t + 1000 * kSecond));
  CHECK_FALSE(limiter.Allow(t + 1000 * kSecond));
}

TEST_CASE("a mark's life: its kind's own, or what its sender asks within bounds") {
  CHECK(comms::LifeOf(MarkKind::Spot, 0) == 20);
  CHECK(comms::LifeOf(MarkKind::Enemy, 0) == 15);
  CHECK(comms::LifeOf(MarkKind::Danger, 0) == 10);
  CHECK(comms::LifeOf(MarkKind::Loot, 0) == 30);
  CHECK(comms::LifeOf(MarkKind::Spot, 1) == comms::kMinMarkSeconds);
  CHECK(comms::LifeOf(MarkKind::Spot, 200) == comms::kMaxMarkSeconds);
}

TEST_CASE("our pings: press, press again for danger, press later to take it back") {
  comms::Pinger pinger;
  const std::uint64_t t = 50 * kSecond;
  const proto::TargetRef npc{TargetKind::Npc, 42};
  const auto first = pinger.Ping(MarkKind::Enemy, npc, {1, 2, 3}, t);
  REQUIRE(first);
  CHECK(first->id == 1);
  CHECK(first->kind == MarkKind::Enemy);
  CHECK(first->seconds == 15);

  SUBCASE("twice in a row: the same mark, a danger") {
    const auto again = pinger.Ping(MarkKind::Enemy, npc, {1, 2, 4}, t + 500'000);
    REQUIRE(again);
    CHECK(again->id == first->id);
    CHECK(again->kind == MarkKind::Danger);
    // A third press in the window says nothing more.
    CHECK_FALSE(pinger.Ping(MarkKind::Enemy, npc, {1, 2, 4}, t + 900'000));
  }
  SUBCASE("later, on the same thing: withdrawn") {
    const auto gone = pinger.Ping(MarkKind::Enemy, npc, {1, 2, 3}, t + 3 * kSecond);
    REQUIRE(gone);
    CHECK(gone->id == first->id);
    CHECK(gone->kind == MarkKind::None);
    CHECK(pinger.Alive(t + 3 * kSecond).empty());
  }
  SUBCASE("after it expired: a new mark") {
    const auto next = pinger.Ping(MarkKind::Enemy, npc, {1, 2, 3}, t + 16 * kSecond);
    REQUIRE(next);
    CHECK(next->id == 2);
    CHECK(next->kind == MarkKind::Enemy);
  }
  SUBCASE("places: the same within 2 m, another beyond") {
    const auto spot = pinger.Ping(MarkKind::Spot, {}, {100, 0, 0}, t + kSecond);
    REQUIRE(spot);
    const auto near = pinger.Ping(MarkKind::Spot, {}, {101, 0, 0}, t + kSecond + 100'000);
    REQUIRE(near);
    CHECK(near->id == spot->id);
    CHECK(near->kind == MarkKind::Danger);
    const auto far = pinger.Ping(MarkKind::Spot, {}, {110, 0, 0}, t + kSecond + 200'000);
    REQUIRE(far);
    CHECK(far->id != spot->id);
  }
  SUBCASE("mashed: three, then the rate holds; an explicit withdrawal always passes") {
    CHECK(pinger.Ping(MarkKind::Spot, {}, {200, 0, 0}, t));
    CHECK(pinger.Ping(MarkKind::Spot, {}, {300, 0, 0}, t));
    CHECK_FALSE(pinger.Ping(MarkKind::Spot, {}, {400, 0, 0}, t));
    const auto gone = pinger.Withdraw(first->id, t);
    REQUIRE(gone);
    CHECK(gone->kind == MarkKind::None);
    CHECK_FALSE(pinger.Withdraw(first->id, t));
  }
  CHECK_FALSE(pinger.Ping(MarkKind::None, {}, {}, t));
}

TEST_CASE("a receiver's board: shown, replaced, capped, expired, forgotten") {
  comms::Board board;
  const std::uint64_t t = 100 * kSecond;
  auto changes = board.Add(MarkOf(2, 1), t);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].kind == comms::Change::Kind::Show);
  CHECK(changes[0].mark.seconds == 20);

  SUBCASE("the same mark again: replaced (hide, then show)") {
    changes = board.Add(MarkOf(2, 1, MarkKind::Danger), t + kSecond);
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == comms::Change::Kind::Hide);
    CHECK(changes[1].kind == comms::Change::Kind::Show);
    CHECK(changes[1].mark.kind == MarkKind::Danger);
    CHECK(board.Size() == 1);
  }
  SUBCASE("withdrawn: hidden, nothing shown") {
    changes = board.Add(MarkOf(2, 1, MarkKind::None), t + kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == comms::Change::Kind::Hide);
    CHECK(board.Size() == 0);
    // A withdrawal of what is not shown changes nothing.
    CHECK(board.Add(MarkOf(2, 9, MarkKind::None), t).empty());
  }
  SUBCASE("one sender's new mark of the same target replaces its old one") {
    const proto::TargetRef npc{TargetKind::Npc, 5};
    board.Add(MarkOf(2, 2, MarkKind::Enemy, npc), t);
    changes = board.Add(MarkOf(2, 3, MarkKind::Enemy, npc), t + kSecond);
    CHECK(Count(changes, comms::Change::Kind::Hide) == 1);
    // Another sender's of the same target is its own.
    changes = board.Add(MarkOf(3, 1, MarkKind::Enemy, npc), t + kSecond);
    CHECK(Count(changes, comms::Change::Kind::Hide) == 0);
  }
  SUBCASE("three per sender: a fourth hides its oldest, never another's") {
    board.Add(MarkOf(3, 1, MarkKind::Spot, {}, {500, 0, 0}), t);
    board.Add(MarkOf(2, 2, MarkKind::Spot, {}, {10, 0, 0}), t + 1);
    board.Add(MarkOf(2, 3, MarkKind::Spot, {}, {20, 0, 0}), t + 2);
    changes = board.Add(MarkOf(2, 4, MarkKind::Spot, {}, {30, 0, 0}), t + 3);
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == comms::Change::Kind::Hide);
    CHECK(changes[0].mark.from == 2);
    CHECK(changes[0].mark.id == 1);
    CHECK(board.Size() == 4);
  }
  SUBCASE("expiry") {
    CHECK(board.Expire(t + 19 * kSecond).empty());
    changes = board.Expire(t + 20 * kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].kind == comms::Change::Kind::Hide);
  }
  SUBCASE("its sender vanishes: its marks go, the others' stay") {
    board.Add(MarkOf(3, 1), t);
    changes = board.Forget(2);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].mark.from == 2);
    CHECK(board.Size() == 1);
    CHECK(board.Clear().size() == 1);
    CHECK(board.Size() == 0);
  }
}

TEST_CASE("links: consent both ways, auto and ask, broken by either side") {
  using proto::LinkMode;
  const proto::Link everyone{.player = 1, .mode = LinkMode::Everyone};
  CHECK(comms::Consents(&everyone, 2));
  CHECK_FALSE(comms::Consents(nullptr, 2));
  const auto without2 = comms::WithConsent(everyone, 2, false);
  CHECK_FALSE(comms::Consents(&without2, 2));
  CHECK(comms::Consents(&without2, 3));
  const auto back = comms::WithConsent(without2, 2, true);
  CHECK(comms::Consents(&back, 2));
  const proto::Link ask{.player = 1, .mode = LinkMode::Listed};
  CHECK_FALSE(comms::Consents(&ask, 2));
  const auto asked = comms::WithConsent(ask, 2, true);
  CHECK(comms::Consents(&asked, 2));
  // link = off never lets anyone in.
  const proto::Link off{.player = 1, .mode = LinkMode::Off};
  const auto refused = comms::WithConsent(off, 2, true);
  CHECK_FALSE(comms::Consents(&refused, 2));

  std::map<proto::PlayerId, proto::Link> all{{1, everyone},
                                             {2, {.player = 2, .mode = LinkMode::Listed}}};
  CHECK_FALSE(comms::Linked(all, 1, 2));
  all[2] = comms::WithConsent(all[2], 1, true);
  CHECK(comms::Linked(all, 1, 2));
  CHECK(comms::Linked(all, 2, 1));
  CHECK_FALSE(comms::Linked(all, 1, 1));
  CHECK_FALSE(comms::Linked(all, 1, 3)); // 3 said nothing

  comms::Links links;
  links.SetYou(1);
  links.SetOwn({.player = 1, .mode = LinkMode::Listed});
  auto changes = links.Join(2);
  CHECK(changes.empty()); // None to None
  CHECK(links.StateOf(2) == comms::LinkState::None);
  // They ask.
  changes = links.Note({.player = 2, .mode = LinkMode::Listed, .players = {1}});
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].after == comms::LinkState::Offered);
  // We accept.
  changes = links.SetOwn(comms::WithConsent(links.Own(), 2, true));
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].before == comms::LinkState::Offered);
  CHECK(changes[0].after == comms::LinkState::Linked);
  CHECK(links.LinkedWith(2));
  // They break it.
  changes = links.Note({.player = 2, .mode = LinkMode::Listed});
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].after == comms::LinkState::Asking);
  // They vanish: nothing left with them.
  changes = links.Forget(2);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].after == comms::LinkState::None);
  CHECK_FALSE(links.LinkedWith(2));
  // A Note from someone not joined yet counts once they are.
  links.Note({.player = 3, .mode = LinkMode::Everyone});
  changes = links.Join(3);
  REQUIRE(changes.size() == 1);
  CHECK(changes[0].after == comms::LinkState::Offered);
}

TEST_CASE("the tag ledger: one outline per target, held by several, released by the last") {
  using proto::TagReason;
  comms::TagLedger ledger;
  const std::uint64_t t = 10 * kSecond;
  const proto::TargetRef npc{TargetKind::Npc, 7}, door{TargetKind::Entity, 0xD00Dull};
  auto changes =
      ledger.Apply({.from = 2, .reason = TagReason::Scan, .on = true, .targets = {npc, door}}, t);
  REQUIRE(changes.size() == 2);
  CHECK(changes[0].on);
  // Again (a repeat): nothing to draw.
  CHECK(ledger.Apply({.from = 2, .reason = TagReason::Scan, .on = true, .targets = {npc}}, t)
            .empty());
  // Another holder, then the first lets go: still on.
  ledger.Apply({.from = 3, .reason = TagReason::Scan, .on = true, .targets = {npc}}, t);
  CHECK(ledger.Apply({.from = 2, .reason = TagReason::Scan, .on = false, .targets = {npc}}, t)
            .empty());
  changes = ledger.Apply({.from = 3, .reason = TagReason::Scan, .on = false, .targets = {npc}}, t);
  REQUIRE(changes.size() == 1);
  CHECK_FALSE(changes[0].on);
  CHECK(changes[0].target == npc);
  // Off for what was never on: nothing.
  CHECK(ledger.Apply({.from = 4, .reason = TagReason::Scan, .on = false, .targets = {npc}}, t)
            .empty());
  // A player is not outlined.
  CHECK(ledger
            .Apply({.from = 2,
                    .reason = TagReason::Scan,
                    .on = true,
                    .targets = {{TargetKind::Player, 1}}},
                   t)
            .empty());

  SUBCASE("a reveal lasts its seconds, refreshed by a longer one") {
    changes = ledger.Apply(
        {.from = 2, .reason = TagReason::Reveal, .on = true, .seconds = 10, .targets = {npc}}, t);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].seconds == doctest::Approx(10.f));
    ledger.Apply(
        {.from = 2, .reason = TagReason::Reveal, .on = true, .seconds = 10, .targets = {npc}},
        t + 5 * kSecond);
    CHECK(ledger.Expire(t + 11 * kSecond).empty());
    changes = ledger.Expire(t + 15 * kSecond);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].reason == TagReason::Reveal);
    // Capped: never more than a minute however long it asks.
    ledger.Apply({.from = 2, .reason = TagReason::Reveal, .on = true, .targets = {npc}}, t);
    CHECK(ledger.Expire(t + 61 * kSecond).size() == 1);
  }
  SUBCASE("its sender vanishes or unlinks: what only it held goes") {
    ledger.Apply({.from = 3, .reason = TagReason::Scan, .on = true, .targets = {door}}, t);
    CHECK(ledger.Forget(2).empty()); // 3 still holds the door
    changes = ledger.Forget(3);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].target == door);
    CHECK(ledger.Size() == 0);
  }
  SUBCASE("a flood is capped") {
    proto::Tag many{.from = 2, .reason = TagReason::Scan, .on = true};
    for (std::uint64_t i = 0; i < 100; ++i)
      many.targets.push_back({TargetKind::Npc, 1000 + i});
    ledger.Apply(many, t);
    CHECK(ledger.Size() == comms::TagLedger::kMaxHeld);
    CHECK(ledger.Clear().size() == comms::TagLedger::kMaxHeld);
  }
}

TEST_CASE("a waypoint is sent again when it changes kind or moves past a metre") {
  using proto::WaypointKind;
  const proto::Waypoint pin{.kind = WaypointKind::Pin, .position = {0, 0, 0}};
  CHECK_FALSE(comms::Moved(pin, {.kind = WaypointKind::Pin, .position = {0.5f, 0, 0}}));
  CHECK(comms::Moved(pin, {.kind = WaypointKind::Pin, .position = {2, 0, 0}}));
  CHECK(comms::Moved(pin, {.kind = WaypointKind::Point, .position = {0, 0, 0}}));
  CHECK(comms::Moved(pin, {}));
  CHECK_FALSE(comms::Moved({}, {.position = {50, 0, 0}}));
}

namespace {
struct Heard {
  std::vector<proto::Mark> marks;
  std::vector<proto::Tag> tags;
  std::map<proto::PlayerId, proto::Link> links;
  std::map<proto::PlayerId, proto::Waypoint> waypoints;
  std::vector<proto::PlayerId> left;
};

void Run(const std::vector<Endpoint *> &endpoints, std::vector<Heard> &heard,
         const std::function<bool()> &done) {
  heard.resize(endpoints.size());
  const auto cap = std::chrono::steady_clock::now() + 5s;
  std::optional<std::chrono::steady_clock::time_point> settled;
  while (!settled || std::chrono::steady_clock::now() < *settled) {
    if (!settled && (done() || std::chrono::steady_clock::now() >= cap))
      settled = std::chrono::steady_clock::now() + 150ms;
    for (std::size_t i = 0; i < endpoints.size(); ++i)
      for (const auto &event : endpoints[i]->Update(1ms)) {
        if (const auto *mark = std::get_if<events::MarkFrom>(&event))
          heard[i].marks.push_back(mark->mark);
        else if (const auto *tag = std::get_if<events::TagFrom>(&event))
          heard[i].tags.push_back(tag->tag);
        else if (const auto *link = std::get_if<events::RemoteLink>(&event))
          heard[i].links[link->link.player] = link->link;
        else if (const auto *waypoint = std::get_if<events::RemoteWaypoint>(&event))
          heard[i].waypoints[waypoint->waypoint.player] = waypoint->waypoint;
        else if (const auto *left = std::get_if<events::PlayerLeft>(&event))
          heard[i].left.push_back(left->player);
      }
  }
}
} // namespace

TEST_CASE("comms in a session: pings to everyone, tags to the linked only, state for late "
          "joiners, nothing left of a player who vanishes") {
  std::string error;
  const Timing quick{.timeout = 1000ms, .takeover = 400ms};
  auto host = Endpoint::Host({"host"}, 0, error, quick);
  REQUIRE_MESSAGE(host, error);
  const auto port = host->LocalPort();
  auto a = Endpoint::Join({"a"}, "127.0.0.1", port, error, quick);
  REQUIRE_MESSAGE(a, error);
  auto b = Endpoint::Join({"b"}, "127.0.0.1", port, error, quick);
  REQUIRE_MESSAGE(b, error);
  std::vector<Heard> heard;
  std::vector<Endpoint *> all{host.get(), a.get(), b.get()};
  Run(all, heard, [&] { return a->IsJoined() && b->IsJoined() && host->Players().size() == 3; });
  REQUIRE(a->You() == 2);
  REQUIRE(b->You() == 3);

  // The host and a link (auto); b asks nobody.
  host->SetLocalLink({.mode = proto::LinkMode::Everyone});
  a->SetLocalLink({.player = 99, .mode = proto::LinkMode::Everyone});
  b->SetLocalLink({.mode = proto::LinkMode::Listed});
  Run(all, heard, [&] {
    return heard[0].links.contains(2) && heard[0].links.contains(3) && heard[1].links.contains(1) &&
           heard[1].links.contains(3) && heard[2].links.contains(1) && heard[2].links.contains(2);
  });
  CHECK(heard[0].links[2].player == 2); // stamped by the host
  CHECK(comms::Linked(host->Links(), 1, 2));
  CHECK_FALSE(comms::Linked(host->Links(), 1, 3));

  SUBCASE("a ping reaches every other player, stamped; the host holds a guest to a rate") {
    a->SendMark({.from = 7, .id = 1, .kind = MarkKind::Enemy, .target = {TargetKind::Npc, 70}});
    Run(all, heard, [&] { return !heard[0].marks.empty() && !heard[2].marks.empty(); });
    REQUIRE(heard[0].marks.size() == 1);
    REQUIRE(heard[2].marks.size() == 1);
    CHECK(heard[1].marks.empty()); // never back to its sender
    CHECK(heard[0].marks[0].from == 2);
    CHECK(heard[2].marks[0].from == 2);
    CHECK(heard[2].marks[0].target.id == 70);
    heard[0].marks.clear();
    // A flood (a game that does not hold to its own rate): the host lets
    // its burst through, no more.
    for (std::uint32_t i = 0; i < 20; ++i)
      a->SendMark({.id = 10 + i, .kind = MarkKind::Spot});
    Run(all, heard, [&] { return false; });
    CHECK(heard[0].marks.size() <= static_cast<std::size_t>(comms::kHostMarkBurst) + 1);
    CHECK(heard[0].marks.size() >= 3);
    CHECK(host->CommsDropped() > 0);
  }
  SUBCASE("a tag reaches the linked players only, in both directions") {
    const proto::Tag tag{
        .reason = proto::TagReason::Scan, .on = true, .targets = {{TargetKind::Entity, 0xABCDEF}}};
    a->SendTag(tag);
    host->SendTag(tag);
    b->SendTag(tag); // linked with nobody
    Run(all, heard, [&] { return !heard[0].tags.empty() && !heard[1].tags.empty(); });
    REQUIRE(heard[0].tags.size() == 1);
    CHECK(heard[0].tags[0].from == 2);
    REQUIRE(heard[1].tags.size() == 1);
    CHECK(heard[1].tags[0].from == 1);
    CHECK(heard[2].tags.empty());
    // b links with a (a consents to everyone): a hears b's tags now.
    b->SetLocalLink({.mode = proto::LinkMode::Listed, .players = {2}});
    Run(all, heard, [&] { return !heard[1].links[3].players.empty(); });
    b->SendTag(tag);
    Run(all, heard, [&] { return heard[1].tags.size() == 2; });
    REQUIRE(heard[1].tags.size() == 2);
    CHECK(heard[1].tags[1].from == 3);
    CHECK(heard[0].tags.size() == 1); // the host and b are not linked
  }
  SUBCASE("waypoints: relayed, deduplicated, handed to a late joiner, gone with their player") {
    a->SetLocalWaypoint({.kind = proto::WaypointKind::Pin, .position = {1, 2, 3}});
    host->SetLocalWaypoint({.kind = proto::WaypointKind::Point, .position = {4, 5, 6}});
    Run(all, heard,
        [&] { return heard[2].waypoints.contains(2) && heard[2].waypoints.contains(1); });
    CHECK(heard[2].waypoints[2].position == proto::Vec3{1, 2, 3});
    CHECK(heard[0].waypoints[2].kind == proto::WaypointKind::Pin);
    CHECK(heard[1].waypoints[1].kind == proto::WaypointKind::Point);

    auto c = Endpoint::Join({"c"}, "127.0.0.1", port, error, quick);
    REQUIRE_MESSAGE(c, error);
    all.push_back(c.get());
    Run(all, heard, [&] {
      return c->IsJoined() && heard[3].waypoints.contains(1) && heard[3].waypoints.contains(2) &&
             heard[3].links.contains(1) && heard[3].links.contains(2);
    });
    CHECK(heard[3].waypoints[2].position == proto::Vec3{1, 2, 3});
    CHECK(heard[3].links[2].mode == proto::LinkMode::Everyone);
    // Marks are of the moment: none for the late joiner.
    CHECK(heard[3].marks.empty());

    // a vanishes (no goodbye): everyone drops its waypoint and link.
    all.erase(all.begin() + 1);
    std::vector<Heard> after;
    a.reset();
    Run(all, after,
        [&] { return !after[0].left.empty() && !after[1].left.empty() && !after[2].left.empty(); });
    CHECK_FALSE(host->Waypoints().contains(2));
    CHECK_FALSE(host->Links().contains(2));
    CHECK_FALSE(b->Waypoints().contains(2));
    CHECK_FALSE(c->Links().contains(2));
    CHECK(c->Waypoints().contains(1));
  }
}
