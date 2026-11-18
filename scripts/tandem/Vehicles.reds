// Vehicles between players (docs/ARCHITECTURE.md, milestone 3). The driver's
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
