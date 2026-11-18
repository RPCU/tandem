// Where the other Vs are (docs/ARCHITECTURE.md, Inside the plugin): one
// marker per remote player, in the world, on the minimap and on the world
// map, the way the game marks your own car.
//
// The marker is a mappin of the game's own, attached to an entity so the
// mappin system moves it every frame: the player's puppet on foot, the car
// it sits in while seated (the "vehMappin" slot every vehicle has, the one
// the game uses for your car's marker). It is registered as soon as the
// puppet exists and unregistered when the player leaves.
//
// Its variant is gamedataMappinVariant.CPO_RemotePlayerVariant, the variant
// the game itself has for a remote player (PlayerPuppet.RegisterRemoteMappin,
// from the cut multiplayer: never registered in a single-player game), so the
// UI changes below touch no other marker. The single-player UI has no
// widget for it: each container is told to draw it with a vanilla one (the
// world: the quest marker widget with your car's profile; the minimap: the
// point-of-interest widget), with a vanilla icon, and labelled with the
// player's name (world label, world map tooltip). Its definition is
// Mappins.TandemPlayerMappin (tweaks/tandem/markers.tweak: the game's
// remote player definition, shown in the world, on the minimap and on the
// map), or the game's own when the tweak is missing.

public class TandemMarker {
  public let player: Uint32;
  public let name: String;
  public let puppet: EntityID;
  // The entity the mappin hangs on (the puppet or its car); none: waiting.
  public let attachedTo: EntityID;
  public let inCar: Bool;
  public let mappin: NewMappinID;
  // Not shown for now: the player is in another world (Elsewhere.reds).
  public let suppressed: Bool;
}

public class TandemMarkersTick extends DelayCallback {
  public let markers: wref<TandemMarkers>;

  public func Call() -> Void {
    if IsDefined(this.markers) {
      this.markers.Tick();
    }
  }
}

public class TandemMarkers extends ScriptableSystem {
  private let m_markers: array<ref<TandemMarker>>;
  private let m_lastTick: Float;
  private let m_reported: Bool;

  // The icon (a part of the game's mappin atlas, as MappinUIUtils names them).
  public static func Icon() -> CName {
    return n"fixer";
  }

  public static func IsOurs(variant: gamedataMappinVariant) -> Bool {
    return Equals(variant, gamedataMappinVariant.CPO_RemotePlayerVariant);
  }

