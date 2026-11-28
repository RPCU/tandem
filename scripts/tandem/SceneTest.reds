// (test) A vanilla scene started outside its quest, by the scene system's own
// entry (plugin/src/engine/scenetest.cpp; docs/research/engine/story.md
// §13.180, in-game tests 1-3). tandem.ini scene_test = on; solo or in a
// session. Commands (tools/game.sh cmd …), from Scenes.reds:
//   story scene play PATH [ENTRY]   create under a new owner of ours, prepare, start
//   story scene play again          the same resource and owner: must be refused
//   story scene play stop           the end request for our last play
//   story scene play state          our V and each actor cast since the play: in a scene?
//   story scene play info PATH      the resource's entry points, actors, plans
//   story scene play cast on|off    the actor dispatcher and bind, logged
//   story scene play hand HASH      the next actor found is that entity instead (0: none)
//   story scene play fact NAME      a quest fact and the quest freeze's counts

public abstract class TandemSceneTest {
  public static func Command(game: GameInstance, command: String) -> Void {
    let args = StrAfterFirst(command, "story scene play ");
    if Equals(args, "stop") {
      TandemNative.Log(TandemStory.SceneStop());
    } else if Equals(args, "again") {
      TandemNative.Log(TandemStory.ScenePlay("", "", true));
    } else if Equals(args, "state") {
      TandemSceneTest.State(game);
    } else if StrBeginsWith(args, "info ") {
      TandemNative.Log(TandemStory.SceneInfo(StrAfterFirst(args, "info ")));
    } else if Equals(args, "cast on") || Equals(args, "cast off") {
      TandemNative.Log(TandemStory.SceneCast(Equals(args, "cast on")));
    } else if StrBeginsWith(args, "hand ") {
      TandemNative.Log(TandemStory.SceneHand(StringToUint64(StrAfterFirst(args, "hand "))));
    } else if StrBeginsWith(args, "fact ") {
      let name = StrAfterFirst(args, "fact ");
      let value = GameInstance.GetQuestsSystem(game).GetFactStr(name);
      TandemNative.Log(s"scene play: fact \(name) = \(value); \(TandemStory.FreezeReport())");
    } else if StrLen(args) > 0 {
      let path = args;
      let entry = "";
      if StrContains(args, " ") {
        path = StrBeforeFirst(args, " ");
        entry = StrAfterFirst(args, " ");
      }
      TandemNative.Log(TandemStory.ScenePlay(path, entry, false));
    } else {
      TandemNative.Log("scene play: story scene play PATH [ENTRY] | again | stop | state | info PATH | cast on|off | hand HASH | fact NAME");
    }
  }

  private static func State(game: GameInstance) -> Void {
    let scenes = GameInstance.GetSceneSystem(game).GetScriptInterface();
    let player = GetPlayer(game);
    if !IsDefined(scenes) || !IsDefined(player) {
      TandemNative.Log("scene play: state: no scene system or no V");
      return;
    }
    let line = s"scene play: state: our V \(scenes.IsEntityInScene(player.GetEntityID()) ? "in a scene" : "in none")";
    let cast = TandemStory.SceneCastEntities();
    let count = ArraySize(cast);
    let i = 0;
    while i < count {
      let id = EntityID.FromHash(cast[i]);
      let entity = GameInstance.FindEntityByID(game, id);
      line += s"; actor \(cast[i]) \(IsDefined(entity) ? "here" : "gone") \(scenes.IsEntityInScene(id) ? "in a scene" : "in none")";
      i += 1;
    }
    TandemNative.Log(s"\(line); \(count) cast");
  }
}
