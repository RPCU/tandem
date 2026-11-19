# Appearance and V's entity

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 5. Appearance / character customization

**Pipeline used for remote players (works on 2.5)** (`App/World/AppearanceSystem.cpp:272-643`)

1. ccstate bytes from the server → `CreateHandle_CharacterCustomizationState` (**2710710832**) →
   `CharacterCustomizationState::Serialize(state, stream)` (**108403442**, a CMPStream reader).
   Reject a blob that doesn't consume exactly its size (short blobs used to deserialize "successfully"
   into garbage) (`:352-367`).
2. Set `PuppetPS.unk72[0] = 1` (checked during item add for &TPP) (`:373-380`). [2.31 field, still used]
3. Clothes: on 2.5 via script `TransactionSystem` (GiveItem + AddItemToSlot); the native path used
   `ItemID_Create` (**3020231866**, seed −1), `GetItemRecord` (**3236045910**), `GetPlacementSlot`
   (**1934369954**), `EvalCName` (**3648983380**), `GetItemAppearanceName` (**3029088864**) then SDK
   virtuals → crashed on 2.5 at `exe+0x887DC4` reading 0xFFFFFFFF right after
   "[Appearance] holstered_default" (`929d924f`). Remote outfits may render with the item's default
   variant (no garment appearance name in the script path). Give and equip must be independent steps
   (muppets arrive with Panam's loadout, so a `!HasItem` guard skipped the equip, `394f941f`).
4. Keys: `GetHeadCustomization("TPP"|"face"|"hairs"|"beards" if male)`, `GetBodyCustomization("TPP_Body",
"breast", "lifted_feet", "flat_feet")`, `GetArmsCustomization("holstered_default","nails")` — the set
   that dresses remotes correctly on 2.5 (`Game/CharacterCustomizationSystem.h:72-104`). "genitals"
   pokes through clothes; "character_customization" does all.
5. `RuntimeSystemEntityAppearanceChanger` + **386815609** (NCO name
   `ScheduleSynchronizedAppearanceChanges`; CW name `AppearanceChangeSystem_ChangeAppearance2`)
   `(changer, WeakHandle<Object>&, Span<Key>* old, Span<Key>* new, std::function<void()> const&, u32
priority)`. **Async: keep the key array alive in the completion callback** (a stack DynArray was a
   use-after-free, more on female states) (`:557-628`). [2.5-verified, CW confirms hash]

- Head/body/arm group names seen: head `TPP, FPP, hairs, character_customization, TPP_proxy, FPP_proxy,
FPP_hairs, TPP_photomode, beards(ma), face, finalSceneBruises`; body `FPP_Body, TPP_Body,
character_creation, genitals, breast, lifted_feet, flat_feet`; arms `holstered_default(_tpp/_fpp),
holstered_strong…, character_customization, personal_link_*, *_launcher, *_mantis, nails`
  (`AppearanceSystem.cpp:414-498`).
- **Don't re-apply an identical ccstate**: re-scheduling an appearance the puppet already wears 15× in
  16 min crashed the observer (exit 0x80000003). Hash the ccstate per entity; same hash → clothes only
  (`MAP:1248-1264`, `AppearanceSystem.cpp:500-527`). Clothing and customization in one message plus a
  holstered weapon toggling the visual item count (4→5→4) caused the re-sends.
- **tandem (2026-11-18, [2.5-verified])**: no ccstate serialization is
  needed: the 15 keys collected from a state (resource path hash + CName
  hash, 16 bytes each) are portable; sent over the network and scheduled on
  another game's puppet they dress it with the sender's face, body and hair.
  Clothes go as the wardrobe's visual item ids, worn as preview items.
- Remote equipment must travel as **numeric TweakDBIDs**; `TDBID.ToStringDEBUG` returns empty strings
  in release builds (no debug name table) (`AppearanceSystem.cpp:81-87`).
- `AddItemToSlot failed` on remotes (~106/evening) — cosmetic, uninvestigated (`MAP:1517-1518`).

**The +0x78 problem / IsGame231Layout** (`Game/CharacterCustomizationSystem.h:111-160`)

- 2.31: `gameuiCharacterCustomizationSystem` (RTTI size **0x388**) holds the _creator's_ state handle
  at **+0x78**, null outside the creator — so "is it null?" told you "am I in the creator".
- 2.5: class is **0x3D8**; +0x78 is **never empty** in play and holds the template/default body. The
  90-second autosave serialized it and overwrote the player's stored face every 90 s (2026-11-01;
  6283-byte male → 6571-byte default woman, test.70, `463bc4d1`).
