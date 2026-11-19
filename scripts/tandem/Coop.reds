// Co-op in the game's own menus (docs/architecture/coop-menu.md, milestone 7).
//
// Main menu (SingleplayerMenuGameController): a "Co-op" entry. Its pages
// live in the menu's own list, the way the menu shows its items:
//   Co-op   -> Host a game | Join a game | Play solo (once one is chosen) | Back
//   Host    -> Continue | Load game | Back      (the game's own load list)
//   Join    -> recent hosts... | Enter an address | Back
//   Save    -> Continue | Load game | Back      (the V to join with)
// Picking Continue or Load game chooses the session in the plugin
// (TandemNative.StartHost / Join); TandemSystem starts it as the save loads.
//
// Pause menu (PauseMenuGameController): "Co-op" -> Host this game | Join a
// game, or, in a session, Invite | Leave session. These start and stop the
// session at once (TandemSystem.CoopHost / CoopJoin / CoopLeave).
//
// Both show a status panel (who is in the session, the ping) and take the
// test harness's commands (tools/game.sh cmd coop ...): our system's tick
// does not run in the main menu, nor while the pause menu pauses the
// world, so the menus keep a clock of their own (a looping ink animation:
// ink animations run in menus, paused or not).
//
// Join codes (tandem-relay): what the player types to join is a join code
// (TandemNative.IsJoinCode, no dot or colon) or an address
// (TandemCoop.Resolve); a code joins through tandem.ini's relay
// (TandemNative.JoinByCode). The host shows its code (RelayCode) in the
// status panel and the invite, with the relay's state (RelayStatus).

enum TandemCoopPage {
  Root = 0,
  Coop = 1,
  Host = 2,
  Join = 3,
  Save = 4,
}

enum TandemCoopAction {
  OpenCoop = 0,
  Back = 1,
  HostPage = 2,
  JoinPage = 3,
  EnterAddress = 4,
  JoinRecent = 5,
  Continue = 6,
  LoadGame = 7,
  Solo = 8,
  HostNow = 9,
  Invite = 10,
  Leave = 11,
  Recorder = 12,
}

// One of our entries in a menu's list: the vanilla item data (its label is
// what the list shows), and what it does.
public class TandemMenuItem extends PauseMenuListItemData {
  public let coop: TandemCoopAction;
  // Its text's key (Text.reds; "" for a label that is data or vanilla's).
  public let key: String;
  // JoinRecent: "address:port"
  public let target: String;
}

// What both menus show of the session, and the in-game notices.
public class TandemCoop {
  public static func HostName() -> String {
    for player in TandemNative.SessionPlayers() {
      if player.host {
        return player.name;
      }
    }
    return "";
  }

  // The session in a few lines, then who is in it.
  public static func StatusLines(opt english: Bool) -> array<String> {
    let lines: array<String>;
    let status = TandemNative.SessionStatus();
    let target = StrLen(status.joinCode) > 0 ? TandemText.FE(english, "Status-CodeTarget", status.joinCode) : s"\(status.address):\(status.port)";
    if status.role == 1 {
      if status.state == 0 {
        ArrayPush(lines, TandemText.FE(english, "Status-HostAtLoad", s"\(status.port)"));
      } else if status.state == 5 {
        ArrayPush(lines, TandemText.FE(english, "Status-HostFailed", TandemText.Reason(status.detail, english)));
      } else {
        ArrayPush(lines, TandemText.FE(english, "Status-Hosting", s"\(status.port)"));
        let code = TandemNative.RelayCode();
        let relay = TandemNative.RelayStatus();
        if StrLen(code) > 0 {
          ArrayPush(lines, TandemText.FE(english, "Status-Code", code));
        } else if StrLen(relay) > 0 && !Equals(relay, "ready") {
          ArrayPush(lines, TandemText.FE(english, "Status-Relay", TandemText.Reason(relay, english)));
        }
      }
    } else if status.role == 2 {
      if status.state == 0 {
        ArrayPush(lines, TandemText.FE(english, "Status-JoinAtLoad", target));
      } else if status.state == 3 {
        let host = TandemCoop.HostName();
        ArrayPush(lines, StrLen(host) > 0 ? TandemText.FE(english, "Status-InSessionOf", host) : TandemText.FE(english, "Status-InSessionAt", target));
      } else if status.state == 4 {
        ArrayPush(lines, TandemText.FE(english, "Status-Retrying", target, TandemText.Reason(status.detail, english)));
      } else {
        ArrayPush(lines, TandemText.FE(english, "Status-Connecting", target));
      }
    } else {
      ArrayPush(lines, TandemText.T(english, "Status-None"));
    }
    if status.state == 3 {
      for player in TandemNative.SessionPlayers() {
        let line = player.name;
        if player.host {
          line = TandemText.FE(english, "Status-Host", line);
        }
        if player.you {
          line = TandemText.FE(english, "Status-You", line);
        } else if player.pingMs >= 0 {
          line = TandemText.FE(english, "Status-Ping", line, s"\(player.pingMs)");
        }
        ArrayPush(lines, line);
      }
    }
    if TandemNative.RecorderOn() {
      ArrayPush(lines, TandemText.FE(english, "Status-Recorder", TandemNative.RecorderStatus()));
    }
    return lines;
  }

