// Each player is detected on their own (docs/architecture/stealth.md). Only the
// host's NPCs are real, so they decide who is seen and hunted:
//
// - Guest: its V enters and leaves combat by the game's own switch, the
//   player combat controller's "tracked as hostile" flag (what an NPC
//   tracking V as a hostile threat sets, final.reds 242808-242816), driven
//   by the host's NpcEntry::detected bit for our player. The PSM combat
//   state it sets carries the rest (music, HUD, fast travel lock, stealth
//   bonuses: AttackData.IsPlayerInCombat, combat-only perks). The host's
//   awareness levels drive the stealth meter (UI_Stealth blackboard, each
//   proxy's detection percentage and sense mappin).
// - Every game: its V's stealth (optical camo, Visibility stat, crouch)
//   goes to the others (Stealth), and is put on its puppet there, where
//   the host's NPC senses look: camouflaged, the puppet's visible object
//   is off.
// - Every game: the stimuli its V gives out (gunshots, its footsteps at a
//   run or a sprint, its landings: what its own state machine broadcast, so
//   a crouched V or one whose perks silence it sends none) are broadcast
//   again from its puppet on the games that run NPCs near it (Noise). There
//   a puppet stands for the player it is where vanilla asks "is this the
//   player's": a stealth stimulus is judged by the NPC's attitude to that
//   puppet, a player-only one passes for it.

// ---- our V's combat state, from the host's NPCs (guest) ----

// The host's NPCs have detected our V, and whether a local NPC (or device)
// tracks it as the game does it.
@addField(PlayerPuppet)
public let tandemHostTracked: Bool;

@addField(PlayerPuppet)
public let tandemLocalTracked: Bool;

@wrapMethod(PlayerPuppet)
protected cb func OnStartedBeingTrackedAsHostile(evt: ref<StartedBeingTrackedAsHostile>) -> Bool {
  this.tandemLocalTracked = true;
  return wrappedMethod(evt);
}

// The last local tracker let go: still in combat while the host's NPCs hunt us.
@wrapMethod(PlayerPuppet)
protected cb func OnStoppedBeingTrackedAsHostile(evt: ref<StoppedBeingTrackedAsHostile>) -> Bool {
  this.tandemLocalTracked = false;
  if this.tandemHostTracked {
    return true;
  }
  return wrappedMethod(evt);
}

// What an NPC starting (or stopping) to track V as hostile does to the
// combat controller; false: it is not there yet (try again later).
@addMethod(PlayerPuppet)
public func TandemSetHostTracked(tracked: Bool) -> Bool {
  if !IsDefined(this.m_combatController) {
    return false;
  }
  this.tandemHostTracked = tracked;
  if tracked {
    this.m_combatController.OnStartedBeingTrackedAsHostile(null);
  } else if !this.tandemLocalTracked {
    this.m_combatController.OnStoppedBeingTrackedAsHostile();
  }
  return true;
}

// ---- our V's noises, for the host's NPCs (guest) ----

// Every single stimulus a component broadcasts passes here; our V's own
// (ShootEvents: Gunshot, SilencedGunshot, IllegalAction...) go to the host.
@wrapMethod(StimBroadcasterComponent)
public final func TriggerSingleBroadcast(contextOwner: wref<GameObject>, gdStimType: gamedataStimType, opt radius: Float, opt investigateData: stimInvestigateData, opt propagationChange: Bool) -> Void {
  TandemNative.HookRan(n"Stealth.StimBroadcasterComponent.TriggerSingleBroadcast");
  wrappedMethod(contextOwner, gdStimType, radius, investigateData, propagationChange);
  let owner = this.GetOwner();
  if IsDefined(owner) && owner.IsPlayer() && TandemNative.IsStarted() {
    let stealth = TandemStealth.Of(owner.GetGame());
    if IsDefined(stealth) {
      stealth.OwnNoise(gdStimType, radius, propagationChange);
    }
  }
}

// ---- a puppet's noises, heard by the NPCs we run ----

