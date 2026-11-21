# Comms: pings, the link, waypoints

"A player should be able to send a request to connect to another one so
they can share stuff like quickhacking ping visuals." Two players
coordinate the way the game already talks to one player: its scanner tag,
its mappins (world, minimap, world map), its outlines, its prompts and
input hints, its phone's call widgets. Nothing here adds a widget of its own.

**State (2026-11-19).** Built and compiled against the game's scripts
(`task scripts:check`: 0 errors, 0 warnings); core logic and the session
relay tested (`tests/comms_test.cpp`, `tests/protocol_test.cpp`). **Nothing
here has run in the game yet**: the game machine was off limits. Engine
facts and their tags: [`../research/engine/ui.md`](../research/engine/ui.md)
§13.39.

## Decisions

Each with what is built. Decided by the user 2026-11-19: every
recommendation below as built, and no text chat (6).

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
   binding does there (§13.39).
2. **The link: automatic in a co-op pair, with a setting** (decided).
   `tandem.ini` `link = auto` (default): every partner is linked from the
   start, until either side breaks it; `ask`: a link starts on a request
   the other accepts; `off`: never. `link` and `pings` are on the co-op
   menu's Settings page, applied at once (a new default replaces the
   consents given so far; `coop-menu.md`). The request itself is the game's way:
   aim at the partner's V and press the tag key (asks, accepts their
   request, or breaks the link), or call them from the game's phone (The
   phone, below); the partner's game rings as for a call (the game's
   ringtone, unless one of its own calls is up), with the HUD's on-screen
   message "NAME is calling: link to share scans and pings" and two input
   hints, "Link with NAME" on the phone key (a press, as a regroup offer,
   [`travel.md`](travel.md)) and "Decline" on `PhoneReject` (held, as for
   a holocall), for 15 s. Declining stops the ring and links nothing; the
   caller is not told (no message for it: their consent stands, as an
   unanswered request's does, until they hang up). One request rings at a
   time: a second one declines the first, whose caller is told why
   (`LinkDecline`, reason `kLinkDeclineOther`, host-stamped and delivered
   to the caller alone: "NAME took another link request: call again
   later"; their consent stands; 2026-11-21, not yet run in game; test
   "comms in a session…", `tests/comms_test.cpp`). Linked players share their
   scanner tags (outlined, in the tag list, as one's own) and the Ping
   quickhack's reveals (outlined for the hack's length). Default `auto`:
   two to four friends who join each other want shared
   scans without a ceremony; `ask` is for sessions with strangers (join
   codes passed around) and costs nothing to keep.
3. **Pings reach everyone in the session**, linked or not: a ping is said
   on purpose, a tag is information that flows on its own (the link gates
   only the latter). `pings = off` neither sends nor shows them.
4. **The shared waypoint is the map pin, or the map point a player
   tracks; with neither, its tracked objective.** Polled once a second
   (`MappinSystem.GetManuallyTrackedMappinID`: the world map's custom pin
   is that mappin, with `CustomPositionVariant`), sent when it changes
   kind or moves past a metre, shown to the others as "NAME's waypoint"
   (world label, world map tooltip "The map pin NAME placed" / "The place
   NAME tracks"). A partner can track it on the world map like any mappin
   and get the game's GPS route to it; a tracked marker of tandem's is
   never sent back as our own. **With nothing tracked by hand, the tracked
   objective is the waypoint** (`WaypointKind::Objective`, protocol 9):
   `JournalManager.GetTrackedEntry` as a `JournalQuestObjectiveBase`, its
   quest mappins' positions
   (`MappinSystem.GetQuestMappinPositionsByObjective`, as the world map's
   `MenuHubGameController.UpdateTrackedQuest` reads them), the one nearest
   V; shown as "NAME's objective" ("The objective NAME tracks"), in the
   player's colour, trackable the same way, with no notice (it comes on
   its own at each join and quest step). Anyone's objective is shared, the
   host's or a guest's: story sync (`quest_freeze`) is off by default, and
   a guest that is not frozen keeps its own journal and sees nothing of
   the host's quest otherwise. **A frozen guest** (story sync on: its
   journal and tracked entry are the host's, `Tracked`,
   [`story.md`](story.md)) sends no Objective of its own (it would be the
   host's again) and shows none of the host's (its own quest marker is
   already there; one heard before the freeze began goes at the next
   poll). Not yet run in game.
5. **Callouts: the danger ping, no emotes.** The double press is the one
   callout with a clean path: no new input, its own icon and sound, the
   same mark replaced (so no clutter). Emotes: no. V has no emote
   animations in first person, a puppet would need animations the pose
   stream does not carry when V stands still, and the only free inputs
   are chords or a radial (the quick slots' radial is the game's own ink;
   adding entries means rebuilding its widget, `MAP:2407`-style staleness).
   Callouts by voice (V's own barks, the cut ping records'
   `VoTriggerName`) are a later polish, ears required.
6. **Text chat: not wanted** (decided 2026-11-19). Not built, and
   not to build unless the user asks again. What was weighed: the only text field is
   Codeware's input in the game's message popup
   (`GenericMessageNotification.ShowInput`, [`coop-menu.md`](coop-menu.md)),
   which takes the screen and is no place to type in a fight; the HUD has
   no chat log, so one would be built in ink from scratch (as the party
   HUD is, `presence` branch) and localized; players of a two-to-four
   co-op are on voice (Discord, Steam) anyway. A clean minimum if wanted
   later: a "Message" entry in the pause menu's co-op page, the popup's
   input, delivered as the HUD's on-screen message on the others' screens
   ("NAME: text", 120 characters, rate-limited like pings): one appended
   message, no log.
7. **The party HUD shows the linked** ([`presence.md`](presence.md), Party
   HUD). "When linked, the health bar of each player linked should be
   displayed too, with their name": the party list has an entry (name,
   health, state, off-screen arrow, in the partner's colour) for each
   partner `TandemCommsNative.LinkState` says is linked. Health is on
   screen only, in that list: the nameplate over a head is the name alone,
   linked or not. An unlinked partner keeps its marker and its nameplate. A link broken (either side) or a partner
   gone takes the entry away at the next frame. Pings and waypoints are
   mappins: their direction off screen is the mappin's own clamping at the
   screen's edge, so no second arrow; names come from the same
   `TandemNative.PlayerName` (the session's shown names: "V (2)").
   Presence's "in a menu" flag could still hold back a ping's sound for a
   player in a menu.

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

## A partner's quickhacks, in their colour

"Each player should have a corresponding quickhack colour, so that other
players see who ran a quickhack on each enemy or other thing; other
players' quickhacks are visible only if the players decided to link."
Built 2026-11-19, not run in game.

- **One colour per player, everywhere** (`TandemMarkers.ColorOf`, by
  player id, 1 to 8: cyan, yellow, green, pink, orange, violet, red, sky
  blue; white only for no player; the colour-blind pairs and how the 8th
  was chosen: [`players.md`](players.md);
  one definition, in `Markers.reds`: `tools/scripts-duplicates.py` refuses a
  second). It tints a player's marker, nameplate and party entry
  (`PartyHud.reds`), pings, waypoint, team target and quickhacks. A ping's
  icon is its sender's colour only (the marker's label wrap no longer
  tints pings).
- **What is shown.** A linked partner's quickhack on a shared NPC is drawn
  with the game's own quickhack indicator over its target:
  - the `QuickHackMappinController` widget a vehicle uses for an upload
    (`VehicleComponent.CreateQuickHackMappin`), with the program's icon,
    its bar filling over the upload;
  - then the effect it left, the same widget in its duration state, the
    bar emptying over the effect's remaining time.

  Both are tinted with the partner's colour (icon and bar). Our own
  quickhacks keep the game's colours and go through vanilla, untouched.
  An unlinked partner's quickhacks show nothing new. Their effects on the
  NPC stay as they are: the proxy wears the same status effects as the
  host's NPC, as before (`Hacks.reds`; a burning NPC burns for everyone).

- **No new message.** What already crosses games says who hacked what:
  - a guest learns every upload on a host NPC from `NpcHackState` (player,
    program, upload time, done or failed) and every effect from
    `NpcStatus` (player, status effect; its remaining time is read on the
    proxy, which wears it);
  - the host learns its guests' uploads from `NpcHack` and their ends
    from its own upload records, and the effects from the status effects
    landing on its NPCs.

  `Hacks.reds` hands each of these to `TandemCommsNative.PartnerUpload` /
  `PartnerEffect`.

- **A partner's breach** (Breach Protocol, `devices.md`): its daemons'
  effects on the owner's NPCs (`MinigameAction.ICEBrokenMinigame*`,
  `VulnerabilityMinigame`, the squad buff) travel as `NpcStatus` with the
  breacher as the player, like a quickhack's (`TandemBreach.IsDaemonEffect`
  in `Hacks.reds`): a linked partner sees each weakened NPC with the
  partner effect indicator, tinted, for as long as the effect lasts. No
  indicator of its own for the network.

