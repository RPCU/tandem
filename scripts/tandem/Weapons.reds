// Weapons between players: what each V holds (TandemNative.SendHeld from
// TandemSystem), and each shot, reload and melee swing, drawn on the puppet
// as the weapon's own effects and sounds, with no projectile or hit.
//
// Dual wielding (patch 2.4; docs/architecture/players.md, "Dual
// wielding"): the game runs one weapon state machine per held weapon, its
// owner that weapon (`WeaponRight`, `WeaponLeft`), so every wrap below runs
// once per hand and says which (IsLeftWeaponLogic): the shot, its bullet,
// its smart round and its reload are the hand's own on the others' games.

// Every shot of the local V's weapons passes here (the weapon state
// machine's shoot state, entered once per shot; burst and full auto
// re-enter it): docs/research/coop-player-vehicles.md §3.4.
@wrapMethod(ShootEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    let weapon = this.GetWeaponObject(scriptInterface);
    if IsDefined(weapon) {
      let left = this.IsLeftWeaponLogic();
      // gamedataFxAction: Shoot 16, SilencedShoot 17
      TandemNative.SendShot(left, weapon.IsSilenced() ? 17 : 16);
      if left {
        TandemNative.LogDebug(s"weapons: our left hand's shot (\(TDBID.ToStringDEBUG(ItemID.GetTDBID(weapon.GetItemID()))))");
      }
      // A smart weapon's round homes on its lock on the others' games too
      // (Throws.reds); in a session only.
      let guided = false;
      let throws = TandemThrows.Of(scriptInterface.executionOwner.GetGame());
      if IsDefined(throws) && Equals(RPGManager.GetWeaponEvolution(weapon.GetItemID()), gamedataWeaponEvolution.Smart) {
        guided = throws.OnOurSmartShot(weapon, left);
      }
      // Any other shot, and a smart round with no lock the others know:
      // where it ended, for the others to draw the bullet (Presence.reds).
      if !guided {
        TandemPresenceSync.ShotFired(scriptInterface.executionOwner, weapon, left);
      }
    }
  }
}

// Reloads: the weapon's reload effects on the puppet (the hands are the pose
// stream's). EnterReload 5 when it starts, ExitReload 11 when it ends or
// is interrupted. Each hand's weapon reloads in its own state machine.
@wrapMethod(ReloadEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(this.IsLeftWeaponLogic(), 5);
  }
}

@wrapMethod(ReloadEvents)
protected func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(this.IsLeftWeaponLogic(), 11);
  }
}

// Melee swings: every attack state (combo, strong, final, sprint, jump...)
// calls this base. On the puppet the weapon's own attack sound, as NPCs
// play it (tandem's effect 200: not a gamedataFxAction).
@wrapMethod(MeleeAttackGenericEvents)
public func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(false, 200);
  }
}

// Quick melee with a ranged weapon (pistol whip, rifle butt): a state of
// its own in the weapon state machine, not a melee attack state. Effect 201.
@wrapMethod(QuickMeleeEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(this.IsLeftWeaponLogic(), 201);
  }
}

public abstract class TandemWeapons {
  public static func Held(owner: ref<GameObject>, slot: TweakDBID) -> TweakDBID {
    let item = GameInstance.GetTransactionSystem(owner.GetGame()).GetItemInSlot(owner, slot);
    return IsDefined(item) ? ItemID.GetTDBID(item.GetItemID()) : TDBID.None();
  }

  // Puts `record` in `slot` on the puppet in place of what was there.
  public static func Hold(puppet: ref<GameObject>, slot: TweakDBID, record: TweakDBID) -> Bool {
    let transactions = GameInstance.GetTransactionSystem(puppet.GetGame());
    // Already in that hand, or still spawning there: nothing to do. Asked
    // again meanwhile, the removal below destroyed the item still spawning
    // (RemoveItemFromSlot destroys a pending spawn: engine/presence.md) and
    // the hand stayed empty (a puppet re-armed twice at its spawn).
    if TDBID.IsValid(record) {
      let held = transactions.GetItemInSlot(puppet, slot);
      if IsDefined(held) && ItemID.GetTDBID(held.GetItemID()) == record {
        return true;
      }
      // Any item: a weapon given is a unique item (its own seed), so a query
      // made from the record never names the one spawning.
      if transactions.IsSlotSpawningAnyItem(puppet, slot) {
        return true;
      }
    }
    transactions.RemoveItemFromSlot(puppet, slot);
    if !TDBID.IsValid(record) {
      return true;
    }
    let item = ItemID.FromTDBID(record);
    transactions.GiveItem(puppet, item, 1);
    return transactions.AddItemToSlot(puppet, slot, item);
  }

