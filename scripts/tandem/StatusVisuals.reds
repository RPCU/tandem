// Status effects seen on a remote V's puppet (docs/architecture/presence.md,
// "Status effects"; engine facts: docs/research/engine/combat.md §13.75).
//
// Our game: the status effects on our V that show on it go to the others as
// one set of record ids (StatusVisuals), a few times a second when it
// changes. Which ones, the same generic way on both sides (Look): a record
// whose own VFX list names an effect, or whose packages run a visual
// effector (PlayVFXEffector, ApplyShaderEffector, ApplyShaderOnObjectEffector
// on its owner). No hand list of records: burning, shock, EMP, poison,
// bleeding, overheat, a Sandevistan's or Berserk's look, a heal's glow, a
// modded effect on a vanilla record are all the same case.
//
// The others: each record's visuals are played on our puppet the way the
// game plays them on any character, never its gameplay. That is
// GameObject.OnStatusEffectApplied's own path (final.reds:12246-12286):
// StartEffectEvent for each VFX name, PlaySoundEvent for each SFX name, and
// what the visual effectors do in their ActionOn (final.reds:307155,
// 299368, 299463). The status effect itself is never applied to the puppet:
// a real copy would run its damage ticks and stat modifiers on it, its AI
// reactions (an NPC on fire runs), and its hits on a puppet are forwarded to
// the V they stand for (Combat.reds) — it would hurt that V a second time.
//
// Optical camo stays Stealth.reds': it puts the camo record itself on the
// puppet (its look and the vision blocker the host's NPCs' senses meet), so
// what NPCs see of a camo'd V is that V's own game's word, as stealth.md
// says. Here it is never sent (CarriedByStealth) and a record the puppet
// already wears for real is never played again.
//
// A record the receiving game does not have (a mod's, on one side only) is
// skipped, with a notice once a session.

public class TandemStatusLook {
  public let record: TweakDBID;
  public let known: Bool;
  public let vfx: array<CName>;
  public let sfx: array<CName>;
  // Effects started once and never broken (PlayVFXEffector fireAndForget).
  public let once: array<CName>;
  // Material overrides of the visual effectors: name, tag, on the weapons.
  public let shaders: array<CName>;
  public let shaderTags: array<CName>;
  public let shaderOnWeapon: array<Bool>;

  public func Visible() -> Bool {
    return ArraySize(this.vfx) > 0 || ArraySize(this.once) > 0 || ArraySize(this.shaders) > 0;
  }

  public func Count() -> Int32 {
    return ArraySize(this.vfx) + ArraySize(this.once) + ArraySize(this.shaders);
  }
}

// What one puppet shows now: the records, and every effect, sound and
// material started on it, counted (two records may share one name: the
// first to end must not break the other's).
public class TandemStatusShown {
  public let player: Uint32;
  public let body: EntityID;
  public let hidden: Bool;
  public let records: array<TweakDBID>;
  // Those whose visuals we started (not an unknown one, nor one the puppet
  // wears for real): only these are stopped.
  public let started: array<TweakDBID>;
  public let fx: array<CName>;
  public let fxCount: array<Int32>;
  public let sfx: array<CName>;
  public let sfxCount: array<Int32>;
  public let shaders: array<CName>;
  public let shaderTags: array<CName>;
  public let shaderOnWeapon: array<Bool>;
  public let shaderCount: array<Int32>;
}

public class TandemStatusVisuals {
  private let m_game: GameInstance;
  private let m_looks: array<ref<TandemStatusLook>>;
  private let m_shown: array<ref<TandemStatusShown>>;
  private let m_changes: Uint32;
  private let m_lastScan: Float;
  private let m_sent: array<TweakDBID>;
  // Unknown records already told of (this session), names never to play
  // (presence status block NAME), names played once (logged once each).
  private let m_unknown: array<TweakDBID>;
  private let m_blocked: array<CName>;
  private let m_played: array<CName>;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public static func Of(game: GameInstance) -> ref<TandemStatusVisuals> {
    let presence = TandemPresenceSync.Of(game);
    return IsDefined(presence) ? presence.Status() : null;
  }

