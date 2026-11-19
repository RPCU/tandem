# Progression, economy, saves, items

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

### 13.30 Progression, economy, saves, items (2026-11-19, 2.50 scripts read, not run)

What the game's own scripts say about levels, XP, vendors, saves and item
data, gathered for [`docs/architecture/progression.md`](../../architecture/progression.md)
and [`docs/architecture/trade.md`](../../architecture/trade.md).

Source: the decompiled vanilla scripts,
`~/.cache/tandem/ref/gamescripts/vanilla/final.reds`, cited as `F:LINE`.
The decompiler mangles some loops and `else if` chains: calls and their
arguments are reliable, exact branches less so.

Tags: **[R]** read at the cited line; **[I]** inferred from what was read;
**[G]** verified in game (none yet: everything here was read offline on
2026-11-19, with the game machine out of reach). When one is verified,
change its tag and say how.

#### 13.30.1 Levels and scaling

- An NPC's level is its `gamedataStatType.PowerLevel` stat. Health, armour
  and damage are derived from it by TweakDB curves
  (`puppet_powerLevelToHealth`, the rarity presets read with the NPC's
  PowerLevel: `RPGManager.GetRarityMultiplier` F:35335-35351) [R]. That
  the derived stats follow a PowerLevel modifier added after the spawn is
  [I]: to confirm in game (scenario `levels-host`, then look at a scaled
  NPC's health bar and damage).
- The level a piece of content gives a player is its **content
  assignment**: `Character_Record.ContentAssignment()` (F:19086) →
  `ContentAssignment_Record.PowerLevelMod()` (F:18357), which is either a
  `ConstantStatModifier_Record` (a fixed level) or a
  `CurveStatModifier_Record` (`Id()`, `Column()`, F:215130) [R]. The journal
  reads a quest's recommended level with
  `StatsDataSystem.GetValueFromCurve(curve.Id, playerLevel, curve.Column)`
  (F:357928-357941): the curve maps the **player's level** to the content's
  [R]. `RPGManager.GetPowerLevelFromContentAssignment` (F:36594) reads the
  same three cases [R].
- The native `LevelAssignmentSystem` (F:34939: `GetLevelAssignment(id)`,
  `LockLevelAssignment`, `IsLocked`, `MarkPlayerLevelRestored`, called when
  the player's development data is restored, F:80130) keeps each
  assignment's level [R]. Which player level it read and when it locks is
  native [I].
- The game's own scripts scale NPCs by one additive PowerLevel modifier:
  `PlayerDevelopmentData.ScaleNPCsToPlayerLevel` (F:82742-82775, a debug
  build setting) removes an NPC's PowerLevel modifiers and adds one for the
  player's level, **only for NPCs whose record has a content assignment**
  (and devices) [R]. `NPCManager.ScaleToPlayer` (F:125162) does the same for
  `PowerLevel` and `Level` and has no caller [R].
- **`StatsSystem.AddModifier` is not saved; `AddSavedModifier` is**
  (F:34970-34982; items use the saved kind, e.g. F:41636-41648) [R]. That a
  plain modifier on a streamed NPC is gone after a load is [I].
- `DamageSystem.ProcessLevelDifference` (F:87465, the
  `pl_diff_to_damage_multiplier` curves) **has no caller** in 2.50 [R]: a
  level difference no longer scales a hit by itself; the levels act through
  the stats. `ScalePlayerDamage` (F:87494) runs only with the fact
  `story_mode` (F:86394) [R].
- Scripts that read a level difference: quickhack cost
  (`GetPowerLevelDiff`, F:27447), the threat skull
  (`CalculatePowerDifferential`, F:36825; `CalculateThreatValue`, F:36841),
  the scanner's hacking resistance (F:36857) [R]. All read the two
  PowerLevel stats of the game they run in.
- A V's level: `PowerLevel` and `Level` stats of the player entity; the
  cap is `BaseStats.PowerLevel`'s `Max()` (F:20082, F:416337) [R].

#### 13.30.2 Experience

- Skill XP from damage: `RPGManager.AwardExperienceFromDamage` (F:36969),
  called from `StatPoolsManager.DrainStatPool` (F:219326) for real damage
  only. Gate (F:36979-36983): the target is active and
  `AwardsExperience()` (not crowd or police, F:20975), the instigator
  `IsPlayer()`. Amount: curve `activity_to_proficiency_xp` /
  `damage_to_skill_xp` at the **target's PowerLevel** × the rarity
  multiplier × the share of health drained × the player's XP multiplier
  (F:36986-37117) [R]. Queued, not paid.
- The queue is `PlayerDevelopmentData.m_queuedCombatExp`, **persistent**
  (F:80043), entries `{amount, type, entity}` (F:80718), paid and erased by
  `ProcessQueuedCombatExperience(entity)` (F:80726-80752), which
  `PlayerPuppet.OnTargetNeutraliziedEvent` requests (F:45336) [R].
- The kill's own reward: `ScriptedPuppet.RewardKiller` (F:21851), **only if
  the killer is a `PlayerPuppet`** (F:21858): the neutralize event, then
  `BountyManager.CompleteBounty` (F:203524): bounty street cred and money
  to `GetPlayer` (F:203537), then
  `RPGManager.GiveReward(RPGActionRewards.Neutralize<Rarity>Enemy, target)`
  (F:203552-203576), the Level XP and street cred of the kill, once per
  body (`m_awarded`) [R]. The reward records are TweakDB [I].
- Quickhack upload XP: `AwardExperienceFromQuickhack` (F:37282), to the
  executor, by the target's PowerLevel, once per target: it leaves the
  status effect `BaseStatusEffect.QuickhackExpHarvested` on it (F:37296,
  F:37326) [R]. Utility grenades leave
  `BaseStatusEffect.UtilityGrenadeRewardHarvested` (F:523580-523599) [R].
  The parry and locomotion markers are on the player (F:37180, F:37219) [R].
- Local by nature (the acting player's game, `GetPlayer` or the executor):
  finishers (F:250120), action rewards (F:26649), deflect and overshield
  (F:37157, F:37250), movement (F:37214), crafting (F:71520), shooting a
  grenade (F:175918), breaching and scavenging (F:19948, F:119201) [R].
- Quest, gig and NCPD rewards: `PlayerPuppet.OnRewardEvent` (F:43468) →
  `RPGManager.GiveReward` to the local player (F:37415-37530): XP scaled by
  that player's level and, with a `contentAssignment` on the reward, by the
  level difference (F:37510-37530) [R].

#### 13.30.3 Perks, attributes, respec

- `PlayerDevelopmentSystem.m_playerData` is persistent, one entry per
  attached player, created in `OnPlayerAttach` (F:79163-79199), found by
  owner id (F:79225) [R]. Only `PlayerPuppet.OnGameAttached` sends the
  attach request (F:40102) [R]: an NPC entity (a tandem puppet) gets no
  entry, earns nothing and answers no perk test [I].

#### 13.30.4 Loot level

- The roll is native (`ProcessLoot` F:19444, `GenerateLoot` F:19406):
  which level it reads is not in the scripts [I].
- On pickup, `PlayerPuppet.TryScaleItemToPlayer` (F:41538, from
  `OnItemAddedToInventory` F:41283) gives an item flagged `scaleToPlayer`
  with PowerLevel ≤ 1 the **picker's** PowerLevel and locks it
  (`ScalingBlocked`) [R]. `RetroRescaleNonIconicWeapons` (F:41672-41771)
  sets `LootLevel` from the player's `Level` [R].
- `Item_Record.PowerLevelDeterminedByParent()` (F:18157) and the stat
  `NPCLootLevelHelper` (F:2409) exist and are not used in scripts [R]:
  whether an NPC's level reaches its loot is to measure in game.

#### 13.30.5 Economy

- **Vendors.** `MarketSystem.m_vendors: [Vendor]` is persistent (F:130219);
  a `Vendor` keeps its stock, sold items, restock time and price multiplier
  (F:130593-130599), found by entity id and record (F:130288-130322) [R].
  The vendor id is the character record's `VendorID()` (F:130453) [R]. The
  buyer and seller is always `GetPlayer` (F:131475, F:131523) [R]. Any
  `MarketSystem` call on an entity whose record has a vendor id creates a
  vendor for it (`GetOrAddVendor`, F:130293) [R].
- No script opens an NPC's shop: the only script caller of
  `UISystem.RequestVendorMenu` (F:50115) is the drop point (F:202567) [R].
  NPC shops open from a quest or scene node [I].
- `Vendor.FillVendorInventory` (F:130998-131024) removes every non-`Prop`
  item from the vendor object and gives it the record's stock [R].
- Vending machines: a non-persistent vendor list (F:130220), the game's own
  device, `GetPlayer` pays (F:201577) [R].
- **Crafting**: `CraftingSystem.m_playerCraftBook`, persistent, the local
  player only (F:70191, F:70203) [R].
- **Stash**: `Stash` is a device with an inventory (F:132145); opening it
  puts `StorageUserData{storageObject}` on `StorageBlackboard.StorageData`
  (F:132193-132211); the car trunk does the same with the vehicle
  (F:92826) [R].
- **Vehicles**: the native `VehicleSystem` (`GetPlayerUnlockedVehicles`,
  `EnablePlayerVehicle`, F:189897-189949), per game and save [I].
- **Apartments, doors**: `DoorControllerPS` persists `m_isOpened`,
  `m_isLocked`, `m_isSealed`, `m_isPlayerAuthorised` (F:134529-134539);
  quests unlock with `OnQuestForceUnlock` (F:135662) [R]. Buying an
  apartment is quest graphs and facts [I].
- **Fast travel**: `FastTravelSystem.m_fastTravelNodes`, persistent
  (F:50236), registered by the game's own devices for the local player
  (F:45520, F:50505) [R].
- **Money** is the item `MarketSystem.Money()` in V's inventory (F:130351)
  [R].

#### 13.30.6 Saves

- `ScriptableSystem.IsSavingLocked()` (F:48411) is asked of every scriptable
  system; `SaveLocksManager` (F:195154) keeps a list of reasons that **is
  not persistent** (`RequestSaveLockAdd`/`Remove`, F:195179) [R].
  `GameInstance.IsSavingLocked(game, out locks)` (F:14340) is what the
  menus ask (F:340425) [R].
- Saving and loading from script, on `inkISystemRequestsHandler`
  (F:272564): `QuickSave()` (F:272625), `ManualSave(name)` (F:272627; the
  menu passes `"ManualSave-"`, F:371156), `LoadLastCheckpoint(onlySame
Playthrough)` (F:272617), `LoadSavedGame(id)` (F:272615),
  `HasLastCheckpoint()` [R]. `AutoSaveSystem.RequestCheckpoint()` and
  `InvalidateAutoSaveRequests()` (F:203130-203134) [R].
- The outcome of a save comes to listeners of the handler's
  `OnSavingComplete(success: Bool, locks: [gameSaveLock])`, registered with
  `RegisterToCallback(n"OnSavingComplete", object, function)`; the pause
  menu (F:340339, F:340477) and the HUD's quick save (F:347083, F:347602)
  do [R]. That a plain `IScriptable` (not an ink controller) may register
  is [I]: `Saves.reds` treats no word as a failed save and loads nothing.
- Closing a vendor or stash screen asks for an autosave
  (`OnBeforeLeaveScenario`, F:404050) [R].
- Weather: Codeware's `WeatherSystem.SetWeather(name, blend, priority)`
  turns the game's weather cycle off and `ResetWeather(forceRestore,
blend)` turns it on again (`coop-quests-world.md` §8.2) [R]. A guest
  that leaves a session without the reset keeps the cycle off: fixed in
  `World.reds` (2026-11-19).

#### 13.30.7 Items: what a trade must carry

- **ItemID** (F:53803-53827): `FromTDBID`, `GetTDBID`, `GetRngSeed`,
  `GetStructure`, `HasFlag(Preview)`, `CreateFromSeedWithOffset(record,
seed, offset)`; Codeware exposes `uniqueCounter` and `flags`. Equal
  record and seed giving equal random rolls in two games is [I] (the loot
  code relies on it too).
- **Stats** are saved modifiers on `itemData.GetStatsObjectID()`, read with
  `GetStatValueByType` (F:39032) and `HasStatData` (F:39036), written by
  vanilla as `RemoveAllModifiers(id, stat, true)` then `AddSavedModifier`
  [R]: `Quality` (F:35861, F:41636), `IsItemPlus` (F:41639),
  `WasItemUpgraded` (`CraftingSystem.UpgradeItem`, F:71395), `PowerLevel`
  and `ItemLevel` (`CraftingSystem.SetItemLevel`, F:71000), `LootLevel` and
  `ScalingBlocked` (F:41538-41570), `IsItemCrafted` (F:70889),
  `ItemPurchasedAtVendor` (read at F:41694).
- A copy flattens modifiers to constants: an iconic's tier curves over
  `WasItemUpgraded` (F:41965, F:42129) and a crafted item's `ItemLevel +=
WasItemUpgraded * 10` (F:71024) are not kept live [R]. Iconics are not
  traded, so only crafted items lose that relation.
- **Parts**: `gameItemData.GetItemParts(out [InnerItemData])` (F:39046),
  `InnerItemData.GetItemID/GetSlotID/GetStaticData` (F:62271); parts made
  with the item carry the record tag `parentPart` (F:67327) or `DummyPart`
  (F:70909) [R]. Installing: the part is an inventory item first, then
  `TransactionSystem.AddPart` / `ForcePartInSlot` (F:52237, F:67214) [R].
- **A detached item**: `Inventory.CreateItemData(ItemModParams{itemID,
quantity, customPartsToInstall}, owner)` (F:69244-69255); vanilla sets
  modifiers on one for crafting previews (F:63700-63716) [R].
  `TransactionSystem.GiveItemByItemData` (F:52052) exists; vanilla only
  uses it for the photo-mode puppet (F:516006) [R].
- **What the game does to a new item** (`PlayerPuppet.OnItemAddedTo
Inventory`, F:41260-41519): skill books are learned (F:41342), readables
  become journal entries (F:41431), money shards become money (F:41475),
  junk may be auto-disassembled (F:41442), grenades and inhalers go through
  `LeaveTheBestQualityConsumable` (F:41452), then `TryScaleItemToPlayer`,
  `SetIconicWeaponsTier`, `LockItemPlusOnNonIconicWeapons` (F:41281-41284)
  [R]. Hence the types a trade refuses, and the second stamp of a traded
  item's stats half a second after it arrives.
- **Not on the item**: "new" and "favourite" are lists in
  `UIScriptableSystem` (F:76257); magazine ammo is the weapon object's
  (F:54328) [R].
- **The game's own rules**: drop (`RPGManager.CanItemBeDropped`,
  F:37853: no `IconicWeapon`, `Quest`, `UnequipBlocked`), sell
  (`UIInventoryItemsManager.IsSellableStatic`, F:77114: the tags of
  `RTDB.Vendor.customerFilterTags`), stash blacklist (F:77435:
  `HideInBackpackUI`, `Grenade`, `Inhaler`, `Injector`) [R].

#### 13.30.8 The storage screen (the trade table)

- `FullscreenVendorGameController.OnSetUserData` (F:403966) reads
  `StorageBlackboard.StorageData`; with a `storageObject` it initialises
  `VendorDataManager` on that object's entity id, hides both balances and
  names the vendor side "Storage" (F:403991-404001) [R].
- Listing: `PopulateVendorInventory` (storage branch F:405152) from
  `VendorDataManager.GetStorageItems()` (F:230174: the object's item list);
  the player's side from `GetPlayerItemsMap()` minus the stash blacklist
  (F:405337): no quest or equipped filter [R].
- Every move ends in `VendorDataManager.TransferItem(source, target,
itemData, amount)` (F:230246): a click (F:404634), the quantity picker
  (F:404852), the equipped-item confirmation (F:404960) [R].
- `OnInitialize` stops time (`SetTimeDilatation(true)`, reason
  `VendorStash`, F:404099) [R]. `OnHandleGlobalRelease` (F:404539) handles
  `sell_junk` only in vendor mode [R]: that the action reaches it in
  storage mode, and `drop_item` / `disassemble_item` too, is [I].
- Closing: `OnBack` spawns `OnVendorClose` on the menu event dispatcher
  (F:404126) [R].
