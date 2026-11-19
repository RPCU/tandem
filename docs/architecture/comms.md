# Comms: pings, the link, waypoints

"A player should be able to send a request to connect to another one so
they can share stuff like quickhacking ping visuals." Two players
coordinate the way the game already talks to one player: its scanner tag,
its mappins (world, minimap, world map), its outlines, its prompts and
input hints. Nothing here adds a widget of its own.

**State (2026-11-19).** Built and compiled against the game's scripts
(`task scripts:check`: 0 errors, 0 warnings); core logic and the session
relay tested (`tests/comms_test.cpp`, `tests/protocol_test.cpp`). **Nothing
here has run in the game yet**: the game machine was off limits. Engine
facts and their tags: [`../research/engine/ui.md`](../research/engine/ui.md)
§13.30.

## Decisions

Each with what is built and the recommendation; the ones marked _user_
are the user's to confirm.

1. **The ping input: the scanner's tag key outside the scanner.** One key,
   `TagButton` (the middle mouse button by default), already means "mark
   that" in the scanner; outside it the game does nothing with it
   (`FocusModeTaggingSystem` listens to it at all times and acts only in
   focus mode). A press pings what V looks at
   (`TargetingSystem.GetLookAtObject` / `GetLookAtPosition`); a second
   press on the same thing within 1.5 s turns it into a **danger** (the
   callout, below); a press on one of our live pings later takes it back.
   In the scanner the key tags, as in single player, and the tag is shared
   (2). Rejected: the cut multiplayer's own `MP_TriggerPing` action
   (`PlayerPuppet.OnActionMultiplayer`): its binding in the single-player
   input contexts is unknown and its `PingSystem` is the multiplayer's
   native replicated system. To check in game: that `TagButton` reaches a
   listener outside focus mode on keyboard and pad, and what else the pad's
   binding does there (§13.30).
2. **The link: automatic in a co-op pair, with a setting** (_user_).
   `tandem.ini` `link = auto` (default): every partner is linked from the
   start, until either side breaks it; `ask`: a link starts on a request
   the other accepts; `off`: never. The request itself is the game's way:
   aim at the partner's V and press the tag key (asks, accepts their
   request, or breaks the link); the partner sees the HUD's on-screen
   message "NAME asks to link: share scans and pings" and an input hint on
   the phone key, "Link with NAME", as a regroup offer
   ([`travel.md`](travel.md)), for 15 s. Linked players share their
   scanner tags (outlined, in the tag list, as one's own) and the Ping
   quickhack's reveals (outlined for the hack's length). Recommendation:
   keep `auto`. Two to four friends who join each other want shared
   scans without a ceremony; `ask` is for sessions with strangers (join
   codes passed around) and costs nothing to keep.
3. **Pings reach everyone in the session**, linked or not: a ping is said
   on purpose, a tag is information that flows on its own (the link gates
   only the latter). `pings = off` neither sends nor shows them.
4. **The shared waypoint is the map pin, or the map point a player
   tracks.** Polled once a second (`MappinSystem.GetManuallyTrackedMappinID`:
   the world map's custom pin is that mappin, with
   `CustomPositionVariant`), sent when it changes kind or moves past a
   metre, shown to the others as "NAME's waypoint" (world label, world map
   tooltip "The map pin NAME placed" / "The place NAME tracks"). A partner
   can track it on the world map like any mappin and get the game's GPS
   route to it; a tracked marker of tandem's is never sent back as our own.
   **The tracked objective is not sent**: in a session every guest's
   journal and tracked entry are the host's (story sync, `Tracked`,
   [`story.md`](story.md)), so the objective and its quest marker are
   already the same in every game. With `story = off` a guest keeps its
   own objective; showing it to the others would be a `WaypointKind`
   appended for it (the quest mappin's position:
   `MappinSystem.GetQuestMappinPositionsByObjective`): open, low value.
5. **Callouts: the danger ping, no emotes.** The double press is the one
   callout with a clean path: no new input, its own icon and sound, the
   same mark replaced (so no clutter). Emotes: no. V has no emote
   animations in first person, a puppet would need animations the pose
   stream does not carry when V stands still, and the only free inputs
   are chords or a radial (the quick slots' radial is the game's own ink;
   adding entries means rebuilding its widget, `MAP:2407`-style staleness).
   Callouts by voice (V's own barks, the cut ping records'
   `VoTriggerName`) are a later polish, ears required.
