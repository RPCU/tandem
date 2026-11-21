# Scripted moments: the audit

How a guest in the host's moment takes part (`story.md`, "Spectating"), in
the base game and in Phantom Liberty, read from the scripts and the quests'
known structure (2026-11-21), **nothing run in game**: every row is a guess
until its line of the checklist below is ticked. Quest ids are the ones the
2.50 scripts name (engine facts §13.131); the rest is to read with `quest
trace` and `story moments`. The host's V is in a moment while it is in a
scene of tier 2+ (**S**; tier 5: **C**), under quest restrictions on how it
moves (**R**), in a ride its scene drives (**M**), in a scripted body mode
(**X**), under the quest's camera animation (**K**) or in another world
(**E**: cyberspace, a braindance, playing as Johnny or another replacer);
never in a private scene, a fight or its own driving. One rule for both
games, no quest list; the moment list (**L**, below) only corrects it. Other
signals: **P** a quest prefab the guest's game cannot load, **G** a story
gate between the two Vs, **T** a quest teleport outside a moment.

Spectating is the last resort (the user's rule, 2026-11-21), and the whole
of it needs no action from the player. Verdicts: **own** (the guest plays it
from its own V: scene replay, restrictions, ride along), **watch (a)** a
cutscene, **(b)** another world, **(c)** the host's V taken beyond the
guest's reach, **(d)** only the host's view shows it (the quest's camera, a
moment the list marks watch), **(e)** quest interiors the guest's game cannot
load; **free** (not a moment: both play), **never** (private), **verify** (a
signal not read yet).

## The fallback

Whatever the rows below say, and for every moment not listed: the guest's
game switches to watching by itself when one of these holds, and back to
its own V, beside the host, when it ends (`core/story.hpp` `Spectator`,
`scripts/tandem/Spectate.reds`; tests in `tests/restrictions_test.cpp`).

<!-- prettier-ignore -->
| Trigger | Signal | Watch | Built |
| --- | --- | --- | --- |
| A cutscene (tier 5, the scene's camera) | C | (a) | before this audit |
| The host in cyberspace, a braindance, as Johnny or another replacer | E | (b) | before |
| A ride of the moment that leaves without the guest (no seat, or not in it) | M + 30 m | (c) at once | 2026-11-21 |
| The host taken beyond 150 m in a moment (a quest teleport, a fade) | S, R, M + 150 m | (c) | before |
| The quest's camera holds the host's view | K | (d) | before |
| A moment the list marks watch | L | (d) | 2026-11-21 |
| The host among quest prefabs the guest's game cannot load (#64's layout check failed), within 150 m, moment or not | P | (e) | 2026-11-21 |
| A quest teleport outside a moment, the guest beside the host (40 m) just before | T | none: it goes along (below) | 2026-11-21 |
| A story gate between the Vs (the Watson lockdown, Dogtown's gate) | G | the carry and the regroup stop at it | 2026-11-21 |

Not triggers, on purpose: a scene of tier 2 to 4 (played: the guest's own
eyes, weapons lowered at 3+, invulnerable at 4+), restrictions and body
modes (worn), a ride with a seat (ridden along), a private scene (never).

## Seamless

The user's rule (2026-11-21): native co-op, no action needed.

- **Behind a black**: every switch into and out of watching happens under
  a short black of our own (`scripts/tandem/Curtain.reds`: 0.25 s down,
  the switch, 0.35 s up; never black more than 2.5 s). The host's view is a
  cut, not a camera blend across the map; our V is carried to the host only
  once the picture is the host's, and put down beside it under the black.
  A reason that comes back before our own eyes did keeps the host's view
  (no flicker between a cutscene and the next).
- **Back with the host, no prompt**: carried along all through (`story.md`,
  "What watching does"); when the carry fell short, the regroup behind the
  fast travel's loading screen. A guest that stood beside the host (40 m)
  when a quest teleported it outside a moment goes along at once
  (`Travel.reds` `TakenAlong`: black, then the fast travel or the hop);
  only a guest that was elsewhere gets the offer. The game's fast travel
  locks keep the offer; a gate between them too.
- **One notice at most, in the game's warning line, only where the picture
  would confuse**: none for a cutscene (it explains itself) or another world
  (Elsewhere.reds says it already); (c) "Watching NAME: the story takes them
  somewhere you can't follow." (`Spectate-Taken`), (e) "Watching NAME:
  their story is somewhere your game can't show." (`Spectate-Missing`, in
  place of `Prefabs-Missing`), (d) "Watching through NAME's eyes.". Once per
  moment. The "Stop watching" hint stays, an option, never needed.

## The moment list

A moment the generic rule gets wrong is marked by hand, no code change
(`core/include/tandem/moments.hpp`; tested in `tests/moments_test.cpp`):

- **Where**: `dist/moments.txt`, shipped as `red4ext/plugins/tandem/moments.txt`
  (the mod's, replaced by each update and by `task deploy`), lines
  `watch KEY # why` or `play KEY # why`; and the host's `tandem.ini`
  `moment_watch = KEY, KEY` / `moment_play = …` (the player's own). Read
  once, at the plugin's load; the log says `moments: N marked moment(s)`.
- **Keys**: `scene:PATH` (the scene the host's V plays), `phase:PATH` (a
  quest phase that ran a node in the last 3 s, or one of its ancestors: the
  phase holding a playing scene runs its node again and again), `quest:TEXT`
  (a piece of the host's tracked journal path, its ids top down, e.g.
  `q101_`), PATH a resource path or its hash `0x…` as the log writes it. The
  scene decides before the phase, the phase before the quest; a key listed
  both ways is watch. The plugin notes running phases only while some entry
  is a phase (`engine/quests.cpp` `SetPhaseNotes`).
- **Verdicts**: `watch` makes it a moment to watch (`kMomentListed` in the
  host's reasons: its guests that share it watch (d)); `play` keeps a
  cutscene or the quest's camera in it played (the guest's own eyes, scene
  replay showing the actors); another world and a ride the guest cannot
  follow are watched all the same.
- **Marking one found broken**: on the host, at the moment, `tools/game.sh
cmd story moments` logs `moments: … now: scene 0x…, phases 0x… 0x…, quest
…; verdict …`; pick the narrowest key (the scene), add the line to
  `moments.txt` (or `moment_watch` to try it), restart the host's game.
  `story moments test watch|play|off` forces a verdict to try the path.
- Shipped with no entry: nothing has been run in game.

## Prologue (q000)

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| Nomad: the Yucca garage, the talk with the mechanic | S (2-3) | own | — |
| Nomad: the drive to the border with the contraband, V at the wheel | own driving | free | the guest rides the copy's passenger seat; no seat (Jackie in it): it drives after them, or (c) once the car is 150 m away in a moment |
| Nomad: the border checkpoint, the chase that follows | S (3-4), M?, C | own; watch (a) | verify whether the chase is the player's driving (free) or a scene's (M: (c) at 30 m without a seat) |
| Streetkid: Kirk at the bar, the talk in the back | S (2-3) | own | — |
| Streetkid: the car theft with Jackie, the arrest | S, M, C | own; watch (a) | the arrest's shots are a cutscene |
| Corpo: Arasaka's office, Jenkins, the bathroom mirror | S (2-4), R (walk) | own | NightCoop met a trigger in Jenkins' corridor that fires only on the walking V: here the host's V walks it (the guest's arrival counts for nothing, gap 4) |
| Corpo: the ride with Jenkins, the bar with Jackie | M, S | own | the ride along seats the guest if the car has a seat, else (c) at 30 m |
| The lifepath's end ("The Nomad", "The Streetkid", "The Corpo-Rat") | C | watch (a) | — |
| The montage: six months with Jackie | C (long) | watch (a) | the longest (a) of the game: the hint stays, carried with the host (`Follow`) |
| The VR tutorial with Jackie (`q000_vr_*`) | E (a replacer: `Character.q000_vr_replacer`) | watch (b) | verify that Elsewhere.reds' replacer test fires on it (§13.26) |
| Waking in V's apartment, Jackie at the door | S (2), X? | own | — |

## Act 1 (q001 to q005)

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| The Rescue (q001): the morning in V's room, getting dressed | S (2), R | own | — |
| The Rescue: Jackie's car to the scavs' den, Jackie driving | M, S (2-3) | own | ridden along (front seat: V; the back for the guest, if the copy has it); else (c) at 30 m |
| The Rescue: T-Bug's hack, the den's stealth and fights | tier 1, fight | free | — |
| The Rescue: Sandra carried out | S (carry scenes), X | own | X is played: the guest walks beside, held by the restrictions |
| The Rescue: Trauma Team's AV | C | watch (a) | — |
| The Ripperdoc (q001, `q001_ripperdoc_done`): Viktor's clinic, the chair | S (4), X (NoMovement), K? | own; watch (d) | the chair's optics scene may hold the camera (K): watched, else played beside the chair |
| The Ride: Dex's car through the city | M, S (3-4) | own; watch (c) | Dex, V and Jackie may fill the car: then (c) at 30 m, carried in its trunk slot, beside it at the end |
| The Pickup (q003): Royce's talks; the fight after | S; fight | own; free | — |
| The Pickup: the All Foods plant | P (prefab) | free; watch (e) | the plant is a quest prefab (#64): loaded for the guest; a failed layout check: (e) |
| The Information (Evelyn's braindance) | E (braindance) | watch (b) | — |
| Dex's gig: the planning with Evelyn and T-Bug (the Heist's briefing) | S (2-3) | own | — |
| The Heist (q005): Delamain ride to Konpeki | M, S | own | the ride along seats the guest (Delamain has seats) |
| The Heist: Konpeki Plaza's interior | P | free; watch (e) | a quest prefab: loaded for the guest, else (e) |
| The Heist: Konpeki infiltration with Jackie and T-Bug | tier 1, walks | free | the guest sneaks with them on its own V; its acts on the host's objects go through quest interactions |
| The Heist: the penthouse, Saburo and Yorinobu | S (4), C | own; watch (a) | the tier 4 hiding played (restrictions), the tier 5 shots watched |
| The Heist: the escape, the AV crash, the Delamain | C, M | watch (a); own | the crash is a cutscene; the Delamain after, ridden along |
| Playing for Time (q101): V wakes, the walk home | C, S, X | watch (a); own | — |

## Act 2 and the endings

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| The Arasaka raid flashback (Johnny) and every Johnny takeover | E (replacer) | watch (b) | the host's puppet still shows V, not Johnny (`story.md`, Johnny) |
| Braindances: Evelyn's (The Information), Judy's, River's | E (braindance) | watch (b) | the BD editor's own camera is the host's active camera: sent as is |
| Netrunning: Alt with the Voodoo Boys (Transmission), I Walk the Line | E (cyberspace), S | watch (b); own | the talks before and after are played |
| Voodoo Boys: the Grand Imperial Mall, the Animals' boss (`q110_animals_boss`) | fight, S | free; own | — |
| Hellman's interrogation, Takemura's van, scenes in chairs | S (3-4) | own | the guest stands by, weapons lowered |
| Play It Safe (q112): the parade, the float | S, R (walk) | own | the float is a quest ride: ridden along if it has a seat, else (c) at 30 m |
| Play It Safe: Oda's fight (`q112_oda_*`) | fight; C at its end | free; watch (a) | — |
| Car chases with an NPC driving (Takemura, Panam in Ghost Town) | M (Scene ride) | own | ride along, passenger combat; no seat and left behind: watch (c) at 30 m (it was 150 m) |
| Car chases with V driving | own driving | free | the guest rides as a passenger of the copy, as now |
| Passenger combat in an NPC's quest car | M if the PSM says Scene | own | the guest rides along and shoots from its seat |
| Panam's Basilisk (Riders on the Storm, Queen of the Highway) | S? (gunner seat) | own | the ride along; full (Panam and V): carried, then (c) at 30 m |
| The Hanako dinner at Embers (Nocturne Op55N1) | S (3-4), V seated (workspot) | own | the guest stands by the table, weapons lowered; verify no actor's path crosses it |
| Endings: Nocturne Op55N1, Mikoshi | E (cyberspace), C | watch (b), (a) | — |
| Endings: Totalimmortal (Johnny's raid), Panam's AV, the Sun, the Star, the Devil | E, M, C | watch (b), (a); own in the AV | — |
| Endings: the tower's bosses (`q113_boss_smasher`, `q116_boss_smasher`) | fight; C | free; watch (a) | — |
| Endings: the space station, the Devil's epilogue (q201) | C, R (walk), X | watch (a); own | the walks in the station are a body mode played beside the host |
| Epilogues: the Star (q202), the Sun (q203), Temperance (q204) | C, M | watch (a); own | the Nomads' convoy (the Star): ridden along if a seat is free, else (c) at 30 m |
| Credits and epilogue calls | C, calls | watch (a) | calls stay everyone's (`Phone.reds`) |
| A quest teleport (a scene putting the host's V far away) | S, R | watch (c) | carried to the host (`Follow`), then own at once |
| A quest teleport outside a moment (a fade between two places) | T | own: taken along | the guest beside the host goes along behind the black (`TakenAlong`) |
| Scenes holding the camera (a scripted look at something) | K | watch (d) | — |
| Forced walks, slow walks, no-sprint stretches | R, X | own | worn by the guest's V (`story.md`, Restrictions) |
| Fades to black between two places of a scene | S, then a jump | own; watch (c) | the jump past 150 m: (c), carried; under it: played |
| Vehicle-only legs (a timed drive, a chase the quest drives) | M or own driving | own; watch (c) | own driving: the guest rides the copy; a scene's drive with no seat: (c) at 30 m |

## Base game: side quests, gigs, NCPD

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| Judy: Pyramid Song, the dive | S on the boat; swimming at tier 1, weapons away | own; free | the dive is free swimming: both swim |
| Judy: Both Sides Now, the BD | E | watch (b) | — |
| Judy: Talkin' 'bout a Revolution, Pisces, Ex-Factor (Judy's BD editing) | E (braindance), S | watch (b); own | — |
| Panam: Ghost Town, Life During Wartime | M, S; fights free | own; free | — |
| Panam: Riders on the Storm, the Basilisk's ride and the Raffen ambush | M (gunner seat), S, fight | own; watch (c) | Panam drives, the host's V the gunner: no seat for the guest, (c) at 30 m, carried in the tank's trunk slot (verify the Basilisk has one, else the 4 Hz cabin placement) |
| River: Following the River (the farm, the Ferris wheel) | E (BD), S | watch (b); own | the farm's search is tier 1: free |
| River: The Hunt, its car chase | M, S, E | own; watch (b) | — |
| Kerry: A Like Supreme (on stage), Holdin' On | S (workspots in a scene) | own | — |
| Kerry: Rebel! Rebel! (V drives) | own driving | free | — |
| Kerry: the boat (Boat Drinks) | M (the boat), private | never | a romance: the host's alone |
| Peralez: Dream On (the apartment, the data dives) | S, E? | own; verify | a data dive that is a scene camera (tier 5), not cyberspace, is (a) |
| Sinnerman: the ride with Joshua, the crucifixion | M, S (4), C; E if V takes the braindance | own; watch (a), (b) | — |
| Romance scenes | private | never | by design (`story.md`, private scenes) |
| Gigs: the fixer's call, the talk on arrival | calls; private one-to-one scenes | free | private scenes are their player's |
| Gigs and NCPD hustles: fights, stealth, hacks | tier 1 | free | — |
| A cyberpsycho, a gang fight | fight | free | — |
| The street races (`sq024_*`) | own driving, a contest | free | `Races.reds` |
| A cab (Delamain), the NCART | ride the player called | free | Transit kinds 2 and 1 are not moments |
| Sitting on a bench, a bar stool, the player's own workspots | NoMovement? | verify | a moment would only be own (played): harmless |

## Phantom Liberty

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| Songbird's first call, the Blackwall holocall (`q301_holocall_blackwall`) | call, effects | free | Songbird's call keeps its own widget; its glitches reach a guest within 30 m (`StoryEffect`) |
| Dogtown's gate (`ep1_side_content`) | G | free | a guest whose story keeps it shut rides in the host's car; never carried nor regrouped across |
| Dogtown intro: the AV, Air Force One's crash | C, M | watch (a); own | the AV ride played if it has seats, else (c) at 30 m |
| Hole in the Sky: the crash site with Songbird, escorting Myers | S, R (walk), Songbird's effects | own | its free fights: free |
| Lucretia My Reflection: Reed's car, the bar | M, S | own | — |
| Firestarter: the Black Sapphire gala (weapons left at the door) | weapons-only zone, S in talks | free; own | the zone is mirrored as a restriction (`story.md`, Restrictions) |
| The gala's dances and talks with Songbird | S (3-4), private? | own; verify | if the scene resource is minor one-to-one it is private (never) |
| Firestarter: the stronghold assault (Kurt) and its boss | fight; C in its cutscenes | free; watch (a) | — |
| The Chimera fight (`q302_sc_02_chimera_*`) | fight; C at its phases | free; watch (a) | — |
| The server cores (`q303_server_core*`) | tier 1, devices | free | quest interactions: the host's cores |
| The Somi chase (Songbird's run across Dogtown) | R?, M?, S | own; verify | on foot it is played; a scripted vehicle leg without a seat is (c) at 30 m |
| Somewhat Damaged: Cynosure, the Cerberus chase (`q305_*` shutdowns) | R? X? (no weapons, slow walk) | own | the guest runs it beside the host under the same restrictions; verify that the chase's own scripted kills reach the guest's V |
| Cynosure's interior | P | free; watch (e) | a quest prefab: loaded for the guest, else (e) |
| Songbird and the Blackwall (Through Pain to Heaven, Leave in Silence) | S, E? | verify | (b) if the Blackwall sets a Cyberspace tag (`elsewhere.md`), else (a) in its cutscenes, own in its scenes |
| MaxTac (`q305_maxtac`) | fight | free | quickhacks are refused on them by the game itself |
| Endings: The Killing Moon, Unknown Destination, the shuttle (q307: the hub locked) | C, M | watch (a); own | — |
| Dogtown airdrops | fight, loot | free | — |
| Barghest checkpoints | weapons-only zone | free | — |
| Mr. Hands gigs: calls, car thefts, talks | calls; own driving; private talks | free | — |

## Counts

Rows by verdict (a row with two verdicts counts once, by its first part:
"own; watch (a)" is shared; "watch (a); own" spectated), 2026-11-21:

| Part                    | Shared (own, free) | Spectate (watch) | Unknown (verify) |
| ----------------------- | ------------------ | ---------------- | ---------------- |
| Prologue                | 8                  | 3                | 0                |
| Act 1                   | 13                 | 4                | 0                |
| Act 2 and the endings   | 14                 | 10               | 0                |
| Side quests, gigs, NCPD | 13                 | 3                | 1                |
| Phantom Liberty         | 16                 | 2                | 1                |

Never watched (private): 2 rows, the romance scenes and the Kerry boat.
Unknown: benches and the player's own workspots, the Blackwall's
sequences; and inside a shared verdict, to verify: Peralez's data dives,
the Nomad's chase, the gala's dances, the Somi chase, the VR tutorial's
replacer.

## Gaps

Big ones, for the user:

1. **A moment that takes the host far away** (reason c): built (2026-11-21, not run).
   The spectator's hidden V is carried along with the host's (`story.md`,
   "What watching does": the copy's trunk body slot in a vehicle, ground
   behind it on foot), so streaming, the NPC links (150 m, `regions.md`)
   and the listener follow; at the end it plays on there, the regroup
   behind a loading screen only a fallback. A ride of the moment that
   leaves without the guest is (c) at 30 m (it waited for 150 m). Open:
   whether the copy of an AV or a quest vehicle has the trunk body slot
   (else the 4 Hz cabin placement), and whether our capsule 3 m behind the
   host's puppet gets in an actor's way on foot.
2. **First person has no body**: built (not run). Only the host's
   puppet's head goes (meshes by name, head, eyes and face items): its
   arms, hands and weapon show. Open: which mesh names the puppet's head
   has (`story spectate parts`), and that the TPP arms, posed for third
   person, are mostly below the first-person view: an FPP rig on the
   camera driven by the host's `PoseState` would be the full answer.
3. **Signals still to read in game**: which of the Blackwall's sequences
   are another world or cutscenes, Peralez's data dives, how often a tier
   5 shot sits inside a tier 4 scene (each a short watch, 1.5 s held; the
   curtain keeps the host's view across a gap shorter than its 0.25 s), and
   whether the CameraAnimation tag (d) appears outside scenes at all; the
   VR tutorial's replacer; which quest vehicles have a seat for a guest.
4. **Played, not watched, means the guest's own V is in the scene**: a
   tier 2 to 4 scene, an escort, a chase on foot. Open: an NPC's scripted
   path through the guest's V (the restrictions hold it, the scene does not
   know it), and quest triggers that fire only on the host's V (the
   guest's arrival counts for nothing, as before).
5. **Sound**: answered from the scripts (engine facts §13.96): the
   listener is the player's, not the active camera's, unless a camera is
   activated with `shouldOverrideAudioListeners`; ours now is. With the
   carry (1) the V is near the picture either way.
6. **The curtain is ours, on the HUD layer**: a menu or photo mode that
   hides the HUD hides it too (the switch then shows); the game's own fade
   (a quest's `questFadeNode`) has no script lever found.

Small ones fixed with the feature: scene voices and the host's subtitles
and hubs reach a spectator wherever the moment went (`Scenes.reds`,
`Dialogue.reds`); the join, watch and ride offers are not shown while
watching; the party HUD shows a spectator "in a scene".

## In-game checklist

As a guest beside the host (two instances or a real second PC), each with
`log = debug`; each line: the log's `spectate:` lines and what the screen
shows.

- [ ] Scenarios: `spectate-guest`, `spectate-leave`, `spectate-host`,
      `spectate-ride`, `spectate-missing`.
- [ ] A tier 2 walk and talk (The Ride, Jackie): **never watched**; the
      guest walks along on its own V, sees the actors (scene replay), hears
      them, wears the host's restrictions.
- [ ] A cutscene in it (tier 5): watching starts within a second, says
      `(a) a cutscene`, the hint shows, the guest's V cannot move, its body
      is out of the picture, nobody shoots it; back on its own V as soon as
      the cutscene ends, the scene going on.
- [ ] Each switch is a short black (`curtain: built on the HUD layer` once),
      never a camera sweep; no `curtain: black too long`.
- [ ] A quest ride (the Delamain to Konpeki): ridden along, not watched.
      A full one (Dex's car, Panam and V): `(c)` once it is 30 m away, the
      log says `our V carried along in the host's vehicle` (and whether
      the trunk mount held); at the ride's end the guest's V is beside the
      car, playing, no loading screen.
- [ ] A quest teleport on foot: `(c)`, then the log's
      `our V carried along with the host's`, then its own V again beside
      the host at once.
- [ ] A quest teleport outside a moment, the guest beside: no offer on
      screen, the log says
      `travel: the host was taken from beside us: going along`.
- [ ] Sound: the moment heard from the host's camera, not from where the
      guest's V was.
- [ ] First person (the Konpeki penthouse, a tier 2 section): the picture
      is the host's eyes; no face or hair in it, its hands and weapon in
      view (`story spectate parts` names the head meshes); the whole
      puppet back when the view leaves it.
- [ ] A braindance and a netrun (The Information, Transmission): watched
      `(b)`; the guest's own V untouched after.
- [ ] Johnny's flashback (Arasaka raid): watched `(b)` to its end.
- [ ] The prologue: the VR tutorial (b), the montage (a), Jenkins'
      corridor (played), the lifepath's end (a).
- [ ] A private scene (a romance, a one-to-one gig talk): never watched.
- [ ] "Stop watching": the V moves at once, every hold off, the hint gone,
      not offered again until that reason ends.
- [ ] The host quits mid-moment: the guest's eyes, V and holds come back.
- [ ] On the host's screen: the spectating guest's puppet faded out, its
      NPCs not turning to it; back when it plays again.
- [ ] The moment list: `cmd story moments` on the host names its scene,
      phases and tracked path; a `moment_watch = scene:0x…` of the scene
      it plays makes the guest watch it `(d)`.
- [ ] Phantom Liberty: the AV crash (Dogtown intro: (a)), Reed's car
      (ridden along), Cynosure's chase (played), a Blackwall sequence
      (verify row), Dogtown's gate with a guest whose story keeps it shut
      (never carried across).
- [ ] Free roam, a fight, the host's own driving, a cab, the metro, a bench:
      never watched.
