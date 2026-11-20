// What a V shows, as item records, and putting it on a puppet. Items go
// through the TransactionSystem from script: NCO's native path (hand-numbered
// vtable slots) crashed on 2.5 (docs/research/engine-knowledge.md §5).

public abstract class TandemOutfit {
  // The slots a V's visible clothes sit in. Since 2.0 a wardrobe outfit
  // overrides the others and is only seen in its own slot.
  public static func Slots() -> array<TweakDBID> {
    return [
      t"AttachmentSlots.Head", t"AttachmentSlots.Face", t"AttachmentSlots.Chest",
      t"AttachmentSlots.Torso", t"AttachmentSlots.Legs", t"AttachmentSlots.Feet",
      t"AttachmentSlots.Outfit"
    ];
  }

  // The clothes `owner` shows, as the inventory paper doll finds them: for
  // every paper-doll area the wardrobe's item (not the stat item), unless the
  // area is hidden. What is sent to the other players.
  public static func Shown(owner: ref<GameObject>) -> array<TweakDBID> {
    let shown: array<TweakDBID>;
    let player = owner as PlayerPuppet;
    let equipment = IsDefined(player) ? EquipmentSystem.GetData(player) : null;
    if !IsDefined(equipment) {
      return shown;
    }
    for area in equipment.GetPaperDollEquipAreas() {
      let item = equipment.GetVisualItemInSlot(area.areaType);
      if ItemID.IsValid(item) && EquipmentSystem.IsClothing(item) && !equipment.IsSlotHidden(area.areaType) {
        ArrayPush(shown, ItemID.GetTDBID(item));
      }
    }
    return shown;
  }

  // What a puppet wears in our game: the clothes its player's game shows,
  // and, when our own game is censored (no nudity), the basic underwear
  // wherever they leave the body bare, as our game does for our own V
  // (EquipmentSystemPlayerData.GetPaperDollSlots, IsBuildCensored). Never
  // more than theirs shows; less only where ours would never show it (a
  // shower in theirs, uncensored, without underwear).
  public static func Modest(game: GameInstance, records: array<TweakDBID>) -> array<TweakDBID> {
    if GameInstance.GetCharacterCustomizationSystem(game).IsNudityAllowed() {
      return records;
    }
    let top = false;
    let bottom = false;
    for record in records {
      let area = EquipmentSystem.GetEquipAreaType(ItemID.FromTDBID(record));
      if Equals(area, gamedataEquipmentArea.Outfit) {
        top = true;
        bottom = true;
      } else if Equals(area, gamedataEquipmentArea.InnerChest) || Equals(area, gamedataEquipmentArea.OuterChest) || Equals(area, gamedataEquipmentArea.UnderwearTop) {
        top = true;
      } else if Equals(area, gamedataEquipmentArea.Legs) || Equals(area, gamedataEquipmentArea.UnderwearBottom) {
        bottom = true;
      }
    }
    let out = records;
    if !top {
      ArrayPush(out, t"Items.Underwear_Basic_01_Top");
    }
    if !bottom {
      ArrayPush(out, t"Items.Underwear_Basic_01_Bottom");
    }
    return out;
  }

  // Puts the records on `puppet` the way the game dresses a puppet of V
  // (EquipmentSystemPlayerData.ChangeAppearanceToItem): a preview copy, then
  // that copy in the item's placement slot. Answers how many were placed.
  // Garments only spawn when the puppet's record names its own gender
  // (puppet.tweak, engine-knowledge.md §13.4).
  public static func WearPreview(puppet: ref<GameObject>, records: array<TweakDBID>) -> Int32 {
    let transactions = GameInstance.GetTransactionSystem(puppet.GetGame());
    let placed = 0;
    for record in records {
      let item = ItemID.FromTDBID(record);
      transactions.GivePreviewItemByItemID(puppet, item);
      if transactions.AddItemToSlot(puppet, EquipmentSystem.GetPlacementSlot(item), transactions.CreatePreviewItemID(item), true) {
        placed += 1;
      }
    }
    return placed;
  }

  // Empties the slots these records went in.
  public static func TakeOff(puppet: ref<GameObject>, records: array<TweakDBID>) -> Void {
    let transactions = GameInstance.GetTransactionSystem(puppet.GetGame());
    for record in records {
      transactions.RemoveItemFromSlot(puppet, EquipmentSystem.GetPlacementSlot(ItemID.FromTDBID(record)));
    }
  }

  public static func Same(a: array<TweakDBID>, b: array<TweakDBID>) -> Bool {
    if ArraySize(a) != ArraySize(b) {
      return false;
    }
    let i = 0;
    while i < ArraySize(a) {
      if a[i] != b[i] {
        return false;
      }
      i += 1;
    }
    return true;
  }

  // The slot an item record goes in (its first placement slot).
  public static func SlotOf(record: TweakDBID) -> TweakDBID {
    let data = TweakDBInterface.GetItemRecord(record);
    if !IsDefined(data) {
      return TDBID.None();
    }
    let slots: array<wref<AttachmentSlot_Record>>;
    data.PlacementSlots(slots);
    if ArraySize(slots) == 0 {
      return TDBID.None();
    }
    return slots[0].GetID();
  }
}

// (measure) What happens to items in a puppet's slots, in the log.
public class TandemSlotWatch extends AttachmentSlotsScriptCallback {
  public let label: String;

  public func OnItemEquipped(slot: TweakDBID, item: ItemID) -> Void {
    TandemNative.Log(s"slots \(this.label): equipped \(TDBID.ToStringDEBUG(ItemID.GetTDBID(item))) in \(TDBID.ToStringDEBUG(slot))");
  }

  public func OnItemEquippedVisual(slot: TweakDBID, item: ItemID) -> Void {
    TandemNative.Log(s"slots \(this.label): visual of \(TDBID.ToStringDEBUG(ItemID.GetTDBID(item))) in \(TDBID.ToStringDEBUG(slot))");
  }

  public func OnItemUnequipped(slot: TweakDBID, item: ItemID) -> Void {
    TandemNative.Log(s"slots \(this.label): unequipped \(TDBID.ToStringDEBUG(ItemID.GetTDBID(item))) from \(TDBID.ToStringDEBUG(slot))");
  }

  public func OnItemUnequippedComplete(slot: TweakDBID, item: ItemID) -> Void {
    TandemNative.Log(s"slots \(this.label): unequip complete \(TDBID.ToStringDEBUG(ItemID.GetTDBID(item))) from \(TDBID.ToStringDEBUG(slot))");
  }
}
