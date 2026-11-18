// How the host's NPCs animate, on the guests' proxies (docs/ARCHITECTURE.md,
// NPCs; engine-knowledge §13.14). The host samples, four times a second, what
// each shared NPC's animation graph reads: its high-level, upper-body and
// stance states (NPCStatesComponent), the weapon in its right hand, whom it
// looks or aims at, and the workspot it plays in (a bench, a wall to lean
// on, a cigarette). It sends only what changed (NpcAnims, reliable), and
// each shot of its weapon (NpcShot). A guest puts its brain-off proxy in the
// same states the way NPCStatesComponent does it (anim features, wrappers,
// blackboard, combat mode), gives it the weapon, a look-at, and plays the
// same workspot on it, so the proxy's own graph plays the animations with
// their sounds. Shots play the weapon's own effects (flash, report, shells).
//
// Owned by TandemNpcs (Npcs.reds), which calls Sample/Forget on the host
// and Follow/Drop on a guest.

// Host: one shot of an NPC's weapon (every NPC shot goes through
// AIWeapon.Fire: AI behaviours and devices alike).
@wrapMethod(AIWeapon)
public final static func Fire(
  weaponOwner: wref<GameObject>,
  weapon: wref<WeaponObject>,
  const timeStamp: Float,
  tbhCoefficient: Float,
  requestedTriggerMode: gamedataTriggerMode,
  opt targetPosition: Vector4,
  opt target: ref<GameObject>,
  opt rangedAttack: TweakDBID,
  opt maxSpreadOverride: Float,
  opt aimingDelay: Float,
  opt offset: Vector4,
  opt shouldTrackTarget: Bool,
  opt predictionTime: Float,
  opt posProviderOverride: ref<IPositionProvider>,
  opt muzzleOffset: Vector4,
  opt weaponCustomEvent: CName
) {
  wrappedMethod(weaponOwner, weapon, timeStamp, tbhCoefficient, requestedTriggerMode, targetPosition, target, rangedAttack, maxSpreadOverride, aimingDelay, offset, shouldTrackTarget, predictionTime, posProviderOverride, muzzleOffset, weaponCustomEvent);
  if TandemNative.IsStarted() && TandemNative.IsHost() && IsDefined(weaponOwner) && IsDefined(weapon) && weapon.IsAttached() {
    let at = targetPosition;
    if Vector4.IsZero(at) && IsDefined(target) {
      at = target.GetWorldPosition();
    }
    TandemNpcAnims.Shot(weaponOwner, weapon.IsSilenced() ? 17 : 16, at);
  }
}

// A guest: the host's states on a proxy, as NPCStatesComponent applies its
// own (ChangeHighLevelState and siblings, final.reds:168077-168123), minus
// what belongs to the NPC's brain or to the local V (senses presets, voice,
// squad and security calls, the player's high-level event): the proxy's
// brain is off and these states are the host's NPC's, not ours.
@addMethod(NPCStatesComponent)
public final func TandemFollow(high: gamedataNPCHighLevelState, upper: gamedataNPCUpperBodyState, stance: gamedataNPCStanceState) -> Void {
  let owner = this.GetOwner();
  let previous = this.GetCurrentHighLevelState();
  if NotEquals(previous, high) && this.SetCurrentHighLevelState(high) {
    this.m_previousHighLevelState = previous;
    let feature = new AnimFeature_NPCState();
    feature.state = EnumInt(high);
    AnimationControllerComponent.ApplyFeature(owner, this.m_highLevelAnimFeatureName, feature);
    AnimationControllerComponent.SetAnimWrapperWeightOnOwnerAndItems(owner, NPCStatesComponent.GetAnimWrapperNameBasedOnHighLevelState(previous), 0.0);
    AnimationControllerComponent.SetAnimWrapperWeightOnOwnerAndItems(owner, NPCStatesComponent.GetAnimWrapperNameBasedOnHighLevelState(high), 1.0);
    let board = this.GetPuppetStateBlackboard();
    if IsDefined(board) {
      board.SetInt(GetAllBlackboardDefs().PuppetState.HighLevel, EnumInt(high));
      board.FireCallbacks();
    }
    this.HandleCombatStateAnimHint(high, previous);
  }
  if NotEquals(this.GetCurrentUpperBodyState(), upper) && this.SetCurrentUpperBodyState(upper) {
    this.UpdateUpperBodyState();
  }
  let stancePrevious = this.GetCurrentStanceState();
  if NotEquals(stancePrevious, stance) && this.SetCurrentStanceState(stance) {
    this.SetPreviousStanceState(stancePrevious);
    this.UpdateStanceState();
  }
}

