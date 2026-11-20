// Hit reactions on proxies and puppets (docs/architecture/presence.md;
// engine facts in docs/research/engine/presence.md §13.35, Hit reactions).
//
// An NPC's reaction to a hit is chosen by its HitReactionComponent and
// played by its AI: only the lightest (a twitch) is started by the
// component alone; an impact, a stagger, a knockdown, a block and the death
// animation are tasks of its behaviour tree, which launch the reaction's
// animation action. A guest's proxy has no brain: it would twitch and
// otherwise stand there, and die without an animation. So the host says
// what each shared NPC plays (NpcReaction: the animation feature the game
// itself filled) and the proxy plays that, with the same calls the tasks
// make. Ragdolls go the same way, on and off.
//
// The host decides, the proxies show: one owner. A proxy is moved by
// tandem's controller to where the host's NPC is, so a reaction's own
// motion never takes it elsewhere.
//
// A remote V's puppet is an NPC body too, but its reactions are its
// player's: what that V does when hit is in its pose stream. A puppet that
// is hit only twitches and flinches (Presence.reds), which moves nothing.

public class TandemPendingDeath {
  public let npc: Uint32;
  public let at: Float;
}

public class TandemReactionsSync {
  private let m_game: GameInstance;
  // Guest: proxies that died with no death reaction from the host yet.
  private let m_deaths: array<ref<TandemPendingDeath>>;
  // Guest: proxies whose death the host's reaction already played.
  private let m_played: array<Uint32>;
  private let m_sent: Int32;
  private let m_shown: Int32;
  private let m_shownByType: array<Int32>;
  private let m_logged: array<CName>;

  public static func Of(game: GameInstance) -> ref<TandemReactionsSync> {
    let presence = TandemPresenceSync.Of(game);
    return IsDefined(presence) ? presence.Reactions() : null;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    ArrayResize(this.m_shownByType, 12);
  }

