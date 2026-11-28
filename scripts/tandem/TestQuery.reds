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
//   held [player N | puppet]  what our V (or player N's puppet, or the
//                         first remote player's) holds: RIGHT LEFT HOW, the
//                         records in its right and left hand slots (none:
//                         empty) and dual (the game dual wields them: our
//                         V's state machine's word, a puppet's two paired
//                         weapons in hand) or single
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
//   remote [player N]     our V's remote view (player N's): "SORT X Y Z
//                         TRAFFIC", SORT camera, turret, vehicle, device or
//                         other, where the view is, the shared vehicle it
//                         drives (0: none); none: in its body
//   car player N          our copy of player N's car (driven, or the last it
//                         left parked): X Y Z
//   traffic ID position   our body of shared vehicle ID (ours or its proxy):
//                         X Y Z
//   car marks             the shared vehicles another player's quickhack is
//                         marked on now (their traffic ids), or none
//   paint [puppet]        the custom paint the car's own state says: our
//                         V's car (the one it sits in, else its last), or
//                         the first remote player's car copy here: stock,
//                         "primary R,G,B [secondary R,G,B] [lights HUE]",
//                         "unique RECORD"; none: no such car
//   attitude group NAME   "V a/b puppet c/d": the group's relation to V's
//                         group / V's to it, then to the first puppet's
//                         group / its to it (the engine's group table;
//                         0 friendly, 1 neutral, 2 hostile)
//   attitude npc WHICH    the same between agents, with the NPC's group:
//                         WHICH is a shared NPC's id, `fresh` (the last NPC
//                         that came into the world beside a puppet) or
//                         `nearest` (to our V, within 60 m)
//   attitude fresh        "N same M differ": the NPCs that came beside a
//                         puppet, by what the engine's relations alone gave
//                         it next to V's (before any copy of ours)
//   wanted                our V's wanted level, 0..5 (the prevention
//                         system's own)
//   upload                the upload running on our V (an NPC's hack or
//                         trace), percent: its QuickHackUpload pool
//   prompt [player N | npc N]  the prompts on the first remote player's
//                         puppet (player N's, the proxy of NPC N), as
//                         Prompts.reds carries them: "PATH STATE layer on|off
//                         active B shown B choice KIND activations N shows N
//                         presses N". PATH game (the game's own interaction)
//                         or drawn; STATE how far the game's has proven
//                         itself there (unproven, activated, shown,
//                         pressed; drawn); active: our V activates the
//                         layer (the component's own list); shown: the
//                         game's prompt on screen holds our choice. none:
//                         no prompt was ever asked on that body
//   nameplate [path|apart] [player N]  the first remote player's nameplate
//                         (player N's), as PartyHud.reds placed it on its
//                         last tick: "engine X Y of W x H, within B, in
//                         screen B, uv U V, distance D (ours M m), slot
//                         'S', told N times; ours X Y, in view B; apart A
//                         units (P px, Q % of
//                         the height), plate on|off" (the engine's screen
//                         projection, in the HUD's units, beside our own
//                         projection of the same tick), or "script X Y of
//                         W x H px, in view B, plate on|off". path: engine
//                         or script alone; apart: Q alone (none: ours
//                         placed it). none: no such player
public class TandemTestQuery {
  public static func Run(game: GameInstance, system: ref<TandemSystem>, command: String) -> Void {
    // "parity [M]" comes this way too: a command of its own (Parity.reds),
    // kept out of RunCommand, which must not grow (AGENTS.md).
    if StrBeginsWith(command, "parity") {
      TandemParity.Command(game, system, command);
      return;
    }
    // "test query corpses [kill]": Corpses.reds.
    if StrBeginsWith(command, "test query corpses") {
      TandemCorpsesTest.Command(game, command);
      return;
    }
    let words = StrSplit(command, " ");
    let count = ArraySize(words);
    // (Padded: every word read below exists.)
    while ArraySize(words) < 6 {
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
        value = s"\(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2)) \(FloatToStringPrec(player.GetWorldYaw(), 1))";
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
    } else if Equals(what, "held") {
      let holder: ref<GameObject> = player;
      if Equals(arg, "puppet") {
        let others = system.PuppetPlayers();
        holder = null;
        if ArraySize(others) > 0 {
          holder = system.PuppetOf(others[0]);
        }
      } else if Equals(arg, "player") && count > 4 {
        holder = system.PuppetOf(Cast<Uint32>(StringToInt(words[4], 0)));
      } else if count > 3 {
        holder = null;
        value = "unknown";
      }
      if IsDefined(holder) {
        value = TandemTestQuery.Held(holder);
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
    } else if Equals(what, "remote") {
      value = TandemRemoteView.Query(game, Equals(arg, "player") ? Cast<Uint32>(StringToUint64(words[4])) : 0u);
    } else if Equals(what, "car") && Equals(arg, "player") && count > 4 {
      // Our copy of player N's car (driven, or the last it left parked).
      let body = TandemTraffic.Body(game, system.LastCarOf(Cast<Uint32>(StringToUint64(words[4]))));
      if IsDefined(body) {
        let at = body.GetWorldPosition();
        value = s"\(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2))";
      }
    } else if Equals(what, "traffic") && count > 4 && Equals(words[4], "position") {
      let traffic = TandemTraffic.Of(game);
      let body = IsDefined(traffic) ? TandemTraffic.Body(game, traffic.BodyOf(Cast<Uint32>(StringToUint64(arg)))) : null;
      if IsDefined(body) {
        let at = body.GetWorldPosition();
        value = s"\(FloatToStringPrec(at.X, 2)) \(FloatToStringPrec(at.Y, 2)) \(FloatToStringPrec(at.Z, 2))";
      }
    } else if Equals(what, "device") && count > 4 {
      let devices = TandemDevices.Get(game);
      if IsDefined(devices) {
        value = devices.Query(arg, words[4]);
      }
    } else if Equals(what, "paint") {
      let paints = system.Paint();
      if IsDefined(paints) && (count == 3 || Equals(arg, "puppet")) {
        value = paints.Query(system, Equals(arg, "puppet"));
      } else if IsDefined(paints) {
        value = "unknown";
      }
    } else if Equals(what, "attitude") {
      value = TandemTestQuery.Attitude(game, system, arg, words[4]);
    } else if Equals(what, "wanted") {
      let prevention = GameInstance.GetScriptableSystemsContainer(game).Get(n"PreventionSystem") as PreventionSystem;
      if IsDefined(prevention) {
        value = IntToString(Cast<Int32>(prevention.GetHeatStageAsInt()));
      }
    } else if Equals(what, "prompt") {
      let prompts = system.Prompts();
      let body: ref<GameObject>;
      if Equals(arg, "player") && count > 4 {
        body = system.PuppetOf(Cast<Uint32>(StringToInt(words[4], 0)));
      } else if Equals(arg, "npc") && count > 4 {
        body = GameInstance.FindEntityByID(game, system.ProxyOfNpc(Cast<Uint32>(StringToInt(words[4], 0)))) as GameObject;
      } else {
        let others = system.PuppetPlayers();
        if ArraySize(others) > 0 {
          body = system.PuppetOf(others[0]);
        }
      }
      if IsDefined(prompts) {
        value = prompts.StatusLine(body);
      }
    } else if Equals(what, "nameplate") {
      let presence = TandemPresenceSync.Of(game);
      let hud = IsDefined(presence) ? presence.Hud() : null;
      let part = Equals(arg, "player") ? "" : arg;
      let whose = Equals(arg, "player") ? words[4] : (Equals(words[4], "player") ? words[5] : "0");
      if IsDefined(hud) {
        value = hud.PlateQuery(Cast<Uint32>(StringToInt(whose, 0)), part);
      }
    } else if Equals(what, "upload") {
      let pools = GameInstance.GetStatPoolsSystem(game);
      if IsDefined(player) && pools.IsStatPoolAdded(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.QuickHackUpload) {
        value = FloatToStringPrec(pools.GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.QuickHackUpload, true), 2);
      }
    } else {
      value = "unknown";
    }
    if Equals(value, "unknown") {
      TandemNative.Log(s"test query: unknown: \(asked) (health [points], health low, item RECORD, money, fact NAME, journal PATH, position, mounted [player N | puppet], held [player N | puppet], npc ID health, threat puppet, device KIND attitude|puppet|target|alarm|mark, car marks, paint [puppet], attitude group NAME | npc ID|fresh|nearest | fresh, wanted, upload, prompt [player N | npc N], nameplate [path|apart] [player N])");
      return;
    }
    TandemNative.Log(s"test query: \(asked) = \(value)");
  }

