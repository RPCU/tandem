// A shared campaign (docs/architecture/campaign.md): one campaign save a
// group shares. Whoever hosts it plays its V; the others join with their own
// V; anyone may play it solo. Saves cannot be merged, so each copy carries
// its lineage: which campaign, which copy, its generation and the copies it
// came from, in the save itself (m_words, below), with who played it last,
// when and after which quest in tandem's own folder (the plugin's records).
//
// - A load of a campaign save starts a new copy (a generation), a child of
//   the loaded one (TandemCampaignNative.CampaignLoaded): a co-op session or
//   a solo play moves the campaign on. A save of it records its slot.
// - In a session every game says its copy of the host's campaign; each draws
//   the same verdict (the same, one ahead: "Continue from NAME's save", a
//   fork: the players choose) and says it in a notice; the co-op menu's
//   Shared campaign page offers the choice.
// - Export campaign (pause menu): a new manual save of this copy, written
//   with its record to one zip in red4ext/plugins/tandem/campaigns/. Import
//   campaign (both menus): a zip there installed as a new manual save slot,
//   never over a save, after a question that names the mods its game had
//   and ours lacks.
//
// Off unless tandem.ini says `campaign = on`. Not yet run in game.

public class TandemCampaignTick extends DelayCallback {
  public let system: wref<TandemCampaignSystem>;
  public let gen: Int32;

  public func Call() -> Void {
    if IsDefined(this.system) {
      this.system.Tick(this.gen);
    }
  }
}

public class TandemCampaignSystem extends ScriptableSystem {
  // The copy of the campaign this save is (core/campaign.hpp, Pack): the
  // campaign, the copy, its generation, its ancestors. Empty: not a
  // campaign. Written by the same save as the world it names.
  private persistent let m_words: array<Uint64>;
  private let m_gen: Int32;
  private let m_news: Uint32;
  private let m_listening: Bool;