  // Was this hit of `owner`'s made with the weapon in its left hand (the
  // off hand of a dual wielding V; the arm launcher)?
  public static func ByLeftHand(owner: ref<GameObject>, hitEvent: ref<gameHitEvent>) -> Bool {
    if !IsDefined(owner) || !IsDefined(hitEvent) || !IsDefined(hitEvent.attackData) {
      return false;
    }
    let used: wref<WeaponObject> = hitEvent.attackData.GetWeapon();
    let offHand: wref<WeaponObject> = GameInstance.GetTransactionSystem(owner.GetGame()).GetItemInSlot(owner, t"AttachmentSlots.WeaponLeft") as WeaponObject;
    return IsDefined(used) && IsDefined(offHand) && used == offHand;
  }

  // Do two held items make a V dual wield? The game's own test of two
  // weapons in hand (GameObject.GetDualWieldWeaponNames): both carry the
  // tag the inventory pairs weapons by (RPGManager.GetDualWieldWeaponTag,
  // "DualWield": pistols and revolvers). Read from the records, so before
  // the items have spawned in the hands. An arm launcher or a throwable in
  // the left hand is not a pair.
  public static func IsPair(right: TweakDBID, left: TweakDBID) -> Bool {
    if !TDBID.IsValid(right) || !TDBID.IsValid(left) {
      return false;
    }
    let first = TweakDBInterface.GetItemRecord(right);
    let second = TweakDBInterface.GetItemRecord(left);
    return IsDefined(first) && IsDefined(second) && first.TagsContains(n"DualWield") && second.TagsContains(n"DualWield");
  }

  // A puppet's hands as `dual` says (a body that dual wields, or stopped):
  // what the game's equip states set on V besides the items
  // (EquipCycleInitEvents: the body's `dual_wield` animation wrapper;
  // ScriptedPuppet.OnItemAddedToSlot: each weapon's `RightHand`, `LeftHand`
  // and `dualWield` wrappers, which the weapon's own rig picks its
  // one-handed, mirrored animations by). The puppet's body follows the pose
  // stream either way; the weapons' own animations do not.
  public static func DualWield(puppet: ref<GameObject>, dual: Bool) -> Void {
    AnimationControllerComponent.SetAnimWrapperWeight(puppet, n"dual_wield", dual ? 1.0 : 0.0);
    // The game refreshes these itself when an item lands in a slot of a
    // ScriptedPuppet; again here for an item that was there already.
    AnimationControllerComponent.RefreshDualWieldAnimWrapperWeight(puppet);
  }

  public static func Fire(puppet: ref<GameObject>, left: Bool, effect: Int32) -> Void {
    let slot = left ? t"AttachmentSlots.WeaponLeft" : t"AttachmentSlots.WeaponRight";
    let weapon = GameInstance.GetTransactionSystem(puppet.GetGame()).GetItemInSlot(puppet, slot) as WeaponObject;
    if !IsDefined(weapon) {
      return;
    }
    if effect == 200 || effect == 201 {
      weapon.AI_PlayMeleeAttackSound(effect == 201);
    } else {
      WeaponObject.TriggerWeaponEffects(weapon, IntEnum<gamedataFxAction>(effect));
      // What the game pushes with an NPC's shot and reload besides the
      // effects (AIWeapon.Fire, final.reds:173344; the reload action,
      // 438285): the weapon's own animation event, which its sounds and
      // moving parts hang on.
      if effect == 15 || effect == 16 || effect == 17 {
        AnimationControllerComponent.PushEventToReplicate(weapon, n"Shoot");
      } else if effect == 5 {
        AnimationControllerComponent.PushEventToReplicate(weapon, n"Reload");
      }
    }
  }
}
