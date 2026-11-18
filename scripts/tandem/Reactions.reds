// NPC reactions that single player gives "the player" (docs/research/
// coop-perks.md §2.1). On the host that is its own V even when a guest's V
// is the one involved: an NPC entering combat with a guest offered the
// host's V the second-chance slow-mo and the gag, a guard broken by a guest
// offered the host's V a finisher, a guest's Bait/Whistle walked the NPC to
// the host's V. Here, on the host, "the player" is whoever acted (the
// guest's puppet) for the NPC's half, and the player's half goes to that
// player's game as an NpcCue, which runs it on its own V with its proxy.

public abstract class TandemReactions {
  // The remote player whose puppet this is (0: none, or not the host).
  public static func GuestOf(game: GameInstance, object: wref<GameObject>) -> Uint32 {
    if !TandemNative.IsHost() || !IsDefined(object) {
      return 0u;
    }
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.PlayerOfPuppet(object.GetEntityID()) : 0u;
  }

  // The player half of a reaction, for that player's game.
  public static func Cue(npc: wref<GameObject>, player: Uint32, cue: Int32) -> Void {
    let id = TandemTakedowns.Get(npc.GetGame()).NpcOf(npc);
    if id != 0u {
      TandemNative.SendNpcCue(id, player, cue);
      TandemNative.Log(s"reactions: \(id) \(cue == 1 ? "entered combat with" : "had its guard broken by") player \(player): their game reacts");
    }
  }

  // Guest: the host's NPC reacted to our V; our V's half, with its proxy
  // standing for the NPC (vanilla: NPCStatesComponent.OnCombat, final.reds
  // 168333-168352; NPCPoiseListener, 203306).
  public static func OnCue(game: GameInstance, npc: Uint32, cue: Int32) -> Void {
    let player = GetPlayer(game);
    let proxy = GameInstance.FindEntityByID(game, TandemTakedowns.ProxyEntity(game, npc)) as NPCPuppet;
    if !IsDefined(player) || !IsDefined(proxy) {
      return;
    }
    if cue == 1 {
      let cooldown = StatusEffectHelper.HasStatusEffectWithTagConst(player, n"DodgeOutOfSightCooldown");
      let failure = StatusEffectHelper.HasStatusEffectWithTagConst(player, n"SecondChancePerkCooldown");
      if player.CanAvoidCombatWithDodge() && !cooldown && !failure {
        StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.SecondChancePerkTimeDilation");
        let evt = new DodgeToAvoidCombatEvent();
        evt.npcID = proxy.GetEntityID();
        GameInstance.GetDelaySystem(game).DelayEvent(player, evt, 1.0, false);
        GameInstance.GetDelaySystem(game).DelayEvent(player, evt, 2.0, false);
        GameInstance.GetDelaySystem(game).DelayEvent(player, evt, 3.0, false);
        TandemNative.Log(s"reactions: \(npc) entered combat with us: second chance");
      } else if player.CanAvoidCombatWithGag() {
        StatusEffectHelper.ApplyStatusEffect(proxy, t"BaseStatusEffect.GagOpportunity");
        TandemNative.Log(s"reactions: \(npc) entered combat with us: gag");
      }
    } else if cue == 2 {
      if player.HasFinisherAvailable() {
        StatusEffectHelper.ApplyStatusEffect(proxy, t"BaseStatusEffect.FinisherActiveStatusEffect");
        if !proxy.IsFinisherSoundPlayed() {
          GameObject.PlaySound(player, n"w_melee_perk_finisher_ready");
          proxy.SetFinisherSoundPlayed(true);
        }
        TandemNative.Log(s"reactions: \(npc)'s guard broken: finisher offered");
      }
    }
  }

  // Host: a whistle (Bait) a guest put on our NPC: who to walk to.
  public static func WhistleCaster(owner: ref<GameObject>) -> ref<GameObject> {
    if !TandemNative.IsHost() || !IsDefined(owner) {
      return null;
    }
    let effects: array<ref<StatusEffect>>;
    StatusEffectHelper.GetAppliedEffectsWithTag(owner, n"Whistle", effects);
    if ArraySize(effects) == 0 {
      return null;
    }
    let caster = GameInstance.FindEntityByID(owner.GetGame(), effects[0].GetInstigatorEntityID()) as GameObject;
    return TandemReactions.GuestOf(owner.GetGame(), caster) != 0u ? caster : null;
  }

  // Host: a guest's hit on one of our NPCs (Npcs.reds TakeHits) makes a
  // hostile NPC turn on that guest's puppet, the way vanilla injects a
  // combat target (AIActionHelper.TryStartCombatWithTarget, final.reds
  // 169940): only if it is hostile to them already; civilians flee by the
  // hit's own stimulus.
  public static func OnGuestHit(body: ref<ScriptedPuppet>, instigator: ref<GameObject>) -> Void {
    if !IsDefined(body) || !IsDefined(instigator) || body.IsDead() || body.IsCharacterCivilian() {
      return;
    }
    if Equals(GameObject.GetAttitudeBetween(body, instigator), EAIAttitude.AIA_Hostile) {
      TargetTrackingExtension.InjectThreat(body, instigator);
    }
  }
}

