// Dialogue in co-op (docs/research/coop-quests-design.md §2.2, §2.4): the
// host's subtitles and choice hubs on the guests' screens, through their own
// vanilla widgets (the subtitle controller listens to UIGameData, final.reds
// :268284; the dialogue hub to UIInteractions, :399949). Face-to-face lines
// and hubs reach guests within 25 m of the host's V; holocall lines and phone
// hubs reach everyone. The host's V decides: a guest moves its own cursor on
// the hub and can mark a suggestion; every player's cursor is marked on the
// others' hubs (a chip in their colour and their name on the option they
// lean to: docs/architecture/story.md, "Choices together"); the host's pick
// closes every hub.

public class TandemDialogue {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;

  // Host: our blackboards and what we last sent.
  private let m_hostListening: Bool;
  private let m_cbShow: ref<CallbackHandle>;
  private let m_cbHide: ref<CallbackHandle>;
  private let m_cbHubs: ref<CallbackHandle>;
  private let m_cbActive: ref<CallbackHandle>;
  private let m_cbSelected: ref<CallbackHandle>;
  private let m_cbAttempt: ref<CallbackHandle>;
  private let m_hubs: DialogChoiceHubs;
  private let m_activeHub: Int32;
  private let m_sentOpen: Bool;
  private let m_sentHub: Int32;
  private let m_sentKey: String;
  private let m_sentTexts: array<String>;
  private let m_testLine: Uint64;
  // (test) `story hub` holds its hub up until `story hub close`: the game
  // takes a hub it did not make off the board a frame later (13 ms,
  // dialogue-marks-host 2026-11-21: no guest's cursor could land on it).
  private let m_testHub: Bool;
  private let m_testHubBack: Int32;

  // Guest: the host's hub as it arrives, and what we show.
  private let m_pendingHub: Int32;
  private let m_pendingCount: Int32;
  private let m_pendingFlags: Int32;
  private let m_pendingTitle: String;
  private let m_pendingTexts: array<String>;
  private let m_pendingTypes: array<Int32>;
  private let m_shown: Bool;
  private let m_shownHub: Int32;
  private let m_cursor: Int32;
  private let m_lines: array<Uint64>;
  // Guest: our own subtitle board, to never show a line twice (the game may
  // show a line itself when its voice plays here: scene replay), and the
  // lines it showed.
  private let m_ownListening: Bool;
  private let m_cbOwn: ref<CallbackHandle>;
  private let m_native: array<Uint64>;

