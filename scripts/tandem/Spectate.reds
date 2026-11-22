// Spectating the host's scripted moments, the last resort (docs/architecture/
// story.md, "Spectating"; the rule: core/story.hpp MomentReasons,
// HostMoment, Spectator). The quest runs on the host's game alone. A guest
// that shares the host's moment (a scene, quest restrictions, a quest ride,
// a scripted body mode) plays it from its own V: scene replay shows the
// actors, the restrictions hold it, the ride along seats it. It watches
// through the host's camera only while it cannot take part from its own
// body: (a) a cutscene (tier 5), (b) the host's V in another world, (c) the
// host's V taken beyond its reach (a ride of the moment that left without
// it, a quest teleport), (d) only the host's view shows it (the quest's
// camera animation, a moment the moment list marks watch), (e) the host's V
// among quest interiors our game cannot load. Every switch, in and out,
// happens behind a short black (Curtain.reds), the carry of our V too; at
// most one notice says why, only where the picture would confuse.
//   - the host reads its own V four times a second, from the game's data
//     (no list of quests; the moment list, moments.txt and tandem.ini,
//     marks a moment found broken), says the moment as kStoryMoment in its
//     StoryState (Story.reds SendFlags), kStoryWatch with it for (a), (b),
//     (d), and sends its camera's view every frame while the moment lasts
//     (SceneCamera, paced by the endpoint);
//   - a guest that stood in the moment (grouped, in this world, within
//     40 m) watches while a reason holds, on a camera of its own, following
//     the host's V wherever the moment takes it; its own V is parked:
//     invisible to the NPCs (the visible object component, as vanilla's
//     grace period after a spawn), held still and safe (the spectating
//     holds, Story.reds UpdateHolds: no movement, no combat, no camera,
//     invulnerable), its body out of the picture, hidden from the others
//     (kStorySpectating), and carried along with the host's V (Follow), so
//     the world streams, the NPC links and the sound come with the picture.
//     "Stop watching" (Choice1) leaves; the reason's end (even with the
//     moment going on), the host vanishing or the session ending release
//     it, the V where it was carried to.
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemSpectateFrame extends DelayCallback {
  public let spectate: wref<TandemSpectate>;

  public func Call() -> Void {
    if IsDefined(this.spectate) {
      this.spectate.Frame();
    }
  }
}

