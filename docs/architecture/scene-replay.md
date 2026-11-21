# Scene replay: a guest watches the host's story scenes

A guest standing next to the host in a story scene should see and hear it
as if it were there: the actors move as the scene moves them, speak with
their voices in the guest's language, Johnny comes and goes with his
glitch. Before this, a guest got the journal, calls, subtitles and choice
hubs, and saw the scene's NPCs as proxies that walk and idle.

Engine facts and their sources: [`research/scene-replay.md`](../research/scene-replay.md)
(read offline on 2.50; tags there). Earlier notes: `research/coop-scenes.md`,
`research/coop-quests-design.md` §2.5, `research/pose-streaming.md`.

**State (2026-11-20)**: designed, built through the protocol, the core
logic, the plugin, the scripts and the mock player; tested without the
game (loopback). **Nothing is verified in game.** It is off unless
`tandem.ini` says `scene_replay = on` (faces and other rigs: `scene_rigs =
on` too); the scenarios turn it on. The second part (props, the watch
camera, the budget, faces and other rigs) was merged without waiting for
CI: its C++ has not been compiled by its author.

## Contents

1. [The decision](#1-the-decision)
2. [What travels](#2-what-travels)
3. [The host's side](#3-the-hosts-side)
4. [The guest's side](#4-the-guests-side)
5. [Voice and subtitles](#5-voice-and-subtitles)
6. [The guest's own V](#6-the-guests-own-v)
7. [Johnny](#7-johnny)
8. [When things go wrong](#8-when-things-go-wrong)
9. [What is built, and where](#9-what-is-built-and-where)
10. [Tests](#10-tests)
11. [In-game checks, and the scenarios](#11-in-game-checks-and-the-scenarios)
12. [Not built, and what would unblock it](#12-not-built-and-what-would-unblock-it)
13. [Risks](#13-risks)

## 1. The decision

Two ways were weighed.

**(a) The guest's game plays the same scene resource, as a spectator**,
driven by the host's scene state, its actors bound to the proxies, the
host's V played by the host's puppet, with no quest effect.

**(b) The host says what its scene shows** (who acts, each actor's pose,
each voiced line, where it is staged) **and the guest shows it on its
proxies.**

(a) would be the more native one: everything a scene does, for free. The
evidence is against building it (research file §8):

- a scene resource carries the story's logic, not only its picture: quest
  nodes embedded in the graph (`scnQuestNode`), sockets fired from the
  timeline, mounts, equipment, spawns. Playing it plays them, on a guest
  whose story is frozen so that this never happens;
- the scene's V is the local player (`scnPlayerActorDef`): the guest's own
  V would be seated, animated and camera-locked as the host's. The one
  alternative in the data, "network player N", is a leftover of CDPR's
  cancelled multiplayer with no proof it runs;
- the scene finds or spawns its own actors; no plan accepts "this proxy".
  Found actors are the guest's own hidden copies, spawned ones a second
  set: two copies of one scene, the failure principle 1 exists to prevent;
- nothing reflected starts a scene outside a quest phase (Codeware's
  `QuestsSystem.ExecuteNode` on a built scene node is the only lead,
  unproven), and nothing sets a running scene's section or time: joining
  mid-scene or following a skip would need the scene system's insides
  (0x10d0 bytes the SDK knows nothing of);
- none of it can be tried without the game.

Four unknowns, each enough to sink it, and a failure mode that breaks the
guest's own story. **(b) is built.** Each of its parts exists by name or is
a hook tandem already has and has verified:

- bodies: the pose stream that already draws a remote V on its puppet,
  from an NPC of the host's game to its proxy (the same rig on both sides,
  no retargeting);
- voice: the game's own dialog line event, queued on the proxy with the
  line's string id (the same id in every copy of the game, each with its
  own language's recording);
- the rest is bookkeeping the session already does for the story.

Faces (the face rig's pose, streamed: §5, "Faces, and bodies on other
rigs") and props (§3, §4) were added on 2026-11-20; what is still missing
is in §12. (a) stays a research spike (research
file §9, last item), worth a look only for scenes with no V in them.

The authority table's row is unchanged: _quests: facts, journal, phases,
scenes — host; followed, never run on their own_.

## 2. What travels

Four messages, appended to the protocol (no version bump); all host to
guests. The first three are story messages (`proto::IsStory`): one path
through `Endpoint::SendStory`, kept by `story::Record` for late joiners,
reliable and ordered.

<!-- prettier-ignore -->
| Message | Channel | When | Carries |
| --- | --- | --- | --- |
| `SceneState` | Event | on every change | the scene's resource path hash (0: none), its `play` number, host clock at its time 0, rate (fast-forward), the host's tier, flags, the staging area |
| `SceneCast` | Event | whole, on every change | `play`, the actors: `NpcId` and flags (ghost, hidden, posed) |
| `SceneLine` | Event | a line begins, or is cut | `play`, the line's string id, the speaker (`NpcId` or `PlayerId`), host clock when it began, its length, flags (cut, holocall, bark) |
| `ActorPose` | State | ≤ 30 Hz per actor, unreliable | `NpcId`, seq, host-clock time, the body pose (a `PoseState`: 68 rotations of 6 bytes and the hips, 431 bytes) |
| `SceneCamera` | State | every frame of a cutscene, paced | `play`, seq, host-clock time, the active camera's position, orientation (5 bytes), field of view, a cut flag; 39 bytes, never kept |
| `SceneProp` | Event | a prop is held, changes hands, goes | `play`, the prop's number in the play, the item's TweakDB id, its holder (`NpcId` or `PlayerId`), flags (gone, left hand); a story message, 26 bytes |
| `ScenePlace` | Event | a prop is set down, moves, goes | `play`, the prop's number, its entity template's path hash, its appearance, position, orientation, flags (gone); a story message, 53 bytes |
| `RigTracks` | State | with a face's `RigPose`, paced | `NpcId`, rig part, seq, host-clock time, the hash and count of the rig's float tracks, the tracks that moved (4 bytes each) |

- `play` counts the host's scenes in a session. A scene played again is
  another play; a cast or a line names its play, so what belongs to a scene
  that is over is void wherever it arrives.
- Times are host-clock microseconds (`uint64_t`): a guest that hears of a
  scene or a line late knows how far in it is.
- `section` (the scene node playing) is in `SceneState` and always 0: the
  host cannot read it today. It is there so that the day it can, the
  message does not change.
- The staging area is the middle of the host's V and the actors that show,
  the distance of the farthest from it, and the way the host's V faces it.
- Cost: bounded. The scene's streams (each actor's body, the camera)
  share **22 kbit/s** on a guest's link (`scene::kSceneBytesPerSecond`,
  2750 bytes a second with 12 a message; `scene_kbps` in `tandem.ini`
  raises it on a good link), split by `scene::Pacer` in the host's
  `Endpoint`: every stream it can serve gets a frame at least every 0.5 s,
  the rest of the budget goes by weight (camera 3; a body 1 within 15 m of
  the nearest other V, 0.5 to 40 m, 0.25 beyond), none faster than 30 a
  second. A stream that would not get its 0.5 s is not sent at all, the
  least weighted first: its actor keeps its own animation. On the wire a
  body is an `ActorFrame` (the `PoseFrame` coding, a key every 2 s, bones
  within 0.006 rad of the key's kept: about 110 bytes against 431); the
  guest's game hears whole `ActorPose`s. With three actors near and a
  camera: about 4 poses a second each and 14 views (computed; the budget
  test's row measures the link). A guest draws a body one pose interval
  in the past, between two poses. `scene_replay = noposes` leaves the
  poses out.

## 3. The host's side

`scripts/tandem/Scenes.reds` (`HostTick`, four times a second) and
`scene::Stage` (`core/src/scene.cpp`):

- **A scene plays** when the host's V is in one (`IsEntityInScene` on V,
  or tier 3 and up) and it holds V (tier 2 and up) or has an actor: at
  tier 1 (free play) the scene system counts V in many a quest's
  background scene (the test save's field: for minutes), with nobody to
  replay (`scene::Stage`; the test scene too needs an actor at tier 1).
  Which one: the scene whose quest node ran in the last 3 s (the hook
  scene privacy already uses, `engine/scenes.cpp`); a scene with no node
  seen is announced all the same, with no name (`kUnnamedScene`, 0x1).
  The name stays the play's: a sample with none goes on with it, a play
  with none takes the first one seen (a change, same play); only another
  name is another play.
- **Private scenes are never announced**: when the story script holds the
  scene private (`kStoryPrivate`), or its class says so at any tier
  (`TandemStory.SceneClass() >= 2`), the stage gets `isPrivate`: no state,
  no cast, no pose (the net thread drops actor poses while private), no
  line. A scene that turns private while watched ends for the guests.
- **Its actors** are the NPCs the host shares that the scene system says
  are in a scene, within 100 m of V (16 at most; the far ones' poses come
  less often: §2, cost). One with a `gamePhantomEntityComponent`
  is a ghost (Johnny), hidden unless a workspot holds it.
- **Their bodies**: each actor is bound as a pose source
  (`engine/posestream.cpp`): read after its entity's animation job, 30
  times a second, sent as `ActorPose`.
- **Lines**: the host's subtitle listener (`Dialogue.reds`) already sees
  every line shown. It now also says whose voice it is: the speaker's
  `NpcId` or the host's own `PlayerId`, when it began, how long it is. A
  subtitle hidden before its end is a cut. A line whose speaker is an NPC
  the host does not share is not voiced (nobody a guest could hear it
  from), unless it is a holocall.
- **Props** (`HostProps`): what the actors hold. Two sources, both by name:
  an item in an actor's `AttachmentSlots.WeaponLeft` (the right hand
  already travels with `NpcAnims`); and an item entity (`gameItemObject`)
  that attached while the scene plays (Codeware's `Entity/Attach`, queued
  only: `TandemStreamKind.Prop`), that the scene system says is in a scene,
  within 1.5 m of an actor or of our V and not already in that body's hand
  slots: held by the nearest, right hand unless only the right is taken.
  When that body's hand slots can be read (`SlotComponent.GetSlotTransform`,
  `RightHand` / `LeftHand`), the item must also be within 0.35 m of one.
  `scene::Stage` numbers each (the same number while it changes hands) and
  says what changed; a prop no longer seen is gone.
- **Props nobody holds** (a glass set on a table, a dropped gun): such an
  item entity of the scene at no hand is announced where it is
  (`ScenePlace`: its template, `Entity.GetTemplatePath` of Codeware, as a
  path hash; its appearance; its transform), said again when it moved 3 cm
  or 0.05 rad (at most twice a second), gone when it is no longer seen or
  someone holds it (then it is a `SceneProp`, another number).
- **Changes only**: the stage says a staging area again only when it moved
  0.75 m and at most twice a second; the cast when it differs; the rest at
  once (fast-forward, a hub holding the scene's time, the tier).
- **The end**: no scene any more. It is an interruption when someone was
  still speaking.

## 4. The guest's side

The plugin turns the host's messages into cues (`scene::Replay`, tested
without the game); the script applies them (`Scenes.reds`):

<!-- prettier-ignore -->
| Cue | The guest's game |
| --- | --- |
| a scene begins | notes it (and how long it has played); the watch offer is armed |
| it changed | staging, tier, flags; a fast-forward stops the voices |
| an NPC acts | once its proxy is ready, binds the proxy's root graph instance to that NPC's pose stream; a ghost is shown or hidden with its effect |
| it no longer does | unbinds it: the proxy's own animation again |
| a line begins | queues the dialog line event on the speaker (the proxy, the host's puppet, or our own V for a holocall), from the right second |
| a line stops | queues the stop event |
| a prop is held | once the holder's body is there (an actor's proxy, a player's puppet), the item goes in its hand slot (`TandemWeapons.Hold`) |
| a prop is gone | out of that hand, if it is still what the hand holds; every prop goes before the scene's end releases the actors |
| a prop is set down | spawned from the same template and appearance where the host says (Codeware's dynamic entities); moved there when it moves |
| it is taken away | that entity deleted; the scene's end deletes every one |
| the scene is over | every actor released, the offer closed |

- A proxy keeps everything it had: its place still comes from the host's
  NPC states, its animation graph still runs with the host's states and
  workspot (`NpcAnims.reds`: footsteps, its sounds). The pose is written
  over the graph's result where the engine commits it, so the proxy's own
  deformation and face graphs follow, exactly as for a remote V.
- The pose is drawn as far in the past as the proxy's position (the same
  track delay), so body and place stay in step.
- **A stream that stops is not a frozen actor**: with no pose for 1 s
  (the scene is over, the host is gone, the actor's body is not one the
  stream knows) the write does nothing and the proxy shows its own
  animation, whether or not the script has unbound it yet.
- A proxy whose body is rebuilt (streamed out and back, a fast travel) is
  bound again on the next tick: the cast names NPCs, not entities.
- The host's V is not in the cast: its puppet already shows its pose.
- While a prop is in a proxy's hands `NpcAnims` leaves its right hand alone
  (`TandemStory.SceneHolds`): the host's animation state says "no weapon".

## 5. Voice and subtitles

- **The voice is the guest's own**: `DialogLineEvent` (Codeware's name for
  `gameaudioeventsDialogLine`) with the line's string id, queued on the
  speaker's entity. The guest's game looks the id up in its own language's
  voice-over map: the guest hears the line in its language, from the
  proxy's place. Expression: spoken; phone for a holocall (queued on our
  own V); inner dialog for a ghost (Johnny is heard in the head, as the
  host hears him).
- **In sync with the subtitle**: the host sends the subtitle, then the
  voice, in one tick on one ordered channel; the guest shows one and plays
  the other in the same frame.
- **A guest arriving mid-line** hears it from where it is: `seekTime` is
  host clock minus the line's start. Less than 0.25 s in: from the start
  (the network's delay is not worth a seek). Less than 0.35 s left: not
  played.
- **One voice per speaker**: a speaker's new line stops its earlier one.
- **Out of earshot** (40 m from the speaker; holocalls excepted): not
  played, as its subtitle is not shown (25 m from the host).
- **No double subtitles.** Today's subtitle is the host's text, written on
  the guest's subtitle board. If the voice event makes the guest's game
  show the line itself (unknown until tested), there would be two. So the
  mirrored line is shown under a stand-in id (`TandemStory.StandInLine`),
  and the guest listens to its own board: a line arriving there under the
  real id of one we mirrored is the game's own, in the guest's language;
  ours is hidden at once, and a mirrored line the game already showed is
  not shown. Whichever way the engine behaves, a line is on screen once,
  and the guest's own language wins when the game provides it.
- A V's line (`speakerPlayer`) is queued on that player's puppet with
  `isPlayer`, in the recording of that V's body: the speaker's body as the
  session's roster has it (1 female, 2 male; no new message) is written
  into the event's `audioDialogLineEventData.gender` (`TandemStory.VoiceGender`: the Int32
  Codeware adds to the RTTI, found by name and type; absent, the game
  picks, logged once). Its values are taken to be `locVoiceTagGender`'s
  (male 1, female 2) [inferred]; `scene_voice_gender = swapped` flips them,
  `off` leaves the game's pick. Limit: a V's voice tone is chosen apart
  from its body (`IsBrainGenderMale`, only reflected on the character
  creation state); the body is what is sent.

### Faces, and bodies on other rigs

`scene_rigs = on`, the default (not yet run in game; it adds work on the
animation job threads; `off` turns it off, and a layout or rig mismatch
turns it off by itself, logged).

- **What is copied**: the pose a graph instance hands over at its commit,
  whole rig, local space, by bone index. For a face that is the entity's
  `face_rig` animated component, whose graph turns the facial tracks
  (lipsync, expressions) into its joints (about 290); for an actor whose
  skeleton lacks the humanoid body's bones (a drone, a robot) it is the
  `root` component. The host's NPC and its proxy are the same record and
  appearance, so the same rig resource: the stream names the rig (the hash
  of its bone names, and their count) and a proxy on another rig takes
  nothing.
- **Host**: the faces of the actors that speak a line of the scene now
  (`scene::Stage::Speakers`, half a second past the line, three at most),
  and the root rig of every actor whose body the humanoid stream could not
  map, are read at that instance's commit, 25 times a second at most
  (`engine/posestream.cpp`, `ReadRig`). `Root` and `Trajectory` are never
  said.
- **On the wire** (`rig::Encoder` inside `Endpoint::SendRigPose`): each
  bone against the rig's reference pose. A frame says the bones furthest
  from what was last said of them (over 0.4 mm or 0.01 rad), the largest
  first, within a tenth of a second of the stream's share (40 to 240
  bytes), then three bones in turn whatever they do, so a lost frame heals
  (a 290-joint face: within 10 s at 10 frames a second). No keys, no
  acknowledgements. A face weighs 2 in the scene's budget.
- **Guest**: every actor's proxy has its face bound (and its root, for a
  body on another rig); at that instance's commit the bones the host moved
  are written over the rig's own (`rig::Track`: each goes to where the
  host put it with a 60 ms time constant; a bone the host put back at rest
  blends back to the rig's own animation and is let go). No frame for 2 s:
  nothing is written.
- **What it costs in fidelity**: within the default budget a face gets 5
  to 10 small frames a second: the jaw and the widest lip moves follow, fine
  shapes lag. `scene_kbps` raises it.
- **Float tracks** (wrinkle maps, pupils, blinks, the lipsync envelope: a
  face rig's `trackNames`, about 427 on an NPC): read and written at the
  same commit, beside the bones, from the sampled pose's track pointer
  (`[sampled+0x38]`, count at `+0x0C`, read from the code:
  `engine/animation.md` §13.3). **Layout
  check**: the first full sampled face decides for the session: its track
  count is its rig's, the pointer is one, every value a finite number
  under 64; anything else turns face tracks off, logged
  (`pose stream: a sampled face has N float tracks …`). On the wire
  (`RigTracks`, `rig::TrackEncoder`): each track that moved over 0.004
  since last said, the largest first, as an index and an int16 of 1/4096,
  within a tenth of a second of the stream's share, and three in turn; its
  own stream in the budget, weight 1. Guest (`rig::TrackSet`): the same
  60 ms blend; a track said at its reference value goes back to the rig's
  own and is let go. Written before the bones: a face that solves its
  joints from the tracks after the commit follows them.

## 6. The guest's own V

It is not an actor. It stands free, weapons lowered near a tier 3+ scene
and invulnerable near a tier 4+ one, as before (`Story.reds`,
`UpdateHolds`).

- **A place to watch from.** A staged scene the host cannot walk away from
  (tier 3 and up), our V within 60 m but not at it (more than 4 m beyond
  its actors): a notice, once per scene, "NAME is in a scene", and a
  "Watch the scene" hint on `Choice1` (the player's own binding,
  `strings.md`). On accept (`Scenes.reds`, `Watch`; `scene::WatchSpot`):
  a spot on a ring 1.6 m beyond the actors (2.2 to 7 m from the middle),
  on the host's side of the scene but at least 25° off its line of sight
  to the actors, first within 80° of it, at least 1.1 m from every actor,
  the host and the other players' puppets, the nearest such spot to where
  our V stands. It must be ground a character can stand on (the
  navigation system's query within 0.75 m), or nothing happens: never into
  a wall. Our V is put there facing the scene. No spot: it stays where it
  is. Far away (over 100 m) the existing offer to join still applies.
- **Never through an actor.** The actors go where the host's scene puts
  them, and may walk into where our V stands. While a scene is on, five
  times a second: if our V is within 0.45 m of an actor's proxy, it takes
  a step of at most 12 cm out, the short way (`scene::KeepOut`), until
  clear. Below the distance at which the game's own collision already
  keeps a walking V out, so it never fights the player's own steps.
- **The watch camera.** A full cutscene (tier 5) is a scene camera's
  picture, not V's eyes. While the host's V is in tier 5 in a scene it
  announces, its script samples the game's active camera every frame
  (`GetActiveCameraWorldTransform`, `GetActiveCameraFOV`); a view far from
  the last one (`scene::IsCut`: over 0.4 m + 12 m/s, 0.2 rad + 4.2 rad/s
  or 6° + 60°/s of zoom) is flagged a cut. `Endpoint::SendSceneCamera`
  paces it (`scene::Pacer`, at most 30 a second; a cut that is not sent is
  carried by the next view). A guest in the host's story moment
  (`RestrictInMoment`), while the host's tier is 5 and views of this play
  come: spawns a camera body (`base\entities\cameras\simple_free_camera.ent`,
  Codeware's dynamic entities), places it each frame at the view
  `scene::CameraTrack` gives (drawn 1.5 view intervals + 20 ms in the past,
  between the two views around that moment; a cut is not blended), sets
  its field of view and activates its `gameCameraComponent` with a 0.4 s
  blend. It is released, with the same blend, when the views stop for 1 s
  (the cutscene or the scene is over, the host is gone), when our V leaves
  the moment, or when the player presses the "Leave the camera" hint
  (`Choice1`; not offered again in that scene). Never for a private scene:
  it is not announced, so nothing is sampled, and the net thread drops
  views while private. `scene_camera = off` keeps a guest's own eyes.
  A guest spectating the host's scripted moment (`story.md`, "Spectating")
  follows the same track with a camera of its own; this one then stands
  aside (`RestrictInMoment` is false while it watches).

## 7. Johnny

Johnny is an NPC of the host's world; a guest has his proxy when the host
shares him. What was open: he is there through the Relic only.

- His entity hides itself unless it plays a workspot (the phantom entity
  component, research file §7), and plays its own glitch when it appears.
  The proxy is the same entity, and tandem already plays the host NPC's
  workspot on the proxy. So the expectation, to verify, is that he appears
  on the guest when he does on the host, by the game's own means.
- On top of it, in case that does not hold, the cast carries it: an actor
  with the phantom component is a **ghost**; **hidden** when no workspot
  holds it on the host. The guest shows or hides the proxy to match, with
  the component's own effects (`teleportEndEffect` or `spawnEffect` to
  appear, `teleportStartEffect` to go: `johnny_teleport_start` is the
  effect scenes use for his exit) and the animation system's fade. A ghost
  that leaves the cast goes the same way: he is not there for the host
  either.
- His lines are voiced as inner dialog.
- Songbird and any other phantom entity follow the same rule: it is the
  component that decides, not a list of names.
- The relic glitches on the host's V (`StoryEffect`) are unchanged.

Limit: outside a scene (Johnny leaning on a wall as a quest phase puts
him, no scene running) only the first point applies.

## 8. When things go wrong

<!-- prettier-ignore -->
| Case | What happens | Tested |
| --- | --- | --- |
| the host skips (fast-forward) | `SceneState` with the skipping flag and the rate: voices stop, lines begun while it lasts are not played; the poses keep coming and move as fast as the host's | `scene_test` (replay) |
| a choice is made | nothing special: the hub closes as before; while it is up the scene's time is held (flag); what follows arrives as cast, poses and lines | `scene_test` (stage, replay) |
| the scene is interrupted | the end comes with the interrupted flag (someone was speaking): voices cut, actors released | `scene_test` |
| one scene straight into another | another play: the guest ends the first (voices, actors) before the second begins, even if the first's end never came | `scene_test` |
| a guest arrives mid-scene | the record gives it the state, the cast and the lines being spoken, in that order; it joins at host clock − start, and each line from where it is now | `scene_test` (endpoints: joins 2 s in) |
| the host vanishes | 5 s of silence end the session; before the script hears of the end it gets the cues: voices stopped, every actor released, the scene over (interrupted); the plugin forgets every actor pose. Even before that, a pose stream silent for 1 s lets go | `scene_test` (endpoints: host destroyed mid-line) |
| a private scene | never announced: no state, cast, pose or line (§3); one that turns private ends for the guests | `scene_test` (stage) |
| the scene's end is lost (a bug) | the story digest hashes the scene and its cast: host and guest differ, the guest asks for the story again, and a resync says "no scene plays" explicitly | `scene_test` (endpoints with a dropped end; ≈ 8 s) |
| repeats (a resync, a duplicate) | the record and the replay drop what they have: a line already playing is not played again | `scene_test` |
| the host's world is rebuilt (an epoch) | actor poses are numbered with the NPC states: the same fence voids the old world's; a pose for an NPC the guest does not know (it overtook its `NpcGone`) is dropped | `scene_test` (unknown NPC); fence shared with NPCs |
| an actor's proxy is not there yet | bound when it is ready; its lines are not voiced until then (logged) | in game |
| a body the stream does not know | not read on the host (logged once per skeleton); the proxy keeps its own animation | in game |
| poses lost or late (unreliable) | the track interpolates between the two around the drawn time and holds past the newest, as for players; older sequence numbers are dropped | `pose_test` (the track) |
| the camera's views stop, or are lost | unreliable: a lost view is skipped (the track blends over it); none for 1 s: the guest's own camera, whether or not the scene's end was heard | `scene_test` (camera track, endpoints) |
| the camera body does not spawn | nothing is activated: the guest keeps its own eyes (logged once if the body has no camera component) | in game |
| scene replay off on one side | a guest with it off ignores the scene messages; a host with it off announces nothing. Both keep today's behaviour | by construction |

Digests: only what lasts and changes rarely is hashed (which scene, which
play, the cast). The staging area, the rate and the lines are left out:
they change too often, and a wrong one heals with the next change.

## 9. What is built, and where

- **Protocol** (`core/include/tandem/protocol.hpp`): `SceneState`,
  `SceneCast`, `SceneLine`, `ActorPose`, appended; bounds checked in
  `Decode` (32 actors, a rate between 0 and 16). Second part, appended
  (2026-11-20): `SceneProp` (built: props),
  `SceneCamera` (the watch camera), `ActorFrame` (bodies on the wire),
  `RigPose` (faces, other rigs).
- **Core** (`core/include/tandem/scene.hpp`, `core/src/scene.cpp`):
  `scene::Stage` (host), `scene::Replay` (guest), `scene::WatchSpot`,
  `scene::KeepOut`. `story::Record` keeps the scene, its cast and the
  lines being spoken for joiners (not those over: a bark of minutes ago
  named an NPC a joiner was never told of, the referee's `ids.unknown-npc`,
  2026-11-21) and hashes scene and cast. `Endpoint`:
  the three story messages through the story path, `SendActorPose` /
  `events::HostActorPose`, the explicit "no scene" in a story resync.
- **Plugin**: `engine/posestream.cpp` (actor sources and actor bindings on
  the two existing hooks; the write side is one function for players and
  actors), `runtime.cpp` (actor pose queues and tracks),
  `story_natives.cpp` (the stage and the replay on the game thread; cues
  as story events 50 to 56; the natives in `StoryNatives.reds`),
  `engine/scenes.cpp` (`ScenePlaying`), `config.cpp` (`scene_replay`).
- **Scripts**: `Scenes.reds` (new), `Dialogue.reds` (the voice of each
  line, the stand-in subtitle ids and the dedupe), `Story.reds` (wiring).
- **Faces and other rigs**: `core/src/rigpose.cpp` (`rig::Encoder`,
  `rig::Track`), `Endpoint::SendRigPose` (heard as `events::Story`, put
  straight in `Runtime`'s tracks by the net thread), `engine/posestream.cpp`
  ("Other rigs": `BindPoseRig`, `ReadRig`, `WriteRig` on the commit hook,
  behind the same layout checks plus the rig's bone count),
  `TandemStory.FaceSource` / `FaceBind` / `SceneSpeakers`, `scene_rigs`.
  A face's float tracks: `RigTracks` (appended), `rig::TrackEncoder`,
  `rig::TrackSet`, `Endpoint::SendRigTracks`, `FaceTracks` in
  `engine/posestream.cpp` (behind `FaceTracksOk`).
- **The watch camera**: `scene::CameraTrack`, `IsCut`, `PackCamera`,
  `scene::Pacer` (`core/src/scene.cpp`), `Endpoint::SendSceneCamera` (a
  guest hears it as `events::Story`; the plugin's net thread puts it
  straight in `Story`'s track, not through the script's queue),
  `TandemStory.SceneCameraSample` / `SceneView`, `Scenes.reds` (`Frame`,
  a per-frame tick only while a cutscene lasts).
- **Props set down**: `ScenePlace` (appended), `scene::PlaceSample`, the
  stage's and the replay's `m_placed`, `story::Record` (kept for joiners;
  the digest hashes which, not where), `TandemStory.ScenePlaced` /
  `ScenePlaceOf` / `PathHash` / `PathOf`, `Scenes.reds` (`HostPlaced`,
  `OnPlaced`, `MoveProps`); story events 62 and 63.
- **Mock player** (`peer/src/main.cpp`): story verbs `scene`, `cast`,
  `prop`, `face`, `camera`, `skip`, `voice`, `cut`; its scene's actors take the pose it sends; as a
  guest it logs what it hears.
- **Config** (`tandem.ini`): `scene_replay = on | noposes | off` (on by default);
  `scene_camera = on | off` (the watch camera); `scene_rigs = off | on`
  (faces, bodies on other rigs); `scene_kbps` (host: what a
  scene's streams may take on a guest's link, 22 by default, 8 to 2000);
  `scene_voice_gender = on | swapped | off` (guest: a V's line in its
  body's voice, §5).

Engine access, by the rules: scripts and RTTI names (Codeware's imports);
the two functions by address-library hash that the pose stream already
hooks, behind its existing layout checks (`entAnimatedComponent` and
`animRig` sizes; the source skeleton has every body bone; the target rig
maps all 71; the first sampled pose has the rig's bone count and is in
local space). **No new hook or hash**; one raw offset pair (a sampled
pose's float tracks, +0x0C and +0x38), behind its own check (§5, float
tracks).

## 10. Tests

`task test` (Linux, no game), in `tests/scene_test.cpp` and
`tests/protocol_test.cpp`:

- the messages survive encoding, travel on the right channels, are story
  messages (but the pose), stay small, and refuse out-of-range values;
- the stage: begin with cast and staging, says only what changes, skip and
  hold flags, the end, plays counted, lines with their start, cuts,
  interruptions, private scenes;
- the replay: begin, cast changes, voices from their start or seeked,
  stale lines dropped, one voice per speaker, a skip, another play, the
  end, a reset;
- the watch spot (beside the host, clear, facing the scene, on the host's
  floor, another one when taken) and the step out of an actor;
- the record: what a joiner gets, the digest, repeats;
- rig poses: the smallest form of each move, the cap and its priority,
  malformed frames; a proxy's rig follows, lets go at rest, takes nothing
  of another rig, heals a lost frame; through endpoints (the host's only,
  known NPCs only, paced);
- the actors' bodies: paced and delta coded on the wire, whole for the
  guest's game (`scene_test`); the link with three actors and a camera
  within 64 kbit/s (`budget_test`, the scene row);
- the camera: a view through the wire, cuts told from moves, the track
  (blend, a cut, old views, silence, another play), the pacer (five
  streams within the budget and none under two a second, twenty bodies on a
  small budget, an ended stream), through endpoints (paced, the host's
  only, the cut flag never lost);
- props: taken, handed over (the same number), put away, gone with the
  scene, never in a private one; set down, moved (paced), taken, kept for
  a joiner and hashed by which, not where; the guest's cues in order; the record and
  its digest; through endpoints (only the host's word, a late joiner gets
  them); the referee (an unknown holder);
- through real endpoints over loopback UDP: a guest watches; a guest
  cannot speak for the scene; poses only for known NPCs and only from the
  host; a second guest joins 2 s in and seeks; the end; the host destroyed
  mid-line; a lost end healed by the digests.

`task scripts:check` compiles the scripts against the game's and
Codeware's (0 errors, 0 warnings); `tools/check-natives.py` matches the
natives.

## 11. In-game checks, and the scenarios

Not run yet (the game machine was not available to this work). In order;
the first three decide how good the feature is (research file §9 has the
detail of each):

1. `tools/game.sh cmd story scene voice STRINGID` next to an NPC: is the
   line heard, from it, in the game's language; does a subtitle appear by
   itself; whose voice with `isPlayer`.
2. As host in a real scene: the id of `subtitle: sent` lines against the
   lines' string ids.
3. **`task scenario -- scene-replay`** (the game as a guest of a scripted
   host): the scene begins, weapons lowered, the actor's proxy follows the
   host's pose (the mock player sends our own V's pose back for it: crouch
   and the Tyger Claw crouches), lines voiced and cut, a skip, the end
   releases the actor; a second scene in which the host vanishes mid-line
   is stopped and nothing stays held. With real string ids in
   `tools/scenarios/scene-replay.story`, someone listening confirms the
   voices and the single subtitle.
4. **`task scenario -- scene-replay-host`** (the game as host, a test
   scene with spawned gangers): announced with cast and staging, the
   actors' bodies read and sent (`actor N's skeleton … mapped`), a line of
   ours voiced for the guest, the end said.
5. Two games, a real story scene (the first with Jackie is enough), a
   guest beside the host: bodies, voices, one subtitle per line, the watch
   offer and the spot, an actor walking into the guest's V.
6. Johnny (the first scene after the heist where he appears): his proxy
   on the guest, visible when he is on the host, his glitch both ways.
7. Props. `scene-replay` (guest): `scene: prop 1 … in the hand of npc
50000`, then handed to the host's puppet, then gone; someone looking
   confirms the inhaler in the Tyger Claw's right hand, then in the host
   puppet's left. `scene-replay-host`: `cmd story scene prop
Items.FirstAidWhiffV0` says `scene prop: 1 … held by npc N`. In a real
   scene with a glass or a shard (the first drink with Jackie): does
   `scene: prop` appear in the host's log at all (is a scene prop a
   `gameItemObject`, does `IsEntityInScene` say yes for it), with the right
   holder and hand; and does the guest's proxy hold it the right way round.
   A prop set down (the glass back on the bar): the host's log says
   `scene: prop N (…) set down at (…)` (`log = debug`), the guest's
   `scene: prop N set down (template …)`; to see: the glass on the guest's
   bar where it is on the host's, moved when the host's moves, gone with
   the scene. Decides it: does a `gameItemObject` template spawn through
   the dynamic entity system without its item data (nothing shown, a
   warning, a crash), and does a spawned prop fall or stay where it is put.
8. The watch camera. **`task scenario -- scene-camera`** (guest of a
   scripted cutscene): `scene: watching through the host's camera`, then
   `our own camera again`. To see: the picture is the view behind the
   host's puppet, jumps at the cut, blends back. Decides the feature: does
   `simple_free_camera.ent` spawn through the dynamic entity system, is it
   a `GameObject` (teleported) or placed with `SetWorldTransform`, does
   `Activate` take the picture from V's first-person camera and
   `Deactivate` give it back; is our V headless in the picture (its
   first-person body). As host (`scene-replay-host`: `cmd story scene
camera on`), and in a real tier 5 cutscene: does
   `GetActiveCameraWorldTransform` follow the scene camera, and the cuts.
9. Faces (`scene_rigs = on` on both games). **`task scenario --
scene-face`**: the proxy's face rig is found and bound (`face rig
0x…: N bones, mapped`: note N), rig poses are heard, the face is let
   go; nothing moves (the mock's face is not the proxy's rig). Two games,
   a real scene: the host's log says `npc N's face is read while it
speaks` and `face rig …: N bones, mapped`; `cmd story scene report` on
   the guest says `writes of B bones` with B > 0 while a line is spoken.
   To see: does the proxy's mouth move with the line, and is anything
   wrong with the face (a twisted jaw would mean the sampled pose of the
   face rig is not what the proxy's graph hands over in the same order).
   The host's and the guest's logs say `face tracks on` once (`cmd story
scene report`: `face tracks on (R reads, W writes)`, R > 0 on the host,
   W > 0 on the guest while a line is spoken); `face tracks off` means the
   sampled pose's track pointer is not as read from the code: note the
   counts in the log line. To see: blinks and brows on the proxy as on the
   host's NPC, eyes not rolled. If B > 0 and nothing moved before the
   tracks, the face is solved from them after the commit. A drone or a robot in a scene:
   `not a body the stream knows` then `body rig …: mapped` on the host,
   `writes` on the guest, and the proxy moves as the host's.
10. V's voice: `tools/game.sh cmd story scene voice STRINGID female`, then
    `… male`, with the string id of a V line: the log says `body 1` /
    `body 2`, and one listening hears the female then the male V. The other
    way round: `scene_voice_gender = swapped` in `tandem.ini`, and the tag
    in research file §3 becomes `[verified-2.5]` with the right values;
    the same recording both times: the field does nothing outside a scene
    (`off`).
11. The earlier story scenarios still pass (`story-guest`, `story-host`,
    `story-johnny`, `story-private`): the subtitle path changed (stand-in
    ids).

## 12. Not built, and what would unblock it

- **Lipsync from the line's own animation** (`m_/f_<string id>` in the
  language's lipsync sets) stays out of reach: nothing reflected plays it
  outside a scene (research file §4). The face's bones and float tracks are
  copied instead (§5).
- **A held prop's exact grip**: a held prop sits in the hand's attachment
  slot, not on the scene's own slot and offset
  (`scneventsAttachPropToPerformer` names a slot of the performer and an
  offset). Nothing reflected reads them from a running scene (the scene
  system's 0x10d0 bytes are unknown to the SDK), and nothing in scripts
  attaches an item with an offset of our choosing: the host could measure
  the offset (the item's transform against its hand slot's), the guest
  could not apply it. Props the scene finds in the world are there
  already; whether they move with the scene is to see in game.
- **The watch camera's depth of field and effects** (a cutscene's blur,
  letterbox, fades): only place, direction and field of view travel.
- **The scene's section and its true clock**: `SceneState.section` is 0.
  A guest follows the host by what the host shows, so it is not needed
  for (b); it would be for (a).
- **A V's voice tone apart from its body**: the guest plays the recording
  of the speaker's body (§5). The tone chosen at character creation is
  read only from the creation state (`gameuiICharacterCustomizationState.IsBrainGenderMale`,
  the save's `brainGender` string): a V of one body with the other voice
  is heard in its body's.
- **Approach (a)**, as a spike only, for scenes with no V in them.

## 13. Risks

- **Everything engine-side is unverified.** The voice event may do
  nothing outside a scene, or play without position; the subtitle id may
  not be the string id; an NPC's pose may not read or write as V's does;
  the phantom system may hide Johnny's proxy whatever we do. Each has a
  check in §11 and a fallback in the design: no voice (today's subtitles
  stay), no pose (the proxy's own animation), the cast's ghost flags. That
  is why each part can be turned off (`scene_replay = noposes | off`,
  `scene_rigs = off`, `scene_camera = off`); it ships on so the first
  test sessions exercise it (2026-11-21).
- **The pose hooks run for more entities.** They were bound to the local
  V and up to 64 puppets; a host now reads up to 16 NPCs and a guest
  writes up to 16 proxies, on animation job threads. The reads are guarded
  and throttled (30 Hz each) and the code paths are the verified ones, but
  the load and the timing on NPCs are new. `scene_replay = noposes` keeps
  the rest of the feature without them.
- **A proxy posed in the wrong place**: the pose is relative to the
  entity's root, which a guest places from the host's NPC states (10 Hz,
  interpolated). In a scene with root motion the two must stay in step;
  they share a clock and a delay, but a visible slide is possible and
  would call for a higher state rate for actors.
- **Staging from positions only**: the watch spot knows the actors, the
  host and the ground, not the furniture. The navigation check keeps V
  out of walls; a spot behind a pillar is possible. It is offered, never
  forced.
- **Voices and the guest's own game**: a line voiced on a proxy while the
  guest's own (frozen) story would voice nothing is fine; a guest far from
  its frozen story but near the host's scene hears only the host's. A
  guest whose own open-world scenes still run (they are not frozen) can
  hear both at once; nothing new, but more audible now.
- **Few poses a second.** Within the 64 kbit/s of a link a scene's bodies
  come 3 to 6 times a second, drawn between: slow acting reads well, a
  sharp gesture is softened, and a body is up to 0.3 s behind its voice.
  `scene_kbps` is the lever where the link allows.
- **The subtitle path changed** for every mirrored line (stand-in ids):
  the story scenarios must be run again.
