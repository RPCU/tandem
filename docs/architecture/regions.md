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
  long as that lasts.

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
   a proxy of a higher-ranked owner's NPC appears within 2.5 m of one of
   ours with the same record, ours gives way: it is hidden and no longer
   shared, and the proxy stands in its place. Matching by record and place
   is used for this only, on NPCs that stand still (ARCHITECTURE's warning
   about matching waves stands: fighting NPCs are never matched).
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

**In the scripts** (`Npcs.reds`, `Regions.reds`):

- _Give_: `TandemProxies.Pacify` on the real NPC, tandem's controller
  attached, the entry moves from `m_known` to `m_proxies` with `native` set
  (the body is the game's own entity, not one of ours: it is never deleted,
  only hidden or woken up again).
- _Take_: `TandemProxies.Wake` (the reverse of `Pacify`), controller
  detached, health, poise and effects applied, threat injected for the
  target (the way tandem's test ganger is set on a puppet), the entry moves
  from `m_proxies` to `m_known`. If the brain does not come back within
  1.5 s (the AI component reports disabled), the NPC is spawned again in
  place from its record and the proxy deleted in the same frame: one frame
  of swap instead of a frozen enemy.

## 5. What travels where

"The host" in every NPC message now reads "the NPC's owner". No existing
message changed; the endpoint routes by owner.

| Messages                                                                                                                                                          | From → to                                                                                                                                   |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `NpcSpawn`, `NpcStates`, `NpcGone`, `NpcHealth`, `NpcAnims`, `NpcShot`, `NpcHold`, `NpcSeat`, `NpcPoise`, `NpcWound`, `NpcHackState`, `NpcStatus`, `NpcAwareness` | the owner → every player linked with it (a guest's go through the host, which checks the sender owns the NPC, keeps the record, and relays) |
| `NpcHit`, `NpcStrike`, `NpcEffect`, `NpcHack`, `NpcAction`, `ShotHit`                                                                                             | any player → the owner (through the host; the host stamps the sender)                                                                       |
| `PlayerHit`, `PlayerHack`, `NpcCue`, `NpcShotAt`, `NpcActionReply`                                                                                                | the owner → one player (through the host; replies go to whoever asked: the host remembers the asker of each forwarded `NpcAction`)          |
| `Noise`                                                                                                                                                           | a player → every player linked with it: each owner's NPCs hear it from the puppet                                                           |
| `SharedItem` on a corpse                                                                                                                                          | the owner → the players linked with it                                                                                                      |
| `TimeEffect`                                                                                                                                                      | unchanged (to everyone); each owner slows its own NPCs inside the bubble                                                                    |
| `Regions`, `NpcOwner`, `NpcHandover`, `OwnedDigest`                                                                                                               | host → guests                                                                                                                               |
| `NpcHandoverState`                                                                                                                                                | giver → host → receiver                                                                                                                     |
| `NpcHandoverDone`                                                                                                                                                 | giver or receiver → host                                                                                                                    |

`RegionHold` goes from a player's game to everyone (the host stamps and
relays it, and keeps who holds for late joiners).

`NpcStates` from a guest are renumbered by the host (one sequence for every
receiver, so the epoch fence and the tracks keep working) and keep the
owner's timestamp on the host clock. An owner linked with nobody sends no
`NpcStates`, `NpcAwareness` or `NpcShot` at all; its reliable messages
(spawns, health…) still go to the host, so the record is whole the moment a
link forms.

**Paths generalised** (each was "host owns, guest asks"; each is now "owner
owns, anyone asks"): hits and their outcomes, status effects, kill credit
(`NpcGone.killer`), stealth and detection (`NpcEntry.detected`,
`NpcAwareness`, noises), loot (corpses per player; quest items on a corpse
from its owner), takedowns, grapples and carried bodies, NPC animation
states and shots, shots at a V, quickhacks, time bubbles, and the digests.

## 6. Crowd density

- **Alone in its region**, a game runs its normal crowd: nothing is turned
  off, nothing hidden. The `ChangeDensityModifier(0)` a guest applied for
  the whole session is gone.
- **Inside a higher-ranked linked player's region** (our V within 45 m of
  theirs; back to normal beyond 60 m): density 0, as before. The two Vs see
  the same streets; one game populates them.
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

**The area owner** is the same rule as one answer per player, for what is
not an NPC: `Endpoint::AreaOwner(player)` (`regions::AreaOwner`) is the
game that runs the world around that player's V (its new NPCs, its crowd,
and the traffic and police of the `traffic-police` branch, whose `traffic.md` asks only this):
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
  NPCs that guest owns, as the host's record has them. The guest hashes its
  own list; a mismatch twice in a row makes it send its whole set again
  (`NpcSet` of its ids, then spawn, health, hold of each). The host drops
  what the set no longer names. This is the same mechanism pointed the
  other way, for what a guest's lost `NpcSpawn` or `NpcGone` would break.
