// Per-player regions on the game's side (docs/architecture/regions.md): the
// rules a game applies on its own. Who is linked with whom and who owns
// which NPC come from the session (RegionNatives.reds); hand-overs are in
// Npcs.reds (Give, Take), since they move an NPC between its two lists.
//
// The numbers are tandem/regions.hpp's: keep them the same.

// What our player sees now: within 50 m of our V and in the camera's
// frustum. The switch from our own crowd to the owner's copies is made out
// of sight (an NPC or a car that pops in or out before the player's eyes).
public abstract class TandemSight {
  public static func Seen(game: GameInstance, object: ref<GameObject>, height: Float, radius: Float) -> Bool {
    let player = GetPlayer(game);
    if !IsDefined(player) || !IsDefined(object) || Vector4.Distance(player.GetWorldPosition(), object.GetWorldPosition()) > 50.0 {
      return false;
    }
    // By position alone: the frustum test asked of the object itself was
    // made of bodies a frame old (spawning, being disposed) in the run that
    // went down three times (2026-11-23). The camera's view cone, widened
    // by the object's radius at its distance.
    let cameras = GameInstance.GetCameraSystem(game);
    let forward = Vector4.Normalize(cameras.GetActiveCameraForward());
    let eye = player.GetWorldPosition();
    eye.Z += 1.7;
    let at = object.GetWorldPosition();
    at.Z += height * 0.5;
    let to = at - eye;
    let far = Vector4.Length(to);
    if far < radius {
      return true;
    }
    let half = Deg2Rad(cameras.GetActiveCameraFOV()) * 0.5 * 1.25 + AtanF(radius, far);
    return Vector4.Dot(to, forward) / far > CosF(half);
  }
}

public abstract class TandemRegionRules {
  // A region: this far around a V (whose crowd and streets these are).
  public static func Radius() -> Float {
    return 60.0;
  }

  // What a game shares of its NPCs around its V, and holds back of its own
  // inside a higher-ranked linked player's (TandemNpcs.Range): both
  // screens show the same people within it.
  public static func Share() -> Float {
    return 100.0;
  }

  // An owner stops sharing an NPC farther than this from its V.
  public static func Lose() -> Float {
    return 130.0;
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

  // A hit dealt by a proxy (its attacker's side is its owner's).
  public static func ProxyHit(game: GameInstance, hitEvent: ref<gameHitEvent>) -> Bool {
    let from = hitEvent.attackData.GetInstigator();
    return IsDefined(from) && TandemOwn.IsProxy(game, from.GetEntityID());
  }
}
