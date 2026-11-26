# The simulated second game

`tandem-peer` as a second game that plays: a referee on every message it
hears, a host world with NPCs and a story, a simulated V with goals and a
persona, all from one seed, so a session can be soaked, fuzzed and covered
with one copy of the game, or with none (`peer/sim/`, the `tandem_sim`
library; tests in `tests/referee_test.cpp` and `tests/sim_test.cpp`).

<!-- prettier-ignore -->
| Piece | File | What |
| --- | --- | --- |
| referee | `referee.hpp` | every message received, checked against the protocol and the authority table |
| scene | `scene.hpp` | what one game knows of the session (players, the host's NPCs, items, call, hub), from the endpoint's events; `Deliver` hands a model's message to the endpoint call a game makes for it |
| world | `world.hpp` | the host's NPCs, time of day, weather and its answers to guests |
| story | `story.hpp` | a scripted quest, played by a director that waits for the guests |
| V | `player.hpp`, `motion.hpp` | the simulated player and how it moves |
| personas | `persona.hpp` | companion, loner, chaos, speedrunner, partner |
| game | `game.hpp` | one simulated game (endpoint, referee, models), `RunPair`, the command line |
| coverage | `coverage.hpp` | which message types crossed the wire, which feature paths were taken |

## The referee

`sim::Referee` watches an endpoint's wire (`Endpoint::SetWireTap`: every
message as it crosses, either way, before anything is filtered, stamped or
applied) from one side, host or guest. It keeps its own record of what was
said (roster, NPCs, holds, lives, seats, items, epochs, digests, requests
awaiting an answer) and reports each breach with its rule:

- `referee: [rule] text`, a violation: the other game did what the protocol
  forbids, whatever the network does. Scenarios reject these lines.
- `referee-hint: [rule] text`: legal on the wire but suspect (a burst, a
  state held very long). Counted, never a failure by itself.

It is on in every `tandem-peer` mode (findings logged as they come, a
`referee summary:` at the end), in every `sim` game, and under the whole
doctest suite (`tests/main.cpp`: every endpoint a test makes is watched
through `Endpoint::SetCreationHook`). There a violation fails the test,
unless the test breaks that rule on purpose and says so first,
`test::ExpectReferee({"ids.unknown-npc"})` (`tests/referee_expect.hpp`);
hints never fail. The findings are listed by test at the end
(`referee-suite:`); `TANDEM_REFEREE_SUITE=0` turns it off, `=1` also prints
examples.

Each message has its speaker in `referee.cpp` (`SaidBy`: host only, guest
only, or either, checked by its own rules); a message without one does not
compile, and `tests/referee_test.cpp` checks that the other side may not
say a host's or a guest's message.