// A stimulus tagged Stealth (a V's footsteps, its landing) is reacted to by
// who is hostile to the player that made it, or neutral and guarding the
// zone it trespasses; vanilla reads the attitude for the local player only
// (final.reds:148207), so a puppet's would always be ignored.
@wrapMethod(ReactionManagerComponent)
public final static func ReactOnPlayerStealthStim(owner: wref<GameObject>, target: wref<GameObject>) -> Bool {
  if TandemNative.IsStarted() && IsDefined(owner) && IsDefined(target) && TandemCombat.PuppetPlayer(owner.GetGame(), target) != 0u {
    let attitude = GameObject.GetAttitudeBetween(owner, target);
    if Equals(attitude, EAIAttitude.AIA_Hostile) {
      return true;
    }
    return Equals(attitude, EAIAttitude.AIA_Neutral) && owner.IsConnectedToSecuritySystem() && owner.IsTargetTresspassingMyZone(target);
  }
  return wrappedMethod(owner, target);
}

// A proxy reacts to nothing of its own: what its game's stimuli would make
// it do (a fear reaction in its workspot, a flinch, a look) is its owner's
// NPC's to decide and to say (its anims, its workspot: NpcAnims.reds). A
// proxy that took a stimulus of its game played the workspot's reaction and
// its exit while its owner sat on (the seated copies that still stood up by
// themselves, 1 in 90, 2026-11-23). Every stimulus goes through this gate
// first, the crowd's and the others' (ReactionManagerComponent.OnStimuliEvent,
// final.reds (2.50):67915).
// job thread safe: it reads (a lock-free native) and changes nothing
@wrapMethod(ReactionManagerComponent)
private final func ShouldEventBeProcessed(stimEvent: ref<StimuliEvent>) -> Bool {
  let owner = this.GetOwner();
  if TandemNative.IsStarted() && IsDefined(owner) && TandemPresence.IsNpcProxy(owner.GetEntityID()) {
    return false;
  }
  return wrappedMethod(stimEvent);
}

// A stimulus tagged PlayerOnly is dropped unless its source, or the attack's
// instigator it names, is a player (final.reds:145046). A puppet's movement
// noise names none: for the length of that judgement our own V is lent as
// the instigator (the stimuli this is done for, footsteps and landings, ask
// nothing else of it: StimFilters), then taken back, so whatever the NPC
// does next it does about the puppet.
// job thread safe: it writes only the stimulus event this NPC's task carries, as the wrapped method's own pipeline does (stimEvent.sourceObject, final.reds:146325), and no state of ours
@wrapMethod(ReactionManagerComponent)
private final func ShouldStimBeProcessed(stimEvent: ref<StimuliEvent>, delayed: Bool) -> Bool {
  if !TandemNative.IsStarted() || !IsDefined(stimEvent) || !TandemStealth.IsMovementNoise(stimEvent.GetStimType()) {
    return wrappedMethod(stimEvent, delayed);
  }
  let owner = this.GetOwner();
  let by = IsDefined(owner) ? TandemCombat.PuppetPlayer(owner.GetGame(), stimEvent.sourceObject) : 0u;
  if by == 0u {
    return wrappedMethod(stimEvent, delayed);
  }
  let lent = false;
  let data = stimEvent.stimInvestigateData;
  if stimEvent.IsTagInStimuli(n"PlayerOnly") && !IsDefined(data.attackInstigator) {
    data.attackInstigator = GetPlayer(owner.GetGame());
    stimEvent.stimInvestigateData = data;
    lent = true;
  }
  let heard = wrappedMethod(stimEvent, delayed);
  if lent {
    data.attackInstigator = null;
    stimEvent.stimInvestigateData = data;
  }
  if heard {
    TandemNative.LogDebug(s"stealth: \(EntityID.ToDebugStringDecimal(owner.GetEntityID())) reacts to player \(by)'s \(EnumValueToString("gamedataStimType", Cast<Int64>(EnumInt(stimEvent.GetStimType()))))");
  }
  return heard;
}

// A remote player's stealth as put on their puppet.
public class TandemStealthApplied {
  public let player: Uint32;
  // The stealth updates received for them that it shows, and on which body.
  public let changes: Int32;
  public let on: EntityID;
  public let flags: Int32;
  // The camo status effect put on the puppet (None: none).
  public let camo: TweakDBID;
}

