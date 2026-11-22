// What the game runs without (plugin/src/degraded_natives.cpp,
// docs/architecture/degradations.md). Keep the two in sync:
// tools/check-natives.py checks them with Natives.reds.
//
// No silent fallback: a path that is off, or a lesser one taken for one
// body, is said here by its id (core/src/degraded.cpp's catalogue;
// `task degraded:check`):
//   TandemDegraded.State("vehicle move", "why");     // holds until Over
//   TandemDegraded.Fallback("prompt drawn", "why");  // once, counted
// Both may be called from any thread (they only record and log).

// One entry of the registry, or one thing to tell the player. `player` 0:
// this game; else the player whose game said it (no reason, no fallbacks).
public native struct TandemDegradedEntry {
  native let player: Uint32;
  native let id: String;
  native let reason: String;
  native let core: Bool;
  native let chosen: Bool;
  native let fallback: Bool;
  native let over: Bool;
  native let count: Int32;
}

public native class TandemDegraded extends IScriptable {
  // A path of the sync is off for now: logged as a warning, shown to the
  // player, told to the other players, until Over.
  public static native func State(id: String, reason: String) -> Void
  // The same by the player's own tandem.ini switch: listed, no failure.
  public static native func Chosen(id: String, reason: String) -> Void
  public static native func Over(id: String) -> Void
  // A lesser path taken once, for one body: counted, with its last reason.
  public static native func Fallback(id: String, reason: String) -> Void
  // This game's states and fallbacks, then what the other games said.
  public static native func Entries() -> array<TandemDegradedEntry>
  // The core features that are off: nothing can be hosted or joined.
  public static native func CoreOff() -> array<TandemDegradedEntry>
  // What to tell the player since the last call (game thread).
  public static native func TakeNews() -> array<TandemDegradedEntry>
  // True once after a chosen session was dropped for a core feature.
  public static native func TakeRefused() -> Bool
  // "degraded: none", or the list on one line.
  public static native func Summary() -> String
  // (cmd degraded) every entry, in the log.
  public static native func LogRegistry() -> Void
  // (test reset) the fallbacks counted so far are forgotten.
  public static native func ResetFallbacks() -> Void
}