6. **Text chat: not now** (_recommendation_). The only text field is
   Codeware's input in the game's message popup
   (`GenericMessageNotification.ShowInput`, [`coop-menu.md`](coop-menu.md)),
   which takes the screen and is no place to type in a fight; the HUD has
   no chat log, so one would be built in ink from scratch (as the party
   HUD is, `presence` branch) and localized; players of a two-to-four
   co-op are on voice (Discord, Steam) anyway. A clean minimum if wanted
   later: a "Message" entry in the pause menu's co-op page, the popup's
   input, delivered as the HUD's on-screen message on the others' screens
   ("NAME: text", 120 characters, rate-limited like pings): one appended
   message, no log. Build it only on request.
7. **The party HUD** (`presence` branch, its design file
   there) is not duplicated. Pings and waypoints are mappins: their
   direction off screen is the mappin's own clamping at the screen's edge,
   so no second arrow; names come from the same `TandemNative.PlayerName`.
   On merging, the party entry is the natural place to show that a partner
   is linked (a small mark beside the name, from
   `TandemCommsNative.LinkState`), and the presence branch's "in a menu"
   flag could hold back a ping's sound for a player in a menu; neither is
   needed for this to work.

## The team target: "Mark target"

"Tell team mates to target a specific target", from the game's own
quickhack panel. Built 2026-11-19, not run in game.

