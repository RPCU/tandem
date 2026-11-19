# Traffic, world vehicles and police: what the game exposes (2.50)

Read on 2026-11-19 for [`architecture/traffic.md`](../../architecture/traffic.md)
and [`architecture/police.md`](../../architecture/police.md). **Nothing here
was run in game**: the game machine was in use, so every line is from the
decompiled scripts, the RTTI dump or Codeware's source. Earlier notes on the
same systems: [`coop-quests-world.md`](../coop-quests-world.md) §9 (police)
and §10 (crowds and traffic), [`coop-regions.md`](../coop-regions.md) §5,
[`engine-knowledge.md`](../engine-knowledge.md) §6 and §13.6 (vehicles).

| Cited as          | What                                                                                     |
| ----------------- | ---------------------------------------------------------------------------------------- |
| `final.reds:LINE` | decompiled vanilla scripts of 2.50, `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` |
| RTTI              | `docs/research/data/rtti-natives-2.50.tsv` (live dump of 2.50)                           |
| `CW path`         | Codeware source (MIT), `~/.cache/tandem/ref/codeware`                                    |

Tags: `[S]` read in the vanilla scripts, `[R]` read in the RTTI dump, `[C]`
Codeware's source, `[T]` measured by tandem in game, `[I]` inferred from
those (stated nowhere, not tested), `[U]` reported by the user from play.

## 13.32 Traffic and world vehicles

### What traffic is

- **Traffic vehicles are crowd members.** A traffic car is a vehicle with a
  `gameCrowdMemberComponent` driven along lane splines by the native
  `worldRuntimeSystemTraffic` (size 0x92d0, no script functions); its
  driver and passengers are ordinary crowd NPCs mounted in it. The vehicle
  side: `vehicleBaseObject.IsCrowdVehicle()`, `HasTrafficSlot()` (the same
  native handler, `0x141525d70`), `IsInTrafficPhysicsState()`
  (`0x142b99670`), the blackboard flag `Vehicle.IsCrowd`, the script
  callbacks `OnInCrowd` / `OnOutOfCrowd` (`final.reds:88921-88937`, they
  set `m_inTrafficLane`) and `m_crowdMemberComponent.ChangeMoveType(n"stop"
/ n"normal")`. `[S]` `[R]`
- **The script interface to the traffic system has two queries and no
  switch**: `TrafficSystem.IsPathIntersectingWithTraffic` and
  `FindEntitiesNearPlane` (`final.reds:317434`, RTTI
  `worldTrafficScriptInterface`). Nothing spawns, despawns or scales
  traffic from scripts. `[S]` `[R]`