  public static func Get(game: GameInstance) -> ref<TandemMarkers> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemMarkers") as TandemMarkers;
  }

  // The car carrying a player who rides along with no seat (Transit Along):
  // ours when we carry them, else our copy of the carrier's car; null when
  // they do not ride along or no such car is here.
  public static func CarrierCar(game: GameInstance, player: Uint32) -> ref<VehicleObject> {
    if TandemNative.RemoteTransitKind(player) != 4 {
      return null;
    }
    let carrier = TandemNative.RemoteTransitCarrier(player);
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    if carrier == 0u || !IsDefined(system) {
      return null;
    }
    if carrier == system.You() {
      let ours: wref<VehicleObject>;
      if VehicleComponent.GetVehicle(game, GetPlayer(game), ours) && IsDefined(ours) {
        return ours;
      }
      return null;
    }
    return GameInstance.GetDynamicEntitySystem().GetEntity(system.CarOf(carrier)) as VehicleObject;
  }

  // ---- from TandemSystem: a puppet came, a puppet goes ----

  public static func Show(game: GameInstance, player: Uint32, name: String, puppet: EntityID) -> Void {
    let markers = TandemMarkers.Get(game);
    if IsDefined(markers) {
      markers.Add(player, name, puppet);
    }
  }

  // A player's marker off for now (on: false, back), its puppet kept.
  public static func Suppress(game: GameInstance, player: Uint32, on: Bool) -> Void {
    let markers = TandemMarkers.Get(game);
    if IsDefined(markers) {
      markers.SetSuppressed(player, on);
    }
  }

  public func SetSuppressed(player: Uint32, on: Bool) -> Void {
    for marker in this.m_markers {
      if marker.player == player && NotEquals(marker.suppressed, on) {
        marker.suppressed = on;
        if on {
          this.Unregister(marker);
        }
        TandemNative.Log(s"marker: player \(player) \(on ? "off for now" : "back")");
      }
    }
  }

  public static func Hide(game: GameInstance, player: Uint32) -> Void {
    let markers = TandemMarkers.Get(game);
    if IsDefined(markers) {
      markers.Remove(player);
    }
  }

  // ---- from the UI ----

  // The player a mappin marks, by name ("" if it is not one of ours).
  public static func NameOf(mappin: wref<IMappin>) -> String {
    if !IsDefined(mappin) || !TandemMarkers.IsOurs(mappin.GetVariant()) {
      return "";
    }
    let markers = TandemMarkers.Get(GetGameInstance());
    return IsDefined(markers) ? markers.PlayerName(mappin.GetNewMappinID()) : "";
  }

  // The player a mappin marks (0 if it is not one of ours): Travel.reds.
  public static func PlayerOf(mappin: wref<IMappin>) -> Uint32 {
    if !IsDefined(mappin) || !TandemMarkers.IsOurs(mappin.GetVariant()) {
      return 0u;
    }
    let markers = TandemMarkers.Get(GetGameInstance());
    if !IsDefined(markers) {
      return 0u;
    }
    let id = mappin.GetNewMappinID();
    for marker in markers.m_markers {
      if marker.mappin.value != 0ul && marker.mappin.value == id.value {
        return marker.player;
      }
    }
    return 0u;
  }

  public func PlayerName(id: NewMappinID) -> String {
    for marker in this.m_markers {
      if marker.mappin.value != 0ul && marker.mappin.value == id.value {
        return marker.name;
      }
    }
    return "";
  }

  // ---- the markers ----

  public func Add(player: Uint32, name: String, puppet: EntityID) -> Void {
    this.Remove(player);
    let marker = new TandemMarker();
    marker.player = player;
    marker.name = name;
    marker.puppet = puppet;
    ArrayPush(this.m_markers, marker);
    if !this.m_reported {
      this.m_reported = true;
      this.ReportDefinition();
    }
    // A tick may be pending; else (none yet, or lost with the last world)
    // start one.
    let now = this.Now();
    if this.m_lastTick == 0.0 || now < this.m_lastTick || now - this.m_lastTick > 1.0 {
      this.Tick();
    }
  }

  public func Remove(player: Uint32) -> Void {
    let kept: array<ref<TandemMarker>>;
    for marker in this.m_markers {
      if marker.player == player {
        this.Unregister(marker);
        TandemNative.Log(s"marker: player \(player) removed");
      } else {
        ArrayPush(kept, marker);
      }
    }
    this.m_markers = kept;
  }

  // Four times a second while there are markers: register the waiting ones,
  // move each to the car its player sits in, or back to the body.
  public func Tick() -> Void {
    if ArraySize(this.m_markers) == 0 {
      this.m_lastTick = 0.0;
      return;
    }
    this.m_lastTick = this.Now();
    let game = this.GetGameInstance();
    let entities = GameInstance.GetDynamicEntitySystem();
    for marker in this.m_markers {
      let body = entities.GetEntity(marker.puppet) as GameObject;
      if IsDefined(body) && !marker.suppressed {
        let target: wref<GameObject> = body;
        let car: wref<VehicleObject>;
        let inCar = VehicleComponent.GetVehicle(game, marker.puppet, car) && IsDefined(car);
        if inCar {
          target = car;
        } else {
          // Riding along with no seat (Transit.reds): its body is hidden and
          // still; the marker goes on the car that carries it.
          let carrier = TandemMarkers.CarrierCar(game, marker.player);
          if IsDefined(carrier) {
            target = carrier;
            inCar = true;
          }
        }
        if !EntityID.IsDefined(marker.attachedTo) || marker.attachedTo != target.GetEntityID() {
          this.Unregister(marker);
          this.Register(marker, target, inCar);
        }
      }
    }
    let tick = new TandemMarkersTick();
    tick.markers = this;
    GameInstance.GetDelaySystem(game).DelayCallback(tick, 0.25, false);
  }

  private func Register(marker: ref<TandemMarker>, target: wref<GameObject>, inCar: Bool) -> Void {
    let data: MappinData;
    data.mappinType = TandemMarkers.Definition();
    data.variant = gamedataMappinVariant.CPO_RemotePlayerVariant;
    data.active = true;
    data.visibleThroughWalls = true;
    let slot = n"vehMappin";
    let offset: Vector3;
    if !inCar {
      // Above the head: the slot NPC markers use, when the body has it (V's
      // cutscene body may not), else 2 m above its feet.
      let transform: WorldTransform;
      let slots = target.GetUISlotComponent();
      if IsDefined(slots) && slots.GetSlotTransform(n"roleMappin", transform) {
        slot = n"roleMappin";
      } else {
        slot = n"";
        offset = new Vector3(0.0, 0.0, 2.0);
      }
    }
    marker.mappin = GameInstance.GetMappinSystem(this.GetGameInstance()).RegisterMappinWithObject(data, target, slot, offset);
    marker.attachedTo = target.GetEntityID();
    marker.inCar = inCar;
    let place = inCar ? "their car" : "the puppet";
    let at = NameToString(slot);
    let registered = marker.mappin.value != 0ul;
    TandemNative.Log(s"marker: player \(marker.player) (\(marker.name)) on \(place), slot '\(at)', registered \(registered)");
  }

  private func Unregister(marker: ref<TandemMarker>) -> Void {
    if marker.mappin.value != 0ul {
      GameInstance.GetMappinSystem(this.GetGameInstance()).UnregisterMappin(marker.mappin);
    }
    let none: NewMappinID;
    let nobody: EntityID;
    marker.mappin = none;
    marker.attachedTo = nobody;
  }

  public static func Definition() -> TweakDBID {
    return IsDefined(TweakDBInterface.GetMappinDefinitionRecord(t"Mappins.TandemPlayerMappin"))
      ? t"Mappins.TandemPlayerMappin" : t"Mappins.CPO_RemotePlayerMappinDefinition";
  }

  // (tools/game.sh cmd markers) every marker and where the mappin system has it.
  public func Report() -> Void {
    this.ReportDefinition();
    if ArraySize(this.m_markers) == 0 {
      TandemNative.Log("markers: none");
    }
    let mappins = GameInstance.GetMappinSystem(this.GetGameInstance());
    for marker in this.m_markers {
      if marker.mappin.value == 0ul {
        TandemNative.Log(s"markers: player \(marker.player) (\(marker.name)) waiting for the puppet");
      } else {
        let mappin = mappins.GetMappin(marker.mappin);
        if IsDefined(mappin) {
          let at = mappin.GetWorldPosition();
          let place = marker.inCar ? "their car" : "the puppet";
          TandemNative.Log(s"markers: player \(marker.player) (\(marker.name)) on \(place) at (\(at.X), \(at.Y), \(at.Z)), \(mappin.GetDistanceToPlayer()) m away, visible \(mappin.IsVisible())");
        } else {
          TandemNative.Log(s"markers: player \(marker.player) (\(marker.name)) registered, but the mappin system has no such mappin");
        }
      }
    }
  }

  private func ReportDefinition() -> Void {
    let id = TandemMarkers.Definition();
    let record = TweakDBInterface.GetMappinDefinitionRecord(id);
    if IsDefined(record) {
      TandemNative.Log(s"markers: definition \(TDBID.ToStringDEBUG(id)): in world \(record.ShowInWorld()), minimap \(record.ShowOnMinimap()), map \(record.ShowOnMap()), range \(record.VisibilityRange())");
    } else {
      TandemNative.Log(s"markers: no definition record \(TDBID.ToStringDEBUG(id))");
    }
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.GetGameInstance()));
  }
}

