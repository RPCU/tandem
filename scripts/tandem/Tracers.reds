// The others' bullets and charges, drawn on their puppets
// (docs/architecture/presence.md, "Bullet visuals"; engine facts in
// docs/research/engine/presence.md §13.133).
//
// - A tracer: the weapon's own hitscan trail (its record's fx package, the
//   trail an NPC's shot of it leaves) spawned from the puppet's muzzle to
//   where the shooter's game says the bullet ended (`Cue` Trace), and on to
//   where a ricochet of it hit (`Cue` Ricochet), with the package's
//   ricochet trail. An effect: nothing flies, nothing is hit. The bullet
//   Presence.reds fires besides it (TandemPresenceSync.Bullet) brings the
//   impact, its decal and its sounds.
// - A tech weapon's charge (`Cue` Charge): the effects the game plays on an
//   NPC's charging weapon (AIWeapon.StartCharging, final.reds:172812, and
//   OnStopShooting, 173113), on the puppet's copy of the weapon.

public abstract class TandemTracers {
  // Where the weapon's barrel ends (its Muzzle slot), else the weapon.
  public static func Muzzle(weapon: ref<WeaponObject>) -> Vector4 {
    let at = new Vector4(0.0, 0.0, 0.0, 0.0);
    let muzzle = IPositionProvider.CreateSlotPositionProvider(weapon, n"Muzzle");
    if !IsDefined(muzzle) || !muzzle.CalculatePosition(at) || Vector4.IsZero(at) {
      at = weapon.GetWorldPosition();
    }
    return at;
  }

  // The weapon's own trail for a shot of its damage type (an NPC's: the
  // one others see), or its ricochet trail.
  private static func TrailOf(record: ref<WeaponItem_Record>, package: ref<WeaponFxPackage_Record>, ricochet: Bool) -> ResRef {
    let path = package.NPC_vfx_hitscan_trail();
    let damage = record.DamageType();
    let kind = IsDefined(damage) ? damage.DamageType() : gamedataDamageType.Physical;
    if ricochet {
      path = package.Vfx_hitscan_trail_ricochet();
    } else if Equals(kind, gamedataDamageType.Thermal) {
      path = package.NPC_vfx_hitscan_trail_thermal();
    } else if Equals(kind, gamedataDamageType.Electric) {
      path = package.NPC_vfx_hitscan_trail_electric();
    } else if Equals(kind, gamedataDamageType.Chemical) {
      path = package.NPC_vfx_hitscan_trail_chemical();
    }
    return path;
  }

  // A trail from `from` to `to`: the effect spawned at `from` facing `to`,
  // its target position `to`. False: the weapon has no such trail.
  public static func Trail(weapon: ref<WeaponObject>, from: Vector4, to: Vector4, ricochet: Bool) -> Bool {
    if !IsDefined(weapon) || Vector4.Distance(from, to) < 0.5 {
      return false;
    }
    let record = TweakDBInterface.GetWeaponItemRecord(ItemID.GetTDBID(weapon.GetItemID()));
    let package = IsDefined(record) ? record.FxPackage() : null;
    if !IsDefined(package) {
      return false;
    }
    let path = TandemTracers.TrailOf(record, package, ricochet);
    if !ResRef.IsValid(path) && ricochet {
      path = TandemTracers.TrailOf(record, package, false);
    }
    if !ResRef.IsValid(path) {
      return false;
    }
    let resource: FxResource; // assigned on every path: its one field set at once
    ResourceAsyncRef.SetPath(resource.effect, path);
    if !FxResource.IsValid(resource) {
      return false;
    }
    let transform = new WorldTransform();
    WorldTransform.SetPosition(transform, from);
    WorldTransform.SetOrientationFromDir(transform, Vector4.Normalize(to - from));
    let fx = GameInstance.GetFxSystem(weapon.GetGame()).SpawnEffect(resource, transform);
    if !IsDefined(fx) {
      return false;
    }
    let end: WorldPosition; // assigned on every path: filled whole by WorldPosition.SetVector4 at once
    WorldPosition.SetVector4(end, to);
    fx.UpdateTargetPosition(end);
    return true;
  }

  // ---- a charge (kCharge*: 0 idle, 1 charging, 2 ready, 3 full, 4 discharging) ----

  // Our V's charge step: the weapon state machine's own (its shared
  // blackboard's ChargeStep, which ChargeEvents, ChargeReadyEvents,
  // ChargeMaxEvents and DischargeEvents set and every other ranged state
  // sets back to Idle): gamedataChargeStep, the same numbers as kCharge*.
  public static func OurStep(weapon: ref<WeaponObject>) -> Int32 {
    let shared = IsDefined(weapon) ? weapon.GetSharedData() : null;
    if !IsDefined(shared) {
      return 0;
    }
    let step = FromVariant<gamedataChargeStep>(shared.GetVariant(GetAllBlackboardDefs().Weapon.ChargeStep));
    return EnumInt(step);
  }

  // How charged it is, 0..1 of its threshold (the glow's chargeValue).
  public static func OurCharge(weapon: ref<WeaponObject>) -> Float {
    return IsDefined(weapon) ? WeaponObject.GetWeaponChargeNormalized(weapon) : 0.0;
  }

  // A puppet's weapon at charge step `step`, `level` charged: the effects an
  // NPC's charging weapon plays (EnterCharge, charging_tpp, its sound;
  // charged when full; their end, discharge_tpp when let go too early) and
  // the weapon's own glow, its "charge" effect at chargeValue
  // (WeaponObject.OnUpdateWeaponCharge, final.reds:55087).
  // `heard`: that hand of the puppet (each weapon of a dual wielding V
  // charges on its own).
  public static func ShowCharge(heard: ref<TandemHeardHand>, weapon: ref<WeaponObject>, step: Int32, level: Float) -> Void {
    let was = heard.charge;
    heard.charge = step;
    if !IsDefined(weapon) {
      heard.chargeBoard = null;
      return;
    }
    let charging = step >= 1 && step <= 3;
    let wasCharging = was >= 1 && was <= 3;
    if charging && !wasCharging {
      WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.EnterCharge);
      AIActionHelper.PlayWeaponEffect(weapon, n"charging_tpp");
      weapon.AI_PlayChargeStartedSound();
      heard.chargeBoard = new worldEffectBlackboard();
      heard.chargeBoard.SetValue(n"chargeValue", ClampF(level, 0.0, 1.0));
      GameObjectEffectHelper.StartEffectEvent(weapon, n"charge", false, heard.chargeBoard);
    } else if charging && IsDefined(heard.chargeBoard) {
      heard.chargeBoard.SetValue(n"chargeValue", ClampF(level, 0.0, 1.0));
    }
    if step == 3 && was != 3 {
      AIActionHelper.PlayWeaponEffect(weapon, n"charged");
    }
    if wasCharging && !charging {
      AIActionHelper.KillWeaponEffect(weapon, n"charging_tpp");
      AIActionHelper.BreakWeaponEffectLoop(weapon, n"charged");
      WeaponObject.TriggerWeaponEffects(weapon, gamedataFxAction.ExitCharge);
      if IsDefined(heard.chargeBoard) {
        heard.chargeBoard.SetValue(n"chargeValue", 0.0);
      }
      GameObjectEffectHelper.BreakEffectLoopEvent(weapon, n"charge");
      heard.chargeBoard = null;
      if step == 4 {
        // Let go before it was ready: it drains (DischargeEvents).
        AIActionHelper.PlayWeaponEffect(weapon, n"discharge_tpp");
      }
    }
  }
}
