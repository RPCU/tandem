// Trade between two players (docs/architecture/trade.md): give, swap or
// sell items and eddies to the player standing beside us.
//
// Beside another player's puppet, out of combat, the game's interaction
// prompt offers "Trade" (and the player's entry in the phone, at any
// distance: PhoneContacts.reds); the other player gets a request to accept (the
// phone key, as a regroup offer) within 15 s. The table is the game's own
// stash screen (FullscreenVendorGameController in storage mode), opened on
// the other player's puppet: our inventory on one side, the table on the
// other. A click puts an item on the table or takes ours back; eddies and
// an asked price (a sale) are typed into the game's input popup; both
// agree, the host commits, and only then does anything change hands.
//
// Nothing moves while the table is open: the screen's own transfer
// (VendorDataManager.TransferItem) is taken over, our side of the table is
// a list here, and the table's side of the screen shows copies. So closing
// the screen, walking away, a fight, a crash or a lost connection leave
// both inventories as they were, with nothing to give back. The commit
// (TandemTrade event 8) removes what we gave and gives what we took, once.
//
// Which items: the game's own "can be sold" rule, minus what is every
// player's already (quest items, keycards, iconics: Loot.reds Important),
// what is equipped, and what the game uses up the moment it is given.
//
// The steps and their guarantees are core/trade.hpp's (tested there); the
// plugin holds our seat (TandemTrade natives). Off unless tandem.ini says
// `trade = on`, until checked in game.

// One stack of ours on the table.
public class TandemTradeStack {
  public let id: ItemID;
  public let quantity: Int32;
}

public class TandemTradeTable {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_you: Uint32;
  private let m_now: Float;
  // The interaction prompt: the player it offers to trade with (0: none).
  private let m_target: Uint32;
  private let m_savedVisualizers: VisualizersInfo;
  private let m_listening: Bool;
  // A request we are asked to answer: by whom, and the phone key's state.
  private let m_askedBy: Uint32;
  private let m_held: Bool;
  // The table: with whom, our side, their side as the screen shows it.
  private let m_with: Uint32;
  // Opened beside them: walking apart ends it. Opened apart (asked from the
  // phone: PhoneContacts.reds), distance does not count.
  private let m_near: Bool;
  private let m_stacks: array<ref<TandemTradeStack>>;
  private let m_money: Int32;
  private let m_asks: Int32;
  private let m_mineShown: array<ref<gameItemData>>;
  private let m_theirs: array<ref<gameItemData>>;
  // Items of their side our game lacks (a mod): the trade is never agreed.
  private let m_unknownTheirs: Int32;
  // The screen, once it is up; whether it is to be drawn again; when we
  // asked the game for it.
  private let m_screen: wref<FullscreenVendorGameController>;
  private let m_dirty: Bool;
  private let m_opening: Bool;
  private let m_openAt: Float;
  // Items we took whose stats are stamped again once the game's own
  // handling of a new item has run (PlayerPuppet.OnItemAddedToInventory).
  private let m_fixIds: array<ItemID>;
  private let m_fixStats: array<Int32>;
  private let m_fixValues: array<Float>;
  private let m_fixAt: Float;
  private let m_trades: Int32;

