// A V's remote view (docs/architecture/remote-views.md; engine facts:
// docs/research/engine/devices.md §13.182): its camera leaves its body for
// something it controls, the body standing where it was.
//
// - What: the takeover system's controlled object (a security camera, a
//   chain of them, a turret, a sniper nest or another device the takeover
//   holds: TakeOverControlSystem.GetControlledObject), or the car the
//   player state machine says it drives remotely (EntityIDVehicleRemote-
//   Controlled, the take control quickhack). Read on the game thread.
// - Said: as our Activity (kind 9, Activities.reds SampleOurs): its sort,
//   the object, where the view is (again each 8 m a car drives), the shared
//   vehicle it drives. The host links us where we look from too, and gives
//   us a shared car we drive (regions.md, traffic.md); time bubbles centre
//   on the view (Time.reds); the partners' party HUD says it (PartyHud.reds).
// - A car another V drives or drives remotely, or a partner's car, is
//   refused at the takeover (Hacks.reds, read and queue only): told here,
//   on the tick, with who drives it.
// - What the others cannot follow is registered (Degraded): a player's car
//   driven remotely (remote car unseen), a device with no id every game
//   knows (held device unshared).
//
// tools/game.sh cmd remote (ours, and the others') | remote camera (our V
// takes the nearest camera, as its control gives it) | remote turret |
// remote car (the take control quickhack's own event on the nearest
// vehicle) | remote push SPEED (our remote car pushed ahead, m/s) | remote
// off (back to our V).

// Our V's remote view as read now.
public class TandemRemoteRead {
  public let on: Bool;
  // RemoteSort: 0 other, 1 camera, 2 turret, 3 vehicle, 4 another device.
  public let sort: Int32;
  public let object: wref<GameObject>;
  public let view: Vector4;
  // The shared vehicle it drives (0: none, or not shared).
  public let traffic: Uint32;
  // The quest holds the takeover (TakeOverControlSystem.IsInputLockedFromQuest).
  public let quest: Bool;
}

