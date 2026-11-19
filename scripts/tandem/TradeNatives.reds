// Trade's natives (plugin/src/trade_natives.cpp). Keep the two in sync:
// tools/check-natives.py checks them with Natives.reds.

// One thing that happened at the table. kind: 1 `player` asks us to trade;
// 2 the trade we asked for is open; 3 their side of the table changed;
// 4 they agree to the table as it is; 5 they no longer do; 6 our own
// agreement did not count (reason 9: the asked price is not on the table,
// or the table is empty); 7 over, nothing changed hands (reason:
// proto::TradeReason: 1 declined, 2 cancelled, 3 lapsed, 4 away, 5 combat,
// 6 down, 7 gone, 8 busy, 9 invalid, 10 idle, 11 over, 12 off); 8 decided:
// give side 2, take side 3, now.
public native struct TandemTradeEvent {
  native let kind: Int32;
  native let player: Uint32;
  native let reason: Int32;
}

// One stack on a side of the table (stats, parts: how many it carries).
public native struct TandemTradeItem {
  native let record: TweakDBID;
  native let seed: Uint32;
  native let quantity: Int32;
  native let stats: Int32;
  native let parts: Int32;
}

// A stat its game saved on the item (stat: gamedataStatType).
public native struct TandemTradeStat {
  native let stat: Int32;
  native let value: Float;
}

// A part installed in one of its slots.
public native struct TandemTradePart {
  native let slot: TweakDBID;
  native let record: TweakDBID;
  native let seed: Uint32;
}

public native class TandemTrade extends IScriptable {
  // tandem.ini `trade`.
  public static native func TradeEnabled() -> Bool
  // Our moves (false: not now).
  public static native func TradeAsk(player: Uint32) -> Bool
  public static native func TradeAnswer(accept: Bool) -> Bool
  // Our side of the table, written whole: Begin, an Item (then its Stats
  // and Parts) for each stack, Send.
  public static native func TradeOfferBegin(money: Int32, asks: Int32)
  public static native func TradeOfferItem(record: TweakDBID, seed: Uint32, quantity: Int32)
  public static native func TradeOfferStat(stat: Int32, value: Float)
  public static native func TradeOfferPart(slot: TweakDBID, record: TweakDBID, seed: Uint32)
  public static native func TradeOfferSend() -> Bool
  public static native func TradeConfirm() -> Bool
  public static native func TradeWithdraw() -> Bool
  public static native func TradeClose(reason: Int32) -> Bool
  // What happened since the last call. Nothing follows a commit (kind 8)
  // in the same call: its sides are read first.
  public static native func TradeTakeEvents() -> array<TandemTradeEvent>
  // 0 not trading, 1 we asked, 2 we are asked, 3 at the table.
  public static native func TradeState() -> Int32
  public static native func TradeWith() -> Uint32
  public static native func TradeWeConfirmed() -> Bool
  public static native func TradeTheyConfirmed() -> Bool
  // A side of the table: 0 ours, 1 theirs, 2 what the last commit has us
  // give, 3 what it has us take.
  public static native func TradeMoney(side: Int32) -> Int32
  public static native func TradeAsks(side: Int32) -> Int32
  public static native func TradeItemCount(side: Int32) -> Int32
  public static native func TradeItemAt(side: Int32, index: Int32) -> TandemTradeItem
  public static native func TradeStatAt(side: Int32, index: Int32, stat: Int32) -> TandemTradeStat
  public static native func TradePartAt(side: Int32, index: Int32, part: Int32) -> TandemTradePart
}
