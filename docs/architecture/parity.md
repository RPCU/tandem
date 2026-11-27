# Parity: the same world on both screens, the game's own way

The bar (the user, 2026-11-23): both players feel they are in the same
world, and nothing feels hacky, in every part of the game and the mod.
Two audits: what two players can both look at (the world), and how each
feature presents itself (the mod). Then the checker that reads the first
one off two running games. Audited 2026-11-23 from code and docs at
`1ce3d6a6`; nothing in this file has been measured in game yet.

Status words: **same** (one owner, the others show its word), **partly**
(the state is shared, its look is each game's own, or some states
diverge), **different** (each game its own), _by design_ (decided so, the
row says where). "Checker" names the `tools/world-compare.py` category
that reads it, or why nothing can.

## The world: what both players see

Ranked by how much a player would notice (first: seen or heard every
minute; last: only side by side).

<!-- prettier-ignore -->
| # | What | Status | Owner, mechanism (evidence) | What a player notices | Checker |
| --- | --- | --- | --- | --- | --- |
| 1 | NPC voices: combat barks, pain, civilians' chatter and screams | partly (not run in game) | the owner's word on each line its NPC's script starts (`NpcVoice`, `npcs.md` "Voices"), said by every proxy's own voice; lines the engine starts without the script call are each game's (none on a proxy); scene lines are replayed (`scene-replay.md`) | — once heard; a bark the engine starts natively is missing on the partner's proxy | not readable (audio) |
| 2 | Traffic, parked cars, car colours and lights, traffic and walk lights, pop-in, one-sided collisions | partly | area owner's (`traffic.md`); traffic lights and the owner's looks and headlights on proxies are on branch `rides-b` (`TrafficLights`, `npc list`), being fixed on instances a/c | cars appear, differ in colour, lights disagree, a car hit on one screen only | `street-compare.py`, spawned |
| 3 | Corpses: where they lie and their pose | partly | a death is the owner's word; every game ragdolls its own proxy (`regions.md` §5: hit reactions and ragdolls "every game plays them on its proxies"); a ledger body is laid down with no death played (`regions.md` §14: "a place of death, not a ragdoll's pose") | the same body sprawled another way, metres apart after a blast, a fall or a car; a body that slid down stairs on one screen only | placed, spawned (position) |
| 4 | Physics props and debris: bins, crates, cones, cans, car parts, limbs | different | no owner, no sync (`world.md`, open: "which ones … is to decide"); each game's physics pushes its own; a destroyed car's parts fly per game (`traffic.md`, Explosions) | a bin knocked over on one screen stands on the other; debris lies elsewhere; the partner walks through a crate that is not there for it | not readable (no targeting component) |
| 5 | A player's summoned car (driving in, parked) | different | seen by the others "only once its owner sits in it" (`garage.md` row 1a, open) | the partner's car appears from nothing when they get in; while it drives up, nothing comes | spawned |
| 6 | Decals: blood, bullet holes, scorch | partly | each game draws its own: a puppet's shot is a real NPC shot on the partner's game, with its impact and decal (`presence.md` §3); a proxy's hit reactions are its owner's word, their blood each game's; scorch follows the explosion each game runs | holes roughly where the shots went; blood pools under bodies that lie elsewhere (row 3); a long fight's walls differ in detail | not readable |
| 7 | Police: units, cars, AVs, MaxTac | same (not run in game) | area owner's NPCs and world vehicles (`police.md`, The rule) | — once run | spawned, `npc-compare.py` |
| 8 | The wanted level shown | different (by design: heat per player) | each V's own prevention system (`police.md`); the partner's stars on its marker (police colours) and in its party entry (the game's star art), not run in game (`police.md`, "The partner's stars") | — once seen | heat (each its own) |
| 9 | Ambient trains, NCART | different (by design for a ride) | a rider's own train; elsewhere its puppet sits in a train of that world or is hidden (`transit.md`, The NCART metro) | the partner's train is another train; ambient trains pass at other moments | spawned |
| 10 | Gangs, quest NPCs, crowds | same when linked (150 m), else each its own | per NPC, by its owner (`regions.md` §1–6); the placed dead stay dead everywhere (§14); quest NPCs: host | same people within the link; far apart each its own, as solo | `npc-compare.py`, placed, spawned |
| 11 | Doors, gates, shutters; automatic doors | same (not run in game) | host's word, a guest's own copy first (`devices.md`); an automatic door opens per local V, as a request each time | — once run; a door that opens for the partner's puppet only by the host's word | devices |
| 12 | Elevators | same (not run in game) | host's floor; each V carried by its own game's cabin (`devices.md`) | — | devices (value: the floor) |
| 13 | Cameras, turrets | same (not run in game) | host's state; each copy senses its own V; a turret's aim and shots are the host's turret's, drawn on every copy (`devices.md`, "A device's fire") | — | devices |
| 14 | Explosive barrels, mines | same (not run in game) | host's word (exploded); the blast is each game's copy, hurting its own V (`devices.md`) | — | devices (exploded) |
| 15 | Explosions and fires: grenades, cars, barrels; burning cars | same moment, each game's own effect | a throw's copy ends where the real one ended (`combat-coverage.md`, Throws); a car's health and burning flag are state, its explosion runs once on every game (`traffic.md`); a grenade's ground fire is the copy's, hurting nobody | flames and smoke drawn per game: same place, not the same particles | spawned (wrecks: dead) |
| 16 | Glass, signs, breakable covers | same (not run in game) | a break is told once, replayed by trigger or impulse (`devices.md`, Glass); the session's record for who was not there | the pieces fall differently; a pane whole until the partner's V comes within 120 m | glass |
| 17 | Lightning and thunder; rain's build-up and puddles | partly | the weather state is the host's (`world.md`); a guest blends to it over 10 s while the host's own cycle took its own time; strikes are each game's random | a flash and thunder on one screen; puddles drying at another pace after a change | world (rain, puddles, moisture) |
| 18 | Time of day, sun, shadows | same | the host's clock; a guest set back only when more than 60 game seconds off (`World.reds`) | nothing; timetables (city lights, shop shutters) switch up to ~7 s apart | world (time, lights) |
| 19 | Weather, fog, rain | same | the host's weather state by name, kept against other mods (`World.reds`) | nothing once blended | world |
| 20 | NCPD scanner crimes, hustles, open-world activities | different | their quest phases keep running on each game (`story.md`: open-world phases are not frozen), each save its own done list; the NPCs on site follow rows 10 | a hustle the host finished still marked, and its scene set again, on the guest | not readable (quest state) |
| 21 | Radio in a car; music | partly | the driver's station (`vehicles.md`, Radio); no playback position, the song asked for best effort; combat music each game's own by its own detection | the same station, another moment of the song | spawned (radio) |
| 22 | Billboards, holograms, ad screens | partly | on, off, broken: host's word like any device; what they show and when they cycle: each game's | another ad on the same screen | devices (status) |
| 23 | Screens, TVs, radios in rooms | same (not run in game) | host's station (`devices.md`, `MediaDeviceControllerPS`) | — | devices (value: station) |
| 24 | Wildlife: the cat, birds | partly | an animal that is an NPC is shared as one (the apartment's cat: `activities.md`); flocks are not entities | birds fly up on one screen only | spawned (NPCs) |
| 25 | Vending machines, ATMs | partly | on, off, broken: host's; a sale is the buyer's (`devices.md`); the can a machine drops is on the buyer's game only | the partner's soda never lands | devices |
| 26 | Quest objects, scene props, interiors | same while grouped | host's quest prefabs loaded in a grouped guest's world (`story.md`, Quest prefabs); a guest's use of the host's objects asked of the host (Quest interactions) | — | placed, devices |
| 27 | Loot glow, containers, dropped weapons | different (by design) | each player rolls and loots their own (`loot.md`); a V's dropped items are shared | an emptied container still glows for the partner, as decided | not compared |
| 28 | Vendors' stock | different (by design) | each player's own shop (`progression.md`, Vendors) | — | not compared |
| 29 | Apartments: decorations, the gun wall | different (by design) | each player's own, from its own save (`activities.md`, V's homes) | a guest in the host's flat sees its own posters | not compared |
| 30 | Fans, signs, idle animations, steam | different | each game's loops | nothing (a loop's phase) | not readable |

