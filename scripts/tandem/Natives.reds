// The plugin's natives (plugin/src/natives.cpp). Keep the two in sync: a
// signature that differs from the C++ side fails at load.

public native struct TandemEvent {
  // 1 joined, 2 left, 3 session ended, 4 we are in the session
  native let kind: Int32;
  native let player: Uint32;
  native let name: String;
  // 0 unknown, 1 female, 2 male
  native let body: Int32;
}

public native struct TandemShot {
  native let player: Uint32;
  native let left: Bool;
  // gamedataFxAction: Shoot 16, SilencedShoot 17, MuzzleBrakeShoot 15
  native let effect: Int32;
}

public native struct TandemNpcEvent {
  // 1 spawn, 2 gone
  native let kind: Int32;
  native let npc: Uint32;
  native let record: TweakDBID;
  native let appearance: CName;
  // Where (plain floats: a 16-byte aligned field crashes the script VM).
  native let x: Float;
  native let y: Float;
  native let z: Float;
  // orientation quaternion
  native let i: Float;
  native let j: Float;
  native let k: Float;
  native let r: Float;
  // gone: 0 out of range, 1 dead
  native let reason: Int32;
  // 3 hit (host): who hit
  native let player: Uint32;
  // 3 hit: damage in health points; 4 health (guest): fraction 0..1
  native let amount: Float;
  // 3 hit: 1 weak spot, 2 critical, 4 melee, 8 lethal
  native let flags: Int32;
  // 3 hit: its direction (x, y, z: where it landed)
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
}

public native class TandemNative extends IScriptable {
  public static native func IsConfigured() -> Bool
  public static native func IsStarted() -> Bool
  public static native func Start(body: Int32)
  public static native func Stop()
  public static native func SendLocalState(position: Vector4, orientation: Quaternion, velocity: Vector4, locomotion: Int32, flags: Int32)
  public static native func TakeEvents() -> array<TandemEvent>
  public static native func RegisterPuppet(player: Uint32, id: EntityID)
  public static native func UnregisterPuppet(player: Uint32)
  public static native func WantsController(player: Uint32) -> Bool
  public static native func AttachController(player: Uint32, entity: ref<Entity>) -> Bool
  public static native func RemotePosition(player: Uint32) -> Vector4
  public static native func RemoteYaw(player: Uint32) -> Float
  public static native func RemoteStance(player: Uint32) -> Int32
  public static native func PuppetRecord(body: Int32) -> String
  public static native func IsProbe() -> Bool
  public static native func ClipLengths(entity: ref<Entity>) -> String
  public static native func ApplyLook(entity: ref<Entity>, state: ref<gameuiICharacterCustomizationState>, male: Bool) -> Int32
  public static native func TakeCommand() -> String
  public static native func MarkThirdPerson(entity: ref<Entity>) -> Bool
  public static native func IsMirror() -> Bool
  public static native func MirrorPlayerTo(entity: ref<Entity>)
  public static native func ClearMirrors()
  public static native func PoseProbe(entity: ref<Entity>)
  public static native func PoseScan(entity: ref<Entity>)
  public static native func PoseTree(entity: ref<Entity>)
  public static native func PoseBones(entity: ref<Entity>)
  public static native func PoseMark(entity: ref<Entity>, diff: Bool)
  public static native func PoseHook() -> Bool
  public static native func PoseWatch(entity: ref<Entity>, label: String, depth: Int32)
  public static native func PoseCopy(source: ref<Entity>, target: ref<Entity>)
  public static native func PoseReport()
  public static native func SendNpcSpawn(npc: Uint32, record: TweakDBID, appearance: CName, position: Vector4, orientation: Quaternion)
  public static native func SendNpcStates(npcs: array<Uint32>, positions: array<Vector4>, orientations: array<Quaternion>, detected: array<Int32>)
  public static native func SendNpcGone(npc: Uint32, reason: Int32, killer: Uint32)
  public static native func SendNpcHealth(npc: Uint32, health: Float)
  public static native func SendNpcHit(npc: Uint32, damage: Float, flags: Int32, position: Vector4, direction: Vector4)
  public static native func SendNpcEffect(npc: Uint32, effects: array<TweakDBID>)
  public static native func NpcDetected(npc: Uint32) -> Int32
  public static native func TakeNpcEvents() -> array<TandemNpcEvent>
  public static native func NpcPuppetId(npc: Uint32) -> Uint32
  public static native func IsHost() -> Bool
  public static native func SendWorld(gameTimeSeconds: Int32, weather: CName, paused: Bool)
  public static native func HostWorldChanges() -> Int32
  public static native func HostGameTime() -> Int32
  public static native func HostWeather() -> CName
  public static native func SendPaused(paused: Bool)
  public static native func PausedPlayers() -> array<Uint32>
  public static native func PausedChanges() -> Int32
  public static native func PlayerName(player: Uint32) -> String
  public static native func SendSeat(vehicle: TweakDBID, seat: CName, owner: Uint32)
  public static native func SendVehicle(position: Vector4, orientation: Quaternion, velocity: Vector4)
  public static native func RemoteSeatChanges(player: Uint32) -> Int32
  public static native func RemoteSeatVehicle(player: Uint32) -> TweakDBID
  public static native func RemoteSeatSlot(player: Uint32) -> CName
  public static native func RemoteSeatOwner(player: Uint32) -> Uint32
  public static native func RemoteVehiclePosition(player: Uint32) -> Vector4
  public static native func RemoteVehicleOrientation(player: Uint32) -> Quaternion
  public static native func SendHeld(right: TweakDBID, left: TweakDBID)
  public static native func SendShot(left: Bool, effect: Int32)
  public static native func RemoteHeldChanges(player: Uint32) -> Int32
  public static native func RemoteHeld(player: Uint32, left: Bool) -> TweakDBID
  public static native func TakeShots() -> array<TandemShot>
  public static native func SubmitAppearance(state: ref<gameuiICharacterCustomizationState>, male: Bool, items: array<TweakDBID>) -> Int32
  public static native func RemoteAppearanceChanges(player: Uint32) -> Int32
  public static native func RemoteItems(player: Uint32) -> array<TweakDBID>
  public static native func ApplyRemoteLook(entity: ref<Entity>, player: Uint32) -> Int32
  public static native func PoseSource(entity: ref<Entity>)
  public static native func PoseBind(player: Uint32, entity: ref<Entity>)
  public static native func PoseLoop(player: Uint32)
  public static native func PoseStreamReport()
  public static native func Log(text: String)
}
