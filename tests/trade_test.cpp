// Trade between two players (core/include/tandem/trade.hpp): the host's
// table and each game's seat, driven directly with messages lost, repeated
// and players gone at every step; then over real sessions (loopback UDP).
// What must hold: nothing changes hands before the commit, a commit is
// applied once by each side or by neither, and both games end on the host's
// view.

#include "ports.hpp"
#include "tandem/endpoint.hpp"
#include "tandem/trade.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>

using namespace std::chrono_literals;
using namespace tandem;
using proto::TradeReason;
using proto::TradeVerb;
using trade::Seat;
using Kind = trade::Seat::Event::Kind;

namespace {
proto::TradeItem Item(std::uint64_t record, std::uint32_t quantity = 1, std::uint32_t seed = 0) {
  return {.item = {.record = record, .seed = seed, .quantity = quantity}};
}

// A host's table and the players' seats, wired as the endpoints wire them:
// what a seat sends goes to the table, what the table says goes to the
// seat, a commit is acknowledged and applied once per game (the ledger).
struct Room {
  trade::Table table;
  std::map<proto::PlayerId, Seat> seats;
  std::set<proto::PlayerId> present;
  std::uint64_t now{1'000'000};
  std::map<proto::PlayerId, std::vector<Seat::Event>> events;
  std::map<proto::PlayerId, std::set<proto::TradeId>> ledger;
  std::map<proto::PlayerId, int> applied; // commits applied by each game
  // Faults: lose a delivery (to the host: `to` 0), deliver everything twice.
  std::function<bool(proto::PlayerId from, proto::PlayerId to, const proto::Message &)> lose;
  bool twice{};

  explicit Room(std::initializer_list<proto::PlayerId> players) {
    for (const auto player : players) {
      seats[player].SetYou(player);
      present.insert(player);
    }
  }
  Seat &S(proto::PlayerId player) { return seats[player]; }

  void ToHost(proto::PlayerId from, const std::vector<proto::Message> &messages) {
    for (const auto &message : messages)
      for (int n = 0; n < (twice ? 2 : 1); ++n) {
        if (lose && lose(from, 0, message))
          continue;
        if (!present.contains(from))
          continue;
        if (const auto *t = std::get_if<proto::Trade>(&message))
          ToPlayers(table.On(from, *t, now, present));
        else if (const auto *o = std::get_if<proto::TradeOffer>(&message))
          ToPlayers(table.On(from, *o, now));
      }
  }
  void ToPlayers(const std::vector<trade::Out> &outs) {
    for (const auto &out : outs)
      for (int n = 0; n < (twice ? 2 : 1); ++n) {
        if (lose && lose(0, out.to, out.message))
          continue;
        if (!present.contains(out.to))
          continue;
        if (const auto *commit = std::get_if<proto::TradeCommit>(&out.message)) {
          // As the endpoint does: acknowledged every time, applied once.
          const bool fresh = ledger[out.to].insert(commit->id).second;
          if (fresh) {
            Hear(out.to, out.message);
            ++applied[out.to];
          }
          ToHost(out.to, {proto::Trade{.id = commit->id, .verb = TradeVerb::Applied}});
        } else {
          Hear(out.to, out.message);
        }
      }
  }
  void Hear(proto::PlayerId player, const proto::Message &message) {
    auto heard = S(player).On(message, now);
    for (const auto &event : heard.events)
      events[player].push_back(event);
    ToHost(player, heard.send);
  }
  void Pass(std::uint64_t us) {
    now += us;
    ToPlayers(table.Tick(now));
    for (auto &[player, seat] : seats) {
      auto heard = seat.Tick(now);
      for (const auto &event : heard.events)
        events[player].push_back(event);
      ToHost(player, heard.send);
    }
  }
  void Gone(proto::PlayerId player) {
    present.erase(player);
    ToPlayers(table.Drop(player, TradeReason::Gone));
  }

  // Moves, as a game makes them.
  void Ask(proto::PlayerId from, proto::PlayerId to) { ToHost(from, S(from).Ask(to, now)); }
  void Answer(proto::PlayerId who, bool accept) { ToHost(who, S(who).Answer(accept, now)); }
  void Offer(proto::PlayerId who, std::vector<proto::TradeItem> items, std::uint32_t money = 0,
             std::uint32_t asks = 0) {
    ToHost(who, S(who).Offer(std::move(items), money, asks));
  }
  void Confirm(proto::PlayerId who) { ToHost(who, S(who).Confirm()); }
  void Withdraw(proto::PlayerId who) { ToHost(who, S(who).Withdraw()); }
  void Close(proto::PlayerId who, TradeReason reason = TradeReason::Cancelled) {
    ToHost(who, S(who).Close(reason));
  }
  // 1 asks 2, 2 accepts.
  void Open() {
    Ask(1, 2);
    Answer(2, true);
  }

