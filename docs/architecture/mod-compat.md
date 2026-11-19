# Other mods: what they add, and when the players' sets differ

The questions this answers: "is tandem going to support my mods, and mods in
general?" and "what if the other player doesn't have my mods?". tandem
plays alongside other mods; two players whose mod sets differ still play
together. Nothing is refused for a mod: what one game lacks is shown as the
nearest vanilla thing, or not at all, and **the player is told every time**
(a requirement: no silent path).

**State (2026-11-19):** built and tested offline (core unit and loopback
tests, the mock player, the scripts' compile check). **Not run in the game
yet**: [Verify in game](#verify-in-game) lists what the first session must
look at.

## 1. What a mod adds, and the comparison at a join

`core/include/tandem/mods.hpp` sorts a game's mods by **what they add**,
from their files (`mods::Classify`, `mods::Scan`; the plugin reads them at
startup, `main.cpp` `CheckInstallation`):

| Category   | Detected from                                                                                                                                                                                                                                                                                                        | In a session                                                                                   |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| local      | archives alone (`archive/pc/mod/*.archive`: textures, meshes, lighting, sound), redscript that only hooks UI classes (`*GameController`, `ink*`, menus, HUD) or defines a library, CET mods that only draw or observe, ArchiveXL `.xl` with only `localization`/`streaming`/`resource` patches, the known frameworks | fine either way: only that player's screen                                                     |
| items      | TweakXL new records under `Items.` (`$type`/`$base`, or `Name : Base` in `.tweak`), ArchiveXL `factories`, CET `TweakDB:CreateRecord("Items...")`                                                                                                                                                                    | degrade on the other side: shown as similar vanilla ones ([§2](#2-records-another-game-names)) |
| vehicles   | new records under `Vehicle.`                                                                                                                                                                                                                                                                                         | the same: a vanilla vehicle of the same type and size                                          |
| characters | new records under `Character.`                                                                                                                                                                                                                                                                                       | the same: a vanilla NPC of the same type                                                       |
| looks      | ArchiveXL `customizations` (character creator options), patches of `.app` files (NPC appearance packs)                                                                                                                                                                                                               | the same: vanilla pieces, or the NPC's usual look                                              |
| gameplay   | TweakXL changes to existing records' fields (stats, status effects, quickhacks, AI), new records elsewhere, redscript hooks of gameplay classes, REDmod `.script`, CET `TweakDB:SetFlat`, `Override`, `SetWeather`, `SetTimeDilation`                                                                                | a warning: the host's game decides shared NPCs, so the host's rules apply to them              |
| unknown    | a RED4ext plugin that is not a known framework (a DLL says nothing), or a script or CET mod read by name alone                                                                                                                                                                                                       | counted and said; nothing else                                                                 |

- **A mod is all its entries.** `Quickhack_Fixes.archive`, its `.archive.xl`,
  `r6/scripts/Quickhack_Fixes` and `r6/tweaks/Quickhack_Fixes` are one mod
  (`mods::Key`: lower case, no extension, no load-order prefix like `##` or
  `zz-`), with the categories of all of them. Two games compare a mod by
  the hash of that key: any version of a mod is the same mod.
- **The known frameworks** (`IsFramework`: ArchiveXL, TweakXL, Codeware,
  RED4ext, Mod Settings, Input Loader, RedFileSystem, RedData, Audioware,
  nativeSettings, cybercmd) count as local.
- **What it reads**: names everywhere; the text of `.reds`, `.yaml`, `.yml`,
  `.tweak`, `.xl`, `.lua` and REDmod `.script` files up to 512 KB each and
  32 MB in all (a game with thousands of mods starts as fast); never a DLL
  or an archive's content.
- **The exchange** (`proto::ModList`, appended, Event channel, never
  relayed, like `Install`): each side sends its mods once after `Install`,
  as **hashes and categories only** (`kModListed`). Names travel only when
  asked: `kModNamesAsked` carries the hashes one side wants, `kModNames`
  the answer, which holds only the answerer's own mods among those asked. The
  co-op menu's **Mods** popup asks when it opens. At most 512 mods, names of
  64 characters. No version bump: no existing message changed meaning
  (`Install.mods` stays the fingerprint of the names; release.md).
- **In words** (`Endpoint` raises `events::RemoteMods` with a
  `mods::Comparison`; `Mods.reds` words it from the counts, English and
  French): a notice at the join and a line in the co-op status panel, for
  example "Judy has mods you don't (3): 2 with new items (shown as similar
  vanilla ones), 1 changing gameplay (the host's rules apply to shared
  enemies)". **The join is never refused for mods.** The log has
  `mods::Describe`'s words, `last-session.txt` and the bug report too.
- Guests compare with the host only (like `Install`).

## 2. Records another game names

One resolver, `mods::Resolve(kind, record, hint)`; every place a TweakDB id
from another game reaches ours goes through it (`plugin/src/mods.cpp`
`ModRecords::Resolve`, one line at each site), memoized per record:

1. the record itself, when this game has it (`TweakDB::GetRecord`);
2. else a **vanilla record like it**, by the hint the sender sent;
3. else the kind's **fixed vanilla default**;
4. else **nothing** (shown as nothing, or refused).

Each stand-in is checked to exist in this game before it is used.

| Kind (`mods::Kind`) | Where                                                                                                                                 | Hint                                             | Stand-ins                                                                                                                                                                                                                          |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| weapon              | `Held` (`RemoteHeld`)                                                                                                                 | `rightHint`/`leftHint`: item type, item category | the same type (`Wea_Handgun` → `Items.Preset_Lexington_Default`, `Wea_Katana` → katana...), its category's, the handgun                                                                                                            |
| clothing            | `Appearance::items` (`RemoteItems`)                                                                                                   | `hints[i]`: the piece's item type                | a plain shirt, trousers or shoes for those slots; a hat, glasses, jacket or outfit is left off                                                                                                                                     |
| look                | `Appearance::look` (`ApplyRemoteLook`): a body or face resource                                                                       | none                                             | left off, the rest of the look is theirs (a modded hair shows as none)                                                                                                                                                             |
| vehicle             | `Seat` (`RemoteSeatVehicle`: the copy and the ride), a shared world vehicle (`TrafficSpawn`)                                          | `Seat::hint`: vehicle type, seats; traffic: none | bike → `Vehicle.v_sportbike2_arch_player`, AV → `Vehicle.max_tac_av`, car by seats (2: `Vehicle.v_sport2_porsche_911turbo`, 5 or more: `Vehicle.v_standard25_mahir_supron_player`), else `Vehicle.v_standard2_archer_hella_player` |
| character           | `NpcSpawn` (`TakeNpcEvents`, `SharedNpcs`)                                                                                            | `NpcSpawn::hint`: NPC type, gender               | a vanilla ganger of that gender (`Character.kab_tyger_claws_biker1_ranged1_saratoga_ma`, `Character.rcr_sixthstreet_hooligan_melee1_knife_wa`), in its own look                                                                    |
| character look      | an NPC's appearance name (`Npcs.reds`, the proxy as it is ready)                                                                      | none                                             | the body's own (the template's): noted when the body wears another than the one asked                                                                                                                                              |
| item                | a dropped bag's stacks (`WorldItem`), a shared quest item (`SharedItem`), a trade's other side, a quest reward, a garage car unlocked | none                                             | **never replaced**: refused (below)                                                                                                                                                                                                |
| effect              | a status effect on an NPC (`NpcEffect`, a handed-over NPC's)                                                                          | none                                             | none: not applied                                                                                                                                                                                                                  |
| radio               | `VehicleRadio`'s station                                                                                                              | none                                             | the radio off: a station past the game's own (`RadioStationDataProvider.GetStationsCount`) is a mod's (radioExt numbers its own after them); the driver sends one as off too                                                       |

- **Items are refused, never lost.** A dropped bag's stack our game lacks is
  left out of our copy (`Loot.reds`: taken here, so it stays in the host's
  bag for a player who has it, the dropper included); a shared quest item
  is not given (the looter keeps theirs); a reward or a car unlock is not
  given; a trade whose other side holds one is never agreed (`Trade.reds`
  `ToggleAgree`, notice `Trade-Unknown`), so nothing changes hands.
- **The hints** (`proto::RecordHint`: `type`, `tag`, `size`; added to `Held`,
  `Appearance`, `Seat`, `NpcSpawn` before protocol 7 left the developers'
  machines, so no version bump) are the game's own enum names as CName
  hashes: `gamedataItemType`, `gamedataItemCategory`, `gamedataVehicleType`,
  `gamedataNPCType`, the gender. `Mods.reds` `ItemHint`, `VehicleHint`,
  `NpcHint` make them on the sender; a message with no hint gets the kind's
  default.
- **Told, never silent** (`mods::Resolver`, no silent path): each
  replaced record is logged once (`mods: unknown weapon 0x... from Judy,
shown as Items.Preset_Lexington_Default`); the player gets **one notice
  per sender and kind**, after 2 s so a look's pieces come together, with
  the count ("Judy's clothes come from a mod you don't have: shown plain or
  left off (3 pieces)"); a later one of that kind from them is only listed.
  The co-op menu's **Mods** popup and `tools/game.sh cmd coop mods` list
  every replacement this session (whose, which record, shown as what).
  Refusals are notices that always show (`TandemText.Importance` 2); the
  others show unless `notices = minimal`.
- **The other side is told too**: our game sends `proto::Substitutes`
  (appended) to the owner of the records, through the host, once per new
  kind; their player reads "Judy sees 3 of your things as vanilla ones:
  mods they don't have". Nothing is blocked.
- A replaced record is what our game knows the thing as: what our scripts
  send back about it (a hand-over of a modded NPC to us) names the stand-in.

**Deliberately not covered** (and why):

- **Quest-content mods** (new quests, new scenes, new phases): the story is
  the host's (`story.md`); a guest without the quest's files follows the
  facts and journal it can show, and its own game cannot run scenes it
  does not have. Needs the same mods on both games.
- **Scripted behaviour mods** (new AI, new mechanics in redscript or CET):
  they run on the game that has them, on what that game simulates (its own
  V, the NPCs it owns). Nothing to substitute: a warning at the join says
  they differ.
- **Attack records** in hits (`NpcStrike`, `ShotHit`): a hit's damage and
  outcome travel with it; the record only names it, so it is not
  resolved, and a hit is never dropped for it.
- **Item stats and mods' stat changes**: a traded item carries its stats
  (`trade.md`); a gameplay mod's stat changes apply where that mod is.
- **Look pieces have no stand-in**: the game cannot say which slot an
  unknown resource is for; it is left off.

## 3. Living with other mods' scripts

- **tandem replaces nothing**: every one of its 189 hooks is a
  `@wrapMethod` that calls `wrappedMethod` (audit 2026-11-19, by
  `tools/mods-check.py`); there is no `@replaceMethod` or `@replaceGlobal`.
  A wrap chains with other mods' wraps of the same method; it breaks only
  under a mod that **replaces** a method tandem wraps (the replacement
  runs instead of the chain, depending on the load order).
- **`tools/mods-check.py DIR`** (`task mods:check DIR=...`) compares every
  method another mod's redscript wraps or replaces (and CET's `Override`,
  `Observe`), in a game folder or a copy, with tandem's: **conflict** (a
  replace on a method tandem wraps, tandem replacing anything, two mods
  replacing one method), **order-sensitive** (a replace under other wraps)
  or **chain** (harmless). Exit 1 when a conflict touches tandem.
- **Kinds of mods known to conflict**:
  - other multiplayer mods (CyberpunkMP, Night City Online and its forks):
    they hook the same systems (puppets, NPC sync, the menus) and cannot
    run beside tandem;
  - mods that **replace** a method tandem wraps; the ones that matter most,
    by function: the player state machine's `ShootEvents.OnEnter`,
    `ReloadEvents.OnEnter/OnExit`, `MeleeAttackGenericEvents.OnEnter`
    (`Weapons.reds`); `PauseMenuGameController` and
    `SingleplayerMenuGameController` (the co-op menu, `Coop.reds`);
    `SetTimeDilationEffector` (`Time.reds`); the hit and death reaction
    tasks, ragdoll and dismemberment (`HitReactions.reds`, `Outcomes.reds`);
    takedowns and finishers (`Takedowns.reds`); NPC status effects
    (`Combat.reds`, `Hacks.reds`). `tools/mods-check.py` lists every one;
  - mods that change the weather or the clock on a guest (WeatherSwitcher):
    the host's weather wins (below); a mod's time scale slows that guest's
    world, which tandem cannot undo: turn it off in co-op;
  - world-node removal mods (TheNullifier): a device or object one game
    removed and the other did not ignores the host's state for it.
- **Weather is the host's** (`World.reds` `Follow`): a guest whose weather
  leaves the host's with no word from the host (another mod set it) gets
  the host's back at once, at a higher priority (`SetWeather(weather, 2.0,
100u)`), and is told once a session ("Another mod changes your weather: in
  co-op the host's weather stays"). Redscript cannot block the mod's own
  call: `WeatherSystem.SetWeather` is native, and CET calls it directly.
- **A mod's slow motion is a time bubble** (`Time.reds` `Unexplained`, core
  `TimeBubbles::UnexplainedSlow`; `time.md`): any global slow motion no
  reason tandem follows explains, with no local one on, is shared as a
  bubble with its factor and said once ("A mod slowed time: shared as a time
  bubble around you"). The reason's name cannot be read (the time system's
  natives cannot be wrapped), so this needs no list of mods
  (OverclockOverheat's optional `OverclockDilation` was the case that
  asked for it).
- **A V never loses a limb** whatever other mods do (CazanuLimbSurvival,
  CrT ragdoll): `Outcomes.reds` `KeepsLimbs` and the wraps around
  dismemberment return early for a player's puppet, and `Combat.reds`
  blocks the Crippled and Dismembered effects on it. An NPC's survival or
  death is its owner's: proxies follow the host's health, death and limbs;
  a guest's lethal strike kills the host's NPC outright (`ApplyStrike`
  calls `Kill`), so a host's limb-survival mod never saves an NPC from a
  guest's kill.
- **Mod Settings: not used, by decision 2026-11-19.** tandem's settings
  live in its own Co-op page only (`coop-menu.md`), `tandem.ini` the one
  source of truth; no second settings menu.

**Reporting a conflict**: Co-op → Write a bug report (or
`tandem-report.exe`, release.md). Its `versions.txt` has the other mods'
count and fingerprint, the log has the categories (`other mods by what they
add`) and every replacement (`mods:` lines); add the output of
`python3 tools/mods-check.py <game folder>` and the mod's name.

## Tested against

A sample modded install, 2026-11-19 (12 RED4ext plugins, 8 CET mods, 15
script folders, 3 tweak folders, 88 archives and `.xl` files), by file
names alone (`tests/data/mods-sample.tsv`, the test "a real install, by its
file names alone"; `tandem-peer mods FILE` prints the table) and, from a
copy of its files, with the contents too (`tandem-peer mods GAME`, as the
plugin reads them at startup).

108 mods from 126 entries. By file names alone: 3 gameplay, 17 unknown,
88 local. With the contents (what the game computes): 1 with new items, 12
gameplay, 4 unknown, 91 local. So for this install a partner without these
mods would read "... 1 with new items, 12 changing gameplay, 4 tandem can't
sort, 91 only changing how the game looks or sounds". The item one is
Quickhack_Fixes (its tweaks add `Items.` records: quickhack programs); the
NPC appearance packs (`zz-NPCs-*`, INCF) are texture and mesh replacers, so
local: proxies keep the host's appearance names, which exist on both sides.

| Mod                        | By file names                  | With contents                  |
| -------------------------- | ------------------------------ | ------------------------------ |
| Quickhack_Fixes            | gameplay (also unknown, local) | items (also gameplay, local)   |
| REDGraph                   | unknown                        | unknown (also local)           |
| ##GoreRunners              | unknown                        | gameplay (also local)          |
| AccessPointSkip            | unknown                        | gameplay                       |
| LoadBegone                 | unknown (also local)           | unknown (also local)           |
| ImmersiveVDialogueExpanded | unknown                        | local                          |
| TheNullifier               | unknown                        | local                          |
| RadioExt                   | unknown                        | gameplay (also unknown, local) |
| CrT_True_Twist_Enabler     | unknown                        | unknown                        |
| AudioXL                    | unknown                        | gameplay (also unknown, local) |
| MusicRadio                 | unknown                        | gameplay (also local)          |
| WeatherSwitcher            | unknown                        | gameplay (also local)          |
| ForcekillDataminer         | gameplay (also unknown)        | gameplay (also local)          |
| CazanuLimbSurvival         | unknown                        | gameplay (also local)          |
| CazanuBloodPools           | unknown (also local)           | gameplay (also unknown, local) |
| OverclockOverheat          | gameplay (also unknown, local) | gameplay (also local)          |
| Torrent                    | unknown                        | unknown                        |
| CrT_Rig_Selector           | unknown                        | local                          |
| INCF-CORE                  | unknown (also local)           | gameplay (also local)          |
| CrT_Ragdoll_Enabler        | unknown                        | gameplay                       |

Local by both (88): #####-NovaCity2, ####GoreRunner, ###-PreemMap, ###-PreemSkin-PaletteFix-Textured, ###AAblud, ##-Preem Shores, ArchiveXL, audioware, Codeware, CrT_RagdollProfiles_Base, CrTFrictionTuning, ETO_2K, GoreRunnerSettings, HD Reworked Project, INCF, Map, Minor NPCS, mod_settings, nativeSettings, Preem Skin, Preem Water 2.0 - Pure, RelentlessLiveDismember, relux, ReLUX-AIO, relux_removals, Skins-Junk, Surfaces 2.0, TheNullifier-removals, TweakXL, VIDEO-Intro, ZZ-Clothing, ZZ-Containers, ZZ-CYBERWARE REDUX, ZZ-Eyes, ZZ-Food, ZZ-Graffiti2K, ZZ-Hair2K, zz-Johnny 2.0, ZZ-Makeup, zz-NPC-Panam, zz-NPC-Ripperdoc, zz-NPC-Wakako, ZZ-NPCFaces2K, zz-NPCs-8ug8ear, zz-NPCs-Alt, zz-NPCs-Clair, zz-NPCs-Dakota, zz-NPCs-Delamain, zz-NPCs-Denny, zz-NPCs-Dex, zz-NPCs-Dino, zz-NPCs-DumDum, zz-NPCs-ElCoyoteBarman, zz-NPCs-Evelyn, zz-NPCs-Fingers, zz-NPCs-Hanako, zz-NPCS-Jackie, zz-NPCs-Josh, zz-NPCs-Judy, zz-NPCs-Karina, zz-NPCs-Kerry, zz-NPCs-Mama, zz-NPCs-Misty, zz-NPCs-Nancy, zz-NPCs-Oda, zz-NPCs-Ozob, zz-NPCs-Placide, zz-NPCs-River, zz-NPCs-Rogue, zz-NPCs-Royce, zz-NPCs-Saburo, zz-NPCs-Saul, zz-NPCs-Sebastian, zz-NPCs-T-Bug, zz-NPCs-Takemura, zz-NPCs-Thiago, zz-NPCs-Thompson, zz-NPCs-Us Cracks, zz-NPCs-Weldon, zz-NPCs-Wilson, zz-NPCs-Yorinobu, ZZ-PlayerHeads2K, ZZ-Posters2K, ZZ-Skins2K, ZZ-Stickers, ZZ-Tattoos2K, ZZ-Vending Machines, ZZ-VsApartment.

`tools/mods-check.py` on the same copy (336 hooks in 16 mods): no conflict
with tandem; 20 overlaps with tandem's wraps, all chains:

- CrT_Ragdoll_Enabler: hit and death reaction tasks, ragdoll on and off,
  carry, takedown release, the blunt-hold finisher. Physics is each game's:
  corpses may fall differently on each screen.
- CazanuLimbSurvival: dismemberment and wounds, ragdoll. Host gameplay; a
  V never loses a limb (§3).
- Several on NPC status effects and the player's (`Combat.reds`,
  `Hacks.reds`, `Story.reds`).
- AccessPointSkip: `Device.OnDetach`. A breach succeeds at once for whoever
  breaches; daemons on host NPCs still go through tandem's hack path.
- GoreRunners (CET observers): finishers and takedowns, blood decals: local.
- LoadBegone: the main menu; its "load last save on launch" (off by
  default) skips past tandem's main-menu co-op page.
- nativeSettings: the pause menu.
- Quickhack_Fixes: 9 replaces, none on a method tandem wraps; its spread
  still keys on the caster, which fits "spread runs on the caster's game".
- ImmersiveVDialogueExpanded: CET overrides of the dialogue UI that call
  the original; tandem writes the host's choice hubs into that UI
  (`Dialogue.reds`): to check in game.

## Verify in game

1. The startup log's `other mods by what they add:` line on the sample
   install, against the table above.
2. A join between two games with different mods: the notice and the
   status line read well in both languages; the Mods popup lists the
   other's names after opening it twice.
3. Scenarios `mods-unknown-held`, `mods-unknown-vehicle`,
   `mods-unknown-look` (the mock player sends records no game has): the
   `mods: unknown` lines, the stand-in in the puppet's hand, the copy
   spawning as the stand-in car and the ride working, the look's plain
   trousers, the notices.
4. `TweakDB::GetRecord` answers null for a record that does not exist and
   is cheap enough every frame (it is memoized per record).
5. The depot check: the log's `mods: the resource depot has our own look`
   (else look pieces are never left off).
6. An NPC with an appearance name our game lacks: what the engine does
   with it (the template's default, or nothing shown), and whether
   `GetCurrentAppearanceName` then differs from the name asked.
7. The vanilla stand-ins named in §2 all exist (each is checked, but one
   missing falls through to the next).
8. WeatherSwitcher on a guest: the host's weather comes back, the notice;
   whether priority 100 wins over its lock mode.
9. A mod's slow motion (OverclockOverheat's `ocDilation` on): the bubble on
   the other game, the notice, and that our own bubble (a partner's
   Sandevistan slowing our V) is not taken for one.
10. radioExt: a partner riding with a custom station on hears the radio off,
    with the notice.
