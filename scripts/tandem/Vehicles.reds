// Vehicles between players (docs/architecture/vehicles.md, milestone 3). The driver's
// game owns its car: it sends where its V sits (Seat) and, while driving,
// the car's transform (VehicleState). The other games spawn a copy of that
// car, move it every frame by teleport to where the driver's game says it is
// (engine-knowledge.md §13.6: the copy's own physics does not fight it), and
// mount the driver's and passengers' puppets in its seats.

public abstract class TandemVehicles {
  // Mounts `child` in `slot` of `parent` at once, the way the game seats a
  // body in a trunk (VehicleComponent.MountEntityToSlot), with no AI.
  public static func Seat(game: GameInstance, child: ref<GameObject>, parent: ref<GameObject>, slot: CName) -> Void {
    let info: MountingInfo;
    info.parentId = parent.GetEntityID();
    info.childId = child.GetEntityID();
    info.slotId.id = slot;
    let request = new MountingRequest();
    request.lowLevelMountingInfo = info;
    request.preservePositionAfterMounting = false;
    request.mountData = new MountEventData();
    request.mountData.isInstant = true;
    request.mountData.slotName = slot;
    request.mountData.mountParentEntityId = parent.GetEntityID();
    let options = new MountEventOptions();
    options.alive = true;
    options.occupiedByNonFriendly = false;
    request.mountData.mountEventOptions = options;
    GameInstance.GetMountingFacility(game).Mount(request);
  }

  public static func Unseat(game: GameInstance, child: ref<GameObject>, parentID: EntityID, slot: CName) -> Void {
    let info: MountingInfo;
    info.parentId = parentID;
    info.childId = child.GetEntityID();
    info.slotId.id = slot;
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = info;
    request.mountData = new MountEventData();
    request.mountData.isInstant = true;
    GameInstance.GetMountingFacility(game).Unmount(request);
  }

  // The game's own forced way out of a seat: an instant unmount naming only
  // the one getting out, as the vehicle state machine does for a V killed
  // on a bike or thrown out (ImmediateExitWithForceEvents.Unmount,
  // final.reds 550633). The state machine then leaves its vehicle states.
  public static func PutOut(game: GameInstance, child: ref<GameObject>) -> Void {
    let info: MountingInfo;
    info.childId = child.GetEntityID();
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = info;
    request.mountData = new MountEventData();
    request.mountData.isInstant = true;
    GameInstance.GetMountingFacility(game).Unmount(request);
  }

  // Where `child` would stand getting out of `vehicle`: the game's own exit
  // spot (VehicleObject.CanUnmount, what the exit states ask), else the
  // navmesh beside it (right, then left), else on its roof.
  public static func ExitSpot(game: GameInstance, vehicle: ref<VehicleObject>, child: ref<GameObject>) -> Vector4 {
    let exit = vehicle.CanUnmount(true, child);
    if NotEquals(exit.direction, vehicleExitDirection.NoDirection) && NotEquals(exit.direction, vehicleExitDirection.Top) {
      let at = WorldPosition.ToVector4(exit.position);
      at.W = 1.0;
      return at;
    }
    let navigation = GameInstance.GetAINavigationSystem(game);
    let center = vehicle.GetWorldPosition();
    let right = vehicle.GetWorldRight();
    let sides = [2.5, -2.5];
    for side in sides {
      let beside = new Vector4(center.X + right.X * side, center.Y + right.Y * side, center.Z, 1.0);
      let ground = navigation.FindPointInSphereForCharacter(beside, 2.0, child);
      if Equals(ground.status, worldNavigationRequestStatus.OK) {
        let at = ground.point;
        at.W = 1.0;
        return at;
      }
    }
    return new Vector4(center.X, center.Y, center.Z + 2.0, 1.0);
  }