  bool Saw(proto::PlayerId player, Kind kind) const {
    const auto it = events.find(player);
    return it != events.end() && std::ranges::any_of(it->second, [&](const Seat::Event &event) {
             return event.kind == kind;
           });
  }
  TradeReason ClosedWith(proto::PlayerId player) const {
    const auto it = events.find(player);
    if (it != events.end())
      for (const auto &event : it->second)
        if (event.kind == Kind::Closed)
          return event.reason;
    return TradeReason::None;
  }
};

bool IsVerb(const proto::Message &message, TradeVerb verb) {
  const auto *trade = std::get_if<proto::Trade>(&message);
  return trade && trade->verb == verb;
}
} // namespace

TEST_CASE("two players swap: asked, accepted, offered, confirmed, committed once") {
  Room room{1, 2};
  room.Ask(1, 2);
  CHECK(room.S(1).Is() == Seat::State::Asking);
  REQUIRE(room.Saw(2, Kind::Asked));
  CHECK(room.S(2).With() == 1);
  CHECK(room.table.Busy(1));
  CHECK(room.table.Busy(2));
  room.Answer(2, true);
  CHECK(room.Saw(1, Kind::Opened));
  CHECK(room.S(1).Is() == Seat::State::Open);

  room.Offer(1, {Item(0xA, 1, 77)});
  REQUIRE(room.Saw(2, Kind::Offer));
  CHECK(room.S(2).Theirs().items == std::vector{Item(0xA, 1, 77)});
  room.Offer(2, {Item(0xB, 3)}, 500);
  CHECK(room.S(1).Theirs().money == 500);

  room.Confirm(1);
  CHECK(room.Saw(2, Kind::Confirmed));
  CHECK(room.S(2).TheyConfirmed());
  CHECK(room.applied.empty()); // one agreement is not a trade
  room.Confirm(2);
  REQUIRE(room.Saw(1, Kind::Commit));
  REQUIRE(room.Saw(2, Kind::Commit));
  // Each gives what its own offer said and takes the other's.
  CHECK(room.S(1).Give().items == std::vector{Item(0xA, 1, 77)});
  CHECK(room.S(1).Take().items == std::vector{Item(0xB, 3)});
  CHECK(room.S(1).Take().money == 500);
  CHECK(room.S(2).Give().money == 500);
  CHECK(room.S(2).Take().items == std::vector{Item(0xA, 1, 77)});
  CHECK(room.applied[1] == 1);
  CHECK(room.applied[2] == 1);
  // Both said Applied: the table is clear, both seats free.
  CHECK(room.table.Count() == 0);
  CHECK(room.table.Committed() == 1);
  CHECK(room.S(1).Is() == Seat::State::None);
  CHECK_FALSE(room.table.Busy(1));
  // And they can trade again.
  room.Open();
  CHECK(room.S(1).Is() == Seat::State::Open);
}

TEST_CASE("every message delivered twice: one trade, applied once each") {
  Room room{1, 2};
  room.twice = true;
  room.Open();
  room.Offer(1, {Item(0xA)});
  room.Offer(2, {}, 100);
  room.Confirm(1);
  room.Confirm(2);
  CHECK(room.applied[1] == 1);
  CHECK(room.applied[2] == 1);
  CHECK(room.table.Committed() == 1);
  CHECK(room.table.Count() == 0);
  // The request heard twice was one request, and nobody was told it closed.
  CHECK(std::ranges::count_if(room.events[2], [](const Seat::Event &event) {
          return event.kind == Kind::Asked;
        }) == 1);
  CHECK_FALSE(room.Saw(1, Kind::Closed));
  CHECK_FALSE(room.Saw(2, Kind::Closed));
}

TEST_CASE("a change to the table takes both agreements back") {
  Room room{1, 2};
  room.Open();
  room.Offer(1, {Item(0xA)});
  room.Offer(2, {}, 100);
  room.Confirm(1);
  // 2 lowers its price after 1 agreed: 1's agreement was to the old table.
  room.Offer(2, {}, 10);
  CHECK_FALSE(room.S(1).WeConfirmed());
  room.Confirm(2);
  CHECK(room.applied.empty());
  CHECK(room.table.Committed() == 0);
  // 1 agrees to the new table: now it is a trade, at the new price.
  room.Confirm(1);
  CHECK(room.applied[1] == 1);
  CHECK(room.S(1).Take().money == 10);
}

TEST_CASE("an offer that crosses a confirmation never commits a table one side did not see") {
  Room room{1, 2};
  room.Open();
  room.Offer(1, {Item(0xA), Item(0xB)});
  room.Offer(2, {}, 100);
  room.Confirm(1);
  // 2 confirms what it sees (1's offer at revision 1) while 1's new offer
  // (one item less) reaches the host first.
  auto confirm = room.S(2).Confirm();
  room.Offer(1, {Item(0xA)});
  room.ToHost(2, confirm);
  CHECK(room.applied.empty());
  CHECK(room.table.Committed() == 0);
  // 2 was told its agreement did not count, and has 1's new offer.
  CHECK(room.Saw(2, Kind::Unconfirmed));
  CHECK_FALSE(room.S(2).WeConfirmed());
  CHECK(room.S(2).Theirs().items.size() == 1);
  room.Confirm(2);
  room.Confirm(1);
  CHECK(room.applied[2] == 1);
  CHECK(room.S(2).Take().items == std::vector{Item(0xA)});
}

