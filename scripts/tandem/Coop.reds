// Co-op in the game's own menus (docs/architecture/coop-menu.md, milestone 7).
//
// Main menu (SingleplayerMenuGameController): a "Co-op" entry. Its pages
// live in the menu's own list, the way the menu shows its items:
//   Co-op   -> Host a game | Join a game | Join by address | Play solo (once
//              one is chosen) | Settings | Write a bug report | Back
//   Settings -> one item per setting (Settings.reds): picking it switches or
//              cycles it, or asks for a text; saved to tandem.ini at once
//   Host    -> Continue | Load game | Back      (the game's own load list)
//   Join    -> recent hosts... | Enter an address | Back
//   Save    -> Continue | Load game | Back      (the V to join with)
// Picking Continue or Load game chooses the session in the plugin
// (TandemNative.StartHost / Join); TandemSystem starts it as the save loads.
//
// Join by address (LAN, VPN: Tailscale, NetBird): a text popup asks for
// host[:port], checked (TandemNative.AddressProblem), saved as tandem.ini's
// last_address (an empty answer joins it again), then joined directly, as
// role = join with that address would, without the relay.
//
// Pause menu (PauseMenuGameController): "Co-op" -> Host this game | Join a
// game | Join by address, or, in a session, Invite | Leave session; Settings. These start and stop the
// session at once (TandemSystem.CoopHost / CoopJoin / CoopLeave).
//
// Compatibility (tandem/compat.hpp): when the plugin's startup check found
// a problem (another game version, a missing or old mod), or the game has
// no Phantom Liberty (TandemCoop.ReportExpansion), co-op is off: the
// Co-op page says why in a popup and only offers the bug report. What
// differs between the players' games is said on screen at the join (the
// plugin's notices) and counted in the status panel. "Write a bug report"
// writes the tandem-report zip (TandemNative.WriteReport).
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
  Settings = 5,
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
  Settings = 13,
  Setting = 14,
  Report = 15,
  NewCode = 16,
  JoinAddress = 17,
  Mods = 18,
}

// One of our entries in a menu's list: the vanilla item data (its label is
// what the list shows), and what it does.
public class TandemMenuItem extends PauseMenuListItemData {
  public let coop: TandemCoopAction;
  // Its text's key (Text.reds; "" for a label that is data or vanilla's).
  public let key: String;
  // JoinRecent: "address:port"; Setting: its tandem.ini key
  public let target: String;
}

// What both menus show of the session, and the in-game notices.
public class TandemCoop {
  // The name the player typed, checked and saved (false: refused, logged).
  public static func TakeName(typed: String) -> Bool {
    let problem = TandemNative.NameProblem(typed);
    if StrLen(problem) > 0 {
      TandemNative.Log(s"coop: name refused (\(problem))");
      return false;
    }
    return TandemSettings.Set("name", typed);
  }

  // coop name answer TEXT | coop name cancel (tools/game.sh cmd): answer the
  // open name popup as a player would (scenario names-prompt). True: it was one.
  public static func AnswerName(menu: ref<TandemCoopMenu>, command: String) -> Bool {
    if !StrBeginsWith(command, "coop name answer") && !Equals(command, "coop name cancel") {
      return false;
    }
    if !IsDefined(menu) || !menu.naming || !IsDefined(menu.inputToken) {
      TandemNative.Log("coop: no name popup open");
      return true;
    }
    let close = new GenericMessageNotificationCloseData();
    close.result = Equals(command, "coop name cancel") ? GenericMessageNotificationResult.Cancel : GenericMessageNotificationResult.Confirm;
    close.input = StrAfterFirst(command, "coop name answer ");
    menu.inputToken.TriggerCallback(close);
    return true;
  }

  public static func HostName() -> String {
    for player in TandemNative.SessionPlayers() {
      if player.host {
        return player.name;
      }
    }
    return "";
  }

  // A player of the session by id; one with no name yet as such.
  public static func NameOf(id: Uint32) -> String {
    for player in TandemNative.SessionPlayers() {
      if player.id == id {
        return player.name;
      }
    }
    return TandemText.Unnamed(id);
  }

