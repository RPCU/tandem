// What scripts/tandem/Trade.reds calls: `TandemTrade` static functions and
// their structs (docs/architecture/trade.md). Declared on the script side in
// scripts/tandem/TradeNatives.reds: keep the two in sync
// (tools/check-natives.py reads every pair). All of these run on the game
// thread: our seat at the table (tandem/trade.hpp) lives here, the net
// thread only passes messages (trade.hpp).

// The SDK's headers need its main header first (own group: keep it first).
#include <RED4ext/RED4ext.hpp>

#include "log.hpp"
#include "runtime.hpp"
#include "trade.hpp"

#include "tandem/session.hpp"
#include "tandem/trade.hpp"

#include <RedLib.hpp>

#include <algorithm>
#include <deque>

namespace tandem::plugin {
// One thing that happened at the table (kinds in TradeNatives.reds).
// Plain fields only (the script VM aligns struct locals to 8).
struct TandemTradeEvent {
  std::int32_t kind{};
  std::uint32_t player{};
  std::int32_t reason{};
};
// One stack on a side of the table.
struct TandemTradeItem {
  Red::TweakDBID record;
  std::uint32_t seed{};
  std::int32_t quantity{};
  std::int32_t stats{};
  std::int32_t parts{};
};
struct TandemTradeStat {
  std::int32_t stat{};
  float value{};
};
struct TandemTradePart {
  Red::TweakDBID slot;
  Red::TweakDBID record;
  std::uint32_t seed{};
};
static_assert(alignof(TandemTradeItem) <= 8, "the script VM aligns struct locals to 8");
static_assert(alignof(TandemTradePart) <= 8, "the script VM aligns struct locals to 8");

namespace {
struct Table {
  trade::Seat seat;
  // What the host said and the scripts have not heard yet.
  std::deque<proto::Message> inbound;
  // Our offer being written by the scripts (OfferBegin … OfferSend).
  std::vector<proto::TradeItem> items;
  std::uint32_t money{}, asks{};
};

Table &TheTable() {
  static Table table;
  return table;
}

// Our seat, in the session we are in now (none: whatever was open is gone).
trade::Seat &Seat() {
  auto &seat = TheTable().seat;
  seat.SetYou(Runtime::Get().You());
  return seat;
}

// 0 our side, 1 theirs, 2 what the last commit has us give, 3 take.
const proto::TradeOffer &Side(std::int32_t side) {
  static const proto::TradeOffer none;
  const auto &seat = TheTable().seat;
  switch (side) {
  case 0:
    return seat.Mine();
  case 1:
    return seat.Theirs();
  case 2:
    return seat.Give();
  case 3:
    return seat.Take();
  default:
    return none;
  }
}

const proto::TradeItem *ItemOf(std::int32_t side, std::int32_t index) {
  const auto &items = Side(side).items;
  return index >= 0 && static_cast<std::size_t>(index) < items.size() ? &items[index] : nullptr;
}

std::int32_t KindOf(trade::Seat::Event::Kind kind) { return static_cast<std::int32_t>(kind) + 1; }
} // namespace

struct TandemTrade : Red::IScriptable {
  // tandem.ini `trade`.
  static bool TradeEnabled() { return Runtime::Get().config.trade; }

  // ---- our moves ----

