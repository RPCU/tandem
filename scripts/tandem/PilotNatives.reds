// The test pilot's natives (plugin/src/pilot_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native class TandemPilotNative extends IScriptable {
  // The pilot state line, to pilot.state next to the DLL (replaced whole).
  public static native func WriteState(line: String) -> Bool
  public static native func ClearState()
  // Saving is locked from the first call until the game ends, across
  // loaded saves (TandemSystem.IsSavingLocked reads it).
  public static native func LockSaves()
  public static native func SavesLocked() -> Bool
  // (probe) The game's functional-test system in this build: its functions
  // go to tandem's log; answers "NAME=present|absent|none ...".
  public static native func ProbeFunctionalTests() -> String
}
