# How a scene drives its actors (game 2.50), for scene replay

Engine facts behind [`docs/architecture/scene-replay.md`](../architecture/scene-replay.md):
how a story scene starts, what in a scene resource has side effects, how
its voice-over, lipsync, animation and props reach an actor, how Johnny
shows, and what of that a guest's game can reproduce without running the
scene. It extends [`coop-scenes.md`](coop-scenes.md) (tiers, the script
interface, subtitles, choices) and does not repeat it.

**Read offline on 2026-11-19. Nothing here was run in game.**

**Sources**

- `reds:LINE` — the decompiled vanilla scripts of 2.50
  (`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`).
- `SDK/…` — RED4ext.SDK's generated headers
  (`vendor/RED4ext.SDK/include/RED4ext/Scripting/Natives/Generated/…`): the
  game's own reflection data, field offsets in the comments.
- `natives`, `vtables` — `docs/research/data/rtti-{natives,vtables}-2.50.tsv`
  (the live RTTI dump of 2.50).
- `CW/…` — Codeware (MIT), `~/.cache/tandem/ref/codeware`.
- `wiki/…` — the modding wiki, `~/.cache/tandem/ref/moddocs`.
- `skeletons` — `docs/research/data/skeletons-2.50.txt` (live entities).

**Tags**: [2.5-rtti] = in the 2.50 game's reflection data (a name, a field,
an offset: it exists; what it does is not proven); [2.5-untested] = read
from the 2.50 scripts or Codeware's, not yet run in game; [wiki] = stated
by the modding wiki for the current game, not checked here; [inferred] = a
deduction; [verified-2.5] = measured in game by tandem on 2.50 (only what
`pose-streaming.md` already verified); [unknown] = looked for, not found.

## Contents

