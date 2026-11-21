# Trade between players

Two players standing together give, swap or sell items and eddies. Asked
for by the user on 2026-11-19 ("trade and sell items to each other, non
quest or unique item only, by interacting when they are close"); designed
and built the same day with the game machine out of reach: **the core is
tested, nothing has run in game yet**, so it is off unless `tandem.ini`
says `trade = on`. The engine facts are in
[`research/engine/progression.md`](../research/engine/progression.md) §13.38.7
and §13.38.8.

## What a player does

1. **Start.** Within 2 m of another player's puppet, out of combat, the
   game's interaction prompt shows the player's name and "Trade" (the
   prompt revive uses; a downed player's revive prompt comes first). The
   interaction key asks. **Or from the game's phone, at any distance**: the
   player's row in the contact list, then "Trade" ([`comms.md`](comms.md),
   "The phone"; `TandemTradeTable.AskFromPhone`, built 2026-11-21, not run
   in game). The same request, refused with "You can't trade right now"
   when the prompt would not show (combat, a scene, down, a trade open),
   and with "NAME can't be reached right now" when their puppet is not in
   our world (none, or elsewhere: cyberspace, a braindance).
2. **The request.** The other player gets "NAME wants to trade" with the
   phone key's hint, as a regroup offer: a press accepts; 15 s without an
   answer and it lapses. A player already trading is "busy".
3. **The table** is the game's own stash screen, opened on the other
   player's puppet: our inventory on the left, the table on the right. A
   click puts an item on the table (a stack asks how many); a click on one
   of ours on the table takes it back. Theirs appear on the table as they
   put them up and cannot be taken ("That is NAME's until you both agree").
   The title line says what each side gives and asks and who has agreed.
   Three keys, shown as the screen's hints:
   - **eddies you give**: typed into the game's input popup;
   - **eddies you ask for**: a price. A sale is a trade where one side
     puts up items and asks for eddies, and the other gives at least that
     many;
   - **agree** (and take the agreement back).
4. **Both agree** to the same table: the host commits and each game's V
   gives what its side said and gets what the other's said. Any change to
   the table takes both agreements back.
5. **Ending without a trade**: closing the screen, walking 5 m apart (a
   table opened within 5 m of the other puppet; one opened apart, asked
   from the phone, ignores distance), a fight, going down, leaving the
   session, a fast travel. Nothing has moved, so nothing is given back.

## What can be traded

`TandemTradeTable.WhyNot`, in this order (each refusal says why on the HUD):

| Rule                                                                                                             | Why                                                                                            |
| ---------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| never a quest item (tag `Quest`, `UnequipBlocked`)                                                               | the game does not let go of them                                                               |
| never an iconic, a keycard, or anything `TandemLoot.Important`                                                   | every player has those already: whoever gets one, every V gets it. A trade would make a second |
| the game's own "can be sold" tags (`UIInventoryItemsManager.IsSellableStatic`), and not hidden from the backpack | the base rule                                                                                  |
| not equipped, not installed (`EquipmentSystem.IsEquipped`)                                                       | unequip it first                                                                               |
| not broken, not single-instance                                                                                  | the game would refuse or merge it                                                              |
| not a skill book, a readable, a money shard                                                                      | the game uses them up the moment they are given                                                |
| not a grenade, an inhaler, an injector                                                                           | counted in charges, and the stash screen refuses them too                                      |
| crafting components and ammunition: always                                                                       | plain stacks (the stash screen hides ammunition: by command only, for now)                     |
| eddies: their own line                                                                                           | `MarketSystem.Money()`                                                                         |
| vehicles, cyberware in the body, recipes: no                                                                     | not inventory items, or equipped                                                               |

## Integrity

The host arbitrates; inventories stay each player's own. The steps
(`core/include/tandem/trade.hpp`: the host's `Table`, each game's `Seat`):

```
asker                    host (Table)                    asked
  Trade Request  ───────►  Asked  ───────────────────►  "NAME wants to trade"
                           Open   ◄───────────────────  Trade Accept
  TradeOffer rev n ─────►  kept, both agreements off ─►  TradeOffer
  Trade Confirm(mine, theirs) ─► agrees if both revisions are the table's
                           both agree, price met, not empty:
  TradeCommit   ◄───────  Committed  ────────────────►  TradeCommit
  apply once; Applied ──►  done when both applied  ◄──  apply once; Applied
```

- **Two phases.** Offers and confirmations change nothing. The commit is
  decided once, by the host, on two offers both parties confirmed at the
  same revisions; after it nothing can call the trade off.
- **Nothing moves before the commit.** The table is a list in the script
  and copies on the screen (the screen's own transfer is taken over): an
  abort, a crash or a lost connection leave both inventories as they were.
- **Applied once.** Every message names its trade (the asker's player id
  and its own count). The commit is sent again every second until its
  party answers `Applied`; the endpoint acknowledges every copy and hands
  the game the first only.
- **Lost messages** end on the host's view: a confirmation for another
  table than the host holds is answered with the offer it missed and a
  note that it did not count (the seat sends its own offer again if the
  host lacks it); anything about a trade the host no longer has is
  answered "over"; a game told about a trade it has left says so, so the
  other side closes; a request nobody answers lapses on both sides.
- **A party gone.** Before the commit: the other is told (gone), nothing
  changed. After it: the one left applies its side; the host stops telling
  a player that is gone. The window that remains, as for any two games
  with their own saves: a player that vanishes in the instant between the
  host's commit and its own receipt never applies it, while the other
  side did. For a guest that is no loss (it never saved in the session:
  its save still has what it offered); the other side keeps what it got.
- **The whole item.** A unique item travels as its record and seed, the
  stats its game saved on it (quality, tier, upgrade count, level, crafted
  and bought flags) and the parts the player installed, each with its slot
  and seed. The receiving game gives the item, stamps the stats the game's
  own way (again half a second later, after the game's own handling of a
  new item, which rescales and re-tiers), blocks rescaling to its own V,
  and installs the parts. Stacks travel as record and quantity.
- **Never shared as loot.** What `TandemLoot.Important` would send to
  every player is not tradable, so a traded item never takes that path.
- **Saves.** A trade is part of the session: the host's inventory change
  is in its next save; a guest's is kept by the rule for everything its V
  gets in a session (`progression.md`, "Saves").
- **The epoch fence**: a request sent before the asker's game knew the
  host's world was rebuilt is dropped (it lapses); a party whose own world
  is rebuilt (a fast travel) closes its trades for both.
- **Distance is no part of it.** Nothing above, nor the host's `Table`,
  reads where the two stand (core has no range check). What depends on
  distance is each game's own: the prompt's 2 m, the 5 m that ends a table
  opened beside the other, and the screen itself, opened on the other
  player's puppet, which must exist in our world (puppets are spawned
  `alwaysSpawned`, so at any distance in the same world; not in another
  one). Hence a trade from the phone at any distance, no protocol change.

## Protocol

Appended, no version bump: `Trade` (request, accept, confirm, withdraw,
close with its reason, applied), `TradeOffer` (one side, whole, with its
revision), `TradeCommit` (both sides, from the host only). All on the
event channel, all one-shots (never duplicated by the fault layer),
`proto::IsTrade`, through `Endpoint::SendTrade` and `events::Trade`. The
host stamps who speaks from the connection; a guest cannot send a commit or
ask in another player's name.

## Where it is

| What                                                                           | Where                                                                                                                                                                                      |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| The table and the seat (rules, healing)                                        | `core/include/tandem/trade.hpp`, `core/src/trade.cpp`                                                                                                                                      |
| Routing, the applied-once ledger, closing on leave / down / epoch              | `core/src/endpoint.cpp` (`SendTrade`, `RouteTrade`, `HearTrade`)                                                                                                                           |
| Our seat for the scripts                                                       | `plugin/src/trade_natives.cpp` (`TandemTrade`), `plugin/src/trade.cpp`                                                                                                                     |
| Prompt, request, table, tradable rule, applying the commit, the screen's wraps | `scripts/tandem/Trade.reds`                                                                                                                                                                |
| Tests                                                                          | `tests/trade_test.cpp`: every step with a message lost, repeated, or a player gone; over loopback with real endpoints, a vanish at each step, down, a fast travel, and 25 % loss both ways |
| Mock player                                                                    | `--trade-offer`, `--trade-accept`, `--trade-give RECORD[:N],…`, `--trade-money N`, `--trade-asks N`                                                                                        |
| In game without the screen                                                     | `tools/game.sh cmd trade [ask [PLAYER] \| accept \| decline \| give RECORD [N] \| back RECORD [N] \| money N \| asks N \| agree \| cancel]`                                                |
| Scenario                                                                       | `trade-peer` (as a guest, so nothing reaches a save); `phone-trade` (from the phone, 40 m apart, as a guest)                                                                               |

## Not verified, in the order to check

1. The storage screen opening on a puppet (`StorageBlackboard.StorageData`
   with the puppet as `storageObject`), and that our list replaces the
   puppet's own items on it.
2. The three keys reaching the screen in storage mode (`sell_junk`,
   `drop_item`, `disassemble_item`), with usable glyphs. Else: other
   actions, or buttons of our own.
3. The copies of the other side's items (`Inventory.CreateItemData` with
   stamped stats) showing the right tier and tooltip.
