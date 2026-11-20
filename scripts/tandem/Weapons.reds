// Weapons between players: what each V holds (TandemNative.SendHeld from
// TandemSystem), and each shot, reload and melee swing, drawn on the puppet
// as the weapon's own effects and sounds, with no projectile or hit.

// Every shot of the local V's weapon passes here (the player state
// machine's shoot state, entered once per shot; burst and full auto
// re-enter it): docs/research/coop-player-vehicles.md §3.4.
@wrapMethod(ShootEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    let weapon = this.GetWeaponObject(scriptInterface);
    if IsDefined(weapon) {
      // gamedataFxAction: Shoot 16, SilencedShoot 17
      TandemNative.SendShot(false, weapon.IsSilenced() ? 17 : 16);
      // A smart weapon's round homes on its lock on the others' games too
      // (Throws.reds); in a session only.
      let guided = false;
      let throws = TandemThrows.Of(scriptInterface.executionOwner.GetGame());
      if IsDefined(throws) && Equals(RPGManager.GetWeaponEvolution(weapon.GetItemID()), gamedataWeaponEvolution.Smart) {
        guided = throws.OnOurSmartShot(weapon);
      }
      // Any other shot, and a smart round with no lock the others know:
      // where it ended, for the others to draw the bullet (Presence.reds).
      if !guided {
        TandemPresenceSync.ShotFired(scriptInterface.executionOwner, weapon, false);
      }
    }
  }
}

// Reloads: the weapon's reload effects on the puppet (the hands are the pose
// stream's). EnterReload 5 when it starts, ExitReload 11 when it ends or
// is interrupted.
@wrapMethod(ReloadEvents)
protected final func OnEnter(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(false, 5);
  }
}

@wrapMethod(ReloadEvents)
protected func OnExit(stateContext: ref<StateContext>, scriptInterface: ref<StateGameScriptInterface>) -> Void {
  wrappedMethod(stateContext, scriptInterface);
  if (TandemNative.IsStarted() || TandemNative.Recording()) && IsDefined(scriptInterface.executionOwner as PlayerPuppet) {
    TandemNative.SendShot(false, 11);
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
    TandemNative.SendShot(false, 201);
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
    transactions.RemoveItemFromSlot(puppet, slot);
    if !TDBID.IsValid(record) {
      return true;
    }
    let item = ItemID.FromTDBID(record);
    transactions.GiveItem(puppet, item, 1);
    return transactions.AddItemToSlot(puppet, slot, item);
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