public class TandemStealth {
  private let m_game: GameInstance;
  private let m_you: Uint32;
  private let m_now: Float;
  // Guest: the host's say (NPCs detecting us), the meter as last written,
  // the proxies we gave a sense mappin to, the PSM combat state last seen.
  private let m_detecting: Int32;
  private let m_meter: Float;
  private let m_meterHostile: Float;
  private let m_meterStep: Int32;
  private let m_marked: array<Uint32>;
  private let m_combatState: Int32;
  private let m_lastMeter: Float;
  // Our stealth as last sent.
  private let m_sent: Bool;
  private let m_sentFlags: Int32;
  private let m_sentVisibility: Float;
  private let m_sentCamo: TweakDBID;
  // Our V cloaked (fully blocking) while in combat with proxies around, not
  // out of it yet.
  private let m_cloakedInCombat: Bool;
  private let m_lastSample: Float;
  // Each remote player's stealth on their puppet. By player, not by index:
  // ids keep growing in a session (players coming back, mock players).
  private let m_applied: array<ref<TandemStealthApplied>>;
  // Guest: when each stimulus type was last sent (at most 10 a second each).
  private let m_noiseAt: array<Float>;
  // Host: noises heard since the last log line.
  private let m_noises: Int32;
  private let m_noiseLog: Float;
  // The stimulus kinds whose record was described in the log (once each).
  private let m_described: array<Int32>;

  // This game shows other games' NPCs (proxies) at the moment.
  public static func ShowsProxies(game: GameInstance) -> Bool {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    let npcs = IsDefined(system) ? system.NpcsSync() : null;
    return IsDefined(npcs) && npcs.ProxyCount() > 0;
  }

