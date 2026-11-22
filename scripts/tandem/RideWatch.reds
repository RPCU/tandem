// (test) A ride, measured every frame (vehicles.md, "How a copy is moved").
//
// The car our V sits in: how it moved from frame to frame, by where it is
// at the start of each tick (where it was drawn): its acceleration and the
// jerk, the change of that acceleration. A car driven smoothly stays under a
// few m/s²; a frame held and the next doubled (a teleport skipped) is a
// jolt of hundreds. On the driver's game the same numbers of its own car are
// the reference.
//
// A puppet seated in a car (on the driver's game, a riding partner's): where
// its body is in the car's frame (its entity's origin and its hips, along,
// across and up), against where it sits while the car is slow: a body drawn
// off its seat at speed shows as metres along.
//
// `vehicle ride watch` says both and starts over.
public class TandemRideWatch {
  // our ride: the last three places of the car and when
  private let m_car: EntityID;
  private let m_samples: Int32;
  private let m_p0: Vector4;
  private let m_p1: Vector4;
  private let m_t0: Float;
  private let m_t1: Float;
  private let m_lastAccel: Vector4;
  private let m_haveAccel: Bool;
  private let m_frames: Int32;
  private let m_accelSum: Float;
  private let m_accelMax: Float;
  private let m_jerkSum: Float;
  private let m_jerkMax: Float;
  private let m_over10: Int32;
  private let m_over50: Int32;
  private let m_over200: Int32;
  private let m_speedMax: Float;
  // a seated puppet's body in its car's frame
  private let m_seatCar: EntityID;
  private let m_seatAt: Vector4;
  private let m_seatTime: Float;
  private let m_restN: Int32;
  private let m_restBody: Vector4;
  private let m_restHips: Vector4;
  private let m_seatFrames: Int32;
  private let m_bodyOffMax: Float;
  private let m_hipsOffMax: Float;
  private let m_hipsAlongAtMax: Float;
  private let m_hipsSpeedAtMax: Float;
  private let m_hipsNow: Vector4;
  private let m_speedNow: Float;

  public func Tick(game: GameInstance, now: Float, puppets: array<ref<TandemPuppet>>) -> Void {
    this.Ride(game, now);
    for puppet in puppets {
      if EntityID.IsDefined(puppet.seatedIn) {
        this.Seat(game, now, puppet);
      }
    }
  }

  private func Ride(game: GameInstance, now: Float) -> Void {
    let player = GetPlayer(game);
    if !IsDefined(player) {
      return;
    }
    let ride = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithObjects(player);
    let car = GameInstance.FindEntityByID(game, ride.parentId) as VehicleObject;
    if !IsDefined(car) {
      this.m_samples = 0;
      return;
    }
    let at = car.GetWorldPosition();
    let same = car.GetEntityID() == this.m_car && this.m_samples > 0;
    if same && now - this.m_t1 < 0.001 {
      return; // the same frame
    }
    if !same || now - this.m_t1 > 0.2 {
      this.m_car = car.GetEntityID();
      this.m_samples = 1;
      this.m_p1 = at;
      this.m_t1 = now;
      this.m_haveAccel = false;
      return;
    }
    if this.m_samples >= 2 {
      let v1 = (this.m_p1 - this.m_p0) / (this.m_t1 - this.m_t0);
      let v2 = (at - this.m_p1) / (now - this.m_t1);
      let half = 0.5 * (now - this.m_t0);
      let accel = (v2 - v1) / half;
      let a = Vector4.Length(accel);
      this.m_frames += 1;
      this.m_accelSum += a * a;
      this.m_accelMax = MaxF(this.m_accelMax, a);
      this.m_speedMax = MaxF(this.m_speedMax, Vector4.Length(v2));
      if a > 10.0 { this.m_over10 += 1; }
      if a > 50.0 { this.m_over50 += 1; }
      if a > 200.0 { this.m_over200 += 1; }
      if this.m_haveAccel {
        let j = Vector4.Length(accel - this.m_lastAccel) / half;
        this.m_jerkSum += j * j;
        this.m_jerkMax = MaxF(this.m_jerkMax, j);
      }
      this.m_lastAccel = accel;
      this.m_haveAccel = true;
    }
    this.m_p0 = this.m_p1;
    this.m_t0 = this.m_t1;
    this.m_p1 = at;
    this.m_t1 = now;
    this.m_samples = Min(this.m_samples + 1, 2);
  }

  private func Seat(game: GameInstance, now: Float, puppet: ref<TandemPuppet>) -> Void {
    let car = GameInstance.FindEntityByID(game, puppet.seatedIn) as GameObject;
    let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as ScriptedPuppet;
    if !IsDefined(car) || !IsDefined(body) || !IsDefined(body.GetSlotComponent()) {
      return;
    }
    let hips: WorldTransform; // assigned on every path: read only when GetSlotTransform filled it
    if !body.GetSlotComponent().GetSlotTransform(n"Hips", hips) {
      return;
    }
    let at = car.GetWorldPosition();
    let speed = 0.0;
    if car.GetEntityID() == this.m_seatCar && now - this.m_seatTime > 0.001 && now - this.m_seatTime < 0.2 {
      speed = Vector4.Length(at - this.m_seatAt) / (now - this.m_seatTime);
    } else if car.GetEntityID() != this.m_seatCar {
      this.m_restN = 0;
    }
    this.m_seatCar = car.GetEntityID();
    this.m_seatAt = at;
    this.m_seatTime = now;
    let local = TandemVehicles.InCar(car, body.GetWorldPosition());
    let hipsLocal = TandemVehicles.InCar(car, WorldPosition.ToVector4(WorldTransform.GetWorldPosition(hips)));
    this.m_hipsNow = hipsLocal;
    this.m_speedNow = speed;
    if speed < 1.0 && this.m_restN < 30 {
      // where it sits: the mean of its first slow frames
      let n = Cast<Float>(this.m_restN);
      this.m_restBody = (this.m_restBody * n + local) / (n + 1.0);
      this.m_restHips = (this.m_restHips * n + hipsLocal) / (n + 1.0);
      this.m_restN += 1;
      return;
    }
    if this.m_restN == 0 {
      return;
    }
    this.m_seatFrames += 1;
    this.m_bodyOffMax = MaxF(this.m_bodyOffMax, Vector4.Length(local - this.m_restBody));
    let off = Vector4.Length(hipsLocal - this.m_restHips);
    if off > this.m_hipsOffMax {
      this.m_hipsOffMax = off;
      this.m_hipsAlongAtMax = hipsLocal.Y - this.m_restHips.Y;
      this.m_hipsSpeedAtMax = speed;
    }
  }

