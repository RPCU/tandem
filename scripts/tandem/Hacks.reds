// Quickhacks in a shared world (docs/architecture/combat.md, "Quickhacks from two
// players on one target"; docs/research/coop-perks.md §6).
//
// A guest's quickhack on a proxy runs in its own game as in solo: the
// panel, the RAM cost, its cooldown and queue, the upload bar, the effect
// on the proxy (its look, its damage ticks: hits for the host). The effects
// it lands go to the host as for a hit (NpcEffect, Combat.reds). Here:
//
// - Guest: the upload's start goes to the host (NpcHack), with the upload
//   time our game computed (our perks).
// - Host: the real NPC is being hacked for that long, as the game does it
//   for an upload (AIQuickHackStatusEffect.BeingHacked, and
//   BeingHackedWithTraceableHack for a traceable one), its uploads in the
//   order they arrive; a non-stealth quickhack effect a guest lands alerts
//   it, turns it on that guest's puppet and traces that guest (the game
//   only does this for the host's V: NPCPuppet.OnQuickHackEffectApplied
//   checks the instigator against the local player).
// - Host: every upload on its NPCs (ours and the guests') and every
//   quickhack effect on them goes to everyone (NpcHackState, NpcStatus);
//   guests show them on their proxies, so their quickhack menu's "already
//   affected" lockouts and the proxy's look are the host's.
// - NPC netrunners: a host NPC hacking a guest's puppet is that guest's
//   business (PlayerHack): its game runs the hack on its own V with its
//   proxy as the hacker (its ICE, the upload bar, the effect; an ICE
//   counterhack lands on the proxy and goes to the host as an effect).
// - Cars: a quickhack on the copy of a car another V drives is a request to
//   the driver's game (VehicleHack), which applies it to the car it drives.
//
// Spreading (Contagion, Short Circuit, Synapse Burnout...): the vanilla
// spread is keyed to the local player (SpreadInitEffector and SpreadEffector
// take GetPlayer for the spread count, range and executor), so it runs on
// the caster's game only, over its proxies; each copy it lands is an effect
// on a proxy, forwarded like any other. The host never runs a guest's
// completion effects (no SpreadInitEffector there), so its spread map has no
// entry for them and a forwarded effect does not spread again.

public class TandemUpload {
  public let npc: Uint32;
  public let player: Uint32;
  public let action: TweakDBID;
  public let endsAt: Float;
  public let traceable: Bool;
}

// Guest: an effect put on a proxy because the host's NPC has it.
public class TandemMirroredEffect {
  public let npc: Uint32;
  public let effect: TweakDBID;
}

public class TandemHacks extends ScriptableSystem {
  // Host: guests' uploads on our NPCs, in arrival order. Guest: the other
  // players' uploads on host NPCs, shown on our proxies.
  private let m_uploads: array<ref<TandemUpload>>;
  private let m_mirrored: array<ref<TandemMirroredEffect>>;
  // Host: when each NPC last reacted to a guest's quickhack (one alert and
  // trace per burst of effects).
  private let m_alertedNpcs: array<Uint32>;
  private let m_alertedAt: array<Float>;
  private let m_now: Float;
  private let m_you: Uint32;

