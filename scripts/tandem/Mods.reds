// Other mods (docs/architecture/mod-compat.md).
//
// What we send next to a record id says what kind of thing it is (its
// type, by the game's own enum names): a game that lacks the record (a mod
// it does not have) shows a similar vanilla one. The plugin resolves every
// record another game names (plugin/src/mods.cpp, one resolver); this file
// makes the hints and tells the player, in their language, each time their
// game shows something otherwise: once per player and kind, with a count.
// Nothing is replaced in silence.

public abstract class TandemMods {
  // The kinds the resolver knows (core's mods::Kind).
  public static func Weapon() -> Int32 { return 0; }
  public static func Clothing() -> Int32 { return 1; }
  public static func LookPiece() -> Int32 { return 2; }
  public static func Vehicle() -> Int32 { return 3; }
  public static func Character() -> Int32 { return 4; }
  public static func CharacterLook() -> Int32 { return 5; }
  public static func Item() -> Int32 { return 6; }
  public static func Effect() -> Int32 { return 7; }
  public static func Radio() -> Int32 { return 8; }

  // ---- hints, for what we send ----

  public static func ItemHint(record: TweakDBID) -> TandemRecordHint {
    let hint: TandemRecordHint;
    hint.type = n"";
    hint.tag = n"";
    hint.size = 0;
    let item = TDBID.IsValid(record) ? TweakDBInterface.GetItemRecord(record) : null;
    if !IsDefined(item) {
      return hint;
    }
    if IsDefined(item.ItemType()) {
      hint.type = StringToName(EnumValueToString("gamedataItemType", Cast<Int64>(EnumInt(item.ItemType().Type()))));
    }
    if IsDefined(item.ItemCategory()) {
      hint.tag = StringToName(EnumValueToString("gamedataItemCategory", Cast<Int64>(EnumInt(item.ItemCategory().Type()))));
    }
    return hint;
  }

  public static func ItemHints(records: array<TweakDBID>) -> array<TandemRecordHint> {
    let hints: array<TandemRecordHint>;
    for record in records {
      ArrayPush(hints, TandemMods.ItemHint(record));
    }
    return hints;
  }

  public static func VehicleHint(record: TweakDBID) -> TandemRecordHint {
    let hint: TandemRecordHint;
    hint.type = n"";
    hint.tag = n"";
    hint.size = 0;
    let vehicle = TDBID.IsValid(record) ? TweakDBInterface.GetVehicleRecord(record) : null;
    if !IsDefined(vehicle) {
      return hint;
    }
    if IsDefined(vehicle.Type()) {
      hint.type = StringToName(EnumValueToString("gamedataVehicleType", Cast<Int64>(EnumInt(vehicle.Type().Type()))));
    }
    let data = vehicle.VehDataPackage();
    if IsDefined(data) && IsDefined(data.VehSeatSet()) {
      let seats: array<wref<VehicleSeat_Record>>;
      data.VehSeatSet().VehSeats(seats);
      hint.size = ArraySize(seats);
    }
    return hint;
  }

  public static func NpcHint(npc: ref<ScriptedPuppet>) -> TandemRecordHint {
    let hint: TandemRecordHint;
    hint.type = n"";
    hint.tag = n"";
    hint.size = 0;
    if !IsDefined(npc) {
      return hint;
    }
    let character = TweakDBInterface.GetCharacterRecord(npc.GetRecordID());
    if IsDefined(character) && IsDefined(character.CharacterType()) {
      hint.type = StringToName(EnumValueToString("gamedataNPCType", Cast<Int64>(EnumInt(character.CharacterType().Type()))));
    }
    hint.tag = npc.GetResolvedGenderName();
    return hint;
  }

  // ---- what our game shows otherwise, said ----

  // Every tick of the system (TandemSystem): the plugin's notices.
  public static func Tick(game: GameInstance) -> Void {
    for notice in TandemModsNative.ModNotices() {
      switch notice.what {
        case 1:
          TandemText.Notice(game, TandemMods.ReplacedKey(notice.kind), notice.name, s"\(notice.count)");
          break;
        case 2:
          let differ = TandemMods.Comparison(notice.player, notice.name, false);
          if StrLen(differ) > 0 {
            TandemText.Say(game, SimpleMessageType.Neutral, 8.0, "Mods-Differ", TandemText.F("Mods-Differ", differ), TandemText.FE(true, "Mods-Differ", TandemMods.Comparison(notice.player, notice.name, true)));
          }
          break;
        case 3:
          TandemText.Notice(game, "Mods-Seen", notice.name, s"\(notice.count)");
          break;
        case 4:
          TandemText.Notice(game, "Mods-NamesCame", notice.name);
          break;
        default:
          break;
      }
    }
  }