  // How friends reach this game. (Join codes: the host's code goes first.)
  public static func InviteText(opt english: Bool) -> String {
    let status = TandemNative.SessionStatus();
    let port = status.role == 1 ? status.port : TandemNative.DefaultPort();
    let addresses = TandemNative.LocalAddresses();
    let code = TandemNative.RelayCode();
    let prefix = StrLen(code) > 0 ? TandemText.FE(english, "Invite-Code", code) : "";
    if ArraySize(addresses) == 0 {
      return prefix + TandemText.FE(english, "Invite-NoAddress", s"\(port)");
    }
    let targets: array<String>;
    for address in addresses {
      ArrayPush(targets, s"\(address):\(port)");
    }
    return prefix + TandemText.FE(english, "Invite-Addresses", TandemText.AnyOf(targets, english));
  }

  // A join code rather than an address (a host name has a dot, or is not
  // six characters of base32).
  public static func IsCode(text: String) -> Bool {
    return TandemNative.IsJoinCode(text) && !StrContains(text, ".") && !StrContains(text, ":");
  }

  // What the player typed: a join code as typed, else "address:port"; ""
  // when it is neither.
  public static func Resolve(text: String) -> String {
    if TandemCoop.IsCode(text) {
      return text;
    }
    return TandemCoop.Target(text);
  }

  public static func Shown(target: String) -> String {
    return TandemCoop.IsCode(target) ? TandemText.F("Status-CodeTarget", StrUpper(target)) : target;
  }

  // Choose to join it (a code through the relay). False: not possible
  // (a code with no relay in tandem.ini, or not an address).
  public static func ChooseJoin(target: String) -> Bool {
    if TandemCoop.IsCode(target) {
      return TandemNative.JoinByCode(target);
    }
    return TandemNative.Join(target, 0);
  }

  // What the player typed, as "address:port"; "" when it is none.
  public static func Target(text: String) -> String {
    let host = TandemNative.ParseAddress(text);
    if StrLen(host.address) == 0 {
      return "";
    }
    return s"\(host.address):\(host.port)";
  }

  public static func LogStatus() -> Void {
    for line in TandemCoop.StatusLines(true) {
      TandemNative.Log(s"coop: \(line)");
    }
    for host in TandemNative.RecentHosts() {
      TandemNative.Log(s"coop: recent host \(host.address):\(host.port) \(host.name)");
    }
    TandemNative.Log(s"coop: invite: \(TandemCoop.InviteText(true))");
  }

  // The game's own short on-screen message (Text.reds).
  public static func Notice(game: GameInstance, key: String, opt a: String, opt b: String) -> Void {
    TandemText.Notice(game, key, a, b);
  }

  public static func NoticeInSession(game: GameInstance) -> Void {
    let status = TandemNative.SessionStatus();
    if status.role == 1 {
      TandemText.Say(game, SimpleMessageType.Neutral, 5.0, "Session-Hosting", TandemText.F("Session-Hosting", TandemCoop.InviteText()), TandemText.FE(true, "Session-Hosting", TandemCoop.InviteText(true)));
    } else {
      let host = TandemCoop.HostName();
      if StrLen(host) > 0 {
        TandemText.Notice(game, "Session-JoinedOf", host);
      } else {
        TandemText.Notice(game, "Session-Joined");
      }
    }
  }
}