// What the host last sent for one NPC.
public class TandemNpcAnimSent {
  public let npc: Uint32;
  public let anim: TandemNpcAnim;
}

// A guest's view of one proxy: the host's latest state, and what of it is
// applied.
public class TandemNpcAnimProxy {
  public let npc: Uint32;
  public let anim: TandemNpcAnim;
  public let dirty: Bool;
  public let applied: Bool;
  public let shown: TandemNpcAnim;
  public let look: ref<LookAtAddEvent>;
  public let lookAt: Uint32;
  // Workspot: 0 out, 1 entering (since `since`), 2 in, 3 leaving.
  public let spot: Int32;
  public let spotPath: Uint64;
  public let spotEntry: Int32;
  public let since: Float;
}

public class TandemNpcAnims {
  private let m_game: GameInstance;
  private let m_npcs: wref<TandemNpcs>;
  private let m_last: Float;
  private let m_sent: array<ref<TandemNpcAnimSent>>;
  private let m_proxies: array<ref<TandemNpcAnimProxy>>;
  private let m_shots: Int32;
  private let m_applied: Int32;
  private let m_you: Uint32;

  public static func Interval() -> Float {
    return 0.25;
  }

  // The component a proxy plays the host's workspots from.
  public static func SpotName() -> CName {
    return n"TandemWorkspot";
  }

  public func Start(game: GameInstance, npcs: ref<TandemNpcs>) -> Void {
    this.m_game = game;
    this.m_npcs = npcs;
    // Every proxy gets a workspot component of its own while it is built
    // (Codeware: components can be added until the entity initializes).
    GameInstance.GetCallbackSystem().RegisterCallback(n"Entity/Assemble", this, n"OnAssemble")
      .AddTarget(DynamicEntityTarget.Tag(n"Tandem.Proxy"));
  }

  private cb func OnAssemble(event: ref<EntityLifecycleEvent>) {
    let entity = event.GetEntity();
    if !IsDefined(entity) {
      return;
    }
    let spot = new WorkspotResourceComponent();
    spot.name = TandemNpcAnims.SpotName();
    entity.AddComponent(spot);
  }

  public func Clear() -> Void {
    ArrayClear(this.m_sent);
    ArrayClear(this.m_proxies);
  }

  public func Report() -> String {
    return s"anims: \(ArraySize(this.m_sent)) sent, \(ArraySize(this.m_proxies)) followed, \(this.m_applied) applied, \(this.m_shots) shots";
  }

  // ---- host ----

  // Whether this tick samples (Npcs.Share runs at 10 Hz, states at 4).
  public func Due(now: Float) -> Bool {
    if now - this.m_last >= TandemNpcAnims.Interval() {
      this.m_last = now;
      return true;
    }
    return false;
  }

  // One shared NPC: its state now, sent when it changed.
  public func Sample(npc: ref<ScriptedPuppet>, id: Uint32, you: Uint32) -> Void {
    let anim = this.StateOf(npc, id, you);
    let sent = this.FindSent(id);
    if !IsDefined(sent) {
      sent = new TandemNpcAnimSent();
      sent.npc = id;
      ArrayPush(this.m_sent, sent);
    } else if TandemNpcAnims.Same(sent.anim, anim) {
      return;
    }
    sent.anim = anim;
    TandemNative.SendNpcAnim(id, anim.highLevel, anim.upperBody, anim.stance, anim.flags, anim.weapon, anim.target, anim.workspot, anim.workspotEntry, anim.workspotAnim);
  }

  public func Forget(id: Uint32) -> Void {
    let kept: array<ref<TandemNpcAnimSent>>;
    for sent in this.m_sent {
      if sent.npc != id {
        ArrayPush(kept, sent);
      }
    }
    this.m_sent = kept;
  }

