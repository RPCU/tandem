# Activities outside combat and quests

Time skips, workspots, minigames and screens, shops and looks, photo mode,
contests, clubs. The rule for each, then what is built.

## Rules

One question per activity: whose is it, what do the others see, and what
of it is shared. "Busy" is the `Activity` a player's game sends
(Inside the plugin).

<!-- prettier-ignore -->
| Activity | Whose | What the others see | Shared effect | State |
| --- | --- | --- | --- | --- |
| Waiting (the time skip wheel) | the asker asks; the clock is the host's | "NAME wants to wait until 14:00", a hint on the phone key: a press agrees | every other player agrees: every clock moves by the hours asked, each game fast forwards its own V, a new epoch; a no, 20 s unanswered, the asker leaving, or a blocker: nothing moves and the wheel closes | built |
| Sleeping in a bed | the sleeper asks | the same prompt ("wants to sleep until 08:00") | host: its clock is put back at once and moves again only on a yes; guest: its clock stays ahead while the others think, the host's World puts it back on a no | built, to verify |
| Quest skips (the host's story shifts the clock: a holocall, a scene) | host | "NAME moved on to 21:00" | said, never asked (the story's own time conditions must hold): every guest moves its clock, a new epoch | built |
| A guest's own story moving its clock | the guest's story is frozen (`story.md`) | nothing | the host's World puts the clock back (as before) | was |
| What blocks a skip | host's record | the asker hears who and why | a V down or out, a time ability (Sandevistan…) on, a V in another world, a pause menu, another request open; each voter's game also says no when vanilla would not let its V skip (`GameTimeUtils.CanPlayerTimeSkip`: combat, a chase, a scene, a call…) | built |
| Workspots: benches, bar stools, chairs, apartment interactions, arcade cabinets, beds, the ripperdoc's chair | the player's game | the puppet held where it sat (no sliding when its poses stop), the same workspot played on the puppet's own graph (sounds, props), the pose stream's exact pose on top | none: the device is the player's own (shared devices: open) | built |
| Breach protocol, arcade minigames | the player's game | busy (a minigame), held where it stands, idling | the breach's daemons on NPCs go to the host as requests (`combat.md`); its timer runs on that game, real time | built (state only) |
| Shards | the reader's game | busy (a screen); its popup stops that game, so the puppet idles on its own graph | none (shards and the codex stay per player, `story.md`) | built |
| Terminals and computers | the player's game | busy (a screen) | device state stays the user's until devices are synced (`world.md`) | open: no signal found yet for "at a computer's screen" |
| Vendors, drop points | the player's game | busy (a shop), held | none: inventory and money are per player | built |
| Ripperdoc | the player's game | seated in the chair (a workspot), then busy (a shop) | its cyberware is its own; leaving sends its look again, so a new arm or face shows on the puppet | built |
| The mirror, the wardrobe, transmog | the player's game | busy (a shop); the new look on the puppet when it closes | the look (face, hair, body, the clothes shown) sent again; an unchanged look reaches nobody, so the puppet is not redressed for nothing | built |
| Hub menus: inventory, map, perks, crafting, journal | the player's game | busy (a menu); while its game stands still (those menus still pause it, `time.md`) its puppet idles on its own graph | nothing pauses for the others | built |
| Photo mode | the photographer's game alone | "NAME is in photo mode", the puppet held and idling; "NAME is back" | the others keep playing; out of it, the photographer's game starts a new epoch, so a guest is checked at once (digest, resync) and drops NPC states it froze over; the host's photo mode stands its world still for everyone (it is the world) | built |
| Contests: races, boxing duels, shooting ranges, cyberpsycho fights, rides | the player who starts it (its world, as a gig: `quest-carryover.md`) | busy (a contest, `device`: the opponent or the course) | proposed: the others may watch; their hits on a duel's opponent are dropped (one on one); a race or a range is that player's alone; a cyberpsycho is a fight like any other (the host's NPC, everyone helps) | rules only: the kind is on the wire, nothing sends it yet |
| Clubs (dance floors, bars) | the host (its NPCs) | the club's crowd and dancers as proxies | nothing special: the bar stool is a workspot | was |
| Braindance clubs | the player in the chair | its body seated in the rig (a workspot), it is in a braindance (`elsewhere.md`) | its body stays visible, settled | was |
| Joytoys | the player who pays | its private scene: the others see it busy where the scene began, their puppets near it fade (`story.md`, private scenes) | none | to verify: that a joytoy's scene classifies as private (its category tag) |

Who decides: the authority table (`docs/ARCHITECTURE.md`) gets "the clock:
time skips" (host's clock, every other player asked) and "photo mode"
(the photographer's game).

## Inside the plugin

- **Wire** (`core/include/tandem/protocol.hpp`, appended, no version
  bump): `Activity` (a player's state: kind, flags `kActivityAnchored`,
  `kActivityLying`, `kActivityFrozen`, the device, the workspot's path
  hash, entry and animation, the anchor and heading); `TimeSkip`,
  `TimeSkipVote`, `TimeSkipResult` (one-shots). All on the Event channel.
  `kEpochTimeSkip` and `kEpochFrozen` name two more reasons for an epoch.
- **Core** (`core/src/activity.cpp`, `endpoint.cpp`; tests in
  `tests/activity_test.cpp`): `activity::Sanitized` says what a received
  state may mean (an unknown kind is none, a broken anchor is no place,
  only a bed or a workspot lies, a workspot game is never frozen). The host
  stamps each `Activity` with its sender, keeps everyone's for late joiners
  and relays it; a player who leaves, vanishes or comes back from a crash
  is free again for everyone. `activity::SkipArbiter`, on the host, keeps
  the one open request: everyone but the asker must answer, one no ends
  it, it lapses after 20 s (`Timing::skipAnswer`), a voter who goes has no
  say any more, the asker going withdraws it, and a blocker found while it
  is open refuses it. Forward and by a day at most; only the host may say
  a quest skip. A refusal at once reaches only the asker; whatever was
  asked of the others ends for everyone.
- **Plugin** (`plugin/src/activity.cpp`, `activity_natives.cpp`,
  `scripts/tandem/ActivityNatives.reds`): queues between the game thread
  and the net thread. While our own activity is frozen, our poses are not
  sent (as in a private scene), so the others' puppet idles on its own
  graph instead of stopping mid-step.
- **Scripts** (`scripts/tandem/Activities.reds`):
  - _Ours_: photo mode (`PhotoModeSystem.IsPhotoModeActive`), the menu
    scenarios we enter (wraps of `OnEnterScenario` of
    `MenuScenario_NetworkBreach`, `_ArcadeMinigame`, `_Vendor` and its
    `OnSwitchToRipperDoc`, `_Wardrobe`, `_CharacterCustomizationMirror`,
    `_TimeSkip`, `_HubMenu`; out through `MenuScenario_BaseMenu.OnLeaveScenario`,
    or `UI_System.IsInMenu` going false), a shard's popup
    (`ShardNotificationController`), a workspot
    (`WorkspotGameSystem.GetWorkspotInstance`: its resource, entry, idle
    animation). Anchored where it began. Frozen while our game stands
    still (`TimeSystem.IsPausedState`, or an active time dilation under
    0.05: the wheel's, a menu's).
  - _Theirs_: a busy player's puppet is held (`HoldPuppet`) and put at
    its anchor, plays the same workspot (`TandemSpot`, the NPC proxies'
    workspot player from `NpcAnims.reds`, now shared; player puppets get
    the `TandemWorkspot` component too) and lets go once its exit has
    played.
  - _Time skips_: the wheel's `TimeskipGameController.Apply` asks
    instead of applying, the wheel stays open ("Waiting for the others to
    agree…"); a yes runs the wheel's own Apply (its animation, the clock,
    `FastForwardPlayerState`), anything else closes it (`Cancel`).
    Closing it withdraws the request. A bed is seen as our clock jumping
    by more than 600 game seconds in one tick while our V is in a
    workspot. A request heard shows the game's on-screen message and an
    input hint on the phone key (after any regroup offer on screen, which
    has that key), and answers no by itself when vanilla's
    `CanPlayerTimeSkip` would refuse our V. An applied skip moves every
    other game's clock by the same difference (`SetGameTimeByHMS` with the
    wheel's reason for whole hours, else `SetGameTimeBySeconds`), fast
    forwards that game's V and starts a new epoch (`TandemSync.WorldRebuilt(3)`).
  - _Looks_: leaving the ripperdoc, the mirror, the wardrobe or the hub
    sends our look again at once (`TandemSystem.RefreshLook`); the
    endpoint sends nothing when it is unchanged.
- **Effects of a skip**: on the host the engine renews its crowd and
  streams (native, unknown in detail): the epoch makes guests drop older
  NPC states, their checks respawn or remove proxies against the host's
  record, and the host checks each guest at once. Game-time timers (quest
  delays, shop restocks, `questTimeSkipped` conditions) run on the host's
  clock, real-time ones (revive bars, regroup offers, a breach's timer)
  are untouched. Time bubbles cannot be caught mid-skip: a time ability on
  blocks it. A V down blocks it too (its bleed-out is real time).
- **Consistency** (`consistency.md`): activities are in the players digest
  (what another player is busy with and where it is held; one's own is
  not hashed) and in the resync (who is not busy, explicitly); requests,
  votes and results are one-shots, never doubled by the fault layer, in no
  digest (the clock itself is the world digest's).
- **Testing**: `tests/activity_test.cpp`, `protocol_test.cpp`,
  `consistency_test.cpp`; `tandem-peer` `--ask-skip`, `--skip-answer`,
  `--activity` and story `activity`, `skip` steps (`testing.md`); in-game
  `tools/game.sh cmd skip ask H|accept|decline`, `busy KIND|off|report`.
  Scenarios `skip-wheel`, `skip-asked`, `busy-puppet`, `look-mirror`: not
  yet run in game.

## Decisions for the user

Recommendations in bold; each one is how it is built now.

- Who must agree to a skip: **everyone else** (one no is enough), or
  only the host, or a majority (with four players).
- A quest skip of the host's story: **said, not asked** (refusing would
  stall the story's own time conditions), or asked with a refusal only
  delaying it.
- The host's own bed: **its clock put back at once and moved again on a
  yes** (the bed's buff and animation stay), or said like a quest skip.
- How long the others have: **20 s**, and whether no answer means no
  (**yes, it lapses**) or yes.
- Photo mode as the host: **its world stands still for everyone, with a
  notice** (the host's game is the world), or photo mode refused to the
  host while others are in the session.
- Hub menus pausing their own game: **left as they are for now**
  (`time.md` keeps the open problem), with the puppet idling meanwhile.
- Contests: **the starter's world, the others watch, hits on a duel's
  opponent dropped** (to build next), or everyone may join a fight.

## Open problems

- Verify in game: the wheel's flow on host and guest (it stays usable
  while it waits, a yes plays its animation), the bed's jump being seen
  and the host's clock put back without side effects, `SetGameTimeByHMS`
  with minutes, a player's workspot on the puppet's NPC rig (the
  animation sets are the player's), photo mode's own notices, the menu
  scenarios' end on every path (the hub's tabs, the mirror's cancel).
- Terminals and computers: the signal that V uses one (a device's
  fullscreen UI) and its device id.
- Contests: detecting a race, a duel, a range (their quests' facts or
  status effects) and the duel's interference rule.
- A fitting idle for a screen or a minigame when the pose stream is off
  (a tablet or phone workspot from the game files).
- The wheel's own time dilation stands the asker's world still while it
  waits; the host's stands the world still for everyone (`time.md`, menus).
