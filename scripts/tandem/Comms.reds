// Players coordinating (docs/architecture/comms.md): pings, the link, shared
// tags and reveals, waypoints.
//
// - Pings. The scanner's tag key (TagButton, the middle mouse button by
//   default) outside the scanner marks what V looks at: an enemy, a device,
//   loot, a vehicle, else the place. Pressed again at once, the mark becomes
//   a danger; pressed on one of our live marks later, it is taken back.
//   Every player sees it in the world, on the minimap and the world map
//   (a mappin of the game's own, one per mark), with an icon for its kind
//   and a short UI sound, until it expires; a mark on an NPC, a device or a
//   car hangs on that entity, so an enemy's mark follows its proxy.
// - The link. Aimed at a partner's V, the same key asks to link (or accepts
//   their request, or breaks the link). Linked players share what each one
//   scans and tags (the scanner's own tag, through FocusModeTaggingSystem)
//   and what the Ping quickhack reveals (outlined for its length, the way
//   the game reveals for the automatic tagging perk). tandem.ini's `link`:
//   auto (every partner, the default), ask, off.
// - Waypoints. The map pin we place (or the map point we track) shows to
//   the others as "NAME's waypoint" in the world, on the minimap and map;
//   with neither, our tracked quest objective, as "NAME's objective".
//
// The decisions (rates, toggles, lifetimes, who is linked, what a vanished
// player leaves) are core's (tandem/comms.hpp) through
// plugin/src/comms_natives.cpp; this file is the engine side only.

// What one of our mappins is, for the UI (icon, world label, map tooltip).
public class TandemCommsMappinData extends MappinScriptData {
  public let icon: CName;
  public let label: String;
  public let description: String;
  // Whose it is: its icon takes that player's colour (Markers' ColorOf).
  public let player: Uint32;
}

// A linked partner's quickhack on a shared NPC or a device, drawn with the
// game's own quickhack indicator (QuickHackMappinController, as a vehicle's upload:
// VehicleComponent.CreateQuickHackMappin), tinted with that partner's
// colour. The controller is kept here once it draws it, so our tick drives
// its bar (no stat pool of the upload exists on our side).
public class TandemHackMappinData extends GameplayRoleMappinData {
  public let player: Uint32;
  public let upload: Bool;
  public let startedAt: Float;
  public let length: Float;
  public let controller: wref<QuickHackMappinController>;
}

public class TandemHackIndicator {
  // Its target: a shared NPC (npc), or a device (entity; npc 0).
  public let npc: Uint32;
  public let entity: EntityID;
  public let player: Uint32;
  public let upload: Bool;
  public let effect: TweakDBID;
  public let mappin: NewMappinID;
  public let data: ref<TandemHackMappinData>;
}

public class TandemCommsMark {
  public let player: Uint32;
  public let id: Uint32;
  public let kind: Int32;
  public let mappin: NewMappinID;
  public let target: EntityID;
}

public class TandemCommsWaypoint {
  public let player: Uint32;
  public let kind: Int32;
  public let position: Vector4;
  public let mappin: NewMappinID;
}

public class TandemComms {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_you: Uint32;
  private let m_now: Float;
  private let m_marks: array<ref<TandemCommsMark>>;
  private let m_waypoints: array<ref<TandemCommsWaypoint>>;
  private let m_listening: Bool;
  private let m_lastWaypointPoll: Float;
  // A partner's link request on screen (0: none), since when, the phone key.
  private let m_offerFrom: Uint32;
  private let m_offerAt: Float;
  private let m_offerListening: Bool;
  private let m_offerHeld: Bool;
  // The request rings as a call does (the game's ringtone, stopped with it).
  private let m_ringing: Bool;
  // Our team target (none: no hash), and when we last looked at it.
  public let m_ownTarget: EntityID;
  private let m_ownTargetSeen: Float;
  // Linked partners' quickhacks on our NPCs (proxies on a guest).
  private let m_hacks: array<ref<TandemHackIndicator>>;

