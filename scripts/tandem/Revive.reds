// Down, revive, respawn (docs/ARCHITECTURE.md "Down and out", milestone 8).
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
  // Someone reviving us, since when.
  private let m_reviver: Uint32;
  private let m_reviverAt: Float;
  // Out: since when the partner we respawn beside has been out of combat.
  private let m_calmSince: Float;
  // Since when nobody else is in the session.
  private let m_aloneSince: Float;
  private let m_lastDrop: Float;

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

  // Our V is down or out: the co-op menu does not leave or switch sessions
  // from there (the state machine's wraps need this object to stand V up).
  public func IsDown() -> Bool {
    return this.m_life != 0 || this.m_dying;
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
      this.Unprotect(player);
      this.Vulnerable(player);
    }
    this.m_listening = false;
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
    this.Protect(player, ArraySize(others) > 0 || this.m_life != 0);
    if this.m_invulnerable && this.m_life == 0 && this.m_graceUntil > 0.0 && now >= this.m_graceUntil {
      this.Vulnerable(player);
    }
    this.FollowOthers(player, others);
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

  public func SessionEnded() -> Void {
    this.CancelHold("the session ended");
    this.HidePrompt();
  }

  // ---- our V's protection ----

  private func Protect(player: ref<PlayerPuppet>, want: Bool) -> Void {
    if want && !this.m_protected {
      GameInstance.GetGodModeSystem(this.m_game).EnableOverride(player.GetEntityID(), gameGodModeType.Immortal, n"tandem_revive");
      this.m_protected = true;
      TandemNative.Log("revive: another player is here: our V goes down instead of dying");
    } else if !want && this.m_protected {
      this.Unprotect(player);
      TandemNative.Log("revive: alone: our V dies the game's way");
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
    if forceKilled || cooldown {
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
    this.m_reviver = 0u;
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
    if life == 1 {
      this.Notice(s"You are down. \(this.UpNames()) can revive you.", SimpleMessageType.Negative);
    } else if this.UpOthers() > 0 {
      this.Notice(s"You are out. You will get up beside \(this.UpNames()).", SimpleMessageType.Negative);
    } else {
      this.Notice("Everyone is down.", SimpleMessageType.Negative);
    }
  }

  private func WhileDown(player: ref<PlayerPuppet>, others: array<Uint32>) -> Void {
    for revive in TandemNative.TakeRevives() {
      this.RevivedBy(player, revive.player, revive.step);
    }
    if this.m_reviver != 0u && this.m_bar == 2 {
      this.SetBar((this.m_now - this.m_reviverAt) / 10.0);
    }
    if this.m_now - this.m_lastDrop >= 1.0 {
      this.m_lastDrop = this.m_now;
      this.DropThreats(player, player);
      this.Invulnerable(player);
    }
    // Nobody else in the session (they left, or the host is gone).
    if ArraySize(others) == 0 {
      if this.m_aloneSince < 0.0 {
        this.m_aloneSince = this.m_now;
      }
      let wait = this.m_lost ? 120.0 : 30.0;
      if this.m_now - this.m_aloneSince >= wait {
        TandemNative.Log(s"revive: alone and down for \(RoundF(wait)) s: our V dies");
        this.RealDeath(player);
      }
      return;
    }
    this.m_aloneSince = -1.0;
    if this.m_lost {
      // Guest: the host reloaded and we are back in its session: up beside
      // the host's V once it is up and seen.
      if this.m_backInSession && TandemNative.RemoteLife(1u) == 0 && TandemNative.RemotePosition(1u).W > 0.0 {
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
      this.m_reviver = reviver;
      this.m_reviverAt = this.m_now;
      this.ShowBar(2, s"\(name) is reviving you", "Stay down");
      TandemNative.Log(s"revive: \(name) is reviving us");
    } else if step == 1 {
      if reviver == this.m_reviver {
        this.m_reviver = 0u;
        this.HideBar(false);
        TandemNative.Log(s"revive: \(name) stopped reviving us");
      }
    } else if step == 2 {
      let at = TandemNative.RemotePosition(reviver);
      let distance = at.W > 0.0 ? Vector4.Distance(new Vector4(at.X, at.Y, at.Z, 1.0), player.GetWorldPosition()) : -1.0;
      if distance < 0.0 || distance > 4.0 {
        TandemNative.Log(s"revive: \(name)'s revive refused: \(distance) m away");
        return;
      }
      this.m_revivedAt = this.m_now;
      this.HideBar(true);
      TandemNative.Log(s"revive: revived by \(name) (\(distance) m away)");
      this.Notice(s"\(name) revived you", SimpleMessageType.Neutral);
      this.Release(player);
    }
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

  // Up again beside `partner`'s V (behind it, facing its way).
  private func Respawn(player: ref<PlayerPuppet>, partner: Uint32, why: String) -> Void {
    let at = TandemNative.RemotePosition(partner);
    // Radians (interpolation.hpp); the game's forward is +Y turned by yaw:
    // (-sin, cos), so behind is (sin, -cos).
    let yaw = TandemNative.RemoteYaw(partner);
    let to = new Vector4(at.X + SinF(yaw) * 1.5, at.Y - CosF(yaw) * 1.5, at.Z, 1.0);
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, to, TandemYawAngles(yaw * 57.29578));
    this.m_lost = false;
    TandemNative.Log(s"revive: respawn beside \(this.NameOf(partner)) (\(why))");
    this.Notice(s"Back beside \(this.NameOf(partner))", SimpleMessageType.Neutral);
    this.Release(player);
  }

  // Let the state machine stand V up (ResurrectEvents), as the others see.
  private func Release(player: ref<PlayerPuppet>) -> Void {
    this.m_release = true;
    this.m_ours = true;
    this.m_life = 0;
    this.m_reviver = 0u;
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
    this.m_ours = false;
    this.m_release = false;
    if this.m_madeInvisible {
      player.SetInvisible(false);
      this.m_madeInvisible = false;
    }
    player.SetSenseObjectType(gamedataSenseObjectType.Player);
    // A moment to get one's bearings before the next bullet.
    if this.m_invulnerable {
      this.m_graceUntil = this.m_now + 3.0;
    }
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
      this.Notice(s"Everyone is down. Waiting for \(this.NameOf(1u)) to load the last checkpoint.", SimpleMessageType.Negative);
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
    let able = this.m_life == 0 && this.m_you != 0u && !VehicleComponent.IsMountedToVehicle(this.m_game, player);
    let here = player.GetWorldPosition();
    let nearest = 0u;
    let best = -1.0;
    if able {
      for p in others {
        let body = this.PuppetOf(p);
        if TandemNative.RemoteLife(p) == 1 && IsDefined(body) {
          let d = Vector4.Distance(body.GetWorldPosition(), here);
          if d <= 2.0 && (best < 0.0 || d < best) {
            best = d;
            nearest = p;
          }
        }
      }
    }
    if this.m_holding != 0u {
      let body = this.PuppetOf(this.m_holding);
      let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
      if !able || !IsDefined(body) || TandemNative.RemoteLife(this.m_holding) != 1 {
        this.CancelHold("they are no longer down, or we cannot");
      } else if Vector4.Distance(body.GetWorldPosition(), here) > 2.5 {
        this.CancelHold("moved away");
      } else if health < this.m_holdHealth - 0.5 {
        this.CancelHold("hit");
      } else {
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
      return;
    }
    if nearest != 0u {
      if nearest != this.m_target || this.m_now - this.m_promptAt >= 0.25 {
        this.ShowPrompt(nearest);
      }
    } else {
      this.HidePrompt();
    }
  }

  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
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
    this.m_holdHealth = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
    TandemNative.SendRevive(target, 0);
    this.HidePrompt();
    this.ShowBar(1, s"Reviving \(this.NameOf(target))", "Hold to revive");
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
    choice.localizedName = "Revive";
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
        // The puppet may have been spawned again since.
        this.Mark(body, life);
        if TandemNative.IsHost() {
          this.DropThreats(player, body);
        }
      }
    }
  }

  private func Changed(p: Uint32, was: Int32, life: Int32, body: ref<GameObject>) -> Void {
    let name = this.NameOf(p);
    TandemNative.Log(s"revive: \(name) is \(life == 0 ? "up" : life == 1 ? "down" : "out")");
    if life == 1 {
      this.Notice(s"\(name) is down", SimpleMessageType.Negative);
    } else if life == 2 && was != 2 {
      this.Notice(s"\(name) is out", SimpleMessageType.Negative);
    } else if life == 0 && was != 0 {
      this.Notice(s"\(name) is back up", SimpleMessageType.Neutral);
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

  private func UpNames() -> String {
    let names = "";
    for p in this.Others() {
      if TandemNative.RemoteLife(p) == 0 {
        names += StrLen(names) > 0 ? s" or \(this.NameOf(p))" : this.NameOf(p);
      }
    }
    return StrLen(names) > 0 ? names : "Nobody";
  }

  private func PuppetOf(p: Uint32) -> ref<GameObject> {
    return IsDefined(this.m_system) ? this.m_system.PuppetOf(p) : null;
  }

  private func NameOf(p: Uint32) -> String {
    let name = TandemNative.PlayerName(p);
    return StrLen(name) > 0 ? name : s"player \(p)";
  }

  // TODO: localized strings.
  private func Notice(text: String, kind: SimpleMessageType) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 5.0;
    message.message = text;
    message.type = kind;
    GameInstance
      .GetBlackboardSystem(this.m_game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
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
    board.SetString(GetAllBlackboardDefs().UI_HUDProgressBar.CompletedText, "Revived", true);
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
    if Equals(command, "revive down") {
      // Our V at the end of its health: down (if another V is up), as from
      // a lethal hit.
      if !this.m_protected {
        TandemNative.Log("revive down: refused, nobody else is here (our V would die)");
        return;
      }
      GameInstance.GetStatPoolsSystem(this.m_game).RequestSettingStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, 0.0, null, true);
      TandemNative.Log("revive down: our V's health to 0");
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
    } else if Equals(command, "revive report") {
      let health = GameInstance.GetStatPoolsSystem(this.m_game).GetStatPoolValue(Cast<StatsObjectID>(player.GetEntityID()), gamedataStatPoolType.Health, true);
      TandemNative.Log(s"revive report: our V life \(this.m_life), health \(health) %, protected \(this.m_protected), invulnerable \(this.m_invulnerable), released \(this.m_release), lost \(this.m_lost), \(this.UpOthers()) other(s) up, revived \(RoundF(this.m_now - this.m_revivedAt)) s ago");
      for p in this.Others() {
        TandemNative.Log(s"revive report: \(this.NameOf(p)) life \(TandemNative.RemoteLife(p)), flags \(TandemNative.RemoteVitalsFlags(p)), puppet \(IsDefined(this.PuppetOf(p)))");
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

@wrapMethod(PlayerPuppet)
protected func OnResurrected() -> Void {
  wrappedMethod();
  TandemRevive.Resurrected(this);
}