  private func Once(key: CName, text: String) -> Void {
    if !ArrayContains(this.m_logged, key) {
      ArrayPush(this.m_logged, key);
      TandemNative.Log(text);
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }

  public static func TypeName(type: Int32) -> String {
    switch type {
      case 0:
        return "ragdoll end";
      case 1:
        return "twitch";
      case 2:
        return "impact";
      case 3:
        return "stagger";
      case 4:
        return "pain";
      case 5:
        return "knockdown";
      case 6:
        return "ragdoll";
      case 7:
        return "death";
      case 8:
        return "block";
      case 9:
        return "guard break";
      case 10:
        return "parry";
      case 11:
        return "bump";
      default:
        return "?";
    }
  }

  // ---- the owner (the host, or a guest for its own NPCs): what they play ----

  // One of our NPCs starts a reaction: `data` is the feature its component
  // or its AI task just applied; `type` 6 a ragdoll, 0 its end.
  public static func Started(owner: wref<GameObject>, data: ref<AnimFeature_HitReactionsData>, type: Int32, by: wref<GameObject>, way: Vector4) -> Void {
    if !TandemNative.IsStarted() || !IsDefined(owner) || !TandemOwn.Entity(owner.GetGame(), owner.GetEntityID()) {
      return;
    }
    let system = TandemCombat.System(owner.GetGame());
    let npc = IsDefined(system) ? system.SharedNpcOf(owner.GetEntityID()) : 0u;
    if npc == 0u {
      return;
    }
    let who = IsDefined(by) ? system.PlayerOfPuppet(by.GetEntityID()) : 0u;
    if IsDefined(data) {
      TandemPresence.SendNpcReaction(npc, who, type, data.hitDirection, data.hitIntensity, data.hitBodyPart, data.animVariation, data.hitSource, data.stance, data.npcMovementSpeed, data.npcMovementDirection, data.useInitialRotation, data.angleToAttack, data.hitDirectionWs);
    } else {
      TandemPresence.SendNpcReaction(npc, who, type, 0, 0, 0, 0, -1, 0, 0, 0, false, 0.0, way);
    }
    let sync = TandemReactionsSync.Of(owner.GetGame());
    if IsDefined(sync) {
      sync.m_sent += 1;
      if type != 1 || sync.m_sent % 50 == 1 {
        TandemNative.Log(s"reaction: npc \(npc) plays \(TandemReactionsSync.TypeName(type)) (sent to the games linked with us; \(sync.m_sent) so far)");
      }
    }
  }

  // ---- every game: the same on our proxies ----

  public func Tick(now: Float, system: ref<TandemSystem>) -> Void {
    let npcs = system.NpcsSync();
    for reaction in TandemPresence.TakeNpcReactions() {
      // Alive or just dead: a death reaction may come after the death.
      let body = IsDefined(npcs) ? GameInstance.FindEntityByID(this.m_game, npcs.ProxyEntity(reaction.npc)) as NPCPuppet : null;
      if IsDefined(body) {
        this.Show(body, reaction, system.You());
      }
    }
    // A proxy that died and was told no death reaction (killed outside its
    // AI's death task, or the message is the host's next): it never stays
    // standing.
    let waiting: array<ref<TandemPendingDeath>>;
    for death in this.m_deaths {
      if ArrayContains(this.m_played, death.npc) {
        ArrayRemove(this.m_played, death.npc);
      } else if now - death.at < 0.6 {
        ArrayPush(waiting, death);
      } else {
        let body = IsDefined(npcs) ? GameInstance.FindEntityByID(this.m_game, npcs.ProxyEntity(death.npc)) as NPCPuppet : null;
        if IsDefined(body) && !body.IsRagdolling() {
          body.QueueEvent(CreateForceRagdollEvent(n"tandem_dead"));
          TandemNative.Log(s"reaction: proxy \(death.npc) died with no death reaction from the host: ragdoll");
        }
      }
    }
    this.m_deaths = waiting;
  }

  // The proxy died (Npcs.reds): its death reaction is the host's.
  public func Died(npc: Uint32) -> Void {
    let death = new TandemPendingDeath();
    death.npc = npc;
    death.at = this.Now();
    ArrayPush(this.m_deaths, death);
  }

  private func Show(body: ref<NPCPuppet>, reaction: TandemNpcReaction, you: Uint32) -> Void {
    let type = reaction.type;
    let way = new Vector4(reaction.x, reaction.y, reaction.z, 0.0);
    if type == 0 {
      body.QueueEvent(CreateDisableRagdollEvent(n"tandem_host"));
    } else if type == 6 {
      ArrayPush(this.m_played, reaction.npc);
      if !body.IsRagdolling() {
        body.QueueEvent(CreateForceRagdollEvent(n"tandem_host"));
        if !Vector4.IsZero(way) {
          // The throw, a moment after the body went limp, as the game's
          // own death task gives it (AIDeathReactionsTask.TurnOnRagdoll).
          let at = body.GetWorldPosition();
          at.Z += 1.0;
          GameInstance.GetDelaySystem(this.m_game).DelayEvent(body, CreateRagdollApplyImpulseEvent(at, way, 10000.0), 0.1, false);
        }
      }
    } else if type == 1 && reaction.by == you && you != 0u {
      // Our own hit: the proxy's component twitched for it already.
      return;
    } else {
      let data = new AnimFeature_HitReactionsData();
      data.hitType = type;
      data.hitDirection = reaction.direction;
      data.hitIntensity = reaction.intensity;
      data.hitBodyPart = reaction.bodyPart;
      data.animVariation = reaction.variation;
      data.hitSource = reaction.source;
      data.stance = reaction.stance;
      data.npcMovementSpeed = reaction.moveSpeed;
      data.npcMovementDirection = reaction.moveDirection;
      data.useInitialRotation = reaction.turns;
      data.angleToAttack = reaction.angle;
      data.hitDirectionWs = way;
      data.initialRotationDuration = 0.1;
      if type == 1 {
        // HitReactionComponent.SendTwitchDataToAnimationGraph.
        AnimationControllerComponent.ApplyFeatureToReplicate(body, n"hit", data);
        AnimationControllerComponent.PushEventToReplicate(body, n"hit");
      } else if type == 8 || type == 10 {
        // AIHitReactionTask.SendDataToAnimationGraph, block and parry.
        AnimationControllerComponent.ApplyFeatureToReplicate(body, n"hit", data);
        AnimationControllerComponent.PushEventToReplicate(body, type == 8 ? n"PlayBlock" : n"PlayParry");
      } else {
        // The reaction's animation action, as the AI's tasks launch it.
        let component = body.GetHitReactionComponent();
        let action = IsDefined(component) ? component.GetHitReactionProxyAction() : null;
        if IsDefined(action) {
          action.Stop();
          action.Setup(data);
          action.Launch();
        }
        AnimationControllerComponent.ApplyFeatureToReplicate(body, n"hit", data);
        if type == 7 {
          ArrayPush(this.m_played, reaction.npc);
        }
      }
    }
    this.m_shown += 1;
    if type >= 0 && type < ArraySize(this.m_shownByType) {
      this.m_shownByType[type] += 1;
    }
    if type != 1 || this.m_shownByType[1] % 50 == 1 {
      TandemNative.Log(s"reaction: proxy \(reaction.npc) plays the host's \(TandemReactionsSync.TypeName(type)) (direction \(reaction.direction), intensity \(reaction.intensity), part \(reaction.bodyPart), variation \(reaction.variation); \(this.m_shown) shown)");
    }
  }

  // The lightest hit reaction, on any NPC body: the one the component
  // starts without the AI, and that moves nothing (a puppet's, Presence.reds).
  // `direction` EAIHitDirection (3 front), `intensity` 0..2, `part`
  // EAIHitBodyPart (4 belly).
  public static func Twitch(body: ref<GameObject>, direction: Int32, intensity: Int32, part: Int32) -> Void {
    let data = new AnimFeature_HitReactionsData();
    data.hitType = 1;
    data.hitDirection = direction;
    data.hitIntensity = intensity;
    data.hitBodyPart = part;
    data.animVariation = RandRange(0, 3);
    data.hitSource = 0;
    AnimationControllerComponent.ApplyFeatureToReplicate(body, n"hit", data);
    AnimationControllerComponent.PushEventToReplicate(body, n"hit");
  }

  // (test) tools/game.sh cmd presence react TYPE: that reaction, from the
  // front, on every proxy (guest) as if the host had sent it.
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let type = StringToInt(command);
    let npcs = system.NpcsSync();
    if !IsDefined(npcs) || type < 0 || type > 11 {
      TandemNative.Log("presence: react TYPE (0 ragdoll end, 1 twitch, 2 impact, 3 stagger, 4 pain, 5 knockdown, 6 ragdoll, 7 death, 8 block, 9 guard break, 10 parry, 11 bump)");
      return;
    }
    let count = 0;
    for proxy in npcs.Proxies() {
      let body = GameInstance.FindEntityByID(this.m_game, proxy.id) as NPCPuppet;
      if IsDefined(body) {
        let reaction: TandemNpcReaction;
        reaction.npc = proxy.npc;
        reaction.by = 0u;
        reaction.type = type;
        reaction.direction = 3;
        reaction.intensity = 1;
        reaction.bodyPart = 4;
        reaction.variation = 0;
        reaction.source = 0;
        reaction.stance = 0;
        reaction.moveSpeed = 0;
        reaction.moveDirection = 0;
        reaction.turns = false;
        reaction.angle = 0.0;
        let back = body.GetWorldForward() * -3.0;
        reaction.x = back.X;
        reaction.y = back.Y;
        reaction.z = 1.0;
        this.Show(body, reaction, 0u);
        count += 1;
      }
    }
    TandemNative.Log(s"presence: \(TandemReactionsSync.TypeName(type)) on \(count) proxies");
  }