TEST_CASE("a sale: the asked price must be on the table; an empty table is no trade") {
  Room room{1, 2};
  room.Open();
  SUBCASE("the price") {
    room.Offer(1, {Item(0xA)}, 0, 300); // 1 sells for 300
    CHECK(room.S(2).Theirs().asks == 300);
    room.Offer(2, {}, 200);
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.applied.empty());
    for (const proto::PlayerId player : {1u, 2u}) {
      CHECK(room.Saw(player, Kind::Unconfirmed));
      CHECK_FALSE(room.S(player).WeConfirmed());
      CHECK_FALSE(room.S(player).TheyConfirmed());
    }
    room.Offer(2, {}, 300);
    room.Confirm(2);
    room.Confirm(1);
    CHECK(room.applied[1] == 1);
    CHECK(room.S(1).Take().money == 300);
  }
  SUBCASE("nothing on the table") {
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.applied.empty());
    CHECK(room.table.Committed() == 0);
    // A gift is a trade: one side empty is fine.
    room.Offer(1, {Item(0xA)});
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.applied[2] == 1);
  }
}

TEST_CASE("closing before the commit changes nothing; after it the commit stands") {
  Room room{1, 2};
  room.Open();
  room.Offer(1, {Item(0xA)});
  room.Offer(2, {}, 100);
  SUBCASE("one side agreed, the other walks away") {
    room.Confirm(1);
    room.Close(2, TradeReason::Away);
    CHECK(room.ClosedWith(1) == TradeReason::Away);
    CHECK(room.S(1).Is() == Seat::State::None);
    CHECK(room.applied.empty());
    CHECK(room.table.Count() == 0);
    // What was said for it afterwards is about nothing.
    room.ToHost(1, {proto::Trade{.id = proto::MakeTradeId(1, 1), .verb = TradeVerb::Confirm}});
    CHECK(room.applied.empty());
  }
  SUBCASE("a close that arrives after both agreed is too late") {
    room.Confirm(1);
    // 2's game closes on its own side (combat) just as its confirmation
    // went out: the host had both agreements first.
    auto yes = room.S(2).Confirm();
    auto bye = room.S(2).Close(TradeReason::Combat);
    room.ToHost(2, yes);
    room.ToHost(2, bye);
    // Decided: both apply, 2 as well though its table is closed.
    CHECK(room.applied[1] == 1);
    CHECK(room.applied[2] == 1);
    CHECK(room.Saw(2, Kind::Commit));
    CHECK_FALSE(room.Saw(1, Kind::Closed));
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("an agreement can be taken back") {
    room.Confirm(1);
    room.Withdraw(1);
    CHECK(room.Saw(2, Kind::Withdrawn));
    room.Confirm(2);
    CHECK(room.applied.empty());
    room.Confirm(1);
    CHECK(room.applied[1] == 1);
  }
}

TEST_CASE("requests: declined, lapsed, busy, gone, not the asker's to make") {
  Room room{1, 2, 3};
  SUBCASE("declined") {
    room.Ask(1, 2);
    room.Answer(2, false);
    CHECK(room.ClosedWith(1) == TradeReason::Declined);
    CHECK(room.table.Count() == 0);
    CHECK(room.S(2).Is() == Seat::State::None);
  }
  SUBCASE("not answered in 15 s") {
    room.Ask(1, 2);
    room.Pass(trade::kRequestLapseUs - 1'000'000);
    CHECK(room.S(2).Is() == Seat::State::Asked);
    room.Pass(1'000'000);
    CHECK(room.ClosedWith(1) == TradeReason::Lapsed);
    CHECK(room.ClosedWith(2) == TradeReason::Lapsed);
    // An answer after that is to nothing.
    room.ToHost(2, {proto::Trade{.id = proto::MakeTradeId(1, 1), .verb = TradeVerb::Accept}});
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("someone at a table is busy; the table goes on") {
    room.Open();
    room.Ask(3, 1);
    CHECK(room.ClosedWith(3) == TradeReason::Busy);
    CHECK(room.S(3).Is() == Seat::State::None);
    CHECK(room.S(1).Is() == Seat::State::Open);
    CHECK_FALSE(room.Saw(1, Kind::Closed));
    // And the one asking is busy for others too.
    room.Ask(2, 3);
    CHECK(room.S(2).Is() == Seat::State::Open); // a seat holds one trade
  }
  SUBCASE("to someone who is not there, to oneself, with another's id") {
    room.present.erase(3);
    room.Ask(1, 3);
    CHECK(room.ClosedWith(1) == TradeReason::Gone);
    CHECK(room.S(1).Ask(1, room.now).empty());
    const auto forged = room.table.On(
        2, proto::Trade{.id = proto::MakeTradeId(1, 9), .to = 1, .verb = TradeVerb::Request},
        room.now, room.present);
    REQUIRE(forged.size() == 1);
    CHECK(forged[0].to == 2);
    CHECK(std::get<proto::Trade>(forged[0].message).reason == TradeReason::Invalid);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("an open trade nobody touches is closed in the end") {
    room.Open();
    room.Pass(trade::kIdleUs - 1'000'000);
    CHECK(room.table.Count() == 1);
    room.Pass(2'000'000);
    CHECK(room.ClosedWith(1) == TradeReason::Idle);
    CHECK(room.ClosedWith(2) == TradeReason::Idle);
  }
}

TEST_CASE("an offer the host refuses closes the trade for both") {
  Room room{1, 2};
  room.Open();
  // The seat itself never sends one...
  CHECK(room.S(1).Offer({Item(0xA), Item(0xA)}, 0, 0).empty());
  CHECK(room.S(1).Offer({Item(0xA, 0)}, 0, 0).empty());
  // ...so a game that does is not playing by the rules.
  proto::TradeOffer twice{.id = room.S(1).Id(), .revision = 1, .items = {Item(0xA), Item(0xA)}};
  CHECK_FALSE(trade::Valid(twice));
  room.ToPlayers(room.table.On(1, twice, room.now));
  CHECK(room.ClosedWith(1) == TradeReason::Invalid);
  CHECK(room.ClosedWith(2) == TradeReason::Invalid);
  CHECK(room.table.Count() == 0);

  proto::TradeOffer big{.id = 1, .revision = 1};
  for (std::uint64_t i = 0; i <= proto::kMaxTradeItems; ++i)
    big.items.push_back(Item(100 + i));
  CHECK_FALSE(trade::Valid(big));
  big.items.pop_back();
  CHECK(trade::Valid(big));
  big.money = proto::kMaxTradeMoney + 1;
  CHECK_FALSE(trade::Valid(big));
  // The same record with two seeds is two items.
  CHECK(trade::Valid({.items = {Item(0xA, 1, 1), Item(0xA, 1, 2)}}));
}

TEST_CASE("a message lost at each step: both games end on the host's view") {
  Room room{1, 2};
  // Lose the first delivery that matches, once.
  bool lost = false;
  const auto once =
      [&](std::function<bool(proto::PlayerId, proto::PlayerId, const proto::Message &)> which) {
        room.lose = [&lost, which](proto::PlayerId from, proto::PlayerId to,
                                   const proto::Message &message) {
          if (lost || !which(from, to, message))
            return false;
          lost = true;
          return true;
        };
      };
  SUBCASE("the request, on its way to the host") {
    once([](auto, auto to, const auto &m) { return to == 0 && IsVerb(m, TradeVerb::Request); });
    room.Ask(1, 2);
    CHECK(room.table.Count() == 0);
    room.Pass(trade::kRequestLapseUs + 2'000'000);
    CHECK(room.ClosedWith(1) == TradeReason::Lapsed);
    CHECK(room.S(1).Is() == Seat::State::None);
  }
  SUBCASE("the request, on its way to the one asked") {
    once([](auto, auto to, const auto &m) { return to == 2 && IsVerb(m, TradeVerb::Request); });
    room.Ask(1, 2);
    room.Pass(trade::kRequestLapseUs);
    CHECK(room.ClosedWith(1) == TradeReason::Lapsed);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("the answer") {
    once([](auto, auto to, const auto &m) { return to == 0 && IsVerb(m, TradeVerb::Accept); });
    room.Ask(1, 2);
    room.Answer(2, true);
    CHECK(room.S(2).Is() == Seat::State::Open); // it thinks so
    room.Pass(trade::kRequestLapseUs);
    CHECK(room.ClosedWith(1) == TradeReason::Lapsed);
    CHECK(room.ClosedWith(2) == TradeReason::Lapsed);
    CHECK(room.S(2).Is() == Seat::State::None);
  }
  SUBCASE("an offer, on its way to the host") {
    room.Open();
    once([](auto from, auto to, const auto &m) {
      return to == 0 && from == 1 && std::holds_alternative<proto::TradeOffer>(m);
    });
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    room.Confirm(1);
    // It did not count: the host had no offer of 1's. 1's seat sent it again.
    CHECK(room.Saw(1, Kind::Unconfirmed));
    CHECK(room.S(2).Theirs().items.size() == 1);
    CHECK(room.applied.empty());
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.applied[1] == 1);
    CHECK(room.applied[2] == 1);
    CHECK(room.S(2).Take().items == std::vector{Item(0xA)});
  }
  SUBCASE("an offer, on its way to the other side") {
    room.Open();
    once([](auto, auto to, const auto &m) {
      return to == 2 && std::holds_alternative<proto::TradeOffer>(m);
    });
    room.Offer(1, {Item(0xA)});
    CHECK(room.S(2).Theirs().items.empty());
    room.Offer(2, {}, 100);
    room.Confirm(1);
    // 2 agrees to a table without 1's item: not what the host holds.
    room.Confirm(2);
    CHECK(room.applied.empty());
    CHECK(room.Saw(2, Kind::Unconfirmed));
    CHECK(room.S(2).Theirs().items.size() == 1); // sent again with the notice
    room.Confirm(2);
    CHECK(room.applied[1] == 1);
    CHECK(room.applied[2] == 1);
  }
  SUBCASE("the commit, on its way to one side") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    once([](auto, auto to, const auto &m) {
      return to == 2 && std::holds_alternative<proto::TradeCommit>(m);
    });
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.applied[1] == 1);
    CHECK(room.applied[2] == 0);
    CHECK(room.table.Busy(2));       // it still has a commit to apply
    CHECK_FALSE(room.table.Busy(1)); // 1 is done
    room.Pass(trade::kResendUs);
    CHECK(room.applied[2] == 1);
    CHECK(room.applied[1] == 1); // not sent to 1 again
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("the word that it was applied") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    once([](auto from, auto to, const auto &m) {
      return to == 0 && from == 1 && IsVerb(m, TradeVerb::Applied);
    });
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.table.Count() == 1);
    room.Pass(trade::kResendUs);
    // Sent again, answered again, applied once.
    CHECK(room.applied[1] == 1);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("a close") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    once([](auto, auto to, const auto &m) { return to == 0 && IsVerb(m, TradeVerb::Close); });
    room.Close(1);
    CHECK(room.S(2).Is() == Seat::State::Open); // nobody told it
    // Its next move reaches 1's game, which is no longer there and says so.
    room.Offer(2, {}, 100);
    CHECK(room.ClosedWith(2) == TradeReason::Cancelled);
    CHECK(room.table.Count() == 0);
    CHECK(room.applied.empty());
  }
  SUBCASE("a close, on its way to the other side") {
    room.Open();
    once([](auto, auto to, const auto &m) { return to == 2 && IsVerb(m, TradeVerb::Close); });
    room.Close(1);
    CHECK(room.table.Count() == 0);
    CHECK(room.S(2).Is() == Seat::State::Open);
    // Whatever it says next, the host knows no such trade.
    room.Confirm(2);
    CHECK(room.ClosedWith(2) == TradeReason::Over);
  }
}