  // Either: the other players' cursors on our hub (the host's: its guests';
  // a guest's: the host's and the other guests'), each one's option and kind
  // (0 hover, 1 suggestion), drawn on our hub by the game's choice widget
  // (dialogWidgetGameController, found on the HUD layer); what was drawn
  // last, and whether our hub was on screen then.
  private let m_markPlayers: array<Uint32>;
  private let m_markIndexes: array<Int32>;
  private let m_markKinds: array<Int32>;
  private let m_marksKey: String;
  private let m_marksOnScreen: Bool;
  private let m_widget: wref<dialogWidgetGameController>;
  private let m_widgetScannedAt: Float;
  private let m_now: Float;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
  }

  public func Stop() -> Void {
    if this.m_hostListening {
      let game = GetAllBlackboardDefs().UIGameData;
      let interactions = GetAllBlackboardDefs().UIInteractions;
      let gameBoard = this.Board(true);
      let interactionsBoard = this.Board(false);
      gameBoard.UnregisterListenerVariant(game.ShowDialogLine, this.m_cbShow);
      gameBoard.UnregisterListenerVariant(game.HideDialogLine, this.m_cbHide);
      interactionsBoard.UnregisterListenerVariant(interactions.DialogChoiceHubs, this.m_cbHubs);
      interactionsBoard.UnregisterListenerInt(interactions.ActiveChoiceHubID, this.m_cbActive);
      interactionsBoard.UnregisterListenerInt(interactions.SelectedIndex, this.m_cbSelected);
      interactionsBoard.UnregisterListenerVariant(interactions.LastAttemptedChoice, this.m_cbAttempt);
      this.m_hostListening = false;
    }
    this.ClearMarks();
    if this.m_ownListening {
      this.Board(true).UnregisterListenerVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, this.m_cbOwn);
      this.m_ownListening = false;
    }
    this.Close();
  }

  // The host's hub is up: its scene waits for the choice (Scenes.reds).
  public func HubOpen() -> Bool {
    return this.m_sentOpen;
  }

  // UIGameData (true) or UIInteractions (false).
  private func Board(game: Bool) -> ref<IBlackboard> {
    let system = GameInstance.GetBlackboardSystem(this.m_game);
    return game ? system.Get(GetAllBlackboardDefs().UIGameData) : system.Get(GetAllBlackboardDefs().UIInteractions);
  }

  public func OnEvent(event: TandemStoryEvent) -> Void {
    switch event.kind {
      case 30:
        this.OnHostLine(event);
        break;
      case 31:
        this.OnHostHub(event);
        break;
      case 32:
        this.OnHostOption(event);
        break;
      case 33:
        this.OnCursor(event);
        break;
      default:
        break;
    }
  }

  // ---- host ----

  public func HostStart() -> Void {
    if this.m_hostListening {
      return;
    }
    let game = GetAllBlackboardDefs().UIGameData;
    let interactions = GetAllBlackboardDefs().UIInteractions;
    let gameBoard = this.Board(true);
    let interactionsBoard = this.Board(false);
    this.m_cbShow = gameBoard.RegisterListenerVariant(game.ShowDialogLine, this, n"OnShowLine");
    this.m_cbHide = gameBoard.RegisterListenerVariant(game.HideDialogLine, this, n"OnHideLine");
    this.m_cbHubs = interactionsBoard.RegisterListenerVariant(interactions.DialogChoiceHubs, this, n"OnHubs");
    this.m_cbActive = interactionsBoard.RegisterListenerInt(interactions.ActiveChoiceHubID, this, n"OnActiveHub");
    this.m_cbSelected = interactionsBoard.RegisterListenerInt(interactions.SelectedIndex, this, n"OnSelected");
    this.m_cbAttempt = interactionsBoard.RegisterListenerVariant(interactions.LastAttemptedChoice, this, n"OnAttempted");
    this.m_hostListening = true;
  }

  protected cb func OnShowLine(value: Variant) -> Bool {
    if !this.m_story.IsHostInSession() {
      return false;
    }
    let lines = FromVariant<array<scnDialogLineData>>(value);
    let player = GetPlayer(this.m_game);
    for line in lines {
      let kind: Int32 = EnumInt(line.type);
      // Radio and TV are each game's own; scene comments and invisible
      // lines are not for players.
      let shared = !(kind == 3 || kind == 5 || kind == 6 || kind == 7 || kind == 11);
      let speaker = 0u;
      if shared {
        if IsDefined(line.speaker) && IsDefined(player) && line.speaker.GetEntityID() == player.GetEntityID() {
          speaker = this.m_story.You();
        }
        TandemStory.SendLine(TandemStory.LineValue(line.id), kind, line.isPersistent ? 2 : 0, line.duration, speaker, line.speakerName, line.text);
        TandemNative.Log(s"subtitle: sent (type \(kind))");
        // Its voice (scene replay): after the subtitle, so a guest has the
        // text when the voice starts.
        this.m_story.Scenes().HostLine(TandemStory.LineValue(line.id), kind, line.speaker, speaker, line.duration);
      }
    }
  }

  protected cb func OnHideLine(value: Variant) -> Bool {
    if !this.m_story.IsHostInSession() {
      return false;
    }
    let ids = FromVariant<array<CRUID>>(value);
    for id in ids {
      TandemStory.SendLine(TandemStory.LineValue(id), 0, 1, 0.0, 0u, "", "");
      this.m_story.Scenes().HostLineHidden(TandemStory.LineValue(id));
    }
  }

  protected cb func OnHubs(value: Variant) -> Bool {
    TandemNative.HookRan(n"Dialogue.TandemDialogue.OnHubs");
    this.m_hubs = FromVariant<DialogChoiceHubs>(value);
    this.SendActiveHub();
  }

  protected cb func OnActiveHub(id: Int32) -> Bool {
    TandemNative.HookRan(n"Dialogue.TandemDialogue.OnActiveHub");
    this.m_activeHub = id;
    this.SendActiveHub();
  }

  protected cb func OnSelected(index: Int32) -> Bool {
    if this.m_story.IsHostInSession() && this.m_sentOpen {
      TandemStory.SendCursor(this.m_sentHub, index, 0);
    }
  }

  protected cb func OnAttempted(value: Variant) -> Bool {
    let choice = FromVariant<InteractionAttemptedChoice>(value);
    if this.m_story.IsHostInSession() && this.m_sentOpen && choice.isSuccess
      && Equals(choice.visualizerType, EVisualizerType.Dialog) {
      TandemStory.SendCursor(this.m_sentHub, choice.choiceIdx, 2);
      TandemNative.Log(s"choice: we picked \(choice.choiceIdx)");
    }
  }

  // Only the active hub counts (not hubs of NPCs we walk past).
  private func SendActiveHub() -> Void {
    if !this.m_story.IsHostInSession() {
      return;
    }
    for hub in this.m_hubs.choiceHubs {
      if hub.id == this.m_activeHub && ArraySize(hub.choices) > 0 {
        let texts: array<String>;
        let types: array<Int32>;
        let key = s"\(hub.id) \(hub.title)";
        for choice in hub.choices {
          let bits = 0;
          let t = 0;
          let bit = 1;
          while t < 16 {
            if ChoiceTypeWrapper.IsType(choice.type, IntEnum<gameinteractionsChoiceType>(t)) {
              bits += bit;
            }
            bit *= 2;
            t += 1;
          }
          ArrayPush(texts, choice.localizedName);
          ArrayPush(types, bits);
          key += s"|\(choice.localizedName)/\(bits)";
        }
        if this.m_sentOpen && Equals(key, this.m_sentKey) {
          return;
        }
        TandemStory.SendHub(hub.id, hub.isPhoneLockActive ? 1 : 0, hub.title, texts, types, 0.0);
        TandemNative.Log(s"choice: our hub \(hub.id) sent (\(ArraySize(texts)) options)");
        this.m_sentOpen = true;
        this.m_sentHub = hub.id;
        this.m_sentKey = key;
        this.m_sentTexts = texts;
        // The guests' cursors were on the hub before; ours goes on theirs.
        this.ClearMarks();
        TandemStory.SendCursor(hub.id, this.Board(false).GetInt(GetAllBlackboardDefs().UIInteractions.SelectedIndex), 0);
        return;
      }
    }
    if this.m_sentOpen {
      if this.m_testHub && this.m_sentHub == 4242 {
        return; // (test) held: the tick puts it back on the board
      }
      let none: array<String>;
      let noTypes: array<Int32>;
      TandemStory.SendHub(this.m_sentHub, 0, "", none, noTypes, 0.0);
      TandemNative.Log("choice: our hub closed");
      this.m_sentOpen = false;
      this.m_sentKey = "";
      this.ClearMarks();
    }
  }

  // ---- either: cursors ----

  // A cursor moved (0), a suggestion (1), the host's pick (2). Another
  // player's cursor on our hub is marked there at once; a suggestion the
  // host cannot see on its hub (not on screen: a menu, the HUD hidden) is
  // its on-screen notice.
  private func OnCursor(event: TandemStoryEvent) -> Void {
    if this.m_story.IsHostInSession() {
      if !this.m_sentOpen || event.extra != this.m_sentHub || event.index < 0 || event.index >= ArraySize(this.m_sentTexts) || event.state > 1 {
        return;
      }
      this.SetMark(event.player, event.index, event.state);
      let marked = this.DrawMarks();
      if event.state == 1 {
        if !marked {
          this.m_story.Notice("Dialogue-Suggests", this.m_story.NameOf(event.player), this.m_sentTexts[event.index]);
        }
        TandemNative.Log(s"choice: player \(event.player) suggests \(event.index)\(marked ? " (marked on the hub)" : " (the notice: our hub is not on screen)")");
      }
      return;
    }
    if event.player == this.m_story.HostPlayer() && event.state == 2 {
      TandemNative.Log(s"choice: the host picked \(event.index)");
      this.CloseHub();
      return;
    }
    if event.player != this.m_story.You() && event.state <= 1 && event.extra == this.m_pendingHub && event.index >= 0 {
      this.SetMark(event.player, event.index, event.state);
      this.DrawMarks();
    }
  }

  // ---- either: the others' markers on our hub ----

  private func SetMark(player: Uint32, index: Int32, kind: Int32) -> Void {
    let i = ArrayFindFirst(this.m_markPlayers, player);
    if i < 0 {
      ArrayPush(this.m_markPlayers, player);
      ArrayPush(this.m_markIndexes, index);
      ArrayPush(this.m_markKinds, kind);
      return;
    }
    // A hover after a suggestion on the same option keeps the suggestion.
    if kind == 0 && this.m_markIndexes[i] == index && this.m_markKinds[i] == 1 {
      return;
    }
    this.m_markIndexes[i] = index;
    this.m_markKinds[i] = kind;
  }

  private func ClearMarks() -> Void {
    ArrayClear(this.m_markPlayers);
    ArrayClear(this.m_markIndexes);
    ArrayClear(this.m_markKinds);
    this.DrawMarks();
  }

  // Ten times a second (Story.reds Tick): the markers follow the choice
  // widgets (a hub redrawn spawns new option widgets, without ours).
  public func Tick(now: Float) -> Void {
    this.m_now = now;
    if this.m_testHub && !this.TestHubUp() {
      this.m_testHubBack += 1;
      if this.m_testHubBack == 1 {
        TandemNative.Log("story: test hub put back on the board (the game took it off)");
      }
      this.ShowTestHub();
    }
    if ArraySize(this.m_markPlayers) > 0 || StrLen(this.m_marksKey) > 0 {
      this.DrawMarks();
    }
  }

  // Our hub, by the id the choice widget knows it by (-1: none shown).
  private func OurHub() -> Int32 {
    if this.m_story.IsHostInSession() {
      return this.m_sentOpen ? this.m_sentHub : -1;
    }
    return this.m_shown ? this.StandIn(this.m_shownHub) : -1;
  }

  // True: drawn on our hub, on screen.
  private func DrawMarks() -> Bool {
    let hub = this.OurHub();
    let indexes: array<Int32>;
    let labels: array<String>;
    let colors: array<HDRColor>;
    let key = "";
    let i = 0;
    while hub >= 0 && i < ArraySize(this.m_markPlayers) {
      let player = this.m_markPlayers[i];
      if TandemSync.InSession(player) {
        let name = this.m_story.NameOf(player);
        let suggests = this.m_markKinds[i] == 1;
        ArrayPush(indexes, this.m_markIndexes[i]);
        ArrayPush(labels, suggests ? TandemText.F("Dialogue-MarkSuggests", name) : name);
        ArrayPush(colors, TandemMarkers.ColorOf(player));
        if StrLen(key) > 0 {
          key += ", ";
        }
        key += s"\(name) on \(this.m_markIndexes[i])\(suggests ? " (suggests)" : "")";
      }
      i += 1;
    }
    if StrLen(key) > 0 {
      key = s"hub \(hub): \(key)";
    }
    let widget = this.Widget();
    if !IsDefined(widget) {
      this.m_marksOnScreen = false;
      return false;
    }
    let shown = widget.TandemMarks(hub, indexes, labels, colors);
    if !Equals(key, this.m_marksKey) && (shown || StrLen(key) == 0) {
      TandemNative.Log(StrLen(key) > 0 ? s"choice: marks on \(key)" : "choice: marks cleared");
      this.m_marksKey = key;
    }
    this.m_marksOnScreen = shown && StrLen(key) > 0;
    return this.m_marksOnScreen;
  }

  // The game's choice widget on the HUD layer (Codeware's controllers),
  // looked for again at most once a second while it is missing.
  private func Widget() -> wref<dialogWidgetGameController> {
    if IsDefined(this.m_widget) {
      return this.m_widget;
    }
    let now = this.m_now;
    if this.m_widgetScannedAt > 0.0 && now >= this.m_widgetScannedAt && now - this.m_widgetScannedAt < 1.0 {
      return null;
    }
    this.m_widgetScannedAt = now;
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    if !IsDefined(layer) {
      return null;
    }
    for controller in layer.GetGameControllers() {
      let widget = controller as dialogWidgetGameController;
      if IsDefined(widget) {
        this.m_widget = widget;
        TandemNative.Log("choice: the game's choice widget found");
        return widget;
      }
    }
    return null;
  }

  // ---- guest ----

  private func OnHostLine(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsGuest() {
      return;
    }
    let board = this.Board(true);
    if !this.m_ownListening {
      this.m_cbOwn = board.RegisterListenerVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, this, n"OnOwnLine");
      this.m_ownListening = true;
    }
    if (event.flags & 1) != 0 {
      if ArrayContains(this.m_lines, event.line) {
        ArrayRemove(this.m_lines, event.line);
        let ids: array<CRUID>;
        ArrayPush(ids, TandemStory.LineId(TandemStory.StandInLine(event.line)));
        board.SetVariant(GetAllBlackboardDefs().UIGameData.HideDialogLine, ToVariant(ids), true);
      }
      return;
    }
    // Our game shows this line itself (its voice plays here): once is enough.
    if ArrayContains(this.m_native, event.line) {
      TandemNative.Log("subtitle: not shown twice (the game shows it itself)");
      return;
    }
    // Holocall lines: everyone on the call. The rest: within earshot, and
    // never from the host's private scene.
    if event.index != 2 && this.m_story.HostBusy() {
      return;
    }
    // On the host's call, the host's V's own lines are part of it too.
    let distance = this.m_story.DistanceToHost();
    // Watching the host's moment, we are where its camera is (Spectate.reds).
    let onCall = this.m_story.InHostCall() || TandemStory.SpectateWatching();
    if event.index != 2 && !onCall && (distance < 0.0 || distance > 25.0) {
      return;
    }
    let line: scnDialogLineData;
    // Under an id of our own: a line our game shows itself has the real one.
    line.id = TandemStory.LineId(TandemStory.StandInLine(event.line));
    line.text = event.text;
    line.speakerName = event.name;
    line.duration = event.duration;
    line.isPersistent = (event.flags & 2) != 0;
    line.type = IntEnum<scnDialogLineType>(event.index);
    if event.player != 0u {
      line.speaker = this.m_story.PuppetOf(event.player);
    }
    if !IsDefined(line.speaker) && event.index == 1 {
      line.type = scnDialogLineType.OwnerlessRegular;
    }
    let lines: array<scnDialogLineData>;
    ArrayPush(lines, line);
    board.SetVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, ToVariant(lines), true);
    ArrayPush(this.m_lines, event.line);
    if ArraySize(this.m_lines) > 64 {
      ArrayErase(this.m_lines, 0);
    }
    TandemNative.Log("subtitle: shown");
  }

  // Our own subtitle board: a line the host's game sent us that our game now
  // shows itself, under its real id (in our language: the better one). Ours
  // goes; no line is ever on screen twice.
  protected cb func OnOwnLine(value: Variant) -> Bool {
    TandemNative.HookRan(n"Dialogue.TandemDialogue.OnOwnLine");
    if !this.m_story.IsGuest() {
      return false;
    }
    let lines = FromVariant<array<scnDialogLineData>>(value);
    for line in lines {
      let id = TandemStory.LineValue(line.id);
      if ArrayContains(this.m_lines, id) {
        ArrayRemove(this.m_lines, id);
        let ids: array<CRUID>;
        ArrayPush(ids, TandemStory.LineId(TandemStory.StandInLine(id)));
        this.Board(true).SetVariant(GetAllBlackboardDefs().UIGameData.HideDialogLine, ToVariant(ids), true);
        ArrayPush(this.m_native, id);
        if ArraySize(this.m_native) > 64 {
          ArrayErase(this.m_native, 0);
        }
        TandemNative.Log("subtitle: the game shows it itself: ours hidden");
      }
    }
  }

  private func OnHostHub(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsGuest() {
      return;
    }
    if event.extra != this.m_pendingHub || event.index == 0 {
      this.ClearMarks();
    }
    this.m_pendingHub = event.extra;
    this.m_pendingCount = event.index;
    this.m_pendingFlags = event.flags;
    this.m_pendingTitle = event.name;
    ArrayClear(this.m_pendingTexts);
    ArrayClear(this.m_pendingTypes);
    if event.index == 0 {
      this.CloseHub();
    }
  }

  private func OnHostOption(event: TandemStoryEvent) -> Void {
    if !this.m_story.IsGuest() || event.extra != this.m_pendingHub {
      return;
    }
    ArrayPush(this.m_pendingTexts, event.text);
    ArrayPush(this.m_pendingTypes, event.flags);
    if ArraySize(this.m_pendingTexts) == this.m_pendingCount {
      this.PresentHub();
    }
  }

  // A stand-in hub of our own (the fork's idea, with an id no game uses).
  private func StandIn(hub: Int32) -> Int32 {
    return 7700000 + (hub % 100000);
  }

  private func PresentHub() -> Void {
    // A call's hub (the game's phone lock, or a hub while we present the
    // host's call) is everyone's on the call; the rest, within earshot.
    let phoneLock = (this.m_pendingFlags & 1) != 0 || this.m_story.InHostCall() || TandemStory.SpectateWatching();
    let distance = this.m_story.DistanceToHost();
    if !phoneLock && (distance < 0.0 || distance > 25.0 || this.m_story.HostBusy()) {
      TandemNative.Log(s"choice: hub not shown (\(this.m_story.HostBusy() ? "the host's private scene" : "too far from the host"))");
      return;
    }
    let data: ListChoiceHubData;
    data.id = this.StandIn(this.m_pendingHub);
    data.activityState = EVisualizerActivityState.Active;
    data.isPhoneLockActive = phoneLock;
    data.title = this.m_pendingTitle;
    let i = 0;
    while i < ArraySize(this.m_pendingTexts) {
      let choice: ListChoiceData;
      choice.localizedName = this.m_pendingTexts[i];
      choice.inputActionName = n"ChoiceApply";
      let t = 0;
      let bits = this.m_pendingTypes[i];
      while t < 16 {
        if (bits & 1) != 0 {
          ChoiceTypeWrapper.SetType(choice.type, IntEnum<gameinteractionsChoiceType>(t));
        }
        bits = bits / 2;
        t += 1;
      }
      ArrayPush(data.choices, choice);
      i += 1;
    }
    let hubs: DialogChoiceHubs;
    ArrayPush(hubs.choiceHubs, data);
    let board = this.Board(false);
    let defs = GetAllBlackboardDefs().UIInteractions;
    board.SetVariant(defs.DialogChoiceHubs, ToVariant(hubs), true);
    board.SetInt(defs.ActiveChoiceHubID, data.id, true);
    this.m_cursor = 0;
    board.SetInt(defs.SelectedIndex, 0, true);
    if !this.m_shown {
      let player = GetPlayer(this.m_game);
      player.RegisterInputListener(this, n"ChoiceScrollUp");
      player.RegisterInputListener(this, n"ChoiceScrollDown");
      player.RegisterInputListener(this, n"ChoiceApply");
    }
    this.m_shown = true;
    this.m_shownHub = this.m_pendingHub;
    // Where we start, marked on the host's hub (and the others').
    TandemStory.SendCursor(this.m_shownHub, 0, 0);
    TandemNative.Log(s"choice: hub shown (\(ArraySize(this.m_pendingTexts)) options)");
  }

  private func CloseHub() -> Void {
    if !this.m_shown {
      return;
    }
    this.m_shown = false;
    this.ClearMarks();
    let hubs: DialogChoiceHubs;
    let board = this.Board(false);
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs, ToVariant(hubs), true);
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      player.UnregisterInputListener(this, n"ChoiceScrollUp");
      player.UnregisterInputListener(this, n"ChoiceScrollDown");
      player.UnregisterInputListener(this, n"ChoiceApply");
    }
    TandemNative.Log("choice: hub closed");
  }

  // Our own cursor on the stand-in hub; apply marks a suggestion.
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Dialogue.TandemDialogue.OnAction");
    if !this.m_shown {
      return false;
    }
    let name = ListenerAction.GetName(action);
    let count = ArraySize(this.m_pendingTexts);
    if count == 0 {
      return false;
    }
    if ListenerAction.IsButtonJustPressed(action) && (Equals(name, n"ChoiceScrollUp") || Equals(name, n"ChoiceScrollDown")) {
      this.m_cursor = Equals(name, n"ChoiceScrollUp") ? (this.m_cursor + count - 1) % count : (this.m_cursor + 1) % count;
      this.Board(false).SetInt(GetAllBlackboardDefs().UIInteractions.SelectedIndex, this.m_cursor, true);
      TandemStory.SendCursor(this.m_shownHub, this.m_cursor, 0);
    } else if Equals(name, n"ChoiceApply") && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED)
      && this.m_cursor >= 0 && this.m_cursor < count {
      TandemStory.SendCursor(this.m_shownHub, this.m_cursor, 1);
      this.m_story.Notice("Dialogue-Suggested", this.m_pendingTexts[this.m_cursor]);
      TandemNative.Log(s"choice: suggested \(this.m_cursor)");
    }
  }

  // Leaving: no hub or line of the host's stays on screen.
  public func Close() -> Void {
    this.CloseHub();
    if ArraySize(this.m_lines) > 0 {
      let ids: array<CRUID>;
      for line in this.m_lines {
        ArrayPush(ids, TandemStory.LineId(TandemStory.StandInLine(line)));
      }
      this.Board(true).SetVariant(GetAllBlackboardDefs().UIGameData.HideDialogLine, ToVariant(ids), true);
      ArrayClear(this.m_lines);
    }
  }

  // ---- test commands (story-host.sh): our own vanilla paths fire ----

  public func Command(command: String) -> Void {
    let player = GetPlayer(this.m_game);
    if StrBeginsWith(command, "story line") {
      let line: scnDialogLineData;
      this.m_testLine += Cast<Uint64>(1);
      line.id = TandemStory.LineId(Cast<Uint64>(990000) + this.m_testLine);
      line.text = StrLen(command) > 11 ? StrAfterFirst(command, "story line ") : "Test line."; // text: test only
      line.type = scnDialogLineType.Regular;
      line.speaker = player;
      line.speakerName = "V"; // text: test only
      line.duration = 3.0;
      let lines: array<scnDialogLineData>;
      ArrayPush(lines, line);
      this.Board(true).SetVariant(GetAllBlackboardDefs().UIGameData.ShowDialogLine, ToVariant(lines), true);
      TandemNative.Log("story: test line shown");
    } else if Equals(command, "story hub off") {
      // (test reset) only a hub a test still holds.
      if this.m_testHub {
        this.Command("story hub close");
      }
    } else if Equals(command, "story hub close") {
      if this.m_testHub {
        TandemNative.Log(s"story: test hub was put back \(this.m_testHubBack) time(s)");
      }
      this.m_testHub = false;
      this.m_testHubBack = 0;
      let hubs: DialogChoiceHubs;
      let board = this.Board(false);
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs, ToVariant(hubs), true);
      board.SetInt(GetAllBlackboardDefs().UIInteractions.ActiveChoiceHubID, -1, true);
      TandemNative.Log("story: test hub closed");
    } else if StrBeginsWith(command, "story hub") {
      this.m_testHub = true;
      this.m_testHubBack = 0;
      this.ShowTestHub();
      TandemNative.Log("story: test hub shown");
    }
  }

  // (test) The test hub is the board's active hub, as we last heard it.
  private func TestHubUp() -> Bool {
    if this.m_activeHub != 4242 {
      return false;
    }
    for hub in this.m_hubs.choiceHubs {
      if hub.id == 4242 {
        return true;
      }
    }
    return false;
  }

  private func ShowTestHub() -> Void {
    let data: ListChoiceHubData;
    data.id = 4242;
    data.activityState = EVisualizerActivityState.Active;
    data.title = "Test"; // text: test only
    let yes: ListChoiceData;
    yes.localizedName = TandemText.Vanilla("UI-UserActions-Yes");
    yes.inputActionName = n"ChoiceApply";
    let no: ListChoiceData;
    no.localizedName = TandemText.Vanilla("UI-UserActions-No");
    no.inputActionName = n"ChoiceApply";
    ArrayPush(data.choices, yes);
    ArrayPush(data.choices, no);
    let hubs: DialogChoiceHubs;
    ArrayPush(hubs.choiceHubs, data);
    let board = this.Board(false);
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs, ToVariant(hubs), true);
    board.SetInt(GetAllBlackboardDefs().UIInteractions.ActiveChoiceHubID, 4242, true);
  }
}

