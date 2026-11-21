// Down, revive, respawn (docs/architecture/revive.md "Down and out", milestone 8).
//
// Each game owns its own V's life. In a session with another player our V
// cannot die the engine's way: it is Immortal through a god-mode override of
// our own, exactly as vanilla keeps a V with Second Heart alive
// (SecondHeartStatListener, final.reds 243476, EnableOverride; a plain
// AddGodMode on V did not protect it, engine-knowledge §13.11), so its
// health stops at the floor. There the player state machine
// enters its death state as it does for Second Heart (DeathDecisions, 528038:
// health <= 1.1 %): V collapses with the game's own death animation, camera
// and sounds, its weapons go away (StartDeathEffects, 283766).
//
// Where vanilla then decides between a resurrection and the death screen
// (SetIsResurrectionAllowedBasedOnState, 283786) Second Heart keeps its own
// rule; failing that, our V goes Down (revivable, no bleed-out) or Out (down
// again within 5 minutes of a revive, or killed outright), with the
// resurrection allowed but held (DeathDecisionsWithResurrection.ToResurrect,
// 528472) and no death screen. A revive or a respawn lets it go: the game's
// own Second Heart stand-up (ResurrectEvents, 529013) gets V up, at 40 %.
// When no V is up any more the host says the session is lost: the host's V
// dies for real (death screen, checkpoint reload), a guest's waits for the
// host's reload and gets up beside the host's V.
//
// The other side: a remote player's downed puppet is outlined as vanilla's
// cut multiplayer did (RefreshCPOVisionAppearance, 45061); within 2 m our V
// gets the game's interaction prompt ([Hold F] Revive), and holding it for
// 10 s (the HUD's progress bar) sends the revive to that player's game.
//
// Down in a seat, our V stays there (vanilla's dies seated) and is revived
// from beside the car once it has stopped; a wreck puts it out beside the
// car first. Down in deep water (or in a sunken car), it is out at once and
// gets up at Viktor's clinic. An out V gets up in a free seat of the
// partner's car, else at the partner's last fit spot on foot, else at
// Viktor's (our fast travel point nearest to it where the story has no Vik
// for us) (revive.md, "Where a respawn puts you").

public class TandemRevive {
  private let m_game: GameInstance;
  private let m_system: wref<TandemSystem>;
  private let m_you: Uint32;
  private let m_now: Float;

  // ---- our V ----
  // 0 up, 1 down, 2 out (proto::Life), and what the others were told.
  private let m_life: Int32;
  private let m_sent: Bool;
  private let m_sentLife: Int32;
  private let m_sentFlags: Int32;
  private let m_lastSample: Float;
  // Our Immortal god mode is on (in a session with another player).
  private let m_protected: Bool;
  // Invulnerable while down and for a moment after getting up.
  private let m_invulnerable: Bool;
  private let m_graceUntil: Float;
  // We made V invisible to NPC senses (camo may have done it already).
  private let m_madeInvisible: Bool;
  private let m_downAt: Float;
  private let m_revivedAt: Float;
  // The state machine may resurrect V (a revive or a respawn), and the next
  // resurrection is ours (its health is ours to set).
  private let m_release: Bool;
  private let m_ours: Bool;
  // The real death is under way: nothing more to do.
  private let m_dying: Bool;
  // The session was lost while we were down (guest: waiting for the host).
  private let m_lost: Bool;
  private let m_lossesSeen: Int32;
  private let m_backInSession: Bool;
  // Someone reviving us, since when: the one our bar shows. With more
  // players, everyone reviving us (the bar goes to the next when its
  // reviver stops), and since when.
  private let m_reviver: Uint32;
  private let m_reviverAt: Float;
  private let m_revivers: array<Uint32>;
  private let m_reviverStarts: array<Float>;
  // Out: since when the partner we respawn beside has been out of combat.
  private let m_calmSince: Float;
  // Since when nobody else is in the session.
  private let m_aloneSince: Float;
  private let m_lastDrop: Float;
  // Up again (GotUp): when to look that nothing of the death state is
  // left (0: nothing to check).
  private let m_upCheckAt: Float;
  // Down in a vehicle (none: on foot): which, the seat, and whether we
  // braked it (our V at the wheel). (test) "revive wreck": taken as wrecked.
  private let m_downCar: EntityID;
  private let m_downSlot: CName;
  private let m_testWreck: Bool;
  // Put out of a seat, then placed (and stood up, for a respawn) once the
  // unmount is through: when, where, facing, and whether to release then.
  private let m_placeAt: Float;
  private let m_placeTo: Vector4;
  private let m_placeYaw: Float;
  private let m_placeRelease: Bool;
  // Our V's speed, from its position (seated or not), sampled 4 times a
  // second: a revive in a moving car waits for it to stop.
  private let m_speed: Float;
  private let m_speedAt: Float;
  private let m_speedFrom: Vector4;
  // A revive done while our car moved: whose, since when (it waits).
  private let m_waiting: Uint32;
  private let m_waitingSince: Float;
  // Deep water, looked at twice a second. (test) "revive water N": as if
  // N m under water.
  private let m_lastWater: Float;
  private let m_testDepth: Float;
  // (test) "revive out": the next fall is out.
  private let m_testOut: Bool;
  // A respawn into a partner's car: the car, the seat, whose, when (0: once
  // up); the seat it gave us last (for "revive unseat").
  private let m_seatCar: EntityID;
  private let m_seatSlot: CName;
  private let m_seatOwner: Uint32;
  private let m_seatAt: Float;
  private let m_seatedCar: EntityID;
  private let m_seatedSlot: CName;
  // Since when no respawn spot beside the partner is found (-1: not yet).
  private let m_noSpotSince: Float;
  // A respawn far away (Viktor's clinic, else a fast travel point of ours):
  // 0 none, 1 the jump due at m_farAt (out of a seat first), 2 jumped, the
  // fast travel's loading screen running (since m_farAt, its end seen at
  // m_farEndedAt). Where, facing (degrees), the point's marker (0: the
  // position), the notice and its name; "nowhere" said once.
  private let m_farStage: Int32;
  private let m_farAt: Float;
  private let m_farEndedAt: Float;
  private let m_farTo: Vector4;
  private let m_farYaw: Float;
  private let m_farMarker: Uint64;
  private let m_farKey: String;
  private let m_farName: String;
  private let m_farSaidNone: Bool;
  // (test) "revive nospot": no spot beside a partner fits (as with no
  // navmesh there); "revive novik": Viktor's is out of our story's reach.
  private let m_testNoSpot: Bool;
  private let m_testNoVik: Bool;

  // ---- the others' places (respawn spots, speeds) ----
  // Each partner's last spot on foot fit to get up at (our navmesh, dry,
  // in our city), and its facing (radians).
  private let m_footPlayers: array<Uint32>;
  private let m_footSpots: array<Vector4>;
  private let m_footYaws: array<Float>;
  private let m_lastFoot: Float;
  // Each partner's speed, from its position, 4 times a second.
  private let m_movePlayers: array<Uint32>;
  private let m_moveFrom: array<Vector4>;
  private let m_moveSpeeds: array<Float>;
  private let m_lastMove: Float;

  // ---- reviving someone ----
  private let m_listening: Bool;
  // The downed player in reach (the prompt shows), and the one we revive.
  private let m_target: Uint32;
  private let m_holding: Uint32;
  private let m_holdAt: Float;
  private let m_holdHealth: Float;
  // (test) "revive hold": held without a key.
  private let m_testHold: Bool;
  private let m_promptAt: Float;
  private let m_savedVisualizers: VisualizersInfo;
  // The HUD bar: 0 off, 1 our revive of someone, 2 someone reviving us.
  private let m_bar: Int32;
  // Our hold waits while the car the downed V sits in moves; our V kept
  // from the car's own door prompt while it revives someone seated.
  private let m_holdPaused: Bool;
  private let m_lastRevival: Float;
  private let m_carRestricted: Bool;

  // ---- the others ----
  private let m_livesSeen: Int32;
  private let m_known: array<Uint32>;
  private let m_knownLife: array<Int32>;
  private let m_lastMark: Float;