  private func StateOf(npc: ref<ScriptedPuppet>, id: Uint32, you: Uint32) -> TandemNpcAnim {
    let anim: TandemNpcAnim;
    anim.npc = id;
    anim.highLevel = EnumInt(npc.GetHighLevelStateFromBlackboard());
    anim.upperBody = EnumInt(npc.GetUpperBodyStateFromBlackboard());
    anim.stance = EnumInt(npc.GetStanceStateFromBlackboard());
    anim.weapon = TandemWeapons.Held(npc, t"AttachmentSlots.WeaponRight");
    if TDBID.IsValid(anim.weapon) {
      anim.flags += 1;
    }
    // Whom it is after: only once it is no longer relaxed (a relaxed NPC's
    // looks are its own small talk, not worth a message).
    if NotEquals(npc.GetHighLevelStateFromBlackboard(), gamedataNPCHighLevelState.Relaxed) {
      let tracker = npc.GetTargetTrackerComponent();
      let threat: TrackedLocation;
      if IsDefined(tracker) && tracker.GetTopHostileThreat(false, threat) {
        anim.target = this.TargetOf(threat.entity as GameObject, you);
      }
    }
    let workspots = GameInstance.GetWorkspotSystem(this.m_game);
    if workspots.IsActorInWorkspot(npc) {
      let instance = workspots.GetWorkspotInstance(npc.GetEntityID());
      if IsDefined(instance) && IsDefined(instance.resource) {
        let path = instance.resource.path;
        let reference = path.resource;
        anim.workspot = ResourceAsyncRef.GetHash(reference);
        anim.workspotEntry = Cast<Int32>(instance.entryId.id);
        anim.workspotAnim = IsNameValid(instance.idleAnim) ? instance.idleAnim : instance.animName;
        if anim.workspot != 0ul {
          anim.flags += 2;
        }
      }
    }
    return anim;
  }

  // A player id (us or a puppet), 0x10000 + a shared NPC's id, or 0.
  private func TargetOf(target: ref<GameObject>, you: Uint32) -> Uint32 {
    if !IsDefined(target) {
      return 0u;
    }
    if target.IsPlayer() {
      return you;
    }
    let system = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
    let player = IsDefined(system) ? system.PlayerOfPuppet(target.GetEntityID()) : 0u;
    if player != 0u {
      return player;
    }
    let npc = IsDefined(this.m_npcs) ? this.m_npcs.NpcOf(target.GetEntityID()) : 0u;
    return npc != 0u ? TandemNative.NpcPuppetId(npc) : 0u;
  }

  // Host: a shot of one of our shared NPCs (AIWeapon.Fire).
  public static func Shot(owner: ref<GameObject>, effect: Int32, at: Vector4) -> Void {
    let system = GameInstance.GetScriptableSystemsContainer(owner.GetGame()).Get(n"TandemSystem") as TandemSystem;
    let npc = IsDefined(system) ? system.SharedNpc(owner.GetEntityID()) : 0u;
    if npc != 0u {
      TandemNative.SendNpcShot(npc, effect, at);
    }
  }

  private func FindSent(id: Uint32) -> ref<TandemNpcAnimSent> {
    for sent in this.m_sent {
      if sent.npc == id {
        return sent;
      }
    }
    return null;
  }

  public static func Same(a: TandemNpcAnim, b: TandemNpcAnim) -> Bool {
    return a.highLevel == b.highLevel && a.upperBody == b.upperBody && a.stance == b.stance && a.flags == b.flags && Equals(a.weapon, b.weapon) && a.target == b.target && a.workspot == b.workspot && a.workspotEntry == b.workspotEntry;
  }

  // ---- guest ----

  // Each frame: the host's new states and shots, onto the proxies that are
  // ready (brain off, controller on); the others keep theirs for later.
  public func Follow(now: Float, you: Uint32) -> Void {
    this.m_you = you;
    for anim in TandemNative.TakeNpcAnims() {
      let proxy = this.FindProxy(anim.npc);
      if !IsDefined(proxy) {
        proxy = new TandemNpcAnimProxy();
        proxy.npc = anim.npc;
        proxy.shown.highLevel = 5;
        proxy.shown.upperBody = 6;
        proxy.shown.stance = 3;
        ArrayPush(this.m_proxies, proxy);
      }
      proxy.anim = anim;
      proxy.dirty = true;
    }
    for shot in TandemNative.TakeNpcShots() {
      let body = this.Body(shot.npc);
      if IsDefined(body) {
        this.Arm(body, this.FindProxy(shot.npc));
        TandemWeapons.Fire(body, false, shot.effect);
        this.m_shots += 1;
      }
    }
    for proxy in this.m_proxies {
      let body = this.Body(proxy.npc);
      if IsDefined(body) {
        if proxy.dirty {
          proxy.dirty = false;
          this.Apply(body, proxy);
        }
        this.Workspot(body, proxy, now);
      }
    }
  }

