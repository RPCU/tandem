// The party HUD (docs/architecture/presence.md): one entry per linked
// partner on the game's HUD layer, and a nameplate over each puppet.
//
// Built in script from the game's own ink widgets, fonts and colour styles
// (a custom .inkwidget from an archive crashed the client:
// engine-knowledge.md §9), on Codeware's HUD layer window, as the co-op
// menu's panel is (Coop.reds). Never interactive: it takes no input.
//
// An entry, like the game's own companion and boss bars, says the name, the
// health, and one line of state (down with the revive countdown, out, in a
// scene, elsewhere, in a menu), then the distance and an arrow when the
// camera does not see them, all in the partner's own colour
// (TandemMarkers.ColorOf). Only partners we are linked with have one
// (comms.md, the link: TandemCommsNative.LinkState): a link broken or a
// partner gone takes it away at the next frame. Health is the entry's
// alone: the nameplate over a puppet's head is its name, for every
// partner, linked or not; everyone else keeps their marker (Markers.reds).
// What it says is decided in core (tandem/presence.hpp: StatusOf, BearingOf,
// ProjectOf, NameplateLookOf), tested there.
//
// The nameplate follows tandem.ini nameplates: near (to 12 m, gone at 20 m),
// always (at any distance in view, smaller and fainter far away; only near
// ones while our V talks, scene tier 2), off.
//
// Hidden in photo mode, in cinematic scenes (our tier 3 and up), in a
// braindance, when the game hides its own HUD and when we are not in a
// session; the list alone with party_hud = off.
//
// The list's place (presence.md, Layout): top left under the game's health
// bar and buffs; below the holocall, compact, while a call is up; compact
// while the dialogue choice hub is up; hidden when there is no room. The
// places and sizes here are in the HUD's units (3840 x 2160 at 16:9), but
// the layer's window is in the game window's pixels: the list and the
// nameplates are drawn at the window's scale (Measure), so they are the
// same share of the screen at 720p as at 2160p, and the list keeps to the
// left edge at any aspect, as the health bar does.
//
// The nameplates are placed by the engine's own screen projection
// (inkScreenProjection, what the game's nameplate, damage numbers and
// subtitles over a head use): the engine follows the body's slot and gives
// the place in the HUD's own units, at its own field of view, aspect and
// scale, every frame. Only an inkProjectedHUDGameController can register
// one, and none can be made from script (a game controller comes from a
// widget library), so the game's damage numbers controller carries ours
// (presence.md, "The engine's projection": why that one). Our own
// projection (core: presence::ProjectOf, on the camera system's numbers)
// stays as the fallback, taken by itself when the carrier is not there,
// registers nothing, or says something far from ours for 3 s; one log line
// says which places the plates, `presence hud projection auto|engine|script`
// picks.

// A point the engine projects for us: a puppet's head, with its nameplate,
// or its chest (in view or not: nothing to place).
public class TandemPlateAnchor {
  public let projection: ref<inkScreenProjection>;
  // The plate the place goes to, while the engine places it.
  public let plate: wref<inkWidget>;
  // As last told, in the carrier's units (the HUD's).
  public let x: Float;
  public let y: Float;
  public let uvX: Float;
  public let uvY: Float;
  public let distance: Float;
  public let inScreen: Bool;
  public let updates: Int32;

  // The engine's word, each time it projects (inkScreenProjection.
  // RegisterListener, as the damage numbers listen to theirs).
  // job thread safe: writes this anchor's own numbers, which the tick only reads (a value one frame old at worst), and its own plate's margin, as every vanilla projection listener does
  protected cb func OnProjected(projection: ref<inkScreenProjection>) -> Bool {
    let at = projection.currentPosition;
    let uv = projection.uvPosition;
    this.x = at.X;
    this.y = at.Y;
    this.uvX = uv.X;
    this.uvY = uv.Y;
    this.distance = projection.distanceToCamera;
    this.inScreen = projection.IsInScreen();
    this.updates += 1;
    if IsDefined(this.plate) {
      this.plate.SetMargin(new inkMargin(at.X, at.Y, 0.0, 0.0));
    }
  }
}

// RegisterScreenProjection, UnregisterScreenProjection and WakeUp are
// protected natives of the projected controllers: reached through the
// damage numbers controller, which listens to each projection of its own
// (SetShouldNotifyProjections(false): no handler of its own is handed
// ours) and sleeps between hits (EnableSleeping).
@addMethod(DamageDigitsGameController)
public func TandemProject(data: inkScreenProjectionData) -> ref<inkScreenProjection> {
  return this.RegisterScreenProjection(data);
}

@addMethod(DamageDigitsGameController)
public func TandemUnproject(projection: ref<inkScreenProjection>) -> Void {
  this.UnregisterScreenProjection(projection);
}

@addMethod(DamageDigitsGameController)
public func TandemWake() -> Void {
  this.WakeUp();
}

public class TandemPartyEntry {
  public let player: Uint32;
  public let root: ref<inkVerticalPanel>;
  public let name: ref<inkText>;
  public let state: ref<inkText>;
  public let bar: ref<inkRectangle>;
  public let where: ref<inkHorizontalPanel>;
  public let arrow: ref<inkCanvas>;
  public let distance: ref<inkText>;
  // The nameplate over the puppet.
  public let plate: ref<inkVerticalPanel>;
  public let plateName: ref<inkText>;
  // What it shows, as last logged.
  public let status: Int32;
  public let health: Int32;
  public let inView: Bool;
  public let side: Int32;
  public let metres: Int32;
  public let plateOn: Bool;
  public let linked: Bool;
  public let loggedAt: Float;
  public let shown: Bool;
  // The nameplate this frame (Markers.reds hides the marker's name under it).
  public let plateNow: Bool;
  // The engine's projections of the body (none: ours places the plate).
  public let head: ref<TandemPlateAnchor>;
  public let chest: ref<TandemPlateAnchor>;
  public let anchoredTo: wref<GameObject>;
  public let slot: CName;
  // No slot on the body: a world point, moved each tick.
  public let fixed: Bool;
  // The engine places the plate this frame; as last logged.
  public let byEngine: Bool;
  public let loggedEngine: Bool;
  // Our own projection of the head this frame (0..1 from the top left),
  // and how far the engine's is from it (the carrier's units; -1: unknown).
  public let ownX: Float;
  public let ownY: Float;
  public let ownSeen: Bool;
  public let ownFar: Float;
  public let apart: Float;
  // Since when the engine's stands far from ours (-1: it does not).
  public let apartSince: Float;
  public let woken: Bool;
}