  // ---- which records show, and how ----

  public func Look(record: TweakDBID) -> ref<TandemStatusLook> {
    for look in this.m_looks {
      if look.record == record {
        return look;
      }
    }
    let look = TandemStatusVisuals.Read(record);
    ArrayPush(this.m_looks, look);
    return look;
  }

  // A record's visuals, from its TweakDB data alone (the same on every
  // game that has the record).
  public static func Read(id: TweakDBID) -> ref<TandemStatusLook> {
    let look = new TandemStatusLook();
    look.record = id;
    let record = TweakDBInterface.GetStatusEffectRecord(id);
    if !IsDefined(record) {
      return look;
    }
    look.known = true;
    let vfx: array<wref<StatusEffectFX_Record>>;
    record.VFX(vfx);
    for fx in vfx {
      if IsDefined(fx) && IsNameValid(fx.Name()) && !ArrayContains(look.vfx, fx.Name()) {
        ArrayPush(look.vfx, fx.Name());
      }
    }
    let sfx: array<wref<StatusEffectFX_Record>>;
    record.SFX(sfx);
    for fx in sfx {
      if IsDefined(fx) && IsNameValid(fx.Name()) && !ArrayContains(look.sfx, fx.Name()) {
        ArrayPush(look.sfx, fx.Name());
      }
    }
    let packages: array<wref<GameplayLogicPackage_Record>>;
    record.Packages(packages);
    for package in packages {
      let effectors: array<wref<Effector_Record>>;
      if IsDefined(package) {
        package.Effectors(effectors);
      }
      for effector in effectors {
        if IsDefined(effector) {
          TandemStatusVisuals.ReadEffector(look, effector);
        }
      }
    }
    return look;
  }

  // The visual effectors, read as their Initialize reads them (the record's
  // own fields). Any other effector is gameplay: left out.
  private static func ReadEffector(look: ref<TandemStatusLook>, effector: wref<Effector_Record>) -> Void {
    let id = effector.GetID();
    let kind = effector.EffectorClassName();
    if Equals(kind, n"PlayVFXEffector") {
      // final.reds:307141: started on ActionOn unless it plays at the end.
      let name = TweakDBInterface.GetCName(id + t".vfxName", n"None");
      if !IsNameValid(name) || TweakDBInterface.GetBool(id + t".startOnUninitialize", false) {
        return;
      }
      if TweakDBInterface.GetBool(id + t".fireAndForget", false) {
        if !ArrayContains(look.once, name) {
          ArrayPush(look.once, name);
        }
      } else if !ArrayContains(look.vfx, name) {
        ArrayPush(look.vfx, name);
      }
    } else if Equals(kind, n"ApplyShaderEffector") {
      // final.reds:299449: on its owner, or on the owner's weapons.
      let onOwner = TweakDBInterface.GetBool(id + t".applyToOwner", false);
      let onWeapon = TweakDBInterface.GetBool(id + t".applyToWeapon", false);
      if onOwner || onWeapon {
        TandemStatusVisuals.AddShader(look, TweakDBInterface.GetCName(id + t".overrideMaterialName", n"None"), TweakDBInterface.GetCName(id + t".overrideMaterialTag", n"None"), !onOwner);
      }
    } else if Equals(kind, n"ApplyShaderOnObjectEffector") {
      // final.reds:299350: on its application target; only the owner's
      // look is ours to show (another object is that object's business).
      let target = TweakDBInterface.GetCName(id + t".applicationTarget", n"None");
      if !IsNameValid(target) || Equals(target, n"Owner") || Equals(target, n"Self") {
        TandemStatusVisuals.AddShader(look, TweakDBInterface.GetCName(id + t".overrideMaterialName", n"None"), TweakDBInterface.GetCName(id + t".overrideMaterialTag", n"None"), false);
      }
    }
  }