  public static func Get(game: GameInstance) -> ref<TandemHacks> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemHacks") as TandemHacks;
  }

  public static func Session(game: GameInstance) -> ref<TandemSystem> {
    return GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
  }

  // Every frame, from TandemSystem.Tick (you: our player id, 0 when not in
  // a session or not playing yet).
  public static func Tick(game: GameInstance, now: Float, you: Uint32) -> Void {
    let hacks = TandemHacks.Get(game);
    if IsDefined(hacks) {
      hacks.Update(game, now, you);
    }
  }

  public func Update(game: GameInstance, now: Float, you: Uint32) -> Void {
    this.m_now = now;
    let events = TandemNative.TakeHackEvents();
    if you == 0u {
      if this.m_you != 0u {
        this.Reset();
      }
      this.m_you = 0u;
      return;
    }
    this.m_you = you;
    for event in events {
      if event.kind == 1 {
        this.StartRemoteUpload(game, event);
      } else if event.kind == 2 {
        this.ShowUpload(game, event);
      } else if event.kind == 3 {
        this.MirrorEffect(game, event);
      } else if event.kind == 4 {
        this.HackedByNpc(game, event);
      } else if event.kind == 5 {
        this.CarHacked(game, event);
      }
    }
    this.EndUploads(game);
  }

  public func You() -> Uint32 {
    return this.m_you;
  }

  public static func StepName(step: Int32) -> String {
    if step == 0 {
      return "runs";
    }
    return step == 1 ? "finished" : "failed";
  }

  public static func CarHackName(hack: Int32) -> String {
    if hack == 0 {
      return "brakes";
    }
    return hack == 1 ? "explode" : "accelerate";
  }

  private func Reset() -> Void {
    ArrayClear(this.m_uploads);
    ArrayClear(this.m_mirrored);
    ArrayClear(this.m_alertedNpcs);
    ArrayClear(this.m_alertedAt);
  }

  // ---- who is who ----

  public static func NpcOf(game: GameInstance, id: EntityID) -> Uint32 {
    let system = TandemHacks.Session(game);
    return IsDefined(system) ? system.NpcOf(id) : 0u;
  }

  public static func NpcEntity(game: GameInstance, npc: Uint32) -> ref<ScriptedPuppet> {
    let system = TandemHacks.Session(game);
    if !IsDefined(system) {
      return null;
    }
    let id = system.NpcEntity(npc);
    return EntityID.IsDefined(id) ? GameInstance.FindEntityByID(game, id) as ScriptedPuppet : null;
  }

  // The player a V entity in our world is: our V (us), a puppet (them), 0.
  public static func PlayerOf(game: GameInstance, id: EntityID) -> Uint32 {
    let system = TandemHacks.Session(game);
    if !IsDefined(system) || !EntityID.IsDefined(id) {
      return 0u;
    }
    let player = GetPlayer(game);
    if IsDefined(player) && player.GetEntityID() == id {
      return system.You();
    }
    return system.PlayerOfPuppet(id);
  }

  // ---- uploads ----

  // Every upload to an NPC (ScriptedPuppet.OnUploadProgressStateChanged,
  // below): our V's on a proxy goes to the host; our V's on one of our NPCs
  // (host) goes to everyone.
  public static func UploadEvent(target: ref<ScriptedPuppet>, evt: ref<UploadProgramProgressEvent>) -> Void {
    if !Equals(evt.progressBarContext, EProgressBarContext.QuickHack) || !Equals(evt.progressBarType, EProgressBarType.UPLOAD) {
      return;
    }
    let action = evt.action;
    if !IsDefined(action) || !IsDefined(target) {
      return;
    }
    let game = target.GetGame();
    let player = GetPlayer(game);
    let executor = action.GetExecutor();
    if !IsDefined(player) || !IsDefined(executor) || executor.GetEntityID() != player.GetEntityID() {
      return;
    }
    let hacks = TandemHacks.Get(game);
    let npc = TandemHacks.NpcOf(game, target.GetEntityID());
    if npc == 0u || !IsDefined(hacks) || hacks.You() == 0u {
      return;
    }
    let started = Equals(evt.state, EUploadProgramState.STARTED);
    if !started && !Equals(evt.state, EUploadProgramState.COMPLETED) {
      return;
    }
    if TandemOwn.Npc(game, npc) {
      TandemNative.SendNpcHackState(npc, hacks.You(), action.GetObjectActionID(), started ? 0 : 1, started ? evt.duration : 0.0);
      return;
    }
    if started {
      let traceable = QuickHackableHelper.WillHackRevealPlayer(player, target.GetRecordID(), action);
      TandemNative.SendNpcHack(npc, action.GetObjectActionID(), evt.duration, traceable ? 1 : 0);
      TandemNative.Log(s"hacks: our upload of \(TDBID.ToStringDEBUG(action.GetObjectActionID())) on proxy \(npc) sent to the host (\(evt.duration) s\(traceable ? ", traceable" : ""))");
    }
  }

  // Host: a guest is uploading on our NPC. The NPC is being hacked for as
  // long as the guest's upload takes, as for one of ours; the effect comes
  // when the guest's upload completes (NpcEffect).
  private func StartRemoteUpload(game: GameInstance, event: TandemHackEvent) -> Void {
    let npc = TandemHacks.NpcEntity(game, event.npc);
    let puppet = TandemNpcs.PuppetOf(game, event.player);
    if !IsDefined(npc) || npc.IsDead() || !IsDefined(puppet) {
      TandemNative.SendNpcHackState(event.npc, event.player, event.action, 2, 0.0);
      TandemNative.Log(s"hacks: player \(event.player)'s upload of \(TDBID.ToStringDEBUG(event.action)) on npc \(event.npc) failed (no NPC or no puppet)");
      return;
    }
    // An actor of our V's scene, while it lasts: the upload fails, as one
    // the NPC is out of reach for (Restrictions.reds, the host's backstop).
    if TandemHacks.SceneRefuses(game, npc, event.npc, event.player, event.action) {
      TandemNative.SendNpcHackState(event.npc, event.player, event.action, 2, 0.0);
      return;
    }
    let ahead = 0;
    for running in this.m_uploads {
      if running.npc == event.npc {
        ahead += 1;
      }
    }
    if npc.HasActiveQuickHackUpload() {
      ahead += 1; // our own V's
    }
    let upload = new TandemUpload();
    upload.npc = event.npc;
    upload.player = event.player;
    upload.action = event.action;
    upload.endsAt = this.m_now + MaxF(event.seconds, 0.0);
    upload.traceable = (event.flags & 1) != 0;
    ArrayPush(this.m_uploads, upload);
    TandemHacks.HoldBeingHacked(npc, upload, puppet.GetEntityID());
    TandemNative.SendNpcHackState(event.npc, event.player, event.action, 0, event.seconds);
    TandemCommsNative.PartnerUpload(event.npc, event.player, event.action, 0, event.seconds);
    TandemNative.Log(s"hacks: player \(event.player) uploads \(TDBID.ToStringDEBUG(event.action)) on npc \(event.npc) (\(event.seconds) s, \(ahead) other upload(s) on it)");
  }

  // Host: a guest's quickhack on an actor of our V's scene, during it (its
  // upload here; the effects it lands in Npcs.reds).
  public static func SceneRefuses(game: GameInstance, npc: ref<ScriptedPuppet>, id: Uint32, player: Uint32, action: TweakDBID) -> Bool {
    let story = TandemStorySystem.Get(game);
    return IsDefined(story) && IsDefined(story.Restrictions())
      && story.Restrictions().RefusesHack(npc, id, player, TDBID.ToStringDEBUG(action));
  }

  // A status effect from someone (or no one: an undefined id).
  public static func Put(target: ref<ScriptedPuppet>, effect: TweakDBID, from: EntityID) -> Void {
    if EntityID.IsDefined(from) {
      StatusEffectHelper.ApplyStatusEffect(target, effect, from);
    } else {
      StatusEffectHelper.ApplyStatusEffect(target, effect);
    }
  }

  // What OnUploadProgressStateChanged applies on STARTED. Applied again if
  // something removed it while an upload still runs (our V's upload ending
  // removes it whatever else is uploading).
  public static func HoldBeingHacked(npc: ref<ScriptedPuppet>, upload: ref<TandemUpload>, from: EntityID) -> Void {
    if !StatusEffectSystem.ObjectHasStatusEffect(npc, t"AIQuickHackStatusEffect.BeingHacked") {
      TandemHacks.Put(npc, t"AIQuickHackStatusEffect.BeingHacked", from);
    }
    if upload.traceable && !StatusEffectSystem.ObjectHasStatusEffect(npc, t"AIQuickHackStatusEffect.BeingHackedWithTraceableHack") {
      TandemHacks.Put(npc, t"AIQuickHackStatusEffect.BeingHackedWithTraceableHack", from);
    }
  }

  // Uploads whose time is up, in the order they arrived (host: the real
  // NPC's; guest: the others' shown on our proxies).
  private func EndUploads(game: GameInstance) -> Void {
    let kept: array<ref<TandemUpload>>;
    let ended: array<ref<TandemUpload>>;
    for upload in this.m_uploads {
      if this.m_now >= upload.endsAt {
        ArrayPush(ended, upload);
      } else {
        ArrayPush(kept, upload);
      }
    }
    this.m_uploads = kept;
    for upload in ended {
      let npc = TandemHacks.NpcEntity(game, upload.npc);
      if TandemOwn.Npc(game, upload.npc) {
        let alive = IsDefined(npc) && !npc.IsDead();
        TandemNative.SendNpcHackState(upload.npc, upload.player, upload.action, alive ? 1 : 2, 0.0);
        TandemCommsNative.PartnerUpload(upload.npc, upload.player, upload.action, alive ? 1 : 2, 0.0);
        TandemNative.Log(s"hacks: player \(upload.player)'s upload of \(TDBID.ToStringDEBUG(upload.action)) on npc \(upload.npc) \(alive ? "done" : "failed")");
      }
      if IsDefined(npc) && !this.Uploading(upload.npc) && !npc.HasActiveQuickHackUpload() {
        StatusEffectHelper.RemoveStatusEffect(npc, t"AIQuickHackStatusEffect.BeingHacked");
        StatusEffectHelper.RemoveStatusEffect(npc, t"AIQuickHackStatusEffect.BeingHackedWithTraceableHack");
      }
    }
    // Still running: still being hacked.
    for upload in this.m_uploads {
      let npc = TandemHacks.NpcEntity(game, upload.npc);
      let puppet = TandemNpcs.PuppetOf(game, upload.player);
      if IsDefined(npc) && !npc.IsDead() {
        TandemHacks.HoldBeingHacked(npc, upload, IsDefined(puppet) ? puppet.GetEntityID() : EntityID());
      }
    }
  }

  private func Uploading(npc: Uint32) -> Bool {
    for upload in this.m_uploads {
      if upload.npc == npc {
        return true;
      }
    }
    return false;
  }

  // Guest: the host's word on an upload to one of its NPCs. Ours: the host
  // runs it. Another player's: shown on our proxy until it ends.
  private func ShowUpload(game: GameInstance, event: TandemHackEvent) -> Void {
    let what = TDBID.ToStringDEBUG(event.action);
    if event.player == this.m_you {
      TandemNative.Log(s"hacks: the host \(TandemHacks.StepName(event.state)) our upload of \(what) on npc \(event.npc)");
      return;
    }
    // A partner's: its indicator, tinted, while we are linked (Comms.reds).
    TandemCommsNative.PartnerUpload(event.npc, event.player, event.action, event.state, event.seconds);
    if event.state == 0 {
      let upload = new TandemUpload();
      upload.npc = event.npc;
      upload.player = event.player;
      upload.action = event.action;
      upload.endsAt = this.m_now + MaxF(event.seconds, 0.0) + 1.0; // the host's end, or this
      ArrayPush(this.m_uploads, upload);
      let proxy = TandemHacks.NpcEntity(game, event.npc);
      let puppet = TandemNpcs.PuppetOf(game, event.player);
      if IsDefined(proxy) {
        TandemHacks.HoldBeingHacked(proxy, upload, IsDefined(puppet) ? puppet.GetEntityID() : EntityID());
      }
      TandemNative.Log(s"hacks: player \(event.player) uploads \(what) on npc \(event.npc) (\(event.seconds) s, shown on its proxy)");
    } else {
      // Ended on the host: ends here at once.
      for upload in this.m_uploads {
        if upload.npc == event.npc && upload.player == event.player && upload.action == event.action {
          upload.endsAt = this.m_now;
        }
      }
      TandemNative.Log(s"hacks: player \(event.player)'s upload of \(what) on npc \(event.npc) \(event.state == 1 ? "done" : "failed")");
    }
  }

  // ---- quickhack effects on the host's NPCs ----

  // Host: a status effect came onto one of our NPCs (NPCPuppet
  // .OnStatusEffectApplied, below).
  public static func EffectApplied(npc: ref<NPCPuppet>, evt: ref<ApplyStatusEffectEvent>) -> Void {
    if !TandemOwn.Entity(npc.GetGame(), npc.GetEntityID()) || !evt.isNewApplication || evt.isAppliedOnSpawn || !IsDefined(evt.staticData) {
      return;
    }
    let effect = evt.staticData.GetID();
    if effect == t"AIQuickHackStatusEffect.BeingHacked" || effect == t"AIQuickHackStatusEffect.BeingHackedWithTraceableHack" {
      return; // uploads travel as NpcHackState
    }
    let tags = evt.staticData.GameplayTags();
    // A breach's daemons too (Breach.reds): an owner's NPC weakened by
    // another player's breach looks so on every proxy. And what stops a
    // netrunner's upload (Interrupts): its proxy wears it where the upload
    // runs on another V's game.
    let interrupt = TandemHacks.Interrupts(evt.staticData);
    if !ArrayContains(tags, n"Quickhack") && !TandemBreach.IsDaemonEffect(effect) && !interrupt {
      return;
    }
    let game = npc.GetGame();
    let hacks = TandemHacks.Get(game);
    let id = TandemHacks.NpcOf(game, npc.GetEntityID());
    if id == 0u || !IsDefined(hacks) || hacks.You() == 0u {
      return;
    }
    let who = TandemHacks.PlayerOf(game, evt.instigatorEntityID);
    TandemNative.SendNpcStatus(id, who, effect, true);
    TandemCommsNative.PartnerEffect(id, who, effect, true, TandemComms.EffectSeconds(npc, effect));
    // A guest's loud quickhack: what the game does for ours.
    if who != 0u && who != hacks.You() && ArrayContains(tags, n"Quickhack") && !ArrayContains(tags, n"Stealth") {
      hacks.Victim(npc, id, who, effect);
    }
  }

  // What stops an NPC netrunner's upload on a V: vanilla's listener reads it
  // on the netrunner each tick (UploadFromNPCToPlayerListener
  // .OnStatPoolValueChanged, final.reds:195098), applied by the NPC's
  // behaviour and senses, both off on a proxy.
  public static func Interrupts(record: ref<StatusEffect_Record>) -> Bool {
    if !IsDefined(record) {
      return false;
    }
    let id = record.GetID();
    return id == t"AIQuickHackStatusEffect.HackingInterrupted" || id == t"AIQuickHackStatusEffect.HackRevealInterrupted" || record.GameplayTagsContains(n"CyberwareMalfunction");
  }

  public static func EffectRemoved(npc: ref<NPCPuppet>, evt: ref<RemoveStatusEffect>) -> Void {
    if !TandemOwn.Entity(npc.GetGame(), npc.GetEntityID()) || !evt.isFinalRemoval || !IsDefined(evt.staticData) {
      return;
    }
    let tags = evt.staticData.GameplayTags();
    if !ArrayContains(tags, n"Quickhack") && !TandemBreach.IsDaemonEffect(evt.staticData.GetID()) && !TandemHacks.Interrupts(evt.staticData) {
      return;
    }
    let id = TandemHacks.NpcOf(npc.GetGame(), npc.GetEntityID());
    if id != 0u {
      TandemNative.SendNpcStatus(id, 0u, evt.staticData.GetID(), false);
      TandemCommsNative.PartnerEffect(id, 0u, evt.staticData.GetID(), false, 0.0);
    }
  }

  // Host: our NPC was hit by a guest's non-stealth quickhack. Vanilla, for
  // our V (NonStealthQuickHackVictimEvent): trace the hacker
  // (RevealPlayerPositionIfNeeded) or else alert, and turn hostile to them.
  // The guest's game decides the trace (its V's combat and reveal state):
  // here the NPC is alerted either way.
  public func Victim(npc: ref<NPCPuppet>, id: Uint32, player: Uint32, effect: TweakDBID) -> Void {
    let i = 0;
    while i < ArraySize(this.m_alertedNpcs) {
      if this.m_alertedNpcs[i] == id && this.m_now - this.m_alertedAt[i] < 2.0 {
        return;
      }
      i += 1;
    }
    let puppet = TandemNpcs.PuppetOf(npc.GetGame(), player);
    if !IsDefined(puppet) || npc.IsDead() {
      return;
    }
    let found = false;
    i = 0;
    while i < ArraySize(this.m_alertedNpcs) {
      if this.m_alertedNpcs[i] == id {
        this.m_alertedAt[i] = this.m_now;
        found = true;
      }
      i += 1;
    }
    if !found {
      ArrayPush(this.m_alertedNpcs, id);
      ArrayPush(this.m_alertedAt, this.m_now);
    }
    NPCStatesComponent.AlertPuppet(npc);
    AIActionHelper.TryChangingAttitudeToHostile(npc, puppet);
    TandemNative.SendPlayerHack(player, id, t"AIQuickHack.HackRevealPosition", 1);
    TandemNative.Log(s"hacks: npc \(id) noticed player \(player)'s quickhack (\(TDBID.ToStringDEBUG(effect))): alerted, hostile, tracing them");
  }

  // Guest: an effect on the host's NPC from another V's hack, or one that
  // left it: on our proxy as well.
  private func MirrorEffect(game: GameInstance, event: TandemHackEvent) -> Void {
    let proxy = TandemHacks.NpcEntity(game, event.npc);
    let what = TDBID.ToStringDEBUG(event.action);
    // A partner's effect: its indicator, tinted, while linked (Comms.reds;
    // the remaining time read once the proxy has it).
    let applied = event.state == 1;
    if applied && IsDefined(proxy) && !proxy.IsDead() && !StatusEffectSystem.ObjectHasStatusEffect(proxy, event.action) {
      let puppet = TandemNpcs.PuppetOf(game, event.player);
      TandemHacks.Put(proxy, event.action, IsDefined(puppet) ? puppet.GetEntityID() : EntityID());
      let mirrored = new TandemMirroredEffect();
      mirrored.npc = event.npc;
      mirrored.effect = event.action;
      ArrayPush(this.m_mirrored, mirrored);
      TandemNative.Log(s"hacks: npc \(event.npc) has \(what) from player \(event.player): on its proxy");
    }
    TandemCommsNative.PartnerEffect(event.npc, event.player, event.action, applied, applied ? TandemComms.EffectSeconds(proxy, event.action) : 0.0);
    if event.state == 1 {
      if !IsDefined(proxy) || proxy.IsDead() {
        return;
      }
      if !StatusEffectSystem.ObjectHasStatusEffect(proxy, event.action) {
        let puppet = TandemNpcs.PuppetOf(game, event.player);
        TandemHacks.Put(proxy, event.action, IsDefined(puppet) ? puppet.GetEntityID() : EntityID());
        let mirrored = new TandemMirroredEffect();
        mirrored.npc = event.npc;
        mirrored.effect = event.action;
        ArrayPush(this.m_mirrored, mirrored);
        TandemNative.Log(s"hacks: npc \(event.npc) has \(what) from player \(event.player): on its proxy");
      }
      return;
    }
    let kept: array<ref<TandemMirroredEffect>>;
    for mirrored in this.m_mirrored {
      if mirrored.npc == event.npc && mirrored.effect == event.action {
        if IsDefined(proxy) {
          StatusEffectHelper.RemoveStatusEffect(proxy, event.action);
        }
        TandemNative.Log(s"hacks: npc \(event.npc) lost \(what): off its proxy");
      } else {
        ArrayPush(kept, mirrored);
      }
    }
    this.m_mirrored = kept;
  }

  // ---- NPC netrunners against guests ----

  // Host: an NPC's hack about to land on a remote player's puppet (the
  // NPC-to-NPC path, since a puppet is no PlayerPuppet). It is that
  // player's: their game runs it on their V. True: handled, not to land here.
  public static func ForwardToGuest(target: ref<ScriptedPuppet>, evt: ref<HackTargetEvent>) -> Bool {
    if !IsDefined(target) {
      return false;
    }
    let game = target.GetGame();
    let system = TandemHacks.Session(game);
    let player = IsDefined(system) ? system.PlayerOfPuppet(target.GetEntityID()) : 0u;
    if player == 0u {
      return false;
    }
    let npc = TandemHacks.NpcOf(game, evt.netrunnerID);
    if npc == 0u || !IsDefined(evt.objectRecord) {
      TandemNative.Log(s"hacks: a hack on player \(player)'s puppet dropped (netrunner not shared)");
      return true;
    }
    let flags = 0;
    if evt.settings.isRevealPositionAction {
      flags += 1;
    }
    if evt.settings.showDirectionalIndicator {
      flags += 2;
    }
    if evt.settings.skipBeingHackedSetUp {
      flags += 4;
    }
    TandemNative.SendPlayerHack(player, npc, evt.objectRecord.GetID(), flags);
    TandemNative.Log(s"hacks: npc \(npc) hacks player \(player) (\(TDBID.ToStringDEBUG(evt.objectRecord.GetID()))): sent to their game");
    return true;
  }

  // Guest: the host's NPC hacks our V: the same hack, from our proxy of it,
  // through the game's own path (ScriptedPuppet.OnHackTargetEvent: the
  // upload bar, ICE, the effect on our V).
  private func HackedByNpc(game: GameInstance, event: TandemHackEvent) -> Void {
    let v = GetPlayer(game);
    let proxy = TandemHacks.NpcEntity(game, event.npc);
    let record = TweakDBInterface.GetObjectActionRecord(event.action);
    let what = TDBID.ToStringDEBUG(event.action);
    if !IsDefined(v) || !IsDefined(proxy) || proxy.IsDead() || !IsDefined(record) {
      TandemNative.Log(s"hacks: npc \(event.npc)'s hack \(what) on our V dropped (no V, proxy or record)");
      return;
    }
    let reveal = (event.flags & 1) != 0;
    // RevealPlayerPositionIfNeeded's own conditions, on the V they are about.
    if reveal && (v.IsInCombat() || v.IsBeingRevealed()) {
      TandemNative.Log(s"hacks: npc \(event.npc) would trace our V: in combat or already traced");
      return;
    }
    let evt = new HackTargetEvent();
    evt.targetID = v.GetEntityID();
    evt.netrunnerID = proxy.GetEntityID();
    evt.objectRecord = record;
    evt.settings.isRevealPositionAction = reveal;
    evt.settings.showDirectionalIndicator = (event.flags & 2) != 0;
    evt.settings.skipBeingHackedSetUp = (event.flags & 4) != 0;
    if reveal {
      evt.settings.HUDData.bottomText = "LocKey#92249";
      evt.settings.HUDData.failedText = "LocKey#92985";
      evt.settings.HUDData.completedText = "LocKey#92985";
      evt.settings.HUDData.type = SimpleMessageType.Reveal;
    }
    v.QueueEvent(evt);
    TandemNative.Log(s"hacks: npc \(event.npc) hacks our V with \(what)\(reveal ? " (trace)" : "")");
  }

  // The V's game: the NPC a trace on our V comes from, when it is a proxy
  // (0: none, or one we run). Its trace's end is then its owner's to play
  // (Traced; the wrap of UploadFromNPCToPlayerListener below).
  public static func TracingProxy(netrunner: wref<ScriptedPuppet>, trace: Bool) -> Uint32 {
    if !trace || !IsDefined(netrunner) {
      return 0u;
    }
    return TandemCombat.ProxyNpc(netrunner.GetGame(), netrunner);
  }

  // The netrunner's owner: its trace on player's V ran to its end on that
  // V's game. What the game does then (UploadFromNPCToPlayerListener
  // .OnStatPoolMaxValueReached, final.reds:194925), here where the
  // netrunner's squad is: it and each squadmate turn hostile to the V's
  // puppet and know where it is, and the security system hears of combat.
  public static func Traced(game: GameInstance, npc: Uint32, player: Uint32, npcs: ref<TandemNpcs>) -> Void {
    let netrunner = npcs.Owns(npc) ? GameInstance.FindEntityByID(game, npcs.EntityOfNpc(npc)) as ScriptedPuppet : null;
    let puppet = TandemNpcs.PuppetOf(game, player) as ScriptedPuppet;
    if !IsDefined(netrunner) || !IsDefined(puppet) || netrunner.IsDead() {
      TandemNative.Log(s"hacks: npc \(npc)'s trace of player \(player) ended, dropped (no netrunner or puppet here)");
      return;
    }
    let squad: array<EntityID>;
    AISquadHelper.GetSquadmatesID(netrunner, squad);
    let tracker: wref<TargetTrackingExtension>;
    let first = ArraySize(squad) > 0 ? GameInstance.FindEntityByID(game, squad[0]) as ScriptedPuppet : null;
    if !TargetTrackingExtension.Get(first, tracker) && !TargetTrackingExtension.Get(netrunner, tracker) {
      TandemNative.Log(s"hacks: npc \(npc)'s trace of player \(player) ended, dropped (no target tracker)");
      return;
    }
    TandemHacks.StartCombat(puppet, netrunner, tracker);
    for id in squad {
      TandemHacks.StartCombat(puppet, GameInstance.FindEntityByID(game, id) as ScriptedPuppet, tracker);
    }
    TandemNative.Log(s"hacks: npc \(npc)'s trace of player \(player) ran to its end: it and \(ArraySize(squad)) squadmate(s) are on them");
  }

  // The listener's TryStartCombat, final.reds:194913.
  private static func StartCombat(target: ref<ScriptedPuppet>, npc: ref<ScriptedPuppet>, tracker: wref<TargetTrackingExtension>) -> Void {
    if IsDefined(npc) && !npc.IsDead() && AIActionHelper.TryChangingAttitudeToHostile(npc, target) {
      tracker.AddThreat(target, true, target.GetWorldPosition(), 1.0, -1.0, false);
      npc.TriggerSecuritySystemNotification(target.GetWorldPosition(), target, ESecurityNotificationType.COMBAT);
    }
  }

  // ---- cars ----

  // The player whose car this is a copy of (0: a car of our world).
  public static func RemoteDriver(game: GameInstance, car: ref<VehicleObject>) -> Uint32 {
    let system = TandemHacks.Session(game);
    return IsDefined(system) && IsDefined(car) && system.You() != 0u ? system.PlayerOfCar(car.GetEntityID()) : 0u;
  }

  // A quickhack on the copy of a remote player's car: theirs to apply
  // (hack: 0 brakes, 1 explode, 2 accelerate). True: sent, not to apply here.
  public static func HackRemoteCar(game: GameInstance, car: ref<VehicleObject>, hack: Int32, seconds: Float) -> Bool {
    let driver = TandemHacks.RemoteDriver(game, car);
    if driver == 0u {
      return false;
    }
    TandemNative.SendVehicleHack(driver, hack, seconds);
    TandemNative.Log(s"hacks: our car hack \(hack) on player \(driver)'s car sent to them (\(seconds) s)");
    return true;
  }

  // The driver: someone hacked the car our V drives.
  private func CarHacked(game: GameInstance, event: TandemHackEvent) -> Void {
    let v = GetPlayer(game);
    let car: wref<VehicleObject>;
    if !IsDefined(v) || !VehicleComponent.GetVehicle(game, v, car) || !IsDefined(car) || !VehicleComponent.IsDriver(game, v) {
      TandemNative.Log(s"hacks: player \(event.player)'s car hack \(event.state) dropped (we are not driving)");
      return;
    }
    if event.state == 0 {
      car.ForceBrakesUntilStoppedOrFor(MaxF(event.seconds, 1.0));
    } else if event.state == 1 {
      car.ActivateNetrunnerQuickhack(VehicleNetrunnerQuickhackType.Explode);
    } else if event.state == 2 {
      car.ActivateNetrunnerQuickhack(VehicleNetrunnerQuickhackType.Accelerate);
    } else {
      return;
    }
    TandemNative.Log(s"hacks: player \(event.player) hacked the car we drive: \(TandemHacks.CarHackName(event.state)) (\(event.seconds) s)");
  }
}

