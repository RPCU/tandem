# Pause, slowed and stopped time

## Inside the plugin

- **Pause** (`scripts/tandem/Pause.reds`): the pause menu pauses everyone.
  Our menu opening and closing is caught where vanilla pauses for it (wraps of
  `PauseMenuBackgroundGameController.OnInitialize/OnUninitialize`; the
  death menu shares that background and is skipped) and sent as `Pause`
  (reliable). The host stamps the sender, keeps who is paused (late joiners
  get it; a player who leaves is no longer paused) and relays it. While any
  other player is in their pause menu, a game pauses its world as the pause
  menu does (the system requests handler's `PauseGame`, Codeware's
  `GameInstance.GetSystemRequestsHandler`) without opening its own menu,
  shows "NAME paused the game" (the HUD's `UI_Notifications.WarningMessage`)
  and resumes when nobody is paused. Scripts' delayed tick stands still
  while paused, so the plugin calls `TandemPause.Frame` every frame from
  RED4ext's running game state (`plugin/src/main.cpp`). Not yet verified in
  game (scenario `pause`).
- **Slowed time** (`scripts/tandem/Time.reds`, `core/src/timebubble.cpp`):
  each game sends its V's time abilities when they change (`TimeEffect`:
  Sandevistan, Kerenzikov, slow-motion perks, scanning, and the world speed
  the implant asks for; reliable, kept by the host for late joiners, ended
  when a player leaves). Every V with a Sandevistan or Kerenzikov on has a
  **time bubble**: the NPCs within 150 m of it are slowed by its factor, for
  everyone. The host slows its real NPCs with the per-entity dilation
  (`SetIndividualTimeDilation`, the local global factor ignored); guests
  see it through their proxies' motion (same bubbles computed there, same
  core code, but entities tandem's own movement controller drives, proxies
  and puppets, get no per-entity dilation). Membership is sticky until the
  bubble ends; an NPC in several takes the smallest factor; when a bubble
  ends its NPCs go back together. The owner keeps its own single-player
  slow motion, except the host, whose global factor is taken back within
  the frame (re-set at normal speed under the same reason, so the state
  machine carries on) unless `time_host = full`, where the host exempts the
  NPCs it shares instead. It runs from `TandemSystem.Tick` once the world
  plays, never from the plugin's per-frame call: there it crashed the game
  loading a save and froze it right after "playing" (2026-11-18; suspects
  in engine-knowledge §13.15). Scenario `time`.

## Open problems