// ---- the others' markers on the game's choice hub (final.reds:399949) ----
//
// Called from TandemDialogue.DrawMarks only, on our story tick (the game
// thread): no wrap, no callback. A marker is a widget of ours added to an
// option's row: a chip and the player's name in their colour
// (TandemMarkers.ColorOf), or "NAME suggests".

// The markers on the hub `hub` (an id of DialogChoiceHubs), none on the
// others. True: that hub is drawn and visible.
@addMethod(dialogWidgetGameController)
public final func TandemMarks(hub: Int32, indexes: array<Int32>, labels: array<String>, colors: array<HDRColor>) -> Bool {
  let noIndexes: array<Int32>;
  let noLabels: array<String>;
  let noColors: array<HDRColor>;
  let shown = false;
  for controller in this.m_hubControllers {
    if IsDefined(controller) {
      if hub >= 0 && controller.GetId() == hub {
        if controller.TandemMarks(indexes, labels, colors) && this.GetRootWidget().IsVisible() {
          shown = true;
        }
      } else {
        controller.TandemMarks(noIndexes, noLabels, noColors);
      }
    }
  }
  return shown;
}

// Each option's markers. True: the hub has its options and is visible.
@addMethod(DialogHubLogicController)
public final func TandemMarks(indexes: array<Int32>, labels: array<String>, colors: array<HDRColor>) -> Bool {
  let names: array<String>;
  let tints: array<HDRColor>;
  let i = 0;
  for item in this.m_itemControllers {
    ArrayClear(names);
    ArrayClear(tints);
    let k = 0;
    while k < ArraySize(indexes) {
      if indexes[k] == i {
        ArrayPush(names, labels[k]);
        ArrayPush(tints, colors[k]);
      }
      k += 1;
    }
    if IsDefined(item) {
      item.TandemMark(names, tints);
    }
    i += 1;
  }
  return ArraySize(this.m_itemControllers) > 0 && IsDefined(this.m_rootWidget) && this.m_rootWidget.IsVisible();
}