  private static func AddShader(look: ref<TandemStatusLook>, name: CName, tag: CName, onWeapon: Bool) -> Void {
    if !IsNameValid(name) {
      return;
    }
    ArrayPush(look.shaders, name);
    ArrayPush(look.shaderTags, tag);
    ArrayPush(look.shaderOnWeapon, onWeapon);
  }

  // Optical camo goes through Stealth (StealthSync.SendOurs picks the same
  // tag): the puppet wears that record for real.
  public static func CarriedByStealth(record: wref<StatusEffect_Record>) -> Bool {
    return IsDefined(record) && record.GameplayTagsContains(n"CamoActiveOnPlayer");
  }

  // ---- our V, for the others ----

  // Four times a second (TandemPresenceSync.Tick).
  public func SendOurs(now: Float) -> Void {
    if now - this.m_lastScan < 0.25 || !TandemNative.IsStarted() {
      return;
    }
    this.m_lastScan = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let ids: array<TweakDBID>;
    let names = "";
    for effect in StatusEffectHelper.GetAppliedEffects(player) {
      let record = IsDefined(effect) ? effect.GetRecord() : null;
      if IsDefined(record) && !ArrayContains(ids, record.GetID()) && !TandemStatusVisuals.CarriedByStealth(record)
        && this.Look(record.GetID()).Visible() {
        ArrayPush(ids, record.GetID());
        names += s" \(TDBID.ToStringDEBUG(record.GetID()))";
      }
    }
    if TandemPresence.SendStatusVisuals(ids) {
      this.m_sent = ids;
      TandemNative.Log(s"status: our V shows \(ArraySize(ids)) effect(s) to the others:\(names)");
    }
  }

  // ---- the others' puppets ----

  // Every frame (TandemPresenceSync.Tick): each puppet shows its V's set,
  // and nothing while hidden (another world, a private scene's fade).
  public func Tick(system: ref<TandemSystem>) -> Void {
    let changes = TandemPresence.StatusChanges();
    let moved = changes != this.m_changes;
    this.m_changes = changes;
    for player in system.PuppetPlayers() {
      let body = system.PuppetOf(player);
      if IsDefined(body) {
        this.Follow(player, body, moved);
      }
    }
  }

  private func Follow(player: Uint32, body: ref<GameObject>, moved: Bool) -> Void {
    let shown = this.Shown(player);
    let rebuilt = shown.body != body.GetEntityID();
    let hidden = TandemVisibility.IsHidden(this.m_game, player);
    if rebuilt {
      // A new body: whatever played on the old one went with it.
      ArrayClear(shown.records);
      this.Forget(shown);
      shown.body = body.GetEntityID();
    }
    if moved || rebuilt || NotEquals(hidden, shown.hidden) {
      shown.hidden = hidden;
      let wanted: array<TweakDBID>;
      if !hidden {
        wanted = TandemPresence.StatusVisualsOf(player);
      }
      this.Show(shown, body, wanted);
    }
  }

  private func Shown(player: Uint32) -> ref<TandemStatusShown> {
    for shown in this.m_shown {
      if shown.player == player {
        return shown;
      }
    }
    let shown = new TandemStatusShown();
    shown.player = player;
    ArrayPush(this.m_shown, shown);
    return shown;
  }

  // From what the puppet shows to `wanted`: what ended stops, what is new
  // starts.
  private func Show(shown: ref<TandemStatusShown>, body: ref<GameObject>, wanted: array<TweakDBID>) -> Void {
    let kept: array<TweakDBID>;
    for record in shown.records {
      if ArrayContains(wanted, record) {
        ArrayPush(kept, record);
      } else {
        if ArrayContains(shown.started, record) {
          ArrayRemove(shown.started, record);
          this.Stop(shown, body, this.Look(record));
        }
        TandemNative.Log(s"status: player \(shown.player) no longer shows \(TDBID.ToStringDEBUG(record)) on its puppet");
      }
    }
    shown.records = kept;
    for record in wanted {
      if !ArrayContains(shown.records, record) {
        ArrayPush(shown.records, record);
        this.Begin(shown, body, record);
      }
    }
  }

