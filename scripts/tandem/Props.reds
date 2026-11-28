// Physics props between players (docs/architecture/props.md; engine facts:
// docs/research/engine/devices.md §13.162).
//
// A placed prop with a physics body (a placed entity's PhysicalMeshComponent:
// a bin, a crate, a chair, a cone that is an entity) that something moved is
// the area owner's, as its NPCs and its street are:
// - The game that runs the area round our V (TandemArea.Owns) simulates its
//   props as alone. A prop the engine tells touched (its entity's
//   PhysicalCollisionEvent, queued by the hook below on whatever thread)
//   within 100 m of our V is followed while it moves: its body's place,
//   pose and speed ten times a second (TandemObjects.PropMoving, PropsRound),
//   then where it came to rest (PropRested: the session's record).
// - Another game's moving prop is held to that game's word: our instance's
//   body kinematic, put where the owner's states say, drawn 200 ms in the
//   past (TandemObjects.PropDrawn); at its rest, put there and given back to
//   our physics, asleep where the owner's lies.
// - A game that does not run its area never simulates a competing copy: a
//   touch of our own V (or its car) on our instance is a push to the owner
//   (PropPushed: our instance moved at once, for feel; the owner gives its
//   real prop the same speed and its word puts ours right). Any touch of
//   ours the owner never answers within 2 s is put back where it stood
//   (degraded fallback "prop put back").
// - A prop the session's record has at rest elsewhere than where our world
//   put it (a late joiner, a prop moved before our V came) is put there
//   once its entity is in our world, checked once a second within 120 m.
//
// tools/game.sh cmd props (counts) | props look (the prop V looks at) |
// props push [M/S] (pushes it, as our V's touch would) | props at HEX
// (where a prop is here); test query prop HEX (TestQuery.reds).

public class TandemPropMotion {
  public let entity: EntityID;
  public let part: Int32;
  public let body: ref<PhysicalBodyInterface>;
  public let last: Vector4;
  public let still: Vector4;
  public let stillAt: Float;
  public let readAt: Float;
}

public class TandemPropHeld {
  public let entity: EntityID;
  public let part: Int32;
  public let owner: Uint32;
  public let body: ref<PhysicalBodyInterface>;
}

// A touch on our instance of a prop another game runs: where it stood, and
// whether our V's push was told (its speed read the tick after).
public class TandemPropKick {
  public let entity: EntityID;
  public let part: Int32;
  public let body: ref<PhysicalBodyInterface>;
  public let from: Transform;
  public let at: Float;
  public let ours: Bool;
  public let told: Bool;
}

public class TandemPropSync extends ScriptableSystem {
  private let m_you: Uint32;
  private let m_moving: array<ref<TandemPropMotion>>;
  private let m_held: array<ref<TandemPropHeld>>;
  private let m_kicks: array<ref<TandemPropKick>>;
  private let m_roundAt: Float;
  private let m_checkAt: Float;
  private let m_logged: Int32;
  // Counts for `props`.
  private let m_touched: Int32;
  private let m_followed: Int32;
  private let m_full: Int32;
  private let m_rested: Int32;
  private let m_heldCount: Int32;
  private let m_placed: Int32;
  private let m_kicked: Int32;
  private let m_putBack: Int32;
  private let m_given: Int32;

