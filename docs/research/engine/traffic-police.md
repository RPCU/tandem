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

A unit's hostility is not only its group's. Vanilla turns a fighting NPC
hostile to whatever tops its threats (`SetTopThreatToCombatTarget.SetCombatTarget`,
`:456609`, `ChangeAttitudeToHostile` on any target but a player), and
`AIActionHelper.TryChangingAttitudeToHostile` (`:169916`: help an ally,
a hostile stimulus, a hit) turns an aggressive NPC's whole group on a
neutral target's (`SetAttitudeTowardsAgentGroup`). The one guard for the
police is in the combat target check (`:456663`): a prevention NPC drops
the player only when `ShouldReactionBeAgressive` is false, which it is not
for a V that is simply not wanted (`:141620`). `[S]` Units sent after a
puppet took the spawning game's own clean V on and killed it
(police-host-cars, 2026-11-21) `[T]`; which of these paths put V among
their threats was not seen `[I]`.
`GameObject.GetReceivedDamageByPlayerLastTimeStamp()` (`:12472`) is the
engine time of the last player hit in an NPC's damage history, which keeps
only the latest frame's hits. `[S]`

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
- Chase vehicles, roadblocks and the AV search are **placed** around "the
  player" inside the native system (`IsPlayerOnHighway`,
  `GetIntersectionInFrontOfPlayerPos`, the strategies
  `DriveTowardsPlayer`…). `[S]` `[I]` What a chase car then **does** is an
  AI command with any target ("Police cars and the AV for someone else").
- Every finished request comes back as
  `PreventionSpawnSystem.SpawnRequestFinished(result)` →
  `PreventionUnitSpawnedRequest` → `PreventionSystem.OnPreventionUnitSpawnedRequest`
  (`:189272`, `:140183`), which looks the ticket up in its registry and
  drops a result it has no ticket for. `SpawnRequestResult{requestID,
policeStrategy, vehicleType, spawnedObjects, success}`. `[S]`
- Records named in the scripts: `Character.prevention_unit_tier1`,
  `_tier2`, `_tier3` (`:138797`, the fallback's failsafe), the MaxTac
  squads `Character.maxtac_av_*` (`:317672`). The pools the game really
  draws from are per district and stage in TweakDB
  (`PreventionHeatData.UnitRecordsPool`, weighted
  `PreventionUnitPoolData` entries; the district's preset is
  `PreventionSystem.m_preventionPreset`, `PreventionData.NCPD` by
  default, `:141848`). `[S]`
- **The three tier records are not in 2.50's TweakDB**: their ids
  (CRC32 of the name and its length) are nowhere in `r6/cache/tweakdb_ep1.bin`
  (2026-11-21), where `Character.Judy`, `Character.Panam` and
  `PreventionData.NCPD` are. `RequestUnitSpawn` with one comes back
  `success = false` some 50 ms later, every time (`police-host`,
  2026-11-21). `Police.reds` draws from the district's pool instead.
  [2.5-verified]

### Police cars and the AV for someone else

Read on 2026-11-21 for `Police.reds`'s cars and AV for a remote V; none of
it has run in game. [2.5-untested]

- `RequestChaseVehicle(vehicle, passengers, strategy)` (`:189170`) spawns
  a car with its crew seated; the prevention system picks the car by
  weight from the level's `PreventionHeatData.VehicleRecordPool`
  (`GetVehicleRecordFromPool`, `:139139`), the crew from the car's
  `Vehicle_Record.PreventionPassengers`, between its
  `Min/MaxVehiclePassengersCount` (`:139301`, `SpawnPoliceVehicle`
  `:141278-141356`). The strategy request carries only ranges
  (`BaseStrategyRequest{distanceRange, minDirectDistance}` and an
  `angleRange` for some, `:199187-199280`), made from the level's
  `PreventionHeatTable.StrategyN()` (`CreateStrategyRequest`, `:141228`).
  `[S]`
- **Strategies are only available while asked for**: the spawn system
  checks the ones given to `SetStrategyPreCheckRequests` (`IsStrategyAvailable`,
  `GetRandomAvailableStrategy`), and the prevention system gives them only
  while V is chased and clears them otherwise (`UpdateStrategyPreCheckRequests`,
  `:141167`). `[S]` That a car spawns for a script that sets them itself
  while V is not chased is `[I]`.
- The car's behaviour is a vehicle AI command sent as
  `AICommandEvent{command}` to the car and kept as
  `AIVehicleAgent.SetInitCmd` (`:160847`). The police's own chase:
  `AIVehicleChaseCommand{target, distanceMin 3, distanceMax 10,
forcedStartSpeed}` with the local V as target (`ChasePlayer`, `:142449`);
  the dynamic spawn system's chase cars add `ignoreChaseVehiclesLimit` and
  `boostDrivingStats` (`DynamicSpawnSystem.SpawnCallback`, `:189617-189633`).
  `target` is any `GameObject`. Standing a car down: the spawn system's
  `InterruptAllActionAndCommands(car)` and an `AIVehiclePanicCommand`
  (`StartVehicleDeescalationBehavior`, `:142670`, sent by `execInstructionSafe`, `:142654`); taking it away,
  `RequestDespawnVehicleAndPassengers` (`:193565`). No
  `AINPCCommandEvent` in 2.50's scripts. `[S]`
- A crew gets out near its target by `AITargetInUnmountingRangeCond`
  (`:163458-163488`): the spawn system's unmounting range for the player,
  **15 m from any other target**. `[S]`
- `NavGenAgentSize` is `Human 0, Vehicle 1` in the RTTI (RED4ext.SDK's
  generated `NavGenAgentSize.hpp`); the scripts declare `Human` alone. `[R]`
- The AV: `RequestAVSpawnAtLocation(record, location)` (`:189220`); the
  prevention system's first wave is `Vehicle.max_tac_av1/2/3`, the second
  `Vehicle.max_tac_av_2nd_wave*`, one AV at a time, at five stars only
  (`CanRequestAVSpawn`, `RequestAVSpawnAtPosition`, `:140016`, `:141117`).
  `RequestAVSpawnAtPosition` also stops the local V's car
  (`PreventionSystemHackerLoop.ForceCarToStop`). `[S]`
- From a study of another co-op mod (NightCoop, on 2.31, facts only): its
  host gives a wanted guest chase cars at points found with agent size
  "vehicle", skipping the points inside the guest's camera view, crewed,
  each given an `AIVehicleChaseCommand` on the guest's body (`target`,
  `distanceMin`, `distanceMax`, `forcedStartSpeed`,
  `ignoreChaseVehiclesLimit`, `boostDrivingStats`) as an event and as its
  init command; at five stars one AV per guest by
  `RequestAVSpawnAtLocation` near the guest's body; units on foot when no
  car can be placed. It also reports that a unit spawned off the spawning
  game's screen may not see a remote V: its sensor stays where it spawned,
  its skeleton not being updated off screen. [2.5-untested]

