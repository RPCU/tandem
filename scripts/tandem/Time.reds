// Slowed time in a shared world (docs/ARCHITECTURE.md, open problems:
// slowed and stopped time; research in docs/research/coop-time.md).
//
// Single player slows the whole world for one V (TimeSystem.SetTimeDilation,
// a global factor). In co-op the world is shared, so no V's ability may slow
// it as a whole. The rules, per mechanism:
//
// - Sandevistan (reason "sandevistan", SandevistanEvents.OnEnter,
//   final.reds:524119) and Kerenzikov ("kereznikov", KerenzikovEvents.OnEnter,
//   final.reds:527831): a time bubble. The NPCs within 50 m of that V are
//   slowed, for every player, by the factor the implant asks for (its stat
//   TimeDilationSandevistanTimeScale / TimeDilationKerenzikovTimeScale): the
//   host slows its real NPCs with the per-entity dilation vanilla gives a
//   single NPC (TimeDilatable.SetIndividualTimeDilation, final.reds:14727;
//   vanilla slows one NPC to 0.05 for 2.5 s that way, final.reds:525204),
//   each guest its proxies of them, so they animate at the speed they move.
//   Rules of the bubbles (several Vs, overlaps, ends): core's
//   tandem/timebubble.hpp. Everything outside the bubbles keeps normal
//   speed, and so does every V: two Vs in a Sandevistan at once each see the
//   NPCs slowed and the other V at normal speed.
//   * Its owner: a guest keeps its single-player slow motion on its own
//     screen (its world is not the shared one), but what the network drives
//     there (proxies, the other Vs' puppets) is exempted from its global
//     factor and set to its true shared speed, so motion and animation
//     agree. The host's world is the shared one: by default (tandem.ini
//     time_host = bubble) its global factor is taken back within the frame
//     (kept "active" at normal speed, so the state machine carries on) and
//     the bubble alone slows what matters, the enemies around it; with
//     time_host = full it keeps the whole slow motion and exempts what it
//     shares, as a guest does (experimental: its projectiles, physics and
//     clock still slow).
//   * The others: that V's puppet keeps normal speed among slowed enemies,
//     with the implant's effect on it (PuppetFx, effect name to confirm).
// - Scanner and quickhack panel ("focusMode", TimeDilationFocusModeEvents,
//   final.reds:529159), Focus ("focusedStatePerkDilation", final.reds:542858),
//   Deadeye's reload ("coolReloadPerkDilation", final.reds:547583), Berserk
//   ("berserk", SetTimeDilationEffector final.reds:307231), melee hit stop
//   ("meleeHit", final.reds:45789) and deflect/one-punch ("deflect",
//   final.reds:39679): local only, no bubble. A guest keeps them on its
//   screen (shared things exempted, as above); the host drops their world
//   part (time_host = bubble). Others see the V as it is: standing, scanning.
// - Second chance (BaseStatusEffect.SecondChancePerkTimeDilation, its
//   dilation in TweakDB): its reason is unknown; any other global dilation
//   left on the host is logged ("an unknown dilation") to find it.
// - Menus, pause, time skips: not here (Pause.reds; ARCHITECTURE).

public class TandemTimeApplied {
  public let id: EntityID;
  // The factor set on it, with the local global factor ignored (-1: given
  // back this frame).
  public let factor: Float;
  // Still a target this frame.
  public let seen: Bool;
}