  // What `holder` has in its hands, from the transaction system's slots,
  // and whether the game dual wields them: our V's weapon state machine
  // says so itself (the blackboard's IsDualWielding, set by its left hand's
  // equip state); a puppet has no such state, so the game's test of two
  // weapons in hand (GameObject.GetDualWieldWeaponNames: both there, both
  // tagged DualWield).
  private static func Held(holder: ref<GameObject>) -> String {
    let right = TandemWeapons.Held(holder, t"AttachmentSlots.WeaponRight");
    let left = TandemWeapons.Held(holder, t"AttachmentSlots.WeaponLeft");
    let dual = false;
    if holder.IsPlayer() {
      dual = GameObject.IsDualWielding(holder);
    } else {
      let first = TandemPresenceSync.WeaponOf(holder, false);
      let second = TandemPresenceSync.WeaponOf(holder, true);
      dual = IsDefined(first) && IsDefined(second) && first.WeaponHasTag(n"DualWield") && second.WeaponHasTag(n"DualWield");
    }
    let rightName = TDBID.IsValid(right) ? TDBID.ToStringDEBUG(right) : "none";
    let leftName = TDBID.IsValid(left) ? TDBID.ToStringDEBUG(left) : "none";
    return s"\(rightName) \(leftName) \(dual ? "dual" : "single")";
  }

