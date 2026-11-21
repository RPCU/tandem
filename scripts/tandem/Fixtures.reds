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
public class TandemFixtures {
  public static func Command(game: GameInstance, command: String) -> Void {
    let words = StrSplit(command, " ");
    let verb = ArraySize(words) > 1 ? words[1] : "";
    if !Equals(verb, "check") {
      TandemNative.Log(s"fixture: unknown command: \(command) (fixture check [M])");
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