  // Phantom Liberty, by the game's own check (the one its main menu uses
  // for the expansion banner): told to the plugin, which keeps co-op off
  // without it. Called when a menu opens and when a V enters a game.
  public static func ReportExpansion() -> Void {
    let handler = GameInstance.GetSystemRequestsHandler();
    if !IsDefined(handler) {
      return;
    }
    let state = 3;
    if handler.IsAdditionalContentEnabled(n"EP1") {
      state = 1;
    } else if handler.IsAdditionalContentOwned(n"EP1") {
      state = 2;
    }
    TandemNative.SetExpansion(state);
  }

  // Co-op is off (the game's version, a missing or old mod, no Phantom
  // Liberty): why, as one text for a popup; "" when it is not.
  public static func ProblemsText() -> String {
    let text = "";
    for problem in TandemNative.CompatProblems() {
      if StrLen(text) > 0 {
        text += "\n\n";
      }
      text += problem;
    }
    return text;
  }

  // The bug report, written now: what to tell the player.
  public static func ReportText() -> String {
    let path = TandemNative.WriteReport();
    if StrLen(path) == 0 {
      return "The report could not be written. tandem's log in red4ext/logs says why.";
    }
    return s"Written in the game's folder:\n\(path)\n\nIt holds tandem's log, the end of the redscript and RED4ext logs, the versions of the game and the mods tandem needs, tandem.ini without its relay token and join code, and the last session's summary. Attach it to your bug report.";
  }

