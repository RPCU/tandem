// Every way a V moves, on the puppet (docs/ARCHITECTURE.md open problem).
//
// Our V sends how it moves (in the air, climbing, on a ladder, swimming...)
// from its player state machine, packed in PlayerState's flags: the layout
// is core/include/tandem/movement.hpp's, keep the two in sync. The puppet's
// movement controller (plugin/src/engine/controller.cpp) places the body
// from it, and asks this side to teleport the body when it cannot bring it
// where the player is (TandemSystem.SnapPuppet).

public abstract class TandemMovement {
  // PlayerState.flags for our V: LocomotionDetailed (bits 0-4), Swimming
  // (bits 5-6), in a moving elevator (7), in a workspot (8), felled (9),
  // filled (15).
  public static func Flags(psm: ref<IBlackboard>) -> Int32 {
    if !IsDefined(psm) {
      return 0;
    }
    let defs = GetAllBlackboardDefs();
    let flags = 32768;
    let detailed = psm.GetInt(defs.PlayerStateMachine.LocomotionDetailed);
    if detailed > 0 && detailed < 32 {
      flags += detailed;
    }
    let swim = psm.GetInt(defs.PlayerStateMachine.Swimming);
    if swim > 0 && swim < 4 {
      flags += swim * 32;
    }
    if psm.GetBool(defs.PlayerStateMachine.IsPlayerInsideMovingElevator) {
      flags += 128;
    }
    if psm.GetInt(defs.PlayerStateMachine.IsInWorkspot) != 0 {
      flags += 256;
    }
    if psm.GetBool(defs.PlayerStateMachine.Felled) {
      flags += 512;
    }
    return flags;
  }
}

// A puppet swims and dives where its player does. An NPC whose head is
// under water is killed by its ragdoll behaviour (AIRagdollDelegate
// DoCheckWaterLevel): not a puppet.
@wrapMethod(NPCPuppet)
public const final func KillIfUnderwater() -> Bool {
  if TandemCombat.PuppetPlayer(this.GetGame(), this) != 0u {
    return false;
  }
  return wrappedMethod();
}
