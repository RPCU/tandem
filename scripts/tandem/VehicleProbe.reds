// (experiment) Can a vehicle we do not simulate be moved where another game
// says it is? Spawns a car ahead of V and teleports it every frame along a
// circle, logging how far it is from where it was told to be.
// tools/game.sh cmd vehicle probe [record] | vehicle probe stop

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

  public static func Radius() -> Float {
    return 6.0;
  }

  public static func Speed() -> Float {
    return 5.0;
  }

  public func Start(game: GameInstance, record: String) -> Void {
    this.m_game = game;
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
    TandemNative.Log(s"vehicle probe: \(record) spawning (\(EntityID.IsDefined(this.m_id)))");
  }

  public func Stop() -> Void {
    if EntityID.IsDefined(this.m_id) {
      TandemEntities.Delete(this.m_id);
    }
    TandemNative.Log("vehicle probe: stopped");
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  private func Target(t: Float) -> Vector4 {
    let angle = t * TandemVehicleProbe.Speed() / TandemVehicleProbe.Radius();
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
    }
    let target = this.Target(t);
    let angle = t * TandemVehicleProbe.Speed() / TandemVehicleProbe.Radius();
    // Facing along the circle: yaw in degrees, 0 = +Y.
    let heading = Rad2Deg(angle) + 180.0;
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(vehicle, target, TandemYawAngles(heading));
    this.m_last = target;
    this.m_haveLast = true;
    this.m_frames += 1;
    if t - (this.m_lastLog - this.m_start) > 2.0 {
      this.m_lastLog = this.Now();
      let at = vehicle.GetWorldPosition();
      TandemNative.Log(s"vehicle probe: \(this.m_frames) frames, drift between frames up to \(this.m_maxError) m, at (\(at.X), \(at.Y), \(at.Z)), speed \(Vector4.Length(vehicle.GetLinearVelocity())) m/s");
      this.m_frames = 0;
      this.m_maxError = 0.0;
    }
  }
}
