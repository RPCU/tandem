# The shared world: time of day, weather, police, saves

## Inside the plugin

- **World** (`scripts/tandem/World.reds`): the host sends `World` (game
  time in seconds, the weather state's name, paused) on a weather change or
  a clock jump and every 10 s; guests set their clock to the host's when it
  is more than a minute off and blend to the host's weather over 10 s
  (Codeware's `WeatherSystem.SetWeather`). Verified 2026-11-18 with the game
  as a guest of `tandem-peer host --time 06:30 --weather 24h_weather_rain`.
  Open: a guest whose own quest pins its clock (the guest's quest phases
  are to be suppressed: milestone 6) is corrected again every 10 s.

## Open problems

- **The second player acts on the world too.** A guest is not a spectator:
  it shoots, hits and kills NPCs, pushes physics objects, opens doors, uses
  devices and terminals, loots, hacks, drives — and every other game sees
  the result. By the authority table these become requests to the state's
  owner (NPCs, devices, physics props, quest objects: the host), which
  applies them and replicates the outcome; the guest's own game shows its
  action at once (its V's animation, sound and effects) so it feels local.
  Physics objects (crates, bodies, cars hit) need an owner and a sync of
  their transform like vehicles; which ones (all props, or only those a
  player touched) is to decide. Done for takedowns, grapples, finishers
  and carried, thrown and disposed bodies (see `stealth.md`; to verify in
  game with scenarios `takedowns-host` and `takedowns-guest`). Still open
  there: the disposal container's own state (door, light, "used") stays the
  actor's until devices are synced, and loot from a disposed body.
- **NCPD chases.** The authority table gives police heat to the host, but a
  chase is about one player: who committed the crime, whom the units hunt,
  who escapes. The game's prevention system only knows its own V
  (`research/coop-quests-world.md` §9). Whatever the rule, police units are
  NPCs, so the host runs them all; a guest's crimes reach it as crime
  events; guests' own prevention is locked off (that lock is saved: remove
  it on leave) and their wanted bar shows what the host says
  (`UI_WantedBar`). **Undecided**: one wanted level shared by the group or
  one per player; chases when players are far apart; vehicle pursuits; what
  losing the police means for a group. Sirens, radio chatter and the wanted
  bar follow the "seen and heard" rule.
- **Saves** (decided 2026-11-18): **quest progress, NPC lives and the state
  of the city belong to the host's save; guests join it.** Whatever happens
  in a session (quests advanced, NPCs killed, doors opened, cars parked,
  time of day) is recorded by the host's game in the host's save. A guest's
  game neither saves that world nor brings its own: its world-state saving
  is locked while in a session, and leaving puts it back in its own world
  as its save had it. The guest's own, in its own save: its V (level, XP,
  perks, inventory, money, appearance, as the authority table says). Open:
  what a guest keeps of a session it played (loot it picked up, XP it
  earned: yes; quest progress: no, unless it plays that quest in its own
  game), and rejoining a host's save later.

  Option for later, when players play apart (region limits): **a world
  ledger on the server.** The simulation cannot leave the players' games
  (no headless game: NPC AI, physics and fights run only in a copy of
  Cyberpunk), but the server (the relay, grown) can hold the world's
  lasting facts: quest facts, the dead, what was looted or destroyed,
  heat, the clock and weather. It can also decide which game runs which
  region. Each game writes what happens around its V, and applies on load
  or on entering a region what it has not seen yet. Each player's save
  then keeps only their V. Cost: persistent storage, accounts, conflict
  rules between facts, and replaying facts into a game (quest facts via
  the quests system; deaths, loot and destroyed objects via their
  persistent states). Until then the host's save is the source.