TEST_CASE("a player gone at each step: closed for the other, or applied if it was decided") {
  Room room{1, 2};
  SUBCASE("asked") {
    room.Ask(1, 2);
    room.Gone(1);
    CHECK(room.ClosedWith(2) == TradeReason::Gone);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("at the table, one side agreed") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    room.Confirm(1);
    room.Gone(2);
    CHECK(room.ClosedWith(1) == TradeReason::Gone);
    CHECK(room.applied.empty());
    CHECK_FALSE(room.table.Busy(1));
  }
  SUBCASE("decided, and one side never hears it") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    // 2 goes as the host decides: the commit never reaches it.
    room.lose = [](auto, auto to, const auto &m) {
      return to == 2 && std::holds_alternative<proto::TradeCommit>(m);
    };
    room.Confirm(1);
    room.Confirm(2);
    room.Gone(2);
    // 1 applied (the host said so); the trade is over, nobody is told to
    // undo anything; 2's game, wherever it is, never applied it.
    CHECK(room.applied[1] == 1);
    CHECK(room.applied[2] == 0);
    CHECK_FALSE(room.Saw(1, Kind::Closed));
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("decided, the one left has not applied yet") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.Offer(2, {}, 100);
    room.lose = [](auto, auto, const auto &m) {
      return std::holds_alternative<proto::TradeCommit>(m);
    };
    room.Confirm(1);
    room.Confirm(2);
    room.Gone(2);
    CHECK(room.table.Count() == 1);
    room.lose = nullptr;
    room.Pass(trade::kResendUs);
    CHECK(room.applied[1] == 1);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("down, or its world rebuilt: closed for both, the player is still there") {
    room.Open();
    room.ToPlayers(room.table.Drop(2, TradeReason::Down));
    CHECK(room.ClosedWith(1) == TradeReason::Down);
    CHECK(room.ClosedWith(2) == TradeReason::Down);
    CHECK(room.table.Count() == 0);
  }
  SUBCASE("a commit nobody ever answers is given up") {
    room.Open();
    room.Offer(1, {Item(0xA)});
    room.lose = [](auto, auto to, const auto &) { return to != 0; };
    room.Confirm(1);
    room.Confirm(2);
    CHECK(room.table.Count() == 1);
    room.Pass(trade::kCommitGiveUpUs);
    CHECK(room.table.Count() == 0);
  }
}

