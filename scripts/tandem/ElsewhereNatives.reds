// Other worlds' natives (plugin/src/elsewhere_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native struct TandemAwayBody {
  // The body stays elsewhere: send it (x, y, z; orientation i, j, k, r) and
  // no velocity, not where our V is.
  native let held: Bool;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let i: Float;
  native let j: Float;
  native let k: Float;
  native let r: Float;
}

public native struct TandemAwayState {
  native let player: Uint32;
  // 0 the city, 1 cyberspace, 2 a braindance, 3 Johnny, 4 someone else
  native let place: Int32;
  // 1 moved: the scene took the player away from its body (x, y, z, yaw);
  // 2 settled: that body sits or lies
  native let flags: Int32;
  native let x: Float;
  native let y: Float;
  native let z: Float;
  native let yaw: Float;
}

public native class TandemAway extends IScriptable {
  public static native func Sample(position: Vector4, orientation: Quaternion, mounted: Bool, settled: Bool, place: Int32) -> TandemAwayBody;
  public static native func Reset() -> Void;
  public static native func Ours() -> TandemAwayState;
  public static native func Holding() -> Bool;
  public static native func Others() -> array<TandemAwayState>;
  public static native func Changes() -> Uint32;
}