// ---- the game's UI, for our variant only ----

// In the world: the quest marker widget, with your car's profile.
@wrapMethod(WorldMappinsContainerController)
public func CreateMappinUIProfile(mappin: wref<IMappin>, mappinVariant: gamedataMappinVariant, customData: ref<MappinControllerCustomData>) -> MappinUIProfile {
  if TandemMarkers.IsOurs(mappinVariant) {
    return MappinUIProfile.Create(r"base\\gameplay\\gui\\widgets\\mappins\\quest\\default_mappin.inkwidget", t"MappinUISpawnProfile.Always", t"WorldMappinUIProfile.Vehicle");
  }
  return wrappedMethod(mappin, mappinVariant, customData);
}

// On the minimap: the point-of-interest widget, at any distance.
@wrapMethod(MinimapContainerController)
public func CreateMappinUIProfile(mappin: wref<IMappin>, mappinVariant: gamedataMappinVariant, customData: ref<MappinControllerCustomData>) -> MappinUIProfile {
  if TandemMarkers.IsOurs(mappinVariant) {
    return MappinUIProfile.Create(r"base\\gameplay\\gui\\widgets\\minimap\\minimap_poi_mappin.inkwidget", t"MappinUISpawnProfile.Always", t"MinimapMappinUIProfile.Default");
  }
  return wrappedMethod(mappin, mappinVariant, customData);
}

