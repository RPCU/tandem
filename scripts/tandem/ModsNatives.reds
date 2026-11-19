// The other mods' natives (plugin/src/mods_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

// One thing to tell the player: what 1, `count` of `player`'s records of
// `kind` (0 weapon, 1 clothing, 2 look piece, 3 vehicle, 4 character, 5
// character look, 6 item, 7 effect, 8 radio) shown otherwise (`outcome`: 1
// a similar vanilla one, 2 the kind's default, 3 nothing or refused); 2,
// `player`'s mods compared with ours (TandemModsNative.ModCounts); 3,
// `player` sees substitutes for `count` of ours (`kind`: bits of the kinds);
// 4, the names of `player`'s mods we asked for came (`count` of them).
public native struct TandemModNotice {
  native let what: Int32;
  native let player: Uint32;
  native let name: String;
  native let kind: Int32;
  native let count: Int32;
  native let outcome: Int32;
}

// One replacement this session: `player`'s record (its id: our game has no
// name for it) of `kind`, shown as `shown` ("": nothing).
public native struct TandemModSubstitution {
  native let player: Uint32;
  native let name: String;
  native let kind: Int32;
  native let outcome: Int32;
  native let record: String;
  native let shown: String;
}

public native class TandemModsNative extends IScriptable {
  public static native func ModNotices() -> array<TandemModNotice>
  public static native func ModPlayers() -> array<Uint32>
  // Per category (items, vehicles, characters, looks, gameplay, unknown,
  // local), then the total: `player`'s mods we lack (theirs), or ours they lack.
  public static native func ModCounts(player: Uint32, theirs: Bool) -> array<Int32>
  public static native func ModNamesOf(player: Uint32) -> array<String>
  public static native func AskModNames(player: Uint32) -> Void
  public static native func ModSubstitutions() -> array<TandemModSubstitution>
  // A replacement found here: an NPC look by its name, a radio station by
  // its number; nothing shown instead.
  public static native func ModNote(kind: Int32, name: CName, number: Int32, player: Uint32) -> Void
  public static native func ResolveRecord(kind: Int32, record: TweakDBID, player: Uint32) -> TweakDBID
  public static native func ModReport() -> Void
}