// A menu's co-op pages: which one shows, its items, the status panel and the
// menu's clock. Owned by the menu controller (m_tandem).
public class TandemCoopMenu {
  public let page: TandemCoopPage;
  // The pause menu (a game is loaded), else the main menu.
  public let inGame: Bool;
  // Main menu: the session to choose with the save ("address:port" to join,
  // the port to host on, 0: tandem.ini's).
  public let target: String;
  public let port: Int32;
  public let items: array<ref<TandemMenuItem>>;
  // A page change waits for the next frame: the list is rebuilt outside
  // the activation callback of one of its own items.
  public let nextPage: TandemCoopPage;
  public let switching: Bool;
  public let panel: wref<inkVerticalPanel>;
  public let lines: array<ref<inkText>>;
  public let clock: wref<inkCanvas>;
  public let later: wref<inkCanvas>;
  public let ticker: ref<inkAnimProxy>;
  public let deferral: ref<inkAnimProxy>;
  public let inputToken: ref<inkGameNotificationToken>;
  // A popup closed with Esc: the menu still gets its release (clock ticks).
  public let swallowBack: Int32;

  public static func Create(inGame: Bool) -> ref<TandemCoopMenu> {
    let menu = new TandemCoopMenu();
    menu.inGame = inGame;
    menu.page = TandemCoopPage.Root;
    return menu;
  }

  public static func PageName(page: TandemCoopPage) -> String {
    if Equals(page, TandemCoopPage.Coop) {
      return "coop";
    }
    if Equals(page, TandemCoopPage.Host) {
      return "host";
    }
    if Equals(page, TandemCoopPage.Join) {
      return "join";
    }
    if Equals(page, TandemCoopPage.Save) {
      return "save";
    }
    return "root";
  }

  public static func PageOf(name: String) -> TandemCoopPage {
    if Equals(name, "coop") {
      return TandemCoopPage.Coop;
    }
    if Equals(name, "host") {
      return TandemCoopPage.Host;
    }
    if Equals(name, "join") {
      return TandemCoopPage.Join;
    }
    if Equals(name, "save") {
      return TandemCoopPage.Save;
    }
    return TandemCoopPage.Root;
  }

  // The page Back (and Esc) leads to.
  public func Up() -> TandemCoopPage {
    if Equals(this.page, TandemCoopPage.Host) || Equals(this.page, TandemCoopPage.Join) {
      return TandemCoopPage.Coop;
    }
    if Equals(this.page, TandemCoopPage.Save) {
      return TandemCoopPage.Join;
    }
    return TandemCoopPage.Root;
  }

  private func Add(key: String, action: TandemCoopAction, opt label: String) -> ref<TandemMenuItem> {
    let item = new TandemMenuItem();
    item.key = key;
    item.label = StrLen(label) > 0 ? label : TandemText.Get(key);
    item.action = PauseMenuAction.OpenSubMenu;
    item.coop = action;
    ArrayPush(this.items, item);
    return item;
  }

  // The items of the page (on the root page, only our entry: the menu's
  // own come first).
  public func Build(saves: Int32, inSession: Bool) -> Void {
    ArrayClear(this.items);
    if Equals(this.page, TandemCoopPage.Root) {
      this.Add("Menu-Coop", TandemCoopAction.OpenCoop);
      return;
    }
    if Equals(this.page, TandemCoopPage.Join) {
      for host in TandemNative.RecentHosts() {
        let address = s"\(host.address):\(host.port)";
        let item = this.Add("", TandemCoopAction.JoinRecent, StrLen(host.name) > 0 ? TandemText.F("Menu-Recent", host.name, address) : address);
        item.target = address;
      }
      this.Add("Menu-EnterCode", TandemCoopAction.EnterAddress);
    } else if this.inGame {
      if inSession {
        if TandemNative.SessionStatus().role == 1 {
          this.Add("Menu-Invite", TandemCoopAction.Invite);
        }
        this.Add("Menu-Leave", TandemCoopAction.Leave);
      } else {
        this.Add("Menu-HostNow", TandemCoopAction.HostNow);
        this.Add("Menu-Join", TandemCoopAction.JoinPage);
      }
    } else if Equals(this.page, TandemCoopPage.Coop) {
      this.Add("Menu-Host", TandemCoopAction.HostPage);
      this.Add("Menu-Join", TandemCoopAction.JoinPage);
      if TandemNative.SessionStatus().fromMenu {
        this.Add("Menu-Solo", TandemCoopAction.Solo);
      }
    } else {
      // Host, Save: the V to play.
      if saves > 0 {
        this.Add("", TandemCoopAction.Continue, TandemText.Vanilla("UI-ScriptExports-Continue0"));
      }
      this.Add("", TandemCoopAction.LoadGame, TandemText.Vanilla("UI-ScriptExports-LoadGame0"));
    }
    // The recorder's switch (until the game closes; tandem.ini's recorder
    // key is the lasting one).
    if Equals(this.page, TandemCoopPage.Coop) {
      this.Add("Status-Recorder", TandemCoopAction.Recorder, TandemText.F("Status-Recorder", TandemText.Get(TandemNative.RecorderOn() ? "Common-On" : "Common-Off")));
    }
    this.Add("Menu-Back", TandemCoopAction.Back);
  }