  public static func Of(game: GameInstance) -> ref<TandemComms> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Comms() : null;
  }

  // Our mappins: their own variant (the cut multiplayer's "go here" ping,
  // never registered in single player), so the UI changes touch no other.
  public static func IsOurs(variant: gamedataMappinVariant) -> Bool {
    return Equals(variant, gamedataMappinVariant.CPO_PingGoHereVariant);
  }

  public static func DataOf(mappin: wref<IMappin>) -> ref<TandemCommsMappinData> {
    if !IsDefined(mappin) || !TandemComms.IsOurs(mappin.GetVariant()) {
      return null;
    }
    return mappin.GetScriptData() as TandemCommsMappinData;
  }

  public static func IconOf(mappin: wref<IMappin>) -> CName {
    let data = TandemComms.DataOf(mappin);
    return IsDefined(data) ? data.icon : n"";
  }

  public static func LabelOf(mappin: wref<IMappin>) -> String {
    let data = TandemComms.DataOf(mappin);
    return IsDefined(data) ? data.label : "";
  }

  public static func DescriptionOf(mappin: wref<IMappin>) -> String {
    let data = TandemComms.DataOf(mappin);
    return IsDefined(data) ? data.description : "";
  }

  public static func Definition() -> TweakDBID {
    return IsDefined(TweakDBInterface.GetMappinDefinitionRecord(t"Mappins.TandemPingMappin"))
      ? t"Mappins.TandemPingMappin" : t"Mappins.CPO_RemotePlayerMappinDefinition";
  }

  // A part of the mappin atlas per kind (MappinUIUtils' names), the word
  // the label says, the UI sound it plays. To pick by eye and ear in game
  // (scenario comms-pings).
  public static func Icon(kind: Int32) -> CName {
    switch kind {
      case 2:
        return n"Hit";
      case 3:
        return n"Use";
      case 4:
        return n"resource";
      case 5:
        return n"car";
      case 6:
        return n"failed_crossing";
      case 7:
        return n"Aim";
      default:
        return n"dynamic_event";
    }
  }

  // What a ping says, in the game's language (Word: the same in English, for
  // the log).
  public static func Label(kind: Int32) -> String {
    switch kind {
      case 2:
        return TandemText.Get("Comms-Enemy");
      case 3:
        return TandemText.Get("Comms-Device");
      case 4:
        return TandemText.Get("Comms-Loot");
      case 5:
        return TandemText.Get("Comms-Vehicle");
      case 6:
        return TandemText.Get("Comms-Danger");
      case 7:
        return TandemText.Get("Comms-Target");
      default:
        return TandemText.Get("Comms-Here");
    }
  }

  public static func Word(kind: Int32) -> String {
    switch kind {
      case 2:
        return "Enemy";
      case 3:
        return "Device";
      case 4:
        return "Loot";
      case 5:
        return "Vehicle";
      case 6:
        return "Danger";
      case 7:
        return "Target";
      default:
        return "Here";
    }
  }

  public static func SoundOf(kind: Int32) -> CName {
    if kind == 7 {
      return n"ui_hacking_access_granted";
    }
    return kind == 6 ? n"ui_jingle_chip_malfunction" : n"ui_menu_map_pin_created";
  }

  // ---- the team target: the "Mark target" quickhack ----

  // Offered on top of the panel's own programs (never in a cyberdeck slot)
  // while we are linked with someone: the wrap of
  // QuickhacksListGameController.OnQuickhackStarted, below.
  public static func OfferMarkTarget(value: ref<RevealInteractionWheel>) -> Void {
    let comms = TandemComms.Of(GetGameInstance());
    if !IsDefined(comms) || comms.m_you == 0u || !TandemCommsNative.AnyLinked() {
      return;
    }
    let target = value.lookAtObject;
    let owner = n"";
    for command in value.commands {
      if command.m_tandemMarkTarget {
        return; // offered already
      }
      if command.m_noQuickhackData {
        // No cyberdeck program at all: the panel shows its own "no
        // quickhacks" page and hides the list; the ping key stays the way.
        return;
      }
      owner = command.m_actionOwnerName;
    }
    let data = new QuickhackData();
    data.m_tandemMarkTarget = true;
    data.m_actionOwner = target.GetEntityID();
    data.m_actionOwnerName = owner;
    let clears = comms.m_ownTarget == target.GetEntityID();
    data.m_title = TandemText.Get(clears ? "Comms-ClearTitle" : "Comms-MarkTitle");
    data.m_description = TandemText.Get(clears ? "Comms-ClearDescription" : "Comms-MarkDescription");
    // The Ping program's look (an icon of the game's own, its category).
    let ping = TweakDBInterface.GetObjectActionRecord(t"QuickHack.FreePingHack");
    if IsDefined(ping) {
      if IsDefined(ping.ObjectActionUI()) && IsDefined(ping.ObjectActionUI().CaptionIcon()) && IsDefined(ping.ObjectActionUI().CaptionIcon().TexturePartID()) {
        data.m_icon = ping.ObjectActionUI().CaptionIcon().TexturePartID().GetID();
      }
      data.m_category = ping.HackCategory();
      if IsDefined(ping.GameplayCategory()) {
        data.m_iconCategory = ping.GameplayCategory().IconName();
      }
    }
    data.m_type = IsDefined(target as ScriptedPuppet) ? gamedataObjectActionType.PuppetQuickHack : gamedataObjectActionType.DeviceQuickHack;
    data.m_isLocked = false;
    data.m_actionState = EActionInactivityReson.Ready;
    data.m_isInstant = true;
    data.m_cost = 0;
    data.m_costRaw = 0;
    data.m_uploadTime = 0.0;
    data.m_duration = 0.0;
    data.m_ICELevelVisible = false;
    ArrayPush(value.commands, data);
  }

  // The panel ran it: our team target is what the panel aimed at.
  public static func MarkTargetFromPanel(game: GameInstance, id: EntityID) -> Void {
    let comms = TandemComms.Of(game);
    let target = GameInstance.FindEntityByID(game, id) as GameObject;
    if IsDefined(comms) && IsDefined(target) {
      comms.MarkTarget(target);
    }
  }

  public func MarkTarget(target: ref<GameObject>) -> Int32 {
    if this.m_you == 0u || !IsDefined(target) {
      return 0;
    }
    let npc = this.NpcOf(target);
    let player = IsDefined(this.m_system) ? this.m_system.PlayerOfPuppet(target.GetEntityID()) : 0u;
    let entity = EntityID.FromHash(0ul);
    if npc == 0u && player == 0u {
      if !EntityID.IsStatic(target.GetEntityID()) {
        TandemNative.Log("comms: team target not marked: this game's own entity, nobody else has it");
        this.Notice("Comms-OnlyShared");
        return 0;
      }
      entity = target.GetEntityID();
    }
    let result = TandemCommsNative.MarkTarget(npc, player, entity, target.GetWorldPosition());
    if result == 1 {
      this.m_ownTarget = target.GetEntityID();
      this.m_ownTargetSeen = this.m_now;
    } else if result == 3 {
      this.m_ownTarget = EntityID.FromHash(0ul);
    }
    TandemNative.Log(s"comms: team target (npc \(npc), static \(EntityID.IsDefined(entity))): \(result == 1 ? "marked" : result == 3 ? "cleared" : "not sent")");
    return result;
  }

  // Ours went down (dead, destroyed) or out of this world: taken back.
  private func WatchOwnTarget() -> Void {
    if !EntityID.IsDefined(this.m_ownTarget) {
      return;
    }
    let target = GameInstance.FindEntityByID(this.m_game, this.m_ownTarget) as GameObject;
    let car = target as VehicleObject;
    let gone = !IsDefined(target) || target.IsDead() || (IsDefined(car) && car.IsDestroyed());
    if gone {
      this.m_ownTarget = EntityID.FromHash(0ul);
      let cleared = TandemCommsNative.ClearTarget();
      TandemNative.Log(s"comms: our team target went down or away: \(cleared ? "cleared" : "already gone")");
    }
  }

  // test reset: the settings said again (scenarios read them).
  public func ResayForTests() -> Void {
    TandemNative.Log(s"comms: on (link \(TandemComms.LinkWord(TandemCommsNative.LinkSetting())), pings \(TandemCommsNative.PingsOn()))");
  }

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    let player = GetPlayer(game);
    if IsDefined(player) && !this.m_listening {
      player.RegisterInputListener(this, n"TagButton");
      this.m_listening = true;
    }
    TandemNative.Log(s"comms: on (link \(TandemComms.LinkWord(TandemCommsNative.LinkSetting())), pings \(TandemCommsNative.PingsOn()))");
  }

  public func Stop() -> Void {
    let player = GetPlayer(this.m_game);
    if IsDefined(player) && this.m_listening {
      player.UnregisterInputListener(this, n"TagButton");
    }
    this.m_listening = false;
    this.HideOffer();
    // Whatever the session's end did not say (the plugin says it: Update).
    this.Apply(TandemCommsNative.Update());
    for mark in this.m_marks {
      this.Unregister(mark.mappin);
    }
    ArrayClear(this.m_marks);
    for waypoint in this.m_waypoints {
      this.Unregister(waypoint.mappin);
    }
    ArrayClear(this.m_waypoints);
    this.ClearHacks();
  }

  public static func LinkWord(setting: Int32) -> String {
    return setting == 0 ? "auto" : setting == 1 ? "ask" : "off";
  }

  // ---- every tick (TandemSystem) ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_you = you;
    this.Apply(TandemCommsNative.Update());
    if now - this.m_lastWaypointPoll >= 1.0 || now < this.m_lastWaypointPoll {
      this.m_lastWaypointPoll = now;
      this.PollWaypoint();
    }
    if now - this.m_ownTargetSeen >= 0.5 || now < this.m_ownTargetSeen {
      this.m_ownTargetSeen = now;
      this.WatchOwnTarget();
    }
    this.DriveHacks();
    if this.m_offerFrom != 0u && now - this.m_offerAt > 15.0 {
      TandemNative.Log(s"comms: link request of \(this.NameOf(this.m_offerFrom)) left unanswered");
      this.HideOffer();
    }
  }

  private func Apply(events: array<TandemCommsEvent>) -> Void {
    for e in events {
      switch e.kind {
        case 1:
          this.ShowMark(e);
          break;
        case 2:
          this.HideMark(e.player, e.id);
          break;
        case 3:
          this.Outline(e, true);
          break;
        case 4:
          this.Outline(e, false);
          break;
        case 5:
          this.LinkChanged(e.player, e.type, e.before);
          break;
        case 6:
          this.WaypointOf(e);
          break;
        case 7:
          this.ShowHack(e, true);
          break;
        case 8:
          this.HideHack(e, true, TDBID.None());
          break;
        case 9:
          this.ShowHack(e, false);
          break;
        case 10:
          this.HideHack(e, false, e.effect);
          break;
        default:
          break;
      }
    }
  }

  // ---- the key ----

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Comms.TandemComms.OnAction");
    let name = ListenerAction.GetName(action);
    if Equals(name, n"PhoneInteract") {
      this.OnPhoneKey(action);
      return false;
    }
    if Equals(name, n"PhoneReject") {
      this.OnRejectKey(action);
      return false;
    }
    if !Equals(name, n"TagButton") || !ListenerAction.IsButtonJustPressed(action) {
      return false;
    }
    // In the scanner the key tags (shared through the tagging system).
    if this.InScanner() {
      return false;
    }
    this.PingLookAt();
    return false;
  }

  private func InScanner() -> Bool {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return false;
    }
    let blackboard = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    return IsDefined(blackboard) && blackboard.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vision) == 1;
  }

  // What V looks at, pinged (or, a partner's V: the link).
  public func PingLookAt() -> Int32 {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || this.m_you == 0u || !IsDefined(this.m_system) {
      return 0;
    }
    let targeting = GameInstance.GetTargetingSystem(this.m_game);
    let target = targeting.GetLookAtObject(player, true, true);
    let at = targeting.GetLookAtPosition(player, true, true);
    if Vector4.IsZero(at) {
      at = player.GetWorldPosition() + Vector4.Normalize(GameInstance.GetCameraSystem(this.m_game).GetActiveCameraForward()) * 10.0;
    }
    if IsDefined(target) {
      let partner = this.m_system.PlayerOfPuppet(target.GetEntityID());
      if partner == 0u {
        partner = this.m_system.PlayerOfCar(target.GetEntityID());
      }
      if partner != 0u {
        this.ToggleLink(partner);
        return 0;
      }
    }
    let kind = 1;
    let npc = 0u;
    let entity = EntityID.FromHash(0ul);
    if IsDefined(target) {
      kind = this.KindOf(target, player);
      if kind != 1 || IsDefined(target as ScriptedPuppet) {
        at = target.GetWorldPosition();
        npc = this.NpcOf(target);
        if npc == 0u && EntityID.IsStatic(target.GetEntityID()) {
          entity = target.GetEntityID();
        }
      }
    }
    let result = TandemCommsNative.Ping(kind, npc, 0u, entity, at);
    let said = result == 0 ? "not sent" : result == 1 ? "marked" : result == 2 ? "made a danger" : "taken back";
    TandemNative.Log(s"comms: ping \(TandemComms.Word(kind)) (npc \(npc), static \(EntityID.IsDefined(entity))) at (\(at.X), \(at.Y), \(at.Z)): \(said)");
    return result;
  }

  private func KindOf(target: ref<GameObject>, player: ref<GameObject>) -> Int32 {
    let puppet = target as ScriptedPuppet;
    if IsDefined(puppet) {
      return Equals(GameObject.GetAttitudeBetween(puppet, player), EAIAttitude.AIA_Hostile) ? 2 : 1;
    }
    if IsDefined(target as VehicleObject) {
      return 5;
    }
    if IsDefined(target as gameLootContainerBase) || IsDefined(target as gameItemDropObject) {
      return 4;
    }
    if IsDefined(target as Device) {
      return 3;
    }
    return 1;
  }

  private func NpcOf(target: ref<GameObject>) -> Uint32 {
    let npcs = IsDefined(this.m_system) ? this.m_system.NpcsSync() : null;
    return IsDefined(npcs) ? npcs.NpcOfEntity(target.GetEntityID()) : 0u;
  }

  // The entity a target names here (none: the place will do).
  private func EntityOf(e: TandemCommsEvent) -> ref<GameObject> {
    if e.targetKind == 1 {
      let npcs = IsDefined(this.m_system) ? this.m_system.NpcsSync() : null;
      if IsDefined(npcs) {
        return GameInstance.FindEntityByID(this.m_game, npcs.EntityOfNpc(e.npc)) as GameObject;
      }
    } else if e.targetKind == 2 {
      return GameInstance.FindEntityByID(this.m_game, e.entity) as GameObject;
    } else if e.targetKind == 3 && IsDefined(this.m_system) {
      return e.npc == this.m_you ? GetPlayer(this.m_game) : this.m_system.PuppetOf(e.npc);
    }
    return null;
  }

  // ---- marks ----

  private func ShowMark(e: TandemCommsEvent) -> Void {
    this.HideMark(e.player, e.id);
    let at = new Vector4(e.x, e.y, e.z, 1.0);
    let ours = e.player == this.m_you;
    let data = new TandemCommsMappinData();
    data.icon = TandemComms.Icon(e.type);
    data.player = e.player;
    data.label = ours ? TandemComms.Label(e.type) : TandemText.F("Comms-PingOf", this.NameOf(e.player), TandemComms.Label(e.type));
    data.description = ours ? TandemText.Get("Comms-YourPing") : TandemText.F("Comms-TheirPing", this.NameOf(e.player));
    if e.type == 7 {
      data.description = ours ? TandemText.Get("Comms-YourTarget") : TandemText.F("Comms-TheirTarget", this.NameOf(e.player));
    }
    let mark = new TandemCommsMark();
    mark.player = e.player;
    mark.id = e.id;
    mark.kind = e.type;
    // On the entity when it is here and where the sender saw it (else a
    // proxy elsewhere, a static id reused): it moves with it.
    let target = this.EntityOf(e);
    if IsDefined(target) && Vector4.Distance(target.GetWorldPosition(), at) < 15.0 {
      mark.mappin = this.RegisterOn(target, data);
      mark.target = target.GetEntityID();
    } else {
      mark.mappin = this.RegisterAt(at, data);
    }
    ArrayPush(this.m_marks, mark);
    // A partner's team target is outlined too, the way a shared tag is
    // (ours: the mark alone, nothing revealed on our side).
    if e.type == 7 && !ours && IsDefined(target) && EntityID.IsDefined(mark.target) {
      this.RevealTarget(target, true, e.seconds);
    }
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      GameObject.PlaySoundEvent(player, TandemComms.SoundOf(e.type));
    }
    let where = EntityID.IsDefined(mark.target) ? "on the entity" : "at a place";
    TandemNative.Log(s"comms: showing ping \(e.id) of \(ours ? "ours" : this.NameOf(e.player)) (\(TandemComms.Word(e.type)), \(e.seconds) s) \(where), registered \(mark.mappin.value != 0ul)");
  }

  private func HideMark(player: Uint32, id: Uint32) -> Void {
    let kept: array<ref<TandemCommsMark>>;
    for mark in this.m_marks {
      if mark.player == player && mark.id == id {
        this.Unregister(mark.mappin);
        if mark.kind == 7 && player == this.m_you {
          this.m_ownTarget = EntityID.FromHash(0ul); // expired, replaced, cleared
        }
        if mark.kind == 7 && player != this.m_you && EntityID.IsDefined(mark.target) {
          let body = GameInstance.FindEntityByID(this.m_game, mark.target) as GameObject;
          if IsDefined(body) {
            this.RevealTarget(body, false, 0.0);
          }
        }
        TandemNative.Log(s"comms: hid ping \(id) of player \(player)");
      } else {
        ArrayPush(kept, mark);
      }
    }
    this.m_marks = kept;
  }

  // As a linked player's reveal (Outline), under its own reason.
  private func RevealTarget(target: ref<GameObject>, on: Bool, seconds: Float) -> Void {
    let evt = new RevealObjectEvent();
    evt.reveal = on;
    evt.reason.reason = n"TandemTeamTarget";
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      evt.reason.sourceEntityId = player.GetEntityID();
    }
    evt.lifetime = on ? seconds : 0.0;
    target.QueueEvent(evt);
  }

  private func Data(data: ref<TandemCommsMappinData>) -> MappinData {
    let mappin: MappinData;
    mappin.mappinType = TandemComms.Definition();
    mappin.variant = gamedataMappinVariant.CPO_PingGoHereVariant;
    mappin.active = true;
    mappin.visibleThroughWalls = true;
    mappin.scriptData = data;
    return mappin;
  }

  // A marker another part of tandem places (Races.reds: the next checkpoint
  // of a partner's race), drawn as our pings are, in `player`'s colour.
  // Unregistered by its placer.
  public func PlaceMarker(at: Vector4, icon: CName, label: String, player: Uint32) -> NewMappinID {
    let data = new TandemCommsMappinData();
    data.icon = icon;
    data.label = label;
    data.description = "";
    data.player = player;
    return this.RegisterAt(at, data);
  }

  private func RegisterAt(at: Vector4, data: ref<TandemCommsMappinData>) -> NewMappinID {
    return GameInstance.GetMappinSystem(this.m_game).RegisterMappin(this.Data(data), at);
  }

  // Above an NPC's head (the slot NPC markers use), on a car's marker slot,
  // else half a metre above the object.
  private func RegisterOn(target: ref<GameObject>, data: ref<TandemCommsMappinData>) -> NewMappinID {
    let slot = n"";
    let offset = new Vector3(0.0, 0.0, 0.5);
    if IsDefined(target as VehicleObject) {
      slot = n"vehMappin";
      offset = new Vector3(0.0, 0.0, 0.0);
    } else {
      let transform: WorldTransform; // assigned on every path: read only when GetSlotTransform filled it
      let slots = target.GetUISlotComponent();
      if IsDefined(slots) && slots.GetSlotTransform(n"roleMappin", transform) {
        slot = n"roleMappin";
        offset = new Vector3(0.0, 0.0, 0.0);
      } else if IsDefined(target as ScriptedPuppet) || IsDefined(target as PlayerPuppet) {
        offset = new Vector3(0.0, 0.0, 2.0);
      }
    }
    return GameInstance.GetMappinSystem(this.m_game).RegisterMappinWithObject(this.Data(data), target, slot, offset);
  }

  private func Unregister(id: NewMappinID) -> Void {
    if id.value != 0ul {
      GameInstance.GetMappinSystem(this.m_game).UnregisterMappin(id);
    }
  }

  // ---- tags and reveals ----

  // Our tagging system tagged or untagged something (the wraps below).
  public func Tagged(target: ref<GameObject>, on: Bool) -> Void {
    if this.m_you == 0u || !IsDefined(target) || !TandemCommsNative.AnyLinked() {
      return;
    }
    let npc = this.NpcOf(target);
    let entity = EntityID.FromHash(0ul);
    if npc == 0u {
      if !EntityID.IsStatic(target.GetEntityID()) {
        return; // a body only this game has: nobody else could find it
      }
      entity = target.GetEntityID();
    }
    TandemCommsNative.Tag(0, on, 0, npc, entity);
    TandemNative.Log(s"comms: \(on ? "tagged" : "untagged") npc \(npc) (static \(EntityID.IsDefined(entity))) for the linked");
  }

  // Our game's Ping quickhack marked (or unmarked) a squad (the wrap below).
  public func Pinged(root: ref<GameObject>, members: array<EntityID>, on: Bool) -> Void {
    if this.m_you == 0u || !IsDefined(root) || !TandemCommsNative.AnyLinked() {
      return;
    }
    let seconds = on ? this.PingSeconds(root) : 0;
    let sent = 0;
    let everyone = members;
    ArrayPush(everyone, root.GetEntityID());
    for id in everyone {
      let body = GameInstance.FindEntityByID(this.m_game, id) as GameObject;
      if IsDefined(body) {
        let npc = this.NpcOf(body);
        if npc != 0u {
          TandemCommsNative.Tag(1, on, seconds, npc, EntityID.FromHash(0ul));
          sent += 1;
        }
      }
    }
    TandemNative.Log(s"comms: ping quickhack \(on ? "revealed" : "ended on") \(sent) shared NPC(s) for the linked (\(seconds) s)");
  }

  // Our Ping quickhack reached a device (Device.OnActionPing, queued by the
  // wrap below): the network pulses from it, and its beams to the devices
  // it is linked with are drawn in our game. The linked draw the same from
  // their own copy of that device (Outline).
  public func NetworkPinged(entity: EntityID) -> Void {
    if this.m_you == 0u || !TandemCommsNative.AnyLinked() || !EntityID.IsStatic(entity) {
      return;
    }
    let device = GameInstance.FindEntityByID(this.m_game, entity) as Device;
    if !IsDefined(device) {
      return;
    }
    let network = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"NetworkSystem") as NetworkSystem;
    let seconds = IsDefined(network) ? Cast<Int32>(network.GetSpacePingDuration()) + 1 : 10;
    TandemCommsNative.Tag(1, true, Clamp(seconds, 3, 60), 0u, entity);
    TandemNative.Log(s"comms: ping quickhack pulsed the network from device \(TandemNative.EntityHex(entity)) for the linked (\(seconds) s)");
  }

  private func PingSeconds(root: ref<GameObject>) -> Int32 {
    let longest = 0.0;
    for effect in StatusEffectHelper.GetAppliedEffects(root) {
      if IsDefined(effect) {
        let tags = effect.GetRecord().GameplayTags();
        if ArrayContains(tags, n"Ping") {
          longest = MaxF(longest, effect.GetRemainingDuration());
        }
      }
    }
    return longest > 0.0 ? Cast<Int32>(longest) + 1 : 30;
  }

  // A linked player's tag or reveal: drawn here the game's way.
  private func Outline(e: TandemCommsEvent, on: Bool) -> Void {
    let target = this.EntityOf(e);
    if !IsDefined(target) {
      TandemNative.Log(s"comms: \(on ? "outline" : "end of outline") of a target not here (kind \(e.targetKind))");
      return;
    }
    if e.type == 0 {
      let tagging = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"FocusModeTaggingSystem") as FocusModeTaggingSystem;
      if IsDefined(tagging) {
        tagging.TandemTag(target, on);
      }
    } else {
      // As the automatic tagging perk reveals (PlayerPuppet.OnBeingNoticed).
      let evt = new RevealObjectEvent();
      evt.reveal = on;
      evt.reason.reason = n"TandemLinkedPing";
      let player = GetPlayer(this.m_game);
      if IsDefined(player) {
        evt.reason.sourceEntityId = player.GetEntityID();
      }
      evt.lifetime = on ? e.seconds : 0.0;
      target.QueueEvent(evt);
      // A device a linked player's Ping reached: its network pulses from
      // our copy of it too, with the beams our own network system draws.
      let device = target as Device;
      if on && IsDefined(device) {
        device.TandemPulseNetwork();
        TandemNative.Log(s"comms: network pulsed from device \(TandemNative.EntityHex(device.GetEntityID())) for a linked player's ping");
      }
    }
    TandemNative.Log(s"comms: \(on ? "outlined" : "stopped outlining") \(e.type == 0 ? "a tag" : "a reveal") of a linked player (target kind \(e.targetKind))");
  }

  // ---- the link ----

  private func ToggleLink(player: Uint32) -> Void {
    let state = TandemCommsNative.LinkState(player);
    if TandemCommsNative.LinkSetting() == 2 {
      this.Notice("Comms-LinksOff");
      return;
    }
    // Linked or asked: break it; else ask (or accept their request).
    let on = state == 0 || state == 2;
    TandemCommsNative.SetLink(player, on);
    TandemNative.Log(s"comms: \(on ? "link with" : "unlink from") \(this.NameOf(player)) (was \(state))");
  }

  public func Link(player: Uint32, on: Bool) -> Bool {
    return TandemCommsNative.SetLink(player, on);
  }

  // The phone's call to a partner (PhoneContacts.reds): with nothing
  // between us, ask to link; their request, answer it; ours, cancel it;
  // linked, hang up (the link breaks).
  public func PhoneCall(player: Uint32) -> Bool {
    if TandemCommsNative.LinkSetting() == 2 {
      this.Notice("Comms-LinksOff");
      return false;
    }
    let state = TandemCommsNative.LinkState(player);
    let name = this.NameOf(player);
    if state == 2 && this.m_offerFrom == player {
      return this.AcceptOffer();
    }
    let on = state == 0 || state == 2;
    let what = "hanging up on";
    if state == 0 {
      what = "calling";
    } else if state == 1 {
      what = "cancelling our call to";
    } else if state == 2 {
      what = "answering";
    }
    TandemNative.Log(s"comms: \(what) \(name) (link was \(state))");
    let done = TandemCommsNative.SetLink(player, on);
    if done && state == 1 {
      this.Notice("Phone-Cancelled", name);
    } else if done && state == 0 && TandemCommsNative.LinkSetting() == 0 {
      // (LinkChanged says it for link = ask; auto says nothing of requests.)
      this.Notice("Comms-LinkSent", name);
    }
    return done;
  }

  private func LinkChanged(player: Uint32, now: Int32, before: Int32) -> Void {
    let name = this.NameOf(player);
    TandemNative.Log(s"comms: link with \(name): \(before) -> \(now)");
    if !TandemSync.InSession(player) {
      // Gone: the co-op menu says so ("NAME left the session").
      if this.m_offerFrom == player {
        this.HideOffer();
      }
      return;
    }
    if now == 3 {
      if this.m_offerFrom == player {
        this.HideOffer();
      }
      this.Notice("Comms-Linked", name);
      this.Sound(n"ui_hacking_access_granted");
    } else if before == 3 {
      this.Notice("Comms-LinkBroken", name);
      this.Sound(n"ui_hacking_close");
    } else if now == 2 && before == 0 {
      // A new request (not the standing consent of a link we broke).
      this.ShowOffer(player);
    } else if now == 1 && TandemCommsNative.LinkSetting() != 0 {
      // (link = auto consents to everyone: no request to speak of.)
      this.Notice("Comms-LinkSent", name);
    }
    if now != 2 && this.m_offerFrom == player {
      this.HideOffer();
    }
  }

  // Their request is a call: the game's on-screen message, its ringtone
  // (unless a call of the game's is up), and the phone keys' hints, answer
  // (a press, as a holocall) and decline (PhoneReject's hold), beside a
  // regroup offer's (Travel.reds) unless one has the key.
  private func ShowOffer(player: Uint32) -> Void {
    let name = this.NameOf(player);
    this.Notice("Comms-LinkAsked", name);
    let travel = TandemTravel.Of(this.m_game);
    if IsDefined(travel) && travel.ShowingOffer() {
      this.Sound(n"ui_phone_sms");
      TandemNative.Log(s"comms: link request of \(name) shown without its hint (a regroup offer has the key)");
      return;
    }
    this.HideOffer();
    this.m_offerFrom = player;
    this.m_offerAt = this.m_now;
    if this.CallUp() {
      this.Sound(n"ui_phone_sms");
    } else {
      GameInstance.GetAudioSystem(this.m_game).Play(n"ui_phone_incoming_call");
      this.m_ringing = true;
    }
    this.Hint(true, TandemText.F("Comms-LinkWith", name));
    TandemText.Hint(this.m_game, n"TandemLinkDecline", n"PhoneReject", true, TandemText.Get("Comms-Decline"), inkInputHintHoldIndicationType.Hold);
    let local = GetPlayer(this.m_game);
    if IsDefined(local) && !this.m_offerListening {
      local.RegisterInputListener(this, n"PhoneInteract");
      local.RegisterInputListener(this, n"PhoneReject");
      this.m_offerListening = true;
      this.m_offerHeld = false;
    }
    TandemNative.Log(s"comms: showing the link request of \(name)\(this.m_ringing ? " (ringing)" : "")");
  }

  private func HideOffer() -> Void {
    if this.m_offerFrom != 0u {
      this.Hint(false, "");
      TandemText.Hint(this.m_game, n"TandemLinkDecline", n"PhoneReject", false, "", inkInputHintHoldIndicationType.Hold);
    }
    this.m_offerFrom = 0u;
    if this.m_ringing {
      GameInstance.GetAudioSystem(this.m_game).Play(n"ui_phone_incoming_call_stop");
      this.m_ringing = false;
    }
    if this.m_offerListening {
      let local = GetPlayer(this.m_game);
      if IsDefined(local) {
        local.UnregisterInputListener(this, n"PhoneInteract");
        local.UnregisterInputListener(this, n"PhoneReject");
      }
      this.m_offerListening = false;
    }
  }

  // A call of the game's own is ringing or on (its keys are its own then).
  private func CallUp() -> Bool {
    let blackboard = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice);
    if !IsDefined(blackboard) {
      return false;
    }
    let info = blackboard.GetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation);
    if !IsDefined(info) {
      return false;
    }
    let call = FromVariant<PhoneCallInformation>(info);
    return Equals(call.callPhase, questPhoneCallPhase.IncomingCall) || Equals(call.callPhase, questPhoneCallPhase.StartCall);
  }

  private func OnPhoneKey(action: ListenerAction) -> Void {
    if this.m_offerFrom == 0u {
      return;
    }
    let kind = ListenerAction.GetType(action);
    if Equals(kind, gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.m_offerHeld = true; // the phone opens: not for us
    } else if Equals(kind, gameinputActionType.BUTTON_RELEASED) {
      if this.m_offerHeld {
        this.m_offerHeld = false;
      } else if !this.CallUp() {
        this.AcceptOffer();
      }
    }
  }

  // PhoneReject held, as for a holocall: the request is declined (it rings
  // no more; their consent stands, as an unanswered request's does).
  private func OnRejectKey(action: ListenerAction) -> Void {
    if this.m_offerFrom != 0u && !this.CallUp() && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_HOLD_COMPLETE) {
      this.DeclineOffer();
    }
  }

  public func DeclineOffer() -> Bool {
    let player = this.m_offerFrom;
    if player == 0u {
      TandemNative.Log("comms: no link request to decline");
      return false;
    }
    this.HideOffer();
    TandemNative.Log(s"comms: declined the link request of \(this.NameOf(player))");
    return true;
  }

  public func AcceptOffer() -> Bool {
    let player = this.m_offerFrom;
    if player == 0u {
      TandemNative.Log("comms: no link request to accept");
      return false;
    }
    this.HideOffer();
    TandemNative.Log(s"comms: accepting the link request of \(this.NameOf(player))");
    return TandemCommsNative.SetLink(player, true);
  }

  private func Hint(show: Bool, label: String) -> Void {
    let data: InputHintData;
    data.action = n"PhoneInteract";
    data.source = n"TandemLink";
    data.localizedLabel = label;
    data.queuePriority = 9;
    data.sortingPriority = 9;
    data.holdIndicationType = inkInputHintHoldIndicationType.Press;
    let evt = new UpdateInputHintEvent();
    evt.data = data;
    evt.show = show;
    evt.targetHintContainer = n"GameplayInputHelper";
    GameInstance.GetUISystem(this.m_game).QueueEvent(evt);
  }

  // ---- waypoints ----

  // Ours: the map pin we placed, or the map point we track; not one of
  // tandem's own markers (a partner's waypoint we track is still theirs).
  // With nothing tracked by hand: our tracked quest objective (kind 3).
  private func PollWaypoint() -> Void {
    let mappins = GameInstance.GetMappinSystem(this.m_game);
    let id = mappins.GetManuallyTrackedMappinID();
    let kind = 0;
    let at = new Vector4(0.0, 0.0, 0.0, 1.0);
    if id.value != 0ul {
      let mappin = mappins.GetMappin(id);
      if IsDefined(mappin) && !TandemComms.IsOurs(mappin.GetVariant()) && !TandemMarkers.IsOurs(mappin.GetVariant()) {
        kind = Equals(mappin.GetVariant(), gamedataMappinVariant.CustomPositionVariant) ? 1 : 2;
        at = mappin.GetWorldPosition();
      }
    } else {
      let objective = this.TrackedObjective();
      if objective.W > 0.5 {
        kind = 3;
        at = new Vector4(objective.X, objective.Y, objective.Z, 1.0);
      }
    }
    TandemCommsNative.SetWaypoint(kind, at);
    this.HideMirroredObjectives();
  }

  // Our tracked quest objective's quest mappin, the one nearest our V (the
  // world map's own: MenuHubGameController.UpdateTrackedQuest); W 0: none.
  // Not on a frozen guest: its tracked entry is the host's, already shown
  // by every frozen game's journal and by the host's own Waypoint.
  private func TrackedObjective() -> Vector4 {
    let none = new Vector4(0.0, 0.0, 0.0, 0.0);
    let story = TandemStorySystem.Get(this.m_game);
    if IsDefined(story) && story.Mirroring() {
      return none;
    }
    let journal = GameInstance.GetJournalManager(this.m_game);
    let objective = journal.GetTrackedEntry() as JournalQuestObjectiveBase;
    if !IsDefined(objective) {
      return none;
    }
    let positions: array<Vector3>;
    let hash = Cast<Uint32>(journal.GetEntryHash(objective));
    if !GameInstance.GetMappinSystem(this.m_game).GetQuestMappinPositionsByObjective(hash, positions) {
      return none;
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return none;
    }
    let from = player.GetWorldPosition();
    let best = none;
    let bestDistance = -1.0;
    for position in positions {
      let at = new Vector4(position.X, position.Y, position.Z, 1.0);
      let distance = Vector4.Distance(from, at);
      if bestDistance < 0.0 || distance < bestDistance {
        best = at;
        bestDistance = distance;
      }
    }
    return best;
  }

  // A frozen guest's journal is the host's: the host's objective is
  // already its own quest marker, so the host's Objective kind shows none.
  private func MirrorsObjectiveOf(player: Uint32) -> Bool {
    let story = TandemStorySystem.Get(this.m_game);
    return IsDefined(story) && story.Mirroring() && player == story.HostPlayer();
  }

  // One heard before the freeze began (the join's first second) goes.
  private func HideMirroredObjectives() -> Void {
    let kept: array<ref<TandemCommsWaypoint>>;
    for waypoint in this.m_waypoints {
      if waypoint.kind == 3 && this.MirrorsObjectiveOf(waypoint.player) {
        this.Unregister(waypoint.mappin);
        TandemNative.Log(s"comms: objective of \(this.NameOf(waypoint.player)) hidden: our journal is the host's");
      } else {
        ArrayPush(kept, waypoint);
      }
    }
    this.m_waypoints = kept;
  }

  private func WaypointOf(e: TandemCommsEvent) -> Void {
    let kept: array<ref<TandemCommsWaypoint>>;
    let hadKind = 0;
    for waypoint in this.m_waypoints {
      if waypoint.player == e.player {
        this.Unregister(waypoint.mappin);
        hadKind = waypoint.kind;
      } else {
        ArrayPush(kept, waypoint);
      }
    }
    this.m_waypoints = kept;
    let name = this.NameOf(e.player);
    if e.type == 0 {
      TandemNative.Log(s"comms: waypoint of \(name) gone");
      return;
    }
    if e.type == 3 && this.MirrorsObjectiveOf(e.player) {
      TandemNative.Log(s"comms: objective of \(name) not shown: our journal is the host's");
      return;
    }
    let waypoint = new TandemCommsWaypoint();
    waypoint.player = e.player;
    waypoint.kind = e.type;
    waypoint.position = new Vector4(e.x, e.y, e.z, 1.0);
    let data = new TandemCommsMappinData();
    data.player = e.player;
    data.icon = e.type == 1 ? n"dynamic_event" : n"quest";
    if e.type == 3 {
      data.label = TandemText.F("Comms-ObjectiveOf", name);
      data.description = TandemText.F("Comms-ObjectiveTracked", name);
    } else {
      data.label = TandemText.F("Comms-WaypointOf", name);
      data.description = TandemText.F(e.type == 1 ? "Comms-PinPlaced" : "Comms-PlaceTracked", name);
    }
    waypoint.mappin = this.RegisterAt(waypoint.position, data);
    ArrayPush(this.m_waypoints, waypoint);
    // Said for a waypoint placed by hand; an objective is there on its own
    // (each join, each step of a quest): shown without a word.
    if e.type != 3 && (hadKind == 0 || hadKind == 3) {
      this.Notice("Comms-WaypointSet", name);
      this.Sound(n"ui_menu_map_pin_on");
    }
    TandemNative.Log(s"comms: waypoint of \(name) (kind \(e.type)) at (\(e.x), \(e.y), \(e.z)), registered \(waypoint.mappin.value != 0ul)");
  }

  // ---- a linked partner's quickhacks ----

  // The colour a comms mappin's icon takes: its player's (none: white).
  public static func ColorOfMappin(mappin: wref<IMappin>) -> HDRColor {
    let data = TandemComms.DataOf(mappin);
    return TandemMarkers.ColorOf(IsDefined(data) ? data.player : 0u);
  }

  // What is left of a status effect on a target (0: not there, or no end).
  public static func EffectSeconds(target: ref<GameObject>, effect: TweakDBID) -> Float {
    if !IsDefined(target) {
      return 0.0;
    }
    for applied in StatusEffectHelper.GetAppliedEffects(target) {
      if IsDefined(applied) && applied.GetRecord().GetID() == effect {
        return MaxF(applied.GetRemainingDuration(), 0.0);
      }
    }
    return 0.0;
  }

  // The program's own icon (its caption icon), as the game's indicator
  // shows for an upload.
  private static func ProgramIcon(action: TweakDBID) -> TweakDBID {
    let record = TweakDBInterface.GetObjectActionRecord(action);
    if IsDefined(record) && IsDefined(record.ObjectActionUI()) && IsDefined(record.ObjectActionUI().CaptionIcon()) && IsDefined(record.ObjectActionUI().CaptionIcon().TexturePartID()) {
      return record.ObjectActionUI().CaptionIcon().TexturePartID().GetID();
    }
    return TDBID.None();
  }

  // What a hack event is on, for the log: "npc 70", "device 1a2b".
  private static func HackTarget(e: TandemCommsEvent) -> String {
    return e.targetKind == 2 ? s"device \(TandemNative.EntityHex(e.entity))" : s"npc \(e.npc)";
  }

  private func ShowHack(e: TandemCommsEvent, upload: Bool) -> Void {
    this.HideHack(e, upload, upload ? TDBID.None() : e.effect);
    let target = this.EntityOf(e);
    let what = upload ? "upload" : "effect";
    if !IsDefined(target) {
      TandemNative.Log(s"comms: \(this.NameOf(e.player))'s \(what) on \(TandemComms.HackTarget(e)): not here");
      return;
    }
    let data = new TandemHackMappinData();
    data.player = e.player;
    data.upload = upload;
    data.startedAt = this.m_now;
    data.length = e.seconds;
    data.m_mappinVisualState = EMappinVisualState.Default;
    data.m_progressBarType = upload ? EProgressBarType.UPLOAD : EProgressBarType.DURATION;
    data.m_progressBarContext = EProgressBarContext.QuickHack;
    data.m_duration = e.seconds;
    data.m_visibleThroughWalls = true;
    data.m_textureID = TandemComms.ProgramIcon(e.record);
    let mappin: MappinData;
    mappin.mappinType = t"Mappins.InteractionMappinDefinition";
    mappin.variant = gamedataMappinVariant.QuickHackVariant;
    mappin.active = true;
    mappin.visibleThroughWalls = true;
    mappin.scriptData = data;
    let hack = new TandemHackIndicator();
    hack.npc = e.targetKind == 1 ? e.npc : 0u;
    hack.entity = e.targetKind == 2 ? e.entity : EntityID();
    hack.player = e.player;
    hack.upload = upload;
    hack.effect = e.effect;
    hack.data = data;
    hack.mappin = GameInstance.GetMappinSystem(this.m_game).RegisterMappinWithObject(mappin, target, target.GetQuickHackIndicatorSlotName());
    ArrayPush(this.m_hacks, hack);
    TandemNative.Log(s"comms: showing \(this.NameOf(e.player))'s \(what) of \(TDBID.ToStringDEBUG(upload ? e.record : e.effect)) on \(TandemComms.HackTarget(e)) (\(e.seconds) s), registered \(hack.mappin.value != 0ul)");
  }

  // Off: a player's upload on a target, or an effect (none: all of theirs).
  private func HideHack(e: TandemCommsEvent, upload: Bool, effect: TweakDBID) -> Void {
    let kept: array<ref<TandemHackIndicator>>;
    let npc = e.targetKind == 1 ? e.npc : 0u;
    let entity = e.targetKind == 2 ? e.entity : EntityID();
    for hack in this.m_hacks {
      let match = hack.npc == npc && hack.entity == entity && hack.player == e.player && Equals(hack.upload, upload) && (upload || !TDBID.IsValid(effect) || hack.effect == effect);
      if match {
        if hack.mappin.value != 0ul {
          GameInstance.GetMappinSystem(this.m_game).UnregisterMappin(hack.mappin);
        }
        TandemNative.Log(s"comms: hid \(this.NameOf(e.player))'s \(upload ? "upload" : "effect") on \(TandemComms.HackTarget(e))");
      } else {
        ArrayPush(kept, hack);
      }
    }
    this.m_hacks = kept;
  }

  // Every tick: each drawn indicator's bar, as its own stat pool would.
  private func DriveHacks() -> Void {
    for hack in this.m_hacks {
      let data = hack.data;
      if IsDefined(data.controller) && data.length > 0.0 {
        data.controller.TandemProgress(ClampF((this.m_now - data.startedAt) / data.length, 0.0, 1.0));
      }
    }
  }

  private func ClearHacks() -> Void {
    for hack in this.m_hacks {
      if hack.mappin.value != 0ul {
        GameInstance.GetMappinSystem(this.m_game).UnregisterMappin(hack.mappin);
      }
    }
    ArrayClear(this.m_hacks);
  }

  // ---- helpers ----

  private func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  private func Sound(name: CName) -> Void {
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      GameObject.PlaySoundEvent(player, name);
    }
  }

  // As Travel.reds: the HUD's simple on-screen message.
  // TODO: localized strings (like the pause notice).
  // A notice by its key (Text.reds).
  private func Notice(key: String, opt a: String, opt b: String) -> Void {
    TandemText.NoticeAs(this.m_game, SimpleMessageType.Neutral, 4.0, key, a, b);
  }


  private func PlayerNamed(name: String) -> Uint32 {
    if !IsDefined(this.m_system) {
      return 0u;
    }
    for player in this.m_system.PuppetPlayers() {
      if StrLen(name) == 0 || Equals(TandemNative.PlayerName(player), name) {
        return player;
      }
    }
    return 0u;
  }

  // tools/game.sh cmd: comms report | comms ping | comms link [NAME] |
  // comms unlink [NAME] | comms accept | comms decline | comms tag |
  // comms target | comms objective
  public func Command(command: String) -> Void {
    if Equals(command, "comms hacks") {
      TandemNative.Log(s"comms: \(ArraySize(this.m_hacks)) partner quickhack indicator(s)");
      for hack in this.m_hacks {
        let left = MaxF(hack.data.length - (this.m_now - hack.data.startedAt), 0.0);
        TandemNative.Log(s"comms: \(hack.upload ? "upload" : "effect") of \(this.NameOf(hack.player)) on npc \(hack.npc), \(left) s left, drawn \(IsDefined(hack.data.controller))");
      }
    } else if Equals(command, "comms target") {
      // As the "Mark target" quickhack on what V looks at (the panel itself
      // is checked by hand: scenario comms-target).
      let player = GetPlayer(this.m_game);
      let target = IsDefined(player) ? GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, true, true) : null;
      if !IsDefined(target) {
        TandemNative.Log("comms: no target to mark");
      } else {
        this.MarkTarget(target);
      }
    } else if Equals(command, "comms report") {
      TandemNative.Log(TandemCommsNative.Report());
      TandemNative.Log(s"comms: \(ArraySize(this.m_marks)) ping marker(s), \(ArraySize(this.m_waypoints)) waypoint marker(s) here");
      for mark in this.m_marks {
        let mappin = GameInstance.GetMappinSystem(this.m_game).GetMappin(mark.mappin);
        let at = IsDefined(mappin) ? mappin.GetWorldPosition() : new Vector4(0.0, 0.0, 0.0, 0.0);
        TandemNative.Log(s"comms: ping \(mark.id) of player \(mark.player) (\(TandemComms.Word(mark.kind))) at (\(at.X), \(at.Y), \(at.Z)), visible \(IsDefined(mappin) && mappin.IsVisible())");
      }
      for player in this.m_system.PuppetPlayers() {
        TandemNative.Log(s"comms: link with \(this.NameOf(player)): \(TandemCommsNative.LinkState(player))");
      }
      for waypoint in this.m_waypoints {
        TandemNative.Log(s"comms: waypoint of \(this.NameOf(waypoint.player)) (kind \(waypoint.kind)) at (\(waypoint.position.X), \(waypoint.position.Y), \(waypoint.position.Z))");
      }
    } else if Equals(command, "comms objective") {
      // What PollWaypoint shares when nothing is tracked by hand.
      let objective = this.TrackedObjective();
      let tracked = GameInstance.GetMappinSystem(this.m_game).GetManuallyTrackedMappinID();
      let manual = tracked.value != 0ul;
      if objective.W < 0.5 {
        TandemNative.Log("comms: no tracked objective to share (none tracked, no quest mappin, or a frozen guest)");
      } else {
        TandemNative.Log(s"comms: our objective at (\(objective.X), \(objective.Y), \(objective.Z))\(manual ? ", not shared: a waypoint is tracked by hand" : ", shared")");
      }
    } else if Equals(command, "comms ping") {
      this.PingLookAt();
    } else if Equals(command, "comms accept") {
      this.AcceptOffer();
    } else if Equals(command, "comms decline") {
      this.DeclineOffer();
    } else if Equals(command, "comms tag") {
      let player = GetPlayer(this.m_game);
      let target = IsDefined(player) ? GameInstance.GetTargetingSystem(this.m_game).GetLookAtObject(player, true, true) : null;
      if IsDefined(target) {
        GameObject.TagObject(target);
        TandemNative.Log("comms: tagging what V looks at");
      } else {
        TandemNative.Log("comms: nothing to tag");
      }
    } else if StrBeginsWith(command, "comms link") || StrBeginsWith(command, "comms unlink") {
      let on = StrBeginsWith(command, "comms link");
      let rest = StrLen(command) > (on ? 11 : 13) ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "";
      let player = this.PlayerNamed(rest);
      if player == 0u {
        TandemNative.Log("comms: no such player here");
        return;
      }
      TandemNative.Log(s"comms: \(on ? "link with" : "unlink from") \(this.NameOf(player)): \(this.Link(player, on))");
    } else {
      TandemNative.Log(s"comms: unknown command \(command)");
    }
  }
}