// (test) tools/game.sh cmd "hack npc [RECORD]", "hack car" (System.reds).
public abstract class TandemHacksTest {
  public static func Run(game: GameInstance, command: String, proxy: ref<ScriptedPuppet>, car: ref<VehicleObject>) -> Void {
    if StrBeginsWith(command, "hack npc") {
      let record = StrLen(command) > 9 ? StrAfterFirst(StrAfterFirst(command, " "), " ") : "QuickHack.OverheatLvl1Hack";
      if !IsDefined(proxy) {
        TandemNative.Log("hack npc: no proxy");
        return;
      }
      // The game's own way to run a quickhack from our V on an NPC
      // (ScriptedPuppet.OnExecutePuppetActionEvent): cost, upload, effect.
      let run = new ExecutePuppetActionEvent();
      run.actionID = TDBID.Create(record);
      proxy.QueueEvent(run);
      TandemNative.Log(s"hack npc: \(record) on \(EntityID.ToDebugStringDecimal(proxy.GetEntityID()))");
    } else if Equals(command, "hack car") {
      if !IsDefined(car) || !TandemHacks.HackRemoteCar(game, car, 0, 3.0) {
        TandemNative.Log("hack car: no remote car");
      }
    } else {
      TandemNative.Log(s"hack: unknown command \(command)");
    }
  }
}

