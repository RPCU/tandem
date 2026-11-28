# Police and wanted level

**Heat is per player.** Only the offender is wanted, as only the V that was
seen fills its own stealth meter. Your partner's stars are not yours;
helping them against the police earns you your own.

Engine facts and their tags are in
[`research/engine/traffic-police.md`](../research/engine/traffic-police.md)
§13.33. **Nothing here has run in the game yet**: the core, the mock player and
the tests are done and pass; the scripts compile; the scenarios at the end
are what to run first.

## The rule

| State                                       | Owner                             | Others                                                      |
| ------------------------------------------- | --------------------------------- | ----------------------------------------------------------- |
| a V's crime score, wanted level, star state | that player's game                | told (`Heat`): shown, and answered by the area's owner      |
| who committed a crime                       | the game that sees it happen      | the offender's game is told what it could not see (`Crime`) |
| police units, their cars, MaxTac            | the owner of the area they are in | proxies, like any NPC and any world vehicle                 |
| who a unit is hostile to                    | the owner of the area             | each V by its own heat                                      |
| whether the police see a V                  | the owner's NPCs (their senses)   | that V's game hears it (`NpcStates.detected`)               |

Why each game keeps its own prevention system for its own V: the game's
whole chase is a script system around "the player" (the score table by
district, the stars and their grey and blinking states, the timers, the
last known position, the save lock, the audio, the HUD). Run by each game
for its own V it is exactly the single-player system, with nothing to
re-implement and nothing to keep in step. What cannot stay per game is the
bodies: police are NPCs of the world, and the world around a V has one
owner.

"The owner of the area" is the same abstraction as for traffic
([`traffic.md`](traffic.md), "Who owns an area"): the area owner of
per-player regions, `TandemArea.Owns()` / `Endpoint::OwnsArea()`. A player
whose own game owns the area around it (the host, any player alone in its
region) is chased by the unmodified game. The rest of this page is about a
wanted V in an area another game owns: today, a wanted guest.

## Crimes: the game's own checks, seeing the right player

Vanilla attributes every crime to "the player" by asking `IsPlayer()` of
whoever did it, or by taking the local V outright (research §13.33, "How a crime is attributed"). In
co-op each game has one real player and the others' puppets, which are
not players. So:

- **What a V does on its own screen** is seen by its own game, against
  proxies that are real entities of the same records: shooting a
  civilian's proxy, hitting an officer's proxy, blowing up a proxy car
  with people in it. Its own damage system has its V as instigator and
  its own prevention system scores it. Nothing is added.
- **The same act on the owner's game** arrives as the guest's hit, applied
  with the guest's **puppet** as instigator: not a player. The owner's V
  is not blamed, and nothing is added.