public class TandemSpectate {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  private let m_frameOn: Bool;
  // Host: when we last looked at our V, the moment as last decided and its
  // reasons (story::kMoment*).
  private let m_lastScan: Float;
  private let m_moment: Bool;
  private let m_reasons: Int32;
  // Guest: when we last checked, whether we watch and why (Spectator::Why),
  // the camera body we asked
  // for, whether the picture is its, what we did to our V and the host's
  // puppet, whether the player asked to leave (OnAction, acted on the frame).
  private let m_lastCheck: Float;
  private let m_watching: Bool;
  private let m_why: Int32;
  private let m_camera: EntityID;
  private let m_cameraAsked: Bool;
  private let m_cameraLive: Bool;
  private let m_cameraLogged: Bool;
  private let m_madeInvisible: Bool;
  private let m_bodyHidden: Bool;
  private let m_headHidden: Bool;
  // Guest: our V carried with the host's (Follow): the copy of the host's
  // vehicle it is mounted to, the mount's check or its fallback, the last
  // teleport on foot.
  private let m_carryCar: EntityID;
  private let m_carried: Bool;
  private let m_carryMounted: Bool;
  private let m_carryCheckAt: Float;
  private let m_carryFallback: Bool;
  private let m_followedAt: Float;
  private let m_followed: Bool;
  private let m_listening: Bool;
  private let m_leaveAsked: Bool;
  // Guest: our own eyes come back once the curtain is black (FinishNow);
  // whether the moment ended (not left); the notice said for this moment;
  // cmd story spectate missing on (as if we could not load the host's
  // prefabs).
  private let m_finishing: Bool;
  private let m_finishEnded: Bool;
  private let m_told: Bool;
  private let m_testMissing: Bool;
  // Everyone: the other guests that watch now (their puppets hidden here).
  private let m_spectators: array<Uint32>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
  }

  // ---- which effects ----

  // Quest restrictions on how the V moves (no sprint, no jump, forced walk,
  // no vehicle...): a moment. Weapons kept away alone is a place's rule (a
  // club, a safe area), not a moment: Restrictions.reds shares it as is.
  // The phone kept away alone is none either (TandemRestrictions.PhoneOnly).
  public static func Restricts(record: wref<StatusEffect_Record>) -> Bool {
    return TandemRestrictions.IsShared(record) && !TandemRestrictions.KeepsWeaponsAway(record.GetID()) && !TandemRestrictions.PhoneOnly(record);
  }

  // A scripted body or camera mode the story holds the V in (no movement,
  // no camera control, forced crouch; a camera animation is IsCamera's),
  // not one of its
  // own activities (a body carried, a call, a braindance, focus, a locker,
  // a contest, driving in combat): those are not the story's moments.
  public static func IsMode(record: wref<StatusEffect_Record>) -> Bool {
    if !TandemRestrictions.IsRestriction(record) || TandemRestrictions.IsShared(record) {
      return false;
    }
    if record.GameplayTagsContains(n"FistFight") || record.GameplayTagsContains(n"ShootingRangeCompetition") {
      return false;
    }
    let name = TDBID.ToStringDEBUG(record.GetID());
    let own = [
      "BodyCarrying", "PhoneCall", "Braindance", "FastForward", "FocusModeLocomotion", "DriverCombat",
      "VehicleCombat", "SecurityLocker", "FistFight", "ShootingRange", "BlockAllHubMenu", "BlockAllMenu",
      "BlockFastTravel", "InfiniteAmmo"
    ];
    for part in own {
      if StrContains(name, part) {
        return false;
      }
    }
    return !record.GameplayTagsContains(n"CameraAnimation") && (StrContains(name, "NoMovement")
      || StrContains(name, "NoCameraControl") || StrContains(name, "ForceCrouch"));
  }

  // The quest's camera animation holds the V's view (the CameraAnimation
  // tag): a picture the guest's own camera cannot give, only the host's.
  public static func IsCamera(record: wref<StatusEffect_Record>) -> Bool {
    return TandemRestrictions.IsRestriction(record) && !TandemRestrictions.IsShared(record)
      && record.GameplayTagsContains(n"CameraAnimation");
  }

  // ---- host ----

  // Four times a second (Story.reds HostTick): is our V in a scripted
  // moment now?
  public func HostTick(now: Float, tier: Int32, isPrivate: Bool) -> Void {
    if now - this.m_lastScan < 0.25 {
      return;
    }
    this.m_lastScan = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let restricted = false;
    let mode = false;
    let camera = false;
    let effects: array<ref<StatusEffect>>;
    GameInstance.GetStatusEffectSystem(this.m_game).GetAppliedEffects(player.GetEntityID(), effects);
    for effect in effects {
      let record = effect.GetRecord();
      if TandemSpectate.Restricts(record) {
        restricted = true;
      } else if TandemSpectate.IsCamera(record) {
        camera = true;
      } else if TandemSpectate.IsMode(record) {
        mode = true;
      }
    }
    // Put in a vehicle by its scene or quest (Transit.reds: kind 3, a ride
    // our game moves while a scene runs it), not one we drive or called.
    let system = TandemSystem.Of(this.m_game);
    let transit = IsDefined(system) ? system.Transit() : null;
    let mounted = IsDefined(transit) && transit.OurKind() == 3;
    // Cyberspace, a braindance, playing as Johnny (Elsewhere.reds).
    let away = TandemElsewhere.Inside(this.m_game);
    // The moment list (core moments.hpp): what our scene, the quest phases
    // that run and our tracked entry are marked; SpectateReasons reads it.
    TandemStory.MomentListed(this.TrackedPath());
    let reasons = TandemStory.SpectateReasons(tier, isPrivate, restricted, mounted, mode, camera, away);
    let on = TandemStory.SpectateMoment(tier, isPrivate, restricted, mounted, mode, camera, away);
    if NotEquals(on, this.m_moment) || (on && reasons != 0 && reasons != this.m_reasons) {
      if on {
        let watch = TandemStory.SpectateHostWatch();
        TandemNative.Log(s"spectate: our V is in a scripted moment (reasons \(reasons): 1 scene, 2 restrictions, 4 quest ride, 8 scripted mode, 16 another world, 32 test, 64 cutscene, 128 quest camera, 256 marked watch): \(watch ? "only our view shows it, the guests in it watch" : "the guests in it play it from their own V")");
      } else {
        TandemNative.Log("spectate: our moment is over");
      }
    }
    if reasons != 0 {
      this.m_reasons = reasons;
    }
    this.m_moment = on;
    this.StartFrames();
  }

  public func HostMoment() -> Bool {
    return this.m_moment;
  }

  // Our tracked journal entry's path, its ids from the top down
  // ("main_quest/q101_resurrection/..."), for the moment list's quest keys.
  private func TrackedPath() -> String {
    let journal = this.m_story.JournalOf();
    if !IsDefined(journal) {
      return "";
    }
    let entry = journal.GetTrackedEntry();
    let path = "";
    let depth = 0;
    while IsDefined(entry) && depth < 8 {
      let id = entry.GetId();
      path = StrLen(path) > 0 ? id + "/" + path : id;
      entry = journal.GetParentEntry(entry);
      depth += 1;
    }
    return path;
  }

  // The session is over (host): no moment, nobody watched.
  public func HostEnd() -> Void {
    this.m_moment = false;
    this.m_reasons = 0;
    TandemStory.SpectateTest(false);
    this.ShowSpectators();
  }

  // ---- guest ----

  // Four times a second (Story.reds GuestTick): watch the host's moment, or
  // stop.
  public func GuestTick(now: Float) -> Void {
    if now - this.m_lastCheck < 0.25 {
      return;
    }
    this.m_lastCheck = now;
    let player = GetPlayer(this.m_game);
    let story = this.m_story;
    if !IsDefined(player) || !IsDefined(story) {
      return;
    }
    let host = story.HostPlayer();
    let flags = story.StoryFlagsOf(host);
    let at = TandemNative.RemotePosition(host);
    let here = player.GetWorldPosition();
    let system = TandemSystem.Of(this.m_game);
    let transit = IsDefined(system) ? system.Transit() : null;
    let hostCar = IsDefined(system) ? system.CarOf(host) : EntityID.FromHash(0ul);
    let hostRiding = EntityID.IsDefined(hostCar);
    // Restrictions alone (no scene, no ride its scene or quest drives:
    // Transit kind 3): where its V goes is its own free roam, never (c).
    let hostFree = story.TierOf(host) < 2 && TandemNative.RemoteTransitKind(host) != 3;
    // Riding along in the host's copy (seated or carried) or in a car of our
    // own: we play. Our spectating's own carry is not a ride.
    let inVehicle = !this.m_carried && (VehicleComponent.IsMountedToVehicle(this.m_game, player) || (IsDefined(transit) && transit.CarriedBy() != 0u));
    // The host among quest interiors our game cannot load (StoryWorld.reds).
    let world = story.World();
    let cannotLoad = this.m_testMissing || (IsDefined(world) && world.CannotLoad());
    let change = TandemStory.SpectateUpdate((flags & 16) != 0, (flags & 64) != 0, (flags & 8) != 0, story.Grouped(), TandemElsewhere.Inside(this.m_game), TandemElsewhere.IsElsewhere(this.m_game, host), story.TierOf(host), hostRiding, hostFree, inVehicle, cannotLoad, at.W >= 0.5, at.X, at.Y, at.Z, here.X, here.Y, here.Z);
    let why = TandemStory.SpectateWhy();
    if change == 2 {
      this.m_why = why;
      this.Begin(player);
    } else if change == 3 {
      this.Finish(true);
    } else if this.m_watching && why != this.m_why {
      TandemNative.Log(s"spectate: still watching, now \(TandemSpectate.Why(why))");
      this.m_why = why;
    }
    if this.m_watching {
      this.Park(player);
      // Carried only once the picture is the host's: our own eyes never see
      // our V moved.
      if this.m_cameraLive {
        this.Follow(player, now);
      }
      if this.m_headHidden {
        this.HideHead(true);
      }
    } else if !this.m_finishing && (flags & 16) == 0 && !cannotLoad {
      this.m_told = false; // the next moment may say it again
    }
  }

  // Why we watch, as the log says it (story::Spectator::Why).
  public static func Why(why: Int32) -> String {
    if why == 1 {
      return "(a) a cutscene: no player control, the scene's camera";
    }
    if why == 2 {
      return "(b) the host's V in another world our game cannot enter";
    }
    if why == 3 {
      return "(c) the host's V taken beyond our reach, no ride along";
    }
    if why == 4 {
      return "(d) only the host's view shows it: the quest's camera, a moment marked watch, or a test";
    }
    if why == 5 {
      return "(e) the host's V among quest interiors our game cannot load";
    }
    return "none";
  }

  // Our V is parked (Story.reds adds the spectating holds while this says so).
  public func Watching() -> Bool {
    return this.m_watching;
  }

  private func Begin(player: ref<PlayerPuppet>) -> Void {
    this.m_watching = true;
    this.m_leaveAsked = false;
    let name = this.m_story.NameOf(this.m_story.HostPlayer());
    let curtain = this.m_story.Curtain();
    if this.m_finishing {
      // Back before our own eyes came back: the host's view stays, the
      // curtain lifts again (no flicker between two reasons).
      this.m_finishing = false;
      if IsDefined(curtain) {
        curtain.Open(0.3, 0.0);
      }
    } else {
      this.m_followed = false;
      this.m_followedAt = 0.0;
      // Black first: the host's view comes behind it (GuestFrame).
      if IsDefined(curtain) {
        curtain.Close(0.25);
      }
    }
    // One short notice per moment, and only where the picture would
    // confuse: a cutscene explains itself, another world has its own
    // (Elsewhere.reds).
    if !this.m_told && this.m_why >= 3 {
      this.m_told = true;
      this.m_story.Notice(this.m_why == 3 ? "Spectate-Taken" : this.m_why == 5 ? "Spectate-Missing" : "Spectate-Watching", name);
    }
    TandemText.Hint(this.m_game, n"TandemSpectate", n"Choice1", true, TandemText.Get("Spectate-Leave"), inkInputHintHoldIndicationType.Press);
    if !this.m_listening {
      this.m_listening = true;
      player.RegisterInputListener(this, n"Choice1");
    }
    this.Park(player);
    this.StartFrames();
    TandemNative.Log(s"spectate: watching \(name)'s moment through their camera, our V parked: \(TandemSpectate.Why(this.m_why))");
  }

  // What keeps our V out of the moment: unseen by the NPCs (PlayerPuppet.
  // SetInvisible, final.reds:43505: the visible object component vanilla
  // turns off for its grace period after a spawn, :39493), out of the
  // picture (the animation system's fade, PuppetVisibility.reds). Only what
  // we did is undone: an invisibility of its own (optical camo) is left.
  private func Park(player: ref<PlayerPuppet>) -> Void {
    if !this.m_madeInvisible && !player.IsInvisible() {
      player.SetInvisible(true);
      this.m_madeInvisible = true;
    }
    if !this.m_bodyHidden {
      TandemVisibility.Fade(this.m_game, player, false, 0.3);
      this.m_bodyHidden = true;
    }
  }

  // Four times a second while watching: our parked V goes with the host's,
  // so our game streams the world, links with the host's NPCs (regions.md)
  // and listens around the picture, not where the moment began. Hidden,
  // unseen and held all along (Park, the spectating holds). In the host's
  // vehicle: mounted to its copy's trunk body slot, position kept (the
  // ride along's carry, Transit.reds), else put in its cabin each check.
  // On foot: put back on the ground 3 m short of the host's V, on our side
  // of it, when it is more than 15 m away (at most once a second): out of
  // its way, never in a wall. Seated or carried in the host's ride already
  // (Transit.reds): that moves us.
  private func Follow(player: ref<PlayerPuppet>, now: Float) -> Void {
    let host = this.m_story.HostPlayer();
    let at = TandemNative.RemotePosition(host);
    if at.W < 0.5 {
      return;
    }
    let system = TandemSystem.Of(this.m_game);
    if !IsDefined(system) {
      return;
    }
    let transit = system.Transit();
    if !this.m_carried && (VehicleComponent.IsMountedToVehicle(this.m_game, player) || (IsDefined(transit) && transit.CarriedBy() != 0u)) {
      return;
    }
    // Never across a gate our own story keeps closed (StoryWorld.reds): our
    // V stays this side, the picture goes on. A carry already mounted in
    // the host's car rides across as a seat would.
    let world = this.m_story.World();
    if !this.m_carried && IsDefined(world) && world.GateBetween() {
      return;
    }
    let carId = system.CarOf(host);
    let car = TandemTraffic.Body(this.m_game, carId);
    if this.m_carried && (!IsDefined(car) || carId != this.m_carryCar) {
      this.Uncarry(player, false);
    }
    if IsDefined(car) {
      this.CarryWith(player, car, carId, now);
      return;
    }
    let here = player.GetWorldPosition();
    if Vector4.Distance(here, at) <= 15.0 || now - this.m_followedAt < 1.0 {
      return;
    }
    this.m_followedAt = now;
    let back = Vector4.Normalize(new Vector4(here.X - at.X, here.Y - at.Y, 0.0, 0.0));
    let spot = new Vector4(at.X + back.X * 3.0, at.Y + back.Y * 3.0, at.Z, 1.0);
    let navigation = GameInstance.GetAINavigationSystem(this.m_game);
    let ground = navigation.FindPointInSphereForCharacter(spot, 1.5, player);
    if NotEquals(ground.status, worldNavigationRequestStatus.OK) {
      ground = navigation.FindPointInSphereForCharacter(new Vector4(at.X, at.Y, at.Z, 1.0), 3.0, player);
    }
    if NotEquals(ground.status, worldNavigationRequestStatus.OK) {
      TandemNative.LogDebug("spectate: no ground near the host's V to carry ours to");
      return;
    }
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, ground.point, Vector4.ToRotation(at - ground.point));
    if !this.m_followed {
      TandemNative.Log("spectate: our V carried along with the host's");
    }
    this.m_followed = true;
    TandemNative.LogDebug(s"spectate: our V put by the host's at \(ground.point.X) \(ground.point.Y) \(ground.point.Z)");
  }

  // In the host's vehicle: placed in its copy's cabin, then mounted to its
  // trunk body slot (not a seat: no seat of the copy is taken); a copy
  // without the slot (not mounted half a second later): placed again each
  // check instead.
  private func CarryWith(player: ref<PlayerPuppet>, car: ref<VehicleObject>, carId: EntityID, now: Float) -> Void {
    if !this.m_carried {
      this.m_carried = true;
      this.m_carryCar = carId;
      this.m_carryFallback = false;
      this.m_carryMounted = false;
      this.PlaceIn(player, car);
      this.m_followed = true;
      TandemNative.Log("spectate: our V carried along in the host's vehicle");
      return;
    }
    // Mounted at the next check, once the teleport has put our V there
    // (the mount keeps where it stands).
    if !this.m_carryMounted && !this.m_carryFallback {
      TandemVehicles.Carry(this.m_game, player, car, TandemTransit.CarrySlot());
      this.m_carryMounted = true;
      this.m_carryCheckAt = now + 0.5;
      return;
    }
    if this.m_carryCheckAt > 0.0 && now >= this.m_carryCheckAt {
      this.m_carryCheckAt = 0.0;
      if !GameInstance.GetMountingFacility(this.m_game).IsMountedToAnything(player.GetEntityID()) {
        this.m_carryMounted = false;
        this.m_carryFallback = true;
        TandemNative.Log("spectate: the host's vehicle copy has no trunk body slot: our V placed in it four times a second");
        TandemDegraded.Fallback("carried by placement", "the host's vehicle copy has no trunk body slot: our V is placed in it four times a second");
      }
    }
    if this.m_carryFallback {
      this.PlaceIn(player, car);
    }
  }

  private func PlaceIn(player: ref<PlayerPuppet>, car: ref<VehicleObject>) -> Void {
    let forward = car.GetWorldForward();
    let at = car.GetWorldPosition();
    let spot = new Vector4(at.X - forward.X * 0.45, at.Y - forward.Y * 0.45, at.Z + 0.2, 1.0);
    GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, spot, car.GetWorldOrientation().ToEulerAngles());
  }

  // Off the host's vehicle copy; `placeOut`: put down beside it (its right
  // side), as the ride along leaves it.
  private func Uncarry(player: ref<PlayerPuppet>, placeOut: Bool) -> Void {
    if !this.m_carried {
      return;
    }
    if this.m_carryMounted {
      TandemVehicles.Uncarry(this.m_game, player, this.m_carryCar, TandemTransit.CarrySlot());
    }
    let car = TandemTraffic.Body(this.m_game, this.m_carryCar);
    if placeOut && IsDefined(car) {
      let right = car.GetWorldRight();
      let at = car.GetWorldPosition();
      let spot = new Vector4(at.X + right.X * 2.5, at.Y + right.Y * 2.5, at.Z, 1.0);
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(player, spot, car.GetWorldOrientation().ToEulerAngles());
    }
    this.m_carried = false;
    this.m_carryMounted = false;
    this.m_carryFallback = false;
    this.m_carryCheckAt = 0.0;
    this.m_carryCar = EntityID.FromHash(0ul);
    TandemNative.Log("spectate: our V off the host's vehicle");
  }

  // Our own eyes and V again. `ended`: the moment is over (not left by the
  // player): far from the host, we join it the game's way. While the
  // picture is the host's, black first: FinishNow comes behind the curtain
  // (Frame).
  private func Finish(ended: Bool) -> Void {
    if !this.m_watching {
      return;
    }
    this.m_watching = false;
    let player = GetPlayer(this.m_game);
    if IsDefined(player) && this.m_listening {
      player.UnregisterInputListener(this, n"Choice1");
    }
    this.m_listening = false;
    TandemText.Hint(this.m_game, n"TandemSpectate", n"Choice1", false, "", inkInputHintHoldIndicationType.Press);
    let curtain = this.m_story.Curtain();
    if this.m_cameraLive && IsDefined(curtain) {
      this.m_finishing = true;
      this.m_finishEnded = ended;
      curtain.Close(0.25);
      this.StartFrames();
      return;
    }
    this.FinishNow(ended);
    if IsDefined(curtain) {
      curtain.Open(0.3, 0.0);
    }
  }

  private func FinishNow(ended: Bool) -> Void {
    this.m_finishing = false;
    this.ReleaseCamera();
    this.ShowHead();
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      this.Uncarry(player, true);
      if this.m_madeInvisible {
        player.SetInvisible(false);
      }
      if this.m_bodyHidden {
        TandemVisibility.Fade(this.m_game, player, true, 0.3);
      }
    }
    this.m_madeInvisible = false;
    this.m_bodyHidden = false;
    let host = this.m_story.HostPlayer();
    let distance = this.m_story.DistanceToHost();
    TandemNative.Log(s"spectate: our own V again (\(ended ? "the reason is over" : "we left it"): \(TandemSpectate.Why(this.m_why)), \(distance) m from the host\(this.m_followed ? ", carried along" : ""))");
    // Our V was carried along (Follow): it plays on where it is, beside the
    // host. Only when that fell short (no ground near the host, its copy
    // gone, a loading screen) and the reason ended far away: we go to it,
    // behind a loading screen (Travel.reds), with no question asked; never
    // across a gate our own story keeps closed.
    let world = this.m_story.World();
    let gated = IsDefined(world) && world.GateBetween();
    if ended && this.m_story.InSession() && distance > 60.0 && !gated && TandemTravel.RegroupTo(this.m_game, host) {
      TandemNative.Log("spectate: joining the host where the moment left it");
    }
    this.m_why = 0;
  }

  // The session is over or the host is gone: everything back, at once.
  public func Leave() -> Void {
    TandemStory.SpectateLift();
    let finishing = this.m_finishing;
    this.Finish(false);
    if this.m_finishing || finishing {
      this.FinishNow(false);
    }
    let curtain = this.m_story.Curtain();
    if IsDefined(curtain) {
      curtain.Clear();
    }
    this.m_told = false;
    this.ShowSpectators();
  }

  // ---- every frame ----

  private func WantsFrames() -> Bool {
    return this.m_moment || this.m_watching || this.m_cameraAsked || this.m_finishing || ArraySize(this.m_spectators) > 0;
  }

  private func StartFrames() -> Void {
    if !this.m_frameOn && this.WantsFrames() {
      this.m_frameOn = true;
      this.ScheduleFrame();
    }
  }

  private func ScheduleFrame() -> Void {
    let frame = new TandemSpectateFrame();
    frame.spectate = this;
    // 0 s: the next frame. Real time: not slowed by time dilation.
    GameInstance.GetDelaySystem(this.m_game).DelayCallback(frame, 0.0, false);
  }

  public func Frame() -> Void {
    // Our view: while our moment lasts, and while a guest watches through
    // it outside one (reason e).
    if this.m_moment || ArraySize(this.m_spectators) > 0 {
      this.HostFrame();
    }
    if this.m_watching && this.m_leaveAsked {
      this.m_leaveAsked = false;
      if TandemStory.SpectateLeave() {
        TandemNative.Log("spectate: the player stops watching");
        this.Finish(false);
      }
    }
    // Black now: our own eyes and V back behind it, then it lifts (a
    // regroup's loading screen takes over: held a moment longer).
    if this.m_finishing {
      let curtain = this.m_story.Curtain();
      if !IsDefined(curtain) || curtain.IsClosed() {
        let far = this.m_finishEnded && this.m_story.DistanceToHost() > 60.0;
        this.FinishNow(this.m_finishEnded);
        if IsDefined(curtain) {
          curtain.Open(0.35, far ? 1.0 : 0.1);
        }
      }
    }
    if this.m_watching {
      this.GuestFrame();
    } else if !this.m_finishing {
      this.ReleaseCamera();
    }
    if this.WantsFrames() {
      this.ScheduleFrame();
    } else {
      this.m_frameOn = false;
    }
  }

  // Host: what our screen shows now, first person or not (the active
  // camera, as the watch camera samples it: Scenes.reds HostFrame).
  private func HostFrame() -> Void {
    let cameras = GameInstance.GetCameraSystem(this.m_game);
    let transform: Transform; // assigned on every path
    if IsDefined(cameras) && cameras.GetActiveCameraWorldTransform(transform) {
      let at = Transform.GetPosition(transform);
      let q = Transform.GetOrientation(transform);
      TandemStory.MomentCameraSample(at.X, at.Y, at.Z, q.i, q.j, q.k, q.r, cameras.GetActiveCameraFOV());
    }
  }

  // Guest: the host's view on a camera of ours (the watch camera's way,
  // Scenes.reds GuestFrame).
  private func GuestFrame() -> Void {
    let view = TandemStory.SpectateView();
    if !view.on {
      // No view yet, or a gap (a loading screen on the host): the last
      // picture stays.
      return;
    }
    let position = new Vector4(view.x, view.y, view.z, 1.0);
    let orientation = new Quaternion(view.i, view.j, view.k, view.r);
    if !this.m_cameraAsked {
      let spec = new DynamicEntitySpec();
      spec.templatePath = r"base\\entities\\cameras\\simple_free_camera.ent";
      spec.position = position;
      spec.orientation = orientation;
      spec.alwaysSpawned = true;
      spec.persistState = false;
      spec.persistSpawn = false;
      spec.spawnInView = true;
      spec.active = true;
      ArrayPush(spec.tags, n"Tandem.Spectate");
      this.m_camera = TandemEntities.Create(spec);
      this.m_cameraAsked = true;
      this.m_cameraLogged = false;
      TandemNative.Log("spectate: a camera for the host's view asked for");
      return;
    }
    let body = GameInstance.FindEntityByID(this.m_game, this.m_camera);
    if !IsDefined(body) {
      return;
    }
    let lens = body.FindComponentByType(n"gameCameraComponent") as CameraComponent;
    if !IsDefined(lens) {
      if !this.m_cameraLogged {
        this.m_cameraLogged = true;
        TandemNative.Log("spectate: the camera body has no camera component: our own eyes stay");
        TandemDegraded.Fallback("scene camera missing", "the spectator's camera body has no camera component: our own eyes stay");
      }
      return;
    }
    let object = body as GameObject;
    if IsDefined(object) {
      GameInstance.GetTeleportationFacility(this.m_game).Teleport(object, position, Quaternion.ToEulerAngles(orientation));
    } else {
      let placed: WorldTransform; // assigned on every path
      WorldTransform.SetPosition(placed, position);
      WorldTransform.SetOrientation(placed, orientation);
      body.SetWorldTransform(placed);
    }
    lens.SetFOV(view.fov);
    if !this.m_cameraLive {
      // Behind the curtain only, a cut (no blend across the map); it lifts
      // a moment later on the host's view.
      let curtain = this.m_story.Curtain();
      if IsDefined(curtain) && !curtain.IsClosed() {
        return;
      }
      this.m_cameraLive = true;
      // The audio listener too (shouldOverrideAudioListeners, final.reds:
      // 106188, as vanilla's camera takeover does, :102051): without it the
      // listener stays our V's.
      lens.Activate(0.0, true);
      if IsDefined(curtain) {
        curtain.Open(0.35, 0.15);
      }
      TandemNative.Log(s"spectate: watching through the host's camera (fov \(view.fov))");
    }
    this.FirstPerson(position);
  }

  // The host's own eyes (first person): the view sits in its puppet's head
  // (within 1 m of it, 1.5 m to leave). As the game draws V's own first
  // person (its FPP head has no face, hair or headgear), only the puppet's
  // head goes: its arms, hands and what they hold stay in the picture.
  // Third person and scene cameras see the whole puppet.
  private func FirstPerson(view: Vector4) -> Void {
    let host = this.m_story.HostPlayer();
    let at = TandemNative.RemotePosition(host);
    if at.W < 0.5 {
      return;
    }
    let distance = Vector4.Distance2D(view, at);
    if !this.m_headHidden && distance < 1.0 {
      this.m_headHidden = true;
      let count = this.HideHead(true);
      TandemNative.Log(s"spectate: first person: the host's puppet's head hidden (\(count) parts), its arms and weapon shown");
    } else if this.m_headHidden && distance > 1.5 {
      this.ShowHead();
    }
  }

  private func ShowHead() -> Void {
    if this.m_headHidden {
      this.m_headHidden = false;
      // A puppet hidden whole for a reason of its own (PuppetVisibility.reds)
      // keeps its meshes hidden: that reason shows them again.
      if !TandemVisibility.IsHidden(this.m_game, this.m_story.HostPlayer()) {
        this.HideHead(false);
      }
    }
  }

  // The host's puppet's head meshes (face, eyes, brows, teeth, hair, beard,
  // headgear) and what it wears on head, eyes and face, hidden or shown
  // (IVisualComponent.TemporaryHide). Again each check while hidden: a look
  // applied since builds them again. Returns how many were touched.
  private func HideHead(hidden: Bool) -> Int32 {
    let puppet = this.m_story.PuppetOf(this.m_story.HostPlayer());
    if !IsDefined(puppet) {
      return 0;
    }
    let count = 0;
    for component in puppet.GetComponents() {
      let visual = component as IVisualComponent;
      if IsDefined(visual) && TandemSpectate.IsHeadPart(NameToString(component.GetName())) {
        visual.TemporaryHide(hidden);
        count += 1;
      }
    }
    let transactions = GameInstance.GetTransactionSystem(this.m_game);
    for slot in [t"AttachmentSlots.Head", t"AttachmentSlots.Eyes", t"AttachmentSlots.Face"] {
      let item = transactions.GetItemInSlot(puppet, slot);
      if IsDefined(item) {
        for component in item.GetComponents() {
          let visual = component as IVisualComponent;
          if IsDefined(visual) {
            visual.TemporaryHide(hidden);
            count += 1;
          }
        }
      }
    }
    return count;
  }

  // A head mesh by its component's name: the game's mesh prefixes (h0_ head,
  // h1_ headgear, hh_ hair, he_ eyes, heb_ brows, hb_ beard, ht_ teeth, hx_
  // head cyberware) or a word of them. Unmeasured on the puppet: `story
  // spectate parts` lists its meshes.
  public static func IsHeadPart(name: String) -> Bool {
    let lower = StrLower(name);
    for prefix in ["h0_", "h1_", "hh_", "he_", "heb_", "hb_", "ht_", "hx_"] {
      if StrBeginsWith(lower, prefix) {
        return true;
      }
    }
    for word in ["head", "hair", "beard", "eyes", "eyebrow", "teeth", "face", "glasses", "mask", "hat_"] {
      if StrContains(lower, word) {
        return true;
      }
    }
    return false;
  }

  // Our own eyes again; the camera body goes.
  private func ReleaseCamera() -> Void {
    if this.m_cameraLive {
      this.m_cameraLive = false;
      let body = GameInstance.FindEntityByID(this.m_game, this.m_camera);
      let lens = IsDefined(body) ? body.FindComponentByType(n"gameCameraComponent") as CameraComponent : null;
      if IsDefined(lens) {
        lens.Deactivate(0.0, true);
      }
      TandemNative.Log("spectate: our own camera again");
    }
    if this.m_cameraAsked {
      this.m_cameraAsked = false;
      TandemEntities.Delete(this.m_camera);
    }
  }

  // "Stop watching": only noted here, done on the next frame.
  // job thread safe: sets a field of its own; Frame acts on it.
  protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
    if this.m_watching && Equals(ListenerAction.GetName(action), n"Choice1")
      && Equals(ListenerAction.GetType(action), gameinputActionType.BUTTON_RELEASED) {
      this.m_leaveAsked = true;
    }
  }

  // ---- the others' spectators ----

  // A player's story flags (Story.reds, every StoryState): a guest that
  // watches the host's moment is parked where it stood: its puppet hidden,
  // and unseen by our NPCs (its visible object component off: ScriptedPuppet
  // .GetVisibleObjectComponent, final.reds:20573).
  public func OnPlayerFlags(player: Uint32, flags: Int32) -> Void {
    let watching = (flags & 32) != 0;
    if NotEquals(watching, ArrayContains(this.m_spectators, player)) {
      if watching {
        ArrayPush(this.m_spectators, player);
        this.StartFrames();
        TandemVisibility.Hide(this.m_game, player, n"spectate", 0.5);
        TandemNative.Log(s"spectate: player \(player) watches the host's moment: its puppet parked out of sight");
      } else {
        ArrayRemove(this.m_spectators, player);
        TandemVisibility.Show(this.m_game, player, n"spectate", 0.5);
        TandemNative.Log(s"spectate: player \(player) plays again");
      }
      this.SeenByNpcs(player, !watching);
      if TandemNative.IsHost() {
        TandemStory.SpectateViewWanted(ArraySize(this.m_spectators) > 0);
      }
    }
  }

  private func SeenByNpcs(player: Uint32, seen: Bool) -> Void {
    let puppet = this.m_story.PuppetOf(player) as ScriptedPuppet;
    let sense = IsDefined(puppet) ? puppet.GetVisibleObjectComponent() : null;
    if IsDefined(sense) {
      sense.Toggle(seen);
    }
  }

  // Every second or so: a puppet built again is unseen again.
  public func KeepSpectatorsHidden() -> Void {
    for player in this.m_spectators {
      this.SeenByNpcs(player, false);
    }
  }

  // A player left: nothing of ours held for it.
  public func OnPlayerGone(player: Uint32) -> Void {
    if ArrayContains(this.m_spectators, player) {
      ArrayRemove(this.m_spectators, player);
      TandemVisibility.Show(this.m_game, player, n"spectate", 0.0);
      TandemStory.SpectateViewWanted(ArraySize(this.m_spectators) > 0);
    }
  }

  private func ShowSpectators() -> Void {
    for player in this.m_spectators {
      TandemVisibility.Show(this.m_game, player, n"spectate", 0.0);
      this.SeenByNpcs(player, true);
    }
    ArrayClear(this.m_spectators);
    TandemStory.SpectateViewWanted(false);
  }

  // ---- commands (tools/game.sh cmd story spectate …) ----

  // (test reset) the test switch off: a failed spectate-missing left it on
  // for the next scenario (spectate-ride watched for its reason, 2026-11-21).
  public func ClearTest() -> Void {
    if this.m_testMissing {
      this.m_testMissing = false;
      TandemNative.Log("spectate: test: the host's interiors as they are here (test reset)");
    }
  }

  public func Command(command: String) -> Void {
    let host = TandemNative.IsHost();
    if Equals(command, "story spectate on") || Equals(command, "story spectate off") {
      let on = Equals(command, "story spectate on");
      if host {
        // (test, host) as if our V were in a scripted moment.
        TandemStory.SpectateTest(on);
        this.m_lastScan = 0.0;
        TandemNative.Log(s"spectate: test moment \(on ? "on" : "off")");
      } else {
        // (guest) watching allowed again, or not for the rest of the session.
        TandemStory.SpectateAllow(on);
        if !on && TandemStory.SpectateLeave() {
          this.Finish(false);
        }
        TandemNative.Log(s"spectate: watching \(on ? "allowed" : "off for this session")");
      }
    } else if Equals(command, "story spectate parts") {
      this.Parts();
    } else if StrBeginsWith(command, "story spectate missing") {
      // (test, guest) as if our game could not load the host's prefabs.
      this.m_testMissing = Equals(command, "story spectate missing on");
      TandemNative.Log(s"spectate: test: the host's interiors \(this.m_testMissing ? "missing" : "as they are") here");
    } else if StrBeginsWith(command, "story moments test") {
      // (test, host) as if the moment list marked our moment.
      let verdict = StrEndsWith(command, " watch") ? 1 : StrEndsWith(command, " play") ? 2 : 0;
      TandemStory.MomentsTest(verdict);
      this.m_lastScan = 0.0;
      TandemNative.Log(s"moments: test verdict \(verdict == 1 ? "watch" : verdict == 2 ? "play" : "off: the list's own")");
    } else if StrBeginsWith(command, "story moments") {
      TandemNative.Log(s"moments: \(TandemStory.MomentsReport(this.TrackedPath()))");
    } else {
      this.Report();
    }
  }

  // (measure) the host's puppet's meshes, each marked when first person
  // hides it as a head part.
  private func Parts() -> Void {
    let puppet = this.m_story.PuppetOf(this.m_story.HostPlayer());
    if !IsDefined(puppet) {
      TandemNative.Log("spectate: no puppet of the host here");
      return;
    }
    let parts = "";
    for component in puppet.GetComponents() {
      if IsDefined(component as IVisualComponent) {
        let name = NameToString(component.GetName());
        parts += s" \(name)\(TandemSpectate.IsHeadPart(name) ? "*" : "")";
      }
    }
    TandemNative.Log(s"spectate: the host's puppet's meshes (* head, hidden in first person):\(parts)");
  }

  public func Report() -> Void {
    let spectators = "";
    for player in this.m_spectators {
      spectators += s" \(player)";
    }
    TandemNative.Log(s"spectate: on \(TandemStory.SpectateOn()), host moment \(this.m_moment) (reasons \(this.m_reasons), to watch \(TandemStory.SpectateHostWatch())), test \(TandemStory.SpectateTestOn()), in the host's moment \(TandemStory.SpectateShared()), watching \(this.m_watching) (why: \(TandemSpectate.Why(this.m_why))), camera \(this.m_cameraLive ? "the host's" : "ours"), carried \(this.m_followed)\(this.m_carried ? " (in the host's vehicle)" : ""), host's head hidden \(this.m_headHidden), spectators:\(StrLen(spectators) > 0 ? spectators : " none")");
  }
}
