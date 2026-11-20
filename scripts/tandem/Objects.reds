// The world's objects between players: glass and breakable scenery
// (docs/architecture/devices.md, "Glass and breakable scenery"; engine
// facts: docs/research/engine/devices.md §13.90).
//
// A window, a cover or a road sign that breaks is physics destruction: it
// has no script state to sync, only the moment it breaks.
// - Our game tells what broke here: a destruction component of a placed
//   entity (the entity's PhysicalDestructionEvent names it: the same entity
//   and component in every game), or a piece of the world's own geometry,
//   which has no name, by its place (the destructible spots system's
//   fracture notice).
// - The others break the same piece their own way: the component's own
//   TriggerDestruction, else a physics impulse at the place (the one a
//   grenade pushes debris with). No attack, no hit, no damage to anyone.
// - Breaking it here makes our game see a break of its own: core
//   devices::Fractures tells that echo from news (TandemObjects.Broke).
// - The hooks run on whatever thread the engine breaks things on: they only
//   queue (StreamingNatives.reds); all of this runs on the tick.
//
// tools/game.sh cmd glass (counts) | glass break (replays a break at what V
// looks at, as if another game had said it: the levers alone, no session
// needed) | glass part (the same on the destruction component of the
// entity V looks at)

public class TandemObjectSync extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_logged: Int32;
  // Replayed here: by a component's own trigger, by an impulse at the place.
  private let m_parts: Int32;
  private let m_pulses: Int32;
  private let m_far: Int32;

  public static func Get(game: GameInstance) -> ref<TandemObjectSync> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemObjectSync") as TandemObjectSync;
  }

  // Every tick, session or not (TandemSystem.OnTick; you: 0 out of one).
  public static func Tick(game: GameInstance, you: Uint32) -> Void {
    let sync = TandemObjectSync.Get(game);
    if IsDefined(sync) {
      sync.Update(you);
    }
  }

  public func Update(you: Uint32) -> Void {
    let game = this.GetGameInstance();
    if this.m_you != you {
      // A new session, or its end: nothing broken is remembered.
      TandemObjects.Reset();
      this.m_you = you;
      this.m_logged = 0;
    }
    // What broke in our game since the last tick (taken off the queue out
    // of a session too).
    let broken = TandemStreaming.TakePoints(TandemPointKind.Fracture());
    // A device's fire (Turrets.reds): its hooks' queues, its polls.
    let now = EngineTime.ToFloat(GameInstance.GetEngineTime(game));
    let turrets = TandemTurrets.Get(game);
    if IsDefined(turrets) {
      turrets.Update(you, now);
    }
    if you == 0u {
      return;
    }
    for point in broken {
      this.Ours(point);
    }
    for event in TandemObjects.TakeEvents() {
      if event.kind == 1 {
        this.Break(event.entity, event.name, new Vector4(event.x, event.y, event.z, 1.0), event.player);
      } else if event.kind == 2 && IsDefined(turrets) {
        turrets.OnEvent(event, now);
      }
    }
  }

  // A line per break is too many in a firefight: the first forty.
  private func LogSome(text: String) -> Void {
    if this.m_logged < 40 {
      this.m_logged += 1;
      TandemNative.Log(text);
    } else {
      TandemNative.LogDebug(text);
    }
  }

  // ---- ours ----

  private func Ours(point: TandemStreamedPoint) -> Void {
    let at = new Vector4(point.x, point.y, point.z, 1.0);
    // A part has a name every game knows only on a placed entity.
    let named = EntityID.IsDefined(point.entity) && EntityID.IsStatic(point.entity) && IsNameValid(point.name);
    let entity = named ? point.entity : EntityID.FromHash(0ul);
    let name = named ? point.name : n"None";
    if TandemObjects.Broke(entity, name, at, point.value) {
      let what = named ? s"part \(NameToString(name)) of \(TandemNative.EntityHex(entity))" : "a piece of the world";
      this.LogSome(s"glass: \(what) broke here at (\(at.X) \(at.Y) \(at.Z)): told");
    }
  }

  // ---- theirs ----

  // Another game's break, here: the same part by its own trigger, else an
  // impulse at the place. Nothing of it is an attack.
  private func Break(entity: EntityID, name: CName, at: Vector4, player: Uint32) -> Void {
    let game = this.GetGameInstance();
    let ours = GetPlayer(game);
    if !IsDefined(ours) {
      return;
    }
    // Not in our world (streamed out): nothing to break, nothing to see.
    if Vector4.Distance(ours.GetWorldPosition(), at) > 200.0 {
      this.m_far += 1;
      return;
    }
    if EntityID.IsDefined(entity) && IsNameValid(name) {
      let object = GameInstance.FindEntityByID(game, entity);
      let part = IsDefined(object) ? object.TandemDestructionPart(name) : null;
      if IsDefined(part) {
        if !part.IsFractured() {
          part.TriggerDestruction(TandemObjectSync.BreakVelocity());
          this.m_parts += 1;
          this.LogSome(s"glass: player \(player) broke part \(NameToString(name)) of \(TandemNative.EntityHex(entity)): triggered here");
        }
        return;
      }
      this.LogSome(s"glass: player \(player) broke part \(NameToString(name)) of \(TandemNative.EntityHex(entity)), not here by that name: an impulse at its place");
    }
    this.Pulse(ours, at);
    this.m_pulses += 1;
    this.LogSome(s"glass: player \(player) broke something at (\(at.X) \(at.Y) \(at.Z)): an impulse there");
  }

  // How hard a replayed part is struck (TriggerDestruction's velocity).
  public static func BreakVelocity() -> Float { return 20.0; }
  // How far around the place the impulse reaches.
  public static func PulseRadius() -> Float { return 1.0; }

  // The physics impulse a grenade pushes the world with
  // (CombatGadgetHelper.SpawnPhysicalImpulse), at a place: no attack with
  // it, so nothing is hit or hurt.
  private func Pulse(source: ref<GameObject>, at: Vector4) -> Void {
    let effect = GameInstance.GetGameEffectSystem(this.GetGameInstance()).CreateEffectStatic(n"physicalImpulseSphere", n"None", source);
    if !IsDefined(effect) {
      return;
    }
    EffectData.SetFloat(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.radius, TandemObjectSync.PulseRadius());
    EffectData.SetVector(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.position, at);
    effect.Run();
  }

  // ---- commands (tools/game.sh cmd glass ...) ----

  public func Command(command: String) -> Void {
    let game = this.GetGameInstance();
    let ours = GetPlayer(game);
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "") {
      TandemNative.Log(TandemObjects.Report());
      TandemNative.Log(s"glass: replayed here: \(this.m_parts) part(s) triggered, \(this.m_pulses) impulse(s), \(this.m_far) too far to matter");
      return;
    }
    if !IsDefined(ours) {
      return;
    }
    let targeting = GameInstance.GetTargetingSystem(game);
    let at = targeting.GetLookAtPosition(ours, true, true);
    if Equals(verb, "break") {
      if Vector4.IsZero(at) {
        TandemNative.Log("glass: V looks at nothing");
        return;
      }
      TandemNative.Log(s"glass: an impulse of \(TandemObjectSync.PulseRadius()) m at (\(at.X) \(at.Y) \(at.Z)), as for another game's break");
      this.Pulse(ours, at);
    } else if Equals(verb, "part") {
      let object = targeting.GetLookAtObject(ours, true, true);
      let parts = IsDefined(object) ? object.TandemDestructionParts() : [];
      if ArraySize(parts) == 0 {
        TandemNative.Log("glass: V looks at no entity with a destruction component");
        return;
      }
      for part in parts {
        TandemNative.Log(s"glass: part \(NameToString(part.GetName())) of \(TandemNative.EntityHex(object.GetEntityID())) (fractured \(part.IsFractured())): triggered");
        part.TriggerDestruction(TandemObjectSync.BreakVelocity());
      }
    } else {
      TandemNative.Log(s"glass: unknown command: \(command)");
    }
  }
}