  // The notice of a kind: who ({1}) and how many ({2}).
  public static func ReplacedKey(kind: Int32) -> String {
    switch kind {
      case 0: return "Mods-Replaced-Weapon";
      case 1: return "Mods-Replaced-Clothing";
      case 2: return "Mods-Replaced-Look";
      case 3: return "Mods-Replaced-Vehicle";
      case 4: return "Mods-Replaced-Character";
      case 5: return "Mods-Replaced-CharacterLook";
      case 6: return "Mods-Replaced-Item";
      case 7: return "Mods-Replaced-Effect";
      default: return "Mods-Replaced-Radio";
    }
  }

  // A category of mods, with how many ({1}), core's mods::kCategoryOrder.
  public static func CategoryKey(index: Int32) -> String {
    switch index {
      case 0: return "Mods-Category-Items";
      case 1: return "Mods-Category-Vehicles";
      case 2: return "Mods-Category-Characters";
      case 3: return "Mods-Category-Looks";
      case 4: return "Mods-Category-Gameplay";
      case 5: return "Mods-Category-Unknown";
      default: return "Mods-Category-Local";
    }
  }

  // "Judy has mods you don't (3): 2 with new items (shown as similar vanilla
  // ones), 1 changing gameplay (...). You have mods Judy doesn't (1): ...".
  // "" when nothing differs or they were not compared.
  public static func Comparison(player: Uint32, name: String, english: Bool) -> String {
    let theirs = TandemMods.Part(TandemModsNative.ModCounts(player, true), "Mods-TheyHave", name, english);
    let ours = TandemMods.Part(TandemModsNative.ModCounts(player, false), "Mods-YouHave", name, english);
    if StrLen(theirs) > 0 && StrLen(ours) > 0 {
      return theirs + ". " + ours;
    }
    return theirs + ours;
  }

  private static func Part(counts: array<Int32>, key: String, name: String, english: Bool) -> String {
    let size = ArraySize(counts);
    if size == 0 || counts[size - 1] == 0 {
      return "";
    }
    let parts = "";
    let i = 0;
    while i < size - 1 {
      if counts[i] > 0 {
        parts += (StrLen(parts) > 0 ? ", " : "") + TandemText.FE(english, TandemMods.CategoryKey(i), s"\(counts[i])");
      }
      i += 1;
    }
    return TandemText.FE(english, key, name, s"\(counts[size - 1])") + ": " + parts;
  }

  // The co-op menu's Mods popup: what differs from each player, their mods'
  // names once asked for, and every replacement this session.
  public static func MenuText(opt english: Bool) -> String {
    let lines: array<String>;
    for player in TandemModsNative.ModPlayers() {
      let name = TandemCoop.NameOf(player);
      let differ = TandemMods.Comparison(player, name, english);
      ArrayPush(lines, StrLen(differ) > 0 ? differ : TandemText.FE(english, "Mods-Same", name));
      let names = TandemModsNative.ModNamesOf(player);
      if ArraySize(names) > 0 {
        let all = "";
        for one in names {
          all += (StrLen(all) > 0 ? ", " : "") + one;
        }
        ArrayPush(lines, TandemText.FE(english, "Mods-Names", name, all));
      }
    }
    let replaced = TandemModsNative.ModSubstitutions();
    if ArraySize(replaced) > 0 {
      ArrayPush(lines, TandemText.FE(english, "Mods-ReplacedCount", s"\(ArraySize(replaced))"));
      for one in replaced {
        let shown = StrLen(one.shown) > 0 ? one.shown : TandemText.T(english, "Mods-Nothing");
        ArrayPush(lines, TandemText.FE(english, TandemMods.ReplacedLine(one.kind), one.name, one.record) + " - " + shown);
      }
    }
    if ArraySize(lines) == 0 {
      return TandemText.T(english, "Mods-NoneCompared");
    }
    let out = "";
    for line in lines {
      out += (StrLen(out) > 0 ? "\n" : "") + line;
    }
    return out;
  }

  // One line of the list: whose ({1}) record ({2}) of a kind.
  public static func ReplacedLine(kind: Int32) -> String {
    switch kind {
      case 0: return "Mods-Line-Weapon";
      case 1: return "Mods-Line-Clothing";
      case 2: return "Mods-Line-Look";
      case 3: return "Mods-Line-Vehicle";
      case 4: return "Mods-Line-Character";
      case 5: return "Mods-Line-CharacterLook";
      case 6: return "Mods-Line-Item";
      case 7: return "Mods-Line-Effect";
      default: return "Mods-Line-Radio";
    }
  }

  // The names of the other players' mods we lack (answered as notices).
  public static func AskNames() -> Void {
    for player in TandemModsNative.ModPlayers() {
      TandemModsNative.AskModNames(player);
    }
  }

  // tools/game.sh cmd coop mods [names]: everything, in the log.
  public static func Command(command: String) -> Bool {
    if !StrBeginsWith(command, "coop mods") {
      return false;
    }
    if Equals(command, "coop mods names") {
      TandemMods.AskNames();
      TandemNative.Log("coop: mods: names asked for");
      return true;
    }
    for line in StrSplit(TandemMods.MenuText(true), "\n") {
      TandemNative.Log(s"coop: mods: \(line)");
    }
    TandemModsNative.ModReport();
    return true;
  }
}