// Every upload to an NPC, as the game reports it to the target.
@wrapMethod(ScriptedPuppet)
protected cb func OnUploadProgressStateChanged(evt: ref<UploadProgramProgressEvent>) -> Bool {
  let result = wrappedMethod(evt);
  TandemHacks.UploadEvent(this, evt);
  return result;
}

// Host: an NPC netrunner's hack on a remote player's puppet goes to them.
@wrapMethod(ScriptedPuppet)
protected cb func OnHackTargetEvent(evt: ref<HackTargetEvent>) -> Bool {
  if TandemHacks.ForwardToGuest(this, evt) {
    return true;
  }
  return wrappedMethod(evt);
}

// A trace on our V from a proxy (HackedByNpc) ran to its end: the game's own
// end of it, but the squad part (each squadmate turned on our V, final.reds
// 194958-194978) is the netrunner's owner's (TandemHacks.Traced): the proxy
// has no squad here, and that part reads its first squadmate whatever.
@wrapMethod(UploadFromNPCToPlayerListener)
protected cb func OnStatPoolMaxValueReached(value: Float) -> Bool {
  let npc = TandemHacks.TracingProxy(this.m_npcPuppet, this.m_ssAction);
  if npc == 0u {
    return wrappedMethod(value);
  }
  this.m_ssAction = false;
  let result = wrappedMethod(value);
  this.m_ssAction = true;
  this.m_playerPuppet.RemoveLinkedStatusEffects(true);
  TandemFight.SendTraced(npc);
  TandemNative.Log(s"hacks: npc \(npc)'s trace of our V ran to its end: its owner sets its squad on us");
  return result;
}