  public static func Get(game: GameInstance) -> ref<TandemCampaignSystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemCampaignSystem") as TandemCampaignSystem;
  }

  // A save loaded (the main menu's world is not one): its copy moves on.
  private final func OnPlayerAttach(request: ref<PlayerAttachRequest>) -> Void {
    if GameInstance.GetSystemRequestsHandler().IsPreGame() {
      return;
    }
    let words = TandemCampaignNative.CampaignLoaded(this.m_words);
    this.m_words = words;
    if !TandemCampaignNative.CampaignOn() {
      return;
    }
    this.Listen();
    this.m_news = TandemCampaignNative.CampaignNews();
    this.m_gen += 1;
    this.Schedule(2.0);
  }

  private final func OnPlayerDetach(request: ref<PlayerDetachRequest>) -> Void {
    this.m_gen += 1;
    if this.m_listening {
      GameInstance.GetSystemRequestsHandler().UnregisterFromCallback(n"OnSavingComplete", this, n"OnCampaignSaved");
      GameInstance.GetJournalManager(this.GetGameInstance()).UnregisterScriptCallback(this, n"OnCampaignJournal");
      this.m_listening = false;
    }
  }

  private func Listen() -> Void {
    if this.m_listening {
      return;
    }
    GameInstance.GetSystemRequestsHandler().RegisterToCallback(n"OnSavingComplete", this, n"OnCampaignSaved");
    GameInstance.GetJournalManager(this.GetGameInstance()).RegisterScriptCallback(this, n"OnCampaignJournal", gameJournalListenerType.State);
    this.m_listening = true;
  }

  private func Schedule(delay: Float) -> Void {
    let tick = new TandemCampaignTick();
    tick.system = this;
    tick.gen = this.m_gen;
    // Real time: not slowed by time dilation.
    GameInstance.GetDelaySystem(this.GetGameInstance()).DelayCallback(tick, delay, false);
  }

  // The game's word on a save: only stored (the plugin's worker reads the
  // saves folder for the slot). A callback: may come on any thread.
  protected cb func OnCampaignSaved(success: Bool, locks: array<gameSaveLock>) -> Bool {
    TandemNative.HookRan(n"Campaign.TandemCampaignSystem.OnCampaignSaved");
    TandemCampaignNative.CampaignSaved(success);
    return true;
  }

  // A quest done: the last quest of this copy (the plugin ignores a guest's
  // journal in a session, the host's story). Reads, and a native that stores.
  protected cb func OnCampaignJournal(hash: Uint32, className: CName, notifyOption: JournalNotifyOption, changeType: JournalChangeType) -> Bool {
    TandemNative.HookRan(n"Campaign.TandemCampaignSystem.OnCampaignJournal");
    let journal = GameInstance.GetJournalManager(this.GetGameInstance());
    let entry = journal.GetEntry(hash);
    let done = entry as JournalQuest;
    if IsDefined(done) && Equals(journal.GetEntryState(done), gameJournalEntryState.Succeeded) {
      TandemCampaignNative.CampaignQuest(done.GetTitle(journal));
    }
    return true;
  }

  // Once a second: the words of a copy that moved on (an export), what to
  // tell the player.
  public func Tick(gen: Int32) -> Void {
    if gen != this.m_gen {
      return;
    }
    if TandemCampaignNative.CampaignWordsChanged() {
      let words = TandemCampaignNative.CampaignWords();
      this.m_words = words;
    }
    let game = this.GetGameInstance();
    if TandemCampaignNative.CampaignLeft() {
      TandemText.Notice(game, "Campaign-Left");
    }
    let warning = TandemCampaignNative.CampaignLoadWarning();
    if warning.copy != 0ul {
      TandemText.Notice(game, "Campaign-OlderLoaded", TandemCampaign.SaveOf(warning));
    }
    let news = TandemCampaignNative.CampaignNews();
    if news != this.m_news {
      this.m_news = news;
      TandemCampaign.TellVerdict(game);
    }
    // A job the pause menu did not see end (it was closed).
    let job = TandemCampaignNative.CampaignJob();
    if job == 3 || job == 4 {
      let key = TandemCampaign.OutcomeKey();
      let text = TandemCampaignNative.CampaignJobText();
      TandemCampaignNative.CampaignJobSeen();
      TandemText.Notice(game, key, text);
    }
    this.Schedule(1.0);
  }

  // The co-op menu's Make this a shared campaign: this save's world, as a
  // new manual save, is the campaign's first copy.
  public func StartCampaign() -> Bool {
    let words = TandemCampaignNative.CampaignStart();
    if ArraySize(words) == 0 {
      return false;
    }
    this.m_words = words;
    if !TandemCampaignNative.CampaignAskSave(1) {
      return false;
    }
    TandemNative.Log("campaign: a new campaign: saving it");
    GameInstance.GetSystemRequestsHandler().ManualSave("ManualSave-");
    if !this.m_listening {
      this.Listen();
    }
    return true;
  }

  // The co-op menu's Export campaign: a new manual save of this copy, then
  // the plugin's worker writes it and its record to a zip.
  public func ExportCampaign() -> Bool {
    if !TandemCampaignNative.CampaignPlaying() || !TandemCampaignNative.CampaignAskSave(2) {
      return false;
    }
    TandemNative.Log("campaign: export: saving this copy");
    GameInstance.GetSystemRequestsHandler().ManualSave("ManualSave-");
    return true;
  }
}

// What both menus and the commands share.
public class TandemCampaign {
  // "Judy's save (2026-11-21 18:40, after The Heist)".
  public static func SaveOf(copy: TandemCampaignCopy) -> String {
    return TandemCampaign.SaveOfText(copy.who, copy.when, copy.lastQuest);
  }

  public static func SaveOfText(who: String, when: String, lastQuest: String) -> String {
    let name = StrLen(who) > 0 ? who : TandemText.Get("Campaign-Someone");
    if StrLen(when) == 0 && StrLen(lastQuest) == 0 {
      return TandemText.F("Campaign-SaveOfPlain", name);
    }
    let details = when;
    if StrLen(lastQuest) > 0 {
      details = StrLen(when) > 0 ? TandemText.F("Campaign-Details", when, lastQuest) : TandemText.F("Campaign-After", lastQuest);
    }
    return TandemText.F("Campaign-SaveOf", name, details);
  }

