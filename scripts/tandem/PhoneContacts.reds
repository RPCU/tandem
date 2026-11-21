// The other players in the game's own phone (docs/architecture/comms.md,
// "The phone"): the contact list V opens by holding the phone key shows the
// session's other players first, above Panam, Judy and the rest. Selecting
// one (the call key, or the messages key) opens their list of what can be
// done with them, as a contact's message threads open:
//
// - Call / Answer / Cancel the call / Hang up: the comms link
//   (Comms.reds PhoneCall). Their game rings with our name; an answer links.
// - Trade: the trade request, at any distance (Trade.reds AskFromPhone).
// - Join NAME: fast travel to them (Travel.reds RegroupTo, the world map's
//   regroup action).
// - Ask NAME to join you: a regroup offer to them (Travel.reds Offer).
//
// The rows are the phone's own ContactData, marked by an id of ours
// ("tandem:PLAYER[:ACTION]"); the wraps at the end of this file put them on
// the list and take the keys for them, the game's contacts untouched. A
// chosen action closes the phone and runs on our next tick. Nothing new
// crosses games: each action is the message its feature already sends.

public class TandemPhoneContacts {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  // The phone's controller, once it listed players (the test command pick).
  private let m_phone: wref<NewHudPhoneGameController>;
  // What a row asked for, done on the next tick (0: nothing).
  private let m_player: Uint32;
  private let m_action: Int32;
  // (test) `phone open` wants the Contacts tab, whatever the phone would
  // open on (Messages while texts are unread, the game's own rule).
  private let m_wantContacts: Bool;

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
  }

  public func Stop() -> Void {
    this.m_player = 0u;
    this.m_action = 0;
  }

  public static func Of(game: GameInstance) -> ref<TandemPhoneContacts> {
    let system = TandemSystem.Of(game);
    return IsDefined(system) ? system.PhoneContacts() : null;
  }

  // ---- the rows ----

  // A row of ours (a player, or one of their actions).
  public static func IsOurs(data: wref<ContactData>) -> Bool {
    return IsDefined(data) && StrBeginsWith(data.id, "tandem:");
  }

  public static func PlayerOf(data: wref<ContactData>) -> Uint32 {
    let parts = StrSplit(data.id, ":");
    return ArraySize(parts) > 1 ? Cast<Uint32>(StringToInt(parts[1], 0)) : 0u;
  }

  // 0: the player's own row; else the action (Actions).
  public static func ActionOf(data: wref<ContactData>) -> Int32 {
    let parts = StrSplit(data.id, ":");
    return ArraySize(parts) > 2 ? StringToInt(parts[2], 0) : 0;
  }

  // The other players, as the session knows them here.
  public func Players() -> array<Uint32> {
    let out: array<Uint32>;
    if !IsDefined(this.m_system) || !this.m_system.InSession() {
      return out;
    }
    let all = this.m_system.PuppetPlayers();
    for player in all {
      if TandemSync.InSession(player) {
        ArrayPush(out, player);
      }
    }
    return out;
  }

  // What can be done with a player: 1 the link (a call), 2 trade (when
  // trade is on), 3 go to them, 4 ask them here.
  public func Actions() -> array<Int32> {
    let out: array<Int32>;
    ArrayPush(out, 1);
    if TandemTrade.TradeEnabled() {
      ArrayPush(out, 2);
    }
    ArrayPush(out, 3);
    ArrayPush(out, 4);
    return out;
  }

  public func PlayerRows() -> array<ref<IScriptable>> {
    let rows: array<ref<IScriptable>>;
    let players = this.Players();
    for player in players {
      ArrayPush(rows, this.Row(player, 0, this.NameOf(player), this.StateText(player, false)));
    }
    return rows;
  }

  public func ActionRows(player: Uint32) -> array<ref<IScriptable>> {
    let rows: array<ref<IScriptable>>;
    let actions = this.Actions();
    for action in actions {
      ArrayPush(rows, this.Row(player, action, this.Label(player, action, false), this.About(player, action, false)));
    }
    return rows;
  }

  private func Row(player: Uint32, action: Int32, name: String, about: String) -> ref<ContactData> {
    let row = new ContactData();
    row.id = action == 0 ? s"tandem:\(player)" : s"tandem:\(player):\(action)";
    row.contactId = row.id;
    row.localizedName = name;
    row.localizedPreview = about;
    row.hasValidTitle = StrLen(about) > 0;
    row.type = MessengerContactType.Contact;
    row.isCallable = true;
    row.hash = -7700 - Cast<Int32>(player) * 16 - action;
    return row;
  }

  // A player's row, under the name: the link between us.
  public func StateText(player: Uint32, english: Bool) -> String {
    let state = TandemCommsNative.LinkState(player);
    let key = "Phone-NotLinked";
    if state == 1 {
      key = "Phone-Calling";
    } else if state == 2 {
      key = "Phone-CallingYou";
    } else if state == 3 {
      key = "Phone-Linked";
    }
    return TandemText.T(english, key);
  }

  // The link's row says what a call does now.
  private func LinkKey(player: Uint32) -> String {
    let state = TandemCommsNative.LinkState(player);
    if state == 1 {
      return "Phone-Cancel";
    }
    if state == 2 {
      return "Phone-Answer";
    }
    if state == 3 {
      return "Phone-HangUp";
    }
    return "Phone-Call";
  }

  private func LinkAboutKey(player: Uint32) -> String {
    if TandemCommsNative.LinkSetting() == 2 {
      return "Comms-LinksOff";
    }
    let state = TandemCommsNative.LinkState(player);
    if state == 1 {
      return "Phone-CancelAbout";
    }
    if state == 2 {
      return "Phone-AnswerAbout";
    }
    if state == 3 {
      return "Phone-HangUpAbout";
    }
    return "Phone-CallAbout";
  }

  public func Label(player: Uint32, action: Int32, english: Bool) -> String {
    let name = this.NameOf(player);
    if action == 1 {
      return TandemText.T(english, this.LinkKey(player));
    }
    if action == 2 {
      return TandemText.T(english, "Trade-Prompt");
    }
    if action == 3 {
      return TandemText.FE(english, "Travel-Join", name);
    }
    return TandemText.FE(english, "Phone-Invite", name);
  }

  public func About(player: Uint32, action: Int32, english: Bool) -> String {
    let name = this.NameOf(player);
    if action == 1 {
      return TandemText.FE(english, this.LinkAboutKey(player), name);
    }
    if action == 2 {
      return TandemText.T(english, "Phone-TradeAbout");
    }
    if action == 3 {
      return TandemText.FE(english, "Phone-JoinAbout", name);
    }
    return TandemText.FE(english, "Phone-InviteAbout", name);
  }

  // A row's text in English (the test command pick names rows so).
  public func EnglishOf(data: wref<ContactData>) -> String {
    if !TandemPhoneContacts.IsOurs(data) {
      return data.localizedName;
    }
    let player = TandemPhoneContacts.PlayerOf(data);
    let action = TandemPhoneContacts.ActionOf(data);
    return action == 0 ? this.NameOf(player) : this.Label(player, action, true);
  }

  public func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  // ---- what a row does ----

  public func TakeWantContacts() -> Bool {
    let want = this.m_wantContacts;
    this.m_wantContacts = false;
    return want;
  }

  public func Bind(phone: ref<NewHudPhoneGameController>) -> Void {
    this.m_phone = phone;
  }

  // Chosen on the phone: done once the phone has closed (the next tick).
  public func Queue(player: Uint32, action: Int32) -> Void {
    this.m_player = player;
    this.m_action = action;
    TandemNative.Log(s"phone: \(this.NameOf(player)): \(this.Label(player, action, true)) chosen");
  }

  public func Tick() -> Void {
    if this.m_action == 0 {
      return;
    }
    let player = this.m_player;
    let action = this.m_action;
    this.m_player = 0u;
    this.m_action = 0;
    this.Do(player, action);
  }

  public func Do(player: Uint32, action: Int32) -> Bool {
    let name = this.NameOf(player);
    if !TandemSync.InSession(player) || !IsDefined(this.m_system) {
      TandemNative.Log(s"phone: player \(player) is no longer in the session");
      return false;
    }
    let done = false;
    if action == 1 {
      let comms = this.m_system.Comms();
      done = IsDefined(comms) && comms.PhoneCall(player);
    } else if action == 2 {
      let trade = this.m_system.Trade();
      done = IsDefined(trade) && trade.AskFromPhone(player);
    } else if action == 3 {
      done = TandemTravel.RegroupTo(this.m_game, player);
    } else if action == 4 {
      done = TandemTravel.OfferRegroup(this.m_game, player, 1, "") != 0u;
      if done {
        TandemText.Notice(this.m_game, "Phone-Invited", name);
      }
    }
    TandemNative.Log(s"phone: \(TandemPhoneContacts.Verb(action)) \(name): \(done)");
    return done;
  }

  private static func Verb(action: Int32) -> String {
    if action == 1 {
      return "call";
    }
    if action == 2 {
      return "trade";
    }
    if action == 3 {
      return "join";
    }
    return "invite";
  }

  // ---- tools/game.sh cmd phone ... (the scenarios) ----
  // phone | phone open | phone close | phone pick TEXT |
  // phone call|trade|join|invite NAME
  public func Command(command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "report";
    let rest = ArraySize(words) > 2 ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "";
    if Equals(verb, "open") {
      // As the phone key's hold (NewHudPhoneGameController.OnAction).
      this.m_wantContacts = true;
      GameInstance.QueueScriptableSystemRequest(this.m_game, n"PhoneSystem", new UsePhoneRequest());
      TandemNative.Log("phone: opening the contacts list");
    } else if Equals(verb, "close") {
      GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice).SetBool(GetAllBlackboardDefs().UI_ComDevice.ContactsActive, false, true);
      TandemNative.Log("phone: closing the contacts list");
    } else if Equals(verb, "pick") {
      this.Pick(rest);
    } else if Equals(verb, "call") || Equals(verb, "trade") || Equals(verb, "join") || Equals(verb, "invite") {
      let player = this.PlayerNamed(rest);
      if player == 0u {
        TandemNative.Log(s"phone: no player \(rest) in the session");
        return;
      }
      let action = 4;
      if Equals(verb, "call") {
        action = 1;
      } else if Equals(verb, "trade") {
        action = 2;
      } else if Equals(verb, "join") {
        action = 3;
      }
      this.Do(player, action);
    } else {
      this.Report();
    }
  }

  // What the phone would list now, in English.
  private func Report() -> Void {
    let players = this.Players();
    let actions = this.Actions();
    TandemNative.Log(s"phone: \(ArraySize(players)) player(s) on the contacts list, the list \(this.ListOpen() ? "open" : "closed")");
    for player in players {
      let line = "";
      for action in actions {
        if StrLen(line) > 0 {
          line += " | ";
        }
        line += this.Label(player, action, true);
      }
      TandemNative.Log(s"phone: \(this.NameOf(player)) (\(this.StateText(player, true))): \(line)");
    }
  }

  private func ListOpen() -> Bool {
    return IsDefined(this.m_phone) && IsDefined(this.m_phone.m_contactListLogicController);
  }

  // A row of the open list, by its text (the game's language or English),
  // chosen as the call key would: a player opens their actions, an action
  // is done. The game's own contacts are never called from here.
  private func Pick(text: String) -> Void {
    if !this.ListOpen() {
      TandemNative.Log("phone: the contacts list is not open");
      return;
    }
    let phone = this.m_phone;
    let logic = phone.m_contactListLogicController;
    let view = logic.m_dataView;
    let i = 0u;
    while i < view.Size() {
      let data = view.GetItem(i) as ContactData;
      if IsDefined(data) && (Equals(data.localizedName, text) || Equals(this.EnglishOf(data), text)) {
        logic.m_listController.SelectItem(i);
        TandemNative.Log(s"phone: picked \(text) (row \(i))");
        if !TandemPhoneContacts.IsOurs(data) {
          TandemNative.Log(s"phone: \(text) is one of the game's contacts: not called");
          return;
        }
        phone.TandemChooseRow(data);
        return;
      }
      i += 1u;
    }
    TandemNative.Log(s"phone: no row \(text) on the open list");
  }

  // A player by name (or id); none given: the first.
  private func PlayerNamed(text: String) -> Uint32 {
    let players = this.Players();
    for player in players {
      if StrLen(text) == 0 || Equals(TandemNative.PlayerName(player), text) {
        return player;
      }
    }
    let id = StringToInt(text, 0);
    return id > 0 ? Cast<Uint32>(id) : 0u;
  }
}