  // Ask `player` to trade. False: not now (at a table already, not in a session).
  static bool TradeAsk(std::uint32_t player) {
    const auto said = Seat().Ask(player, net::NowUs());
    TradeLink::Get().Send(said);
    return !said.empty();
  }
  // Answer the request we were asked (accept, or decline).
  static bool TradeAnswer(bool accept) {
    const auto said = Seat().Answer(accept, net::NowUs());
    TradeLink::Get().Send(said);
    return !said.empty();
  }
  // Our side of the table, written whole: Begin, an Item (then its Stats
  // and Parts) for each stack, Send.
  static void TradeOfferBegin(std::int32_t money, std::int32_t asks) {
    auto &table = TheTable();
    table.items.clear();
    table.money =
        static_cast<std::uint32_t>(std::clamp<std::int64_t>(money, 0, proto::kMaxTradeMoney));
    table.asks =
        static_cast<std::uint32_t>(std::clamp<std::int64_t>(asks, 0, proto::kMaxTradeMoney));
  }
  static void TradeOfferItem(Red::TweakDBID record, std::uint32_t seed, std::int32_t quantity) {
    if (quantity > 0)
      TheTable().items.push_back({.item = {.record = record.value,
                                           .seed = seed,
                                           .quantity = static_cast<std::uint32_t>(quantity)}});
  }
  static void TradeOfferStat(std::int32_t stat, float value) {
    auto &items = TheTable().items;
    if (!items.empty() && stat >= 0 && stat <= 0xFFFF &&
        items.back().stats.size() < proto::kMaxTradeStats)
      items.back().stats.push_back({.stat = static_cast<std::uint16_t>(stat), .value = value});
  }
  static void TradeOfferPart(Red::TweakDBID slot, Red::TweakDBID record, std::uint32_t seed) {
    auto &items = TheTable().items;
    if (!items.empty() && items.back().parts.size() < proto::kMaxTradeParts)
      items.back().parts.push_back({.slot = slot.value, .record = record.value, .seed = seed});
  }
  // False: not sent (no open trade, or more than the table holds).
  static bool TradeOfferSend() {
    auto &table = TheTable();
    const auto said = Seat().Offer(std::move(table.items), table.money, table.asks);
    table.items.clear();
    TradeLink::Get().Send(said);
    return !said.empty();
  }
  static bool TradeConfirm() {
    const auto said = Seat().Confirm();
    TradeLink::Get().Send(said);
    return !said.empty();
  }
  static bool TradeWithdraw() {
    const auto said = Seat().Withdraw();
    TradeLink::Get().Send(said);
    return !said.empty();
  }
  // reason: proto::TradeReason (2 cancelled, 4 away, 5 combat, 6 down, 12 off).
  static bool TradeClose(std::int32_t reason) {
    const auto why = reason >= 0 && reason <= static_cast<std::int32_t>(proto::TradeReason::Off)
                         ? static_cast<proto::TradeReason>(reason)
                         : proto::TradeReason::Cancelled;
    const auto said = Seat().Close(why);
    TradeLink::Get().Send(said);
    return !said.empty();
  }

  // ---- what happened ----

  // Since the last call, oldest first. After a commit (kind 8) nothing more
  // is returned in the same call: its two sides (2 give, 3 take) are read
  // before the next one can replace them.
  static Red::DynArray<TandemTradeEvent> TradeTakeEvents() {
    Red::DynArray<TandemTradeEvent> out;
    auto &table = TheTable();
    auto &seat = Seat();
    for (auto &message : TradeLink::Get().TakeInbound())
      table.inbound.push_back(std::move(message));
    const auto report = [&](const trade::Seat::Heard &heard) {
      TradeLink::Get().Send(heard.send);
      bool commit = false;
      for (const auto &event : heard.events) {
        out.EmplaceBack(TandemTradeEvent{.kind = KindOf(event.kind),
                                         .player = event.with,
                                         .reason = static_cast<std::int32_t>(event.reason)});
        commit = commit || event.kind == trade::Seat::Event::Kind::Commit;
      }
      return commit;
    };
    while (!table.inbound.empty()) {
      const auto message = std::move(table.inbound.front());
      table.inbound.pop_front();
      if (report(seat.On(message, net::NowUs())))
        return out;
    }
    report(seat.Tick(net::NowUs()));
    return out;
  }

  // 0 not trading, 1 we asked, 2 we are asked, 3 at the table.
  static std::int32_t TradeState() { return static_cast<std::int32_t>(Seat().Is()); }
  static std::uint32_t TradeWith() { return Seat().With(); }
  static bool TradeWeConfirmed() { return Seat().WeConfirmed(); }
  static bool TradeTheyConfirmed() { return Seat().TheyConfirmed(); }

