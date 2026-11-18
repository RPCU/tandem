// Each player is detected on their own (docs/ARCHITECTURE.md). Only the
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
// - Guest -> host: the stimuli its V gives out (gunshots...) are broadcast
//   again from its puppet on the host (Noise).

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
  wrappedMethod(contextOwner, gdStimType, radius, investigateData, propagationChange);
  let owner = this.GetOwner();
  if IsDefined(owner) && owner.IsPlayer() && TandemNative.IsStarted() && !TandemNative.IsHost() {
    let stealth = TandemStealth.Of(owner.GetGame());
    if IsDefined(stealth) {
      stealth.OwnNoise(gdStimType, radius, propagationChange);
    }
  }
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
  private let m_lastSample: Float;
  // Each remote player's stealth on their puppet (index: player id).
  private let m_appliedChanges: array<Int32>;
  private let m_appliedOn: array<EntityID>;
  private let m_appliedFlags: array<Int32>;
  private let m_appliedCamo: array<TweakDBID>;
  // Guest: when each stimulus type was last sent (at most 10 a second each).
  private let m_noiseAt: array<Float>;
  // Host: noises heard since the last log line.
  private let m_noises: Int32;
  private let m_noiseLog: Float;

  public static func Of(game: GameInstance) -> ref<TandemStealth> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Stealth() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    let i = 0;
    while i <= 8 {
      ArrayPush(this.m_appliedChanges, 0);
      let none: EntityID;
      ArrayPush(this.m_appliedOn, none);
      ArrayPush(this.m_appliedFlags, 0);
      ArrayPush(this.m_appliedCamo, TDBID.None());
      i += 1;
    }
    i = 0;
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
    if TandemNative.IsHost() {
      this.HearGuests();
    } else {
      this.FollowHost(player, npcs);
    }
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
    let i = 0;
    while i <= 8 {
      this.m_appliedChanges[i] = 0;
      let none: EntityID;
      this.m_appliedOn[i] = none;
      this.m_appliedFlags[i] = 0;
      this.m_appliedCamo[i] = TDBID.None();
      i += 1;
    }
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
    let p = 1u;
    while p <= 8u {
      let i = Cast<Int32>(p);
      let changes = p == this.m_you ? 0 : TandemNative.RemoteStealthChanges(p);
      let puppet = changes > 0 ? system.PuppetOf(p) : null;
      if IsDefined(puppet) && (changes != this.m_appliedChanges[i] || puppet.GetEntityID() != this.m_appliedOn[i]) {
        this.m_appliedChanges[i] = changes;
        this.m_appliedOn[i] = puppet.GetEntityID();
        this.Apply(p, puppet, player);
      }
      p += 1u;
    }
  }

  // How the player's V is seen, on their puppet: what vanilla does to its
  // own V's visible object (PlayerPuppet.SetInvisible,
  // UpdateVisibilityModifier, UpdateSecondaryVisibilityOffset), and the
  // camo status effect itself for its look.
  private func Apply(p: Uint32, puppet: ref<GameObject>, player: ref<PlayerPuppet>) -> Void {
    let i = Cast<Int32>(p);
    let flags = TandemNative.RemoteStealthFlags(p);
    let visibility = TandemNative.RemoteStealthVisibility(p);
    let camo = TandemNative.RemoteStealthCamo(p);
    let camoOn = (flags & 1) != 0;
    let wasOn = (this.m_appliedFlags[i] & 1) != 0;
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
    let applied = this.m_appliedCamo[i];
    if TDBID.IsValid(applied) && (!camoOn || applied != camo) {
      StatusEffectHelper.RemoveStatusEffect(puppet, applied);
      this.m_appliedCamo[i] = TDBID.None();
    }
    if camoOn && TDBID.IsValid(camo) && this.m_appliedCamo[i] != camo {
      StatusEffectHelper.ApplyStatusEffect(puppet, camo, puppet.GetEntityID());
      this.m_appliedCamo[i] = camo;
    }
    // Fully blocking (the relic): the NPCs hunting them lose them, as
    // vanilla drops V from its hostile NPCs' trackers.
    if (flags & 2) != 0 && (this.m_appliedFlags[i] & 2) == 0 && TandemNative.IsHost() {
      let dropped = 0;
      for npc in player.GetNPCsAroundObject(TandemNpcs.Range()) {
        let tracker = npc.GetTargetTrackerComponent();
        let threat: TrackedLocation;
        if IsDefined(tracker) && tracker.ThreatFromEntity(puppet, threat) {
          tracker.DeactivateThreat(puppet);
          dropped += 1;
        }
      }
      TandemNative.Log(s"stealth: player \(p)'s camo blocks: \(dropped) NPC(s) lost them");
    }
    if NotEquals(camoOn, wasOn) {
      TandemNative.Log(s"stealth: player \(p) camo \(camoOn ? "on" : "off") (\(TDBID.ToStringDEBUG(camo)))");
    }
    TandemNative.Log(s"stealth: player \(p): visibility \(visibility), crouch \((flags & 4) != 0)");
    this.m_appliedFlags[i] = flags;
  }

  // ---- noises ----

  // Guest: a stimulus our V gave out, to the host (each type at most ten
  // times a second).
  public func OwnNoise(stim: gamedataStimType, radius: Float, propagation: Bool) -> Void {
    let kind = Cast<Int32>(EnumInt(stim));
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
