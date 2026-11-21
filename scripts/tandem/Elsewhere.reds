// Other worlds (docs/architecture/elsewhere.md, other worlds; engine-knowledge §13.18):
// a V in cyberspace, in a braindance, playing as Johnny (a takeover, a
// flashback) or as someone else, often far from its body.
//
// Our V: which world it is in is read here four times a second (the game's
// own signals, below); every state sent goes through the plugin's tracker
// (core elsewhere.hpp), which tells when the scene took V away from its body:
// then the body's place and pose, as they were, are what the others get, and
// our NPCs are not shared (they are that world's).
//
// The others: a player whose body was taken away is hidden here with a
// notice, unless the body sits or lies (a netrunner chair, a braindance rig),
// then it stays there as it was. While our own V is somewhere the others are
// not (cyberspace, a braindance, a scene's other place), every puppet and
// marker is hidden from us. Coming back shows them again; nothing stays
// hidden when a player leaves or the session ends.
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemAwayPlayer {
  public let player: Uint32;
  // As the player's game says (TandemAwayState): 0 the city.
  public let place: Int32;
  public let flags: Int32;
  // Hidden here, and when it may show again (back: once its puppet has
  // moved to where the player really is).
  public let hidden: Bool;
  public let revealAt: Float;
}

public class TandemElsewhere {
  private let m_game: GameInstance;
  private let m_lastTick: Float;
  // Our V: its world now (TandemAwayState.place), why we think so, whether
  // it sits or lies, and the test's world ("elsewhere test PLACE"; -1 none).
  private let m_place: Int32;
  private let m_why: String;
  private let m_settled: Bool;
  private let m_test: Int32;
  private let m_testUntil: Float;
  // "elsewhere state" reports, numbered (a scenario expects the Nth).
  private let m_reports: Int32;
  // What the others are told (place + 256 * flags), as last logged.
  private let m_told: Int32;
  // Our V is where the others are not; since when it is back.
  private let m_inside: Bool;
  private let m_backAt: Float;
  // The other players we know of.
  private let m_players: array<ref<TandemAwayPlayer>>;
  private let m_changes: Uint32;