  public static func Of(game: GameInstance) -> ref<TandemStealth> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Stealth() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    let i = 0;
    while i < 70 {
      ArrayPush(this.m_noiseAt, -1.0);
      i += 1;
    }
  }

  public func Tick(now: Float, you: Uint32, npcs: ref<TandemNpcs>) -> Void {
    this.m_now = now;
    this.m_you = you;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if now - this.m_lastSample >= 0.25 {
      this.m_lastSample = now;
      this.SendOurs(player);
    }
    this.FollowOthers(player);
    // The NPCs we run hear the other Vs; the proxies we show notice ours.
    this.HearGuests();
    this.FollowHost(player, npcs);
  }

  // The session is over: our V is nobody's target any more.
  public func End() -> Void {
    let player = GetPlayer(this.m_game);
    if IsDefined(player) && player.tandemHostTracked {
      player.TandemSetHostTracked(false);
      TandemNative.Log("stealth: session over, out of the host's combat");
    }
    this.m_detecting = 0;
    ArrayClear(this.m_marked);
    if this.m_meter > 0.0 || this.m_meterHostile > 0.0 {
      this.WriteMeter(0.0, 0.0);
    }
    this.m_sent = false;
    ArrayClear(this.m_applied);
  }

  // ---- guest: what the host's NPCs know of us ----

  private func FollowHost(player: ref<PlayerPuppet>, npcs: ref<TandemNpcs>) -> Void {
    let detecting = TandemNative.NpcsDetectingUs();
    let want = detecting > 0;
    if NotEquals(player.tandemHostTracked, want) && player.TandemSetHostTracked(want) {
      if want {
        TandemNative.Log(s"stealth: \(detecting) host NPC(s) detect us: in combat");
      } else {
        TandemNative.Log("stealth: no host NPC detects us: out of combat");
      }
    }
    this.m_detecting = detecting;
    // The game's own combat state, as the PSM has it (1 combat, 2 out of
    // combat, 3 stealth).
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let state = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Combat) : 0;
    if state != this.m_combatState {
      this.m_combatState = state;
      TandemNative.Log(s"stealth: combat state \(state)\(player.IsInCombat() ? " (in combat)" : "")");
    }
    if IsDefined(npcs) && this.m_now - this.m_lastMeter >= 0.1 {
      this.m_lastMeter = this.m_now;
      this.Meter(player, npcs);
    }
  }

  // The stealth meter from the host's detection of us: on each proxy (its
  // detection percentage, and its sense mappin, the indicator over an NPC,
  // while it notices us) and on the HUD (UI_Stealth, which the detection
  // perks listen to).
  private func Meter(player: ref<PlayerPuppet>, npcs: ref<TandemNpcs>) -> Void {
    let ours = TandemStealth.BitOf(this.m_you);
    let highest = 0.0;
    let hostile = 0.0;
    let entities = GameInstance.GetDynamicEntitySystem();
    for proxy in npcs.Proxies() {
      let body = entities.GetEntity(proxy.id) as NPCPuppet;
      if IsDefined(body) && !proxy.dead {
        let level = TandemNative.NpcAwareness(proxy.npc);
        if (TandemNative.NpcDetected(proxy.npc) & ours) != 0 {
          level = 1.0;
        }
        body.SetDetectionPercentage(level * 100.0);
        this.Indicator(proxy.npc, body, level > 0.0);
        highest = MaxF(highest, level);
        if Equals(GameObject.GetAttitudeBetween(body, player), EAIAttitude.AIA_Hostile) {
          hostile = MaxF(hostile, level);
        }
      }
    }
    if AbsF(highest - this.m_meter) >= 0.01 || AbsF(hostile - this.m_meterHostile) >= 0.01 || (highest == 0.0 && this.m_meter > 0.0) {
      this.WriteMeter(highest, hostile);
    }
  }

  private func WriteMeter(highest: Float, hostile: Float) -> Void {
    this.m_meter = highest;
    this.m_meterHostile = hostile;
    let ui = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_Stealth);
    if IsDefined(ui) {
      ui.SetFloat(GetAllBlackboardDefs().UI_Stealth.highestDetectionOnPlayer, highest * 100.0);
      ui.SetFloat(GetAllBlackboardDefs().UI_Stealth.highestHostileDetectionOnPlayer, hostile * 100.0);
    }
    let step = FloorF(highest * 4.0);
    if step != this.m_meterStep {
      this.m_meterStep = step;
      TandemNative.Log(s"stealth: detection meter \(step * 25)%");
    }
  }

  // The indicator over a proxy while the host's NPC notices us. A proxy's
  // senses are off (TandemProxies.Pacify removed its mappin).
  private func Indicator(npc: Uint32, body: ref<NPCPuppet>, on: Bool) -> Void {
    let senses = body.GetSensesComponent();
    if !IsDefined(senses) {
      return;
    }
    let marked = ArrayContains(this.m_marked, npc);
    if on && !marked {
      if !senses.HasSenseMappin() {
        senses.CreateSenseMappin();
      }
      ArrayPush(this.m_marked, npc);
    } else if !on && marked {
      senses.RemoveSenseMappin();
      ArrayRemove(this.m_marked, npc);
    }
  }

  public static func BitOf(player: Uint32) -> Int32 {
    let bit = 1;
    let i = 1u;
    while i < player {
      bit *= 2;
      i += 1u;
    }
    return bit;
  }

  // ---- every game: our stealth to the others ----

  private func SendOurs(player: ref<PlayerPuppet>) -> Void {
    if this.m_cloakedInCombat && !player.IsInCombat() {
      this.m_cloakedInCombat = false;
      GameObject.PlaySoundEvent(player, n"ui_gmpl_stealth_avoid_combat");
      TandemNative.Log("stealth: emergency cloaking: the NPCs lost us, out of combat");
    }
    if this.m_cloakedInCombat && !player.IsInvisible() {
      this.m_cloakedInCombat = false;
    }
    let flags = 0;
    let camo = TDBID.None();
    for effect in StatusEffectHelper.GetAppliedEffects(player) {
      let record = effect.GetRecord();
      if IsDefined(record) && record.GameplayTagsContains(n"CamoActiveOnPlayer") {
        camo = record.GetID();
        flags |= 1;
      }
    }
    // The relic's fully blocking camo turns V's visible object off.
    if IsDefined(player.GetVisibleObjectComponent()) && player.IsInvisible() {
      flags |= 3;
    }
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let locomotion = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Locomotion) : 0;
    // gamePSMLocomotionStates: Crouch 1, CrouchSprint 11, CrouchDodge 12
    if locomotion == 1 || locomotion == 11 || locomotion == 12 {
      flags |= 4;
    }
    let visibility = GameInstance.GetStatsSystem(this.m_game).GetStatValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatType.Visibility);
    if this.m_sent && flags == this.m_sentFlags && AbsF(visibility - this.m_sentVisibility) < 0.01 && camo == this.m_sentCamo {
      return;
    }
    // The Relic's Emergency Cloaking, on a game that shows other games' NPCs
    // (a guest, or a host linked with a guest that owns NPCs, regions.md):
    // vanilla plays its "combat avoided" sound from the NPCs our V's tracker
    // held (PlayerPuppet.OnActivateOpticalCamoToExitCombatEvent, final.reds
    // 44034); proxies are no threats there, so it is played when their
    // owners' NPCs let go.
    if (flags & 2) != 0 && (this.m_sentFlags & 2) == 0 && TandemStealth.ShowsProxies(player.GetGame()) {
      this.m_cloakedInCombat = player.IsInCombat();
    }
    TandemNative.SendStealth(flags, visibility, camo);
    TandemNative.Log(s"stealth: ours sent: camo \((flags & 1) != 0) (\(TDBID.ToStringDEBUG(camo))), blocking \((flags & 2) != 0), crouch \((flags & 4) != 0), visibility \(visibility)");
    this.m_sent = true;
    this.m_sentFlags = flags;
    this.m_sentVisibility = visibility;
    this.m_sentCamo = camo;
  }

  // ---- every game: the others' stealth on their puppets ----

  private func FollowOthers(player: ref<PlayerPuppet>) -> Void {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    if !IsDefined(system) {
      return;
    }
    for p in system.PuppetPlayers() {
      let changes = p == this.m_you ? 0 : TandemNative.RemoteStealthChanges(p);
      let puppet = changes > 0 ? system.PuppetOf(p) : null;
      if IsDefined(puppet) {
        let applied = this.AppliedOf(p);
        if changes != applied.changes || puppet.GetEntityID() != applied.on {
          applied.changes = changes;
          applied.on = puppet.GetEntityID();
          this.Apply(p, puppet, player, applied);
        }
      }
    }
  }

  private func AppliedOf(p: Uint32) -> ref<TandemStealthApplied> {
    for applied in this.m_applied {
      if applied.player == p {
        return applied;
      }
    }
    let applied = new TandemStealthApplied();
    applied.player = p;
    applied.camo = TDBID.None();
    ArrayPush(this.m_applied, applied);
    return applied;
  }

  // How the player's V is seen, on their puppet: what vanilla does to its
  // own V's visible object (PlayerPuppet.SetInvisible,
  // UpdateVisibilityModifier, UpdateSecondaryVisibilityOffset), and the
  // camo status effect itself for its look. The flags decide camo; the
  // effect id (0 when unknown, e.g. the mock player) only adds its look.
  private func Apply(p: Uint32, puppet: ref<GameObject>, player: ref<PlayerPuppet>, state: ref<TandemStealthApplied>) -> Void {
    let flags = TandemNative.RemoteStealthFlags(p);
    let visibility = TandemNative.RemoteStealthVisibility(p);
    let camo = TandemNative.RemoteStealthCamo(p);
    let camoOn = (flags & 1) != 0;
    let wasOn = (state.flags & 1) != 0;
    // NPC senses see the puppet through its (grafted) visible object.
    for component in puppet.GetComponents() {
      if Equals(component.GetClassName(), n"senseVisibleObjectComponent") {
        component.Toggle(!camoOn);
      }
    }
    if visibility >= 0.0 {
      let multiplier = new VisibleObjectDetectionMultEvent();
      multiplier.multiplier = visibility;
      puppet.QueueEvent(multiplier);
    }
    let offset = new VisibleObjectSecondaryPositionEvent();
    offset.offset.Z = (flags & 4) != 0 ? TweakDBInterface.GetFloat(t"player.stealth.crouchVisibilityZOffset", 0.6) : TweakDBInterface.GetFloat(t"player.stealth.chestVisibilityZOffset", 1.3);
    puppet.QueueEvent(offset);
    // The camo's own effect (its look, its vision blocker), in place of the last.
    let applied = state.camo;
    if TDBID.IsValid(applied) && (!camoOn || applied != camo) {
      StatusEffectHelper.RemoveStatusEffect(puppet, applied);
      state.camo = TDBID.None();
    }
    if camoOn && TDBID.IsValid(camo) && state.camo != camo {
      StatusEffectHelper.ApplyStatusEffect(puppet, camo, puppet.GetEntityID());
      state.camo = camo;
    }
    // Fully blocking (the Relic's Emergency Cloaking): the NPCs hunting them
    // lose them, as vanilla drops V from its hostile NPCs' trackers
    // (final.reds 44034-44048), and forget having seen them (what keeps
    // them detected here, Npcs.reds Detected); the NPCs' memory wipe that
    // follows in vanilla would free every V, so it is left out. The NPCs our
    // game runs around our V (those we share) and around theirs.
    if (flags & 2) != 0 && (state.flags & 2) == 0 {
      let dropped = 0;
      let around = player.GetNPCsAroundObject(TandemNpcs.Range());
      for near in puppet.GetNPCsAroundObject(TandemNpcs.Range()) {
        if !ArrayContains(around, near) {
          ArrayPush(around, near);
        }
      }
      for npc in around {
        let tracker = npc.GetTargetTrackerComponent();
        let threat: TrackedLocation;
        if IsDefined(tracker) && tracker.ThreatFromEntity(puppet, threat) {
          tracker.DeactivateThreat(puppet);
          let senses = npc.GetSensesComponent();
          if IsDefined(senses) {
            senses.ResetDetection(puppet.GetEntityID());
          }
          dropped += 1;
        }
      }
      TandemNative.Log(s"stealth: player \(p)'s camo blocks: \(dropped) NPC(s) lost them");
    }
    if NotEquals(camoOn, wasOn) {
      TandemNative.Log(s"stealth: player \(p) camo \(camoOn ? "on" : "off") (\(TDBID.ToStringDEBUG(camo)))");
    }
    TandemNative.Log(s"stealth: player \(p): visibility \(visibility), crouch \((flags & 4) != 0)");
    state.flags = flags;
  }

  // ---- host: a V getting out of combat lets only that V go ----

  // The memory wipes that take a V out of combat in single player:
  // Emergency Cloaking's (PlayerPuppet.OnActivateOpticalCamoToExitCombatEvent,
  // final.reds 39730, passed on to the NPC's squad, 126490-126503) and the
  // second chance's (OnDodgeToAvoidCombatEvent, 39689; a guest's comes as a
  // request, Reactions.reds). Their ClearThreats (NPCPuppet
  // .OnStatusEffectApplied, 126486) empties the NPC's whole tracker, the
  // other Vs it hunts with it: those are kept (OthersHunted before the
  // effect, HuntAgain after), so only the V who got away is out of combat.
  public static func IsWayOut(effect: TweakDBID) -> Bool {
    return effect == t"BaseStatusEffect.MemoryWipeExitCombat" || effect == t"BaseStatusEffect.MemoryWipeOnDodge";
  }

  public static func OthersHunted(npc: ref<NPCPuppet>, evt: ref<ApplyStatusEffectEvent>) -> array<wref<GameObject>> {
    let others: array<wref<GameObject>>;
    if !IsDefined(evt.staticData) || !TandemStealth.IsWayOut(evt.staticData.GetID()) || !TandemOwn.Entity(npc.GetGame(), npc.GetEntityID()) {
      return others;
    }
    let system = TandemCombat.System(npc.GetGame());
    let tracker = npc.GetTargetTrackerComponent();
    let player = GetPlayer(npc.GetGame());
    if !IsDefined(system) || system.You() == 0u || !IsDefined(tracker) || !IsDefined(player) {
      return others;
    }
    // Who got away: the guest whose puppet the effect names, else our V
    // (vanilla puts these on for the local player only).
    let away = evt.instigatorEntityID;
    if !EntityID.IsDefined(away) || system.PlayerOfPuppet(away) == 0u {
      away = player.GetEntityID();
    }
    for threat in tracker.GetHostileThreats(false) {
      let v = threat.entity as GameObject;
      if IsDefined(v) && v.GetEntityID() != away && (v.IsPlayer() || system.PlayerOfPuppet(v.GetEntityID()) != 0u) {
        ArrayPush(others, v);
      }
    }
    return others;
  }

  public static func HuntAgain(npc: ref<NPCPuppet>, others: array<wref<GameObject>>) -> Void {
    for v in others {
      if IsDefined(v) && !TargetTrackingExtension.IsThreatInThreatList(npc, v, false, true) {
        TargetTrackingExtension.InjectThreat(npc, v);
        TandemNative.Log(s"stealth: \(EntityID.ToDebugStringDecimal(npc.GetEntityID())) forgot one V, still hunts \(EntityID.ToDebugStringDecimal(v.GetEntityID()))");
      }
    }
  }

  // ---- noises ----

  // gamedataStimType: what a V's state machine broadcasts as it moves
  // (presence::IsMovementNoise): FootStepRegular 25, FootStepSprint 26,
  // LandingHard 34, LandingRegular 35, LandingVeryHard 36.
  public static func IsMovementNoise(stim: gamedataStimType) -> Bool {
    return Equals(stim, gamedataStimType.FootStepRegular) || Equals(stim, gamedataStimType.FootStepSprint)
      || Equals(stim, gamedataStimType.LandingHard) || Equals(stim, gamedataStimType.LandingRegular)
      || Equals(stim, gamedataStimType.LandingVeryHard);
  }

  // What a puppet's stimulus is by its record, once a kind: its reach, and
  // the tags vanilla judges it by (see the wraps above).
  private func Describe(puppet: ref<GameObject>, kind: Int32, radius: Float, propagation: Bool) -> Void {
    if ArrayContains(this.m_described, kind) {
      return;
    }
    ArrayPush(this.m_described, kind);
    let data: stimInvestigateData;
    let event = StimBroadcasterComponentHelper.CreateStimEvent(puppet, IntEnum<gamedataStimType>(kind), radius, data, propagation);
    if IsDefined(event) {
      TandemNative.Log(s"stealth: a puppet's \(EnumValueToString("gamedataStimType", Cast<Int64>(kind))): radius \(event.radius) m, reaction stim \(IsDefined(event.stimRecord) && event.stimRecord.IsReactionStim()), tags Stealth \(event.IsTagInStimuli(n"Stealth")), PlayerOnly \(event.IsTagInStimuli(n"PlayerOnly")), IgnoreFriendly \(event.IsTagInStimuli(n"IgnoreFriendly"))");
    }
  }

  // Guest: a stimulus our V gave out, to the host (each type at most ten
  // times a second).
  public func OwnNoise(stim: gamedataStimType, radius: Float, propagation: Bool) -> Void {
    let kind: Int32 = EnumInt(stim);
    if kind < 0 || kind >= ArraySize(this.m_noiseAt) {
      return;
    }
    let now = EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
    if this.m_noiseAt[kind] >= 0.0 && now - this.m_noiseAt[kind] < 0.1 {
      return;
    }
    this.m_noiseAt[kind] = now;
    TandemNative.SendNoise(kind, radius, propagation);
  }

  // Host: the guests' noises, given out again from their puppets, so our
  // NPCs hear them where the guest is.
  private func HearGuests() -> Void {
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    for noise in TandemNative.TakeNoises() {
      let puppet = IsDefined(system) ? system.PuppetOf(noise.player) : null;
      if IsDefined(puppet) {
        let stim = IntEnum<gamedataStimType>(noise.stim);
        let data: stimInvestigateData;
        this.Describe(puppet, noise.stim, noise.radius, noise.propagation);
        let broadcaster = puppet.GetStimBroadcasterComponent();
        if IsDefined(broadcaster) {
          broadcaster.TriggerSingleBroadcast(puppet, stim, noise.radius, data, noise.propagation);
        } else {
          let targets: array<NPCstubData>;
          let event = StimBroadcasterComponentHelper.CreateStimEvent(puppet, stim, noise.radius, data, noise.propagation);
          StimBroadcasterComponentHelper.ProcessSingleStimuliBroadcast(puppet, event, false, false, false, targets);
        }
        this.m_noises += 1;
        if this.m_now - this.m_noiseLog >= 1.0 {
          this.m_noiseLog = this.m_now;
          TandemNative.Log(s"stealth: player \(noise.player) makes noise: stim \(noise.stim) (\(EnumValueToString("gamedataStimType", Cast<Int64>(noise.stim)))), radius \(noise.radius) (\(this.m_noises) since last)");
          this.m_noises = 0;
        }
      }
    }
  }
}