  // The proxy is gone or dead: nothing more to follow.
  public func Drop(npc: Uint32) -> Void {
    let kept: array<ref<TandemNpcAnimProxy>>;
    for proxy in this.m_proxies {
      if proxy.npc != npc {
        ArrayPush(kept, proxy);
      }
    }
    this.m_proxies = kept;
  }

  private func Body(npc: Uint32) -> ref<ScriptedPuppet> {
    return IsDefined(this.m_npcs) ? this.m_npcs.ReadyProxy(npc) : null;
  }

  private func Apply(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>) -> Void {
    let anim = proxy.anim;
    let before = proxy.shown;
    let states = body.GetStatesComponent();
    if IsDefined(states) {
      states.TandemFollow(IntEnum<gamedataNPCHighLevelState>(anim.highLevel), IntEnum<gamedataNPCUpperBodyState>(anim.upperBody), IntEnum<gamedataNPCStanceState>(anim.stance));
    }
    // The weapon first: an aim or a shot needs it in the hand.
    this.Arm(body, proxy);
    // Reloads: the weapon's reload sounds (the graph plays the hands).
    if anim.upperBody == 8 && before.upperBody != 8 {
      TandemWeapons.Fire(body, false, 5);
    } else if anim.upperBody != 8 && before.upperBody == 8 {
      TandemWeapons.Fire(body, false, 11);
    }
    if anim.target != proxy.lookAt {
      this.LookAt(body, proxy, anim.target);
    }
    proxy.shown = anim;
    this.m_applied += 1;
    if !proxy.applied {
      proxy.applied = true;
      TandemNative.Log(s"npc anims: proxy \(proxy.npc) follows (high \(anim.highLevel), upper \(anim.upperBody), stance \(anim.stance), weapon \(TDBID.ToStringDEBUG(anim.weapon)), workspot \(anim.workspot))");
    }
  }

  // The host's right-hand item on the proxy (none: put away).
  private func Arm(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>) -> Void {
    if !IsDefined(proxy) {
      return;
    }
    let want = proxy.anim.weapon;
    let have = TandemWeapons.Held(body, t"AttachmentSlots.WeaponRight");
    if NotEquals(want, have) {
      TandemWeapons.Hold(body, t"AttachmentSlots.WeaponRight", want);
    }
  }

  // Head and chest towards whom the host's NPC is after, as the game's own
  // reactions turn an NPC to look (ReactionComponent, final.reds:147434).
  private func LookAt(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>, target: Uint32) -> Void {
    if IsDefined(proxy.look) {
      LookAtRemoveEvent.QueueRemoveLookatEvent(body, proxy.look);
      proxy.look = null;
    }
    proxy.lookAt = target;
    let entity = this.TargetEntity(target);
    if !IsDefined(entity) {
      return;
    }
    let look = new LookAtAddEvent();
    look.SetEntityTarget(entity, n"pla_default_tgt", Vector4.EmptyVector());
    look.SetStyle(animLookAtStyle.Normal);
    look.request.limits.softLimitDegrees = 360.0;
    look.request.limits.hardLimitDegrees = 270.0;
    look.request.limits.backLimitDegrees = 210.0;
    look.request.limits.hardLimitDistance = 100.0;
    look.request.calculatePositionInParentSpace = true;
    look.bodyPart = n"Eyes";
    let parts: array<LookAtPartRequest>;
    let part: LookAtPartRequest;
    part.partName = n"Head";
    part.weight = 0.1;
    part.suppress = 1.0;
    part.mode = 0;
    ArrayPush(parts, part);
    part.partName = n"Chest";
    part.weight = 2.0;
    part.suppress = 0.0;
    part.mode = 0;
    ArrayPush(parts, part);
    look.SetAdditionalPartsArray(parts);
    body.QueueEvent(look);
    proxy.look = look;
  }

