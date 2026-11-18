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

  // Quest rewards this V got in co-op (its own save keeps them: a quest it
  // was rewarded for with a host pays nothing again in its own story).
  // Pairs (reward record, its quest's journal hash): generic records are
  // shared by many quests, so the quest is part of the key.
  private persistent let m_rewarded: array<TweakDBID>;
  private persistent let m_rewardedQuests: array<Uint32>;
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
    // Journal changes: the host sends them; every game keeps the last quest
    // that moved (rewards, below).
    this.Listen();
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
    if this.m_you != 0u && this.m_playing {
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
          this.m_you = 0u;
          break;
        case 3:
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
    let quest = this.QuestOf(entry);
    if IsDefined(quest) {
      let at = GetPlayer(this.GetGameInstance()).GetWorldPosition();
      TandemStory.QuestMoved(Cast<Uint32>(this.m_journal.GetEntryHash(quest)), at.X, at.Y, at.Z);
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
    for quest in quests {
      this.WalkEntry(quest, context.stateFilter, hashes, states, 0);
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
      } else if !this.m_warnedNoFreeze {
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
      if !ArrayContains(this.m_snapshotHashes, hash) && this.SetEntry(hash, 1, false) {
        hidden += 1;
      }
    }
    // Twice: an entry applied before its parent is applied again after it.
    let pass = 0;
    while pass < 2 {
      let i = 0;
      while i < ArraySize(this.m_snapshotHashes) {
        this.SetEntry(this.m_snapshotHashes[i], this.m_snapshotStates[i], false);
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
    this.m_phone.Close();
    this.m_dialogue.Close();
    this.ReleaseHolds();
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
    let wantCombat = near && this.m_hostTier >= 3;
    let wantGod = near && this.m_hostTier >= 4;
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
    if this.m_hostTier >= 4 && (distance < 0.0 || distance > 100.0) {
      if !this.m_offerShown {
        this.m_offerShown = true;
        this.m_offerOpen = true;
        this.Notice(s"\(this.NameOf(this.m_host)) started a scene. Join them: press Interact.");
        GetPlayer(this.GetGameInstance()).RegisterInputListener(this, n"Choice1");
        TandemNative.Log("story: join offer shown");
      }
    } else if this.m_hostTier < 4 {
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
      TandemStory.SendState(tier, 0);
      if this.m_isHost {
        TandemNative.Log(s"story: our scene tier \(tier)");
      }
    }
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
  private func OnReward(reward: TweakDBID, quest: Uint32) -> Void {
    if this.m_isHost {
      return;
    }
    if this.Rewarded(reward, quest) {
      TandemNative.Log(s"reward: \(TDBID.ToStringDEBUG(reward)) already given in co-op");
      return;
    }
    ArrayPush(this.m_rewarded, reward);
    ArrayPush(this.m_rewardedQuests, quest);
    // By our own rules: our level, our perks, our toasts (final.reds:37406).
    RPGManager.GiveReward(this.GetGameInstance(), reward);
    TandemNative.Log(s"reward: given \(TDBID.ToStringDEBUG(reward)) (quest \(quest))");
  }

  // Our own story pays a reward: not again if co-op already paid it.
  // Only for a quest we know (the host's quest hash is 0 when no quest
  // moved in the 10 s before its reward: then nothing is blocked later).
  public func AlreadyRewarded(reward: TweakDBID) -> Bool {
    if this.InSession() || this.m_lastQuest == 0u || this.Now() - this.m_lastQuestAt > 10.0 {
      return false;
    }
    return this.Rewarded(reward, this.m_lastQuest);
  }

  private func Rewarded(reward: TweakDBID, quest: Uint32) -> Bool {
    let i = 0;
    while i < ArraySize(this.m_rewarded) {
      if this.m_rewarded[i] == reward && i < ArraySize(this.m_rewardedQuests) && this.m_rewardedQuests[i] == quest {
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
