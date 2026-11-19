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
// partner gone takes it away at the next frame. Everyone else keeps their
// marker (Markers.reds) and a nameplate with their name alone, no health.
// What it says is decided in core (tandem/presence.hpp: StatusOf, BearingOf,
// ProjectOf, NameplateLookOf), tested there.
//
// The nameplate follows tandem.ini nameplates: near (to 12 m, gone at 20 m),
// always (at any distance in view, smaller and fainter far away; only near
// ones while our V talks, scene tier 2), off.
//
// Hidden in photo mode, in cinematic scenes (our tier 3 and up), in a
// braindance and when we are not in a session.

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
  public let plateBar: ref<inkRectangle>;
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
  // The camera's field of view is read as horizontal (false) or vertical.
  private let m_fovVertical: Bool;
  private let m_now: Float;
  // Our V's scene tier, as Hidden read it.
  private let m_tier: Int32;

  public static func BarWidth() -> Float {
    return 420.0;
  }

  public static func PlateWidth() -> Float {
    return 160.0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    // Under the player's own health bar and buffs (the HUD's space is
    // 3840 x 2160 whatever the screen).
    this.m_left = 96.0;
    this.m_top = 340.0;
    this.m_why = "no session";
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

    // The nameplate: the name (and, linked, a thin health line), centred
    // over the head; scaled about its foot.
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
    entry.plateBar = TandemPartyHud.Block(n"health", TandemPartyHud.PlateWidth(), 5.0, n"", player, 1.0);
    entry.plateBar.SetHAlign(inkEHorizontalAlign.Center);
    entry.plateBar.SetMargin(new inkMargin(0.0, 4.0, 0.0, 0.0));
    entry.plateBar.SetVisible(false);
    entry.plateBar.Reparent(plate);

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

  // ---- every frame of the session ----

  // Why the HUD of ours is hidden now ("": it is shown).
  private func Hidden(you: Uint32, count: Int32) -> String {
    this.m_tier = 0;
    if you == 0u || count == 0 {
      return "no session";
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
    this.m_root.SetVisible(show);
    this.m_plates.SetVisible(show);
  }

  public func Tick(now: Float, system: ref<TandemSystem>, you: Uint32) -> Void {
    this.m_now = now;
    let players = system.PuppetPlayers();
    let why = this.Hidden(you, ArraySize(players));
    if NotEquals(why, this.m_why) {
      this.m_why = why;
      TandemNative.Log(Equals(why, "") ? s"hud: party shown (\(ArraySize(players)) partners)" : s"hud: party hidden (\(why))");
    }
    this.m_visible = Equals(why, "");
    if you == 0u || ArraySize(players) == 0 || !this.Build() {
      this.Apply();
      return;
    }
    this.Apply();
    if !this.m_visible {
      return;
    }
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let frame: Transform;
    cameras.GetActiveCameraWorldTransform(frame);
    let camera = Transform.GetPosition(frame);
    let forward = cameras.GetActiveCameraForward();
    let right = cameras.GetActiveCameraRight();
    let half = Deg2Rad(cameras.GetActiveCameraFOV()) * 0.5;
    if half <= 0.01 {
      half = Deg2Rad(40.0);
    }
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
      let left = IsDefined(revive) ? revive.HoldLeft(player) : -1.0;
      entry.name.SetText(TandemPartyHud.NameOf(player));
      entry.bar.SetSize(new Vector2(TandemPartyHud.BarWidth() * ClampF(health, 0.0, 1.0), 10.0));
      let text = TandemPartyHud.StateText(status, place, left);
      entry.state.SetText(text);
      entry.state.SetVisible(StrLen(text) > 0);

      // Where they are: the body here, hidden or not (a V elsewhere has
      // its body where it was left).
      let where = IsDefined(body) ? body.GetWorldPosition() : TandemNative.RemotePosition(player);
      let bearing = TandemPresence.Bearing(camera, forward, half, where);
      let hiddenBody = TandemVisibility.IsHidden(this.m_game, player);
      let off = !bearing.inView || hiddenBody;
      entry.where.SetVisible(linked && off);
      let metres = TandemPresence.RoundedMetres(bearing.distance);
      if linked && off {
        entry.arrow.SetRotation(Rad2Deg(bearing.angle));
        entry.distance.SetText(metres < 1000 ? TandemText.F("Party-Metres", s"\(metres)") : TandemText.F("Party-Kilometres", s"\(metres / 1000).\((metres % 1000) / 100)"));
      }

      // The nameplate, over the head: near, always (only the near ones
      // while our V talks), or off.
      let head = where;
      head.Z += 2.0;
      let point = TandemPresence.Project(camera, forward, right, half, this.m_fovVertical, 16.0 / 9.0, head);
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
        entry.plate.SetMargin(new inkMargin(point.x * 3840.0, point.y * 2160.0, 0.0, 0.0));
        entry.plate.SetOpacity(opacity);
        entry.plate.SetScale(new Vector2(scale, scale));
        entry.plateName.SetText(TandemPartyHud.NameOf(player));
        entry.plateBar.SetVisible(linked);
        entry.plateBar.SetSize(new Vector2(TandemPartyHud.PlateWidth() * ClampF(health, 0.0, 1.0), 5.0));
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

  private func Log(entry: ref<TandemPartyEntry>, state: String) -> Void {
    let where = entry.inView ? "in view" : s"off screen (arrow side \(entry.side))";
    let plate = entry.plateOn ? (entry.linked ? "on, with health" : "on, name only") : "off";
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
    } else if Equals(command, "camera") {
      // What the engine's own projection says of each puppet's head, next
      // to ours: one run settles the field of view's convention.
      let cameras = GameInstance.GetCameraSystem(this.m_game);
      let frame: Transform;
      cameras.GetActiveCameraWorldTransform(frame);
      for player in system.PuppetPlayers() {
        let body = system.PuppetOf(player);
        if IsDefined(body) {
          let head = body.GetWorldPosition();
          head.Z += 2.0;
          let theirs = cameras.ProjectPoint(head);
          let half = Deg2Rad(cameras.GetActiveCameraFOV()) * 0.5;
          let h = TandemPresence.Project(Transform.GetPosition(frame), cameras.GetActiveCameraForward(), cameras.GetActiveCameraRight(), half, false, 16.0 / 9.0, head);
          let v = TandemPresence.Project(Transform.GetPosition(frame), cameras.GetActiveCameraForward(), cameras.GetActiveCameraRight(), half, true, 16.0 / 9.0, head);
          TandemNative.Log(s"hud: camera fov \(cameras.GetActiveCameraFOV()); player \(player)'s head: engine ProjectPoint (\(theirs.X) \(theirs.Y) \(theirs.Z) \(theirs.W)); ours as horizontal (\(h.x) \(h.y) \(h.visible)), as vertical (\(v.x) \(v.y) \(v.visible))");
        }
      }
    } else {
      this.Report();
    }
  }

  public func Report() -> Void {
    let modes = ["near", "always", "off"];
    TandemNative.Log(s"hud: party \(this.m_visible ? "shown" : s"hidden (\(this.m_why))"), \(ArraySize(this.m_entries)) entries, widget \(IsDefined(this.m_root) ? "built" : "not built"), at \(this.m_left) \(this.m_top), nameplates \(modes[TandemSettings.NameplateMode()])");
    for entry in this.m_entries {
      this.Log(entry, TandemPartyHud.StateText(entry.status, 0, -1.0, true));
    }
  }
}
