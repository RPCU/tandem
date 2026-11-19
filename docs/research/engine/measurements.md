# tandem's own measurements

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 12. tandem's own measurements (game 2.50, Proton, anya)

Measured in this project; each line says how.

- **Movement-controller footprint**: right after `AttachController` (4018412273)
  the engine writes the 32-bit value `1` at controller **+0x20** and nothing
  else in +0x20..+0x7F (zero-filled before attach, dumped at the first
  `SendAnimationParameters`, same on every run). Anything stored at +0x20 is
  lost: an id placed there read back as 1. NCO's layout had its target
  position there, masked by rewriting it from the main thread every frame.
  [2.5-verified]
- **The engine-attached controller works on 2.5** with the 19-slot layout of
  §3, a `kType` marker at +0x08 and the `IdleController::SetAnimation` hook
  skipping our own controllers: `GetDeltaTransform`'s delta moves the body
  to within **0.01 m** of the target at ~60 frames/s, with the engine's
  walk animation (`MotionAction::Move`, `LocomotionStyle::Walk`, clip time,
  speed). No crash in ~10 minutes over 5 runs. [2.5-verified]
- **Clip lengths** (`GetAnimLength` 3750304414): `Character.Panam`'s rig:
  `walk_0` 2.67 s, `sprint_0` 0.57 s, **no `jog_0`** (-1). The player's
  `player_wa_tpp_cutscene.ent`: all three **-1** (no NPC locomotion clips, the
  "glide"). Missing clips answer -1, not 0. [2.5-verified]