  // The session in a few lines, then who is in it.
  public static func StatusLines(opt english: Bool) -> array<String> {
    let lines: array<String>;
    if ArraySize(TandemNative.CompatProblems()) > 0 {
      ArrayPush(lines, "Co-op is off: a startup check failed");
      return lines;
    }
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
          ArrayPush(lines, TandemText.FE(english, TandemNative.RelayCodeKept() ? "Status-CodeKept" : "Status-Code", code));
        } else if StrLen(relay) > 0 && !Equals(relay, "ready") {
          ArrayPush(lines, TandemText.FE(english, "Status-Relay", TandemText.Reason(relay, english)));
        }
        for line in TandemCoop.ConnectionLines(english) {
          ArrayPush(lines, line);
        }
      }
    } else if status.role == 2 {
      if status.state == 0 {
        ArrayPush(lines, TandemText.FE(english, "Status-JoinAtLoad", target));
      } else if status.state == 3 {
        let host = TandemCoop.HostName();
        ArrayPush(lines, StrLen(host) > 0 ? TandemText.FE(english, "Status-InSessionOf", host) : TandemText.FE(english, "Status-InSessionAt", target));
        for line in TandemCoop.ConnectionLines(english) {
          ArrayPush(lines, line);
        }
      } else if status.state == 4 {
        ArrayPush(lines, TandemText.FE(english, "Status-Retrying", target, TandemText.Reason(status.detail, english)));
      } else if status.state == 5 {
        ArrayPush(lines, TandemText.FE(english, "Status-JoinFailed", target));
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
    // What differs from the other players' games: said on screen when it
    // was found, and counted here (the log and the bug report have the words).
    // Each player's mods against ours, in words (Mods.reds).
    if status.state == 3 {
      for player in TandemModsNative.ModPlayers() {
        let differ = TandemMods.Comparison(player, TandemCoop.NameOf(player), english);
        if StrLen(differ) > 0 {
          ArrayPush(lines, differ);
        }
      }
    }
    let differences = ArraySize(TandemNative.CompatWarnings());
    if differences > 0 {
      ArrayPush(lines, s"\(differences) difference(s) between your games: see tandem's log");
    }
    if TandemNative.RecorderOn() {
      ArrayPush(lines, TandemText.FE(english, "Status-Recorder", TandemNative.RecorderStatus()));
    }
    return lines;
  }

  // Through a relay: what the connection check found (direct connections
  // likely, or relay only), and the direct paths up.
  public static func ConnectionLines(english: Bool) -> array<String> {
    let lines: array<String>;
    let check = TandemNative.RelayCheck();
    if Equals(check, "direct") {
      ArrayPush(lines, TandemText.T(english, "Status-CheckDirect"));
    } else if Equals(check, "relay") {
      ArrayPush(lines, TandemText.T(english, "Status-CheckRelay"));
    }
    let direct = TandemNative.RelayDirect();
    if direct > 0 {
      ArrayPush(lines, TandemText.FE(english, "Status-Direct", s"\(direct)"));
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

  // ---- Join by address ----

  // What Join by address joins: what was typed, or the last address for an
  // empty answer.
  public static func DirectText(typed: String) -> String {
    if Equals(TandemNative.AddressProblem(typed), "empty") {
      return TandemNative.Setting("last_address");
    }
    return typed;
  }

  // Its popup's words: what to type, and the last address when there is one.
  public static func DirectPrompt() -> String {
    let text = TandemText.F("Popup-JoinAddress", s"\(TandemNative.DefaultPort())");
    let last = TandemNative.Setting("last_address");
    if StrLen(last) > 0 {
      text += " " + TandemText.F("Popup-JoinAddressLast", last);
    }
    return text;
  }

  // Why an answer is not an address to join, in words ("": it is one).
  public static func DirectRefusal(text: String) -> String {
    let problem = TandemNative.AddressProblem(text);
    if StrLen(problem) == 0 {
      return "";
    }
    if Equals(problem, "empty") {
      return TandemText.Get("Address-Empty");
    }
    if Equals(problem, "port") {
      return TandemText.F("Address-Port", text);
    }
    if Equals(problem, "ipv6") {
      return TandemText.F("Address-Ipv6", text);
    }
    return TandemText.F("Address-Name", text);
  }

  // The answer: "address:port" to join, saved as last_address; "" when it
  // is none (DirectRefusal of DirectText says why).
  public static func TakeAddress(typed: String) -> String {
    let text = TandemCoop.DirectText(typed);
    let target = TandemCoop.Target(text);
    if StrLen(target) == 0 {
      TandemNative.Log(s"coop: join by address: \"\(typed)\" refused (\(TandemNative.AddressProblem(text)))");
      return "";
    }
    TandemNative.Log(s"coop: join by address: \"\(typed)\" -> \(target)");
    TandemSettings.Set("last_address", target);
    return target;
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
    for problem in TandemNative.CompatProblems() {
      TandemNative.Log(s"coop: off: \(problem)");
    }
    for warning in TandemNative.CompatWarnings() {
      TandemNative.Log(s"coop: differs: \(warning)");
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
  // The setting a text popup asks for.
  public let settingKey: String;
  // The name popup is open (the first co-op use with the default name), and
  // what was picked before it, to go on with once it is answered.
  public let naming: Bool;
  public let nameFor: TandemCoopAction;
  // A popup closed with Esc: the menu still gets its release (clock ticks).
  public let swallowBack: Int32;

  public static func Create(inGame: Bool) -> ref<TandemCoopMenu> {
    TandemCoop.ReportExpansion();
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
    if Equals(page, TandemCoopPage.Settings) {
      return "settings";
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
    if Equals(name, "settings") {
      return TandemCoopPage.Settings;
    }
    return TandemCoopPage.Root;
  }

  // The page Back (and Esc) leads to.
  public func Up() -> TandemCoopPage {
    if Equals(this.page, TandemCoopPage.Host) || Equals(this.page, TandemCoopPage.Join) || Equals(this.page, TandemCoopPage.Settings) {
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
    } else if Equals(this.page, TandemCoopPage.Settings) {
      for key in TandemSettings.Keys() {
        let item = this.Add("", TandemCoopAction.Setting, TandemSettings.Label(key, inSession || TandemNative.IsStarted()));
        item.target = key;
      }
    } else if ArraySize(TandemNative.CompatProblems()) > 0 {
      // Co-op is off: nothing to host or join, only the report.
      this.Add("Menu-Report", TandemCoopAction.Report);
    } else if this.inGame {
      if inSession {
        if TandemNative.SessionStatus().role == 1 {
          this.Add("Menu-Invite", TandemCoopAction.Invite);
          // Another join code (a kept one: the next, kept from now on).
          if StrLen(TandemNative.RelayCode()) > 0 {
            this.Add("Menu-NewCode", TandemCoopAction.NewCode);
          }
        }
        this.Add("Menu-Mods", TandemCoopAction.Mods);
        this.Add("Menu-Leave", TandemCoopAction.Leave);
      } else {
        this.Add("Menu-HostNow", TandemCoopAction.HostNow);
        this.Add("Menu-Join", TandemCoopAction.JoinPage);
        this.Add("Menu-JoinAddress", TandemCoopAction.JoinAddress);
      }
      this.Add("Menu-Report", TandemCoopAction.Report);
    } else if Equals(this.page, TandemCoopPage.Coop) {
      this.Add("Menu-Host", TandemCoopAction.HostPage);
      this.Add("Menu-Join", TandemCoopAction.JoinPage);
      this.Add("Menu-JoinAddress", TandemCoopAction.JoinAddress);
      if TandemNative.SessionStatus().fromMenu {
        this.Add("Menu-Solo", TandemCoopAction.Solo);
      }
      this.Add("Menu-Report", TandemCoopAction.Report);
    } else {
      // Host, Save: the V to play.
      if saves > 0 {
        this.Add("", TandemCoopAction.Continue, TandemText.Vanilla("UI-ScriptExports-Continue0"));
      }
      this.Add("", TandemCoopAction.LoadGame, TandemText.Vanilla("UI-ScriptExports-LoadGame0"));
    }
    // The settings (the recorder among them), the game's own word for it.
    if Equals(this.page, TandemCoopPage.Coop) {
      this.Add("Menu-Settings", TandemCoopAction.Settings, TandemText.Vanilla("UI-Labels-Settings"));
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
      } else if Equals(this.page, TandemCoopPage.Settings) {
        ArrayPush(lines, TandemText.Get("Settings-Saved"));
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
    let problems = TandemCoop.ProblemsText();
    if StrLen(problems) > 0 {
      GenericMessageNotification.Show(this, TandemText.Get("Popup-CoopOff"), problems, GenericMessageNotificationType.OK);
    }
  } else if Equals(action, TandemCoopAction.Report) {
    GenericMessageNotification.Show(this, TandemText.Get("Popup-Report"), TandemCoop.ReportText(), GenericMessageNotificationType.OK);
  } else if Equals(action, TandemCoopAction.Back) {
    this.TandemGo(this.m_tandem.Up());
  } else if Equals(action, TandemCoopAction.HostPage) {
    if !this.TandemAskName(action) {
      this.m_tandem.port = 0;
      this.TandemGo(TandemCoopPage.Host);
    }
  } else if Equals(action, TandemCoopAction.JoinPage) {
    if !this.TandemAskName(action) {
      this.TandemGo(TandemCoopPage.Join);
    }
  } else if Equals(action, TandemCoopAction.JoinAddress) {
    if !this.TandemAskName(action) {
      this.TandemAskDirect();
    }
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
  } else if Equals(action, TandemCoopAction.Settings) {
    this.TandemGo(TandemCoopPage.Settings);
  } else if Equals(action, TandemCoopAction.Setting) {
    if TandemSettings.Kind(item.target) == 2 {
      this.TandemAskSetting(item.target);
    } else {
      TandemSettings.Set(item.target, TandemSettings.Next(item.target));
      this.TandemGo(TandemCoopPage.Settings);
    }
  }
  return true;
}

// A typed setting: the game's message popup with Codeware's text field.
@addMethod(SingleplayerMenuGameController)
private func TandemAskSetting(key: String) -> Void {
  this.m_tandem.settingKey = key;
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemSettings.Title(key), TandemSettings.Prompt(key), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemSetting");
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemSetting(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  if !TandemSettings.Set(this.m_tandem.settingKey, close.input) {
    GenericMessageNotification.Show(this, TandemSettings.Title(this.m_tandem.settingKey), TandemSettings.Refusal(this.m_tandem.settingKey, close.input), GenericMessageNotificationType.OK);
  }
  this.TandemGo(TandemCoopPage.Settings);
  return true;
}

// The first co-op use with the default name (V): the game's message popup
// with Codeware's text field asks for one, once a game run, before the page
// the player picked. False: no need, go on.
@addMethod(SingleplayerMenuGameController)
private func TandemAskName(then: TandemCoopAction) -> Bool {
  if !TandemNative.NameToAsk() {
    return false;
  }
  TandemNative.NameAsked(true);
  this.m_tandem.nameFor = then;
  this.TandemNamePopup(TandemText.Get("Name-Ask"));
  TandemNative.Log("coop: main menu: asking for a name (still the default)");
  return true;
}

@addMethod(SingleplayerMenuGameController)
private func TandemNamePopup(message: String) -> Void {
  this.m_tandem.naming = true;
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Name-Title"), message, GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemName");
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemName(data: ref<inkGameNotificationData>) -> Bool {
  if !this.m_tandem.naming {
    return true; // answered already (coop name answer)
  }
  this.m_tandem.inputToken = null;
  this.m_tandem.naming = false;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if Equals(close.result, GenericMessageNotificationResult.Confirm) {
    if !TandemCoop.TakeName(close.input) {
      // Asked again, saying why: one popup at a time.
      this.TandemNamePopup(TandemSettings.Refusal("name", close.input));
      return true;
    }
  } else {
    // Cancelled: V for this game run (asked again at the next launch).
    TandemNative.Log("coop: name prompt cancelled: the default for now");
    this.m_tandem.swallowBack = 2;
  }
  // On to the page the player picked.
  if Equals(this.m_tandem.nameFor, TandemCoopAction.HostPage) {
    this.m_tandem.port = 0;
    this.TandemGo(TandemCoopPage.Host);
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.JoinAddress) {
    this.TandemAskDirect();
  } else {
    this.TandemGo(TandemCoopPage.Join);
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

// Join by address: the popup, then the save to join with.
@addMethod(SingleplayerMenuGameController)
private func TandemAskDirect() -> Void {
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-JoinAddress"), TandemCoop.DirectPrompt(), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemDirect");
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemDirect(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  this.TandemJoinDirect(close.input);
  return true;
}

@addMethod(SingleplayerMenuGameController)
private func TandemJoinDirect(typed: String) -> Void {
  let target = TandemCoop.TakeAddress(typed);
  if StrLen(target) == 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-JoinAddress"), TandemCoop.DirectRefusal(TandemCoop.DirectText(typed)), GenericMessageNotificationType.OK);
    return;
  }
  this.m_tandem.target = target;
  this.TandemGo(TandemCoopPage.Save);
}

// tools/game.sh cmd coop ...: coop menu PAGE | coop pick TEXT | coop host
// [PORT] | coop join ADDRESS | coop address [TEXT] | coop solo | coop status.
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
  } else if Equals(verb, "address") {
    // Join by address's answer, without the popup (empty: the last one).
    this.TandemJoinDirect(arg);
  } else if Equals(verb, "solo") || Equals(verb, "leave") {
    TandemNative.Leave();
    this.TandemGo(TandemCoopPage.Root);
  } else if Equals(verb, "report") {
    TandemNative.Log(s"coop: report: \(TandemNative.WriteReport())");
  } else if TandemCoop.AnswerName(this.m_tandem, command) {
  } else if TandemSettings.Command(command) {
    if Equals(this.m_tandem.page, TandemCoopPage.Settings) {
      this.TandemGo(TandemCoopPage.Settings);
    }
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
  } else if TandemCoop.AnswerName(this.m_tandem, command) {
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
    let problems = TandemCoop.ProblemsText();
    if StrLen(problems) > 0 {
      GenericMessageNotification.Show(this, TandemText.Get("Popup-CoopOff"), problems, GenericMessageNotificationType.OK);
    }
  } else if Equals(action, TandemCoopAction.Report) {
    GenericMessageNotification.Show(this, TandemText.Get("Popup-Report"), TandemCoop.ReportText(), GenericMessageNotificationType.OK);
  } else if Equals(action, TandemCoopAction.Back) {
    this.TandemGo(this.m_tandem.Up());
  } else if Equals(action, TandemCoopAction.HostNow) {
    if !this.TandemAskName(action) {
      system.CoopHost(0);
      this.TandemGo(TandemCoopPage.Coop);
    }
  } else if Equals(action, TandemCoopAction.JoinPage) {
    if !this.TandemAskName(action) {
      this.TandemGo(TandemCoopPage.Join);
    }
  } else if Equals(action, TandemCoopAction.JoinAddress) {
    if !this.TandemAskName(action) {
      this.TandemAskDirect();
    }
  } else if Equals(action, TandemCoopAction.EnterAddress) {
    this.TandemAskAddress();
  } else if Equals(action, TandemCoopAction.JoinRecent) {
    system.CoopJoin(item.target);
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Invite) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Invite"), TandemCoop.InviteText(), GenericMessageNotificationType.OK);
  } else if Equals(action, TandemCoopAction.Mods) {
    // The names come when the others answer: said in a notice, shown here
    // the next time.
    TandemMods.AskNames();
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Mods"), TandemMods.MenuText(), GenericMessageNotificationType.OK);
  } else if Equals(action, TandemCoopAction.NewCode) {
    TandemNative.RelayNewCode();
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Leave) {
    system.CoopLeave();
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(action, TandemCoopAction.Settings) {
    this.TandemGo(TandemCoopPage.Settings);
  } else if Equals(action, TandemCoopAction.Setting) {
    if TandemSettings.Kind(item.target) == 2 {
      this.TandemAskSetting(item.target);
    } else {
      TandemSettings.Set(item.target, TandemSettings.Next(item.target));
      this.TandemGo(TandemCoopPage.Settings);
    }
  }
}

@addMethod(PauseMenuGameController)
private func TandemAskSetting(key: String) -> Void {
  this.m_tandem.settingKey = key;
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemSettings.Title(key), TandemSettings.Prompt(key), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemSetting");
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemSetting(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  if !TandemSettings.Set(this.m_tandem.settingKey, close.input) {
    GenericMessageNotification.Show(this, TandemSettings.Title(this.m_tandem.settingKey), TandemSettings.Refusal(this.m_tandem.settingKey, close.input), GenericMessageNotificationType.OK);
  }
  this.TandemGo(TandemCoopPage.Settings);
  return true;
}


// The first co-op use with the default name (V): asked before hosting or
// the join page (as in the main menu). False: no need, go on.
@addMethod(PauseMenuGameController)
private func TandemAskName(then: TandemCoopAction) -> Bool {
  if !TandemNative.NameToAsk() {
    return false;
  }
  TandemNative.NameAsked(true);
  this.m_tandem.nameFor = then;
  this.TandemNamePopup(TandemText.Get("Name-Ask"));
  TandemNative.Log("coop: pause menu: asking for a name (still the default)");
  return true;
}

@addMethod(PauseMenuGameController)
private func TandemNamePopup(message: String) -> Void {
  this.m_tandem.naming = true;
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Name-Title"), message, GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemName");
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemName(data: ref<inkGameNotificationData>) -> Bool {
  if !this.m_tandem.naming {
    return true;
  }
  this.m_tandem.inputToken = null;
  this.m_tandem.naming = false;
  let close = data as GenericMessageNotificationCloseData;
  let system = this.CoopSystem();
  if !IsDefined(close) || !IsDefined(system) {
    return false;
  }
  if Equals(close.result, GenericMessageNotificationResult.Confirm) {
    if !TandemCoop.TakeName(close.input) {
      this.TandemNamePopup(TandemSettings.Refusal("name", close.input));
      return true;
    }
  } else {
    TandemNative.Log("coop: name prompt cancelled: the default for now");
    this.m_tandem.swallowBack = 2;
  }
  if Equals(this.m_tandem.nameFor, TandemCoopAction.HostNow) {
    system.CoopHost(0);
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.JoinAddress) {
    this.TandemAskDirect();
  } else {
    this.TandemGo(TandemCoopPage.Join);
  }
  return true;
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

// Join by address: the popup, then joined at once.
@addMethod(PauseMenuGameController)
private func TandemAskDirect() -> Void {
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-JoinAddress"), TandemCoop.DirectPrompt(), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemDirect");
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemDirect(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandem.inputToken = null;
  let close = data as GenericMessageNotificationCloseData;
  let system = this.CoopSystem();
  if !IsDefined(close) || !IsDefined(system) {
    return false;
  }
  if NotEquals(close.result, GenericMessageNotificationResult.Confirm) {
    this.m_tandem.swallowBack = 2;
    return true;
  }
  let refusal = system.CoopJoinDirect(close.input);
  if StrLen(refusal) > 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-JoinAddress"), refusal, GenericMessageNotificationType.OK);
    return true;
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
