# Per-player regions: who runs which NPC

The user's rule: _"you need to be able to handle if the 2 players are from 2
different position on the map, they should be able to play on their own
wherever on the map too, but the game should be coherent that they live in
the same world."_

Until this change the host's game ran every shared NPC, and only within 60 m
of the host's V. A guest's own crowd was switched off for the whole session,
so a guest away from the host walked an empty city. This document is the
design that replaces it. Research and engine facts are in
[`../research/coop-regions.md`](../research/coop-regions.md) (desk study)
and [`../research/engine/regions.md`](../research/engine/regions.md) (what
this change relies on, with tags).

**State: built and tested without the game** (core, loopback tests, the mock
player, script compile checks). Nothing here has run in Cyberpunk yet: the
scenarios at the end are the in-game checks to run.

## Contents

1. [The rules in one page](#1-the-rules-in-one-page)
2. [Regions and links](#2-regions-and-links)
3. [Who owns an NPC](#3-who-owns-an-npc)
4. [Hand-over](#4-hand-over)
5. [What travels where](#5-what-travels-where)
6. [Crowd density](#6-crowd-density)
7. [Late joiners, vanishing owners, rebuilt worlds](#7-late-joiners-vanishing-owners-rebuilt-worlds)
8. [Consistency](#8-consistency)
9. [What stays host-only](#9-what-stays-host-only)
   - [Gigs: grouped or apart, and holding a region](#gigs-grouped-or-apart-and-holding-a-region)
10. [Edge cases](#10-edge-cases)
11. [Where the code is](#11-where-the-code-is)
12. [Testing](#12-testing)
13. [Open points and risks](#13-open-points-and-risks)
14. [The death ledger](#14-the-death-ledger)

## 1. The rules in one page

- **A region** is the 60 m around a V (`regions::kRadius`, the sharing range
  NPCs always had). Each game simulates the NPCs it spawned around its own
  V: crowd, gangs, vendors, police. That is the single-player game, left on.
- **Two players are linked** when their Vs are closer than 150 m, and stay
  linked until they are farther than 190 m apart (hysteresis). The host
  computes the links from the positions it relays anyway and tells everyone
  (`Regions`). Players whose position is not known yet count as linked.
- **Only linked players see each other's NPCs.** An owner's NPCs are shown
  as proxies (brain off, moved by the owner's states) in the games linked
  with it, exactly as guests showed the host's NPCs before. Unlinked players
  share no NPC at all: each plays in its own live city.
- **One owner per NPC, always.** The owner is the game that spawned it,
  until a hand-over. Ownership is a property of the NPC kept by every
  endpoint (`Endpoint::NpcOwner`), not of the role host or guest. Every
  message about an NPC goes to, or comes from, its owner; the host only
  relays.
- **Ownership is sticky.** Meeting does not move any NPC: both games keep
  what they run and show each other's. An NPC changes owner only when its
  owner's V is leaving it (more than 80 m away) while another linked
  player's V is within 55 m of it, or when its owner vanishes or its
  owner's world is rebuilt.
- **Hand-over has no pop.** Both games already have a body for the NPC (the
  real one, and its proxy). The giver switches its body's brain off and
  keeps it as a proxy; the receiver switches its proxy's brain on and
  applies the state that came with it (health, poise, status effects,
  combat target, attitude). Nothing is spawned or deleted.
- **A player that holds its region ranks first** (a gig of its own world
  runs where it stands: §9), **then the host, then the lowest player id**
  (`regions::Outranks`).
  Rank decides who spawns where regions overlap, who inherits an NPC, and
  which of two copies of one placed NPC stays.
- **Quest and story NPCs are the host's and never change owner** (pinned).
  So are NPCs that are held, mid-takedown or seated in a vehicle, for as
  long as that lasts: a rider changes owner only with its vehicle.
- **World vehicles follow the same rules** (traffic, parked cars, police
  cars, chase cars: [`traffic.md`](traffic.md), "Several owners, and
  hand-overs"): owned by the game that announced them (only where it owns
  its area), shown to linked players, handed over by the same thresholds
  with their state and their riders, inherited when their owner goes.

## 2. Regions and links

`core/include/tandem/regions.hpp`.

| Constant   | Value | Meaning                                                             |
| ---------- | ----- | ------------------------------------------------------------------- |
| `kRadius`  | 60 m  | a region: what a game shares around its V (`TandemNpcs.Range`)      |
| `kLinkIn`  | 150 m | two Vs closer than this: linked                                     |
| `kLinkOut` | 190 m | linked Vs farther than this: unlinked                               |
| `kKeep`    | 80 m  | an owner keeps its NPC while its V is within this of it             |
| `kTake`    | 55 m  | another linked player may take an NPC over when its V is this close |
| `kLose`    | 90 m  | an owner stops sharing an NPC farther than this from its V          |

`kLinkIn` is two radii plus 30 m: proxies of the other player's NPCs first
appear at least 90 m away, where the game's own crowd pops in too. `kTake <
kRadius < kKeep < kLose` is the hysteresis of ownership: an NPC between two
players does not bounce, and a hand-over is asked for before the owner
would drop the NPC.

**Links, not groups.** Links are pairwise. With three players in a row (A
near B, B near C, A far from C), A and C share nothing with each other and B
sees both. Nothing needs a "group owner": every NPC has its own owner.

**Where a V is** is its last `PlayerState` position. For a player in another
world (`Elsewhere` with `kElsewhereMoved`), that is the body's anchor, as
sent today. A fast travel or a teleport is just a large move: the next
arbitration pass (every 250 ms) unlinks, and the owner's world epoch (§7)
moves its NPCs on.

**The host decides.** `regions::Links::Update` runs on the host endpoint.
Changes go out as one `Regions` message (the whole set of linked pairs, a
revision) on the reliable channel, kept for late joiners and part of a
players resync. A guest's endpoint only reads it.

## 3. Who owns an NPC

**Ids.** An `NpcId` is unique in the session: the game that first announces
an NPC puts its player id minus one in the top 8 bits
(`regions::MakeNpcId`). The host's ids are below 2^24, as they always were.
The id never changes, whoever owns the NPC later.

**The owner** is the announcer (`regions::Announcer(id)`) until the host
says otherwise with `NpcOwner{npc, owner}`. Every endpoint keeps the map;
`Endpoint::NpcOwner(npc)` answers for scripts and routing.

**What the owner does** is what the host did: runs the real NPC, announces
it (`NpcSpawn`), sends its position 10 times a second (`NpcStates`), its
health, poise, animation state, shots, holds, seat, death. Scripts keep one
list of the NPCs their game owns (`TandemNpcs.m_known`) and one of the
proxies they show (`m_proxies`); every game now runs both sides, where the
host ran the first and guests the second.

**Rank** matters in three places only:

1. _Spawning in an overlap_ (§6): a lower-ranked game does not add new NPCs
   inside a higher-ranked linked player's region.
2. _Twins_: a placed NPC (a vendor at its stall) exists in both games. When
   a proxy of a higher-ranked owner's NPC comes, our own copy of the same
   person gives way: it is hidden and no longer shared, and the proxy
   stands in its place. Which of ours that is (`twins::Match`,
   `tandem/twins.hpp`; `TandemNpcs.MergeTwins`):
   - _by identity_: an owner says which of its NPCs are placed
     (`NpcPlaced{npc, entity}`: the static `EntityID`, the same in every
     game; never a crowd NPC). Ours of that id is its twin, wherever each
     stands; with no such NPC of ours, none is (never a neighbour of its
     record);
   - _else by record and place, one to one_: among the proxies with no
     identity and our NPCs with a dynamic id, of the same record and within
     2.5 m (`twins::kReach`), the nearest pairs first. A proxy takes one of
     ours at most and one of ours goes to one proxy at most: two identical
     guards side by side are two pairs. One of ours with a static id is
     never matched by place.

   The proxies of one batch are matched together, 0.3 s after they came
   (the owner's `NpcPlaced` comes by another queue than its spawn). An NPC
   of ours in a fight, hurt or down is never given up (ARCHITECTURE's
   warning about matching waves stands). A placed NPC of ours that comes
   into range while another game's proxy of the same entity is here is
   never announced (`IsTwinOfProxy`), whatever the ranks: one owner per
   NPC. A vendor's twin keeps its shop: beside the proxy, our own hidden
   copy's vendor opens (`progression.md`, "Vendors").

3. _Heirs_ (§7): when an owner goes, the highest-ranked linked player near
   each of its NPCs takes it.

## 4. Hand-over

The host arbitrates (it has every V's position and every shared NPC's
position, both at 10–30 Hz). Every 250 ms, for each shared NPC that is not
pinned and has no hand-over in flight:

> the owner's V is farther than `kKeep` from it, and a player linked with
> the owner has its V within `kTake` of it → hand it to the highest-ranked
> such player.

```
host                 giver O                         receiver P
 │  NpcHandover{npc,to=P}  │                               │
 │────────────────────────>│ reads the snapshot,           │
 │                         │ brain off, keeps the body     │
 │  NpcHandoverState       │ as a proxy                    │
 │<────────────────────────│                               │
 │ owner := P                                              │
 │  NpcOwner{npc,P} to everyone who sees it                │
 │  NpcHandoverState ─────────────────────────────────────>│ brain on,
 │                                                         │ applies the
 │  NpcHandoverDone{npc, accepted}                         │ snapshot
 │<────────────────────────────────────────────────────────│
```

- **One owner at every moment.** The host changes the owner when the
  giver's state arrives. Before that the giver runs the NPC; after it the
  receiver does. In between (one relay hop) the NPC stands still: a pause
  of a few tens of milliseconds, never two brains.
- **The snapshot** (`NpcHandoverState`): position, yaw, health and poise
  (fractions; −1: unknown, keep the proxy's), flags (in combat, defeated,
  unconscious, blind: the giver had no body to read), the V it fights
  (`target`) and the Vs it is hostile to (`hostileTo`, a bit per player),
  its high-level state, and up to 8 status effects with remaining time and
  stacks. What cannot be carried (the behaviour tree's inner state, cover,
  squad, the animation in progress) is in coop-regions §2.3: the receiver's
  NPC decides again from its target.
- **Refusal.** The giver may answer `NpcHandoverDone{accepted = false}`:
  the NPC is a quest NPC, held, in a takedown, seated or being hacked. The
  host pins it for 5 s (for good when the giver also sent `NpcOwner` with
  `kNpcPinned`) and asks nobody else.
- **Failure on the receiver.** No proxy body and none could be spawned: the
  receiver answers `accepted = false`, and as its owner declares the NPC
  gone (`NpcGone` reason 0). An NPC is never left with an owner that does
  not run it.
- **Loss.** Every step is retried by the host after 1 s (3 times) from
  what it kept: the request, then the state. The giver's endpoint keeps the
  state it gave until it hears the new owner, and answers a repeated
  request from it without asking its game again. The receiver's endpoint
  answers a repeated state for an NPC it already runs with the same `Done`.
  A request that got no answer after the retries is dropped (the giver
  still owns the NPC; the next pass asks again if it still applies). The
  digests (§8) cover the owner map.
- **Crossing the digests.** While a hand-over is in flight the two games'
  word on who runs the NPC lags the host's record by design (the receiver
  learns it owns the NPC from the state, which may be lost and sent again a
  second later). So the host leaves that NPC out of the giver's and the
  receiver's `OwnedDigest`, an owner's set (§8) never drops it, and a resync's
  `NpcSet` to either still names it. Before this, an owner's set that
  crossed the state made the host forget an NPC the receiver then woke up:
  run for good by a guest whose spawns of it the host refused (another's
  range of ids), a resend loop that never healed (the lossy loopback test
  failed about one run in five).

**In the scripts** (`Npcs.reds`, `Regions.reds`):

- _Give_: `TandemProxies.Pacify` on the real NPC, tandem's controller
  attached, the entry moves from `m_known` to `m_proxies` with `native` set
  (the body is the game's own entity, not one of ours: it is never deleted,
  only hidden or woken up again).
- _Take_: `TandemProxies.Wake` (the reverse of `Pacify`), controller
  detached, health, poise and effects applied, threat injected for the
  target (the way tandem's test ganger is set on a puppet), the entry moves
  from `m_proxies` to `m_known`. A rider of a shared vehicle that came with
  its vehicle stays in its seat (it drives our car now); any other seat is
  left.
- _Vehicles_ (`Traffic.reds`): the giver's real car is kept as the proxy
  (a crowd car held out of its lane); the receiver's proxy is put where the
  giver left it, with its health, and drives on (`traffic.md`). If the brain does not come back within
  1.5 s (the AI component reports disabled), the NPC is spawned again in
  place from its record and the proxy deleted in the same frame: one frame
  of swap instead of a frozen enemy.

## 5. What travels where

"The host" in every NPC message now reads "the NPC's owner". No existing
message changed; the endpoint routes by owner.

| Messages                                                                                                                                                                                   | From → to                                                                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `NpcSpawn`, `NpcStates`, `NpcGone`, `NpcHealth`, `NpcAnims`, `NpcShot`, `NpcHold`, `NpcSeat`, `NpcPoise`, `NpcWound`, `NpcHackState`, `NpcStatus`, `NpcAwareness`, `NpcLevel`, `NpcPlaced` | the owner → every player linked with it (a guest's go through the host, which checks the sender owns the NPC, keeps the record, and relays) |
| `NpcHit`, `NpcStrike`, `NpcEffect`, `NpcHack`, `NpcAction`, `ShotHit`, `Defended`                                                                                                          | any player → the owner (through the host; the host stamps the sender)                                                                       |
| `PlayerHit`, `PlayerHack`, `NpcCue`, `NpcShotAt`                                                                                                                                           | the owner → one player, only if that player's game shows the NPC (in the record, linked with its owner), through the host                   |
| `NpcActionReply`                                                                                                                                                                           | the owner → whoever asked (the host remembers the asker of each forwarded `NpcAction`), even if the link broke since: it asked              |
| `ActorPose`                                                                                                                                                                                | the host → the players linked with it (a scene actor is a pinned NPC of the host's)                                                         |
| `AreaHit`, `PlayerHit` with no NPC (`npc` 0, host only)                                                                                                                                    | to the V hit, whatever the links: not an NPC's word (a V's grenade on another V; a hit of the host's world no NPC is shared for)            |
| `Noise`                                                                                                                                                                                    | a player → every player linked with it: each owner's NPCs hear it from the puppet                                                           |
| `SharedItem` on a corpse                                                                                                                                                                   | the owner → the players linked with it                                                                                                      |
| `TimeEffect`                                                                                                                                                                               | unchanged (to everyone); each owner slows its own NPCs inside the bubble                                                                    |
| `Regions`, `NpcOwner`, `NpcHandover`, `OwnedDigest`, `TrafficOwnership`, `TrafficHandover`                                                                                                 | host → guests                                                                                                                               |
| `TrafficSpawn`, `TrafficStates`, `TrafficStatus`, `TrafficGone`, `Chase`, a shared vehicle's `VehicleDamage`                                                                               | the vehicle's owner → every player linked with it (through the host, as for NPCs)                                                           |
| `TrafficHit`, `TrafficClaim`, a shared vehicle's `VehicleHit` and `GunnerRequest`                                                                                                          | any player → the vehicle's owner                                                                                                            |
| `TrafficHandoverState`, `TrafficHandoverDone`                                                                                                                                              | as `NpcHandoverState`, `NpcHandoverDone`                                                                                                    |
| `NpcHandoverState`                                                                                                                                                                         | giver → host → receiver                                                                                                                     |
| `NpcHandoverDone`                                                                                                                                                                          | giver or receiver → host                                                                                                                    |

`NpcDeath` goes from the NPC's owner to the host, and from the host to
everyone, whatever the links (§14). `RegionHold` goes from a player's game to everyone (the host stamps and
relays it, and keeps who holds for late joiners).

**Only to the games that show the NPC.** Everything an owner says about its
NPC goes where its states go: to the players its NPC is shared with
(`Endpoint::SharedWith`: in the host's record, its owner linked with that
player), the host's own game included. That holds for the word to one
player too: a hit, a hack, a cue or a shot aimed at a V whose game does not
show the NPC (a guest just past `kLinkOut`, one in another region, an NPC
past its region's edge) is dropped by the host (`SendPlayerHit` and
`SendNpcShotAt` return false, and the plugin logs it). Before, the endpoint
passed these on whatever the links: the
guest's game dropped the shot (shooter unknown) but took the hit of an NPC
it never saw. Requests (`NpcHit`, `NpcStrike`, `NpcAction`…) still reach the
owner whatever the asker shows: one in flight when a link breaks counts.

`NpcStates` from a guest are renumbered by the host (one sequence for every
receiver, so the epoch fence and the tracks keep working) and keep the
owner's timestamp on the host clock. An owner linked with nobody sends no
`NpcAwareness` or `NpcShot` at all, and its `NpcStates` once a second
(`regions::kApartStatesUs`), which the host keeps in its record and passes
to nobody; its reliable messages (spawns, health…) still go to the host, so
the record is whole, positions included, the moment a link forms. Without
those states a link showed its NPCs where they were first announced: a
proxy's spawn beyond 250 m of the V is ignored by `Npcs.reds`, so the
proxies came 2.5 s later from the consistency check (`regions-meet`,
2026-11-21).

**Paths generalised** (each was "host owns, guest asks"; each is now "owner
owns, anyone asks"): hits and their outcomes, status effects, kill credit
(`NpcGone.killer`), stealth and detection (`NpcEntry.detected`,
`NpcAwareness`, noises), loot (corpses per player; quest items on a corpse
from its owner), takedowns, grapples and carried bodies, NPC animation
states and shots, shots at a V, quickhacks, time bubbles, and the digests.
Since then (2026-11-19, the checks that still read "the host"): NPC levels
(`NpcLevel`: each owner scales its own NPCs to the session's level and
says so, [`progression.md`](progression.md)), a V's block or parry of an
NPC's attack (`Defended`, to the owner), hit reactions and ragdolls
(`NpcReaction`, from the owner; every game plays them on its proxies), an
NPC's grenades (`Throw`, from the owner's game only), the Relic's marks,
one-punch slow motion and instant dismemberment (`Outcomes.reds`, on
the NPC's owner), a way out of combat (`Stealth.reds`, on the owner), a
downed partner dropped from every game's own NPCs' threats
(`Revive.reds`), and the shots a proxy fires at our V (`Shots.reds`, any
game). What still reads `IsHost()` there means the session's host: its
world's devices and turrets, scene actors, difficulty and rules,
companions, the session lost.

## 6. Crowd density

- **Alone in its region**, a game runs its normal crowd: nothing is turned
  off, nothing hidden. The `ChangeDensityModifier(0)` a guest applied for
  the whole session is gone.
- **Inside a higher-ranked linked player's region** (our V within 45 m of
  theirs; back to normal beyond 60 m): density 0, as before, once that has
  held for a second (at a join, a place not yet known covered a guest 1 km
  away for a moment and emptied its street). The two Vs see the same
  streets; one game populates them.
- **In the overlap only**: an NPC of ours first seen inside a higher-ranked
  linked player's region, and not in a fight, is suppressed (brain off,
  hidden) and never announced. Everything we already ran when the regions
  met stays ours and is shown to the other player (no pop on either
  screen); only _new_ spawns in the overlap are held back. Our NPCs outside
  that region are untouched.

- **A linked player that outranks us and has said nothing of where it is**
  (we just joined, or it sends no state): for three seconds our game
  neither announces nor hides its new NPCs; after that it takes that player
  to be with us (crowd off, own NPCs near us hidden), which is what a guest
  did before regions. Where a player is comes from the session's states
  (`TandemRegions.CoveredBy`), not from its puppet: a puppet takes seconds
  to spawn.

**The overlap, held back** (2026-11-20; built, not run in game). A _cover_
is the region of a linked player that outranks us (`regions::Covers`,
`TandemRegions.Covers()`): its game populates the world there. What ours
does about its own ambient crowd and traffic, beyond the three rules above:

- **Around a cover** (our V linked, not deep inside), by this game's
  `overlap` setting (`tandem.ini`):
  - `density` (default): the crowd's density modifier follows the part of
    our region no cover overlaps (`regions::CrowdShare`: two discs of 60 m,
    in tenths: 0.9 at 100 m, 0.6 at 60 m, 0.5 at 46 m, then 0 inside 45 m),
    so the two crowds together are about one. The modifier is global: our
    crowd thins on our far side too.
  - `area`: a crowd null area (`CommunitySystem.EnableDynamicCrowdNullArea`,
    a 120 × 120 × 80 m box, not saved) is kept over each cover: nothing of
    ours spawns in it, pedestrian or car, and our far side is untouched.
    The plugin plans them (`regions::NullAreas`, `TandemRegions.NullSteps`):
    placed again when the cover's V has moved 15 m or after 8 s, each with a
    lifetime of 12 s, so one ends by itself if our game stops renewing it.
    Untested on 2.50 (`engine/regions.md` §13.95): opt-in.
  - `off`: neither.
- **A driver or a passenger** of a car of our world that is not shared is
  never hidden at the wheel: it follows its vehicle (not announced while in
  the cover, `TandemNpcs.RidesUnshared`).
- **Our own traffic** (`Traffic.reds` `HeldBack`): a crowd car of ours inside
  a cover, or anywhere while another game runs our area, is held back:
  never announced while in there, announced like any other once it has
  driven out and we run our area. Not quest, police, player or wrecked
  vehicles, nor one a V sits in. With `overlap_despawn = on` (a probe, off
  by default) each held back car is asked once to leave
  (`PreventionSpawnSystem.RequestDespawnVehicleAndPassengers`).
- **Restored when apart**: the covers go with the link (or the session):
  density is reset, the null areas are disabled, held back cars are shared
  again. NPCs hidden meanwhile stay hidden until they stream out (§10).
- Nothing is switched in the crowd, traffic or prevention systems
  themselves: the game's own levers only (a density, a null area), so what
  expects a crowd (quests' own crowd nodes, fear reactions) keeps working.

`tools/game.sh cmd overlap` reports the setting, the covers, the crowd's
share, the null areas and the cars held back.

**The area owner** is the same rule as one answer per player, for what is
not an NPC: `Endpoint::AreaOwner(player)` (`regions::AreaOwner`) is the
game that runs the world around that player's V (its new NPCs, its crowd,
and its traffic and police, [`traffic.md`](traffic.md), "Who owns an area"):
its own game's, unless its V is within 45 m (60 m to leave) of a
higher-ranked linked player's V, then that player's; a linked player that
outranks it while either place is unknown covers it. Every endpoint decides
it every 250 ms from the places it hears (a guest hears every V's state),
so no message carries it. `Endpoint::OwnsArea()` is "ours, around our own
V"; scripts: `TandemRegions.AreaOwner(player)`, `TandemRegions.OwnsArea()`.

When regions first meet, both crowds are there for a moment in the overlap
(each real in one game, a proxy in the other, the same on both screens).
The lower-ranked game's share thins out as its people walk off and are not
replaced. Twins of placed NPCs are merged at once (§3).

## 7. Late joiners, vanishing owners, rebuilt worlds

- **A late joiner** gets the links (`Regions`), then the NPC domain of the
  owners it is linked with: spawns, owners that differ from the announcer,
  health, poise, holds, animation states, seats. It is linked with everyone
  until its first position arrives, then only with who is near (a resync of
  the NPC domain, `NpcSet` first, drops the rest). Its scripts do not spawn
  a proxy farther than 250 m from their V.
- **A link forms**: the newly linked player is sent the owner's NPC domain,
  as a late joiner. **A link breaks**: it is sent the set it still sees
  (`NpcSet`), and drops the rest. The host's own game gets the same as
  events.
- **An owner vanishes** (no goodbye: 5 s of silence, `RemovePlayer`). For
  each of its NPCs, in the host endpoint:
  - the highest-ranked player who was linked with it and whose V is known
    to be within `kKeep` of the NPC becomes its owner (a player the host
    has no position of yet inherits nothing), and is sent an `NpcHandoverState`
    built from the session's record (last position, last health, flag
    _blind_: no target). Its proxy wakes up and its AI picks a target with
    its own senses. The others hear `NpcOwner`;
  - an NPC nobody is near, a pinned one, or one held by the player that
    left, goes (`NpcGone` reason 0) for whoever saw it.
- **The host vanishes**: the session ends for every guest, as before. Each
  guest's own NPCs were always its own real NPCs: they simply stay. Its
  proxies are deleted, bodies it had lent (given away in place) wake up
  again, its density returns. A guest alone in its region notices nothing
  but the notice.
- **An owner's world is rebuilt** (`Epoch`: fast travel, loading screen):
  the players it leaves behind inherit its NPCs that are near them (not
  the pinned or seated ones: a companion travels with its owner). The owner's
  endpoint hears `NpcOwner` for each and its game stops running them. The
  ones nobody is near stay its own: its game reports them gone when its scan
  no longer finds them, as it always did, and finds new NPCs at the
  destination.

## 8. Consistency

- **The NPC digest** a guest gets covers the NPCs it must show: those of
  the owners it is linked with, with their owner in the hash. The guest
  hashes the NPCs it does not own the same way. A mismatch twice in a row
  asks for the domain again (`NpcSet`, spawns, owners…), as before.
- **`OwnedDigest`** (host → each guest, with each round of digests): the
  NPCs and the shared vehicles that guest owns, as the host's record has
  them. The guest hashes its
  own list; a mismatch twice in a row makes it send its whole set again
  (`NpcSet` of its ids, then spawn, health, hold of each; `TrafficSet` of its vehicles, then
  their spawns and statuses). The host drops what the sets no longer name, and answers what it names that the record
  gives to another (`NpcOwner`: a word on a new owner that was lost) or no
  longer holds and that is not of the guest's own range (`NpcGone`: its spawn
  of it would be refused). Neither applies to an NPC or a vehicle in a hand-over with that
  guest (§4). This is the same mechanism pointed the other way, for what a
  guest's lost `NpcSpawn`, `NpcGone` or `NpcOwner` would break.
- **Links** are in the players digest (`Regions.pairs`), and sent with a
  players resync. They were said to be and were not: a lost `Regions` was
  never sent again until the links changed, and an owner that believed it
  was linked with nobody sent no NPC states, so the host decided hand-overs
  from where its NPCs had been (`peer-regions` lossy stage, about one run in
  three under load).
- **Idempotence**: a repeated `NpcOwner`, `Regions`, `NpcHandoverState` or
  `NpcHandoverDone` changes nothing and raises no event. A spawn of a known
  NPC is not applied again. `NpcHandover*` are one-shots for the fault
  layer (never duplicated by it).
- **Only the owner speaks.** The host drops a guest's NPC message when the
  sender does not own that NPC (or, for a spawn, when the id is not in the
  sender's range). A guest drops requests for NPCs it does not own.

## 9. What stays host-only

- Quests, journal, phone, scenes, dialogue (`IsStory` messages), rewards.
- **Quest and story NPCs**: announced by the host and pinned (`NpcOwner`
  with `kNpcPinned`: `IsQuest`, bosses, NPCs in a scene, companions seated
  in the host's vehicle). They are never handed over and never inherited;
  far from the host they are simply not shared, as before. A guest's own
  copy of a pinned NPC near the host is its twin and gives way (§3).
- Time of day, weather, difficulty, the session's roster, loot arbitration
  of dropped items, game over.
- The arbitration itself: links, owners, hand-overs, digests.
- Police _heat_ is not shared: each game's `PreventionSystem` chases its
  own V with its own units (they are NPCs like any other: owned by the game
  that spawned them, shown to linked players, pinned).

### Gigs: grouped or apart, and holding a region

What the quest work needs from regions
([`quest-carryover.md`](quest-carryover.md), "Whose gig is it": a gig
belongs to the world of the player who starts it), and how each point is met.

1. **One "grouped or apart" fact per player, with hysteresis, for scripts
   and core.** _Apart_ is "linked with nobody"; links have the 150 m / 190 m
   hysteresis of §2, so a doorway never flips it. A player's _group_ is
   itself and everyone linked with it, directly or through others
   (`regions::GroupOf`), and the group's _leader_ is the player whose
   world's quests are played there (`regions::Leader`): a player that holds
   its region, else the host, else the lowest id; a player apart leads
   itself. Core: `Endpoint::Apart()`, `Endpoint::RegionLeader()`, the
   `RegionsChanged` event (links and leader). Plugin, any thread:
   `Regions::Get().IsApart()`, `.Leader()`. Scripts: `TandemRegions.IsApart()`,
   `TandemRegions.Leader()`. The quest freeze holds a guest's gig phases
   exactly when `Leader() != you`.
2. **While apart, a guest's game owns and shows the NPCs around its V, gig
   NPCs included, and shares them with a partner who arrives.** Apart,
   nothing of a game's own is hidden and its crowd is on (§6). When a
   partner links, every NPC the guest runs stays its own (ownership is
   sticky, §1) and is announced to the partner, which shows proxies: the
   partner is a helper.
3. **A region keeps its owner while a gig phase of that world is active
   there.** The owner's game says so with `RegionHold` (`Endpoint
::SetRegionHold`, `TandemRegions.Hold(true)`; the quest work calls it
   when a gig phase of its own world starts where its V stands, and with
   `false` when it ends; `tools/game.sh cmd region hold on` for tests).
   While a player holds:
   - it **ranks first** where regions overlap, before the host: the other
     games hold back their new spawns inside its region and turn their
     crowd off deep inside it, so the gig's next room is populated by the
     holder's game even when the helper gets there first;
   - **none of its NPCs is handed over** (`regions::Taker` returns nobody
     for an owner that holds; its own game refuses too);
   - it **leads its group**: the helpers' own copy of that gig waits.
     A holder that leaves the session holds nothing (the host says so).
4. **Quest NPCs are never handed over between games.** Pinned (§4, §9):
   never asked for, never inherited, refused by their own game.

**Where this differs from the rest of the design**, and what it costs:

- Without a hold, rank is static (host first), and a guest mid-gig would
  become "grouped, not the leader" the moment the host came within 150 m:
  its gig would freeze and its new gig spawns near the host would be held
  back. The hold exists for that case; the quest work must set it before
  the gig's first NPCs spawn. Until it does, a guest's gig is only
  protected by rule 4 (pinned NPCs) and by ownership being sticky.
- No hand-over mid-gig means the holder's NPCs stay shared only within
  90 m of the holder's V (`kLose`). A helper that stays in a room the
  holder has left by more than that sees those NPCs go, although its own V
  is next to them. Accepted: the gig is the holder's.
- Two players that each hold a gig and then meet both keep their NPCs; the
  group's leader is the host if it is one of them, else the lowest id, so
  the other one's gig waits while they stay linked (the freeze already
  holds and resumes a gig left half done).
- A holder that vanishes mid-gig: its gig NPCs that are not quest NPCs are
  inherited by the helpers near them like any others (§7) and fight on;
  its pinned ones go.

## 10. Edge cases

| Case                                                              | What happens                                                                                                                                                                       |
| ----------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Two players stand between each other's NPCs                       | nothing moves: an NPC changes owner only when its owner is beyond 80 m of it                                                                                                       |
| An NPC chases a player away from its owner                        | it is handed to the chased player when the owner is 80 m behind: wounds, burns and target kept                                                                                     |
| The owner walks off, nobody is near the NPC                       | no hand-over; beyond 90 m from its owner the NPC leaves the shared world (`NpcGone` 0), as at the 60 m edge before                                                                 |
| A hand-over is asked while the NPC is grappled, carried or hacked | the giver refuses; asked again 5 s later                                                                                                                                           |
| The NPC dies during a hand-over                                   | the giver answers with its death (`NpcGone` 1) instead of a state; the host drops the hand-over. Dead bodies never change owner: a corpse stays with the game where it fell        |
| The receiver leaves or vanishes before `Done`                     | it owned the NPC already: the vanish rule (§7) finds an heir, the giver included (its body is still there, lent)                                                                   |
| The giver vanishes before its state arrives                       | the NPC was still the giver's: the vanish rule                                                                                                                                     |
| Both players fast travel at once                                  | each epoch moves that owner's NPCs on; with nobody left near them they all go                                                                                                      |
| A player in cyberspace or a braindance                            | its place is its body's; its game shares no NPC while away (as before), and takes none: it refuses hand-overs                                                                      |
| NCART                                                             | no NPC is shared from a train (as before); passengers are each game's own                                                                                                          |
| A guest joins far from everyone                                   | linked until its first state (a moment), then unlinked: its scripts spawn no far proxy, its crowd was never off                                                                    |
| Three players, A–B linked, B–C linked, A–C not                    | an NPC of A's is shown to B only. If B takes it over, C starts seeing it (it is B's now)                                                                                           |
| A lent body (given in place) when the link breaks                 | it was the game's own entity: hidden for good, like a suppressed NPC (it belongs to the other world now)                                                                           |
| A woken proxy (taken over) that walks out of its owner's range    | deleted by its owner when it leaves the shared world: it is one of our spawned entities, the game would never stream it out                                                        |
| An older build in the session                                     | the new messages are appended; a build that does not know them ignores them and never owns an NPC as a guest (the protocol version is unchanged, so it is not refused: do not mix) |

## 11. Where the code is

| Piece                                                  | Where                                                                                                                                                                  |
| ------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Constants, rank, links, hand-over, heir and area rules | `core/include/tandem/regions.hpp`, `core/src/regions.cpp`, `tests/regions_test.cpp`                                                                                    |
| Messages                                               | `protocol.hpp`: `Regions`, `NpcOwner`, `NpcHandover`, `NpcHandoverState`, `NpcHandoverDone`, `OwnedDigest`, `RegionHold`                                               |
| Ownership, routing, arbitration, retries               | `core/src/endpoint.cpp` (search "Regions"), `tests/regions_endpoint_test.cpp`                                                                                          |
| Natives for scripts                                    | `plugin/src/region_natives.cpp` ↔ `scripts/tandem/RegionNatives.reds`                                                                                                  |
| Owner and proxy sides in one game                      | `scripts/tandem/Npcs.reds`; give, take, suppression, twins: `scripts/tandem/Regions.reds`                                                                              |
| World vehicles of several owners                       | `core/src/endpoint_traffic.cpp`, `tests/traffic_regions_test.cpp`, `scripts/tandem/Traffic.reds` (`traffic.md`)                                                        |
| Mock player as an NPC owner                            | `tandem-peer --mode region` (`peer/src/main.cpp`), `tests/peer_regions.sh`                                                                                             |
| The death ledger, twins                                | `tandem/ledger.hpp`, `tandem/twins.hpp`, `NoteDeath` and `RecordDeath` in `core/src/endpoint_regions.cpp`, `scripts/tandem/Ledger.reds`, `tests/ledger_test.cpp` (§14) |
| Scenarios                                              | `tools/scenarios/regions-apart.sh`, `regions-meet.sh`, `regions-vanish.sh`, `regions-overlap.sh`, `ledger-guest.sh`                                                    |
| The overlap                                            | `regions::Covers`, `CrowdShare`, `NullAreas` (`regions.hpp`); `TandemNpcs.UpdateQuiet`, `UpdateNullAreas`; `TandemTraffic.HeldBack`                                    |

## 12. Testing

Without the game (all in `task test`):

- `regions_test`: links with hysteresis, unknown places, rank, the take,
  heir and area-owner rules.
- `regions_endpoint_test` (loopback UDP, real endpoints): a guest owns NPCs
  and the host shows them; apart, nothing is shared either way; meeting
  shares both sets; hits, effects, hacks, actions and replies reach the
  owner and come back; an owner's word on its NPC (hits, cues, shots,
  hacks, animations, holds, wounds, poise, health) reaches no guest just
  past the link distance, while a V's own area hit still does; a lost
  `Regions` healed by the players digest; a hand-over when the owner walks
  away, the same with a third of all messages lost (every NPC run by
  exactly one game, whatever was lost; 50 runs in a row), a refusal; an old
  owner that missed the word on the new one stops running it; an owner
  that vanishes (heirs, and nobody near); an owner's epoch; digests in both
  directions; the area owner around each player as every endpoint sees it,
  with its hysteresis.
- `peer_regions.sh`: two mock players, one hosting and one in
  `--mode region`, far apart and then walking together, with `--drop`
  (the lossy stage walks off once the guest knows it is linked,
  `--region-leave-linked`, and waits as long as a few lost resyncs take;
  `--fault-seed` replays a run's faults, whose seed is logged).
  The guest also runs cars of its own (`--region-cars`, one driven by its
  NPC): handed over to the host when it walks off.
- `traffic_regions_test`: a guest that owns its area shares its vehicles
  with linked players only, requests reach that guest, a meet and a part
  (the vehicle and its driver handed over and back, no pop, no duplicate), a
  third player, a refusal and a failed take, an owner that vanishes or
  fast travels, the owned digest, a late joiner.

In the game (one PC and the mock player):

- `regions-apart`: the game hosts, the peer joins 1 km away with NPCs of its
  own. Expect no proxy, the game's NPC count unchanged, no `NpcStates`
  leaving. Then the game as a guest of a far host: "own crowd on", nothing
  hidden.
- `regions-meet`: the peer walks into the game's region with three NPCs.
  Expect proxies of the peer's NPCs on the game, a hit on one routed to the
  peer, then the peer walks off leaving them: "handover in" lines, the
  proxies now attack on their own. The reverse with the game walking away
  is a manual check (the game's V has to move).
- `regions-vanish`: the peer owns NPCs next to the game and is killed
  without a goodbye. Expect the game to inherit them within 5 s
  ("inherited npc …"), none duplicated, none frozen.
- `ledger-guest`: the game stands by a placed vendor of its own world and
  joins a mock host 300 m away (or at `LEDGER_AWAY`, a spot to stand on)
  that runs "the same" placed NPC (its record and entity) and kills it.
  Expect `ledger: placed npc … died in player 1's game`, `our copy is kept
out of our world`, and with `LEDGER_AWAY`, once our V is teleported
  there, `a body for placed npc`.
- Twins: `vendor-guest` (`progression.md`) notes which way our own vendor
  gave way (`the same placed NPC`: by identity). Two identical guards side
  by side need two real games (the mock player cannot stand NPCs on ours):
  expect two `is the twin of` lines naming two different NPCs of ours.
- `regions-overlap`: the game joins a mock host 100 m away that walks up
  and off again. Expect `own crowd at 0.9` (or, with `overlap = area`, a
  `crowd null area … over player 1's region`), `own crowd off` deep inside,
  `crowd vehicle(s) of our own held back` in `cmd overlap`, and the crowd
  back after. Look for: the street's density while near (not doubled, not
  empty), no driverless car, how fast the crowd returns.
- `wheels-guest-area` (`traffic.md`): the peer owns its area and runs cars;
  they are shown as proxies when it comes near and handed over, driver
  seated, when it walks off.

## 13. Open points and risks

See the tags in [`../research/engine/regions.md`](../research/engine/regions.md).

1. **Waking a proxy** (`AIHumanComponent.Toggle(true)` after `false`) is
   untested on 2.50. The respawn fallback needs nothing untested but shows
   one frame of swap and loses the animation in progress.
2. **An adopted ambient NPC** is no longer in a community: it stands or
   wanders by its record's default behaviour until it leaves the shared
   world.
3. **Twins**: by identity where the owner named one, else one to one (§3).
   Open: which NPCs have a static id on 2.50 (`engine/regions.md` §13.94):
   a gang member with a dynamic id is matched by place, and two that swapped
   posts between the games are paired crosswise (harmless: same record).
4. **Deaths across regions**: the ledger (§14), for placed NPCs, for the
   session. Not kept: an NPC with a dynamic id (the crowd, and whatever
   else 2.50 gives one), the host's save.
5. **Traffic and parked cars** follow the NPCs' ownership (`traffic.md`,
   built, not run in game). Our own crowd's cars where another game runs
   the street are held back and kept from spawning (§6, "The overlap, held
   back"); a car already there is only removed if the `overlap_despawn`
   probe works (no call is known to remove one, `engine/traffic-police.md`
   §13.32): until then it drives on, seen by us alone.
6. **The epoch fence** covers requests aimed at the host's world only; a
   hit in flight at a guest owner whose world was just rebuilt is dropped
   by that guest because it no longer owns the NPC.
7. **Bandwidth**: an owner with 64 NPCs sends about 13 kB/s of states to
   the host while linked with anyone; the host relays it to each linked
   guest.

## 14. The death ledger

The same dead stay dead: a placed NPC (a guard at its post, a gang member
at its hangout, a vendor) that any player kills is dead in every game for
the rest of the session, also for a player who was at the other end of the
map and comes by an hour later. Built and tested without the game.

- **Which NPCs.** The ones their owner said are placed (`NpcPlaced`: a
  static `EntityID`, `EntityID.IsStatic`, and not `IsCrowd()`). The random
  crowd is never in it. A knock-out counts (`kDeathKnockedOut`: such an NPC
  never gets up); a later death of the same NPC is not a second entry.
- **Who records.** The owner's endpoint, when its game reports the NPC dead
  or knocked out (`SendNpcGone`): `NoteDeath` builds an `NpcDeath` from the
  NPC's records (entity, record, appearance, where it last stood) and
  sends it to the host before the `NpcGone`. Scripts say nothing more than
  `TandemRegions.Placed` when they announce the NPC.
- **Who keeps.** The host (`ledger::Deaths`, `tandem/ledger.hpp`): it
  checks the sender runs that NPC, stamps the death (`by`, `order` from 1,
  `timeUs` on its clock), keeps the newest 256 (`kMaxDeaths`; the oldest
  goes, the same in every game) and tells **everyone**, linked or not, its
  own game and the reporter included. A late joiner is sent the whole
  ledger with the world; the world digest covers it (a lost word comes back
  with a world resync). One entry per entity: a repeat is applied nowhere.
- **Applied** by each game's scripts (`Npcs.reds` `Share`, `Ledger.reds`):
  ten times a second, an NPC of our own world within 90 m whose static id
  the ledger names is taken out of the shared world if we had announced it
  and hidden (`Bury`: brain off, `HideIrreversibly`), unless it lies there
  itself (dead, unconscious, defeated) or is one of ours in a fight. A
  quest NPC of our own world is only hidden for a death in the host's
  game: a gig of ours may still need the one a partner killed in its own
  world. A quest NPC is one whose character record has a quest affiliation
  (`Character_Record.Quest()`, anything but `General`: there from the
  start) or that a quest marked (`IsQuest()`, which can come after the NPC
  first streams in, when it was buried already; `TandemLedger.QuestNpc`,
  2026-11-21, not yet run in game). Never killed: nothing reaches a save.
- **The body** (`death_bodies = on`): for a death less than 10 minutes old
  (`ledger::kBodyForUs`) in another game, within 70 m of our V (100 m to
  drop it, 8 at a time, the nearest), a copy of the NPC is spawned where it
  fell and laid down with no death played (`Kill(null, skipNPCDeathAnim)`,
  or unconscious for a knock-out), tagged `Tandem.Body`, never shared,
  deleted when our V leaves. Not for a death we saw (its proxy is the
  corpse) nor for our own kill. Its loot is our game's own roll, as for
  every corpse (`loot.md`).
- **At leave, and saves.** The ledger is the session's: every endpoint
  forgets it with the session, the bodies are deleted, and what a game hid
  stays hidden until it streams out, then is alive again in that game's
  own world. A guest's save is not changed (a guest never saves in a
  session, and nothing was killed); **neither is the host's**: after the
  session, what a guest killed alone is alive in the host's world again.
  The killer's own world keeps its dead by the game's own rules.
- **Settings** (each game's own, `tandem.ini` only): `death_ledger = off`
  (others' kills stay alive in our game), `death_bodies = off`.

**Limits.** An NPC seen from farther than 90 m is alive until our V is
within 90 m. An entry is a place of death, not a ragdoll's pose. A
community the killer's game respawns later stays hidden in the others. Only
static ids: if 2.50 gives gang members dynamic ids, they are not kept
(`engine/regions.md` §13.94, to check first). A report lost on its way to
the host is not sent again (ENet delivers it; only the fault layer loses it).

Tests: `tests/ledger_test.cpp` (the ledger's order, bound and digest, whose
body shows and for how long, the messages, twins), `tests/regions_endpoint_test.cpp`
("a placed NPC…", "the ledger…": alone on the host and on a guest, a
knock-out, a late joiner, a lost word healed by the digest, a reporter that
vanishes, an heir's kill), `tests/referee_test.cpp` (the ledger's rules),
`tests/peer_regions.sh` (two mock players apart, each kills a placed NPC:
`tandem-peer --mode region --region-placed HEX --region-kill-at S`).
In the game: `tools/game.sh cmd ledger`; `ledger near` / `ledger goto`
(the nearest placed, alive, non-quest NPC of our own world within 200 m,
with how many NPCs and placed ones are there: `ledger-guest`'s); scenarios `ledger-guest`,
`vendor-guest` (§12).