What is left out on purpose: each V's own HUD and menus, the scanner's
view, time bubbles (`time.md`), a V's own body (`players.md`).

### The gaps, in order

1. **Proxies are mute** (row 1): built (`NpcVoice`, protocol 23, not run
   in game): the owner's script-started lines on every proxy's own voice.
   Left: lines the engine starts natively, and hearing it.
2. **Traffic and the street** (row 2): the other agent's work.
3. **Corpse poses** (row 3): the owner's body comes to rest; its rest
   pose (hips and the ragdoll's end) is not sent. A pose stream exists for
   scene actors (`ActorPose`, `scene-replay.md`).
4. **Physics props** (row 4): no owner today. A first step is the props
   a V or a car touched near both Vs, as a placed entity's transform.
5. **The summoned car** (row 5): share it as a parked vehicle from its
   arrival (`garage.md` 1a).
6. **The partner's stars** (row 8): built (its marker in the police's
   colours, its stars in its party entry; `police.md`, "The partner's stars"), not seen yet.
7. **Devices, glass, police** (rows 7, 11–16, 23): built, never run in
   game; the checker reads every one of them.
8. **Open-world activities** (row 20): a hustle's done state per save.

## The mod: every feature, the game's own way

The bar (the user, 2026-11-23): each feature up to the standard of other
co-op games, and built-in to the eye. One row per player-facing feature:
what it is today, its stand-ins (a blackboard written to fake a state, a
teleport for motion, a poll or timer for an event, a body deleted and
spawned again, a widget of ours where the game has one, a hidden or held
V), how comparable co-op games present the same thing (what a player sees
there, no code), and the engine's own way. Ranked by how hacky it feels
to a player, worst first. Run in game: the topic file's word, as of
2026-11-23.

