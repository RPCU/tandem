# Animation

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 4. Animation

- **AnimationDriver** = an engine struct NCO calls through three hashes: `Attach(Entity*)`
  **2624656810**, `Detach()` **2385973466**, `SendParameters(AnimationData&)` **2132808949**; plus
  `AnimationControllerComponent::GetAnimLength(CName)` **3750304414**
  (`Game/Animation/AnimationDriver.cpp`). Struct members: `Handle<anim::AnimFeature_Locomotion>`,
  `Handle<anim::AnimFeature_CrowdLocomotion>`, `WeakHandle<AnimationControllerComponent>`
  (`AnimationDriver.h:20-22`). Engine struct size **0x30** on 2.5 (`e3c5e455`). Hashes present in
  2.50, used on 2.5 via the MultiMovementController path (test.85+). [2.5-verified via the default path]
- `AnimationData` (passed to SendParameters / the idle controller): `{CName controller; bool debug;
Locomotion_Style style; MotionTableAction action; float path, slope, delta; bool stairs; float ground,
time, footScale(=1), startAngle; bool unk; float speed}` (`AnimationData.h`) — field order is the ABI;
  engine size **0x3C** on 2.5 (`e3c5e455`). `speed` is the graph's blend parameter — it was never
  written at first (`0d3a747e`). A missing clip stalls the motion pipeline (frozen puppet) → degrade to
  idle-glide; jog band falls back to `sprint_0` if no `jog_0` (`feb1e504`, `b4bfc201`).
- Enums: `Locomotion_Style {Idle, Rotation, Walk, Jog, Sprint, Any}`; `MotionTableAction`
  (None 0, Start 1, Stop 2, Move 3, TurnInPlace 4 … CrowdDirectionalStartFast 47); `Locomotion_AnimType`
  clip ids (idle_stand 1, walk_0 7, jog_0 10, sprint_0 13 …) (`Game/Animation/Base.h`).
- State machine (Spawning→Idling/Walking/Jogging/Sprinting): Walking sends `action=MTA_Move,
style=LS_Walk, time=fmod(t, len(walk_0)), speed`; if the clip length is 0 it degrades to idle
  (`States/Walking.cpp`). Band thresholds **walk 1.2, jog 4.0, sprint 6.5 m/s** (real speeds: walk
  ~1.8, jog ~5.5, sprint 7.5+; old walk threshold 3.0 made walkers glide) (`States/Base.h:24-29`).
- `AnimationControllerComponent::ApplyFeature(CName, Handle<AnimFeature>)` hash **316482401** declared
  but unused (`MultiMovementController.cpp:11`). [unverified]
- `PuppetDriver` (mod-owned, ticked on main thread, re-binds when the weak component handle expires —
  mounts rebuild components; rate-limited 1/s) is the transform-only path; idle controller then gets no
  vanilla writes for driver puppets on 2.31 (`PuppetDriver.h:10-19`). Off 2.31 driver puppets must still
  run the vanilla idle, or they have no idle pose (test.80: invisible body) (`InterpolationSystem.cpp:1329-1338`).
- AI-steered bodies (fallback) get the engine's own locomotion; a jump cannot be reproduced (AI walk
  can't jump) → steer to ground height.

### 13.2 Animation: from an entity to its pose

All offsets measured on the local V and checked on every live
`entAnimatedComponent` (≈760 at the time, ≈357 of them root components).

- **`entAnimatedComponent` unmapped fields** (RTTI size 0x2B0, instance
  vtable `0x1430320F8`): +0x168 the `worldAnimationSystem`, +0x170 the
  `workWorkspotSystem` (the two "global objects" of pose-streaming.md),
  **+0x178 the entity's animated object** (null on non-root components:
  deformations, face rig, shadow…), +0x250/+0x258 a weak handle to itself,
  **+0x270 its graph instance**, +0x288 `controlBinding`. [2.5-verified]