- **Crowd density covers the cars.** With the guest's crowd turned off
  (`CommunitySystem.ChangeDensityModifier(0.0)`, `Npcs.reds`), its streets
  are empty of traffic too: this is what the user sees today ("each game
  has its own traffic, or none where the crowd is suppressed"). `[U]` It
  fits traffic vehicles being crowd members. Not measured: how long cars
  already spawned stay, and whether parked crowd cars
  (`worldCompiledCrowdParkingSpaceNode`) go with them. `[I]`
- **Parked cars** come in two kinds. Placed vehicles (a quest's car, a
  gang's cars in front of its hideout) are world entities with a static
  `EntityID`, the same in every game (`EntityID.IsStatic`,
  `final.reds` native struct). Crowd parking spaces are filled by the crowd
  system per game: their cars have dynamic ids and differ between two games
  (the roll is not seeded by anything shared). `[S]` for the id kinds,
  `[I]` for the roll.
- **No script call removes a vehicle that is not ours.** Codeware's
  `DynamicEntitySystem.DeleteEntity` only knows entities it spawned
  (`CW scripts/World/DynamicEntitySystem.reds`); `gamePuppet.HideIrreversibly`
  is for puppets; `PreventionSpawnSystem.RequestDespawn` /
  `RequestDespawnVehicleAndPassengers(vehicle)` take any id or vehicle but
  are written for what that system spawned (`IsEntityRegistered`);
  `ReactionSystem.MarkDespawnCandidate(id)` is used by an AI task on NPCs
  (`final.reds:474500`). Whether the last two take a traffic car away is a
  probe for a day the game is free. `[S]` `[C]` `[I]`

### Finding the vehicles of the world

- The targeting filters have no vehicle class (`TSFMV`: player, puppet,
  sensor, device, other; `final.reds` enum), so
  `GetEntitiesAroundObject` is not a way to list cars. `[S]`
- Every vehicle runs script callbacks when it enters and leaves the world:
  `VehicleObject.OnGameAttached()` and `OnDetach()`
  (`final.reds:88001`, `:88015`). Wrapping both gives a complete list of
  the vehicles of a game's world, by name, with no engine layout.
  `[S]`; used by `Traffic.reds`, not yet run.
- Telling them apart: `IsCrowdVehicle()`, `IsVehicleParked()`,
  `IsPlayerVehicle()` (a garage car), `IsPrevention()`
  (`final.reds:87731`), `IsQuest()` (`VehicleComponentPS.IsMarkedAsQuest`),
  `IsAbandoned()`, `IsStolen()`, `IsDestroyed()`, `HasPassengers()`,
  `VehicleComponent.HasActiveDriverMounted(game, id)`,
  `VehicleComponent.GetAllPassengers(game, id, includeTrunkBody, out)`
  (`final.reds:89773`); classes `WheeledObject` (cars and bikes: `CarObject`,
  `BikeObject`), `AVObject`, `TankObject`. `[S]`
- State worth sharing, all readable: `GetWorldPosition/Orientation`,
  `GetLinearVelocity()`, the `Health` stat pool of the vehicle's id (a
  street car has about 1050 points, engine-knowledge §6),
  `GetVehiclePS().GetSirenState()`, `IsPerformingPanicDriving()`. `[S]`

### Collisions, hits, explosions

- **A bump is an event on the vehicle**: `VehicleBumpEvent{impactVelocityChange,
isInTraffic, hitVehicle, hitNormal}` → `VehicleObject.OnVehicleBumpEvent`
  (`final.reds:88458`). For a vehicle in traffic it calls the private
  `HandleTrafficBump(impact)`: above `AIGeneralSettings.minUnconsciousImpact`
  (6.5 m/s) the passengers may be knocked out
  (`TriggerUnconsciousBehaviorForPassengers`), else the driver escalates
  (`EscalateBumpVehicleReaction`), and it honks below the honking
  threshold. `[S]`
- **Damage goes through the damage system like an NPC's**: `OnHit` →
  `DamagePipelineFinalized(evt)` (`final.reds:88832`: a stim to the driver
  when the passengers are police, then `ApplyDamagesToDriver`). The
  vehicle's `Health` pool reaching zero is `VehicleComponent.OnDeath`
  (`:91722`): `ExplodeVehicle(instigator)` (the `Attacks.CarMediumKill`
  game effect, parts detached, the explosion effects, `KillPassengers`,
  the wreck appearance), then `PreventionSystem.NotifyVehicleExplosion`
  when `ShouldPreventionReactToExplosion()` (hit by the player, a crowd or
  parked car). So **setting a proxy's health to zero is the game's own
  explosion**, with its sound, fire and wreck. `[S]`
- The damage stages in between (smoke, fire) follow the health pool by
  themselves on whatever game holds the vehicle. `[I]` from the stage
  effects being driven from `VehicleComponent`'s health callbacks.
- A fleeing driver: `TriggerDrivingPanicBehavior(threatPosition)` (private,
  `final.reds:88621`), guarded by `CanStartPanicDriving()`. `[S]`

### Getting in, stealing

- `VehicleObject.OnMountingEvent(evt)` (`final.reds:88088`) runs for every
  mount; for the player it calls `ReevaluateStealing(character, slot,
occupiedByNonFriendly)` and then `StealVehicle(thief)`: a
  `CrowdIllegalAction` stim, a `CrimeWitness` active stim for 4.4 s, and
  `GetVehiclePS().SetIsStolen(true)`. `[S]`
- The driver being pulled out is the player's own workspot sequence;
  `VehicleComponent.StealVehicle(slotID)` (`:92056`) sends the passenger a
  `VehicleHijackEvent{driverAllowedToGetAggressive}` and the vehicle a
  `StealVehicleEvent` (which sets `m_abandoned`). `[S]`
- **Stealing a car is no heat by itself**: `PreventionVehicleStolenRequest`
  only counts for NCPD vehicles (`OnVehicleStolenRequest`, `:141470`), and
  nothing in the scripts creates that request (native, or dead). What a
  theft costs is the witnesses' `CrimeWitness` reaction. `[S]`

### Moving a vehicle we do not simulate

Unchanged from engine-knowledge §13.6 `[T]`: a dynamic vehicle teleported
every frame with the teleportation facility stays exactly where it is put;
deleting a dynamic vehicle that has had an occupant crashes the game.
New for traffic, all `[I]`: forty teleports a frame were never measured
(the plugin only teleports the ones that moved); a real traffic vehicle
moved by teleport after its driver left it (the owner's side of a theft)
was never tried, nor was `DynamicEntitySystem.DisableEntity` as a way to
retire a proxy.

## 13.33 Police: the prevention system

The prevention system is described in `coop-quests-world.md` §9: a script
system (`PreventionSystem`, `final.reds:138624-143058`) that keeps the
score, the heat stage and the stars, over a native spawn system
(`PreventionSpawnSystem`, `:189163`). What follows is what the co-op rule
needed beyond it.

### How a crime is attributed

Every input checks for **the local player**, by `IsPlayer()` on the
instigator or by taking the local player outright. There is no notion of
"a player":

| Crime                              | Where                                                                                                                                                                                                                      | Attributed by                   |
| ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------- |
| damage to a civilian, a cop, a car | the damage system, when a hit lands: `SendDamageRequestToPreventionSystem(hitEvent)` when `hitEvent.attackData.GetInstigator().IsPlayer()` and the damage is above 1.5 % of the target's health (`final.reds:85852-85875`) | the hit's instigator            |
| a kill                             | `NPCPuppet.CheckIfPreventionShouldReact(damageDealers)`: any dealer `IsPlayer()` (`:12644`)                                                                                                                                | the damage dealers              |
| attacking the police               | `ScriptedPuppet` damage handler: `instigator.IsPlayer() && ShouldPreventionSystemReactToDamageDealt(this)` → `CombatStartedRequestToPreventionSystem` (`:21899`)                                                           | the hit's instigator            |
| a witness calls it in              | the AI task `CallPolice.Activate` → `CrimeWitnessRequestToPreventionSystem(game, localPlayer.GetWorldPosition())` (`:483172`)                                                                                              | **nobody: always the local V**  |
| hacking a police car               | `VehicleComponentPS`, with the local `player` (`:96505-96530`)                                                                                                                                                             | the quickhack's own player      |
| a prevention turret acquiring      | `SecurityTurret.OnCurrentTargetAppears` → `CombatStartedRequestToPreventionSystem(game, this)` (`:171422`)                                                                                                                 | **nobody: whatever the target** |
| the police see V                   | `OnSenseVisibilityEvent`: `evt.target.IsPlayer()` and the owner is prevention → `RegisterAsViewerToPreventionSystem` / `UnRegister…` (`:144065-144074`)                                                                    | the seen target                 |
| a car V shot blows up              | `VehicleComponent.OnDeath` → `NotifyVehicleExplosion` when `m_hitByPlayer` (`:91722`)                                                                                                                                      | a flag set by the local V's hit |

What counts as a victim: `ShouldPreventionSystemReactToAttack(target, …)`
(`:141717`): a puppet that is crowd, vendor, civilian or prevention (or
tagged `TriggerPrevention`, and not `DoNotTriggerPrevention`), once per
puppet for a kill (`PuppetPS.IsPreventionNotified`); a vehicle that is
prevention or has passengers and is not destroyed. `IsPrevention()` is the
record's (`IsCharacterPolice`), so **a proxy spawned from a police record is
police on the guest's game too**. `[S]`

Consequences `[I]`:

- A guest's hit on a proxy runs the guest's own damage system with its V
  as instigator: its own prevention system scores it. Nothing to add.
- The host applies that hit to the real NPC with the guest's **puppet** as
  instigator (`Outcomes.reds`): not a player, so the host's V is not
  blamed. Nothing to add.
- The two inputs that name nobody (`CallPolice`, the turret) would blame
  the host's V for what a guest's puppet did. Those are the ones to wrap.
- The stimulus an NPC reacts to keeps its source:
  `ScriptedPuppet.GetStimReactionComponent().GetActiveReactionData()` (or
  `GetDesiredReactionData()`) is an `AIReactionData` whose `stimTarget` is
  the object the stimulus came from (`final.reds:147824-147828`, class
  `AIReactionData`). That is the culprit a witness saw. `[S]`

### The score and the stars

- `ProcessPreventionDamageRequest` (`:140645`) adds to `m_totalCrimeScore`
  by victim and attack type from the district's heat table and raises the
  stage by one when the score passes `HeatThresholdCapacity`
  (`HeatPipeline`, `:140714`). `OnCrimeWitnessRequest`,
  `OnCombatStartedRequest` and `OnVehicleStolenRequest` raise it by one
  when it was zero. `[S]`
- `ChangeHeatStage` (`:140733`) is the one place the stage changes; it
  writes `UI_WantedBar.CurrentWantedLevel`. `GetHeatStageAsInt()`,
  `GetStarState()` (`EStarState`: Default 0, Active 1, Searching 2,
  Blinking 3) and `IsChasingPlayer()` are public. `[S]`
- Nothing reacts while `CanPreventionReactToInput()` is false
  (`:139635`): no player, the system off or locked, a security area reset,
  a scene tier above 1, or V in vehicle combat. `IsMultiplayer()` is one
  of its conditions and is false in tandem. `[S]`
- Forcing a stage: the quest request `SetWantedLevel{m_wantedLevel, …}`
  (`:141996`); standing down: `PreventionForceDeescalateRequest`. `[S]`

### Who the police are hostile to

`ChangeAgentsAttitude` (`:141851`) sets the **group** relation between
`police` (and the registered agent groups) and the local player's attitude
group, persistently. So hostility is per attitude group, and tandem's
puppets, each in a group of its own (`Attitudes.reds`), are not touched by
it. But `Attitudes.reds` copies V's relations onto the puppets every 2 s:
without an exception the host's stars would make the police hostile to
every guest. `[S]` `[I]`

At stage 0 (`OnHeatChanged`, `:140769`) each registered unit drops the
player as a threat, turns neutral to it, goes `Relaxed`, gets the status
effects `PreventionStatusEffect.OnStarDropToZero` and
`BaseStatusEffect.MemoryWipeLevel2Police`, and
`StopPoliceBehaviour()`. That is the recipe for standing a unit down. `[S]`

### Spawning

Four script functions are the only callers of the native spawn requests
`[S]`:

| Script (private)                      | Native                                                     |
| ------------------------------------- | ---------------------------------------------------------- |
| `RequestUnitSpawn(record, transform)` | `PreventionSpawnSystem.RequestUnitSpawn` (`:140129`)       |
| `SpawnPoliceVehicle() -> Uint32`      | `RequestChaseVehicle(vehicle, passengers, strategy)`       |
| `SpawnRoadblockade(heatStage)`        | `RequestRoadBlockadeSpawn`                                 |
| `RequestAVSpawnAtPosition(position)`  | `RequestAVSpawnAtLocation` (MaxTac, `Vehicle.max_tac_av*`) |

- On-foot units are placed by the script at points the native system finds
  around given origins: `FindPursuitPointsRangeAsync(origins, radiusMin,
radiusMax, unitCount, characterRecords, navVisCheck, agentSize,
scriptable, functionName)` calls `functionName(spawnPoints: [Vector3],
characterRecords: [TweakDBID], unitCount: Uint32) -> Int32` on the
  scriptable (`TrySpawnPoliceOnFootFallback`, `SpawnFallbackUnits`,
  `:140949-141006`). **The origins are arguments**: units can be asked
  around any point, a remote V's puppet included. `[S]`
- Chase vehicles, roadblocks and the AV search are written around "the
  player" inside the native system (`IsPlayerOnHighway`,
  `GetIntersectionInFrontOfPlayerPos`, the strategies
  `DriveTowardsPlayer`…): they cannot be aimed at a puppet from scripts.
  `[S]` `[I]`
- Every finished request comes back as
  `PreventionSpawnSystem.SpawnRequestFinished(result)` →
  `PreventionUnitSpawnedRequest` → `PreventionSystem.OnPreventionUnitSpawnedRequest`
  (`:189272`, `:140183`), which looks the ticket up in its registry and
  drops a result it has no ticket for. `SpawnRequestResult{requestID,
policeStrategy, vehicleType, spawnedObjects, success}`. `[S]`
- Records that exist by name in the scripts: `Character.prevention_unit_tier1`,
  `_tier2`, `_tier3` (`:138797`), the MaxTac squads
  `Character.maxtac_av_*` (`:317672`). The pools the game really draws
  from are per district and stage in TweakDB
  (`PreventionHeatData.UnitRecordsPool`). `[S]`

## 13.34 Traffic and police: open questions (need the game)

1. Does `ChangeDensityModifier(0.0)` remove the crowd's **parked** cars,
   and how fast do already spawned traffic cars leave?
2. Do `PreventionSpawnSystem.RequestDespawnVehicleAndPassengers` or
   `ReactionSystem.MarkDespawnCandidate` take a traffic car away?
3. Cost of teleporting 30–40 vehicles a frame; wheels and suspension of a
   teleported proxy (they were not checked for player car copies either).
4. Does a proxy whose health is set to zero explode with the same timing
   on every game, and does the explosion's own attack hurt the local V
   only (it should: the target's game decides)?
5. A real traffic vehicle whose NPC driver was unmounted by script and
   which is then teleported every frame (the owner's side of a theft): does
   the traffic system let go of it?
6. Does a unit spawned by `RequestUnitSpawn` whose ticket the script
   registry does not know behave (it is not registered as a police agent:
   no `StartPoliceBehaviour`), and does it chase a puppet it was made
   hostile to?
7. Does `AIReactionData.stimTarget` name the puppet when the stimulus was
   broadcast from it (`Stealth.reds` broadcasts a guest's noise from its
   puppet)?
8. Do a guest's hits on police proxies raise its own heat (the proxies'
   `IsPrevention()` and `IsCrowd()` on a dynamic entity)?
