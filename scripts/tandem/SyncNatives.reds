// The consistency checks' natives (plugin/src/sync_natives.cpp). Keep the
// two in sync: tools/check-natives.py checks them with Natives.reds.

// One NPC the host shares: what to spawn its proxy from, where it is now.
public native struct TandemSyncNpc {
  native let npc: Uint32;
  native let record: TweakDBID;
  native let appearance: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
}

// One player in the session (0 unknown, 1 female, 2 male).
public native struct TandemSyncPlayer {
  native let player: Uint32;
  native let name: String;
  native let body: Int32;
}

public native class TandemSync extends IScriptable {
  // Guest: the NPCs the host shares, as the session's record has them.
  public static native func SharedNpcs() -> array<TandemSyncNpc>
  public static native func IsNpcShared(npc: Uint32) -> Bool
  public static native func SharedNpcCount() -> Int32
  // The players in the session (us too).
  public static native func Players() -> array<TandemSyncPlayer>
  public static native func InSession(player: Uint32) -> Bool
  // Our world was rebuilt around our V: 1 a loading screen, 2 a fast travel.
  public static native func WorldRebuilt(reason: Int32)
  // How many times WorldRebuilt was called since the game started.
  public static native func Rebuilds() -> Int32
}