TEST_CASE("trade messages: encoding, limits, one-shots") {
  using namespace proto;
  const TradeOffer offer{
      .id = MakeTradeId(3, 7),
      .player = 3,
      .revision = 2,
      .money = 1500,
      .asks = 20,
      .items = {{.item = {.record = 0xABCDEF, .seed = 42, .quantity = 1},
                 .stats = {{.stat = 12, .value = 3.f}, {.stat = 900, .value = 1.f}},
                 .parts = {{.slot = 1, .record = 2, .seed = 3}}},
                Item(0x99, 250)}};
  for (const Message message :
       {Message(
            Trade{.id = 9, .from = 1, .to = 2, .verb = TradeVerb::Confirm, .mine = 3, .theirs = 4}),
        Message(offer), Message(TradeCommit{.id = offer.id, .first = offer, .second = {}})}) {
    const auto decoded = Decode(Encode(message));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == message);
    CHECK(IsTrade(message));
    CHECK_FALSE(IsCombat(message));
    CHECK_FALSE(IsStory(message));
    CHECK(ChannelOf(message) == Channel::Event);
    // Each one counts: never sent twice by the fault layer.
    CHECK(consistency::IsOneShot(message));
  }
  CHECK(TradeAsker(MakeTradeId(3, 7)) == 3);
  CHECK(MakeTradeId(3, 7) != MakeTradeId(3, 8));
  CHECK(Message(Trade{}).index() == Message(NpcLevel{}).index() + 1);
  CHECK(Message(VehicleUnlock{}).index() == Message(TradeCommit{}).index() + 1); // then appended
  CHECK(std::string_view(NameOf(TradeCommit{})) == "TradeCommit");
  CHECK(Encode(Trade{}).size() < 32);

  // Out of range: refused at the door.
  CHECK_FALSE(Decode(Encode(Trade{.verb = static_cast<TradeVerb>(6)})).has_value());
  CHECK_FALSE(Decode(Encode(Trade{.reason = static_cast<TradeReason>(13)})).has_value());
  TradeOffer big;
  big.items.resize(kMaxTradeItems + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  big.items.resize(1);
  big.items[0].stats.resize(kMaxTradeStats + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  big.items[0].stats.clear();
  big.items[0].parts.resize(kMaxTradeParts + 1);
  CHECK_FALSE(Decode(Encode(big)).has_value());
  CHECK_FALSE(Decode(Encode(TradeCommit{.first = big})).has_value());
  big.items[0].parts.clear();
  CHECK(Decode(Encode(big)).has_value());
}

// ---- over real sessions ----

namespace {
using test::NextPort;

// A game: an endpoint and its seat, as the plugin and the mock player wire
// them (events::Trade into the seat, what the seat says into SendTrade).
struct Game {
  std::unique_ptr<Endpoint> endpoint;
  Seat seat;
  std::vector<Seat::Event> events;
  int applied{};
  // Agree to whatever is on the table as soon as there is something on it.
  bool eager{};
  // ...and only once our own side is up (a player who means to put
  // something up does not agree before it has).
  bool ownFirst{};

  void Send(const std::vector<proto::Message> &messages) {
    for (const auto &message : messages)
      endpoint->SendTrade(message);
  }
  void Pump() {
    seat.SetYou(endpoint->You());
    for (const auto &event : endpoint->Update(1ms))
      if (const auto *trade = std::get_if<events::Trade>(&event)) {
        auto heard = seat.On(trade->message, net::NowUs());
        Send(heard.send);
        for (const auto &e : heard.events) {
          events.push_back(e);
          applied += e.kind == Kind::Commit;
        }
      }
    // Who we are may have just been said (the Welcome).
    seat.SetYou(endpoint->You());
    auto heard = seat.Tick(net::NowUs());
    Send(heard.send);
    for (const auto &e : heard.events)
      events.push_back(e);
    if (eager && seat.Is() == Seat::State::Open && !seat.WeConfirmed() &&
        (!trade::Empty(seat.Mine()) || !trade::Empty(seat.Theirs())) &&
        (!ownFirst || seat.Mine().revision > 0))
      Send(seat.Confirm());
  }
  bool Saw(Kind kind) const {
    return std::ranges::any_of(events, [&](const Seat::Event &e) { return e.kind == kind; });
  }
  TradeReason ClosedWith() const {
    for (const auto &e : events)
      if (e.kind == Kind::Closed)
        return e.reason;
    return TradeReason::None;
  }
};

bool Until(std::vector<Game *> games, const std::function<bool()> &done,
           std::chrono::milliseconds limit = 4s) {
  const auto deadline = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < deadline) {
    for (auto *game : games)
      if (game->endpoint)
        game->Pump();
    if (done())
      return true;
  }
  return false;
}

constexpr Timing kFast{.timeout = 1000ms, .takeover = 400ms};

struct Session {
  Game host, a, b;
  std::vector<Game *> All() { return {&host, &a, &b}; }
  Session() {
    const auto port = NextPort();
    std::string error;
    host.endpoint = Endpoint::Host({"host"}, port, error, kFast);
    a.endpoint = Endpoint::Join({"a"}, "127.0.0.1", port, error, kFast);
    b.endpoint = Endpoint::Join({"b"}, "127.0.0.1", port, error, kFast);
    REQUIRE((host.endpoint && a.endpoint && b.endpoint));
    REQUIRE(Until(All(), [&] {
      return a.endpoint->Players().size() == 3 && b.endpoint->Players().size() == 3;
    }));
  }
};
} // namespace