  public static func Of(game: GameInstance) -> ref<TandemTradeTable> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Trade() : null;
  }

  public static func HubId() -> Int32 {
    return -7702;
  }

  // How close two players stand to start a trade, and how far apart ends it.
  public static func StartRange() -> Float {
    return 2.0;
  }

  public static func EndRange() -> Float {
    return 5.0;
  }

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
  }

  // ---- every frame in a session (TandemSystem.OnTick) ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_you = you;
    for event in TandemTrade.TradeTakeEvents() {
      this.On(event);
    }
    this.Restamp();
    let player = GetPlayer(this.m_game);
    if you == 0u || !IsDefined(player) {
      // Out of the session: whatever was open went with it.
      this.HidePrompt();
      this.HideAsk();
      if this.m_with != 0u {
        this.CloseTable();
      }
      return;
    }
    if !this.m_listening {
      player.RegisterInputListener(this, n"Choice1");
      player.RegisterInputListener(this, n"PhoneInteract");
      this.m_listening = true;
    }
    let state = TandemTrade.TradeState();
    if state == 3 {
      this.HidePrompt();
      this.Watch(player);
      if this.m_dirty && IsDefined(this.m_screen) {
        this.m_dirty = false;
        this.m_screen.TandemTradeRefresh();
      }
      if this.m_opening && now - this.m_openAt > 5.0 {
        this.m_opening = false;
        TandemNative.Log("trade: the table's screen did not open (the trade goes on: tools/game.sh cmd trade ...)");
      }
    } else if state == 0 {
      this.Prompt(player);
    } else {
      this.HidePrompt();
    }
  }

  // The session is over, or the world goes away.
  public func End() -> Void {
    this.HidePrompt();
    this.HideAsk();
    this.CloseTable();
    if this.m_listening {
      let player = GetPlayer(this.m_game);
      if IsDefined(player) {
        player.UnregisterInputListener(this, n"Choice1");
        player.UnregisterInputListener(this, n"PhoneInteract");
      }
      this.m_listening = false;
    }
  }

  // ---- the prompt, the request ----

  // Our V may trade now: up, not fighting, not in a scene that holds it.
  private func Free(player: ref<PlayerPuppet>) -> Bool {
    if !TandemTrade.TradeEnabled() || player.IsInCombat() || PlayerPuppet.GetSceneTier(player) > 2 {
      return false;
    }
    let revive = this.m_system.Revive();
    return !IsDefined(revive) || (!revive.IsDown() && !revive.Prompting());
  }

  // The nearest player within reach, 0 if none.
  private func Nearest(player: ref<PlayerPuppet>) -> Uint32 {
    let best = 0u;
    let bestAt = TandemTradeTable.StartRange();
    for other in this.m_system.PuppetPlayers() {
      let puppet = this.m_system.PuppetOf(other);
      if IsDefined(puppet) && !TandemElsewhere.IsElsewhere(this.m_game, other) {
        let far = Vector4.Distance(player.GetWorldPosition(), puppet.GetWorldPosition());
        if far <= bestAt {
          best = other;
          bestAt = far;
        }
      }
    }
    return best;
  }

  // The game's interaction prompt on the player beside us: [F] Trade. A
  // downed player's revive prompt comes first (Revive.reds).
  private func Prompt(player: ref<PlayerPuppet>) -> Void {
    let target = this.Free(player) ? this.Nearest(player) : 0u;
    if target == 0u {
      this.HidePrompt();
      return;
    }
    if target == this.m_target {
      return;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    if this.m_target == 0u {
      this.m_savedVisualizers = FromVariant<VisualizersInfo>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo));
    }
    let choice: InteractionChoiceData;
    choice.inputAction = n"Choice1";
    choice.isHoldAction = false;
    choice.localizedName = TandemText.Get("Trade-Prompt");
    let hub: InteractionChoiceHubData;
    hub.id = TandemTradeTable.HubId();
    hub.active = true;
    hub.title = this.NameOf(target);
    ArrayPush(hub.choices, choice);
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(hub), true);
    let visualizers: VisualizersInfo;
    visualizers.activeVisId = TandemTradeTable.HubId();
    ArrayPush(visualizers.visIds, TandemTradeTable.HubId());
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(visualizers), true);
    this.m_target = target;
    TandemNative.Log(s"trade: \(this.NameOf(target)) is beside us: trade prompt shown");
  }

  // Our prompt is up (Vendors.reds gives way to it).
  public func Prompting() -> Bool {
    return this.m_target != 0u;
  }

  private func HidePrompt() -> Void {
    if this.m_target == 0u {
      return;
    }
    this.m_target = 0u;
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    // Only ours: the game may have put its own up since.
    let shown = FromVariant<InteractionChoiceHubData>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub));
    if shown.id == TandemTradeTable.HubId() {
      let none: InteractionChoiceHubData;
      none.id = TandemTradeTable.HubId();
      none.active = false;
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(none), true);
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(this.m_savedVisualizers), true);
    }
  }

  // The interaction key on the prompt asks; the phone key answers a
  // request (a press: released before the phone's own hold).
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Trade.TandemTradeTable.OnAction");
    let name = ListenerAction.GetName(action);
    if Equals(name, n"Choice1") {
      if ListenerAction.IsButtonJustPressed(action) && this.m_target != 0u && TandemTrade.TradeState() == 0 {
        this.Ask(this.m_target);
      }
      return false;
    }
    if !Equals(name, n"PhoneInteract") || this.m_askedBy == 0u {
      return false;
    }
    let kind = ListenerAction.GetType(action);
    if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.m_held = true; // the phone opens: not for us
    } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
      if this.m_held {
        this.m_held = false;
      } else {
        this.Accept();
      }
    }
    return false;
  }

  public func Ask(player: Uint32) -> Bool {
    if !TandemTrade.TradeEnabled() {
      TandemNative.Log("trade: off (tandem.ini trade = on)");
      return false;
    }
    if !TandemTrade.TradeAsk(player) {
      TandemNative.Log(s"trade: cannot ask \(this.NameOf(player)) now");
      return false;
    }
    this.HidePrompt();
    this.Notice("Trade-Asked", 5.0, this.NameOf(player));
    TandemNative.Log(s"trade: asked \(this.NameOf(player)) (player \(player)) to trade");
    return true;
  }

  // From the phone (PhoneContacts.reds): the same request at any distance.
  // The host arbitrates and each inventory stays its player's own, so
  // nothing of the trade depends on where the two stand; the table needs
  // their puppet in our world (it is the storage screen's object), and our
  // V free to trade, as for the prompt.
  public func AskFromPhone(player: Uint32) -> Bool {
    let local = GetPlayer(this.m_game);
    if !TandemTrade.TradeEnabled() {
      TandemNative.Log("trade: off (tandem.ini trade = on)");
      return false;
    }
    if !IsDefined(local) || !this.Free(local) || TandemTrade.TradeState() != 0 {
      this.Notice("Trade-NotNow", 4.0);
      TandemNative.Log(s"trade: cannot ask \(this.NameOf(player)) from the phone now");
      return false;
    }
    if !IsDefined(this.m_system.PuppetOf(player)) || TandemElsewhere.IsElsewhere(this.m_game, player) {
      this.Notice("Phone-Unreachable", 4.0, this.NameOf(player));
      TandemNative.Log(s"trade: \(this.NameOf(player)) is not in our world: not asked");
      return false;
    }
    TandemNative.Log(s"trade: asking \(this.NameOf(player)) from the phone");
    return this.Ask(player);
  }

  public func Accept() -> Bool {
    let by = this.m_askedBy;
    if by == 0u || !TandemTrade.TradeAnswer(true) {
      TandemNative.Log("trade: no request to accept");
      return false;
    }
    this.HideAsk();
    TandemNative.Log(s"trade: accepted \(this.NameOf(by))'s request");
    this.OpenTable(by);
    return true;
  }

  public func Decline() -> Bool {
    let by = this.m_askedBy;
    if by == 0u || !TandemTrade.TradeAnswer(false) {
      return false;
    }
    this.HideAsk();
    TandemNative.Log(s"trade: declined \(this.NameOf(by))'s request");
    return true;
  }

  private func HideAsk() -> Void {
    if this.m_askedBy != 0u {
      this.Hint(false, "");
    }
    this.m_askedBy = 0u;
    this.m_held = false;
  }

  // ---- what the host says ----

  private func On(event: TandemTradeEvent) -> Void {
    let name = this.NameOf(event.player);
    switch event.kind {
      case 1:
        // Asked. A game that does not trade says so.
        if !TandemTrade.TradeEnabled() {
          TandemTrade.TradeClose(12);
          TandemNative.Log(s"trade: \(name) asked: refused, trade is off here");
          break;
        }
        this.m_askedBy = event.player;
        this.m_held = false;
        this.Notice("Trade-Asks", 15.0, name);
        this.Hint(true, TandemText.F("Trade-HintWith", name));
        TandemNative.Log(s"trade: \(name) (player \(event.player)) asks us to trade");
        break;
      case 2:
        TandemNative.Log(s"trade: \(name) accepted");
        this.OpenTable(event.player);
        break;
      case 3:
        this.ShowTheirs();
        this.m_dirty = true;
        TandemNative.Log(s"trade: \(name)'s side: \(TandemTrade.TradeItemCount(1)) stacks, \(TandemTrade.TradeMoney(1)) eddies, asks \(TandemTrade.TradeAsks(1))");
        break;
      case 4:
        this.m_dirty = true;
        TandemNative.Log(s"trade: \(name) agrees to the table");
        break;
      case 5:
        this.m_dirty = true;
        TandemNative.Log(s"trade: \(name) no longer agrees");
        break;
      case 6:
        this.m_dirty = true;
        if event.reason == 9 {
          this.Notice("Trade-PriceMissing", 4.0);
        }
        TandemNative.Log(s"trade: our agreement did not count (reason \(event.reason))");
        break;
      case 7:
        TandemNative.Log(s"trade: closed with \(name) (reason \(event.reason)): nothing changed hands");
        this.HideAsk();
        this.CloseTable();
        this.Notice(TandemTradeTable.WhyClosed(event.reason), 4.0, name);
        break;
      case 8:
        this.Apply(event.player);
        this.CloseTable();
        this.Notice("Trade-Done", 4.0, name);
        break;
      default:
        break;
    }
  }

  // The key of why a trade closed (its {1}: the other player's name).
  public static func WhyClosed(reason: Int32) -> String {
    switch reason {
      case 1:
        return "Trade-Declined";
      case 3:
        return "Trade-NoAnswer";
      case 4:
        return "Trade-TooFar";
      case 5:
        return "Trade-Combat";
      case 6:
        return "Trade-Down";
      case 7:
        return "Trade-Left";
      case 8:
        return "Trade-Busy";
      case 12:
        return "Trade-Refuses";
      default:
        return "Trade-Cancelled";
    }
  }


  // ---- the table ----

  private func OpenTable(with: Uint32) -> Void {
    this.m_with = with;
    ArrayClear(this.m_stacks);
    ArrayClear(this.m_mineShown);
    ArrayClear(this.m_theirs);
    this.m_money = 0;
    this.m_asks = 0;
    this.m_dirty = false;
    let puppet = this.m_system.PuppetOf(with);
    if !IsDefined(puppet) {
      TandemNative.Log(s"trade: no body of \(this.NameOf(with)) here: closed");
      this.Close(4);
      return;
    }
    // The game's storage screen, on their puppet (Stash.OnOpenStash's way:
    // final.reds:132193). What it lists and moves is ours to say (the wraps
    // at the end of this file).
    let storage = new StorageUserData();
    storage.storageObject = puppet;
    this.m_opening = true;
    this.m_openAt = this.m_now;
    let local = GetPlayer(this.m_game);
    this.m_near = IsDefined(local) && Vector4.Distance(local.GetWorldPosition(), puppet.GetWorldPosition()) <= TandemTradeTable.EndRange();
    GameInstance
      .GetBlackboardSystem(this.m_game)
      .Get(GetAllBlackboardDefs().StorageBlackboard)
      .SetVariant(GetAllBlackboardDefs().StorageBlackboard.StorageData, ToVariant(storage), true);
    TandemNative.Log(s"trade: table open with \(this.NameOf(with)) (player \(with)), \(this.m_near ? "beside us" : "apart: walking does not end it")");
  }

  // The table is gone: the screen with it. Nothing was moved, so there is
  // nothing to give back.
  private func CloseTable() -> Void {
    let screen = this.m_screen;
    this.m_screen = null;
    this.m_opening = false;
    this.m_with = 0u;
    ArrayClear(this.m_stacks);
    ArrayClear(this.m_mineShown);
    ArrayClear(this.m_theirs);
    this.m_money = 0;
    this.m_asks = 0;
    if IsDefined(screen) {
      screen.TandemTradeClose();
    }
  }

  // We end the trade (reason: proto::TradeReason).
  public func Close(reason: Int32) -> Void {
    let with = this.m_with;
    if TandemTrade.TradeClose(reason) {
      TandemNative.Log(s"trade: we closed the trade (reason \(reason))");
      if with != 0u {
        this.Notice(TandemTradeTable.WhyClosed(reason), 4.0, this.NameOf(with));
      }
    }
    this.HideAsk();
    this.CloseTable();
  }

  // At the table: over when the two walk apart (a table opened beside
  // them), one fights, or ours is down (the host closes it for a player
  // that is down or gone).
  private func Watch(player: ref<PlayerPuppet>) -> Void {
    let puppet = this.m_system.PuppetOf(this.m_with);
    if this.m_with == 0u {
      // Opened without us knowing with whom (a script reload): closed.
      this.Close(2);
    } else if !IsDefined(puppet) || (this.m_near && Vector4.Distance(player.GetWorldPosition(), puppet.GetWorldPosition()) > TandemTradeTable.EndRange()) {
      this.Close(4);
    } else if player.IsInCombat() {
      this.Close(5);
    } else if IsDefined(this.m_system.Revive()) && this.m_system.Revive().IsDown() {
      this.Close(6);
    }
  }

  // The screen's own questions (the wraps below).

  // `object` is the table of the trade that is open now.
  public func IsTable(object: wref<GameObject>) -> Bool {
    if this.m_with == 0u || !IsDefined(object) || TandemTrade.TradeState() != 3 {
      return false;
    }
    let puppet = this.m_system.PuppetOf(this.m_with);
    return IsDefined(puppet) && puppet.GetEntityID() == object.GetEntityID();
  }

  // The screen we asked for is on its way up (it must not stop time).
  public func IsOpening() -> Bool {
    return this.m_opening && TandemTrade.TradeState() == 3;
  }

  public func Bind(screen: ref<FullscreenVendorGameController>) -> Void {
    this.m_screen = screen;
    this.m_opening = false;
    this.m_dirty = true;
    TandemNative.Log("trade: the table's screen is up");
  }

  // The screen closed: by the player (the trade is cancelled), or by us.
  public func Unbind(screen: ref<FullscreenVendorGameController>) -> Void {
    if !IsDefined(this.m_screen) {
      return;
    }
    this.m_screen = null;
    if TandemTrade.TradeState() == 3 {
      this.Close(2);
    }
  }

  // What the table's side of the screen lists: our stacks, then theirs.
  public func Shown() -> array<ref<gameItemData>> {
    let player = GetPlayer(this.m_game);
    let ts = GameInstance.GetTransactionSystem(this.m_game);
    let out: array<ref<gameItemData>>;
    ArrayClear(this.m_mineShown);
    for stack in this.m_stacks {
      let data: ref<gameItemData> = ts.GetItemData(player, stack.id);
      if IsDefined(data) && data.GetQuantity() != stack.quantity {
        // Part of a stack: a copy with what is on the table.
        let params: ItemModParams;
        params.itemID = stack.id;
        params.quantity = stack.quantity;
        data = Inventory.CreateItemData(params, player);
      }
      if IsDefined(data) {
        ArrayPush(this.m_mineShown, data);
        ArrayPush(out, data);
      }
    }
    for theirs in this.m_theirs {
      ArrayPush(out, theirs);
    }
    return out;
  }

  // A click on the screen: from our inventory onto the table, or from the
  // table back (ours only: theirs is theirs until both agree).
  public func Move(source: wref<GameObject>, data: wref<gameItemData>, amount: Int32) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(data) || !IsDefined(player) || amount <= 0 {
      return;
    }
    if IsDefined(source) && source.GetEntityID() == player.GetEntityID() {
      this.Put(data.GetID(), amount);
      return;
    }
    for theirs in this.m_theirs {
      if theirs == data {
        this.Notice("Trade-Theirs", 3.0, this.NameOf(this.m_with));
        return;
      }
    }
    this.TakeBack(data.GetID(), amount);
  }

  private func FindStack(id: ItemID) -> ref<TandemTradeStack> {
    for stack in this.m_stacks {
      if stack.id == id {
        return stack;
      }
    }
    return null;
  }

  // An item of ours onto the table. False: it is not one to trade, or
  // there is no more of it.
  public func Put(id: ItemID, amount: Int32) -> Bool {
    let player = GetPlayer(this.m_game);
    let ts = GameInstance.GetTransactionSystem(this.m_game);
    let data = ts.GetItemData(player, id);
    let why = TandemTradeTable.WhyNot(player, data);
    if StrLen(why) > 0 {
      this.Notice("Trade-NotForTrade", 3.0, TandemText.Get(why));
      TandemNative.Log(s"trade: \(TDBID.ToStringDEBUG(ItemID.GetTDBID(id))) not put on the table: \(TandemText.English(why))");
      return false;
    }
    let stack = this.FindStack(id);
    let on = IsDefined(stack) ? stack.quantity : 0;
    let put = Min(amount, ts.GetItemQuantity(player, id) - on);
    if put <= 0 {
      this.Notice("Trade-OnTable", 3.0);
      return false;
    }
    if !IsDefined(stack) {
      if ArraySize(this.m_stacks) >= 12 {
        this.Notice("Trade-TableFull", 3.0);
        return false;
      }
      stack = new TandemTradeStack();
      stack.id = id;
      ArrayPush(this.m_stacks, stack);
    }
    stack.quantity = on + put;
    TandemNative.Log(s"trade: \(TDBID.ToStringDEBUG(ItemID.GetTDBID(id))) x\(stack.quantity) on the table");
    this.SendOffer();
    return true;
  }

  public func TakeBack(id: ItemID, amount: Int32) -> Bool {
    let stack = this.FindStack(id);
    if !IsDefined(stack) {
      return false;
    }
    stack.quantity -= amount;
    if stack.quantity <= 0 {
      ArrayRemove(this.m_stacks, stack);
    }
    TandemNative.Log(s"trade: \(TDBID.ToStringDEBUG(ItemID.GetTDBID(id))) x\(amount) taken back");
    this.SendOffer();
    return true;
  }

  // The eddies we give, and the eddies we ask for in return (a sale).
  public func SetMoney(money: Int32) -> Void {
    let player = GetPlayer(this.m_game);
    let have = GameInstance.GetTransactionSystem(this.m_game).GetItemQuantity(player, MarketSystem.Money());
    this.m_money = Max(Min(money, have), 0);
    if money > have {
      this.Notice("Trade-YouHave", 3.0, s"\(have)");
    }
    this.SendOffer();
  }

  public func SetAsks(asks: Int32) -> Void {
    this.m_asks = Max(asks, 0);
    this.SendOffer();
  }

  public func ToggleAgree() -> Void {
    // Their side holds an item our game lacks (a mod): it cannot come
    // here, so we never agree, and nothing changes hands.
    if !TandemTrade.TradeWeConfirmed() && this.m_unknownTheirs > 0 {
      this.Notice("Trade-Unknown", 5.0, TandemCoop.NameOf(TandemTrade.TradeWith()));
      TandemNative.Log(s"trade: their side has \(this.m_unknownTheirs) item(s) our game lacks: not agreed");
      return;
    }
    if TandemTrade.TradeWeConfirmed() {
      TandemTrade.TradeWithdraw();
      TandemNative.Log("trade: we no longer agree");
    } else if TandemTrade.TradeConfirm() {
      TandemNative.Log("trade: we agree to the table");
    }
    this.m_dirty = true;
  }

  // Our side, whole, to the host: each stack with what makes it that item.
  private func SendOffer() -> Void {
    let player = GetPlayer(this.m_game);
    let ts = GameInstance.GetTransactionSystem(this.m_game);
    TandemTrade.TradeOfferBegin(this.m_money, this.m_asks);
    for stack in this.m_stacks {
      let seed = Equals(ItemID.GetStructure(stack.id), gamedataItemStructure.Unique) ? ItemID.GetRngSeed(stack.id) : 0u;
      TandemTrade.TradeOfferItem(ItemID.GetTDBID(stack.id), seed, stack.quantity);
      let data = ts.GetItemData(player, stack.id);
      if seed != 0u && IsDefined(data) {
        for stat in TandemTradeTable.Stats() {
          if data.HasStatData(stat) {
            TandemTrade.TradeOfferStat(EnumInt(stat), data.GetStatValueByType(stat));
          }
        }
        let parts: array<InnerItemData>;
        data.GetItemParts(parts);
        for part in parts {
          let partId = InnerItemData.GetItemID(part);
          let record = InnerItemData.GetStaticData(part);
          // Not the parts the item is made with: those come with its record.
          if ItemID.IsValid(partId) && IsDefined(record) && !record.TagsContains(n"parentPart") && !record.TagsContains(n"DummyPart") {
            TandemTrade.TradeOfferPart(InnerItemData.GetSlotID(part), ItemID.GetTDBID(partId), ItemID.GetRngSeed(partId));
          }
        }
      }
    }
    if !TandemTrade.TradeOfferSend() {
      TandemNative.Log("trade: our side was not sent (no open trade, or too much on it)");
    }
    this.m_dirty = true;
  }

  // What its game saved on an item beyond its record and seed: quality,
  // tier, upgrades, level (docs/research/engine/progression.md).
  public static func Stats() -> array<gamedataStatType> {
    return [gamedataStatType.WasItemUpgraded, gamedataStatType.Quality, gamedataStatType.IsItemPlus, gamedataStatType.PowerLevel, gamedataStatType.ItemLevel, gamedataStatType.LootLevel, gamedataStatType.IsItemCrafted, gamedataStatType.ItemPurchasedAtVendor];
  }

  // Why an item cannot be traded ("" if it can). The game's own "can be
  // sold" tags first (UIInventoryItemsManager.IsSellableStatic), then: never
  // what is every player's already (quest items, keycards, iconics), never
  // what is equipped or installed, never what the game uses up or changes
  // the moment it is given (PlayerPuppet.OnItemAddedToInventory).
  // Why an item cannot be traded: a Text.reds key ("" when it can).
  public static func WhyNot(player: ref<GameObject>, data: wref<gameItemData>) -> String {
    if !IsDefined(data) || !IsDefined(player) {
      return "Trade-Not-Inventory";
    }
    let id = data.GetID();
    if !ItemID.IsValid(id) || ItemID.HasFlag(id, gameEItemIDFlag.Preview) {
      return "Trade-Not-Item";
    }
    if id == MarketSystem.Money() {
      return "Trade-Not-Eddies";
    }
    if data.HasTag(n"Quest") || data.HasTag(n"UnequipBlocked") {
      return "Trade-Not-Quest";
    }
    if TandemLoot.Important(data) || RPGManager.IsItemIconic(data) || data.HasTag(n"IconicWeapon") {
      return "Trade-Not-Iconic";
    }
    let strong: ref<gameItemData> = data;
    let type = data.GetItemType();
    // Crafting components and ammunition are plain stacks: always fine,
    // whatever a vendor or the backpack would say of them.
    let plain = Equals(type, gamedataItemType.Gen_CraftingMaterial) || Equals(type, gamedataItemType.Con_Ammo);
    let hidden: array<CName> = [n"HideInBackpackUI", n"HideInUI", n"TppHead", n"Currency", n"base_fists", n"Recipe"];
    if !plain && data.HasAnyOfTags(hidden) {
      return "Trade-Not-Hidden";
    }
    if !plain && !UIInventoryItemsManager.IsSellableStatic(strong) {
      return "Trade-Not-Unsellable";
    }
    if EquipmentSystem.GetInstance(player).IsEquipped(player, id) {
      return "Trade-Not-Equipped";
    }
    if RPGManager.IsItemBroken(strong) || RPGManager.IsItemSingleInstance(data) {
      return "Trade-Not-Single";
    }
    if Equals(type, gamedataItemType.Con_Skillbook) || Equals(type, gamedataItemType.Gen_Readable) || Equals(type, gamedataItemType.Gen_MoneyShard) || Equals(type, gamedataItemType.Gen_Keycard) {
      return "Trade-Not-UsedUp";
    }
    if Equals(type, gamedataItemType.Gad_Grenade) || Equals(type, gamedataItemType.Con_Inhaler) || Equals(type, gamedataItemType.Con_Injector) {
      return "Trade-Not-Charges";
    }
    return "";
  }

  // ---- their side ----

  public static func IdOf(record: TweakDBID, seed: Uint32) -> ItemID {
    return seed != 0u ? ItemID.CreateFromSeedWithOffset(record, seed, 0) : ItemID.FromTDBID(record);
  }

  // Their side as items the screen can show: made here from what they
  // sent, in no inventory.
  private func ShowTheirs() -> Void {
    let player = GetPlayer(this.m_game);
    ArrayClear(this.m_theirs);
    this.m_unknownTheirs = 0;
    let count = TandemTrade.TradeItemCount(1);
    let i = 0;
    while i < count {
      let item = TandemTrade.TradeItemAt(1, i);
      if !TDBID.IsValid(item.record) {
        // From a mod our game lacks: not shown, and the trade not agreed.
        this.m_unknownTheirs += 1;
      } else {
        this.ShowTheir(player, i, item);
      }
      i += 1;
    }
  }

  private func ShowTheir(player: ref<PlayerPuppet>, i: Int32, item: TandemTradeItem) -> Void {
    let params: ItemModParams;
    params.itemID = TandemTradeTable.IdOf(item.record, item.seed);
    params.quantity = item.quantity;
    let p = 0;
    while p < item.parts {
      let part = TandemTrade.TradePartAt(1, i, p);
      ArrayPush(params.customPartsToInstall, TandemTradeTable.IdOf(part.record, part.seed));
      p += 1;
    }
    let data = Inventory.CreateItemData(params, player);
    if IsDefined(data) {
      this.Stamp(data, 1, i, item);
      ArrayPush(this.m_theirs, data);
    }
  }

  // The stats its game saved on an item, on ours (the game's own way to
  // set them: final.reds:35861, :41636, :71000).
  private func Stamp(data: wref<gameItemData>, side: Int32, index: Int32, item: TandemTradeItem) -> Void {
    if !IsDefined(data) || item.seed == 0u {
      return;
    }
    let s = 0;
    while s < item.stats {
      let stat = TandemTrade.TradeStatAt(side, index, s);
      this.SetStat(data, stat.stat, stat.value);
      s += 1;
    }
    // As its owner had it: our game does not scale it to our V's level.
    this.SetStat(data, EnumInt(gamedataStatType.ScalingBlocked), 1.0);
  }

  private func SetStat(data: wref<gameItemData>, stat: Int32, value: Float) -> Void {
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let type = IntEnum<gamedataStatType>(stat);
    stats.RemoveAllModifiers(data.GetStatsObjectID(), type, true);
    stats.AddSavedModifier(data.GetStatsObjectID(), RPGManager.CreateStatModifier(type, gameStatModifierType.Additive, value));
  }

  // ---- the commit ----

  // Decided by the host, once: what our side of the commit says leaves our
  // inventory, what theirs says comes in.
  private func Apply(with: Uint32) -> Void {
    let player = GetPlayer(this.m_game);
    let ts = GameInstance.GetTransactionSystem(this.m_game);
    if !IsDefined(player) {
      TandemNative.Log("trade: a commit with no V to apply it to");
      return;
    }
    let short = 0;
    let given = TandemTrade.TradeItemCount(2);
    let i = 0;
    while i < given {
      let item = TandemTrade.TradeItemAt(2, i);
      let id = TandemTradeTable.IdOf(item.record, item.seed);
      let have = ts.GetItemQuantity(player, id);
      if have < item.quantity {
        short += 1;
        TandemNative.Log(s"trade: \(TDBID.ToStringDEBUG(item.record)): we have \(have) of the \(item.quantity) we gave");
      }
      if have > 0 {
        ts.RemoveItem(player, id, Min(have, item.quantity));
      }
      i += 1;
    }
    let pay = TandemTrade.TradeMoney(2);
    if pay > 0 {
      ts.RemoveItem(player, MarketSystem.Money(), Min(pay, ts.GetItemQuantity(player, MarketSystem.Money())));
    }
    let taken = TandemTrade.TradeItemCount(3);
    i = 0;
    while i < taken {
      // (Never one our game lacks: ToggleAgree refuses such a table, and a
      // commit is only of offers both confirmed.)
      let item = TandemTrade.TradeItemAt(3, i);
      let id = TandemTradeTable.IdOf(item.record, item.seed);
      if item.seed != 0u && ts.HasItem(player, id) {
        // We own that very item already (co-op gave every player one): this
        // one gets another seed rather than vanish into it.
        id = ItemID.CreateFromSeedWithOffset(item.record, item.seed, 1);
        TandemNative.Log(s"trade: \(TDBID.ToStringDEBUG(item.record)) seed \(item.seed) is ours already: taken under another seed");
      }
      let ok = ts.GiveItem(player, id, item.quantity);
      if item.seed != 0u {
        let data = ts.GetItemData(player, id);
        this.Stamp(data, 3, i, item);
        // Again once the game has handled the new item (it scales and
        // tiers what comes into V's inventory).
        let s = 0;
        while s < item.stats {
          let stat = TandemTrade.TradeStatAt(3, i, s);
          ArrayPush(this.m_fixIds, id);
          ArrayPush(this.m_fixStats, stat.stat);
          ArrayPush(this.m_fixValues, stat.value);
          s += 1;
        }
        ArrayPush(this.m_fixIds, id);
        ArrayPush(this.m_fixStats, EnumInt(gamedataStatType.ScalingBlocked));
        ArrayPush(this.m_fixValues, 1.0);
        this.m_fixAt = this.m_now + 0.5;
        let p = 0;
        while p < item.parts {
          let part = TandemTrade.TradePartAt(3, i, p);
          let partId = TandemTradeTable.IdOf(part.record, part.seed);
          // A part is an item first, then goes into its slot (as
          // ItemModificationSystem installs one: final.reds:67214).
          ts.GiveItem(player, partId, 1);
          if IsDefined(data) && data.HasPartInSlot(part.slot) {
            let old = ts.RemovePart(player, id, part.slot);
            if ItemID.IsValid(old) {
              ts.RemoveItem(player, old, 1);
            }
          }
          if !ts.AddPart(player, id, partId, part.slot) {
            ts.ForcePartInSlot(player, id, partId, part.slot);
          }
          p += 1;
        }
      }
      TandemNative.Log(s"trade: took \(TDBID.ToStringDEBUG(item.record)) x\(item.quantity) (\(ok)), \(item.stats) stats, \(item.parts) parts");
      i += 1;
    }
    let got = TandemTrade.TradeMoney(3);
    if got > 0 {
      ts.GiveItem(player, MarketSystem.Money(), got);
    }
    this.m_trades += 1;
    TandemNative.Log(s"trade: applied with \(this.NameOf(with)): gave \(given) stacks and \(pay) eddies, took \(taken) stacks and \(got) eddies\(short > 0 ? s" (\(short) stacks short)" : "")");
  }

  private func Restamp() -> Void {
    if ArraySize(this.m_fixIds) == 0 || this.m_now < this.m_fixAt {
      return;
    }
    let player = GetPlayer(this.m_game);
    let ts = GameInstance.GetTransactionSystem(this.m_game);
    let i = 0;
    while i < ArraySize(this.m_fixIds) {
      let data = ts.GetItemData(player, this.m_fixIds[i]);
      if IsDefined(data) {
        this.SetStat(data, this.m_fixStats[i], this.m_fixValues[i]);
      }
      i += 1;
    }
    ArrayClear(this.m_fixIds);
    ArrayClear(this.m_fixStats);
    ArrayClear(this.m_fixValues);
  }

  // ---- the screen's words ----

  // One line: what each side gives and asks, and who agrees.
  public func Title() -> String {
    let name = this.NameOf(this.m_with);
    let theirs = s"\(name): \(TandemTrade.TradeItemCount(1)) items, \(TandemTrade.TradeMoney(1)) eddies";
    if TandemTrade.TradeAsks(1) > 0 {
      theirs += s", asks \(TandemTrade.TradeAsks(1))";
    }
    if TandemTrade.TradeTheyConfirmed() {
      theirs += " - AGREED";
    }
    let ours = s"You: \(ArraySize(this.m_stacks)) items, \(this.m_money) eddies";
    if this.m_asks > 0 {
      ours += s", ask \(this.m_asks)";
    }
    if TandemTrade.TradeWeConfirmed() {
      ours += " - AGREED";
    }
    return s"\(theirs)   |   \(ours)";
  }

  public func AgreeLabel() -> String {
    return TandemTrade.TradeWeConfirmed() ? "Take back your agreement" : "Agree to the trade";
  }

  public func Money() -> Int32 {
    return this.m_money;
  }

  public func Asks() -> Int32 {
    return this.m_asks;
  }

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  // As Travel.reds: the HUD's on-screen message, and the phone key's hint.
  // A notice by its key (Text.reds).
  private func Notice(key: String, seconds: Float, opt a: String, opt b: String) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, seconds, key, a, b);
  }


  private func Hint(show: Bool, label: String) -> Void {
    let data: InputHintData;
    data.action = n"PhoneInteract";
    data.source = n"TandemTrade";
    data.localizedLabel = label;
    data.queuePriority = 10;
    data.sortingPriority = 10;
    data.holdIndicationType = inkInputHintHoldIndicationType.Press;
    let evt = new UpdateInputHintEvent();
    evt.data = data;
    evt.show = show;
    evt.targetHintContainer = n"GameplayInputHelper";
    GameInstance.GetUISystem(this.m_game).QueueEvent(evt);
  }

  // ---- tools/game.sh cmd trade ... (the scenarios: no screen needed) ----
  // trade | trade ask [PLAYER] | trade accept | trade decline |
  // trade give RECORD [N] | trade back RECORD [N] | trade money N |
  // trade asks N | trade agree | trade cancel
  public func Command(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "status";
    let arg = ArraySize(words) > 2 ? words[2] : "";
    let count = ArraySize(words) > 3 ? StringToInt(words[3], 1) : 1;
    if Equals(verb, "ask") {
      let to = Cast<Uint32>(StringToInt(arg, 0));
      if to == 0u {
        for other in this.m_system.PuppetPlayers() {
          if to == 0u {
            to = other;
          }
        }
      }
      this.Ask(to);
    } else if Equals(verb, "accept") {
      this.Accept();
    } else if Equals(verb, "decline") {
      this.Decline();
    } else if Equals(verb, "give") || Equals(verb, "back") {
      let id = this.Owned(TDBID.Create(arg));
      if !ItemID.IsValid(id) {
        TandemNative.Log(s"trade: no \(arg) in our inventory");
      } else if Equals(verb, "give") {
        TandemNative.Log(s"trade: give \(arg) x\(count): \(this.Put(id, count))");
      } else {
        TandemNative.Log(s"trade: back \(arg) x\(count): \(this.TakeBack(id, count))");
      }
    } else if Equals(verb, "money") {
      this.SetMoney(StringToInt(arg, 0));
    } else if Equals(verb, "asks") {
      this.SetAsks(StringToInt(arg, 0));
    } else if Equals(verb, "agree") {
      this.ToggleAgree();
    } else if Equals(verb, "cancel") {
      this.Close(2);
    }
    let state = TandemTrade.TradeState();
    let names: array<String> = ["none", "asking", "asked", "open"];
    TandemNative.Log(s"trade: \(TandemTrade.TradeEnabled() ? "on" : "off"), \(names[state]) with player \(TandemTrade.TradeWith()); ours \(ArraySize(this.m_stacks)) stacks, \(this.m_money) eddies, asks \(this.m_asks), agreed \(TandemTrade.TradeWeConfirmed()); theirs \(TandemTrade.TradeItemCount(1)) stacks, \(TandemTrade.TradeMoney(1)) eddies, asks \(TandemTrade.TradeAsks(1)), agreed \(TandemTrade.TradeTheyConfirmed()); screen \(IsDefined(this.m_screen)); \(this.m_trades) trades done");
  }

  // The first item of that record in our inventory.
  private func Owned(record: TweakDBID) -> ItemID {
    let items: array<wref<gameItemData>>;
    GameInstance.GetTransactionSystem(this.m_game).GetItemList(GetPlayer(this.m_game), items);
    for data in items {
      if IsDefined(data) && ItemID.GetTDBID(data.GetID()) == record {
        return data.GetID();
      }
    }
    return ItemID.None();
  }
}

