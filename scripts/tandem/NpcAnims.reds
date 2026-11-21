// How the host's NPCs animate, on the guests' proxies (docs/architecture/npcs.md,
// NPCs; engine-knowledge §13.21). The host samples, four times a second, what
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
  // At a guest's puppet: that guest's game fires it and decides the hit
  // (Shots.reds). The cosmetic shot below then skips that guest (its proxy
  // fires for real): the endpoint leaves it out.
  if IsDefined(weaponOwner) && IsDefined(target) && TandemNative.IsStarted() && TandemOwn.Entity(weaponOwner.GetGame(), weaponOwner.GetEntityID()) {
    let shots = TandemShots.Of(weaponOwner.GetGame());
    if IsDefined(shots) {
      shots.Announce(weaponOwner, weapon, target, tbhCoefficient, requestedTriggerMode, rangedAttack);
    }
  }
  // A placed turret's shot (Turrets.reds). Host: queued for the guests, who
  // draw it on their copy. Guest, linked with the host: our copy fires only
  // the shots the host's turret fired (the passing call), never its own.
  let turret = weaponOwner as SecurityTurret;
  // A copy in another player's hands fires nothing of its own.
  if IsDefined(turret) && turret.TandemInHands() && !TandemStreaming.IsPassing(turret.GetEntityID()) {
    return;
  }
  if IsDefined(turret) && TandemNative.IsStarted() && TandemTurrets.Follows(turret) {
    if TandemNative.IsHost() {
      let at = targetPosition;
      if IsDefined(target) {
        at = target.GetWorldPosition();
      }
      TandemStreaming.QueuePoint(TandemPointKind.DeviceShot(), turret.GetEntityID(), IsDefined(target) ? target.GetEntityID() : EntityID.FromHash(0ul), n"None", at, 0);
    } else if !turret.m_tandemOwnFire && TandemTurrets.HostFires(turret) && !TandemStreaming.IsPassing(turret.GetEntityID()) {
      return;
    }
  }
  wrappedMethod(weaponOwner, weapon, timeStamp, tbhCoefficient, requestedTriggerMode, targetPosition, target, rangedAttack, maxSpreadOverride, aimingDelay, offset, shouldTrackTarget, predictionTime, posProviderOverride, muzzleOffset, weaponCustomEvent);
  if TandemNative.IsStarted() && IsDefined(weaponOwner) && IsDefined(weapon) && weapon.IsAttached() && TandemOwn.Entity(weaponOwner.GetGame(), weaponOwner.GetEntityID()) {
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
  // The host's workspot, played on the proxy.
  public let spot: ref<TandemSpot>;
}

// A workspot played on a body tandem moves (an NPC proxy, a player's
// puppet: Activities.reds): the same workspot resource, played from the
// body's own workspot component placed where the body stands, then the same
// entry of its tree. While it plays, tandem's controller keeps still (the
// workspot places the body); when the one it follows gets up, the body
// plays the exit. `hold`: the puppet id this holds while it plays (0: the
// caller holds it).
public class TandemSpot {
  // 0 out, 1 entering (since `since`), 2 in, 3 leaving.
  public let state: Int32;
  public let path: Uint64;
  public let entry: Int32;
  public let since: Float;

  public func Playing() -> Bool {
    return this.state == 1 || this.state == 2;
  }

  public func Step(game: GameInstance, body: ref<ScriptedPuppet>, hold: Uint32, want: Uint64, entry: Int32, now: Float, label: String) -> Void {
    let workspots = GameInstance.GetWorkspotSystem(game);
    switch this.state {
      case 0:
        if want != 0ul {
          this.Enter(game, body, hold, want, now, label);
        }
        break;
      case 1:
        if workspots.IsActorInWorkspot(body) {
          this.state = 2;
          workspots.SendJumpCommandEnt(body, entry, true);
          this.entry = entry;
          TandemNative.Log(s"\(label) in workspot \(want), entry \(this.entry)");
        } else if now - this.since > 2.0 {
          // Never started: leave it standing, moved by the controller.
          this.state = 0;
          this.path = want;
          if hold != 0u {
            TandemNative.HoldPuppet(hold, false);
          }
          TandemNative.Log(s"\(label) could not enter workspot \(want)");
        }
        break;
      case 2:
        if want != this.path {
          this.state = 3;
          this.since = now;
          workspots.SendSlowExitSignal(body);
        } else if entry != this.entry {
          this.entry = entry;
          workspots.SendJumpCommandEnt(body, this.entry, false);
        }
        break;
      case 3:
        if !workspots.IsActorInWorkspot(body) || now - this.since > 4.0 {
          if workspots.IsActorInWorkspot(body) {
            workspots.StopNpcInWorkspot(body);
          }
          this.state = 0;
          this.path = 0ul;
          if hold != 0u {
            TandemNative.HoldPuppet(hold, false);
          }
        }
        break;
    }
  }

  // Out at once (the one it follows is gone).
  public func Stop(game: GameInstance, body: ref<ScriptedPuppet>) -> Void {
    if IsDefined(body) && GameInstance.GetWorkspotSystem(game).IsActorInWorkspot(body) {
      GameInstance.GetWorkspotSystem(game).StopNpcInWorkspot(body);
    }
    this.state = 0;
    this.path = 0ul;
  }

