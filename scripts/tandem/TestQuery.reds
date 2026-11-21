// tools/game.sh cmd test query WHAT [ARGS]: the game's own state, read from
// its systems (not from what our code says it did), for a scenario's
// before/after checks (tools/scenario.sh query, expect_value,
// expect_changed; docs/architecture/testing.md, "Queries"). One line per
// query, the question echoed: "test query: WHAT [ARGS] = VALUE", VALUE
// "none" when there is nothing to read (no V, not mounted, no such NPC or
// entry). Solo or in a session.
//
//   health [points]       our V's health, percent (2 decimals); points: in
//                         health points; percent starts the watch "health low" reads
//   health low            the lowest our V's health went since the last
//                         "health", read every tick: a hit's dip that
//                         regeneration fills again within a second
//   item RECORD           how many of RECORD our inventory holds (0: none)
//   money                 our eddies
//   fact NAME             a quest fact's value (0: never set)
//   journal PATH [CLASS]  a journal entry's state (Inactive, Active, ...)
//   position              our V's X Y Z (2 decimals)
//   mounted [player N | puppet]  the vehicle and seat our V (or player N's
//                         puppet, or the first remote player's) is mounted
//                         in: RECORD SLOT ENTITYHASH
//   npc ID health         a shared NPC's health, percent: our own NPC or
//                         our proxy of it
//   threat puppet         the largest threat value an NPC we run near our V
//                         holds on the first remote player's puppet
//   device KIND WHAT      the nearest device of KIND (camera, turret...)
//                         within 40 m: attitude (to our V: friendly, neutral,
//                         hostile), puppet (to the first puppet), target (what
//                         it follows: none, V, puppet, other), alarm (its
//                         security system: SAFE, ALERTED, COMBAT; none), mark
//                         (the player whose hack is marked on it; 0: none)
//   car marks             the shared vehicles another player's quickhack is
//                         marked on now (their traffic ids), or none
public class TandemTestQuery {
  public static func Run(game: GameInstance, system: ref<TandemSystem>, command: String) -> Void {
    let words = StrSplit(command, " ");
    let count = ArraySize(words);
    // (Padded: every word read below exists.)
    while ArraySize(words) < 5 {
      ArrayPush(words, "");
    }
    let what = words[2];
    let arg = words[3];
    let asked = StrAfterFirst(command, "test query ");
    let player = GetPlayer(game);
    let value = "none";
    if Equals(what, "health") && Equals(arg, "low") {
      let low = 0.0;
      if system.HealthLow(low) {
        value = FloatToStringPrec(low, 2);
      }
    } else if Equals(what, "health") {
      if IsDefined(player) && Equals(arg, "points") {
        let points = GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, false);
        value = FloatToStringPrec(points, 2);
      } else if IsDefined(player) {
        let percent = GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
        value = FloatToStringPrec(percent, 2);
        system.StartHealthWatch(percent);
      }
    } else if Equals(what, "item") && count > 3 {
      if IsDefined(player) {
        value = IntToString(TandemTestQuery.ItemCount(game, player, TDBID.Create(arg)));
      }
    } else if Equals(what, "money") {
      if IsDefined(player) {
        value = IntToString(GameInstance.GetTransactionSystem(game).GetItemQuantity(player, MarketSystem.Money()));
      }
    } else if Equals(what, "fact") && count > 3 {
      value = IntToString(GameInstance.GetQuestsSystem(game).GetFactStr(arg));
    } else if Equals(what, "journal") && count > 3 {
      value = TandemTestQuery.JournalState(game, arg, words[4]);
    } else if Equals(what, "position") {
      if IsDefined(player) {
        let at = player.GetWorldPosition();
        value = s"\(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2))";
      }
    } else if Equals(what, "mounted") {
      let who: ref<GameObject> = player;
      if Equals(arg, "puppet") {
        let players = system.PuppetPlayers();
        who = null;
        if ArraySize(players) > 0 {
          who = system.PuppetOf(players[0]);
        }
      } else if Equals(arg, "player") && count > 4 {
        who = system.PuppetOf(Cast<Uint32>(StringToInt(words[4], 0)));
      } else if count > 3 {
        who = null;
        value = "unknown";
      }
      if IsDefined(who) {
        value = TandemTestQuery.Mounted(game, who);
      }
    } else if Equals(what, "npc") && count > 4 && Equals(words[4], "health") {
      let npcs = system.Npcs();
      if IsDefined(npcs) {
        let id = npcs.EntityOfNpc(Cast<Uint32>(StringToInt(arg, 0)));
        let npc = GameInstance.FindEntityByID(game, id) as GameObject;
        if EntityID.IsDefined(id) && IsDefined(npc) {
          value = TandemTestQuery.Health(game, npc);
        }
      }
    } else if Equals(what, "threat") && Equals(arg, "puppet") {
      value = TandemThreats.PuppetThreat(game, system);
    } else if Equals(what, "car") && Equals(arg, "marks") {
      let hacks = TandemHacks.Get(game);
      if IsDefined(hacks) {
        value = hacks.CarMarks();
      }
    } else if Equals(what, "device") && count > 4 {
      let devices = TandemDevices.Get(game);
      if IsDefined(devices) {
        value = devices.Query(arg, words[4]);
      }
    } else {
      value = "unknown";
    }
    if Equals(value, "unknown") {
      TandemNative.Log(s"test query: unknown: \(asked) (health [points], health low, item RECORD, money, fact NAME, journal PATH, position, mounted [player N | puppet], npc ID health, threat puppet, device KIND attitude|puppet|target|alarm|mark, car marks)");
      return;
    }
    TandemNative.Log(s"test query: \(asked) = \(value)");
  }

  private static func Health(game: GameInstance, object: ref<GameObject>) -> String {
    let id = Cast<StatsObjectID>(object.GetEntityID());
    let percent = GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(id, gamedataStatPoolType.Health, true);
    return FloatToStringPrec(percent, 2);
  }

  // Every stack of the record, whatever its seed (a non-stackable item is a
  // stack of one per copy).
  private static func ItemCount(game: GameInstance, player: ref<GameObject>, record: TweakDBID) -> Int32 {
    let items: array<wref<gameItemData>>;
    let total = 0;
    if !GameInstance.GetTransactionSystem(game).GetItemList(player, items) {
      return 0;
    }
    for data in items {
      if IsDefined(data) && ItemID.GetTDBID(data.GetID()) == record {
        total += data.GetQuantity();
      }
    }
    return total;
  }

  // The path's entry of CLASS, else of the journal's base class or one of
  // the quest classes (GetEntryByString wants the entry's class).
  private static func JournalState(game: GameInstance, path: String, named: String) -> String {
    let journal = GameInstance.GetJournalManager(game);
    let classes: array<String>;
    if StrLen(named) > 0 {
      ArrayPush(classes, named);
    }
    ArrayPush(classes, "gameJournalEntry");
    ArrayPush(classes, "gameJournalQuest");
    ArrayPush(classes, "gameJournalQuestPhase");
    ArrayPush(classes, "gameJournalQuestObjective");
    ArrayPush(classes, "gameJournalQuestSubObjective");
    ArrayPush(classes, "gameJournalPhoneMessage");
    ArrayPush(classes, "gameJournalOnscreen");
    for name in classes {
      let entry = journal.GetEntryByString(path, name);
      if IsDefined(entry) {
        let state = journal.GetEntryState(entry);
        return EnumValueToString("gameJournalEntryState", Cast<Int64>(EnumInt(state)));
      }
    }
    return "none";
  }

  private static func Mounted(game: GameInstance, who: ref<GameObject>) -> String {
    let info = GameInstance.GetMountingFacility(game).GetMountingInfoSingleWithObjects(who);
    let car = info.parentId;
    if !EntityID.IsDefined(car) {
      return "none";
    }
    let vehicle = GameInstance.FindEntityByID(game, car) as VehicleObject;
    let record = IsDefined(vehicle) ? TDBID.ToStringDEBUG(vehicle.GetRecordID()) : "unknown-record";
    let slot = info.slotId.id;
    return s"\(record) \(NameToString(slot)) \(EntityID.GetHash(car))";
  }
}