// ---- the storage screen as a trade table ----

// What the table's side lists: not the puppet's own inventory (its clothes
// and weapons) but the two sides of the trade.
@wrapMethod(VendorDataManager)
public final func GetStorageItems() -> array<ref<gameItemData>> {
  let table = TandemTradeTable.Of(GetGameInstance());
  if IsDefined(table) && table.IsTable(this.m_VendorObject) {
    return table.Shown();
  }
  return wrappedMethod();
}

// Every move on the storage screen ends here (a click, the quantity
// picker, the equipped-item confirmation). At a trade table nothing moves:
// the table takes note.
@wrapMethod(VendorDataManager)
public final func TransferItem(source: wref<GameObject>, target: wref<GameObject>, itemData: wref<gameItemData>, amount: Int32) -> Void {
  let table = TandemTradeTable.Of(GetGameInstance());
  if IsDefined(table) && table.IsTable(this.m_VendorObject) {
    table.Move(source, itemData, amount);
    return;
  }
  wrappedMethod(source, target, itemData, amount);
}

// The screen is up: if it is ours, its title is the trade and its hints
// are the table's moves.
@wrapMethod(FullscreenVendorGameController)
protected cb func OnSetUserData(userData: ref<IScriptable>) -> Bool {
  TandemNative.HookRan(n"Trade.FullscreenVendorGameController.OnSetUserData");
  let result = wrappedMethod(userData);
  let table = TandemTradeTable.Of(GetGameInstance());
  if IsDefined(table) && IsDefined(this.m_storageUserData) && table.IsTable(this.m_storageUserData.storageObject) {
    table.Bind(this);
    this.TandemTradeRefresh();
  }
  return result;
}

