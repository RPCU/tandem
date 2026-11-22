// A car's custom paint between players (docs/architecture/vehicles.md,
// "Paint"; engine-knowledge §13.150). The game's CrystalCoat / TwinTone
// (patch 2.1): a template (two colours and a lights' hue, or a unique
// pattern scanned from another vehicle) kept in the vehicle's persistent
// state and drawn by its VehicleCustomizationComponent.
//
// The car's owner's game says it (CarPaint, state: kept by the host for late
// joiners, a repeat applied nowhere): the paint of the car our V sits in, or
// last sat in, that is no partner's, read from the car four times a second
// and said when it changes (a new template, switched off, taken off by
// damage or by the game when we walk away from the car). Every other game
// puts it on its copy of that car, when the paint changes and when the copy
// spawns, and puts it back when its own game takes it off the copy (our V
// rode in it and walked away, a part fell off the copy here).
//
// Vanilla (final.reds, 2.50): VehicleComponentPS holds the template
// (Get/SetVehicleVisualCustomizationTemplate) and whether it shows
// (GetIsVehicleVisualCustomizationActive, ...BlockedByDamage). The game
// applies one through events that ask CanApplyTemplateOnVehicle (the fact
// vvc_visual_customization_unlocked and the record's HasVisualCustomization,
// or the fact vvc_visual_customization_for_all_vehicles): a game that has
// not unlocked CrystalCoat would refuse a partner's paint. So a copy is
// painted by what those events end in, VehicleComponent's
// ApplyVehicleVisualCustomizationTemplate (the component's
// SetupGenericCustomization / SetupUniqueCustomization), the lights by the
// vehicle's own VehicleCustomizationLightsEvent, which reads the template.
// Taking a paint off first (DisableCurrentCustomization) and putting the
// next one on 0.6 s later is the game's own order
// (ExecuteVehicleVisualCustomization).

// A copy of a player's car here that has, or had, a paint of ours on it.
public class TandemPaintedCopy {
  public let body: EntityID;
  public let player: Uint32;
  // The car's record as its owner's seat named it (a copy may be a stand-in
  // of another record: mod-compat.md).
  public let record: TweakDBID;
  // The paint on it (TandemPaint.Key; "": stock, as it spawned).
  public let key: String;
  // The second half of putting a paint on: when (0: none due), and at once
  // or with the game's own transition.
  public let applyAt: Float;
  public let instant: Bool;
  // Once more after we put it back (a paint of our own garage the game had
  // already queued for the copy lands after ours): when (0: not).
  public let againAt: Float;
  // When we last put it back.
  public let fixedAt: Float;
  // Since when its body cannot be found (0: it is there).
  public let missingSince: Float;
}

@addMethod(VehicleComponent)
public final func TandemApplyPaint(template: VehicleVisualCustomizationTemplate, instant: Bool) -> Void {
  this.ApplyVehicleVisualCustomizationTemplate(template, instant);
}