  public static func Get(game: GameInstance) -> ref<TandemPropSync> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemPropSync") as TandemPropSync;
  }

  // How far from our V a touched prop is followed (regions::kShare).
  public static func Range() -> Float { return 100.0; }
  // Ours followed at once, others' held at once, at most.
  public static func MaxMoving() -> Int32 { return 32; }
  // Under 1 cm in half a second: at rest (props::kRestMove, kRestUs).
  public static func RestMove() -> Float { return 0.01; }
  public static func RestTime() -> Float { return 0.5; }
  // Ten rounds a second (props::kStatesEveryUs).
  public static func RoundTime() -> Float { return 0.1; }
  // A touch of ours on another game's prop that game never answered.
  public static func AnswerTime() -> Float { return 2.0; }
  // The record's rests within this of our V are checked once a second.
  public static func Reach() -> Float { return 120.0; }
  // A body this far from its rest is put there.
  public static func Off() -> Float { return 0.05; }
  // A push's speed when nobody says (props push).
  public static func PushSpeed() -> Float { return 4.0; }
  // A prop's nominal mass where its body's own velocity setter is missing.
  public static func Mass() -> Float { return 10.0; }

  // Every tick in a session, after TandemObjectSync took the queue
  // (Objects.reds: TakeEvents routes props here).
  public static func Tick(game: GameInstance, you: Uint32, now: Float) -> Void {
    let sync = TandemPropSync.Get(game);
    if IsDefined(sync) {
      sync.Update(you, now);
    }
  }

  public func Update(you: Uint32, now: Float) -> Void {
    let touches = TandemStreaming.TakePoints(TandemPointKind.PropTouch());
    if this.m_you != you {
      // A new session, or its end: what we held goes back to our physics.
      this.ReleaseAll();
      this.m_you = you;
      this.m_logged = 0;
    }
    if you == 0u {
      return;
    }
    let game = this.GetGameInstance();
    let ours = GetPlayer(game);
    if !IsDefined(ours) {
      return;
    }
    let here = ours.GetWorldPosition();
    let owns = TandemArea.Owns();
    for point in touches {
      this.Touched(point, ours, here, owns, now);
    }
    for event in TandemObjects.TakeProps() {
      if event.kind == 1 {
        this.Hold(event);
      } else if event.kind == 2 {
        this.Rest(event);
      } else if event.kind == 3 {
        this.Give(event);
      }
    }
    this.Follow(here, now);
    this.Draw();
    this.Answer(now);
    if now - this.m_checkAt >= 1.0 || now < this.m_checkAt {
      this.m_checkAt = now;
      this.Check(here);
    }
  }

  // A line per prop is too many after a blast: the first forty.
  private func LogSome(text: String) -> Void {
    if this.m_logged < 40 {
      this.m_logged += 1;
      TandemNative.Log(text);
    } else {
      TandemNative.LogDebug(text);
    }
  }

  // ---- bodies ----

  // A prop's body: the part-th physical mesh of its entity, simulated.
  public static func Body(entity: ref<Entity>, part: Int32) -> ref<PhysicalBodyInterface> {
    if !IsDefined(entity) {
      return null;
    }
    let index = 0;
    for component in entity.GetComponents() {
      let mesh = component as PhysicalMeshComponent;
      if IsDefined(mesh) {
        if index == part {
          return mesh.CreatePhysicalBodyInterface();
        }
        index += 1;
      }
    }
    return null;
  }

  // The index of the physical mesh named `name` among its entity's (-1: none).
  public static func PartOf(entity: ref<Entity>, name: CName) -> Int32 {
    if !IsDefined(entity) {
      return -1;
    }
    let index = 0;
    let first = -1;
    for component in entity.GetComponents() {
      let mesh = component as PhysicalMeshComponent;
      if IsDefined(mesh) {
        if Equals(mesh.GetName(), name) {
          return index;
        }
        if first < 0 {
          first = index;
        }
        index += 1;
      }
    }
    // A touch that named no mesh of its own (its collider): its first.
    return first;
  }

  private func Find(entity: EntityID) -> ref<Entity> {
    return GameInstance.FindEntityByID(this.GetGameInstance(), entity);
  }

  // ---- touches ----

  private func Touched(point: TandemStreamedPoint, ours: ref<PlayerPuppet>, here: Vector4, owns: Bool, now: Float) -> Void {
    let at = new Vector4(point.x, point.y, point.z, 1.0);
    if Vector4.Distance(here, at) > TandemPropSync.Range() {
      return;
    }
    let entity = this.Find(point.entity);
    let part = TandemPropSync.PartOf(entity, point.name);
    if part < 0 || this.Following(point.entity, part) || TandemObjects.PropTheirs(point.entity, part) {
      return;
    }
    let body = TandemPropSync.Body(entity, part);
    if !IsDefined(body) || !body.IsSimulated() {
      return;
    }
    this.m_touched += 1;
    if owns {
      this.Start(point.entity, part, body, now);
      return;
    }
    // Not ours to simulate: our own touch is a push to the owner; any other
    // waits for its word.
    if this.Kicked(point.entity, part) {
      return;
    }
    let car = GetMountedVehicle(ours);
    let byUs = point.other == ours.GetEntityID() || (IsDefined(car) && point.other == car.GetEntityID());
    this.Kick(point.entity, part, body, byUs, now);
  }

  private func Following(entity: EntityID, part: Int32) -> Bool {
    for motion in this.m_moving {
      if motion.entity == entity && motion.part == part {
        return true;
      }
    }
    return false;
  }

  private func Kicked(entity: EntityID, part: Int32) -> Bool {
    for kick in this.m_kicks {
      if kick.entity == entity && kick.part == part {
        return true;
      }
    }
    return false;
  }

  // Ours: followed until it rests.
  private func Start(entity: EntityID, part: Int32, body: ref<PhysicalBodyInterface>, now: Float) -> Void {
    if ArraySize(this.m_moving) >= TandemPropSync.MaxMoving() {
      this.m_full += 1;
      return;
    }
    let motion = new TandemPropMotion();
    motion.entity = entity;
    motion.part = part;
    motion.body = body;
    let pose = body.GetTransform();
    motion.last = pose.position;
    motion.still = pose.position;
    motion.stillAt = now;
    motion.readAt = now;
    ArrayPush(this.m_moving, motion);
    this.m_followed += 1;
    this.LogSome(s"props: \(TandemNative.EntityHex(entity)) part \(part) moves here: ours to say");
  }

  private func Kick(entity: EntityID, part: Int32, body: ref<PhysicalBodyInterface>, ours: Bool, now: Float) -> Void {
    if ArraySize(this.m_kicks) >= TandemPropSync.MaxMoving() {
      this.m_full += 1;
      return;
    }
    let kick = new TandemPropKick();
    kick.entity = entity;
    kick.part = part;
    kick.body = body;
    kick.from = body.GetTransform();
    kick.at = now;
    kick.ours = ours;
    ArrayPush(this.m_kicks, kick);
  }

  // ---- ours, moving ----

  private func Follow(here: Vector4, now: Float) -> Void {
    if ArraySize(this.m_moving) == 0 {
      return;
    }
    let round = now - this.m_roundAt >= TandemPropSync.RoundTime() || now < this.m_roundAt;
    let still: array<ref<TandemPropMotion>>;
    for motion in this.m_moving {
      let pose = motion.body.GetTransform();
      let dt = now - motion.readAt;
      let velocity = new Vector4(0.0, 0.0, 0.0, 0.0);
      if dt > 0.0 {
        velocity = (pose.position - motion.last) / dt;
      }
      motion.last = pose.position;
      motion.readAt = now;
      if Vector4.Distance(pose.position, motion.still) > TandemPropSync.RestMove() {
        motion.still = pose.position;
        motion.stillAt = now;
      }
      if now - motion.stillAt >= TandemPropSync.RestTime() {
        TandemObjects.PropRested(motion.entity, motion.part, pose.position, pose.orientation);
        this.m_rested += 1;
        this.LogSome(s"props: \(TandemNative.EntityHex(motion.entity)) part \(motion.part) at rest at (\(pose.position.X) \(pose.position.Y) \(pose.position.Z)): told");
      } else {
        if round {
          TandemObjects.PropMoving(motion.entity, motion.part, pose.position, pose.orientation, velocity);
        }
        ArrayPush(still, motion);
      }
    }
    this.m_moving = still;
    if round {
      this.m_roundAt = now;
      TandemObjects.PropsRound(here);
    }
  }

  // ---- theirs ----

  private func HeldIndex(entity: EntityID, part: Int32) -> Int32 {
    let i = 0;
    while i < ArraySize(this.m_held) {
      if this.m_held[i].entity == entity && this.m_held[i].part == part {
        return i;
      }
      i += 1;
    }
    return -1;
  }

  // Another game moves a prop: our instance is held to its word.
  private func Hold(event: TandemPropEvent) -> Void {
    let entity = this.Find(event.entity);
    let body = TandemPropSync.Body(entity, event.part);
    if !IsDefined(body) {
      // Not in our world (far, not streamed in): its rest comes with the record.
      if IsDefined(entity) {
        TandemDegraded.Fallback("prop not held", s"prop \(TandemNative.EntityHex(event.entity)) part \(event.part) of player \(event.player): no physical body of that index here");
      }
      return;
    }
    // Ours stops speaking for it (two games moved it at once: the first won).
    this.StopFollowing(event.entity, event.part);
    this.Answered(event.entity, event.part);
    if this.HeldIndex(event.entity, event.part) >= 0 {
      return;
    }
    if ArraySize(this.m_held) >= TandemPropSync.MaxMoving() {
      this.m_full += 1;
      return;
    }
    let held = new TandemPropHeld();
    held.entity = event.entity;
    held.part = event.part;
    held.owner = event.player;
    held.body = body;
    body.SetIsKinematic(true);
    ArrayPush(this.m_held, held);
    this.m_heldCount += 1;
    this.LogSome(s"props: player \(event.player) moves \(TandemNative.EntityHex(event.entity)) part \(event.part): held to its word");
  }

  private func StopFollowing(entity: EntityID, part: Int32) -> Void {
    let still: array<ref<TandemPropMotion>>;
    for motion in this.m_moving {
      if !(motion.entity == entity && motion.part == part) {
        ArrayPush(still, motion);
      }
    }
    this.m_moving = still;
  }

  // The owner's word came for a prop we touched: nothing to put back.
  private func Answered(entity: EntityID, part: Int32) -> Void {
    let still: array<ref<TandemPropKick>>;
    for kick in this.m_kicks {
      if !(kick.entity == entity && kick.part == part) {
        ArrayPush(still, kick);
      }
    }
    this.m_kicks = still;
  }

  private func Draw() -> Void {
    if ArraySize(this.m_held) == 0 {
      return;
    }
    let still: array<ref<TandemPropHeld>>;
    for held in this.m_held {
      let pose = TandemObjects.PropDrawn(held.entity, held.part);
      if pose.found {
        let where = new Transform(new Vector4(pose.x, pose.y, pose.z, 1.0), new Quaternion(pose.qi, pose.qj, pose.qk, pose.qr));
        held.body.SetTransform(where);
        ArrayPush(still, held);
      } else {
        // Its game went quiet with no rest (it left): ours again.
        held.body.SetIsKinematic(false);
        this.LogSome(s"props: \(TandemNative.EntityHex(held.entity)) part \(held.part): player \(held.owner) said nothing more: back to our physics");
      }
    }
    this.m_held = still;
  }

  // Where another game's prop came to rest (or the record's, `checked`).
  private func Rest(event: TandemPropEvent) -> Void {
    let rest = new Transform(new Vector4(event.x, event.y, event.z, 1.0), new Quaternion(event.qi, event.qj, event.qk, event.qr));
    let index = this.HeldIndex(event.entity, event.part);
    let body: ref<PhysicalBodyInterface>;
    if index >= 0 {
      body = this.m_held[index].body;
      ArrayErase(this.m_held, index);
    } else {
      body = TandemPropSync.Body(this.Find(event.entity), event.part);
    }
    this.StopFollowing(event.entity, event.part);
    this.Answered(event.entity, event.part);
    if !IsDefined(body) {
      // Not in our world: put there when it is (Check).
      return;
    }
    body.SetIsKinematic(true);
    body.SetTransform(rest);
    body.SetIsKinematic(false);
    this.m_placed += 1;
    this.LogSome(s"props: player \(event.player)'s \(TandemNative.EntityHex(event.entity)) part \(event.part) at rest at (\(event.x) \(event.y) \(event.z)): put there");
  }

  // Another V pushed a prop our game runs: ours gives it that speed.
  private func Give(event: TandemPropEvent) -> Void {
    let body = TandemPropSync.Body(this.Find(event.entity), event.part);
    if !IsDefined(body) {
      return;
    }
    // Moving already: our puppet of that V pushed it here.
    if this.Following(event.entity, event.part) {
      return;
    }
    let velocity = new Vector4(event.vx, event.vy, event.vz, 0.0);
    body.SetIsKinematic(false);
    if !TandemObjects.SetBodyVelocity(body, velocity) {
      TandemDegraded.State("prop velocity", "a body's velocity setter is not there: pushes are given as an impulse of a nominal mass");
      body.AddLinearImpulse(velocity * TandemPropSync.Mass(), true);
    }
    this.m_given += 1;
    this.Start(event.entity, event.part, body, EngineTime.ToFloat(GameInstance.GetEngineTime(this.GetGameInstance())));
    this.LogSome(s"props: player \(event.player) pushed \(TandemNative.EntityHex(event.entity)) part \(event.part): given here");
  }

  // Our touches on others' props: our V's push told (its speed the tick
  // after), and put back where it stood if the owner said nothing.
  private func Answer(now: Float) -> Void {
    if ArraySize(this.m_kicks) == 0 {
      return;
    }
    let still: array<ref<TandemPropKick>>;
    for kick in this.m_kicks {
      if kick.ours && !kick.told && now > kick.at {
        let pose = kick.body.GetTransform();
        let velocity = (pose.position - kick.from.position) / (now - kick.at);
        TandemObjects.PropPushed(kick.entity, kick.part, kick.from.position, velocity);
        kick.told = true;
        this.m_kicked += 1;
        this.LogSome(s"props: our V pushed \(TandemNative.EntityHex(kick.entity)) part \(kick.part), \(Vector4.Length(velocity)) m/s: asked its owner");
      }
      if now - kick.at >= TandemPropSync.AnswerTime() {
        kick.body.SetIsKinematic(true);
        kick.body.SetTransform(kick.from);
        kick.body.SetIsKinematic(false);
        this.m_putBack += 1;
        TandemDegraded.Fallback("prop put back", s"prop \(TandemNative.EntityHex(kick.entity)) part \(kick.part): the game that runs our area said nothing of it in 2 s: put back where it stood");
      } else {
        ArrayPush(still, kick);
      }
    }
    this.m_kicks = still;
  }

  // Once a second: the record's rests round our V, where our world put
  // the prop elsewhere (a late joiner, a prop moved before we came).
  private func Check(here: Vector4) -> Void {
    for rest in TandemObjects.PropsAround(here, TandemPropSync.Reach()) {
      let busy = this.Following(rest.entity, rest.part) || this.HeldIndex(rest.entity, rest.part) >= 0 || TandemObjects.PropTheirs(rest.entity, rest.part);
      let body = busy ? null : TandemPropSync.Body(this.Find(rest.entity), rest.part);
      if IsDefined(body) {
        let pose = body.GetTransform();
        let at = new Vector4(rest.x, rest.y, rest.z, 1.0);
        if Vector4.Distance(pose.position, at) > TandemPropSync.Off() {
          this.Rest(rest);
        }
      }
    }
  }

  private func ReleaseAll() -> Void {
    for held in this.m_held {
      held.body.SetIsKinematic(false);
    }
    ArrayClear(this.m_held);
    ArrayClear(this.m_moving);
    ArrayClear(this.m_kicks);
  }

  // ---- commands (tools/game.sh cmd props ...) ----

  public func Command(command: String) -> Void {
    let game = this.GetGameInstance();
    let ours = GetPlayer(game);
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "") {
      TandemNative.Log(TandemObjects.PropsReport());
      TandemNative.Log(s"props: here: \(this.m_touched) touch(es), \(this.m_followed) followed, \(this.m_rested) rest(s) told, \(this.m_heldCount) held, \(this.m_placed) put at rest, \(this.m_kicked) push(es) asked, \(this.m_given) given, \(this.m_putBack) put back, \(this.m_full) past the cap; now \(ArraySize(this.m_moving)) ours moving, \(ArraySize(this.m_held)) held");
      return;
    }
    if !IsDefined(ours) {
      return;
    }
    if Equals(verb, "at") {
      let hex = ArraySize(words) > 2 ? words[2] : "";
      TandemNative.Log(s"props: \(hex) \(TandemPropSync.Where(game, TandemObjects.EntityFromHex(hex)))");
      return;
    }
    let object = GameInstance.GetTargetingSystem(game).GetLookAtObject(ours, true, true);
    let body = IsDefined(object) ? TandemPropSync.Body(object, 0) : null;
    if !IsDefined(body) || !EntityID.IsStatic(object.GetEntityID()) {
      TandemNative.Log("props: V looks at no placed entity with a physical mesh");
      return;
    }
    let id = object.GetEntityID();
    if Equals(verb, "look") {
      let pose = body.GetTransform();
      TandemNative.Log(s"props: looks at \(TandemNative.EntityHex(id)) (\(NameToString(object.GetClassName()))) part 0 at \(pose.position.X) \(pose.position.Y) \(pose.position.Z) simulated \(body.IsSimulated()) kinematic \(body.IsKinematic())");
    } else if Equals(verb, "push") {
      let speed = ArraySize(words) > 2 ? StringToFloat(words[2]) : TandemPropSync.PushSpeed();
      let way = Vector4.Normalize(object.GetWorldPosition() - ours.GetWorldPosition());
      let velocity = new Vector4(way.X * speed, way.Y * speed, 1.0, 0.0);
      let now = EngineTime.ToFloat(GameInstance.GetEngineTime(game));
      // As our V's touch: ours to move, else a push to the owner.
      if TandemArea.Owns() || this.m_you == 0u {
        body.SetIsKinematic(false);
        if !TandemObjects.SetBodyVelocity(body, velocity) {
          body.AddLinearImpulse(velocity * TandemPropSync.Mass(), true);
        }
        if this.m_you != 0u {
          this.Start(id, 0, body, now);
        }
        TandemNative.Log(s"props: pushed \(TandemNative.EntityHex(id)) at \(speed) m/s: ours");
      } else {
        this.Kick(id, 0, body, true, now);
        body.SetIsKinematic(false);
        if !TandemObjects.SetBodyVelocity(body, velocity) {
          body.AddLinearImpulse(velocity * TandemPropSync.Mass(), true);
        }
        TandemNative.Log(s"props: pushed \(TandemNative.EntityHex(id)) at \(speed) m/s: another game runs our area");
      }
    } else {
      TandemNative.Log(s"props: unknown command: \(command) (props, props look, props push [M/S], props at HEX)");
    }
  }

  // Where a prop is in our world: its body's place, its entity's, how it
  // stands (test query prop HEX; props at HEX).
  public static func Where(game: GameInstance, id: EntityID) -> String {
    let sync = TandemPropSync.Get(game);
    let entity = GameInstance.FindEntityByID(game, id);
    let body = TandemPropSync.Body(entity, 0);
    if !IsDefined(body) {
      return "none";
    }
    let pose = body.GetTransform();
    let how = "still";
    if TandemObjects.PropTheirs(id, 0) {
      how = "held";
    } else if IsDefined(sync) && sync.Following(id, 0) {
      how = "moving";
    }
    let at = entity.GetWorldPosition();
    return s"\(FloatToStringPrec(pose.position.X, 2)) \(FloatToStringPrec(pose.position.Y, 2)) \(FloatToStringPrec(pose.position.Z, 2)) \(how) kinematic \(body.IsKinematic()) entity \(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2))";
  }
}

