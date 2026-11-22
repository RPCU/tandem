// (experiment) Can a vehicle we do not simulate be moved where another game
// says it is? Spawns a car ahead of V and moves it every frame along a
// circle, logging how far it is from where it was told to be and the speed
// the game reads on it. The path: teleport (the teleportation facility),
// kinematic (the engine's kinematic state and move: TandemWheels.Hold, Move)
// or velocity (the move alone, on a simulated body); still: kept where it
// spawned by the kinematic path (V can sit in it: vehicle probe ride). The
// kinematic and still paths need tandem.ini vehicle_move = kinematic.
// tools/game.sh cmd vehicle probe [record] [teleport|kinematic|velocity|still] | vehicle probe ride | vehicle probe stop

public class TandemVehicleProbe {
  private let m_game: GameInstance;
  private let m_id: EntityID;
  private let m_center: Vector4;
  private let m_start: Float;
  private let m_lastLog: Float;
  private let m_last: Vector4;
  private let m_haveLast: Bool;
  private let m_frames: Int32;
  private let m_maxError: Float;
  private let m_path: String;
  private let m_prev: Float;
  private let m_speedSum: Float;

  public static func Radius() -> Float {
    return 6.0;
  }

  public static func Speed() -> Float {
    return 5.0;
  }

  public func Start(game: GameInstance, record: String, path: String) -> Void {
    this.m_game = game;
    this.m_path = path;
    let player = GetPlayer(game);
    let forward = player.GetWorldForward();
    this.m_center = player.GetWorldPosition();
    this.m_center.X += forward.X * 10.0;
    this.m_center.Y += forward.Y * 10.0;
    let spec = new DynamicEntitySpec();
    spec.recordID = TDBID.Create(record);
    spec.position = this.Target(0.0);
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Probe");
    this.m_id = TandemEntities.Create(spec);
    this.m_start = this.Now();
    this.m_lastLog = this.m_start;
    TandemNative.Log(s"vehicle probe: \(record) spawning (\(EntityID.IsDefined(this.m_id))), path \(path)");
  }

  public func Stop() -> Void {
    if EntityID.IsDefined(this.m_id) {
      let vehicle = GameInstance.GetDynamicEntitySystem().GetEntity(this.m_id) as VehicleObject;
      if IsDefined(vehicle) {
        TandemWheels.Hold(vehicle, false);
      }
      TandemEntities.Delete(this.m_id);
    }
    TandemNative.Log("vehicle probe: stopped");
  }

  public func Body() -> ref<VehicleObject> {
    return GameInstance.GetDynamicEntitySystem().GetEntity(this.m_id) as VehicleObject;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  private func Target(t: Float) -> Vector4 {
    let angle = Equals(this.m_path, "still") ? 0.0 : t * TandemVehicleProbe.Speed() / TandemVehicleProbe.Radius();
    let p = this.m_center;
    p.X += TandemVehicleProbe.Radius() * CosF(angle);
    p.Y += TandemVehicleProbe.Radius() * SinF(angle);
    p.W = 1.0;
    return p;
  }

  public func Tick() -> Void {
    let vehicle = GameInstance.GetDynamicEntitySystem().GetEntity(this.m_id) as VehicleObject;
    if !IsDefined(vehicle) {
      return;
    }
    let t = this.Now() - this.m_start;
    let step = t - this.m_prev;
    if this.m_haveLast && step <= 0.0 {
      return;
    }
    this.m_prev = t;
    // Where the car was put last frame against where it is now: how much
    // its own physics moved it in between.
    if this.m_haveLast {
      let actual = vehicle.GetWorldPosition();
      let dx = actual.X - this.m_last.X;
      let dy = actual.Y - this.m_last.Y;
      let error = SqrtF(dx * dx + dy * dy);
      if error > this.m_maxError {
        this.m_maxError = error;
      }
      this.m_speedSum += Vector4.Length(vehicle.GetLinearVelocity());
    }
    let target = this.Target(t);
    let angle = Equals(this.m_path, "still") ? 0.0 : t * TandemVehicleProbe.Speed() / TandemVehicleProbe.Radius();
    // Facing along the circle: yaw in degrees, 0 = +Y.
    let heading = Rad2Deg(angle) + 180.0;
    let moved = false;
    if this.m_haveLast && !Equals(this.m_path, "teleport") {
      if !Equals(this.m_path, "velocity") {
        TandemWheels.Hold(vehicle, true);
      }
      moved = TandemWheels.Move(vehicle, target, EulerAngles.ToQuat(TandemYawAngles(heading)), ClampF(step, 0.005, 0.2));
    }
    if !moved {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(vehicle, target, TandemYawAngles(heading));
    }
    this.m_last = target;
    this.m_haveLast = true;
    this.m_frames += 1;
    if t - (this.m_lastLog - this.m_start) > 2.0 {
      this.m_lastLog = this.Now();
      let at = vehicle.GetWorldPosition();
      let speed = this.m_speedSum / Cast<Float>(Max(this.m_frames, 1));
      TandemNative.Log(s"vehicle probe: \(this.m_frames) frames, path \(this.m_path) (moved by the engine \(moved), held \(TandemWheels.Held(vehicle))), drift between frames up to \(this.m_maxError) m, at (\(at.X), \(at.Y), \(at.Z)), speed \(Vector4.Length(vehicle.GetLinearVelocity())) m/s (mean \(speed), asked \(Equals(this.m_path, "still") ? 0.0 : TandemVehicleProbe.Speed()))");
      this.m_frames = 0;
      this.m_maxError = 0.0;
      this.m_speedSum = 0.0;
    }
  }
}