// ---- shared tags: the scanner's own tagging ----

@addField(FocusModeTaggingSystem)
private let m_tandemRemote: Bool;

// A linked player's tag, applied as ours, without saying it back.
@addMethod(FocusModeTaggingSystem)
public final func TandemTag(target: ref<GameObject>, on: Bool) -> Void {
  this.m_tandemRemote = true;
  if on {
    this.TagObject(target);
  } else {
    this.UntagObject(target);
  }
  this.m_tandemRemote = false;
}

@wrapMethod(FocusModeTaggingSystem)
private final func TagObject(target: ref<GameObject>) {
  wrappedMethod(target);
  if !this.m_tandemRemote && IsDefined(target) && this.IsTagged(target) {
    let comms = TandemComms.Of(this.GetGameInstance());
    if IsDefined(comms) {
      comms.Tagged(target, true);
    }
  }
}

@wrapMethod(FocusModeTaggingSystem)
private final func UntagObject(target: ref<GameObject>) {
  wrappedMethod(target);
  if !this.m_tandemRemote && IsDefined(target) {
    let comms = TandemComms.Of(this.GetGameInstance());
    if IsDefined(comms) {
      comms.Tagged(target, false);
    }
  }
}

@wrapMethod(FocusModeTaggingSystem)
private final func UntagAll() {
  let comms = TandemComms.Of(this.GetGameInstance());
  if IsDefined(comms) {
    for object in this.GetTaggedObjectsList() {
      if IsDefined(object) {
        comms.Tagged(object, false);
      }
    }
  }
  wrappedMethod();
}

