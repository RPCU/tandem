# Presence: the other player, seen and heard

Principle 5 of [`../ARCHITECTURE.md`](../ARCHITECTURE.md): everything a
puppet shows comes with the game's own feedback for it. This is the audit
of what a player perceives of their partner, and what closes each gap.
Engine facts, with their sources and tags, are in
[`../research/engine/presence.md`](../research/engine/presence.md).

**State (2026-11-19).** Built, compiled against the game's scripts (`task
scripts:check`: 0 errors, 0 warnings), core logic tested (`task test`).
**Nothing here has run in the game yet**: the game machine was off limits.
"After" below is what the code does; each row says what a person must
still see or hear.

## How it works

- **The body is the pose stream's.** Whatever V's body does on its own
  game (crouch, lean out of cover, look where it aims, swim, climb, stagger,
  fall) is in the 30 Hz pose, drawn on the puppet. Presence is what a pose
  cannot carry.
- **Heard from what is already sent.** A game hears its partner through an
  `Observer` (`core/include/tandem/presence.hpp`), fed every frame with
  the puppet's position and what the partner's game already sends:
  footsteps by gait, jumps, landings by how hard, climbs, vaults, dashes
  and the end of a slide from `PlayerState` (position and movement flags);
  draws and holsters from `Held`; pain from a fall of `Presence`'s health;
  going down from `Vitals`; time abilities from `TimeEffect`. None of it
  costs a byte.
- **Sent: only what the receiver cannot know.** Three messages, appended
  to the protocol with no version bump:

  | Message       | Channel                         | Size | Carries                                                                                                            |
  | ------------- | ------------------------------- | ---- | ------------------------------------------------------------------------------------------------------------------ |
  | `Presence`    | Event, kept for late joiners    | < 16 | a V's health (a byte) and flags (in a menu, photo mode, in combat); at once on a loss or a flag, else 5 Hz at most |
  | `Cue`         | State (a late one is worthless) | < 32 | where a shot's bullet ended; that a melee swing landed, and where                                                  |
  | `NpcReaction` | State for a twitch, else Event  | < 40 | host only: the hit reaction one of its NPCs starts, as the animation feature the game filled                       |

- **Played by the game's own rules.** Every sound is queued on the puppet
  entity (`AudioEvent`, `SoundPlayVo`, `entFootPlantedEvent`,
  `AudioNotifyItemUnequippedEvent`, the weapon's own effects): the audio
  system places it in 3D, attenuates and occludes it, and picks a footstep
  from the surface under the puppet. Nothing is played as a 2D sound.
- **One owner.** A puppet that is hit flinches (a twitch of its NPC body
  and a lean laid over its streamed pose, `presence::Flinch`), which moves
  nothing: where a V is stays its player's. A proxy plays the reaction the
  host's NPC plays; the host alone chooses it. A bullet drawn for someone
  else's shot hits nothing here (`TandemPresenceSync.Cosmetic`, at the
  door of the damage pipeline): the shooter's game decided.

## Audit

"Today" is the state before this work (integration at `2fa03ca`).

### 1. Audio of the remote V's puppet

| The owner…                         | Partner sees and hears today                                                                                    | After                                                                                                                                                                                                                          | Needs ears / eyes                                                                                        |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| walks, runs, sprints, sneaks       | the body moves; footsteps only if the puppet's own walk clip raises them (never checked), at the clip's cadence | the puppet's own are detected (`OnAudioEvent`); where it has none, a foot is planted per stride (`entFootPlantedEvent`: the engine picks surface and footwear), by gait: sneak 0.55 m, walk 0.72, run 1.15, sprint 1.55 a step | both sources never doubled; right surface; cadence against the body (`cmd presence steps auto\|on\|off`) |
| climbs a ladder                    | silent                                                                                                          | a plant per rung (0.45 m)                                                                                                                                                                                                      | the sound suits a ladder                                                                                 |
| jumps, double or charged jump      | silent                                                                                                          | a plant on take-off; `lcm_player_double_jump` for the second or charged one                                                                                                                                                    | that `lcm_*` event is positional on a puppet                                                             |
| lands                              | silent                                                                                                          | two plants and V's landing effort (voice `regularLanding` / `hardLanding` / `veryhardLanding`), the `landing_hard` effects; hardness from the state machine's own landing state, else from the fall's speed                    | voice resolves on the puppet (see voices)                                                                |
| dashes, slides, climbs, vaults     | silent                                                                                                          | `lcm_dash`, `lcm_fs_additional_slide_stop`, voices `climbStart`, `Vault`                                                                                                                                                       | as above                                                                                                 |
| swims                              | body follows; silent                                                                                            | no footsteps in water (unchanged otherwise)                                                                                                                                                                                    | swim sounds: **open**                                                                                    |
| fires                              | the weapon's own effects (`TriggerWeaponEffects`: flash, shells, report — sounds never checked)                 | also the weapon's `Shoot` animation event, as the game pushes for an NPC's shot; the fire loop ended 0.3 s after the last shot (`ExitShoot`); the bullet itself (below) brings its own whizz and impact sounds                 | the right gun's report, 3D, occluded; no stuck loop on automatic fire; no doubled report                 |
| reloads                            | `EnterReload` / `ExitReload` effects                                                                            | also the weapon's `Reload` animation event                                                                                                                                                                                     | the reload is heard                                                                                      |
| swings a melee weapon              | the NPC swing sound (`AI_PlayMeleeAttackSound`)                                                                 | also V's effort (voice `meleeAttack`)                                                                                                                                                                                          | —                                                                                                        |
| lands a melee hit                  | nothing                                                                                                         | `Cue` MeleeHit → the weapon's own `MeleeHit` effects on the puppet's weapon                                                                                                                                                    | the impact sound matches the weapon                                                                      |
| draws, swaps, holsters             | the item appears or disappears in the hand                                                                      | draw: the engine's own equip notification when the item enters the slot (nothing added); holster: `AudioNotifyItemUnequippedEvent` for the item put away                                                                       | both are heard; if the draw is silent, queue the equip notification too                                  |
| uses the mantis blades, other arms | held and swung as any weapon                                                                                    | as any weapon (draw, swing, impact)                                                                                                                                                                                            | the unsheathing sound                                                                                    |
| triggers the Sandevistan           | the trail effect on the puppet (Time.reds); silent                                                              | the sounds of V's own status effect (`SandevistanPlayerBuff`'s SFX records) played on the puppet, stopped when it ends; Kerenzikov the same                                                                                    | those sounds exist and are positional (they may be authored for V's own ears)                            |
| is hurt                            | nothing (on the host, the hit reaction of a reported NPC shot)                                                  | a fall of its health → V's pain (voice `onPlayerHit`), by how much health was lost                                                                                                                                             | voice                                                                                                    |
| goes down                          | the collapse (pose), the outline                                                                                | a cry (voice `stlh_death`: V's own death cry is native and has no script name)                                                                                                                                                 | whether that context suits; else pick one by ear (`cmd presence vo NAME`)                                |

**Voices.** The puppet records have an empty `voiceTag`. If V's contexts
are silent on them, the fix is V's own voice tag on the records, by body:
one run with `cmd presence vo onPlayerHit` decides.

### 2. Hit reactions

| The owner's game shows…                               | Partner sees today                                                   | After                                                                                                                                                                                             | Needs eyes                                                                 |
| ----------------------------------------------------- | -------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- |
| an NPC twitches under fire                            | a proxy twitches only for our own hits                               | every twitch the host's NPC plays (`NpcReaction`, unreliable), except the echo of our own hit                                                                                                     | —                                                                          |
| an NPC is hit hard: impact, stagger, pain, knockdown  | **nothing**: those are tasks of the NPC's AI, and a proxy has none   | the host sends the reaction with the animation feature its AI filled; the proxy launches the same animation action (`ActionHitReactionScriptProxy`) with it                                       | the action plays with the brain off; the proxy stays where the host has it |
| an NPC blocks, parries, has its guard broken          | nothing                                                              | the same (feature + `PlayBlock` / `PlayParry`)                                                                                                                                                    | —                                                                          |
| an NPC ragdolls (explosion, fall, thrown) and gets up | only a knock-out ragdolled                                           | every ragdoll, with its impulse, and its end                                                                                                                                                      | getting up without its AI                                                  |
| an NPC dies                                           | the proxy is killed: no death animation plays without the AI         | the host's death animation (direction, body part, variation) or its ragdoll; a proxy told neither within 0.6 s ragdolls, so a dead proxy never stands                                             | the animation matches the host's                                           |
| a V is shot                                           | on the host only: the hit reaction of an NPC shot its guest reported | on every game: a twitch of the puppet's body and a flinch over its pose (0.45 s, the way the hit went when known), from the fall of its health; the body never moves from where its player has it | the flinch reads well at each strength (`cmd presence flinch`)             |
| a V staggers, is knocked down                         | its body does it (pose stream; `Felled` places the body freely)      | unchanged: the owner's body is the truth                                                                                                                                                          | —                                                                          |
| a V would lose a limb                                 | never (Outcomes.reds, `KeepsLimbs`)                                  | unchanged; the puppet's twitch carries no wound                                                                                                                                                   | —                                                                          |

### 3. Bullet visuals

| The owner…                    | Partner sees today                 | After                                                                                                                                                                                                                                                                          | Needs eyes                                                             |
| ----------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------- |
| fires a hitscan weapon        | flash, report, shells; no bullet   | `Cue` Trace: where the crosshair's ray ended (the game's own look-at query). The partner's game fires a real NPC shot of the puppet's weapon at that point: the weapon's own tracer, impact, decal and sounds. No ammunition, the Vs present are not hit, its hits are dropped | tracer from the muzzle to the right place; decal; nothing takes damage |
| fires a smart or lobbed round | flash, report                      | unchanged: their flight is not a line (**open**)                                                                                                                                                                                                                               | —                                                                      |
| (host) an NPC fires           | flash, report, shells on the proxy | the same real shot from the proxy's weapon at the target the host already sends in `NpcShot`                                                                                                                                                                                   | as above; an NPC shot at our own V is still the one Shots.reds decides |

What a drawn bullet breaks in the world (glass, a barrel) is each game's
own, as before for props nobody shares.

### 4. Party HUD

Built in script from the game's ink widgets, font and colour styles, on
the HUD layer (`scripts/tandem/PartyHud.reds`).

| About the partner                 | Today                                    | After                                                                                                                                                  | Needs eyes                                                         |
| --------------------------------- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------ |
| name                              | on the world marker and the map tooltip  | an entry under V's own health bar: the name in the HUD's font                                                                                          | place and size on screen (`cmd presence hud at X Y`)               |
| health                            | not shown                                | a bar under the name (`Presence`, a byte)                                                                                                              | colours                                                            |
| down / out                        | a notice, the outline, the revive prompt | "Down", "Reviving 7" while we hold the revive (its countdown), "Out"; the bar empties                                                                  | —                                                                  |
| distance and direction off screen | the world marker                         | under the bar when the camera does not see them: an arrow turned their way and "124 m" / "1.2 km"                                                      | the arrow's sense                                                  |
| in a scene, elsewhere, in a menu  | notices only                             | one line: "In a scene" (their tier 3 and up), "Cyberspace" / "Braindance"…, "In a menu" (the pause menu or any full-screen menu); most important first | —                                                                  |
| nameplate                         | only the game's own, when aimed at       | the name and a thin health line over the puppet's head, full to 12 m, gone at 20 m, hidden with the body                                               | its place over the head (the camera's field of view: see research) |
| photo mode, cinematics            | —                                        | all of it hidden in photo mode, when our own scene tier is 3 or more, in a braindance, with no session                                                 | hidden in both                                                     |

### 5. Small presence details

| Detail                   | Verdict                                                                                                                                                                                 |
| ------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| flashlight, weapon light | **Nothing to sync**: vanilla V has neither (only debug hooks for components its entity lacks; no weapon light exists in scripts). If a mod adds one, a `kPresence*` flag is free for it |
| look-at toward the aim   | In the pose: head, neck and spine rotations are streamed as V's own body has them. Without a pose stream (the mock player) the body faces its heading. Nothing added                    |
| crouch                   | Done before (stance → the NPC body's crouch wrappers); the sneak gait now sounds like one                                                                                               |
| cover                    | V has no cover stance: leaning out is its pose. Nothing to add                                                                                                                          |
| swimming, ladders        | Placement done by the movement work; now a ladder is heard rung by rung and water is silent. Swim strokes and splashes: **open**                                                        |
| holstering               | Seen through `Held` (the hand empties); now heard                                                                                                                                       |

## Tests

- `tests/presence_test.cpp`: strides and gaits, a mock player with no
  flags, silence when still, swimming, sliding or teleported, the first
  step, jumps and landings by state and by fall speed, ladders, climbs and
  dashes, pain from health (not from a gain or a tick), draws and holsters,
  down, abilities, health in a byte, the send gate, the HUD's status
  priority, the bearing and its arrow, the screen projection, distances,
  the nameplate's fade, the flinch.
- `tests/pose_test.cpp`: the flinch leans the upper body about the body's
  axes and leaves hips and legs alone.
- `tests/protocol_test.cpp`, `tests/endpoint_test.cpp`: the three messages
  round-trip, travel on the right channels, stay small, are stamped by the
  host, reach late joiners (Presence) or never do (Cue), and only the host
  sends reactions.

## In game

Scenarios (`tools/scenario.sh NAME`), each checking the game's log:

| Scenario                  | Proves                                                                                                |
| ------------------------- | ----------------------------------------------------------------------------------------------------- |
| `presence-audio`          | footsteps (their source), the shot and its bullet, pain and the flinch, holster and draw              |
| `presence-moves`          | jump, landing, climb, the blade's swing and its impact                                                |
| `presence-hud`            | the entry, health, menu, down, off screen with its arrow, the nameplate; logs the camera's projection |
| `presence-reactions`      | as a guest: impact, stagger, knockdown, ragdoll and its end on a proxy                                |
| `presence-reactions-host` | as host: a guest's hits make our NPCs react, and each reaction goes out                               |

By hand: `tools/game.sh cmd presence report | steps auto|on|off | vo NAME |
sound NAME | fx NAME | flinch | bullet | hud [at X Y | fov h|v | camera] |
react TYPE`.

The mock player: `--hold RECORD`, `--holster-at N`, `--shoot N` (now also
as host, with the bullet's end), `--melee N`, `--hurt N`, `--menu-at N`,
`--react-npcs N` (host).

## Open

- Everything tagged "needs ears / eyes" above.
- Swim sounds; smart and lobbed rounds' flight; a remote V's footsteps as a
  stimulus for the host's NPCs (their noise travels as `Noise` for
  gunshots only).
- The world marker and the party entry both say where the partner is: once
  both are seen in game, decide whether the entry's arrow replaces the
  marker's distance text.
- Localized strings for the entry's state line.