- CAMP's guest character restore also writes through `ccSystem+0x78`: `InitializeState()` → native
  `ImportCustomizationState(base64)` (Serialize **108403442** into the +0x78 state) → `SetLifePath` →
  `SetIsExpansionStandalone(true)` → `FinalizeState()` → `StartNewGame(state)`
  (`campaign/code/assets/redscript/Plugins/CampaignLobby.reds:995-1027`) — inside the creator flow, where
  +0x78 was valid on 2.31. [2.31-only; re-check on 2.5]
- `IsGame231Layout()` = `GetClass("gameuiCharacterCustomizationSystem")->size == 0x388`; used as the
  only available build fingerprint to gate every 2.31 decode. On 2.5 the +0x78 read is disabled
  (creator/ripperdoc appearance saves OFF). [2.31-only decode; fingerprint itself works on 2.5]
- Safe on 2.5: dispatch customization methods against the **interface**
  `gameuiICharacterCustomizationSystem` (24 functions: InitializeState, InitializeOptionsFromFinalizedState,
  FinalizeState, ReFinalizeState, CancelFinalizedStateUpdate, ClearState, GetState, Get{Head,Body,Arms,
  United}Options, RandomizeOptions, ApplyEditTag, ApplyUIPreset, ApplyChangeToOption, … ; redscript sees
  only `HasCharacterCustomizationComponent` + `GetState`; the concrete class has 0 functions)
  (`MAP:1391-1394`, `8558b7b1`, `78713d7a`); `GetState()` works in ordinary gameplay
  (equipmentSystem.script:4992) (`MAP:1395-1397`). **On 2.5 read the live state through `GetState`**
  (`17599f98`). [2.5-verified]
- **Capture timing on 2.5** (`ce05d986`): mid-creator autosave = 23-byte unpopulated state; the save at
  `OnBeforeWorldDetach` as the New Game world tears down = the real creator look (10263 bytes, female) —
  **the only moment that works**; after the MP world loads = template body. [2.5-verified]
- **Re-saving a restored look corrupts it** (`286c0312`): the restore writes the stored look _into_ a
  state still holding the template body; whatever the look doesn't mention survives: 6283 → 6571 bytes
  with `female_base`, `full_breast_big`, female genital options added while the body flag stays male.
  Restored characters must save possessions only; a clean write needs an emptied/fresh state.
  [2.5-verified]
- Body type must be chosen before world load: two template saves `MultiplayerStart` (female) and
  `MultiplayerStartMale`; a male look on the female template changes nothing on screen (`c05b8ba6`).
  Server refuses a save that flips `is_male` on an established character (`3903a8a3`).
- Blob sizes seen: 5435, 6283, 6518, 6586, 7138, 8588, 9141, 9206, 10263 bytes; a 23-byte half-built
  blob exists — require ≥1 KB on both ends (`8558b7b1`).
- Since 2.0 wardrobe outfits override slots and `GetPaperDollEquipAreas` doesn't report the outfit —
  read the Outfit slot explicitly (`32e6a486`). `GetItemList` = carried items only; equipped weapons
  and cyberware via `GetPaperDollEquipAreas` + `GetVisualItemInSlot`; unequip before RemoveItem
  (`642663dd`).
- **RED4ext SDK trap**: `ExecuteFunction` with a null out-slot returns false **without calling** a
  function that has a return type; `InitializeState`, `ReFinalizeState`, `FinalizeState` return Bool so
  never ran; void `InitializeOptionsFromFinalizedState` "worked" but does not rebuild the body
  (`MAP:1590-1599`, `Scripting/Utils-inl.hpp`).
- Own body: still open on 2.5 — "a player sees themself as the template body while everyone else sees
  them right". Candidate ("Route A"): stored look into a FRESH state, same key collection, scheduled on
  the local player (`MAP:1590-1599`). [unverified]
- SDK carries `CharacterCustomizationState` 0xF0→**0x110** and `Item_Record` 0x478→**0x488** on 2.5 as
  opaque growth, insertion point unknown (`NCO-PROVENANCE.md:73-75`). [2.31-only internals]
- Never hand an appearance blob to the engine while on the main menu: `GetState` returns nothing; keep
  the bytes until a live state exists (`MAP:1114-1126`).
- Morph targets: `SetTargetWeightWithNormalize` **633871992** (CW: `MorphTargetManager_ApplyMorphTarget`),
  `ResetWeights` **1054086242**, weight array at component+0x98 — declared, unused. [unverified]

**Session/character flow lessons**

- Load one fixed template save always and let the server own identity; picking "newest save" chose a
  random character by file timestamp (`MAP:1547-1576`). Template inventory/balance arrives ~88 s after
  load (save data arriving late), strip it once by detection, not timer (`MAP:1414-1429`).
- Lock saving with the engine's `SaveLocksManager` (`AddSaveLock(reason)`; covers F5/menu/autosave;
  saveLocksManager.script:29) — not by hiding menu entries (`code/assets/redscript/SaveLock.reds`).
