# Activities outside combat and quests

Time skips, workspots, minigames and screens, shops and looks, photo mode,
contests, clubs, V's homes (every apartment interaction). The rule for
each, then what is built.

## Rules

One question per activity: whose is it, what do the others see, and what
of it is shared. "Busy" is the `Activity` a player's game sends
(Inside the plugin).

<!-- prettier-ignore -->
| Activity | Whose | What the others see | Shared effect | State |
| --- | --- | --- | --- | --- |
| Waiting (the time skip wheel) | the asker asks; the clock is the host's | "NAME wants to wait until 14:00", a hint on the phone key: a press agrees | every other player agrees: every clock moves by the hours asked, each game fast forwards its own V, a new epoch; a no, 20 s unanswered, the asker leaving, or a blocker: nothing moves and the wheel closes | built |
| Sleeping in a bed | the sleeper asks | the same prompt ("wants to sleep until 08:00"); the sleeper's puppet lies in the same bed (busy "in bed") | host: its clock is put back at once and moves again only on a yes, the bed's way (its Rested buff stays); guest: its clock stays ahead while the others think, the host's World puts it back on a no; two beds at once: the second sleep is that player's yes ("V's homes") | built, to verify (`apartment-bed`) |
| Quest skips (the host's story shifts the clock: a holocall, a scene) | host | "NAME moved on to 21:00" | said, never asked (the story's own time conditions must hold): every guest moves its clock, a new epoch | built |
| A guest's own story moving its clock | the guest's story is frozen (`story.md`) | nothing | the host's World puts the clock back (as before) | was |
| What blocks a skip | host's record | the asker hears who and why | a V down or out, a time ability (Sandevistan…) on, a V in another world, a pause menu, another request open; each voter's game also says no when vanilla would not let its V skip (`GameTimeUtils.CanPlayerTimeSkip`: combat, a chase, a scene, a call…) | built |
| Workspots: benches, bar stools, chairs, apartment interactions, arcade cabinets, beds, the ripperdoc's chair | the player's game | the puppet held where it sat (no sliding when its poses stop), the same workspot played on the puppet's own graph (sounds, props), the pose stream's exact pose on top | none: the device is the player's own (shared devices: open) | built |
| Breach protocol, arcade minigames | the player's game | busy (a minigame), held where it stands, idling | the breach's daemons on NPCs go to the host as requests (`combat.md`; the outcome, devices.md "Breach Protocol"); its timer runs on that game, real time | built (state; the breach's outcome built, not run) |
| Shards | the reader's game | busy (a screen); its popup stops that game, so the puppet idles on its own graph | none (shards and the codex stay per player, `story.md`) | built |
| Terminals and computers, the smart window | the player's game | busy (a screen, with the device's id), held where it stands | the device's state is the host's (`devices.md`); its mails and files each player's own | built: the player state machine's `IsUIZoomDevice` (engine-knowledge §13.85), not run |
| Vendors, drop points, vending machines | the player's game: a vendor another game runs opens our own game's vendor of the same placed NPC (`progression.md`, "Vendors") | busy (a shop), held | none: stock, inventory and money are per player; leaving a shop sends the look again (a clothing store's purchase) | built (a partner's vendor: not run in game, scenario `vendor-guest`) |
| Ripperdoc | the player's game | seated in the chair (a workspot), then busy (a shop) | its cyberware is its own; leaving sends its look again, so a new arm or face shows on the puppet | built; at a partner's ripperdoc the screen opens where our V stands (no chair: its scene is our hidden copy's) |
| The mirror, the wardrobe, transmog | the player's game | busy (a shop); the new look on the puppet when it closes | the look (face, hair, body, the clothes shown) sent again; an unchanged look reaches nobody, so the puppet is not redressed for nothing | built |
| The stash | the player's game (its save) | busy (a shop) | none: each V's stash is its own | built (`MenuScenario_Storage`), not run |
| V's homes: shower, bed, coffee, sink, the cat, the guitar… | each player's own apartment quest (`mq000`) | the table below | buffs stay on their own V | "V's homes" |
| Hub menus: inventory, map, perks, crafting, journal | the player's game | busy (a menu); while its game stands still (those menus still pause it, `time.md`) its puppet idles on its own graph | nothing pauses for the others | built |
| Photo mode | the photographer's game alone | "NAME is in photo mode", the puppet held and idling; "NAME is back" | the others keep playing; out of it, the photographer's game starts a new epoch, so a guest is checked at once (digest, resync) and drops NPC states it froze over; the host's photo mode stands its world still for everyone (it is the world) | built |
| Contests: races, boxing duels, shooting ranges, cyberpsycho fights, rides | the player who starts it (its world, as a gig: `quest-carryover.md`) | busy (a contest, `device`: the opponent or the course) | proposed: the others may watch; their hits on a duel's opponent are dropped (one on one); a race or a range is that player's alone; a cyberpsycho is a fight like any other (the host's NPC, everyone helps) | rules only: the kind is on the wire, nothing sends it yet |
| Clubs (dance floors, bars) | the host (its NPCs) | the club's crowd and dancers as proxies | nothing special: the bar stool is a workspot | was |
| Braindance clubs | the player in the chair | its body seated in the rig (a workspot), it is in a braindance (`elsewhere.md`) | its body stays visible, settled | was |
| Joytoys | the player who pays | its private scene: the others see it busy where the scene began, their puppets near it fade (`story.md`, private scenes) | none | to verify: that a joytoy's scene classifies as private (its category tag) |

Who decides: the authority table (`docs/ARCHITECTURE.md`) gets "the clock:
time skips" (host's clock, every other player asked) and "photo mode"
(the photographer's game).

## V's homes

The apartments: H10 in the megabuilding (V's from the start), the four
bought ones (Northside, Japantown, the Glen, Corpo Plaza: the free
`dlc6_apart` content), and the homes some romances open (Judy's
apartment, Kerry's mansion; the Aldecaldo camp has a shower too).
Phantom Liberty adds none of V's. One quest runs them all, `mq000`: its
interactions phase and one scene, `mq000_01_apartment.scene`, whose
sections are the interactions; its decorations phase dresses the rooms
from the save (engine facts: engine-knowledge §13.85).

**The rule.** An apartment is each player's own: its interactions are its
quest in that player's own game, so they run there, on that V, with the
game's own prompt, animation and sound. Nothing of them is shared but what
the session already shares: the clock (a bed is a time skip, asked of
everyone), the look (what the player's game renders), the world's devices
(TV, radio, lights, doors: the host's), and the player being busy. Buffs
are never sent. A guest in the host's apartment uses its own game's copy
of it: the same bed and shower where its own save has that apartment (H10:
always), none where it does not (the door is the host's device, the
interactions are the guest's own quest). Under the quest freeze
(`quest_freeze = on`) the apartment phases keep running on a guest, as its
surroundings do (`plugin/src/engine/quests.cpp` `kPersonal`; the bought
apartments' phases are not named yet: add them with `quest_freeze_allow`).

**The audit** (read from the 2.50 scripts and the game's resource list, not
run; "generic" means the workspot and busy paths, no case of its own):

<!-- prettier-ignore -->
| Interaction (where) | How vanilla runs it | Time | Buff | Look | Co-op | State |
| --- | --- | --- | --- | --- | --- | --- |
| Shower (every home, the camp; Judy's, Kerry's) | a section of the apartment scene (props `mq000_shower.ent`, `nomad_camp_shower.ent`, `shower_kerry.ent`), V held, its workspot played by the scene | none | Refreshed (type `Housing`) | undressed by the scene (slots hidden or clothes unequipped), underwear as vanilla's paper doll shows; dressed again after | the look goes out the moment the clothes shown change, and again when they come back; the puppet: the pose stream (an important scene) or busy where the scene began (a private one), the scene's workspot replayed when the workspot system reports it; a receiving game that is censored adds the basic underwear | built, to verify (`apartment-shower`) |
| Bed: sleep, nap | a section of the apartment scene: lie down, the scene jumps the clock, get up | the scene's jump: a sleep skip asked of everyone; durations as the bed offers (measured by `apartment-bed`: `our clock jumped A -> B s`) | Rested (type `Housing`) | none | host's clock put back and moved again on a yes without the wheel's fast forward (Rested kept); a guest's stays ahead; busy "in bed", lying, once the jump is seen in a workspot; two beds at once below | built, to verify (`apartment-bed`) |
| Coffee (H10, the Glen, Corpo Plaza; tea in Corpo Plaza) | a scene section (`mq000_coffee_machine.ent`, `dlc6_apart_*_coffee_machine.ent`, teapot and teacup) | none | Energized (type `Housing`) | none | generic; the buff traced at the end | built (generic), to verify (`apartment-tour`) |
| The mirror (bathroom, smart mirrors) | `MenuScenario_CharacterCustomizationMirror`; three homes also have a mirror scene | none | none | face, hair, body | busy (a shop), the look resent on close | built (`look-mirror`) |
| The wardrobe (`mq000_clothes_container.ent`, `Wardrobe` device) | `MenuScenario_Wardrobe` | none | none | the outfit | busy (a shop), the look resent on close | built |
| The stash | `Stash` device → `MenuScenario_Storage` | none | none | none | busy (a shop) | built, to verify (`apartment-tour`) |
| The gun wall, the decorations (bedding, chair, posters, the plushie, PL's mementos) | the decorations phase, from each save (the gun wall shows that save's iconics) | none | none | none | each game shows its own V's: in the host's apartment a guest sees its own wall | by design |
| Sink, toilet | the workspots `generic__stand_sink_*__wash_hands__01`, the toilet a scene section | none | none | none | generic | built (generic) |
| Couches, chairs, beanbag, pouf, the low table's sake (Japantown) | workspots (`generic__sit_low_table_sake__drink__01`) or scene sections | none | none | none | generic: held where it sits, the same workspot | built (`busy-puppet`) |
| Computer, the smart window | `Computer` device, its screen up close (`IsUIZoomDevice`) | none | none | none | busy (a screen, the device's id); the device's state the host's | built, to verify (`apartment-tour`) |
| TV, radio, speakers, lights, the fridge, the door and lift | world devices (`TV`, `Radio`, `Speaker`, `Fridge`…) | none | none | none | their state the host's word (`devices.md`): on, off, the channel | built (`devices.md`) |
| Record players, the `mq000` speaker, the guitar, the pool table, incense (Japantown) | scene sections with quest props (`dlc6_apart_*_record_player.ent`…) | none | none | none | generic; their music is the scene's, heard only in the game that plays it | built (generic); the music: open |
| The cat, Nibbles | a quest NPC of the interactions phase (`nibbles.ent`); feeding and petting are scene sections | none | none | none | the host's cat is a proxy for a guest near it, as any NPC; a guest's own phase spawns its own cat too | open: two cats in H10 for a guest who has one |
| Johnny in V's apartment | the story's (`q101` triggers, his idle workspots) | — | — | — | the host's story NPC (`story.md`) | was |
| A partner's visit | `mq055` phases, romance scenes | — | — | — | private scenes (`story.md`): busy, the others' puppets faded near | was, to verify |
| The time skip wheel | `MenuScenario_TimeSkip` | the hours asked | none (it removes timed buffs, as vanilla) | none | asked of everyone | built (`skip-wheel`) |

**The shower, in full.** The showering player's game runs it; its V is
held by the scene. Whatever that game renders is what the others see: the
look is read from the paper doll as vanilla fills it (a hidden or removed
slot is not sent; underwear only where that game shows it), and a change
while the interaction lasts (or within 5 s after) is sent at once. The
receiving game adds the basic underwear where the sent look leaves the
body bare and its own game is censored: never more than the sender's game
shows, less only where the receiver's would never show it. When the
scene is private (`engine/scenes.cpp` classifies it: a minor quest's
scene with V alone is), the private-scene rules hold: the others see the
player busy where the scene began, idling on its own graph (no pose sent),
their puppets near the showering player fade out on its screen; the puppet
itself stays visible. When it is important (other actors in it), the pose
stream shows the scene's animation on the puppet. Refreshed is that
player's alone.

**Sleeping, checked.**

- _The bed's sleep (and nap)_: the scene moves the clock; seen as a jump
  over 600 game seconds in a tick while in a workspot or in an apartment's
  scene (`TandemActivity.ApartmentScene`: a scene bed the workspot system
  may not report), it is asked as a sleep. Its length is the bed's: the
  scenario logs it.
- _Rested_: given by the scene; vanilla's bed never fast forwards (only the
  wheel does, and that removes every timed buff), so the host's own yes
  moves its clock without it. Every other game's clock moves with the
  wheel's fast forward (their V waited the hours: their timed buffs go, as
  vanilla's wheel would take them).
- _A guest in the host's apartment_: the bed is the guest's own game's
  (its save has the apartment: H10 always); the time skip request applies
  as for any guest's bed.
- _Two players in two beds at once_: one request at a time, so the second
  sleeper's request is refused (one at a time) and, when that player was
  still to answer the open one, the host counts it as its yes
  (`activity::SkipArbiter::Joins`; a wheel's request never joins, it waits
  for its own hours). Its game hides the prompt, says so ("You go to sleep
  too: waiting for NAME's answer") and moves its clock, the bed's way, when
  the open one is applied. Each sleeper's puppet lies in its own bed for
  the others. Two players in the same bed: each game lays its own V in it
  and shows the other's puppet there too (nothing claims a bed).
- _A refusal_ refuses the clock only: the bed's buff, given by the scene,
  stays (the same on host and guest).

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
  asked of the others ends for everyone. A sleep asked while another
  request is open, by a player still to answer it, is refused to it and
  then voted as its yes (`SkipArbiter::Joins`, `Endpoint::OnTimeSkip`).
- **Plugin** (`plugin/src/activity.cpp`, `activity_natives.cpp`,
  `scripts/tandem/ActivityNatives.reds`): queues between the game thread
  and the net thread. While our own activity is frozen, our poses are not
  sent (as in a private scene), so the others' puppet idles on its own
  graph instead of stopping mid-step.
- **Scripts** (`scripts/tandem/Activities.reds`):
  - _Ours_: photo mode (`PhotoModeSystem.IsPhotoModeActive`), the menu
    scenarios we enter (wraps of `OnEnterScenario` of
    `MenuScenario_NetworkBreach`, `_ArcadeMinigame`, `_Vendor` (the look
    sent again on the way out) and its
    `OnSwitchToRipperDoc`, `_Wardrobe`, `_CharacterCustomizationMirror`,
    `_TimeSkip`, `_HubMenu`, `_Storage` (the stash); out through
    `MenuScenario_BaseMenu.OnLeaveScenario`, or `UI_System.IsInMenu` going
    false), a shard's popup (`ShardNotificationController`), a device's
    screen up close (the player state machine's `IsUIZoomDevice`, the
    device from `UIZoomDeviceID`: a screen), a workspot
    (`WorkspotGameSystem.GetWorkspotInstance`: its resource, entry, idle
    animation), a bed (a workspot in which our clock jumped: lying, kind
    Sleep). Anchored where it began. Frozen while our game stands
    still (`TimeSystem.IsPausedState`, or an active time dilation under
    0.05: the wheel's, a menu's).
  - _Interactions_: a workspot, or a scene holding our V (tier 2 and
    up), traced from start to end (`activities: interaction begins …`,
    `… over after S s (scene, apartment, workspot, tier, clothes A -> fewest
-> B, clock +N s, buffs gained: …)`); while it lasts and 5 s after, a
    change of the clothes shown sends our look at once
    (`TandemSystem.RefreshLook`). The scene is the one whose quest node ran
    last (`TandemActivity.OurScene`), an apartment's when it is in
    `activity_natives.cpp`'s list (`ApartmentScene`). Buffs gained are the
    status effects of type `Housing`.
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
    workspot or an apartment's scene. Our own bed's yes moves our clock
    without `FastForwardPlayerState` (the bed's buff stays); our sleep
    refused because another request is open is our yes to it (two beds),
    and its hours move our clock the same way. A request heard shows the game's on-screen message and an
    input hint on the phone key (after any regroup offer on screen, which
    has that key), and answers no by itself when vanilla's
    `CanPlayerTimeSkip` would refuse our V. An applied skip moves every
    other game's clock by the same difference (`SetGameTimeByHMS` with the
    wheel's reason for whole hours, else `SetGameTimeBySeconds`), fast
    forwards that game's V and starts a new epoch (`TandemSync.WorldRebuilt(3)`).
  - _Looks_: leaving a shop, the ripperdoc, the mirror, the wardrobe or
    the hub sends our look again at once (`TandemSystem.RefreshLook`); the
    endpoint sends nothing when it is unchanged.
  - _Shops of a partner's vendors_ (`scripts/tandem/Vendors.reds`): the
    "Trade" prompt beside a vendor's proxy opens our own game's vendor of
    that placed NPC, the shop activity above with it (`progression.md`,
    "Vendors").
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
- **Testing**: `tests/activity_test.cpp` (two beds at once: the arbiter
  and three endpoints), `protocol_test.cpp`, `consistency_test.cpp`;
  `tandem-peer` `--ask-skip`, `--skip-answer yes|no|ignore|sleep` (sleep:
  it goes to bed too), `--activity` and story `activity`, `skip` steps
  (`testing.md`); in-game `tools/game.sh cmd skip ask H|accept|decline`,
  `busy KIND|off|report`, `apartment report` (our scene and whether it is
  an apartment's, its class, the tier, the clothes shown, the `Housing`
  buffs, the game's nudity setting). Scenarios `skip-wheel`, `skip-asked`,
  `busy-puppet`, `look-mirror`, `apartment-shower`, `apartment-bed`,
  `apartment-tour` (each interaction by hand, with the quest trace on):
  not yet run in game.

## Decisions

**Decided by the user 2026-11-19**: every option in bold below, each as it
is built now.

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
- A bed whose sleep the others refuse: **the buff stays, only the clock is
  refused**, or the buff taken off too.
- Two beds at once: **the second sleep is that player's yes to the first,
  to the first's hour**, or refused like any second request.
- Apartment scenes: **classified like any scene** (private when the scene
  says so), or always private (the others never get the moment's holds).

## Open problems

- Verify in game: the wheel's flow on host and guest (it stays usable
  while it waits, a yes plays its animation), the bed's jump being seen
  and the host's clock put back without side effects, `SetGameTimeByHMS`
  with minutes, a player's workspot on the puppet's NPC rig (the
  animation sets are the player's), photo mode's own notices, the menu
  scenarios' end on every path (the hub's tabs, the mirror's cancel).
- Terminals and computers: the signal (`IsUIZoomDevice`) is read, not yet
  seen in game.
- V's homes, to verify in game: whether a scene's workspot shows in
  `WorkspotGameSystem` (the `in a workspot` of `interaction begins`),
  how the shower undresses V and whether the puppet takes underwear in
  its slots, the apartment scene's class (private or not), the buff
  records' names (`HousingStatusEffect.Rested`, `Refreshed`, `Energized`:
  community names), the bed's durations, the four bought apartments'
  phases (for `kPersonal`) and Northside's scene. Open: the music of quest
  props is heard only in the game that plays it; a guest with its own cat
  sees two in H10; nothing claims a bed or a shower, so two players can
  use the same one, each game showing its own V in it.
- Contests: detecting a race, a duel, a range (their quests' facts or
  status effects) and the duel's interference rule.
- A fitting idle for a screen or a minigame when the pose stream is off
  (a tablet or phone workspot from the game files).
- The wheel's own time dilation stands the asker's world still while it
  waits; the host's stands the world still for everyone (`time.md`, menus).
