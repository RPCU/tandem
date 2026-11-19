// NPC proxies on a guest (milestone 5): the host's NPCs drawn on a guest
// with their brain off, moved by tandem's controller. Recipe from
// docs/research/coop-npcs-combat.md §2.2, first tried here.
// tools/game.sh cmd proxy test [record] | proxy clear

public class TandemProxyTest {
  public let id: EntityID;
  public let pacified: Bool;
  public let since: Float;
}

public abstract class TandemProxies {
  // What the game switches off when an NPC is incapacitated
  // (ScriptedPuppet.OnIncapacitated), plus the behaviour tree itself.
  public static func Pacify(npc: ref<ScriptedPuppet>) -> String {
    let done = "";
    let ai = npc.GetAIControllerComponent();
    if IsDefined(ai) {
      ai.Toggle(false);
      done += " ai";
    }
    let senses = npc.GetSensesComponent();
    if IsDefined(senses) {
      senses.ToggleComponent(false);
      senses.RemoveSenseMappin();
      done += " senses";
    }
    let bump = npc.GetBumpComponent();
    if IsDefined(bump) {
      bump.Toggle(false);
      done += " bump";
    }
    let tracker = npc.GetTargetTrackerComponent();
    if IsDefined(tracker) {
      tracker.ClearThreats();
      done += " threats";
    }
    return done;
  }

  // The reverse of Pacify, for a proxy our game takes over (regions: its
  // owner left it with us) and for a body of ours given back to us. What its
  // behaviour tree was doing is gone: it decides again from what it senses.
  public static func Wake(npc: ref<ScriptedPuppet>) -> String {
    let done = "";
    let ai = npc.GetAIControllerComponent();
    if IsDefined(ai) {
      ai.Toggle(true);
      done += " ai";
    }
    let senses = npc.GetSensesComponent();
    if IsDefined(senses) {
      senses.ToggleComponent(true);
      senses.CreateSenseMappin();
      done += " senses";
    }
    let bump = npc.GetBumpComponent();
    if IsDefined(bump) {
      bump.Toggle(true);
      done += " bump";
    }
    return done;
  }

  // Its brain is on (the AI component's own word).
  public static func Awake(npc: ref<ScriptedPuppet>) -> Bool {
    let ai = npc.GetAIControllerComponent();
    return IsDefined(ai) && ai.IsEnabled();
  }
}
