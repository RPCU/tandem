// NPC voices (docs/architecture/npcs.md, "Voices"; proto::NpcVoice).
//
// An NPC's barks (a callout, a warning, a pain or death cry, a civilian's
// fear, a curious grunt): a voice context, asked of the NPC's own voice,
// which picks a line of its voice set in the player's language. Whoever
// starts one (the game's scripts through GameObject.PlayVoiceOver or a
// SoundPlayVo of their own, the delay system, the engine's own voice paths),
// it reaches the NPC as one event, SoundPlayVo (entGameplayVOEvent, queued
// on the entity: research §13.159). The audio system's own picks (combat and
// idle chatter: a voice trigger, its base name and variation) reach it as
// another, entTriggerVOEvent (research §13.163). A proxy has no brain to
// start them: the owner says which context or trigger its NPC was asked
// for, and every proxy asks its own voice for the same one. One voice per
// NPC: on a proxy, a line our own game's scripts start (its local hit, a
// reaction) is left unsaid; its owner's word says it.

public class TandemNpcVoices {
  private let m_game: GameInstance;
  private let m_sent: Int32;
  private let m_said: Int32;
  private let m_unshown: Int32;
  private let m_triggersSent: Int32;
  private let m_triggersSaid: Int32;
  // A trigger the audio system of our own game picked for a proxy: heard
  // beside its owner's (an event cannot be held back): counted to know.
  private let m_proxyTriggers: Int32;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
  }

  public func Tick(system: ref<TandemSystem>) -> Void {
    // Ours: the lines our shared NPCs started (queued by the handlers below).
    let ours = TandemPresence.TakeLocalNpcVoices();
    for line in ours {
      if line.trigger && TandemPresence.IsNpcProxy(line.entity) {
        this.m_proxyTriggers += 1;
        if this.m_proxyTriggers == 1 {
          TandemNative.Log(s"voice: a proxy's own trigger \(NameToString(line.context)) (our game's audio picked it for another game's NPC: heard beside the owner's)");
        }
      } else {
        let npc = system.SharedNpcOf(line.entity);
        if npc != 0u && TandemOwn.Entity(this.m_game, line.entity) {
          if line.trigger {
            TandemPresence.SendNpcTriggerVoice(npc, line.context, line.variation, line.variations, line.voContext);
            this.m_triggersSent += 1;
            if this.m_triggersSent == 1 {
              TandemNative.Log(s"voice: npc \(npc) says trigger \(NameToString(line.context)) variation \(line.variation) of \(line.variations) (the first of our NPCs' triggers, sent to the games linked with us)");
            } else {
              TandemNative.LogDebug(s"voice: npc \(npc) says trigger \(NameToString(line.context)) variation \(line.variation) of \(line.variations)");
            }
          } else {
            TandemPresence.SendNpcVoice(npc, line.context, line.delay, line.inVehicle);
            this.m_sent += 1;
            if this.m_sent == 1 {
              TandemNative.Log(s"voice: npc \(npc) says \(NameToString(line.context)) (the first of our NPCs' lines, sent to the games linked with us)");
            } else {
              TandemNative.LogDebug(s"voice: npc \(npc) says \(NameToString(line.context))");
            }
          }
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
        if line.trigger {
          TandemNpcVoices.Trigger(body, line.context, line.variation, line.variations, line.voContext, n"Scripts:TandemNpcVoice");
          this.m_triggersSaid += 1;
          if this.m_triggersSaid == 1 {
            TandemNative.Log(s"voice: proxy \(line.npc) says the owner's trigger \(NameToString(line.context)) variation \(line.variation) of \(line.variations) (the first trigger heard)");
          } else {
            TandemNative.LogDebug(s"voice: proxy \(line.npc) says the owner's trigger \(NameToString(line.context)) variation \(line.variation) of \(line.variations)");
          }
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
  }

  // A voice trigger said the engine's way: the event its voice interface
  // builds (entTriggerVOEvent: base name, variation index and number, the
  // overriding VO context, 5 when none), queued on the entity, where the
  // NPC's voice set takes it (research §13.163).
  public static func Trigger(body: ref<GameObject>, trigger: CName, variation: Uint32, variations: Uint32, voContext: Int32, tag: CName) -> Void {
    let evt = new entTriggerVOEvent();
    evt.triggerBaseName = trigger;
    evt.triggerVariationIndex = variation;
    evt.triggerVariationNumber = variations;
    evt.debugInitialContext = tag;
    evt.answeringEntityIDHash = 0ul;
    evt.overridingVoContext = IntEnum<locVoiceoverContext>(voContext);
    body.QueueEvent(evt);
  }

  // (test) tools/game.sh cmd presence voice trigger NAME [VARIATION
  // VARIATIONS]: that trigger on the nearest of the NPCs we share, queued as
  // the engine queues one, so our handler hears it as it hears the audio
  // system's.
  public func Command(command: String, system: ref<TandemSystem>) -> Void {
    if !StrBeginsWith(command, "trigger ") {
      TandemNative.Log("presence: voice trigger NAME [VARIATION VARIATIONS]");
      return;
    }
    let parts = StrSplit(StrAfterFirst(command, "trigger "), " ");
    let name = ArraySize(parts) > 0 ? StringToName(parts[0]) : n"None";
    let variation = ArraySize(parts) > 1 ? Cast<Uint32>(StringToInt(parts[1], 0)) : 1u;
    let variations = ArraySize(parts) > 2 ? Cast<Uint32>(StringToInt(parts[2], 0)) : 1u;
    let npcs = system.NpcsSync();
    let player = GetPlayer(this.m_game);
    if !IsDefined(npcs) || !IsDefined(player) || !IsNameValid(name) {
      TandemNative.Log("presence: voice trigger: no NPCs shared, no V or no name");
      return;
    }
    let at = player.GetWorldPosition();
    let best: ref<GameObject> = null;
    let bestNpc = 0u;
    let bestDistance = 0.0;
    let known = npcs.Known();
    for entry in known {
      let body = GameInstance.FindEntityByID(this.m_game, entry.id) as GameObject;
      if IsDefined(body) && !entry.dead {
        let distance = Vector4.DistanceSquared(body.GetWorldPosition(), at);
        if !IsDefined(best) || distance < bestDistance {
          best = body;
          bestNpc = entry.npc;
          bestDistance = distance;
        }
      }
    }
    if !IsDefined(best) {
      TandemNative.Log("presence: voice trigger: none of our shared NPCs is here");
      return;
    }
    TandemNpcVoices.Trigger(best, name, variation, variations, 5, n"Scripts:TandemTest");
    TandemNative.Log(s"presence: voice trigger \(NameToString(name)) variation \(variation) of \(variations) queued on npc \(bestNpc)");
  }

  public func Report() -> Void {
    TandemNative.Log(s"voice: sent \(this.m_sent) (our NPCs), said \(this.m_said) (proxies), \(this.m_unshown) for no proxy here; triggers sent \(this.m_triggersSent), said \(this.m_triggersSaid), a proxy's own \(this.m_proxyTriggers)");
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

// Every voice trigger the audio system picks for an NPC (its combat and
// idle chatter: the voice interface's third start, research §13.163): the
// event the voice takes, entTriggerVOEvent, on the NPC itself, with its base
// name, variation and their number. Queued through a native (an entity's
// events may run on a job thread); the tick sends those of our shared NPCs
// and counts a proxy's own (ours said as the owner's carry our tag).
@addMethod(ScriptedPuppet)
protected cb func OnTandemTriggerVo(evt: ref<entTriggerVOEvent>) -> Bool {
  if TandemNative.IsStarted() && IsDefined(evt) && IsNameValid(evt.triggerBaseName) && this.IsNPC() && NotEquals(evt.debugInitialContext, n"Scripts:TandemNpcVoice") {
    TandemPresence.QueueNpcTriggerVoice(this.GetEntityID(), evt.triggerBaseName, evt.triggerVariationIndex, evt.triggerVariationNumber, EnumInt(evt.overridingVoContext));
  }
  return false;
}
