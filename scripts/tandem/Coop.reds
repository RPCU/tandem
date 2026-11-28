// Co-op in the game's own menus (docs/architecture/coop-menu.md, milestone 7).
//
// Main menu (SingleplayerMenuGameController): a "Co-op" entry. Its pages
// live in the menu's own list, the way the menu shows its items:
//   Co-op   -> Continue co-op campaign (when one is known) | Start a new
//              co-op game | Host a saved game | Join a game | Join by
//              address | Play solo (once one is chosen) | Write a bug
//              report | Co-op settings | Back
//   Settings -> one item per setting (Settings.reds: the name, the host's
//              rules but on a guest in a session, the player's own
//              screen) | Back: picking it switches or cycles it, or asks
//              for a text; saved to tandem.ini at once
//   Host    -> Continue | Load game | Back      (the game's own load list)
//   Join    -> recent hosts (by address, or by code: today's code asked
//              for)... | Enter a join code | Back (no recent host:
//              straight to the code's popup)
//   Save    -> Your V from last time (a known host) | Create a new V |
//              Continue | Load game | Back (the V to join with)
// A co-op campaign (a world the co-op menu hosted; docs/architecture/
// coop-menu.md, "Campaigns") opens to friends as it loads, by the host's
// rule (System.reds OpenCampaign); campaigns.txt knows which saves are one,
// and which V each host was last joined with (CampaignNatives.reds).
// Picking Continue or Load game chooses the session in the plugin
// (TandemNative.StartHost / Join); TandemSystem starts it as the save loads.
// Start a new co-op game and Create a new V choose it the same way, then
// open the game's own New Game (difficulty, lifepath, character creation:
// SpawnEvent(n"OnNewGame"), as the menu's New game does); the session
// starts as the new V's world loads. A join chosen in the menu arrives
// beside the host at its first welcome (TandemNative.ChooseArrival,
// Travel.reds Arrive), behind the fast travel's loading screen.
//
// Join by address (LAN, VPN: Tailscale, NetBird): a text popup asks for
// host[:port], checked (TandemNative.AddressProblem), saved as tandem.ini's
// last_address (an empty answer joins it again), then joined directly, as
// role = join with that address would, without the relay.
//
// Pause menu (PauseMenuGameController): "Co-op" -> Host this game | Join a
// game | Join by address, or, in a session, Invite | Partners' mods | Leave
// session; Co-op settings. These start and stop the
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
  NewHost = 19,
  NewV = 20,
  ContinueCampaign = 21,
  LastV = 22,
}