// ---- the phone's contact list (NewHudPhoneGameController, final.reds:408140) ----
//
// Its Contacts tab is filled by SetScreenType (MessengerUtils'
// GetCallableAndNonEmptyContacts, sorted by name); the call key is
// CallContact, the messages key and a thread's row ExecuteAction; a
// contact's threads are a list pushed over the contacts (PushList), popped
// by Back or a tab change. Ours use the same: no widget of our own. These
// are the HUD controller's own functions, reached from its input listener
// on the game thread (not a per-entity class, no cb func).

// The player whose actions the pushed list shows (0: none).
@addField(NewHudPhoneGameController)
private let m_tandemPlayer: Uint32;

// The Contacts tab: the session's players first, then the game's contacts
// as it sorted them.
@wrapMethod(NewHudPhoneGameController)
public final func SetScreenType(type: PhoneScreenType) {
  let screen = type;
  let contacts = TandemPhoneContacts.Of(GetGameInstance());
  if IsDefined(contacts) && contacts.TakeWantContacts() {
    screen = PhoneScreenType.Contacts;
  }
  wrappedMethod(screen);
  this.m_tandemPlayer = 0u;
  if Equals(screen, PhoneScreenType.Contacts) {
    this.TandemPlayersFirst();
  }
}

@addMethod(NewHudPhoneGameController)
private final func TandemPlayersFirst() -> Void {
  let logic = this.m_contactListLogicController;
  if !IsDefined(logic) || !IsDefined(this.m_player) {
    return;
  }
  let contacts = TandemPhoneContacts.Of(this.m_player.GetGame());
  if !IsDefined(contacts) {
    return;
  }
  contacts.Bind(this);
  let rows = contacts.PlayerRows();
  if ArraySize(rows) == 0 {
    return;
  }
  let players = ArraySize(rows);
  let current = logic.m_dataSource.GetArray();
  for item in current {
    if !TandemPhoneContacts.IsOurs(item as ContactData) {
      ArrayPush(rows, item);
    }
  }
  // (Sorting is off again after the list was filled: kept in this order.)
  logic.m_dataSource.Reset(rows);
  TandemNative.Log(s"phone: the contacts list shows \(players) player(s) first");
}