<!-- prettier-ignore -->
| Rules | Checks |
| --- | --- |
| `authority.host-only`, `authority.guest-only` | a guest sending what only the host says (the session, the world, the story, scenes, the region arbitration: `Regions`, `NpcHandover`, an unpinned `NpcOwner`, digests), the host sending what only a guest asks (pings, noises, resync requests, `DeviceUse`…) |
| `authority.not-owner` | since per-player regions (`regions.md`) an NPC's owner speaks for it: a guest's spawn outside its range of ids (`regions::Announcer`), a guest's health, hold, hit or reply for an NPC another game runs (`NpcOwner` moves it), the host passing us a request, a hand-over or another's word on an NPC our game does not run, or on one it does; the same for world vehicles (`traffic.md`: `TrafficSpawn`, `TrafficStatus` from the vehicle's owner, `TrafficHit`, `TrafficClaim` to it) |
| `authority.not-shared` | an owner's word to us (`PlayerHit`, `PlayerHack`, `NpcCue`, `NpcShotAt`, `NpcAnims`) on an NPC that left our view (gone "away": a link broke, its region's edge, a resync's set) more than 1 s ago and has not come back (`regions.md`, "What travels where") |
| `authority.not-yours`, `authority.own-state-back`, `authority.misdelivered` | a guest speaking in another player's name (heat, presence, cues and vehicle hits too); the host sending us our own state; a hit, hack, revive, crime or answer meant for another player |
| `authority.acting-while-down`, `authority.revive-self`, `authority.revive-not-down` | firing, grabbing or uploading while down or out (after 1.5 s of grace); reviving oneself, or a V that is not down |
| `authority.vehicle-not-yours`, `authority.accepted-on-dead`, `authority.state-after-gone` (hint) | moving a car while riding in someone else's; the host accepting a takedown on an NPC it said dead; an NPC still in the host's states 3 s after it went |
| `ids.unknown-npc`, `ids.unknown-player`, `ids.unknown-item`, `ids.unknown-offer`, `ids.reused` | names never announced (an NPC, a player, a world item's stack, a regroup offer); an NPC id announced again as another NPC, or after it died or was disposed of (out of view and back, a link that broke and formed again, is the same NPC) |
| `oneshot.*` | the second welcome, a state sequence number repeated, a bag dropped twice, a death, a reward, a game over, a quest item or an NPC shot at us twice; one stack to two takers; the same hit byte for byte within 30 ms (hint); more health drops than our hits with nobody else fighting (hint) |
| `reply.unasked`, `roster.broken` | an answer (takedown reply, upload, pickup, pong, a turret's refusal) to nothing we asked; a roster without us or the host, with a player twice, or too long |
| `value.not-finite`, `value.range`, `value.clock` | NaN or infinite numbers; health, poise, damage, factors, tiers, phone actions out of range; a hit taken (`HitTaken`) off its V's body, by an unnamed attacker or a named nobody; flags no version knows (`NpcShotAt`'s, a cue's); a charge's step or level out of range; a state stamped more than 3 s from the host clock |
| `rate.over-budget`, `rate.events` (hint), `rate.bytes` (hint), `rate.digest-silent`, `size.over-budget`, `size.state-fragmented` (hint) | states, poses, cars, shots, NPC states, awareness, digests, pings over their budget, more resync requests than two per digest period (6 in 10 s at the game's 3 s; `Attach` reads the endpoint's timings, which tests shorten); a `PlayerState` over 64 bytes; an unreliable message over a datagram; no digest for six digest periods |
| `digest.diverged`, `digest.resync-loop` | our copy of a domain differing from the host's longer than two digests and a resync take; a guest asking for the same domain four times in a row (our resync does not heal it) |
| `epoch.backwards`, `epoch.unknown`, `epoch.unacknowledged` | an epoch going back; a guest naming a host epoch we never had; a guest that has not acknowledged our new world within 5 s |
| `breach.ungranted`, `oneshot.breach-twice` (Breach Protocol, `devices.md`) | the host passing on a breach it did not grant (no `kBreachApply`); one network granted to two players; also `authority.host-only` for a guest's granted breach or a `BreachResult`, `authority.not-owner` for an NPC's breach passed to a game that does not run it, `reply.unasked` for an answer to a breach we never made, `authority.not-yours` for ours granted in another's name |
| `authority.turret-taken` (`devices.md`, "A turret in a player's hands") | a guest refused a turret another player holds three times in a row (once a second while it claims): its game never let go; also `authority.host-only` for a guest's `TurretBusy`, `authority.own-state-back` for one naming us as the holder |
| `stuck.hold`, `stuck.after-leave`, `stuck.time-effect`, `stuck.revive`, `stuck.life` (hint), `stuck.call` (hint) | a takedown held 30 s; a hold, time bubble or revive of a player that left 2 s ago; slowed time for 3 min; a revive held 30 s; a V down 10 min; a call ringing 2 min |

What it found on the existing loopback tests: "Findings" below.

## The host's world

`sim::HostWorld` runs the NPCs around the host's own V (its region: they
go beyond 85 m of it, `regions::kLose`; the endpoint shares them with the
linked guests) with a small behaviour machine: idle, patrol,
alerted (a noise, a traced hack), combat, cover, staggered (guard broken,
stunned), flee (civilians), held, unconscious, dead. Each perceives every V
by distance, its visibility, crouching and camo (meter rising, detection,
`NpcCue` CombatStart to that player's game, losing them after 8 s unseen);
ranged NPCs keep their distance and fire in bursts (`NpcShotAt` to a guest,
whose game decides the hit; `NpcShot` for everyone), melee ones close in
(`PlayerHit`), netrunners hack guests (`PlayerHack`). It answers guests by
the real host's rules: strikes by `combat::StrikeLoss` (non-lethal knocks
out, health and poise, guard broken, wounds, a dead NPC ignores hits),
effects and uploads (one upload at a time per target, `NpcHackState`
started, done or failed), takedown requests with the refusals of
`Takedowns.reds` in their order (gone, held, too far, conditions, not
yours), slowed NPCs in time bubbles (`sync::TimeBubbles`), the world's time
and weather, the pause menu, quest items on corpses, bodies cleared after
40 s, NPCs out of its region despawned, a new wave a while after the last
fighter fell, everything rebuilt on a fast travel. It writes protocol
messages; the endpoint keeps the session's record and its digests from
them, and `HostWorld::Check` compares the world with that record every
second (`sim-check:` on a difference).

## The story

`sim::StoryDirector` plays a `.story` file (the `host --story` format of
`tools/scenarios/story-host.story`) or its own quest (every story message
once: snapshot, journal, tracked entry, a call, lines, a hub and its pick,
tiers, a story effect, a private scene, a cinematic, a reward, Johnny, a
braindance), and adds `await answer|suggest SECS` (the story's clock stops
until a guest answers the call or suggests an option), `cinematic SECS` and
`loop SECS`. Rewards go once, to the guests near the host
(`story::Participation`); a looped quest pays nothing again.

## The simulated V

`sim::SimV` picks goals by its persona's weights among those the scene
makes possible: follow, wander, fight (ranged or melee, headshots, a
finisher once the guard broke), hack (upload, then the effect when the
upload is done), Sandevistan, takedown (grab, takedown or knockout, carry,
throw, drop or dispose), drive, ride in the partner's car, fast travel
(epoch, then a regroup offer), regroup, metro, another world (guests), pause
menu, camo, drop and loot items, go down, hack the partner's car, a grenade,
dial a call (guests). It answers the session as its game would: hits hurt;
shots fired at it land by the AI's odds (`ShotHit`); at 0 health it goes
down when another V is up and out otherwise, down alone for 3 s is out, out
for 10 s respawns beside its partner; a helper revives a downed partner
(10 s within 2 m), answers the host's calls and suggests at hubs, picks up
what others drop, accepts regroup offers. As host, its requests on NPCs go
to its own world, with no message, as a host's V hits its own NPCs.

`sim::Motion` moves it: real play when there is some (`recordings/play/*.tplay`,
the game's own V), cut into on-foot clips of 3 s (no car, water, ladder or
workspot), each laid down where the V stands and turned toward its goal, the
next stitched where it ended, with the recorded movement flags and stances;
without recordings (or crouched), a plain walk at the game's speeds (1.5,
4, 6.5 m/s) with its flags.

## Personas

<!-- prettier-ignore -->
| Persona | Plays | On the session |
| --- | --- | --- |
| `companion` | near its partner (4 m), fights with it, rides with it, revives it | — |
| `loner` | 400 m away in its own region (as host, sharing the NPCs around itself), drives, fast travels | never comes to regroup offers, never revives |
| `chaos` | everything, twice as fast; hits right after a world is rebuilt, grabs what just died or someone else holds, lets go early | leaves and joins again every 45 s, half the time with no goodbye; 40 ms, 60 ms jitter, 15% loss, 5% reordered each way |
| `speedrunner` | fast travel, cars, the metro, Sandevistan | — |
| `partner` | as `companion`, but never goes down by itself (downed by an NPC and not revived within a minute, it is out, then up beside its partner), pauses, hacks a car, calls or goes elsewhere: the bot of the player's own sessions (`tools/play-session.sh`), so it never stops their game (a V down blocks waiting) | — |

## Running it

- `tandem-peer sim --role host|join [ADDRESS[:PORT]] --persona P --seed N
--minutes M [--story FILE] [--play-dir DIR] [--pace X] [--no-link]` (or
  `task sim -- …`): one simulated game against the real one. It logs its
  goals, the session, the referee's findings and its own checks, and at the
  end the coverage report and `sim summary: … N violation(s)`; it exits 1 on
  a violation or a failed check.
- `tandem-peer sim-fuzz --seeds K [--first N] [--seconds S]` (`task
sim:fuzz K=50`): K pairs of simulated games in one process over loopback,
  each seed its own pairing of personas; one coverage report for the lot;
  fails on any finding.
- `task test` runs pairs headless (`tests/sim_test.cpp`: companion and
  companion, companion and chaos, speedrunner and loner, at four times the
  pace and with short digests) and expects no violation and no failed
  check.
- Soak scenarios (`tools/scenarios/soak-guest.sh`, `soak-host.sh`;
  `PERSONA`, `SEED`, `MINUTES`): the real game against a simulated host or
  guest for ten minutes.

## Coverage

`sim::Coverage` counts every message type sent and received (from the
referees) and every feature path the models declare and take (`npc.*`,
`world.*`, `story.*`, `v.*`, `session.*`). Its report says what a run never
touched: message types that never crossed either way, feature paths never
taken.

## Findings

On the existing loopback tests (2026-11-19, the whole suite watched):

- **The host broadcast to every connection**, including ones that had not
  said Hello: a late joiner heard player 2's epoch before its Welcome and
  before the roster that brings player 2 (`ids.unknown-player`). Fixed:
  `Endpoint::Broadcast` on the host sends to the session's players only.
- What is left is the tests' own doing (they break rules on purpose),
  each now named by its test (`ExpectReferee`): NPCs never announced in the
  routing tests, forged pause names, answers to regroup offers never made,
  a revive of a V that is not down, a death sent twice, a third of the
  host's messages lost or doubled (`ids.unknown-npc` for healths whose
  spawn was lost, `oneshot.seq-repeated`, `digest.resync-loop`), an old
  owner speaking for an NPC it no longer runs.