  // The verdict of the session's copies, as a notice.
  public static func TellVerdict(game: GameInstance) -> Void {
    let verdict = TandemCampaignNative.CampaignVerdict();
    let heads = TandemCampaignNative.CampaignHeads();
    let count = ArraySize(heads);
    if verdict.kind == 2 && count > 0 && (verdict.selfBehind || verdict.hostBehind) {
      let head = heads[0];
      if verdict.selfBehind {
        TandemText.Notice(game, verdict.hosting ? "Campaign-HostBehind" : "Campaign-Newer", TandemCampaign.SaveOf(head));
      } else {
        // We are ahead: the others are offered ours.
        TandemText.Notice(game, "Campaign-OursNewer");
      }
    } else if verdict.kind == 3 {
      TandemText.Notice(game, "Campaign-Fork", s"\(count)");
    } else if verdict.kind == 1 {
      TandemText.Notice(game, "Campaign-Same");
    }
  }

  // The key that says how the last job ended (its text fills it).
  public static func OutcomeKey() -> String {
    let job = TandemCampaignNative.CampaignJob();
    let purpose = TandemCampaignNative.CampaignJobPurpose();
    if job == 4 {
      return "Campaign-Failed";
    }
    if purpose == 1 {
      return "Campaign-Started";
    }
    if purpose == 2 {
      return "Campaign-Exported";
    }
    return "Campaign-Imported";
  }

  // The Shared campaign page's items (Coop.reds, TandemCoopMenu.Build).
  public static func Build(menu: ref<TandemCoopMenu>, inSession: Bool) -> Void {
    let status = TandemNative.SessionStatus();
    let guest = inSession && status.you != status.host;
    if menu.inGame {
      // The session's copies: the one to go on from, or the fork's choice.
      let verdict = TandemCampaignNative.CampaignVerdict();
      let heads = TandemCampaignNative.CampaignHeads();
      let i = 0;
      while inSession && i < ArraySize(heads) {
        let head = heads[i];
        if verdict.kind == 2 && verdict.selfBehind && !head.mine {
          menu.AddItem("", TandemCoopAction.CampaignContinue, TandemText.F("Campaign-ContinueFrom", TandemCampaign.SaveOf(head)), s"\(i)");
        } else if verdict.kind == 3 {
          let label = head.mine ? TandemText.F("Campaign-GoOnFrom", TandemText.Get("Campaign-Yours")) : TandemText.F("Campaign-GoOnFrom", TandemCampaign.SaveOf(head));
          menu.AddItem("", TandemCoopAction.CampaignContinue, label, s"\(i)");
        }
        i += 1;
      }
      if !guest {
        if TandemCampaignNative.CampaignPlaying() {
          menu.AddItem("Menu-CampaignExport", TandemCoopAction.CampaignExport, "", "");
        } else {
          menu.AddItem("Menu-CampaignStart", TandemCoopAction.CampaignStart, "", "");
        }
      }
    }
    let archives = TandemCampaignNative.CampaignArchives();
    let a = 0;
    while a < ArraySize(archives) && a < 12 {
      let archive = archives[a];
      menu.AddItem("", TandemCoopAction.CampaignImport, TandemText.F("Campaign-ImportItem", TandemCampaign.SaveOfText(archive.who, archive.when, archive.lastQuest)), s"\(a)");
      a += 1;
    }
    menu.AddItem("Menu-CampaignFolder", TandemCoopAction.CampaignFolder, "", "");
  }

  // The Shared campaign page's status lines.
  public static func StatusLines(inGame: Bool) -> array<String> {
    let lines: array<String>;
    if inGame {
      if TandemCampaignNative.CampaignPlaying() {
        let running = TandemCampaignNative.CampaignRunning();
        if StrLen(running.lastQuest) > 0 {
          ArrayPush(lines, TandemText.F("Campaign-ThisSaveAfter", s"\(running.generation)", running.lastQuest));
        } else {
          ArrayPush(lines, TandemText.F("Campaign-ThisSave", s"\(running.generation)"));
        }
      } else {
        ArrayPush(lines, TandemText.Get("Campaign-NotOne"));
      }
    }
    let job = TandemCampaignNative.CampaignJob();
    if job == 1 {
      ArrayPush(lines, TandemText.Get("Campaign-Saving"));
    } else if job == 2 {
      ArrayPush(lines, TandemText.Get("Campaign-Writing"));
    }
    let archives = TandemCampaignNative.CampaignArchives();
    if ArraySize(archives) == 0 {
      ArrayPush(lines, TandemText.Get("Campaign-NoArchive"));
    }
    return lines;
  }