  public static func Of(game: GameInstance) -> ref<TandemRevive> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Revive() : null;
  }

  public func Start(game: GameInstance, system: ref<TandemSystem>) -> Void {
    this.m_game = game;
    this.m_system = system;
    this.m_revivedAt = -1000.0;
    this.m_lossesSeen = TandemNative.SessionLosses();
    this.m_livesSeen = -1;
  }

  // Another V is up: ours goes down instead of dying.
  public func Protected() -> Bool {
    return this.m_protected;
  }

  // Our V is down or out (no save meanwhile: TandemSystem.IsSavingLocked).
  // Seconds left of our revive of `player` (-1: we are not reviving them):
  // the party HUD's countdown.
  public func HoldLeft(player: Uint32) -> Float {
    if this.m_holding == 0u || this.m_holding != player {
      return -1.0;
    }
    return MaxF(0.0, 10.0 - (this.m_now - this.m_holdAt));
  }

  public func IsDown() -> Bool {
    return this.m_life != 0 || this.m_dying;
  }

  // The revive prompt is up, or a revive is under way: the interaction key
  // is ours (Trade.reds keeps its own prompt off meanwhile).
  public func Prompting() -> Bool {
    return this.m_target != 0u || this.m_holding != 0u;
  }

  // We leave the session, or it ends under us, while our V is down or out:
  // it dies the game's normal way (death screen, the last save), so leaving
  // is never a way out of being down. True if it was down.
  public func DieIfDown(why: String) -> Bool {
    let player = GetPlayer(this.m_game);
    if this.m_dying {
      return true;
    }
    // Up, or being stood up right now: nothing to die of.
    if this.m_life == 0 || this.m_release || !IsDefined(player) {
      if IsDefined(player) {
        TandemNative.Log(s"revive: \(why) with our V up: nothing to do");
      }
      return false;
    }
    TandemNative.Log(s"revive: \(why) with our V \(this.m_life == 1 ? "down" : "out"): it dies");
    this.RealDeath(player);
    return true;
  }

  // The world goes away (another save, the main menu), or the co-op menu
  // leaves the session.
  public func Stop() -> Void {
    this.HidePrompt();
    this.HideBar(false);
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      if this.m_listening {
        player.UnregisterInputListener(this, n"Choice1");
      }
      // Up, or being stood up: nothing of being down stays on it (a V that
      // is down here dies first, TandemSystem.EndSession).
      if this.m_life == 0 && !this.m_dying {
        this.ClearDown(player, "the session is over");
      }
      this.Unprotect(player);
      this.Vulnerable(player);
      this.KeepFromCar(player, false);
    }
    this.m_listening = false;
    let none = EntityID.FromHash(0ul);
    this.m_seatCar = none;
    this.m_placeAt = 0.0;
    this.m_waiting = 0u;
    this.m_farStage = 0;
  }

  // ---- the state machine's questions (wraps at the end of this file) ----

  // Our V's health is at the floor our Immortal god mode keeps it at: what
  // DeathDecisions asks of a V with Second Heart.
  public static func AtFloor(owner: wref<GameObject>) -> Bool {
    if !IsDefined(owner) {
      return false;
    }
    let revive = TandemRevive.Of(owner.GetGame());
    if !IsDefined(revive) || !revive.m_protected || revive.m_life != 0 || revive.m_release {
      return false;
    }
    let id = owner.GetEntityID();
    let pools = GameInstance.GetStatPoolsSystem(owner.GetGame());
    if !pools.IsStatPoolAdded(Cast<StatsObjectID>(id), gamedataStatPoolType.Health) {
      return false;
    }
    if GameInstance.GetGodModeSystem(owner.GetGame()).HasGodMode(id, gameGodModeType.Invulnerable) {
      return false;
    }
    return pools.GetStatPoolValue(Cast<StatsObjectID>(id), gamedataStatPoolType.Health, true) <= 1.1;
  }

  // Our V is dying and Second Heart does not save it: true if co-op keeps
  // it down (or out) instead of the death screen.
  public static func TakeDeath(owner: wref<GameObject>, forceKilled: Bool) -> Bool {
    if !IsDefined(owner) {
      return false;
    }
    let revive = TandemRevive.Of(owner.GetGame());
    return IsDefined(revive) && revive.OnDeath(owner as PlayerPuppet, forceKilled);
  }

  // The state machine must not resurrect our V yet.
  public static func Holds(owner: wref<GameObject>) -> Bool {
    if !IsDefined(owner) {
      return false;
    }
    // Left a session down: dying, never stood up by the state machine.
    let system = GameInstance.GetScriptableSystemsContainer(owner.GetGame()).Get(n"TandemSystem") as TandemSystem;
    if IsDefined(system) && system.LeftDown() {
      return true;
    }
    let revive = TandemRevive.Of(owner.GetGame());
    return IsDefined(revive) && revive.m_life != 0 && !revive.m_release;
  }

  // Our V got up (PlayerPuppet.OnResurrected): from a revive or a respawn
  // (ours), or from Second Heart.
  public static func Resurrected(player: ref<PlayerPuppet>) -> Void {
    let revive = TandemRevive.Of(player.GetGame());
    if IsDefined(revive) {
      revive.GotUp(player);
    }
  }

  // ---- every frame (TandemSystem.Tick) ----

  public func Tick(now: Float, you: Uint32) -> Void {
    this.m_now = now;
    this.m_you = you;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) || this.m_dying {
      return;
    }
    if !this.m_listening {
      player.RegisterInputListener(this, n"Choice1");
      this.m_listening = true;
    }
    let others = this.Others();
    // Kept while V is down or out, and while it is being stood up (a partner
    // leaving then must not take V's floor away under it).
    this.Protect(player, ArraySize(others) > 0 || this.m_life != 0 || this.m_release);
    if this.m_upCheckAt > 0.0 && now >= this.m_upCheckAt {
      this.m_upCheckAt = 0.0;
      this.CheckUp(player);
    }
    if this.m_invulnerable && this.m_life == 0 && this.m_graceUntil > 0.0 && now >= this.m_graceUntil {
      this.Vulnerable(player);
    }
    this.FollowOthers(player, others);
    this.Track(player, others);
    if this.m_placeAt > 0.0 && now >= this.m_placeAt {
      this.Place(player);
    }
    // Respawned into a partner's car: seated once up.
    if EntityID.IsDefined(this.m_seatCar) && this.m_life == 0 && !this.m_release {
      this.SeatWhenUp(player);
    }
    if now - this.m_lastSample >= 0.5 {
      this.m_lastSample = now;
      this.SendOurs(player);
    }
    // The host says no V is up any more.
    let losses = TandemNative.SessionLosses();
    if losses != this.m_lossesSeen {
      this.m_lossesSeen = losses;
      this.Lost(player);
    }
    if this.m_life != 0 && !this.m_release {
      this.WhileDown(player, others);
    } else {
      this.Revivals(player, others);
    }
  }

  // The session's players changed (TandemSystem.HandleEvents).
  public func InSession() -> Void {
    this.m_backInSession = true;
  }

  // The session ended under us (the host quit, crashed or was cut off; we
  // were cut off). Whoever we were reviving is gone with it. Down or out
  // ourselves: nobody is left to stand our V up, it dies (as when the last
  // V up leaves), unless everyone was down and we wait for the host's
  // reload (Lost).
  public func SessionEnded() -> Void {
    this.CancelHold("the session ended");
    this.HidePrompt();
    this.HideBar(false);
    this.ClearRevivers();
    if !this.m_lost {
      this.DieIfDown("the session ended");
    }
  }

  // ---- our V's protection ----

  private func Protect(player: ref<PlayerPuppet>, want: Bool) -> Void {
    if want && !this.m_protected {
      GameInstance.GetGodModeSystem(this.m_game).EnableOverride(player.GetEntityID(), gameGodModeType.Immortal, n"tandem_revive");
      this.m_protected = true;
      TandemNative.Log("revive: another player is here: our V goes down instead of dying");
    } else if !want && this.m_protected {
      this.Unprotect(player);
      TandemNative.Log("revive: nobody else here: our V is no longer kept from dying (it is up)");
    }
  }

  private func Unprotect(player: ref<PlayerPuppet>) -> Void {
    if this.m_protected {
      GameInstance.GetGodModeSystem(this.m_game).DisableOverride(player.GetEntityID(), n"tandem_revive");
      this.m_protected = false;
    }
  }

  // The status effect Bloodswell protects V with (final.reds 307839); put
  // on again every second while down, in case it runs out.
  private func Invulnerable(player: ref<PlayerPuppet>) -> Void {
    if !StatusEffectSystem.ObjectHasStatusEffect(player, t"BaseStatusEffect.PlayerInvulnerable") {
      StatusEffectHelper.ApplyStatusEffect(player, t"BaseStatusEffect.PlayerInvulnerable");
    }
    this.m_invulnerable = true;
    this.m_graceUntil = 0.0;
  }

  private func Vulnerable(player: ref<PlayerPuppet>) -> Void {
    if this.m_invulnerable {
      StatusEffectHelper.RemoveStatusEffect(player, t"BaseStatusEffect.PlayerInvulnerable");
      this.m_invulnerable = false;
    }
    this.m_graceUntil = 0.0;
  }

  // ---- going down ----

  private func OnDeath(player: ref<PlayerPuppet>, forceKilled: Bool) -> Bool {
    if !IsDefined(player) || !this.m_protected || this.m_life != 0 {
      return false;
    }
    this.CancelHold("our V went down");
    this.HidePrompt();
    this.KeepFromCar(player, false);
    if this.UpOthers() == 0 {
      if TandemNative.IsHost() {
        // The last V up: the session is lost (the others are told by the
        // host's arbitration), and the host's game dies the game's way.
        this.m_life = 2;
        this.SendOurs(player);
        this.Unprotect(player);
        this.m_dying = true;
        TandemNative.Log("revive: our V died with nobody up: the session is lost");
        return false;
      }
      // A guest never dies on its own: it waits for the host's reload.
      this.GoDown(player, 2, "nobody up");
      return true;
    }
    let cooldown = this.m_now - this.m_revivedAt < 300.0;
    if this.m_testOut {
      this.m_testOut = false;
      this.GoDown(player, 2, "a test");
    } else if forceKilled || cooldown {
      this.GoDown(player, 2, forceKilled ? "killed outright" : s"down again \(RoundF(this.m_now - this.m_revivedAt)) s after a revive");
    } else {
      this.GoDown(player, 1, "revivable");
    }
    return true;
  }

  private func GoDown(player: ref<PlayerPuppet>, life: Int32, why: String) -> Void {
    this.m_life = life;
    this.m_downAt = this.m_now;
    this.m_release = false;
    this.m_ours = false;
    this.ClearRevivers();
    this.m_calmSince = -1.0;
    this.m_aloneSince = -1.0;
    this.m_backInSession = false;
    this.Invulnerable(player);
    // NPCs lose a downed V, as they lose a V in the relic's camo; vanilla's
    // multiplayer made an incapacitated V a dead body to their senses.
    if !player.IsInvisible() {
      player.SetInvisible(true);
      this.m_madeInvisible = true;
    }
    player.SetSenseObjectType(gamedataSenseObjectType.Deadbody);
    this.DropThreats(player, player);
    this.SendOurs(player);
    TandemNative.Log(s"revive: our V is \(life == 1 ? "down" : "out") (\(why))");
    this.m_waiting = 0u;
    this.m_lastWater = -1.0;
    this.m_noSpotSince = -1.0;
    this.m_farStage = 0;
    this.m_farSaidNone = false;
    this.NoteCar(player);
    if life == 1 {
      this.Notice(SimpleMessageType.Negative, this.UpOthers() > 0 ? "Revive-Down" : "Revive-DownAlone", this.UpNames());
    } else if this.UpOthers() > 0 {
      this.Notice(SimpleMessageType.Negative, "Revive-Out", this.UpNames());
    } else {
      this.Notice(SimpleMessageType.Negative, "Revive-AllDown");
    }
  }

  private func WhileDown(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    for revive in TandemNative.TakeRevives() {
      this.RevivedBy(player, revive.player, revive.step);
    }
    // Our reviver vanished (no word from it: it left, crashed, was cut off).
    let r = ArraySize(this.m_revivers) - 1;
    while r >= 0 {
      if !ArrayContains(others, this.m_revivers[r]) {
        TandemNative.Log(s"revive: our reviver (player \(this.m_revivers[r])) is gone");
        this.StopReviver(this.m_revivers[r]);
      }
      r -= 1;
    }
    if this.m_reviver != 0u && this.m_bar == 2 {
      this.SetBar((this.m_now - this.m_reviverAt) / 10.0);
    }
    if this.m_now - this.m_lastDrop >= 1.0 {
      this.m_lastDrop = this.m_now;
      this.DropThreats(player, player);
      this.Invulnerable(player);
    }
    // On the way to a far respawn: nothing else until we are up there (a
    // partner leaving meanwhile takes nothing from it).
    if this.m_farStage != 0 {
      this.FollowFar(player);
      return;
    }
    // Twice a second: in a car, kept in the seat, out of a wreck; in deep
    // water, out and up at Viktor's.
    if this.m_placeAt == 0.0 && this.m_now - this.m_lastWater >= 0.5 {
      this.m_lastWater = this.m_now;
      this.InCar(player);
      this.InWater(player);
    }
    // A revive done while our car moved: taken once it stops.
    if this.m_waiting != 0u {
      if !ArrayContains(others, this.m_waiting) || this.m_now - this.m_waitingSince > 30.0 {
        TandemNative.Log(s"revive: \(this.NameOf(this.m_waiting))'s waiting revive dropped (gone, or 30 s)");
        this.m_waiting = 0u;
      } else if this.m_speed <= 1.0 {
        let reviver = this.m_waiting;
        this.m_waiting = 0u;
        TandemNative.Log("revive: our car has stopped: the waiting revive goes on");
        this.RevivedBy(player, reviver, 2);
        if this.m_life != 1 {
          return;
        }
      }
    }
    // Nobody else in the session (they left, or the host is gone).
    if ArraySize(others) == 0 {
      if this.m_aloneSince < 0.0 {
        this.m_aloneSince = this.m_now;
      }
      // Nobody left to revive us: no V is up, ours dies (3 s: a roster in
      // flight). After a lost session the host's reload gets two minutes.
      let wait = this.m_lost ? 120.0 : 3.0;
      if this.m_now - this.m_aloneSince >= wait {
        TandemNative.Log(s"revive: alone and down for \(RoundF(wait)) s: our V dies");
        this.RealDeath(player);
      }
      return;
    }
    this.m_aloneSince = -1.0;
    // Being put out of a seat for a respawn: it stands up once placed.
    if this.m_placeAt > 0.0 {
      return;
    }
    if this.m_lost {
      // Guest: the host reloaded and we are back in its session: up beside
      // the host's V once it is up and seen.
      let hostAt = TandemNative.RemotePosition(1u);
      if this.m_backInSession && TandemNative.RemoteLife(1u) == 0 && hostAt.W > 0.0 {
        this.Respawn(player, 1u, "the host reloaded");
      }
      return;
    }
    if this.m_life == 2 {
      this.MaybeRespawn(player, others);
    }
  }

  // Someone's revive of our V: started, given up, done (their game ran the
  // 10 s hold within 2 m; we check that they are still beside us).
  private func RevivedBy(player: ref<PlayerPuppet>, reviver: Uint32, step: Int32) -> Void {
    let name = this.NameOf(reviver);
    if this.m_life != 1 {
      TandemNative.Log(s"revive: \(name)'s revive (step \(step)) ignored: we are not down");
      return;
    }
    if step == 0 {
      let known = ArrayFindFirst(this.m_revivers, reviver);
      if known >= 0 {
        this.m_reviverStarts[known] = this.m_now;
      } else {
        ArrayPush(this.m_revivers, reviver);
        ArrayPush(this.m_reviverStarts, this.m_now);
      }
      if this.m_reviver == 0u || this.m_reviver == reviver {
        this.ShowReviver(reviver, this.m_now);
      }
      TandemNative.Log(s"revive: \(name) is reviving us");
    } else if step == 1 {
      if ArrayContains(this.m_revivers, reviver) {
        TandemNative.Log(s"revive: \(name) stopped reviving us");
      }
      this.StopReviver(reviver);
      if this.m_waiting == reviver {
        this.m_waiting = 0u;
      }
    } else if step == 2 {
      let at = TandemNative.RemotePosition(reviver);
      let distance = at.W > 0.0 ? Vector4.Distance(new Vector4(at.X, at.Y, at.Z, 1.0), player.GetWorldPosition()) : -1.0;
      if distance < 0.0 || distance > 4.0 {
        TandemNative.Log(s"revive: \(name)'s revive refused: \(distance) m away");
        return;
      }
      // Seated in a car still moving: the revive waits until it stops.
      if this.m_speed > 1.0 && VehicleComponent.IsMountedToVehicle(this.m_game, player) {
        if this.m_waiting != reviver {
          TandemNative.Log(s"revive: \(name)'s revive waits: our car still moves (\(this.m_speed) m/s)");
        }
        this.m_waiting = reviver;
        this.m_waitingSince = this.m_now;
        return;
      }
      this.m_revivedAt = this.m_now;
      this.HideBar(true);
      TandemNative.Log(s"revive: revived by \(name) (\(distance) m away)");
      this.Notice(SimpleMessageType.Neutral, "Revive-RevivedBy", name);
      this.Release(player);
    }
  }

  // Our bar shows `reviver`'s revive, begun at `at`.
  private func ShowReviver(reviver: Uint32, at: Float) -> Void {
    this.m_reviver = reviver;
    this.m_reviverAt = at;
    this.ShowBar(2, TandemText.F("Revive-RevivingYou", this.NameOf(reviver)), TandemText.Get("Revive-StayDown"));
  }

  // `reviver` stopped (or is gone): the bar goes to whoever else is reviving
  // us, else away.
  private func StopReviver(reviver: Uint32) -> Void {
    let at = ArrayFindFirst(this.m_revivers, reviver);
    if at >= 0 {
      ArrayErase(this.m_revivers, at);
      ArrayErase(this.m_reviverStarts, at);
    }
    if reviver != this.m_reviver {
      return;
    }
    this.m_reviver = 0u;
    if ArraySize(this.m_revivers) > 0 {
      this.ShowReviver(this.m_revivers[0], this.m_reviverStarts[0]);
    } else {
      this.HideBar(false);
    }
  }

  private func ClearRevivers() -> Void {
    this.m_reviver = 0u;
    ArrayClear(this.m_revivers);
    ArrayClear(this.m_reviverStarts);
  }

  // Out: up beside a partner once it is out of combat (3 s), or after 30 s
  // if the fight drags on.
  private func MaybeRespawn(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    let partner = 0u;
    let best = -1.0;
    for p in others {
      let at = TandemNative.RemotePosition(p);
      if TandemNative.RemoteLife(p) == 0 && at.W > 0.0 {
        let d = Vector4.Distance(new Vector4(at.X, at.Y, at.Z, 1.0), player.GetWorldPosition());
        if best < 0.0 || d < best {
          best = d;
          partner = p;
        }
      }
    }
    if partner == 0u {
      return;
    }
    let calm = (TandemNative.RemoteVitalsFlags(partner) & 1) == 0;
    if !calm {
      this.m_calmSince = -1.0;
    } else if this.m_calmSince < 0.0 {
      this.m_calmSince = this.m_now;
    }
    if calm && this.m_now - this.m_calmSince >= 3.0 {
      this.Respawn(player, partner, "out of combat");
    } else if this.m_now - this.m_downAt >= 30.0 {
      this.Respawn(player, partner, "30 s out");
    }
  }

  // Up again with `partner` (revive.md, "Where a respawn puts you"): in a
  // free seat of the car it drives or rides; else at its last spot on foot
  // (our navmesh, dry, in our city), 1.5 m behind it when that is clear;
  // else beside its stopped car when that is dry ground; never beside a
  // car on the move (10 s for it to stop); else at Viktor's (RespawnFar).
  // False: nowhere yet (tried again).
  private func Respawn(player: ref<PlayerPuppet>, partner: Uint32, why: String) -> Bool {
    let name = this.NameOf(partner);
    let none = EntityID.FromHash(0ul);
    let carId = IsDefined(this.m_system) ? this.m_system.SeatedIn(partner) : none;
    let car = EntityID.IsDefined(carId) ? GameInstance.FindEntityByID(this.m_game, carId) as VehicleObject : null;
    if IsDefined(car) {
      let ours: wref<VehicleObject>;
      if VehicleComponent.GetVehicle(this.m_game, player, ours) && IsDefined(ours) && ours.GetEntityID() == carId {
        TandemNative.Log(s"revive: respawn beside \(name) (\(why)): in its car, where we sit");
        this.Back(player, partner, "Revive-BackBeside");
        this.Release(player);
        return true;
      }
      let slot = TandemVehicles.FreeSlot(this.m_game, car, n"seat_front_right", player.GetEntityID());
      if IsNameValid(slot) && !VehicleComponent.IsDriverSlot(slot) {
        TandemNative.Log(s"revive: respawn beside \(name) (\(why)): into a free seat of its car (\(NameToString(slot)))");
        this.m_seatCar = carId;
        this.m_seatSlot = slot;
        this.m_seatOwner = partner;
        this.m_seatAt = 0.0;
        this.Back(player, partner, "Revive-BackInCar");
        // Up where we lie (out of a seat of ours first, beside that car),
        // seated once up.
        if IsDefined(ours) {
          this.PutOutTo(player, TandemVehicles.ExitSpot(this.m_game, ours, player), player.GetWorldYaw(), true);
        } else {
          this.Release(player);
        }
        return true;
      }
    }
    let i = ArrayFindFirst(this.m_footPlayers, partner);
    let to = new Vector4(0.0, 0.0, 0.0, 0.0);
    let yaw = 0.0;
    let how = "";
    let sunken = IsDefined(car) && this.WaterOver(car.GetWorldPosition(), 1.0) > 0.0;
    if i >= 0 && !this.m_testNoSpot {
      yaw = this.m_footYaws[i];
      to = this.ClearOf(player, this.m_footSpots[i], yaw);
      how = to.W > 1.5 ? "at its last spot on foot, 1.5 m away" : "at its last spot on foot (no clear room beside it)";
      to.W = 1.0;
    } else if IsDefined(car) && this.SpeedOf(partner) <= 2.0 && !sunken && !this.m_testNoSpot {
      // In a car with no free seat, never seen on foot here: beside the
      // car, once it has (nearly) stopped, on dry navmesh.
      let exit = TandemVehicles.ExitSpot(this.m_game, car, player);
      let ground = GameInstance.GetAINavigationSystem(this.m_game).FindPointInSphereForCharacter(exit, 0.5, player);
      if Equals(ground.status, worldNavigationRequestStatus.OK) && this.Dry(ground.point) {
        to = exit;
        yaw = Deg2Rad(car.GetWorldYaw());
        how = "beside its car (no free seat, a stopped car)";
      }
    }
    if to.W == 0.0 {
      let first = this.m_noSpotSince < 0.0;
      if first {
        this.m_noSpotSince = this.m_now;
      }
      // A car on the move may stop: 10 s for it.
      let moving = IsDefined(car) && !sunken && !this.m_testNoSpot && this.SpeedOf(partner) > 2.0;
      if moving && this.m_now - this.m_noSpotSince < 10.0 {
        if first {
          TandemNative.Log(s"revive: no spot to get up at beside \(name) yet (in a moving car with no free seat): 10 s for it to stop");
        }
        return false;
      }
      let where = this.m_testNoSpot ? "a test" : sunken ? "its car under water" : moving ? "its car still moving after 10 s"
        : IsDefined(car) ? "no ground beside its car" : "never seen on foot where our world has ground";
      TandemNative.Log(s"revive: no spot to get up at beside \(name) (\(where))");
      return this.RespawnFar(player, why);
    }
    TandemNative.Log(s"revive: respawn beside \(name) (\(why)): \(how)");
    this.Back(player, partner, "Revive-BackBeside");
    this.PutOutTo(player, to, Rad2Deg(yaw), true);
    return true;
  }

  // The respawn's notice (and the lost session forgotten).
  private func Back(player: ref<PlayerPuppet>, partner: Uint32, key: String) -> Void {
    this.m_lost = false;
    this.Notice(SimpleMessageType.Neutral, key, this.NameOf(partner));
  }

  // Where to get up at `spot` (its facing `yaw`, radians): 1.5 m behind it
  // when that is on our navmesh, dry and with nothing between, else beside
  // or in front, else the spot itself. W 2: an offset spot; 1: the spot.
  private func ClearOf(player: ref<PlayerPuppet>, spot: Vector4, yaw: Float) -> Vector4 {
    // Radians (interpolation.hpp); the game's forward is +Y turned by yaw:
    // (-sin, cos), so behind is (sin, -cos) and right (cos, sin).
    let s = SinF(yaw);
    let c = CosF(yaw);
    let offsets = [new Vector4(s, -c, 0.0, 0.0), new Vector4(c, s, 0.0, 0.0), new Vector4(-c, -s, 0.0, 0.0), new Vector4(-s, c, 0.0, 0.0)];
    let navigation = GameInstance.GetAINavigationSystem(this.m_game);
    let queries = GameInstance.GetSpatialQueriesSystem(this.m_game);
    for offset in offsets {
      let there = new Vector4(spot.X + offset.X * 1.5, spot.Y + offset.Y * 1.5, spot.Z, 1.0);
      let ground = navigation.FindPointInSphereForCharacter(there, 0.4, player);
      if Equals(ground.status, worldNavigationRequestStatus.OK) && this.Dry(ground.point) {
        let hit: TraceResult;
        let from = new Vector4(spot.X, spot.Y, spot.Z + 1.0, 1.0);
        let to = new Vector4(ground.point.X, ground.point.Y, ground.point.Z + 1.0, 1.0);
        if !queries.SyncRaycastByCollisionGroup(from, to, n"Static", hit, true, false) || !TraceResult.IsValid(hit) {
          return new Vector4(ground.point.X, ground.point.Y, ground.point.Z, 2.0);
        }
      }
    }
    return new Vector4(spot.X, spot.Y, spot.Z, 1.0);
  }

  // Out of a seat first when in one (the game's forced exit), then at `to`
  // facing `yawDegrees` once the unmount is through; stood up then if
  // `release`.
  private func PutOutTo(player: ref<PlayerPuppet>, to: Vector4, yawDegrees: Float, release: Bool) -> Void {
    this.m_placeTo = new Vector4(to.X, to.Y, to.Z, 1.0);
    this.m_placeYaw = yawDegrees;
    this.m_placeRelease = release;
    if VehicleComponent.IsMountedToVehicle(this.m_game, player) {
      TandemVehicles.PutOut(this.m_game, player);
      this.m_placeAt = this.m_now + 0.25;
      return;
    }
    this.Place(player);
  }

  private func Place(player: ref<PlayerPuppet>) -> Void {
    this.m_placeAt = 0.0;
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, this.m_placeTo, TandemYawAngles(this.m_placeYaw));
    if this.m_placeRelease {
      this.m_placeRelease = false;
      this.Release(player);
    }
  }

  // Respawned into a partner's car: half a second after getting up, into
  // the seat it had free (still free, the car still here), else beside it.
  private func SeatWhenUp(player: ref<PlayerPuppet>) -> Void {
    if this.m_seatAt == 0.0 {
      this.m_seatAt = this.m_now + 0.5;
      return;
    }
    if this.m_now < this.m_seatAt {
      return;
    }
    let none = EntityID.FromHash(0ul);
    let carId = this.m_seatCar;
    this.m_seatCar = none;
    let car = GameInstance.FindEntityByID(this.m_game, carId) as VehicleObject;
    let name = this.NameOf(this.m_seatOwner);
    if !IsDefined(car) {
      TandemNative.Log(s"revive: \(name)'s car is gone: we stay where we got up");
      return;
    }
    if !TandemVehicles.FreeFor(this.m_game, car, this.m_seatSlot, player.GetEntityID()) {
      let spot = TandemVehicles.ExitSpot(this.m_game, car, player);
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, spot, TandemYawAngles(car.GetWorldYaw()));
      TandemNative.Log(s"revive: \(NameToString(this.m_seatSlot)) of \(name)'s car was taken meanwhile: beside it instead");
      return;
    }
    TandemVehicles.Seat(this.m_game, player, car, this.m_seatSlot);
    this.m_seatedCar = carId;
    this.m_seatedSlot = this.m_seatSlot;
    TandemNative.Log(s"revive: seated in \(NameToString(this.m_seatSlot)) of \(name)'s car");
  }

  // ---- down in a car, in deep water ----

  // Going down seated (a car, a bike; not the NCART, not carried along): V
  // stays in its seat in the game's own death state, as vanilla's does (no
  // death exit for a V that is not dead: DeathExitingDecisions, final.reds
  // 551187). At the wheel, the car is braked to a stop.
  private func NoteCar(player: ref<PlayerPuppet>) -> Void {
    let none = EntityID.FromHash(0ul);
    this.m_downCar = none;
    this.m_downSlot = n"None";
    let car: wref<VehicleObject>;
    let transit = IsDefined(this.m_system) ? this.m_system.Transit() : null;
    let along = IsDefined(transit) && transit.CarriedBy() != 0u;
    if along || !VehicleComponent.GetVehicle(this.m_game, player, car) || !IsDefined(car) || TandemTransit.IsMetro(this.m_game, player, car) {
      return;
    }
    let slot = n"None";
    VehicleComponent.GetMountedSlotName(this.m_game, player, slot);
    this.m_downCar = car.GetEntityID();
    this.m_downSlot = slot;
    let driver = VehicleComponent.IsDriver(this.m_game, player);
    if driver {
      car.ForceBrakesUntilStoppedOrFor(20.0);
    }
    TandemNative.Log(s"revive: our V is down in its seat (\(NameToString(slot))\(driver ? ", at the wheel: braking" : ""))");
  }

  // Each tick while down in a seat: out beside the car when it is wrecked,
  // upside down or under water, once it has (nearly) stopped.
  private func InCar(player: ref<PlayerPuppet>) -> Void {
    if !EntityID.IsDefined(this.m_downCar) {
      return;
    }
    let none = EntityID.FromHash(0ul);
    if !VehicleComponent.IsMountedToVehicle(this.m_game, player) {
      TandemNative.Log("revive: our V is no longer in the seat it went down in");
      this.m_downCar = none;
      return;
    }
    let car = GameInstance.FindEntityByID(this.m_game, this.m_downCar) as VehicleObject;
    if !IsDefined(car) {
      return;
    }
    let why = "";
    if this.m_testWreck || car.IsDestroyed() {
      why = "wrecked";
    } else if car.IsVehicleUpsideDown() || car.ComputeIsVehicleUpsideDown() {
      why = "upside down";
    } else if this.WaterOver(car.GetWorldPosition(), 1.0) > 0.0 {
      why = "under water";
    }
    if StrLen(why) == 0 || this.m_speed > 2.0 {
      return;
    }
    // Sunk: deep water's rule (out, up at Viktor's), out of the seat first.
    if Equals(why, "under water") && this.Drowned(player, "a sunken car") {
      this.m_downCar = none;
      return;
    }
    let spot = TandemVehicles.ExitSpot(this.m_game, car, player);
    TandemNative.Log(s"revive: our car is \(why): our V is put out beside it (\(spot.X), \(spot.Y), \(spot.Z))");
    this.m_downCar = none;
    this.m_testWreck = false;
    this.Notice(SimpleMessageType.Neutral, "Revive-OutOfCar");
    this.PutOutTo(player, spot, player.GetWorldYaw(), false);
  }

  // Twice a second while down: in water deeper than V's height (head
  // under, or a floor more than 1.8 m under the surface) nobody reaches
  // it: out at once, up at Viktor's (revive.md, "Down in deep water").
  private func InWater(player: ref<PlayerPuppet>) -> Void {
    if VehicleComponent.IsMountedToVehicle(this.m_game, player) {
      return;
    }
    let feet = player.GetWorldPosition();
    let depth = this.WaterOver(feet, 0.0);
    if depth <= 0.0 {
      return;
    }
    let surface = feet.Z + depth;
    if depth < 1.7 && this.FloorUnder(feet, surface) < 1.8 {
      return;
    }
    this.Drowned(player, s"deep water, \(depth) m over its feet");
  }

  // Down where nobody can reach it (deep water, a sunken car): out at once
  // and up at Viktor's, unless no other V is up (the session's loss then
  // decides: Lost). True if it goes.
  private func Drowned(player: ref<PlayerPuppet>, where: String) -> Bool {
    if this.m_lost || this.UpOthers() == 0 {
      return false;
    }
    TandemNative.Log(s"revive: our V is down in \(where): out, no revive there");
    if this.m_life != 2 {
      this.m_life = 2;
      this.ClearRevivers();
      this.HideBar(false);
      this.SendOurs(player);
    }
    return this.RespawnFar(player, where);
  }

  // How deep `at` (raised by `up`) is under water, in metres (0: dry):
  // AIScriptUtils.GetWaterLevel (final.reds 158907) answers under water only.
  private func WaterOver(at: Vector4, up: Float) -> Float {
    if this.m_testDepth > 0.0 && up == 0.0 {
      return this.m_testDepth;
    }
    let level = 0.0;
    let probe = new Vector4(at.X, at.Y, at.Z + up, 1.0);
    if AIScriptUtils.GetWaterLevel(this.m_game, Vector4.Vector4To3(probe), level) && level > probe.Z {
      return level - at.Z;
    }
    return 0.0;
  }

  // How far the floor (static collision) lies under the water's `surface`
  // at `at` (30 m if none is found).
  private func FloorUnder(at: Vector4, surface: Float) -> Float {
    if this.m_testDepth > 0.0 {
      return this.m_testDepth;
    }
    let hit: TraceResult;
    let from = new Vector4(at.X, at.Y, surface, 1.0);
    let to = new Vector4(at.X, at.Y, surface - 30.0, 1.0);
    if GameInstance.GetSpatialQueriesSystem(this.m_game).SyncRaycastByCollisionGroup(from, to, n"Static", hit, true, false) && TraceResult.IsValid(hit) {
      return surface - hit.position.Z;
    }
    return 30.0;
  }

  // ---- a far respawn: Viktor's clinic, else a fast travel point ----

  // Where Vik patches V up: on the clinic's floor, 4.4 m from Viktor's own
  // spot (#ws_victor_vector_default, -1542.59 1230.24 11.52), facing the
  // ripper's chair (#q307_mp_viktor_chair, -1546.67 1234.67 12.30): the
  // journal's quest mappins (engine-knowledge §13.110).
  public static func VikSpot() -> Vector4 {
    return new Vector4(-1546.726, 1227.393, 11.520, 1.0);
  }

  // Degrees; the game's forward is +Y turned by yaw (-sin, cos): the chair
  // lies 7.3 m along +Y.
  public static func VikYaw() -> Float {
    return -0.45;
  }

  // Why Viktor's is out of our story's reach ("": it is not). Not met yet:
  // the fact vanilla's ripperdoc tutorial reads (q001_ripperdoc_done,
  // final.reds 42352, 387895). Watson's lockdown (Act 1, until
  // q101_done; final.reds 332681 gates the Act 1 tarot on it): the clinic
  // only from inside Watson.
  private func VikOutOfReach() -> String {
    if this.m_testNoVik {
      return "a test";
    }
    let quests = GameInstance.GetQuestsSystem(this.m_game);
    if quests.GetFact(n"q001_ripperdoc_done") < 1 {
      return "Vik not met yet";
    }
    if quests.GetFact(n"q101_done") < 1 && !this.InWatson() {
      return "Watson's lockdown, our V outside Watson";
    }
    return "";
  }

  // Our V's district is Watson or one of its own (the district manager's,
  // PreventionSystem.GetCurrentDistrict, final.reds 138920; parents up to
  // three levels, as District.GetRadioEntryName walks them).
  private func InWatson() -> Bool {
    let prevention = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"PreventionSystem") as PreventionSystem;
    if !IsDefined(prevention) {
      return false;
    }
    let district = prevention.GetCurrentDistrict();
    if !IsDefined(district) {
      return false;
    }
    let record: wref<District_Record> = district.GetDistrictRecord();
    let level = 0;
    while IsDefined(record) && level < 4 {
      if Equals(record.Type(), gamedataDistrict.Watson) {
        return true;
      }
      record = record.ParentDistrict();
      level += 1;
    }
    return false;
  }

  // Our fast travel point (on our world map) nearest to `at`, at any
  // distance; none in a save that has not found one.
  private func NearestPoint(at: Vector4) -> ref<FastTravelPointData> {
    let travel = GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"FastTravelSystem") as FastTravelSystem;
    if !IsDefined(travel) {
      return null;
    }
    let mappins = GameInstance.GetMappinSystem(this.m_game);
    let best: ref<FastTravelPointData>;
    let bestDistance = -1.0;
    for known in travel.GetFastTravelPoints() {
      if IsDefined(known) && known.ShouldShowMappinOnWorldMap() {
        let mappin = mappins.GetMappin(known.mappinID);
        if IsDefined(mappin) {
          let distance = Vector4.Distance(mappin.GetWorldPosition(), at);
          if bestDistance < 0.0 || distance < bestDistance {
            best = known;
            bestDistance = distance;
          }
        }
      }
    }
    return best;
  }

  // Up far away: at Viktor's clinic, else (out of our story's reach) at
  // our fast travel point nearest to where we lie. Out of a seat first;
  // then the jump (FollowFar). False: nowhere (tried again).
  private func RespawnFar(player: ref<PlayerPuppet>, why: String) -> Bool {
    let reach = this.VikOutOfReach();
    if StrLen(reach) == 0 {
      this.m_farTo = TandemRevive.VikSpot();
      this.m_farYaw = TandemRevive.VikYaw();
      this.m_farMarker = 0ul;
      this.m_farKey = "Revive-AtViktor";
      this.m_farName = "";
      TandemNative.Log(s"revive: respawn at Viktor's clinic (\(why))");
    } else {
      let point = this.NearestPoint(player.GetWorldPosition());
      if !IsDefined(point) {
        if !this.m_farSaidNone {
          this.m_farSaidNone = true;
          TandemNative.Log(s"revive: no respawn at Viktor's (\(reach)) and no fast travel point of ours: we wait (\(why))");
        }
        return false;
      }
      let mappin = GameInstance.GetMappinSystem(this.m_game).GetMappin(point.mappinID);
      this.m_farTo = IsDefined(mappin) ? mappin.GetWorldPosition() : player.GetWorldPosition();
      this.m_farYaw = player.GetWorldYaw();
      this.m_farMarker = TandemNative.NodeRefHash(point.GetMarkerRef());
      this.m_farKey = "Revive-AtPoint";
      this.m_farName = GetLocalizedText(point.GetPointDisplayName());
      TandemNative.Log(s"revive: respawn at our fast travel point \(this.m_farName) (\(why); Viktor's: \(reach))");
    }
    this.m_farStage = 1;
    this.m_farAt = this.m_now;
    if VehicleComponent.IsMountedToVehicle(this.m_game, player) {
      TandemVehicles.PutOut(this.m_game, player);
      this.m_farAt = this.m_now + 0.25;
    }
    return true;
  }

  // The jump the way a fast travel makes it (Travel.reds TravelToMarker;
  // FastTravelSystem.PerformFastTravel, final.reds 50415): the fast
  // travel's loading screen asked for, the teleport (to the point's marker,
  // or Viktor's spot), the UI told, the world rebuilt around us. Its end
  // flag is cleared first so that its next true is this travel's. Up once
  // it has ended and the world has run 1.5 s behind it (as FollowTrip
  // waits), else after 30 s.
  private func FollowFar(player: ref<PlayerPuppet>) -> Void {
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().FastTRavelSystem);
    if this.m_farStage == 1 {
      if this.m_now < this.m_farAt {
        return;
      }
      if IsDefined(board) {
        board.SetBool(GetAllBlackboardDefs().FastTRavelSystem.FastTravelLoadingScreenFinished, false);
      }
      let next = new inkSetNextLoadingScreenEvent();
      next.SetNextLoadingScreenType(inkLoadingScreenType.FastTravel);
      GameInstance.GetUISystem(this.m_game).QueueEvent(next);
      player.GetFPPCameraComponent().ResetPitch();
      TandemSync.WorldRebuilt(2);
      let teleport = GameInstance.GetTeleportationFacility(this.m_game);
      if this.m_farMarker != 0ul {
        teleport.TeleportToNode(player, TandemNative.NodeRefOf(this.m_farMarker));
      } else {
        teleport.Teleport(player, this.m_farTo, TandemYawAngles(this.m_farYaw));
      }
      GameInstance.GetUISystem(this.m_game).NotifyFastTravelStart();
      this.m_farStage = 2;
      this.m_farAt = this.m_now;
      this.m_farEndedAt = -1.0;
      TandemNative.Log(s"revive: on the way, the fast travel's loading screen (\(this.m_farTo.X), \(this.m_farTo.Y), \(this.m_farTo.Z))");
      return;
    }
    if this.m_farEndedAt < 0.0 && IsDefined(board) && board.GetBool(GetAllBlackboardDefs().FastTRavelSystem.FastTravelLoadingScreenFinished) {
      this.m_farEndedAt = this.m_now;
    }
    let ready = this.m_farEndedAt >= 0.0 && this.m_now - this.m_farEndedAt >= 1.5;
    if !ready && this.m_now - this.m_farAt < 30.0 {
      return;
    }
    TandemNative.Log(ready ? "revive: there, the loading screen ended" : "revive: there? no end of the loading screen in 30 s");
    this.m_farStage = 0;
    this.m_lost = false;
    this.Notice(SimpleMessageType.Neutral, this.m_farKey, this.m_farName);
    this.Release(player);
  }

  // Not under water (half a metre of it at most).
  private func Dry(at: Vector4) -> Bool {
    let level = 0.0;
    let probe = new Vector4(at.X, at.Y, at.Z + 0.2, 1.0);
    return !AIScriptUtils.GetWaterLevel(this.m_game, Vector4.Vector4To3(probe), level) || level - at.Z < 0.5;
  }

  // ---- speeds and spots (4 and 2 times a second) ----

  private func Track(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    if this.m_now - this.m_speedAt >= 0.25 {
      let here = player.GetWorldPosition();
      let dt = this.m_now - this.m_speedAt;
      this.m_speed = this.m_speedAt > 0.0 && dt < 2.0 ? Vector4.Distance(here, this.m_speedFrom) / dt : 0.0;
      this.m_speedFrom = here;
      this.m_speedAt = this.m_now;
    }
    if this.m_now - this.m_lastMove >= 0.25 {
      let dt = this.m_now - this.m_lastMove;
      this.m_lastMove = this.m_now;
      for p in others {
        let at = TandemNative.RemotePosition(p);
        let i = ArrayFindFirst(this.m_movePlayers, p);
        if i < 0 {
          ArrayPush(this.m_movePlayers, p);
          ArrayPush(this.m_moveFrom, at);
          ArrayPush(this.m_moveSpeeds, 0.0);
        } else {
          let from = this.m_moveFrom[i];
          this.m_moveSpeeds[i] = at.W > 0.0 && from.W > 0.0 && dt < 2.0 ? Vector4.Distance(new Vector4(at.X, at.Y, at.Z, 1.0), new Vector4(from.X, from.Y, from.Z, 1.0)) / dt : 0.0;
          this.m_moveFrom[i] = at;
        }
      }
    }
    if this.m_now - this.m_lastFoot >= 0.5 {
      this.m_lastFoot = this.m_now;
      for p in others {
        this.TrackFoot(player, p);
      }
    }
  }

  // A partner's spot now, kept when it is fit to get up at: up, on foot on
  // the ground (no seat, no ride, not swimming, climbing or in the air), in
  // our city (not elsewhere), on our navmesh (an interior our world lacks
  // has none here) and dry.
  private func TrackFoot(player: ref<PlayerPuppet>, p: Uint32) -> Void {
    let at = TandemNative.RemotePosition(p);
    if at.W <= 0.0 || TandemNative.RemoteLife(p) != 0 || TDBID.IsValid(TandemNative.RemoteSeatVehicle(p))
      || TandemNative.RemoteTransitKind(p) != 0 || TandemNative.RemotePlacement(p) != 0
      || TandemElsewhere.IsElsewhere(this.m_game, p) || TandemElsewhere.BodyHidden(this.m_game, p) {
      return;
    }
    let ground = GameInstance.GetAINavigationSystem(this.m_game).FindPointInSphereForCharacter(new Vector4(at.X, at.Y, at.Z, 1.0), 0.5, player);
    if NotEquals(ground.status, worldNavigationRequestStatus.OK) || !this.Dry(ground.point) {
      return;
    }
    let spot = new Vector4(ground.point.X, ground.point.Y, ground.point.Z, 1.0);
    let i = ArrayFindFirst(this.m_footPlayers, p);
    if i < 0 {
      ArrayPush(this.m_footPlayers, p);
      ArrayPush(this.m_footSpots, spot);
      ArrayPush(this.m_footYaws, TandemNative.RemoteYaw(p));
    } else {
      this.m_footSpots[i] = spot;
      this.m_footYaws[i] = TandemNative.RemoteYaw(p);
    }
  }

  // A partner's speed (m/s), from its position.
  private func SpeedOf(p: Uint32) -> Float {
    let i = ArrayFindFirst(this.m_movePlayers, p);
    return i >= 0 ? this.m_moveSpeeds[i] : 0.0;
  }

  // The downed partner sits in a vehicle.
  private func Seated(p: Uint32) -> Bool {
    return TDBID.IsValid(TandemNative.RemoteSeatVehicle(p));
  }

  // Our camera looks at `body` (within 60 degrees, level): a seated V is
  // revived through the door one faces.
  private func Facing(body: ref<GameObject>, here: Vector4) -> Bool {
    let forward = GameInstance.GetCameraSystem(this.m_game).GetActiveCameraForward();
    let to = body.GetWorldPosition() - here;
    let flatForward = Vector4.Normalize(new Vector4(forward.X, forward.Y, 0.0, 0.0));
    let flatTo = Vector4.Normalize(new Vector4(to.X, to.Y, 0.0, 0.0));
    return Vector4.Dot(flatForward, flatTo) >= 0.5;
  }

  // Reviving a seated V: the car's own door prompt (the same key) kept off
  // with the game's restriction a V riding along wears.
  private func KeepFromCar(player: ref<PlayerPuppet>, want: Bool) -> Void {
    if Equals(want, this.m_carRestricted) {
      return;
    }
    this.m_carRestricted = want;
    if want {
      StatusEffectHelper.ApplyStatusEffect(player, t"GameplayRestriction.VehicleNoInteraction");
    } else {
      StatusEffectHelper.RemoveStatusEffect(player, t"GameplayRestriction.VehicleNoInteraction");
    }
  }

  // Let the state machine stand V up (ResurrectEvents), as the others see.
  private func Release(player: ref<PlayerPuppet>) -> Void {
    this.m_release = true;
    this.m_ours = true;
    this.m_life = 0;
    this.ClearRevivers();
    this.SendOurs(player);
  }

  private func GotUp(player: ref<PlayerPuppet>) -> Void {
    let pools = GameInstance.GetStatPoolsSystem(this.m_game);
    let id = Cast<StatsObjectID>(player.GetEntityID());
    if this.m_ours {
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, 40.0, null, true);
      TandemNative.Log("revive: our V is up at 40 %");
    } else if this.m_protected {
      // Second Heart in co-op: vanilla's multiplayer rule (OnResurrected, 45084).
      pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, 20.0, null, true);
      TandemNative.Log("revive: Second Heart got our V up, at 20 % (co-op rule)");
    }
    this.ClearDown(player, "up");
    // A moment to get one's bearings before the next bullet.
    if this.m_invulnerable {
      this.m_graceUntil = this.m_now + 3.0;
    }
    this.m_upCheckAt = this.m_now + 2.0;
  }

  // Up: the one place every trace of being down, out or dying goes. Our
  // life, the hold on the state machine, a death after leaving the session
  // (TandemSystem.LeftDown, which also locked saving), the HUD bar, NPC
  // senses, and the death state's screen effect.
  private func ClearDown(player: ref<PlayerPuppet>, why: String) -> Void {
    let was = this.m_life != 0 || this.m_release || this.m_dying || this.m_lost;
    this.m_life = 0;
    this.m_release = false;
    this.m_ours = false;
    this.m_dying = false;
    this.m_lost = false;
    this.ClearRevivers();
    this.m_calmSince = -1.0;
    this.m_aloneSince = -1.0;
    let none = EntityID.FromHash(0ul);
    this.m_downCar = none;
    this.m_waiting = 0u;
    this.m_testWreck = false;
    this.m_farStage = 0;
    this.m_noSpotSince = -1.0;
    this.HideBar(false);
    if IsDefined(this.m_system) {
      this.m_system.ClearLeftDown();
    }
    if this.m_madeInvisible {
      player.SetInvisible(false);
      this.m_madeInvisible = false;
    }
    player.SetSenseObjectType(gamedataSenseObjectType.Player);
    // The death state's own effect (DeathEvents, final.reds 527775): its
    // exit stops it, but never twice is harmless.
    GameObjectEffectHelper.BreakEffectLoopEvent(player, n"dying");
    this.SendOurs(player);
    if was {
      TandemNative.Log(s"revive: our V is up, nothing of being down left (\(why))");
    }
  }

  // Two seconds after getting up the player state machine must be past its
  // death state (ExplorationEvents.OnEnter, final.reds 525154: vitals 0, the
  // death sound state ended). If it still says dead, do what that state does
  // for the screen and the sound.
  private func CheckUp(player: ref<PlayerPuppet>) -> Void {
    if this.m_life != 0 {
      return;
    }
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    let vitals = IsDefined(psm) ? psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Vitals) : 0;
    if vitals == 0 {
      TandemNative.Log("revive: up: the state machine is past its death state");
      return;
    }
    TandemNative.Log(s"revive: up, but the state machine still says vitals \(vitals): its death effects ended by hand");
    psm.SetInt(GetAllBlackboardDefs().PlayerStateMachine.Vitals, 0);
    GameObjectEffectHelper.BreakEffectLoopEvent(player, n"dying");
    GameInstance.GetAudioSystem(this.m_game).Play(n"global_death_exit");
  }

  // The host says no V is up any more.
  private func Lost(player: ref<PlayerPuppet>) -> Void {
    TandemNative.Log(s"revive: the session is lost (our V \(this.m_life == 0 ? "up" : "down"))");
    if this.m_life == 0 {
      return;
    }
    if TandemNative.IsHost() {
      this.RealDeath(player);
    } else {
      this.m_lost = true;
      this.m_backInSession = false;
      this.Notice(SimpleMessageType.Negative, "Revive-WaitHost", this.NameOf(1u));
    }
  }

  // The game's own death from where V lies: the death screen, a reload.
  private func RealDeath(player: ref<PlayerPuppet>) -> Void {
    this.m_dying = true;
    this.m_life = 2;
    this.SendOurs(player);
    this.Vulnerable(player);
    this.Unprotect(player);
    this.HideBar(false);
    let psm = GameInstance.GetBlackboardSystem(this.m_game).GetLocalInstanced(player.GetEntityID(), GetAllBlackboardDefs().PlayerStateMachine);
    if IsDefined(psm) {
      psm.SetBool(GetAllBlackboardDefs().PlayerStateMachine.DisplayDeathMenu, true);
    }
    GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValueIgnoreChangeMode(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, 0.0, null);
    TandemNative.Log("revive: our V dies: death screen");
  }

  // NPCs that had `target` as a threat let it go (vanilla drops a V from
  // its hostile NPCs' trackers the same way when the relic's camo blocks).
  private func DropThreats(player: ref<PlayerPuppet>, target: ref<GameObject>) -> Void {
    if !IsDefined(target) {
      return;
    }
    for npc in player.GetNPCsAroundObject(60.0) {
      let tracker = npc.GetTargetTrackerComponent();
      let threat: TrackedLocation;
      if IsDefined(tracker) && tracker.ThreatFromEntity(target, threat) {
        tracker.DeactivateThreat(target);
      }
    }
  }

  // Our V's life and whether it fights, to the others when it changes.
  private func SendOurs(player: ref<PlayerPuppet>) -> Void {
    if this.m_you == 0u {
      return;
    }
    let flags = player.IsInCombat() && this.m_life == 0 ? 1 : 0;
    if this.m_sent && flags == this.m_sentFlags && this.m_life == this.m_sentLife {
      return;
    }
    TandemNative.SendVitals(this.m_life, flags);
    this.m_sent = true;
    this.m_sentLife = this.m_life;
    this.m_sentFlags = flags;
  }

  // ---- reviving a downed partner ----

  private func Revivals(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    // Revives of us that came too late to matter.
    TandemNative.TakeRevives();
    // Not from another world (Elsewhere.reds): our V is not here.
    let able = this.m_life == 0 && this.m_you != 0u && !VehicleComponent.IsMountedToVehicle(this.m_game, player)
      && !TandemElsewhere.Inside(this.m_game);
    let here = player.GetWorldPosition();
    let dt = this.m_lastRevival > 0.0 ? this.m_now - this.m_lastRevival : 0.0;
    this.m_lastRevival = this.m_now;
    let nearest = 0u;
    let best = -1.0;
    if able {
      for p in others {
        let body = this.PuppetOf(p);
        // A downed V whose body the scene took out of sight is not here
        // to revive; one left sitting or lying is. One seated in a car is
        // revived from beside it, through the door we face, once the car
        // has stopped.
        if TandemNative.RemoteLife(p) == 1 && IsDefined(body) && !TandemElsewhere.BodyHidden(this.m_game, p) {
          let seated = this.Seated(p);
          let d = Vector4.Distance(body.GetWorldPosition(), here);
          let reach = seated ? 2.5 : 2.0;
          let fits = !seated || (this.Facing(body, here) && this.SpeedOf(p) <= 1.0);
          if d <= reach && fits && (best < 0.0 || d < best) {
            best = d;
            nearest = p;
          }
        }
      }
    }
    if this.m_holding != 0u {
      this.HoldOn(player, able, dt);
      this.KeepFromCar(player, this.m_holding != 0u && this.Seated(this.m_holding));
      return;
    }
    if nearest != 0u {
      if nearest != this.m_target || this.m_now - this.m_promptAt >= 0.25 {
        this.ShowPrompt(nearest);
      }
    } else {
      this.HidePrompt();
    }
    this.KeepFromCar(player, this.m_target != 0u && this.Seated(this.m_target));
  }

  // Our hold, each tick: given up when they are up, gone or we cannot;
  // when we move away or are hit; paused while their car moves.
  private func HoldOn(player: ref<PlayerPuppet>, able: Bool, dt: Float) -> Void {
    let here = player.GetWorldPosition();
    let body = this.PuppetOf(this.m_holding);
    let seated = this.Seated(this.m_holding);
    let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
    if !able || !IsDefined(body) || TandemNative.RemoteLife(this.m_holding) != 1 {
      this.CancelHold("they are no longer down, or we cannot");
    } else if Vector4.Distance(body.GetWorldPosition(), here) > (seated ? 3.0 : 2.5) && !(this.m_testHold && this.m_now - this.m_holdAt < 2.0) {
      this.CancelHold("moved away");
    } else if health < this.m_holdHealth - 0.5 {
      this.CancelHold("hit");
    } else if seated && this.SpeedOf(this.m_holding) > 1.0 {
      // Their car moves: the hold waits, its progress kept.
      this.m_holdAt += dt;
      if !this.m_holdPaused {
        this.m_holdPaused = true;
        this.SetBarBottom(TandemText.Get("Revive-CarMoving"));
        TandemNative.Log(s"revive: reviving \(this.NameOf(this.m_holding)) waits: their car moves");
      }
    } else {
      if this.m_holdPaused {
        this.m_holdPaused = false;
        this.SetBarBottom(TandemText.Get("Revive-KeepHolding"));
      }
      let progress = (this.m_now - this.m_holdAt) / 10.0;
      this.SetBar(progress);
      if progress >= 1.0 {
        TandemNative.SendRevive(this.m_holding, 2);
        TandemNative.Log(s"revive: we revived \(this.NameOf(this.m_holding))");
        this.m_holding = 0u;
        this.m_testHold = false;
        this.HideBar(true);
        this.HidePrompt();
      }
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    TandemNative.HookRan(n"Revive.TandemRevive.OnAction");
    if !Equals(ListenerAction.GetName(action), n"Choice1") {
      return false;
    }
    if ListenerAction.IsButtonJustPressed(action) && this.m_holding == 0u && this.m_target != 0u {
      this.StartHold(this.m_target, false);
    } else if ListenerAction.IsButtonJustReleased(action) && this.m_holding != 0u && !this.m_testHold {
      this.CancelHold("released");
    }
    return false;
  }

  private func StartHold(target: Uint32, test: Bool) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    this.m_holding = target;
    this.m_holdAt = this.m_now;
    this.m_testHold = test;
    this.m_holdPaused = false;
    this.m_holdHealth = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
    TandemNative.SendRevive(target, 0);
    this.HidePrompt();
    this.ShowBar(1, TandemText.F("Revive-Reviving", this.NameOf(target)), TandemText.Get("Revive-KeepHolding"));
    TandemNative.Log(s"revive: reviving \(this.NameOf(target))\(test ? " (test hold)" : "")");
  }

  private func CancelHold(why: String) -> Void {
    if this.m_holding == 0u {
      return;
    }
    TandemNative.SendRevive(this.m_holding, 1);
    TandemNative.Log(s"revive: stopped reviving \(this.NameOf(this.m_holding)) (\(why))");
    this.m_holding = 0u;
    this.m_testHold = false;
    this.m_holdPaused = false;
    this.HideBar(false);
  }

  // The game's interaction prompt over the downed V: its name, [Hold F] Revive.
  private func ShowPrompt(target: Uint32) -> Void {
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    if this.m_target == 0u {
      this.m_savedVisualizers = FromVariant<VisualizersInfo>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo));
    }
    let choice: InteractionChoiceData;
    choice.inputAction = n"Choice1";
    choice.isHoldAction = true;
    choice.localizedName = TandemText.Get("Revive-Prompt");
    let hub: InteractionChoiceHubData;
    hub.id = TandemRevive.HubId();
    hub.active = true;
    hub.title = this.NameOf(target);
    ArrayPush(hub.choices, choice);
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(hub), true);
    let visualizers: VisualizersInfo;
    visualizers.activeVisId = TandemRevive.HubId();
    ArrayPush(visualizers.visIds, TandemRevive.HubId());
    board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(visualizers), true);
    if target != this.m_target {
      TandemNative.Log(s"revive: \(this.NameOf(target)) is down beside us: revive prompt shown");
    }
    this.m_target = target;
    this.m_promptAt = this.m_now;
  }

  private func HidePrompt() -> Void {
    if this.m_target == 0u {
      return;
    }
    this.m_target = 0u;
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UIInteractions);
    if !IsDefined(board) {
      return;
    }
    // Only ours: the game may have put its own up since.
    let shown = FromVariant<InteractionChoiceHubData>(board.GetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub));
    if shown.id == TandemRevive.HubId() {
      let none: InteractionChoiceHubData;
      none.id = TandemRevive.HubId();
      none.active = false;
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.InteractionChoiceHub, ToVariant(none), true);
      board.SetVariant(GetAllBlackboardDefs().UIInteractions.VisualizersInfo, ToVariant(this.m_savedVisualizers), true);
    }
  }

  public static func HubId() -> Int32 {
    return 731402;
  }

  // ---- the others' Vs ----

  private func FollowOthers(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    let changes = TandemNative.LivesChanges();
    let mark = this.m_now - this.m_lastMark >= 2.0;
    if changes == this.m_livesSeen && !mark {
      return;
    }
    this.m_livesSeen = changes;
    if mark {
      this.m_lastMark = this.m_now;
    }
    for p in others {
      let life = TandemNative.RemoteLife(p);
      let i = ArrayFindFirst(this.m_known, p);
      let was = i >= 0 ? this.m_knownLife[i] : 0;
      if i < 0 {
        ArrayPush(this.m_known, p);
        ArrayPush(this.m_knownLife, life);
      } else {
        this.m_knownLife[i] = life;
      }
      let body = this.PuppetOf(p);
      if life != was {
        this.Changed(p, was, life, body);
      } else if life != 0 && IsDefined(body) {
        // The puppet may have been spawned again since. The NPCs we run (the
        // host's, or a guest's own, regions.md) let it go.
        this.Mark(body, life);
        this.DropThreats(player, body);
      }
    }
  }

  private func Changed(p: Uint32, was: Int32, life: Int32, body: ref<GameObject>) -> Void {
    let name = this.NameOf(p);
    TandemNative.Log(s"revive: \(name) is \(life == 0 ? "up" : life == 1 ? "down" : "out")");
    if life == 1 {
      this.Notice(SimpleMessageType.Negative, "Revive-PlayerDown", name);
    } else if life == 2 && was != 2 {
      this.Notice(SimpleMessageType.Negative, "Revive-PlayerOut", name);
    } else if life == 0 && was != 0 {
      this.Notice(SimpleMessageType.Neutral, "Revive-PlayerUp", name);
    }
    if IsDefined(body) {
      if life == 0 {
        GameInstance.GetVisionModeSystem(this.m_game).CancelForceVisionAppearance(body);
      } else {
        this.Mark(body, life);
      }
    }
  }

  // Vanilla multiplayer's look for an incapacitated player: outlined and
  // filled, through walls.
  private func Mark(body: ref<GameObject>, life: Int32) -> Void {
    let look: VisionAppearance;
    look.showThroughWalls = true;
    look.fill = 1;
    look.outline = life == 1 ? 4 : 1;
    GameInstance.GetVisionModeSystem(this.m_game).ForceVisionAppearance(body, look);
  }

  // ---- helpers ----

  private func Others() -> array<Uint32> {
    if this.m_you == 0u {
      let none: array<Uint32>;
      return none;
    }
    return TandemNative.OtherPlayers();
  }

  private func UpOthers() -> Int32 {
    let up = 0;
    for p in this.Others() {
      if TandemNative.RemoteLife(p) == 0 {
        up += 1;
      }
    }
    return up;
  }

  // Who is up to revive us: "A", "A or B", "A, B or C".
  private func UpNames() -> String {
    let names: array<String>;
    for p in this.Others() {
      if TandemNative.RemoteLife(p) == 0 {
        ArrayPush(names, this.NameOf(p));
      }
    }
    return TandemText.AnyOf(names);
  }

  private func PuppetOf(p: Uint32) -> ref<GameObject> {
    return IsDefined(this.m_system) ? this.m_system.PuppetOf(p) : null;
  }

  private func NameOf(p: Uint32) -> String {
    let name = TandemNative.PlayerName(p);
    return StrLen(name) > 0 ? name : s"player \(p)";
  }

  // The HUD's warning line (Text.reds).
  private func Notice(kind: SimpleMessageType, key: String, opt a: String) -> Void {
    TandemText.NoticeAs(this.m_game, kind, 5.0, key, a);
  }

  // The HUD's progress bar (what quest uploads and the Twintone scan use).
  private func ShowBar(owner: Int32, header: String, bottom: String) -> Void {
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_HUDProgressBar);
    if !IsDefined(board) {
      return;
    }
    this.m_bar = owner;
    board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.Header, header, true);
    board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.BottomText, bottom, true);
    board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.CompletedText, TandemText.Get("Revive-Done"), true);

    board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.FailedText, "", true);
    board.SetVariant(GetAllBlackboardDefs().UI_HUDProgressBar.MessageType, ToVariant(SimpleMessageType.Neutral), true);
    board.SetFloat(GetAllBlackboardDefs().UI_HUDProgressBar.Progress, 0.0, true);
    board.SetBool(GetAllBlackboardDefs().UI_HUDProgressBar.Active, true, true);
  }

  private func SetBar(progress: Float) -> Void {
    if this.m_bar == 0 {
      return;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_HUDProgressBar);
    if IsDefined(board) {
      board.SetFloat(GetAllBlackboardDefs().UI_HUDProgressBar.Progress, MinF(progress, 1.0), true);
    }
  }

  private func SetBarBottom(text: String) -> Void {
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_HUDProgressBar);
    if this.m_bar != 0 && IsDefined(board) {
      board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.BottomText, text, true);
    }
  }

  private func HideBar(done: Bool) -> Void {
    if this.m_bar == 0 {
      return;
    }
    this.m_bar = 0;
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().UI_HUDProgressBar);
    if IsDefined(board) {
      if done {
        board.SetFloat(GetAllBlackboardDefs().UI_HUDProgressBar.Progress, 1.0, true);
      }
      board.SetBool(GetAllBlackboardDefs().UI_HUDProgressBar.Active, false, true);
    }
  }

  // ---- tests (tools/game.sh cmd revive …) ----

  public func Command(command: String) -> Void {
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    if Equals(command, "revive down") || Equals(command, "revive out") {
      // Our V at the end of its health: down (if another V is up), as from
      // a lethal hit; "out": out at once (as killed outright).
      if !this.m_protected {
        TandemNative.Log("revive down: refused, nobody else is here (our V would die)");
        return;
      }
      this.m_testOut = Equals(command, "revive out");
      GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, 0.0, null, true);
      TandemNative.Log(s"revive down: our V's health to 0\(this.m_testOut ? " (out)" : "")");
    } else if StrBeginsWith(command, "revive water") {
      // (test) "revive water N": as if our V's feet were N m under water
      // (deep water's rule as for real water); "off": real water.
      let depth = StrLen(command) > 13 ? StringToFloat(StrAfterFirst(StrAfterFirst(command, " "), " ")) : 0.0;
      this.m_testDepth = MaxF(0.0, depth);
      TandemNative.Log(s"revive water: \(this.m_testDepth > 0.0 ? s"as if \(this.m_testDepth) m under water" : "the real water")");
    } else if StrBeginsWith(command, "revive nospot") {
      // (test) no spot beside a partner fits, as with no navmesh there
      // (a respawn goes to Viktor's); "revive nospot off": the real spots.
      this.m_testNoSpot = !StrEndsWith(command, " off");
      TandemNative.Log(s"revive nospot: \(this.m_testNoSpot ? "no spot beside a partner fits" : "the real spots")");
    } else if StrBeginsWith(command, "revive novik") {
      // (test) Viktor's is out of our story's reach (a far respawn goes to
      // our nearest fast travel point); "revive novik off": the story's.
      this.m_testNoVik = !StrEndsWith(command, " off");
      TandemNative.Log(s"revive novik: \(this.m_testNoVik ? "Viktor's out of reach" : "the story decides")");
    } else if Equals(command, "revive wreck") {
      // (test) the vehicle our V went down in is taken as wrecked.
      this.m_testWreck = true;
      TandemNative.Log(s"revive wreck: our V's car taken as wrecked (down in a seat: \(EntityID.IsDefined(this.m_downCar)))");
    } else if Equals(command, "revive unseat") {
      // (test) out of the seat a respawn gave us.
      if EntityID.IsDefined(this.m_seatedCar) {
        TandemVehicles.Unseat(this.m_game, player, this.m_seatedCar, this.m_seatedSlot);
        let none = EntityID.FromHash(0ul);
        this.m_seatedCar = none;
        TandemNative.Log("revive unseat: out of the respawn's seat");
      } else {
        TandemNative.Log("revive unseat: no respawn seat");
      }
    } else if Equals(command, "revive hold") {
      // Beside the first downed player, then the 10 s hold, with no key.
      for p in this.Others() {
        let body = this.PuppetOf(p);
        if this.m_holding == 0u && TandemNative.RemoteLife(p) == 1 && IsDefined(body) {
          let at = body.GetWorldPosition();
          at.X += 1.0;
          GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, at, TandemYawAngles(player.GetWorldYaw()));
          this.StartHold(p, true);
        }
      }
      if this.m_holding == 0u {
        TandemNative.Log("revive hold: nobody down");
      }
    } else if Equals(command, "revive fresh") {
      // (test) as if our V had never been revived: the next fall is a
      // revivable down, not out (the 300 s cooldown of an earlier test).
      this.m_revivedAt = -1000.0;
      // An earlier test's pretend water, wreck, out, spot or story.
      this.m_testDepth = 0.0;
      this.m_testWreck = false;
      this.m_testOut = false;
      this.m_testNoSpot = false;
      this.m_testNoVik = false;
      // Still down or out from an earlier test: up first.
      if this.m_life != 0 && !this.m_dying {
        TandemNative.Log("revive fresh: our V was down: it stands up");
        this.Release(player);
      }
      TandemNative.Log("revive fresh: no recent revive");
    } else if Equals(command, "revive report") {
      let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
      let reach = this.VikOutOfReach();
      TandemNative.Log(s"revive report: our V life \(this.m_life), health \(health) %, protected \(this.m_protected), invulnerable \(this.m_invulnerable), released \(this.m_release), lost \(this.m_lost), dying \(this.m_dying), left down \(IsDefined(this.m_system) && this.m_system.LeftDown()), \(this.UpOthers()) other(s) up, revived \(RoundF(this.m_now - this.m_revivedAt)) s ago");
      TandemNative.Log(s"revive report: our V in a seat \(EntityID.IsDefined(this.m_downCar)) (\(NameToString(this.m_downSlot))), speed \(this.m_speed) m/s, a revive waiting \(this.m_waiting != 0u), far respawn \(this.m_farStage), Viktor's \(StrLen(reach) == 0 ? "in reach" : reach), a seat to take \(EntityID.IsDefined(this.m_seatCar))");
      for p in this.Others() {
        let foot = ArrayContains(this.m_footPlayers, p);
        TandemNative.Log(s"revive report: \(this.NameOf(p)) life \(TandemNative.RemoteLife(p)), flags \(TandemNative.RemoteVitalsFlags(p)), puppet \(IsDefined(this.PuppetOf(p))), seated \(this.Seated(p)), speed \(this.SpeedOf(p)) m/s, a spot on foot \(foot)");
      }
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }
}