- **Graph instance** (`GI = [component+0x270]`, one per animated component,
  ≥ 0x25C8 bytes): +0x20 the `animAnimGraph`, +0x28 the instance data (third
  argument of a node's sample function), +0x40 the `animRig`, +0x2510 root
  motion of the last sample (48-byte transform), +0x2540 / +0x2544 update /
  sample counters (≈ frames). `[GI+0x40]` is the component's rig on 757 of
  758 components, `[GI+0x20]` its graph likewise. [2.5-verified]
- **Animated object** (`obj = [root component+0x178]`, one per entity, stable
  while the entity lives; no vtable): +0x00 a 32-bit id (-1 = unusable),
  **+0x08 the merged skeleton**, **+0x18 the pose**, +0x60 the root
  component's graph instance (equal to `[component+0x270]` on 356 of 356).
  Without +0x178 the engine finds it through the animation system:
  `0x140286A3C(system, component)` (hash 1353065690). [2.5-verified]
- **Merged skeleton** (`[obj+0x08]`): the root rig's bones first, then the
  bones of every other animated component of the entity (deformations, face
  rig, shadow, dangles). +0x00 reference transforms (count at +0x08), +0x10
  parent indices (int16), +0x20 bone names (CName), +0x40/+0x50 float track
  defaults and names (count at +0x48), +0x60 a signature that changes when
  the skeleton is rebuilt (appearance or equipment change: 677 → 619
  transforms on V during the session, new skeleton and pose objects, same
  animated object). Lists for V and for NPCs on V's two body rigs:
  `data/skeletons-2.50.txt`. [2.5-verified]
- **Pose** (`[obj+0x18]`): +0x00 → transforms, 48 bytes each (translation,
  rotation quaternion, scale: 3 × 4 floats), one per skeleton entry, count at
  +0x08; +0x18 → float tracks (count at +0x20); +0x28 / +0x38 extra named
  tracks and transforms (64-byte entries, name first). The arrays keep their
  address from frame to frame and are rewritten in place. They hold
  **model space** except for a short window each frame (≈3 % of 700
  samples): the graph's local-space result is copied in, then converted in
  place, parents first. Hips of a standing male V: z = 0.95–0.98 m.
  [2.5-verified]
- **Reading one bone** as the engine does: `0x14032AA4C(component, boneRef,
out WorldTransform)` (hash 3930267764; `boneRef` = index, name, skeleton
  signature: re-resolved by name when the signature differs) →
  `0x14032B1E8(obj, boneRef, out)` (hash 444473555), which is
  `pose.transforms[boneRef.index]`. Slots use it. [2.5-static]
- **V's own entity** has four animated components (`root` on rig
  7304775167351189356 with 152 bones, `deformations`, `shadow`,
  `face_rig`) and one animated object of 619 transforms. Its skeleton has
  `shadow_Hips … shadow_WeaponRight` (indices 236–261), a 26-bone copy of
  the body that follows the main bones. [2.5-verified]

### 13.3 Animation: where a frame's pose is produced

From the code (decompiled), consistent with the live counters and buffers:

1. `0x1401B25DC(GI, params)` (hash 4286257933) runs once per graph instance
   and frame, on a job thread: it updates the graph
   (`0x1403A9D90(graph, ctx)`), samples it (`0x1403A98D0(graph, ctx)` →
   `0x1403A9C80` → the root node's virtual at vtable +0x128), stores the root
   motion at GI+0x2510, then commits. `params[7]` (+0x38) is the entity's
   pose (13.2), `params[6]` (+0x30) the bone mapping from this component's
   rig to the merged skeleton. [2.5-static]
2. **The commit: `0x1401B3C08(GI, params, pose, lod)`** (hash 2074943978,
   hooked and written to by tandem in game 2026-11-18: **[2.5-verified]**;
   also called from `0x1401B5AF4` and `0x142430540`). It copies
   `[pose+0x30][src]` into the entity's pose at `[dst]` for each (src, dst)
   pair of the mapping, and the float tracks from `[pose+0x38]`. `pose` is
   the graph's output: bone count at +0x00, extra transform and track counts
   at +0x08 / +0x0C, **a pointer to the transforms at +0x30** (local space,
   in the rig's bone order, 48 bytes each), a pointer to the float tracks at
   +0x38. [2.5-verified: count 71 for a body rig, LeftHand 0.275 m from its
   parent = the reference forearm]
3. The entity's pose is then converted to model space in place (seen live,
   13.2); the function was not looked for.

**What this gives tandem** (pose streaming): one hook on the commit sees
every character's final local-space pose with its identity:
`GI == [component+0x270]`. Read V's pose there (copy `pose` when GI is the
local V's root component's); write a puppet's there (overwrite the
transforms before calling the original). No need to tell characters apart
by bone count, as the CTO mod does at the output node.