  private func Begin(shown: ref<TandemStatusShown>, body: ref<GameObject>, record: TweakDBID) -> Void {
    let look = this.Look(record);
    let what = TDBID.ToStringDEBUG(record);
    if !look.known {
      TandemNative.Log(s"status: player \(shown.player) shows \(what), a record this game does not have (a mod's?): skipped");
      TandemDegraded.Fallback("record substituted", s"player \(shown.player) shows \(what), a status this game does not have: skipped");
      if !ArrayContains(this.m_unknown, record) {
        ArrayPush(this.m_unknown, record);
        TandemText.Notice(this.m_game, "Status-Unknown", TandemPartyHud.NameOf(shown.player));
      }
      return;
    }
    // Worn for real already (Stealth's camo): its own visuals play.
    if StatusEffectSystem.ObjectHasStatusEffect(body, record) {
      TandemNative.Log(s"status: player \(shown.player)'s puppet wears \(what) itself: nothing added");
      return;
    }
    ArrayPush(shown.started, record);
    let sounds = 0;
    for name in look.vfx {
      if this.CountFx(shown, name, 1) == 1 && !ArrayContains(this.m_blocked, name) {
        GameObjectEffectHelper.StartEffectEvent(body, name, false);
        this.Played(name);
      }
    }
    for name in look.once {
      if !ArrayContains(this.m_blocked, name) {
        GameObjectEffectHelper.StartEffectEvent(body, name, false);
        this.Played(name);
      }
    }
    let presence = TandemPresenceSync.Of(this.m_game);
    for name in look.sfx {
      // A time ability's own sounds may play already (Presence.reds Ability).
      let theirs = IsDefined(presence) && presence.PlaysAbilitySound(shown.player, name);
      if this.CountSfx(shown, name, 1) == 1 && !theirs {
        GameObject.PlaySoundEvent(body, name);
        sounds += 1;
      }
    }
    let i = 0;
    while i < ArraySize(look.shaders) {
      if this.CountShader(shown, look.shaders[i], look.shaderTags[i], look.shaderOnWeapon[i], 1) == 1 {
        this.Material(body, look.shaders[i], look.shaderTags[i], look.shaderOnWeapon[i], true);
        this.Played(look.shaders[i]);
      }
      i += 1;
    }
    TandemNative.Log(s"status: player \(shown.player) shows \(what) on its puppet (\(ArraySize(look.vfx) + ArraySize(look.once)) effect(s), \(sounds) sound(s), \(ArraySize(look.shaders)) material(s))");
  }

  private func Stop(shown: ref<TandemStatusShown>, body: ref<GameObject>, look: ref<TandemStatusLook>) -> Void {
    if !look.known {
      return;
    }
    for name in look.vfx {
      if this.CountFx(shown, name, -1) == 0 {
        GameObjectEffectHelper.BreakEffectLoopEvent(body, name);
      }
    }
    for name in look.sfx {
      if this.CountSfx(shown, name, -1) == 0 {
        GameObject.StopSoundEvent(body, name);
      }
    }
    let i = 0;
    while i < ArraySize(look.shaders) {
      if this.CountShader(shown, look.shaders[i], look.shaderTags[i], look.shaderOnWeapon[i], -1) == 0 {
        this.Material(body, look.shaders[i], look.shaderTags[i], look.shaderOnWeapon[i], false);
      }
      i += 1;
    }
  }

  // A name's count after `by` (+1 or -1): 1 its first start, 0 its last
  // end (it goes then), -1 a name none of ours plays.
  private func CountFx(shown: ref<TandemStatusShown>, name: CName, by: Int32) -> Int32 {
    let i = ArrayFindFirst(shown.fx, name);
    if i < 0 {
      if by <= 0 {
        return -1;
      }
      ArrayPush(shown.fx, name);
      ArrayPush(shown.fxCount, by);
      return by;
    }
    shown.fxCount[i] += by;
    let left = shown.fxCount[i];
    if left <= 0 {
      ArrayErase(shown.fx, i);
      ArrayErase(shown.fxCount, i);
      return 0;
    }
    return left;
  }