  // The item whose label has this text (any case), for the test harness.
  public func Find(text: String) -> ref<TandemMenuItem> {
    let wanted = StrLower(text);
    for item in this.items {
      if StrContains(StrLower(item.label), wanted) || StrLen(item.key) > 0 && StrContains(StrLower(TandemText.English(item.key)), wanted) {
        return item;
      }
    }
    return null;
  }

  public func ItemsText() -> String {
    let text = "";
    for item in this.items {
      text += s" [\(item.label)]";
    }
    return text;
  }

  // ---- the status panel and the clock ----

  public func CreatePanel(root: wref<inkCompoundWidget>, owner: ref<IScriptable>) -> Void {
    if IsDefined(this.panel) || !IsDefined(root) {
      return;
    }
    // Top right of the menu, clear of its list (the menu's space is 3840 x
    // 2160 whatever the screen: docs/research/nco-commits.md). Never
    // interactive: it must not take the menu's input.
    let panel = new inkVerticalPanel();
    panel.SetName(n"tandem_coop");
    panel.SetFitToContent(true);
    panel.SetAnchor(inkEAnchor.TopRight);
    panel.SetAnchorPoint(Vector2(1.0, 0.0));
    panel.SetHAlign(inkEHorizontalAlign.Right);
    panel.SetMargin(inkMargin(0.0, 260.0, 220.0, 0.0));
    panel.SetInteractive(false);
    panel.Reparent(root);
    this.panel = panel;
    let i = 0;
    while i < 10 {
      let text = new inkText();
      text.SetName(n"tandem_line");
      text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
      text.SetFontStyle(i == 0 ? n"Medium" : n"Regular");
      text.SetFontSize(i == 0 ? 56 : 40);
      text.SetLetterCase(i == 0 ? textLetterCase.UpperCase : textLetterCase.OriginalCase);
      text.SetStyle(r"base\\gameplay\\gui\\common\\main_colors.inkstyle");
      text.BindProperty(n"tintColor", i == 0 ? n"MainColors.Blue" : n"MainColors.Red");
      text.SetFitToContent(true);
      text.SetHAlign(inkEHorizontalAlign.Right);
      text.SetHorizontalAlignment(textHorizontalAlignment.Right);
      text.SetMargin(inkMargin(0.0, 0.0, 0.0, i == 0 ? 12.0 : 4.0));
      text.SetVisible(false);
      text.Reparent(panel);
      ArrayPush(this.lines, text);
      i += 1;
    }
    // Two empty widgets to animate: the clock (every half second) and a
    // one-frame wait for page changes.
    let clock = new inkCanvas();
    clock.SetName(n"tandem_clock");
    clock.SetSize(Vector2(0.0, 0.0));
    clock.Reparent(panel);
    this.clock = clock;
    let later = new inkCanvas();
    later.SetName(n"tandem_later");
    later.SetSize(Vector2(0.0, 0.0));
    later.Reparent(panel);
    this.later = later;

    let tick = new inkAnimTransparency();
    tick.SetStartTransparency(1.0);
    tick.SetEndTransparency(1.0);
    tick.SetDuration(0.5);
    let def = new inkAnimDef();
    def.AddInterpolator(tick);
    let options: inkAnimOptions;
    options.loopInfinite = true;
    options.loopType = inkanimLoopType.Cycle;
    this.ticker = clock.PlayAnimationWithOptions(def, options);
    this.ticker.RegisterToCallback(inkanimEventType.OnStartLoop, owner, n"OnTandemTick");
  }

  public func Stop() -> Void {
    if IsDefined(this.ticker) {
      this.ticker.Stop(true);
      this.ticker = null;
    }
    if IsDefined(this.deferral) {
      this.deferral.Stop(true);
      this.deferral = null;
    }
  }

