// (test) tools/game.sh cmd parity [M]: what this game shows within M m
// (default 100) of any V it draws (ours and every puppet), read from the
// game's own systems, for tools/world-compare.py to set beside another
// game's dump of the same moment (docs/architecture/parity.md, "The
// checker"). Solo or in a session. Lines, in order:
//
//   parity: begin within M m, player P, our V at X Y Z
//   parity: puppet N at X Y Z
//   parity world: time S weather NAME rain I type T puddles P moisture W
//                 lights STATE paused B
//   parity heat: ours N
//   parity entity: HEX static|dynamic CLASS RECORD APPEARANCE at X Y Z
//                  dead|alive|- radio N|- body|-
//   parity device: HEX CLASS at X Y Z status S flags F value V (Devices.reds)
//   parity glass: (the session's record of what broke, ObjectsNatives)
//   then `street list` and `npc list` (when the build has it), the
//   vehicle and NPC lists tools/street-compare.py and npc-compare.py read
//   parity: end, N entities, M devices
//
// Entities are what the targeting system finds around each V (NPCs, dead
// or alive, vehicles, devices, item drops: whatever has a targeting
// component); world geometry, decals, particles, physics props without one
// and the crowd's flocks are not readable from scripts and are not listed.
public class TandemParity {
  public static func Command(game: GameInstance, system: ref<TandemSystem>, command: String) -> Void {
    let words = StrSplit(command, " ");
    let radius = 100.0;
    if ArraySize(words) > 1 {
      let asked = StringToFloat(words[1]);
      if asked > 0.0 {
        radius = asked;
      }
    }
    let player = GetPlayer(game);
    if !IsDefined(player) {
      TandemNative.Log("parity: no V");
      return;
    }
    let here = player.GetWorldPosition();
    TandemNative.Log(s"parity: begin within \(radius) m, player \(TandemNative.You()), our V at \(here.X) \(here.Y) \(here.Z)");
    let centers: array<ref<GameObject>>;
    ArrayPush(centers, player);
    let players = system.PuppetPlayers();
    for id in players {
      let body = system.PuppetOf(id);
      if IsDefined(body) {
        let at = body.GetWorldPosition();
        TandemNative.Log(s"parity: puppet \(id) at \(at.X) \(at.Y) \(at.Z)");
        ArrayPush(centers, body);
      }
    }
    TandemParity.World(game);
    let prevention = GameInstance.GetScriptableSystemsContainer(game).Get(n"PreventionSystem") as PreventionSystem;
    let heat = IsDefined(prevention) ? Cast<Int32>(prevention.GetHeatStageAsInt()) : -1;
    TandemNative.Log(s"parity heat: ours \(heat)");
    let entities = TandemParity.Entities(centers, radius);
    let devices = 0;
    let tracked = TandemDevices.Get(game);
    if IsDefined(tracked) {
      devices = tracked.ParityList(centers, radius);
    }
    TandemNative.Log(s"parity glass: \(TandemObjects.Report())");
    system.RunCommand("street list");
    system.RunCommand("npc list");
    TandemNative.Log(s"parity: end, \(entities) entities, \(devices) devices");
  }

  // Within `radius` of one of the Vs drawn here.
  public static func Within(object: ref<GameObject>, centers: array<ref<GameObject>>, radius: Float) -> Bool {
    let at = object.GetWorldPosition();
    for center in centers {
      if IsDefined(center) && Vector4.Distance(at, center.GetWorldPosition()) <= radius {
        return true;
      }
    }
    return false;
  }

  private static func World(game: GameInstance) -> Void {
    let weather = GameInstance.GetWeatherSystem(game);
    let rain = weather.GetRainIntensity();
    let intensity = weather.GetRainIntensityType();
    let kind = EnumInt(intensity);
    let puddles = weather.GetRainPuddles();
    let moisture = weather.GetRainMoisture();
    let lights = -1;
    let city = GameInstance.GetScriptableSystemsContainer(game).Get(n"CityLightSystem") as CityLightSystem;
    if IsDefined(city) {
      let forced = city.GetState();
      lights = EnumInt(forced);
    }
    let paused = GameInstance.GetTimeSystem(game).IsPausedState();
    TandemNative.Log(s"parity world: time \(TandemWorld.GameSeconds(game)) weather \(NameToString(TandemWorld.Weather(game))) rain \(FloatToStringPrec(rain, 3)) type \(kind) puddles \(FloatToStringPrec(puddles, 3)) moisture \(FloatToStringPrec(moisture, 3)) lights \(lights) paused \(paused)");
  }

  // Every entity the targeting system finds around each center, once.
  private static func Entities(centers: array<ref<GameObject>>, radius: Float) -> Int32 {
    let seen: array<Uint64>;
    let count = 0;
    for center in centers {
      let around = center.GetEntitiesAroundObject(radius);
      for entity in around {
        let object = entity as GameObject;
        if IsDefined(object) {
          let id = object.GetEntityID();
          let hash = EntityID.ToHash(id);
          if !ArrayContains(seen, hash) {
            ArrayPush(seen, hash);
            TandemParity.Entity(object);
            count += 1;
          }
        }
      }
    }
    return count;
  }

  private static func Entity(object: ref<GameObject>) -> Void {
    let id = object.GetEntityID();
    let at = object.GetWorldPosition();
    let record = "-";
    let state = "-";
    let radio = "-";
    let puppet = object as ScriptedPuppet;
    let vehicle = object as VehicleObject;
    if IsDefined(puppet) {
      record = TDBID.ToStringDEBUG(puppet.GetRecordID());
      state = object.IsDead() ? "dead" : "alive";
    } else if IsDefined(vehicle) {
      record = TDBID.ToStringDEBUG(vehicle.GetRecordID());
      state = vehicle.IsDestroyed() ? "dead" : "alive";
      let on = vehicle.IsRadioReceiverActive();
      radio = on ? IntToString(Cast<Int32>(vehicle.GetCurrentRadioIndex())) : "off";
    }
    let kind = EntityID.IsStatic(id) ? "static" : "dynamic";
    let body = TandemLedger.IsBody(id) ? "body" : "-";
    let look = NameToString(object.GetCurrentAppearanceName());
    TandemNative.Log(s"parity entity: \(TandemNative.EntityHex(id)) \(kind) \(NameToString(object.GetClassName())) \(record) \(StrLen(look) > 0 ? look : "-") at \(at.X) \(at.Y) \(at.Z) \(state) radio \(radio) \(body)");
  }
}