public class TandemTime {
  private let m_game: GameInstance;
  // Our abilities as last sent (proto::kTime* bits) and their world speed.
  private let m_sentAbilities: Int32;
  private let m_sentScale: Float;
  // The speed a taken-back global dilation is kept at: 1.0, or just under if
  // the time system reads a dilation of exactly 1 as none (checked once).
  private let m_neutral: Float;
  private let m_neutralChecked: Bool;
  // Reasons taken back during their current activation (logged once each).
  private let m_taken: array<CName>;
  private let m_unknownLogged: Float;
  // What we set on entities, and the bubble update's clock (real time).
  private let m_applied: array<ref<TandemTimeApplied>>;
  private let m_lastBubbles: Float;
  // Remote players' abilities as last seen (for the puppet's effect).
  private let m_seenChanges: Int32;
  private let m_fxPlayers: array<Uint32>;
  private let m_fx: CName;
  // Bubble sizes as last logged, by owner.
  private let m_bubbleOwners: array<Uint32>;
  private let m_bubbleSizes: array<Int32>;
  // (test) "time test SCALE SECONDS": a dilation set as Sandevistan's.
  private let m_testUntil: Float;
  private let m_testScale: Float;
  private let m_measureFrom: Float;
  private let m_measureSim: Float;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_neutral = 1.0;
    this.m_sentScale = 1.0;
    this.m_fx = TandemTime.PuppetFx();
  }

  // The implant's effect played on a puppet while its player is in a
  // Sandevistan. The NPC Sandevistan's own effect is in TweakDB/entity data
  // we have not read: this is the one vanilla's Sandevistan boss carries
  // (AdamSmasher, final.reds:296271). Try others in game: "time fx NAME".
  public static func PuppetFx() -> CName {
    return n"sandevistan_medium_00";
  }

  public static func Of(game: GameInstance) -> ref<TandemTime> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.TimeSync() : null;
  }

  // Every frame, from the plugin (plugin/src/main.cpp): a dilation that
  // would slow the shared world is taken back within the frame it began.
  public static func Frame(game: GameInstance) -> Void {
    // The plugin hands an empty game instance: the game's own global is the
    // right one (as TandemPause.Frame).
    let time = TandemTime.Of(GetGameInstance());
    if IsDefined(time) {
      time.Update();
    }
  }

  private func TandemSys() -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(this.m_game).Get(n"TandemSystem") as TandemSystem;
  }

  private func RealNow() -> Float {
    return EngineTime.ToFloat(GameInstance.GetEngineTime(this.m_game));
  }

  private func SimNow() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  // ---- the frame ----

  public func Update() -> Void {
    if !TandemNative.IsStarted() {
      return;
    }
    let player = GetPlayer(this.m_game);
    let system = this.TandemSys();
    // Not behind the loading screen: the plugin calls this every frame from
    // the game's running state, and the world is still being built there
    // (the game crashed in this frame on a save load, 2026-11-18).
    if !IsDefined(player) || !IsDefined(system) || system.You() == 0u || !system.IsPlaying() {
      return;
    }
    let times = GameInstance.GetTimeSystem(this.m_game);
    let now = this.RealNow();
    this.EndTest(times, now);
    this.SendOurs(times, player);
    // Alone in the session, single player rules: nothing is shared.
    let together = ArraySize(system.PuppetPlayers()) > 0;
    if together && TandemNative.IsHost() && !TandemNative.IsTimeHostFull() {
      this.TakeBack(times);
    } else {
      ArrayClear(this.m_taken);
    }
    this.PuppetEffects(system);
    if now - this.m_lastBubbles >= 0.05 {
      this.m_lastBubbles = now;
      this.Bubbles(system, player);
    }
    // Our world runs slower than the network (a guest's Sandevistan, the
    // host with time_host = full): what the network drives is exempted.
    let exempt = together && times.GetActiveTimeDilation() < 0.99;
    this.Apply(system, exempt);
    this.Measure(now);
  }

  // Our V's time abilities, to the others when they change.
  private func SendOurs(times: ref<TimeSystem>, player: ref<GameObject>) -> Void {
    let abilities = 0;
    let scale = 1.0;
    let stats = GameInstance.GetStatsSystem(this.m_game);
    let id = Cast<StatsObjectID>(player.GetEntityID());
    if times.IsTimeDilationActive(n"sandevistan") {
      abilities += 1;
      scale = MinF(scale, TandemTime.ScaleOf(stats.GetStatValue(id, gamedataStatType.TimeDilationSandevistanTimeScale), this.TestScale()));
    }
    if times.IsTimeDilationActive(n"kereznikov") {
      abilities += 2;
      scale = MinF(scale, TandemTime.ScaleOf(stats.GetStatValue(id, gamedataStatType.TimeDilationKerenzikovTimeScale), 1.0));
    }
    if times.IsTimeDilationActive(n"focusedStatePerkDilation") || times.IsTimeDilationActive(n"coolReloadPerkDilation")
      || times.IsTimeDilationActive(n"berserk") || times.IsTimeDilationActive(n"meleeHit") || times.IsTimeDilationActive(n"deflect") {
      abilities += 4;
    }
    if times.IsTimeDilationActive(n"focusMode") {
      abilities += 8;
    }
    if abilities == this.m_sentAbilities && AbsF(scale - this.m_sentScale) < 0.01 {
      return;
    }
    this.m_sentAbilities = abilities;
    this.m_sentScale = scale;
    TandemNative.SendTimeEffect(abilities, scale);
    TandemNative.Log(s"time: our abilities \(abilities) (world speed \(scale))");
  }

  // An implant's stat, or the test's scale when there is none (no implant).
  private static func ScaleOf(stat: Float, fallback: Float) -> Float {
    return stat > 0.0 && stat < 1.0 ? stat : fallback;
  }

  private func TestScale() -> Float {
    return this.m_testUntil > 0.0 ? this.m_testScale : 0.25;
  }

  // Host (time_host = bubble): the world part of our V's dilations taken
  // back. The reasons a state holds are kept active at normal speed (their
  // state ends on "no longer active", and unsets them itself); the ones that
  // expire on their own are unset.
  private func TakeBack(times: ref<TimeSystem>) -> Void {
    this.Hold(times, n"sandevistan", 999.0);
    this.Hold(times, n"kereznikov", 999.0);
    this.Hold(times, n"focusMode", 999.0);
    this.Hold(times, n"focusedStatePerkDilation", 12.0);
    this.Hold(times, n"coolReloadPerkDilation", 3.0);
    this.Hold(times, n"berserk", 999.0);
    this.Drop(times, n"meleeHit");
    this.Drop(times, n"deflect");
    let left = times.GetActiveTimeDilation();
    let now = this.RealNow();
    if left < 0.99 && now - this.m_unknownLogged > 5.0 {
      this.m_unknownLogged = now;
      TandemNative.Log(s"time: an unknown dilation slows the world to \(left) (a menu, or a reason to add)");
    }
  }

  private func Hold(times: ref<TimeSystem>, reason: CName, duration: Float) -> Void {
    if !times.IsTimeDilationActive(reason) {
      ArrayRemove(this.m_taken, reason);
      return;
    }
    let asked = times.GetActiveTimeDilation(reason, true);
    if asked >= 0.99 {
      return;
    }
    times.SetTimeDilation(reason, this.m_neutral, duration, n"None", n"None");
    if !this.m_neutralChecked {
      this.m_neutralChecked = true;
      if !times.IsTimeDilationActive(reason) {
        this.m_neutral = 0.999;
        times.SetTimeDilation(reason, this.m_neutral, duration, n"None", n"None");
        TandemNative.Log("time: a dilation of 1.0 reads as none: taken-back dilations are kept at 0.999");
      }
    }
    if !ArrayContains(this.m_taken, reason) {
      ArrayPush(this.m_taken, reason);
      TandemNative.Log(s"time: \(NameToString(reason)) would slow the shared world to \(asked): kept at \(this.m_neutral), still active \(times.IsTimeDilationActive(reason))");
    }
  }

  private func Drop(times: ref<TimeSystem>, reason: CName) -> Void {
    if times.IsTimeDilationActive(reason) {
      times.UnsetTimeDilation(reason, n"None");
      TandemNative.Log(s"time: \(NameToString(reason)) dropped (it would slow the shared world)");
    }
  }

  // ---- the bubbles ----

  private func Bubbles(system: ref<TandemSystem>, player: ref<GameObject>) -> Void {
    let owners: array<Uint32>;
    let scales: array<Float>;
    let at: array<Vector4>;
    if (this.m_sentAbilities & 3) != 0 && this.m_sentScale < 1.0 {
      ArrayPush(owners, system.You());
      ArrayPush(scales, this.m_sentScale);
      ArrayPush(at, player.GetWorldPosition());
    }
    for other in TandemNative.TimePlayers() {
      let scale = TandemNative.TimeScale(other);
      let spot = TandemNative.RemotePosition(other);
      if (TandemNative.TimeAbilities(other) & 3) != 0 && scale < 1.0 && spot.W != 0.0 {
        ArrayPush(owners, other);
        ArrayPush(scales, scale);
        ArrayPush(at, spot);
      }
    }
    let ids: array<Uint32>;
    let positions: array<Vector4>;
    let npcs = system.NpcsSync();
    if IsDefined(npcs) {
      let shared = npcs.TimeIds();
      let entities = npcs.TimeEntities();
      let i = 0;
      while i < ArraySize(shared) {
        let body = GameInstance.FindEntityByID(this.m_game, entities[i]) as GameObject;
        if IsDefined(body) {
          ArrayPush(ids, shared[i]);
          ArrayPush(positions, body.GetWorldPosition());
        }
        i += 1;
      }
    }
    let changes = TandemNative.UpdateTimeBubbles(owners, scales, at, ids, positions);
    if ArraySize(changes) > 0 {
      this.ReportBubbles(owners);
    }
  }

  // One line per bubble whose size changed, and per bubble that ended.
  private func ReportBubbles(owners: array<Uint32>) -> Void {
    let i = 0;
    while i < ArraySize(this.m_bubbleOwners) {
      if !ArrayContains(owners, this.m_bubbleOwners[i]) {
        TandemNative.Log(s"time: bubble of player \(this.m_bubbleOwners[i]) ended");
      }
      i += 1;
    }
    let sizes: array<Int32>;
    for owner in owners {
      let size = TandemNative.TimeBubbleMembers(owner);
      let index = ArrayFindFirst(this.m_bubbleOwners, owner);
      if index < 0 || this.m_bubbleSizes[index] != size {
        TandemNative.Log(s"time: bubble of player \(owner): \(size) NPC(s)");
      }
      ArrayPush(sizes, size);
    }
    this.m_bubbleOwners = owners;
    this.m_bubbleSizes = sizes;
  }

  // Each NPC we share or show at its bubble's speed, ignoring our own global
  // factor; with `exempt`, everything else the network drives (the other
  // NPCs, the other Vs' puppets) at normal speed, ignoring it too.
  private func Apply(system: ref<TandemSystem>, exempt: Bool) -> Void {
    for applied in this.m_applied {
      applied.seen = false;
    }
    let npcs = system.NpcsSync();
    if IsDefined(npcs) {
      let shared = npcs.TimeIds();
      let entities = npcs.TimeEntities();
      let i = 0;
      while i < ArraySize(shared) {
        this.Want(entities[i], TandemNative.TimeFactorOf(shared[i]), exempt, shared[i]);
        i += 1;
      }
    }
    for other in system.PuppetPlayers() {
      let puppet = system.PuppetOf(other);
      if IsDefined(puppet) {
        this.Want(puppet.GetEntityID(), 1.0, exempt, 0u);
      }
    }
    // No longer a target (gone, dead, no longer shared): given back, unless
    // Want gave it back already (factor -1).
    let kept: array<ref<TandemTimeApplied>>;
    for applied in this.m_applied {
      if applied.seen {
        ArrayPush(kept, applied);
      } else if applied.factor >= 0.0 {
        this.Release(applied.id, n"None");
      }
    }
    this.m_applied = kept;
  }

  private func Want(id: EntityID, factor: Float, exempt: Bool, npc: Uint32) -> Void {
    let wanted = factor < 0.999 || exempt;
    let applied = this.Find(id);
    if IsDefined(applied) {
      applied.seen = wanted;
      if wanted && AbsF(applied.factor - factor) > 0.001 {
        this.Set(id, factor, npc);
        applied.factor = factor;
      } else if !wanted && applied.factor >= 0.0 {
        // Out of its last bubble: eased back like the implant's own end.
        this.Release(id, applied.factor < 0.999 ? n"SandevistanEaseOut" : n"None");
        if npc != 0u && applied.factor < 0.999 {
          TandemNative.Log(s"time: npc \(npc) back to normal speed");
        }
        applied.factor = -1.0;
      }
      return;
    }
    if !wanted {
      return;
    }
    if this.Set(id, factor, npc) {
      let entry = new TandemTimeApplied();
      entry.id = id;
      entry.factor = factor;
      entry.seen = true;
      ArrayPush(this.m_applied, entry);
    }
  }

  private func Set(id: EntityID, factor: Float, npc: Uint32) -> Bool {
    let body = GameInstance.FindEntityByID(this.m_game, id) as gamePuppet;
    if !IsDefined(body) {
      return false;
    }
    if body.HasIndividualTimeDilation() {
      body.UnsetIndividualTimeDilation(n"None");
    }
    body.SetIndividualTimeDilation(n"tandemTime", factor, 999.0, n"None", n"None", true, false);
    if npc != 0u && factor < 0.999 {
      TandemNative.Log(s"time: npc \(npc) at \(factor) (now \(body.GetTimeDilationValue()), ignoring our global factor \(body.IsIgnoringGlobalTimeDilation()))");
    }
    return true;
  }

  private func Release(id: EntityID, curve: CName) -> Void {
    let body = GameInstance.FindEntityByID(this.m_game, id) as gamePuppet;
    if IsDefined(body) && body.HasIndividualTimeDilation(n"tandemTime") {
      body.UnsetIndividualTimeDilation(curve);
    }
  }

  private func Find(id: EntityID) -> ref<TandemTimeApplied> {
    for applied in this.m_applied {
      if applied.id == id {
        return applied;
      }
    }
    return null;
  }

  // ---- what the others see ----

  // A remote player in a Sandevistan: the implant's effect on their puppet.
  private func PuppetEffects(system: ref<TandemSystem>) -> Void {
    let changes = TandemNative.TimeChanges();
    if changes == this.m_seenChanges {
      return;
    }
    this.m_seenChanges = changes;
    let active: array<Uint32>;
    for other in TandemNative.TimePlayers() {
      let abilities = TandemNative.TimeAbilities(other);
      if (abilities & 1) != 0 {
        ArrayPush(active, other);
      }
      TandemNative.Log(s"time: player \(other) abilities \(abilities) (world speed \(TandemNative.TimeScale(other)))");
    }
    for other in active {
      if !ArrayContains(this.m_fxPlayers, other) {
        let puppet = system.PuppetOf(other);
        if IsDefined(puppet) {
          GameObjectEffectHelper.StartEffectEvent(puppet, this.m_fx, false);
        }
        TandemNative.Log(s"time: player \(other) in a sandevistan (effect \(NameToString(this.m_fx)) on its puppet: \(IsDefined(puppet)))");
      }
    }
    for other in this.m_fxPlayers {
      if !ArrayContains(active, other) {
        let puppet = system.PuppetOf(other);
        if IsDefined(puppet) {
          GameObjectEffectHelper.BreakEffectLoopEvent(puppet, this.m_fx);
        }
        TandemNative.Log(s"time: player \(other)'s sandevistan ended");
      }
    }
    this.m_fxPlayers = active;
  }

  // ---- the session ends, the world goes ----

  public func End() -> Void {
    TandemNative.ClearTimeBubbles();
    for applied in this.m_applied {
      this.Release(applied.id, n"None");
    }
    ArrayClear(this.m_applied);
    ArrayClear(this.m_fxPlayers);
    ArrayClear(this.m_bubbleOwners);
    ArrayClear(this.m_bubbleSizes);
    ArrayClear(this.m_taken);
    this.m_sentAbilities = 0;
    this.m_sentScale = 1.0;
  }

  public func Stop() -> Void {
    this.End();
    if this.m_testUntil > 0.0 {
      GameInstance.GetTimeSystem(this.m_game).UnsetTimeDilation(n"sandevistan", n"None");
      this.m_testUntil = 0.0;
    }
  }

  // ---- tests (tools/game.sh cmd time ...) ----

  // "time test SCALE SECONDS": a global dilation as Sandevistan's (no state
  // machine behind it); on the host it must be taken back, and the world
  // speed (sim time over real time) is logged a second in.
  // "time fx NAME": the effect played on puppets in a Sandevistan, now.
  // "time report": what this game does with time now.
  public func Command(command: String) -> Void {
    let times = GameInstance.GetTimeSystem(this.m_game);
    if StrBeginsWith(command, "time test") {
      let rest = StrAfterFirst(command, "test ");
      let scale = StringToFloat(StrBeforeFirst(rest + " ", " "), 0.25);
      let seconds = StringToFloat(StrAfterFirst(rest, " "), 3.0);
      this.m_testScale = scale;
      this.m_testUntil = this.RealNow() + seconds;
      this.m_measureFrom = this.RealNow();
      this.m_measureSim = this.SimNow();
      times.SetTimeDilation(n"sandevistan", scale, 999.0, n"None", n"None");
      TandemNative.Log(s"time: test dilation \(scale) for \(seconds) s");
    } else if StrBeginsWith(command, "time fx ") {
      let system = this.TandemSys();
      for other in this.m_fxPlayers {
        let old = IsDefined(system) ? system.PuppetOf(other) : null;
        if IsDefined(old) {
          GameObjectEffectHelper.BreakEffectLoopEvent(old, this.m_fx);
        }
      }
      this.m_fx = StringToName(StrAfterFirst(command, "fx "));
      if IsDefined(system) {
        for other in system.PuppetPlayers() {
          let puppet = system.PuppetOf(other);
          if IsDefined(puppet) {
            GameObjectEffectHelper.StartEffectEvent(puppet, this.m_fx, false);
          }
        }
      }
      TandemNative.Log(s"time: puppet effect \(NameToString(this.m_fx)) played on every puppet");
    } else if Equals(command, "time report") {
      TandemNative.Log(s"time: world speed here \(times.GetActiveTimeDilation()), our abilities \(this.m_sentAbilities) (\(this.m_sentScale)), \(ArraySize(this.m_applied)) entities set, taken back \(ArraySize(this.m_taken)), neutral \(this.m_neutral), host full \(TandemNative.IsTimeHostFull())");
      for applied in this.m_applied {
        let body = GameInstance.FindEntityByID(this.m_game, applied.id) as gamePuppet;
        if IsDefined(body) {
          TandemNative.Log(s"time: \(EntityID.ToDebugStringDecimal(applied.id)) at \(applied.factor) (reads \(body.GetTimeDilationValue()))");
        }
      }
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }

  private func EndTest(times: ref<TimeSystem>, now: Float) -> Void {
    if this.m_testUntil > 0.0 && now >= this.m_testUntil {
      this.m_testUntil = 0.0;
      times.UnsetTimeDilation(n"sandevistan", n"None");
      TandemNative.Log("time: test dilation over");
    }
  }

  // A second into a test: how fast the world ran (sim time over real time).
  private func Measure(now: Float) -> Void {
    if this.m_measureFrom > 0.0 && now - this.m_measureFrom >= 1.0 {
      let speed = (this.SimNow() - this.m_measureSim) / (now - this.m_measureFrom);
      this.m_measureFrom = 0.0;
      TandemNative.Log(s"time: world speed \(speed) over the test's first second");
    }
  }
}
