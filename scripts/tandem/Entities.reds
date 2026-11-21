// Deleting a body Codeware spawned for us (DynamicEntitySystem): only once
// it exists. A body deleted before its entity stub was created is freed
// under the game: Codeware forgets it at once, the stub is created anyway,
// registered with the population system, then freed with Codeware's last
// reference to it, and the population system's next pooling update reads
// the freed stub (the guest's crash of the first two-game session,
// 2026-11-21: a join's first tick takes seconds of spawns and removals in
// one batch; docs/research/engine/vehicles.md §13.140). So every deletion of
// ours goes through here: at once for a body that exists, at the first tick
// it does otherwise (TandemSystem.TickDeleting).

public class TandemDeleting {
  public let id: EntityID;
  public let since: Float;
}

public abstract class TandemEntities {
  // A body that never appears (a record that does not spawn) has had its
  // stub long before this: deleting it then is the game's usual path.
  public static func WaitLimit() -> Float {
    return 10.0;
  }

  public static func Delete(id: EntityID) -> Void {
    if !EntityID.IsDefined(id) {
      return;
    }
    let entities = GameInstance.GetDynamicEntitySystem();
    if !entities.IsManaged(id) {
      return;
    }
    if entities.IsSpawned(id) {
      entities.DeleteEntity(id);
      return;
    }
    let system = TandemSystem.Of(GetGameInstance());
    if IsDefined(system) {
      system.DeleteLater(id);
    }
  }

  // Every body of ours with this tag.
  public static func DeleteTagged(tag: CName) -> Void {
    let ids = GameInstance.GetDynamicEntitySystem().GetTaggedIDs(tag);
    for id in ids {
      TandemEntities.Delete(id);
    }
  }
}