- **The rules** are core's (`comms::HackBoard`). One upload indicator per
  player and NPC (a new upload replaces it); an effect's indicator carries
  the program that left it. Indicators are shown only for linked players:
  - a link made shows what still runs;
  - a link broken hides everything of that player at once (kept, in case
    the link comes back);
  - a vanished hacker leaves nothing (`PlayerLeft`);
  - an effect the host says has ended goes, whoever's it was;
  - everything expires (the upload's length and a second; the effect's
    remaining time, or 120 s when unknown); at most 64 shown.
- **Devices** (2026-11-19, not run in game): the same board as an NPC's
  `PartnerUpload` / `PartnerEffect`, the target a
  device's entity (`TargetRef` kind Entity, its static id: the same in
  every game), the same link gating. No program crosses games for a device
  ([`devices.md`](devices.md): `DeviceUse` from a guest, the host's live
  `DeviceState` with `by`), so `Devices.reds` hands the board the state
  bits before and after (`TandemCommsNative.PartnerDevice`; host: a
  guest's request, guest: the host's live word, never a record) and
  `comms::HackBoard::Device` reads them: `kDeviceGlitching` came on, a
  distraction, shown with the malfunction quickhack's icon while it lasts;
  `kDeviceFriendly`, a turned camera or turret, while it lasts; any other
  `kDeviceHacked` change, the indicator's own icon for 5 s. A bit that goes
  off hides its indicator, whoever's it was. Tests: "a partner's quickhacks
  on devices" (`tests/comms_hacks_test.cpp`).