  private func Enter(game: GameInstance, body: ref<ScriptedPuppet>, hold: Uint32, path: Uint64, now: Float, label: String) -> Void {
    if path == this.path {
      return; // that one failed: not again until it changes
    }
    let spot = TandemSpot.ComponentOf(body);
    if !IsDefined(spot) {
      TandemNative.Log(s"\(label) has no workspot component");
      this.path = path;
      return;
    }
    if !TandemNative.SetResourcePath(spot, n"workspotResource", path) {
      this.path = path;
      return;
    }
    spot.SetLocalTransform(body.GetWorldPosition(), body.GetWorldOrientation());
    if hold != 0u {
      TandemNative.HoldPuppet(hold, true);
    }
    GameInstance.GetWorkspotSystem(game).PlayNpcInWorkspot(body, body, body, TandemNpcAnims.SpotName(), n"None", 0.3);
    this.state = 1;
    this.path = path;
    this.since = now;
  }

  public static func ComponentOf(body: ref<ScriptedPuppet>) -> ref<WorkspotResourceComponent> {
    for component in body.GetComponents() {
      let spot = component as WorkspotResourceComponent;
      if IsDefined(spot) && Equals(spot.GetName(), TandemNpcAnims.SpotName()) {
        return spot;
      }
    }
    return null;
  }
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

  // Guest: the host's NPC shots whose bullet was drawn.
  private let m_bullets: Int32;

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
    // Every proxy and player puppet gets a workspot component of its own
    // while it is built (Codeware: components can be added until the entity
    // initializes).
    GameInstance.GetCallbackSystem().RegisterCallback(n"Entity/Assemble", this, n"OnAssemble")
      .AddTarget(DynamicEntityTarget.Tag(n"Tandem.Proxy"))
      .AddTarget(DynamicEntityTarget.Tag(n"Tandem.Puppet"));
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
    let npc = IsDefined(system) ? system.SharedNpcOf(owner.GetEntityID()) : 0u;
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

  // (test) Host: someone sitting, leaning or smoking to share. The nearest
  // NPC playing a workspot within `range` m of V; when it is beyond the
  // sharing range, V is moved to 40 m of it, on V's side (where V stood is
  // street, where the NPC sits may not be). False: nobody in a workspot.
  public func Seek(range: Float) -> Bool {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return false;
    }
    let workspots = GameInstance.GetWorkspotSystem(this.m_game);
    let from = player.GetWorldPosition();
    let best: ref<NPCPuppet>;
    let bestDistance = range + 1.0;
    let seen = 0;
    for npc in player.GetNPCsAroundObject(range) {
      if !npc.IsDead() && workspots.IsActorInWorkspot(npc) {
        seen += 1;
        let distance = Vector4.Distance(npc.GetWorldPosition(), from);
        if distance < bestDistance {
          bestDistance = distance;
          best = npc;
        }
      }
    }
    if !IsDefined(best) {
      TandemNative.Log(s"npc seek: nobody in a workspot within \(range) m");
      return false;
    }
    let at = best.GetWorldPosition();
    let moved = bestDistance > 50.0;
    if moved {
      let keep = 40.0 / bestDistance;
      let to = new Vector4(at.X + (from.X - at.X) * keep, at.Y + (from.Y - at.Y) * keep, at.Z + (from.Z - at.Z) * keep, 1.0);
      TandemTravel.ExpectMove(this.m_game);
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, TandemYawAngles(player.GetWorldYaw()));
    }
    TandemNative.Log(s"npc seek: \(seen) in a workspot, the nearest \(TDBID.ToStringDEBUG(best.GetRecordID())) \(bestDistance) m away\(moved ? ", V moved to 40 m of it" : "")");
    return true;
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
        // Where the host's shot went: its bullet, drawn (tracer, impact,
        // decal), deciding nothing here (Presence.reds).
        let presence = TandemPresenceSync.Of(body.GetGame());
        let weapon = TandemPresenceSync.WeaponOf(body, false);
        if IsDefined(presence) && IsDefined(weapon) && weapon.IsAttached() && (shot.effect == 15 || shot.effect == 16 || shot.effect == 17) {
          if presence.Bullet(body, weapon, new Vector4(shot.x, shot.y, shot.z, 1.0), now) {
            this.m_bullets += 1;
            if this.m_bullets == 1 || this.m_bullets % 50 == 0 {
              TandemNative.Log(s"bullet: npc \(shot.npc)'s shot \(this.m_bullets) drawn from its proxy's weapon to (\(shot.x) \(shot.y) \(shot.z))");
            }
          }
        }
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
      TandemNative.LogDebug(s"npc anims: proxy \(proxy.npc) follows (high \(anim.highLevel), upper \(anim.upperBody), stance \(anim.stance), weapon \(TDBID.ToStringDEBUG(anim.weapon)), workspot \(anim.workspot))");
    }
  }

  // The host's right-hand item on the proxy (none: put away).
  private func Arm(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>) -> Void {
    if !IsDefined(proxy) {
      return;
    }
    // The host's scene put something in its hands (Scenes.reds): left there.
    if TandemStory.SceneHolds(proxy.npc, 0u) {
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

  // The host's workspot on the proxy (TandemSpot).
  private func Workspot(body: ref<ScriptedPuppet>, proxy: ref<TandemNpcAnimProxy>, now: Float) -> Void {
    if !IsDefined(proxy.spot) {
      proxy.spot = new TandemSpot();
    }
    let want = (proxy.anim.flags & 2) != 0 ? proxy.anim.workspot : 0ul;
    proxy.spot.Step(this.m_game, body, TandemNative.NpcPuppetId(proxy.npc), want, proxy.anim.workspotEntry, now, s"npc anims: proxy \(proxy.npc)");
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