- **An extra entry in the quickhack panel**, "Mark target", on whatever
  the panel opens on: NPCs, devices (cameras, turrets...), vehicles. It is
  offered only while we are linked with at least one partner. It never
  takes a cyberdeck slot. Every target builds its panel list
  (`ScriptedPuppet`, `Device`, `VehicleObject`'s `SendQuickhackCommands`)
  and hands it to the panel in one event (`RevealInteractionWheel`).
  `QuickhacksListGameController.OnQuickhackStarted` is wrapped to append
  our `QuickhackData` after the deck's programs. The player's equipped
  programs are never touched.
  - Its look: the Ping program's icon and category, read from
    `QuickHack.FreePingHack`. Cost 0, instant. Its label is "Clear the
    team mark" on our own current target.
  - **No TweakXL record and no action behind it.** Choosing it goes
    through `ApplyQuickHack`, wrapped: the game's own accept animation
    plays, then our mark. No `QuickSlotCommandUsed` reaches the target, so
    there is no upload, no RAM, no trace, no combat start, no alert and no
    reveal on our side. `ApplyQuickhackSelection` is skipped for it: it
    would hand the target an entry with no action.
  - A record cloned from a vanilla quickhack would have needed an action
    class to neutralize every one of those effects. Leaving the action out
    is the cleaner path.
- **Without a cyberdeck** (no program at all), the panel shows its own "no
  quickhacks" page and hides the list. Our entry is not added: the scanner
  key's ping, and the shared scanner tag, remain the way to point at
  something. With a deck whose programs do not apply to this target (all
  locked), the entry is still offered.
- **The mark**: a new `Mark` kind, `Target` (7; unreleased, so no version
  bump). Every partner sees "NAME: Target" on the entity (world, minimap,
  map; tooltip "NAME's target: focus it"), with its own icon and sound.
  The target is outlined for as long as the mark lasts, as a shared tag is
  drawn (a `RevealObjectEvent`, reason `TandemTeamTarget`). Our own side
  shows the mark only.
- **Lifetime**: one team target per player. It lasts until:
  - the target dies or is destroyed (our game watches it twice a second:
    `IsDead`, a vehicle's `IsDestroyed`, or gone from our world);
  - we choose it again on the same target (cleared);
  - we mark another target (replaced: receivers drop a sender's last
    target on its next one);
  - 60 s pass;
  - we vanish (`PlayerLeft`: every board forgets our marks).

  Pings and team targets never replace each other, and a team target is
  not counted in the three pings a player may show. `pings = off` does not
  hide a team target (it is an order, not chatter).

- Only shared things can be marked: a shared NPC, a static entity, a
  player. An entity that exists in one game only (a car copy, a body) is
  refused with a notice.
- Test command: `tools/game.sh cmd comms target` marks what V looks at.
  The mock player: `--mark-target ID|nearest`. Scenario `comms-target`.

## Protocol

Four messages, appended after `ShotHit` (no version bump; the `presence`
branch appends its own three: whichever merges second goes after the
other's, before either ships). All on the Event channel.

| Message    | Kind     | Size  | Carries                                                                                                                             |
| ---------- | -------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `Mark`     | one-shot | < 40  | a ping: sender, its id, kind (spot, enemy, device, loot, vehicle, danger, team target; none: taken back), target, position, seconds |
| `Tag`      | one-shot | < 200 | a scan tag (until off) or a reveal (seconds) of up to 16 targets, on or off                                                         |
| `Link`     | state    | small | whom a player consents to share tags with: nobody, the listed players, everyone but the listed                                      |
| `Waypoint` | state    | < 32  | a player's waypoint: none, a map pin, a tracked map point; where                                                                    |

A target (`TargetRef`) is named the same in every game: a shared NPC by
its `NpcId` (its proxy on a guest), a placed entity by its static
`EntityID` hash, a player by id. A receiver that does not find it, or finds
it more than 15 m from the position that comes with it, shows the place.

## Inside the plugin

- **Core** (`core/include/tandem/comms.hpp`, tested in
  `tests/comms_test.cpp`): `Limiter` (a token bucket), `Pinger` (our pings:
  ids, the danger double press, taking back, 3 at once, 3 then one every
  2 s), `Board` (a receiver's marks: replaced by id or by the same target,
  3 per sender, each kind's lifetime: spot 20 s, enemy 15, danger 10,
  device and loot 30, vehicle 20, capped 3..60), `Links` (our state with
  each player: none, asking, offered, linked), `Linked` (each consents to
  the other), `TagLedger` (what we outline for others: one outline per
  target however many hold it, a reveal's length capped at 60 s, at most
  64), `Moved` (a waypoint worth sending again).
- **Endpoint**: the host stamps the sender of each, holds each guest to a
  rate (marks: 6 then 1 a second; tags: 12 then 4 a second; the rest
  dropped and counted, `CommsDropped`), passes marks to every other
  player, delivers tags only to the players linked with their sender
  (`DeliverTag`; a guest checks again on arrival), keeps everyone's `Link`
  and `Waypoint` for late joiners and resyncs, in the players digest.
- **Plugin** (`plugin/src/comms.cpp`, natives `plugin/src/comms_natives.cpp`
  / `scripts/tandem/CommsNatives.reds`): the net thread pushes what
  arrives and the session's joins, leaves and end; the game thread runs
  the core pieces and hands the script one list of changes a tick (show or
  hide a mark, outline or stop, a link's new state, a waypoint). Our own
  pings go on our own board too: we see them as the others do.
- **Script** (`scripts/tandem/Comms.reds`, `TandemComms`, ticked by
  `TandemSystem` in a session):
  - the key: a listener on `TagButton` (and on `PhoneInteract` while a link
    request is shown); the kind from what V looks at (a hostile
    `ScriptedPuppet` an enemy, a `VehicleObject`, a `gameLootContainerBase`
    or `gameItemDropObject` loot, a `Device`, else the place);
  - marks: one mappin each, `MappinSystem.RegisterMappinWithObject` on the
    entity when it is here (an NPC's `roleMappin` slot, a car's
    `vehMappin`), so an enemy ping follows the NPC's proxy, else
    `RegisterMappin` at the place; variant `CPO_PingGoHereVariant` (the cut
    multiplayer's, never registered in single player), definition
    `Mappins.TandemPingMappin` (`tweaks/tandem/comms.tweak`: the remote
    player's, shown in the world, on the minimap and the map). Markers.reds'
    UI wraps draw it as a remote player's marker (the quest widget in the
    world, the point-of-interest widget on the minimap, always shown on the
    map); its `MappinScriptData` (`TandemCommsMappinData`) carries the icon
    (a part of the mappin atlas per kind, set after each controller's
    `UpdateIcon`), the label ("NAME: Enemy"; ours "Enemy") and the map
    tooltip; a UI sound per kind (`ui_menu_map_pin_created`, danger
    `ui_jingle_chip_malfunction`);
  - shared tags: wraps of `FocusModeTaggingSystem.TagObject`,
    `UntagObject` and `UntagAll` say ours to the linked; theirs are applied
    through the same system (`TandemTag`, which does not say them back);
  - reveals: a wrap of `PingSquadEffector.MarkSquad` (the Ping quickhack
    marking a squad) sends the squad's shared NPCs with the effect's
    remaining time; a linked player's reveal is a `RevealObjectEvent`
    (reason `TandemLinkedPing`, with its lifetime), the way the automatic
    tagging perk reveals (`PlayerPuppet.OnBeingNoticed`);
  - waypoints as in decision 4; link notices ("Linked with NAME: scans and
    pings are shared", "Link with NAME broken", "NAME set a waypoint"),
    none for a player who left (the co-op menu says that).
- `tools/game.sh cmd comms report | ping | link [NAME] | unlink [NAME] |
accept | tag`.

## Consistency

The audit's rows ([`consistency.md`](consistency.md)), in short:

- **One-shots**: `Mark` and `Tag` are never duplicated by the fault layer
  (`IsOneShot`), never kept, never sent to a late joiner (a ping is of the
  moment). A lost one is a ping not heard; a doubled one cannot happen.
- **State**: `Link` and `Waypoint` are kept by the host, deduplicated
  (`Keep`), handed to late joiners, in the players digest and its resync
  (a resync says who has none).
- **A sender that vanishes takes everything with it**: its `PlayerLeft`
  (the 5 s silence is enough) makes every other game drop its marks, its
  outlines (those only it held), its waypoint marker and its link; the
  host drops its record and its rates. A player back from a crash on its
  old id starts with no waypoint and no link until its new game says.
- **A link that breaks** (either side, or a player leaving) takes the
  other's outlines off at once; scans are untagged, reveals end.
- **The session's end** clears every mark, outline, waypoint and link
  state; our own consent and waypoint carry into the next session.
- **Epochs**: nothing fenced. A ping or tag aimed at an NPC of the host's
  old world finds no proxy and falls back to its place, or is ignored.
- **Floods**: the sender's own rate, the host's per guest, per-sender caps
  on every board.

## Tests and scenarios

- `tests/comms_test.cpp`: the rate, lifetimes, the ping toggles (danger,
  take back, expiry, places within 2 m, the rate), the board (replace,
  withdraw, same target, the cap, expiry, a sender gone), consents and
  links (auto, ask, off, requests, breaking, leaving), the tag ledger
  (holders, reveals, expiry, the cap), waypoints worth sending; over
  loopback: pings to every other player stamped and rate-held by the host,
  tags to the linked only both ways, a new link, waypoints relayed and
  handed to a late joiner, and a guest that vanishes leaving nothing.
- `tests/protocol_test.cpp`: round trips, channels, sizes, refusals.
- The mock player: `--link auto|ask|off`, `--link-at N`, `--ping N`,
  `--tag N`, `--reveal N`, `--waypoint X,Y,Z`, `--waypoint-clear N`; it logs
  every mark, tag, link and waypoint it hears.
- Scenarios (not run yet): `comms-pings` (its spot, danger, withdrawal;
  ours; it vanishes), `comms-enemy` (as a guest: an enemy ping on our
  proxy, reveals outlined), `comms-link` (link = ask on the other side:
  accepted, broken, again), `comms-waypoint`.

## Open

- Everything in game: the input outside the scanner (keyboard and pad),
  the mappins in each of the three places with their icons and labels
  (the parts chosen in `TandemComms.Icon` are a first pick by name), the
  sounds (by ear), the outlines of a shared tag and of a reveal on a proxy,
  the input hint beside a regroup offer's.
- The Ping quickhack's network beams (`EffectExecutor_PingNetwork`: the
  devices a ping links to) are the caster's alone; only NPC outlines are
  shared. Devices revealed by other means are not shared either.
- A tag of an entity that exists only in one game (a dynamic one that is
  not a shared NPC: a car copy, a body) is not sent.
- Localized strings for the notices and labels.