- Our game drives the indicator's bar itself (`TandemProgress`, every
  tick). The widget's own driver is a stat pool of the upload on the
  target, which exists only in the hacker's game.
- Test command: `tools/game.sh cmd comms hacks`. The mock player, as host:
  `--hack-npc ID|nearest`. Scenario `comms-hacks`.

## The Ping's network

"The network beams it draws to connected devices are shown to linked
partners too." Built 2026-11-20, not run in game; engine facts:
[`../research/engine/devices.md`](../research/engine/devices.md) §13.90.

- **Where a ping becomes beams.** The Ping quickhack on a device, or on an
  NPC of a network (the game passes it to the network's access points),
  ends in `Device.OnActionPing` on one device, which pulses the network
  from itself (`PulseNetwork`): the game's network system then draws a beam
  from that device to each device in range it is linked with. Nothing of
  it is state: the beams are the caster's game's alone.
- **Shared as a reveal of that device.** The wrap of `OnActionPing` only
  queues the device (`TandemStreamKind.NetworkPing`: an entity's handler
  may run on any thread); on the tick `TandemComms.NetworkPinged` sends a
  `Tag` (reason Reveal, the pulse's length, the device's static id) to the
  linked. No new message: a reveal of a placed device **is** a network
  pulse from it.
- **A linked player's game** outlines the device as any reveal, and pulses
  the network from its own copy of it (`Device.TandemPulseNetwork`): the
  same beams, drawn by its own network system between its own copies. It
  is not a ping of ours: nothing is said back, no RAM, no upload.
- **Gating, rates, consistency**: a tag's (linked players only, the host's
  rate per guest, a one-shot, nothing kept). A device not in the receiver's
  world is logged and skipped.
- The mock player: `--reveal-device ENTITY[@S]`; it logs `player N pulsed
the network from device X`. Scenario `comms-network`.

## The phone: partners in the contact list

"Integrate any interaction we can do directly to another player, like
trading/selling and linking, via holocall using the vanilla menu and
selecting the player" (the user, 2026-11-21). Built 2026-11-21, compiled
against the game's scripts (`task scripts:check`); **not run in game
yet**. `scripts/tandem/PhoneContacts.reds`.

- **Where.** The game's own contact list (the phone key held: the HUD's
  `NewHudPhoneGameController` and its `PhoneDialerLogicController`). On its
  Contacts tab the session's other players come first, above the game's
  contacts as it sorted them; under each name the link between us: "Not
  linked", "Calling...", "Calling you", "Linked". The Unread tab is the
  game's alone.
