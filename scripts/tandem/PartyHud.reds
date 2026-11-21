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
// HUD's units are the window's at the UI scale, so the list keeps to the
// left edge at any aspect, as the health bar does.

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
    this.m_fovVertical = true;
  }

  public func Stop() -> Void {
    this.Clear();
    if IsDefined(this.m_root) {
      this.m_root.Reparent(null);
    }
    if IsDefined(this.m_plates) {
      this.m_plates.Reparent(null);
    }
    this.m_root = null;
    this.m_plates = null;
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
  // phase NewHudPhoneGameController shows its holo for).
  private func HoloCall() -> Bool {
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
    for controller in layer.GetGameControllers() {
      let module = controller as inkGameController;
      if IsDefined(module) {
        if IsDefined(module as BossHealthBarGameController) {
          this.m_boss = module.GetRootWidget();
        } else if IsDefined(module as healthbarWidgetGameController) || IsDefined(module as MinimapContainerController) || IsDefined(module as QuestTrackerGameController) {
          ArrayPush(this.m_modules, module.GetRootWidget());
        }
      }
    }
    if this.m_modulesLogged != ArraySize(this.m_modules) {
      this.m_modulesLogged = ArraySize(this.m_modules);
      TandemNative.Log(s"hud: the game's HUD modules followed: \(ArraySize(this.m_modules)) (health bar, minimap, quest tracker), boss bar \(IsDefined(this.m_boss) ? "found" : "not found")");
    }
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

  // The HUD's size in its own units (the window at the UI scale: 3840 x
  // 2160 at 16:9, wider at 21:9, taller at 16:10) and the screen's aspect.
  private func Measure() -> Void {
    let ui = GameInstance.GetUISystem(this.m_game);
    let window = ui.GetCurrentWindowSize();
    let inverse = ui.GetInverseUIScale();
    if window.X > 0.0 && window.Y > 0.0 && inverse > 0.0 {
      this.m_aspect = window.X / window.Y;
      this.m_width = window.X * inverse;
      this.m_height = window.Y * inverse;
    } else {
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
    this.m_root.SetMargin(new inkMargin(left, top, 0.0, 0.0));
    let place = call ? "below the holocall" : "under the health bar";
    let yields = (call ? ", holocall up" : "") + (choices ? ", choice hub up" : "") + (boss ? ", boss bar up (top centre: nothing to yield)" : "");
    let layout = s"\(place) (\(compact ? "compact" : "full")) at \(left) \(top), HUD \(RoundF(this.m_width)) x \(RoundF(this.m_height))\(yields)";
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
  }

  // test reset: the layout, the list's state and the game's modules are
  // said again (scenarios read them, a session keeps them).
  public func ResayForTests() -> Void {
    this.m_layout = "";
    this.m_listWhy = "-";
    this.m_modulesLogged = -1;
    this.m_why = "-";
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
      let seen = point.visible || atChest.visible;
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
      let opacity = TandemPresence.NameplateOpacity(bearing.distance, mode);
      let plateOn = IsDefined(body) && !hiddenBody && point.visible && opacity > 0.0 && status != 3;
      entry.plate.SetVisible(plateOn);
      entry.plateNow = plateOn;
      if plateOn {
        let scale = TandemPresence.NameplateScale(bearing.distance, mode);
        entry.plate.SetMargin(new inkMargin(point.x * this.m_width, point.y * this.m_height, 0.0, 0.0));
        entry.plate.SetOpacity(opacity);
        entry.plate.SetScale(new Vector2(scale, scale));
        entry.plateName.SetText(TandemPartyHud.NameOf(player));
      }

      // The log proves the state (scenarios read it): on a change of what
      // is shown, at most twice a second (a link change at once).
      let percent = Cast<Int32>(health * 100.0);
      let changed = status != entry.status || Equals(off, entry.inView) || NotEquals(plateOn, entry.plateOn) || AbsF(Cast<Float>(percent - entry.health)) >= 10.0 || (off && bearing.side != entry.side) || !entry.shown;
      let relinked = NotEquals(linked, entry.linked);
      if relinked || changed && now - entry.loggedAt >= 0.5 {
        entry.loggedAt = now;
        entry.shown = true;
        entry.status = status;
        entry.inView = !off;
        entry.plateOn = plateOn;
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
    TandemNative.Log(s"hud: player \(entry.player) \"\(TandemPartyHud.NameOf(entry.player))\" health \(entry.health) % status \(TandemPartyHud.StatusName(entry.status))\(StrLen(state) > 0 ? s" [\(state)]" : ""), \(entry.metres) m, \(where), nameplate \(plate), \(entry.linked ? "linked: in the party list" : "not linked: no entry")");
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
        this.m_root.SetMargin(new inkMargin(this.m_left, this.m_top, 0.0, 0.0));
      }
      TandemNative.Log(s"hud: party widget at \(this.m_left) \(this.m_top)");
    } else if Equals(command, "fov v") || Equals(command, "fov h") {
      this.m_fovVertical = Equals(command, "fov v");
      TandemNative.Log(s"hud: the camera's field of view read as \(this.m_fovVertical ? "vertical" : "horizontal")");
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
    for entry in this.m_entries {
      this.Log(entry, TandemPartyHud.StateText(entry.status, 0, -1.0, true));
    }
  }
}