- Save the character at the top of `OnBeforeWorldDetach` (last moment the body is the server's); a
  `Live` lifecycle state gates all saves (`MAP:1482-1493`, `MAP:1087-1096`).

### 13.4 What V's entity is made of

`tools/re/live.py player` lists it: about 150 components on the local V
(`PlayerPuppet`, native vtable `gamePuppet`). Worn clothes are
**components of V's own entity**, not item entities:
`entGarmentSkinnedMeshComponent` named after the mesh with a 4-digit suffix
(`t2_206_pma_jacket__tw3_coat1761`, `t0_000_pma_base__full`, …), next to
`entSkinnedMeshComponent`s `legs`, `shoes`, `torso` and
`entMorphTargetSkinnedMeshComponent`s for head and nails. The entity also
carries `gameAttachmentSlots` "AttachmentSlots", `gameInventory`
"Inventory", `entVisualControllerComponent`, `gameTPPRepresentationComponent`
and the customization controllers. For the open clothes problem (12.1): a
puppet that shows clothes needs these garment components on its entity,
whatever its slots answer. [2.5-verified]

**Clothes on tandem's puppet, measured 2026-11-18** (live slot data read with
`tools/re`, slot events from a script `AttachmentSlotsScriptCallback` kept
alive on the puppet — a listener nobody holds is dropped):

- `gameAttachmentSlots` (RTTI 0x118): slot table at **+0xB0** (count +0xB8),
  entries `gameAttachmentSlotData` of 0x90 bytes: +0x00 slot TweakDBID,
  **+0x08 the item object handle** (a `gameGarmentItemObject` for V's
  clothes), +0x18 the item id being placed, +0x58 appearance item id, +0x68
  the slot record. V has 23 slots; the puppet (record based on
  `Character.Panam`) **33 of its own, Head, Chest, Torso, Legs, Feet and
  Outfit included**: `puppet.tweak` used to add them again, making
  duplicates (removed). [2.5-verified]
- On the puppet, clothing placed either way — the paper doll's
  `GivePreviewItemByItemID` + `AddItemToSlot(CreatePreviewItemID(item))`, or
  `GiveItem` + `AddItemToSlot(item)` — is accepted (both calls true, the id
  written at +0x18/+0x58) but **no item object ever spawns and no slot event
  fires**. A pistol (`GiveItem` + `AddItemToSlot(WeaponRight)`) on the same
  puppet spawns at once (`OnItemEquipped`, `OnItemEquippedVisual`, object
  at +0x08). The refusal is specific to garments on this body. [2.5-verified]
- **The cause: the record's `genders`.** `Character.Panam`'s single entry
  is female with `panam.ent`; the puppet records inherited it. Giving each
  puppet record its own entry (`genders = [{ initial = true; gender =
"Gender.Male"; entity = <its template>; }]`, as NCO's records did) makes
  the same preview items equip at once: `OnItemEquipped` and
  `OnItemEquippedVisual` for coat, shirt, pants and boots, and the puppet
  is drawn dressed in V's clothes, face and hair from the look, pose from
  the stream. (The items are placed twice in the first 2 s: once by our
  call, once again by the engine's refresh; harmless.) [2.5-verified]

**How the game itself dresses a puppet of V** (vanilla scripts, decompiled
reference `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`; natives and
their handlers in `data/rtti-natives-2.50.tsv`) — not what 12.1 tried
(`GiveItem` + `AddItemToSlot` with the real item id): [unverified on a
tandem puppet]

- The inventory's paper doll (`OnPuppetReady(sceneName, puppet)`, line
  347417): for each area of `EquipmentSystem.GetData(player).GetPaperDollEquipAreas()`,
  the item is `GetVisualItemInSlot(area)` (the wardrobe's choice, not the
  stat item; skipped when `IsSlotHidden(area)`); clothing gets
  `GivePreviewItemByItemID(puppet, item)`, other items
  `GivePreviewItemByItemData(puppet, data)` then
  `AddItemToSlot(puppet, EquipmentSystem.GetPlacementSlot(item), CreatePreviewItemID(item))`.
  Then the head: `GiveItem` + `AddItemToSlot` of `Items.PlayerMaTppHead` /
  `Items.PlayerWaTppHead` by `puppet.GetResolvedGenderName()`.
- On the player (`EquipmentSystemPlayerData`, lines 58600–58836): a visual
  override is `GivePreviewItemByItemID(owner, item)` +
  `AddItemToSlot(owner, slot, CreatePreviewItemID(item), true)`; an equipped
  item with another look is `AddItemToSlot(owner, slot, item, visualItem)`;
  a hidden slot `AddItemToSlot(owner, slot, item, n"empty_appearance_default")`;
  and `OnItemAddedToEquipmentSlot(owner, item, opt garmentAppearance, opt appearanceItem)` /
  `OnItemRemovedFromEquipmentSlot` tell the engine about equipment changes.
