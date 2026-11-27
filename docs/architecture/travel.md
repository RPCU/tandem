# Fast travel together and regions

## Inside the plugin

- **Fast travel together** (`scripts/tandem/Travel.reds`, milestone 4):
  no tether. When our V arrives from a fast travel (seen where vanilla
  performs it, a wrap of `FastTravelSystem.PerformFastTravel`; arrived
  once V has jumped, the fast travel's loading screen has ended
  (`FastTRavelSystem.FastTravelLoadingScreenFinished`, the signal vanilla's
  own autosave waits for; 30 s at most) and the world has run 1.5 s after
  it), the others get `Regroup` (reason fast travel, the point's record and
  marker node, where we stand; the host stamps the sender and delivers it
  to whom it is for). An offer shows the game's way: the HUD's on-screen
  message ("NAME fast travelled to POINT") and an input hint on the phone
  key, the one that answers a holocall ("Join NAME"); a press joins, it
  lapses after 15 s, and the sender is told either way (`RegroupAnswer`:
  "NAME is on the way" / "NAME stays where they are"). Not shown to a
  player already within 60 m. Joining goes the game's way: our own fast
  travel to the same point when we know it (`PerformFastTravelRequest`: its
  loading screen and streaming); else vanilla's steps to that point's
  marker (`TeleportToNode`, `NotifyFastTravelStart`) without adding the
  point to our map; else (2026-11-27, not yet run in game), more than
  40 m away, straight to 2 m behind them, facing their way, **behind the
  fast travel's loading screen** (`HiddenJoin`: the arrival's
  `TeleportHidden`, one move; before, a fast travel to our point nearest
  to them within 1.5 km, then a visible hop of up to 150 m); within 40 m
  the hop alone. The last metres after a loading screen are a hop too,
  behind another loading screen when they walked on more than 40 m
  (scenario `regroup-hidden`). The game's fast travel locks (combat, a
  vehicle, quests) block joining too. The regroup action: a player's marker
  on the world map, held like a fast travel point ("Join NAME"), joins them
  the same way; so does the player's "Join NAME" row in the game's phone,
  and its "Ask NAME to join you" is an offer of reason 1 (asked) to that
  player alone ([`comms.md`](comms.md), "The phone"; scenario
  `phone-regroup`, not run in game yet). API for the story work:
  `TandemTravel.OfferRegroup(game, player, reason, label)` (reason 2: a
  story scene; `label` its title) and
  `TandemTravel.RegroupTo(game, player)` (the story's join offer takes it:
  `story.md`, _Scenes_). Every fast travel tandem starts
  asks for the fast travel loading screen first, as the world map does
  (`inkSetNextLoadingScreenEvent`, type `FastTravel`), and nothing moves V
  (no hop) before that loading screen has ended: in game (2026-11-19) a
  travel started from gameplay without it left the screen black for good
  after its loading screen, the game running on behind it. The log says
  `travel: screen restored` on arrival (`still black?` when no end of the
  loading screen came). The host's world stays coherent when the host
  travels away from a guest: the guest keeps playing; the host's NPCs
  around the old place go (`NpcGone`, out of range) and new ones are shared
  around the host. In game (2026-11-19): offers both ways and declines
  work; the black screen above is fixed in the scripts, to run again
  (scenarios `travel`, `regroup`; `tools/game.sh cmd travel`,
  `regroup [PLAYER]`, `regroup accept|decline|offer`).
- **Taken somewhere else** (`Travel.reds` `WatchJump`, 2026-11-21, not yet
  run in game; scenario `regroup-jump`): the host's V moved more than 50 m
  in a moment (beyond 100 m a second of the world's time) with nothing to
  explain it is a quest's teleport or fade. Explained: a fast travel or a
  regroup of ours, a move we expect (a test's teleport, a race's grid, the
  test commands': `TandemTravel.ExpectMove`), a vehicle or a ride, our V
  down or just up (a respawn), the host's moment (the guests beside it
  watch it, `story.md`), an offer of ours in the last 15 s. Once V has
  stood there a second of the world's time, the others get `Regroup`
  reason 3 ("NAME was taken somewhere else"), shown to the far ones only
  (`tools/game.sh cmd regroup jump X Y Z`: such a teleport). **A guest that
  stood beside the host** (grouped, within 40 m in the last 8 s:
  `NoteBeside`) is not asked: it goes along at once (`TakenAlong`, the
  user's rule of 2026-11-21: shared play resumes with no prompt), the
  join's usual way with every move behind the fast travel's loading
  screen, any hop over 4 m too (2026-11-27: no black of our own,
  `Curtain.reds`, before it any more); the game's fast
  travel locks, or a story gate between the two (`StoryWorld.reds`), keep
  the offer as before. Not yet run in game (by hand: the mock player sends
  no reason 3).

- **The arrival** (`Travel.reds` `ArriveBeside`, 2026-11-21, not yet run
  in game; scenarios `coop-arrive`, `coop-arrive-load`): a join the co-op
  menu chose (`coop-menu.md`, New co-op game) was the player's yes. At its
  first welcome our V waits until it is free to go (not in a scene of tier
  3 and up, a vehicle, or down; 90 s at most, then "Joined NAME's session"
  and the usual ways), then goes 2 m behind the host **behind the fast
  travel's loading screen** (`TeleportHidden`: what `PerformFastTravel`
  does, gameplay restrictions off and `NotifyFastTravelStart`, around a
  plain teleport to the spot: no fast travel point needed), whatever the
  distance; within 60 m it stays. Our own fast travel locks do not hold it
  (our story's, held in the session). After the loading screen, within
  30 m of the host it stays where it landed (no visible hop), else the last
  metres as a regroup's. One notice: "Joined NAME's session". No
  `RegroupAnswer`: the host already hears "NAME joined the session".
- **Seen arriving** (`System.reds` `SnapPuppet`, 2026-11-27, not yet run
  in game; scenario `arrival-fade`): a partner's puppet whose body is put
  more than 25 m from where it was (a join's arrival, a fast travel, a
  regroup, a respawn) is hidden for the jump (`TandemVisibility` reason
  `arrival`) and fades in at its new place 0.4 s later over 0.8 s, with
  the animation system's fade: the way the game's phantoms appear
  (`scene-replay.md`, Johnny: his phantom component's effect, then that
  fade; a V's body has no phantom component, so no effect of its own).
  Not while we watch the host's view (`Spectate.reds`). Log: `player N:
arriving from M m away: hidden for the jump`, then `arrived, faded in`.

## Open problems

- **Region authority** once players spread out (feasibility and a staged
  plan: `research/coop-regions.md`).