// The call key on a row of ours.
@wrapMethod(NewHudPhoneGameController)
public final func CallContact() {
  if this.TandemChoose() {
    return;
  }
  wrappedMethod();
}

// The messages key, and a pushed list's row (a thread; our actions).
@wrapMethod(NewHudPhoneGameController)
public final func ExecuteAction() {
  if this.TandemChoose() {
    return;
  }
  wrappedMethod();
}

@addMethod(NewHudPhoneGameController)
private final func TandemChoose() -> Bool {
  if !IsDefined(this.m_contactListLogicController) {
    return false;
  }
  return this.TandemChooseRow(this.m_contactListLogicController.GetSelectedContactData());
}

// A row of ours chosen: a player's opens their actions, an action closes
// the phone and is done on the next tick. False: not ours.
@addMethod(NewHudPhoneGameController)
public final func TandemChooseRow(data: wref<ContactData>) -> Bool {
  if !TandemPhoneContacts.IsOurs(data) {
    return false;
  }
  let contacts: ref<TandemPhoneContacts>;
  if IsDefined(this.m_player) {
    contacts = TandemPhoneContacts.Of(this.m_player.GetGame());
  }
  if !IsDefined(contacts) {
    // The session ended with the list open: the row does nothing.
    return true;
  }
  let player = TandemPhoneContacts.PlayerOf(data);
  let action = TandemPhoneContacts.ActionOf(data);
  this.PlayRumble(RumbleStrength.SuperLight, RumbleType.Pulse, RumblePosition.Right);
  if action == 0 {
    this.TandemShowActions(contacts, player);
  } else {
    contacts.Queue(player, action);
    this.CloseContactList();
  }
  return true;
}

