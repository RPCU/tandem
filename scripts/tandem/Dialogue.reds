// Dialogue in co-op (docs/research/coop-quests-design.md §2.2, §2.4): the
// host's subtitles and choice hubs on the guests' screens, through their own
// vanilla widgets (the subtitle controller listens to UIGameData, final.reds
// :268284; the dialogue hub to UIInteractions, :399949). Face-to-face lines
// and hubs reach guests within 25 m of the host's V; holocall lines and phone
// hubs reach everyone. The host's V decides: a guest moves its own cursor on
// the hub and can mark a suggestion, which the host sees; the host's pick
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
    this.m_hubs = FromVariant<DialogChoiceHubs>(value);
    this.SendActiveHub();
  }

  protected cb func OnActiveHub(id: Int32) -> Bool {
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
        return;
      }
    }
    if this.m_sentOpen {
      let none: array<String>;
      let noTypes: array<Int32>;
      TandemStory.SendHub(this.m_sentHub, 0, "", none, noTypes, 0.0);
      TandemNative.Log("choice: our hub closed");
      this.m_sentOpen = false;
      this.m_sentKey = "";
    }
  }

  // ---- either: cursors ----

  private func OnCursor(event: TandemStoryEvent) -> Void {
    if this.m_story.IsHostInSession() {
      // A guest's suggestion, for the host to see (its hover: TODO, a marker
      // on the option through a wrap of UpdateDialogsData).
      if event.state == 1 && event.index >= 0 && event.index < ArraySize(this.m_sentTexts) {
        this.m_story.Notice("Dialogue-Suggests", this.m_story.NameOf(event.player), this.m_sentTexts[event.index]);
        TandemNative.Log(s"choice: player \(event.player) suggests \(event.index)");
      }
      return;
    }
    if event.player == this.m_story.HostPlayer() && event.state == 2 {
      TandemNative.Log(s"choice: the host picked \(event.index)");
      this.CloseHub();
    }
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
    let onCall = this.m_story.InHostCall();
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
    let phoneLock = (this.m_pendingFlags & 1) != 0 || this.m_story.InHostCall();
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
    TandemNative.Log(s"choice: hub shown (\(ArraySize(this.m_pendingTexts)) options)");
  }

  private func CloseHub() -> Void {
    if !this.m_shown {
      return;
    }
    this.m_shown = false;
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

  public func GuestTick() -> Void {}

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
    } else if Equals(command, "story hub close") {
      let hubs: DialogChoiceHubs;
      let board = this.Board(false);
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs, ToVariant(hubs), true);
      board.SetInt(GetAllBlackboardDefs().UIInteractions.ActiveChoiceHubID, -1, true);
      TandemNative.Log("story: test hub closed");
    } else if StrBeginsWith(command, "story hub") {
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
      TandemNative.Log("story: test hub shown");
    }
  }
}