1. [Starting a scene](#1-starting-a-scene)
2. [What a scene resource does besides showing](#2-what-a-scene-resource-does-besides-showing)
3. [Voice-over](#3-voice-over)
4. [Lipsync and faces](#4-lipsync-and-faces)
5. [Body animation](#5-body-animation)
6. [Props, effects, cameras](#6-props-effects-cameras)
7. [Johnny](#7-johnny)
8. [The two approaches against these facts](#8-the-two-approaches-against-these-facts)
9. [To check in game, in this order](#9-to-check-in-game-in-this-order)

## 1. Starting a scene

- **No script or RTTI function starts a scene.** `scnISceneSystem` has 2
  natives, `scnScriptInterface` 24: queries, fast-forward, the camera, the
  rewindable (braindance) section (`natives:13252-13277`). [2.5-rtti]
- The SDK knows nothing inside `scnSceneSystem` (size 0x10d0, vtable
  0x1430f7d48): no members, no virtuals, no `scnSceneInstance` class, only
  the id `scnSceneInstanceId { sceneId +0, ownerId +8, internalId +0x10,
hash +0x18 }` (`SDK/scn/SceneSystem.hpp`, `SceneInstanceId.hpp`).
  Driving a running scene from outside (its section, its time) would be
  raw reverse engineering of that system. [2.5-rtti]
- The one entry is the quest node `questSceneNodeDefinition { sceneFile
+0x48, sceneLocation +0x50, interruptionOperations +0x68, syncToMusic
+0x78, notAllowedToBeFrozen +0x79 }` (`SDK/quest/SceneNodeDefinition.hpp`),
  run by `QuestPhaseInstance::ExecuteNode` (hash 3227858325, the function
  tandem's quest freeze already hooks: engine-knowledge §13.15). [2.5-rtti]
- A scene can be staged on the player instead of a world marker:
  `sceneLocation { type: Tag, tag: around_player }`
  (`wiki/modding-guides/quest/johnny-glitching-and-talking-with-v.md:57-67`).
  [wiki]
- **Outside a quest phase**: Codeware's `QuestsSystem.ExecuteNode(node,
opt inputSocket)` runs one node on the root phase
  (`CW/scripts/Quest/QuestsSystem.reds`, `CW/src/App/Quest/QuestsSystemEx.hpp`),
  and Codeware imports `questSceneNodeDefinition` with a writable
  `sceneFile` (`CW/scripts/Base/Imports/questSceneNodeDefinition.reds`). So a
  script can build a scene node and ask for it to run. Codeware itself only
  does it for world-state nodes (`CW/src/App/World/WorldStateSystem.cpp`);
  **a scene started this way is unproven**, and no public mod or source
  read here does it (mods add scenes statically: a `.questphase` registered
  through ArchiveXL). [2.5-untested / unknown]
- CDPR's multiplayer leftovers are still in the reflection data:
  `scnPlayerActorDef.acquisitionPlan +0xA8` with `findNetworkPlayerParams {
networkId: Uint32 } +0xAC` (a scene could cast "network player N" as its
  V), `scnSceneSharedState { entrypoint, syncNodesVisited, instanceHash,
finishedOnClient, finishedOnServer }`, `scnChoiceNode
.cpoHoldInputActionSection`, `questMultiplayerJunctionDialogNodeDefinition`
  (`SDK/scn/PlayerActorDef.hpp:48-50`, `SceneSharedState.hpp`). Whether any
  of it runs in the retail game is [unknown]: `coop-scenes.md` §3.4 found
  the script half always answering "single player".

## 2. What a scene resource does besides showing

A scene is not a film: the story's logic runs through it.

- Graph nodes (`vtables:6157-6271`): Start, End, Section, RewindableSection,
  Choice, **Quest**, Xor, And, Hub, Randomizer, CutControl, FlowControl,
  InterruptManager, DeletionMarker. [2.5-rtti]
- **`scnQuestNode { questNode: Handle<questNodeDefinition> +0x48,
isockMappings, osockMappings }`** embeds any quest node in the scene: facts,
  journal, tiers, spawns, teleports (`SDK/scn/QuestNode.hpp`). [2.5-rtti]
- `scnChoiceNode` waits for the local player's input and holds quest
  conditions; `scnInterruptManagerNode` reads facts, combat and distances.
  [2.5-rtti]
- Timeline events that change the game, not the picture (`SDK/scn/…`,
  `SDK/scn/events/…`): `scneventsSocket { osockStamp }` (fires an output of
  the scene node mid-section: the quest goes on), `scnGameplayTransitionEvent`,
  `scnGameplayActionEvent`, `scneventsMountEvent`, `scnUnmountEvent`,
  `scneventsEquipItemToPerformer` (+ two unequips), `scneventsSpawnEntityEvent`,
  `scneventsDespawnEntityEvent`, `scneventsRagdollEvent`, `scnPlacementEvent`,
  `scnWalkToEvent`, `scneventsClueEvent { factName }`. [2.5-rtti]
- **Its V is the local player.** `scnPlayerActorDef` binds the scene's V to
  the game's own player (or to a "network player" id, unproven, §1):
  `scnPlayFPPControlAnimEvent` takes the local camera and controls, the
  tier nodes put the local V in a workspot. [2.5-rtti; the consequence
  inferred]
- **Its actors are found by the scene.** `scnActorDef.acquisitionPlan
+0x10`: `findInContext`, `findInWorld` (an entity reference), `findInNode`
  (a NodeRef), `community`, `spawner` bind entities of the world;
  `spawnDespawn` and `spawnSet` spawn their own (`SDK/scn/ActorDef.hpp`,
  `SpawnDespawnEntityParams.hpp`). No plan takes "this entity I give you".
  [2.5-rtti]

## 3. Voice-over

- A line is named by its **string id**: `scnscreenplayDialogLine {
locstringId +0x10 }` in the scene's `screenplayStore`; the subtitle entry
  and the voice-over map (`locVoiceoverMap`: `{ stringId, femaleResPath,
maleResPath }` → `.wem`) use the same id, per language
  (`SDK/scn/screenplay/DialogLine.hpp`;
  `wiki/modding-guides/quest/adding-a-custom-voiceline-to-a-scene.md:191-216`).
  So a line's id is the same in every copy of the game, and each game has
  its own language's sound for it. [2.5-rtti, wiki]
- **An event that names a line and is sent to an entity exists**:
  `gameaudioeventsDialogLine : redEvent` (size 0x68, vtable 0x14309a7f0),
  `data: audioDialogLineEventData { stringId: CRUID +0, context:
locVoiceoverContext +8, expression: locVoiceoverExpression +9, isPlayer
+0x10, isRewind +0x11, isHolocall +0x12, customVoEvent: CName +0x18,
seekTime: Float +0x20, playbackSpeedParameter: Float +0x24 }`
  (`SDK/audio/DialogLineEventData.hpp`, `SDK/game/audio/events/DialogLine.hpp`,
  `vtables:2948`). Its fields are a scene line's `scnDialogLineVoParams`
  (context, expression, custom event, holocall speaker) plus a seek time
  and a rewind flag: it is very likely what the scene sends the speaker for
  each `scnDialogLineEvent`. [2.5-rtti; the role inferred]
- With it: `gameaudioeventsStopDialogLine { stringId +0x40, fadeOut +0x48 }`
  and `gameaudioeventsDialogLineEnd`. [2.5-rtti]
- Codeware gives scripts the three: `DialogLineEvent` (`data:
audioDialogLineEventData`), `gameaudioeventsStopDialogLine`,
  `gameaudioeventsDialogLineEnd`, the enums `locVoiceoverContext` (Quest 0,
  Community 1, Combat 2, Minor_Activity 3, Default 5) and
  `locVoiceoverExpression` (Spoken 0, Phone 1, InnerDialog 2, Loudspeaker
  3–5, Radio 6, GlobalTV 7, Cb_Radio 8, Cyberspace 9, Possessed 10, Helmet 11) (`CW/scripts/Base/Imports/DialogLineEvent.reds`,
  `audioDialogLineEventData.reds`, …). tandem's scripts compile against
  them (`task scripts:check`). [2.5-untested]
- Codeware adds a field the reflection data hides: `gender: Int32 +0xC`
  (`CW/src/App/Audio/audioDialogLineEventData.hpp`): which of the two
  recordings (`femaleResPath`, `maleResPath`) plays. Its values are
  [unknown]; without it the game decides (for V: the local V's voice,
  [inferred]).
- **No vanilla script creates the event, and no native takes a line id**
  (grep of `reds` and `natives`). What vanilla scripts can play is a
  voiceset bark by name, the engine picking the variation:
  `GameObject.PlayVoiceOver` → `SoundPlayVo` (`reds:12047-12075`), and
  `entTriggerVOEvent` (unused by scripts). Not a scene's line. [2.5-untested]
- **Unknown, and the first thing to test**: whether queueing
  `gameaudioeventsDialogLine` on an NPC (or on a remote player's puppet)
  outside a scene plays the localized voice-over in 3D from it, and
  whether the game then shows the line's subtitle itself.
- The host's side has no "line X began on entity Y" callback either. The
  subtitle is the signal: `UIGameData.ShowDialogLine` carries
  `scnDialogLineData { id: CRUID, speaker, duration, type }`
  (`coop-scenes.md` §4.1). That `id` **is taken to be the string id**
  [inferred: the hide message names lines by the same CRUID, and the
  subtitle text is looked up by string id]; to confirm in game (log it,
  compare with a known line's id).

## 4. Lipsync and faces

- **Lipsync is an animation, not a by-product of the sound.** Each line
  names two clips, `maleLipsyncAnimationName +0x18` and
  `femaleLipsyncAnimationName +0x20` (`SDK/scn/screenplay/DialogLine.hpp`),
  named `m_/f_<the string id in hex>`
  (`wiki/modding-guides/quest/generating-vanilla-lipsync-animation-sets.md:62-67`).
  [2.5-rtti, wiki]
- The clips live in per-language sets found through `animLipsyncMapping {
languageCodeName +0x40, scenePaths +0x48, sceneEntries { actorVoiceTags,
animSets } +0x58 }` (`.lipmap`), by scene path and the actor's voicetag;
  the scene attaches them to the actor (`scnActorDef.lipsyncAnimSet
+0x1A8`). `scnSceneSystemGlobalSettings.syncLipsyncToSceneTime` says the
  clip is clocked by the scene's time. V has no lipsync
  (`SDK/anim/LipsyncMapping.hpp`, `SDK/scn/SceneSystemGlobalSettings.hpp`;
  wiki same page `:53`, `:112-122`). [2.5-rtti, wiki]
- **No named way to play a line's lipsync on an entity outside a scene.**
  `entSceneAnimSetEvent` (0x58, no reflected field) is probably how sets
  are attached; `Localization_LoadLipsyncs` is an address hash only
  (1488657506, `CW/src/Red/Shared/AddressLib.hpp`). Scripts reach faces
  only through reaction presets (`AnimFeature_FacialReaction`,
  `reds:193270`). [2.5-rtti / unknown]
- **What a face is, as data**: an NPC's merged skeleton has 512 transforms
  and **427 float tracks**, among them `lipSyncEnvelope`, `jaliJaw`,
  `jaliLips`, and the facial poses (`lips_l_corner_up`, `eye_l_blink`…)
  (`skeletons`, the man base rig). An NPC's entity job runs three graph
  instances in order: `root`, `face_rig`, `deformations`
  (engine-knowledge §13.3b). The face is the `face_rig` instance's sampled
  pose: mostly tracks, few bones. [verified-2.5 for the lists; where the
  tracks sit in the sampled pose is unknown]

## 5. Body animation

- Scenes animate actors with timeline events: `scnPlaySkAnimEvent
{ animName +0xF8, rootMotionData +0x120, playerData +0x1E0 }`,
  `scnPlayAnimEvent`, `scnChangeIdleAnimEvent`, `scnLookAtEvent`,
  `scnIKEvent`, `scnPoseCorrectionEvent`, `scneventsSetAnimFeatureEvent`,
  from anim sets the scene registers on the actor (`scnActorDef` anim set
  ids into `resouresReferences`) (`SDK/scn/…`;
  `wiki/modding-guides/quest/adding-animations.md:73-80`). [2.5-rtti, wiki]
- From scripts, on an NPC outside a scene: `AnimationControllerComponent`
  drives graph inputs only (no clip by name, `reds:57739-57760`);
  `AIRootMotionCommand` with `SceneAnimationMotionActionParams
{ animationName, placementMode, startTime, endTime… }` plays a named
  clip with root motion (`reds:294284`, `:458635`) but needs a working AI
  brain (a proxy has none) and the clip in the entity's sets (the scene's
  are not there); a workspot's `SendJumpToAnimEnt` plays a clip of the
  workspot it is in. None replays a scene's animation. [2.5-untested]
- **The result can be copied instead of the cause.** The engine's
  per-entity animation job and the pose commit are hooked by tandem
  already (`plugin/src/engine/posestream.cpp`; hashes 2251364700 and
  2074943978): a pose read after the job is complete, a pose written into a
  root graph instance's sampled pose at the commit is what the entity
  shows, with its own deformation and face graphs following. Verified in
  game on 2.50 for V → a remote player's puppet. [verified-2.5]
- **A puppet's body is an NPC's body.** The man base rig
  (14264052059588999261) and the woman base rig (10037276762472186253) are
  the rigs V's third-person body uses; the 71 bones of the wire pose
  (`core/include/tandem/pose.hpp`) are theirs, by name (`skeletons`,
  `pose-streaming.md`). So the same read, the same wire pose and the same
  write apply between an NPC of the host's game and its proxy on a
  guest's, which is spawned from the same record and appearance: the same
  rig, no retargeting. [inferred from verified parts; the NPC case itself
  is not yet run]
- Other humanoid rigs (big, fat, massive, children) carry the same bone
  names [inferred]; a skeleton that lacks one is not read (the plugin logs
  it) and its proxy keeps its own animation. Non-humanoid actors (drones,
  robots, animals) are out.

## 6. Props, effects, cameras

- Props are scene entities (`scnPropDef`, the same acquisition plans as
  actors) attached by events: `scneventsAttachPropToPerformer`,
  `…ToWorld`, `…ToNode`. A prop the scene **finds** in the world exists on
  the guest too (a placed entity); one it **spawns** exists only on the
  host unless it is announced like an NPC. Items put in an actor's hands
  go through `scneventsEquipItemToPerformer`. [2.5-rtti]
- **A spawned prop is an item with an owner**: `scnPropDef { propId +0x120,
specPropRecordId: TweakDBID +0x148, spawnDespawnParams +0x30 }` with
  `scnSpawnDespawnEntityParams { specRecordId: TweakDBID +0x44, itemOwnerId:
scnPerformerId +0x40, appearance +0x50 }`; it is put in a hand by
  `scneventsAttachPropToPerformer { propId +0x58, performerId +0x5C, slot:
CName +0x60, offsetMode +0x68, customOffsetPos +0x6C, customOffsetRot
+0x80 }`, items by `scneventsEquipItemToPerformer { performerId +0x58,
slotId: TweakDBID +0x5C, itemId: TweakDBID +0x64 }` (an attachment slot)
  (`SDK/scn/PropDef.hpp`, `SpawnDespawnEntityParams.hpp`,
  `SDK/scn/events/AttachPropToPerformer.hpp`, `EquipItemToPerformer.hpp`).
  [2.5-rtti] What a script can see of it: the item in an attachment slot
  (`TransactionSystem.GetItemInSlot`, `reds:52211`), and an item entity
  appearing (Codeware's `Entity/Attach` on `gameItemObject`). Whether a
  scene's prop is a `gameItemObject` and whether `IsEntityInScene` answers
  yes for it are [unknown]: tandem's host tries both (`Scenes.reds`,
  `HostProps`). [2.5-untested]
- Effects: `scneventsVFXEvent { effectEntry, action: Play/Break/Kill,
performerId, nodeRef }`: an effect by name on a performer, which a script
  can start on any `GameObject` (`GameObjectEffectHelper.StartEffectEvent`,
  `reds:106045`). [2.5-rtti, 2.5-untested]
- Cameras: tier 3 and 4 keep V's own first-person camera; tier 5 uses
  scene cameras (`scneventsCamera*`). A third party has no camera in the
  scene; what a script can do for one is in `coop-scenes.md` §2.4
  (`GetActiveCameraWorldTransform` on the host, a free camera or an
  activated `CameraComponent` on the guest, both unverified). Nothing new
  was found.

## 7. Johnny

- An NPC of the host's world: record `Character.Silverhand`, appearance
  `silverhand_default` (`wiki/…/johnny-glitching-and-talking-with-v.md:116-123`);
  the playable one is `Character.johnny_replacer`. Scenes spawn him
  (`spawnDespawn`). [wiki]
- **He is hidden unless he plays a workspot.** His entity carries a
  `gamePhantomEntityComponent { params: gamePhantomEntityParameters
{ teleportStartEffect, teleportEndEffect, spawnEffect, glitchEffect:
CName; phantomVisibleStates: [RootMotion 0, Workspot 1, MoveOnSpline 2] } }`,
  run by the native `gamePhantomEntitySystem` (0x3320, no script function)
  (`SDK/game/PhantomEntityParameters.hpp`, `PhantomEntityState.hpp`; wiki
  same page `:254`). Codeware gives scripts the component and its
  parameters (`CW/scripts/Base/Imports/gamePhantomEntityComponent.reds`),
  and `Entity.FindComponentByType`. [2.5-rtti, wiki, 2.5-untested]
- He glitches in by himself when his workspot starts; glitching out is a
  scene effect event, `johnny_teleport_start`, 250 ms before the end (wiki
  same page `:256-290`, `:477-490`): an entity effect name, so
  `StartEffectEvent(johnny, n"johnny_teleport_start")` should play it.
  `scnOverridePhantomParamsEvent` overrides the effects per scene. [wiki;
  the script call inferred]
- His lines: `visualStyle innerDialog`, `voExpression
Vo_Expression_InnerDialog` (2): heard in V's head, not from a place
  (wiki same page `:437-444`). [wiki]
- **Consequence for a proxy**: a guest's proxy of Johnny is the same
  entity template, so the guest's own phantom system hides it until it
  plays a workspot. tandem's NPC animation sync already plays the host
  NPC's workspot on the proxy (`NpcAnims.reds`): if that holds for him, he
  appears on the guest when he does on the host, with his own glitch.
  [inferred; nothing of it run]

## 8. The two approaches against these facts

**(a) The guest's game plays the same scene resource, as a spectator.**
Reachable in principle (§1: build the node, `ExecuteNode` on the root
phase), and it would bring everything at once: animation, faces, lipsync,
voice, props, effects. Against it, each from the reflection data:

1. _It is the story._ The scene's own quest nodes, sockets and gameplay
   events (§2) run with it: facts, journal, spawns, mounts, equipment, on a
   guest whose story is frozen so that exactly this does not happen.
   Filtering them means refusing nested nodes in the node hook (possible)
   and editing timeline events in the loaded resource (raw memory).
2. _Its V is the local V._ The guest's own V would be put in the scene's
   workspot, animated and camera-locked as the host's (§2). The only
   alternative in the data, `findNetworkPlayer`, is a CDPR leftover with no
   proof of life.
3. _Its actors are its own._ Found actors are the guest's own copies of
   those NPCs, which tandem hides (the host's proxies stand for them);
   spawned actors would be spawned again: two sets of actors, the failure
   the architecture forbids. No acquisition plan takes a proxy (§2).
4. _It cannot be steered._ Nothing reflected sets a running scene's
   section or time (§1): joining mid-scene, following a skip or a choice
   would need the scene system's insides.
5. _It cannot be tried offline._ Each point needs the game.

So (a) is a research spike with four separate unknowns, each of which
alone sinks it, and a failure mode (a guest's story advancing) that the
whole quest design exists to prevent. Not built.

**(b) The host says what its scene shows; the guest shows it on proxies.**
Everything it needs exists by name or is already hooked:

| What                   | How                                                                      | Evidence                              |
| ---------------------- | ------------------------------------------------------------------------ | ------------------------------------- |
| a scene is on          | the quest node hook sees `questSceneNodeDefinition` (as scene privacy)   | built, [2.5-untested]                 |
| its actors             | `IsEntityInScene` over the NPCs the host shares                          | [2.5-untested]                        |
| bodies                 | the pose stream, NPC → proxy, same rig                                   | §5: verified for V → puppet           |
| voice                  | `gameaudioeventsDialogLine` on the proxy, by string id, with a seek time | §3: exists; effect to test            |
| subtitles              | already mirrored; the guest's own if the event shows one                 | built                                 |
| Johnny                 | his proxy, his workspot, his own effects                                 | §7: [inferred]                        |
| lipsync, faces         | **missing**: the `face_rig` instance's tracks, streamed like the body    | §4: needs the game (where tracks sit) |
| props the scene spawns | **missing**: to announce like NPCs                                       | §6                                    |

(b) is what is built. Its cost is fidelity where the table says missing;
its gain is that nothing of the story can run on a guest.

## 9. To check in game, in this order

Each is one command or one look; the first three decide how good (b) is.

1. **The voice event** (`tools/game.sh cmd story scene voice STRINGID`,
   with a string id from the subtitle files, standing by an NPC): is the
   line heard, from the NPC, in the game's language? Does a subtitle
   appear by itself? Does the NPC's mouth move (it should not: §4)? With
   `isPlayer`, whose voice?
2. **The line's id**: as host, log `scnDialogLineData.id` for a known line
   (`subtitle: sent` is there; add the id) and compare with its string id.
3. **An NPC's pose on its proxy** (scenario `scene-replay`): the proxy
   takes the pose; and as host (`scene-replay-host`): `actor N's skeleton …
mapped`, for men, women and one "big" body.
4. Johnny's proxy on a guest: hidden outside a workspot? Visible in one?
   Do `teleportEndEffect` / `teleportStartEffect` play on it?
5. `IsEntityInScene` for the actors of a real story scene, and for the
   host's V in tier 1 and 2 conversations.
6. Fast-forward: what the host's `ShowDialogLine` / `HideDialogLine` do
   during it (are lines hidden early: a cut), and the time dilation's value.
7. For the faces (the next stage): where the float tracks sit in a sampled
   pose of the `face_rig` instance (`tools/re/`: the commit's `sampled`
   argument for that instance; +0x00 bone count and +0x30 transforms are
   known, the tracks are not), and whether writing them at the commit
   moves the proxy's face.
8. For (a), if ever: `QuestsSystem.ExecuteNode` on a built
   `questSceneNodeDefinition` of a scene with no player actor, in a
   single-player game.
