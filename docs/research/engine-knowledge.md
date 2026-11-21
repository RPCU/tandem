# Cyberpunk 2077 co-op — knowledge harvested from Night City Online (NCO) and the campaign fork

Facts only, for a clean-room rebuild on game patch **2.50** (RED4ext C++ + redscript, Proton). No code
was copied; each fact names where it came from.

**Sources and abbreviations** (read on 2026-11-17; nothing below is copied code)

- `NCO` = Night City Online, https://github.com/ofmiceandcam98-eng/CyberpunkMP at `92474fc9e470`,
  a CyberpunkMP fork ported to patch 2.5. Paths written `cpmp/nco/...` or bare `code/...` are in
  that repository. Its own name for the game build: "2.5". Its history before 2026-11-01 ran on **2.31**.
- `CAMP` = the story co-op campaign fork (2.31 core + campaign plugin),
  https://github.com/ziomek123pl4-lang/CyberpunkMP at `f3b4df24435a` (`cpmp/campaign/...`).
- `CW` = Codeware (MIT), https://github.com/psiberx/cp2077-codeware at `60c4ab59035f`
  (2026-11-14) (`cpmp/codeware/src/...`) — the reference for what is known-good on 2.5.
- `addr` = the game's own `bin/x64/cyberpunk2077_addresses.json` from patch 2.50 (linker map
  2026-10-19). "in 2.50" means the hash exists in that list; most are unnamed there.
- `MAP` = NCO's `docs/MAP.md`.
- Commit hashes (8 hex) are NCO commits; their bodies are summarised in `nco-commits.md`; the
  campaign fork in `campaign-fork.md` (both next to this file).
- **Era**: game patch 2.5 shipped 2026-10-22 (`5e749fa6`); NCO's 2.5 port began 2026-11-01 (SDK
  vendored that day), so only commits from 2026-11-01 to 2026-11-10 (test.68…test.91) carry 2.5
  measurements. Everything in `MAP` dated August/September 2026 is 2.31 knowledge.

**Tags**: [2.5-verified] = NCO notes say it ran on 2.5, or CW uses it today; [2.31-only] = decoded/used
on 2.31 and not re-verified (or known different) on 2.5; [broken-on-2.5] = recorded as crashing/failing
on 2.5; [unverified] = built, never proven live; [2.5-untested] = read from the 2.50 game's own
scripts, not yet run in game; [to-hear] / [to-see] = built, only a person in the game can tell
whether it sounds or looks right; [native] = decided inside the engine, no script lever found.

**The 2.31→2.5 rule NCO learned the hard way** (`code/client/Game/CharacterCustomizationSystem.h:111-119`):
"Every raw offset, hand-numbered vtable slot and engine-struct mimic in the client was decoded on 2.31
and carried into the 2.5 SDK merge unverified - and on 2.5 they have overwritten a character, crashed
every client dressing a remote player, and crashed every client moving one." Address-library _hashes_
survived the patch (every hash both repos use is present in the 2.50 list); hand offsets and vtable
slots did not.

## Where each section is

The facts live in `engine/<File>`; a citation such as "§13.19" is
found through this table, each number once.