- **The pause menu pauses everyone; nothing else does.** The pause menu
  (settings, save/load, quit) stops the world, the NPCs and every V
  together for every player, each seeing who paused it, until nobody is in
  it. Every other menu that stops time in solo play (inventory, perks and
  attributes, cyberware, crafting, map, journal, messages, shards,
  wardrobe, vendors) does **not** pause in co-op: the others keep playing
  while one levels perks or changes weapons, as in other co-op games. The
  player in such a menu stays in the world, vulnerable, and the others see
  them standing there (later: a small "in menu" sign over their V).
  Scanning slow-motion, braindances and the like follow their own notes.
  Done (2026-11-18, to verify in game with scenario `pause`): the pause
  menu, see Inside the plugin. (`World.timePaused` still carries only the
  host's own time-system state.) Still to do: the other menus keep pausing
  their own game (vanilla:
  `MenuHubGameController`/`RadialMenuHubGameController` set the `hubMenu`
  time dilation profile and the `HubMenu` menu mode; `TimeDilationHelper`
  gives up when `IsMultiplayer()`), and the "in menu" sign.
- **Slowed and stopped time.** The single-player game bends time for one V,
  who in a shared world is not alone. Everything that does it needs a
  deliberate co-op rule:
  - _time dilation_: Sandevistan, Kerenzikov, scanning with the Kiroshi
    optics (and perks that slow time while scanning or aiming), slow-motion
    finishers, quickhack breach timers, dodge/slide slow-mo;
  - _another timeline_: braindances (the player is in a recording, not in the
    world), Johnny flashbacks, the Relic sequences;
  - _skipped time_: sleeping, waiting, fast travel, phone-call time skips
    (done for the wheel, beds and the host's story: asked of everyone,
    `activities.md`);
  - _stopped time_: the pause menu, inventory, map, character and perk
    screens, crafting, the wardrobe, photo mode, tutorials and popups —
    single-player pauses the whole world for all of them.

  Done (2026-11-18, not yet run in game; scenario `time`, unit tests in
  `tests/timebubble_test.cpp`): the rule per mechanism, in
  `scripts/tandem/Time.reds`.
  - _Sandevistan, Kerenzikov_: a **time bubble** around the V using it
    (Inside the plugin). The shared world slows only where it matters, the
    enemies around that V, really and for everyone, so the ability keeps
    both its purposes: more actions per enemy action, and more real time
    to aim. Its hits go to the host at real time, as always. The host
    cannot slow only its view of its own NPCs (the time system has global,
    local-player and per-entity scopes, no render-only one:
    `research/engine-knowledge.md` §13.15), so as the user it keeps its
    world at normal speed and its bubble does the slowing (default), or
    with `time_host = full` keeps the whole slow motion and exempts what
    it shares (experimental: its projectiles, physics and clock still
    slow). Kerenzikov is the same, shorter; its V keeps its own player
    factor.
  - _What the others see_: the NPCs around that V slow down; the V itself
    moves at its normal speed among them (never slowed, nor sped up), with
    the implant's effect played on its puppet (effect name to confirm in
    game). Two Vs in a Sandevistan at once: one bubble each, an NPC in
    both takes the stronger factor; each sees the NPCs slowed and the
    other V at normal speed; when one ends, its NPCs go back unless the
    other's bubble holds them.
  - _Scanner and quickhack panel, Focus, Deadeye's reload, Berserk, melee
    hit stop, deflect_: local only, no bubble. A guest keeps them on its
    screen; the host drops their world part. Others see the V as it is
    (standing, scanning).
  - _Second chance_ (its dilation is in TweakDB, reason unknown): any
    other dilation left on the host is logged to find it.
  - Breach timers and network dive: not handled yet (owner-local, real
    time, `research/coop-time.md` §2.7).

  Since then (2026-11-18, same state: not yet run in game):
  - _Perks and implants that slow time_ (Focus, Deadeye's reload, and
    every `SetTimeDilationEffector`: Reflex Tuner, Synaptic Accelerator,
    second chance) make a bubble with their own factor too, so they keep
    their purpose; only the scanner, hit stop and deflect stay local.
    Everything tied to slowed time was read for what still holds for its
    user (`research/coop-perks.md` §5.1a): nothing of the game's is
    changed, the user's own checks see what single player shows.
  - _A bubble slows the other Vs in it_: each game slows its own V (the
    player's own time factor, as Kerenzikov gives it; the per-entity
    dilation if that one counts as an active dilation) by the strongest
    bubble around it that is not its own, divided by its own ability's
    factor. No ability: as slow as the NPCs, the owner looks very fast.
    The same or a stronger ability: not slowed, in the bubble on an equal
    footing. A weaker one: slowed by the ratio. Eased in and out with
    Kerenzikov's curves. Nothing new on the wire: every game has the
    `TimeEffect`s and where each V is. Decided: a slowed V's shots and
    hits need nothing more (its game slows them); a V in a vehicle, dead
    or downed is not slowed; three bubbles: the strongest; host in a
    guest's bubble and the reverse are the same rule; the factor is
    always given back when the session or the world ends. Open: no native
    sound or screen cue exists for being slowed (vanilla never slows V
    from outside), and leaving a bubble on foot is not sticky (a V at its
    edge can flicker between speeds: add a margin if seen).

  And then (2026-11-18, the user: unchanged for the ability's user):
  - the host keeps the game's own global dilation too (`time_host = full`
    is the default), so its fire rate, duration, effects and sound are
    single player's; the NPCs it shares get their bubble's speed whatever
    its own factor, and its NPC updates go out on real time;
  - a bubble reaches 150 m (every NPC a game shows), a V leaves it 15 m
    farther out than it enters;
  - the scanner's slow motion is off while another V is in the session;
  - a slowed NPC's bullets fly at its speed (per-entity dilation on the
    projectile, and the game's own projectile attack chosen for it where
    no dilation is active);
  - **NPC shots at a guest are simulated by that guest's game**
    (`scripts/tandem/Shots.reds`, authority row "NPC shots at a V"),
    because dodging bullets under slow motion only feels like single
    player if the game that draws the bullet decides the hit. The host's
    NPC still decides to fire (when, at whom, weapon, attack, its
    time-between-hits coefficient) and sends `NpcShotAt` to that guest
    only, from the one `AIWeapon.Fire` wrap that also sends the cosmetic
    `NpcShot` (`NpcAnims.reds`) to every other guest: the target guest is
    left out of the cosmetic one (its proxy fires for real, the endpoint
    pairs the two). The host still fires for its own view, but its hit on
    the puppet is dropped. The guest's game fires the shot from its proxy
    of the NPC at its own V (the player-side miss rules, the weapon's
    spread, a slow projectile under a bubble or its own Sandevistan),
    decides the hit and applies it through its own pipeline (attacker
    side from the proxy, which has the NPC's record and weapon and plays
    at the host's difficulty; the proxy turns hostile to our V when it
    fires at it, so the hit is not dropped as friendly fire); a hit is
    reported (`ShotHit`) and the host plays the hit reaction on the
    puppet. The shot reaches the guest about RTT/2 late: its projectile
    runs faster for 0.25 s to catch up (`TimeBubbles::ShotCatchUp`), so
    the guest dodges against the host's timeline. Melee, explosions and
    NPCs the guest has no proxy of stay the host's (`PlayerHit`). Cheating
    is out of scope (co-op among friends: a guest's game could ignore
    hits). Scenario `npcs-shoot-guest`; not yet run in game;
  - a guest's lethal hit kills the proxy at once, so kill extensions fire
    as in single player; the host's word reconciles;
  - an enemy answering with its own Sandevistan is left out of bubbles
    while its AI does so.

  To verify in game: the bubble NPCs slow on host and guest alike, that a
  factor of 1.0 keeps Sandevistan going on the host (else 0.999 is used),
  the effect on the puppet, the projectile bullets of NPCs while a held
  dilation is active, and how a guest's proxies look under its own slow
  motion.

  Direction to start from: the world (host) never slows or stops for one
  player. A V's own time effects are local to that player's game and seen by
  the others as what they are (that V moving very fast, or scanning); the
  rest of the world keeps running on the host's clock, with a co-op stand-in
  for what the effect gives the player (for example, Sandevistan speeds that
  V up for everyone instead of slowing everyone else). Menus don't pause a
  co-op world. Story moments that move everyone (braindances, flashbacks,
  time skips) follow the host, with the guests brought along. Every effect
  must be listed, decided and tested; the authority table gets a row for
  each.