TEST_CASE("two guests trade through the host; then a guest sells to the host") {
  Session s;
  s.a.Send(s.a.seat.Ask(s.b.endpoint->You(), net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.b.Saw(Kind::Asked); }));
  // The host's own game hears nothing of a trade that is not its.
  CHECK(s.host.events.empty());
  CHECK(s.host.endpoint->Trades().Busy(s.a.endpoint->You()));
  s.b.Send(s.b.seat.Answer(true, net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.a.Saw(Kind::Opened); }));
  s.a.Send(s.a.seat.Offer({Item(0xA, 1, 5)}, 0, 0));
  s.b.Send(s.b.seat.Offer({Item(0xB, 2)}, 250, 0));
  REQUIRE(Until(
      s.All(), [&] { return s.a.seat.Theirs().revision == 1 && s.b.seat.Theirs().revision == 1; }));
  s.a.Send(s.a.seat.Confirm());
  s.b.Send(s.b.seat.Confirm());
  REQUIRE(Until(s.All(), [&] { return s.a.applied == 1 && s.b.applied == 1; }));
  CHECK(s.a.seat.Take().money == 250);
  CHECK(s.a.seat.Take().items == std::vector{Item(0xB, 2)});
  CHECK(s.b.seat.Take().items == std::vector{Item(0xA, 1, 5)});
  // The offers carry who made them, as the host saw the connections.
  CHECK(s.a.seat.Take().player == s.b.endpoint->You());
  REQUIRE(Until(s.All(), [&] { return s.host.endpoint->Trades().Count() == 0; }));
  CHECK(s.host.applied == 0);

  // a sells to the host's own V: the host's game is a party like any other.
  s.a.Send(s.a.seat.Ask(s.host.endpoint->You(), net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.host.Saw(Kind::Asked); }));
  s.host.Send(s.host.seat.Answer(true, net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.a.seat.Is() == Seat::State::Open; }));
  s.a.Send(s.a.seat.Offer({Item(0xC)}, 0, 400));
  REQUIRE(Until(s.All(), [&] { return s.host.seat.Theirs().asks == 400; }));
  s.host.Send(s.host.seat.Offer({}, 400, 0));
  s.host.eager = s.a.eager = true;
  REQUIRE(Until(s.All(), [&] { return s.a.applied == 2 && s.host.applied == 1; }));
  CHECK(s.host.seat.Take().items == std::vector{Item(0xC)});
  CHECK(s.a.seat.Take().money == 400);
  REQUIRE(Until(s.All(), [&] { return s.host.endpoint->Trades().Count() == 0; }));
  // Nothing was applied twice while the commits were being acknowledged.
  Until(s.All(), [] { return false; }, 1500ms);
  CHECK(s.a.applied == 2);
  CHECK(s.b.applied == 1);
  CHECK(s.host.applied == 1);
}