public class TandemPartyHud {
  private let m_game: GameInstance;
  private let m_root: ref<inkVerticalPanel>;
  private let m_plates: ref<inkCanvas>;
  private let m_entries: array<ref<TandemPartyEntry>>;
  private let m_visible: Bool;
  private let m_why: String;
  private let m_left: Float;
  private let m_top: Float;
  // The camera's field of view is read as vertical (true, the default) or
  // horizontal (research engine/presence.md, 13.35).
  private let m_fovVertical: Bool;
  private let m_now: Float;
  // Our V's scene tier, as Hidden read it.
  private let m_tier: Int32;
  // Below a holocall (HUD units, top left).
  private let m_callLeft: Float;
  private let m_callTop: Float;
  // What the list yields to now (Layout), as last logged; shown or why not.
  private let m_layout: String;
  private let m_compact: Bool;
  private let m_listOn: Bool;
  private let m_listWhy: String;
  // The test commands' cases (presence hud holocall|boss|choices|hudhidden
  // on|off|auto): -1 auto, 0 off, 1 on.
  private let m_force: array<Int32>;
  // The game's HUD modules (Scan).
  private let m_scannedAt: Float;
  private let m_boss: wref<inkWidget>;
  private let m_modules: array<wref<inkWidget>>;
  private let m_modulesLogged: Int32;
  // The HUD in its own units, and the screen's aspect (Measure).
  private let m_width: Float;
  private let m_height: Float;
  private let m_aspect: Float;
  // The game's window in pixels, which are the HUD layer window's units,
  // and a HUD unit in them (1 at 2160p, 1/3 at 720p).
  private let m_windowWidth: Float;
  private let m_windowHeight: Float;
  private let m_scale: Float;
  // What the game says of its own UI scale (logged beside ours).
  private let m_inverse: Float;
  // The engine's projection: the controller that carries ours (Scan), our
  // canvas under its root, its root's size (its units), the body slot the
  // head is read from, the path asked (0 auto, 1 engine, 2 ours), why the
  // engine's was given up ("": it was not), and the path as last logged.
  private let m_host: wref<DamageDigitsGameController>;
  private let m_projected: ref<inkCanvas>;
  private let m_hostWidth: Float;
  private let m_hostHeight: Float;
  private let m_slot: CName;
  private let m_projection: Int32;
  private let m_tripped: String;
  private let m_path: String;

  public static func BarWidth() -> Float {
    return 420.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    // Under the player's own health bar and buffs (the HUD's space is
    // 3840 x 2160 whatever the screen).
    this.m_left = 96.0;
    this.m_top = 340.0;
    // Below the holo portrait, which takes the left column under the
    // health bar (a first pick: presence.md, Layout, to check on screen).
    this.m_callLeft = 96.0;
    this.m_callTop = 1080.0;
    this.m_why = "no session";
    this.m_listOn = true;
    this.m_modulesLogged = -1;
    this.m_force = [-1, -1, -1, -1];
    this.m_width = 3840.0;
    this.m_height = 2160.0;
    this.m_aspect = 16.0 / 9.0;
    this.m_windowWidth = 3840.0;
    this.m_windowHeight = 2160.0;
    this.m_scale = 1.0;
    this.m_fovVertical = true;
    this.m_hostWidth = 3840.0;
    this.m_hostHeight = 2160.0;
    // Where the marker is (Markers.reds): the plate takes its label's place.
    this.m_slot = n"roleMappin";
    this.m_path = "-";
  }

  public func Stop() -> Void {
    this.Clear();
    if IsDefined(this.m_root) {
      this.m_root.Reparent(null);
    }
    if IsDefined(this.m_plates) {
      this.m_plates.Reparent(null);
    }
    if IsDefined(this.m_projected) {
      this.m_projected.Reparent(null);
    }
    this.m_root = null;
    this.m_plates = null;
    this.m_projected = null;
    this.m_host = null;
  }

  public func Clear() -> Void {
    for entry in this.m_entries {
      this.Remove(entry);
    }
    ArrayClear(this.m_entries);
  }

  public func Forget(player: Uint32) -> Void {
    let kept: array<ref<TandemPartyEntry>>;
    for entry in this.m_entries {
      if entry.player == player {
        this.Remove(entry);
        TandemNative.Log(s"hud: player \(player)'s entry removed");
      } else {
        ArrayPush(kept, entry);
      }
    }
    this.m_entries = kept;
  }

  private func Remove(entry: ref<TandemPartyEntry>) -> Void {
    // (Its plate back under our own canvas.)
    this.Unanchor(entry);
    if IsDefined(entry.root) && IsDefined(this.m_root) {
      this.m_root.RemoveChild(entry.root);
    }
    if IsDefined(entry.plate) && IsDefined(this.m_plates) {
      this.m_plates.RemoveChild(entry.plate);
    }
  }

  // Photo mode came on or went (heard at once: our tick stands still there).
  public func Refresh() -> Void {
    this.Apply();
  }

  // Whether a player's nameplate shows now (Markers.reds: one name over a
  // player, the plate's where it shows, else the marker's).
  public func PlateOn(player: Uint32) -> Bool {
    if !this.m_visible {
      return false;
    }
    for entry in this.m_entries {
      if entry.player == player {
        return entry.plateNow;
      }
    }
    return false;
  }

  public static func PlateShown(game: GameInstance, player: Uint32) -> Bool {
    let presence = TandemPresenceSync.Of(game);
    let hud = IsDefined(presence) ? presence.Hud() : null;
    return IsDefined(hud) && hud.PlateOn(player);
  }

  // ---- widgets ----

  private func Window() -> wref<inkCompoundWidget> {
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    return IsDefined(layer) ? layer.GetVirtualWindow() : null;
  }

  private func Build() -> Bool {
    if IsDefined(this.m_root) && IsDefined(this.m_plates) {
      return true;
    }
    let window = this.Window();
    if !IsDefined(window) {
      return false;
    }
    let plates = new inkCanvas();
    plates.SetName(n"tandem_nameplates");
    plates.SetAnchor(inkEAnchor.Fill);
    plates.SetInteractive(false);
    plates.Reparent(window);
    this.m_plates = plates;
    let root = new inkVerticalPanel();
    root.SetName(n"tandem_party");
    root.SetFitToContent(true);
    root.SetAnchor(inkEAnchor.TopLeft);
    // Scaled about its top left corner (Layout).
    root.SetRenderTransformPivot(new Vector2(0.0, 0.0));
    root.SetMargin(new inkMargin(this.m_left, this.m_top, 0.0, 0.0));
    root.SetInteractive(false);
    root.Reparent(window);
    this.m_root = root;
    TandemNative.Log("hud: party widget built on the HUD layer");
    return true;
  }

  // A text in the HUD's font: in a colour of the game's style (`style`), or
  // in a player's own colour (`player`, when not 0).
  private static func Text(name: CName, size: Int32, weight: CName, style: CName, player: Uint32) -> ref<inkText> {
    let text = new inkText();
    text.SetName(name);
    text.SetFontFamily("base\\gameplay\\gui\\fonts\\raj\\raj.inkfontfamily");
    text.SetFontStyle(weight);
    text.SetFontSize(size);
    text.SetLetterCase(textLetterCase.UpperCase);
    TandemPartyHud.Tint(text, style, player);
    text.SetFitToContent(true);
    text.SetInteractive(false);
    return text;
  }

  private static func Block(name: CName, width: Float, height: Float, style: CName, player: Uint32, opacity: Float) -> ref<inkRectangle> {
    let block = new inkRectangle();
    block.SetName(name);
    block.SetSize(new Vector2(width, height));
    TandemPartyHud.Tint(block, style, player);
    block.SetOpacity(opacity);
    block.SetInteractive(false);
    return block;
  }