// Host: quickhack effects on our NPCs, for everyone (and a guest's alert).
@wrapMethod(NPCPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
  TandemNative.HookRan(n"Hacks.NPCPuppet.OnStatusEffectApplied");
  let result = wrappedMethod(evt);
  TandemHacks.EffectApplied(this, evt);
  return result;
}

@wrapMethod(NPCPuppet)
protected cb func OnStatusEffectRemoved(evt: ref<RemoveStatusEffect>) -> Bool {
  let result = wrappedMethod(evt);
  TandemHacks.EffectRemoved(this, evt);
  return result;
}

// Vehicle quickhacks on the copy of a car another V drives: requests to the
// driver's game (the copy only follows the driver's car).
@wrapMethod(VehicleComponentPS)
protected func OnVehicleOverrideForceBrakes(evt: ref<VehicleOverrideForceBrakes>) -> EntityNotificationType {
  if TandemHacks.HackRemoteCar(this.GetGameInstance(), this.GetOwnerEntity(), 0, TweakDBInterface.GetFloat(t"player.vehicleQuickHacks.forceBrakesDuration", 0.0)) {
    return EntityNotificationType.DoNotNotifyEntity;
  }
  return wrappedMethod(evt);
}

@wrapMethod(VehicleComponentPS)
protected func OnVehicleOverrideExplode(evt: ref<VehicleOverrideExplode>) -> EntityNotificationType {
  if TandemHacks.HackRemoteCar(this.GetGameInstance(), this.GetOwnerEntity(), 1, 0.0) {
    return EntityNotificationType.DoNotNotifyEntity;
  }
  return wrappedMethod(evt);
}

@wrapMethod(VehicleComponentPS)
protected func OnVehicleOverrideAccelerate(evt: ref<VehicleOverrideAccelerate>) -> EntityNotificationType {
  if TandemHacks.HackRemoteCar(this.GetGameInstance(), this.GetOwnerEntity(), 2, TweakDBInterface.GetFloat(t"player.vehicleQuickHacks.accelerateDuration", 0.0)) {
    return EntityNotificationType.DoNotNotifyEntity;
  }
  return wrappedMethod(evt);
}

// Taking control of a car another V drives needs the car handed over
// (ownership moves to the hacker while controlled): not done yet, so the
// hack does nothing on a copy rather than fight the driver's sync.
@wrapMethod(VehicleComponentPS)
protected func OnToggleTakeOverControl(evt: ref<ToggleTakeOverControl>) -> EntityNotificationType {
  let driver = TandemHacks.RemoteDriver(this.GetGameInstance(), this.GetOwnerEntity());
  if driver != 0u {
    TandemNative.Log(s"hacks: taking control of player \(driver)'s car is not supported yet");
    return EntityNotificationType.DoNotNotifyEntity;
  }
  return wrappedMethod(evt);
}
