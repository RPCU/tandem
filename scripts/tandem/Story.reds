// Story sync, milestone 6 step 1 (docs/research/coop-quests-design.md).
// The host's game runs the story; a guest's own story waits (the quest
// freeze, plugin/src/engine/quests.cpp), its saving is locked, and the host's
// story is shown through the guest's own systems: journal, tracker and map
// pins (here), phone (Phone.reds), subtitles and choice hubs (Dialogue.reds),
// rewards (here). Leaving puts the guest's journal back as it was.
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemStoryTick extends DelayCallback {
  public let system: wref<TandemStorySystem>;

  public func Call() -> Void {
    if IsDefined(this.system) {
      this.system.Tick();
    }
  }
}

public class TandemStorySystem extends ScriptableSystem {
  private let m_inGame: Bool;
  private let m_attachedAt: Float;
  private let m_playing: Bool;
  private let m_you: Uint32;
  private let m_host: Uint32;
  private let m_isHost: Bool;
  private let m_journal: wref<JournalManager>;
  private let m_phone: ref<TandemPhone>;
  private let m_dialogue: ref<TandemDialogue>;
  private let m_scenes: ref<TandemScenes>;

  // Host: the journal snapshot is owed (sent once the world plays), and the
  // changes since the last tick, sent together.
  private let m_snapshotOwed: Bool;
  private let m_changedHashes: array<Uint32>;
  private let m_changedStates: array<Int32>;
  private let m_changedNotify: array<Bool>;
  private let m_trackedOwed: Bool;
  private let m_listening: Bool;
  private let m_ourTier: Int32;

  // Guest: saving locked, own quests frozen, and every journal entry the
  // mirror changed with its state before (what leaving puts back).
  private let m_saveLocked: Bool;
  private let m_frozen: Bool;
  private let m_warnedNoFreeze: Bool;
  private let m_touched: array<Uint32>;
  private let m_touchedBefore: array<Int32>;
  private let m_ownTracked: Uint32;
  private let m_ownTrackedKept: Bool;
  private let m_inSnapshot: Bool;
  // The host's journal as last heard: its snapshot, then every change.
  private let m_snapshotHashes: array<Uint32>;
  private let m_snapshotStates: array<Int32>;
  private let m_lastAutosaveDrop: Float;
  private let m_unknownEntries: Int32;

  // Guest: the host's scene tier and what we hold for it.
  private let m_hostTier: Int32;
  private let m_holdCombat: Bool;
  private let m_holdGod: Bool;
  private let m_offerShown: Bool;
  private let m_offerOpen: Bool;
  // The host's story flags (StoryState: 1 braindance, 2 plays as Johnny),
  // and ours as last sent; story effects of the host's V we wear too.
  private let m_hostFlags: Int32;
  private let m_ourFlags: Int32;
  private let m_lastFlagsCheck: Float;
  private let m_effects: array<TweakDBID>;
  // Johnny or Songbird around our V (theirs, or a proxy of the host's).
  private let m_companions: String;
  // Private scenes (design §2.12): ours (and its class: 2 minor, 3 romance),
  // the puppets we hid for it, and the other players busy in theirs.
  private let m_others: array<Uint32>;
  private let m_inPrivate: Bool;
  private let m_forcedPrivate: Bool;
  private let m_lastPrivateCheck: Float;
  private let m_hiddenPuppets: array<Uint32>;
  private let m_busy: array<Uint32>;
  private let m_lastCompanionScan: Float;

  // Quest rewards this V got in co-op (its own save keeps them: a quest it
  // was rewarded for with a host pays nothing again in its own story).
  // Pairs (reward record, its quest's journal hash): generic records are
  // shared by many quests, so the quest is part of the key.
  private persistent let m_rewarded: array<TweakDBID>;
  private persistent let m_rewardedQuests: array<Uint32>;
  // Session items (docs/architecture/quest-carryover.md): the host's quest
  // props (tag Quest, keycards) our V holds as a guest. Not ours to keep:
  // taken back out when we leave, and at the next load if a save ever held
  // them (persistent for that).
  private persistent let m_sessionItems: array<TweakDBID>;
  private persistent let m_sessionCounts: array<Int32>;
  // (regions, later) apart from the host: our own gigs run and their journal
  // entries stay ours. Always grouped until the regions work says otherwise.
  private let m_apart: Bool;

  // The quest whose journal moved last, and when (a reward in our own story
  // belongs to it: the host's rule, core/story.hpp).
  private let m_lastQuest: Uint32;
  private let m_lastQuestAt: Float;