### 13.102 The strategy pre-check needs all seven strategies (2026-11-21, game 2.50, exe read)

`SetStrategyPreCheckRequests` (`0x1408fcf28`, then `0x1408fd010`) stores
each request in a fixed slot by strategy, (strategy − 1) × 0x1d8 from +0x2d8
of the checker at system+0x40, gives it a callback (`0x1408fd118`) and turns
the checker on (+0xfd8 = 1). Its timed updater (`0x14044dc98`) queues all
seven slots, set or not, and the job (`0x14044e0a4`, hash 1848187983) reads
the strategy byte at req+0 and calls the slot's callback (req+0x28, its
implementation at req+0xa8) through a thunk (`0x14044efec`, hashes
1034173256 and 1856516452: `mov rax,[rcx+0x80]; jmp [rax]`). A slot never
set has no callback: a null call at `Cyberpunk2077.exe+0x44eff3`. `ClearStrategyPreCheckRequests`
(`0x1408fdea8`) leaves the slots filled, so the crash needs slots vanilla
has not filled yet (a fresh load, our V never chased). Vanilla always sets
all seven (`UpdateStrategyPreCheckRequests`, final.reds :141167); a set of
three (strategies 1, 5, 7) crashed a job thread 30 ms later (`police-host`,
2026-11-21). `Police.reds` now asks all seven. [broken-on-2.5]

### 13.156 Traffic lights: the native crossing tells each light its colour (2026-11-22, game 2.50, scripts read)

A traffic light is a placed device entity, `TrafficLight` (a `Device`;
pedestrian lights `CrossingLight`, its subclass), with the same static
`EntityID` in every game. Its phase is not the script's: the native
`TrafficLightListenerComponent` (RTTI `worldTrafficLightListenerComponent`,
0xb8, no script surface) queues `TrafficLightChangeEvent{lightColor}`
(`worldTrafficLightColor`: GREEN 0, RED 1, YELLOW 2, INVALID 3) on the
light, and `TrafficLight.OnTrafficLightChangeEvent` (final.reds :280206)
turns its lamps: off, then for YELLOW `CommenceLightChangeSequence` from the
colour it had (to red: yellow; to green: red and yellow), else the colour
(`CompleteLightChangeSequence`), kept in `m_lightState`. The NPCs' crossing
(`CrosswalkEvent`) and the traffic system's lanes read the native phase, not
the device; no seed or clock of the phase is exposed, so two games' crossings
run apart. A light given a colour by script the same way shows it until the
next event. `Lights.reds` (traffic.md, "Traffic lights"). [2.5-untested]

The walk markings painted on a crossing are a third kind, `TrafficZebra`
(a `TrafficLight`, controller `TrafficZebraController`, display name
"Gameplay-Devices-DisplayNames-Zebra"; final.reds :508381): it **overrides**
`OnTrafficLightChangeEvent` without calling the light's, keeps the colour in
`m_lightState` and shows it as its mesh appearance, `default` for GREEN and
`stop` for any other (`SetMeshAppearance`). A wrap of the light's handler
never sees a zebra. [2.5-static]