  private func CountSfx(shown: ref<TandemStatusShown>, name: CName, by: Int32) -> Int32 {
    let i = ArrayFindFirst(shown.sfx, name);
    if i < 0 {
      if by <= 0 {
        return -1;
      }
      ArrayPush(shown.sfx, name);
      ArrayPush(shown.sfxCount, by);
      return by;
    }
    shown.sfxCount[i] += by;
    let left = shown.sfxCount[i];
    if left <= 0 {
      ArrayErase(shown.sfx, i);
      ArrayErase(shown.sfxCount, i);
      return 0;
    }
    return left;
  }

  private func CountShader(shown: ref<TandemStatusShown>, name: CName, tag: CName, onWeapon: Bool, by: Int32) -> Int32 {
    let i = 0;
    while i < ArraySize(shown.shaders) {
      if Equals(shown.shaders[i], name) && Equals(shown.shaderTags[i], tag) && Equals(shown.shaderOnWeapon[i], onWeapon) {
        shown.shaderCount[i] += by;
        let left = shown.shaderCount[i];
        if left <= 0 {
          ArrayErase(shown.shaders, i);
          ArrayErase(shown.shaderTags, i);
          ArrayErase(shown.shaderOnWeapon, i);
          ArrayErase(shown.shaderCount, i);
          return 0;
        }
        return left;
      }
      i += 1;
    }
    if by <= 0 {
      return -1;
    }
    ArrayPush(shown.shaders, name);
    ArrayPush(shown.shaderTags, tag);
    ArrayPush(shown.shaderOnWeapon, onWeapon);
    ArrayPush(shown.shaderCount, by);
    return by;
  }

  // A material override on or off, as the shader effectors run it: a static
  // game effect of that name and tag, enabled for the entity.
  private func Material(body: ref<GameObject>, name: CName, tag: CName, onWeapon: Bool, on: Bool) -> Void {
    let targets: array<ref<GameObject>>;
    if onWeapon {
      let right = TandemPresenceSync.WeaponOf(body, false);
      let left = TandemPresenceSync.WeaponOf(body, true);
      if IsDefined(right) {
        ArrayPush(targets, right);
      }
      if IsDefined(left) {
        ArrayPush(targets, left);
      }
    } else {
      ArrayPush(targets, body);
    }
    for target in targets {
      let effect = GameInstance.GetGameEffectSystem(this.m_game).CreateEffectStatic(name, tag, body);
      if IsDefined(effect) {
        EffectData.SetBool(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.enable, on);
        EffectData.SetEntity(effect.GetSharedData(), GetAllBlackboardDefs().EffectSharedData.entity, target);
        effect.Run();
      }
    }
  }

  private func Played(name: CName) -> Void {
    if !ArrayContains(this.m_played, name) {
      ArrayPush(this.m_played, name);
      TandemNative.Log(s"status: first play of \(NameToString(name)) on a puppet (presence status block \(NameToString(name)) to keep it off)");
    }
  }

  // Everything one puppet shows, off (its player left, its body goes).
  private func StopAll(shown: ref<TandemStatusShown>) -> Void {
    let body = IsDefined(shown) ? GameInstance.FindEntityByID(this.m_game, shown.body) as GameObject : null;
    if IsDefined(body) {
      for record in shown.started {
        this.Stop(shown, body, this.Look(record));
      }
    }
    ArrayClear(shown.records);
    this.Forget(shown);
  }