// ---- the player state machine (final.reds 283786, 528038, 528472, 45083) ----

// Our V at the health floor of its co-op Immortal god mode enters the death
// state, as a V with Second Heart does.
@wrapMethod(DeathDecisions)
protected const final func EnterCondition(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if wrappedMethod(stateContext, scriptInterface) {
    return true;
  }
  return TandemRevive.AtFloor(scriptInterface.executionOwner);
}

// Second Heart first, by its own rules; then co-op keeps V down (or out)
// with its resurrection held, instead of the death screen.
@wrapMethod(HighLevelTransition)
protected final func SetIsResurrectionAllowedBasedOnState(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  let owner = scriptInterface.executionOwner;
  let forceKilled = StatusEffectSystem.ObjectHasStatusEffect(owner, t"BaseStatusEffect.ForceKill");
  let landing = IntEnum<gamePSMDetailedLocomotionStates>(scriptInterface.localBlackboard.GetInt(GetAllBlackboardDefs().PlayerStateMachine.LocomotionDetailed));
  let secondHeart = scriptInterface.GetStatsSystem().GetStatValue(Cast<StatsObjectID>(scriptInterface.ownerEntityID), gamedataStatType.ForcePreventResurrect) == 0.0
    && this.HasSecondHeart(scriptInterface) && NotEquals(landing, gamePSMDetailedLocomotionStates.DeathLand) && !forceKilled;
  if !secondHeart && TandemRevive.TakeDeath(owner, forceKilled) {
    stateContext.SetPermanentBoolParameter(n"isResurrectionAllowed", true, true);
    return;
  }
  wrappedMethod(stateContext, scriptInterface);
}

// Down or out: no resurrection until a revive or a respawn lets it go.
@wrapMethod(DeathDecisionsWithResurrection)
protected const final func ToResurrect(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if TandemRevive.Holds(scriptInterface.executionOwner) {
    return false;
  }
  return wrappedMethod(stateContext, scriptInterface);
}

// Swimming leaves for the death state only at the health minimum
// (SwimmingDecisions.ToDeath, final.reds 525674); our floor stops short of
// it, so a V at the floor in the water goes as on land (DeathDecisions
// above). Only reads.
@wrapMethod(SwimmingDecisions)
protected const final func ToDeath(const stateContext: ref<StateContext>, const scriptInterface: ref<StateGameScriptInterface>) -> Bool {
  if wrappedMethod(stateContext, scriptInterface) {
    return true;
  }
  return TandemRevive.AtFloor(scriptInterface.executionOwner);
}

@wrapMethod(PlayerPuppet)
protected func OnResurrected() -> Void {
  TandemNative.HookRan(n"Revive.PlayerPuppet.OnResurrected");
  wrappedMethod();
  TandemRevive.Resurrected(this);
}