  public func Report() -> Void {
    TandemNative.Log(s"reaction: sent \(this.m_sent) (host), shown \(this.m_shown) (guest: twitch \(this.m_shownByType[1]), impact \(this.m_shownByType[2]), stagger \(this.m_shownByType[3]), knockdown \(this.m_shownByType[5]), ragdoll \(this.m_shownByType[6]), death \(this.m_shownByType[7])); deaths waiting \(ArraySize(this.m_deaths))");
  }
}

// ---- host: where the game starts each reaction ----

// A twitch: the component's own (final.reds:187759).
@wrapMethod(HitReactionComponent)
protected final func SendTwitchDataToAnimationGraph() -> Void {
  TandemNative.HookRan(n"HitReactions.HitReactionComponent.SendTwitchDataToAnimationGraph");
  wrappedMethod();
  TandemReactionsSync.Started(this.GetOwner(), this.m_animHitReaction, 1, this.GetHitInstigator(), new Vector4(0.0, 0.0, 0.0, 0.0));
}

// Impact, stagger, pain, knockdown, guard break, block, parry, bump: the
// AI's reaction task fills the feature and launches the action
// (final.reds:459725).
@wrapMethod(AIHitReactionTask)
private final func SendDataToAnimationGraph(context: ScriptExecutionContext) -> Void {
  wrappedMethod(context);
  let owner = ScriptExecutionContext.GetOwner(context) as NPCPuppet;
  let component = IsDefined(owner) ? owner.GetHitReactionComponent() : null;
  if IsDefined(component) {
    let data = component.GetHitReactionData();
    if IsDefined(data) {
      TandemReactionsSync.Started(owner, data, data.hitType, component.GetHitInstigator(), new Vector4(0.0, 0.0, 0.0, 0.0));
    }
  }
}

