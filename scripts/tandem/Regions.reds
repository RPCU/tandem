// Per-player regions on the game's side (docs/architecture/regions.md): the
// rules a game applies on its own. Who is linked with whom and who owns
// which NPC come from the session (RegionNatives.reds); hand-overs are in
// Npcs.reds (Give, Take), since they move an NPC between its two lists.
//
// The numbers are tandem/regions.hpp's: keep them the same.

public abstract class TandemRegionRules {
  // A region: this far around a V (TandemNpcs.Range).
  public static func Radius() -> Float {
    return 60.0;
  }

  // An owner stops sharing an NPC farther than this from its V.
  public static func Lose() -> Float {
    return 90.0;
  }

  // Our crowd goes off when our V is this deep in a higher-ranked linked
  // player's region, and comes back beyond the radius.
  public static func QuietIn() -> Float {
    return 45.0;
  }

  // Where regions overlap, a player that holds its region ranks first (a
  // gig of its own world runs there), then the host (player 1), then the
  // lowest id.
  public static func Outranks(a: Uint32, b: Uint32) -> Bool {
    if a == b {
      return false;
    }
    let holdsA = TandemRegions.Holds(a);
    let holdsB = TandemRegions.Holds(b);
    if NotEquals(holdsA, holdsB) {
      return holdsA;
    }
    if a == 1u || b == 1u {
      return a == 1u;
    }
    return a < b;
  }

  // The higher-ranked linked player whose region covers this point (its V
  // within `radius` of it, as the session places it), 0: none. That
  // player's game populates the place. `unplaced`: one that outranks us has
  // sent no state yet, so we cannot tell: the caller waits.
  public static func CoveredBy(at: Vector4, radius: Float, out unplaced: Bool) -> Uint32 {
    let by = TandemRegions.CoveredBy(at.X, at.Y, at.Z, radius);
    unplaced = by >= 2147483648u;
    return unplaced ? by - 2147483648u : by;
  }

  // In a fight, hurt or otherwise doing something a player would miss: never
  // held back, whoever's region it stands in.
  public static func Engaged(game: GameInstance, npc: ref<ScriptedPuppet>) -> Bool {
    if NPCPuppet.IsInCombat(npc) || ScriptedPuppet.IsDefeated(npc) || ScriptedPuppet.IsUnconscious(npc) {
      return true;
    }
    if NotEquals(npc.GetHighLevelStateFromBlackboard(), gamedataNPCHighLevelState.Relaxed) {
      return true;
    }
    let health = GameInstance.GetStatPoolsSystem(game).GetStatPoolValue(Cast<StatsObjectID>(npc.GetEntityID()), gamedataStatPoolType.Health, true);
    return health < 99.0;
  }

  // Never changes owner: a quest or story NPC, a boss, a police unit (it is
  // registered with the prevention system of the game that spawned it).
  public static func Pinned(npc: ref<ScriptedPuppet>) -> Bool {
    return npc.IsQuest() || npc.IsBoss() || npc.IsPrevention();
  }
}

// Who runs a shared NPC, for every script that used to ask "are we the
// host?" about an NPC: the game that runs it plays the owner's part (the real
// NPC, its hits, its word to the others), every other game the proxy's.
public abstract class TandemOwn {
  private static func Npcs(game: GameInstance) -> ref<TandemNpcs> {
    let system = GameInstance.GetScriptableSystemsContainer(game).Get(n"TandemSystem") as TandemSystem;
    return IsDefined(system) ? system.NpcsSync() : null;
  }

  // We run this shared NPC (by its session id, or by its entity).
  public static func Npc(game: GameInstance, npc: Uint32) -> Bool {
    let npcs = TandemOwn.Npcs(game);
    return IsDefined(npcs) && npcs.Owns(npc);
  }

  public static func Entity(game: GameInstance, id: EntityID) -> Bool {
    let npcs = TandemOwn.Npcs(game);
    return IsDefined(npcs) && npcs.OwnsEntity(id);
  }

  // The entity is a proxy of an NPC another game runs.
  public static func IsProxy(game: GameInstance, id: EntityID) -> Bool {
    let npcs = TandemOwn.Npcs(game);
    return IsDefined(npcs) && npcs.IsProxyEntity(id);
  }

  // A hit dealt by an NPC we run (its attacker's side is ours to compute).
  public static func Hit(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let from = hitEvent.attackData.GetInstigator();
    return IsDefined(from) && TandemOwn.Entity(game, from.GetEntityID());
  }
}
