// World vehicles and police: the plugin's natives (plugin/src/street_natives.cpp).
// Keep the two in sync: tools/check-natives.py checks them with Natives.reds.

// One thing that happened to a shared vehicle, or a crime of our V seen by
// the area's owner.
public native struct TandemStreetEvent {
  // 1 spawn, 2 gone, 3 status, 4 hit (its owner), 5 crime (ours), 6 give
  // (the host asks us to give one of ours to `player`), 7 take (ours now:
  // the giver's state), 8 owner changed (`player` runs it now, `reason` did)
  native let kind: Int32;
  native let id: Uint32;
  native let record: TweakDBID;
  native let appearance: CName;
  // a placed vehicle's id in its owner's world (the same in ours); else 0
  native let entity: EntityID;
  // Where (plain floats: a 16-byte aligned field crashes the script VM).
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let i: Float;
  native let j: Float;
  native let k: Float;
  native let r: Float;
  // spawn, status: 1 parked, 2 police, 4 destroyed, 8 burning, 16 siren,
  // 32 lights, 64 panic, 128 abandoned; hit: 1 collision, 2 explosion,
  // 4 melee; crime: 1 the victim is police, 2 a vehicle
  native let flags: Int32;
  // gone: 0 out of range, 1 taken (it is `player`'s car now), 2 removed;
  // crime: 1 witnessed, 2 combat with the police, 3 damage, 4 kill; take:
  // 16 inherited (its owner is gone: nobody read the car); owner: who ran it
  native let reason: Int32;
  native let player: Uint32;
  // status, take: health 0..1; hit: damage; crime: the share of the victim's health
  native let amount: Float;
  // hit: the push given (collision, m/s) or the way it went; take: its velocity
  native let dx: Float;
  native let dy: Float;
  native let dz: Float;
  // crime: the victim or the witness as the host shares it; its attack type
  native let npc: Uint32;
  native let attack: Int32;
}

// One shared vehicle as the session's record has it.
public native struct TandemStreetCar {
  native let id: Uint32;
  native let record: TweakDBID;
  native let appearance: CName;
  native let entity: EntityID;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let i: Float;
  native let j: Float;
  native let k: Float;
  native let r: Float;
  native let flags: Int32;
  native let health: Float;
}

// A traffic light's colour (Lights.reds): another game's word (owner: that
// game; full: it named every light it knows round its V) or our own game's
// (owner 0). Colours as worldTrafficLightColor: 0 green, 1 red, 2 yellow.
public native struct TandemStreetLight {
  native let entity: EntityID;
  native let owner: Uint32;
  native let color: Int32;
  native let full: Bool;
}

public native class TandemStreet extends IScriptable {
  // Our lights while our game runs our street: those that changed, or all
  // we know round our V (full).
  public static native func SendLights(full: Bool, entities: array<EntityID>, colors: array<Int32>)
  // The other games' lights since the last call.
  public static native func TakeLights() -> array<TandemStreetLight>
  // (a light's own event hook: any thread) the colour our game gave it.
  public static native func LightSaid(entity: EntityID, color: Int32)
  public static native func TakeLightsSaid() -> array<TandemStreetLight>
  // The lights that show another game's colours now, and the question a
  // light's hook asks (any thread).
  public static native func FollowLights(entities: array<EntityID>)
  public static native func FollowsLight(entity: EntityID) -> Bool
  // Our crowd's own spawners (pedestrians, traffic, parked cars) off while
  // another game runs our street, on again after (any thread).
  public static native func SpawnersOff(off: Bool)
  public static native func SpawnersReport() -> String
  // A shared vehicle's id: its announcer's player id and its counter.
  public static native func MakeId(owner: Uint32, counter: Uint32) -> Uint32
  // Who announced it (the id says), and who runs it now (a hand-over moves
  // it; 0: not one the session knows).
  public static native func OwnerOf(id: Uint32) -> Uint32
  public static native func RunBy(id: Uint32) -> Uint32
  // TandemNative.SendNpcSeat's owner for a seat in a shared vehicle, and back.
  public static native func SeatOwner(id: Uint32) -> Uint32
  public static native func IsStreetSeat(owner: Uint32) -> Bool
  public static native func CarOfSeat(owner: Uint32) -> Uint32
  // Area owner: its world vehicles.
  public static native func SendSpawn(id: Uint32, record: TweakDBID, appearance: CName, entity: EntityID, position: Vector4, orientation: Quaternion, flags: Int32)
  public static native func SendStates(ids: array<Uint32>, positions: array<Vector4>, orientations: array<Quaternion>, velocities: array<Vector4>, viewers: array<Vector4>)
  public static native func SendGone(id: Uint32, reason: Int32)
  public static native func SendStatus(id: Uint32, health: Float, flags: Int32)
  // Anyone: we hit a vehicle another game owns; our V gets into one.
  public static native func SendHit(id: Uint32, damage: Float, flags: Int32, position: Vector4, impulse: Vector4)
  public static native func Claim(id: Uint32, seat: CName)
  // Hand-over: one of ours as we leave it, to `to`; our answer to a give
  // (false: we keep it) or a take (whether we run it now).
  public static native func Give(id: Uint32, to: Uint32, position: Vector4, orientation: Quaternion, velocity: Vector4, health: Float, flags: Int32)
  public static native func Answer(id: Uint32, accepted: Bool)
  public static native func TakeEvents() -> array<TandemStreetEvent>
  // Where to draw a shared vehicle now (W 0: nothing says).
  public static native func Position(id: Uint32) -> Vector4
  public static native func Orientation(id: Uint32) -> Quaternion
  public static native func Velocity(id: Uint32) -> Vector4
  // The proxies we must show (the vehicles another game runs).
  public static native func Shared() -> array<TandemStreetCar>
  public static native func IsShared(id: Uint32) -> Bool
  // Police: our V's wanted level (0..5) and EStarState; another player's.
  public static native func SendHeat(stage: Int32, state: Int32)
  public static native func HeatStage(player: Uint32) -> Int32
  public static native func HeatState(player: Uint32) -> Int32
  // Area owner: `player`'s puppet committed a crime here.
  public static native func SendCrime(player: Uint32, kind: Int32, flags: Int32, attack: Int32, amount: Float, npc: Uint32, car: Uint32, position: Vector4)
}