  // The question before an item's action (Yes does it): "" for none.
  public static func Question(action: TandemCoopAction, target: String) -> String {
    if Equals(action, TandemCoopAction.CampaignStart) {
      return TandemText.Get("Campaign-AskStart");
    }
    if Equals(action, TandemCoopAction.CampaignExport) {
      return TandemText.Get("Campaign-AskExport");
    }
    let i = StringToInt(target, -1);
    if Equals(action, TandemCoopAction.CampaignImport) {
      let archives = TandemCampaignNative.CampaignArchives();
      if i < 0 || i >= ArraySize(archives) {
        return "";
      }
      let archive = archives[i];
      let what = TandemCampaign.SaveOfText(archive.who, archive.when, archive.lastQuest);
      if archive.missing > 0 {
        return TandemText.F("Campaign-AskImportMods", what, archive.missingNames);
      }
      return TandemText.F("Campaign-AskImport", what);
    }
    if Equals(action, TandemCoopAction.CampaignContinue) {
      let heads = TandemCampaignNative.CampaignHeads();
      if i < 0 || i >= ArraySize(heads) {
        return "";
      }
      let head = heads[i];
      if head.mine {
        return TandemText.Get("Campaign-AskKeepOurs");
      }
      let verdict = TandemCampaignNative.CampaignVerdict();
      return TandemText.F(verdict.kind == 3 ? "Campaign-AskGoOn" : "Campaign-AskContinue", TandemCampaign.SaveOf(head), head.name);
    }
    return "";
  }

  // The answer Yes: done, and the words to show now ("" none: the job's
  // outcome comes later). `system` is null in the main menu.
  public static func Do(system: ref<TandemCampaignSystem>, action: TandemCoopAction, target: String) -> String {
    let i = StringToInt(target, -1);
    if Equals(action, TandemCoopAction.CampaignStart) {
      return IsDefined(system) && system.StartCampaign() ? "" : TandemText.Get("Campaign-Busy");
    }
    if Equals(action, TandemCoopAction.CampaignExport) {
      return IsDefined(system) && system.ExportCampaign() ? "" : TandemText.Get("Campaign-Busy");
    }
    if Equals(action, TandemCoopAction.CampaignImport) {
      let archives = TandemCampaignNative.CampaignArchives();
      if i < 0 || i >= ArraySize(archives) {
        return "";
      }
      let archive = archives[i];
      TandemNative.Log(s"campaign: import \(archive.file)");
      return TandemCampaignNative.CampaignImport(archive.path) ? "" : TandemText.Get("Campaign-Busy");
    }
    if Equals(action, TandemCoopAction.CampaignContinue) {
      let heads = TandemCampaignNative.CampaignHeads();
      if i < 0 || i >= ArraySize(heads) {
        return "";
      }
      let head = heads[i];
      TandemCampaignNative.CampaignChoose(head.copy);
      if head.mine {
        return TandemText.Get("Campaign-KeptOurs");
      }
      return TandemText.F("Campaign-Chosen", TandemCampaign.SaveOf(head), head.name);
    }
    return "";
  }