  // The player's identity colour, the same in every game and on every mark
  // of theirs (marker, pings, quickhacks), else the game's style colour.
  private static func Tint(widget: ref<inkWidget>, style: CName, player: Uint32) -> Void {
    if player != 0u {
      widget.SetTintColor(TandemMarkers.ColorOf(player));
    } else {
      widget.SetStyle(r"base\\gameplay\\gui\\common\\main_colors.inkstyle");
      widget.BindProperty(n"tintColor", style);
    }
  }

  private func Entry(player: Uint32) -> ref<TandemPartyEntry> {
    for entry in this.m_entries {
      if entry.player == player {
        return entry;
      }
    }
    let entry = new TandemPartyEntry();
    entry.player = player;
    entry.status = -1;
    entry.health = -1;
    entry.metres = -1;
    entry.slot = n"None";
    entry.apart = -1.0;
    entry.apartSince = -1.0;

    let root = new inkVerticalPanel();
    root.SetName(n"tandem_party_entry");
    root.SetFitToContent(true);
    root.SetMargin(new inkMargin(0.0, 0.0, 0.0, 28.0));
    root.SetInteractive(false);
    root.SetVisible(false);
    root.Reparent(this.m_root);
    entry.root = root;

    // The name, as the game writes a character's over its bar.
    entry.name = TandemPartyHud.Text(n"name", 38, n"Medium", n"", player);
    entry.name.Reparent(root);

    // The health bar: a dim track and its fill.
    let track = new inkCanvas();
    track.SetName(n"health");
    track.SetSize(new Vector2(TandemPartyHud.BarWidth(), 10.0));
    track.SetMargin(new inkMargin(0.0, 6.0, 0.0, 6.0));
    track.SetInteractive(false);
    track.Reparent(root);
    let back = TandemPartyHud.Block(n"track", TandemPartyHud.BarWidth(), 10.0, n"", player, 0.25);
    back.Reparent(track);
    entry.bar = TandemPartyHud.Block(n"fill", TandemPartyHud.BarWidth(), 10.0, n"", player, 1.0);
    entry.bar.Reparent(track);

    // One line of state: down, out, in a scene, elsewhere, in a menu (the
    // HUD's red: it says something is wrong).
    entry.state = TandemPartyHud.Text(n"state", 30, n"Regular", n"MainColors.Red", 0u);
    entry.state.SetVisible(false);
    entry.state.Reparent(root);

    // Off screen: an arrow turned their way, and how far.
    let where = new inkHorizontalPanel();
    where.SetName(n"where");
    where.SetFitToContent(true);
    where.SetInteractive(false);
    where.SetVisible(false);
    where.Reparent(root);
    entry.where = where;
    // A chevron of two bars, pointing up, turned about its centre.
    let arrow = new inkCanvas();
    arrow.SetName(n"arrow");
    arrow.SetSize(new Vector2(32.0, 32.0));
    arrow.SetMargin(new inkMargin(0.0, 4.0, 14.0, 0.0));
    arrow.SetRenderTransformPivot(new Vector2(0.5, 0.5));
    arrow.SetInteractive(false);
    arrow.Reparent(where);
    let leftBar = TandemPartyHud.Block(n"left", 5.0, 22.0, n"", player, 1.0);
    leftBar.SetAnchor(inkEAnchor.Centered);
    leftBar.SetAnchorPoint(new Vector2(0.5, 0.5));
    leftBar.SetRenderTransformPivot(new Vector2(0.5, 0.5));
    leftBar.SetTranslation(new Vector2(-6.0, 0.0));
    leftBar.SetRotation(35.0);
    leftBar.Reparent(arrow);
    let rightBar = TandemPartyHud.Block(n"right", 5.0, 22.0, n"", player, 1.0);
    rightBar.SetAnchor(inkEAnchor.Centered);
    rightBar.SetAnchorPoint(new Vector2(0.5, 0.5));
    rightBar.SetRenderTransformPivot(new Vector2(0.5, 0.5));
    rightBar.SetTranslation(new Vector2(6.0, 0.0));
    rightBar.SetRotation(-35.0);
    rightBar.Reparent(arrow);
    entry.arrow = arrow;
    entry.distance = TandemPartyHud.Text(n"distance", 30, n"Regular", n"", player);
    entry.distance.Reparent(where);

    // The nameplate: the name alone, centred over the head; scaled about
    // its foot.
    let plate = new inkVerticalPanel();
    plate.SetName(n"tandem_nameplate");
    plate.SetFitToContent(true);
    plate.SetAnchor(inkEAnchor.TopLeft);
    plate.SetAnchorPoint(new Vector2(0.5, 1.0));
    plate.SetRenderTransformPivot(new Vector2(0.5, 1.0));
    plate.SetInteractive(false);
    plate.SetVisible(false);
    plate.Reparent(this.m_plates);
    entry.plate = plate;
    entry.plateName = TandemPartyHud.Text(n"name", 32, n"Medium", n"", player);
    entry.plateName.SetHAlign(inkEHorizontalAlign.Center);
    entry.plateName.Reparent(plate);

    ArrayPush(this.m_entries, entry);
    TandemNative.Log(s"hud: entry for player \(player) (\(TandemPartyHud.NameOf(player)))");
    return entry;
  }

  public static func NameOf(player: Uint32) -> String {
    let name = TandemNative.PlayerName(player);
    return StrLen(name) > 0 ? name : TandemText.Unnamed(player);
  }

  // The state line (in English for the log: scenarios read it).
  public static func StateText(status: Int32, place: Int32, reviveLeft: Float, opt english: Bool) -> String {
    switch status {
      case 1:
        return reviveLeft >= 0.0 ? TandemText.FE(english, "Party-Reviving", s"\(CeilF(reviveLeft))") : TandemText.T(english, "Party-Down");
      case 2:
        return TandemText.T(english, "Party-Out");
      case 3:
        return TandemText.T(english, TandemPartyHud.PlaceKey(place));
      case 4:
        return TandemText.T(english, "Party-Scene");
      case 5:
        return TandemText.T(english, "Party-Menu");
      default:
        return "";
    }
  }

  // Where a V elsewhere is (proto::Place, Elsewhere.reds).
  public static func PlaceKey(place: Int32) -> String {
    switch place {
      case 1:
        return "Party-Cyberspace";
      case 2:
        return "Party-Braindance";
      case 3:
        return "Party-Johnny";
      case 4:
        return "Party-OtherBody";
      default:
        return "Party-Elsewhere";
    }
  }

  // ---- where the list sits, and what it yields to (presence.md, Layout) ----

  // The space kept free above the bottom left corner: the game's hotkeys
  // (phone, consumable, grenade) and its radio toast.
  public static func BottomClear() -> Float {
    return 420.0;
  }

  // How tall an entry is, full (name, bar, state, where) or compact (name,
  // bar, and the state when there is one).
  public static func EntryHeight(compact: Bool) -> Float {
    return compact ? 110.0 : 170.0;
  }

  // A test command's answer for case `i` (0 holocall, 1 boss bar, 2 choice
  // hub, 3 the game's HUD hidden), else what the game says.
  private func Forced(i: Int32, auto: Bool) -> Bool {
    let forced = this.m_force[i];
    return forced < 0 ? auto : forced == 1;
  }