- **What only the owner's game can see** is what its NPCs do about a
  puppet, and that is where vanilla names nobody and blames the local V:
  - a witness calling the police (`CallPolice`, an AI task: "the police
    are called on the player");
  - a prevention turret opening on its target.

  Both are wrapped on the owner. The culprit is read from the game itself
  (the stimulus the witness reacts to keeps its source,
  `AIReactionData.stimTarget`; the turret's target): when it is a remote
  V's puppet, the owner's game does **not** run the vanilla request, and
  sends `Crime{player, kind, npc, position}` to that player's game alone.
  There the same vanilla request is queued on that game's own prevention
  system (`CrimeWitnessRequestToPreventionSystem`,
  `CombatStartedRequestToPreventionSystem`, `CreateNewDamageRequest`), as
  if its own NPCs had seen its own V. When the culprit is the owner's V,
  or nobody we can name, vanilla runs untouched.

`Crime` has kinds for damage and a kill as well: unused today (the
offender's game sees its own hits), kept for the cases found in game where
it does not (a proxy that does not count as a civilian there, research §13.34 question 8).

## Being wanted in an area another game owns

1. **The stars are the offender's.** Its prevention system raises the
   stage, shows the stars, runs the timers. Its game says so:
   `Heat{player, stage, state}` (stage 0..5, the star state: seen, lost,
   about to give up), reliable, kept by the host for late joiners, in the
   players digest.
2. **It spawns nothing.** A game that follows keeps its prevention system
   and loses the four calls that reach the native spawn system (units,
   chase vehicles, roadblocks, the MaxTac AV: `Police.reds` wraps them
   while `TandemPolice.Follows`). Its own police would be NPCs of a world
   it does not run.
3. **The owner answers the heat.** For each remote V with a stage above 0
   in its area, the owner's game sends units after that V's puppet: a few
   at a time, up to stage + 1 (6 at five stars), at points the game's own
   spawn system finds around the puppet (`FindPursuitPointsRangeAsync`
   takes its origins as arguments), spawned by the game's own
   `RequestUnitSpawn`, drawn by weight from the district's pool for that
   stage at its spawn range (the prevention system's preset,
   `PreventionData.NCPD` before it has one; the script's failsafe records
   `Character.prevention_unit_tier1/2/3` do not exist, §13.33). They are
   the owner's NPCs: shared with everyone
   as proxies, shot at and killed like any NPC, and they fire at the puppet
   through the existing path (`NpcShotAt`: the target's game simulates the
   shot). A car's crew counts among them: units on foot fill in for the
   cars that could not come.
4. **Cars from two stars** (one, two from four), **on by default** since
   2026-11-21: a car chased the guest's puppet in game (`police-host-cars`:
   "car … chasing player 6 (2 aboard)") with no crash. `police cars on|off`
   stays as a test command, the AV of item 5 behind the same switch. (Off
   for a day: asking roads for three strategies of seven crashed a job
   thread, police-host; all seven now, as the game does:
   [`§13.102`](../research/engine/traffic-police.md).)
   The owner finds a place on
   the roads around the puppet (`FindPursuitPointsRangeAsync` with
   `NavGenAgentSize` 1, vehicle, at the level's vehicle spawn range; none
   there, 2 s later at 40–120 m, then at 40–120 m with size 0, a person's,
   whose navmesh has the streets too: drake-ave's 150–250 m found 0,
   2026-11-21) and
   takes the first one more than 60° off where that V looks
   (`TandemNative.RemoteYaw`). It asks the game's own chase vehicle with
   its crew seated (`RequestChaseVehicle`: a car from the level's pool, its
   own passengers, a `GetToPlayerFromAnywhere`, `SearchFromAnywhere` or
   `DriveTowardsPlayer` strategy, whichever the spawn system has a road
   for), moves it to that place facing the puppet, and sends it after the
   puppet: `AIVehicleChaseCommand` on the puppet as an event and as its
   init command, no limit on chasing vehicles, driving stats boosted. It is
   given the chase again while its driver is in and it has lost it. Its
   crew are units for that player (below), and the game's own behaviour
   gets them out within 15 m of the puppet. The spawn system only has
   roads while someone asks it to check its strategies, and the prevention
   system asks only while the owner's own V is chased: while a remote V
   wants cars and ours is not chased, `Police.reds` asks
   (`SetStrategyPreCheckRequests`, from that V's level) and clears them
   when nobody it answers is wanted. No place out of sight, no road, or a
   failed spawn: on foot only, another try after the cooldown (the level's
   `VehicleSpawnCooldown`, 15 s by default). The car is a world vehicle
   like any (`Traffic.reds` shares it, police flag and siren).
5. **The MaxTac AV at five stars**, one for each wanted V while it stays at
   five: `RequestAVSpawnAtLocation` (a first-wave record,
   `Vehicle.max_tac_av1/2/3`) at a point 15–40 m from the puppet out of its
   sight (or the first found, or the puppet's own position). Its MaxTac are
   units for that player. The game's own `RequestAVSpawnAtPosition` is not
   used: it also stops our V's car, and the follower's copy of it is the one
   `Police.reds` blocks (a follower spawns nothing).
6. **Hostile to that V alone.** A unit sent for a player (on foot, a
   crew, MaxTac) is made hostile to that player's puppet and given it as a
   threat: a unit spawned off our screen may not see the puppet (research
   §13.34 question 6), a threat needs no sight. The police around a
   puppet look at it by its own stage (`TandemPolice.Face`: hostile while
   wanted, neutral and forgotten again at 0), and never by the owner V's:
   `Attitudes.reds`, which copies V's relations onto the puppets, leaves
   the police out. **They spare the owner's own V** while it is not wanted
   and has not hit them: vanilla gives a fighting NPC whoever tops its
   threats as its target and turns it hostile to that target
   (`SetTopThreatToCombatTarget.SetCombatTarget`), and its help-an-ally and
   hostile-stimulus paths turn it on whoever is near
   (`TryChangingAttitudeToHostile`), so units sent for a guest's puppet
   took the host's own V on and killed it (police-host-cars, 2026-11-21).
   Each unit, and each officer `Face` turns on a puppet, carries a flag
   (`NPCPuppet.m_tandemSparesV`); while the owner's V is not wanted
   (`TandemPolice.OursWanted`), `Threats.reds` takes that V off its threats
   (base −1, as vanilla does under `BlockTargetingPlayer`) and refuses it as
   its combat target, and every tick `TandemPolice.Spare` sets it neutral
   to that V and drops that V from its threats and its squad's. The
   vanilla provocation rule ends it: the owner's V hitting one of them
   (`GetReceivedDamageByPlayerLastTimeStamp` after it was enlisted) makes
   that one hostile to it, a threat, and no longer spared; the owner's V's
   own heat (from that hit or anything else) gives them all to vanilla's
   rules, the unit's attitude back to its group's. A game that answers the
   host's heat (a guest owning its area) spares its own V the same way. In
   the other direction nothing is needed: vanilla makes the police hostile
   to the local player's attitude group, which no puppet is in.
7. **Their chase is not the owner's heat** (not yet run in game). Vanilla
   knows one culprit, "the player", and some of what it hears around a
   chase names nobody: a witness of the fight calling it in (`CallPolice`
   and the native `CrowdCallingPoliceEvent` name the local V), a prevention
   turret opening on whatever it follows, the native stolen-vehicle
   request. Each gives a V at no star its first
   (`OnCrimeWitnessRequest`, `OnCombatStartedRequest`,
   `OnVehicleStolenRequest`): the host's V, which did nothing, got "our
   wanted level 1" 2.4 s after MaxTac landed for a guest's puppet 3 m from
   it, as it entered combat (police-host-cars, 2026-11-21; that log does
   not say which input). While a game answers another player's heat
   (`TandemPolice.Answering`: one wanted in its area or within 10 s after,
   or units or cars still out) and its own V has no star, a first star
   needs that V to have done something (`PreventionSystem.TandemNotOurs`):
   an input vanilla attributes by the hit's instigator (damage, a kill, an
   officer hit, a car it blew up), a `Crime` from the area's owner, or
   damage it dealt in the last 10 s
   (`PlayerPuppet.GetLastDamageInflictedTime`). The three handlers above
   drop the input otherwise, and `ChangeHeatStage`, the one place the
   level changes, refuses whatever came another way ("police: heat refused
   for our V (REASON)"). A quest's level (`SetWantedLevel`, `police heat`)
   always counts; with a star, vanilla's rules. Every first star in a
   session is logged with vanilla's reason ("police: our V gets heat
   (REASON)"). Not counted while answering: a police car our V only
   quickhacks, shots it fires that hit nobody.
8. **Seen or lost.** Whether the police see the wanted V is the owner's
   NPCs' business (their senses, the puppet's stealth). The offender's
   game already receives which NPCs have detected its V
   (`NpcStates.detected`): for each officer among them it registers the
   proxy as a viewer with its own prevention system, and unregisters it
   when the officer loses sight (`TandemPolice.Watch`), by the body it
   registered (a proxy handed over to us is no proxy to look up any more).
   When its own game runs its area again (the other owner went, it holds
   its region) and when the session ends, every one it registered is
   unregistered at once (`Unwatch`: Watch stops running then; a proxy
   still standing there stayed a viewer, its place a spawn origin of our
   own police from then). While any of them sees our V, the prevention
   system's "police in combat with the player"
   (`PoliceAgentRegistry.IsPoliceInCombatWithPalyer`, wrapped) answers yes:
   its own star logic keeps the stars active and the last known position
   on our V (research §13.170); the
   proxies are in no agent registry there, so before the stars went grey
   the moment they came and stayed grey under their fire. They go grey,
   searching and lost by the game's own rules once nobody sees our V.
   `police report` ends with how many of the prevention system's own
   viewers are proxies (`TandemProxyViewers`: only Watch puts those there).
9. **It ends** when the offender's stage is 0, or when it leaves or
   vanishes: the owner's units for that player stand down the way the
   game's do (the threat dropped, neutral, relaxed, the game's own "stars
   dropped" effect), walk off, and are taken away by the spawn system after
   20 s. Its cars stop what they were told and drive off as the game's own
   do (`InterruptAllActionAndCommands`, `AIVehiclePanicCommand`), and are
   taken away with whoever is still aboard after 20 s
   (`RequestDespawnVehicleAndPassengers`); the AV too.

What a wanted guest does not get today, and why: roadblocks are placed by
the native spawn system around "the player" (the road ahead of it, the
intersection in front), which is the owner's V. With per-player regions a
player alone in its region owns it and gets the whole chase; the gap is
only for a wanted guest standing in someone else's area.

## The cases the user asked about

- **Near a wanted partner**: you are not wanted. Their units are hostile
  to them alone; yours are not out.
- **Stray fire** follows vanilla on both ends. An officer's bullet that
  hits you hurts you (the shot is simulated on the target's game either
  way). Your bullet that hits an officer, a civilian or a police car is
  your own game's hit on a proxy: scored by your own prevention system by
  the game's own thresholds (1.5 % of the victim's health, the per-officer
  "first attack", the score table). One stray round at a cop is as
  forgiven, or not, as alone.
- **Helping your partner**: attack the police and the heat is yours, with
  your own stars, your own units from the owner, your own timers. Two
  wanted players in one street are two chases that happen to share a
  street.
- **A crime a guest commits in front of the host's NPCs** gives heat to
  the guest (`Crime`), never to the host's V.
- **Fleeing in a car** is fleeing: the offender's own game decides it has
  lost them (the distance from its last known position, nobody seeing it).

## What travels

Appended to the protocol; no existing message changed.

| Message | Channel         | From → to                      | What                                              |
| ------- | --------------- | ------------------------------ | ------------------------------------------------- |
| `Heat`  | Event           | each game → host → all (kept)  | that V's wanted level 0..5 and star state         |
| `Crime` | Event, one-shot | area owner → the offender only | a crime of that V only the owner's game could see |

Nothing else: the units are NPCs (`NpcSpawn`…), their cars and the AV are
world vehicles (`TrafficSpawn` with the police flag), their sight is
`NpcStates.detected`.

## Consistency

| How it diverges                                         | What the player would see                     | Guard                                                                                                      |
| ------------------------------------------------------- | --------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| a late joiner                                           | no stars on a wanted partner; no units for it | the host keeps every level above 0 and sends it with the players domain                                    |
| a `Heat` lost (a bug)                                   | police that never stand down, or never come   | levels are in the players digest; a resync says who is wanted and, explicitly, who is not                  |
| a `Heat` sent twice                                     | —                                             | the same level and state again changes nothing, on either end                                              |
| a wanted player leaves, or vanishes mid-chase           | units hunting nobody                          | the host forgets its level on leaving or on the 5 s timeout and says stage 0 to everyone; units stand down |
| a wanted player's game crashes and comes back           | stars it no longer has                        | a returning player starts clean until its game says otherwise                                              |
| the owner vanishes                                      | the police frozen                             | the session ends; the proxies go; the offender's own game spawns its own police again                      |
| a `Crime` sent twice, or queued behind a loading screen | two stars for one crime; stars on arrival     | a one-shot (never duplicated by the fault layer); dropped after 3 s                                        |
| a `Crime` for a player that left                        | —                                             | no connection to send it to: dropped                                                                       |
| the offender fast travels                               | units left behind                             | its stage drops by its own game's rules (far from the last known position); they stand down                |
| a save made while following                             | prevention off for good in single player      | nothing persistent is written: the spawn calls are skipped while following, no lock is put in the save     |

## Stages

Built in this change:

1. **Protocol and session**: `Heat`, `Crime`, the host's record, late
   joiners, leaving, returning, the digest and resync. Tests:
   `tests/police_test.cpp` (who hears whose stars, a late joiner, a crime
   told to the offender alone, a wanted player that vanishes or comes
   back, a lost change healed by the digests).
2. **Mock player**: `--heat N [--heat-at S] [--heat-for S]` (either role),
   `host --crime-at S`.
3. **Plugin**: `TandemStreet.SendHeat`, `HeatStage`, `HeatState`,
   `SendCrime`; crimes reach the scripts with the street's events.
4. **Scripts**: `Police.reds` (our own level sent; following: no spawns,
   the owner's police as viewers; owner: units for a remote V, attitudes
   by each V's own heat, standing down; crimes attributed), the police
   left out of `Attitudes.reds`'s copying.
5. **Scenarios**: `police-guest`, `police-host`, `police-host-cars`,
   `police-spare-host`.
6. **Cars and the AV** for a remote V (above, 4 and 5): scripts only,
   compiled, not run.

7. **The partner's stars** (`Markers.reds`, `PartyHud.reds`; not yet
   run in game): from its `Heat` (`TandemStreet.HeatStage`, `HeatState`).
   Its marker (world, minimap, map) flashes the police lights' red and
   blue while the police see it, its colour and grey while they search;
   the map's tooltip says "Wanted by the NCPD: N of 5 stars" (out of their
   sight while they search). Its party entry has its stars beside the
   name, in the game's own art: the atlas of the game's wanted bar's first
   star (`WantedBarGameController`, `StarController`'s `star_active`, read
   through Codeware's `inkImage.textureAtlas`), red, grey while searched,
   fading in and out when about to be dropped. The wanted bar not found on
   the HUD: the level is written in the entry ("Wanted 3/5"), registered
   as the `wanted stars` fallback. Scenario `police-partner-stars`.

Not built:

- Roadblocks for a wanted guest in another game's area (above).
- Security areas and their own alarm (a guest trespassing in a host's
  restricted area) are not looked at here.

## Scenarios to run in game

- **police-guest**: the game joins a mock host. The host reports a
  witnessed crime of our V: one star on our own HUD, said to the others,
  gone again when asked to calm. Then the mock host is wanted at three
  stars: our log hears it, our own level stays 0. Look for: our stars and
  their sound, no police of our own appearing.
- **police-host**: the game hosts; the mock guest is wanted at two stars.
  Checks that our own level stays 0, that units are asked for and come
  out for that player, that they stand down when its stars go, and again
  when it vanishes mid-chase. Look for: where the units appear, that they
  go for the puppet and leave our V alone, how they walk off.

- **police-spare-host**: the game hosts on drake-ave; the mock guest,
  12 m off our V (mirror with an offset), is wanted at three stars. Checks
  that units come for it, that none is on our V (`police report`: "0 on
  our V"), that our V's health never dips (`test query health low`), and
  that they stand down with the guest's stars.

- **police-host-cars**: the game hosts; the mock guest is wanted at three
  stars, then at five. Checks that a place for a car is found around the
  puppet, that the car comes and chases it, that it stands down and is
  taken away when the stars go, then that the MaxTac AV comes at five.
  Needs roads near V (`POLICE_AT="X Y Z"` teleports there first); without
  them the log says "no road" or "no place for a car" and the scenario
  fails at the car. Look for: where the car appears (out of the puppet's
  sight), that it drives to the puppet and its crew get out, that our V is
  left alone (its health never dips, `test query health low`; the puppet
  stands 3 m in front of it, so a stray round can fail this), that it
  gets no star of its own (`test query wanted` stays 0; a "police: heat
  refused for our V (REASON)" line names the input that would have given
  one), the AV's landing.

- **pair-police** (two real games, `two-instances.md`, "Scenarios on two
  games"; not yet run): the partner, a guest 12 m off our V at drake-ave,
  is wanted at two stars by its own game (`police heat 2`). Checks that it
  spawns nothing (`we follow`), that our units and a car come for its
  puppet, none on our V (`0 on our V`, our health never dips, no star of
  ours), and that they stand down with its stars.

- **pair-police-area** (two real games; not yet run): the same, until our
  officers' proxies are viewers in the partner's prevention system and its
  stars are active (its `police report`: state 1, not the grey 2); the
  partner then holds its region (`region hold on`)
  beside us, its game running its area: none of those proxies may stay
  among its viewers ("0 proxy viewer(s)", read from its prevention
  system).

By hand, two games: a guest shoots a civilian near the host (stars on the
guest only); the host joins the fight against the police (its own stars);
a civilian sees the guest draw and calls it in (the guest's star, not the
host's).
