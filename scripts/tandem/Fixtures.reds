// (test) tools/game.sh cmd fixture check [M]: what stands around our V, for
// the scenarios' named spots (tools/scenarios/fixtures.txt;
// docs/architecture/testing.md, "Fixtures"). It logs where our V is; the
// devices of our world within M m (default 60) by class, and the nearest of
// each kind the device commands take within 200 m, with where it stands
// (Devices.reds, Survey); the NPCs within M m, the vendors of our own world
// and those in a workspot among them, and the nearest vendor and the
// nearest NPC in a workspot within 200 m. World glass has no entity: not
// listed (the survey breaks it with `glass break`). Solo or in a session;
// the last line is "fixture: done".
//
// (test) fixture pins [N]: the N (default 16) nearest open-world activity
// pins of our world (the NCPD scanner's hustles, reported crimes, gang
// watches, outposts, hidden stashes: gang ground, where barrels, mines and
// sentries stand), nearest first: the places scenario.sh's find_spot tries
// when no fixture has the device it needs. One "fixture: pin" line each,
// then "fixture: pins done".
public class TandemFixtures {
  public static func Command(game: GameInstance, command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if Equals(verb, "pins") {
      let wanted = ArraySize(words) > 2 ? StringToInt(words[2], 16) : 16;
      TandemFixtures.Pins(game, wanted);
      return;
    }
    if !Equals(verb, "check") {
      TandemNative.Log(s"fixture: unknown command: \(command) (fixture check [M], fixture pins [N])");
      return;
    }
    let radius = 60.0;
    if ArraySize(words) > 2 {
      let asked = words[2];
      let value = StringToFloat(asked);
      if value > 0.0 {
        radius = value;
      }
    }
    let player = GetPlayer(game);
    if !IsDefined(player) {
      TandemNative.Log("fixture: no V");
      return;
    }
    let here = player.GetWorldPosition();
    TandemNative.Log(s"fixture: check within \(radius) m of our V at \(here.X) \(here.Y) \(here.Z) yaw \(player.GetWorldYaw())");
    TandemDevices.Get(game).Survey(radius);
    TandemFixtures.People(game, player, radius);
    TandemNative.Log("fixture: done");
  }

  private static func IsActivity(variant: gamedataMappinVariant) -> Bool {
    return Equals(variant, gamedataMappinVariant.MinorActivityVariant) || Equals(variant, gamedataMappinVariant.Zzz06_NCPDGigVariant) || Equals(variant, gamedataMappinVariant.GangWatchVariant) || Equals(variant, gamedataMappinVariant.OutpostVariant) || Equals(variant, gamedataMappinVariant.HiddenStashVariant);
  }

  // The nearest activity pins (MappinSystem.GetAllMappins, Codeware).
  private static func Pins(game: GameInstance, wanted: Int32) -> Void {
    let player = GetPlayer(game);
    let system = GameInstance.GetMappinSystem(game);
    if !IsDefined(player) || !IsDefined(system) {
      TandemNative.Log("fixture: pins: no V or no mappin system");
      TandemNative.Log("fixture: pins done");
      return;
    }
    let here = player.GetWorldPosition();
    let all = system.GetAllMappins();
    let places: array<Vector4>;
    let names: array<String>;
    let far: array<Float>;
    for mappin in all {
      if IsDefined(mappin) && TandemFixtures.IsActivity(mappin.GetVariant()) {
        let at = mappin.GetWorldPosition();
        ArrayPush(places, at);
        ArrayPush(names, EnumValueToString("gamedataMappinVariant", Cast<Int64>(EnumInt(mappin.GetVariant()))));
        ArrayPush(far, Vector4.Distance(here, at));
      }
    }
    let told = 0;
    let used: array<Bool>;
    for place in places {
      ArrayPush(used, false);
    }
    while told < wanted && told < ArraySize(places) {
      let best = -1;
      let i = 0;
      while i < ArraySize(places) {
        if !used[i] && (best < 0 || far[i] < far[best]) {
          best = i;
        }
        i += 1;
      }
      used[best] = true;
      let spot = places[best];
      TandemNative.Log(s"fixture: pin \(names[best]) at \(far[best]) m (\(spot.X) \(spot.Y) \(spot.Z))");
      told += 1;
    }
    TandemNative.Log(s"fixture: pins done (\(told) of \(ArraySize(places)) activity pins)");
  }

  // The NPCs around our V: vendors (placed, alive) and those playing a
  // workspot (sitting, leaning, smoking: npcs-anims-host's).
  private static func People(game: GameInstance, player: ref<PlayerPuppet>, radius: Float) -> Void {
    let here = player.GetWorldPosition();
    let workspots = GameInstance.GetWorkspotSystem(game);
    let near = 0;
    let vendors = 0;
    let seated = 0;
    let vendor: ref<NPCPuppet>;
    let vendorAt = 201.0;
    let sitter: ref<NPCPuppet>;
    let sitterAt = 201.0;
    for npc in player.GetNPCsAroundObject(200.0) {
      if IsDefined(npc) && !npc.IsDead() {
        let far = Vector4.Distance(here, npc.GetWorldPosition());
        let inside = far <= radius;
        if inside {
          near += 1;
        }
        if Equals(TandemVendors.Lacks(npc), "") {
          if inside {
            vendors += 1;
          }
          if far < vendorAt {
            vendor = npc;
            vendorAt = far;
          }
        }
        if workspots.IsActorInWorkspot(npc) {
          if inside {
            seated += 1;
          }
          if far < sitterAt {
            sitter = npc;
            sitterAt = far;
          }
        }
      }
    }
    TandemNative.Log(s"fixture: npcs within \(radius) m: \(near), vendors of our own world \(vendors), in a workspot \(seated)");
    if IsDefined(vendor) {
      let at = vendor.GetWorldPosition();
      TandemNative.Log(s"fixture: nearest vendor \(TDBID.ToStringDEBUG(vendor.GetRecordID())) entity \(TandemNative.EntityHex(vendor.GetEntityID())) at \(vendorAt) m (\(at.X) \(at.Y) \(at.Z))");
    } else {
      TandemNative.Log("fixture: no vendor within 200 m");
    }
    if vendors == 0 {
      TandemVendors.LogLacking(player, "fixture", radius);
    }
    if IsDefined(sitter) {
      let at = sitter.GetWorldPosition();
      TandemNative.Log(s"fixture: nearest in a workspot \(TDBID.ToStringDEBUG(sitter.GetRecordID())) at \(sitterAt) m (\(at.X) \(at.Y) \(at.Z))");
    } else {
      TandemNative.Log("fixture: nobody in a workspot within 200 m");
    }
  }
}