## 13.158 The crowd's spawners: managers, parked cars, the quest's switch (2026-11-23, game 2.50, exe read)

All read in the exe, nothing run. The community system's crowd is the
object at `gameCommunitySystem +0xD0` (`ChangeDensityModifier`, hash
509682613, writes its `+0x720`); its vtable (`0x14307DFF0`) has the
request functions at `+0x30` and `+0x38`. [2.5-static]

- **Crowd managers** at `+0x640`, inline, their count (u32) at `+0x650`
  (room for two). A manager's update (`0x140430858`) runs only while its
  byte `+0x248` and the config variable `Crowd/Enabled` (`0x143917610`) are
  set; it spawns up to its target (`+0x1D0`) less its count (`+0x1D4`),
  capped by `Crowd/SpawnLimit` (100, `0x140430E40`). Its enable
  (`0x140FA35F0`, hash 2088899922) zeroes its counters when turned off.
  [2.5-static]
- **The parking-space spawner** at `+0x670` (`0x1407D3D68`, called per
  parking space as it streams in): only while its byte `+0x9C` is set, for
  a space not already in its list (`+0x48`), outside the null areas
  (`0x1407D3F28`), with fewer than 50 cars, it rolls the crowd's parked
  probability times `Crowd/BaseParkedCarProbability` (`0x143C0DC70`) and
  spawns one. So a parked crowd car is rolled when its space streams in:
  one removed is rolled again on the next arrival. [2.5-static]
- **The quest's switch** (`questCrowdManagerNodeType_ControlCrowd`,
  `questControlCrowdAction` Disable/Enable, `distantCrowdOnly`): a request
  list on the crowd. Disable (`0x1421DBEC0`, hash 3834778093, `(crowd, key,
distantOnly)`) and enable (`0x142BD257C`, hash 2891845843, `(crowd, key)`)
  queue under the lock `+0x6F8` into `+0x6D8`; the community update
  (`0x140DC9E00`, hash 1426265232) moves them to the active keys (`+0x6C8`,
  count `+0x6D4`). From none to some: `0x1421DBF40` turns the distant crowd
  off (`+0x660` byte `+0x15`) and, unless every key is distant-only, deletes
  every parked car's stub (`0x14193936C` → DeleteStub: in sight too), clears
  `+0x9C`, turns every manager off and tells the traffic object (`+0x6A8`,
  virtual `+0x40`, "disable crowd"). From some to none: the enable
  (`0x142BD26CC`, hash 2843676965) sets every manager's `+0x248`, `+0x9C`
  and the distant crowd's `+0x15`. The active keys are written into the
  save (`gameCommunitySystem` slot 41, `0x140A5C5BC`, reads them back and
  disables again). [2.5-static]
- `Crowd/EnablePedestrians` and `Crowd/EnableVehicles` exist as config
  variables (`0x143917620`, `0x143917658`) and nothing in the exe reads them.
  [2.5-static]
- **tandem** (`engine/crowd.cpp`, feature `crowd spawners`): the game that
  does not run its street (deep inside a cover, `regions.md` §6) clears the
  bytes the enable sets, managers and parked spawner, every frame while so
  (a quest's enable may set them again), and calls the enable when it runs
  its street again unless a quest holds the crowd off. Not the request list:
  it deletes parked cars in sight and goes into the save. [unverified]

## 13.34 Traffic and police: open questions (need the game)

1. Does `ChangeDensityModifier(0.0)` remove the crowd's **parked** cars,
   and how fast do already spawned traffic cars leave? (Measured: density
   0 does not stop the traffic, `traffic.md`; the spawners' own bytes,
   §13.158.)
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
   hostile to? Another co-op mod reports units spawned off the
   spawning game's screen not seeing a remote V (the sensor stays where it
   spawned, the skeleton not being updated off screen, §13.33 "Police cars
   and the AV"): `Police.reds` injects the puppet as a threat into every
   unit it spawns, which needs no sight; whether they keep it is the test.
7. Does `AIReactionData.stimTarget` name the puppet when the stimulus was
   broadcast from it (`Stealth.reds` broadcasts a guest's noise from its
   puppet)?
8. Do a guest's hits on police proxies raise its own heat (the proxies'
   `IsPrevention()` and `IsCrowd()` on a dynamic entity)?
9. Does `RequestChaseVehicle` spawn a car when the script set the strategy
   pre-checks itself while the local V is not chased (§13.33 "Police cars
   and the AV")? Does the result list the crew with the car?
10. Does a fresh chase car teleported once to a point found with
    `NavGenAgentSize` 1 (with its crew seated) drive on from there, and
    does its `AIVehicleChaseCommand` on a puppet bring it to the puppet,
    its crew out within 15 m?
11. Does `RequestAVSpawnAtLocation` land the AV at a navmesh point 15–40 m
    from a puppet, and do its MaxTac come back as passengers or in the
    spawn result?