  public static func Get(game: GameInstance) -> ref<TandemStorySystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemStorySystem") as TandemStorySystem;
  }

  // Vanilla asks every scriptable system (ScriptableSystem.IsSavingLocked,
  // final.reds:48411; SaveLocksManager's :195157): a guest in a session
  // never saves (its world is the host's story on top of its own).
  private func IsSavingLocked() -> Bool {
    return this.m_saveLocked;
  }

  private final func OnPlayerAttach(request: ref<PlayerAttachRequest>) -> Void {
    if GameInstance.GetSystemRequestsHandler().IsPreGame() {
      return;
    }
    this.Begin();
  }

  // At the load, or when the co-op menu starts a session in a loaded game
  // (TandemSystem.BeginSession): either way after the session was chosen.
  public func Begin() -> Void {
    if !TandemStory.StoryOn() {
      return;
    }
    if this.m_inGame {
      // Already listening (a role the menu changed is taken at the session's
      // start, after the old one's end: HandleEvents).
      return;
    }
    this.m_inGame = true;
    this.m_playing = false;
    this.m_attachedAt = this.Now();
    this.m_isHost = TandemNative.IsHost();
    this.m_journal = GameInstance.GetJournalManager(this.GetGameInstance());
    this.m_phone = new TandemPhone();
    this.m_phone.Start(this.GetGameInstance(), this);
    this.m_dialogue = new TandemDialogue();
    this.m_dialogue.Start(this.GetGameInstance(), this);
    this.m_scenes = new TandemScenes();
    this.m_scenes.Start(this.GetGameInstance(), this);
    // Journal changes: the host sends them; every game keeps the last quest
    // that moved (rewards, below).
    this.Listen();
    // A save that held session items (a session that did not end cleanly):
    // out they go once the world plays (Tick), now that it is our own.
    if this.m_you == 0u && ArraySize(this.m_sessionItems) > 0 {
      TandemNative.Log("story: this save holds quest props of a co-op session: taken out once the world plays");
    }
    // A load brings the save's own lock list back: ours again if in a session.
    if this.m_saveLocked {
      SaveLocksManager.RequestSaveLockAdd(this.GetGameInstance(), n"tandem");
    }
    // The host loaded a save in a session: its journal is another one now.
    if this.m_isHost && this.m_you != 0u {
      this.HostStart();
    }
    this.Schedule();
  }

  private final func OnPlayerDetach(request: ref<PlayerDetachRequest>) -> Void {
    this.End();
  }

  // The story side off: the world goes away, or the co-op menu left the
  // session and no other is chosen (solo is vanilla again).
  public func End() -> Void {
    if !this.m_inGame {
      return;
    }
    this.m_inGame = false;
    if IsDefined(this.m_dialogue) {
      this.m_dialogue.Stop();
    }
    if IsDefined(this.m_phone) {
      this.m_phone.Stop();
    }
    if this.m_listening && IsDefined(this.m_journal) {
      this.m_journal.UnregisterScriptCallback(this, n"OnJournalState");
      this.m_journal.UnregisterScriptCallback(this, n"OnJournalTracked");
    }
    this.m_listening = false;
  }

  private func Schedule() -> Void {
    let tick = new TandemStoryTick();
    tick.system = this;
    GameInstance.GetDelaySystem(this.GetGameInstance()).DelayCallback(tick, 0.1, false);
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
  }

  public func Tick() -> Void {
    if !this.m_inGame {
      return;
    }
    if !this.m_playing && this.Now() - this.m_attachedAt > 2.0 {
      this.m_playing = true;
    }
    this.HandleEvents();
    // Session items out of a session: a leave that found no V, or a save
    // that held them (Begin).
    if this.m_you == 0u && this.m_playing && ArraySize(this.m_sessionItems) > 0 {
      this.RemoveSessionItems();
    }
    if this.m_you != 0u && this.m_playing {
      this.ScanCompanions();
      this.UpdatePrivate();
      this.SendFlags();
      if this.m_isHost {
        this.HostTick();
      } else {
        this.GuestTick();
      }
    }
    this.Schedule();
  }

  public func InSession() -> Bool {
    return this.m_you != 0u;
  }

  public func IsGuest() -> Bool {
    return this.m_you != 0u && !this.m_isHost;
  }

  public func IsHostInSession() -> Bool {
    return this.m_you != 0u && this.m_isHost;
  }

  // The host's story is mirrored into this guest's own systems (journal,
  // texts): only while its own quests wait, so nothing mirrored wakes them.
  public func Mirroring() -> Bool {
    return this.IsGuest() && this.m_frozen;
  }

  public func You() -> Uint32 {
    return this.m_you;
  }

  public func HostPlayer() -> Uint32 {
    return this.m_host;
  }

  // We present the host's call (ringing or talking): its lines and hubs
  // are everyone's on the call, wherever they stand (design §2.2, §2.4).
  public func InHostCall() -> Bool {
    return IsDefined(this.m_phone) && this.m_phone.Presenting();
  }

  // The host is in a private scene (design §2.12): its lines and hubs are
  // its own.
  public func HostBusy() -> Bool {
    return (this.m_hostFlags & 8) != 0;
  }

  // Scene replay (Scenes.reds): the host's scenes for its guests.
  public func Scenes() -> ref<TandemScenes> {
    return this.m_scenes;
  }

  public func JournalOf() -> wref<JournalManager> {
    return this.m_journal;
  }

  // How far our V is from the host's (a guest; -1: the host is not drawn).
  public func DistanceToHost() -> Float {
    let at = TandemNative.RemotePosition(this.m_host);
    let player = GetPlayer(this.GetGameInstance());
    if at.W < 0.5 || !IsDefined(player) {
      return -1.0;
    }
    return Vector4.Distance(at, player.GetWorldPosition());
  }

  // A remote player's puppet (null if none), through the main system.
  public func PuppetOf(player: Uint32) -> ref<GameObject> {
    let system = GameInstance.GetScriptableSystemsContainer(this.GetGameInstance()).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.PuppetOf(player) : null;
  }

  public func Notice(text: String) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 5.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(this.GetGameInstance())
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }

  public func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : s"player \(player)";
  }

  // ---- the session ----

  private func HandleEvents() -> Void {
    for event in TandemStory.TakeEvents() {
      switch event.kind {
        case 1:
          this.m_you = event.player;
          this.m_host = event.hash;
          // The co-op menu can switch a loaded game from guest to host.
          this.m_isHost = TandemNative.IsHost();
          if this.m_isHost {
            this.HostStart();
          } else {
            this.GuestJoin();
          }
          break;
        case 2:
          if !this.m_isHost && this.m_you != 0u {
            this.GuestLeave();
          }
          if this.m_isHost {
            this.m_scenes.HostEnd();
          }
          this.EndPrivate();
          this.m_you = 0u;
          // Left from the co-op menu with no other session chosen: solo again.
          if !TandemStory.StoryOn() {
            TandemNative.Log("story: off (no session)");
            this.End();
          }
          break;
        case 4:
          // Gone (it may have vanished mid-scene): no longer busy, nothing
          // of ours held for it.
          ArrayRemove(this.m_others, event.player);
          ArrayRemove(this.m_hiddenPuppets, event.player);
          this.SetBusy(event.player, false);
          break;
        case 3:
          if !ArrayContains(this.m_others, event.player) {
            ArrayPush(this.m_others, event.player);
          }
          if this.m_isHost {
            // A joiner gets the journal from the plugin's record; our scene
            // tier too. Nothing to do here but say so.
            TandemNative.Log(s"story: \(this.NameOf(event.player)) joins the story");
          }
          break;
        case 10:
          this.m_inSnapshot = true;
          ArrayClear(this.m_snapshotHashes);
          ArrayClear(this.m_snapshotStates);
          break;
        case 11:
          this.OnHostJournal(event);
          break;
        case 12:
          this.ApplySnapshot();
          break;
        case 14:
          this.OnHostTracked(event.hash);
          break;
        case 20:
        case 21:
        case 22:
          this.m_phone.OnEvent(event);
          break;
        case 30:
        case 31:
        case 32:
        case 33:
          this.m_dialogue.OnEvent(event);
          break;
        case 40:
          if event.player == this.m_host && !this.m_isHost {
            this.m_hostTier = event.state;
            this.OnHostFlags(event.flags);
          }
          if event.player != this.m_you {
            this.SetBusy(event.player, (event.flags & 8) != 0);
          }
          break;
        case 42:
          this.OnHostEffect(event.reward, event.flags != 0);
          break;
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
        case 56:
          if !this.m_isHost {
            this.m_scenes.OnEvent(event);
          }
          break;
        case 41:
          this.OnReward(event.reward, event.hash);
          break;
        default:
          break;
      }
    }
  }

  // ---- host ----

  private func Listen() -> Void {
    if !this.m_listening {
      this.m_journal.RegisterScriptCallback(this, n"OnJournalState", gameJournalListenerType.State);
      this.m_journal.RegisterScriptCallback(this, n"OnJournalTracked", gameJournalListenerType.Tracked);
      this.m_listening = true;
    }
  }

  private func HostStart() -> Void {
    TandemNative.Log("story: hosting the story");
    this.Listen();
    this.m_dialogue.HostStart();
    this.m_snapshotOwed = true;
    this.m_trackedOwed = true;
  }

  private func HostTick() -> Void {
    if this.m_snapshotOwed {
      this.m_snapshotOwed = false;
      let hashes: array<Uint32>;
      let states: array<Int32>;
      let notify: array<Bool>;
      this.Walk(hashes, states);
      for hash in hashes {
        ArrayPush(notify, false);
      }
      TandemStory.SendJournal(true, hashes, states, notify);
      ArrayClear(this.m_changedHashes);
      ArrayClear(this.m_changedStates);
      ArrayClear(this.m_changedNotify);
      let player = GetPlayer(this.GetGameInstance());
      this.m_ourTier = PlayerPuppet.GetSceneTier(player);
      TandemStory.SendState(this.m_ourTier, 0);
    }
    if ArraySize(this.m_changedHashes) > 0 {
      TandemStory.SendJournal(false, this.m_changedHashes, this.m_changedStates, this.m_changedNotify);
      TandemNative.Log(s"journal: \(ArraySize(this.m_changedHashes)) change(s) sent");
      ArrayClear(this.m_changedHashes);
      ArrayClear(this.m_changedStates);
      ArrayClear(this.m_changedNotify);
    }
    if this.m_trackedOwed {
      this.m_trackedOwed = false;
      let tracked = this.m_journal.GetTrackedEntry();
      TandemStory.SendTracked(IsDefined(tracked) ? Cast<Uint32>(this.m_journal.GetEntryHash(tracked)) : 0u);
    }
    // The scene our game plays, for the guests to watch (Scenes.reds).
    this.m_scenes.HostTick(this.Now(), this.m_ourTier, this.m_inPrivate, this.m_dialogue.HubOpen());
  }

  // JournalManager callbacks (signature of JournalNotificationQueue's,
  // final.reds:341545).
  protected cb func OnJournalState(hash: Uint32, className: CName, notifyOption: JournalNotifyOption, changeType: JournalChangeType) -> Bool {
    let entry = this.m_journal.GetEntry(hash);
    if !IsDefined(entry) {
      return false;
    }
    if !this.IsHostInSession() {
      // Our own story (not a guest's mirror): which quest moved last.
      if !this.InSession() {
        let own = this.QuestOf(entry);
        if IsDefined(own) {
          this.m_lastQuest = Cast<Uint32>(this.m_journal.GetEntryHash(own));
          this.m_lastQuestAt = this.Now();
        }
      }
      return false;
    }
    ArrayPush(this.m_changedHashes, hash);
    ArrayPush(this.m_changedStates, EnumInt(this.m_journal.GetEntryState(entry)));
    ArrayPush(this.m_changedNotify, Equals(notifyOption, JournalNotifyOption.Notify));
    // Taking part: the quest this entry belongs to moved, with whoever is
    // near our V now (plugin: 150 m).
    let questEntry = this.QuestOf(entry);
    if IsDefined(questEntry) {
      let at = GetPlayer(this.GetGameInstance()).GetWorldPosition();
      TandemStory.QuestMoved(Cast<Uint32>(this.m_journal.GetEntryHash(questEntry)), at.X, at.Y, at.Z);
    }
  }

  protected cb func OnJournalTracked(hash: Uint32, className: CName, notifyOption: JournalNotifyOption, changeType: JournalChangeType) -> Bool {
    if this.IsHostInSession() {
      this.m_trackedOwed = true;
    }
  }

  // The gameJournalQuest above an entry (itself when it is one).
  private func QuestOf(entry: wref<JournalEntry>) -> wref<JournalEntry> {
    let current = entry;
    let depth = 0;
    while IsDefined(current) && depth < 16 {
      if IsDefined(current as JournalQuest) {
        return current;
      }
      current = this.m_journal.GetParentEntry(current);
      depth += 1;
    }
    return null;
  }

  // Every quest entry (with its phases, objectives, pins) and every contact
  // with its messages and replies that is not Inactive.
  private func Walk(out hashes: array<Uint32>, out states: array<Int32>) -> Void {
    let context: JournalRequestContext;
    context.stateFilter.active = true;
    context.stateFilter.succeeded = true;
    context.stateFilter.failed = true;
    context.stateFilter.inactive = false;
    let quests: array<wref<JournalEntry>>;
    this.m_journal.GetQuests(context, quests);
    for questEntry in quests {
      this.WalkEntry(questEntry, context.stateFilter, hashes, states, 0);
    }
    let contacts: array<wref<JournalEntry>>;
    this.m_journal.GetContacts(context, contacts);
    for contact in contacts {
      this.Add(contact, hashes, states);
      let conversations: array<wref<JournalEntry>>;
      this.m_journal.GetConversations(contact, conversations);
      for conversation in conversations {
        this.Add(conversation, hashes, states);
        let messages: array<wref<JournalEntry>>;
        let choices: array<wref<JournalEntry>>;
        this.m_journal.GetMessagesAndChoices(conversation, messages, choices);
        for message in messages {
          this.Add(message, hashes, states);
        }
        for choice in choices {
          this.Add(choice, hashes, states);
        }
      }
    }
  }

  private func WalkEntry(entry: wref<JournalEntry>, filter: JournalRequestStateFilter, out hashes: array<Uint32>, out states: array<Int32>, depth: Int32) -> Void {
    this.Add(entry, hashes, states);
    if depth >= 8 {
      return;
    }
    let children: array<wref<JournalEntry>>;
    this.m_journal.GetChildren(entry, filter, children);
    for child in children {
      this.WalkEntry(child, filter, hashes, states, depth + 1);
    }
  }

  private func Add(entry: wref<JournalEntry>, out hashes: array<Uint32>, out states: array<Int32>) -> Void {
    if !IsDefined(entry) {
      return;
    }
    let state: Int32 = EnumInt(this.m_journal.GetEntryState(entry));
    // 1 Inactive, 0 Undefined: not part of anyone's story.
    if state <= 1 {
      return;
    }
    let hash = Cast<Uint32>(this.m_journal.GetEntryHash(entry));
    if !ArrayContains(hashes, hash) {
      ArrayPush(hashes, hash);
      ArrayPush(states, state);
    }
  }

  // ---- guest ----

  private func GuestJoin() -> Void {
    let game = this.GetGameInstance();
    TandemNative.Log("story: joining the host's story");
    // Saving, locked twice (manager reason + IsSavingLocked) and no queued
    // autosave (final.reds:195179, :203134).
    if !this.m_saveLocked {
      this.m_saveLocked = true;
      SaveLocksManager.RequestSaveLockAdd(game, n"tandem");
      GameInstance.GetAutoSaveSystem(game).InvalidateAutoSaveRequests();
      TandemNative.Log("save: locked (tandem)");
    }
    // Our own scenes already playing are not quest nodes: they play on.
    let player = GetPlayer(game);
    if PlayerPuppet.GetSceneTier(player) > 1 {
      TandemNative.Log(s"story: our own scene (tier \(PlayerPuppet.GetSceneTier(player))) is playing as we join");
    }
    if !this.m_frozen {
      if TandemStory.FreezeAvailable() && TandemStory.Freeze(true) {
        this.m_frozen = true;
        TandemNative.Log("quest: own quests frozen");
      } else {
        // Said at every join: it explains a session with no journal.
        this.m_warnedNoFreeze = true;
        TandemNative.Log("story: own quests not frozen (tandem.ini quest_freeze = on, or a layout mismatch): the host's journal and texts are not mirrored; calls, lines, hubs and rewards are");
      }
    }
    TandemStory.SendState(PlayerPuppet.GetSceneTier(player), 0);
  }

  private func GuestTick() -> Void {
    // An autosave may have been queued since (a quest checkpoint of an
    // open-world phase): dropped every few seconds while locked.
    if this.m_saveLocked && this.Now() - this.m_lastAutosaveDrop > 5.0 {
      this.m_lastAutosaveDrop = this.Now();
      GameInstance.GetAutoSaveSystem(this.GetGameInstance()).InvalidateAutoSaveRequests();
    }
    this.UpdateHolds();
    this.m_dialogue.GuestTick();
    this.m_scenes.GuestTick(this.Now());
    this.m_phone.GuestTick();
  }

  // The journal entry's state before the mirror first changed it.
  private func Touch(hash: Uint32, entry: wref<JournalEntry>) -> Void {
    if !ArrayContains(this.m_touched, hash) {
      ArrayPush(this.m_touched, hash);
      ArrayPush(this.m_touchedBefore, EnumInt(this.m_journal.GetEntryState(entry)));
    }
  }

  private func SetEntry(hash: Uint32, state: Int32, notify: Bool) -> Bool {
    let entry = this.m_journal.GetEntry(hash);
    if !IsDefined(entry) {
      this.m_unknownEntries += 1;
      return false;
    }
    this.Touch(hash, entry);
    this.m_journal.ChangeEntryStateByHash(hash, IntEnum<gameJournalEntryState>(state), notify ? JournalNotifyOption.Notify : JournalNotifyOption.DoNotNotify);
    return true;
  }

  private func OnHostJournal(event: TandemStoryEvent) -> Void {
    if !this.Mirroring() {
      return;
    }
    if this.m_inSnapshot {
      ArrayPush(this.m_snapshotHashes, event.hash);
      ArrayPush(this.m_snapshotStates, event.state);
      return;
    }
    this.HostHeard(event.hash, event.state);
    // Apart, a gig is its starter's: the host's gig entries are not ours
    // to show (we see them when we help: grouped).
    if this.OwnGigEntry(event.hash) {
      return;
    }
    // A change of the host's story, with its toast and sound when the
    // host's had one (JournalNotificationQueue.OnJournalUpdate, :341545).
    this.SetEntry(event.hash, event.state, event.flags != 0);
  }

  private func ApplySnapshot() -> Void {
    this.m_inSnapshot = false;
    if !this.Mirroring() {
      return;
    }
    this.m_unknownEntries = 0;
    // Our own story out of sight: entries the host's snapshot lacks go
    // Inactive (put back on leave).
    let own: array<Uint32>;
    let ownStates: array<Int32>;
    this.Walk(own, ownStates);
    let hidden = 0;
    for hash in own {
      if !ArrayContains(this.m_snapshotHashes, hash) && !this.OwnGigEntry(hash) && this.SetEntry(hash, 1, false) {
        hidden += 1;
      }
    }
    // Twice: an entry applied before its parent is applied again after it.
    let pass = 0;
    while pass < 2 {
      let i = 0;
      while i < ArraySize(this.m_snapshotHashes) {
        // Apart, the host's gig entries are not ours to show (OnHostJournal).
        if !this.OwnGigEntry(this.m_snapshotHashes[i]) {
          this.SetEntry(this.m_snapshotHashes[i], this.m_snapshotStates[i], false);
        }
        i += 1;
      }
      pass += 1;
    }
    TandemNative.Log(s"journal: snapshot \(ArraySize(this.m_snapshotHashes)) entries (\(this.m_unknownEntries / 2) unknown here), \(hidden) own entries hidden");
  }

  private func OnHostTracked(hash: Uint32) -> Void {
    if !this.Mirroring() {
      return;
    }
    if !this.m_ownTrackedKept {
      let own = this.m_journal.GetTrackedEntry();
      this.m_ownTracked = IsDefined(own) ? Cast<Uint32>(this.m_journal.GetEntryHash(own)) : 0u;
      this.m_ownTrackedKept = true;
    }
    // Applied on join and when the host changes it; we may track something
    // else after (TrackEntry, final.reds:196701).
    let entry = this.m_journal.GetEntry(hash);
    if IsDefined(entry) {
      this.m_journal.TrackEntry(entry);
      TandemNative.Log(s"journal: tracking the host's \(hash)");
    } else if hash == 0u {
      this.m_journal.UntrackEntry();
    }
  }

  // Leaving (we quit, or the host is gone): close what the mirrors opened,
  // put the journal back, resume our own quests, unlock saving.
  private func GuestLeave() -> Void {
    TandemNative.Log("story: leaving the host's story");
    // The host's quest props first: our own quests test our inventory.
    this.RemoveSessionItems();
    this.m_phone.Close();
    this.m_dialogue.Close();
    this.m_scenes.Leave();
    this.ReleaseHolds();
    this.TakeOffEffects();
    // Back to the default before the journal is put back (our own gig
    // entries given back meanwhile are no longer in the touched list).
    this.m_apart = false;
    TandemStory.Grouped(true);
    this.m_hostFlags = 0;
    this.m_hostTier = 0;
    let mismatches = 0;
    if ArraySize(this.m_touched) > 0 {
      let i = 0;
      while i < ArraySize(this.m_touched) {
        // Undefined (never set) goes back as Inactive: the same to the game.
        let before = this.m_touchedBefore[i] <= 1 ? 1 : this.m_touchedBefore[i];
        this.m_journal.ChangeEntryStateByHash(this.m_touched[i], IntEnum<gameJournalEntryState>(before), JournalNotifyOption.DoNotNotify);
        i += 1;
      }
      // The check: every entry as it was.
      i = 0;
      while i < ArraySize(this.m_touched) {
        let entry = this.m_journal.GetEntry(this.m_touched[i]);
        let now: Int32 = IsDefined(entry) ? EnumInt(this.m_journal.GetEntryState(entry)) : -1;
        let same = now <= 1 ? this.m_touchedBefore[i] <= 1 : now == this.m_touchedBefore[i];
        if !same {
          mismatches += 1;
        }
        i += 1;
      }
    }
    if this.m_ownTrackedKept {
      let own = this.m_journal.GetEntry(this.m_ownTracked);
      if IsDefined(own) {
        this.m_journal.TrackEntry(own);
      } else {
        this.m_journal.UntrackEntry();
      }
      this.m_ownTrackedKept = false;
    }
    let restored = ArraySize(this.m_touched);
    ArrayClear(this.m_touched);
    ArrayClear(this.m_touchedBefore);
    if mismatches == 0 {
      TandemNative.Log(s"journal: restored, digest matches baseline (\(restored) entries)");
    } else {
      TandemNative.Log(s"journal: restore mismatch: \(mismatches) of \(restored) entries differ");
    }
    let lost = false;
    if this.m_frozen {
      lost = TandemStory.FreezeLost();
      TandemStory.Freeze(false);
      this.m_frozen = false;
      TandemNative.Log(s"quest: own quests resumed (\(TandemStory.FreezeReport()))");
    }
    if mismatches > 0 || lost {
      // Never a save that differs from our own story: saving stays locked;
      // the way out is our last save.
      TandemNative.Log("save: stays locked (our story could not be put back exactly): load your last save");
      this.Notice("Co-op: your story could not be restored exactly. Load your last save.");
      return;
    }
    if this.m_saveLocked {
      this.m_saveLocked = false;
      SaveLocksManager.RequestSaveLockRemove(this.GetGameInstance(), n"tandem");
      TandemNative.Log("save: unlocked");
    }
    // TODO (design §1.4 step 4): write one save ("back from co-op") and load
    // it, so session artifacts (hidden own NPCs, proxies' leftovers) are gone
    // and the world is rebuilt from our own save. Until it is tested in game
    // the guest goes on in place; its next save keeps its V's gains and the
    // co-op reward ledger.
    TandemNative.Log("save: back from co-op (TODO: save and reload; continuing in place)");
  }

  // ---- the host's scenes, near or far (design §2.3, §5.1) ----

  private func UpdateHolds() -> Void {
    let distance = this.DistanceToHost();
    let near = distance >= 0.0 && distance <= 30.0;
    let tier = this.HostSceneTier();
    let wantCombat = near && tier >= 3;
    let wantGod = near && tier >= 4;
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    if NotEquals(wantCombat, this.m_holdCombat) {
      this.m_holdCombat = wantCombat;
      if wantCombat {
        StatusEffectHelper.ApplyStatusEffect(player, t"GameplayRestriction.NoCombat");
      } else {
        StatusEffectHelper.RemoveStatusEffect(player, t"GameplayRestriction.NoCombat");
      }
      TandemNative.Log(s"story: weapons \(wantCombat ? "lowered" : "free") (host tier \(this.m_hostTier), \(distance) m)");
    }
    // A status effect: script god mode does not protect V (engine-knowledge
    // §13.11).
    if NotEquals(wantGod, this.m_holdGod) {
      this.m_holdGod = wantGod;
      if wantGod {
        StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.Invulnerable");
      } else {
        StatusEffectHelper.RemoveStatusEffect(player, t"BaseStatusEffect.Invulnerable");
      }
    }
    // Far from a cinematic: offered to join (once per scene).
    if tier >= 4 && (distance < 0.0 || distance > 100.0) {
      if !this.m_offerShown {
        this.m_offerShown = true;
        this.m_offerOpen = true;
        this.Notice(s"\(this.NameOf(this.m_host)) started a scene. Join them: press Interact.");
        GetPlayer(this.GetGameInstance()).RegisterInputListener(this, n"Choice1");
        TandemNative.Log("story: join offer shown");
      }
    } else if tier < 4 {
      this.CloseOffer();
      this.m_offerShown = false;
    }
  }

  private func CloseOffer() -> Void {
    if this.m_offerOpen {
      this.m_offerOpen = false;
      GetPlayer(this.GetGameInstance()).UnregisterInputListener(this, n"Choice1");
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if this.m_offerOpen && Equals(ListenerAction.GetName(action), n"Choice1")
      && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      this.AcceptOffer();
    }
  }

  public func AcceptOffer() -> Void {
    this.CloseOffer();
    // TODO (milestone 4's regroup): bring our V next to the host's behind a
    // loading screen.
    TandemNative.Log("story: join offer accepted (regroup: TODO)");
  }

  private func ReleaseHolds() -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if IsDefined(player) && this.m_holdCombat {
      StatusEffectHelper.RemoveStatusEffect(player, t"GameplayRestriction.NoCombat");
    }
    if IsDefined(player) && this.m_holdGod {
      StatusEffectHelper.RemoveStatusEffect(player, t"BaseStatusEffect.Invulnerable");
    }
    this.m_holdCombat = false;
    this.m_holdGod = false;
    this.CloseOffer();
    this.m_offerShown = false;
  }

  // Our scene tier changed (PlayerPuppet.OnSceneTierChange, final.reds:43746).
  public func OnOurTier(tier: Int32) -> Void {
    this.m_ourTier = tier;
    if this.InSession() {
      TandemStory.SendState(tier, this.m_ourFlags);
      if this.m_isHost {
        TandemNative.Log(s"story: our scene tier \(tier)");
      }
    }
  }

  // ---- Johnny, Songbird, braindances (design §2.6, §2.10) ----

  // The host's scene tier as a guest lives it: playing as Johnny (a
  // takeover, a flashback, "Johnny drives") or in a braindance counts as a
  // cinematic: weapons down and safe near the host, an offer to join far away.
  private func HostSceneTier() -> Int32 {
    // A private scene is nobody else's: no holds, no offer. Nor is a
    // host away from its body in another world, or our V in one
    // (Elsewhere.reds): there is no scene here to hold us for or to join.
    let game = this.GetGameInstance();
    if (this.m_hostFlags & 8) != 0 || TandemElsewhere.IsAwayFromBody(game, this.m_host)
      || TandemElsewhere.Inside(game) {
      return 1;
    }
    if (this.m_hostFlags & 3) != 0 && this.m_hostTier < 4 {
      return 4;
    }
    return this.m_hostTier;
  }

  // Host: our flags (UI_PlayerStats.isReplacer, final.reds:83411;
  // Braindance.IsActive, :48378), sent when they change.
  private func SendFlags() -> Void {
    if this.Now() - this.m_lastFlagsCheck < 0.5 {
      return;
    }
    this.m_lastFlagsCheck = this.Now();
    let boards = GameInstance.GetBlackboardSystem(this.GetGameInstance());
    let flags = 0;
    if boards.Get(GetAllBlackboardDefs().Braindance).GetBool(GetAllBlackboardDefs().Braindance.IsActive) {
      flags += 1;
    }
    if boards.Get(GetAllBlackboardDefs().UI_PlayerStats).GetBool(GetAllBlackboardDefs().UI_PlayerStats.isReplacer) {
      flags += 2;
    }
    if this.m_inPrivate {
      flags += 8;
    }
    if flags != this.m_ourFlags {
      this.m_ourFlags = flags;
      TandemStory.SendState(this.m_ourTier, flags);
      TandemNative.Log(s"story: our story flags \(flags) (1 braindance, 2 Johnny)");
    }
  }

  private func OnHostFlags(flags: Int32) -> Void {
    let before = this.m_hostFlags;
    this.m_hostFlags = flags;
    if (flags & 2) != 0 && (before & 2) == 0 {
      // TODO: dress the host's puppet as Johnny (Character.Silverhand's
      // look) for the takeover; until then its V shows, moving as Johnny.
      // The notice is Elsewhere.reds' (a takeover or a flashback).
      TandemNative.Log("story: the host plays as Johnny");
    } else if (flags & 2) == 0 && (before & 2) != 0 {
      TandemNative.Log("story: the host is V again");
    }
    if (flags & 1) != 0 && (before & 1) == 0 {
      // The notice is Elsewhere.reds'.
      TandemNative.Log("story: the host is in a braindance");
    }
  }

  // Johnny and Songbird are NPCs of the host's world (Character.Silverhand,
  // final.reds:40624): the host shares them like any NPC near its V, so a
  // guest near the host sees them where the host does, as proxies (walk and
  // idle; their scene gestures and voice come with scene replay, design
  // §2.5). Logged when the set around us changes.
  private func ScanCompanions() -> Void {
    if this.Now() - this.m_lastCompanionScan < 2.0 {
      return;
    }
    this.m_lastCompanionScan = this.Now();
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    let found = "";
    for npc in player.GetNPCsAroundObject(60.0) {
      let name = TDBID.ToStringDEBUG(npc.GetRecordID());
      if StrContains(name, "Silverhand") || StrContains(name, "ongbird") {
        found += s" \(name)";
      }
    }
    if NotEquals(found, this.m_companions) {
      this.m_companions = found;
      if StrLen(found) > 0 {
        TandemNative.Log(s"story: beside \(this.m_isHost ? "our V, shared with the guests" : "the host, as proxies"):\(found)");
      } else {
        TandemNative.Log("story: no Johnny or Songbird around");
      }
    }
  }

  // Status effects of the host's V that are the story's look and feel, not
  // its rules: Johnny's relic glitches (the JohnnySickness tag,
  // final.reds:44352), Songbird's and other glitch effects (by record name).
  public static func IsStoryEffect(record: wref<StatusEffect_Record>) -> Bool {
    if !IsDefined(record) {
      return false;
    }
    if ArrayContains(record.GameplayTags(), n"JohnnySickness") {
      return true;
    }
    let name = TDBID.ToStringDEBUG(record.GetID());
    return StrContains(name, "Johnny") || StrContains(name, "SoMi") || StrContains(name, "Songbird")
      || StrContains(name, "Glitch");
  }

  // Host: a story effect came onto our V or went (PlayerPuppet wraps below).
  public func OnOurEffect(effect: TweakDBID, applied: Bool) -> Void {
    if this.IsHostInSession() {
      TandemStory.SendEffect(effect, applied);
      TandemNative.Log(s"story: effect \(TDBID.ToStringDEBUG(effect)) \(applied ? "on" : "off") our V, sent");
    }
  }

  // Guest: the host's V glitches; near it (30 m), ours does too, as long as
  // the host's does.
  private func OnHostEffect(effect: TweakDBID, applied: Bool) -> Void {
    if !this.IsGuest() {
      return;
    }
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    if applied {
      let distance = this.DistanceToHost();
      if distance < 0.0 || distance > 30.0 || ArrayContains(this.m_effects, effect) {
        return;
      }
      ArrayPush(this.m_effects, effect);
      StatusEffectHelper.ApplyStatusEffect(player, effect);
      TandemNative.Log(s"story: effect \(TDBID.ToStringDEBUG(effect)) with the host");
    } else if ArrayContains(this.m_effects, effect) {
      ArrayRemove(this.m_effects, effect);
      StatusEffectHelper.RemoveStatusEffect(player, effect);
      TandemNative.Log(s"story: effect \(TDBID.ToStringDEBUG(effect)) over");
    }
  }

  private func TakeOffEffects() -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if IsDefined(player) {
      for effect in this.m_effects {
        StatusEffectHelper.RemoveStatusEffect(player, effect);
      }
    }
    ArrayClear(this.m_effects);
  }

  // ---- private scenes (design §2.12) ----

  // Our V's scene: private when its resource says so (engine/scenes.cpp:
  // chats, open-world and minor one-to-one scenes, a romance partner alone)
  // and it holds our V (tier 2 and up). It stays private until the scene
  // lets go of our V.
  private func UpdatePrivate() -> Void {
    if this.Now() - this.m_lastPrivateCheck < 0.5 {
      return;
    }
    this.m_lastPrivateCheck = this.Now();
    let player = GetPlayer(this.GetGameInstance());
    if !IsDefined(player) {
      return;
    }
    let tier = PlayerPuppet.GetSceneTier(player);
    let kind = tier >= 2 ? TandemStory.SceneClass() : 0;
    let want = this.m_forcedPrivate || (tier >= 2 && (kind >= 2 || (this.m_inPrivate && kind == 0)));
    if NotEquals(want, this.m_inPrivate) {
      this.m_inPrivate = want;
      TandemStory.SetPrivate(want);
      if want {
        TandemNative.Log(s"story: private scene (\(kind == 3 ? "romance" : "minor"), tier \(tier)): the others see us busy");
      } else {
        this.ShowPuppets();
        TandemNative.Log("story: private scene over");
      }
    }
    // No other V intrudes: their puppets near us fade out while it lasts.
    if this.m_inPrivate {
      this.HidePuppets(player);
    }
  }

  private func HidePuppets(player: ref<PlayerPuppet>) -> Void {
    let hidden = 0;
    for other in this.m_others {
      let puppet = this.PuppetOf(other);
      if IsDefined(puppet) && !ArrayContains(this.m_hiddenPuppets, other)
        && Vector4.Distance(puppet.GetWorldPosition(), player.GetWorldPosition()) < 30.0 {
        // One set of reasons per puppet (PuppetVisibility.reds): another
        // world's hiding and ours never undo each other.
        TandemVisibility.Hide(this.GetGameInstance(), other, n"private", 0.5);
        ArrayPush(this.m_hiddenPuppets, other);
        hidden += 1;
      }
    }
    if hidden > 0 {
      TandemNative.Log(s"story: private scene: \(hidden) puppet(s) hidden");
    }
  }

  private func ShowPuppets() -> Void {
    for other in this.m_hiddenPuppets {
      TandemVisibility.Show(this.GetGameInstance(), other, n"private", 0.5);
    }
    ArrayClear(this.m_hiddenPuppets);
  }

  // Another player is busy in its own private scene: its puppet stands where
  // the scene began (its game holds it there) and idles on its own; a small
  // notice, nothing of the scene.
  private func SetBusy(player: Uint32, busy: Bool) -> Void {
    if Equals(busy, ArrayContains(this.m_busy, player)) {
      return;
    }
    TandemStory.PausePose(player, busy);
    if busy {
      ArrayPush(this.m_busy, player);
      this.Notice(s"\(this.NameOf(player)) is busy.");
      TandemNative.Log(s"story: \(this.NameOf(player)) is busy (private scene)");
    } else {
      ArrayRemove(this.m_busy, player);
      TandemNative.Log(s"story: \(this.NameOf(player)) is back");
    }
  }

  private func EndPrivate() -> Void {
    if this.m_inPrivate {
      this.m_inPrivate = false;
      TandemStory.SetPrivate(false);
    }
    this.ShowPuppets();
    for player in this.m_busy {
      TandemStory.PausePose(player, false);
    }
    ArrayClear(this.m_busy);
    ArrayClear(this.m_others);
  }

  // ---- session items, and whose gig it is (quest-carryover.md) ----

  // Loot.reds: a quest prop came into our V's inventory.
  public func NoteSessionItem(record: TweakDBID, quantity: Int32) -> Void {
    // Ours to keep when we host, play alone, or run our own gig apart.
    if !this.IsGuest() || this.m_apart || quantity <= 0 {
      return;
    }
    let i = 0;
    while i < ArraySize(this.m_sessionItems) {
      if this.m_sessionItems[i] == record {
        this.m_sessionCounts[i] += quantity;
        return;
      }
      i += 1;
    }
    ArrayPush(this.m_sessionItems, record);
    ArrayPush(this.m_sessionCounts, quantity);
    TandemNative.Log(s"story: session item \(TDBID.ToStringDEBUG(record)) x\(quantity) (the host's quest prop: ours until we leave)");
  }

  private func RemoveSessionItems() -> Void {
    let player = GetPlayer(this.GetGameInstance());
    if ArraySize(this.m_sessionItems) == 0 || !IsDefined(player) {
      return;
    }
    let items = GameInstance.GetTransactionSystem(this.GetGameInstance());
    // One try each: one that is gone already (used, sold, dropped) is no
    // longer ours to take.
    let gone = 0;
    let i = 0;
    while i < ArraySize(this.m_sessionItems) {
      if !items.RemoveItemByTDBID(player, this.m_sessionItems[i], this.m_sessionCounts[i]) {
        gone += 1;
      }
      i += 1;
    }
    TandemNative.Log(s"story: \(ArraySize(this.m_sessionItems)) session item record(s) taken back out (\(gone) gone already)");
    ArrayClear(this.m_sessionItems);
    ArrayClear(this.m_sessionCounts);
  }

  // How many of the host's quest props our V still holds (for progression's
  // TandemStorySystem.LeftOver, bit 1: no save while any is left).
  public func SessionItemsLeft() -> Int32 {
    return ArraySize(this.m_sessionItems);
  }

  // Whether our own world's gigs are played where we stand: apart from the
  // host, or (regions) leading our group. The regions work calls this with
  // TandemRegions.Leader() == You() (quest-carryover.md, "Whose gig is it");
  // until then only the test command does, so we are always grouped and
  // nothing changes. Guest only; leaving sets it back.
  public func SetApart(apart: Bool) -> Void {
    if Equals(apart, this.m_apart) || (apart && !this.IsGuest()) {
      return;
    }
    this.m_apart = apart;
    TandemStory.Grouped(!apart);
    if !this.Mirroring() {
      TandemNative.Log(s"story: \(apart ? "apart from the host: our gigs are ours" : "grouped with the host")");
      return;
    }
    // The gig entries change hands: apart, ours are given back as they were
    // before the mirror touched them (the host's go back to hidden, which is
    // what they were here); grouped, the host's are shown as last heard and
    // ours not in the host's journal are hidden again.
    let changed = 0;
    if apart {
      let i = ArraySize(this.m_touched) - 1;
      while i >= 0 {
        if this.GigEntry(this.m_touched[i]) {
          let before = this.m_touchedBefore[i] <= 1 ? 1 : this.m_touchedBefore[i];
          this.m_journal.ChangeEntryStateByHash(this.m_touched[i], IntEnum<gameJournalEntryState>(before), JournalNotifyOption.DoNotNotify);
          ArrayErase(this.m_touched, i);
          ArrayErase(this.m_touchedBefore, i);
          changed += 1;
        }
        i -= 1;
      }
    } else {
      let own: array<Uint32>;
      let ownStates: array<Int32>;
      this.Walk(own, ownStates);
      for hash in own {
        if this.GigEntry(hash) && !ArrayContains(this.m_snapshotHashes, hash) && this.SetEntry(hash, 1, false) {
          changed += 1;
        }
      }
      let i = 0;
      while i < ArraySize(this.m_snapshotHashes) {
        if this.GigEntry(this.m_snapshotHashes[i]) && this.SetEntry(this.m_snapshotHashes[i], this.m_snapshotStates[i], false) {
          changed += 1;
        }
        i += 1;
      }
    }
    TandemNative.Log(s"story: \(apart ? "apart from the host: our gigs are ours" : "grouped with the host"), \(changed) gig journal entries changed hands");
  }

  public func Apart() -> Bool {
    return this.m_apart;
  }

  // The host's journal as last heard (a change after the snapshot).
  private func HostHeard(hash: Uint32, state: Int32) -> Void {
    let i = 0;
    while i < ArraySize(this.m_snapshotHashes) {
      if this.m_snapshotHashes[i] == hash {
        this.m_snapshotStates[i] = state;
        return;
      }
      i += 1;
    }
    ArrayPush(this.m_snapshotHashes, hash);
    ArrayPush(this.m_snapshotStates, state);
  }

  // A journal entry of a gig while we are apart: it belongs to its
  // starter's world, so the mirror leaves ours alone and does not show the
  // host's.
  private func OwnGigEntry(hash: Uint32) -> Bool {
    return this.m_apart && this.GigEntry(hash);
  }

  // A journal entry of a gig (street story, contract, cyberpsycho, courier).
  private func GigEntry(hash: Uint32) -> Bool {
    let entry = this.m_journal.GetEntry(hash);
    let quest = IsDefined(entry) ? this.QuestOf(entry) : null;
    if !IsDefined(quest) {
      return false;
    }
    let kind = this.m_journal.GetQuestType(quest);
    return Equals(kind, gameJournalQuestType.StreetStory) || Equals(kind, gameJournalQuestType.Contract)
      || Equals(kind, gameJournalQuestType.CyberPsycho) || Equals(kind, gameJournalQuestType.CourierQuest)
      || Equals(kind, gameJournalQuestType.CourierSideQuest);
  }

  // ---- rewards (design §4) ----

  // Host: our V got a reward (PlayerPuppet.OnRewardEvent, final.reds:43468).
  public func OnOurReward(reward: TweakDBID) -> Void {
    if !this.IsHostInSession() {
      return;
    }
    let at = GetPlayer(this.GetGameInstance()).GetWorldPosition();
    TandemStory.Reward(reward, at.X, at.Y, at.Z);
  }

  // Guest: a reward of the host's quest for our own V, once.
  private func OnReward(reward: TweakDBID, questHash: Uint32) -> Void {
    if this.m_isHost {
      return;
    }
    if this.Rewarded(reward, questHash) {
      TandemNative.Log(s"reward: \(TDBID.ToStringDEBUG(reward)) already given in co-op");
      return;
    }
    ArrayPush(this.m_rewarded, reward);
    ArrayPush(this.m_rewardedQuests, questHash);
    // By our own rules: our level, our perks, our toasts (final.reds:37406).
    RPGManager.GiveReward(this.GetGameInstance(), reward);
    TandemNative.Log(s"reward: given \(TDBID.ToStringDEBUG(reward)) (quest \(questHash))");
  }

  // Our own story pays a reward: not again if co-op already paid it.
  // Only for a quest we know (the host's quest hash is 0 when no quest
  // moved in the 10 s before its reward: then nothing is blocked later).
  public func AlreadyRewarded(reward: TweakDBID) -> Bool {
    // Solo is vanilla: the co-op ledger only holds back rewards while a
    // session is chosen.
    if !TandemStory.StoryOn() || this.InSession() || this.m_lastQuest == 0u || this.Now() - this.m_lastQuestAt > 10.0 {
      return false;
    }
    return this.Rewarded(reward, this.m_lastQuest);
  }

  private func Rewarded(reward: TweakDBID, questHash: Uint32) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_rewarded) {
      if this.m_rewarded[i] == reward && i < ArraySize(this.m_rewardedQuests) && this.m_rewardedQuests[i] == questHash {
        return true;
      }
      i += 1;
    }
    return false;
  }

  // ---- commands (tools/game.sh cmd story … / quest …) ----

  public func Command(command: String) -> Void {
    if !this.m_inGame {
      TandemNative.Log("story: off (tandem.ini story = off, or not in a game)");
      return;
    }
    let game = this.GetGameInstance();
    if Equals(command, "quest freeze on") {
      TandemNative.Log(s"quest: freeze \(TandemStory.Freeze(true) ? "on" : "unavailable (tandem.ini quest_freeze = on?)")");
    } else if Equals(command, "quest freeze off") {
      TandemStory.Freeze(false);
      TandemNative.Log(s"quest: freeze off (\(TandemStory.FreezeReport()))");
    } else if Equals(command, "quest trace on") || Equals(command, "quest trace off") {
      // Spike C2: phases and their journal nodes, in the log.
      TandemStory.QuestTrace(Equals(command, "quest trace on"));
    } else if Equals(command, "quest apart") || Equals(command, "quest grouped") {
      // (test, until regions) as if we were apart from the host, or back.
      this.SetApart(Equals(command, "quest apart"));
    } else if Equals(command, "story items") {
      TandemNative.Log(s"story: \(ArraySize(this.m_sessionItems)) session item record(s) held");
    } else if StrBeginsWith(command, "story item ") {
      // (test) as if the host's quest had given our V this prop.
      let record = TDBID.Create(StrAfterFirst(command, "item "));
      let player = GetPlayer(this.GetGameInstance());
      if IsDefined(player) && GameInstance.GetTransactionSystem(this.GetGameInstance()).GiveItem(player, ItemID.FromTDBID(record), 1) {
        this.NoteSessionItem(record, 1);
      }
    } else if Equals(command, "quest report") {
      TandemNative.Log(TandemStory.FreezeReport());
    } else if StrBeginsWith(command, "story journal ") {
      // story journal HASH STATE: a change on our own journal, as a quest would.
      let args = StrAfterFirst(command, "journal ");
      let hashText = StrBeforeFirst(args, " ");
      let stateText = StrAfterFirst(args, " ");
      let hash = Cast<Uint32>(StringToUint64(hashText));
      let state = StringToInt(stateText);
      this.m_journal.ChangeEntryStateByHash(hash, IntEnum<gameJournalEntryState>(state), JournalNotifyOption.Notify);
      TandemNative.Log(s"story: journal \(hash) set to \(state)");
    } else if StrBeginsWith(command, "story reward ") {
      let evt = new RewardEvent();
      evt.rewardName = TDBID.Create(StrAfterFirst(command, "reward "));
      GetPlayer(game).QueueEvent(evt);
      TandemNative.Log(s"story: reward \(StrAfterFirst(command, "reward ")) queued on our V");
    } else if Equals(command, "story dump") {
      let hashes: array<Uint32>;
      let states: array<Int32>;
      this.Walk(hashes, states);
      let i = 0;
      while i < ArraySize(hashes) {
        let entry = this.m_journal.GetEntry(hashes[i]);
        TandemNative.Log(s"story dump: \(hashes[i]) \(states[i]) \(NameToString(entry.GetClassName())) \(entry.GetId())");
        i += 1;
      }
      TandemNative.Log(s"story dump: \(ArraySize(hashes)) entries");
    } else if Equals(command, "story private on") || Equals(command, "story private off") {
      // (test) as if our V were in a private scene.
      this.m_forcedPrivate = Equals(command, "story private on");
      this.m_lastPrivateCheck = 0.0;
      TandemNative.Log(s"story: test private scene \(this.m_forcedPrivate ? "on" : "off")");
    } else if StrBeginsWith(command, "story scene") {
      this.m_scenes.Command(command);
    } else if Equals(command, "story join") {
      this.AcceptOffer();
    } else if Equals(command, "story state") {
      TandemNative.Log(s"story: you \(this.m_you), host \(this.m_host), frozen \(this.m_frozen), save locked \(this.m_saveLocked), touched \(ArraySize(this.m_touched)), host tier \(this.m_hostTier), distance \(this.DistanceToHost()), rewarded \(ArraySize(this.m_rewarded))");
    } else if StrBeginsWith(command, "story call") || StrBeginsWith(command, "story line") || StrBeginsWith(command, "story hub") {
      if StrBeginsWith(command, "story call") {
        this.m_phone.Command(command);
      } else {
        this.m_dialogue.Command(command);
      }
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }
}

