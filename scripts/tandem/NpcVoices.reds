// NPC voices (docs/architecture/npcs.md, "Voices"; proto::NpcVoice).
//
// An NPC's barks (a callout, a warning, a pain or death cry, a civilian's
// fear, a curious grunt) start in the game's scripts through
// GameObject.PlayVoiceOver: a voice context, asked of the NPC's own voice,
// which picks a line of its voice set in the player's language. A proxy has
// no brain to start them: the owner says which context its NPC asked for,
// and every proxy asks its own voice for the same one. One voice per NPC:
// on a proxy, a line our own game starts (its local hit, a reaction) is left
// unsaid; its owner's word says it.

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

// Every voice line the game starts (its 70 callers: OnHit, hit reactions,
// death, combat start, warnings, fear, grenade throws, grapples). A hit's or
// a reaction's may run on a job thread: this reads, queues through a native
// and calls the game's; the tick does the rest. Our own lines (proxies,
// puppets: Scripts:TandemNpcVoice, Scripts:TandemPresence) go as they are.
@wrapMethod(GameObject)
public final static func PlayVoiceOver(self: ref<GameObject>, voName: CName, debugInitialContext: CName, opt delay: Float, opt answeringEntityID: EntityID, opt canPlayInVehicle: Bool) -> DelayID {
  if TandemNative.IsStarted() && IsDefined(self) && IsNameValid(voName) && self.IsNPC() && NotEquals(debugInitialContext, n"Scripts:TandemNpcVoice") && NotEquals(debugInitialContext, n"Scripts:TandemPresence") {
    let id = self.GetEntityID();
    if TandemPresence.IsNpcProxy(id) {
      return GetInvalidDelayID();
    }
    TandemPresence.QueueNpcVoice(id, voName, delay, canPlayInVehicle);
  }
  return wrappedMethod(self, voName, debugInitialContext, delay, answeringEntityID, canPlayInVehicle);
}
