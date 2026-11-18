# Pose streaming: the plan for player-only moves

Why: a remote V must show everything a player does (crouch, slide, vault,
dodge, climb, weapon handling). The NPC graph has no such animations, the
player's gameplay graph cannot run on another body (different rig, crash),
and mirroring anim features does nothing on the cutscene template's graph
(docs/research/engine-knowledge.md §12.1). What is left reproduces the
result instead of the inputs: the sender's final pose, written on the
puppet where the animation graph outputs it (what the CTO mod does to V).

## Steps

1. **Find the pose in memory.** `entAnimatedComponent` (0x2B0) has unmapped
   regions at 0x168–0x180, 0x1B8–0x1F8, 0x200–0x260, 0x298–0x2B0. On the
   local player and on a puppet, dump them each frame and look for a pointer
   to an array of QsTransforms (translation, rotation, scale: 48 bytes) whose
   length is the rig's bone count (`anim::Rig`, SDK `animRig.hpp`) and whose
   values change when the body moves. A probe native + `task cmd`.
2. **Read**: copy the local player's model-space bone transforms after the
   graph ran (animation thread: find the "pose ready" point, or read from the
   skinning job input). Map bones by name through both rigs' bone name lists
   (first-person player rig 7304775167351189356 → third-person rig
   14264052059588999261 / 10037276762472186253).
3. **Write**: on the puppet, overwrite the graph's output with the received
   pose before skinning (the hook CTO calls "output node"), or drive a graph
   node made for it (`anim::SetBoneTransform…`, `StackTransformsExtender`,
   `AnimNodeSourceChannel_AnimFeatureQsTransform` exist in RTTI).
4. **Wire format**: quantised rotations for ~100 body bones (smallest-three,
   6 bytes each), root from PlayerState; delta against the previous pose;
   sent on the state channel at 30 Hz, interpolated (slerp) on the receiver
   with the same clock as positions.
5. **Fallback** stays the grafted NPC locomotion (walk, sprint) when no pose
   stream arrives (mock players, bad links).

## Unknowns to settle first

- Whether the first-person player entity computes a full-body pose every
  frame (its shadow says yes) and in which buffer.
- Thread and timing of the write so the engine does not overwrite it.
- Facial bones and physics-driven bones (hair, cloth): leave to the puppet.

## Measured so far (2026-11-17, game 2.50)

Tools: `task cmd -- pose` (dumps the root animated component's unmapped
slots) and `task cmd -- pose scan` (follows readable pointers three levels
down and reports runs of ≥20 bone-like transforms), on the local V and on
every puppet. Output in tandem's log.

- The live component's first fields match the SDK: rig reference at +0x138
  (path hash, then the token at +0x140), graph at +0x150.
- **Rig**: `root+0x140 → +0x28` is the `anim::Rig`; its `referencePoseLS`
  (+0x48) and `referencePoseMS` (+0x60) read as 152 transforms for the
  first-person player rig (hips, bone 5, at height 0.961 in model space).
  Bone names are `Rig::boneNames` (+0x50): the name mapping between rigs is
  available.
- `root+0x168` leads into a **shared pose pool** (arrays of 512+ contiguous
  transforms, other characters' poses among them, paths that move between
  two scans): pointer-walking finds pose buffers but not which one is this
  entity's output. `root+0x168 → +0x80 → +0x108 / +0x118` looked like V's
  local / model pose (hips at 0.951) but did not change when V crouched.
- Objects with vtables in the game image at +0x168, +0x170, +0x250, +0x288;
  arrays at +0x210 (42 entries) and +0x230 (4).

## Next

- Identify the entity's own pose by behaviour, not by walking: sample the
  candidates each frame while V crouches and stands, keep the ones whose
  hips height follows; or find the pose from the skinned mesh side
  (`entSkinnedMeshComponent` → skinning matrices), which is per entity.
- Then the write side on a puppet, and the hook point after the graph.

## Found 2026-11-17 (second pass)