// A player's actions, pushed over the list as a contact's threads are.
@addMethod(NewHudPhoneGameController)
private final func TandemShowActions(contacts: ref<TandemPhoneContacts>, player: Uint32) -> Void {
  let logic = this.m_contactListLogicController;
  let rows = contacts.ActionRows(player);
  this.m_threadsVisible = true;
  this.m_isSingleThread = false;
  this.m_tandemPlayer = player;
  logic.PushList(rows, ContactsSortMethod.ByName);
  // In our order (PushList sorted them by name).
  logic.m_dataSource.Reset(rows);
  this.m_audioSystem.Play(n"ui_menu_map_pin_created");
  logic.SetTitle(contacts.NameOf(player));
  logic.ShowTitle(true);
  let line = "";
  let actions = contacts.Actions();
  for action in actions {
    if StrLen(line) > 0 {
      line += " | ";
    }
    line += contacts.Label(player, action, true);
  }
  TandemNative.Log(s"phone: \(contacts.NameOf(player))'s actions: \(line)");
}

// Back from a player's actions: the contacts again, players first (the
// game's PopList sorted the list it kept).
@wrapMethod(NewHudPhoneGameController)
public final func Back() {
  let ours = this.m_threadsVisible && this.m_tandemPlayer != 0u;
  wrappedMethod();
  if ours {
    this.m_tandemPlayer = 0u;
    this.TandemPlayersFirst();
  }
}

@wrapMethod(NewHudPhoneGameController)
public final func SelectOtherTab() {
  let ours = this.m_threadsVisible && this.m_tandemPlayer != 0u;
  wrappedMethod();
  if ours {
    this.m_tandemPlayer = 0u;
    this.TandemPlayersFirst();
  }
}

// A row of ours has no messages: the messenger panel is not opened on it
// (OnContactSelectionChanged opens it for any row of a pushed list).
@wrapMethod(NewHudPhoneGameController)
public final func GotoSmsMessenger(contactData: wref<ContactData>) {
  if TandemPhoneContacts.IsOurs(contactData) {
    return;
  }
  wrappedMethod(contactData);
}

@wrapMethod(NewHudPhoneGameController)
public final func RefreshSmsMessager(contactData: wref<ContactData>) {
  if TandemPhoneContacts.IsOurs(contactData) {
    return;
  }
  wrappedMethod(contactData);
}
