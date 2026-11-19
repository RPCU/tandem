# Loot

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

### 13.19 Loot: corpses, dropped items, quest items (2026-11-18, 2.50 scripts read)

Read from the 2.50 scripts (`final.reds` line numbers), used by
`scripts/tandem/Loot.reds`; none of it has run in a session yet.

- **A corpse's loot is rolled by its own death.** `Kill()` on a puppet
  (`ScriptedPuppet.Kill` 21286, `NPCPuppet.Kill` 125843) takes its health
  to zero; `OnDeath` (21683) → `OnDied` → `NPCPuppet.OnIncapacitated`
  (128876), which calls the native `ProcessLoot()` (128931, `gamePuppet` 19444) after `ScriptedPuppet.OnIncapacitated` (21797) has queued the
  loot interaction for `items.dropSettings.delayBeforeEnablingLootPickUp`
  (default 2 s, 22251). So a guest's proxy killed by the host's word rolls
  its own loot, from its record (the real NPC's: the proxy is spawned from
  it), in the guest's game only. `WasLootGenerated()` (19436) says whether
  it did; `GenerateLoot()` (19406) is what a corpse loaded from a save
  runs (`NPCPuppet.OnGameAttached`, 125562). Quality and quest flags are
  worked out from the inventory afterwards (`EvaluateLootQuality` 23381:
  items tagged `Quest` set `m_hasQuestItems`).
  [2.5-untested]
- **Weapons**: `ScriptedPuppet.DropHeldItems` (22027, private, no script
  caller: presumably called by `ProcessLoot`) drops the items in
  `AttachmentSlots.WeaponRight` / `WeaponLeft` when the record's
  `DropsWeaponOnDeath()` and the item's `DropObject()` allow, via
  `DropWeapons()`. Only what is **in the hands** drops: a proxy whose brain
  went off before it drew its weapon may drop none. [2.5-untested]
- **A body that goes leaves a bag**: vanilla's disposal and trunk paths
  (210096-210100, 92707) call `ProcessLoot()` if `!WasLootGenerated()`,
  else `DropLootBag()` (19432) if `!RPGManager.IsInventoryEmpty(body)`
  (36468). Whether the bag survives deleting the body a second later is
  unknown. [2.5-untested]
- **The player's drops**: the backpack and inventory menus queue what the
  player drops and drop it when they close
  (`BackpackMainGameController.OnCloseMenu` 350981,
  `gameuiInventoryGameController.OnCloseMenu` 429123): one item of
  quantity 1 through `ItemActionsHelper.DropItem` (69371, an item action),
  more through `RPGManager.DropManyItems` (38020): retrievable weapon mods
  back to the player (`ReturnRetrievableWeaponMods` 36198), then
  `LootManager.SpawnItemDropOfManyItems(player, [DropInstruction], n"playerDropBag",
position)` (202210), which presumably moves the items out of `player`'s
  inventory into one bag. `GameInstance.GetLootManager` (14188). Which
  class the `playerDropBag` spawns (`gameLootBag` 298042 or
  `gameItemDropObject` 294978, a `gameLootObject`) is unknown: tandem
  watches both with Codeware's `Entity/Attach` callback. [2.5-untested]
- **Making the same item in another game**: `ItemID.GetRngSeed(id)` and
  `ItemID.CreateFromSeedWithOffset(record, seed, 0)` (53824-53828);
  `ItemID.GetStructure(id)` tells a unique item (`gamedataItemStructure.Unique` 2) from a stackable one (`ItemID.FromTDBID`). Whether offset 0 gives back
  the same stats is unknown; crafting upgrades (stat modifiers on the item
  data) are not in the seed. [2.5-untested]
- **Picking up** from a bag or drop is native (no script hook before the
  transfer); `gameItemDropObject` sees it after the fact
  (`OnItemRemoveddEvent` 295221, `OnInventoryEmptyEvent` 295198). "Take
  all" on a corpse is `TransactionSystem.TransferAllItems(body, player)`
  (22557, 52091). [2.5-untested]
- **Every item our V gets** passes through
  `PlayerPuppet.OnItemAddedToInventory(evt: ItemAddedEvent)` (41260:
  `itemID`, `itemData`, `currentQuantity`, `flaggedAsSilent`), which also
  writes the pickup log; tandem wraps it to share important items. What
  makes one important: `gameItemData.HasTag(n"Quest")`, item type
  `gamedataItemType.Gen_Keycard` (`RPGManager.GetItemType`, 36087), or
  `RPGManager.IsItemIconic` (the `IsItemIconic` stat, 35655); bought ones
  carry the stat `ItemPurchasedAtVendor` (41291). `TransactionSystem.GiveItem`
  (52046) adds an item the way loot does (the same event). [2.5-untested]
- **Rewards**: `RPGManager.GiveReward(gi, rewardID)` (37406) pays a reward
  record's items, XP (street cred included) and currencies, scaled by the
  receiving V. [2.5-untested]