- **Node sample function**: `animAnimNode_Output` (instance vtable
  `0x1430C1160`) has it at vtable **+0x128** (`0x1403AACCC`), as CTO found:
  `Sample(node, ctx, instanceData, out)`. `out[0]` is the pose, `out[1]` a
  root-motion transform. `ctx` is a **per-call struct on the stack** (bone
  counts by LOD first, a "reference pose only" byte at +0x34, a pointer to
  the rig at +0x40): it does not identify the entity; `instanceData` does
  (`== [GI+0x28]`). With no input the node writes the rig's reference pose
  (`0x1403B1D5C(pose, rig)`). 255 node classes share the slot.
  [2.5-static; the +0x30 pointer contradicts "bones at +0x30" in
  pose-streaming.md's fourth pass: check which when the hook runs]

### 13.3b The whole per-entity animation job (second pass, 2026-11-18)

- **`0x1401B3E00(obj, entityParams)`** (hash 2251364700, hooked by tandem in
  game 2026-11-18: [2.5-verified]; the pose is complete model space when it
  returns) is the job for one entity: `obj` is the animated object of 13.2 (`[root component+0x178]`).
  It walks the entity's graph instances, a linked list from `[obj+0x60]`
  through `[GI+0x08]` (parent instance at `[GI+0x10]`), builds each one's
  parameters (skeleton at +0x28, bone mapping at +0x30 =
  `[skeleton+0x68] + index * 0x80`, pose at +0x38) and calls
  **`0x1401B5AF4(GI, params)`** (hash 1991448876), which runs
  `0x1401B25DC` (update, sample, commit). When its loop ends it calls
  **`0x140582B3C(pose, skeleton)`** (hash 1343032985): the local → model
  space conversion, in place, parents first (the short local-space window
  seen live in 13.2). [2.5-static]
- So three hook points, by need: the commit (local space, per graph
  instance, before conversion); **after `0x1401B3E00` returns** (the
  entity's complete model-space pose, identity = `obj`, nothing half
  written: the simplest place to _read_ V's pose and a candidate to _write_
  a puppet's, if nothing reads the pose in between); the conversion itself.
  [2.5-static]
- **`GI+0x25C8` (byte)**: when set, `0x1401B5AF4` does not run the graph: it
  commits the rig's reference pose (`0x142741E44(pose, rig)`). A way to
  silence a puppet's own graph while its pose is driven from outside; who
  sets it was not looked for. Other fields: `GI+0x25CA` (int16) the bone of
  the merged skeleton this instance hangs from (-1: none), `GI+0x25B1` a
  "skipped, needs re-init" flag, `GI+0x2490 / +0x24C0 / +0x24F0` transforms
  copied from the parameters each tick. [2.5-static]
- **V's first-person body is a complete body.** Model-space read of the
  local V standing (z up, metres): Hips 0.95, Spine3 1.33, Head 1.64,
  hands 0.96, knees 0.52, feet 0.09. The `shadow_*` copies equal the main
  bones for limbs and differ by ≈5 cm in the spine, neck, head and
  shoulders (the first-person camera pull): for other players, take
  `shadow_*` where it exists, the main bone elsewhere. [2.5-verified]
- A component's world transform (+0xE0) stores its position as three
  32-bit fixed-point numbers, 1/131072 m (`WorldPosition`), then the
  rotation quaternion at +0xF0. [2.5-verified]