TEST_CASE("a guest does not speak for the host's table; a commit is the host's to send") {
  Session s;
  CHECK_FALSE(s.a.endpoint->SendTrade(proto::TradeCommit{.id = 1}));
  CHECK_FALSE(s.host.endpoint->SendTrade(proto::TradeCommit{.id = 1}));
  CHECK_FALSE(s.a.endpoint->SendTrade(proto::NpcLevel{}));
  // Whoever a message claims to be from, the connection says who it is.
  s.a.endpoint->SendTrade(proto::Trade{.id = proto::MakeTradeId(s.b.endpoint->You(), 1),
                                       .from = s.b.endpoint->You(),
                                       .to = s.host.endpoint->You(),
                                       .verb = TradeVerb::Request});
  Until(s.All(), [] { return false; }, 300ms);
  CHECK(s.host.events.empty());
  CHECK(s.host.endpoint->Trades().Count() == 0);
}

TEST_CASE("a party that vanishes: closed for the other before the commit, applied after it") {
  Session s;
  s.a.Send(s.a.seat.Ask(s.b.endpoint->You(), net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.b.Saw(Kind::Asked); }));
  SUBCASE("asked, the asker vanishes") {
    s.a.endpoint.reset(); // no goodbye
    REQUIRE(Until(s.All(), [&] { return s.b.Saw(Kind::Closed); }));
    CHECK(s.b.ClosedWith() == TradeReason::Gone);
    CHECK(s.b.seat.Is() == Seat::State::None);
    CHECK(s.host.endpoint->Trades().Count() == 0);
  }
  SUBCASE("at the table, one agreed, the other vanishes") {
    s.b.Send(s.b.seat.Answer(true, net::NowUs()));
    REQUIRE(Until(s.All(), [&] { return s.a.seat.Is() == Seat::State::Open; }));
    s.a.Send(s.a.seat.Offer({Item(0xA)}, 0, 0));
    s.a.Send(s.a.seat.Confirm());
    REQUIRE(Until(s.All(), [&] { return s.b.seat.TheyConfirmed(); }));
    s.b.endpoint.reset();
    REQUIRE(Until(s.All(), [&] { return s.a.Saw(Kind::Closed); }));
    CHECK(s.a.ClosedWith() == TradeReason::Gone);
    CHECK(s.a.applied == 0);
  }
  SUBCASE("decided, then one vanishes: the other keeps its trade, once") {
    s.b.Send(s.b.seat.Answer(true, net::NowUs()));
    REQUIRE(Until(s.All(), [&] { return s.a.seat.Is() == Seat::State::Open; }));
    s.a.Send(s.a.seat.Offer({Item(0xA)}, 0, 0));
    REQUIRE(Until(s.All(), [&] { return s.b.seat.Theirs().revision == 1; }));
    s.a.Send(s.a.seat.Confirm());
    s.b.Send(s.b.seat.Confirm());
    REQUIRE(Until(s.All(), [&] { return s.a.applied == 1; }));
    s.b.endpoint.reset();
    REQUIRE(Until(s.All(), [&] { return s.host.endpoint->Players().size() == 2; }));
    Until(s.All(), [] { return false; }, 1500ms);
    CHECK(s.a.applied == 1);
    CHECK_FALSE(s.a.Saw(Kind::Closed));
    CHECK(s.host.endpoint->Trades().Count() == 0);
  }
  SUBCASE("the host vanishes mid-trade: the guests' session is over, nothing applied") {
    s.b.Send(s.b.seat.Answer(true, net::NowUs()));
    REQUIRE(Until(s.All(), [&] { return s.a.seat.Is() == Seat::State::Open; }));
    s.a.Send(s.a.seat.Offer({Item(0xA)}, 0, 0));
    s.host.endpoint.reset();
    bool ended = false;
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!ended && std::chrono::steady_clock::now() < deadline)
      for (const auto &event : s.a.endpoint->Update(1ms))
        ended = ended || std::holds_alternative<events::Ended>(event);
    CHECK(ended);
    CHECK(s.a.applied == 0);
    // The game clears its seat with the session (SetYou(0)).
    s.a.seat.SetYou(proto::kNoPlayer);
    CHECK(s.a.seat.Is() == Seat::State::None);
  }
}

