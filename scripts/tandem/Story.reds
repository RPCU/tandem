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
  // Story restrictions (Restrictions.reds): the host's reported, a guest's
  // worn while in the host's moment.
  private let m_restrictions: ref<TandemRestrictions>;
  private let m_spectate: ref<TandemSpectate>;
  // The host's quest prefabs in our world, the story's gates (StoryWorld.reds).
  private let m_world: ref<TandemStoryWorld>;
  private let m_curtain: ref<TandemCurtain>;

  // Host: the journal snapshot is owed (sent once the world plays), and the
  // changes since the last tick, sent together.
  // Every other player's scene tier and story flags as last heard (the
  // party HUD's "in a scene": PartyHud.reds).
  private let m_tierPlayers: array<Uint32>;
  private let m_tiers: array<Int32>;
  private let m_tierFlags: array<Int32>;
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
  // Both of those taken at the load for a join chosen in the co-op menu,
  // before its welcome (HoldEarly): given back if the welcome never comes.
  private let m_heldEarly: Bool;
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
  // Our own journal at the session's first snapshot (every entry not
  // Inactive, the walk's order): the baseline the leave is checked against.
  private let m_baseHashes: array<Uint32>;
  private let m_baseStates: array<Int32>;
  // The gig entries the last switch to apart gave back (the grouped switch
  // accounts for each), and whether we were apart at all this session (our
  // gig entries are then our game's, not the baseline's).
  private let m_given: array<Uint32>;
  private let m_givenStates: array<Int32>;
  private let m_wasApart: Bool;
  // Our journal could not be put back as it was (an apart switch or a
  // leave): saving stays locked until a save is loaded, whatever a later
  // session's restore says (its baseline is the damaged journal).
  private let m_journalDamaged: Bool;

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

  // Quest rewards this V got in co-op before the ledger (pairs: reward
  // record, its quest's journal hash): moved into the ledger at the first
  // load, then empty. Kept declared so older saves still hand them over.
  private persistent let m_rewarded: array<TweakDBID>;
  private persistent let m_rewardedQuests: array<Uint32>;
  // The ledger of what our V did and earned in co-op (core/carryover.hpp:
  // its packed words; docs/architecture/quest-carryover.md), in our own
  // save: written by the same save as what it describes, lost by the same
  // load. The quests we took part in (with whom, completed), the rewards
  // paid (once, whoever the host), the vehicles earned (Garage.reds).
  private persistent let m_ledger: array<Uint64>;
  // Race rewards our own game just paid (PayRace): not passed on to the
  // guests as our quest's (OnOurReward), for a few seconds.
  private let m_raceRewards: array<TweakDBID>;
  private let m_raceRewardsAt: Float;
  // Our garage, and the host's news of it (Garage.reds).
  private let m_garage: ref<TandemGarage>;
  // A guest's use of the host's objects, done by the host's game
  // (Interact.reds).
  private let m_interact: ref<TandemInteract>;
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
    // The ledger as this save has it (and the rewards of a save from
    // before it).
    TandemStory.CarryLoad(this.m_ledger);
    this.MigrateRewards();
    this.m_garage = new TandemGarage();
    this.m_garage.Start(this.GetGameInstance());
    this.m_scenes = new TandemScenes();
    this.m_scenes.Start(this.GetGameInstance(), this);
    this.m_restrictions = new TandemRestrictions();
    this.m_restrictions.Start(this.GetGameInstance(), this);
    this.m_spectate = new TandemSpectate();
    this.m_spectate.Start(this.GetGameInstance(), this);
    this.m_interact = new TandemInteract();
    this.m_interact.Start(this.GetGameInstance(), this);
    this.m_world = new TandemStoryWorld();
    this.m_world.Start(this.GetGameInstance(), this);
    this.m_curtain = new TandemCurtain();
    this.m_curtain.Start(this.GetGameInstance());
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
    // A join the co-op menu chose: our own story waits from the load.
    let chosen = TandemNative.SessionStatus();
    if this.m_you == 0u && chosen.role == 2 && TandemNative.Arriving() {
      this.HoldEarly();
    }
    this.Schedule();
  }

  // From the load to the host's welcome (seconds, through a relay more),
  // our own story would run on: a new V's prologue its first steps (a scene,
  // items, restrictions), a save its quest's next ones. It waits from the
  // load instead, saving locked as in the session (a save made now would
  // lose the signals held). The welcome keeps both (GuestJoin); a session
  // that ends before it gives them back (ReleaseEarly).
  private func HoldEarly() -> Void {
    if this.m_frozen || !TandemStory.FreezeAvailable() || !TandemStory.Freeze(true) {
      return;
    }
    this.m_frozen = true;
    this.m_heldEarly = true;
    if !this.m_saveLocked {
      this.m_saveLocked = true;
      SaveLocksManager.RequestSaveLockAdd(this.GetGameInstance(), n"tandem");
      GameInstance.GetAutoSaveSystem(this.GetGameInstance()).InvalidateAutoSaveRequests();
    }
    TandemNative.Log("quest: own quests held from the load, until the host's welcome (a join of the co-op menu)");
  }

  private func ReleaseEarly() -> Void {
    if !this.m_heldEarly {
      return;
    }
    this.m_heldEarly = false;
    if this.m_frozen {
      TandemStory.Freeze(false);
      this.m_frozen = false;
    }
    if this.m_saveLocked {
      this.m_saveLocked = false;
      SaveLocksManager.RequestSaveLockRemove(this.GetGameInstance(), n"tandem");
    }
    TandemNative.Log(s"quest: own quests resumed, no welcome came (\(TandemStory.FreezeReport()))");
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
      if IsDefined(this.m_garage) {
        this.m_garage.Tick(this.Now(), this.m_isHost);
      }
      if IsDefined(this.m_interact) {
        this.m_interact.Tick();
      }
      if IsDefined(this.m_world) {
        this.m_world.Tick(this.Now(), this.m_isHost, this.Grouped());
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

  private func NoteTier(player: Uint32, tier: Int32, flags: Int32) -> Void {
    let i = ArrayFindFirst(this.m_tierPlayers, player);
    if i < 0 {
      ArrayPush(this.m_tierPlayers, player);
      ArrayPush(this.m_tiers, tier);
      ArrayPush(this.m_tierFlags, flags);
    } else {
      this.m_tiers[i] = tier;
      this.m_tierFlags[i] = flags;
    }
  }

  // A player's GameplayTier (1..5; 0: never heard) and its kStory* flags.
  public func TierOf(player: Uint32) -> Int32 {
    let i = ArrayFindFirst(this.m_tierPlayers, player);
    return i < 0 ? 0 : this.m_tiers[i];
  }

  public func StoryFlagsOf(player: Uint32) -> Int32 {
    let i = ArrayFindFirst(this.m_tierPlayers, player);
    return i < 0 ? 0 : this.m_tierFlags[i];
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

  public func Restrictions() -> ref<TandemRestrictions> {
    return this.m_restrictions;
  }

  public func Spectate() -> ref<TandemSpectate> {
    return this.m_spectate;
  }

  public func World() -> ref<TandemStoryWorld> {
    return this.m_world;
  }

  // The black screen over a switch (Curtain.reds): spectating, a regroup.
  public func Curtain() -> ref<TandemCurtain> {
    return this.m_curtain;
  }

  public func Interact() -> ref<TandemInteract> {
    return this.m_interact;
  }

  // Grouped with the host (the same world; regions: not apart).
  public func Grouped() -> Bool {
    return !this.m_apart;
  }

  // The host's scene tier as heard, private scenes included (a guest
  // inside one's area is held as well: Restrictions.reds).
  public func HostTierRaw() -> Int32 {
    return this.m_hostTier;
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

  // The HUD's warning line (Text.reds).
  public func Notice(key: String, opt a: String, opt b: String) -> Void {
    TandemText.Notice(this.GetGameInstance(), key, a, b);
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
          if IsDefined(this.m_interact) {
            this.m_interact.End();
          }
          if !this.m_isHost && this.m_you != 0u {
            this.GuestLeave();
          } else if this.m_you == 0u {
            // An attempt ended before the welcome while the join tries again
            // (coop-arrive-load: the first failed, the retry got in a moment
            // later, our quests let go in between): held until it gives up.
            let status = TandemNative.SessionStatus();
            if status.role == 2 && (status.state == 1 || status.state == 2 || status.state == 4) {
              TandemNative.Log("quest: own quests still held: the join tries again");
            } else {
              this.ReleaseEarly();
            }
          }
          if this.m_isHost {
            this.m_scenes.HostEnd();
            this.m_spectate.HostEnd();
            if IsDefined(this.m_world) {
              this.m_world.Leave(false);
            }
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
          this.m_spectate.OnPlayerGone(event.player);
          if IsDefined(this.m_world) {
            this.m_world.OnPlayerGone(event.player);
          }
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
          this.NoteTier(event.player, event.state, event.flags);
          if event.player == this.m_host && !this.m_isHost {
            this.m_hostTier = event.state;
            this.OnHostFlags(event.flags);
          }
          if event.player != this.m_you {
            this.SetBusy(event.player, (event.flags & 8) != 0);
            // A guest watching the host's moment is parked (Spectate.reds).
            this.m_spectate.OnPlayerFlags(event.player, event.flags);
          }
          break;
        case 42:
          this.OnHostEffect(event.reward, event.flags != 0);
          break;
        case 44:
          // The host's story restrictions (worn at the next check while in
          // its moment, Restrictions.reds).
          if !this.m_isHost {
            TandemNative.Log(s"story: the host has \(event.index) story restriction(s) on");
          }
          break;
        case 45:
          // The host's world prefabs (made true in ours at the next check).
          if !this.m_isHost && IsDefined(this.m_world) {
            this.m_world.OnHostPrefabs(event.index);
          }
          break;
        case 46:
          if IsDefined(this.m_world) {
            this.m_world.OnGates(event.player, event.flags);
          }
          break;
        case 50:
        case 51:
        case 52:
        case 53:
        case 54:
        case 55:
        case 56:
        case 57:
        case 58:
        case 62:
        case 63:
          if !this.m_isHost {
            this.m_scenes.OnEvent(event);
          }
          break;
        case 41:
          // A reward our game lacks (a mod's; said on screen): not given.
          if TDBID.IsValid(event.reward) {
            this.OnReward(event.reward, event.hash);
          }
          break;
        case 60:
        case 61:
          if IsDefined(this.m_interact) {
            this.m_interact.OnEvent(event);
          }
          break;
        case 43:
          // A vehicle the host's quest gave while we took part (one our game
          // lacks is none: nothing to credit).
          if !this.m_isHost && TDBID.IsValid(event.reward) && IsDefined(this.m_garage) && this.m_garage.Earned(event.reward, event.hash, this.NameOf(this.m_host)) {
            this.m_ledger = TandemStory.CarryWords();
          }
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
    // The restrictions on our V a guest beside it shares.
    this.m_restrictions.HostTick(this.Now(), this.m_ourTier);
    // Our scripted moment, for the guests beside us to watch (Spectate.reds).
    this.m_spectate.HostTick(this.Now(), this.m_ourTier, this.m_inPrivate);
  }

  // JournalManager callbacks (signature of JournalNotificationQueue's,
  // final.reds:341545).
  protected cb func OnJournalState(hash: Uint32, className: CName, notifyOption: JournalNotifyOption, changeType: JournalChangeType) -> Bool {
    TandemNative.HookRan(n"Story.TandemStorySystem.OnJournalState");
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
    TandemNative.HookRan(n"Story.TandemStorySystem.OnJournalTracked");
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
    // Held since the load (HoldEarly): the session's now.
    this.m_heldEarly = false;
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
    // Whether we watch the host's moment first: the holds follow it.
    this.m_spectate.GuestTick(this.Now());
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

  // An entry's state in our journal (-1: not in our game).
  private func StateOf(hash: Uint32) -> Int32 {
    let entry = this.m_journal.GetEntry(hash);
    if !IsDefined(entry) {
      return -1;
    }
    let state: Int32 = EnumInt(this.m_journal.GetEntryState(entry));
    return state;
  }

  // Undefined (never set), Inactive and not in our game are the same to the
  // game: not part of anyone's story.
  private func SameState(now: Int32, before: Int32) -> Bool {
    return now <= 1 ? before <= 1 : now == before;
  }

  // One entry back to `before` if it is not (true: it was changed).
  private func PutBack(hash: Uint32, before: Int32) -> Bool {
    let now = this.StateOf(hash);
    if now < 0 || this.SameState(now, before) {
      return false;
    }
    let want = before <= 1 ? 1 : before;
    // The engine refuses Failed on an Inactive entry: through Active.
    if want == 4 && now <= 1 {
      this.m_journal.ChangeEntryStateByHash(hash, gameJournalEntryState.Active, JournalNotifyOption.DoNotNotify);
    }
    this.m_journal.ChangeEntryStateByHash(hash, IntEnum<gameJournalEntryState>(want), JournalNotifyOption.DoNotNotify);
    return true;
  }

  // Entries back to their states before the mirror (the walk's order:
  // parents first), checked; returns how many are not as they were.
  // One ChangeEntryStateByHash changes more than its entry (engine
  // story.md §13.99): Active activates every ancestor below the root
  // folder whatever its state, Succeeded and Failed end every Active
  // descendant alike, a phone reply chosen fails its siblings, and Failed
  // is refused on an Inactive entry. So the Failed ones go first (through
  // Active, deepest first, while their parents are still hidden), then
  // every entry in order, again until a pass changes nothing; what is left
  // is logged entry by entry (the first few).
  private func RestoreEntries(hashes: array<Uint32>, befores: array<Int32>, what: String) -> Int32 {
    let count = Min(ArraySize(hashes), ArraySize(befores));
    let i = count - 1;
    while i >= 0 {
      if befores[i] == 4 {
        this.PutBack(hashes[i], 4);
      }
      i -= 1;
    }
    let passes = 0;
    let changed = 1;
    let firstPass = 0;
    while changed > 0 && passes < 4 {
      changed = 0;
      i = 0;
      while i < count {
        if this.PutBack(hashes[i], befores[i]) {
          changed += 1;
        }
        i += 1;
      }
      if passes == 0 {
        firstPass = changed;
      }
      passes += 1;
    }
    let mismatches = 0;
    i = 0;
    while i < count {
      let now = this.StateOf(hashes[i]);
      if !this.SameState(now, befores[i]) {
        mismatches += 1;
        if mismatches <= 5 {
          this.LogEntry(s"\(what): not as before", hashes[i], befores[i], now);
        }
      }
      i += 1;
    }
    TandemNative.Log(s"journal: \(what): \(count) entries put back (\(firstPass) set, \(passes) passes), \(mismatches) not as before");
    return mismatches;
  }

  // One entry that is not as expected, with its class and parent: which of
  // the engine's rules moved it.
  private func LogEntry(what: String, hash: Uint32, expected: Int32, now: Int32) -> Void {
    let entry = this.m_journal.GetEntry(hash);
    if !IsDefined(entry) {
      TandemNative.Log(s"journal: \(what): \(hash) (not in our game) expected \(expected)");
      return;
    }
    let parent = this.m_journal.GetParentEntry(entry);
    let parentHash = 0u;
    let parentState = -1;
    if IsDefined(parent) {
      parentHash = Cast<Uint32>(this.m_journal.GetEntryHash(parent));
      parentState = EnumInt(this.m_journal.GetEntryState(parent));
    }
    TandemNative.Log(s"journal: \(what): \(hash) \(NameToString(entry.GetClassName())) \(entry.GetId()) expected \(expected) now \(now), parent \(parentHash) \(parentState), gig \(this.GigEntry(hash))");
  }

  // Leave: our whole journal against the baseline (the session's first
  // snapshot). An entry of the baseline the mirror never touched must be as
  // it was (an engine rule may have moved it), except our gigs after a time
  // apart (our game's then); an entry new since the join is our own game's
  // (an open-world phase ran), kept and counted. Returns the differences.
  private func CheckBaseline() -> Int32 {
    if ArraySize(this.m_baseHashes) == 0 {
      return 0;
    }
    let now: array<Uint32>;
    let nowStates: array<Int32>;
    this.Walk(now, nowStates);
    let differ = 0;
    let checked = 0;
    let i = 0;
    while i < ArraySize(this.m_baseHashes) {
      let hash = this.m_baseHashes[i];
      if !ArrayContains(this.m_touched, hash) && !(this.m_wasApart && this.GigEntry(hash)) {
        checked += 1;
        let state = this.StateOf(hash);
        if !this.SameState(state, this.m_baseStates[i]) {
          differ += 1;
          if differ <= 5 {
            this.LogEntry("leave: not as at the join", hash, this.m_baseStates[i], state);
          }
        }
      }
      i += 1;
    }
    let added = 0;
    i = 0;
    while i < ArraySize(now) {
      if !ArrayContains(this.m_baseHashes, now[i]) && !ArrayContains(this.m_touched, now[i]) {
        added += 1;
        if added <= 5 {
          this.LogEntry("leave: new since the join (kept)", now[i], 1, nowStates[i]);
        }
      }
      i += 1;
    }
    TandemNative.Log(s"journal: own entries: \(ArraySize(this.m_baseHashes)) at the join, \(ArraySize(now)) now (\(checked) untouched checked, \(differ) differ; \(added) new, kept)");
    return differ;
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
    this.NoteJournal(event.hash, event.state);
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
    // The baseline: our journal before the mirror's first change of the
    // session (a later snapshot finds it partly hidden already).
    if ArraySize(this.m_baseHashes) == 0 && ArraySize(this.m_touched) == 0 {
      this.m_baseHashes = own;
      this.m_baseStates = ownStates;
    }
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
    this.m_spectate.Leave();
    // What we loaded of the host's world (its quest interiors), off again.
    if IsDefined(this.m_world) {
      this.m_world.Leave(true);
    }
    this.ReleaseHolds();
    this.TakeOffEffects();
    // Back to the default before the journal is put back (our own gig
    // entries given back meanwhile are no longer in the touched list).
    this.m_apart = false;
    TandemStory.Grouped(true);
    this.m_hostFlags = 0;
    this.m_hostTier = 0;
    // Every entry the mirror changed, as it was; then the whole journal
    // against the baseline (what the engine's rules changed beside them).
    let mismatches = 0;
    if ArraySize(this.m_touched) > 0 {
      mismatches = this.RestoreEntries(this.m_touched, this.m_touchedBefore, "leave");
    }
    mismatches += this.CheckBaseline();
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
    // What we did with the host, for the next save.
    this.Persist();
    ArrayClear(this.m_touched);
    ArrayClear(this.m_touchedBefore);
    ArrayClear(this.m_baseHashes);
    ArrayClear(this.m_baseStates);
    ArrayClear(this.m_given);
    ArrayClear(this.m_givenStates);
    this.m_wasApart = false;
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
    if mismatches > 0 || lost || this.m_journalDamaged {
      // Never a save that differs from our own story: saving stays locked;
      // the way out is our last save. Once damaged, a later session's clean
      // restore does not unlock it either (it restores the damaged journal).
      this.m_journalDamaged = true;
      TandemNative.Log("save: stays locked (our story could not be put back exactly): load your last save");
      this.Notice("Story-NotRestored");
      return;
    }
    if this.m_saveLocked {
      this.m_saveLocked = false;
      SaveLocksManager.RequestSaveLockRemove(this.GetGameInstance(), n"tandem");
      TandemNative.Log("save: unlocked");
    }
    // Our story is ours again. What follows (go on in place, one save, a
    // reload: tandem.ini `leave`) is Saves.reds', once nothing else of the
    // session is left either; the next save keeps our V's gains and the
    // co-op reward ledger.
    TandemNative.Log("save: our story is back as it was");
  }

  // What of the host's story is still in our game (Saves.reds; the bits are
  // core/progress.hpp's): 1 its journal, our story on hold, our lock or its
  // quest props in our inventory;
  // 16 what we hold for its scenes (combat, god mode, story effects).
  public func LeftOver() -> Int32 {
    let left = 0;
    if this.m_saveLocked || this.m_frozen || ArraySize(this.m_touched) > 0 || this.m_ownTrackedKept || this.SessionItemsLeft() > 0 {
      left += 1;
    }
    if this.m_holdCombat || this.m_holdGod || ArraySize(this.m_effects) > 0 {
      left += 16;
    }
    return left;
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
      TandemNative.Log(s"story: weapons \(wantCombat ? "lowered" : "free") (host tier \(this.m_hostTier), \(distance) m)");
    }
    // Invulnerable is a status effect: script god mode does not protect V
    // (engine-knowledge §13.11).
    this.m_holdGod = wantGod;
    // Both go on and off with the host's story restrictions, through one
    // ledger (Restrictions.reds): an effect held for both, or our own
    // game's, is never taken off by the other.
    let holds: array<TweakDBID>;
    if wantCombat {
      ArrayPush(holds, t"GameplayRestriction.NoCombat");
    }
    if wantGod {
      ArrayPush(holds, t"BaseStatusEffect.Invulnerable");
    }
    // Watching the host's moment (Spectate.reds): our V parked, still and
    // safe, through the same ledger (the host's own set is not worn then).
    let watching = this.m_spectate.Watching();
    if watching {
      for hold in [t"GameplayRestriction.NoMovement", t"GameplayRestriction.NoCameraControl", t"GameplayRestriction.NoCombat", t"BaseStatusEffect.Invulnerable"] {
        if !ArrayContains(holds, hold) {
          ArrayPush(holds, hold);
        }
      }
    }
    this.m_restrictions.GuestTick(this.Now(), holds);
    // Far from a cinematic: offered to join (once per scene). Not while
    // watching it: we follow the host's camera wherever it goes.
    if tier >= 4 && !watching && (distance < 0.0 || distance > 100.0) {
      if !this.m_offerShown {
        this.m_offerShown = true;
        this.m_offerOpen = true;
        this.Notice("Story-SceneStarted", this.NameOf(this.m_host));
        // The hint bar draws the player's own binding for Choice1.
        TandemText.Hint(this.GetGameInstance(), n"TandemScene", n"Choice1", true, TandemText.F("Story-Join", this.NameOf(this.m_host)), inkInputHintHoldIndicationType.Press);
        GetPlayer(this.GetGameInstance()).RegisterInputListener(this, n"Choice1");
        TandemNative.Log("story: join offer shown");
      }
    } else if tier < 4 || watching {
      this.CloseOffer();
      this.m_offerShown = false;
    }
  }

  private func CloseOffer() -> Void {
    if this.m_offerOpen {
      this.m_offerOpen = false;
      TandemText.Hint(this.GetGameInstance(), n"TandemScene", n"Choice1", false, "", inkInputHintHoldIndicationType.Press);
      GetPlayer(this.GetGameInstance()).UnregisterInputListener(this, n"Choice1");
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Story.TandemStorySystem.OnAction");
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
    // The holds and the host's restrictions: what we put on comes off.
    this.m_restrictions.Leave();
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
    // Spectating (Spectate.reds): the host's scripted moment, a guest
    // watching it.
    if this.m_isHost && this.m_spectate.HostMoment() {
      flags += 16;
      if TandemStory.SpectateHostWatch() {
        flags += 64;
      }
    }
    if !this.m_isHost && this.m_spectate.Watching() {
      flags += 32;
    }
    this.m_spectate.KeepSpectatorsHidden();
    if flags != this.m_ourFlags {
      this.m_ourFlags = flags;
      TandemStory.SendState(this.m_ourTier, flags);
      TandemNative.Log(s"story: our story flags \(flags) (1 braindance, 2 Johnny, 8 private, 16 moment, 32 spectating, 64 to watch)");
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
    let tags = record.GameplayTags();
    if ArrayContains(tags, n"JohnnySickness") {
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
      this.Notice("Story-Busy", this.NameOf(player));

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
    // ours not in the host's journal are hidden again. The invariant, logged
    // both ways: every gig entry given back is hidden again at the grouped
    // switch, unless our game cleared it meanwhile; besides them, only gig
    // entries new while apart are hidden.
    let changed = 0;
    if apart {
      this.m_wasApart = true;
      // Ours out of the touched list in its order (parents first: the
      // engine's rules, RestoreEntries), the rest kept as they are.
      let gigs: array<Uint32>;
      let gigsBefore: array<Int32>;
      let rest: array<Uint32>;
      let restBefore: array<Int32>;
      let i = 0;
      while i < ArraySize(this.m_touched) {
        if this.GigEntry(this.m_touched[i]) {
          ArrayPush(gigs, this.m_touched[i]);
          ArrayPush(gigsBefore, this.m_touchedBefore[i]);
        } else {
          ArrayPush(rest, this.m_touched[i]);
          ArrayPush(restBefore, this.m_touchedBefore[i]);
        }
        i += 1;
      }
      this.m_touched = rest;
      this.m_touchedBefore = restBefore;
      changed = ArraySize(gigs);
      let wrong = this.RestoreEntries(gigs, gigsBefore, "apart");
      // Ours given back (the host's put back to hidden are not ours to hide
      // again).
      ArrayClear(this.m_given);
      ArrayClear(this.m_givenStates);
      i = 0;
      while i < ArraySize(gigs) {
        if gigsBefore[i] > 1 {
          ArrayPush(this.m_given, gigs[i]);
          ArrayPush(this.m_givenStates, gigsBefore[i]);
        }
        i += 1;
      }
      if wrong > 0 {
        // Our gig entries are no longer our save's: no save until a load.
        this.m_journalDamaged = true;
      }
      TandemNative.Log(s"story: apart from the host: our gigs are ours, \(changed) gig journal entries changed hands (\(ArraySize(this.m_given)) of ours given back, \(wrong) not as before)");
      return;
    }
    let own: array<Uint32>;
    let ownStates: array<Int32>;
    this.Walk(own, ownStates);
    let hidden = 0;
    let again = 0;
    for hash in own {
      if this.GigEntry(hash) && !ArrayContains(this.m_snapshotHashes, hash) && this.SetEntry(hash, 1, false) {
        hidden += 1;
        if ArrayContains(this.m_given, hash) {
          again += 1;
        }
      }
    }
    let shown = 0;
    let i = 0;
    while i < ArraySize(this.m_snapshotHashes) {
      if this.GigEntry(this.m_snapshotHashes[i]) && this.SetEntry(this.m_snapshotHashes[i], this.m_snapshotStates[i], false) {
        shown += 1;
      }
      i += 1;
    }
    // Given back and no longer in our journal: our game cleared it while
    // apart (its own story; not ours to hide).
    let cleared = 0;
    i = 0;
    while i < ArraySize(this.m_given) {
      if !ArrayContains(own, this.m_given[i]) {
        cleared += 1;
        if cleared <= 5 {
          this.LogEntry("grouped: given back, since cleared", this.m_given[i], this.m_givenStates[i], this.StateOf(this.m_given[i]));
        }
      }
      i += 1;
    }
    changed = hidden + shown;
    TandemNative.Log(s"story: grouped with the host, \(changed) gig journal entries changed hands (\(hidden) of ours hidden: \(again) of the \(ArraySize(this.m_given)) given back, \(hidden - again) new while apart; \(cleared) given back and since cleared; \(shown) of the host's shown)");
    ArrayClear(this.m_given);
    ArrayClear(this.m_givenStates);
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
    if !this.IsHostInSession() || this.PaidForRace(reward) {
      return;
    }
    let at = GetPlayer(this.GetGameInstance()).GetWorldPosition();
    TandemStory.Reward(reward, at.X, at.Y, at.Z);
  }

  // Our V bought a vehicle (Garage.reds): ours alone, never a guest's.
  public func OnVehicleBought() -> Void {
    if IsDefined(this.m_garage) {
      this.m_garage.Bought();
    }
  }

  // tools/game.sh cmd garage [summon]
  public func GarageCommand(command: String) -> Void {
    if IsDefined(this.m_garage) {
      this.m_garage.Command(command);
    } else {
      TandemNative.Log("garage: story sync is off (tandem.ini story)");
    }
  }

  // Guest: a reward of the host's quest for our own V, once (the ledger:
  // a resend, a rejoin or another host pays nothing again; a quest we
  // finished alone before pays nothing either).
  private func OnReward(reward: TweakDBID, questHash: Uint32) -> Void {
    if this.m_isHost {
      return;
    }
    this.NoteQuest(questHash);
    let pay = TandemStory.CarryPay(questHash, reward);
    if pay == 1 {
      TandemNative.Log(s"reward: \(TDBID.ToStringDEBUG(reward)) already given in co-op");
      return;
    }
    if pay == 2 {
      TandemNative.Log(s"reward: \(TDBID.ToStringDEBUG(reward)) (quest \(questHash)): we finished that quest alone, not paid again");
      return;
    }
    this.Persist();
    // By our own rules: our level, our perks, our toasts (final.reds:37406).
    RPGManager.GiveReward(this.GetGameInstance(), reward);
    TandemNative.Log(s"reward: given \(TDBID.ToStringDEBUG(reward)) (quest \(questHash))");
  }

  // A street race we raced in a partner's race paid us its reward
  // (Races.reds): once per (race quest, reward) in the ledger, like a
  // quest's; 0 paid now, 1 already paid in co-op, 2 we finished that quest
  // alone. `key` is the race quest's journal hash (or the race's own key).
  public func PayRace(reward: TweakDBID, key: Uint32) -> Int32 {
    let pay = TandemStory.CarryPay(key, reward);
    if pay != 0 {
      TandemNative.Log(s"races: reward \(TDBID.ToStringDEBUG(reward)) (race \(key)) not paid again (\(pay == 1 ? "already given in co-op" : "that race done alone"))");
      return pay;
    }
    this.Persist();
    if this.Now() - this.m_raceRewardsAt > 10.0 || this.m_raceRewardsAt > this.Now() {
      ArrayClear(this.m_raceRewards);
    }
    ArrayPush(this.m_raceRewards, reward);
    this.m_raceRewardsAt = this.Now();
    RPGManager.GiveReward(this.GetGameInstance(), reward);
    TandemNative.Log(s"races: reward given \(TDBID.ToStringDEBUG(reward)) (race \(key))");
    return 0;
  }

  // (read only: the reward hook calls it) one of those, a moment ago.
  private func PaidForRace(reward: TweakDBID) -> Bool {
    let now = this.Now();
    return now - this.m_raceRewardsAt <= 10.0 && now >= this.m_raceRewardsAt && ArrayContains(this.m_raceRewards, reward);
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
    return !TandemStory.CarryOwnPays(this.m_lastQuest, reward);
  }

  // ---- the ledger (core/carryover.hpp) ----

  // The ledger changed: its words into our persistent field, for the next
  // save (never one in a session: the guest's lock).
  private func Persist() -> Void {
    this.m_ledger = TandemStory.CarryWords();
  }

  // A save from before the ledger: its paid rewards go into it, once.
  private func MigrateRewards() -> Void {
    let moved = 0;
    let i = 0;
    while i < ArraySize(this.m_rewarded) && i < ArraySize(this.m_rewardedQuests) {
      TandemStory.CarryPay(this.m_rewardedQuests[i], this.m_rewarded[i]);
      moved += 1;
      i += 1;
    }
    if moved > 0 || ArraySize(this.m_rewarded) > 0 {
      ArrayClear(this.m_rewarded);
      ArrayClear(this.m_rewardedQuests);
      this.Persist();
      TandemNative.Log(s"ledger: \(moved) reward(s) of an older save moved into it");
    }
  }

  // Our own journal's state of a quest before the host's mirror changed it
  // (gameJournalEntryState: 1 not started, 2 active, 3 done, 4 failed; 0 not
  // in our game, a quest of content we lack).
  private func OwnStateOf(questHash: Uint32) -> Int32 {
    let i = 0;
    while i < ArraySize(this.m_touched) {
      if this.m_touched[i] == questHash && i < ArraySize(this.m_touchedBefore) {
        return Max(this.m_touchedBefore[i], 1);
      }
      i += 1;
    }
    let entry = this.m_journal.GetEntry(questHash);
    if !IsDefined(entry) {
      return 0;
    }
    let state: Int32 = EnumInt(this.m_journal.GetEntryState(entry));
    return Max(state, 1);
  }

  // Guest: we take part in the host's quest (its first note fixes its kind,
  // our own state of it and the host).
  private func NoteQuest(questHash: Uint32) -> Void {
    if questHash == 0u || !IsDefined(this.m_journal) {
      return;
    }
    let entry = this.m_journal.GetEntry(questHash);
    let kind: Int32 = IsDefined(entry) ? EnumInt(this.m_journal.GetQuestType(entry)) : 255;
    TandemStory.CarryQuest(questHash, kind, this.OwnStateOf(questHash), this.NameOf(this.m_host));
  }

  // Guest: a change of the host's journal while near the host (the reward
  // rule's 150 m): the quest it belongs to is one we take part in, and its
  // end, seen with the host, is ours too.
  private func NoteJournal(hash: Uint32, state: Int32) -> Void {
    let distance = this.DistanceToHost();
    if distance < 0.0 || distance > 150.0 {
      return;
    }
    let entry = this.m_journal.GetEntry(hash);
    if !IsDefined(entry) {
      return;
    }
    let questEntry = this.QuestOf(entry);
    if !IsDefined(questEntry) {
      return;
    }
    let questHash = Cast<Uint32>(this.m_journal.GetEntryHash(questEntry));
    this.NoteQuest(questHash);
    // 3: Succeeded, on the quest itself.
    if questHash == hash && state == 3 {
      TandemStory.CarryCompleted(questHash);
      TandemNative.Log(s"ledger: quest \(questHash) done with \(this.NameOf(this.m_host))");
    }
    this.Persist();
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
    } else if Equals(command, "story prefabs") || Equals(command, "story gates") {
      if IsDefined(this.m_world) {
        this.m_world.Report(StrAfterFirst(command, "story "));
      }
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
    } else if StrBeginsWith(command, "story spectate") || StrBeginsWith(command, "story moments") {
      this.m_spectate.Command(command);
    } else if StrBeginsWith(command, "story interact") {
      this.m_interact.Command(command);
    } else if Equals(command, "story restrict") {
      this.m_restrictions.Report();
    } else if StrBeginsWith(command, "story restrict on ") || StrBeginsWith(command, "story restrict off ") {
      // (test) a restriction on our own V, as a quest would put it on: the
      // host reports it to its guests.
      let turnOn = StrBeginsWith(command, "story restrict on ");
      let record = TDBID.Create(StrAfterFirst(StrAfterFirst(command, "restrict "), " "));
      let player = GetPlayer(game);
      if turnOn {
        StatusEffectHelper.ApplyStatusEffect(player, record);
      } else {
        StatusEffectHelper.RemoveStatusEffect(player, record);
      }
      TandemNative.Log(s"story: test restriction \(TDBID.ToStringDEBUG(record)) \(turnOn ? "on" : "off") our V");
    } else if Equals(command, "story join") {
      this.AcceptOffer();
    } else if Equals(command, "story state") {
      TandemNative.Log(s"story: you \(this.m_you), host \(this.m_host), frozen \(this.m_frozen), save locked \(this.m_saveLocked), touched \(ArraySize(this.m_touched)), host tier \(this.m_hostTier), distance \(this.DistanceToHost())");
      TandemNative.Log(TandemStory.CarryReport());
    } else if Equals(command, "story carry") {
      TandemNative.Log(TandemStory.CarryReport());
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
  TandemNative.HookRan(n"Story.PlayerPuppet.OnSceneTierChange");
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
  // A street race's reward on its starter's V (Races.reds): queued.
  if IsDefined(GetPlayer(this.GetGame())) && this.GetEntityID() == GetPlayer(this.GetGame()).GetEntityID() {
    TandemActivity.QueueRaceReward(evt.rewardName);
  }
  return result;
}

// Story effects of our V (final.reds:43899, :44474): the host's glitches
// reach the guests near it.
@wrapMethod(PlayerPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  let result = wrappedMethod(evt);
  // A restriction on our V while we wear the host's (Restrictions.reds).
  if TandemRestrictions.IsRestriction(evt.staticData) || (IsDefined(evt.staticData) && evt.staticData.GetID() == t"BaseStatusEffect.Invulnerable") {
    let guest = TandemStorySystem.Get(this.GetGame());
    let own = GetPlayer(this.GetGame());
    if IsDefined(guest) && guest.IsGuest() && IsDefined(own) && this.GetEntityID() == own.GetEntityID() {
      guest.Restrictions().OnApplied(evt.staticData, evt.isNewApplication);
    }
  }
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
  if TandemRestrictions.IsRestriction(evt.staticData) || (IsDefined(evt.staticData) && evt.staticData.GetID() == t"BaseStatusEffect.Invulnerable") {
    let guest = TandemStorySystem.Get(this.GetGame());
    let own = GetPlayer(this.GetGame());
    if IsDefined(guest) && guest.IsGuest() && IsDefined(own) && this.GetEntityID() == own.GetEntityID() {
      guest.Restrictions().OnRemoved(evt.staticData, evt.isFinalRemoval);
    }
  }
  if evt.isFinalRemoval && TandemStorySystem.IsStoryEffect(evt.staticData) {
    let story = TandemStorySystem.Get(this.GetGame());
    let player = GetPlayer(this.GetGame());
    if IsDefined(story) && IsDefined(player) && this.GetEntityID() == player.GetEntityID() {
      story.OnOurEffect(evt.staticData.GetID(), false);
    }
  }
  return result;
}