  // Asks for OnTandemDeferred on the owner next frame; false: no panel to
  // wait on (then the owner switches at once).
  public func Defer(page: TandemCoopPage, owner: ref<IScriptable>) -> Bool {
    this.nextPage = page;
    this.switching = true;
    if !IsDefined(this.later) {
      return false;
    }
    let wait = new inkAnimTransparency();
    wait.SetStartTransparency(1.0);
    wait.SetEndTransparency(1.0);
    wait.SetDuration(0.03);
    let def = new inkAnimDef();
    def.AddInterpolator(wait);
    this.deferral = this.later.PlayAnimation(def);
    this.deferral.RegisterToCallback(inkanimEventType.OnFinish, owner, n"OnTandemDeferred");
    return true;
  }

  // The page to show now, if a change was asked for.
  public func TakeSwitch() -> Bool {
    if !this.switching {
      return false;
    }
    this.switching = false;
    this.page = this.nextPage;
    return true;
  }

  public func Refresh() -> Void {
    let lines: array<String>;
    let status = TandemNative.SessionStatus();
    // The root page: only a session chosen or running is news.
    if NotEquals(this.page, TandemCoopPage.Root) || status.role != 0 {
      ArrayPush(lines, TandemText.Get("Menu-Coop"));
      for line in TandemCoop.StatusLines() {
        ArrayPush(lines, line);
      }
      if Equals(this.page, TandemCoopPage.Host) {
        ArrayPush(lines, TandemText.Get("Status-ChooseHostSave"));
      } else if Equals(this.page, TandemCoopPage.Save) {
        ArrayPush(lines, TandemText.F("Status-ChooseJoinSave", TandemCoop.Shown(this.target)));
      } else if Equals(this.page, TandemCoopPage.Join) {
        ArrayPush(lines, TandemText.Get("Status-PickHost"));
      } else if this.inGame && status.role == 1 && status.state == 3 {
        ArrayPush(lines, TandemCoop.InviteText());
      }
    }
    let i = 0;
    while i < ArraySize(this.lines) {
      let text = this.lines[i];
      if i < ArraySize(lines) {
        let value = lines[i];
        text.SetText(value);
        text.SetVisible(true);
      } else {
        text.SetVisible(false);
      }
      i += 1;
    }
    if this.swallowBack > 0 {
      this.swallowBack -= 1;
    }
  }

  // Esc on one of our pages goes up a page (0: not ours; 1: swallow it, a
  // popup had it; 2: go up).
  public func Back(e: ref<inkPointerEvent>) -> Int32 {
    if e.IsHandled() || !e.IsAction(n"back") {
      return 0;
    }
    if IsDefined(this.inputToken) || this.swallowBack > 0 {
      this.swallowBack = 0;
      return 1;
    }
    return Equals(this.page, TandemCoopPage.Root) ? 0 : 2;
  }
}

// ---- the main menu ----

@addField(SingleplayerMenuGameController)
public let m_tandem: ref<TandemCoopMenu>;

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
  // Before the vanilla setup, which builds the list (PopulateMenuItemList).
  this.m_tandem = TandemCoopMenu.Create(false);
  let result = wrappedMethod();
  this.m_tandem.CreatePanel(this.GetRootCompoundWidget(), this);
  this.m_tandem.Refresh();
  return result;
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnUninitialize() -> Bool {
  if IsDefined(this.m_tandem) {
    this.m_tandem.Stop();
  }
  return wrappedMethod();
}

@wrapMethod(SingleplayerMenuGameController)
private func PopulateMenuItemList() -> Void {
  let root = !IsDefined(this.m_tandem) || Equals(this.m_tandem.page, TandemCoopPage.Root);
  if root {
    wrappedMethod();
  }
  if !IsDefined(this.m_tandem) {
    return;
  }
  this.m_tandem.Build(this.m_savesCount, false);
  for item in this.m_tandem.items {
    this.m_menuListController.PushData(item);
  }
  this.m_menuListController.Refresh();
  if !root {
    this.SetCursorOverWidget(inkCompoundRef.GetWidgetByIndex(this.m_menuList, 0));
  }
}

@wrapMethod(SingleplayerMenuGameController)
protected func HandleMenuItemActivate(data: ref<PauseMenuListItemData>) -> Bool {
  let item = data as TandemMenuItem;
  if !IsDefined(item) {
    return wrappedMethod(data);
  }
  return this.TandemActivate(item);
}

// Esc goes up one of our pages instead of asking to quit.
@wrapMethod(SingleplayerMenuGameController)
protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
  if IsDefined(this.m_tandem) {
    let back = this.m_tandem.Back(e);
    if back > 0 {
      e.Handle();
      if back == 2 {
        this.PlaySound(n"Button", n"OnPress");
        this.TandemGo(this.m_tandem.Up());
      }
      return true;
    }
  }
  return wrappedMethod(e);
}