- **Templates present in 2.50** (`basegame_4_appearance.archive`, found by
  FNV1a64 path hash in the archive indexes): `player_{wa,ma}_tpp.ent`,
  `player_{wa,ma}_tpp_cutscene.ent` under `base\characters\entities\player\`;
  no repacked copy is needed to reference them. [2.5-verified]
- **Main menu world**: its background scene (V's arm in the junkyard) attaches
  a player: `ScriptableSystem.OnPlayerAttach` fires for it, at positions near
  the world origin, with a **female** body whatever the save's V is.
  `GameInstance.GetSystemRequestsHandler().IsPreGame()` (Codeware's
  accessor, the engine's flag) is **true** there and **false** in a loaded
  save: the reliable test. [2.5-verified]
- **Script systems are per world**: a loaded save gets a new instance of each
  `ScriptableSystem` (`OnAttach` runs again). [2.5-verified]
- **Codeware session events don't reach a script system after a save loads**:
  `Session/Start` and `Session/Ready` arrive for the main menu (pre-game),
  none for the loaded game, sticky or not. Codeware also drops every
  non-sticky callback on world detach. Use `OnPlayerAttach` + `IsPreGame()`.
  [2.5-verified]
- **Script callbacks run on several threads** (thread ids differ between
  `OnAttach`, `OnPlayerAttach`, delay callbacks): natives called from scripts
  must be thread-safe. [2.5-verified]
- **redscript has no `continue`**; a compile error opens a modal dialog that
  blocks the main thread, and on a headless display the engine watchdog then
  fires after 120 s. [2.5-verified]
- **Startup hangs happen without tandem's involvement too**: 2 of ~8 launches
  hung on the logo or behind a dialog (watchdog dump in
  `pfx/.../REDEngine/ReportQueue/`), the player's mod list being the other
  suspect (see the anya notes). Not yet attributed. [open] In test
  instances (`docs/architecture/two-instances.md`, 2026-11-19) the phase
  between the script compile and the audio's start takes about 50 s alone
  and over 2 min with a second game and builds on every core; past 120 s
  the engine watchdog fires (`Watchdog timeout!`, engineWatchdog.cpp:224)
  and the process hangs in the crash handler: 2 of about 10 instance starts.
  [2.5-verified]
- **Frame rate under Proton without an overlay**: `WINEDEBUG=-all,+fps`
  makes Wine log `trace:fps:win32u_vkQueuePresentKHR … @ approx 30.01fps`
  at every present (vkd3d-proton presents through winevulkan).
  [2.5-verified]

### 12.1 V's body for a remote player (2026-11-17, probe runs on anya)

Measured with the probe (`tandem.ini probe = on`, scripts/tandem/Probe.reds),
which spawns bodies in rows and logs their components.

- **V's templates carry no meshes**: `player_{wa,ma}_tpp(_cutscene).ent`
  extract with character-customization controllers and no mesh component.
  Without a look the puppet is invisible. [2.5-verified]
- **The look**: the live state of `GameInstance.GetCharacterCustomizationSystem(game).GetState()`,
  its keys collected through `gameuiICharacterCustomizationState` vtable
  slots 0x190 / 0x1A0 / 0x1B0 (head / body / arms; slot 0x120
  IsBodyGenderMale answers right, the check used before calling them), groups
  TPP, face, hairs, (beards), TPP_Body, breast, lifted_feet, flat_feet,
  holstered_default, nails: 15 keys for a male V, scheduled with 386815609 on
  `GetRuntimeSystem<RuntimeSystemEntityAppearanceChanger>()` (RedLib
  mapping 34); the completion callback runs, the body shows face, hair,
  tattoos. [2.5-verified]
- **`player_*_tpp.ent` draws only a shadow** (it is the first-person player's
  third-person body); `player_*_tpp_cutscene.ent` draws. [2.5-verified]
- **NPC locomotion lives in `entAnimationSetupExtensionComponent`s**
  ("Gameplay Animation Setup" 125 sets, "Special Locomotion Setup",
  "Fear Extension", "Character Entity Animation Setup"…), not in the root
  `entAnimatedComponent`'s own setup nor in the `AnimationControllerComponent`
  (identical on every body: actions db 4960121789495904435). V's templates
  have " Player TPP Animation Setup" (4 sets) and an empty "Character Entity
  Animation Setup". `GetAnimLength` finds `walk_0` once the donor's extension
  sets are added to V's "Character Entity Animation Setup" (bound to V's
  root) and the root runs the donor's graph (11281804297281181379, shared by
  NPCs): male V 3.10 / 1.73 s (walk / sprint, from Kerry), female 2.67 /
  0.57 (Panam). No `jog_0` on any. [2.5-verified]
- **Edits in Codeware's `Entity/Extract` reach the live entity** (read back
  from `entity.GetComponents()`); edits in `Entity/Assemble` came too late or
  never ran. Setup hashes change with the edit (`hash` + constant) since the
  engine may key setups by them. [2.5-verified]
- **Skeletons** (root rig hash): female V = Panam = 10037276762472186253;
  male V = Kerry = Saul = Takemura = Mitch = 14264052059588999261; Jackie
  1707357870198610305 (man_big). Records `Character.Kerry`, `Saul`, `Takemura`,
  `Mitch`, `Jackie` exist; `Kerry_Eurodyne`, `River_Ward`, `Viktor_Vektor`,
  `Goro_Takemura`, `Johnny`, `Dex`, `Viktor`, `River` do not. [2.5-verified]
- **Unregistering a Codeware callback while a job thread dispatches it hung
  the game** (watchdog dump, no exception): register once per world, gate the
  handler with a flag. [2.5-verified]
- **Clothes: solved 2026-11-18, see §13.4 (the record's `genders`).** `GiveItem` + `AddItemToSlot` (script) return true for the
  local V's 4 worn items, before or after the look, with the PuppetPS byte
  at +0x72 set (`gamePuppetPS` RTTI size 0x78; `gameObject.persistentState`
  found by RTTI property offset) and with clothing `attachmentSlots` added to
  the record; afterwards `GetItemInSlot` is empty for every clothing slot and
  nothing is drawn. [2.5-verified, unsolved]
- **Dev loop**: RedHotTools v1.3.0-beta.13 hot-reloads scripts (`.hot-scripts`
  next to its DLL, ~1.7 s) and tweak values, but **not a record's
  `entityTemplatePath`**, and after a script compile error it stops
  compiling until the game restarts. Load Begone (Nexus 8144) loads the last
  save at launch with Mod Settings' `user.ini` section
  `[LoadBegone.LoadBegoneConfig]` (`enabled`, `skipRecapVideos`): the section
  is the script class's full name, module included. [2.5-verified]
- **NPC bodies can't show a player's moves.** Applying `stanceState`
  (AnimFeature_NPCState) = Crouch, the `inCrouch` anim wrapper (what
  `NPCStatesComponent.OnStanceStateChanged` does), `GamePuppetPS.SetCrouch`,
  high-level Stealth, and `AnimationData.areAnimWrappersUnlocked` (+0x34 of
  the locomotion data) leave a controller-driven NPC (pure Panam too)
  upright: no animation set of Panam, Kerry or the 6th Street gang records
  (`Character.rcr_sixthstreet_hooligan_melee1_knife_{wa,ma}`, V's skeletons,
  292/309 sets) is gated by `inCrouch` or `stealthLocomotion`. NPCs crouch in
  cover only. [2.5-verified]
- **Anim features the engine applies** (trace: hook on 316482401
  `ApplyFeature(component, CName, Handle<AnimFeature>*)`, works on 2.5): to
  NPCs `locomotion` (animAnimFeature_Locomotion), `crowd_locomotion`,
  `stanceState` / `upperBodyState` / `highLevelState` (animAnimFeature_NPCState);
  to the local `PlayerPuppet` `locomotion`, `LocomotionStateMachine`
  (AnimFeature_PlayerLocomotionStateMachine), `playerLocomotion`
  (animAnimFeature_PlayerMovement)… Crouching V adds no new name: the
  state travels as values. `NPCStatesComponent`'s feature names are
  `stanceState`, `upperBodyState`, `highLevelState` (read through an added
  method; V's template leaves them empty). `GamePuppetPS.GetTpp()` is the
  byte at PuppetPS+0x72. [2.5-verified]
- **Design consequence**: the remote V keeps V's own third-person graph and
  gets the local player's anim features mirrored (generic RTTI field copy,
  sent when they change); the movement controller only places the body.
- **The player's gameplay graph can't go on a puppet.** Mirroring every anim
  feature applied to the local `PlayerPuppet` onto a puppet of V's cutscene
  template (no controller, no NPC stance code) changes nothing when V
  crouches: that template's graph (14299736328643609073) is not the gameplay
  one. The live player's root runs graph 8975379029519062281 on rig
  7304775167351189356 (122 root sets + 115 from extensions); putting that
  graph and its sets on the third-person body (rig 14264052059588999261)
  crashed the game at spawn (2026-11-17 19:53). [2.5-verified]