  // tools/game.sh cmd campaign [list | start | export | import N | choose N]:
  // what the page's items do, without the questions (scenarios campaign-*).
  public static func Command(command: String, system: ref<TandemCampaignSystem>) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    let arg = ArraySize(words) > 2 ? words[2] : "";
    if !TandemCampaignNative.CampaignOn() {
      TandemNative.Log("campaign: off (tandem.ini campaign = on)");
      return;
    }
    if Equals(verb, "list") {
      let archives = TandemCampaignNative.CampaignArchives();
      TandemNative.Log(s"campaign: \(ArraySize(archives)) archive(s) in \(TandemCampaignNative.CampaignFolder())");
      let i = 0;
      while i < ArraySize(archives) {
        let archive = archives[i];
        TandemNative.Log(s"campaign: archive \(i): \(archive.file), \(archive.who), generation \(archive.generation), \(archive.missing) mod(s) missing\(archive.known ? ", known" : "")");
        i += 1;
      }
      return;
    }
    if Equals(verb, "start") || Equals(verb, "export") || Equals(verb, "import") || Equals(verb, "choose") {
      let action = TandemCoopAction.CampaignImport;
      if Equals(verb, "start") {
        action = TandemCoopAction.CampaignStart;
      } else if Equals(verb, "export") {
        action = TandemCoopAction.CampaignExport;
      } else if Equals(verb, "choose") {
        action = TandemCoopAction.CampaignContinue;
      }
      let answer = TandemCampaign.Do(system, action, arg);
      TandemNative.Log(s"campaign: \(verb) \(arg): \(StrLen(answer) > 0 ? answer : "under way")");
      return;
    }
    TandemCampaignNative.CampaignReport();
    let heads = TandemCampaignNative.CampaignHeads();
    let i = 0;
    while i < ArraySize(heads) {
      let head = heads[i];
      TandemNative.Log(s"campaign: head \(i): \(head.name) (player \(head.player)), \(head.who), generation \(head.generation)\(head.mine ? ", ours" : "")");
      i += 1;
    }
  }
}

// ---- The menus: the Shared campaign page's questions and outcomes ----

@addField(PauseMenuGameController)
private let m_tandemCampaignAction: TandemCoopAction;

@addField(PauseMenuGameController)
private let m_tandemCampaignTarget: String;

@addField(PauseMenuGameController)
private let m_tandemCampaignToken: ref<inkGameNotificationToken>;

// True: the item was the campaign's.
@addMethod(PauseMenuGameController)
private func TandemCampaignActivate(item: ref<TandemMenuItem>) -> Bool {
  let action = item.coop;
  if Equals(action, TandemCoopAction.CampaignPage) {
    this.TandemGo(TandemCoopPage.Campaign);
    return true;
  }
  if Equals(action, TandemCoopAction.CampaignFolder) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), TandemText.F("Campaign-Folder", TandemCampaignNative.CampaignFolder()), GenericMessageNotificationType.OK);
    return true;
  }
  if NotEquals(action, TandemCoopAction.CampaignStart) && NotEquals(action, TandemCoopAction.CampaignExport) && NotEquals(action, TandemCoopAction.CampaignImport) && NotEquals(action, TandemCoopAction.CampaignContinue) {
    return false;
  }
  // A start and an export make a save: not when the game would refuse one.
  let owner = this.GetPlayerControlledObject();
  let system = IsDefined(owner) ? TandemSystem.Of(owner.GetGame()) : null;
  if (Equals(action, TandemCoopAction.CampaignStart) || Equals(action, TandemCoopAction.CampaignExport)) && IsDefined(system) {
    let why = system.SaveBlocker();
    if StrLen(why) > 0 {
      GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), TandemText.F("Campaign-CannotSave", TandemText.Get(why)), GenericMessageNotificationType.OK);
      return true;
    }
  }
  let question = TandemCampaign.Question(action, item.target);
  if StrLen(question) == 0 {
    return true;
  }
  this.m_tandemCampaignAction = action;
  this.m_tandemCampaignTarget = item.target;
  this.m_tandemCampaignToken = GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), question, GenericMessageNotificationType.YesNo);
  this.m_tandemCampaignToken.RegisterListener(this, n"OnTandemCampaignAnswer");
  return true;
}

@addMethod(PauseMenuGameController)
protected cb func OnTandemCampaignAnswer(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandemCampaignToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) || NotEquals(close.result, GenericMessageNotificationResult.Yes) {
    if IsDefined(this.m_tandem) {
      this.m_tandem.swallowBack = 2;
    }
    return true;
  }
  let owner = this.GetPlayerControlledObject();
  let system = IsDefined(owner) ? TandemCampaignSystem.Get(owner.GetGame()) : null;
  let answer = TandemCampaign.Do(system, this.m_tandemCampaignAction, this.m_tandemCampaignTarget);
  if StrLen(answer) > 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), answer, GenericMessageNotificationType.OK);
  }
  this.TandemGo(TandemCoopPage.Campaign);
  return true;
}