  // Carries `child` with `parent` from where it stands now (a V riding
  // along, Transit.reds): a mount to a slot that is not a seat, its world
  // position kept (preservePositionAfterMounting, as vanilla's trunk body
  // mount) and silent (no vehicle state machine for the player, as
  // vanilla's seat switches mount it).
  public static func Carry(game: GameInstance, child: ref<GameObject>, parent: ref<GameObject>, slot: CName) -> Void {
    let info: MountingInfo;
    info.parentId = parent.GetEntityID();
    info.childId = child.GetEntityID();
    info.slotId.id = slot;
    let request = new MountingRequest();
    request.lowLevelMountingInfo = info;
    request.preservePositionAfterMounting = true;
    request.mountData = new MountEventData();
    request.mountData.isInstant = true;
    let options = new MountEventOptions();
    options.alive = true;
    options.silentUnmount = true;
    request.mountData.mountEventOptions = options;
    GameInstance.GetMountingFacility(game).Mount(request);
  }

  public static func Uncarry(game: GameInstance, child: ref<GameObject>, parentID: EntityID, slot: CName) -> Void {
    let info: MountingInfo;
    info.parentId = parentID;
    info.childId = child.GetEntityID();
    info.slotId.id = slot;
    let request = new UnmountingRequest();
    request.lowLevelMountingInfo = info;
    request.mountData = new MountEventData();
    request.mountData.isInstant = true;
    let options = new MountEventOptions();
    options.silentUnmount = true;
    request.mountData.mountEventOptions = options;
    GameInstance.GetMountingFacility(game).Unmount(request);
  }

  // The seat to put `child` in: `wanted` when the vehicle has it and nobody
  // else sits there, else its first free seat (the driver's only when no
  // other is free), else none. A seat someone sits in is never taken: in
  // the host's quest vehicle that someone is the scene's (Panam at the
  // wheel), and the scene must not find its seat gone.
  public static func FreeSlot(game: GameInstance, vehicle: ref<VehicleObject>, wanted: CName, child: EntityID) -> CName {
    if !IsDefined(vehicle) {
      return n"None";
    }
    if IsNameValid(wanted) && VehicleComponent.HasSlot(game, vehicle, wanted) && TandemVehicles.FreeFor(game, vehicle, wanted, child) {
      return wanted;
    }
    let seats: array<wref<VehicleSeat_Record>>;
    VehicleComponent.GetSeats(game, vehicle, seats);
    let driver = n"None";
    for seat in seats {
      let name = seat.SeatName();
      if TandemVehicles.FreeFor(game, vehicle, name, child) {
        if !VehicleComponent.IsDriverSlot(name) {
          return name;
        }
        driver = name;
      }
    }
    return driver;
  }

  // Nobody sits in `slot`, or `child` itself does.
  public static func FreeFor(game: GameInstance, vehicle: ref<VehicleObject>, slot: CName, child: EntityID) -> Bool {
    if !VehicleComponent.IsSlotOccupied(game, vehicle.GetEntityID(), slot) {
      return true;
    }
    let mine = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithIds(child);
    return mine.parentId == vehicle.GetEntityID() && Equals(mine.slotId.id, slot);
  }

  // A copy of a player's car, where their game says it is.
  public static func SpawnCopy(record: TweakDBID, position: Vector4, orientation: Quaternion) -> EntityID {
    let spec = new DynamicEntitySpec();
    spec.recordID = record;
    spec.position = position;
    spec.orientation = orientation;
    spec.alwaysSpawned = true;
    spec.persistState = false;
    spec.persistSpawn = false;
    spec.spawnInView = true;
    spec.active = true;
    ArrayPush(spec.tags, n"Tandem.Vehicle");
    return GameInstance.GetDynamicEntitySystem().CreateEntity(spec);
  }

  // Puts the copy where the driver's game says (interpolated in the plugin).
  public static func Follow(game: GameInstance, car: ref<GameObject>, player: Uint32) -> Void {
    let position = TandemNative.RemoteVehiclePosition(player);
    if position.W == 0.0 {
      return;
    }
    position.W = 1.0;
    let angles = Quaternion.ToEulerAngles(TandemNative.RemoteVehicleOrientation(player));
    GameInstance.GetTeleportationFacility(game).Teleport(car, position, angles);
  }
}