// The death animation (final.reds:458028); a death that ragdolls instead
// comes through the ragdoll below.
@wrapMethod(AIDeathReactionsTask)
protected func PlayHitReactionAction(context: ScriptExecutionContext) -> Bool {
  let played = wrappedMethod(context);
  let owner = ScriptExecutionContext.GetOwner(context) as NPCPuppet;
  let component = IsDefined(owner) ? owner.GetHitReactionComponent() : null;
  if IsDefined(component) && IsDefined(this.m_hitData) {
    TandemReactionsSync.Started(owner, this.m_hitData, 7, component.GetHitInstigator(), new Vector4(0.0, 0.0, 0.0, 0.0));
  }
  return played;
}

// Ragdolls, whatever started them (a death, an explosion, a fall, a throw).
@wrapMethod(NPCPuppet)
protected cb func OnRagdollEnabledEvent(evt: ref<RagdollNotifyEnabledEvent>) -> Bool {
  TandemNative.HookRan(n"HitReactions.NPCPuppet.OnRagdollEnabledEvent");
  let result = wrappedMethod(evt);
  if TandemNative.IsStarted() && TandemOwn.Entity(this.GetGame(), this.GetEntityID()) {
    let component = this.GetHitReactionComponent();
    let way = new Vector4(0.0, 0.0, 0.0, 0.0);
    if IsDefined(component) {
      way = component.GetOverridenHitDirection() * MaxF(component.GetRagdollImpulse(), 1.0);
    }
    TandemReactionsSync.Started(this, null, 6, IsDefined(component) ? component.GetHitInstigator() : null, way);
  }
  return result;
}

@wrapMethod(NPCPuppet)
protected cb func OnRagdollDisabledEvent(evt: ref<RagdollNotifyDisabledEvent>) -> Bool {
  TandemNative.HookRan(n"HitReactions.NPCPuppet.OnRagdollDisabledEvent");
  let result = wrappedMethod(evt);
  if TandemNative.IsStarted() && TandemOwn.Entity(this.GetGame(), this.GetEntityID()) {
    TandemReactionsSync.Started(this, null, 0, null, new Vector4(0.0, 0.0, 0.0, 0.0));
  }
  return result;
}