public class TandemPaint {
  private let m_game: GameInstance;
  private let m_lastPoll: Float;
  // Ours: the last car of our own our V sat in, and its paint as last said
  // ("?": not said yet in this world).
  private let m_car: EntityID;
  private let m_saidKey: String;
  // Theirs.
  private let m_copies: array<ref<TandemPaintedCopy>>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_car = EntityID.FromHash(0ul);
    this.m_saidKey = "?";
  }

  // From TandemSystem.Tick, in a session; looks four times a second.
  public func Tick(now: Float, system: ref<TandemSystem>) -> Void {
    if now - this.m_lastPoll < 0.25 {
      return;
    }
    this.m_lastPoll = now;
    this.SayOurs(system);
    this.ShowTheirs(now, system);
  }

  // ---- paints as values ----

  // Stock paint: every field set (the VM does not clear a local).
  public static func Stock() -> TandemCarPaint {
    let paint: TandemCarPaint;
    paint.changes = 0;
    paint.vehicle = TDBID.None();
    paint.flags = 0;
    paint.primary = 0;
    paint.secondary = 0;
    paint.lightsHue = 0.0;
    paint.unique = TDBID.None();
    paint.model = n"None";
    return paint;
  }

  // 1 primary, 2 secondary, 4 lights, 8 unique.
  private static func Has(flags: Int32, bit: Int32) -> Bool {
    return (flags / bit) % 2 == 1;
  }

  private static func Pack(r: Uint8, g: Uint8, b: Uint8) -> Int32 {
    return Cast<Int32>(r) * 65536 + Cast<Int32>(g) * 256 + Cast<Int32>(b);
  }

  // The paint a vehicle shows, by its own state: its template while it is
  // on and no damage took it off; else stock.
  public static func Read(car: ref<VehicleObject>) -> TandemCarPaint {
    let paint = TandemPaint.Stock();
    if !IsDefined(car) {
      return paint;
    }
    let ps = car.GetVehiclePS();
    if !IsDefined(ps) || !ps.GetIsVehicleVisualCustomizationActive() || ps.GetIsVehicleVisualCustomizationBlockedByDamage() {
      return paint;
    }
    let template = ps.GetVehicleVisualCustomizationTemplate();
    if !VehicleVisualCustomizationTemplate.IsValid(template) {
      return paint;
    }
    paint.vehicle = car.GetRecordID();
    if template.hasUniqueTemplate {
      paint.flags = 8;
      paint.unique = template.uniqueData.recordId;
      paint.model = template.uniqueData.twintoneModelName;
      return paint;
    }
    let generic = template.genericData;
    if generic.primaryColorDefined {
      paint.flags += 1;
      paint.primary = TandemPaint.Pack(generic.primaryColorR, generic.primaryColorG, generic.primaryColorB);
      if generic.secondaryColorDefined {
        paint.flags += 2;
        paint.secondary = TandemPaint.Pack(generic.secondaryColorR, generic.secondaryColorG, generic.secondaryColorB);
      }
    }
    if generic.lightsColorDefined {
      paint.flags += 4;
      paint.lightsHue = ClampF(generic.lightsColorHue, 0.0, 360.0);
    }
    return paint;
  }

  // The game's template for a paint (stock: an empty one). `known` false: a
  // unique template whose record this game lacks (a mod's).
  public static func Template(paint: TandemCarPaint, out known: Bool) -> VehicleVisualCustomizationTemplate {
    // Every field we read set here (the VM does not clear a local).
    let template: VehicleVisualCustomizationTemplate;
    template.hasUniqueTemplate = false;
    template.genericData.primaryColorDefined = false;
    template.genericData.primaryColorR = Cast<Uint8>(0);
    template.genericData.primaryColorG = Cast<Uint8>(0);
    template.genericData.primaryColorB = Cast<Uint8>(0);
    template.genericData.secondaryColorDefined = false;
    template.genericData.secondaryColorR = Cast<Uint8>(0);
    template.genericData.secondaryColorG = Cast<Uint8>(0);
    template.genericData.secondaryColorB = Cast<Uint8>(0);
    template.genericData.lightsColorDefined = false;
    template.genericData.lightsColorHue = 0.0;
    template.uniqueData.recordId = TDBID.None();
    template.uniqueData.twintoneModelName = n"None";
    known = true;
    if paint.flags == 0 {
      return template;
    }
    if TandemPaint.Has(paint.flags, 8) {
      let record = TweakDBInterface.GetVehicleColorTemplateRecord(paint.unique);
      known = IsDefined(record);
      if known {
        template = VehicleVisualCustomizationTemplate.FromRecord(record, paint.model);
      }
      return template;
    }
    if TandemPaint.Has(paint.flags, 1) {
      template.genericData.primaryColorDefined = true;
      template.genericData.primaryColorR = Cast<Uint8>((paint.primary / 65536) % 256);
      template.genericData.primaryColorG = Cast<Uint8>((paint.primary / 256) % 256);
      template.genericData.primaryColorB = Cast<Uint8>(paint.primary % 256);
    }
    if TandemPaint.Has(paint.flags, 2) {
      template.genericData.secondaryColorDefined = true;
      template.genericData.secondaryColorR = Cast<Uint8>((paint.secondary / 65536) % 256);
      template.genericData.secondaryColorG = Cast<Uint8>((paint.secondary / 256) % 256);
      template.genericData.secondaryColorB = Cast<Uint8>(paint.secondary % 256);
    }
    if TandemPaint.Has(paint.flags, 4) {
      template.genericData.lightsColorDefined = true;
      template.genericData.lightsColorHue = paint.lightsHue;
    }
    return template;
  }

  // One string per paint ("": stock).
  public static func Key(paint: TandemCarPaint) -> String {
    if paint.flags == 0 {
      return "";
    }
    return s"\(paint.flags) \(paint.primary) \(paint.secondary) \(paint.lightsHue) \(TDBID.ToStringDEBUG(paint.unique)) on \(TDBID.ToStringDEBUG(paint.vehicle))";
  }

  // "primary 255,0,0 secondary 0,255,0 lights 120", "unique NAME", "stock".
  public static func Describe(paint: TandemCarPaint) -> String {
    if paint.flags == 0 {
      return "stock";
    }
    if TandemPaint.Has(paint.flags, 8) {
      return s"unique \(TDBID.ToStringDEBUG(paint.unique))";
    }
    let out = "";
    if TandemPaint.Has(paint.flags, 1) {
      out += "primary " + TandemPaint.Rgb(paint.primary);
    }
    if TandemPaint.Has(paint.flags, 2) {
      out += " secondary " + TandemPaint.Rgb(paint.secondary);
    }
    if TandemPaint.Has(paint.flags, 4) {
      let hue = RoundF(paint.lightsHue);
      out += (StrLen(out) > 0 ? " " : "") + s"lights \(hue)";
    }
    return out;
  }

  // 0xRRGGBB as "R,G,B".
  private static func Rgb(packed: Int32) -> String {
    let r = (packed / 65536) % 256;
    let g = (packed / 256) % 256;
    let b = packed % 256;
    return s"\(r),\(g),\(b)";
  }

  // ---- ours ----

  private func SayOurs(system: ref<TandemSystem>) -> Void {
    let player = GetPlayer(this.m_game);
    // Out of a session nothing is said: the next one hears what the car
    // shows then.
    if !IsDefined(player) || TandemNative.You() == 0u {
      return;
    }
    // Our own car: the one we sit in when it is no partner's copy; on foot,
    // or riding with a partner, still the last one.
    let car: wref<VehicleObject>;
    if VehicleComponent.GetVehicle(this.m_game, player, car) && IsDefined(car) {
      let id = car.GetEntityID();
      if system.LastCarOwnerOf(id) == 0u {
        this.m_car = id;
      }
    }
    let paint = TandemPaint.Read(TandemTraffic.Body(this.m_game, this.m_car));
    let key = TandemPaint.Key(paint);
    if Equals(key, this.m_saidKey) {
      return;
    }
    this.m_saidKey = key;
    TandemProgress.SendCarPaint(paint.vehicle, paint.flags, paint.primary, paint.secondary, paint.lightsHue, paint.unique, paint.model);
    TandemNative.Log(s"paint: our car's paint said: \(TandemPaint.Describe(paint))");
  }

  // ---- theirs ----

  private func Find(body: EntityID) -> ref<TandemPaintedCopy> {
    for copy in this.m_copies {
      if copy.body == body {
        return copy;
      }
    }
    return null;
  }

  private func ShowTheirs(now: Float, system: ref<TandemSystem>) -> Void {
    let players = system.PuppetPlayers();
    // A copy to paint: the car a player sits in that is its own, when its
    // paint is that car's.
    for player in players {
      let record = TandemNative.RemoteSeatVehicle(player);
      let body = system.CarOf(player);
      if TDBID.IsValid(record) && TandemNative.RemoteSeatOwner(player) == player && EntityID.IsDefined(body) && !IsDefined(this.Find(body)) {
        let paint = TandemProgress.CarPaintOf(player);
        if paint.flags != 0 && paint.vehicle == record {
          let copy = new TandemPaintedCopy();
          copy.body = body;
          copy.player = player;
          copy.record = record;
          copy.key = "";
          ArrayPush(this.m_copies, copy);
        }
      }
    }
    let kept: array<ref<TandemPaintedCopy>>;
    for copy in this.m_copies {
      if this.Show(copy, now, ArrayContains(players, copy.player)) {
        ArrayPush(kept, copy);
      }
    }
    this.m_copies = kept;
  }

  // One copy, one look. False: nothing of ours is on it and nothing is due
  // (forgotten until its owner paints that car again), or its body is gone.
  private func Show(copy: ref<TandemPaintedCopy>, now: Float, here: Bool) -> Bool {
    let body = TandemTraffic.Body(this.m_game, copy.body);
    if !IsDefined(body) {
      // Still spawning, or streamed out for a while.
      if copy.missingSince == 0.0 {
        copy.missingSince = now;
      }
      return now - copy.missingSince < 30.0 && (here || StrLen(copy.key) > 0);
    }
    copy.missingSince = 0.0;
    let paint = TandemProgress.CarPaintOf(copy.player);
    // Its owner's paint, while it is in the session and that paint is this
    // car's (it changed cars: the one it left is stock again).
    let wanted = here && paint.flags != 0 && paint.vehicle == copy.record;
    if !wanted {
      paint = TandemPaint.Stock();
    }
    let key = TandemPaint.Key(paint);
    if NotEquals(key, copy.key) {
      // A copy never painted takes its first paint as it is (it just
      // spawned, or nobody saw it painted); a change shows as in its
      // owner's game.
      let first = StrLen(copy.key) == 0;
      if this.Begin(copy, body, paint, first, first, now) {
        TandemNative.Log(s"paint: player \(copy.player)'s car copy: \(TandemPaint.Describe(paint))\(wanted || here ? "" : " (its owner left)")");
        copy.key = key;
        copy.againAt = 0.0;
      }
      return true;
    }
    if copy.applyAt > 0.0 {
      if now >= copy.applyAt {
        this.Finish(copy, body, paint);
      }
      return true;
    }
    if !wanted {
      return false;
    }
    if copy.againAt > 0.0 && now >= copy.againAt {
      copy.againAt = 0.0;
      this.Begin(copy, body, paint, false, true, now);
    } else if now - copy.fixedAt > 2.0 && !TandemPaint.Holds(body, paint) {
      // Our own game took it off, or put one of our garage's on it.
      copy.fixedAt = now;
      if this.Begin(copy, body, paint, false, true, now) {
        copy.againAt = now + 2.5;
        TandemNative.LogDebug(s"paint: player \(copy.player)'s car copy: put back (\(TandemPaint.Describe(paint)))");
      }
    }
    return true;
  }

  // The copy's own state says that paint is on it.
  private static func Holds(body: ref<VehicleObject>, paint: TandemCarPaint) -> Bool {
    let ps = body.GetVehiclePS();
    let known = true;
    let wanted = TandemPaint.Template(paint, known);
    // A template this game lacks is on no copy here: nothing to put back.
    if !IsDefined(ps) || !known {
      return true;
    }
    if !ps.GetIsVehicleVisualCustomizationActive() || ps.GetIsVehicleVisualCustomizationBlockedByDamage() {
      return false;
    }
    let has = ps.GetVehicleVisualCustomizationTemplate();
    return VehicleVisualCustomizationTemplate.Equals(has, wanted);
  }

  // The first half: the copy's state says the paint (the game's own
  // handlers read it: the lights, a mount), the old paint comes off, the
  // new one is due. `fresh`: nothing of ours to take off. False: the copy
  // is not ready (no component yet), try again.
  private func Begin(copy: ref<TandemPaintedCopy>, body: ref<VehicleObject>, paint: TandemCarPaint, fresh: Bool, instant: Bool, now: Float) -> Bool {
    let component = body.GetVehicleComponent();
    let ps = body.GetVehiclePS();
    if !IsDefined(component) || !IsDefined(ps) || !IsDefined(component.m_customizationComponent) {
      return false;
    }
    let known = true;
    let template = TandemPaint.Template(paint, known);
    let on = paint.flags != 0 && known;
    if !known {
      TandemNative.Log(s"paint: player \(copy.player)'s car copy: no template \(TDBID.ToStringDEBUG(paint.unique)) in this game (a mod's), stock paint kept");
      TandemDegraded.Fallback("record substituted", s"player \(copy.player)'s car paint: no template \(TDBID.ToStringDEBUG(paint.unique)) in this game: stock paint kept");
    }
    ps.SetVehicleVisualCustomizationTemplate(template);
    ps.SetVehicleVisualCustomizationActive(on);
    ps.SetVehicleApperanceCustomizationInDistanceTermination(false);
    if on {
      ps.SetVehicleVisualCustomizationnBlockedByDamage(false);
    }
    if !fresh {
      component.DisableCurrentCustomization();
    }
    copy.instant = instant;
    if on {
      // The game's own order: the next paint 0.6 s after the old one went.
      copy.applyAt = fresh ? now : now + 0.6;
      if fresh {
        this.Finish(copy, body, paint);
      }
    } else {
      copy.applyAt = 0.0;
      // Its lights back to the record's.
      body.QueueEvent(new VehicleCustomizationLightsEvent());
    }
    return true;
  }

  // The second half: the paint itself, and the lights.
  private func Finish(copy: ref<TandemPaintedCopy>, body: ref<VehicleObject>, paint: TandemCarPaint) -> Void {
    copy.applyAt = 0.0;
    let component = body.GetVehicleComponent();
    if !IsDefined(component) || !IsDefined(component.m_customizationComponent) {
      return;
    }
    let known = true;
    let template = TandemPaint.Template(paint, known);
    if known && VehicleVisualCustomizationTemplate.IsValid(template) && !VehicleVisualCustomizationTemplate.IsLightsOnly(template) {
      component.TandemApplyPaint(template, copy.instant);
    }
    body.QueueEvent(new VehicleCustomizationLightsEvent());
  }

  // ---- tests ----

  // (tools/game.sh cmd paint ...)
  //   paint                     what we say and what we show, to the log
  //   paint car R,G,B [R,G,B]   (test) that paint on the car our V sits in,
  //                             or last sat in, as the game's own state
  //   paint car off             (test) stock paint again
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    let words = StrSplit(command, " ");
    while ArraySize(words) < 5 {
      ArrayPush(words, "");
    }
    if Equals(words[1], "car") {
      let player = GetPlayer(this.m_game);
      let mounted: wref<VehicleObject>;
      let car = TandemTraffic.Body(this.m_game, this.m_car);
      if IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, mounted) && IsDefined(mounted) {
        car = mounted;
      }
      if !IsDefined(car) {
        TandemNative.Log("paint car: no car (our V sits in none, and sat in none)");
        return;
      }
      let paint = TandemPaint.Stock();
      if NotEquals(words[2], "off") {
        paint.vehicle = car.GetRecordID();
        paint.flags = 1;
        paint.primary = TandemPaint.Colour(words[2]);
        if StrLen(words[3]) > 0 {
          paint.flags = 3;
          paint.secondary = TandemPaint.Colour(words[3]);
        }
      }
      let copy = new TandemPaintedCopy();
      copy.body = car.GetEntityID();
      if this.Begin(copy, car, paint, false, true, 0.0) {
        if paint.flags != 0 {
          this.Finish(copy, car, paint);
        }
        TandemNative.Log(s"paint car: \(TDBID.ToStringDEBUG(car.GetRecordID())): \(TandemPaint.Describe(paint))");
      } else {
        TandemNative.Log("paint car: the car is not ready");
      }
      return;
    }
    let ours = TandemPaint.Read(TandemTraffic.Body(this.m_game, this.m_car));
    let copies = "";
    for copy in this.m_copies {
      copies += s" player \(copy.player): \(StrLen(copy.key) > 0 ? copy.key : "stock");";
    }
    TandemNative.Log(s"paint: our car: \(TandemPaint.Describe(ours)); copies painted here: \(ArraySize(this.m_copies))\(copies)");
  }

  // "255,0,0" as 0xRRGGBB.
  private static func Colour(text: String) -> Int32 {
    let parts = StrSplit(text, ",");
    while ArraySize(parts) < 3 {
      ArrayPush(parts, "0");
    }
    let r = Clamp(StringToInt(parts[0], 0), 0, 255);
    let g = Clamp(StringToInt(parts[1], 0), 0, 255);
    let b = Clamp(StringToInt(parts[2], 0), 0, 255);
    return r * 65536 + g * 256 + b;
  }

  // (test query paint [puppet]) the paint a car's own state says: our V's
  // car (the one it sits in, else the last), or the first remote player's
  // car copy here.
  public func Query(system: ref<TandemSystem>, puppet: Bool) -> String {
    let car = TandemTraffic.Body(this.m_game, this.m_car);
    let player = GetPlayer(this.m_game);
    let mounted: wref<VehicleObject>;
    if puppet {
      let players = system.PuppetPlayers();
      car = null;
      if ArraySize(players) > 0 {
        car = TandemTraffic.Body(this.m_game, system.LastCarOf(players[0]));
      }
    } else if IsDefined(player) && VehicleComponent.GetVehicle(this.m_game, player, mounted) && IsDefined(mounted) {
      car = mounted;
    }
    if !IsDefined(car) {
      return "none";
    }
    return TandemPaint.Describe(TandemPaint.Read(car));
  }
}