4. A traded unique item arriving with its quality, tier and mods.
5. The prompt on a puppet next to the revive prompt and next to a vehicle
   or a body's own prompt.
6. The screen not stopping time for us (the skip of `VendorStash`'s time
   dilation), and what a fight does to an open screen.
7. The storage screen opened on a puppet far away (a trade from the
   phone): that nothing of the game's closes it for the distance.

## Open, and decisions

The defaults below as built were accepted by the user on 2026-11-19; 5 has
no default yet.

1. **Off by default** until the six points above are seen in game; then
   on. (A game with trade off refuses requests: "NAME does not trade".)
2. **Our own items on the table still show in our inventory side** (they
   have not moved): a second click says "on the table already". Hiding
   them means rebuilding the screen's player list; worth it once the
   screen is proven.
3. **One screen for both modes**, or a separate "sell" entry in the
   prompt? Built as one: asking a price makes it a sale.
4. **Iconic items** are never traded (they are every player's by the loot
   rule). If iconics should be giftable, they need to leave the "shared to
   everyone" rule first.
5. **Ammunition** is tradable by rule but the stash screen hides it. A
   line for it like the eddies line, or leave it out?
6. **Distance and combat** are each game's own check (2 m to start from
   the prompt, any distance from the phone; 5 m ends a table opened within
   5 m). The host checks who is in the session, who is down and whose
   world was rebuilt.
7. A **price cap** or a confirmation for large amounts: none; both sides
   see the whole table and both must agree.