<!-- prettier-ignore -->
| # | Feature | Today | Stand-ins (evidence) | Other co-op games | The engine's own way |
| --- | --- | --- | --- | --- | --- |
| 1 | Scenes, spectating (`story.md` Spectating, `scene-replay.md`) | near the host's scene a guest wears its restrictions and sees the cast posed and voiced; in a cutscene or another world it watches through the host's camera, its own V parked. Not run in game | a watch camera spawned and placed every frame; V `SetInvisible`, faded, held by restrictions and carried 4×/s (trunk mount or navmesh placement); the watch spot a teleport, `KeepOut` steps of 12 cm 5×/s; our black curtain over each switch; restrictions re-applied by a 4×/s poll; lines under stand-in subtitle ids | GTA Online heists and BG3 play a cutscene for every player at once, each from their own client, the players' own characters in it; Elden Ring sends a summoned player home before the host's cutscenes | the scene system plays a scene for the local V with its own camera; a guest's game can play the host's scene's camera track (`scene::CameraTrack`) in its own scene camera rather than a spawned entity; a fade is the game's `FadeOut` |
| 2 | Riding along, no free seat (`transit.md`) | a guest asked "Ride with NAME / Follow"; with no seat it is carried hidden in the cabin. Car rides with a seat seen in game 2026-11-22 | V hidden, crouched, invulnerable, held by restrictions, mounted to `trunk_body` or teleported into the cabin 5×/s (`carried by placement`); car copies teleported every frame by default; copies never deleted | GTA Online and Far Cry: a car's seats are its seats, a full car cannot be entered; nobody rides unseen | a seat or nothing: the scene's own follower logic (follow the host's car on its route, as the game's companions do), or the regroup at the ride's end |
| 3 | Dialogue choices (`story.md` Choices together) | the host's hub shown on a guest within 25 m, each player's cursor as a tag on the others' hubs, suggestions; only the host picks. Run 2026-11-21: no hub on either screen (the held hub's root hidden by the widget's own redraw: fixed, not run since) | a stand-in hub kept on the game's choice widget by two wraps (the game takes a hub it did not make off the `UIInteractions` board); suggestions as notices when no hub shows | BG3: others "listen in" (a bubble over the speaker), see the options and the choice live, and any party member can start their own dialogue; Divinity: the same, persuasion rolled by the speaker | the game's own choice widget draws the hub, its own selection the player's cursor, its own option tag the others' (no panel of ours since 2026-11-27); the hub itself from the guest's own interaction system would need a dialog visualizer of the game's (`gameinteractionsvisDialogVisualizerLogic`) fed the host's hub |
| 4 | Down and revive (`revive.md`) | down at zero health, no bleed-out; a partner holds F 10 s; out: respawn beside a partner or at Viktor's. The first two-game session saw a black screen while down | god mode keeps V alive; the death menu wrapped, the `dying` effect broken once a second; V turned off to NPC senses; the bar through `UI_HUDProgressBar`; in a car a prompt drawn by us; the respawn a teleport behind a loading screen | Borderlands: "Fight For Your Life" (crawl, shoot, a kill gives a second wind), the partner holds interact with a ring; Far Cry 5: a downed partner crawls and calls for help, revive by holding; Dying Light: the same | the game's own downed states (`Defeated`, the NPCs' defeated mechanic) and its Second Heart revive effect; an interaction choice on the body (the game's own, as `prompts.md`'s puppets) with the game's hold-to-use bar |
| 5 | Joining, arrival (`coop-menu.md`, `travel.md`) | a Co-op entry in the main and pause menus; the guest arrives 2 m behind the host behind a loading screen, "Joined NAME's session". Not run in game | the arrival a teleport behind a fast-travel loading screen (`TeleportHidden`); every restriction stripped then put back; the join code a HUD message; on a rejoin puppets and proxies deleted and spawned again | GTA Online: the sky-camera switch into the session; Borderlands, Far Cry: the partner spawns beside the host with a short intro; Elden Ring: the phantom materialises with its summoning effect and sound | the fast travel's own loading screen is right; the puppet could appear with the game's own spawn effect (the one `scene-replay.md` uses for Johnny) instead of popping in |
| 6 | Party HUD, nameplates (`presence.md` §4, §5, §7) | a list under V's health bar (name, health, down, distance, arrow), a name over each partner. Seen 2026-11-21 three times too large, rescaled, not run since | the list drawn by us from ink widgets on the HUD layer, its layout recomputed every frame plus a 2 s scan of hidden modules; plates through `DamageDigitsGameController`'s projection, else ours; a world point moved each tick for a body without a head slot | Borderlands: partners' portraits and bars at the screen's edge, names and levels over heads; Elden Ring: summoned players' bars top-left; GTA Online: names over heads, blips on the minimap | the game's own nameplate (`NameplateVisualsController`, as NPC names show) and its companion health bar (the follower's bar quests show), the minimap's own mappins |
| 7 | Hits on puppets and proxies (`combat.md`, `presence.md` §2–3) | a guest's hit is its own game's, applied by the NPC's owner; a puppet's hit reaction, partners' tracers. Hits verified 2026-11-18; outcomes and tracers not run | attack records our game lacks replaced (`Attacks.ForwardPush`); a puppet's hit shown by our twitch when its reaction does not take; tracers a real NPC shot with its hits dropped plus an fx trail; `proxy respawned`, `npc state polled` (2 s) fallbacks | every co-op shooter shows the partner's shots with their tracers and the target's reaction from the shooter's report | the hit reaction component's own reactions on the puppet (an `NPCPuppet` with its own hit reaction), the weapon's own trail; no twitch |
| 8 | Trade (`trade.md`, `prompts.md`) | "[F] Trade" or the phone, the stash screen as the table, eddies in a popup, the host commits. Off by default; the prompt seen on instance b 2026-11-22 | the storage screen opened by writing `StorageBlackboard.StorageData`, its transfer taken over; the partner's items made-up copies (`CreateItemData`); the commit resent every second; a drawn prompt when the game's does not take | Borderlands 3, Diablo: a trade window, both lock, both confirm; Elden Ring: drop the item on the ground | the storage screen is the game's own (right); dropping items is the game's too (`loot.md`'s shared bags) |
| 9 | Phone, holocalls (`comms.md`, `story.md` calls) | partners first in the contact list (call, trade, join, invite); a ring and the audio call view 8 s; the host's story calls ring on every phone. Not run in game | rows injected by wraps; the call widgets spawned into fields of ours with no call state; the avatar the tinted "unknown" one; the hang-up key a flag the tick reads | GTA Online: other players in the phone's contacts, a call rings as any NPC's | the phone system's own call (a `PhoneCallInformation` the game runs) and a contact entry of its own, the partner's face from its photo-mode or character portrait |
| 10 | Regroup, fast travel together (`travel.md`) | one player's fast travel offers the others "Join NAME" (15 s); a yes fast travels or hops them beside. Offers both ways seen 2026-11-19 | the offer a HUD message and an input hint; the fallback a teleport hop; the host's jumps found by polling (> 50 m); "taken along" under our own curtain | BG3: grouped characters travel together, an unchained one stays; Dying Light 2 and Far Cry: a partner far from the host's mission is brought to it after a countdown | the fast travel itself (`PerformFastTravel`) for every hop, its loading screen rather than a curtain; the offer as the game's own phone message or a choice on the fast travel screen |
| 11 | Vendors (`progression.md` Vendors) | "Trade" on the host's vendor opens our own game's vendor, our stock and money. Not run in game | our own vendor hidden behind the proxy, the shop opened on it; prompts drawn when the game's does not take | Borderlands, GTA: shops per player, the same counter | the vendor's own interaction on the proxy (`prompts.md`'s graft) opening `MenuScenario_Vendor` on our own vendor record, no hidden body |
| 12 | Notices, degraded (`degradations.md`, `strings.md`) | short HUD warning lines, 6 s apart; a status panel of what a game runs without. Not run in game | every notice a write to `UI_Notifications.WarningMessage`: the warning style for news (a join, a regroup offer) too | GTA Online: the feed ticker for joins and leaves; Borderlands: a chat line | the game's onscreen messages by kind: the warning for warnings, the `OnscreenMessage` / journal notification for news |
| 13 | Police (`police.md`) | heat per player; the area owner's units for a wanted guest. A car chase seen 2026-11-21 | the follower's spawns wrapped off; chase cars put at a found spot; every tick units set neutral to the owner's V (`TandemPolice.Spare`); unearned heat refused; units on foot when no car comes | GTA Online: a wanted player's blip flashes red and blue on everyone's map; police chase that player | the prevention system's own spawn and attitude per target; the partner's level on its map marker (police colours) and its party entry (the game's star art), built (`police.md`, "The partner's stars") |
| 14 | Stealth, takedowns (`stealth.md`) | takedowns played locally, validated by the host; detection per player. Verified 2026-11-18 (`stealth-guest`) | a grappled NPC held by a per-tick teleport when the mount does not take; holds ended by timers; the stealth meter a write to `UI_Stealth`; our V lent as instigator for player-only stimuli | Far Cry, Splinter Cell co-op: each player's own detection meter; the partner's takedown animation seen whole | the grapple's own mount (the game's), the detection meter fed by the senses themselves |
| 15 | Quickhacks, devices (`devices.md`, `combat.md`) | hacks run locally, forwarded to the owner; the host owns devices; "Hacked by NAME" marks. Not run in game | the mark our own mappin re-registered twice a second; a guest's turret copy held and firing drawn bullets; turret targets forced | Watch Dogs 2: a partner's hacks act on the one shared world, seen at once | device state as the host's word is right; the mark as the device's own highlight in the hacker's colour |
| 16 | Story sync, gates (`story.md`) | the guest's story frozen, the host's journal, calls, rewards and interiors mirrored; gates said as notices. Freeze passed 11 scenarios against the mock player 2026-11-21 | a node hook refuses the guest's nodes; prefabs polled once a second; quest interactions done again by the host's V | Far Cry 5 and 6, Borderlands: only the host's story moves; the guest keeps gear, XP and money, and is credited a mission it is at in its own story | the quest system's own phases, as built; the gate as the game's own blocked-area message |
| 17 | Saves on leave (`story.md` Saves, `progression.md`) | a guest never saves in a session; on leave its journal is restored and checked. Exit not run in game | three save locks, autosaves dropped; the pause menu's Exit wrapped and held | Elden Ring: the phantom goes home with its runes; Borderlands: XP and loot kept | the game's own save lock (`SaveLocksManager`) with its own "saving disabled" message |
| 18 | Loot (`loot.md`) | per player; quest items and iconics to every V; dropped bags shared. Not run in game | a proxy's loot rolled again the save-load way when its death rolled none; bag copies spawned from the dropper's puppet | Borderlands 3, Diablo: instanced loot, each player's own drops | as built: the game's own roll per V |
| 19 | Apartments (`activities.md` V's homes) | each player uses its own game's apartment; a bed asks everyone. Mostly built, to verify | the host's clock put back and moved again on a yes; a guest who owns the cat sees two | GTA Online: a guest invited in sees the owner's apartment | the host's apartment as the host's save has it would need its decorations' phase state; today the guest's own |
| 20 | Co-op menus (`coop-menu.md`) | the game's main and pause menus with a Co-op entry; a name popup. Not run in game | the pages are wraps of `SingleplayerMenuGameController`; their own clock is a looping ink animation | every co-op game: a multiplayer entry in its own menus | as built: the game's own menu pages |

### The top rows, next

1. **Spectating** (row 1): a guest's own scene camera playing the host's
   camera track, its own V kept out of shot by the scene's own means.
2. **Carried along** (row 2): drop it for "follow", with the regroup at the
   ride's end; ride only in a seat.
3. **Dialogue** (row 3): run `dialogue-marks-guest` and
   `dialogue-marks-host` with the fixed hold (the 2026-11-21 run showed no
   hub: the redraw hid it).
4. **Down and revive** (row 4): the black screen first; then the game's
   defeated state and its own interaction bar.
5. **Arrival** (row 5): the puppet in with the game's spawn effect.

## The checker

`tools/game.sh cmd parity [M]` (default 100 m; `tools/instance.sh cmd
NAME parity`): each game writes what it shows around every V it draws, its
own and each puppet, read from the game's own systems
(`scripts/tandem/Parity.reds`, routed through `TandemTestQuery.Run` so
`RunCommand` does not grow): the world line (clock, weather, rain,
puddles, the city's lights), our V's heat, every entity the targeting
system finds within M m (its static or dynamic id, class, record, look,
position, dead or alive, a vehicle's radio station, our ledger bodies),
every device the session follows (`TandemDevices.ParityList`: status,
state bits, value), the glass record, then `street list` and `npc list`.

`tools/world-compare.py A.log B.log`, or `--instances a c` (asks both at
once, `--shots NAME` takes a screenshot of each at that moment), sets the
last dump of each side by side and prints one line per difference, by
category: world, placed (static ids: one side only, dead on one, apart
more than `--apart`, another look or station), spawned (dynamic ids paired
by class and record within 5 m: those with no twin), devices (bits named;
the scanner's "exposed" left out: each V's own), glass; then runs
`street-compare.py` and `npc-compare.py` on the same dumps. It exits 1 on
any difference. What it cannot read (audio, decals, particles, physics
props without a targeting component, flocks, quest state) is marked "not
readable" above.

## Open

- Not yet run in game: the targeting query's reach (which classes it
  returns at 100 m; item drops and props are expected to be missing) and
  the three situations to measure (on foot together, riding, fighting
  near devices).
