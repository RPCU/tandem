# Complete Third-person Overhaul (CTO): what it teaches about drawing a V

Read 2026-11-17: https://www.nexusmods.com/cyberpunk2077/mods/38266, v1.0.2
(file 178683, game 2.5 only), by Typiakk with dd4nt388 (a fork of CTPP).
Inspected for ideas only; nothing of it is shipped or copied. Its animations
are retargeted from many sources and are not ours to redistribute.

## How it is built

- `red4ext/plugins/CompleteThirdPersonOverhaul/CompleteThirdPersonOverhaul.dll`
  (the "VMotion" plugin, hooks the 2.5 exe directly) and `vmotion.vmm`, a
  74 MB motion-matching database, plus tuning data (`tune.txt`, `grip.txt`,
  `cover.bin`, `katana_wr.bin`).
- CET Lua (`CompleteThirdPersonOverhaul_Motion/init.lua`, 234 KB) feeds the
  plugin every frame through natives `Game.VMotionInput`, `VMotionLook`,
  `VMotionHit`, `VMotionWeaponType`, `VMotionWalkStyle`, `VMotionMale`,
  `VMotionKatana`... Its header: _"Sends: V's velocity and the velocity the
  stick/keys ask for, both in V's own frame (x right, y forward, m/s), her
  yaw rate (rad/s, + = turning left) and flags (1 = drive locomotion,
  2 = weapon in hand -> legs only)."_
- redscript (`cto_core.reds`, `cto_move360.reds`, `cto_slide.reds`...) for
  game-side state; ArchiveXL archives (`!cto.archive` 325 MB) replace V's
  animation graph and player entity pieces; Audioware/AudioXL foley.
- The plugin writes **bones at the animation graph's output** ("upper
  override: ... our torso re-applied at the graph output", "leg override: our
  leg bones re-applied at the graph output during traversal", "output node:
  V's final pose seen"). Male V: bone lengths corrected on the final pose.
  Foot IK, head/upper-body look-at the camera, weapon carry poses, grip IK.

## What tandem takes from it

1. **The remote V is V's real third-person body**: the player's own TPP
   template with that player's customization and equipment, as they would
   see themselves in third person. Not an NPC rig with a face on it.
2. **What the network must carry** for a remote body to animate like CTO's
   local one: velocity and _intended_ velocity in the body's frame, yaw rate,
   look direction (camera yaw/pitch), stance (stand/crouch/air/slide/vault),
   weapon (type, drawn, aiming, firing). These become fields of the
   player-state message as the animation work needs them.
3. **Pose at the graph output** is how a rig without locomotion clips gets a
   full movement set. For tandem it opens two later options for the remote
   V: stream the sender's final pose (exact, ~1 KB per state for ~100 bones),
   or run our own motion matching on the receiver from the state above.
4. CTO itself conflicts with any mod that replaces V's graph or player entity
   ("Not compatible with other third-person mods"): a remote puppet must not
   use the local player's entity or graph, or the two mods fight over it.
5. **Where it hooks** (strings and disassembly of its hooking routine, facts
   only): it creates an `animAnimNode_Output` through RTTI, reads its vtable
   and replaces the slot at +0x128 (the node's sample function); other node
   classes (`animAnimNode_SkAnim`…) the same way, for tracing. Details and
   the pose layout in `pose-streaming.md` (fourth pass).