// Our V's scene tier, for the others (final.reds:43746).
@wrapMethod(PlayerPuppet)
protected cb func OnSceneTierChange(newState: Int32) -> Bool {
  let result = wrappedMethod(newState);
  let story = TandemStorySystem.Get(this.GetGame());
  if IsDefined(story) && IsDefined(GetPlayer(this.GetGame())) && this.GetEntityID() == GetPlayer(this.GetGame()).GetEntityID() {
    story.OnOurTier(newState);
  }
  return result;
}

// Quest rewards (final.reds:43468): the host's go to the guests that took
// part; a reward already paid in co-op is not paid again in our own story.
@wrapMethod(PlayerPuppet)
protected cb func OnRewardEvent(evt: ref<RewardEvent>) -> Bool {
  let story = TandemStorySystem.Get(this.GetGame());
  if IsDefined(story) && story.AlreadyRewarded(evt.rewardName) {
    TandemNative.Log(s"reward: \(TDBID.ToStringDEBUG(evt.rewardName)) was earned in co-op: not paid again");
    return true;
  }
  let result = wrappedMethod(evt);
  if IsDefined(story) {
    story.OnOurReward(evt.rewardName);
  }
  return result;
}

// Story effects of our V (final.reds:43899, :44474): the host's glitches
// reach the guests near it.
@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  let result = wrappedMethod(evt);
  if evt.isNewApplication && TandemStorySystem.IsStoryEffect(evt.staticData) {
    let story = TandemStorySystem.Get(this.GetGame());
    let player = GetPlayer(this.GetGame());
    if IsDefined(story) && IsDefined(player) && this.GetEntityID() == player.GetEntityID() {
      story.OnOurEffect(evt.staticData.GetID(), true);
    }
  }
  return result;
}

@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectRemoved(evt: ref<RemoveStatusEffect>) -> Bool {
  let result = wrappedMethod(evt);
  if evt.isFinalRemoval && TandemStorySystem.IsStoryEffect(evt.staticData) {
    let story = TandemStorySystem.Get(this.GetGame());
    let player = GetPlayer(this.GetGame());
    if IsDefined(story) && IsDefined(player) && this.GetEntityID() == player.GetEntityID() {
      story.OnOurEffect(evt.staticData.GetID(), false);
    }
  }
  return result;
}