// An NPC entering combat with a guest's puppet: the host's own V gets no
// second chance or gag for it; the guest's game gets the cue.
@wrapMethod(NPCStatesComponent)
private final func OnCombat() -> Void {
  let npc = this.GetPuppet() as NPCPuppet;
  let guest = 0u;
  if IsDefined(npc) {
    let target = FromVariant<wref<GameObject>>(npc.GetAIControllerComponent().GetBehaviorArgument(n"CombatTarget"));
    guest = TandemReactions.GuestOf(npc.GetGame(), target);
  }
  if guest == 0u {
    wrappedMethod();
    return;
  }
  let takedowns = TandemTakedowns.Get(npc.GetGame());
  takedowns.SuppressAvoid(true);
  wrappedMethod();
  takedowns.SuppressAvoid(false);
  TandemReactions.Cue(npc, guest, 1);
}

@wrapMethod(PlayerPuppet)
public const final func CanAvoidCombatWithDodge() -> Bool {
  if TandemTakedowns.Get(this.GetGame()).AvoidSuppressed() {
    return false;
  }
  return wrappedMethod();
}

@wrapMethod(PlayerPuppet)
public const final func CanAvoidCombatWithGag() -> Bool {
  if TandemTakedowns.Get(this.GetGame()).AvoidSuppressed() {
    return false;
  }
  return wrappedMethod();
}

// Guest: the second chance on a proxy. The proxy has no senses: the host's
// detection says whether the NPC still sees us; the memory wipe goes to
// the real NPC (vanilla: PlayerPuppet.OnDodgeToAvoidCombatEvent, 39682).
@wrapMethod(PlayerPuppet)
protected cb func OnDodgeToAvoidCombatEvent(evt: ref<DodgeToAvoidCombatEvent>) -> Bool {
  let npc = TandemTakedowns.Get(this.GetGame()).NpcOf(GameInstance.FindEntityByID(this.GetGame(), evt.npcID) as GameObject);
  if npc == 0u || TandemNative.IsHost() {
    return wrappedMethod(evt);
  }
  let system = GameInstance.GetScriptableSystemsContainer(this.GetGame()).Get(n"TandemSystem") as TandemSystem;
  let you = IsDefined(system) ? system.You() : 0u;
  let bit = 1;
  let p = 1u;
  while p < you {
    bit *= 2;
    p += 1u;
  }
  if you > 0u && you <= 8u && (TandemNative.NpcDetected(npc) & bit) != 0 {
    return false; // it still sees us
  }
  let result = wrappedMethod(evt);
  let proxy = GameInstance.FindEntityByID(this.GetGame(), evt.npcID) as GameObject;
  if StatusEffectSystem.ObjectHasStatusEffect(proxy, t"BaseStatusEffect.MemoryWipeOnDodge") {
    let ids: array<TweakDBID>;
    ArrayPush(ids, t"BaseStatusEffect.MemoryWipeOnDodge");
    TandemNative.SendNpcEffect(npc, ids);
    TandemNative.Log(s"reactions: second chance taken on \(npc): its memory wiped");
  }
  return result;
}

// A guard broken by a guest's hit offers that guest the finisher, not our V.
@wrapMethod(NPCPoiseListener)
protected cb func OnStatPoolMinValueReached(value: Float) -> Bool {
  if IsDefined(this.npc) {
    let guest = TandemReactions.GuestOf(this.npc.GetGame(), this.npc.GetLastHitInstigator());
    if guest != 0u {
      TandemReactions.Cue(this.npc, guest, 2);
      return true;
    }
  }
  return wrappedMethod(value);
}

// Bait / Whistle from a guest: the NPC walks to that guest's V, not ours
// (vanilla takes the local player: final.reds 317768, 317836).
@wrapMethod(ForceMoveInCombatWhistleEffector)
protected func ActionOn(owner: ref<GameObject>) -> Void {
  let caster = TandemReactions.WhistleCaster(owner);
  if IsDefined(caster) {
    this.m_targetPosition = caster.GetWorldPosition();
    TandemNative.Log(s"reactions: whistle leads to player \(TandemReactions.GuestOf(owner.GetGame(), caster))");
  }
  wrappedMethod(owner);
}

@wrapMethod(UpdateWhistlePosition)
protected func Activate(context: ScriptExecutionContext) -> Void {
  let owner = AIBehaviorScriptBase.GetPuppet(context);
  let caster = TandemReactions.WhistleCaster(owner);
  if !IsDefined(caster) {
    wrappedMethod(context);
    return;
  }
  let proxy: ref<GameObject>;
  let position: Vector4;
  if !UpdateWhistlePosition.TryGetDesiredWhistlePosition(owner, caster.GetWorldPosition(), position, proxy) {
    return;
  }
  this.SetTarget(context, IsDefined(proxy) ? proxy : caster);
  this.SetPosition(context, position);
}
