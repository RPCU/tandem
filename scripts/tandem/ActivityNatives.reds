// Activities' natives (plugin/src/activity_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native struct TandemBusyState {
  native let player: Uint32;
  // 0 nothing, 1 a workspot, 2 a bed, 3 a screen, 4 a minigame, 5 a shop,
  // 6 a menu, 7 photo mode, 8 a contest
  native let kind: Int32;
  // 1 anchored: hold the body at x, y, z, yaw; 2 lying; 4 its game stands still
  native let flags: Int32;
  // the device used (its EntityID hash), the workspot resource's path hash,
  // its tree entry, the animation playing
  native let device: Uint64;
  native let workspot: Uint64;
  native let entry: Int32;
  native let anim: CName;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
}

public native struct TandemSkipEvent {
  // 1 asked of us, 2 a vote on ours, 3 ended
  native let kind: Int32;
  native let asker: Uint32;
  native let request: Uint32;
  // 0 the wheel, 1 a bed, 2 the host's story
  native let reason: Int32;
  // ended: 0 applied, 1 declined, 2 lapsed, 3 withdrawn, 4 refused
  native let outcome: Int32;
  native let by: Uint32;
  // 0 no reason, 1 another one is open, 2 cannot skip now, 3 a V is down,
  // 4 a time ability, 5 another world, 6 a pause menu, 7 not the host
  native let why: Int32;
  native let accepted: Bool;
  native let fromSeconds: Int32;
  native let toSeconds: Int32;
}

public native class TandemActivity extends IScriptable {
  public static native func SetOurs(kind: Int32, flags: Int32, device: Uint64, workspot: Uint64, entry: Int32, anim: CName, position: Vector4, orientation: Quaternion) -> Void;
  public static native func Others() -> array<TandemBusyState>;
  public static native func Changes() -> Uint32;
  public static native func AskSkip(reason: Int32, fromSeconds: Int32, toSeconds: Int32) -> Void;
  public static native func AnswerSkip(asker: Uint32, request: Uint32, accepted: Bool, why: Int32) -> Void;
  public static native func WithdrawSkip() -> Void;
  public static native func TakeSkips() -> array<TandemSkipEvent>;
}