  // The first puppet next to V: with a group, an NPC, or the count of
  // fresh NPCs (Attitudes.reds).
  private static func Attitude(game: GameInstance, system: ref<TandemSystem>, arg: String, which: String) -> String {
    let attitudes = system.Attitudes();
    let players = system.PuppetPlayers();
    if !IsDefined(attitudes) {
      return "none";
    }
    if Equals(arg, "fresh") {
      return attitudes.QueryFresh();
    }
    let body: ref<GameObject>;
    if ArraySize(players) > 0 {
      body = system.PuppetOf(players[0]);
    }
    if !IsDefined(body) || !IsDefined(body.GetAttitudeAgent()) || StrLen(which) == 0 {
      return "none";
    }
    if Equals(arg, "group") {
      return attitudes.QueryGroup(StringToName(which), body.GetAttitudeAgent().GetAttitudeGroup());
    }
    if !Equals(arg, "npc") {
      return "unknown";
    }
    let id = EntityID.FromHash(0ul);
    if Equals(which, "fresh") {
      id = attitudes.LastFresh();
    } else if Equals(which, "nearest") {
      let player = GetPlayer(game);
      let best = 3600.0;
      if IsDefined(player) {
        let around = player.GetNPCsAroundObject(60.0);
        for npc in around {
          let agent = npc.GetAttitudeAgent();
          let distance = Vector4.DistanceSquared(npc.GetWorldPosition(), player.GetWorldPosition());
          if IsDefined(agent) && !TandemAttitudes.IsOurs(agent.GetAttitudeGroup()) && distance < best {
            best = distance;
            id = npc.GetEntityID();
          }
        }
      }
    } else {
      let npcs = system.Npcs();
      if IsDefined(npcs) {
        id = npcs.EntityOfNpc(Cast<Uint32>(StringToInt(which, 0)));
      }
    }
    if !EntityID.IsDefined(id) {
      return "none";
    }
    return attitudes.QueryNpc(GameInstance.FindEntityByID(game, id) as GameObject, body);
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
