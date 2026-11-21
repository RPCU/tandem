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
  game (scenario `pause`). A co-op pause has no time limit; while it lasts
  the physics system's command queue is watched (it is not drained while
  the world is paused and overflows past 0xFFFF entries): its growth is
  logged, and near the limit the pause is lifted for a few frames so the
  physics drains it (`plugin/src/engine/physics.cpp`, behind a byte check
  of the 2.50 code; engine facts `research/engine/time.md` §13.100).
- **Slowed time** (`scripts/tandem/Time.reds`, `core/src/timebubble.cpp`):
  each game sends its V's time abilities when they change (`TimeEffect`:
  Sandevistan, Kerenzikov, slow-motion perks, scanning, and the world speed
  the implant asks for; reliable, kept by the host for late joiners, ended
  when a player leaves). Every V with a Sandevistan or Kerenzikov on (or a
  slow-motion perk or implant, or its scanner: below) has a
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
  in engine-knowledge §13.42). Scenario `time`.
- **A mod's slow motion is a bubble too** (`Time.reds` `Unexplained`,
  core `TimeBubbles::UnexplainedSlow`, 2026-11-19, not yet run in game):
  any global slow motion on our V's game that none of the reasons above
  explains (the whole factor clearly below the smallest of theirs) is sent
  as a slow-motion ability (`kTimeSlowMo`) with that factor, so the others
  see a bubble, and our player reads once "A mod slowed time: shared as a
  time bubble around you". The reason cannot be named: the time system's
  natives cannot be wrapped and no script lists the active reasons, so the
  rule needs no list of mods (OverclockOverheat's optional
  `OverclockDilation` asked for it, [`mod-compat.md`](mod-compat.md)). The
  exceptions stay as they were: melee hit stop (`meleeHit`,
  `meleeHitStrong`) and deflect local, menus (`WorldMap`, the TweakDB
  profiles' reasons of `radialMenu`, `hubMenu`, `vehicleRadioMenu`,
  `nanoWireBreach`, `settings`; a shop's or the stash's screen, the
  vendor hub menu, whose 0.05 has no reason scripts can name: while
  `TandemVendors.ScreenUp`, seen in game 2026-11-21), a ride's skip (`customFFTimeDilation`, a
  speed-up) and our V slowed by another V's bubble (`tandemBubble`) are
  never taken for one. A host cannot take it back (no reason to hold): its
  world stays slowed and the NPCs it shares are exempted, as with
  `time_host = full`. Tests: `tests/timebubble_test.cpp`.