// One of our entries in a menu's list: the vanilla item data (its label is
// what the list shows), and what it does.
public class TandemMenuItem extends PauseMenuListItemData {
  public let coop: TandemCoopAction;
  // Its text's key (Text.reds; "" for a label that is data or vanilla's).
  public let key: String;
  // JoinRecent: "address:port" ("" for a join code); Setting: its tandem.ini key
  public let target: String;
  // JoinRecent: the host's key (TandemHost.key), its name and its last join
  // code ("" by address)
  public let hostKey: String;
  public let hostName: String;
  public let code: String;
  // JoinRecent: its host ID on the relay ("" not known), and what the relay
  // said of it (TandemNative.HostPresenceOf: 2 joinable, 1 online, 0
  // offline, -1 not known).
  public let hostId: String;
  public let presence: Int32;
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
      // (A core feature that is off: in the player's language, below.)
      if !StrBeginsWith(problem, "This game cannot run co-op: ") {
        if StrLen(text) > 0 {
          text += "\n\n";
        }
        text += problem;
      }
    }
    let core = TandemDegradedSay.CoreText();
    if StrLen(core) > 0 {
      text += StrLen(text) > 0 ? "\n\n" + core : core;
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
    let problems = TandemNative.CompatProblems();
    if ArraySize(problems) > 0 {
      ArrayPush(lines, "Co-op is off: a startup check failed");
      for line in TandemDegradedSay.StatusLines(english) {
        ArrayPush(lines, line);
      }
      return lines;
    }
    let status = TandemNative.SessionStatus();
    let target = StrLen(status.joinCode) > 0 ? TandemText.FE(english, "Status-CodeTarget", status.joinCode) : s"\(status.address):\(status.port)";
    if StrLen(status.hostId) > 0 {
      target = TandemCoop.NameOfHostId(status.hostId);
    }
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
        // The host decides them: a guest reads them here, its page has
        // no row for them (Settings.reds).
        ArrayPush(lines, TandemSettings.HostRulesLine(english));
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
    let warnings = TandemNative.CompatWarnings();
    let differences = ArraySize(warnings);
    if differences > 0 {
      ArrayPush(lines, s"\(differences) difference(s) between your games: see tandem's log");
    }
    // What this game runs without, the fallbacks it took, and what the other
    // players' games said (Degraded.reds): kept here after their notices.
    for line in TandemDegradedSay.StatusLines(english) {
      ArrayPush(lines, line);
    }
    if TandemNative.RecorderOn() {
      ArrayPush(lines, TandemText.FE(english, "Status-Recorder", TandemNative.RecorderStatus()));
    }
    return lines;
  }

  // A co-op campaign's line on the pause menu: whether friends can drop in.
  public static func CampaignLine(status: TandemStatus) -> String {
    if status.role == 1 && status.state == 3 {
      return TandemText.Get(Equals(TandemNative.Setting("campaign"), "invite") ? "Status-CampaignInvite" : "Status-CampaignOpen");
    }
    return TandemText.Get("Status-CampaignClosed");
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
    // A code reaches everyone, on the network or not: nothing else to say
    // (the addresses are Join by address's, for those who look for it).
    if StrLen(code) > 0 {
      return TandemText.FE(english, "Invite-Code", code);
    }
    if ArraySize(addresses) == 0 {
      return TandemText.FE(english, "Invite-NoAddress", s"\(port)");
    }
    let targets: array<String>;
    for address in addresses {
      ArrayPush(targets, s"\(address):\(port)");
    }
    return TandemText.FE(english, "Invite-Addresses", TandemText.AnyOf(targets, english));
  }

  // A recent host's name by its host ID ("" not known).
  public static func NameOfHostId(id: String) -> String {
    let recent = TandemNative.RecentHosts();
    for host in recent {
      if Equals(host.hostId, id) {
        return host.name;
      }
    }
    return "";
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

  // What campaigns.txt knows the host of this target by: an address is its
  // own key ("address:port"); a code, the recent host's that came with it
  // ("" when none did).
  public static func KeyOf(target: String) -> String {
    if !TandemCoop.IsCode(target) {
      return target;
    }
    let wanted = StrReplaceAll(StrUpper(target), "-", "");
    let recent = TandemNative.RecentHosts();
    for host in recent {
      if StrLen(host.code) > 0 && Equals(StrReplaceAll(StrUpper(host.code), "-", ""), wanted) {
        return host.key;
      }
    }
    return "";
  }

  // The code's popup for a recent host joined by code: whose code, and its
  // last one as the empty answer.
  public static func CodeOfPrompt(menu: ref<TandemCoopMenu>) -> String {
    return TandemText.F("Popup-EnterCodeOf", menu.hostName) + " " + TandemText.F("Popup-EnterCodeLast", menu.lastCode);
  }

  // What the code's popup answered: the recent host's last code for an
  // empty answer.
  public static func Typed(menu: ref<TandemCoopMenu>, input: String) -> String {
    if StrLen(input) == 0 && StrLen(menu.lastCode) > 0 {
      return menu.lastCode;
    }
    return input;
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
      // Through a relay the join code says it all, once the relay gives it
      // (TandemSystem.ShowCode); the addresses only without one.
      if StrLen(TandemNative.Setting("relay")) > 0 {
        return;
      }
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
  // The host picked, as campaigns.txt knows it (its V from last time): ""
  // for a code typed afresh. A recent host joined by code: its name and
  // last code, for the code's popup.
  public let hostKey: String;
  public let hostName: String;
  public let lastCode: String;
  // A recent host joined by its ID (joinable: no code), else "".
  public let joinHostId: String;
  // The save the page offered first when it was built (Offered).
  public let builtOffer: Int32;
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
  // The row the cursor goes to when the page shows (the setting just
  // changed), and the next page's.
  public let cursor: Int32;
  public let nextCursor: Int32;
  // In a session when the page was built (when a setting takes effect).
  public let sessionNow: Bool;
  // The description of the row under the cursor (the Settings page): its
  // title, what it does, when a change takes effect.
  public let aboutTitle: String;
  public let about: String;
  public let aboutNote: String;
  public let aboutBox: wref<inkVerticalPanel>;
  public let aboutHead: wref<inkText>;
  public let aboutBody: wref<inkText>;
  public let aboutFoot: wref<inkText>;

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
    this.builtOffer = this.Offered();
    if Equals(this.page, TandemCoopPage.Root) {
      this.Add("Menu-Coop", TandemCoopAction.OpenCoop);
      return;
    }
    let problems = TandemNative.CompatProblems();
    let status = TandemNative.SessionStatus();
    if Equals(this.page, TandemCoopPage.Join) {
      // The hosts joined before, by address or by a join code (known by
      // their name: a code changes from session to session).
      let recent = TandemNative.RecentHosts();
      for host in recent {
        let address = s"\(host.address):\(host.port)";
        let label = StrLen(host.name) > 0 ? TandemText.F("Menu-Recent", host.name, address) : address;
        if StrLen(host.code) > 0 {
          label = TandemText.F("Menu-RecentCode", host.name);
          address = "";
        }
        // Its state, from the relay, once it answered (QueryPresence).
        let presence = StrLen(host.hostId) > 0 ? TandemNative.HostPresenceOf(host.hostId) : -1;
        if presence == 2 {
          label = TandemText.F("Menu-RecentJoinable", host.name);
        } else if presence == 1 {
          label = TandemText.F("Menu-RecentOnline", host.name);
        } else if presence == 0 {
          label = TandemText.F("Menu-RecentOffline", host.name);
        }
        let item = this.Add("", TandemCoopAction.JoinRecent, label);
        item.target = address;
        item.code = host.code;
        item.hostKey = host.key;
        item.hostName = host.name;
        item.hostId = host.hostId;
        item.presence = presence;
      }
      this.Add("Menu-EnterCode", TandemCoopAction.EnterAddress);
    } else if Equals(this.page, TandemCoopPage.Settings) {
      this.sessionNow = inSession || TandemNative.IsStarted();
      let rows = TandemSettings.Rows();
      for key in rows {
        let item = this.Add("", TandemCoopAction.Setting, TandemSettings.Label(key));
        item.target = key;
      }
    } else if ArraySize(problems) > 0 {
      // Co-op is off: nothing to host or join, only the report.
      this.Add("Menu-Report", TandemCoopAction.Report);
    } else if this.inGame {
      if inSession {
        if status.role == 1 {
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
      // The co-op campaign played last: one click back into it, hosting.
      let campaign = TandemCampaignNative.CampaignSave();
      if campaign.index >= 0 {
        this.Add("Menu-ContinueCampaign", TandemCoopAction.ContinueCampaign);
      }
      this.Add("Menu-NewHost", TandemCoopAction.NewHost);
      this.Add("Menu-Host", TandemCoopAction.HostPage);
      this.Add("Menu-Join", TandemCoopAction.JoinPage);
      this.Add("Menu-JoinAddress", TandemCoopAction.JoinAddress);
      if status.fromMenu {
        this.Add("Menu-Solo", TandemCoopAction.Solo);
      }
      this.Add("Menu-Report", TandemCoopAction.Report);
    } else {
      // Host, Save: the V to play (a guest may make a new one).
      if Equals(this.page, TandemCoopPage.Save) {
        // The V this host was last joined with first: one click.
        let last = TandemCampaignNative.LastV(this.hostKey);
        if StrLen(this.hostKey) > 0 && last.index >= 0 {
          this.Add("Menu-LastV", TandemCoopAction.LastV, TandemText.F("Menu-LastV", s"\(last.level)", last.location));
        }
        this.Add("Menu-NewV", TandemCoopAction.NewV);
      }
      if saves > 0 {
        this.Add("", TandemCoopAction.Continue, TandemText.Vanilla("UI-ScriptExports-Continue0"));
      }
      this.Add("", TandemCoopAction.LoadGame, TandemText.Vanilla("UI-ScriptExports-LoadGame0"));
    }
    // The settings (the recorder among them): "Co-op settings", not the
    // game's own word, which its Settings entry already uses.
    if Equals(this.page, TandemCoopPage.Coop) {
      this.Add("Menu-Settings", TandemCoopAction.Settings);
    }
    this.Add("Menu-Back", TandemCoopAction.Back);
  }

  // The save the main menu's page offers first, its index (-1 none): on the
  // Co-op page the co-op campaign, on the V page a known host's V from last
  // time. The saves' metadata comes after the menu opens: a page built
  // before it is built again when it does (the menu's tick).
  public func Offered() -> Int32 {
    let problems = TandemNative.CompatProblems();
    if ArraySize(problems) > 0 {
      return -1;
    }
    // The Join list, either menu: its hosts' states, as one number.
    if Equals(this.page, TandemCoopPage.Join) {
      let sum = 0;
      let recent = TandemNative.RecentHosts();
      for host in recent {
        sum = sum * 4 + (StrLen(host.hostId) > 0 ? TandemNative.HostPresenceOf(host.hostId) + 1 : 0);
      }
      return sum;
    }
    if this.inGame {
      return -1;
    }
    if Equals(this.page, TandemCoopPage.Coop) {
      let campaign = TandemCampaignNative.CampaignSave();
      return campaign.index;
    }
    if Equals(this.page, TandemCoopPage.Save) && StrLen(this.hostKey) > 0 {
      let last = TandemCampaignNative.LastV(this.hostKey);
      return last.index;
    }
    return -1;
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

  // On our pages the list holds our items alone. Other wraps of
  // PopulateMenuItemList add theirs after ours (the Mod Settings mod appends
  // its entry when the game's Mods entry is missing, as on our pages), and
  // ShowActionsList its Close game: dropped here. True: the list was rebuilt.
  public func Tidy(list: wref<ListController>) -> Bool {
    this.aboutTitle = "";
    this.about = "";
    this.aboutNote = "";
    if !IsDefined(list) || Equals(this.page, TandemCoopPage.Root) {
      return false;
    }
    let rebuilt = false;
    if list.Size() != ArraySize(this.items) {
      list.Clear();
      for item in this.items {
        list.PushData(item);
      }
      list.Refresh();
      rebuilt = true;
    }
    if Equals(this.page, TandemCoopPage.Settings) {
      let target = this.Pointed(list);
      if StrLen(target) == 0 {
        return rebuilt;
      }
      this.aboutTitle = TandemSettings.Title(target);
      this.about = TandemSettings.About(target);
      this.aboutNote = TandemSettings.Note(target, this.sessionNow);
    }
    return rebuilt;
  }

  // The row the cursor goes to as the page shows (within the list).
  public func Cursor() -> Int32 {
    return this.cursor >= 0 && this.cursor < ArraySize(this.items) ? this.cursor : 0;
  }

  // The index of the row that stands for this target (a setting's key).
  public func RowOf(target: String) -> Int32 {
    let i = 0;
    while i < ArraySize(this.items) {
      if Equals(this.items[i].target, target) {
        return i;
      }
      i += 1;
    }
    return 0;
  }

  // The setting key of the row under the mouse, else of the selected one.
  private func Pointed(list: wref<ListController>) -> String {
    let i = 0;
    while i < list.Size() {
      let widget = list.GetItemAt(i);
      let row = IsDefined(widget) ? widget.GetController() as ListItemController : null;
      if IsDefined(row) && Equals(row.GetState(), inkEButtonState.Hover) {
        let data = row.GetData() as TandemMenuItem;
        return IsDefined(data) ? data.target : "";
      }
      i += 1;
    }
    let selected = list.HasValidSelection() ? list.GetSelectedIndex() : -1;
    return selected >= 0 && selected < ArraySize(this.items) ? this.items[selected].target : "";
  }

  public func ItemsText() -> String {
    let text = "";
    for item in this.items {
      text += s" [\(item.label)]";
    }
    return text;
  }

  // What the page shows, read back from its widgets (coop menu shown: the
  // tests read the screen, not only our items): each row as its list item
  // draws it against the row we built ("as built", or each one that
  // differs), then each status line drawn.
  public func LogShown(list: wref<ListController>) -> Void {
    let which = this.inGame ? "pause" : "main";
    let page = TandemCoopMenu.PageName(this.page);
    let size = IsDefined(list) ? list.Size() : 0;
    let built = ArraySize(this.items);
    let differ = 0;
    if size != built {
      TandemNative.Log(s"coop: \(which) menu draws \(size) rows on page \(page), built \(built)");
      differ += 1;
    }
    let i = 0;
    while i < size && i < built {
      let widget = list.GetItemAt(i);
      let row = IsDefined(widget) ? widget.GetController() as ListItemController : null;
      let drawn = IsDefined(row) ? inkTextRef.GetText(row.m_labelPathRef) : "";
      let item = this.items[i];
      if NotEquals(drawn, item.label) {
        TandemNative.Log(s"coop: \(which) menu row \(i) draws \"\(drawn)\", built \"\(item.label)\"");
        differ += 1;
      }
      i += 1;
    }
    if differ == 0 {
      TandemNative.Log(s"coop: \(which) menu shows page \(page) as built: \(size) rows");
    }
    for line in this.lines {
      if IsDefined(line) && line.IsVisible() {
        TandemNative.Log(s"coop: \(which) menu status: \(line.GetText())");
      }
    }
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
    // The Settings page's description, apart from the status above as the
    // game's settings show theirs: a rule, the row's name, what it does
    // (the game's settings info grey), when a change takes effect.
    let box = new inkVerticalPanel();
    box.SetName(n"tandem_about");
    box.SetFitToContent(true);
    box.SetHAlign(inkEHorizontalAlign.Right);
    box.SetMargin(inkMargin(0.0, 56.0, 0.0, 0.0));
    box.SetVisible(false);
    box.Reparent(panel);
    this.aboutBox = box;
    let rule = new inkRectangle();
    rule.SetName(n"tandem_about_rule");
    rule.SetSize(Vector2(1100.0, 2.0));
    rule.SetStyle(r"base\\gameplay\\gui\\common\\main_colors.inkstyle");
    rule.BindProperty(n"tintColor", n"MainColors.MildRed");
    rule.SetMargin(inkMargin(0.0, 0.0, 0.0, 24.0));
    rule.Reparent(box);
    let parts = 0;
    while parts < 3 {
      let part = new inkText();
      part.SetName(parts == 0 ? n"tandem_about_title" : (parts == 1 ? n"tandem_about_text" : n"tandem_about_note"));
      part.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
      part.SetFontStyle(parts == 0 ? n"Medium" : n"Regular");
      part.SetFontSize(parts == 0 ? 46 : 38);
      part.SetLetterCase(parts == 0 ? textLetterCase.UpperCase : textLetterCase.OriginalCase);
      part.SetStyle(r"base\\gameplay\\gui\\common\\main_colors.inkstyle");
      part.BindProperty(n"tintColor", parts == 0 ? n"MainColors.White" : (parts == 1 ? n"MainColors.Grey" : n"MainColors.Yellow"));
      part.SetFitToContent(true);
      part.SetWrapping(true, 1100.0);
      part.SetHAlign(inkEHorizontalAlign.Left);
      part.SetHorizontalAlignment(textHorizontalAlignment.Left);
      part.SetMargin(inkMargin(0.0, parts == 2 ? 24.0 : 0.0, 0.0, parts == 0 ? 12.0 : 0.0));
      part.Reparent(box);
      if parts == 0 {
        this.aboutHead = part;
      } else if parts == 1 {
        this.aboutBody = part;
      } else {
        this.aboutFoot = part;
      }
      parts += 1;
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
  public func Defer(page: TandemCoopPage, owner: ref<IScriptable>, opt cursor: Int32) -> Bool {
    this.nextPage = page;
    this.nextCursor = cursor;
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
    // The Join list opens: the relay asked about its hosts once (each
    // answer rebuilds it: the menus' tick, Offered).
    if Equals(this.nextPage, TandemCoopPage.Join) && NotEquals(this.page, TandemCoopPage.Join) {
      TandemNative.QueryPresence();
    }
    this.page = this.nextPage;
    this.cursor = this.nextCursor;
    return true;
  }

  // Back (and Esc): up a page (Up).
  public func DeferUp(owner: ref<IScriptable>) -> Bool {
    return this.Defer(this.Up(), owner);
  }

  public func Refresh() -> Void {
    let lines: array<String>;
    let status = TandemNative.SessionStatus();
    // The root page: only a session chosen or running is news, or the
    // co-op campaign this world is.
    let campaign = this.inGame && TandemCampaignNative.IsCampaign() && status.role != 2;
    if NotEquals(this.page, TandemCoopPage.Root) || status.role != 0 || campaign {
      ArrayPush(lines, TandemText.Get("Menu-Coop"));
      if campaign {
        ArrayPush(lines, TandemCoop.CampaignLine(status));
      }
      for line in TandemCoop.StatusLines() {
        ArrayPush(lines, line);
      }
      if !this.inGame && Equals(this.page, TandemCoopPage.Coop) {
        let save = TandemCampaignNative.CampaignSave();
        if save.index >= 0 {
          ArrayPush(lines, TandemText.F("Status-Campaign", s"\(save.level)", save.location));
        }
      }
      if Equals(this.page, TandemCoopPage.Host) {
        ArrayPush(lines, TandemText.Get("Status-ChooseHostSave"));
      } else if Equals(this.page, TandemCoopPage.Save) {
        let last = TandemCampaignNative.LastV(this.hostKey);
        let shown = StrLen(this.hostName) > 0 ? this.hostName : TandemCoop.Shown(this.target);
        ArrayPush(lines, TandemText.F(StrLen(this.hostKey) > 0 && last.index >= 0 ? "Status-ChooseJoinSaveLast" : "Status-ChooseJoinSave", shown));
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
    let described = Equals(this.page, TandemCoopPage.Settings) && StrLen(this.about) > 0;
    if IsDefined(this.aboutBox) {
      this.aboutBox.SetVisible(described);
    }
    if described && IsDefined(this.aboutHead) && IsDefined(this.aboutBody) && IsDefined(this.aboutFoot) {
      this.aboutHead.SetText(this.aboutTitle);
      this.aboutBody.SetText(this.about);
      this.aboutFoot.SetText(this.aboutNote);
      this.aboutFoot.SetVisible(StrLen(this.aboutNote) > 0);
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
    return this.BackPressed();
  }

  // The same for a back that is no input event (the pause menu's scenario).
  public func BackPressed() -> Int32 {
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
    this.TandemCursor();
  }
}

// The cursor on the page's row (the first, or the one just changed).
@addMethod(SingleplayerMenuGameController)
private func TandemCursor() -> Void {
  let row = this.m_tandem.Cursor();
  this.m_menuListController.SetSelectedIndex(row);
  this.SetCursorOverWidget(inkCompoundRef.GetWidgetByIndex(this.m_menuList, row));
}

@wrapMethod(SingleplayerMenuGameController)
protected func HandleMenuItemActivate(data: ref<PauseMenuListItemData>) -> Bool {
  let item = data as TandemMenuItem;
  if !IsDefined(item) {
    // The game's own items: Continue loads the newest save (the list's
    // first); any other loads none we know of yet (CampaignNatives.reds).
    TandemCampaignNative.Loading(IsDefined(data) && Equals(data.action, PauseMenuAction.QuickLoad) ? 0 : -1);
    return wrappedMethod(data);
  }
  return this.TandemActivate(item);
}

// The saves' list (the menu reads it as it opens, for Continue): each save's
// metadata asked for, as the game's own load list does (a screenshot request
// brings it), so that campaigns.txt learns its playthroughs and the Co-op
// page can offer the co-op campaign and a host's V from last time. Asked
// here, said by OnSaveMetadataReady below; the newest 40 are enough.
@addField(SingleplayerMenuGameController)
private let m_tandemShots: wref<inkCanvas>;

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnSavesForLoadReady(saves: array<String>) -> Bool {
  let result = wrappedMethod(saves);
  TandemCampaignNative.SavesReset();
  this.TandemReadSaves(ArraySize(saves));
  return result;
}

@addMethod(SingleplayerMenuGameController)
private func TandemReadSaves(count: Int32) -> Void {
  let root = this.GetRootCompoundWidget();
  if !IsDefined(root) {
    return;
  }
  if IsDefined(this.m_tandemShots) {
    this.m_tandemShots.RemoveAllChildren();
  } else {
    let shots = new inkCanvas();
    shots.SetName(n"tandem_saves");
    shots.SetSize(Vector2(0.0, 0.0));
    shots.SetVisible(false);
    shots.SetInteractive(false);
    shots.Reparent(root);
    this.m_tandemShots = shots;
  }
  // The newest (0) is the game's own request, for its Continue.
  let handler = this.GetSystemRequestsHandler();
  let i = 1;
  while i < count && i < 40 {
    let image = new inkImage();
    image.SetSize(Vector2(8.0, 8.0));
    image.Reparent(this.m_tandemShots);
    handler.RequestSavedGameScreenshot(i, image);
    i += 1;
  }
  TandemNative.Log(s"campaign: reading the metadata of \(count) save(s)");
}

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnSaveMetadataReady(info: ref<SaveMetadataInfo>) -> Bool {
  let result = wrappedMethod(info);
  if IsDefined(info) && info.isValid {
    TandemCampaignNative.SaveSeen(info.saveIndex, info.playthroughID, Cast<Float>(info.playthroughTime), Cast<Float>(info.level), info.locationName, info.timestamp, info.isModded);
  }
  return result;
}

// A save of the list, loaded (the co-op campaign, a host's V from last
// time), as the game's own load list loads one.
@addMethod(SingleplayerMenuGameController)
private func TandemLoad(save: TandemSave) -> Void {
  TandemCampaignNative.Loading(save.index);
  if save.modded {
    this.LoadModdedSave(save.index);
  } else {
    this.LoadSaveInGame(save.index);
  }
}

// Continue co-op campaign: its newest save, hosted as it loads (whatever
// the campaign rule says: the player asked for co-op).
@addMethod(SingleplayerMenuGameController)
private func TandemContinueCampaign() -> Void {
  let save = TandemCampaignNative.CampaignSave();
  if save.index < 0 {
    TandemNative.Log("coop: main menu: no co-op campaign to continue");
    return;
  }
  TandemNative.StartHost(0);
  TandemNative.Log(s"coop: main menu: continuing the co-op campaign: save \(save.index) (level \(save.level), \(save.location))");
  this.TandemLoad(save);
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
        this.TandemUp();
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
private func TandemGo(page: TandemCoopPage, opt cursor: Int32) -> Void {
  if !this.m_tandem.Defer(page, this, cursor) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

@addMethod(SingleplayerMenuGameController)
private func TandemUp() -> Void {
  if !this.m_tandem.DeferUp(this) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

// Only our items on our pages (TandemCoopMenu.Tidy), the cursor back on
// the page's row when the list was rebuilt.
@addMethod(SingleplayerMenuGameController)
private func TandemTidy() -> Void {
  if this.m_tandem.Tidy(this.m_menuListController) {
    this.TandemCursor();
  }
}

@addMethod(SingleplayerMenuGameController)
private func TandemShowPage() -> Void {
  this.ShowActionsList();
  this.TandemTidy();
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
  this.TandemTidy();
  this.m_tandem.Refresh();
  if !this.m_tandem.switching && !IsDefined(this.m_tandem.inputToken) && this.m_tandem.Offered() != this.m_tandem.builtOffer {
    TandemNative.Log(s"coop: main menu: the saves' list came: page \(TandemCoopMenu.PageName(this.m_tandem.page)) built again");
    this.TandemGo(this.m_tandem.page, this.m_tandem.cursor);
  }
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
  if Equals(this.m_tandem.page, TandemCoopPage.Save) && StrLen(this.m_tandem.joinHostId) > 0 {
    if !TandemNative.JoinByHost(this.m_tandem.joinHostId) {
      TandemNative.Log("coop: main menu: a host ID needs a relay (popup)");
      GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-NeedsRelay"), GenericMessageNotificationType.OK);
      return false;
    }
    TandemNative.ChooseArrival();
    return true;
  }
  if Equals(this.m_tandem.page, TandemCoopPage.Save) {
    if !TandemCoop.ChooseJoin(this.m_tandem.target) {
      TandemNative.Log(s"coop: main menu: \(this.m_tandem.target) not joined: a join code needs a relay (popup)");
      GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-NeedsRelay"), GenericMessageNotificationType.OK);
      return false;
    }
    // Once in, our V goes beside the host, no prompt (Travel.reds).
    TandemNative.ChooseArrival();
  }
  return true;
}

// The game's own New Game (the main menu's New game: difficulty, lifepath,
// character creation), the session already chosen: it starts as the new V's
// world loads (TandemSystem.OnPlayerAttach), as a save's load would.
@addMethod(SingleplayerMenuGameController)
private func TandemNewGame() -> Void {
  let status = TandemNative.SessionStatus();
  TandemNative.Log(s"coop: main menu: a new game, the session (role \(status.role)) starts as its world loads");
  TandemCampaignNative.Loading(-1);
  this.m_menuEventDispatcher.SpawnEvent(n"OnNewGame");
}

// Join a game: the recent hosts' page, or straight to the code's popup
// when there is none to pick (one step less).
@addMethod(SingleplayerMenuGameController)
private func TandemJoinPage() -> Void {
  let recent = TandemNative.RecentHosts();
  if ArraySize(recent) == 0 {
    this.TandemAskAddress();
  } else {
    this.TandemGo(TandemCoopPage.Join);
  }
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
    this.TandemUp();
  } else if Equals(action, TandemCoopAction.HostPage) {
    if !this.TandemAskName(action) {
      this.m_tandem.port = 0;
      this.TandemGo(TandemCoopPage.Host);
    }
  } else if Equals(action, TandemCoopAction.NewHost) {
    if !this.TandemAskName(action) {
      TandemNative.StartHost(0);
      this.TandemNewGame();
    }
  } else if Equals(action, TandemCoopAction.JoinPage) {
    if !this.TandemAskName(action) {
      this.TandemJoinPage();
    }
  } else if Equals(action, TandemCoopAction.JoinAddress) {
    if !this.TandemAskName(action) {
      this.TandemAskDirect();
    }
  } else if Equals(action, TandemCoopAction.EnterAddress) {
    this.TandemAskAddress();
  } else if Equals(action, TandemCoopAction.JoinRecent) {
    this.m_tandem.hostKey = item.hostKey;
    this.m_tandem.hostName = item.hostName;
    this.m_tandem.joinHostId = "";
    if item.presence == 2 {
      // Online and open: joined by its ID, no code.
      TandemNative.Log(s"coop: main menu: \(item.hostName) is joinable: by its host ID");
      this.m_tandem.joinHostId = item.hostId;
      this.m_tandem.target = "";
      this.TandemGo(TandemCoopPage.Save);
    } else if StrLen(item.code) > 0 {
      // Joined by a code: today's, asked for (the last one as the empty answer).
      this.m_tandem.lastCode = item.code;
      this.TandemAskCodeOf();
    } else {
      this.m_tandem.target = item.target;
      this.TandemGo(TandemCoopPage.Save);
    }
  } else if Equals(action, TandemCoopAction.ContinueCampaign) {
    if !this.TandemAskName(action) {
      this.TandemContinueCampaign();
    }
  } else if Equals(action, TandemCoopAction.LastV) {
    let last = TandemCampaignNative.LastV(this.m_tandem.hostKey);
    if last.index >= 0 && this.TandemChoose() {
      TandemNative.Log(s"coop: main menu: joining with your V from last time: save \(last.index) (level \(last.level), \(last.location))");
      this.TandemLoad(last);
    }
  } else if Equals(action, TandemCoopAction.NewV) {
    if this.TandemChoose() {
      this.TandemNewGame();
    }
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
    // The vanilla Load game: the game's own list of saves (which one is
    // learnt from its playthrough time: CampaignNatives.reds).
    TandemCampaignNative.Loading(-1);
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
      this.TandemGo(TandemCoopPage.Settings, this.m_tandem.RowOf(item.target));
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
  this.TandemGo(TandemCoopPage.Settings, this.m_tandem.RowOf(this.m_tandem.settingKey));
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
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.NewHost) {
    TandemNative.StartHost(0);
    this.TandemNewGame();
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.ContinueCampaign) {
    this.TandemContinueCampaign();
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.JoinAddress) {
    this.TandemAskDirect();
  } else {
    this.TandemJoinPage();
  }
  return true;
}

// The game's message popup with Codeware's text field: a code or an address
// typed afresh.
@addMethod(SingleplayerMenuGameController)
private func TandemAskAddress() -> Void {
  this.m_tandem.hostKey = "";
  this.m_tandem.hostName = "";
  this.m_tandem.lastCode = "";
  this.m_tandem.joinHostId = "";
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-EnterCode"), GenericMessageNotificationType.ConfirmCancel);
  this.m_tandem.inputToken.RegisterListener(this, n"OnTandemAddress");
}

// A recent host joined by a join code: today's, in the same popup.
@addMethod(SingleplayerMenuGameController)
private func TandemAskCodeOf() -> Void {
  this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-Join"), TandemCoop.CodeOfPrompt(this.m_tandem), GenericMessageNotificationType.ConfirmCancel);
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
  let typed = TandemCoop.Typed(this.m_tandem, close.input);
  let target = TandemCoop.Resolve(typed);
  TandemNative.Log(s"coop: main menu: typed \(typed) -> \(target)");
  if StrLen(target) == 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.F("Popup-NotATarget", typed), GenericMessageNotificationType.OK);
    return true;
  }
  this.m_tandem.target = target;
  if StrLen(this.m_tandem.hostKey) == 0 {
    this.m_tandem.hostKey = TandemCoop.KeyOf(target);
  }
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
  this.m_tandem.hostKey = target;
  this.m_tandem.hostName = "";
  this.m_tandem.joinHostId = "";
  this.TandemGo(TandemCoopPage.Save);
}

// tools/game.sh cmd coop ...: coop menu PAGE | coop menu shown | coop pick
// TEXT | coop host [PORT] | coop join ADDRESS | coop address [TEXT] | coop
// solo | coop status.
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
  if Equals(verb, "menu") && Equals(arg, "shown") {
    this.m_tandem.LogShown(this.m_menuListController);
  } else if Equals(verb, "menu") {
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
      this.m_tandem.hostKey = TandemCoop.KeyOf(target);
      this.m_tandem.hostName = "";
      this.m_tandem.joinHostId = "";
      this.TandemGo(TandemCoopPage.Save);
    }
  } else if Equals(verb, "continue") {
    // The game's own Continue, with no co-op choice of the menu's (a co-op
    // campaign opens by its rule as it loads).
    TandemNative.Log("coop: main menu: the game's own Continue");
    let load = new PauseMenuListItemData();
    load.action = PauseMenuAction.QuickLoad;
    this.HandleMenuItemActivate(load);
  } else if Equals(verb, "campaign") {
    let save = TandemCampaignNative.CampaignSave();
    TandemNative.Log(TandemCampaignNative.CampaignReport());
    let which = "none";
    if save.index >= 0 {
      which = s"save \(save.index) (level \(save.level), \(save.location))";
    }
    TandemNative.Log(s"campaign: the co-op campaign to continue: \(which)");
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
      this.TandemGo(TandemCoopPage.Settings, this.m_tandem.cursor);
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
    this.TandemCursor();
  }
}

// The cursor on the page's row (the first, or the one just changed).
@addMethod(PauseMenuGameController)
private func TandemCursor() -> Void {
  let row = this.m_tandem.Cursor();
  this.m_menuListController.SetSelectedIndex(row);
  this.SetCursorOverWidget(inkCompoundRef.GetWidgetByIndex(this.m_menuList, row));
}

// The pause menu's Exit as a guest (progression.md, decision 7): held here,
// re-run once our V is saved (Saves.reds), or at once when the player says
// to exit without the save. The popup's token, and the item to re-run.
@addField(PauseMenuGameController)
private let m_tandemExitToken: ref<inkGameNotificationToken>;

@addField(PauseMenuGameController)
private let m_tandemExitIndex: Int32;

@addField(PauseMenuGameController)
private let m_tandemExitTarget: ref<ListItemController>;

// The re-run's pass through to the game's own Exit.
@addField(PauseMenuGameController)
private let m_tandemExitPass: Bool;

@wrapMethod(PauseMenuGameController)
protected cb func OnMenuItemActivated(index: Int32, target: ref<ListItemController>) -> Bool {
  TandemNative.HookRan(n"Coop.PauseMenuGameController.OnMenuItemActivated");
  let item = target.GetData() as TandemMenuItem;
  if !IsDefined(item) {
    let data = target.GetData() as PauseMenuListItemData;
    let exiting = IsDefined(data) && (Equals(data.action, PauseMenuAction.ExitToMainMenu) || Equals(data.action, PauseMenuAction.ExitGame));
    if exiting && !this.m_tandemExitPass && this.TandemHoldExit(data.action, index, target) {
      return true;
    }
    this.m_tandemExitPass = false;
    // Back to the main menu: a co-op choice made in game was for this game.
    if IsDefined(data) && Equals(data.action, PauseMenuAction.ExitToMainMenu) {
      TandemNative.ResetChoice();
    }
    return wrappedMethod(index, target);
  }
  this.PlaySound(n"Button", n"OnPress");
  this.TandemActivate(item);
  return true;
}

// Esc in the pause menu is not the controller's input: the menu scenario
// takes it (MenuScenario_PauseMenu.OnBack, GoBack), which closes the whole
// pause menu from pause_menu, whatever the page. The scenario asks the
// pause menu first (TandemBackAnswer, below): one of our pages goes up a
// page, as the game's own sub-pages go back to pause_menu.
public class TandemBackAnswer extends IScriptable {
  public let taken: Bool;
}

@wrapMethod(MenuScenario_PauseMenu)
protected cb func OnBack() -> Bool {
  if Equals(this.m_currMenuName, n"pause_menu") && Equals(this.m_prevMenuName, n"None") {
    let answer = new TandemBackAnswer();
    let menus = this.GetMenusState();
    if IsDefined(menus) {
      menus.DispatchEvent(n"pause_menu", n"OnTandemBack", answer);
    }
    if answer.taken {
      return true;
    }
  }
  return wrappedMethod();
}

@wrapMethod(PauseMenuGameController)
protected cb func OnSetMenuEventDispatcher(menuEventDispatcher: wref<inkMenuEventDispatcher>) -> Bool {
  let result = wrappedMethod(menuEventDispatcher);
  if IsDefined(menuEventDispatcher) {
    menuEventDispatcher.RegisterToEvent(n"OnTandemBack", this, n"OnTandemBack");
  }
  return result;
}

// The scenario's Esc on one of our pages (0: not ours, the menu closes; 1:
// a popup had it; 2: up a page).
@addMethod(PauseMenuGameController)
protected cb func OnTandemBack(userData: ref<IScriptable>) -> Bool {
  let answer = userData as TandemBackAnswer;
  if !IsDefined(this.m_tandem) || !IsDefined(answer) {
    return false;
  }
  let back = this.m_tandem.BackPressed();
  if back == 0 {
    return false;
  }
  answer.taken = true;
  if back == 2 {
    this.PlaySound(n"Button", n"OnPress");
    this.TandemUp();
  }
  return true;
}

// Exit (to the main menu, to the desktop) while our V's session gains are
// not on disk: held (true), or false for the game's own Exit. This runs in
// the wrap above, a UI callback not proven to run on the game thread: it
// reads, opens a popup, and leaves what changes state to the tick (the
// held exit goes through TandemProgress.AskExit, a native that only
// stores it; Saves.reds takes it). The menu closes so that the world, and
// with it our tick, runs again for the leave steps.
@addMethod(PauseMenuGameController)
private func TandemHoldExit(action: PauseMenuAction, index: Int32, target: ref<ListItemController>) -> Bool {
  let system = this.CoopSystem();
  if !IsDefined(system) {
    return false;
  }
  let exit = Equals(action, PauseMenuAction.ExitGame) ? 2 : 1;
  if !system.ExitOwes() {
    // Nothing to save, but a session to end first: the main menu detaches
    // no player in 2.50, so the game's own Exit left a host hosting from the
    // main menu (exit-host). The tick ends it, says goodbye, then exits.
    if !system.InSession() {
      return false;
    }
    TandemNative.Log(s"exit: \(TandemSaves.ExitName(exit)): the session ends first");
    TandemProgress.AskExit(exit);
    this.m_menuEventDispatcher.SpawnEvent(n"OnClosePauseMenu");
    return true;
  }
  let why = system.SaveBlocker();
  this.PlaySound(n"Button", n"OnPress");
  if StrLen(why) > 0 {
    // No save can be made now: the player chooses, exit anyway or stay.
    TandemNative.Log(s"exit: \(TandemSaves.ExitName(exit)): our V cannot be saved now (\(TandemText.English(why)); the game's locks: \(system.SaveLockNames())): asking");
    this.m_tandemExitIndex = index;
    this.m_tandemExitTarget = target;
    this.m_tandemExitToken = GenericMessageNotification.Show(this, TandemText.Get("Exit-Title"), TandemText.F("Exit-CannotSave", TandemText.Get(why)), GenericMessageNotificationType.YesNo);
    this.m_tandemExitToken.RegisterListener(this, n"OnTandemExitAnswer");
    return true;
  }
  TandemNative.Log(s"exit: \(TandemSaves.ExitName(exit)): held until our V is saved");
  TandemProgress.AskExit(exit);
  this.m_menuEventDispatcher.SpawnEvent(n"OnClosePauseMenu");
  return true;
}

// The answer: Yes exits without the save (the game's own Exit, re-run),
// anything else stays in the game.
@addMethod(PauseMenuGameController)
protected cb func OnTandemExitAnswer(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandemExitToken = null;
  let close = data as GenericMessageNotificationCloseData;
  let target = this.m_tandemExitTarget;
  this.m_tandemExitTarget = null;
  if !IsDefined(close) || NotEquals(close.result, GenericMessageNotificationResult.Yes) || !IsDefined(target) {
    TandemNative.Log("exit: the player stays");
    if IsDefined(this.m_tandem) {
      this.m_tandem.swallowBack = 2;
    }
    return true;
  }
  TandemNative.Log("exit: the player exits without the save");
  this.m_tandemExitPass = true;
  this.OnMenuItemActivated(this.m_tandemExitIndex, target);
  return true;
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
private func TandemGo(page: TandemCoopPage, opt cursor: Int32) -> Void {
  if !this.m_tandem.Defer(page, this, cursor) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

@addMethod(PauseMenuGameController)
private func TandemUp() -> Void {
  if !this.m_tandem.DeferUp(this) && this.m_tandem.TakeSwitch() {
    this.TandemShowPage();
  }
}

// Only our items on our pages (TandemCoopMenu.Tidy), the cursor back on
// the page's row when the list was rebuilt.
@addMethod(PauseMenuGameController)
private func TandemTidy() -> Void {
  if this.m_tandem.Tidy(this.m_menuListController) {
    this.TandemCursor();
  }
}

@addMethod(PauseMenuGameController)
private func TandemShowPage() -> Void {
  this.ShowActionsList();
  this.TandemTidy();
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
  this.TandemTidy();
  this.m_tandem.Refresh();
  // The Join list's hosts answered (online, joinable): built again.
  if !this.m_tandem.switching && !IsDefined(this.m_tandem.inputToken) && this.m_tandem.Offered() != this.m_tandem.builtOffer {
    TandemNative.Log(s"coop: pause menu: the relay answered: page \(TandemCoopMenu.PageName(this.m_tandem.page)) built again");
    this.TandemGo(this.m_tandem.page, this.m_tandem.cursor);
  }
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
    this.TandemUp();
  } else if Equals(action, TandemCoopAction.HostNow) {
    if !this.TandemAskName(action) {
      system.CoopHost(0);
      system.MarkCampaign();
      this.TandemGo(TandemCoopPage.Coop);
    }
  } else if Equals(action, TandemCoopAction.JoinPage) {
    if !this.TandemAskName(action) {
      this.TandemJoinPage();
    }
  } else if Equals(action, TandemCoopAction.JoinAddress) {
    if !this.TandemAskName(action) {
      this.TandemAskDirect();
    }
  } else if Equals(action, TandemCoopAction.EnterAddress) {
    this.TandemAskAddress();
  } else if Equals(action, TandemCoopAction.JoinRecent) {
    if item.presence == 2 {
      // Online and open: joined at once by its ID, no code.
      TandemNative.Log(s"coop: pause menu: \(item.hostName) is joinable: by its host ID");
      if !system.CoopJoinHost(item.hostId) {
        GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.Get("Popup-NeedsRelay"), GenericMessageNotificationType.OK);
      }
      this.TandemGo(TandemCoopPage.Coop);
    } else if StrLen(item.code) > 0 {
      // Joined by a code: today's, asked for (the last one as the empty answer).
      this.m_tandem.hostName = item.hostName;
      this.m_tandem.lastCode = item.code;
      this.m_tandem.inputToken = GenericMessageNotification.ShowInput(this, TandemText.Get("Menu-Join"), TandemCoop.CodeOfPrompt(this.m_tandem), GenericMessageNotificationType.ConfirmCancel);
      this.m_tandem.inputToken.RegisterListener(this, n"OnTandemAddress");
    } else {
      system.CoopJoin(item.target);
      this.TandemGo(TandemCoopPage.Coop);
    }
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
      this.TandemGo(TandemCoopPage.Settings, this.m_tandem.RowOf(item.target));
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
  this.TandemGo(TandemCoopPage.Settings, this.m_tandem.RowOf(this.m_tandem.settingKey));
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
    system.MarkCampaign();
    this.TandemGo(TandemCoopPage.Coop);
  } else if Equals(this.m_tandem.nameFor, TandemCoopAction.JoinAddress) {
    this.TandemAskDirect();
  } else {
    this.TandemJoinPage();
  }
  return true;
}

// Join a game: the recent hosts' page, or straight to the code's popup.
@addMethod(PauseMenuGameController)
private func TandemJoinPage() -> Void {
  let recent = TandemNative.RecentHosts();
  if ArraySize(recent) == 0 {
    this.TandemAskAddress();
  } else {
    this.TandemGo(TandemCoopPage.Join);
  }
}

@addMethod(PauseMenuGameController)
private func TandemAskAddress() -> Void {
  this.m_tandem.hostName = "";
  this.m_tandem.lastCode = "";
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
  let typed = TandemCoop.Typed(this.m_tandem, close.input);
  let target = TandemCoop.Resolve(typed);
  let system = this.CoopSystem();
  TandemNative.Log(s"coop: pause menu: typed \(typed) -> \(target)");
  if StrLen(target) == 0 || !IsDefined(system) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Join"), TandemText.F("Popup-NotATarget", typed), GenericMessageNotificationType.OK);
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

// coop menu PAGE | coop menu shown | coop pick TEXT, while the pause menu is
// open (the rest of the coop commands are TandemSystem's).
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
  if Equals(verb, "menu") && Equals(arg, "shown") {
    this.m_tandem.LogShown(this.m_menuListController);
  } else if Equals(verb, "menu") {
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
