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
    if !IsDefined(object) {
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
    } else if cue == 3 {
      // The host's NPC lost sight of us when we dodged: the success half of
      // vanilla's second chance (PlayerPuppet.OnDodgeToAvoidCombatEvent,
      // final.reds 39689-39696), its memory wipe done there.
      GameObject.PlaySoundEvent(player, n"ui_gmpl_stealth_avoid_combat");
      StatusEffectHelper.RemoveStatusEffect(player, t"BaseStatusEffect.SecondChancePerkTimeDilation");
      StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.DodgeOutOfSightCooldown");
      StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.SecondChancePerkCooldown");
      TandemNative.Log(s"reactions: second chance taken on \(npc): out of its sight, its memory wiped");
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

  // Host: a guest's second chance on our NPC (OnDodgeToAvoidCombatEvent,
  // below): its memory is wiped only if it does not see their puppet now,
  // as vanilla asks of its own V (IsAgentVisible); false: refused.
  public static func DodgedOutOfSight(body: ref<ScriptedPuppet>, puppet: ref<GameObject>, npc: Uint32, player: Uint32) -> Bool {
    let senses = body.GetSensesComponent();
    if IsDefined(senses) && senses.IsAgentVisible(puppet) {
      TandemNative.Log(s"reactions: player \(player)'s second chance on \(npc) refused: it still sees them");
      return false;
    }
    TandemNative.SendNpcCue(npc, player, 3);
    return true;
  }

  // Host: a whistle (Bait) a guest put on our NPC: who to walk to.
  public static func WhistleCaster(owner: ref<GameObject>) -> ref<GameObject> {
    if !IsDefined(owner) {
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
    // Our companions fight whoever a guest fights, as they would for our V.
    TandemReactions.RallyCompanions(body.GetGame(), body, instigator);
  }

  // Host: a fight between one of our NPCs (`enemy`) and a guest's V (its
  // puppet `guest`). Our V's companions (Jackie, Panam, Judy, Takemura...:
  // AIFollowerRole followers, ScriptedPuppet.IsPlayerCompanion) take that
  // NPC on as they take on our V's enemies, when they are hostile to it and
  // friendly to the guest; vanilla's follower only ever looks out for the
  // player it follows. Their attitudes to the guest's V are our V's
  // (Attitudes.reds), so they never turn on the guest.
  public static func RallyCompanions(game: GameInstance, enemy: wref<GameObject>, guest: wref<GameObject>) -> Void {
    if !TandemNative.IsHost() || !IsDefined(enemy) || !IsDefined(guest) {
      return;
    }
    let player = GetPlayer(game);
    let foe = enemy as ScriptedPuppet;
    if !IsDefined(player) || !IsDefined(foe) || foe.IsDead() || foe.IsCharacterCivilian() || TandemReactions.GuestOf(game, guest) == 0u {
      return;
    }
    for companion in player.GetNPCsAroundObject(60.0) {
      if IsDefined(companion) && companion != foe && !companion.IsDead() && companion.IsPlayerCompanion() && Equals(GameObject.GetAttitudeBetween(companion, enemy), EAIAttitude.AIA_Hostile) && NotEquals(GameObject.GetAttitudeBetween(companion, guest), EAIAttitude.AIA_Hostile) {
        TargetTrackingExtension.InjectThreat(companion, enemy);
      }
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

// Guest: the second chance on a proxy. The proxy has no senses and the
// host's NPC is the one that must not see us: vanilla's own conditions on
// our side (we just dodged, no cooldown), then the memory wipe goes to the
// host as a request (NpcEffect), which applies it only if its NPC does not
// see our puppet and answers with the OutOfSight cue for the rest (OnCue).
// (vanilla: PlayerPuppet.OnDodgeToAvoidCombatEvent, final.reds 39682. The
// host's detected bit cannot stand for "sees us": it holds while the NPC is
// in combat with us, which it has just entered.)
@wrapMethod(PlayerPuppet)
protected cb func OnDodgeToAvoidCombatEvent(evt: ref<DodgeToAvoidCombatEvent>) -> Bool {
  let npc = TandemTakedowns.Get(this.GetGame()).NpcOf(GameInstance.FindEntityByID(this.GetGame(), evt.npcID) as GameObject);
  if npc == 0u || TandemOwn.Npc(this.GetGame(), npc) {
    return wrappedMethod(evt);
  }
  let justDodged = StatusEffectHelper.HasStatusEffectWithTagConst(this, n"JustDodgedActiveBuffer");
  let cooldown = StatusEffectHelper.HasStatusEffectWithTagConst(this, n"DodgeOutOfSightCooldown");
  let failure = StatusEffectHelper.HasStatusEffectWithTagConst(this, n"SecondChancePerkCooldown");
  if justDodged && !cooldown && !failure {
    let ids: array<TweakDBID>;
    ArrayPush(ids, t"BaseStatusEffect.MemoryWipeOnDodge");
    TandemNative.SendNpcEffect(npc, ids);
    TandemNative.Log(s"reactions: second chance on \(npc): we dodged, does it still see us?");
  }
  return false;
}

// A guard broken by a guest's hit offers that guest the finisher, not our V.
@wrapMethod(NPCPoiseListener)
protected cb func OnStatPoolMinValueReached(value: Float) -> Bool {
  // Guest: a proxy's poise follows the host's (NpcPoise, Outcomes.reds);
  // whose finisher it is comes from the host, as a cue.
  if IsDefined(this.npc) && TandemOwn.IsProxy(this.npc.GetGame(), this.npc.GetEntityID()) {
    return true;
  }
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
  let position = new Vector4(0.0, 0.0, 0.0, 0.0);
  if !UpdateWhistlePosition.TryGetDesiredWhistlePosition(owner, caster.GetWorldPosition(), position, proxy) {
    return;
  }
  this.SetTarget(context, IsDefined(proxy) ? proxy : caster);
  this.SetPosition(context, position);
}
