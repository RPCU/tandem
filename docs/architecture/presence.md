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
- **Sent: only what the receiver cannot know.** Four messages, appended
  to the protocol with no version bump:

  <!-- prettier-ignore -->
  | Message | Channel | Size | Carries |
  | --- | --- | --- | --- |
  | `Presence` | Event, kept for late joiners | < 16 | a V's health (a byte) and flags (in a menu, photo mode, in combat); at once on a loss or a flag, else 5 Hz at most |
  | `Cue` | State (a late one is worthless) | < 32 | where a shot's bullet ended; that a melee swing landed, and where |
  | `NpcReaction` | State for a twitch, else Event | < 40 | host only: the hit reaction one of its NPCs starts, as the animation feature the game filled |
  | `StatusVisuals` | Event, kept for late joiners | < 160 | the status effects that show on a V (burning, shock, a heal's glow…): their record ids, the whole set on every change, at most 16 |

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

<!-- prettier-ignore -->
| The owner… | Partner sees and hears today | After | Needs ears / eyes |
| --- | --- | --- | --- |
| walks, runs, sprints, sneaks | the body moves; footsteps only if the puppet's own walk clip raises them (never checked), at the clip's cadence | the puppet's own are detected (`OnAudioEvent`); where it has none, a foot is planted per stride (`entFootPlantedEvent`: the engine picks surface and footwear), by gait: sneak 0.55 m, walk 0.72, run 1.15, sprint 1.55 a step | both sources never doubled; right surface; cadence against the body (`cmd presence steps auto\|on\|off`) |
| climbs a ladder | silent | a plant per rung (0.45 m) | the sound suits a ladder |
| jumps, double or charged jump | silent | a plant on take-off; `lcm_player_double_jump` for the second or charged one | that `lcm_*` event is positional on a puppet |
| lands | silent | two plants and V's landing effort (voice `regularLanding` / `hardLanding` / `veryhardLanding`), the `landing_hard` effects; hardness from the state machine's own landing state, else from the fall's speed | voice resolves on the puppet (see voices) |
| dashes, slides, climbs, vaults | silent | `lcm_dash`, `lcm_fs_additional_slide_stop`, voices `climbStart`, `Vault` | as above |
| swims | body follows; silent | no footsteps in water (unchanged otherwise) | swim sounds: **open** |
| fires | the weapon's own effects (`TriggerWeaponEffects`: flash, shells, report — sounds never checked) | also the weapon's `Shoot` animation event, as the game pushes for an NPC's shot; the fire loop ended 0.3 s after the last shot (`ExitShoot`); the bullet itself (below) brings its own whizz and impact sounds | the right gun's report, 3D, occluded; no stuck loop on automatic fire; no doubled report |
| reloads | `EnterReload` / `ExitReload` effects | also the weapon's `Reload` animation event | the reload is heard |
| swings a melee weapon | the NPC swing sound (`AI_PlayMeleeAttackSound`) | also V's effort (voice `meleeAttack`) | — |
| lands a melee hit | nothing | `Cue` MeleeHit → the weapon's own `MeleeHit` effects on the puppet's weapon | the impact sound matches the weapon |
| draws, swaps, holsters | the item appears or disappears in the hand | draw: the engine's own equip notification when the item enters the slot (nothing added); holster: `AudioNotifyItemUnequippedEvent` for the item put away | both are heard; if the draw is silent, queue the equip notification too |
| uses the mantis blades, other arms | held and swung as any weapon | as any weapon (draw, swing, impact) | the unsheathing sound |
| triggers the Sandevistan | the trail effect on the puppet (Time.reds); silent | the sounds of V's own status effect (`SandevistanPlayerBuff`'s SFX records) played on the puppet, stopped when it ends; Kerenzikov the same | those sounds exist and are positional (they may be authored for V's own ears) |
| is hurt | nothing (on the host, the hit reaction of a reported NPC shot) | a fall of its health → V's pain (voice `onPlayerHit`), by how much health was lost | voice |
| goes down | the collapse (pose), the outline | a cry (voice `stlh_death`: V's own death cry is native and has no script name) | whether that context suits; else pick one by ear (`cmd presence vo NAME`) |

**Voices.** The puppet records have an empty `voiceTag`. If V's contexts
are silent on them, the fix is V's own voice tag on the records, by body:
one run with `cmd presence vo onPlayerHit` decides.

### 2. Hit reactions

<!-- prettier-ignore -->
| The owner's game shows… | Partner sees today | After | Needs eyes |
| --- | --- | --- | --- |
| an NPC twitches under fire | a proxy twitches only for our own hits | every twitch the host's NPC plays (`NpcReaction`, unreliable), except the echo of our own hit | — |
| an NPC is hit hard: impact, stagger, pain, knockdown | **nothing**: those are tasks of the NPC's AI, and a proxy has none | the host sends the reaction with the animation feature its AI filled; the proxy launches the same animation action (`ActionHitReactionScriptProxy`) with it | the action plays with the brain off; the proxy stays where the host has it |
| an NPC blocks, parries, has its guard broken | nothing | the same (feature + `PlayBlock` / `PlayParry`) | — |
| an NPC ragdolls (explosion, fall, thrown) and gets up | only a knock-out ragdolled | every ragdoll, with its impulse, and its end | getting up without its AI |
| an NPC dies | the proxy is killed: no death animation plays without the AI | the host's death animation (direction, body part, variation) or its ragdoll; a proxy told neither within 0.6 s ragdolls, so a dead proxy never stands | the animation matches the host's |
| a V is shot | on the host only: the hit reaction of an NPC shot its guest reported | on every game: a twitch of the puppet's body and a flinch over its pose (0.45 s, the way the hit went when known), from the fall of its health; the body never moves from where its player has it | the flinch reads well at each strength (`cmd presence flinch`) |
| a V staggers, is knocked down | its body does it (pose stream; `Felled` places the body freely) | unchanged: the owner's body is the truth | — |
| a V would lose a limb | never (Outcomes.reds, `KeepsLimbs`) | unchanged; the puppet's twitch carries no wound | — |

### 3. Bullet visuals

<!-- prettier-ignore -->
| The owner… | Partner sees today | After | Needs eyes |
| --- | --- | --- | --- |
| fires a hitscan weapon | flash, report, shells; no bullet | `Cue` Trace: where the crosshair's ray ended (the game's own look-at query). The partner's game fires a real NPC shot of the puppet's weapon at that point: the weapon's own tracer, impact, decal and sounds. No ammunition, the Vs present are not hit, its hits are dropped | tracer from the muzzle to the right place; decal; nothing takes damage |
| fires a smart or lobbed round | flash, report | unchanged: their flight is not a line (**open**) | — |
| (host) an NPC fires | flash, report, shells on the proxy | the same real shot from the proxy's weapon at the target the host already sends in `NpcShot` | as above; an NPC shot at our own V is still the one Shots.reds decides |

What breaks in the world is told and replayed whatever broke it
(`devices.md`, "Glass and breakable scenery"); a barrel is a device.

### 4. Party HUD

Built in script from the game's ink widgets, font and colour styles, on
the HUD layer (`scripts/tandem/PartyHud.reds`).

<!-- prettier-ignore -->
| About the partner | Today | After | Needs eyes |
| --- | --- | --- | --- |
| who has an entry | — | **linked partners only** (both consent, [`comms.md`](comms.md): `TandemCommsNative.LinkState` 3, `link = auto` links everyone from the start); a link broken or a partner gone (`PlayerLeft`) takes it away at the next frame. An unlinked partner keeps its marker and a nameplate with its name alone | that a break is seen at once |
| name | on the world marker and the map tooltip | an entry under V's own health bar: the name in the HUD's font, in the partner's colour (`TandemMarkers.ColorOf`) | place and size on screen (`cmd presence hud at X Y`) |
| health | not shown | a bar under the name (`Presence`, a byte), in the partner's colour on a dim track of it | colours against the HUD's |
| down / out | a notice, the outline, the revive prompt | "Down", "Reviving 7" while we hold the revive (its countdown), "Out"; the bar empties | — |
| distance and direction off screen | the world marker | under the bar when the camera does not see them: an arrow turned their way and "124 m" / "1.2 km", in their colour | the arrow's sense |
| in a scene, elsewhere, in a menu | notices only | one line in the HUD's red: "In a scene" (their tier 3 and up), "Cyberspace" / "Braindance"…, "In a menu" (the pause menu or any full-screen menu); most important first; keyed texts (`Party-*`, English in the log) | — |
| nameplate | only the game's own, when aimed at | the name alone over the puppet's head, in their colour, for every partner, linked or not (health is the party entry's only: the user's call, 2026-11-19); hidden with the body. `nameplates = near` (default): full to 12 m, gone at 20 m; `always`: at any distance in view, full to 12 m then smaller and fainter to a floor at 150 m (55 % size, 50 % opacity, `NameplateLookOf`), only the near ones while our V talks (scene tier 2); `off`: none | its place over the head (the camera's field of view: see research); `always` in a crowded street |
| the marker's name | its world label | one name over a player: the marker's label hides while the nameplate shows (`TandemPartyHud.PlateShown`), and with `nameplates = off` | no flicker at 20 m |
| photo mode, cinematics | — | all of it hidden in photo mode, when our own scene tier is 3 or more, in a braindance, with no session | hidden in both |

### 5. Layout: where the list sits, what it yields to

"Make sure it doesn't overlap with holo call or anything else" (the user,
2026-11-19). The list is ours on the game's HUD layer, not one of its
modules, so it follows the game's own signals itself
(`TandemPartyHud.Layout`, every frame). **Nothing here is measured on
screen yet**: the places are first picks from the vanilla scripts and the
HUD as it is played; scenario `hud-layout` screenshots every case for a
person to check, and `presence hud at X Y` / `call-at X Y` move the two
places without a rebuild.

**Units.** The HUD layer's virtual window, in the HUD's own units: the
window at the UI scale (`UISystem.GetCurrentWindowSize()` x
`GetInverseUIScale()`): 3840 x 2160 at 16:9, wider at 21:9 (5160 x 2160 at
3440 x 1440), taller at 16:10. The list is anchored top left, as the
health bar is, so it keeps to the left edge at every aspect and scales with
the UI. The nameplates are placed in the same units with the screen's own
aspect (they assumed 16:9 before).

**The vanilla widgets, by place, and the rule for each** (2.50 scripts:
the controllers named, `inkHUDGameController.ShowRequest` / `HideRequest`
toggling their root's visibility):

<!-- prettier-ignore -->
| Vanilla widget | Where | Shares space with the list? | Rule |
| --- | --- | --- | --- |
| health bar, buffs, cyberware cooldowns (`healthbarWidgetGameController`, `inkCooldownGameController`) | top left | the list sits under them (96, 340) | fixed: the list starts below them |
| holocall / audio call portrait (`NewHudPhoneGameController`, `HoloAudioCallLogicController`) | left column, under the health bar | **yes** | while `UI_ComDevice.PhoneCallInformation` is in its `IncomingCall` or `StartCall` phase: the list moves below the portrait (96, 1080) and turns compact |
| dialogue choice hub (`dialogWidgetGameController`), subtitles | right of centre; bottom centre | no | while `UIInteractions.DialogChoiceHubs` has a hub: compact in place (a conversation needs no arrow or distance) |
| boss health bar (`BossHealthBarGameController`) | top centre | no, at any aspect from 16:10 to 32:9 | nothing to yield: logged (its root's visibility), the partners' health stays whole in a boss fight |
| minimap, wanted level, quest tracker and objectives, district name (`MinimapContainerController`, `WantedBarGameController`, `QuestTrackerGameController`) | top right | no | none |
| ammo and weapon (`WeaponRosterGameController`), input hints (`InputHintManagerGameController`, ours too) | bottom right | no | none |
| hotkeys (phone, consumable, grenade, `HotkeysWidgetController`), radio toast (`HotkeyCustomRadioWidgetController`) | bottom left | below the list | the list never goes into the bottom 420 units (`BottomClear`): with more entries than fit, it hides ("no room"; 3 partners always fit) |
| item, XP, level, journal, zone-alert toasts (`ItemsNotificationQueue`, `JournalNotificationQueue`, `LevelUpNotificationQueue`, `ZoneAlertNotificationQueue`) | right side, top centre | no | none |
| our notices (the warning line) | top centre | no | none |
| interaction prompts (`InteractionsHubGameController`), stealth indicator (`StealthIndicatorGameController`) | around the crosshair | no | none |
| vehicle HUD (`hudCarController`: speed, gear) | bottom centre / right | no | none |
| the whole HUD hidden (cinematic camera, the `Empty` UI context, a taxi ride: `DelamainTaxiSystem` pushes `UIGameContext.DelamainTaxi`) | — | — | when every module we follow (health bar, minimap, quest tracker, found on the HUD layer with Codeware's `GetGameControllers`, again every 2 s) is hidden, the list and the nameplates hide too; one module turned off in the game's settings does not count |

**Compact**: the name, the bar, and the state line when there is one
(Down, Reviving…); no arrow or distance; 12 units between entries instead
of 28.

**The setting**: `party_hud = on|off` (default `on`, the settings page,
applied at once): off hides the list alone; nameplates are `nameplates`'
own. **No input to toggle it in game**: every free action is a chord or a
debug binding, the tag key already pings and links (`comms.md`), the phone
key answers offers; and the vanilla HUD has no "hide HUD" key of its own to
follow (2.50 hides it by context, which the list follows). The settings
page is the toggle.

**Test commands**: `presence hud holocall|boss|choices|hudhidden
on|off|auto` force a case (auto: the game's word), `presence hud at X Y`,
`presence hud call-at X Y`, `presence report` (the layout and why the list
is hidden), `coop set party_hud on|off`. Each change of layout is logged:
`hud: layout below the holocall (compact) at 96 1080, HUD 3840 x 2160,
holocall up`; `hud: party list hidden (party_hud off)`.

### 6. Small presence details

<!-- prettier-ignore -->
| Detail | Verdict |
| --- | --- |
| flashlight, weapon light | **Nothing to sync**: vanilla V has neither (only debug hooks for components its entity lacks; no weapon light exists in scripts). If a mod adds one, a `kPresence*` flag is free for it |
| look-at toward the aim | In the pose: head, neck and spine rotations are streamed as V's own body has them. Without a pose stream (the mock player) the body faces its heading. Nothing added |
| crouch | Done before (stance → the NPC body's crouch wrappers); the sneak gait now sounds like one |
| cover | V has no cover stance: leaning out is its pose. Nothing to add |
| swimming, ladders | Placement done by the movement work; now a ladder is heard rung by rung and water is silent. Swim strokes and splashes: **open** |
| holstering | Seen through `Held` (the hand empties); now heard |

### 7. Names

- **A pseudo for everyone** (`tandem.ini name`, default `V`): the co-op
  menu asks for one in the game's own text popup (Codeware's
  `GenericMessageNotification.ShowInput`, as the settings page's typed
  values) the first time a player hosts or joins while the name is still
  `V` (or empty), once a game run (`TandemNative.NameToAsk`, `NameAsked`):
  main menu Host a game / Join a game, pause menu Host this game / Join a
  game. A refused name asks again with the reason; cancel plays as `V`
  for now (asked again at the next launch); then the page picked goes on.
  Saved through the settings native (`SetSetting`), so the settings page
  changes it too.
- **What a name is** (`core/include/tandem/names.hpp`): 1 to 16
  characters (code points: "Hélène" is 6), trimmed of spaces, valid UTF-8,
  no control or invisible format character (C0, C1, DEL, zero widths,
  direction overrides, line separators, the byte order mark), no `;` or `#`
  (tandem.ini's comment signs: `names::Problem`). `tandem.ini`'s parser
  refuses any other (`ApplySetting`, `plugin/src/config.cpp`).
- **Unique in a session**, by the host: a guest whose name another player
  has (the same but for ASCII case) is shown as "V (2)", "V (3)", the first
  free number (`names::Unique`); whatever a guest sent is sanitized first
  (`names::Sanitize`: controls dropped, cut to 16 characters, nothing left
  is `V`). The roster carries the shown name, so every game names the
  player the same way; **no protocol change** (`Hello::name` and
  `PlayerInfo::name` keep their meaning: asked, and shown). A guest learns
  its own from the roster (`events::NamedAs`, the notice "V is taken in this
  session: the others see you as V (2)", `Session-Renamed`). A returning
  player (`session.md`) is matched by the name it asked for, so it keeps
  "V (2)". A name freed by a player leaving is given again.

### 8. Status effects

What the owner's V wears that shows on it (burning, shock, EMP, poison,
bleeding, overheat, the user's OverclockOverheat mod's burning, optical
camo, a Sandevistan's or Kerenzikov's trail, Berserk, a heal's glow, any
cyberware activation with a look) is seen on its puppet: the game's own
effects for that status effect, never its gameplay
(`scripts/tandem/StatusVisuals.reds`; engine facts
[`../research/engine/combat.md`](../research/engine/combat.md) §13.75).

- **Which ones, with no hand list.** A record shows when its own `VFX` list
  names an effect, or one of its packages runs a visual effector:
  `PlayVFXEffector` (its `vfxName`), `ApplyShaderEffector` (a material
  override on the owner or its weapons), `ApplyShaderOnObjectEffector` on
  its owner. Its `SFX` play with it. Every other effector is gameplay and
  is left out. The same reading on both sides, from TweakDB alone
  (`TandemStatusVisuals.Read`), so a vanilla record a mod applies (the
  burning of OverclockOverheat) is the same case as the game's own.
- **Sent as state.** Four times a second the owner's game lists its V's
  applied effects that show (`StatusEffectHelper.GetAppliedEffects`) and
  sends the set when it changes (`StatusVisuals`: sorted, at most 16,
  `presence::StatusSet`; the host stamps the player, keeps everyone's
  latest for late joiners and in a resync, an empty set too, and relays
  it). A repeat is neither sent nor applied.
- **Played, not applied.** The receiver plays each record the way
  `GameObject.OnStatusEffectApplied` plays any status effect on a
  character: `StartEffectEvent` for each VFX name, `PlaySoundEvent` for each
  SFX name, and the visual effectors' own action (the material override as
  a static game effect enabled for the puppet), then `BreakEffectLoopEvent`,
  `StopSoundEvent` and the override off when the record leaves the set
  (`presence::StatusDiff`). Names two records share are counted, so the
  first to end does not break the other's. **Why not apply a copy of the
  status effect to the puppet**: a real copy runs its damage ticks and stat
  modifiers on the puppet, its AI reactions (an NPC on fire runs about),
  and every hit on a puppet is forwarded to the V it stands for
  (`Combat.reds`): the burn would hurt that V a second time. The events
  alone are the effect's whole look with none of that.
- **Optical camo** stays Stealth's (`stealth.md`): the puppet wears the camo
  record itself, so it is as transparent as the V (the same record, its own
  material), and the host's NPCs meet the vision blocker the V's game
  decided on. The status visuals never send a record tagged
  `CamoActiveOnPlayer`, and never play a record the puppet already wears
  for real.
- **Time abilities**: a Sandevistan's or Kerenzikov's record plays its
  sounds once: whichever of the ability's sounds (`Ability`, from
  `TimeEffect`) and the record's starts first keeps it.
- **Consistency**: a hidden puppet (another world, a private scene's fade)
  shows nothing until it is back; a rebuilt body starts afresh; a player
  who leaves or vanishes takes its effects with it (`ForgetPlayer`); the
  session's end stops every one and clears our sent set.
- **A record the receiving game does not have** (a mod's, installed on one
  side only) is skipped, with a notice once a session ("NAME shows an
  effect this game doesn't know (a mod?): not shown.", `Status-Unknown`).
  No mod-compat resolver is in integration yet; this is where one would
  map such a record.
- **Logs**: `status: our V shows N effect(s) to the others: …`, `status:
player P shows REC on its puppet (E effect(s), S sound(s), M
material(s))`, `… no longer shows REC …`, `status: first play of NAME on
a puppet`, `status: player P's effects cleared (puppet gone)`.

## Tests

- `tests/presence_test.cpp`: strides and gaits, a mock player with no
  flags, silence when still, swimming, sliding or teleported, the first
  step, jumps and landings by state and by fall speed, ladders, climbs and
  dashes, pain from health (not from a gain or a tick), draws and holsters,
  down, abilities, health in a byte, the send gate, the HUD's status
  priority, the bearing and its arrow, the screen projection, distances,
  the nameplate's fade, near / always / off (size and opacity by
  distance), the flinch; status visuals: one set (sorted, no repeat, the
  bound), what starts and stops as it changes, a vanish.
- `tests/names_test.cpp`: typed names (trim, length in characters, UTF-8,
  controls, comment signs), the default, what a host shows for anything
  sent, unique names.
- `tests/endpoint_test.cpp`, over loopback: three Vs and a "v" are shown
  as V, V (2), v (3), each guest told its own once; the name freed by a
  leaver is given again; a namesake of a live player is "a (2)"; a
  returning player keeps its shown name.
- `tests/pose_test.cpp`: the flinch leans the upper body about the body's
  axes and leaves hips and legs alone.
- `tests/protocol_test.cpp`, `tests/endpoint_test.cpp`: the four messages
  round-trip, travel on the right channels, stay small, are stamped by the
  host, reach late joiners (Presence, StatusVisuals) or never do (Cue), and
  only the host sends reactions; a status set refused unsorted, repeated or
  over the bound; over loopback, a guest's set reaches the host stamped, a
  repeat says nothing, a cleared set is an empty set, a late joiner hears
  both, a vanished guest's goes everywhere.

## In game

Scenarios (`tools/scenario.sh NAME`), each checking the game's log:

<!-- prettier-ignore -->
| Scenario | Proves |
| --- | --- |
| `presence-audio` | footsteps (their source), the shot and its bullet, pain and the flinch, holster and draw |
| `presence-moves` | jump, landing, climb, the blade's swing and its impact |
| `presence-hud` | the entry, health, menu, down, off screen with its arrow, the nameplate; logs the camera's projection |
| `hud-layout` | screenshots of each case: the list under the health bar, below a holocall (compact), with the boss bar, with the choice hub (compact), the game's HUD hidden, `party_hud = off` |
| `hud-linked` | a linked partner's entry with health, its plate the name alone; the link broken from its side: no entry, the plate stays; `nameplates = always`; a vanish takes plate and marker |
| `names-prompt` | the pause menu's Host this game asks for a name while it is V; a bad one asked again; a good one saved, then hosting |
| `names-duplicate` | two mock players named V and v beside our V: shown as V (2) and v (3), each told its own, markers and HUD entries under those names |
| `presence-reactions` | as a guest: impact, stagger, knockdown, ragdoll and its end on a proxy |
| `presence-reactions-host` | as host: a guest's hits make our NPCs react, and each reaction goes out |
| `presence-status` | a partner burns, goes camo and heals: each record's visuals on its puppet, Stealth's camo worn for real, an unknown record skipped with its notice; our own V's effect sent; a vanish clears its puppet |

By hand: `tools/game.sh cmd presence report | steps auto|on|off | vo NAME |
sound NAME | fx NAME | flinch | bullet | hud [at X Y | fov h|v | camera] |
react TYPE | status apply|remove REC | status block|unblock NAME |
status report`; `coop name` (the name, and whether the menu would ask),
`coop name reset` (ask again), `coop name answer TEXT` / `coop name
cancel` (answer the open name popup), `coop set nameplates
near|always|off`.

The mock player: `--hold RECORD`, `--holster-at N`, `--shoot N` (now also
as host, with the bullet's end), `--melee N`, `--hurt N`, `--menu-at N`,
`--react-npcs N` (host), `--unlink-at N` (breaks its links), `--name NAME`
(logs `the session shows us as …` when the host renamed it), `--status
REC@FROM-TO[,…]` (status effects on its V), `--camo N --camo-record REC`.

## Open

- Everything tagged "needs ears / eyes" above.
- Swim sounds; smart and lobbed rounds' flight; a remote V's footsteps as a
  stimulus for the host's NPCs (their noise travels as `Noise` for
  gunshots only).
- The world marker and the party entry both say where the partner is: once
  both are seen in game, decide whether the entry's arrow replaces the
  marker's distance text.
- The name popup answered by `coop name answer` calls the token's
  callback: whether the popup also closes (else the command must close it
  as Codeware's `Close` does) is to see in game.
- Codeware's text field draws what is typed in capitals: whether its
  `GetText` gives the case typed (what is saved and shown) is to see in
  game.
- Status effects, to see in game: that the player's records' effect names
  resolve on the puppet's body (`player_*_tpp.ent`), and to a body effect,
  not to the first-person screen effect the same name means on V's own
  entity (a burning screen on the partner's view would be a bug: `presence
status block NAME` keeps a name off while it is fixed); whether V's own
  camo record runs its material on an NPC body; which records are on a V
  all the time and so always sent (the first `status: our V shows` line);
  whether Time.reds' guessed Sandevistan trail (`sandevistan_medium_00`) is
  still needed once the record's own effect plays.