// An entity's destruction component by its name, and all of them.
@addMethod(Entity)
public func TandemDestructionPart(name: CName) -> ref<PhysicalDestructionComponent> {
  return this.FindComponentByName(name) as PhysicalDestructionComponent;
}

@addMethod(Entity)
public func TandemDestructionParts() -> array<ref<PhysicalDestructionComponent>> {
  let parts: array<ref<PhysicalDestructionComponent>>;
  for component in this.GetComponents() {
    let part = component as PhysicalDestructionComponent;
    if IsDefined(part) {
      ArrayPush(parts, part);
    }
  }
  return parts;
}

// A destruction component of this entity broke (the engine's notice to its
// entity), on whatever thread: queued with its name, told on the tick.
public abstract class TandemGlass {
  public static func Queue(object: ref<GameObject>, evt: ref<PhysicalDestructionEvent>) -> Void {
    if TandemNative.IsStarted() && IsDefined(evt) {
      TandemStreaming.QueuePoint(TandemPointKind.Fracture(), object.GetEntityID(), EntityID.FromHash(0ul), evt.componentName, object.GetWorldPosition(), Cast<Int32>(evt.levelOfDestruction));
    }
  }
}

@wrapMethod(GameObject)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

// The classes with a handler of their own (none calls the one above).
@wrapMethod(DestructibleMasterDevice)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(DestructibleMasterLight)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(DisplayGlass)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(BillboardDevice)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(BaseDestructibleDevice)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(DestructibleRoadSign)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(RetractableAd)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(ElectricLight)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(Window)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

@wrapMethod(RoadBlock)
protected cb func OnPhysicalDestructionEvent(evt: ref<PhysicalDestructionEvent>) -> Bool {
  TandemGlass.Queue(this, evt);
  return wrappedMethod(evt);
}

// A piece of the world's own geometry broke (no entity: a pane of a shop
// front, a bus shelter): the engine tells where. Queued, as above.
@wrapMethod(gameDestructibleSpotsSystem)
protected cb func OnNotifyAboutFracture(localPlayerObj: wref<GameObject>, fracturePos: Vector4) -> Bool {
  if TandemNative.IsStarted() {
    TandemStreaming.QueuePoint(TandemPointKind.Fracture(), EntityID.FromHash(0ul), EntityID.FromHash(0ul), n"None", fracturePos, 0);
  }
  return wrappedMethod(localPlayerObj, fracturePos);
}
