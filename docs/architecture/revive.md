# Down, revive, respawn

## Inside the plugin

- **Down, revive, respawn** (`scripts/tandem/Revive.reds`, milestone 8;
  the rules are under Open problems, "Down and out"). Each game owns its
  own V's life and sends it as `Vitals` (up, down, out, and whether it
  fights; reliable, stamped and kept by the host for late joiners). In a
  session with another player our V is Immortal through a god-mode
  override of our own, the way vanilla keeps a V with Second Heart alive
  (`SecondHeartStatListener`, `EnableOverride`), so its health stops at the
  floor and the player state machine enters its death state there
  (`DeathDecisions.EnterCondition` wrap, as for Second Heart at 1.1 %): V
  collapses with the game's own death animation, camera and sounds, its
  weapons away. Where vanilla picks resurrection or the death screen
  (`HighLevelTransition.SetIsResurrectionAllowedBasedOnState` wrap),
  Second Heart keeps its own rule (it revives at 20 % in a session, the
  vanilla multiplayer branch's amount); failing it, V goes down (or out),
  resurrection allowed but held (`DeathDecisionsWithResurrection.ToResurrect`
  wrap) and no death screen. While down it is invulnerable
  (`BaseStatusEffect.PlayerInvulnerable`), invisible to NPC senses (its
  visible object off, a dead body to their senses) and dropped from their
  trackers. Getting up is the game's Second Heart stand-up
  (`ResurrectEvents`), then `PlayerPuppet.OnResurrected` (wrapped) sets 40 %
  health, with 3 s of invulnerability. On the others' screens a downed
  puppet is outlined through walls (vanilla multiplayer's look,
  `ForceVisionAppearance`) with a notice; within 2 m the game's own
  interaction prompt (`UIInteractions.InteractionChoiceHub`, "[Hold F]
  Revive") appears; holding `Choice1` for 10 s with the HUD progress bar
  (`UI_HUDProgressBar`) sends `Revive` (start, cancel, done) to the downed
  V's game through the host, which drops it unless the target is down; the
  downed game shows "NAME is reviving you" on the same bar and accepts a
  done from a reviver within 4 m. Moving past 2.5 m, letting go, being hit
  or getting in a car cancels. Out: it gets up with the nearest V that is
  up (where: below) once that V has been out of combat for 3 s, or after
  30 s.
  The host arbitrates the shared game over (`GameOver` when no V in the
  session is up; once per loss): the host's V dies the game's way (death
  screen, checkpoint reload); a guest's lies waiting and gets up beside the
  host's V once back in the host's session. **Leaving is no way out of
  being down** (decided 2026-11-18): leaving is never refused (a player can
  always kill the game), but a V that is down or out when it leaves (the
  menu's Leave, hosting or joining elsewhere) or when its session ends (the
  host quit or vanished, we were cut off) dies the game's normal way: death
  screen, the last save. With nobody else left in the session a downed V
  dies after 3 s (no V is up); after a lost session a guest gives the
  host's reload 120 s. No save is made while our V is down or out
  (`TandemSystem.IsSavingLocked`), so a game killed there reloads from
  before the fight like any death. Several players may revive one V at
  once: its bar shows the first, then the next when that one stops. A
  reviver that vanishes takes its revive with it (the bar goes to the next,
  or away); a downed player that vanishes is removed with the
  usual "NAME left the session". **Up is one state** (fixed 2026-11-19,
  after a run left the screen dark): getting up (a revive, a respawn, the
  game's stand-up after Second Heart) clears in one place everything of
  being down, out or dying: the life sent to the others, the hold on the
  state machine, a pending death after leaving (`TandemSystem.LeftDown`,
  which also locks saving), the HUD bar, NPC senses, the death state's
  `dying` screen effect; 2 s later the state machine must be past its
  death state (`Vitals` 0), else its death effects are ended by hand. The
  leave-while-down death looks at the state at that moment and does
  nothing for a V that is up or being stood up; the god-mode floor stays
  on while V is being stood up, so a partner leaving then takes nothing
  from under it. Loopback test: down, out, up, the partner leaves (no
  loss), a partner joins (finds it up). `tools/game.sh cmd
revive down|hold|fresh|report` (`fresh` forgets the 5-minute cooldown of
  an earlier test and stands a downed V up); scenarios `revive-peer`,
  `revive-game`, `revive-out` (also: the mock player leaving an up V
  changes nothing), `revive-lost`. **Not yet verified in game.**
- **Down in a car** (2026-11-21; engine facts §13.20). Vanilla's V dies in
  its seat (the death state runs beside the vehicle state machine, with a
  vehicle death camera; the one death exit is a bike's, for a V that is
  dead, which a downed V is not), so a downed V, driver or passenger,
  **stays seated** in the game's own death pose; its `Seat` is unchanged,
  so the others' puppet stays in the seat, outlined as downed. At the
  wheel the car is braked to a stop (`ForceBrakesUntilStoppedOrFor`, 20 s).
  The reviver revives **from beside the car**, on foot, with the same
  prompt: within 2.5 m of the seated body (2 m on foot) and facing it
  (the camera within 60°, the door one faces), the car's own door prompt
  on the same key held off meanwhile (`GameplayRestriction.
VehicleNoInteraction`); it lets go past 3 m. **While the car moves**
  (the downed V's speed over 1 m/s, from its position, on both games) the
  prompt does not show and a hold already running waits, its bar saying
  so; the downed V's game, which decides, keeps a done that comes while
  its car moves and takes it once the car has stopped (the reviver still
  within 4 m, no cancel since, 30 s at most). The stand-up is the game's
  in the seat. **A wrecked, upside-down or sunken car** (`IsDestroyed`,
  `IsVehicleUpsideDown`, water over the car), once below 2 m/s: the
  downed V is put out the game's way (the forced exit's instant unmount,
  `TandemVehicles.PutOut`) and placed where the game would set it down
  (`CanUnmount`; else the navmesh beside the car), lying there.
  Not on the NCART, nor carried along (`transit.md`).
- **Down in deep water** (2026-11-21; the user's call the same day: no
  lift to a shore or the surface). A V whose head is under water, or whose
  water has its floor more than 1.8 m under the surface (a static
  raycast), or that is down in a sunken car, is **out at once** (no revive
  there) and gets up **at Viktor's clinic** (below), while another V is up
  and leaving is allowed (else near: "No far respawn", below); with none
  up the session's loss decides. A V at its floor while swimming
  enters the death state as on land (`SwimmingDecisions.ToDeath` wrapped:
  vanilla leaves swimming for death only at the health minimum).
- **At Viktor's** (2026-11-21; engine facts §13.110): V gets up on the
  clinic's floor in Little China, facing the ripper's chair, "Vik patched
  you up". The jump is a fast travel's (Travel.reds `TravelToMarker`'s
  steps: the fast travel's loading screen asked for, the teleport,
  `NotifyFastTravelStart`, the epoch moved on), out of a seat first; V is
  stood up once that loading screen has ended and the world has run 1.5 s
  (30 s at most). Before Vik is met (`q001_ripperdoc_done`) or, during
  Watson's lockdown (`q101_done` not set), with our V outside Watson: our
  fast travel point nearest to where V lay (its marker, as the fast travel
  does), "Patched up, back on your feet at POINT"; none found: it waits.
  The partners' party HUD and markers follow the V's new position.
- **Where a respawn puts you** (2026-11-21; was 1.5 m behind the partner's
  position of the moment, its yaw, wherever that was: in a car, in a wall,
  mid-air). The out V's game decides, from what it sees of the partner:
  (1) the partner sits in a vehicle here (driving or riding, its copy, a
  third player's, ours): a free seat of it, never the driver's
  (`TandemVehicles.FreeSlot`): our V gets up where it lies and is seated
  0.5 s after (the ride's own seating; already in that car: up in its
  seat); (2) else the partner's **last spot on foot** fit to get up at,
  kept twice a second: up, on the ground (`RemotePlacement` 0: no swim,
  climb, ladder or air), no seat, no ride, in our city
  (`TandemElsewhere`), on our navmesh within 0.5 m (an interior our world
  lacks has none here) and dry; there, 1.5 m behind its facing when that
  is on the navmesh, dry and with no static wall between, else its right,
  left or front, else the spot itself; (3) a partner in a car with no free
  seat and no spot on foot: beside the car once it is below 2 m/s, when
  that is dry navmesh and the car is not under water; a moving car gets
  10 s to stop; (4) else (no navmesh at its spot, elsewhere, a sunken car,
  no ground beside it, still moving) **at Viktor's**, not waiting. A V
  seated elsewhere is put out of its seat first. `tools/game.sh cmd revive
out` (out at once), `revive water N|off` (as if N m under water),
  `revive wreck` (its car taken as wrecked), `revive unseat`, `revive
nospot [off]` (no spot beside a partner fits), `revive novik [off]`
  (Viktor's out of the story's reach); scenarios `revive-car`,
  `revive-wreck`, `revive-water` (up at Viktor's), `respawn-car`,
  `respawn-foot`, `respawn-viktor` (no fit spot), `respawn-point` (Vik out
  of reach). **None of this has run in game**; `revive-water` uses a
  pretend depth (no real water spot is known to the scenarios).
- **No far respawn out of a locked quest area** (2026-11-21, the user's
  call: a jump to Viktor's from Konpeki Plaza or a Phantom Liberty
  mission would leave no way back). A far respawn (Viktor's, a fast
  travel point) only when the game itself would let that V fast travel
  now. Our own game's test (`TandemRevive.TravelLock`): no
  `BlockFastTravel` status effect on V
  (`FastTravelSystem.RegisterFastTravelPoint`) and no fast travel lock
  (`IsFastTravelEnabled`) other than `InCombat`, `InVehicle` and
  `PreventionSystem`, which pass with the moment
  (`FastTravelSystem.TandemQuestLock`, ours). Not
  `IsFastTravelEnabledOnMap`: its flag is the fast travel menu open at a
  terminal (engine facts §13.132). The host and a player alone use their own game's; a guest's
  own story is frozen (`story.md`), so the quest is the host's: the host
  sends `kStoryNoFastTravel` (128) in its `StoryState` while its own test
  says blocked, and the guest honours that and its own game's locks.
  Refused, the V never leaves the area: (1)-(3) above as usual, with 10 s
  for a fit spot beside the partner (as for a moving car), then up **where
  it lies** (out of its seat beside the car; under water, at the surface
  over it), with "No way out of here: back on your feet where you fell".
  Deep water or a sunken car: out at once as usual, then up beside a
  partner (MaybeRespawn), else at the water's surface over where it lay
  (beside the sunken car). The log says `revive: far respawn refused: fast
travel blocked (host's quest|ours)`; `revive report` says whether
  leaving is allowed. `tools/game.sh cmd revive locked [off]`: our game's
  fast travel taken as blocked. Scenario `respawn-locked` (as a guest:
  the mock host's `--no-fast-travel`, `revive nospot`). **Not yet run in
  game.**

## Open problems

- **Down and out** (decided by the user, 2026-11-18; milestone 8). (1) A V
  that would die while another V is up (not down) goes **down** instead:
  on the ground in the game's own downed or dying look and sound, unable
  to fight; death prevention (Second Heart, Bloodswell, health floors)
  resolves first by its own rules. There is **no bleed-out**: a downed V
  waits as long as it takes. (2) Another player **revives** it through
  the game's interaction prompt on the downed V's puppet, held for **10 s**
  within **2 m**; moving away or being interrupted cancels; the revived V
  gets up with 40 % health. (3) **Cooldown, per player: 5 minutes.** A V
  that goes down again within 5 minutes of being revived cannot be
  revived: it is out. (4) A V that is out while another is up **respawns
  beside the living partner** once out of combat (or after a short timer
  if the fight drags on), with no death screen, no reload and no death
  penalty; until then it watches from where it lies. (5) The session is
  **lost only when every V is down or out at once**: then the normal death
  runs (the host's game shows the death screen and reloads its
  checkpoint; guests follow the host's reload). Done (2026-11-18, not yet
  verified in game; see Inside the plugin, authority included): all five,
  with loopback tests (`tests/endpoint_test.cpp`) and scenarios. Choices
  made for the user's open points (accepted 2026-11-19): 40 % on a revive
  or respawn; Second Heart at 20 % in a session (research/coop-perks.md §5.5); out also when
  killed outright (`BaseStatusEffect.ForceKill`); respawn 3 s after the
  partner leaves combat, or after 30 s out; the last V up leaving the
  session loses it for whoever is left down (host) or leaves the guest's
  V down until it is alone for 30 s. Open: the puppet
  of a downed V shows what the owner's third-person body does in its death
  state (pose stream: to check; a downed animation on the puppet is the
  fallback); in a car (built 2026-11-21, above): whether the stand-up and
  the forced exit play right from the death state in a seat; whether the
  fast travel's jump and loading screen play right from the death state
  (Viktor's); the reviver's own kneeling animation;
  localized prompt texts; whether `BaseStatusEffect.SecondHeart` (applied
  by the stand-up we reuse) starts the Second Heart cooldown of a V that
  has the cyberware.