- `task cmd -- pose mark` / `pose diff` (mark hips-height transforms while V
  stands, re-read after V crouches): the array reached through
  `root+0x168 → +0x310 → +0x318` holds V's **model-space pose**: entries 26,
  37, 49, 59, 60, 68, 69 (WeaponLeft and hand bones by the rig's name list)
  went from 0.87–0.94 m to 0.57–0.61 m. A second copy sits ~459 entries
  further (the other frame's buffer). The path through +0x168 is the graph
  instance and moves between runs: the stable reference is still to find.
- `task cmd -- pose bones`: bone names by index, from `Rig::boneNames`
  (rig = token at component +0x140, +0x28; names +0x50, count +0x5C). The
  first-person player rig has **152 bones**, the third-person body
  (`player_ma_tpp_cutscene.ent`) **71**: Root, Trajectory, Hips,
  reference_joint, Spine, LeftUpLeg… Lists in
  `docs/research/data/rig-bones-2.50.txt`. The 71 are mapped by name.

## Next

1. A stable way from an entity to its output pose buffer (and the puppet's).
2. In one game: copy V's pose into the puppet's buffer each frame, by bone
   name. If the puppet crouches with V, the write side is proven.
3. Then the wire format and interpolation.

## Found 2026-11-17 (third pass)

- `task cmd -- pose tree`: the objects at component +0x168 (vtable
  0x1430a86d8) and +0x170 (vtable 0x14319a980) are **the same addresses for
  the local V and for a puppet**: global animation-system objects, not per
  entity. Poses of every character live in their pools; the component holds
  a handle into them, not a pointer to its pose. +0x250 points back into the
  component (a secondary base), +0x288 at an object of its own (vtable
  0x14360bb58).
- No script or RTTI native answers a bone transform (only
  `SlotComponent.GetSlotTransform` for named slots).

## Next (needs a disassembler)

- Ghidra on Cyberpunk2077.exe (2.50): from the two vtables above, find the
  function that resolves a component's pose in the pool (the skinning or
  slot-transform code calls it: `GetSlotTransform` is a small, findable
  entry), and the point where the graph's output is committed. Hook there:
  read for the local V, write for puppets.

## Found 2026-11-17 (fourth pass): the hook point, without a disassembler on the exe

Source: how the CTO plugin (v1.0.2, game 2.5) finds the same point, read from
its strings and the disassembly of its hooking routine (`objdump`); facts
only. The four MIT mods checked (Codeware, ArchiveXL, RedHotTools, CET) do
not touch poses, and the address library names only 84 symbols, none about
animation.

- Every graph ends in an **`animAnimNode_Output`** (RTTI size 0x60). Its
  "sample" function is the **vtable slot at +0x128**; the vtable is read
  from an instance created through RTTI, so nothing is addressed by offset
  into the exe. [CTO-2.5, to verify]
- `void Sample(node, a2, a3, out)`; after the original ran, `*out` points
  to the pose: **bone count (u32) at +0x00, bones at +0x30** (48-byte
  transforms in **parent space**). V's first-person rig arrives with 152
  bones, a second rig of V's with 183 (CTO's "deformations rig"). CTO reads
  a u32 at node +0x40 around the call. [CTO-2.5, to verify]
- The function runs several times per character and frame (sub-graphs,
  repeat passes) on animation job threads; CTO recognises V by bone count
  and one bone's length, and rewrites bones in place.
- Not answered by CTO: which argument identifies the **entity** (it only
  ever touches V). Needed to tell a puppet from the NPCs sharing its rig.

In the plugin: `engine/pose.cpp`. Commands: `task cmd -- pose hook`,
`pose report` (calls, distinct arguments and buffers, hips height per bone
count), `pose watch [3]` (searches the arguments of the next calls for
pointers to V's and the puppets' entity and components), `pose copy` /
`pose copy off` (V's rotations and hips offset written, by bone name, on
every pose of the first puppet's rig: NPCs of that rig move too, it is an
experiment).

## Next

1. In game: `pose hook`, `pose report` while V stands then crouches (the
   152-bone hips must follow), `pose watch`, then `pose copy` and look at
   the puppet.
2. Entity identification from the watch's hits; write only on puppets.
3. Wire format and interpolation (step 4 above).

## Found 2026-11-17 (fifth pass): the entity's pose and the commit point

Read-only reverse engineering of the running game and of the exe
(`tools/re/`); details, offsets and hashes in
`engine-knowledge.md` §13.2–13.3.

- **The stable path the earlier passes lacked**:
  `[root component+0x178]` → the entity's animated object → `+0x18` → pose →
  `+0x00` → transforms (48 bytes, model space, indexed like the merged
  skeleton at `[object+0x08]`, whose bone names are at `+0x20`). Same
  addresses every frame; they change only when the skeleton is rebuilt
  (appearance or equipment change). The "shared pool" seen through +0x168 was
  the animation system itself.
- **The commit**: `0x1401B3C08(GI, params, pose, lod)` (hash 2074943978)
  copies a graph's sampled local-space pose into the entity's pose, once per
  graph instance and frame, on a job thread. `GI == [component+0x270]`
  names the character. One hook there replaces both the output-node hook
  and the search for the entity (steps 2 and 3 of the plan).
- Corrections to the fourth pass: the sample function's second argument is a
  per-call struct on the stack (it cannot identify the entity; the third
  argument, `[GI+0x28]`, can), and the pose holds a **pointer** to its
  transforms at +0x30 according to the engine's own code.
- The merged skeletons (V: 619 transforms, 152 from the first-person rig;
  NPCs on V's body rigs: 512 / 533, 71 from the rig) are in
  `data/skeletons-2.50.txt`: the name mapping of step 2 can be built from
  them without the game.

## Next

1. In `engine/pose.cpp`: hook the commit by hash. Log, for the local V and
   one puppet, `GI`, the pose's bone count and the hips' local transform:
   confirms the signature, the local-space claim and the +0x30 pointer
   (a startup check: bone count == the rig's, hips translation ≈ 0.95 m up).
2. Read: copy V's pose in the hook (it is complete and consistent there;
   reading the entity's pose from the game thread sees a half-converted
   buffer ≈3 % of the time).
3. Write: in the hook, when `GI` is a puppet's, overwrite the transforms by
   bone name before the original runs. Check the two other callers of the
   commit (`0x1401B5AF4`, `0x142430540`) when a puppet is far or not updated.
4. First-person V has no usable legs or spine pose for others (camera rig):
   compare with what the third-person body needs; the `shadow_*` bones of
   V's skeleton are a candidate source. Then the wire format (step 4 of the
   plan).

## Found 2026-11-18 (sixth pass): what a remote V needs to look right

Read-only, on the running game (`tools/re/`, `engine-knowledge.md` §13.3b).

**The job, end to end** (one per entity and frame, on a job thread):
`0x1401B3E00(animated object)` → for each graph instance of the entity, in
this order — V: `root`, `deformations`, `shadow`, `face_rig`; an NPC: `root`,
`face_rig`, `deformations` — update, sample and commit (local space) →
`0x140582B3C(pose, skeleton)` converts the entity's pose to model space.
The root instance is always first: what is written into its pose at the
commit is what the deformation (twist and muscle bones), shadow and face
graphs of the same frame build on.

**Source: V's own pose is a full body.** In first person the root graph
still animates legs, spine, arms, fingers and head (hips 0.95 m, head 1.64 m
standing; 0.35 m hips seated or crouched low). All 71 bones of the
third-person body rig exist in V's 152-bone rig under the same names, with
the same parents. The `shadow_*` bones (26, the body's main chain, no
fingers) are the same pose without the first-person camera pull (≈5 cm on
spine, neck, head, shoulders).

**The two bodies are not the same size.** Reference offsets, V's
first-person rig → the third-person rig (male): hips height 0.961 → 1.013 m,
shoulder to arm 0.174 → 0.136, forearm 0.290 → 0.271, lower leg 0.443 →
0.431, spine 0.103 → 0.071, neck 0.159 → 0.145. The shadow bones have the
first-person proportions. Reference _rotations_ differ too (T-pose against
a relaxed pose: elbows and fingers bent), which does not matter: a pose's
local rotations are absolute, not relative to the reference. So:

- send **local rotations** per bone (computed from the model-space pose and
  the sender's hierarchy, `shadow_*` where it exists) and the **hips
  translation**;
- on the puppet keep the receiving rig's own bone offsets, apply the
  rotations, scale the hips height by the rigs' ratio (1.013 / 0.961 for a
  male V) so the feet stay on the ground;
- hands end up ≈2–4 cm from where the sender's are (shorter arms): two-handed
  grips need a small correction (two-bone IK to the weapon's grip slot)
  once weapons are shown.

**Destination: write at the root instance's commit**, in local space, so
the puppet's own deformation and face graphs follow. Respect the commit's
`lod`: far characters commit fewer bones (the mapping's count is per LOD).
`Root` and `Trajectory` stay identity: the entity is placed by the movement
controller.

**Keep the puppet's graph running, or silence it?** `GI+0x25C8` (byte, set
only to 0 by the instance's constructor at `0x140223B38`: nothing in the
game sets it) makes the tick commit the reference pose without running the
graph. Cheap, but a silent graph raises no animation events: no footstep
sounds, no foot-phase data. Recommendation: keep the grafted NPC locomotion
graph running (it gives footsteps at the right speed for free), overwrite
its result at the commit, and fall back to it untouched when no pose stream
arrives (mock players, bad links).

**Rebuilds.** The skeleton and pose objects of an entity are replaced when
its parts change (seen on V: 677 → 619 → 618 transforms across equipment and
vehicle changes); the animated object and the graph instances stay. Cache
bone indices against the skeleton's signature (`[skeleton+0x60]`) and remap
by name when it changes; never keep a pose pointer across frames.

**Reading without a hook** (what the recorder does): two reads in a row
that agree, and no probe bone sitting at its reference offset, give a clean
model-space pose ≈50 times a second from outside the process. Inside the
plugin, read after `0x1401B3E00` returned for V's animated object instead.

**Data for offline work**: `tools/re/record_pose.py` writes
`recordings/pose-NAME.{json,bin}` (not in git): V's model-space pose for the
71 body bones and the 26 shadow bones at up to 50 Hz, with V's world
transform, the skeleton (names, parents, reference) and the reference poses
of the two third-person body rigs. Enough to build and test the retarget,
the quantisation (smallest-three quaternions) and the interpolation without
the game.

## Next (replaces the earlier lists)

1. Offline, from a recording: model space → local rotations on the
   third-person hierarchy; measure the error of 30 Hz + slerp against the
   50 Hz original, and the packet size (71 rotations × 6 bytes + hips).
2. In the plugin: hook the commit by hash with a startup check (the first
   call for V's root instance has 152 bones and a hips translation near
   0.95 m); read V after the entity job; write the first puppet from V's own
   pose in the same game (a mirror with no network) and look at it.
3. Then through the network path with the mock player's mirror mode.
4. Weapons in hand, grip correction, and the events a pose cannot carry
   (shots, reload sounds, footsteps policy): `coop-player-vehicles.md`.

## Measured offline 2026-11-18 (`tools/re/pose_codec.py`)

On `recordings/pose-play` (7 minutes of normal play: 18 407 frames at
≈44 Hz, 425 m walked and run, standing, crouched and seated), V's pose
retargeted to the male third-person rig as proposed above:

| Send rate | Rotation size        | Packet    | Rate      | Error after slerp: mean / 99th |
| --------- | -------------------- | --------- | --------- | ------------------------------ |
| 30 Hz     | 6 bytes (3 × 15 bit) | 485 bytes | 14.6 kB/s | 1.6 mm / 26 mm                 |
| 30 Hz     | 4 bytes (3 × 10 bit) | 352 bytes | 10.6 kB/s | 2.5 mm / 26 mm                 |
| 20 Hz     | 6 bytes              | 485 bytes | 9.7 kB/s  | 2.8 mm / 38 mm                 |

(Error = distance between a bone drawn from the interpolated stream and
from the original frames, over all 71 bones; quantisation alone is 0.2 mm
at 15 bits, 5.7 mm at 10. The few outliers up to 0.8 m are gaps in the
recording and instant pose changes, where any interpolation is wrong for
one frame.) 60 of the 71 bones move at all; the others need not be sent.
One packet fits a single datagram; delta coding is not needed to start.

Cost of the different proportions: with the same rotations, the puppet's
hands are **10.7 cm (mean) from the sender's**, up to 33 cm with the arms
stretched; feet 4 cm, head 5 cm. Fine for locomotion and gestures; a held
weapon needs the hands pulled onto its grip slots (two-bone IK), or the
second hand floats.

## Implemented 2026-11-18

- `core/include/tandem/pose.hpp`, `core/src/pose.cpp`: the 71-bone body
  table (generated from `data/skeletons-2.50.txt`), model → local, 15-bit
  smallest-three quaternions, `PoseTrack` (slerp between the two poses around
  the render time, hold past the newest). Message `PoseState` (protocol 3):
  hips (3 × int16, 1/8192 of the reference hips height) and 68 rotations of 6
  bytes, 431 bytes encoded, State channel. The endpoint relays it like
  `PlayerState` and stamps the sender's id. Tests: `tests/pose_test.cpp`.
- `plugin/src/engine/posestream.cpp`: hooks the entity job (hash 2251364700:
  reads the local V after it, 30 Hz, `shadow_*` bones preferred) and the
  commit (hash 2074943978: writes the remote rotations and the scaled hips
  into a puppet's root instance's sampled pose before the copy). Checks: the
  component and rig RTTI sizes; the source skeleton has every body bone; the
  puppet rig maps all 71; the first sampled pose has the rig's bone count and
  is in local space (LeftHand a forearm's length from its parent). Any
  failure turns the side off and logs why. `tandem.ini pose = off` keeps the
  hooks out.
- Scripts bind the local V and each puppet twice a second
  (`TandemNative.PoseSource/PoseBind`). Commands: `pose stream` (counters),
  `pose loop` / `pose loop off` (our own pose on the first puppet, no
  network: tests read and write in one game).
- `tandem-peer` echoes poses in `mirror` mode and plays a recording with
  `--pose recordings/pose-play.tpose` (`tools/re/pose_export.py`). Checked
  between two peers on Linux: 30 poses/s each way.
- The remote pose is drawn with the same delay as the player's position
  (`Track::DelayUs`), so body and placement stay in step.

## Verified in game 2026-11-18 (anya, game 2.50, host + `tandem-peer`)

- Hooks attach by hash at load; V's skeleton maps all 71 body bones
  (shadow_ preferred); the male puppet's rig maps 71 of 71, reference hips
  0.951 m.
- **The commit's sampled pose is local space**: LeftHand 0.275 m from its
  parent, the forearm's reference length exactly. Bone count 71 at full
  detail.
- Rates over ~20 s: ≈47 reads/s of V (throttled to 30 sends/s, the peer
  heard ≈28/s), ≈84 writes/s on the puppet (its root graph commits every
  frame). No skipped reads, no crash.
- Seen on screen: the puppet takes the recorded poses (standing, arm
  gestures, a hand at the face), coherent body, feet on the ground; with
  `pose loop` it stands in V's live idle pose. (Clothes were missing that
  session; solved later the same day.)
- Log kept locally: `.cache/re/pose-test-2026-11-18.log`.

Third session: the puppet is now **dressed** (V's clothes, face and hair;
`puppet.tweak` gender entry, engine-knowledge.md §13.4) and shows V's live
pose.

Second session (same day): with `tandem-peer --mode front` echoing V's pose
(the puppet stays 3 m in front of V), **V crouching made the puppet crouch**
on screen, and the puppet kept V's live idle pose otherwise. Walking V by
simulated keys did not work on anya (`tools/game.sh hold 2 17` opened the
radio menu instead: the keyboard layout or bindings differ), so live walking
still needs a person at the controls.

## Still to check in game

1. With someone playing: `--mode mirror` while V walks, runs, crouches,
   jumps, and the delay against the puppet's movement (body vs placement).
2. Whether the first-person arm poses read oddly from outside (V's arms are
   animated for the camera: raised gestures, phone, eating) — if so, blend
   the arm chain toward the puppet's own graph when V's arms are in a
   first-person-only state.
3. Foot sliding: the body bones are overwritten, so the grafted locomotion
   only adds footstep events; compare feet to the ground while walking.
4. A female V on a female puppet (same table, other rig).

## Original in-game test plan

1. `task deploy`, launch; `tools/game.sh peer join … --mode front --pose
recordings/pose-play.tpose`: the puppet in front walks, crouches and sits
   as V did. Read the log: "puppet rig … 71 of 71", "sampled pose … local
   space, writes on". `tools/game.sh cmd pose stream` for counters.
2. `pose loop`: the puppet copies V live, in the same game.
3. `--mode mirror` (no `--pose`): the same through the network.
4. If the puppet's legs fight the grafted NPC locomotion (foot sliding when
   the controller moves it): the locomotion graph's result is fully
   overwritten for the body bones, so it should not; check feet against the
   ground when walking.