TEST_CASE("a party that goes down, or whose world is rebuilt, closes the trade for both") {
  Session s;
  s.a.Send(s.a.seat.Ask(s.b.endpoint->You(), net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.b.Saw(Kind::Asked); }));
  s.b.Send(s.b.seat.Answer(true, net::NowUs()));
  REQUIRE(Until(s.All(), [&] { return s.a.seat.Is() == Seat::State::Open; }));
  SUBCASE("down") {
    s.b.endpoint->SetLocalVitals({.life = proto::Life::Down});
    REQUIRE(Until(s.All(), [&] { return s.a.Saw(Kind::Closed) && s.b.Saw(Kind::Closed); }));
    CHECK(s.a.ClosedWith() == TradeReason::Down);
    CHECK(s.b.ClosedWith() == TradeReason::Down);
  }
  SUBCASE("a fast travel") {
    s.a.endpoint->BumpEpoch(proto::kEpochFastTravel);
    REQUIRE(Until(s.All(), [&] { return s.a.Saw(Kind::Closed) && s.b.Saw(Kind::Closed); }));
    CHECK(s.b.ClosedWith() == TradeReason::Away);
  }
  CHECK(s.host.endpoint->Trades().Count() == 0);
  CHECK(s.a.applied == 0);
  CHECK(s.b.applied == 0);
}

TEST_CASE("under loss in both directions a trade is applied by both sides, once, or by neither") {
  for (const std::uint32_t seed : {3u, 29u}) {
    CAPTURE(seed);
    Session s;
    // A quarter of everything never leaves, reliable messages too (as a
    // bug would lose them).
    s.host.endpoint->SetFaults({.drop = 0.25, .dup = 0.25, .seed = seed});
    s.a.endpoint->SetFaults({.drop = 0.25, .seed = seed + 1});
    s.b.endpoint->SetFaults({.drop = 0.25, .seed = seed + 2});
    s.a.eager = s.b.eager = true;
    // Each means to put a side up: agreeing before it has (its own offer
    // lost or not yet sent, the other's arrived) is a valid trade of nothing
    // for 50 eddies, not the one this test checks.
    s.a.ownFirst = s.b.ownFirst = true;
    // Players who keep trying: ask when free, accept when asked, put their
    // side up when at the table.
    const auto play = [&] {
      const auto now = net::NowUs();
      if (s.a.seat.Is() == Seat::State::None && s.a.applied == 0)
        s.a.Send(s.a.seat.Ask(s.b.endpoint->You(), now));
      if (s.b.seat.Is() == Seat::State::Asked)
        s.b.Send(s.b.seat.Answer(true, now));
      if (s.a.seat.Is() == Seat::State::Open && s.a.seat.Mine().revision == 0)
        s.a.Send(s.a.seat.Offer({Item(0xA, 1, seed)}, 0, 50));
      if (s.b.seat.Is() == Seat::State::Open && s.b.seat.Mine().revision == 0)
        s.b.Send(s.b.seat.Offer({}, 50, 0));
      return false;
    };
    Until(s.All(), play, 3s);
    // Never one side without the other for good, never twice.
    CHECK(s.a.applied <= 1);
    CHECK(s.b.applied <= 1);
    // The link is whole again: whatever was decided gets through, and what
    // was not is traded now.
    for (auto *game : s.All())
      game->endpoint->SetFaults({});
    const bool settled = Until(
        s.All(),
        [&] {
          play();
          return s.a.applied == 1 && s.b.applied == 1 && s.host.endpoint->Trades().Count() == 0;
        },
        25s);
    CHECK(settled);
    Until(s.All(), [] { return false; }, 1200ms);
    CHECK(s.a.applied == 1);
    CHECK(s.b.applied == 1);
    CHECK(s.host.endpoint->Trades().Committed() == 1);
    CHECK(s.b.seat.Take().items == std::vector{Item(0xA, 1, seed)});
    CHECK(s.a.seat.Take().money == 50);
  }
}