  // A holocall or an audio call is up (the phone's own blackboard: the
  // phase NewHudPhoneGameController shows its holo for), or a call between
  // players drawn the same way (Holocall.reds).
  private func HoloCall() -> Bool {
    if TandemComms.HolocallUp(this.m_game) {
      return true;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_ComDevice);
    if !IsDefined(board) {
      return false;
    }
    let value = board.GetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation);
    if !IsDefined(value) {
      return false;
    }
    let call = FromVariant<PhoneCallInformation>(value);
    return Equals(call.callPhase, questPhoneCallPhase.IncomingCall) || Equals(call.callPhase, questPhoneCallPhase.StartCall);
  }

  // The dialogue choice hub is up (dialogWidgetGameController's blackboard).
  private func Choices() -> Bool {
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return false;
    }
    let value = board.GetVariant(GetAllBlackboardDefs().UIInteractions.DialogChoiceHubs);
    if !IsDefined(value) {
      return false;
    }
    let hubs = FromVariant<DialogChoiceHubs>(value);
    return ArraySize(hubs.choiceHubs) > 0;
  }

  // The game's HUD modules on the HUD layer (Codeware's controllers), found
  // again every 2 s: the boss bar, and the health bar, minimap and quest
  // tracker whose visibility says whether the game shows its HUD.
  private func Scan(now: Float) -> Void {
    if this.m_scannedAt > 0.0 && now >= this.m_scannedAt && now - this.m_scannedAt < 2.0 {
      return;
    }
    this.m_scannedAt = now;
    this.m_boss = null;
    ArrayClear(this.m_modules);
    let layer = GameInstance.GetInkSystem().GetLayer(n"inkHUDLayer");
    if !IsDefined(layer) {
      return;
    }
    let host: wref<DamageDigitsGameController>;
    for controller in layer.GetGameControllers() {
      let module = controller as inkGameController;
      if IsDefined(module) {
        if IsDefined(module as BossHealthBarGameController) {
          this.m_boss = module.GetRootWidget();
        } else if IsDefined(module as DamageDigitsGameController) {
          host = module as DamageDigitsGameController;
        } else if IsDefined(module as healthbarWidgetGameController) || IsDefined(module as MinimapContainerController) || IsDefined(module as QuestTrackerGameController) {
          ArrayPush(this.m_modules, module.GetRootWidget());
        }
      }
    }
    if this.m_modulesLogged != ArraySize(this.m_modules) {
      this.m_modulesLogged = ArraySize(this.m_modules);
      TandemNative.Log(s"hud: the game's HUD modules followed: \(ArraySize(this.m_modules)) (health bar, minimap, quest tracker), boss bar \(IsDefined(this.m_boss) ? "found" : "not found")");
    }
    // Another carrier (the HUD was built again): ours start over on it.
    if host != this.m_host {
      this.Unproject();
      this.m_host = host;
    }
  }

  // ---- the engine's projection (presence.md, "The engine's projection") ----

  // Every projection of ours given back, the plates under our own canvas.
  private func Unproject() -> Void {
    for entry in this.m_entries {
      this.Unanchor(entry);
    }
    if IsDefined(this.m_projected) {
      this.m_projected.Reparent(null);
    }
    this.m_projected = null;
  }

  // The carrier's root and everything above it is shown.
  private func HostShown() -> Bool {
    let widget: wref<inkWidget> = this.m_host.GetRootWidget();
    let depth = 0;
    while IsDefined(widget) && depth < 16 {
      if !widget.IsVisible() {
        return false;
      }
      widget = widget.GetParentWidget();
      depth += 1;
    }
    return true;
  }

  // Whether the engine places the nameplates this frame, said in the log
  // when it changes; our canvas under the carrier's root, built once.
  private func Engine() -> Bool {
    let why = "";
    if this.m_projection == 2 {
      why = "asked: presence hud projection script";
    } else if this.m_projection == 0 && StrLen(this.m_tripped) > 0 {
      why = this.m_tripped;
    } else if !IsDefined(this.m_host) {
      why = "no DamageDigitsGameController on the HUD layer";
    } else if !this.HostShown() {
      why = "the game hides its damage numbers' widget";
    } else if !IsDefined(this.m_projected) {
      let root = this.m_host.GetRootCompoundWidget();
      if IsDefined(root) {
        let canvas = new inkCanvas();
        canvas.SetName(n"tandem_nameplates_projected");
        canvas.SetAnchor(inkEAnchor.Fill);
        canvas.SetInteractive(false);
        canvas.Reparent(root);
        this.m_projected = canvas;
        let size = root.GetSize();
        this.m_hostWidth = size.X > 0.0 ? size.X : this.m_width;
        this.m_hostHeight = size.Y > 0.0 ? size.Y : this.m_height;
      } else {
        why = "the damage numbers' controller has no root to draw under";
      }
    }
    let path = Equals(why, "") ? s"the engine's screen projection (carried by DamageDigitsGameController, its root \(RoundF(this.m_hostWidth)) x \(RoundF(this.m_hostHeight)), slot '\(NameToString(this.m_slot))')" : s"our own projection (\(why))";
    if NotEquals(path, this.m_path) {
      this.m_path = path;
      TandemNative.Log(s"hud: nameplates placed by \(path)");
      // Our own projection is the lesser path (degradations.md).
      if Equals(why, "") {
        TandemDegraded.Over("nameplate projection");
      } else {
        TandemDegraded.State("nameplate projection", why);
      }
    }
    if NotEquals(why, "") {
      this.Unproject();
    }
    return Equals(why, "");
  }

  // One projection of `body`: its slot lifted by `lift`, or the world point
  // `at` when it has no such slot.
  private func ProjectionOf(body: ref<GameObject>, component: CName, slot: CName, lift: Float, at: Vector4) -> ref<TandemPlateAnchor> {
    let anchor = new TandemPlateAnchor();
    // (Every field set: the VM does not clear a local.)
    let data: inkScreenProjectionData;
    if IsNameValid(slot) {
      data.entity = body;
      data.fixedWorldOffset = new Vector4(0.0, 0.0, lift, 0.0);
    } else {
      data.entity = null;
      data.fixedWorldOffset = new Vector4(0.0, 0.0, 0.0, 0.0);
    }
    data.slotComponentName = component;
    data.slotName = slot;
    data.slotFallbackName = slot;
    data.staticWorldPosition = at;
    data.userData = anchor;
    data.adjustWithDistance = false;
    data.extraOffsetAtMaxDistance = new Vector3(0.0, 0.0, 0.0);
    anchor.projection = this.m_host.TandemProject(data);
    if !IsDefined(anchor.projection) {
      return null;
    }
    anchor.projection.RegisterListener(anchor, n"OnProjected");
    anchor.projection.SetEnabled(true);
    return anchor;
  }

  // The engine follows `body` for this entry: its head (the slot the
  // marker sits in, else 2 m above its feet) and its chest, 1 m lower.
  private func Anchor(entry: ref<TandemPartyEntry>, body: ref<GameObject>, feet: Vector4) -> Void {
    let head = feet;
    head.Z += 2.0;
    let chest = feet;
    chest.Z += 1.0;
    if IsDefined(entry.head) && entry.anchoredTo == body {
      if entry.fixed {
        entry.head.projection.SetStaticWorldPosition(head);
        if IsDefined(entry.chest) {
          entry.chest.projection.SetStaticWorldPosition(chest);
        }
      }
      return;
    }
    this.Unanchor(entry);
    let slot = n"None";
    let component = n"None";
    let height = 2.0;
    let transform: WorldTransform; // assigned on every path: read only when GetSlotTransform filled it
    let slots = body.GetUISlotComponent();
    if IsNameValid(this.m_slot) && IsDefined(slots) && slots.GetSlotTransform(this.m_slot, transform) {
      slot = this.m_slot;
      component = slots.GetName();
      let there = WorldPosition.ToVector4(WorldTransform.GetWorldPosition(transform));
      height = there.Z - feet.Z;
    }
    entry.head = this.ProjectionOf(body, component, slot, 0.0, head);
    if !IsDefined(entry.head) {
      this.m_tripped = "RegisterScreenProjection gave nothing";
      return;
    }
    entry.chest = this.ProjectionOf(body, component, slot, -1.0, chest);
    this.m_host.TandemWake();
    entry.anchoredTo = body;
    entry.slot = slot;
    entry.fixed = !IsNameValid(slot);
    entry.apart = -1.0;
    entry.apartSince = -1.0;
    entry.woken = false;
    let how = entry.fixed ? s"no slot '\(NameToString(this.m_slot))' on the body: a world point 2 m above its feet, moved each tick" : s"slot '\(NameToString(slot))' of '\(NameToString(component))', \(FloatToStringPrec(height, 2)) m above the feet";
    TandemNative.Log(s"hud: player \(entry.player)'s nameplate anchored by the engine: \(how)");
  }

  private func Unlisten(anchor: ref<TandemPlateAnchor>) -> Void {
    if !IsDefined(anchor) || !IsDefined(anchor.projection) {
      return;
    }
    anchor.plate = null;
    anchor.projection.UnregisterListener(anchor, n"OnProjected");
    anchor.projection.SetEnabled(false);
    if IsDefined(this.m_host) {
      this.m_host.TandemUnproject(anchor.projection);
    }
    anchor.projection = null;
  }

  private func Unanchor(entry: ref<TandemPartyEntry>) -> Void {
    this.PlateUnder(entry, false);
    this.Unlisten(entry.head);
    this.Unlisten(entry.chest);
    entry.head = null;
    entry.chest = null;
    entry.anchoredTo = null;
    entry.apart = -1.0;
    entry.apartSince = -1.0;
  }

  // The plate under the carrier's root, where the engine's listener places
  // it, or back under our own canvas.
  private func PlateUnder(entry: ref<TandemPartyEntry>, engine: Bool) -> Void {
    if Equals(entry.byEngine, engine) {
      return;
    }
    entry.byEngine = engine;
    if !IsDefined(entry.plate) {
      return;
    }
    if engine {
      entry.plate.Reparent(this.m_projected);
      entry.plate.SetMargin(new inkMargin(entry.head.x, entry.head.y, 0.0, 0.0));
      entry.head.plate = entry.plate;
    } else {
      if IsDefined(entry.head) {
        entry.head.plate = null;
      }
      entry.plate.SetVisible(false);
      entry.plate.Reparent(this.m_plates);
    }
  }

  // The engine's place for the head against ours, when ours has the head
  // well inside the frame: never told, off screen, or a quarter of the
  // screen's height away, its controller is woken after 1 s (it sleeps
  // between hits) and the engine's projection given up after 3 s.
  private func Compare(entry: ref<TandemPartyEntry>, now: Float) -> Void {
    let inside = entry.ownSeen && entry.ownX > 0.15 && entry.ownX < 0.85 && entry.ownY > 0.15 && entry.ownY < 0.85;
    let wrong = false;
    entry.apart = -1.0;
    if entry.head.updates == 0 {
      wrong = inside;
    } else {
      let dx = entry.head.x - entry.ownX * this.m_hostWidth;
      let dy = entry.head.y - entry.ownY * this.m_hostHeight;
      entry.apart = SqrtF(dx * dx + dy * dy);
      wrong = inside && (!entry.head.inScreen || entry.apart > 0.25 * this.m_hostHeight);
    }
    if !wrong {
      entry.apartSince = -1.0;
      entry.woken = false;
      return;
    }
    if entry.apartSince < 0.0 {
      entry.apartSince = now;
      return;
    }
    let what = entry.head.updates == 0 ? "never told" : s"at \(RoundF(entry.head.x)) \(RoundF(entry.head.y)) (in screen \(entry.head.inScreen)), ours at \(RoundF(entry.ownX * this.m_hostWidth)) \(RoundF(entry.ownY * this.m_hostHeight))";
    if now - entry.apartSince >= 1.0 && !entry.woken {
      entry.woken = true;
      this.m_host.TandemWake();
      TandemNative.Log(s"hud: the engine's projection of player \(entry.player) \(what): its controller woken");
    }
    if now - entry.apartSince >= 3.0 && this.m_projection == 0 {
      this.m_tripped = s"the engine's projection of player \(entry.player) \(what) for 3 s";
    }
  }

  // (test query nameplate [path|apart] [player N]) Where a player's
  // nameplate is and what placed it, both projections of the same tick:
  // path: engine, script; apart: the engine's from ours, percent of the
  // screen's height (none: ours placed it).
  public func PlateQuery(whose: Uint32, part: String) -> String {
    for entry in this.m_entries {
      if whose == 0u || entry.player == whose {
        let on = entry.plateNow ? "on" : "off";
        if Equals(part, "path") {
          return entry.byEngine ? "engine" : "script";
        }
        if Equals(part, "apart") {
          return entry.byEngine && entry.apart >= 0.0 ? FloatToStringPrec(100.0 * entry.apart / this.m_hostHeight, 2) : "none";
        }
        if !entry.byEngine {
          return s"script \(RoundF(entry.ownX * this.m_windowWidth)) \(RoundF(entry.ownY * this.m_windowHeight)) of \(RoundF(this.m_windowWidth)) x \(RoundF(this.m_windowHeight)) px, in view \(entry.ownSeen), plate \(on)";
        }
        let anchor = entry.head;
        let within = anchor.x >= 0.0 && anchor.x <= this.m_hostWidth && anchor.y >= 0.0 && anchor.y <= this.m_hostHeight;
        return s"engine \(RoundF(anchor.x)) \(RoundF(anchor.y)) of \(RoundF(this.m_hostWidth)) x \(RoundF(this.m_hostHeight)), within \(within), in screen \(anchor.inScreen), uv \(FloatToStringPrec(anchor.uvX, 2)) \(FloatToStringPrec(anchor.uvY, 2)), distance \(FloatToStringPrec(anchor.distance, 1)) (ours \(FloatToStringPrec(entry.ownFar, 1)) m), slot '\(NameToString(entry.slot))', told \(anchor.updates) times; ours \(RoundF(entry.ownX * this.m_hostWidth)) \(RoundF(entry.ownY * this.m_hostHeight)), in view \(entry.ownSeen); apart \(RoundF(entry.apart)) units (\(RoundF(entry.apart * this.m_scale)) px, \(FloatToStringPrec(100.0 * entry.apart / this.m_hostHeight, 2)) % of the height), plate \(on)";
      }
    }
    return "none";
  }

  // The game hides its whole HUD (cinematic camera, the empty context, a
  // taxi ride...): every module we follow is hidden. One module turned off
  // in the game's settings does not count.
  private func GameHudHidden() -> Bool {
    if ArraySize(this.m_modules) == 0 {
      return false;
    }
    for module in this.m_modules {
      if IsDefined(module) && module.IsVisible() {
        return false;
      }
    }
    return true;
  }

  // A HUD unit in the game window's pixels: the HUD is 2160 units tall
  // (3840 wide on a screen narrower than 16:9). The HUD layer's virtual
  // window, where our widgets are, is in pixels: at 1280 x 720 the list
  // was drawn three times too large, 96 340 at mid-screen (two games,
  // 2026-11-21).
  public static func WindowScale(game: GameInstance) -> Float {
    let window = GameInstance.GetUISystem(game).GetCurrentWindowSize();
    if window.X <= 0.0 || window.Y <= 0.0 {
      return 1.0;
    }
    return MinF(window.X / 3840.0, window.Y / 2160.0);
  }

  // The window, the scale, and the HUD's size in its own units (3840 x
  // 2160 at 16:9, wider at 21:9, taller at 16:10) with the screen's aspect.
  private func Measure() -> Void {
    let ui = GameInstance.GetUISystem(this.m_game);
    let window = ui.GetCurrentWindowSize();
    this.m_inverse = ui.GetInverseUIScale();
    if window.X > 0.0 && window.Y > 0.0 {
      this.m_scale = TandemPartyHud.WindowScale(this.m_game);
      this.m_windowWidth = window.X;
      this.m_windowHeight = window.Y;
      this.m_aspect = window.X / window.Y;
      this.m_width = window.X / this.m_scale;
      this.m_height = window.Y / this.m_scale;
    } else {
      this.m_scale = 1.0;
      this.m_windowWidth = 3840.0;
      this.m_windowHeight = 2160.0;
      this.m_aspect = 16.0 / 9.0;
      this.m_width = 3840.0;
      this.m_height = 2160.0;
    }
  }

  // Where the list goes this frame, compact or not, shown or not.
  private func Layout(linked: Int32) -> Void {
    this.Measure();
    let call = this.Forced(0, this.HoloCall());
    let boss = this.Forced(1, IsDefined(this.m_boss) && this.m_boss.IsVisible());
    let choices = this.Forced(2, this.Choices());
    let compact = call || choices;
    let left = call ? this.m_callLeft : this.m_left;
    let top = call ? this.m_callTop : this.m_top;
    let why = "";
    if !TandemSettings.PartyHud() {
      why = "party_hud off";
    } else if top + Cast<Float>(linked) * TandemPartyHud.EntryHeight(compact) > this.m_height - TandemPartyHud.BottomClear() {
      why = "no room";
    }
    this.m_compact = compact;
    this.m_listOn = Equals(why, "");
    this.m_root.SetScale(new Vector2(this.m_scale, this.m_scale));
    this.m_root.SetMargin(new inkMargin(left * this.m_scale, top * this.m_scale, 0.0, 0.0));
    let place = call ? "below the holocall" : "under the health bar";
    let yields = (call ? ", holocall up" : "") + (choices ? ", choice hub up" : "") + (boss ? ", boss bar up (top centre: nothing to yield)" : "");
    let layout = s"\(place) (\(compact ? "compact" : "full")) at \(left) \(top), HUD \(RoundF(this.m_width)) x \(RoundF(this.m_height)) drawn at \(this.m_scale) (window \(RoundF(this.m_windowWidth)) x \(RoundF(this.m_windowHeight)), the game's inverse UI scale \(this.m_inverse))\(yields)";
    if NotEquals(layout, this.m_layout) {
      this.m_layout = layout;
      TandemNative.Log(s"hud: layout \(layout)");
    }
    if NotEquals(why, this.m_listWhy) {
      this.m_listWhy = why;
      TandemNative.Log(Equals(why, "") ? "hud: party list shown" : s"hud: party list hidden (\(why))");
    }
  }

  // ---- every frame of the session ----

  // Why the HUD of ours is hidden now ("": it is shown).
  private func Hidden(you: Uint32, count: Int32, now: Float) -> String {
    this.m_tier = 0;
    if you == 0u || count == 0 {
      return "no session";
    }
    this.Scan(now);
    if this.Forced(3, this.GameHudHidden()) {
      return "the game's HUD is hidden";
    }
    let presence = TandemPresenceSync.Of(this.m_game);
    if GameInstance.GetPhotoModeSystem(this.m_game).IsPhotoModeActive() || (IsDefined(presence) && presence.InPhotoMode()) {
      return "photo mode";
    }
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return "no player";
    }
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    if IsDefined(psm) {
      this.m_tier = psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.SceneTier);
    }
    if this.m_tier >= 3 {
      return "cinematic";
    }
    let braindance = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().Braindance);
    if IsDefined(braindance) && braindance.GetBool(GetAllBlackboardDefs().Braindance.IsActive) {
      return "braindance";
    }
    return "";
  }

  private func Apply() -> Void {
    if !IsDefined(this.m_root) {
      return;
    }
    let photo = GameInstance.GetPhotoModeSystem(this.m_game).IsPhotoModeActive();
    let presence = TandemPresenceSync.Of(this.m_game);
    if IsDefined(presence) && presence.InPhotoMode() {
      photo = true;
    }
    let show = this.m_visible && !photo;
    this.m_root.SetVisible(show && this.m_listOn);
    this.m_plates.SetVisible(show);
    if IsDefined(this.m_projected) {
      this.m_projected.SetVisible(show);
    }
  }

  // test reset: the layout, the list's state and the game's modules are
  // said again (scenarios read them, a session keeps them).
  public func ResayForTests() -> Void {
    this.m_layout = "";
    this.m_listWhy = "-";
    this.m_modulesLogged = -1;
    this.m_why = "-";
    // The path chosen again from the start, and said again.
    this.m_projection = 0;
    this.m_tripped = "";
    this.m_path = "-";
    this.m_slot = n"roleMappin";
    this.Unproject();
    // The test commands' forced cases back to the game's word (a failed
    // hud-layout left a holocall and a choice hub forced up).
    let i = 0;
    while i < ArraySize(this.m_force) {
      this.m_force[i] = -1;
      i += 1;
    }
  }

  public func Tick(now: Float, system: ref<TandemSystem>, you: Uint32) -> Void {
    this.m_now = now;
    let players = system.PuppetPlayers();
    let why = this.Hidden(you, ArraySize(players), now);
    if NotEquals(why, this.m_why) {
      this.m_why = why;
      TandemNative.Log(Equals(why, "") ? s"hud: party shown (\(ArraySize(players)) partners)" : s"hud: party hidden (\(why))");
    }
    this.m_visible = Equals(why, "");
    if you == 0u || ArraySize(players) == 0 || !this.Build() {
      this.Apply();
      return;
    }
    let linkedCount = 0;
    for player in players {
      if TandemCommsNative.LinkState(player) == 3 {
        linkedCount += 1;
      }
    }
    this.Layout(linkedCount);
    this.Apply();
    if !this.m_visible {
      return;
    }
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let camera = this.CameraPosition(cameras);
    let forward = cameras.GetActiveCameraForward();
    let right = cameras.GetActiveCameraRight();
    // GetActiveCameraFOV is the vertical angle (vanilla's own fallback for
    // it is 0.89 rad, 51 degrees: the height of an 80 degree wide 16:9
    // frame). Bearing wants half the horizontal one.
    let half = Deg2Rad(cameras.GetActiveCameraFOV()) * 0.5;
    if half <= 0.01 {
      half = this.m_fovVertical ? 0.445 : Deg2Rad(40.0);
    }
    let wide = this.m_fovVertical ? TandemPartyHud.HorizontalHalf(half, this.m_aspect) : half;
    let story = TandemStorySystem.Get(this.m_game);
    let away = TandemElsewhere.Get(this.m_game);
    let revive = TandemRevive.Of(this.m_game);
    let paused = TandemNative.PausedPlayers();
    // The settings page's choice, read as it changes.
    let plates = TandemSettings.NameplateMode();
    let engine = this.Engine();
    for player in players {
      let body = system.PuppetOf(player);
      let entry = this.Entry(player);
      let known = IsDefined(away) ? away.Find(player) : null;
      let place = IsDefined(known) ? known.place : 0;
      let flags = TandemPresence.Flags(player);
      let status = TandemPresence.Status(TandemNative.RemoteLife(player), place, IsDefined(story) ? story.TierOf(player) : 0, IsDefined(story) ? story.StoryFlagsOf(player) : 0, ArrayContains(paused, player), flags);
      let health = TandemPresence.Health(player);
      if health < 0.0 {
        health = 1.0; // never heard: an older game, or a mock player that says nothing
      }
      if status == 1 || status == 2 {
        health = 0.0;
      }
      // Linked (both consent, comms.md): its entry and its health; else its
      // name over the head alone.
      let linked = TandemCommsNative.LinkState(player) == 3;
      entry.root.SetVisible(linked);
      entry.root.SetMargin(new inkMargin(0.0, 0.0, 0.0, this.m_compact ? 12.0 : 28.0));
      let left = IsDefined(revive) ? revive.HoldLeft(player) : -1.0;
      entry.name.SetText(TandemPartyHud.NameOf(player));
      entry.bar.SetSize(new Vector2(TandemPartyHud.BarWidth() * ClampF(health, 0.0, 1.0), 10.0));
      let text = TandemPartyHud.StateText(status, place, left);
      entry.state.SetText(text);
      entry.state.SetVisible(StrLen(text) > 0);

      // Where they are: the body here, hidden or not (a V elsewhere has
      // its body where it was left). A body just spawned stands at the
      // world's origin until it is put in place: their own position then.
      let where = IsDefined(body) ? body.GetWorldPosition() : TandemNative.RemotePosition(player);
      if IsDefined(body) && Vector4.IsZero(where) {
        where = TandemNative.RemotePosition(player);
      }
      let bearing = TandemPresence.Bearing(camera, forward, wide, where);
      let hiddenBody = TandemVisibility.IsHidden(this.m_game, player);
      // Seen: the chest or the head inside the frame (the feet of a body a
      // few metres away are below its bottom edge).
      let chest = where;
      chest.Z += 1.0;
      let head = where;
      head.Z += 2.0;
      let point = TandemPresence.Project(camera, forward, right, half, this.m_fovVertical, this.m_aspect, head);
      let atChest = TandemPresence.Project(camera, forward, right, half, this.m_fovVertical, this.m_aspect, chest);
      let headSeen = point.visible;
      let seen = point.visible || atChest.visible;
      // (The fade stays on our own distance: the engine's distanceToCamera
      // is only logged until its unit is seen, test query nameplate.)
      let far = bearing.distance;
      entry.ownFar = far;
      entry.ownX = point.x;
      entry.ownY = point.y;
      entry.ownSeen = point.visible;
      // The engine's word for it, once it has told the head's place (ours
      // until then, and when it is given up).
      if engine && IsDefined(body) {
        this.Anchor(entry, body, where);
      } else {
        this.Unanchor(entry);
      }
      let placed = IsDefined(entry.head) && entry.head.updates > 0;
      if IsDefined(entry.head) {
        this.Compare(entry, now);
      }
      this.PlateUnder(entry, placed);
      if placed {
        headSeen = entry.head.inScreen;
        seen = headSeen || atChest.visible;
        if IsDefined(entry.chest) && entry.chest.updates > 0 {
          seen = headSeen || entry.chest.inScreen;
        }
      }
      let off = !seen || hiddenBody;
      entry.where.SetVisible(linked && off && !this.m_compact);
      let metres = TandemPresence.RoundedMetres(bearing.distance);
      if linked && off {
        entry.arrow.SetRotation(Rad2Deg(bearing.angle));
        entry.distance.SetText(metres < 1000 ? TandemText.F("Party-Metres", s"\(metres)") : TandemText.F("Party-Kilometres", s"\(metres / 1000).\((metres % 1000) / 100)"));
      }

      // The nameplate, over the head (point): near, always (only the near
      // ones while our V talks), or off.
      let mode = plates;
      if mode == 1 && this.m_tier >= 2 {
        mode = 0;
      }
      let opacity = TandemPresence.NameplateOpacity(far, mode);
      let plateOn = IsDefined(body) && !hiddenBody && headSeen && opacity > 0.0 && status != 3;
      entry.plate.SetVisible(plateOn);
      entry.plateNow = plateOn;
      if plateOn {
        let scale = TandemPresence.NameplateScale(far, mode);
        if placed {
          // Under the carrier's root: its units, its scale; the place is
          // the engine's listener's (TandemPlateAnchor.OnProjected).
          entry.plate.SetScale(new Vector2(scale, scale));
        } else {
          entry.plate.SetMargin(new inkMargin(point.x * this.m_windowWidth, point.y * this.m_windowHeight, 0.0, 0.0));
          entry.plate.SetScale(new Vector2(scale * this.m_scale, scale * this.m_scale));
        }
        entry.plate.SetOpacity(opacity);
        entry.plateName.SetText(TandemPartyHud.NameOf(player));
      }

      // The log proves the state (scenarios read it): on a change of what
      // is shown, at most twice a second (a link change at once).
      let percent = Cast<Int32>(health * 100.0);
      let changed = status != entry.status || Equals(off, entry.inView) || NotEquals(plateOn, entry.plateOn) || AbsF(Cast<Float>(percent - entry.health)) >= 10.0 || (off && bearing.side != entry.side) || NotEquals(placed, entry.loggedEngine) || !entry.shown;
      let relinked = NotEquals(linked, entry.linked);
      if relinked || changed && now - entry.loggedAt >= 0.5 {
        entry.loggedAt = now;
        entry.shown = true;
        entry.status = status;
        entry.inView = !off;
        entry.plateOn = plateOn;
        entry.loggedEngine = placed;
        entry.linked = linked;
        entry.health = percent;
        entry.side = bearing.side;
        entry.metres = metres;
        this.Log(entry, TandemPartyHud.StateText(status, place, left, true));
      }
    }
  }

  // The active camera's position; our V's eyes when the camera system has
  // none (the transform is not left unset: AGENTS.md, value-type locals).
  private func CameraPosition(cameras: ref<CameraSystem>) -> Vector4 {
    let eyes = new Vector4(0.0, 0.0, 0.0, 1.0);
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      eyes = player.GetWorldPosition();
      eyes.Z += 1.7;
    }
    let frame = Transform.Create(eyes);
    if !IsDefined(cameras) || !cameras.GetActiveCameraWorldTransform(frame) {
      return eyes;
    }
    return Transform.GetPosition(frame);
  }

  // Half the horizontal field of view of a frame `aspect` wide (width /
  // height) whose half vertical angle is `half` (radians): the frame's
  // half width at a distance of 1 is tan(half) * aspect.
  public static func HorizontalHalf(half: Float, aspect: Float) -> Float {
    let w = TanF(half) * (aspect > 0.0 ? aspect : 16.0 / 9.0);
    // atan(w) = asin(w / sqrt(1 + w^2)), without relying on AtanF's
    // argument order.
    return AsinF(w / SqrtF(1.0 + w * w));
  }

  private func Log(entry: ref<TandemPartyEntry>, state: String) -> Void {
    let where = entry.inView ? "in view" : s"off screen (arrow side \(entry.side))";
    let plate = entry.plateOn ? "on" : "off";
    let by = entry.loggedEngine ? "the engine" : "our projection";
    TandemNative.Log(s"hud: player \(entry.player) \"\(TandemPartyHud.NameOf(entry.player))\" health \(entry.health) % status \(TandemPartyHud.StatusName(entry.status))\(StrLen(state) > 0 ? s" [\(state)]" : ""), \(entry.metres) m, \(where), nameplate \(plate), \(entry.linked ? "linked: in the party list" : "not linked: no entry"), placed by \(by)");
  }

  public static func StatusName(status: Int32) -> String {
    switch status {
      case 0:
        return "up";
      case 1:
        return "down";
      case 2:
        return "out";
      case 3:
        return "elsewhere";
      case 4:
        return "scene";
      case 5:
        return "menu";
      default:
        return "?";
    }
  }

  // ---- developer commands (tools/game.sh cmd presence hud ...) ----

  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    if StrBeginsWith(command, "at ") {
      // presence hud at X Y: where the party widget sits (HUD units).
      let rest = StrAfterFirst(command, "at ");
      this.m_left = StringToFloat(StrBeforeFirst(rest, " "));
      this.m_top = StringToFloat(StrAfterFirst(rest, " "));
      if IsDefined(this.m_root) {
        this.m_root.SetMargin(new inkMargin(this.m_left * this.m_scale, this.m_top * this.m_scale, 0.0, 0.0));
      }
      TandemNative.Log(s"hud: party widget at \(this.m_left) \(this.m_top)");
    } else if Equals(command, "fov v") || Equals(command, "fov h") {
      this.m_fovVertical = Equals(command, "fov v");
      TandemNative.Log(s"hud: the camera's field of view read as \(this.m_fovVertical ? "vertical" : "horizontal")");
    } else if StrBeginsWith(command, "projection ") {
      // presence hud projection auto|engine|script: what places the
      // nameplates (engine: never given up for standing apart from ours).
      let asked = StrAfterFirst(command, "projection ");
      this.m_projection = Equals(asked, "engine") ? 1 : (Equals(asked, "script") ? 2 : 0);
      this.m_tripped = "";
      this.m_path = "-";
      TandemNative.Log(s"hud: projection asked: \(this.m_projection == 1 ? "engine" : (this.m_projection == 2 ? "script" : "auto"))");
    } else if StrBeginsWith(command, "slot ") {
      // presence hud slot NAME|none: the body slot the engine reads the
      // head from (none: the world point 2 m above the feet).
      let asked = StrAfterFirst(command, "slot ");
      this.m_slot = Equals(asked, "none") ? n"None" : StringToName(asked);
      this.m_path = "-";
      this.Unproject();
      TandemNative.Log(s"hud: the engine reads the head from slot '\(NameToString(this.m_slot))'");
    } else if StrBeginsWith(command, "call-at ") {
      // presence hud call-at X Y: where the list goes below a holocall.
      let rest = StrAfterFirst(command, "call-at ");
      this.m_callLeft = StringToFloat(StrBeforeFirst(rest, " "));
      this.m_callTop = StringToFloat(StrAfterFirst(rest, " "));
      TandemNative.Log(s"hud: below a holocall, the list goes at \(this.m_callLeft) \(this.m_callTop)");
    } else if this.ForceCommand(command) {
      return;
    } else if Equals(command, "camera") {
      // What the engine's own projection says of each puppet's head, next
      // to ours: one run settles the field of view's convention.
      let cameras = GameInstance.GetCameraSystem(this.m_game);
      let camera = this.CameraPosition(cameras);
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        if IsDefined(body) {
          let head = body.GetWorldPosition();
          head.Z += 2.0;
          let theirs = cameras.ProjectPoint(head);
          let half = Deg2Rad(cameras.GetActiveCameraFOV()) * 0.5;
          let h = TandemPresence.Project(camera, cameras.GetActiveCameraForward(), cameras.GetActiveCameraRight(), half, false, 16.0 / 9.0, head);
          let v = TandemPresence.Project(camera, cameras.GetActiveCameraForward(), cameras.GetActiveCameraRight(), half, true, 16.0 / 9.0, head);
          TandemNative.Log(s"hud: camera fov \(cameras.GetActiveCameraFOV()); player \(player)'s head: engine ProjectPoint (\(theirs.X) \(theirs.Y) \(theirs.Z) \(theirs.W)); ours as horizontal (\(h.x) \(h.y) \(h.visible)), as vertical (\(v.x) \(v.y) \(v.visible))");
        }
      }
    } else {
      this.Report();
    }
  }

  // presence hud holocall|boss|choices|hudhidden on|off|auto: force a case
  // the list yields to (scenario hud-layout screenshots each).
  private func ForceCommand(command: String) -> Bool {
    let cases = ["holocall", "boss", "choices", "hudhidden"];
    let i = 0;
    while i < ArraySize(cases) {
      if StrBeginsWith(command, cases[i] + " ") {
        let value = StrAfterFirst(command, " ");
        this.m_force[i] = Equals(value, "on") ? 1 : (Equals(value, "off") ? 0 : -1);
        TandemNative.Log(s"hud: \(cases[i]) forced \(Equals(value, "on") || Equals(value, "off") ? value : "auto")");
        return true;
      }
      i += 1;
    }
    return false;
  }

  public func Report() -> Void {
    TandemNative.Log(s"hud: layout \(this.m_layout); list \(this.m_listOn ? "shown" : s"hidden (\(this.m_listWhy))")");
    let modes = ["near", "always", "off"];
    TandemNative.Log(s"hud: party \(this.m_visible ? "shown" : s"hidden (\(this.m_why))"), \(ArraySize(this.m_entries)) entries, widget \(IsDefined(this.m_root) ? "built" : "not built"), at \(this.m_left) \(this.m_top), nameplates \(modes[TandemSettings.NameplateMode()])");
    TandemNative.Log(s"hud: nameplates placed by \(this.m_path)");
    for entry in this.m_entries {
      this.Log(entry, TandemPartyHud.StateText(entry.status, 0, -1.0, true));
    }
  }
}
