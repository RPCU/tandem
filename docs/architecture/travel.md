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
  point to our map; else, more than 150 m away, our fast travel point
  nearest to them (within 1.5 km) and then a hop; else the hop alone, 2 m
  behind them, facing their way. The game's fast travel locks (combat, a
  vehicle, quests) block joining too. The regroup action: a player's marker
  on the world map, held like a fast travel point ("Join NAME"), joins them
  the same way; so does the player's "Join NAME" row in the game's phone,
  and its "Ask NAME to join you" is an offer of reason 1 (asked) to that
  player alone ([`comms.md`](comms.md), "The phone"; scenario
  `phone-regroup`, not run in game yet). API for the story work:
  `TandemTravel.OfferRegroup(game, player, reason, label)` (reason 2: a
  story scene; `label` its title) and
  `TandemTravel.RegroupTo(game, player)`. Every fast travel tandem starts
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

## Open problems

- **Region authority** once players spread out (feasibility and a staged
  plan: `research/coop-regions.md`).