// ---- shared reveals: the Ping quickhack ----

@wrapMethod(PingSquadEffector)
public final func MarkSquad(mark: Bool, root: ref<GameObject>) {
  wrappedMethod(mark, root);
  if IsDefined(root) {
    let comms = TandemComms.Of(root.GetGame());
    if IsDefined(comms) {
      comms.Pinged(root, this.m_squadMembers, mark);
    }
  }
}

// The Ping quickhack on a device, or on an NPC of its network (the ping is
// passed to the network's access point, PuppetDeviceLinkPS
// .PingDevicesNetwork), ends here on a device: PulseNetwork draws the
// beams. An entity's event handler: queued only, shared on the tick
// (TandemComms.NetworkPinged).
@wrapMethod(Device)
protected cb func OnActionPing(evt: ref<PingDevice>) -> Bool {
  let result = wrappedMethod(evt);
  if TandemNative.IsStarted() {
    TandemStreaming.QueueStreamed(TandemStreamKind.NetworkPing(), this.GetEntityID(), n"None", true);
  }
  return result;
}

// A linked player's ping reached this device in their game: the same pulse
// from our copy (not a ping of ours: nothing is said back).
@addMethod(Device)
public func TandemPulseNetwork() -> Void {
  this.PulseNetwork(true);
}

