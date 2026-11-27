// NPC voices (docs/architecture/npcs.md, "Voices"; proto::NpcVoice).
//
// An NPC's barks (a callout, a warning, a pain or death cry, a civilian's
// fear, a curious grunt): a voice context, asked of the NPC's own voice,
// which picks a line of its voice set in the player's language. Whoever
// starts one (the game's scripts through GameObject.PlayVoiceOver or a
// SoundPlayVo of their own, the delay system, the engine's own voice paths),
// it reaches the NPC as one event, SoundPlayVo (entGameplayVOEvent, queued
// on the entity: research §13.159). A proxy has no brain to start them: the
// owner says which context its NPC asked for, and every proxy asks its own
// voice for the same one. One voice per NPC: on a proxy, a line our own
// game's scripts start (its local hit, a reaction) is left unsaid; its
// owner's word says it.

public class TandemNpcVoices {
  private let m_game: GameInstance;
  private let m_sent: Int32;
  private let m_said: Int32;
  private let m_unshown: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func Tick(system: ref<TandemSystem>) -> Void {
    // Ours: the lines our shared NPCs started (queued by the wrap below).
    let ours = TandemPresence.TakeLocalNpcVoices();
    for line in ours {
      let npc = system.SharedNpcOf(line.entity);
      if npc != 0u && TandemOwn.Entity(this.m_game, line.entity) {
        TandemPresence.SendNpcVoice(npc, line.context, line.delay, line.inVehicle);
        this.m_sent += 1;
        if this.m_sent == 1 {
          TandemNative.Log(s"voice: npc \(npc) says \(NameToString(line.context)) (the first of our NPCs' lines, sent to the games linked with us)");
        } else {
          TandemNative.LogDebug(s"voice: npc \(npc) says \(NameToString(line.context))");
        }
      }
    }
    // Theirs: the lines of the NPCs we show, said by our proxies' own voices.
    let theirs = TandemPresence.TakeNpcVoices();
    if ArraySize(theirs) == 0 {
      return;
    }
    let npcs = system.NpcsSync();
    for line in theirs {
      let body = IsDefined(npcs) ? GameInstance.FindEntityByID(this.m_game, npcs.ProxyEntity(line.npc)) as GameObject : null;
      if !IsDefined(body) {
        this.m_unshown += 1;
      } else {
        GameObject.PlayVoiceOver(body, line.context, n"Scripts:TandemNpcVoice", line.delay, EMPTY_ENTITY_ID(), line.inVehicle);
        this.m_said += 1;
        if this.m_said == 1 {
          TandemNative.Log(s"voice: proxy \(line.npc) says the owner's \(NameToString(line.context)) (the first line heard)");
        } else {
          TandemNative.LogDebug(s"voice: proxy \(line.npc) says the owner's \(NameToString(line.context))");
        }
      }
    }
  }

  public func Report() -> Void {
    TandemNative.Log(s"voice: sent \(this.m_sent) (our NPCs), said \(this.m_said) (proxies), \(this.m_unshown) for no proxy here");
  }
}

// A proxy's own lines, from the game's scripts (its 70 callers of
// PlayVoiceOver: OnHit, hit reactions, death, combat start, warnings, fear,
// grenade throws, grapples), are left unsaid: its owner's word says them.
// Our own lines (proxies, puppets: Scripts:TandemNpcVoice,
// Scripts:TandemPresence) go as they are. Reads and calls the game's: a
// hit's or a reaction's may run on a job thread.
@wrapMethod(GameObject)
public final static func PlayVoiceOver(self: ref<GameObject>, voName: CName, debugInitialContext: CName, opt delay: Float, opt answeringEntityID: EntityID, opt canPlayInVehicle: Bool) -> DelayID {
  if TandemNative.IsStarted() && IsDefined(self) && IsNameValid(voName) && self.IsNPC() && NotEquals(debugInitialContext, n"Scripts:TandemNpcVoice") && NotEquals(debugInitialContext, n"Scripts:TandemPresence") && TandemPresence.IsNpcProxy(self.GetEntityID()) {
    return GetInvalidDelayID();
  }
  return wrappedMethod(self, voName, debugInitialContext, delay, answeringEntityID, canPlayInVehicle);
}

// Every voice line asked of an NPC's voice, whoever started it: the event
// the voice takes (SoundPlayVo, the engine's entGameplayVOEvent), on the
// NPC itself. PlayVoiceOver queues it (at once, or through the delay system
// when the line waits); so do the scripts that make their own (a battle
// cry on impact, a fleeing civilian's fear, PlayImpactSound and
// TriggerFearRunningVO) and the engine's own voice paths (a puppet's voice
// interface: research §13.159). It comes when the line plays: no delay to
// send, and it played in a vehicle when the NPC sits in one. Queued through
// a native (an entity's events may run on a job thread); the tick sends
// those of our shared NPCs. A proxy's never go.
@addMethod(ScriptedPuppet)
protected cb func OnTandemGameplayVo(evt: ref<SoundPlayVo>) -> Bool {
  if TandemNative.IsStarted() && IsDefined(evt) && IsNameValid(evt.voContext) && this.IsNPC() && NotEquals(evt.debugInitialContext, n"Scripts:TandemNpcVoice") && NotEquals(evt.debugInitialContext, n"Scripts:TandemPresence") {
    let id = this.GetEntityID();
    if !TandemPresence.IsNpcProxy(id) {
      TandemPresence.QueueNpcVoice(id, evt.voContext, 0.0, true);
    }
  }
  return false;
}