  public static func Get(game: GameInstance) -> ref<TandemElsewhere> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.Elsewhere() : null;
  }

  // Our V is somewhere the other players are not: they are hidden from us.
  public static func Inside(game: GameInstance) -> Bool {
    let away = TandemElsewhere.Get(game);
    return IsDefined(away) && away.IsInside();
  }

  public func IsInside() -> Bool {
    return this.m_inside;
  }

  // The scene took our V away from its body: the NPCs around it are the
  // other world's.
  public static func AwayFromBody(game: GameInstance) -> Bool {
    let ours = TandemAway.Ours();
    return (ours.flags & 1) != 0;
  }

  // A player in another world (not the city), and one away from its body.
  public static func IsElsewhere(game: GameInstance, player: Uint32) -> Bool {
    let away = TandemElsewhere.Get(game);
    let known = IsDefined(away) ? away.Find(player) : null;
    return IsDefined(known) && known.place != 0;
  }

  // A player whose body is out of sight here (away, not sitting or lying):
  // not here to be revived, hit or met.
  public static func BodyHidden(game: GameInstance, player: Uint32) -> Bool {
    let away = TandemElsewhere.Get(game);
    let known = IsDefined(away) ? away.Find(player) : null;
    return IsDefined(known) && (known.flags & 1) != 0 && (known.flags & 2) == 0;
  }

  public static func IsAwayFromBody(game: GameInstance, player: Uint32) -> Bool {
    let away = TandemElsewhere.Get(game);
    let known = IsDefined(away) ? away.Find(player) : null;
    return IsDefined(known) && (known.flags & 1) != 0;
  }

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_test = -1;
    this.m_place = 0;
    this.m_told = 0;
    this.m_inside = false;
    ArrayClear(this.m_players);
    // A save loaded: our V is in its body, whatever the last game was.
    TandemAway.Reset();
  }

  // The game is gone (its entities with it): nothing to show again.
  public func Stop() -> Void {
    ArrayClear(this.m_players);
    this.m_inside = false;
  }

  // ---- our V ----

  // Every state sent (TandemSystem.SampleLocal): what to send of our V.
  public func Sample(position: Vector4, orientation: Quaternion, mounted: Bool) -> TandemAwayBody {
    return TandemAway.Sample(position, orientation, mounted, this.m_settled, this.m_place);
  }

  // Which world our V is in, by the game's own signals:
  // - a braindance: the Braindance blackboard (final.reds:48378), set by
  //   BraindanceSystem from PlayerPuppet.OnRewindableSectionEvent (:45353);
  // - cyberspace: a status effect tagged Cyberspace (GameplayRestriction.
  //   Cyberspace: no radial, no quick slots, :528002) or CyberspacePresence
  //   (no footsteps, no camera bob, :43947), or a cyberspace district (the
  //   gamedataDistrict *Cyberspace entries, :4716, as the district manager
  //   puts them on UI_Map.currentLocationEnumName, :194095);
  // - Johnny: our V is Character.johnny_replacer (PlayerPuppet.
  //   IsJohnnyReplacer, :39362); someone else: any other replacer record
  //   (IsReplacer, :39344: not Character.Player_Puppet_Base).
  private func Classify(player: ref<PlayerPuppet>) -> Int32 {
    // A test world never outlives its test: a scenario that stopped half
    // way left one on and every later puppet hidden (2026-11-19).
    if this.m_test >= 0 && this.m_lastTick > this.m_testUntil {
      TandemNative.Log(s"elsewhere: test \(TandemElsewhere.PlaceName(this.m_test)) over (2 minutes)");
      this.m_test = -1;
    }
    if this.m_test >= 0 {
      this.m_why = "a test";
      return this.m_test;
    }
    let boards = GameInstance.GetBlackboardSystem(this.m_game);
    if boards.Get(GetAllBlackboardDefs().Braindance).GetBool(GetAllBlackboardDefs().Braindance.IsActive) {
      this.m_why = "Braindance.IsActive";
      return 2;
    }
    if StatusEffectSystem.ObjectHasStatusEffectWithTag(player, n"Cyberspace") {
      this.m_why = "a Cyberspace status effect";
      return 1;
    }
    if StatusEffectSystem.ObjectHasStatusEffectWithTag(player, n"CyberspacePresence") {
      this.m_why = "a CyberspacePresence status effect";
      return 1;
    }
    let district = boards.Get(GetAllBlackboardDefs().UI_Map).GetString(GetAllBlackboardDefs().UI_Map.currentLocationEnumName);
    if StrContains(district, "Cyberspace") {
      this.m_why = s"the district \(district)";
      return 1;
    }
    if player.IsJohnnyReplacer() {
      this.m_why = "Character.johnny_replacer";
      return 3;
    }
    if player.IsReplacer() {
      this.m_why = TDBID.ToStringDEBUG(player.GetRecordID());
      return 4;
    }
    return 0;
  }

  public static func PlaceName(place: Int32) -> String {
    switch place {
      case 1:
        return "cyberspace";
      case 2:
        return "a braindance";
      case 3:
        return "Johnny's body";
      case 4:
        return "another body";
      default:
        return "the city";
    }
  }

  // Four times a second (TandemSystem.Tick), in a session or not: our V's
  // world goes on through a join.
  public func Tick(now: Float, system: ref<TandemSystem>, you: Uint32) -> Void {
    if now - this.m_lastTick < 0.25 && now >= this.m_lastTick {
      return;
    }
    this.m_lastTick = now;
    let player = GetPlayer(this.m_game);
    if IsDefined(player) {
      // A chair, a bed, a braindance rig: a workspot (final.reds:53275).
      this.m_settled = GameInstance.GetWorkspotSystem(this.m_game).IsActorInWorkspot(player);
      let place = this.Classify(player);
      if place != this.m_place {
        if place == 0 {
          TandemNative.Log(s"elsewhere: our V is back from \(TandemElsewhere.PlaceName(this.m_place))");
        } else {
          let at = player.GetWorldPosition();
          TandemNative.Log(s"elsewhere: our V is in \(TandemElsewhere.PlaceName(place)) (\(this.m_why)), \(this.m_settled ? "settled" : "standing") at (\(at.X), \(at.Y), \(at.Z))");
        }
        this.m_place = place;
      }
    }
    let ours = TandemAway.Ours();
    let told = ours.place + 256 * ours.flags;
    if told != this.m_told {
      this.m_told = told;
      if (ours.flags & 1) != 0 {
        TandemNative.Log(s"elsewhere: away from our body, which stays \((ours.flags & 2) != 0 ? "sitting or lying" : "out of sight") at (\(ours.x), \(ours.y), \(ours.z))");
      } else {
        TandemNative.Log(s"elsewhere: the others are told we are in \(TandemElsewhere.PlaceName(ours.place))");
      }
    }
    let inside = this.m_place == 1 || this.m_place == 2 || (ours.flags & 1) != 0;
    if NotEquals(inside, this.m_inside) {
      this.m_inside = inside;
      if inside {
        TandemNative.Log("elsewhere: we are where the others are not: their puppets and markers hidden");
      } else {
        this.m_backAt = now;
        TandemNative.Log("elsewhere: we are back: the others shown again");
      }
    }
    if you != 0u {
      this.Follow(now, system);
    }
  }

  // ---- the others ----

  public func Find(player: Uint32) -> ref<TandemAwayPlayer> {
    for known in this.m_players {
      if known.player == player {
        return known;
      }
    }
    return null;
  }

  private func Known(player: Uint32) -> ref<TandemAwayPlayer> {
    let known = this.Find(player);
    if !IsDefined(known) {
      known = new TandemAwayPlayer();
      known.player = player;
      ArrayPush(this.m_players, known);
    }
    return known;
  }

  private func Follow(now: Float, system: ref<TandemSystem>) -> Void {
    let changes = TandemAway.Changes();
    if changes != this.m_changes {
      this.m_changes = changes;
      let others = TandemAway.Others();
      let listed: array<Uint32>;
      for state in others {
        ArrayPush(listed, state.player);
        let known = this.Known(state.player);
        if state.place != known.place || state.flags != known.flags {
          this.Announce(known, state.place, state.flags, now);
        }
      }
      for known in this.m_players {
        if known.place != 0 && !ArrayContains(listed, known.player) {
          this.Announce(known, 0, 0, now);
        }
      }
    }
    let game = this.m_game;
    for player in system.PuppetPlayers() {
      let known = this.Known(player);
      // Only for a reason: we are elsewhere (or just back), or their own
      // last state says their body is out of sight. A player never heard
      // of as away is shown.
      let why = "";
      if this.m_inside {
        why = s"we are in \(TandemElsewhere.PlaceName(this.m_place)) (\(this.m_why))";
      } else if this.m_backAt > 0.0 && now - this.m_backAt < 0.75 {
        why = "we are just back";
      } else if now < known.revealAt {
        why = "they are just back";
      } else if (known.flags & 1) != 0 && (known.flags & 2) == 0 {
        why = s"they are in \(TandemElsewhere.PlaceName(known.place)), body out of sight";
      }
      let hide = StrLen(why) > 0;
      // The shared set of reasons (PuppetVisibility.reds): a private scene
      // that hides the same puppet keeps it hidden after we let go.
      if hide {
        // Every time: a marker made again (a puppet built again) stays off.
        TandemMarkers.Suppress(game, player, true);
        if !known.hidden {
          known.hidden = true;
          TandemVisibility.Hide(game, player, n"elsewhere", 0.0);
          TandemNative.Log(s"elsewhere: player \(player)'s puppet hidden: \(why)");
        }
      } else if known.hidden {
        known.hidden = false;
        TandemVisibility.Show(game, player, n"elsewhere", 0.0);
        TandemMarkers.Suppress(game, player, false);
        TandemNative.Log(s"elsewhere: player \(player)'s puppet shown");
      }
    }
  }

  // A player's world changed: a notice the game's way, and the log.
  private func Announce(known: ref<TandemAwayPlayer>, place: Int32, flags: Int32, now: Float) -> Void {
    let name = TandemNative.PlayerName(known.player);
    if StrLen(name) == 0 {
      name = s"player \(known.player)";
    }
    let moved = (flags & 1) != 0;
    let wasMoved = (known.flags & 1) != 0;
    if place == 0 {
      // Back from away: a hidden puppet shows once it has moved to where
      // they really are (one shown all along, in place or sitting, stays).
      if wasMoved && (known.flags & 2) == 0 {
        known.revealAt = now + 0.75;
      }
      if wasMoved || known.place == 1 || known.place == 2 {
        TandemText.Notice(this.m_game, "Elsewhere-Back", name);
      }
      TandemNative.Log(s"elsewhere: player \(known.player) is back from \(TandemElsewhere.PlaceName(known.place))");
    } else {
      if place != known.place || NotEquals(moved, wasMoved) {
        TandemText.Notice(this.m_game, TandemElsewhere.NoticeOf(place, moved), name);
      }
      let body = "in place";
      if moved {
        body = (flags & 2) != 0 ? "left sitting or lying where it was" : "out of sight: hidden";
      }
      TandemNative.Log(s"elsewhere: player \(known.player) is in \(TandemElsewhere.PlaceName(place)), body \(body)");
    }
    known.place = place;
    known.flags = flags;
  }

  // The notice's key (Text.reds) for a player's world.
  public static func NoticeOf(place: Int32, moved: Bool) -> String {
    switch place {
      case 1:
        return "Elsewhere-Cyberspace";
      case 2:
        return "Elsewhere-Braindance";
      case 3:
        return moved ? "Elsewhere-JohnnyMemory" : "Elsewhere-Johnny";
      default:
        return moved ? "Elsewhere-Somewhere" : "Elsewhere-Someone";
    }
  }


  // A puppet out of sight: its own meshes and what it wears and holds
  // (IVisualComponent.TemporaryHide, final.reds:106206). Through
  // TandemVisibility only: it knows every reason a puppet is hidden for.
  public static func HideBody(game: GameInstance, body: ref<GameObject>, hide: Bool) -> Void {
    if !IsDefined(body) {
      return;
    }
    TandemElsewhere.HideVisuals(body, hide);
    let transactions = GameInstance.GetTransactionSystem(game);
    let slots = TandemOutfit.Slots();
    ArrayPush(slots, t"AttachmentSlots.WeaponRight");
    ArrayPush(slots, t"AttachmentSlots.WeaponLeft");
    for slot in slots {
      let item = transactions.GetItemInSlot(body, slot);
      if IsDefined(item) {
        TandemElsewhere.HideVisuals(item, hide);
      }
    }
  }

  private static func HideVisuals(entity: ref<Entity>, hide: Bool) -> Void {
    for component in entity.GetComponents() {
      let visual = component as IVisualComponent;
      if IsDefined(visual) {
        visual.TemporaryHide(hide);
      }
    }
  }

  // A player left: nothing of theirs to keep.
  public func Forget(player: Uint32) -> Void {
    let kept: array<ref<TandemAwayPlayer>>;
    for known in this.m_players {
      if known.player != player {
        ArrayPush(kept, known);
      }
    }
    this.m_players = kept;
  }

  // The session ended: everyone is back in the city (their puppets go).
  public func End() -> Void {
    let away = 0;
    for known in this.m_players {
      if known.hidden {
        TandemMarkers.Suppress(this.m_game, known.player, false);
        TandemVisibility.Show(this.m_game, known.player, n"elsewhere", 0.0);
      }
      if known.place != 0 || known.hidden {
        away += 1;
      }
    }
    if away > 0 {
      TandemNative.Log(s"elsewhere: the session is over, \(away) player(s) back in the city");
    }
    ArrayClear(this.m_players);
    this.m_changes = 0u;
    // A test world is the session's (a scenario's): it ends with it.
    if this.m_test >= 0 {
      TandemNative.Log(s"elsewhere: test \(TandemElsewhere.PlaceName(this.m_test)) over with the session");
      this.m_test = -1;
    }
  }

  // ---- commands (tools/game.sh cmd elsewhere …) ----

  public func Command(command: String) -> Void {
    if StrBeginsWith(command, "elsewhere test ") {
      // As if our V were in that world (with "teleport X Y Z" for the scene
      // taking it away from its body).
      let what = StrAfterFirst(command, "test ");
      this.m_test = -1;
      if Equals(what, "cyberspace") {
        this.m_test = 1;
      } else if Equals(what, "braindance") {
        this.m_test = 2;
      } else if Equals(what, "johnny") {
        this.m_test = 3;
      } else if Equals(what, "other") {
        this.m_test = 4;
      }
      this.m_testUntil = this.m_lastTick + 120.0;
      TandemNative.Log(s"elsewhere: test \(this.m_test >= 0 ? TandemElsewhere.PlaceName(this.m_test) : "off")");
    } else if Equals(command, "elsewhere state") {
      let ours = TandemAway.Ours();
      this.m_reports += 1;
      TandemNative.Log(s"elsewhere: state #\(this.m_reports): our V in \(TandemElsewhere.PlaceName(this.m_place)) (\(this.m_why)), settled \(this.m_settled), told place \(ours.place) flags \(ours.flags), body held \(TandemAway.Holding()), inside \(this.m_inside)");
      for known in this.m_players {
        TandemNative.Log(s"elsewhere: player \(known.player) in \(TandemElsewhere.PlaceName(known.place)), flags \(known.flags), hidden \(known.hidden)");
      }
    } else {
      TandemNative.Log(s"unknown command: \(command)");
    }
  }
}