  private func TargetEntity(target: Uint32) -> ref<GameObject> {
    if target == 0u {
      return null;
    }
    if target >= TandemNative.NpcPuppetId(0u) {
      return this.Body(target - TandemNative.NpcPuppetId(0u));
    }
    if target == this.m_you {
      return GetPlayer(this.m_game);
    }
    return TandemNpcs.PuppetOf(this.m_game, target);
  }

  // The host's workspot on the proxy: the same workspot resource, played
  // from the proxy's own workspot component placed where the proxy stands
  // (which is where the host's NPC sits), then the same entry of its tree.
  // While it plays, tandem's controller keeps still (the workspot places
  // the body); when the host's NPC gets up, the proxy plays the exit.
  private func Workspot(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>, now: Float) -> Void {
    let workspots = GameInstance.GetWorkspotSystem(this.m_game);
    let puppet = TandemNative.NpcPuppetId(proxy.npc);
    let want = (proxy.anim.flags & 2) != 0 ? proxy.anim.workspot : 0ul;
    switch proxy.spot {
      case 0:
        if want != 0ul {
          this.Enter(body, proxy, want, now);
        }
        break;
      case 1:
        if workspots.IsActorInWorkspot(body) {
          proxy.spot = 2;
          workspots.SendJumpCommandEnt(body, proxy.anim.workspotEntry, true);
          proxy.spotEntry = proxy.anim.workspotEntry;
          TandemNative.Log(s"npc anims: proxy \(proxy.npc) in workspot \(want), entry \(proxy.spotEntry)");
        } else if now - proxy.since > 2.0 {
          // Never started: leave it standing, moved by the controller.
          proxy.spot = 0;
          proxy.spotPath = want;
          TandemNative.HoldPuppet(puppet, false);
          TandemNative.Log(s"npc anims: proxy \(proxy.npc) could not enter workspot \(want)");
        }
        break;
      case 2:
        if want != proxy.spotPath {
          proxy.spot = 3;
          proxy.since = now;
          workspots.SendSlowExitSignal(body);
        } else if proxy.anim.workspotEntry != proxy.spotEntry {
          proxy.spotEntry = proxy.anim.workspotEntry;
          workspots.SendJumpCommandEnt(body, proxy.spotEntry, false);
        }
        break;
      case 3:
        if !workspots.IsActorInWorkspot(body) || now - proxy.since > 4.0 {
          if workspots.IsActorInWorkspot(body) {
            workspots.StopNpcInWorkspot(body);
          }
          proxy.spot = 0;
          proxy.spotPath = 0ul;
          TandemNative.HoldPuppet(puppet, false);
        }
        break;
    }
  }

  private func Enter(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>, path: Uint64, now: Float) -> Void {
    if path == proxy.spotPath {
      return; // that one failed: not again until it changes
    }
    let spot = this.SpotOf(body);
    if !IsDefined(spot) {
      TandemNative.Log(s"npc anims: proxy \(proxy.npc) has no workspot component");
      proxy.spotPath = path;
      return;
    }
    if !TandemNative.SetResourcePath(spot, n"workspotResource", path) {
      proxy.spotPath = path;
      return;
    }
    spot.SetLocalTransform(body.GetWorldPosition(), body.GetWorldOrientation());
    TandemNative.HoldPuppet(TandemNative.NpcPuppetId(proxy.npc), true);
    GameInstance.GetWorkspotSystem(this.m_game).PlayNpcInWorkspot(body, body, body, TandemNpcAnims.SpotName(), n"None", 0.3);
    proxy.spot = 1;
    proxy.spotPath = path;
    proxy.since = now;
  }

  private func SpotOf(body: ref<ScriptedPuppet>) -> ref<WorkspotResourceComponent> {
    for component in body.GetComponents() {
      let spot = component as WorkspotResourceComponent;
      if IsDefined(spot) && Equals(spot.GetName(), TandemNpcAnims.SpotName()) {
        return spot;
      }
    }
    return null;
  }

  private func FindProxy(npc: Uint32) -> ref<TandemNpcAnimProxy> {
    for proxy in this.m_proxies {
      if proxy.npc == npc {
        return proxy;
      }
    }
    return null;
  }
}