@addField(DialogChoiceLogicController)
private let m_tandemMarks: wref<inkHorizontalPanel>;

@addField(DialogChoiceLogicController)
private let m_tandemMarksKey: String;

// This option's markers, after its text (in the row that holds the text,
// else at the option's right edge); redrawn only when they change.
@addMethod(DialogChoiceLogicController)
public final func TandemMark(labels: array<String>, colors: array<HDRColor>) -> Void {
  let key = "";
  for label in labels {
    key += label + "|";
  }
  if Equals(key, this.m_tandemMarksKey) {
    return;
  }
  this.m_tandemMarksKey = key;
  if !IsDefined(this.m_tandemMarks) {
    if ArraySize(labels) == 0 {
      return;
    }
    let panel = new inkHorizontalPanel();
    panel.SetName(n"tandem_marks");
    panel.SetFitToContent(true);
    panel.SetInteractive(false);
    panel.SetVAlign(inkEVerticalAlign.Center);
    panel.SetMargin(new inkMargin(24.0, 0.0, 0.0, 0.0));
    let row = IsDefined(this.m_TextFlex) ? this.m_TextFlex.GetParentWidget() as inkHorizontalPanel : null;
    if IsDefined(row) {
      panel.Reparent(row);
    } else {
      panel.SetAnchor(inkEAnchor.CenterRight);
      panel.SetAnchorPoint(new Vector2(0.0, 0.5));
      panel.Reparent(this.m_RootWidget);
    }
    this.m_tandemMarks = panel;
  }
  this.m_tandemMarks.RemoveAllChildren();
  let i = 0;
  while i < ArraySize(labels) {
    let chip = new inkRectangle();
    chip.SetSize(new Vector2(6.0, 30.0));
    chip.SetTintColor(colors[i]);
    chip.SetVAlign(inkEVerticalAlign.Center);
    chip.SetMargin(new inkMargin(i == 0 ? 0.0 : 20.0, 0.0, 8.0, 0.0));
    chip.SetInteractive(false);
    chip.Reparent(this.m_tandemMarks);
    let text = new inkText();
    text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
    text.SetFontStyle(n"Medium");
    text.SetFontSize(30);
    text.SetLetterCase(textLetterCase.UpperCase);
    text.SetText(labels[i]);
    text.SetTintColor(colors[i]);
    text.SetVAlign(inkEVerticalAlign.Center);
    text.SetFitToContent(true);
    text.SetInteractive(false);
    text.Reparent(this.m_tandemMarks);
    i += 1;
  }
  this.m_tandemMarks.SetVisible(ArraySize(labels) > 0);
  if ArraySize(labels) > 0 {
    // The mark arrives: a short fade in.
    let anim = new inkAnimDef();
    let fade = new inkAnimTransparency();
    fade.SetStartTransparency(0.0);
    fade.SetEndTransparency(1.0);
    fade.SetDuration(0.2);
    anim.AddInterpolator(fade);
    this.m_tandemMarks.PlayAnimation(anim);
  }
}
