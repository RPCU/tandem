// Garages (docs/architecture/garage.md). Each player's garage is their own:
// a guest summons its own vehicles in the host's world by the game's own
// call (nothing of ours is in the way), and the game that moves a vehicle
// owns it (Vehicles.reds).
//
// What a quest, a gig or a fixer puts in the host's garage while a guest
// takes part is the guest's too, once: the game unlocks a vehicle from a
// quest node (never a reward record, never a script), so the host's game
// watches its own garage and tells the guests that took part
// (VehicleUnlock, by the reward rule); the guest's game notes it in its
// ledger, which lives in its own save. A car the host buys is the host's
// alone. By default the guest's own garage is not touched ("credited"):
// quests can test for a vehicle (questVehicleAvailable_ConditionType) and
// the autofixer goes by its own ownership facts, so unlocking one early is
// the user's call: tandem.ini vehicle_rewards = keep.
//
// Not yet checked in game (scenario garage-guest).

public class TandemGarage {
  private let m_game: GameInstance;
  // Host: our garage as last seen (first seen: nothing is news), and when we
  // last bought a vehicle (what arrives then is a purchase).
  private let m_known: array<TweakDBID>;
  private let m_seen: Bool;
  private let m_last: Float;
  private let m_boughtAt: Float;
  // The car we summoned, as last logged.
  private let m_summoned: EntityID;
  private let m_now: Float;

  public func Start(game: GameInstance) -> Void {
    this.m_game = game;
    this.m_boughtAt = -1000.0;
  }

  // Our own garage: the records of the vehicles unlocked in it.
  public static func Unlocked(game: GameInstance) -> array<TweakDBID> {
    let vehicles: array<PlayerVehicle>;
    let out: array<TweakDBID>;
    GameInstance.GetVehicleSystem(game).GetPlayerUnlockedVehicles(vehicles);
    for vehicle in vehicles {
      if TDBID.IsValid(vehicle.recordID) {
        ArrayPush(out, vehicle.recordID);
      }
    }
    return out;
  }

  // Our V bought a vehicle (the autofixer's purchase, wrapped below).
  public func Bought() -> Void {
    this.m_boughtAt = this.m_now;
    TandemNative.Log("garage: we bought a vehicle: ours alone");
  }

  // Once a second, every game: our summoned car, said once (the scenarios
  // read it); host: a vehicle new in our garage goes to the guests that
  // took part in its quest.
  public func Tick(now: Float, host: Bool) -> Void {
    this.m_now = now;
    if now - this.m_last < 1.0 {
      return;
    }
    this.m_last = now;
    let player = GetPlayer(this.m_game);
    if !IsDefined(player) {
      return;
    }
    let board = GameInstance.GetBlackboardSystem(this.m_game).Get(GetAllBlackboardDefs().VehicleSummonData);
    if IsDefined(board) {
      let summoned = board.GetEntityID(GetAllBlackboardDefs().VehicleSummonData.SummonedVehicleEntityID);
      if summoned != this.m_summoned {
        this.m_summoned = summoned;
        let car = GameInstance.FindEntityByID(this.m_game, summoned) as VehicleObject;
        if IsDefined(car) {
          TandemNative.Log(s"garage: our \(TDBID.ToStringDEBUG(car.GetRecordID())) is here, \(Vector4.Distance(car.GetWorldPosition(), player.GetWorldPosition())) m from our V");
        }
      }
    }
    if !host {
      this.m_seen = false;
      return;
    }
    let now_ = TandemGarage.Unlocked(this.m_game);
    if this.m_seen {
      for vehicle in now_ {
        if !ArrayContains(this.m_known, vehicle) {
          if now - this.m_boughtAt < 30.0 {
            TandemNative.Log(s"garage: \(TDBID.ToStringDEBUG(vehicle)) is new in our garage: bought, not shared");
          } else {
            let at = player.GetWorldPosition();
            let told = TandemStory.VehicleUnlocked(vehicle, at.X, at.Y, at.Z);
            TandemNative.Log(s"garage: \(TDBID.ToStringDEBUG(vehicle)) is new in our garage: \(told) guest(s) that took part are told");
          }
        }
      }
    }
    this.m_known = now_;
    this.m_seen = true;
  }

  // Guest: the host's quest gave a vehicle while we took part. True when
  // our ledger changed (the caller stores it for the next save).
  public func Earned(vehicle: TweakDBID, questHash: Uint32, host: String) -> Bool {
    let system = GameInstance.GetVehicleSystem(this.m_game);
    let name = TDBID.ToStringDEBUG(vehicle);
    let owned = system.IsVehiclePlayerUnlocked(vehicle);
    if !TandemStory.CarryVehicle(vehicle, questHash, owned) {
      TandemNative.Log(s"garage: \(name) with \(host): credited already");
      return false;
    }
    if owned {
      TandemNative.Log(s"garage: \(name) with \(host) (quest \(questHash)): credited; it is in our garage already");
      return true;
    }
    if !TandemStory.VehicleRewardsKept() {
      TandemNative.Log(s"garage: \(name) with \(host) (quest \(questHash)): credited (vehicle_rewards = keep to unlock it in our own garage)");
      TandemCoop.Notice(this.m_game, "Garage-Credited", host);
      return true;
    }
    // Ours too, for good: the game's own garage call (Codeware's by-record
    // form of it), once.
    let ok = system.EnablePlayerVehicleID(vehicle, true);
    if ok {
      TandemStory.CarryVehicleKept(vehicle);
      TandemCoop.Notice(this.m_game, "Garage-Kept", host);
    }
    TandemNative.Log(s"garage: \(name) with \(host) (quest \(questHash)): unlocked in our own garage (\(ok))");
    return true;
  }

  // tools/game.sh cmd garage | garage summon
  public func Command(command: String) -> Void {
    let system = GameInstance.GetVehicleSystem(this.m_game);
    let player = GetPlayer(this.m_game);
    if Equals(command, "garage summon") {
      let asked = system.SpawnActivePlayerVehicle();
      TandemNative.Log(s"garage: summon asked (\(asked))");
    }
    let restricted = IsDefined(player) && VehicleSystem.IsSummoningVehiclesRestricted(this.m_game);
    let earned = TandemStory.CarryVehicles();
    let kept = 0;
    for vehicle in earned {
      if TandemStory.CarryVehicleIsKept(vehicle) {
        kept += 1;
      }
    }
    let unlocked = TandemGarage.Unlocked(this.m_game);
    TandemNative.Log(s"garage: \(ArraySize(unlocked)) vehicles in our garage, summon \(restricted ? "restricted" : "allowed"); earned in co-op \(ArraySize(earned)) (\(kept) kept in our garage; vehicle_rewards = \(TandemStory.VehicleRewardsKept() ? "keep" : "credit"))");
  }
}

// The autofixer's purchase (final.reds:484413): what comes into our garage
// in the next moments is bought, ours alone.
@wrapMethod(gameuiVehicleShopGameController)
protected cb func OnVehicleShopPurchaseEventEvent(evt: ref<VehicleShopPurchaseEvent>) -> Bool {
  TandemNative.HookRan(n"Garage.gameuiVehicleShopGameController.OnVehicleShopPurchaseEventEvent");
  let story = TandemStorySystem.Get(GetGameInstance());
  if IsDefined(story) {
    story.OnVehicleBought();
  }
  return wrappedMethod(evt);
}
