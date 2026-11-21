# Scripted moments: the audit

What spectating (`story.md`, "Spectating") catches in the base game and in
Phantom Liberty, read from the scripts and the quests' known structure
(2026-11-21), **nothing run in game**: every row is a guess until its line
of the checklist below is ticked. The detection is one rule for both games
(no quest list, nothing of EP1's): the host's V is in a moment while it is
in a scene of tier 2+ (**S**), under quest restrictions on how it moves
(**R**), in a ride its scene drives (**M**), in a scripted body or camera
mode (**X**) or in another world (**E**: cyberspace, a braindance, playing
as Johnny); never in a private scene, a fight or its own driving.

Verdicts: **caught** (a guest beside the host watches), **free** (not a
moment, rightly: both play), **missed** (should be watched, is not),
**wrong** (watched, should not be), **verify** (depends on a signal not
read yet).

## Base game: main story

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| The Rescue: Sandra carried out | S (carry scenes), X | caught | — |
| The Pickup: Royce's talks; the fight after | S; fight | caught; free | — |
| The Heist: Delamain ride to Konpeki | M, S | caught | — |
| The Heist: Konpeki infiltration with Jackie and T-Bug | tier 1, walks | free | the guest sneaks with them on its own V; its acts on the host's objects go through quest interactions |
| The Heist: the penthouse, Saburo and Yorinobu | S (4-5) | caught | — |
| The Heist: the escape, the AV crash, the Delamain | S (4-5), M | caught | — |
| Playing for Time: V wakes, the walk home | S, X | caught | — |
| The Arasaka raid flashback (Johnny) and every Johnny takeover | E (replacer) | caught | the host's puppet still shows V, not Johnny (`story.md`, Johnny) |
| Braindances: Evelyn's (The Information), Judy's, River's | E (braindance) | caught | the BD editor's own camera is the host's active camera: sent as is |
| Netrunning: Alt with the Voodoo Boys (Transmission), I Walk the Line | E (cyberspace), S | caught | — |
| Hellman's interrogation, Takemura's van, scenes in chairs | S (3-4) | caught | — |
| Play It Safe: the parade, the float | S, R (walk) | caught | parts at tier 1 between scenes come and go; the 1.5 s hold covers a gap, not a section |
| Car chases with an NPC driving (Takemura, Panam in Ghost Town) | M (Scene ride) | caught | — |
| Car chases with V driving | own driving | free | the guest rides as a passenger of the copy, as now |
| Passenger combat in an NPC's quest car | M only if the PSM says Scene | verify | if its vehicle state is Combat (tier 1), missed: read the quest's `VehicleScene` tag in game |
| Panam's Basilisk (Riders on the Storm, Queen of the Highway) | S? (gunner seat) | verify | a turret seat in a quest vehicle may be tier 1 + Combat: missed then |
| Endings: Nocturne Op55N1, Mikoshi | E (cyberspace), S (5) | caught | — |
| Endings: Totalimmortal (Johnny's raid), Panam's AV, the Sun, the Star, the Devil | E, M, S | caught | — |
| Credits and epilogue calls | S (5), calls | caught | calls stay everyone's (`Phone.reds`) |

## Base game: side quests, gigs, NCPD

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| Judy: Pyramid Song, the dive | S on the boat; swimming at tier 1, weapons away | caught; free | the dive is free swimming: both swim, rightly not watched |
| Judy: Both Sides Now, the BD | E | caught | — |
| Panam: Ghost Town, Life During Wartime | M, S; fights free | caught; free | — |
| River: Following the River (the farm, the Ferris wheel) | E (BD), S | caught | the farm's search is tier 1: free |
| River: The Hunt, its car chase | M, S, E | caught | — |
| Peralez: Dream On (the apartment, the data dives) | S, E? | caught; verify | a data dive that is a scene camera, not cyberspace, is S only |
| Kerry: A Like Supreme (on stage), Holdin' On | S (workspots in a scene) | caught | — |
| Kerry: Rebel! Rebel! (V drives) | own driving | free | — |
| Romance scenes | private | never | by design (`story.md`, private scenes) |
| Gigs: the fixer's call, the talk on arrival | calls; private one-to-one scenes | free | private scenes are their player's |
| Gigs and NCPD hustles: fights, stealth, hacks | tier 1 | free | — |
| A cyberpsycho, a gang fight | fight | free | — |
| A cab (Delamain), the NCART | ride the player called | free | Transit kinds 2 and 1 are not moments |
| Sitting on a bench, a bar stool, the player's own workspots | NoMovement? | verify | if a player-started workspot puts NoMovement on V, **wrong**: tell it by the workspot's activity (`activities.md`) |

## Phantom Liberty

<!-- prettier-ignore -->
| Moment | Signals | Verdict | Fix or gap |
| --- | --- | --- | --- |
| Dogtown intro: the AV, Air Force One's crash | S (5), M | caught | — |
| Hole in the Sky: the crash site with Songbird, escorting Myers | S, R (walk), Songbird's effects | caught where restricted | its free fights: free |
| Lucretia My Reflection: Reed's car, the bar | M, S | caught | — |
| Firestarter: the Black Sapphire gala (weapons left at the door) | weapons-only zone, S in talks | free; caught | the zone is mirrored as a restriction (`story.md`, Restrictions), not watched |
| Firestarter: the stronghold assault (Kurt) and its boss | fight; S in its cutscenes | free; caught | — |
| The Chimera fight | fight; S at its phases | free; caught | — |
| Somewhat Damaged: Cynosure, the Cerberus chase | R? X? (no weapons, slow walk) | verify | if only weapons are taken: missed (a weapons-only restriction is not a moment); read the chase's restrictions in game |
| Songbird and the Blackwall (Through Pain to Heaven, Leave in Silence) | S, E? | verify | whether the Blackwall sets a Cyberspace tag is open (`elsewhere.md`); its scenes are S |
| Endings: The Killing Moon, Unknown Destination, the shuttle | S (5), M | caught | — |
| Dogtown airdrops | fight, loot | free | — |
| Barghest checkpoints | weapons-only zone | free | — |
| Mr. Hands gigs: calls, car thefts, talks | calls; own driving; private talks | free | — |

## Gaps

Big ones, for the user:

1. **A moment that takes the host far away.** The spectator's own V stays
   parked where it was: its game streams the world, and links with the
   host's NPCs (150 m, `regions.md`), around that V, not around the
   camera. A ride or a quest teleport hundreds of metres away shows an
   empty or unstreamed world through the host's camera. Proposed: the
   parked body follows the host's V, hidden (a carry like `Transit.reds`'
   ride along, or a teleport behind each host jump), so streaming, the
   links and the regroup at the end come with it.
2. **First person has no body.** The host's puppet is hidden while the
   view sits in it: the spectator sees no arms, hands or weapon. Proposed:
   the host's first-person arms (its `PoseState` on an FPP rig attached to
   the camera), or the puppet with its head hidden.