  // Where each seated puppet is, in its car's frame: the seat (the car's
  // own slot), the puppet's origin and its hips.
  public func Detail(game: GameInstance, puppets: array<ref<TandemPuppet>>) -> Void {
    let player = GetPlayer(game);
    if IsDefined(player) {
      let ride = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithObjects(player);
      let ours = GameInstance.FindEntityByID(game, ride.parentId) as GameObject;
      if IsDefined(ours) {
        let mine = TandemVehicles.SeatInCar(ours, ride.slotId.id);
        let me = TandemVehicles.InCar(ours, player.GetWorldPosition());
        TandemNative.Log(s"ride watch: our V in \(NameToString(ride.slotId.id)) of car \(EntityID.ToDebugStringDecimal(ride.parentId)): in the car's frame its seat \(mine.X) \(mine.Y) \(mine.Z) (found \(mine.W > 0.0)), our origin \(me.X) \(me.Y) \(me.Z)");
      }
    }
    for puppet in puppets {
      if EntityID.IsDefined(puppet.seatedIn) {
        let car = GameInstance.FindEntityByID(game, puppet.seatedIn) as GameObject;
        let body = GameInstance.GetDynamicEntitySystem().GetEntity(puppet.id) as ScriptedPuppet;
        if IsDefined(car) && IsDefined(body) {
          let hips: WorldTransform; // assigned on every path: read only when GetSlotTransform filled it
          let hipsAt = new Vector4(0.0, 0.0, 0.0, 0.0);
          if IsDefined(body.GetSlotComponent()) && body.GetSlotComponent().GetSlotTransform(n"Hips", hips) {
            hipsAt = TandemVehicles.InCar(car, WorldPosition.ToVector4(WorldTransform.GetWorldPosition(hips)));
          }
          let seatAt = TandemVehicles.SeatInCar(car, puppet.seatedSlot);
          let origin = TandemVehicles.InCar(car, body.GetWorldPosition());
          let at = car.GetWorldPosition();
          let mount = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithObjects(body);
          TandemNative.Log(s"ride watch: player \(puppet.player) in \(NameToString(puppet.seatedSlot)) of car \(EntityID.ToDebugStringDecimal(puppet.seatedIn)) at \(at.X) \(at.Y) \(at.Z) (mounted to \(EntityID.ToDebugStringDecimal(mount.parentId)) \(NameToString(mount.slotId.id))): in the car's frame its seat \(seatAt.X) \(seatAt.Y) \(seatAt.Z) (found \(seatAt.W > 0.0)), its origin \(origin.X) \(origin.Y) \(origin.Z), its hips \(hipsAt.X) \(hipsAt.Y) \(hipsAt.Z)");
        }
      }
    }
  }

  public func Report() -> Void {
    let n = Cast<Float>(Max(this.m_frames, 1));
    TandemNative.Log(s"ride watch: our car, \(this.m_frames) frames, top speed \(this.m_speedMax) m/s, acceleration rms \(SqrtF(this.m_accelSum / n)) max \(this.m_accelMax) m/s², over 10/50/200 m/s² in \(this.m_over10)/\(this.m_over50)/\(this.m_over200) frames, jerk rms \(SqrtF(this.m_jerkSum / n)) max \(this.m_jerkMax) m/s³");
    TandemNative.Log(s"ride watch: a seated puppet, \(this.m_seatFrames) frames at speed (rest from \(this.m_restN)), its origin off its seat by \(this.m_bodyOffMax) m at most, its hips \(this.m_hipsOffMax) m (\(this.m_hipsAlongAtMax) m along the car, at \(this.m_hipsSpeedAtMax) m/s); now hips at \(this.m_hipsNow.X) \(this.m_hipsNow.Y) \(this.m_hipsNow.Z) in the car (rest \(this.m_restHips.X) \(this.m_restHips.Y) \(this.m_restHips.Z)), \(this.m_speedNow) m/s");
    this.m_frames = 0;
    this.m_accelSum = 0.0;
    this.m_accelMax = 0.0;
    this.m_jerkSum = 0.0;
    this.m_jerkMax = 0.0;
    this.m_over10 = 0;
    this.m_over50 = 0;
    this.m_over200 = 0;
    this.m_speedMax = 0.0;
    this.m_seatFrames = 0;
    this.m_bodyOffMax = 0.0;
    this.m_hipsOffMax = 0.0;
    this.m_hipsAlongAtMax = 0.0;
    this.m_hipsSpeedAtMax = 0.0;
  }
}