- **Links** are in the players digest, and sent with a players resync.
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

| Piece                                                  | Where                                                                                                                    |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| Constants, rank, links, hand-over, heir and area rules | `core/include/tandem/regions.hpp`, `core/src/regions.cpp`, `tests/regions_test.cpp`                                      |
| Messages                                               | `protocol.hpp`: `Regions`, `NpcOwner`, `NpcHandover`, `NpcHandoverState`, `NpcHandoverDone`, `OwnedDigest`, `RegionHold` |
| Ownership, routing, arbitration, retries               | `core/src/endpoint.cpp` (search "Regions"), `tests/regions_endpoint_test.cpp`                                            |
| Natives for scripts                                    | `plugin/src/region_natives.cpp` ↔ `scripts/tandem/RegionNatives.reds`                                                    |
| Owner and proxy sides in one game                      | `scripts/tandem/Npcs.reds`; give, take, suppression, twins: `scripts/tandem/Regions.reds`                                |
| Mock player as an NPC owner                            | `tandem-peer --mode region` (`peer/src/main.cpp`), `tests/peer_regions.sh`                                               |
| Scenarios                                              | `tools/scenarios/regions-apart.sh`, `regions-meet.sh`, `regions-vanish.sh`                                               |

## 12. Testing

Without the game (all in `task test`):

- `regions_test`: links with hysteresis, unknown places, rank, the take,
  heir and area-owner rules.
- `regions_endpoint_test` (loopback UDP, real endpoints): a guest owns NPCs
  and the host shows them; apart, nothing is shared either way; meeting
  shares both sets; hits, effects, hacks, actions and replies reach the
  owner and come back; a hand-over when the owner walks away, the same with
  a third of all messages lost, a refusal; an owner that vanishes (heirs,
  and nobody near); an owner's epoch; digests in both directions; the area owner around
  each player as every endpoint sees it, with its hysteresis.
- `peer_regions.sh`: two mock players, one hosting and one in
  `--mode region`, far apart and then walking together, with `--drop`.

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

## 13. Open points and risks

See the tags in [`../research/engine/regions.md`](../research/engine/regions.md).

1. **Waking a proxy** (`AIHumanComponent.Toggle(true)` after `false`) is
   untested on 2.50. The respawn fallback needs nothing untested but shows
   one frame of swap and loses the animation in progress.
2. **An adopted ambient NPC** is no longer in a community: it stands or
   wanders by its record's default behaviour until it leaves the shared
   world.
3. **Twins by record and place** can merge two different people of one
   record standing at one spot (two identical guards side by side).
4. **Deaths are not kept across regions** yet: a gang one player wipes out
   alone is alive in the other's game when they come later (coop-regions
   §4, the death ledger). The world is coherent while players can see each
   other; lasting facts are the next step.
5. **Traffic and parked cars** are still each game's own (no switch found).
6. **The epoch fence** covers requests aimed at the host's world only; a
   hit in flight at a guest owner whose world was just rebuilt is dropped
   by that guest because it no longer owns the NPC.
7. **Bandwidth**: an owner with 64 NPCs sends about 13 kB/s of states to
   the host while linked with anyone; the host relays it to each linked
   guest.