// The icon, everywhere (world, minimap, world map).
@wrapMethod(MappinUIUtils)
public final static func MappinToTexturePart(mappinVariant: gamedataMappinVariant) -> CName {
  if TandemMarkers.IsOurs(mappinVariant) {
    return TandemMarkers.Icon();
  }
  return wrappedMethod(mappinVariant);
}

// The world label: the player's name where the game writes a marker's name,
// else before the distance.
@wrapMethod(QuestMappinController)
protected cb func OnUpdate() -> Bool {
  let result = wrappedMethod();
  let name = TandemMarkers.NameOf(this.GetMappin());
  if NotEquals(name, "") {
    if inkWidgetRef.IsValid(this.displayName) {
      inkTextRef.SetText(this.displayName, name);
      inkWidgetRef.SetVisible(this.displayName, true);
    } else if inkWidgetRef.IsValid(this.distanceText) {
      inkTextRef.SetText(this.distanceText, s"\(name)  \(RoundF(this.GetDistanceToPlayer())) M");
      inkWidgetRef.SetVisible(this.distanceText, true);
    }
  }
  return result;
}

// On the world map: shown whatever the zoom and filters (no map filter
// group knows the variant).
@wrapMethod(BaseWorldMapMappinController)
private final func GetDesiredOpacityAndInteractivity(out opacity: Float, out interactive: Bool) {
  wrappedMethod(opacity, interactive);
  let mappin = this.GetMappin();
  if IsDefined(mappin) && TandemMarkers.IsOurs(mappin.GetVariant()) {
    opacity = 1.0;
    interactive = true;
  }
}

// The world map tooltip: the player's name.
@wrapMethod(WorldMapMenuGameController)
private final func UpdateTooltip(tooltipType: WorldMapTooltipType, controller: wref<BaseWorldMapMappinController>) {
  wrappedMethod(tooltipType, controller);
  if IsDefined(controller) {
    let name = TandemMarkers.NameOf(controller.GetMappin());
    if NotEquals(name, "") && IsDefined(this.m_tooltipController) {
      this.m_tooltipController.TandemTitle(name);
    }
  }
}

@addMethod(WorldMapTooltipContainer)
public final func TandemTitle(title: String) -> Void {
  let tooltip = this.m_defaultTooltipController as WorldMapTooltipController;
  if IsDefined(tooltip) {
    tooltip.TandemTitle(title);
  }
}

@addMethod(WorldMapTooltipController)
public final func TandemTitle(title: String) -> Void {
  inkTextRef.SetText(this.m_titleText, title);
  inkTextRef.SetText(this.m_descText, "Another V, in your city");
}
