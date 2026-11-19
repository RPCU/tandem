// Story restrictions (docs/architecture/story.md, "Restrictions"; engine
// facts: docs/research/engine/story.md §13.45). The quest runs on the host
// only, so the gameplay restrictions its story puts on the host's V (no
// weapons, no sprint, no jump, forced walk, no vehicle, no quickhacks...)
// are the host's alone. Here:
//   - the host reports the GameplayRestriction status effects of its V that
//     a guest beside it must share (TandemStory.SendRestrictions);
//   - a guest in the host's story moment (core/story.hpp InMoment) wears the
//     same effects on its own V, by the same records, so the game holsters
//     and restricts it the game's way; it takes off only what it put on;
//   - the host refuses a guest's hits on the scene's actors while its V is
//     in that moment (a backstop).
// Names checked against the 2.50 scripts (final.reds lines in the comments).

public class TandemRestrictions {
  private let m_game: GameInstance;
  private let m_story: wref<TandemStorySystem>;
  // Host: when we last looked at our V, whether it has story restrictions on
  // (that last look) and its scene tier, and when we last said we refused a
  // guest's hit.
  private let m_lastScan: Float;
  private let m_restricted: Bool;
  private let m_tier: Int32;
  private let m_lastRefusal: Float;
  // Guest: when we last checked the moment, whether its notice was shown,
  // and the effects we took off lately (their removal is not a loss).
  private let m_lastCheck: Float;
  private let m_noticed: Bool;
  private let m_removed: array<TweakDBID>;
  private let m_removedAt: array<Float>;

  public func Start(game: GameInstance, story: ref<TandemStorySystem>) -> Void {
    this.m_game = game;
    this.m_story = story;
  }

  private func Now() -> Float {
    return EngineTime.ToFloat(GameInstance.GetSimTime(this.m_game));
  }

  // ---- which effects ----

  // A gameplay restriction: what PlayerPuppet.OnStatusEffectApplied hands
  // to PlayerGameplayRestrictions (final.reds:43931, the tag
  // GameplayRestriction; removed the same way, :44490).
  public static func IsRestriction(record: wref<StatusEffect_Record>) -> Bool {
    return IsDefined(record) && record.GameplayTagsContains(n"GameplayRestriction");
  }

  // One a guest beside the host shares: the story's rule on the place, not
  // what holds the host's own body or camera (NoMovement, NoCameraControl,
  // ForceCrouch, camera animations and breathing), its menus and fast
  // travel, nor what its own activity puts on (a body carried, a call, a
  // braindance, a time skip, focus mode, driving in combat, a locker).
  public static func IsShared(record: wref<StatusEffect_Record>) -> Bool {
    if !TandemRestrictions.IsRestriction(record) {
      return false;
    }
    if record.GameplayTagsContains(n"CameraAnimation") || record.GameplayTagsContains(n"Breathing") {
      return false;
    }
    let name = TDBID.ToStringDEBUG(record.GetID());
    let own = [
      "NoMovement", "NoCameraControl", "ForceCrouch", "BlockAllHubMenu", "BlockAllMenu",
      "BlockFastTravel", "BodyCarrying", "PhoneCall", "Braindance", "FastForward",
      "FocusModeLocomotion", "DriverCombat", "VehicleCombat", "SecurityLocker", "InfiniteAmmo"
    ];
    for part in own {
      if StrContains(name, part) {
        return false;
      }
    }
    return true;
  }

  // Weapons away: the restrictions the game answers by holstering or
  // blocking the weapon wheel (NoCombat, final.reds:246203 and 540443;
  // NoWeapons, :540826; fists or melee only, :246185).
  public static func KeepsWeaponsAway(effect: TweakDBID) -> Bool {
    let record = TweakDBInterface.GetStatusEffectRecord(effect);
    if !IsDefined(record) {
      return false;
    }
    return record.GameplayTagsContains(n"NoCombat") || record.GameplayTagsContains(n"NoWeapons")
      || record.GameplayTagsContains(n"Fists") || record.GameplayTagsContains(n"Melee")
      || StrContains(TDBID.ToStringDEBUG(effect), "NoWeapons") || StrContains(TDBID.ToStringDEBUG(effect), "NoCombat");
  }

  // ---- host ----