  // A side of the table: 0 ours, 1 theirs, 2 what the last commit has us
  // give, 3 what it has us take.
  static std::int32_t TradeMoney(std::int32_t side) {
    return static_cast<std::int32_t>(Side(side).money);
  }
  static std::int32_t TradeAsks(std::int32_t side) {
    return static_cast<std::int32_t>(Side(side).asks);
  }
  static std::int32_t TradeItemCount(std::int32_t side) {
    return static_cast<std::int32_t>(Side(side).items.size());
  }
  static TandemTradeItem TradeItemAt(std::int32_t side, std::int32_t index) {
    const auto *item = ItemOf(side, index);
    if (!item)
      return {};
    return {.record = Red::TweakDBID(item->item.record),
            .seed = item->item.seed,
            .quantity = static_cast<std::int32_t>(item->item.quantity),
            .stats = static_cast<std::int32_t>(item->stats.size()),
            .parts = static_cast<std::int32_t>(item->parts.size())};
  }
  static TandemTradeStat TradeStatAt(std::int32_t side, std::int32_t index, std::int32_t stat) {
    const auto *item = ItemOf(side, index);
    if (!item || stat < 0 || static_cast<std::size_t>(stat) >= item->stats.size())
      return {};
    return {.stat = item->stats[stat].stat, .value = item->stats[stat].value};
  }
  static TandemTradePart TradePartAt(std::int32_t side, std::int32_t index, std::int32_t part) {
    const auto *item = ItemOf(side, index);
    if (!item || part < 0 || static_cast<std::size_t>(part) >= item->parts.size())
      return {};
    const auto &p = item->parts[part];
    return {.slot = Red::TweakDBID(p.slot), .record = Red::TweakDBID(p.record), .seed = p.seed};
  }

  RTTI_IMPL_TYPEINFO(TandemTrade);
  RTTI_IMPL_ALLOCATOR();
};
} // namespace tandem::plugin

RTTI_DEFINE_CLASS(tandem::plugin::TandemTradeEvent, "TandemTradeEvent", {
  RTTI_PROPERTY(kind);
  RTTI_PROPERTY(player);
  RTTI_PROPERTY(reason);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemTradeItem, "TandemTradeItem", {
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(seed);
  RTTI_PROPERTY(quantity);
  RTTI_PROPERTY(stats);
  RTTI_PROPERTY(parts);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemTradeStat, "TandemTradeStat", {
  RTTI_PROPERTY(stat);
  RTTI_PROPERTY(value);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemTradePart, "TandemTradePart", {
  RTTI_PROPERTY(slot);
  RTTI_PROPERTY(record);
  RTTI_PROPERTY(seed);
});

RTTI_DEFINE_CLASS(tandem::plugin::TandemTrade, "TandemTrade", {
  RTTI_METHOD(TradeEnabled);
  RTTI_METHOD(TradeAsk);
  RTTI_METHOD(TradeAnswer);
  RTTI_METHOD(TradeOfferBegin);
  RTTI_METHOD(TradeOfferItem);
  RTTI_METHOD(TradeOfferStat);
  RTTI_METHOD(TradeOfferPart);
  RTTI_METHOD(TradeOfferSend);
  RTTI_METHOD(TradeConfirm);
  RTTI_METHOD(TradeWithdraw);
  RTTI_METHOD(TradeClose);
  RTTI_METHOD(TradeTakeEvents);
  RTTI_METHOD(TradeState);
  RTTI_METHOD(TradeWith);
  RTTI_METHOD(TradeWeConfirmed);
  RTTI_METHOD(TradeTheyConfirmed);
  RTTI_METHOD(TradeMoney);
  RTTI_METHOD(TradeAsks);
  RTTI_METHOD(TradeItemCount);
  RTTI_METHOD(TradeItemAt);
  RTTI_METHOD(TradeStatAt);
  RTTI_METHOD(TradePartAt);
});