// ---- per-kind icons (Markers.reds draws our variant like a remote player's) ----

@wrapMethod(QuestMappinController)
protected func UpdateIcon() {
  wrappedMethod();
  let icon = TandemComms.IconOf(this.GetMappin());
  if NotEquals(icon, n"") {
    inkImageRef.SetTexturePart(this.iconWidget, icon);
    inkWidgetRef.SetTintColor(this.iconWidget, TandemComms.ColorOfMappin(this.GetMappin()));
  }
}

@wrapMethod(MinimapPOIMappinController)
protected final func UpdateIcon() {
  wrappedMethod();
  let icon = TandemComms.IconOf(this.GetMappin());
  if NotEquals(icon, n"") {
    inkImageRef.SetTexturePart(this.iconWidget, icon);
    inkWidgetRef.SetTintColor(this.iconWidget, TandemComms.ColorOfMappin(this.GetMappin()));
  }
}

@wrapMethod(BaseWorldMapMappinController)
protected func UpdateIcon() {
  wrappedMethod();
  let icon = TandemComms.IconOf(this.GetMappin());
  if NotEquals(icon, n"") {
    inkImageRef.SetTexturePart(this.iconWidget, icon);
    inkWidgetRef.SetTintColor(this.iconWidget, TandemComms.ColorOfMappin(this.GetMappin()));
  }
}