// The menu's clock: a job that ended is said in a popup.
@addMethod(PauseMenuGameController)
private func TandemCampaignTick() -> Void {
  let job = TandemCampaignNative.CampaignJob();
  if job != 3 && job != 4 || IsDefined(this.m_tandemCampaignToken) {
    return;
  }
  let key = TandemCampaign.OutcomeKey();
  let text = TandemCampaignNative.CampaignJobText();
  TandemCampaignNative.CampaignJobSeen();
  TandemNative.Log(s"campaign: \(TandemText.FE(true, key, text))");
  GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), TandemText.F(key, text), GenericMessageNotificationType.OK);
  if IsDefined(this.m_tandem) && Equals(this.m_tandem.page, TandemCoopPage.Campaign) {
    this.TandemGo(TandemCoopPage.Campaign);
  }
}

@addField(SingleplayerMenuGameController)
private let m_tandemCampaignAction: TandemCoopAction;

@addField(SingleplayerMenuGameController)
private let m_tandemCampaignTarget: String;

@addField(SingleplayerMenuGameController)
private let m_tandemCampaignToken: ref<inkGameNotificationToken>;

// The main menu: no game, so only importing (and the folder).
@addMethod(SingleplayerMenuGameController)
private func TandemCampaignActivate(item: ref<TandemMenuItem>) -> Bool {
  let action = item.coop;
  if Equals(action, TandemCoopAction.CampaignPage) {
    this.TandemGo(TandemCoopPage.Campaign);
    return true;
  }
  if Equals(action, TandemCoopAction.CampaignFolder) {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), TandemText.F("Campaign-Folder", TandemCampaignNative.CampaignFolder()), GenericMessageNotificationType.OK);
    return true;
  }
  if NotEquals(action, TandemCoopAction.CampaignImport) {
    return false;
  }
  let question = TandemCampaign.Question(action, item.target);
  if StrLen(question) == 0 {
    return true;
  }
  this.m_tandemCampaignAction = action;
  this.m_tandemCampaignTarget = item.target;
  this.m_tandemCampaignToken = GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), question, GenericMessageNotificationType.YesNo);
  this.m_tandemCampaignToken.RegisterListener(this, n"OnTandemCampaignAnswer");
  return true;
}

@addMethod(SingleplayerMenuGameController)
protected cb func OnTandemCampaignAnswer(data: ref<inkGameNotificationData>) -> Bool {
  this.m_tandemCampaignToken = null;
  let close = data as GenericMessageNotificationCloseData;
  if !IsDefined(close) || NotEquals(close.result, GenericMessageNotificationResult.Yes) {
    if IsDefined(this.m_tandem) {
      this.m_tandem.swallowBack = 2;
    }
    return true;
  }
  let answer = TandemCampaign.Do(null, this.m_tandemCampaignAction, this.m_tandemCampaignTarget);
  if StrLen(answer) > 0 {
    GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), answer, GenericMessageNotificationType.OK);
  }
  this.TandemGo(TandemCoopPage.Campaign);
  return true;
}

@addMethod(SingleplayerMenuGameController)
private func TandemCampaignTick() -> Void {
  let job = TandemCampaignNative.CampaignJob();
  if job != 3 && job != 4 || IsDefined(this.m_tandemCampaignToken) {
    return;
  }
  let key = TandemCampaign.OutcomeKey();
  let text = TandemCampaignNative.CampaignJobText();
  TandemCampaignNative.CampaignJobSeen();
  TandemNative.Log(s"campaign: \(TandemText.FE(true, key, text))");
  GenericMessageNotification.Show(this, TandemText.Get("Menu-Campaign"), TandemText.F(key, text), GenericMessageNotificationType.OK);
  if IsDefined(this.m_tandem) && Equals(this.m_tandem.page, TandemCoopPage.Campaign) {
    this.TandemGo(TandemCoopPage.Campaign);
  }
}