// The latest save's preview belongs to Continue, not to our first items.
@wrapMethod(SingleplayerMenuGameController)
protected cb func OnContinueButtonEnter(evt: ref<inkPointerEvent>) -> Bool {
  if IsDefined(this.m_tandem) && (Equals(this.m_tandem.page, TandemCoopPage.Coop) || Equals(this.m_tandem.page, TandemCoopPage.Join)) {
    return false;
  }
  return wrappedMethod(evt);
}

@addMethod(SingleplayerMenuGameController)
private func TandemGo(page: TandemCoopPage) -> Void {
  if !this.m_tandem.Defer(page, this) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

@addMethod(SingleplayerMenuGameController)
private func TandemShowPage() -> Void {
  this.ShowActionsList();
  this.m_tandem.Refresh();
  TandemNative.Log(s"coop: main menu page \(TandemCoopMenu.PageName(this.m_tandem.page)):\(this.m_tandem.ItemsText())");
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemDeferred(proxy: ref<inkAnimProxy>) -> Bool {
  if IsDefined(this.m_tandem) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
  return true;
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemTick(proxy: ref<inkAnimProxy>) -> Bool {
  if !IsDefined(this.m_tandem) {
    return false;
  }
  this.m_tandem.Refresh();
  let command = TandemNative.TakeCommand();
  if StrBeginsWith(command, "coop") {
    this.TandemCommand(command);
  } else if StrLen(command) > 0 {
    TandemNative.Log(s"main menu: not in a game: \(command)");
  }
  return true;
}

// The session the page stands for, chosen as its save is picked. False
// (and said in a popup): it cannot be (a code with no relay set).
@addMethod(SingleplayerMenuGameController)
private func TandemChoose() -> Bool {
  if Equals(this.m_tandem.page, TandemCoopPage.Host) {
    return TandemNative.StartHost(this.m_tandem.port);
  }
  if Equals(this.m_tandem.page, TandemCoopPage.Save) && !TandemCoop.ChooseJoin(this.m_tandem.target) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-NeedsRelay"), GenericMessageNotificationType.OK);
    return false;
  }
  return true;
}

@addMethod(SingleplayerMenuGameController)
private func TandemActivate(item: ref<TandemMenuItem>) -> Bool {
  let action = item.coop;
  TandemNative.Log(s"coop: main menu: \(item.label)");
  if Equals(action, TandemCoopAction.OpenCoop) {
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Back) {
    this.TandemGo(this.m_tandem.Up());
  } else if Equals(action, TandemCoopAction.HostPage) {
    this.m_tandem.port = 0;
    this.TandemGo(TandemCoopPage.Host);
  } else if Equals(action, TandemCoopAction.JoinPage) {
    this.TandemGo(TandemCoopPage.Join);
  } else if Equals(action, TandemCoopAction.EnterAddress) {
    this.TandemAskAddress();
  } else if Equals(action, TandemCoopAction.JoinRecent) {
    this.m_tandem.target = item.target;
    this.TandemGo(TandemCoopPage.Save);
  } else if Equals(action, TandemCoopAction.Continue) {
    if !this.TandemChoose() {
      return true;
    }
    // The vanilla Continue: the latest save (with its "made with mods" prompt).
    let load = new PauseMenuListItemData();
    load.action = PauseMenuAction.QuickLoad;
    return this.HandleMenuItemActivate(load);
  } else if Equals(action, TandemCoopAction.LoadGame) {
    if !this.TandemChoose() {
      return true;
    }
    // The vanilla Load game: the game's own list of saves.
    this.m_menuEventDispatcher.SpawnEvent(n"OnLoadGame");
  } else if Equals(action, TandemCoopAction.Solo) {
    TandemNative.Leave();
    this.TandemGo(TandemCoopPage.Root);
  } else if Equals(action, TandemCoopAction.Recorder) {
    TandemNative.RecorderSet(!TandemNative.RecorderOn());
    this.TandemGo(TandemCoopPage.Coop);
  }
  return true;
}

// The game's message popup with Codeware's text field.
@addMethod(SingleplayerMenuGameController)
private func TandemAskAddress() -> Void {
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-EnterCode"), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemAddress");
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemAddress(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  let target = TandemCoop.Resolve(close.input);
  TandemNative.Log(s"coop: main menu: typed \(close.input) -> \(target)");
  if StrLen(target) == 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.F("Popup-NotATarget", close.input), GenericMessageNotificationType.OK);
    return true;
  }
  this.m_tandem.target = target;
  this.TandemGo(TandemCoopPage.Save);
  return true;
}