  // Four times a second (TandemStorySystem.HostTick): the restrictions on
  // our V that a guest beside it shares, sent when they change.
  public func HostTick(now: Float, tier: Int32) -> Void {
    if now - this.m_lastScan < 0.25 {
      return;
    }
    this.m_lastScan = now;
    this.m_tier = tier;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let effects: array<ref<StatusEffect>>;
    GameInstance.GetStatusEffectSystem(this.m_game).GetAppliedEffects(player.GetEntityID(), effects);
    let ids: array<TweakDBID>;
    let sources = "";
    for effect in effects {
      let record = effect.GetRecord();
      if TandemRestrictions.IsShared(record) && !ArrayContains(ids, record.GetID()) {
        ArrayPush(ids, record.GetID());
        // Who put it on (Codeware's StatusEffect.sourcesData: a quest's
        // node names its source): logged, for the facts to check in game.
        let from = "";
        let i = 0;
        while i < ArraySize(effect.sourcesData) {
          from += s" \(NameToString(effect.sourcesData[i].name))";
          i += 1;
        }
        sources += s" \(TDBID.ToStringDEBUG(record.GetID())) [\(from) ]";
      }
    }
    // Hands kept empty by our scene or our quest (the upper body state
    // machine in ForceEmptyHands, final.reds:540449; a safe zone's is not
    // ours to share: the guest's world has its own, :46240): for a guest
    // the game's way is NoCombat, which holsters it (:540443).
    let psm = player.GetPlayerStateMachineBlackboard();
    let emptyHands = psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.UpperBody) == 5
      && psm.GetInt(GetAllBlackboardDefs().PlayerStateMachine.Zones) != 2
      && (tier >= 2 || GameInstance.GetQuestsSystem(this.m_game).GetFact(n"ForceEmptyHands") > 0);
    if emptyHands && !ArrayContains(ids, t"GameplayRestriction.NoCombat") {
      ArrayPush(ids, t"GameplayRestriction.NoCombat");
      sources += " GameplayRestriction.NoCombat [ hands forced empty ]";
    }
    this.m_restricted = ArraySize(ids) > 0;
    if TandemStory.SendRestrictions(ids) {
      if ArraySize(ids) > 0 {
        TandemNative.Log(s"story: our restrictions for the guests (tier \(tier)):\(sources)");
      } else {
        TandemNative.Log("story: our restrictions lifted");
      }
    }
  }

  // Our V is in a restricted story moment: in a scene, or with story
  // restrictions on.
  public func HostMoment() -> Bool {
    return this.m_restricted || this.m_tier >= 2;
  }

  // The host's backstop (Outcomes.reds, Takedowns.reds): while our V is in
  // a restricted moment, a guest's hit on one of the scene's actors (scene
  // replay's cast, or an NPC in a scene that the quest marks or keeps alive)
  // is refused. A guest outside the moment fighting elsewhere is the combat
  // path's, as always.
  public func RefusesHit(body: ref<ScriptedPuppet>, npc: Uint32, player: Uint32, what: String) -> Bool {
    if !IsDefined(body) || !TandemNative.IsHost() || !this.HostMoment() {
      return false;
    }
    let actor = TandemStory.SceneCasts(npc);
    if !actor {
      let id = body.GetEntityID();
      let scenes = GameInstance.GetSceneSystem(this.m_game).GetScriptInterface();
      let gods = GameInstance.GetGodModeSystem(this.m_game);
      // GameObject.IsQuest (final.reds:13517), GodModeSystem (:87673),
      // IsEntityInScene (:193151).
      let kept = body.IsQuest() || gods.HasGodMode(id, gameGodModeType.Immortal)
        || gods.HasGodMode(id, gameGodModeType.Invulnerable);
      actor = kept && IsDefined(scenes) && scenes.IsEntityInScene(id);
    }
    if actor {
      let now = this.Now();
      if now - this.m_lastRefusal > 1.0 {
        this.m_lastRefusal = now;
        TandemNative.Log(s"story: refused a guest's hit during the host's scene (player \(player), NPC \(npc), \(what))");
      }
    }
    return actor;
  }

  // ---- guest ----

  // Four times a second (TandemStorySystem.UpdateHolds): in the host's
  // moment or not, what to put on our V and take off. `holds`: what the
  // host's scene tier asks for (weapons lowered, invulnerable).
  public func GuestTick(now: Float, holds: array<TweakDBID>) -> Void {
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
    let at = TandemNative.RemotePosition(host);
    let here = player.GetWorldPosition();
    // Another world (cyberspace, a braindance, a flashback) holds no scene
    // of this one (Elsewhere.reds).
    let away = TandemElsewhere.IsAwayFromBody(this.m_game, host) || TandemElsewhere.Inside(this.m_game);
    let before = this.Wearing(story);
    let apply = TandemStory.RestrictUpdate(story.Grouped(), away, at.W >= 0.5, at.X, at.Y, at.Z, here.X, here.Y, here.Z, story.HostTierRaw(), holds);
    let weapons = false;
    for effect in apply {
      if this.PutOn(player, effect) && TandemRestrictions.KeepsWeaponsAway(effect) {
        weapons = true;
      }
    }
    this.TakeOffRemoved(player);
    // A short notice when the moment first restricts us (once per moment).
    if !TandemStory.RestrictInMoment() && !this.Wearing(story) {
      this.m_noticed = false;
    } else if !this.m_noticed && !before && this.Wearing(story) {
      this.m_noticed = true;
      let name = story.NameOf(host);
      story.Notice(weapons ? "Story-RestrictHolstered" : "Story-RestrictSlow", name);
    }
  }

  // We wear something of the host's moment now (its set, while in it).
  private func Wearing(story: ref<TandemStorySystem>) -> Bool {
    if !TandemStory.RestrictInMoment() {
      return false;
    }
    for effect in TandemStory.RestrictHostSet() {
      if TandemStory.RestrictOurs(effect) {
        return true;
      }
    }
    return false;
  }

  // One effect on our V, by its record: never over one our own game put on
  // (that one stays theirs). True when we applied it.
  private func PutOn(player: ref<PlayerPuppet>, effect: TweakDBID) -> Bool {
    if StatusEffectSystem.ObjectHasStatusEffect(player, effect) {
      TandemStory.RestrictTheirs(effect);
      TandemNative.Log(s"story: \(TDBID.ToStringDEBUG(effect)) is already our own: left to our game");
      return false;
    }
    // One stack, no instigator (StatusEffectHelper.ApplyStatusEffect,
    // final.reds:168952): what the game's own restrictions do.
    StatusEffectHelper.ApplyStatusEffect(player, effect);
    TandemNative.Log(s"story: \(TDBID.ToStringDEBUG(effect)) on, with the host's moment");
    return true;
  }

  // What we put on and no longer want: one stack off (the game's own
  // counted removals do the same, final.reds:282700), so a stack our game
  // added meanwhile stays.
  private func TakeOffRemoved(player: ref<PlayerPuppet>) -> Void {
    let now = this.Now();
    for effect in TandemStory.RestrictRemovals() {
      if IsDefined(player) && StatusEffectSystem.ObjectHasStatusEffect(player, effect) {
        GameInstance.GetStatusEffectSystem(this.m_game).RemoveStatusEffect(player.GetEntityID(), effect, 1u);
      }
      ArrayPush(this.m_removed, effect);
      ArrayPush(this.m_removedAt, now);
      TandemNative.Log(s"story: \(TDBID.ToStringDEBUG(effect)) off");
    }
    // Our own removals are forgotten after a second.
    let i = ArraySize(this.m_removedAt) - 1;
    while i >= 0 {
      if now - this.m_removedAt[i] > 1.0 {
        ArrayErase(this.m_removed, i);
        ArrayErase(this.m_removedAt, i);
      }
      i -= 1;
    }
  }

  // The session is over, or the host is gone: everything we put on goes.
  public func Leave() -> Void {
    TandemStory.RestrictLift();
    this.TakeOffRemoved(GetPlayer(this.m_game));
    this.m_noticed = false;
  }

  // Our V got a restriction (PlayerPuppet.OnStatusEffectApplied, Story.reds'
  // wrap). One we put on, applied again by someone else: if it did not
  // stack (one stack: our own game refreshed it), it is theirs now and stays
  // when we go.
  public func OnApplied(record: wref<StatusEffect_Record>, isNew: Bool) -> Void {
    if isNew || !IsDefined(record) || !TandemStory.RestrictOurs(record.GetID()) {
      return;
    }
    let player = GetPlayer(this.m_game);
    let effect = IsDefined(player) ? StatusEffectHelper.GetStatusEffectByID(player, record.GetID()) : null;
    if IsDefined(effect) && effect.GetStackCount() <= 1u {
      TandemStory.RestrictTheirs(record.GetID());
      TandemNative.Log(s"story: \(TDBID.ToStringDEBUG(record.GetID())) put on again by our game: left to it");
    }
  }

  // Our V lost a restriction we hold (its own quest, a cleanse, the game's
  // RemoveAllGameplayRestrictions on a fast travel, final.reds:50417): put
  // on again at the next check if the moment still wants it.
  public func OnRemoved(record: wref<StatusEffect_Record>, isFinal: Bool) -> Void {
    if !isFinal || !IsDefined(record) || !TandemStory.RestrictHolds(record.GetID())
      || ArrayContains(this.m_removed, record.GetID()) {
      return;
    }
    TandemStory.RestrictGone(record.GetID());
    TandemNative.Log(s"story: \(TDBID.ToStringDEBUG(record.GetID())) went (not by us)");
  }

  // tools/game.sh cmd story restrict: what is on, ours and the host's.
  public func Report() -> Void {
    let host = "";
    for effect in TandemStory.RestrictHostSet() {
      host += s" \(TDBID.ToStringDEBUG(effect))\(TandemStory.RestrictOurs(effect) ? "*" : "")";
    }
    TandemNative.Log(s"story: restrictions: host moment \(this.HostMoment()), in the host's moment \(TandemStory.RestrictInMoment()), the host's set (* ours):\(host)");
  }
}