On 2026-11-20, before violations failed the suite (45 in 16 tests): the
referee's `NpcSeat` rule took a shared world vehicle's seat
(`kSeatInTraffic`, `traffic.md`) for an unknown player (fixed); two tests
announced NPCs badly (an NPC respawned as another, a scene's actor never
spawned; fixed in the tests); the rest as above. No bug in `core/`.

With per-player regions merged, the referee learnt the owners' rules from
the regions tests (a receiver learns it owns an NPC from the hand-over's
state, an owner resends the spawns of NPCs it took over with its set,
`Noise` goes to every linked player, each owner numbers its own NPC
states, and an NPC can leave a guest's view and come back); what is left
there is the lossy hand-over test's lost and doubled messages. That test
failed one CI run in two on `rate.over-budget` (7 `ResyncRequest` in 10 s):
the budget was the game's (digests every 3 s) while the test's come every
100 ms; it now follows the endpoint's timings, as the digests' own does.

`sim-fuzz` after the merge caught the sim's own world fighting a guest its
NPCs were not shared with (spawned around the partner, far from the host's
V: `NpcShotAt`, `NpcCue` and the guest's `ShotHit` about NPCs the guest
never heard of): under regions a host's NPCs live in the host's region.
The endpoint itself sent `NpcShotAt`, `NpcCue` and `PlayerHit` to a guest
the NPC was not shared with (an NPC at its region's edge, a guest just past
the link distance); the guest dropped the shot (shooter unknown). Fixed on
the `regions-fixes` branch (only to a game that shows the NPC); the referee
calls a word on an NPC that left our view `authority.not-shared`, and no
longer calls the host's `NpcGone` "away" of one of ours (its record letting
go) `authority.not-owner`.

