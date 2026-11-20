# V's homes: apartment interactions

### 13.85 Apartment interactions: what runs them, what they change (2026-11-19, read offline, not run)

Sources: `final.reds` = `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`
(2.50, line numbers); `KnownHashes` = Codeware's resource path list
(`~/.cache/tandem/ref/codeware/data/KnownHashes.txt`, the game's resource
names; it names only what is known, so a missing path proves nothing);
`moddocs` = the redmodding wiki's guides (`~/.cache/tandem/ref/moddocs`).
Nothing here has been run in game: scenarios `apartment-shower`,
`apartment-bed`, `apartment-tour` check it (`tools/game.sh cmd apartment
report` logs what V's home shows of our V). Co-op rules built on it:
`docs/architecture/activities.md`, "V's homes".

- **One quest runs V's homes: `mq000`** [2.5-untested]. KnownHashes:
  `base\quest\minor_quests\mq000\phases\mq000_vs_apartment_interactions.questphase`
  and `mq000_vs_apartment_decorations.questphase`, one scene for the
  interactions, `base\quest\minor_quests\mq000\scenes\mq000_01_apartment.scene`
  (with `gold`, `patch0`, `patch1_2` versions), and its props under
  `mq000\entities`: `mq000_shower.ent`, `mq000_shower_black.ent`,
  `nomad_camp_shower.ent` (the Aldecaldo camp's), `mq000_coffee_machine.ent`,
  `mq000_smart_mirror.ent`, `mq000_clothes_container.ent` (the wardrobe),
  `mq000_toilet.ent`, `mq000_couch.ent`, `mq000_chair.ent`, `mq000_beanbag.ent`,
  `mq000_pouf.ent`, `mq000_table.ent`, `mq000_guitar.ent`, `mq000_speaker.ent`,
  `mq000_towels.ent`, `mq000_bedding.ent` with its appearances (`bedding_pink`,
  `bedding_rich_gold_black`, `bedding_rich_white_silk`), posters, statues,
  `mq000_gwent_plushie.ent`, the cat `mq000\characters\nibbles.ent`, and the
  gun wall: `mq000\entities\stash_weapons\mq000_fake_*.ent` (one display copy
  per iconic weapon: Malorian, Skippy, Overture of Kerry's, Jackie's Nue…).
  The moddocs guide "How to remove an animation" works on that scene: an
  interaction is a scene section whose `scnPlaySkAnimEvent` carries a
  `scnEventBlendWorkspotSetup` (a workspot the scene plays on V, e.g.
  `q001_01_wakeup__look_mirror.workspot`). Whether `WorkspotGameSystem
.IsActorInWorkspot` sees V in such a scene workspot is not known: the
  scenarios' `interaction begins (… in a workspot BOOL …)` line says.
- **The bought apartments** (the free "apartments" DLC, `dlc\dlc6_apart`)
  [2.5-untested]: KnownHashes names their props by location,
  `loc_dlc6_apart_cct_dtn` (Corpo Plaza: coffee machine and mug, teapot and
  teacup with their `dlc6_teacup_drink.effect`, record player, smart
  mirror, speaker), `loc_dlc6_apart_hey_gle` (the Glen: coffee machine,
  record player, smart mirror, speaker, the pool table's ball, cue ball and
  stick, a tablet), `loc_dlc6_apart_wbr_jpn` (Japantown: guitar, incense
  stick, record player, speakers); Northside's location and the four
  apartments' phases are not in the list (`tools/game.sh cmd quest trace on`
  logs every phase met). Judy's apartment after her romance:
  `dlc\dlc6_apart\phases\dlc6_apart_judy_apartment.questphase`,
  `dlc\dlc6_apart\scenes\dlc6_apart_judy_apartment_interactions.scene`;
  Kerry's mansion: `dlc\dlc6_apart\entities\shower_kerry.ent`. Plain
  workspots for homes: `base\workspots\apartment\generic__stand_sink_high__wash_hands__01.workspot`,
  `generic__stand_sink_low__wash_hands__01.workspot`,
  `generic__sit_low_table_sake__drink__01.workspot`; a bed's:
  `base\workspots\common\bed\generic__lie_bed_lean_right__lie_around__01.workspot`.
  Mirrors as scenes: `base\open_world\scenes\mirrors\mirror_scene_v_penthouse.scene`,
  `mirror_scene_judys_apartment.scene`, `mirror_scene_peralez_penthouse.scene`.
  Partners in V's apartments: `base\quest\minor_quests\mq055\phases\mq055_apartment.questphase`
  and one phase per home (`_megabuilding`, `_northside`, `_japantown`,
  `_heywood`, `_downtown`), with `_tymsg_judy`, `_kerry`, `_panam`,
  `_river`. Phantom Liberty adds no home of V's in KnownHashes: `mq305`'s
  apartment (`ep1\quest\minor_quests\mq305\phases\mq305_02_apartment.questphase`)
  is a gig's, and `q306_mementos_apartment.ent` a memento put in V's.
- **Apartment buffs are status effects of type `Housing`**
  [2.5-untested]. `gamedataStatusEffectType.Housing = 24`
  (`final.reds:4357`, enum at `:4332`); a stat lengthens them,
  `gamedataStatType.HousingBuffsDurationBonus = 834` (`:2206`). No script
  applies one: the scene does (a quest node). Their records are not in the
  scripts; community mods name them `HousingStatusEffect.Rested` (the bed),
  `HousingStatusEffect.Refreshed` (the shower), `HousingStatusEffect.Energized`
  (the coffee) [unverified]. tandem lists them by type
  (`TandemActivities.HousingBuffs`: `StatusEffectHelper.GetAppliedEffects`,
  `GetRecord().StatusEffectType().Type()`), so a renamed record still shows.
  Their values (what Rested gives, for how long) are TweakDB's: not read.
- **A time skip removes them** [2.5-untested]. `GameTimeUtils
.FastForwardPlayerState` (`:332610`), which the wheel's `Apply` calls,
  raises health to the out-of-combat regen threshold, sets stamina to 100
  and **removes every status effect with a remaining duration**, apartment
  buffs included. No script calls it for a bed (the bed's clock jump is the
  scene's quest node, `research/coop-time.md` §4.2), so vanilla's bed keeps
  the Rested it gives. tandem's own clock move for the host's bed must not
  call it (it did: `MoveClock(delta, true)` on the host's own yes).
- **What can block a skip**: `GameTimeUtils.CanPlayerTimeSkip`
  (`:332543`): combat, `NoTimeSkip` tag, pause, moving vertically, diving,
  a fast car, **scene tier 3 to 5**, a hostile security area, a call,
  carrying, a lore animation scene, a police chase, disabled hardware,
  autodrive. A voter lying in its own bed's scene (tier 3+) answers no by
  this rule; tandem's join path does not ask it.
- **How a scene undresses V** [2.5-untested; which one the shower uses is
  not known]. Two script paths a quest node reaches:
  - _Hide the visual slots_: the quest sends `HideVisualSlot { slot:
TransmogSlots }` (`:251079`) and later `RestoreVisualSlot` to the
    player; `PlayerPuppet.OnHideVisualSlot` (`:46417`) queues
    `QuestHideSlotRequest` (`:226028`) → `EquipmentSystemPlayerData
.QuestHideSlot` (`:58810`) → `ClearVisuals(area)` (`:58765`): the item
    stays equipped, its appearance goes (`empty_appearance_default`, or the
    transmog preview removed) and the slot's `isHidden` is set.
    `QuestRestoreSlot` (`:58814`) puts it back. A wardrobe set is put away
    and back the same way (`DisableVisualOverride`, `RestoreVisualOverride`:
    `OnQuestDisableWardrobeSetRequest` `:59369`, `OnQuestRestoreWardrobeSetRequest`
    `:59377`).
  - _Unequip by context_: `UnequipByContextRequest { itemUnequipContext:
gameItemUnequipContexts }` (native, `:236604`; contexts `:11162`,
    `AllClothing = 7`) → `EquipmentSystem.OnUnequipByContextRequest`
    (`:56330`): each clothing area unequipped, the wardrobe set off.
    Either way `TandemOutfit.Shown` (the paper doll's visual item unless the
    slot `IsSlotHidden`, `:59074`) drops what V no longer shows, so the look
    sent is what V's game renders.
- **Underwear, as vanilla shows it** [2.5-untested].
  `EquipmentSystemPlayerData.GetPaperDollSlots` (`:61440`) lists
  `UnderwearTop` when the build is censored and `UnderwearBottom` when it is
  censored or V has no genitals option (`ShouldShowGenitals`, `:61460`:
  `genitals_01..03` in the character's state); `IsBuildCensored` (`:61469`)
  is `!CharacterCustomizationSystem.IsNudityAllowed()` (`:56521`). Under
  clothes the underwear's slot is hidden (`ClearItemAppearanceEvent`,
  `:58899`), so it is shown, and sent, only when nothing covers it. The
  basic items are `Items.Underwear_Basic_01_Bottom` and `_Top`
  (`UnderwearEquipFailsafe`, `:59321`).
- **The mirror, the wardrobe, the stash are menus** [2.5-untested]. The
  mirror opens `MenuScenario_CharacterCustomizationMirror` (`:340882`), a
  `Wardrobe` device (`:511519`) `MenuScenario_Wardrobe` (`:335834`). The
  stash: `Stash.OnOpenStash` (`:132193`) writes `StorageBlackboard
.StorageData`; the hub's listener (`:347133`, `OnOpenStorage`) enters
  `MenuScenario_Storage` (`:377734`: `vendor_hub_menu`, then
  `fullscreen_vendor` with `ScreenDisplayContext.Storage`).
- **A device's screen up close** [2.5-untested]. `Device
.SetZoomBlackboardValues(newState, lockPlayerFor)` (`:99503`, class
  `Device` `:97969`) sets the player state machine's `IsInteractingWithDevice`,
  `IsUIZoomDevice` and `UIZoomDeviceID` (the device's entity id) and fires
  them: computers (`Computer` `:254633`), terminals, the smart window
  (`SmartWindow extends Computer`, `:477117`). The signal "V uses a
  computer" activities.md lacked.
- **The rest are world devices**, synced as devices.md says: `TV`
  (`:280265`), `Radio` (`:477681`), `Speaker` (`:508179`), `Jukebox`
  (`:475249`), their state `MediaDeviceControllerPS` (`:259142`); `Fridge`
  (`:494349`: a door animation and a fact, `m_factOnDoorOpened`); lights;
  the apartment's own screen, `ApartmentScreen` (`:468436`, an
  `LcdScreen` the quest sets with `SetApartmentScreenStatusEvent`). The
  record players and the `mq000`/`dlc6` speakers are quest props: their
  music is the scene's, in the game that plays it.
- **`Sleep`** is also a status effect type and tag NPCs sleep with
  (`:148802`, reactions `:148505`); nothing shows V's bed using it.