// A trade does not stop the world (ours runs on while we look at the
// table: a fight that starts ends the trade).
@wrapMethod(FullscreenVendorGameController)
private final func SetTimeDilatation(enable: Bool) -> Void {
  let table = TandemTradeTable.Of(GetGameInstance());
  if enable && IsDefined(table) && table.IsOpening() {
    return;
  }
  wrappedMethod(enable);
}

@wrapMethod(FullscreenVendorGameController)
protected cb func OnUninitialize() -> Bool {
  TandemNative.HookRan(n"Trade.FullscreenVendorGameController.OnUninitialize");
  let table = TandemTradeTable.Of(GetGameInstance());
  if IsDefined(table) && this.m_tandemTrade {
    table.Unbind(this);
  }
  return wrappedMethod();
}

@addField(FullscreenVendorGameController)
private let m_tandemTrade: Bool;

@addField(FullscreenVendorGameController)
private let m_tandemInput: ref<inkGameNotificationToken>;

@addField(FullscreenVendorGameController)
private let m_tandemAsking: Bool;

// The table's keys: agree (the vendor screen's "sell junk" key, unused in
// storage mode), the eddies we give, the eddies we ask for.
@wrapMethod(FullscreenVendorGameController)
protected cb func OnHandleGlobalRelease(evt: ref<inkPointerEvent>) -> Bool {
  TandemNative.HookRan(n"Trade.FullscreenVendorGameController.OnHandleGlobalRelease");
  let table = TandemTradeTable.Of(GetGameInstance());
  if this.m_tandemTrade && IsDefined(table) && !IsDefined(this.m_tandemInput) {
    if evt.IsAction(n"sell_junk") {
      table.ToggleAgree();
      this.TandemTradeRefresh();
    } else if evt.IsAction(n"drop_item") {
      this.m_tandemAsking = false;
      this.m_tandemInput = GenericMessageNotification.ShowInput(this, TandemText.Get("Trade-Prompt"), TandemText.F("Trade-InputGive", s"\(table.Money())"), GenericMessageNotificationType.ConfirmCancel);
      this.m_tandemInput.RegisterListener(this, n"OnTandemTradeInput");
    } else if evt.IsAction(n"disassemble_item") {
      this.m_tandemAsking = true;
      this.m_tandemInput = GenericMessageNotification.ShowInput(this, TandemText.Get("Trade-Prompt"), TandemText.F("Trade-InputAsk", s"\(table.Asks())"), GenericMessageNotificationType.ConfirmCancel);
      this.m_tandemInput.RegisterListener(this, n"OnTandemTradeInput");
    }
  }
  return wrappedMethod(evt);
}