3. **Signals still to read in game**: passenger combat in an NPC's quest
   car and the Basilisk's turret (tier and PSM), Cynosure's chase,
   the Blackwall's sequences, and whether a player's own workspot puts
   NoMovement on V (the one possible wrong catch).
4. **Sound**: the spectator's listener is its game's active camera (the
   one we activate) if the engine follows it; its V's own world sounds
   stay its world's. Unknown until heard.

Small ones fixed with the feature: scene voices and the host's subtitles
and hubs reach a spectator wherever the moment went (`Scenes.reds`,
`Dialogue.reds`); the join, watch and ride offers are not shown while
watching; the party HUD shows a spectator "in a scene".

## In-game checklist

As a guest beside the host (two instances or a real second PC), each with
`log = debug`; each line: the log's `spectate:` lines and what the screen
shows.

- [ ] Scenarios: `spectate-guest`, `spectate-leave`, `spectate-host`.
- [ ] A tier 2 walk and talk (The Ride, Jackie): watching starts within a
      second, the hint shows, the guest's V cannot move, its body is out of
      the picture, nobody shoots it.
- [ ] A quest ride (the Delamain to Konpeki): watched all the way; the
      guest joins the host where it ends (more than 60 m).
- [ ] First person (the Konpeki penthouse, a tier 2 section): the picture
      is the host's eyes, the host's puppet hidden, back when the view
      leaves it.
- [ ] A braindance and a netrun (The Information, Transmission): watched;
      the guest's own V untouched after.
- [ ] Johnny's flashback (Arasaka raid): watched to its end.
- [ ] A private scene (a romance, a one-to-one gig talk): never watched.
- [ ] "Stop watching": the V moves at once, every hold off, the hint gone,
      not offered again until the next moment.
- [ ] The host quits mid-moment: the guest's eyes, V and holds come back.
- [ ] On the host's screen: the spectating guest's puppet faded out, its
      NPCs not turning to it; back when it plays again.
- [ ] Phantom Liberty: the AV crash (Dogtown intro), Reed's car, Cynosure's
      chase (verify row), a Blackwall sequence (verify row).
- [ ] Free roam, a fight, the host's own driving, a cab, the metro, a bench:
      never watched.
