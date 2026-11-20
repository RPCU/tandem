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

public native class TandemStreaming extends IScriptable {
  // From any thread: append to `kind`'s queue (TandemStreamKind).
  public static native func QueueStreamed(kind: Int32, entity: EntityID, cls: CName, attached: Bool)
  // Game thread: everything queued of `kind` since the last call, in order.
  public static native func TakeStreamed(kind: Int32) -> array<TandemStreamed>
}

// The queues (kKinds in the .cpp).
public abstract class TandemStreamKind {
  public static func Device() -> Int32 { return 0; }
  public static func DeviceTouched() -> Int32 { return 1; }
  public static func Vehicle() -> Int32 { return 2; }
  public static func Loot() -> Int32 { return 3; }
}