- **The scanner's slow time is a bubble too** (`scripts/tandem/Time.reds`,
  2026-11-19, not yet run in game; the user: "the scanner has a bubble
  slow time too so it's standard"). It was switched off while another V
  was in the session before; now the scanner's own dilation (reason
  `focusMode`) makes a bubble exactly as a Sandevistan's does, through the
  same path: `TimeEffect` with `kTimeScanning`, a bubble ability since
  protocol 7 (`proto::kTimeBubbleAbilities`); the same reach
  (`TimeBubbles::kRadius`, 150 m: in single player the scanner slows the
  whole world, as the Sandevistan does, and one reach for every bubble
  keeps the rules one), the same sticky membership, the same slowing of
  the other Vs inside it (`PlayerFactor`).
  - _Its factor_ is the one the game sets, read as it sets it
    (`GetActiveTimeDilation(n"focusMode", true)`) and never changed.
    Nothing in a build changes it: the scanner's slow motion is one
    TweakDB profile, the same for every V, in combat or not, with no stat,
    perk or implant on it (`research/engine/time.md` §13.40). So there is
    no build to scale anything by, and nothing to keep: what the scanner
    gives in single player it gives here.
  - _When_: whenever the game's scanner state slows time, which is only
    while an enemy or a sensor (camera, turret) is in view (its TweakDB
    flag), never in a vehicle or a takedown, and only while no other
    dilation is on. The quickhack panel adds no slow motion of its own:
    picking a quickhack is the same one.
  - _The scanning V_ keeps the scanner's slow motion on its own screen, as
    in single player: a guest always; the host with `time_host = full`
    (the default), its shared NPCs set to the bubble's speed whatever its
    own factor; with `time_host = bubble` the host holds it at normal
    speed under its own reason, as the other abilities, and the bubble
    alone slows the NPCs around it.
  - _The others_: the NPCs around the scanning V slow, for everyone; a V
    within the reach with no ability of its own is slowed with them (its
    own game, `SlowSelf`), and sees the scanning V move at its own speed;
    a V scanning too stands in it on an equal footing; a V in a vehicle,
    dead or downed is not slowed.
  - _Slow time cancels slow time, by strength_ (the user, 2026-11-19: "the
    slow time bubble still should be cancelled out by other slow time
    (depending on slowtime power), for example sandevistan or if the other
    player scan too"). A V's scanner is its own ability like a
    Sandevistan, on the host and on a guest alike: each game computes its
    V's `own` and the others' bubbles through one core rule
    (`TimeBubbles::BubbleScale`, native `TimeBubbleScale`), and a V is
    slowed by the strongest other bubble around it divided by its own
    ability's factor, never below that and never sped up
    (`TimeBubbles::PlayerFactor`). The slow is that ratio alone, not the
    product: the scanner and the Sandevistan exempt their own V from their
    global dilation, and ours goes on the player's own factor under its
    own reason (`tandemBubble`), which their states never set or unset.
    The NPCs between two bubbles take the stronger one. With a scanner at
    0.2 (its real factor is not read yet):

    <!-- prettier-ignore -->
    | A | B | A slowed to | B slowed to | NPCs near both |
    | --- | --- | --- | --- | --- |
    | scans (0.2) | nothing | 1 | 0.2 | 0.2 |
    | scans (0.2) | Sandevistan 0.1 | 0.1 / 0.2 = 0.5 | 1 | 0.1 |
    | scans (0.2) | Sandevistan 0.5 | 1 | 0.2 / 0.5 = 0.4 | 0.2 |
    | scans (0.2) | Kerenzikov 0.4 | 1 | 0.2 / 0.4 = 0.5 | 0.2 |
    | scans (0.2) | scans (0.2) | 1 | 1 | 0.2 |

    Locked by `tests/timebubble_test.cpp` (the table) and
    `tests/endpoint_test.cpp` (a host and a guest both scanning, each
    weighing the others' `TimeEffect`s against its own), in game by
    scenario `time-scan`. To read in game: whether a scanner profile that
    slows the player too (`applyTimeDilationToPlayer`) adds its own player
    factor to ours, and how the two combine (`time report` logs what our
    V's speed reads, `GetTimeDilationValue`); and a vanilla clean-up 1 s
    after the scanner closes (`CleanUpTimeDilationEvent`, final.reds
    43307-43318) that sets a player factor of 1 for 0.1 s under no reason,
    which could cut into ours for that long.

  - _Measured for the partner's side_: the log lines
    `time: our scanner's bubble ran N s` (when one ends) and
    `time: our V back to its own speed after N s`; `time report` gives
    this session's scanner bubbles and how long our V was slowed by the
    others' bubbles (by a scanner's alone, too), and the same tally is
    logged when the session ends.
  - _A cue while slowed_ (the user's decision, 2026-11-19: mitigation 6
    below). While another V's bubble slows ours (a scanner's or a
    Sandevistan's, any ability that makes one), our game plays the game's
    own soft slow motion on our V: the Focus perk's full-screen effect
    (`cool_perk_focused_state_fullscreen`) and its enter sound
    (`time_dilation_focused_enter`), and its exit sound
    (`time_dilation_focused_exit`) when we are free again, as the perk
    plays them when it slows time a little (final.reds 542843-542849,
    44513-44514; engine facts `engine/combat.md` §13.75). It starts and
    ends with the factor `TimePlayerFactor` gives going under 1 and back
    (`SlowSelf`, `FreeSelf`), so nothing plays when our own ability
    matches or beats the bubble (both scanning), nor in a vehicle, dead or
    downed. Our own Focus or Deadeye reload holding the same effect keeps
    it (never started nor broken by us then). Once a session, a notice
    names whose it is (`TimeBubbles::SlowedBy`, native `TimeSlowedBy`: the
    strongest other bubble around us, the nearest of equals): "NAME's
    scanner slows you." (`Time-SlowedScanner`), "NAME's Sandevistan slows
    you." (`Time-SlowedSandevistan`), "NAME's slow motion reaches you."
    (`Time-SlowedOther`), English and French in `Text.reds`. Logged:
    `time: slowed by player P's bubble (abilities A): the game's
slow-motion cue on (…)`, `time: the slow-motion cue off`. Tested in
    `tests/timebubble_test.cpp` (whose bubble; nobody when our own matches)
    and scenario `time-scan` (the cue and notice on a partner's scan, none
    while both scan, one notice a session). To hear and see: that the
    effect is subtle at the scanner's rate of opening.
  - _Tests_: `tools/game.sh cmd time scan SCALE SECONDS` (a dilation as
    the scanner's state sets it, without opening it); the mock player's
    `--scan-at N` (its scanner on for `--sandevistan-for` seconds at
    `--sandevistan-scale`); scenario `time-scan`; the endpoint test keeps
    a scanning scale (`tests/endpoint_test.cpp`).

## Open problems

- **What the partner lives with when a V scans.** The scanner is opened
  far more often than a Sandevistan: every quickhack, every look at an
  enemy's weak points, every camera spotted. A partner within 150 m with
  no ability of their own is slowed with the NPCs each time, for as long
  as the scanner shows an enemy or a sensor. Against the enemies nothing
  changes (both slow together), but in real time the partner's own V
  feels sluggish while the netrunner moves at full speed, and the fight
  takes longer for them.
  Estimate, to replace by the logs' tallies: a quickhacking V keeps the
  scanner open about 1 to 3 s per quickhack and 5 to 15 quickhacks per
  fight, so 10 to 40 s of a 60 to 120 s fight, roughly 15 to 40 % of it.
  A partner in the same fight spends that share at the scanner's factor
  (not read yet: the first scan logs it). At a factor of 0.1 and 30 %
  that is about 27 % of the partner's real time lost to slow motion; a
  partner out of the 150 m, in a vehicle, or scanning too loses none.
  **Decided by the user 2026-11-19: the bubble as built, plus the cue
  (6, built)**; if a partner finds it intrusive, 1 comes before any
  mitigation that hands out free slow motion (2, 3, 4). The mitigations
  weighed:
  1. a smaller reach for the scanner's bubble only (say 40 m): a partner
     fighting the same group still shares it, one elsewhere is untouched;
     NPCs beyond it keep their speed on the host;
  2. the scanner's bubble slows the NPCs only, never another V: the
     partner keeps full speed, but gets the scanner's slow motion on the
     enemies for free, which single player never gives them;
  3. a V in combat (or aiming, or shooting) is not slowed by a scanner's
     bubble: the same as 2 in a fight;
  4. a softer factor for the other Vs than for the NPCs (half as slow,
     say): less sluggish, and a smaller edge on the enemies than 2;
  5. a time limit per opening (say 3 s): after it the scanner works in
     real time for its owner, as with the bubble off; the netrunner loses
     some of single player's comfort;
  6. a cue on the partner's screen while a scanner slows them (a sound, a
     screen effect), so being slowed reads as "my partner is scanning";
     polish whatever else is chosen (no native one exists, below).
     **Built** (2026-11-19): the Focus perk's own screen effect and sounds,
     and one notice a session (_A cue while slowed_, above).

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
    `research/engine-knowledge.md` §13.42), so as the user it keeps its
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
    hit stop, deflect_: local only, no bubble (Focus and Deadeye's reload
    since made bubbles, below; the scanner on 2026-11-19, Inside the
    plugin). A guest keeps them on its
    screen; the host drops their world part. Others see the V as it is
    (standing, scanning). The Relic's Gorilla Arms one-punch slow motion
    is a deflect: the puncher's alone (a guest's mark on the host's NPC
    would slow the host's V; the host drops it, `research/coop-perks.md`
    §4).
  - _Second chance_ (its dilation is in TweakDB, reason unknown): any
    other dilation left on the host is logged to find it.
  - Breach timers and network dive: not handled yet (owner-local, real
    time, `research/coop-time.md` §2.7).

  Since then (2026-11-18, same state: not yet run in game):
  - _Perks and implants that slow time_ (Focus, Deadeye's reload, and
    every `SetTimeDilationEffector`: Reflex Tuner, Synaptic Accelerator,
    second chance) make a bubble with their own factor too, so they keep
    their purpose; only the scanner, hit stop and deflect stay local (the
    scanner: a bubble since 2026-11-19).
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
  - the scanner's slow motion is off while another V is in the session
    (until 2026-11-19: since then a bubble, Inside the plugin);
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