@addMethod(FullscreenVendorGameController)
protected cb func OnTandemTradeInput(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandemInput = null;
  let close = data as GenericMessageNotificationCloseData;
  let table = TandemTradeTable.Of(GetGameInstance());
  if !IsDefined(close) || !IsDefined(table) || NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    return false;
  }
  let amount = StringToInt(close.input, -1);
  if amount < 0 {
    return false;
  }
  if this.m_tandemAsking {
    table.SetAsks(amount);
  } else {
    table.SetMoney(amount);
  }
  this.TandemTradeRefresh();
  return true;
}

// Drawn again: the table's side, the title line, the hints.
@addMethod(FullscreenVendorGameController)
public func TandemTradeRefresh() -> Void {
  let table = TandemTradeTable.Of(GetGameInstance());
  if !IsDefined(table) {
    return;
  }
  this.m_tandemTrade = true;
  this.PopulateVendorInventory();
  inkTextRef.SetText(this.m_vendorName, table.Title());
  this.m_buttonHintsController.AddButtonHint(n"sell_junk", table.AgreeLabel());
  this.m_buttonHintsController.AddButtonHint(n"drop_item", "Eddies you give");
  this.m_buttonHintsController.AddButtonHint(n"disassemble_item", "Eddies you ask for");
}

// Closed by the trade (done, or over): as the player's own Close.
@addMethod(FullscreenVendorGameController)
public func TandemTradeClose() -> Void {
  this.m_tandemTrade = false;
  this.m_menuEventDispatcher.SpawnEvent(n"OnVendorClose");
}