// ---- the "Mark target" quickhack in the panel ----

// Our entry, apart from the cyberdeck's programs: no action behind it.
@addField(QuickhackData)
public let m_tandemMarkTarget: Bool;

// The panel's list as the target built it, with ours added on top of the
// equipped programs (never a slot of the deck) while we are linked.
@wrapMethod(QuickhacksListGameController)
protected cb func OnQuickhackStarted(value: ref<RevealInteractionWheel>) -> Bool {
  if IsDefined(value) && value.shouldReveal && IsDefined(value.lookAtObject) {
    TandemComms.OfferMarkTarget(value);
  }
  return wrappedMethod(value);
}

// Ours, chosen: the game's own accept animation, then our mark. Nothing is
// uploaded (no RAM, no upload, no trace, no alert: no action reaches the
// target).
@wrapMethod(QuickhacksListGameController)
private final func ApplyQuickHack(shouldUseUI: Bool) -> Bool {
  if IsDefined(this.m_selectedData) && this.m_selectedData.m_tandemMarkTarget {
    if shouldUseUI {
      this.PlayChoiceAnimation();
    }
    TandemComms.MarkTargetFromPanel(this.m_gameInstance, this.m_selectedData.m_actionOwner);
    return true;
  }
  return wrappedMethod(shouldUseUI);
}