- **Selecting a player** (the call key, or the messages key) opens their
  actions over the list, as a contact's message threads open (`PushList`,
  the player's name as the title; back or a tab change returns, players
  first again):

  - **Call / Answer / Cancel the call / Hang up**, by the link's state (0,
    2, 1, 3): ask to link, accept their request, take ours back, break the
    link; with `link = off`, "Links are off" (`TandemComms.PhoneCall`,
    `SetLink`);
  - **Trade**, only with `trade = on`: the trade request, at any distance
    ([`trade.md`](trade.md); `TandemTradeTable.AskFromPhone`);
  - **Join NAME**: fast travel to them, as the world map's regroup action
    (`TandemTravel.RegroupTo`);
  - **Ask NAME to join you**: a regroup offer to them alone (reason 1,
    asked; `TandemTravel.OfferRegroup`).

  A chosen row closes the phone; the action runs on our next tick
  (`TandemPhoneContacts.Tick`), when the list's modal context and its slowed
  time are gone. Each row shows its effect under its label ("Ask NAME to
  link: share scans, pings and health", "Give, swap or sell items and
  eddies, wherever you both are", "Fast travel to NAME", "NAME is offered
  a fast travel to you").

- **The other side of a call** is the link request's (decision 2): their
  game rings with our name, "Link with NAME" answers (the phone key's
  press), "Decline" (`PhoneReject` held) stops the ring. An answered call
  is the link: the existing link notice ("Linked with NAME: scans and
  pings are shared"), the party HUD's entry (presence.md); hanging up from
  either phone breaks it ("Link with NAME broken"). Both sides see the call
  as the game draws one (The call on screen, below).
- **The call on screen** (`scripts/tandem/Holocall.reds`, 2026-11-21,
  **not yet run in game**): the game's own call widgets, without its call.
  - Their call rings with the **incoming-call widget**
    (`IncomingCallLogicController`): their name, the game's unknown avatar
    (`PhoneAvatars.Avatar_Unknown`) tinted in their colour
    (`TandemMarkers.ColorOf`), the hold-to-reject glyph, the `ring`
    animation played again until it is answered, declined or left (15 s).
  - Our call (the phone's Call, or a ping on their V) shows the **call
    portrait** connecting (`HoloAudioCallLogicController.ShowIncomingContact`,
    status "Calling..."), for at most 20 s unanswered.
  - Answered (either side): the **audio call view** with the avatar
    (`StartAudiocall`, status "Linked") for 8 s, then it hides with the
    game's animation and the link goes on, shown by the party HUD. A link
    lasts the session: a portrait held that long would sit on the HUD's
    left column for hours. A link made without a call (`link = auto` at the
    start) shows none.
  - **One button ends the call**: `PhoneReject` held, as for the game's
    calls, hint "Hang up", while the view is up: our call taken back, or the
    link broken (the phone's Hang up); the view says "Call ended" with the
    game's disconnecting animation. The link broken from anywhere else ends
    it the same way.
  - The portrait is not the partner's face: the holocall mode's render
    texture (`HudPhoneAvatarController.m_HolocallRenderTexture`) is fed by
    the engine's holocall setup, which no script points at another V (not
    tried); the audio call view shows the avatar instead.
  - How: the HUD phone (`NewHudPhoneGameController`, found on the HUD layer
    with Codeware's `GetGameControllers`) spawns its own library widgets
    (`incomingCallElement`, `holoAudioCallElement`: their `libraryID`, into
    their `slot`) into fields of ours, and plays them with the game's
    functions (`SetCallInfo`'s steps without its `JournalContact`; the
    holo's take an avatar id and a name). The game's call state never hears
    of them: no `questTriggerCallRequest`, no `m_CurrentCallInformation`,
    no `TalkingTriggerRequest`, so no `phonecall_*` fact for quests or story
    sync (`Phone.reds`). `ActivatePhoneElement` is called as the game's
    calls do (the HUD's phone icon; `UI_ComDevice.ActivatePhoneElements`, a
    UI board). A call of the game's comes first: while one rings or is on,
    ours is not drawn (the link and its keys unchanged). No wrap and no
    callback: everything runs on `TandemComms.Tick` (the game thread); the
    hang-up key's listener (`TandemComms.OnAction`, already in
    `scripts-threads.known`) only sets a flag the tick acts on.
  - The party HUD moves below our call widgets as below the game's
    (`TandemComms.HolocallUp`, [`presence.md`](presence.md)).
  - To see in game: that the spawned widgets sit and animate as the game's
    (the slots' layout without the game's call state), the tint on the
    unknown avatar, the status line, the ring animation's length.
  - Test commands: `comms holocall` (what is shown), `comms hangup` (as the
    key). Scenarios (not run yet): `phone-holocall-ring` (their call rings
    with the widget and their name, answered, the view closes, the link
    stays, the party list back under the health bar), `phone-holocall-call` (our call connecting, answered, hung up
    with the call's button).
- **How, in the game's scripts** (`final.reds:408140`, wrapped, each a
  `public final func` of the HUD controller, reached from its own input
  listener on the game thread; none is a cb func or a per-entity class,
  `tools/scripts-threads.classes` has its ancestry):
  - `SetScreenType`: after the game fills the Contacts tab, our rows
    (`ContactData`, `type` Contact, `isCallable`, an id of ours
    `tandem:PLAYER[:ACTION]`) go first and the list is reset in that order
    (its sorting is off again by then);
  - `CallContact` (the call key) and `ExecuteAction` (the messages key, a
    pushed row): a row of ours is ours to act on, before the game's
    `isCallable` and `IsCallingEnabled` checks (a quest's call lock does not
    lock our rows);
  - `Back`, `SelectOtherTab`: the game's `PopList` sorts the list it kept,
    so the players are put first again;
  - `GotoSmsMessenger`, `RefreshSmsMessager`: never on a row of ours (the
    game opens the messenger for any row of a pushed list).
- **Nothing new crosses games**: each row is the message its feature sends
  (`Link`, `Trade`, `Regroup`). No protocol change.
- Test commands: `tools/game.sh cmd phone` (every player's row and actions,
  in English), `phone open` (the contact list, as the key's hold), `phone
close`, `phone pick TEXT` (a row of the open list by its text, chosen as
  the call key does; never one of the game's contacts), `phone call | trade
| join | invite NAME` (a row's action without the list); `comms decline`.
  Scenarios (not run yet): `phone-link` (our call answered, hung up),
  `phone-ring` (theirs rings here, declined, answered from the phone),
  `phone-trade` (a trade 40 m apart, as a guest), `phone-regroup` (ask
  them here, declined; join them).
- To see in game: the rows' look (the Contact template's preview line, the
  call hint's glyph, the unknown avatar), that a reset with sorting off
  keeps our order, `PhoneReject`'s binding while no call is up, the
  ringtone stopping with the request.

## Protocol

Four messages, appended after `ShotHit` (no version bump; the `presence`
branch appends its own three: whichever merges second goes after the
other's, before either ships). All on the Event channel.

| Message    | Kind     | Size  | Carries                                                                                                                             |
| ---------- | -------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `Mark`     | one-shot | < 40  | a ping: sender, its id, kind (spot, enemy, device, loot, vehicle, danger, team target; none: taken back), target, position, seconds |
| `Tag`      | one-shot | < 200 | a scan tag (until off) or a reveal (seconds) of up to 16 targets, on or off                                                         |
| `Link`     | state    | small | whom a player consents to share tags with: nobody, the listed players, everyone but the listed                                      |
| `Waypoint` | state    | < 32  | a player's waypoint: none, a map pin, a tracked map point, its tracked objective (protocol 9); where                                |

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
  - the key: a listener on `TagButton` (and on `PhoneInteract` and
    `PhoneReject` while a link request is shown); the kind from what V looks at (a hostile
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
accept | tag`, `comms decline` (a link request) and `comms objective`
  (the tracked objective we would share, and whether a waypoint tracked
  by hand takes its place).

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

- The party HUD's linked entries: scenario `hud-linked` (the mock player's
  `--unlink-at N` breaks its links), presence.md.

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
  `--tag N`, `--reveal N`, `--waypoint X,Y,Z`,
  `--waypoint-kind pin|point|objective`, `--waypoint-clear N`; it logs
  every mark, tag, link and waypoint it hears ("player N's waypoint:
  objective at …").
- Scenarios (not run yet): `comms-pings` (its spot, danger, withdrawal;
  ours; it vanishes), `comms-enemy` (as a guest: an enemy ping on our
  proxy, reveals outlined), `comms-link` (link = ask on the other side:
  accepted, broken, again), `comms-waypoint`, `comms-objective` (the
  host's tracked objective heard by the mock player; the mock's shown as
  "tandem-peer's objective").

## Open

- Everything in game: the input outside the scanner (keyboard and pad),
  the mappins in each of the three places with their icons and labels
  (the parts chosen in `TandemComms.Icon` are a first pick by name), the
  sounds (by ear), the outlines of a shared tag and of a reveal on a proxy,
  the input hint beside a regroup offer's.
- The Ping quickhack's network beams are shared (below, "The Ping's
  network"), not run in game. Devices revealed by other means (a scan's
  own pulse, a breach) are not shared.
- A tag of an entity that exists only in one game (a dynamic one that is
  not a shared NPC: a car copy, a body) is not sent.
- Localized strings for the notices and labels.
