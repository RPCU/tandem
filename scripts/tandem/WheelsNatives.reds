// Vehicle combat: the plugin's natives (plugin/src/wheels_natives.cpp).
// Keep the two in sync: tools/check-natives.py checks them with Natives.reds.

// One thing that happened to a vehicle, or to a V in one.
public native struct TandemWheelsEvent {
  // 1 damage (a: 0 shows its health, 1 blows up now, 2 a wreck put there),
  // 2 hit on our vehicle (amount: damage; a: 1 explosion, 2 melee, 4 tyre,
  // 8 window; b: tyre; c: window), 3 exit (a: 0 normal, 1 bail, 2 thrown,
  // 3 knocked off a bike, 4 dead, 5 blown up; b: 1 it rolls on, 2 the
  // driver), 4 weapons in a vehicle (a: 0 none, 1 driver, 2 passenger,
  // 3 mounted, 4 gunner; item: the weapon), 5 a vehicle weapon fired (item:
  // weapon, slot: its slot; x, y, z: its target), 6 gunner seat (player:
  // the gunner, 0 free; seat), 7 ram on our NPC (npc; dx, dy, dz: the
  // car's velocity; a: 1 knocked down, 2 lethal), 8 chase (vehicle: the
  // shared vehicle's id; player: its target, 0 nobody)
  native let kind: Int32;
  native let player: Uint32;
  native let vehicle: Uint32;
  native let a: Int32;
  native let b: Int32;
  native let c: Int32;
  native let item: TweakDBID;
  native let slot: TweakDBID;
  native let seat: CName;
  native let amount: Float;
  native let npc: Uint32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
}

// How a vehicle another game owns is damaged, as last heard.
public native struct TandemWheelsDamage {
  native let known: Bool;
  native let vehicle: Uint32;
  native let record: TweakDBID;
  native let health: Float;
  // 1 burning, 2 destroyed, 4 drowned
  native let flags: Int32;
  // bit n: tyre n flat; window n broken
  native let tyres: Int32;
  native let windows: Int32;
  native let instigator: Uint32;
}

// A push another player's car gave ours, due now.
public native struct TandemWheelsPush {
  native let player: Uint32;
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
  native let x: Float;
  native let y: Float;
  native let z: Float;
}

public native class TandemWheels extends IScriptable {
  // A vehicle named across games: a player's car, or a shared world vehicle.
  public static native func CarOf(player: Uint32) -> Uint32
  public static native func StreetOf(id: Uint32) -> Uint32
  public static native func IsStreet(vehicle: Uint32) -> Bool
  public static native func TrafficIdOf(vehicle: Uint32) -> Uint32
  public static native func OwnerOf(vehicle: Uint32) -> Uint32

  // A vehicle another game drives, moved to where its driver's game says in
  // `seconds` (the engine's kinematic move; false: not moved, teleport it).
  // ... and whether it can be used in this game (the layout check).
  public static native func CanMove() -> Bool
  public static native func Move(vehicle: ref<Entity>, position: Vector4, orientation: Quaternion, seconds: Float) -> Bool
  // The engine's kinematic state for a vehicle Move drives (on: a kinematic
  // body, put where told; off: its physics its own again), whether it can
  // be used here, and whether a body is in it now.
  public static native func CanHold() -> Bool
  public static native func Hold(vehicle: ref<Entity>, on: Bool) -> Bool
  public static native func Held(vehicle: ref<Entity>) -> Bool
  // The engine's removal of an entity (entEntity.Dispose): our own crowd's
  // vehicles and riders where another game runs the street.
  public static native func Dispose(entity: ref<Entity>) -> Bool

  // Our own vehicles: the destruction grid and parts as the game reports
  // them, then how it stands (sent when it changed).
  public static native func NoteGrid(vehicle: Uint32, cells: array<Float>)
  public static native func NotePart(vehicle: Uint32, part: CName)
  public static native func SendDamage(vehicle: Uint32, record: TweakDBID, health: Float, flags: Int32, tyres: Int32, windows: Int32, instigator: Uint32)
  public static native func ForgetDamage(vehicle: Uint32)

  // What our V does.
  public static native func SendHit(vehicle: Uint32, damage: Float, flags: Int32, tyre: Int32, window: Int32, position: Vector4, direction: Vector4)
  public static native func SendExit(vehicle: Uint32, kind: Int32, flags: Int32, velocity: Vector4)
  public static native func SendCombat(mode: Int32, vehicle: Uint32, weapon: TweakDBID)
  public static native func SendFire(vehicle: Uint32, weapon: TweakDBID, slot: TweakDBID, target: Vector4)
  public static native func AskGunner(vehicle: Uint32, seat: CName, leave: Bool)
  public static native func FreeGunner(vehicle: Uint32)
  public static native func SendAim(vehicle: Uint32, yaw: Float, pitch: Float, firing: Bool)
  public static native func NoteContact(player: Uint32)
  public static native func SendBump(other: Uint32, impulse: Vector4, position: Vector4)
  public static native func SendRam(npc: Uint32, velocity: Vector4, position: Vector4, flags: Int32)
  public static native func SendChase(id: Uint32, target: Uint32, flags: Int32)

  // What the others say.
  public static native func TakeEvents() -> array<TandemWheelsEvent>
  public static native func DamageOf(vehicle: Uint32) -> TandemWheelsDamage
  public static native func GridCell(vehicle: Uint32, cell: Int32) -> Float
  public static native func Parts(vehicle: Uint32) -> array<CName>
  public static native func GunnerOf(vehicle: Uint32) -> Uint32
  // X yaw, Y pitch (radians), Z 1 when firing, W 0 when nobody aims.
  public static native func AimOf(vehicle: Uint32) -> Vector4
  public static native func SeatCombatOf(player: Uint32) -> Int32
  public static native func TakePushes() -> array<TandemWheelsPush>

  // The rules (core/include/tandem/vehicle_combat.hpp).
  public static native func ExitKindOf(speed: Bool, collision: Bool, death: Bool, forced: Bool, bike: Bool, destroyed: Bool) -> Int32
  public static native func Rolls(kind: Int32, speed: Float, driver: Bool) -> Bool
  public static native func RestSpeed() -> Float
  public static native func RollSeconds() -> Float
  public static native func RamFlags(speed: Float, crowd: Bool) -> Int32
}