// The target hears of the selection to preview area hacks from its action:
// ours has none.
@wrapMethod(QuickhacksListGameController)
private final func ApplyQuickhackSelection() {
  if IsDefined(this.m_selectedData) && this.m_selectedData.m_tandemMarkTarget {
    return;
  }
  wrappedMethod();
}

// ---- a partner's quickhack, on the game's own indicator ----

@addMethod(QuickHackMappinController)
public final func TandemData() -> ref<TandemHackMappinData> {
  return IsDefined(this.GetMappin()) ? this.GetMappin().GetScriptData() as TandemHackMappinData : null;
}

// Its icon and bar in the partner's colour (ours keep the game's).
@addMethod(QuickHackMappinController)
public final func TandemTint(player: Uint32) -> Void {
  let color = TandemMarkers.ColorOf(player);
  inkWidgetRef.SetTintColor(this.iconWidget, color);
  inkWidgetRef.SetTintColor(this.m_iconWidgetActive, color);
  inkWidgetRef.SetTintColor(this.m_bar, color);
}

@addMethod(QuickHackMappinController)
public final func TandemProgress(progress: Float) -> Void {
  this.OnStatsDataUpdated(progress);
}

@wrapMethod(QuickHackMappinController)
protected cb func OnIntro() -> Bool {
  TandemNative.HookRan(n"Comms.QuickHackMappinController.OnIntro");
  let result = wrappedMethod();
  let data = this.TandemData();
  if IsDefined(data) {
    data.controller = this;
    this.TandemTint(data.player);
  }
  return result;
}

// Ours has no action behind it (the partner's runs in another game): the
// view vanilla gives an upload or an effect, without reading one.
@wrapMethod(QuickHackMappinController)
private final func UpdateView() {
  let data = this.TandemData();
  if !IsDefined(data) {
    wrappedMethod();
    return;
  }
  if data.upload {
    inkWidgetRef.SetVisible(this.m_header, false);
    this.m_rootWidget.SetState(n"Upload");
  } else {
    this.m_rootWidget.SetState(n"Default");
  }
  this.TandemTint(data.player);
}
