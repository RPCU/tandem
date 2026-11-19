// Progression's natives (plugin/src/progress_natives.cpp). Keep the two in
// sync: tools/check-natives.py checks them with Natives.reds.

public native class TandemProgress extends IScriptable {
  // Our V's level (1..100), when it changes: the host keeps everyone's.
  public static native func SendOwnLevel(level: Int32)
  // Host: one of our NPCs' power level, scaling included.
  public static native func SendNpcLevel(npc: Uint32, level: Float)
  // Host: the PowerLevel modifier one of our NPCs should carry (0: none).
  // curveAtHost, curveAtSession: its content assignment's curve at the two
  // levels (< 0: none read); fixed: a constant content assignment.
  public static native func NpcLevelShift(level: Float, applied: Float, hostLevel: Float, sessionLevel: Float, curveAtHost: Float, curveAtSession: Float, fixed: Bool, maxLevel: Float) -> Float
  // Guest: the modifier a proxy should carry to be at the host's NPC's level.
  public static native func ProxyLevelShift(level: Float, applied: Float, hostNpcLevel: Float) -> Float
  // Enemy scaling (Scaling.reds, core tandem/scaling.hpp). The session's rule
  // as this game knows it: on, and the health added per extra player.
  public static native func EnemyScaling() -> Bool
  public static native func EnemyScalingHealth() -> Float
  // How many players count for one of the NPCs our game runs this round
  // (1..4): distances, player p's at p - 1 (< 0: not placed here); engaged,
  // bit p - 1: that player fights it. Hysteresis inside; 1 out of a session.
  public static native func ScaleCount(npc: Uint32, distances: array<Float>, engaged: Int32, now: Float) -> Int32
  // It died, went or changed hands; the session is over.
  public static native func ForgetScale(npc: Uint32)
  public static native func ClearScales()
  // The multipliers an NPC scaled for `players` carries (1: none), and its
  // hit reaction thresholds' (its health without ours, and what its
  // HitReactionComponent read).
  public static native func ScaleHealth(players: Int32) -> Float
  public static native func ScalePoise(players: Int32) -> Float
  public static native func ScaleArmor(players: Int32) -> Float
  public static native func ScaleThreshold(players: Int32, baseHealth: Float, componentHealth: Float) -> Float
  // Our word on one of our NPCs (NpcScale).
  public static native func SendNpcScale(npc: Uint32, players: Int32)
  // Cars (docs/architecture/garage.md). tandem.ini `lend` (0 crew, 1 only
  // with us aboard, 2 never), said to everyone; a partner's word (0 when it
  // said none); whether a partner may take the wheel under a word.
  public static native func OwnLend() -> Int32
  public static native func SendCarLend(lend: Int32)
  public static native func CarLendOf(player: Uint32) -> Int32
  public static native func MayDrive(lend: Int32, ownerAboard: Bool, ownerInSession: Bool) -> Bool
  // tandem.ini `leave`: 0 stay, 1 save, 2 reload.
  public static native func LeaveMode() -> Int32
  // One step of our way out of a session (core/progress.hpp). left: what
  // of the session is still in our game (bits, Saves.reds); blocked: the
  // game's own save locks; saved: since we asked for the save, 0 no word,
  // 1 written, -1 failed. Returns 0 none, 1 clean (saving unlocks), 2 save
  // now, 3 saved, 4 load now, 5 the save failed, 6 gave up, 7 dirty.
  public static native func LeaveTick(now: Float, session: Bool, chosen: Bool, left: Int32, blocked: Bool, saved: Int32) -> Int32
  public static native func LeaveLocked() -> Bool
  // 0 solo, 1 session, 2 cleaning, 3 waiting, 4 saving, 5 loading, 6 dirty.
  public static native func LeaveStep() -> Int32
  // A world loaded: what the old one left is gone with it.
  public static native func LeaveReset()
  public static native func LeftNames(left: Int32) -> String
}
