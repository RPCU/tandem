// The streaming queue's natives (plugin/src/streaming_natives.cpp). Keep the
// two in sync: tools/check-natives.py checks them with Natives.reds.
//
// The wraps of per-entity callbacks (Device and VehicleObject attach and
// detach, a device state's Notify, loot appearing) may run on job threads
// while the world streams in: they only queue the entity here, and their
// systems take the queue on the game thread's tick (docs/architecture/devices.md).

// One entity that came (attached), went (!attached) or was touched (cls: its
// persistent state's class, devices touched only).
public native struct TandemStreamed {
  native let entity: EntityID;
  native let cls: CName;
  native let attached: Bool;
}

// Something that happened at a place, to an entity or between two (a piece
// of the world breaking, a device's shot).
public native struct TandemStreamedPoint {
  native let entity: EntityID;
  native let other: EntityID;
  native let name: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let value: Int32;
}

public native class TandemStreaming extends IScriptable {
  // From any thread: append to `kind`'s queue (TandemStreamKind).
  public static native func QueueStreamed(kind: Int32, entity: EntityID, cls: CName, attached: Bool)
  // Game thread: everything queued of `kind` since the last call, in order.
  public static native func TakeStreamed(kind: Int32) -> array<TandemStreamed>
  // The same for what happened at a place (TandemPointKind).
  public static native func QueuePoint(kind: Int32, entity: EntityID, other: EntityID, name: CName, position: Vector4, value: Int32)
  public static native func TakePoints(kind: Int32) -> array<TandemStreamedPoint>
  // Game thread: we call `entity`'s wrapped callback ourselves until
  // SetPassing(no entity); its wrap asks IsPassing and lets that call through.
  public static native func SetPassing(entity: EntityID)
  public static native func IsPassing(entity: EntityID) -> Bool
}

// The queues (kKinds in the .cpp).
public abstract class TandemStreamKind {
  public static func Device() -> Int32 { return 0; }
  public static func DeviceTouched() -> Int32 { return 1; }
  public static func Vehicle() -> Int32 { return 2; }
  public static func Loot() -> Int32 { return 3; }
  // Item entities appearing while the host's scene plays (Scenes.reds, props).
  public static func Prop() -> Int32 { return 4; }
  // A breach's outcome on a device, on an NPC (attached: won).
  public static func BreachDevice() -> Int32 { return 5; }
  public static func BreachNpc() -> Int32 { return 6; }
  // The Ping quickhack reached a device: its network pulses from it.
  public static func NetworkPing() -> Int32 { return 7; }
  // Our V got into a vehicle (entity: the vehicle, cls: the seat).
  public static func Mounted() -> Int32 { return 8; }
  // An NPC came into our world (attached; Attitudes.reds: V's attitudes on
  // the puppets), or the game changed something its animation reads (not
  // attached, cls: n"high", n"upper", n"stance", n"weapon", n"workspot";
  // NpcAnims.reds). One taker, which splits them: TandemAttitudes.Drain.
  public static func Npc() -> Int32 { return 9; }
}

// The point queues (kPointKinds in the .cpp).
public abstract class TandemPointKind {
  // A piece of the world broke (entity and name: the placed entity and its
  // destruction component, when it is one).
  public static func Fracture() -> Int32 { return 0; }
  // A device fired its weapon (other: what at), turned to a target or let
  // go of it (value: 0 idle, 1 aim).
  public static func DeviceShot() -> Int32 { return 1; }
  public static func DeviceAim() -> Int32 { return 2; }
  // A grenade bounced (entity: the grenade; the place of the bounce; value:
  // the engine time of it, in milliseconds).
  public static func ThrowBounce() -> Int32 { return 3; }
  // A placed entity's physics body touched something (Props.reds; other:
  // what it touched; name: its component; the place of the touch; value:
  // the impulse in hundredths).
  public static func PropTouch() -> Int32 { return 4; }
}