// tools/game.sh cmd coop ...: coop menu PAGE | coop pick TEXT | coop host
// [PORT] | coop join ADDRESS | coop solo | coop status.
@addMethod(SingleplayerMenuGameController)
private func TandemCommand(command: String) -> Void {
  let words = StrSplit(command, " ");
  let verb = "status";
  let arg = "";
  if ArraySize(words) > 1 {
    verb = words[1];
  }
  if ArraySize(words) > 2 {
    arg = StrAfterFirst(command, s"\(verb) ");
  }
  if Equals(verb, "menu") {
    this.TandemGo(TandemCoopMenu.PageOf(arg));
  } else if Equals(verb, "pick") {
    let item = this.m_tandem.Find(arg);
    if IsDefined(item) {
      this.TandemActivate(item);
    } else {
      TandemNative.Log(s"coop: main menu: no item \(arg) on page \(TandemCoopMenu.PageName(this.m_tandem.page)):\(this.m_tandem.ItemsText())");
    }
  } else if Equals(verb, "host") {
    this.m_tandem.port = StringToInt(arg, 0);
    this.TandemGo(TandemCoopPage.Host);
  } else if Equals(verb, "join") {
    let target = TandemCoop.Resolve(arg);
    if StrLen(target) == 0 {
      TandemNative.Log(s"coop: main menu: not an address: \(arg)");
    } else {
      this.m_tandem.target = target;
      this.TandemGo(TandemCoopPage.Save);
    }
  } else if Equals(verb, "solo") || Equals(verb, "leave") {
    TandemNative.Leave();
    this.TandemGo(TandemCoopPage.Root);
  } else {
    TandemCoop.LogStatus();
    TandemNative.Log(s"coop: main menu page \(TandemCoopMenu.PageName(this.m_tandem.page)):\(this.m_tandem.ItemsText())");
  }
}

// ---- the pause menu ----

@addField(PauseMenuGameController)
public let m_tandem: ref<TandemCoopMenu>;

@wrapMethod(PauseMenuGameController)
protected cb func OnInitialize() -> Bool {
  this.m_tandem = TandemCoopMenu.Create(true);
  let result = wrappedMethod();
  this.m_tandem.CreatePanel(this.GetRootCompoundWidget(), this);
  this.m_tandem.Refresh();
  return result;
}

@wrapMethod(PauseMenuGameController)
protected cb func OnUninitialize() -> Bool {
  if IsDefined(this.m_tandem) {
    this.m_tandem.Stop();
  }
  return wrappedMethod();
}

@wrapMethod(PauseMenuGameController)
private func PopulateMenuItemList() -> Void {
  let root = !IsDefined(this.m_tandem) || Equals(this.m_tandem.page, TandemCoopPage.Root);
  if root {
    wrappedMethod();
  }
  let system = this.CoopSystem();
  if !IsDefined(this.m_tandem) || !IsDefined(system) {
    return;
  }
  this.m_tandem.Build(0, system.InSession());
  for item in this.m_tandem.items {
    this.m_menuListController.PushData(item);
  }
  this.m_menuListController.Refresh();
  if !root {
    this.SetCursorOverWidget(inkCompoundRef.GetWidgetByIndex(this.m_menuList, 0));
  }
}

@wrapMethod(PauseMenuGameController)
protected cb func OnMenuItemActivated(index: Int32, target: ref<ListItemController>) -> Bool {
  let item = target.GetData() as TandemMenuItem;
  if !IsDefined(item) {
    // Back to the main menu: a co-op choice made in game was for this game.
    let data = target.GetData() as PauseMenuListItemData;
    if IsDefined(data) && Equals(data.action, PauseMenuAction.ExitToMainMenu) {
      TandemNative.ResetChoice();
    }
    return wrappedMethod(index, target);
  }
  this.PlaySound(n"Button", n"OnPress");
  this.TandemActivate(item);
  return true;
}

@wrapMethod(PauseMenuGameController)
protected cb func OnGlobalRelease(e: ref<inkPointerEvent>) -> Bool {
  if IsDefined(this.m_tandem) {
    let back = this.m_tandem.Back(e);
    if back > 0 {
      e.Handle();
      if back == 2 {
        this.PlaySound(n"Button", n"OnPress");
        this.TandemGo(this.m_tandem.Up());
      }
      return true;
    }
  }
  return wrappedMethod(e);
}