// A placed entity's physics body touched something (the engine's own
// notice, PhysicalCollisionEvent), on whatever thread: a read of the event
// and a queue, nothing else; Props.reds acts on the tick.
public abstract class TandemPropTouch {
  // Below this the touch is a body settling, not a push (the event's
  // impulse, as the engine gives it).
  public static func MinImpulse() -> Float { return 1.0; }

  public static func Queue(object: ref<GameObject>, evt: ref<PhysicalCollisionEvent>) -> Void {
    if !TandemNative.IsStarted() || !IsDefined(evt) {
      return;
    }
    let id = object.GetEntityID();
    // Only a placed prop has the same name in every game.
    if !EntityID.IsStatic(id) || evt.impulse < TandemPropTouch.MinImpulse() {
      return;
    }
    let other = evt.otherEntity as Entity;
    let otherId = IsDefined(other) ? other.GetEntityID() : EntityID.FromHash(0ul);
    let mine = evt.myComponent as IComponent;
    let name = IsDefined(mine) ? mine.GetName() : n"None";
    TandemStreaming.QueuePoint(TandemPointKind.PropTouch(), id, otherId, name, evt.worldPosition, Cast<Int32>(evt.impulse * 100.0));
  }
}

@addMethod(GameObject)
protected cb func OnPhysicalCollisionEvent(evt: ref<PhysicalCollisionEvent>) -> Bool {
  TandemPropTouch.Queue(this, evt);
  return false;
}