Sim against sim (`sim-fuzz`, 8 seeds of 30 s, every persona as host and
guest): no violation, no failed check; 59 of 72 message types sent, 87 of
134 feature paths taken (the report lists the rest: rewards come 37 s into
the default quest, leaves every 45 s).

`sim-fuzz` on 2026-11-22 (204 seeds of 30 s at `--pace 4`, the chaos
guest leaving and joining again under a new id), state kept across a leave:
a rejoined guest heard a bag dropped by its former self (`WorldItem`'s
dropper) and a call it had answered (`PhoneCall`'s `answeredBy`) as names
of a player never in its session (`ids.unknown-player`; also a claim told
the stack's taker who had left). Fixed: a leaver's bags lie there as
nobody's, a stack it took answers a claim as gone, a call it answered is
the host's. A leaver's holds are now released before its NPCs go to their
heirs or away (one it held and ran went with it in silence:
`stuck.after-leave`). The sim's own host world ignored the region
hand-overs (`sim-check: npc N is in the session's record and not in the
world` after one came back): it now gives, takes and lets go of NPCs
(`npc.given`, `npc.taken`).

## Open

- Regions: the simulated guest runs no NPCs (it answers no hand-over;
  `tandem-peer --mode region` does); its referee checks ownership both
  ways.
- A real host recorded (`.tsession`) as the world model's training data:
  the sim's NPCs keep the game's enums and rules, not its pacing.
- The pose stream: the simulated V sends no `PoseState`.
- Per-player regions: a loner's world is the host's, shared only where the
  host is.