public class TandemRemoteView {
  private let m_game: GameInstance;
  // What we last read: its sort, the object, and the fallbacks said.
  private let m_sort: Int32;
  private let m_object: EntityID;
  private let m_unseen: EntityID;
  private let m_unshared: EntityID;
  private let m_refusals: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_sort = -1;
  }

  public static func Of(game: GameInstance) -> ref<TandemRemoteView> {
    let activities = TandemActivities.Of(game);
    return IsDefined(activities) ? activities.Remote() : null;
  }

  // ---- ours ----

  public static func Ours(game: GameInstance) -> ref<TandemRemoteRead> {
    let out = new TandemRemoteRead();
    out.sort = -1;
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return out;
    }
    let control = GameInstance.GetScriptableSystemsContainer(game).Get(n"TakeOverControlSystem") as TakeOverControlSystem;
    let held = IsDefined(control) ? control.GetControlledObject() : null;
    if IsDefined(held) {
      out.on = true;
      out.object = held;
      out.sort = IsDefined(held as SurveillanceCamera) ? 1 : (IsDefined(held as SecurityTurret) ? 2 : 4);
      out.view = held.GetWorldPosition();
      out.quest = control.IsInputLockedFromQuest();
      return out;
    }
    let car = TandemRemoteView.OurCar(game, player);
    if IsDefined(car) {
      out.on = true;
      out.sort = 3;
      out.object = car;
      out.view = car.GetWorldPosition();
      let traffic = TandemTraffic.Of(game);
      out.traffic = IsDefined(traffic) ? traffic.IdOfBody(car.GetEntityID()) : 0u;
    }
    return out;
  }

  // The car our V drives remotely (none: none).
  public static func OurCar(game: GameInstance, player: ref<PlayerPuppet>) -> ref<VehicleObject> {
    let psm = IsDefined(player) ? player.GetPlayerStateMachineBlackboard() : null;
    if !IsDefined(psm) {
      return null;
    }
    let id = psm.GetEntityID(GetAllBlackboardDefs().PlayerStateMachine.EntityIDVehicleRemoteControlled);
    if !EntityID.IsDefined(id) {
      return null;
    }
    let car: wref<VehicleObject>;
    if !VehicleComponent.GetVehicleFromID(game, id, car) || !IsDefined(car) || !car.IsVehicleRemoteControlled() {
      return null;
    }
    return car;
  }

  // Where our V's time bubble is (Time.reds): its remote view while it has
  // one (what it sees and acts on), else its body.
  public static func OurCentre(game: GameInstance, body: Vector4) -> Vector4 {
    let ours = TandemRemoteView.Ours(game);
    return ours.on ? ours.view : body;
  }

  // The takeover's own NoCameraControl is our V's remote view, not the
  // story's body mode (Spectate.reds: no scripted moment of it), unless a
  // quest holds the takeover.
  public static func IsOwnRestriction(game: GameInstance, record: wref<StatusEffect_Record>) -> Bool {
    if !IsDefined(record) || record.GetID() != t"GameplayRestriction.NoCameraControl" {
      return false;
    }
    let ours = TandemRemoteView.Ours(game);
    return ours.on && ours.sort != 3 && !ours.quest;
  }

  // ---- the others' ----

  // Another player's remote view: its sort (-1: none).
  public static func SortOf(player: Uint32) -> Int32 {
    for busy in TandemActivity.Others() {
      if busy.player == player && busy.kind == 9 {
        return busy.entry;
      }
    }
    return -1;
  }

  // Where it is (W 0: none, or not said).
  public static func ViewOf(player: Uint32) -> Vector4 {
    for busy in TandemActivity.Others() {
      if busy.player == player && busy.kind == 9 && (busy.viewX != 0.0 || busy.viewY != 0.0 || busy.viewZ != 0.0) {
        return new Vector4(busy.viewX, busy.viewY, busy.viewZ, 1.0);
      }
    }
    return new Vector4(0.0, 0.0, 0.0, 0.0);
  }

  // Where another V's time bubble is: its view while it has one, else
  // `body` (its V's place).
  public static func CentreOf(player: Uint32, body: Vector4) -> Vector4 {
    let view = TandemRemoteView.ViewOf(player);
    return view.W != 0.0 ? view : body;
  }

  // Who else drives this shared vehicle remotely (0: nobody).
  public static func RemoteDriverOf(traffic: Uint32) -> Uint32 {
    if traffic == 0u {
      return 0u;
    }
    for busy in TandemActivity.Others() {
      if busy.kind == 9 && busy.traffic == traffic {
        return busy.player;
      }
    }
    return 0u;
  }

  // The player whose car this is, or who drives it, remotely or at its
  // wheel (0: ours to take). From the takeover's hook: read only.
  public static func TakenBy(game: GameInstance, car: ref<VehicleObject>) -> Uint32 {
    let system = TandemSystem.Of(game);
    if !IsDefined(car) || !IsDefined(system) || system.You() == 0u {
      return 0u;
    }
    let owner = system.CopyOwnerOf(car.GetEntityID());
    if owner != 0u {
      return owner;
    }
    let traffic = TandemTraffic.Of(game);
    return IsDefined(traffic) ? TandemRemoteView.RemoteDriverOf(traffic.IdOfBody(car.GetEntityID())) : 0u;
  }

  // (test) TestQuery.reds "remote [player N]": "SORT X Y Z TRAFFIC", none.
  public static func Query(game: GameInstance, player: Uint32) -> String {
    let sort = -1;
    let at = new Vector4(0.0, 0.0, 0.0, 0.0);
    let traffic = 0u;
    if player == 0u {
      let ours = TandemRemoteView.Ours(game);
      if ours.on {
        sort = ours.sort;
        at = ours.view;
        traffic = ours.traffic;
      }
    } else {
      for busy in TandemActivity.Others() {
        if busy.player == player && busy.kind == 9 {
          sort = busy.entry;
          at = new Vector4(busy.viewX, busy.viewY, busy.viewZ, 1.0);
          traffic = busy.traffic;
        }
      }
    }
    if sort < 0 {
      return "none";
    }
    let names = ["other", "camera", "turret", "vehicle", "device"];
    let name = sort < ArraySize(names) ? names[sort] : "other";
    return s"\(name) \(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2)) \(traffic)";
  }

  // ---- the tick (Activities.reds, four times a second) ----

  // What our V looks through now, logged as it changes; what cannot be
  // shared registered; refused takeovers told.
  public func Sample() -> ref<TandemRemoteRead> {
    let ours = TandemRemoteView.Ours(this.m_game);
    let id = ours.on && IsDefined(ours.object) ? ours.object.GetEntityID() : EntityID.FromHash(0ul);
    if ours.sort != this.m_sort || id != this.m_object {
      if ours.on {
        let player = GetPlayer(this.m_game);
        let body = IsDefined(player) ? player.GetWorldPosition() : ours.view;
        TandemNative.Log(s"remote: our V \(TandemRemoteView.SortName(ours.sort)) \(TandemNative.EntityHex(id)), \(Vector4.Distance(body, ours.view)) m from its body\(ours.traffic != 0u ? s", shared vehicle \(ours.traffic)" : "")\(ours.quest ? ", the quest's" : "")");
      } else if this.m_sort >= 0 {
        TandemNative.Log("remote: our V is back in its body");
      }
      this.m_sort = ours.sort;
      this.m_object = id;
    }
    if ours.on && ours.sort == 3 && ours.traffic == 0u && id != this.m_unseen && this.Unshareable(ours.object as VehicleObject) {
      this.m_unseen = id;
      TandemDegraded.Fallback("remote car unseen", s"our V drives a player's car remotely (\(TandemNative.EntityHex(id))): the others see it where it was left");
    }
    // A camera's and a turret's aim travel (Turrets.reds, TurretHands) when
    // every game knows them by their static id; another device's does not.
    if ours.on && ours.sort != 3 && id != this.m_unshared && (ours.sort == 4 || !EntityID.IsStatic(id)) {
      this.m_unshared = id;
      let why = ours.sort == 4 ? "a device that is neither a camera nor a turret" : "a device with a dynamic id";
      TandemDegraded.Fallback("held device unshared", s"our V holds \(why) (\(TandemNative.EntityHex(id))): its copies do not turn with it");
    }
    for refused in TandemStreaming.TakePoints(TandemPointKind.RemoteRefused()) {
      this.m_refusals += 1;
      let player = Cast<Uint32>(refused.value);
      TandemNative.Log(s"remote: our V's takeover of \(TandemNative.EntityHex(refused.entity)) refused: player \(player) drives it, or it is their car");
      let system = TandemSystem.Of(this.m_game);
      let theirs = IsDefined(system) && system.CopyOwnerOf(refused.entity) == player && system.CarOwnerOf(refused.entity) == 0u;
      TandemText.Notice(this.m_game, theirs ? "Remote-CarTheirs" : "Remote-CarBusy", TandemCoop.NameOf(player));
    }
    return ours;
  }

  // A car of a player's (our garage's, one our V took, a partner's copy):
  // never a shared world vehicle, so its remote drive is ours alone.
  private func Unshareable(car: ref<VehicleObject>) -> Bool {
    if !IsDefined(car) {
      return false;
    }
    let traffic = TandemTraffic.Of(this.m_game);
    return car.IsPlayerVehicle() || IsDefined(traffic) && traffic.Taken(car.GetEntityID());
  }

  public static func SortName(sort: Int32) -> String {
    switch sort {
      case 1:
        return "looks through camera";
      case 2:
        return "holds turret";
      case 3:
        return "drives remotely";
      case 4:
        return "holds device";
      default:
        return "looks through";
    }
  }

  // The party HUD's words for a partner's remote view (PartyHud.reds).
  public static func PartyKey(sort: Int32) -> String {
    switch sort {
      case 1:
        return "Party-Camera";
      case 2:
        return "Party-Turret";
      case 3:
        return "Party-RemoteCar";
      default:
        return "Party-Remote";
    }
  }

  // ---- commands (tools/game.sh cmd remote ...) ----

  public func Command(command: String) -> Void {
    let game = this.m_game;
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    let player = GetPlayer(game);
    if !IsDefined(player) {
      TandemNative.Log("remote: no V");
      return;
    }
    if Equals(verb, "") {
      let ours = TandemRemoteView.Ours(game);
      let id = ours.on && IsDefined(ours.object) ? ours.object.GetEntityID() : EntityID.FromHash(0ul);
      let body = player.GetWorldPosition();
      let line = ours.on ? s"\(TandemRemoteView.SortName(ours.sort)) \(TandemNative.EntityHex(id)) at \(ours.view.X) \(ours.view.Y) \(ours.view.Z), \(Vector4.Distance(body, ours.view)) m from its body, shared vehicle \(ours.traffic)" : "none";
      let others = "";
      for busy in TandemActivity.Others() {
        if busy.kind == 9 {
          others += s" player \(busy.player): sort \(busy.entry) at \(busy.viewX) \(busy.viewY) \(busy.viewZ), vehicle \(busy.traffic);";
        }
      }
      TandemNative.Log(s"remote: ours \(line); others:\(StrLen(others) > 0 ? others : " none"); takeovers refused \(this.m_refusals)");
    } else if Equals(verb, "camera") || Equals(verb, "turret") {
      let devices = TandemDevices.Get(game);
      let device = IsDefined(devices) ? devices.NearestOf(verb, 60.0) : null;
      if !IsDefined(device) {
        TandemNative.Log(s"remote: no \(verb) within 60 m");
        return;
      }
      // As its control panel gives it (TakeOverControlSystem.RequestTakeControl).
      let take = new ToggleTakeOverControl();
      take.SetProperties(false, false);
      TakeOverControlSystem.RequestTakeControl(device, take);
      TandemNative.Log(s"remote: our V takes control of \(verb) \(TandemNative.EntityHex(device.GetEntityID()))");
    } else if Equals(verb, "car") {
      let traffic = TandemTraffic.Of(game);
      let car = IsDefined(traffic) ? traffic.NearestBody(player.GetWorldPosition(), 40.0) : null;
      let ps = IsDefined(car) ? car.GetVehiclePS() : null;
      if !IsDefined(ps) {
        TandemNative.Log("remote: no vehicle within 40 m");
        return;
      }
      // The quickhack's own action, to the car's state (its takeover hook
      // runs: VehicleComponentPS.OnToggleTakeOverControl).
      let take = new ToggleTakeOverControl();
      take.SetProperties(false, true);
      take.SetExecutor(player);
      GameInstance.GetPersistencySystem(game).QueuePSEvent(ps.GetID(), ps.GetClassName(), take);
      let id = IsDefined(traffic) ? traffic.IdOfBody(car.GetEntityID()) : 0u;
      TandemNative.Log(s"remote: our V takes control of vehicle \(TandemNative.EntityHex(car.GetEntityID())) (shared vehicle \(id))");
    } else if Equals(verb, "push") {
      let car = TandemRemoteView.OurCar(game, player);
      if !IsDefined(car) {
        TandemNative.Log("remote: our V drives no car remotely");
        return;
      }
      let speed = ArraySize(words) > 2 ? StringToFloat(words[2]) : 8.0;
      let forward = car.GetWorldForward();
      let at = car.GetWorldPosition();
      let mass = car.GetTotalMass();
      car.PhysicsWakeUp();
      let impulse = new PhysicalImpulseEvent();
      impulse.radius = 1.0;
      impulse.worldPosition.X = at.X;
      impulse.worldPosition.Y = at.Y;
      impulse.worldPosition.Z = at.Z + 0.5;
      impulse.worldImpulse.X = forward.X * speed * mass;
      impulse.worldImpulse.Y = forward.Y * speed * mass;
      impulse.worldImpulse.Z = 0.0;
      car.QueueEvent(impulse);
      TandemNative.Log(s"remote: our remote car pushed ahead at \(speed) m/s from \(at.X) \(at.Y) \(at.Z)");
    } else if Equals(verb, "off") {
      TakeOverControlSystem.ReleaseControlOfRemoteControlledVehicle(game, player.GetEntityID());
      TakeOverControlSystem.ReleaseControl(game);
      TandemNative.Log("remote: our V lets go (back to its body)");
    } else {
      TandemNative.Log(s"remote: unknown command \(command)");
    }
  }
}