@addMethod(PauseMenuGameController)
private func CoopSystem() -> ref<TandemSystem> {
  let owner = this.GetPlayerControlledObject();
  if !IsDefined(owner) {
    return null;
  }
  return TandemSystem.Of(owner.GetGame());
}

@addMethod(PauseMenuGameController)
private func TandemGo(page: TandemCoopPage) -> Void {
  if !this.m_tandem.Defer(page, this) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

@addMethod(PauseMenuGameController)
private func TandemShowPage() -> Void {
  this.ShowActionsList();
  this.m_tandem.Refresh();
  TandemNative.Log(s"coop: pause menu page \(TandemCoopMenu.PageName(this.m_tandem.page)):\(this.m_tandem.ItemsText())");
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemDeferred(proxy: ref<inkAnimProxy>) -> Bool {
  if IsDefined(this.m_tandem) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
  return true;
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemTick(proxy: ref<inkAnimProxy>) -> Bool {
  if !IsDefined(this.m_tandem) {
    return false;
  }
  this.m_tandem.Refresh();
  // The world is paused, and our system's tick with it: its commands too.
  let command = TandemNative.TakeCommand();
  if StrLen(command) == 0 {
    return true;
  }
  if StrBeginsWith(command, "coop menu") || StrBeginsWith(command, "coop pick") {
    this.TandemCommand(command);
  } else {
    let system = this.CoopSystem();
    if IsDefined(system) {
      system.RunCommand(command);
    }
  }
  return true;
}

@addMethod(PauseMenuGameController)
private func TandemActivate(item: ref<TandemMenuItem>) -> Void {
  let action = item.coop;
  let system = this.CoopSystem();
  TandemNative.Log(s"coop: pause menu: \(item.label)");
  if !IsDefined(system) {
    return;
  }
  if Equals(action, TandemCoopAction.OpenCoop) {
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Back) {
    this.TandemGo(this.m_tandem.Up());
  } else if Equals(action, TandemCoopAction.HostNow) {
    system.CoopHost(0);
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.JoinPage) {
    this.TandemGo(TandemCoopPage.Join);
  } else if Equals(action, TandemCoopAction.EnterAddress) {
    this.TandemAskAddress();
  } else if Equals(action, TandemCoopAction.JoinRecent) {
    system.CoopJoin(item.target);
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Invite) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Invite"), TandemCoop.InviteText(), GenericMessageNotificationType.OK);
  } else if Equals(action, TandemCoopAction.Leave) {
    system.CoopLeave();
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Recorder) {
    TandemNative.RecorderSet(!TandemNative.RecorderOn());
    this.TandemGo(TandemCoopPage.Coop);
  }
}

@addMethod(PauseMenuGameController)
private func TandemAskAddress() -> Void {
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-EnterCode"), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemAddress");
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemAddress(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  let target = TandemCoop.Resolve(close.input);
  let system = this.CoopSystem();
  TandemNative.Log(s"coop: pause menu: typed \(close.input) -> \(target)");
  if StrLen(target) == 0 || !IsDefined(system) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.F("Popup-NotATarget", close.input), GenericMessageNotificationType.OK);
    return true;
  }
  if !system.CoopJoin(target) && TandemCoop.IsCode(target) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-NeedsRelay"), GenericMessageNotificationType.OK);
  }
  this.TandemGo(TandemCoopPage.Coop);
  return true;
}

// coop menu PAGE | coop pick TEXT, while the pause menu is open (the rest
// of the coop commands are TandemSystem's).
@addMethod(PauseMenuGameController)
private func TandemCommand(command: String) -> Void {
  let words = StrSplit(command, " ");
  let verb = "";
  let arg = "";
  if ArraySize(words) > 1 {
    verb = words[1];
  }
  if ArraySize(words) > 2 {
    arg = StrAfterFirst(command, s"\(verb) ");
  }
  if Equals(verb, "menu") {
    this.TandemGo(TandemCoopMenu.PageOf(arg));
  } else {
    let item = this.m_tandem.Find(arg);
    if IsDefined(item) {
      this.TandemActivate(item);
    } else {
      TandemNative.Log(s"coop: pause menu: no item \(arg) on page \(TandemCoopMenu.PageName(this.m_tandem.page)):\(this.m_tandem.ItemsText())");
    }
  }
}