  private func Forget(shown: ref<TandemStatusShown>) -> Void {
    ArrayClear(shown.started);
    ArrayClear(shown.fx);
    ArrayClear(shown.fxCount);
    ArrayClear(shown.sfx);
    ArrayClear(shown.sfxCount);
    ArrayClear(shown.shaders);
    ArrayClear(shown.shaderTags);
    ArrayClear(shown.shaderOnWeapon);
    ArrayClear(shown.shaderCount);
  }

  // A sound one of our records plays on that player's puppet now.
  public func PlaysSound(player: Uint32, name: CName) -> Bool {
    for shown in this.m_shown {
      if shown.player == player {
        return ArrayContains(shown.sfx, name);
      }
    }
    return false;
  }

  // A puppet is gone (its player left, or it is rebuilt).
  public func ForgetPlayer(player: Uint32) -> Void {
    let kept: array<ref<TandemStatusShown>>;
    for shown in this.m_shown {
      if shown.player == player {
        this.StopAll(shown);
        TandemNative.Log(s"status: player \(player)'s effects cleared (puppet gone)");
      } else {
        ArrayPush(kept, shown);
      }
    }
    this.m_shown = kept;
  }

  // The session ended.
  public func End() -> Void {
    for shown in this.m_shown {
      this.StopAll(shown);
    }
    ArrayClear(this.m_shown);
    ArrayClear(this.m_unknown);
    ArrayClear(this.m_sent);
    TandemPresence.SendStatusVisuals(this.m_sent);
  }

  // ---- tools/game.sh cmd presence status ... ----

  public func Command(command: String) -> Void {
    let player = GetPlayer(this.m_game);
    if StrBeginsWith(command, "apply ") || StrBeginsWith(command, "remove ") {
      // A status effect on our own V, for real (its gameplay too: a test):
      // what the others' puppets of it then show.
      let apply = StrBeginsWith(command, "apply ");
      let name = StrAfterFirst(command, " ");
      let id = TDBID.Create(name);
      if !IsDefined(player) || !IsDefined(TweakDBInterface.GetStatusEffectRecord(id)) {
        TandemNative.Log(s"presence status: no V, or no status effect record \(name)");
        return;
      }
      if apply {
        StatusEffectHelper.ApplyStatusEffect(player, id);
      } else {
        StatusEffectHelper.RemoveStatusEffect(player, id);
      }
      let look = this.Look(id);
      TandemNative.Log(s"presence status: \(name) \(apply ? "on" : "off") our V (shows: \(look.Visible()), \(look.Count()) effect(s), \(ArraySize(look.sfx)) sound(s))");
    } else if StrBeginsWith(command, "block ") || StrBeginsWith(command, "unblock ") {
      let name = StringToName(StrAfterFirst(command, " "));
      if StrBeginsWith(command, "block ") {
        if !ArrayContains(this.m_blocked, name) {
          ArrayPush(this.m_blocked, name);
        }
      } else {
        ArrayRemove(this.m_blocked, name);
      }
      TandemNative.Log(s"presence status: \(NameToString(name)) \(StrBeginsWith(command, "block ") ? "never played" : "played again") on puppets (this run)");
    } else if Equals(command, "report") {
      this.Report();
    } else {
      TandemNative.Log(s"presence status: unknown command '\(command)' (apply REC, remove REC, block NAME, unblock NAME, report)");
    }
  }

  public func Report() -> Void {
    let ours = "";
    for record in this.m_sent {
      ours += s" \(TDBID.ToStringDEBUG(record))";
    }
    TandemNative.Log(s"status: our V shows \(ArraySize(this.m_sent)):\(ours)");
    for shown in this.m_shown {
      let records = "";
      for record in shown.records {
        records += s" \(TDBID.ToStringDEBUG(record))";
      }
      let names = "";
      for name in shown.fx {
        names += s" \(NameToString(name))";
      }
      TandemNative.Log(s"status: player \(shown.player)'s puppet shows \(ArraySize(shown.records)):\(records); effects\(names); \(ArraySize(shown.sfx)) sound(s), \(ArraySize(shown.shaders)) material(s)");
    }
  }
}