| §     | Title                                                                                   | File                |
| ----- | --------------------------------------------------------------------------------------- | ------------------- |
| 1     | Networking stack and protocol                                                           | `networking.md`     |
| 2     | Spawning remote player puppets                                                          | `puppets.md`        |
| 3     | Moving puppets — every method tried                                                     | `puppets.md`        |
| 4     | Animation                                                                               | `animation.md`      |
| 5     | Appearance / character customization                                                    | `appearance.md`     |
| 6     | Vehicles                                                                                | `vehicles.md`       |
| 7     | Quests / facts / scenes (campaign fork + NCO)                                           | `story.md`          |
| 8     | Combat / NPC / damage                                                                   | `combat.md`         |
| 9     | HUD / UI / archives / tweaks / saves / Steam                                            | `ui.md`             |
| 10    | Build gotchas (NCO, Windows/MSVC)                                                       | `build.md`          |
| 11    | Address-hash table                                                                      | `build.md`          |
| 12    | tandem's own measurements                                                               | `measurements.md`   |
| 12.1  | V's body for a remote player                                                            | `measurements.md`   |
| 13    | Reverse engineering of the running game                                                 | `re.md`             |
| 13.1  | RTTI, as the game holds it                                                              | `re.md`             |
| 13.2  | Animation: from an entity to its pose                                                   | `animation.md`      |
| 13.3  | Animation: where a frame's pose is produced                                             | `animation.md`      |
| 13.3b | The whole per-entity animation job                                                      | `animation.md`      |
| 13.4  | What V's entity is made of                                                              | `appearance.md`     |
| 13.5  | NCO's hand-numbered slots, checked against 2.50                                         | `re.md`             |
| 13.6  | Moving a vehicle we do not simulate                                                     | `vehicles.md`       |
| 13.7  | Naming NPCs across games                                                                | `npcs.md`           |
| 13.8  | Crashes that look like freezes, and three ways to cause them                            | `re.md`             |
| 13.9  | A guest's own NPCs off                                                                  | `npcs.md`           |
| 13.10 | Damage to an NPC another game owns                                                      | `combat.md`         |
| 13.11 | Host NPCs against a guest's puppet                                                      | `combat.md`         |
| 13.12 | Splitting a hit between the attacker's game and the target's                            | `combat.md`         |
| 13.13 | Calling a script static function from the plugin                                        | `re.md`             |
| 13.14 | Quickhacks between games                                                                | `combat.md`         |
| 13.15 | Quest phases: making a node wait                                                        | `story.md`          |
| 13.16 | Story levers in the scripts                                                             | `story.md`          |
| 13.17 | Attitude groups and a puppet's record traits                                            | `npcs.md`           |
| 13.18 | Fast travel and regroup                                                                 | `travel.md`         |
| 13.19 | Loot: corpses, dropped items, quest items                                               | `loot.md`           |
| 13.20 | Down, revive, respawn: the levers                                                       | `revive.md`         |
| 13.21 | An NPC's animation state, for a brain-off proxy                                         | `npcs.md`           |
| 13.22 | Every way a V moves: the signals, and NPC bodies in water                               | `puppets.md`        |
| 13.23 | Scenes: what kind, from the game's data                                                 | `story.md`          |
| 13.24 | A hit's whole outcome                                                                   | `combat.md`         |
| 13.25 | The main menu and the pause menu, for a co-op entry                                     | `ui.md`             |
| 13.26 | Other worlds: cyberspace, braindances, replacers                                        | `elsewhere.md`      |
| 13.27 | The NCART metro, rides and quest vehicles                                               | `transit.md`        |
| 13.28 | Firing an NPC's shot from a proxy                                                       | `combat.md`         |
| 13.29 | What a quest's completion is made of, and what quests test                              | `story.md`          |
| 13.30 | Devices and world objects                                                               | `devices.md`        |
| 13.31 | Per-player regions: brains, state, pins, crowd                                          | `regions.md`        |
| 13.32 | Traffic and world vehicles                                                              | `traffic-police.md` |
| 13.33 | Police: the prevention system                                                           | `traffic-police.md` |
| 13.34 | Traffic and police: open questions (need the game)                                      | `traffic-police.md` |
| 13.35 | Presence: a remote V seen and heard                                                     | `presence.md`       |
| 13.36 | The Relic tree's levers                                                                 | `combat.md`         |
| 13.37 | Phantom Liberty: the game's own check                                                   | `ui.md`             |
| 13.38 | Progression, economy, saves, items (levels, XP, vendors, trade)                         | `progression.md`    |
| 13.39 | Pings, tags, reveals and waypoints, for comms                                           | `ui.md`             |
| 13.40 | The scanner's slow time                                                                 | `time.md`           |
| 13.41 | Vehicle combat: the hooks and setters                                                   | `vehicles.md`       |
| 13.42 | Time dilation in a shared world                                                         | `time.md`           |
| 13.43 | The car radio and the pocket radio                                                      | `vehicles.md`       |
| 13.44 | Combat state, detection and stealth for a guest                                         | `stealth.md`        |
| 13.45 | Gameplay restrictions, and a guest wearing the host's                                   | `story.md`          |
| 13.46 | Takedowns, finishers, carried bodies and "the player" reactions                         | `stealth.md`        |
| 13.50 | Enemy scaling: the stats and how modifiers stack                                        | `combat.md`         |
| 13.55 | Does this game have a record, and its vanilla stand-in                                  | `mods.md`           |
| 13.60 | Breach Protocol: where a breach's outcome becomes effects                               | `combat.md`         |
| 13.65 | Interactions: where a choice goes, what casts the doer                                  | `story.md`          |
| 13.70 | World vehicles changing hands, the owner's turret, passenger combat in a copy           | `vehicles.md`       |
| 13.75 | A status effect's look, without its gameplay                                            | `combat.md`         |
| 13.80 | Vendors: what the shop's screen is bound to, from a proxy                               | `ui.md`             |
| 13.85 | Apartment interactions: what runs them, what they change                                | `apartments.md`     |
| 13.90 | The world's objects: a ping's beams, a turret's fire, physics destruction               | `devices.md`        |
| 14.1  | What a script can do                                                                    | `autopilot.md`      |
| 14.2  | Virtual input                                                                           | `autopilot.md`      |
| 13.91 | A V's movement noise, and the gates that ask for the player                             | `stealth.md`        |
| 13.92 | Contests: what the game's own data says of a fist fight, a race, a shooting competition | `story.md`          |
| 13.93 | Swimming's effects and sounds, a projectile put on a path                               | `presence.md`       |
| 13.94 | Placed NPCs: static ids, the ledger's hiding and bodies                                 | `regions.md`        |
| 13.95 | The overlap: the crowd's density in parts, null areas, a despawn probe                  | `regions.md`        |
| 13.96 | Spectating: what makes a scripted moment, a V the NPCs do not see                       | `story.md`          |
| 13.97 | An item taken from a slot still spawning it is destroyed                                | `presence.md`       |
| 13.98 | Two engine crashes on an entity gone: the transform update, the AI workspot's end       | `re.md`             |
| 13.99 | One journal change moves others: the engine's rules                                     | `story.md`          |
| 13.100 | The physics queue while the world is paused                                             | `time.md`           |
| 13.101 | Removing a car copy: the stub path, and what crashed                                    | `vehicles.md`       |

## Top traps (one line each)

1. Hand offsets/vtable slots from 2.31 broke silently on 2.5 (ccSystem+0x78, ITransactionSystem 0x250+,
   component+0x90 owner, render context); hashes survived. Fingerprint the build (class sizes) and gate.
2. Engine callbacks reached from redscript run on job threads — never touch your own world/ECS there.
3. Never put an epoch-ms tick in a float (131 s resolution); drop tick 0.
4. NPC-record puppets on 2.5 are owned by their AI/move component: transform writes and
   TeleportationFacility don't move them; use an engine move controller (MMC) or AI move commands.
5. `ExecuteFunction` with a null out-slot silently skips functions that return a value.
6. Replacing `prototype_hud.inkhud` goes stale every patch; one bad `.reds` disables every script.
7. Panam-derived records bring god-mode tags, name and AI — strip/blank all three.
8. Re-applying an identical appearance repeatedly crashes the observer; key by ccstate hash.
