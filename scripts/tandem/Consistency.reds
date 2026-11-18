// Consistency (docs/ARCHITECTURE.md, "Consistency"): what the plugin's
// checks need from the game. The checks themselves run where the state
// lives: NPC proxies in Npcs.reds (TandemNpcs.Check), puppets in System.reds
// (TandemSystem.CheckPuppets), both against the session's record
// (SyncNatives.reds).

public abstract class TandemConsistency {
  // Something serious, the game's own way: the HUD's simple warning message
  // (as Pause.reds and Story.reds show theirs). The plugin decides when
  // (a resync, a lost host, a reconnection), at most once in a while.
  public static func Notice(game: GameInstance, text: String) -> Void {
    let message: SimpleScreenMessage;
    message.isShown = true;
    message.duration = 4.0;
    message.message = text;
    message.type = SimpleMessageType.Neutral;
    GameInstance
      .GetBlackboardSystem(game)
      .Get(GetAllBlackboardDefs().UI_Notifications)
      .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
  }
}

// A fast travel rebuilds the world around our V (FastTravelSystem,
// final.reds:50415): the session's epoch moves on, so what was in flight
// about the old one (our hits on NPCs that are gone, the host's NPC states)
// is dropped, and the others' checks run at once.
@wrapMethod(FastTravelSystem)
private final func PerformFastTravel(player: ref<GameObject>, nodeData: ref<FastTravelPointData>) {
  if TandemNative.IsStarted() {
    TandemSync.WorldRebuilt(2);
  }
  wrappedMethod(player, nodeData);
}
