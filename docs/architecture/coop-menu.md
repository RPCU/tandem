# Co-op menu and invites

## Inside the plugin

- **Co-op menu** (`scripts/tandem/Coop.reds`, milestone 7): the session is
  chosen at runtime. The main menu gets a "Co-op" entry whose pages live in
  the menu's own list (wraps of `SingleplayerMenuGameController`'s
  `PopulateMenuItemList` / `HandleMenuItemActivate`): Start a new co-op
  game (New co-op game, below); Host a saved game → the
  vanilla Continue or Load game (the game's own load list,
  `SpawnEvent(n"OnLoadGame")`); Join a game → recent hosts (`recent.txt`
  next to the DLL, written when a host welcomes us) or "Enter a join code"
  (the game's message popup with Codeware's text field,
  `GenericMessageNotification.ShowInput`; an address typed there works
  too), then the V (a new one, or a save); Play solo
  drops the choice. **Join by address** (both menus' Co-op page, after
  Join a game; for a LAN or a VPN such as Tailscale or NetBird, without the
  relay): the same popup asks for `host[:port]` (port: tandem.ini's,
  27450), with the last address as the empty answer; `TandemNative.AddressProblem`
  (`hosts::Check`: a host name of valid labels, a dotted IPv4 address; an
  IPv6 one, bracketed or not, refused in words since ENet speaks IPv4
  only) says why an answer is refused (`Address-*` texts, in a message popup); a good one is saved as `last_address` (`SetSetting`) and joined
  like `role = join` with `address`: the main menu's save page, the pause
  menu at once (`TandemSystem.CoopJoinDirect`). `coop address [TEXT]` is
  its answer without the popup, in both menus (scenario
  `join-address`). The first time a player
  hosts or joins (main menu Host a game / Join a game / Join by address,
  pause menu Host this game / Join a game / Join by address) while
  `name` is still `V`, the menu first asks for a pseudo in the same popup
  (title "Your co-op name", its rules; refused: asked again with the
  reason; cancel: `V` for now), saved with `SetSetting`, once a game run
  (`TandemNative.NameToAsk` / `NameAsked`; `presence.md`, Names). Picking the save calls `TandemNative.StartHost(port)` /
  `Join(address, port)`, which only choose; `TandemSystem` starts the
  session when the save loads. The pause menu gets "Co-op" too: Host this
  game / Join a game (start at once), Invite (this machine's addresses and
  port) / Leave session. `TandemSystem.BeginSession` /
  `EndSession(worldStays)` are the start and stop: a runtime leave removes
  puppets step by step, deletes the proxies and gives the crowd back, the
  net thread stops (its rejoin loop with it), and the story side gets an
  end (`StoryInbound::Ended`) so a guest's own story comes back. The pause
  menu's own Exit (to the main menu, Close game) as a guest leaves and
  saves the V first, or asks when no save can be made
  ([`progression.md`](progression.md), Leaving). Both menus
  show a status panel (state, everyone with their ping:
  `Endpoint::LinkStatsOf`, `TandemNative.SessionStatus` /
  `SessionPlayers`) and notices in game ("NAME joined", "left", "session
  ended"). Menus keep their own clock (a looping ink animation: our
  system's delayed tick does not run in the main menu nor while paused),
  which also takes `tools/game.sh cmd coop ...` commands: `coop status`,
  `coop host [PORT]`, `coop join ADDRESS[:PORT]`, `coop address
[ADDRESS[:PORT]]`, `coop leave` (in game,
  any time), `coop menu PAGE`, `coop pick TEXT` (drive an open menu),
  `coop name [reset]`, `coop name answer TEXT`, `coop name cancel` (the name
  prompt; scenario `names-prompt`).
  `tandem.ini`'s role stays the default (and the harness's): it is chosen
  at load, a menu choice replaces it until the player exits to the main
  menu. The engine hooks are installed whatever the role (they pass
  through without a session) so a session can start at any time. Join
  codes: what is typed is a code when `TandemNative.IsJoinCode` says so and
  it has no dot or colon (`TandemCoop.Resolve`), joined with `JoinByCode`
  (which sets the choice, `SessionChoice::joinCode`); the host's panel
  and invite show `RelayCode()` and a relay problem from `RelayStatus()`.
  **Not yet verified in game** (scenario `coop` for the runtime switches;
  the menus by hand or with `coop menu` / `coop pick`).

### New co-op game

Two friends starting a campaign together, in the fewest steps, the game's
own screens doing the rest (`Coop.reds`, `Travel.reds`, 2026-11-21, **not
yet run in game**).

- **Host**: main menu Co-op → **Start a new co-op game** (the name popup
  first, the first time) → the game's own New Game (`SpawnEvent(n"OnNewGame")`,
  as the menu's New game: Phantom Liberty's start choice, difficulty,
  lifepath, character creation; engine facts §13.130), the session chosen
  before it (`StartHost`). It starts as the new V's world loads, as a load
  does (`TandemSystem.OnPlayerAttach`). Through a relay the host's game
  says nothing at the start; as soon as the relay registers the code, the
  HUD's message shows it, 12 s, "Co-op join code: K7Q-2M9" (`Session-Code`,
  `TandemSystem.ShowCode`), again when it changes (New join code); without
  a code, the relay's problem once ("No join code yet: …", `Session-NoCode`).
  It stays on the pause menu's status panel and in Invite, which with a
  code says the code alone ("friends enter it in Co-op, Join a game"; the
  addresses only without a relay). No relay set: the old notice with this
  machine's addresses.
- **Guest**: Co-op → **Join a game** → the code's popup at once when there
  is no recent host to pick (else the recent hosts' page, then Enter a join
  code) → the V page: **Create a new V** (the game's own New Game, as the
  host's), Continue or Load game (a saved V). Picking one chooses the join
  (`JoinByCode` / `Join`) and its **arrival** (`TandemNative.ChooseArrival`,
  `SessionChoice::arrive`, kept by the plugin across the New Game flow and
  the load like the choice itself, taken once at the welcome:
  `TakeArrival`). Every join the co-op menu makes arrives, in game too
  (`TandemSystem.CoopJoin`: the pause menu, `coop join`); tandem.ini's
  `role = join` and a checkpoint's rejoin do not.
- **The arrival** (`TandemTravel.ArriveBeside`, travel.md): at the welcome
  the joined notice waits; once our V is free to go (not in a scene of tier
  3 and up, a vehicle, or down; 90 s at most, then the notice and the
  regroup's usual ways), it goes 2 m behind the host **behind the fast
  travel's loading screen** whatever the distance, no prompt, no fast
  travel point needed; within 60 m it stays. Then one notice, "Joined
  NAME's session".
- **A fresh guest's prologue** (the guest's own story, `story.md`): a join
  the menu chose holds the guest's own quests **from the load**, not from
  the welcome (`TandemStorySystem.HoldEarly`: the quest freeze and the save
  lock), so the new V's lifepath start does not run on while it connects;
  a session that ends before the welcome gives them back (`ReleaseEarly`).
  What is known and what is not (engine facts §13.130):
  - nodes that ran before the player's attach (the lifepath's opening
    scene, its first items and restrictions) have run: a scene already
    playing plays out (it is not a quest node), and the arrival waits for
    it;
  - the arrival takes every gameplay restriction off our V, as vanilla's
    fast travel does (no weapons, no sprint, forced walk of the opening are
    the frozen prologue's, never lifted otherwise while frozen); the host's
    own moments put theirs on again (`Restrictions.reds`);
  - the new V has what its lifepath gave it by then (maybe no weapon, no
    money, no cyberdeck); the prologue's later gifts wait in its own story;
  - the new V's world is in the Watson lockdown (§13.112): the arrival
    crosses it as a ride in the host's car does (the gates' notices still
    say so);
  - arriving inside a host's quest interior relies on the prefab mirror
    (`story.md`) loading it first; a Phantom Liberty start (`ep1_standalone`,
    Songbird's holocall) is not tried.
    To see in game: scenarios `coop-new-host`, `coop-new-guest` (by hand: the
    character creation), `coop-arrive`, `coop-arrive-load`, `coop-code`
    (needs a relay: `RELAY=HOST:PORT`).
- **Level gap**: a level-1 guest beside a veteran host: progression.md,
  "Levels" (`level_gap`).

### Settings

- **The pages' entries** (`TandemCoopMenu.Build`): main menu Co-op → Start
  a new co-op game, Host a saved game, Join a game, Join by address, Play
  solo (once one is chosen), Write a bug report, Co-op settings, Back; the
  join's V page: Create a new V, Continue, Load game, Back; pause menu Co-op → Host this game,
  Join a game, Join by address, or in a session Invite and New join code
  (host), Partners' mods, Leave session; then Write a bug report, Co-op
  settings, Back. No entry reads as one of the game's own or another mod's
  ("Settings", "Mods", "Mod Settings": `Menu-Settings` is "Co-op settings", not the game's `UI-Labels-Settings`,
  `Menu-Mods` "Partners' mods"). On every page but the root the list holds
  our items alone (`TandemCoopMenu.Tidy`, at each page and clock tick):
  the Mod Settings mod appends its entry wherever the game's Mods entry is
  missing, and `ShowActionsList` its Close game.
- **The page** (`scripts/tandem/Settings.reds`, both menus' Co-op page →
  Co-op settings): its groups, as the game's settings have tabs, so that
  no list runs past the menu's panel: Session (name, friendly_fire,
  enemy_scaling, level_gap, story), Gameplay (quest_freeze, scene_privacy,
  time_host, race_car), Interface (notices, markers, nameplates,
  party_hud), Scans and pings (link, pings), Advanced (recorder, relay,
  code, direct); each opens a page of one item per setting, a short label
  on one line, "Name: V" (a typed value past 18 characters is cut, the
  description shows it whole). Back and Esc go to the list of groups, on
  the group left; a change keeps the cursor on its row. Beside the list,
  under the status lines and apart from them (a rule, then the row's name,
  what it does in the game's settings info grey, when a change takes
  effect in yellow, as the game's settings say "requires a restart"): the
  description of the row under the cursor (hovered, else selected;
  `Settings-About-*`, `Settings-Group-About-*`, `TandemSettings.About`,
  `Note`). Its arrays (`Keys`,
  `Labels`, `Abouts`, `Kinds`, `Whens`, `Groups`) have one entry per setting in the
  page's order: name, friendly_fire, enemy_scaling, level_gap, notices, markers,
  nameplates, party_hud, link, pings, recorder, relay, code, direct, story,
  quest_freeze, scene_privacy, time_host, race_car. `tools/menu-check.py`
  (`task menu:check`, run by `scripts:check` and CI) checks them aligned,
  every group named and described and none empty,
  each key taken by `ApplySetting` and `settings::IsEditable`, every label,
  description and choice text in English and French, no two entries
  reading the same, nothing registered with Mod Settings, and no value
  read straight from an array a call returned (`Labels()[i]` gave an empty
  String in game: every row read ": V", 2026-11-20). Picking an on/off or a choice moves to the next value; a
  typed one (name, relay, relay host token: never shown, "set" or "not
  set") opens the message popup with Codeware's text field. Each change
  goes through `TandemNative.SetSetting(key, value)`: the plugin checks it
  with tandem.ini's own parser (`ApplySetting`, `plugin/src/config.cpp`),
  saves that one line (`tandem::settings::SetKey`, `core/`, tested: the
  other keys, the comments and the line endings stay; a key only in a
  comment is appended, not uncommented; a value with a line break, `;` or
  `#` is refused) through a file written aside, and applies it.
  `TandemNative.Setting(key)` reads one back.
- **When it applies**: at once for `recorder`, `friendly_fire`, `enemy_scaling`, `notices`,
  `markers`, `nameplates`, `party_hud`, `race_car` (the scripts read them as they need
  them; the nameplates and the party list every frame), `link` and `pings`
  (`Comms::Configure`: in a session a new link default replaces our
  consents and is sent at once; pings off stops new pings, a team target
  still shows; `comms.md`);
  `name`, `relay`, `relay_token`, `story`, `time_host`, `level_gap` are what a running
  session was set up with, so a change during one waits for its end (the
  description says so, `Settings-NextSession`); `quest_freeze` and `scene_privacy`
  choose hooks installed at launch (`Settings-AfterRestart`).
- **What they do**: `friendly_fire` is the host's rule for the whole
  session (`friendly_fire = off` by default; `combat.md`, `Rules`): on a host's page it
  changes the rule at once (the host sends `Rules` again, every game
  follows); on a guest's page in a session it shows the host's value,
  the description says it is the host's rule, and it cannot be changed there (its own tandem.ini's
  value counts when it hosts). `enemy_scaling` (on by default; `npcs.md`,
  "Enemy scaling") is the host's rule the same way: hostile NPCs tougher for
  each extra player near them; its health per player,
  `enemy_scaling_health`, is not on the page (tandem.ini only).
  `notices = important` hides news (rides, calls, pauses,
  players elsewhere, loot), `minimal` keeps only what asks for an answer or
  went wrong (`TandemText.Importance`); every notice is logged either way.
  `markers = off` takes the other players' markers off (HUD, minimap, map);
  `nameplates` cycles near (to 12 m), always (any distance, smaller and
  fainter) and off (no name over them, the marker's label neither);
  `party_hud = off` hides the party list (`presence.md`, Layout); a
  tandem.ini of before with `on` reads as near. The name's popup says its
  rules ("1 to 16 characters…", `Name-Ask`) and a refusal why
  (`Name-Invalid`); `tandem.ini`'s parser checks it (`names::Clean`) and
  saves it trimmed.
- **Only the page's keys** (`settings::IsEditable`, and two it saves for
  the menu: `code_salt`, `last_address`): never `role`, `address`, `port`
  or `join`, which the co-op menu chooses, nor the test harness's keys; `TANDEM_PORT` and `TANDEM_CONFIG` keep working (the page
  writes the file the game read, `Config::file`). The harness's
  `tools/game.sh role` files swap around it: a setting changed while a test
  `tandem.ini` is in place lands in that one. Tests: `coop settings` logs
  them all, `coop set KEY VALUE` changes one (scenario `settings`).
- The pause menu's page seen in game 2026-11-23 (English; scenario
  `settings-page`, its shots). Esc (the simulated key) there closed the
  whole pause menu instead of going up a page: to look at by hand. French
  lengths not seen. Not done: the Mod Settings mod's page (it keeps
  its own saved values, which would compete with tandem.ini's: one source of
  truth was kept). **Mod Settings: not used, by decision 2026-11-19**
  (the user: no duplicated settings menu), even when it is installed: this
  page is the only one ([`mod-compat.md`](mod-compat.md)).

### Mods

- In a session the pause menu's Co-op page has **Partners' mods**
  (`TandemCoopAction.Mods`, [`mod-compat.md`](mod-compat.md)): a popup
  with each player's mods against ours in words, their names once asked
  (opening it asks; they arrive as a notice, shown the next time), and
  every record shown otherwise this session (whose, which, as what). The
  status panel has one line per player whose mods differ.
- **What the game runs without** (`degradations.md`): the status panel
  keeps a line per state that holds ("Runs without: …", "(your setting)"
  for a `tandem.ini` switch), per fallback taken ("Fell back N time(s):
  …") and per state of another player's game ("NAME's game runs without:
  …"), solo too (`TandemDegradedSay.StatusLines`). With a core feature off
  the page opens with the popup of the startup problems, the feature and
  its reason in the player's language.
  `tools/game.sh cmd coop mods` logs the same, `coop mods names` asks for
  the names.

### Invites: the in-game flow (for the main menu)

The co-op main menu (another piece of milestone 7) builds on three natives
(`scripts/tandem/Natives.reds`), all safe to call every frame from the
game thread:

- `TandemNative.RelayCode() -> String`: the host's join code to show,
  `"K7Q-2M9"`; `""` while there is none (no relay set, not registered yet,
  or the code just expired and a new one is on its way).
- `TandemNative.RelayStatus() -> String`: `""` no relay, `"connecting"`,
  `"ready"`, or what went wrong in words (`"the relay did not answer"`,
  `"the relay refused our host token (relay_token)"`, `"no session has this
join code (wrong, or expired)"`, `"too many join attempts: try again in a
minute"`…), to show as is.
- `TandemNative.JoinByCode(code: String) -> Bool`: become the guest of the
  session the typed code names, through `tandem.ini`'s relay (false: not a
  code, or no relay). A running session is left for it at once; otherwise it
  starts when the next game is loaded (`TandemNative.Start`). The outcome
  arrives as the usual `TandemEvent`s (4 in the session, 3 ended with a
  reason).
- `RelayCodeKept() -> Bool` (the code is the host's kept one: "Join code:
  K7Q-2M9 (kept: the same next time)"), `RelayNewCode() -> Bool` (the pause
  menu's "New join code", for a host with a code: a new one now; a kept
  code's next salt, saved as `code_salt`), `RelayCheck() -> String` (the
  connection check: `"direct"` likely, `"relay"` only, `""` unknown) and
  `RelayDirect() -> Int32` (direct paths up), shown as status lines
  (`TandemCoop.ConnectionLines`). See [The relay](relay.md).

The flow: **host** — the menu's "Host co-op" loads one of the player's saves
with `role = host` and shows `RelayCode()` (with a copy button and the
code's state from `RelayStatus()`), refreshing it when it changes; the code
is what the player sends a friend. **Join** — "Join co-op" asks for the code
(6 characters, a dash optional), calls `JoinByCode`, loads the guest's own
save and waits for event 4, showing `RelayStatus()` and the end reason on
failure. `scripts/tandem/Coop.reds` does this (the co-op menu above, which
also hosts and joins by address). Still missing: a relay address shipped
by default (today `relay =` is set by hand). (The "new code" request, once
missing because `RelayLink` did not expose the wire's `renew`, is the
pause menu's New join code since 2026-11-19.)

## Open problems

- **Co-op from the main menu.** Today a session is set up in `tandem.ini`
  (role, address). Goal: a co-op entry in the game's main menu, where a
  player hosts (with one of their saves) or picks whom to join — friends'
  sessions, a join code from an invite, the relay for players behind NAT —
  and then loads straight into the host's world with their own V. Options
  (name, privacy, max players) live there too; `tandem.ini` stays for
  tests. Part of milestone 7 (invites, relay). Done: the relay and join
  codes (see [Invites](#invites-the-in-game-flow-for-the-main-menu)); and
  (2026-11-18, to verify in game) everything in Inside the plugin, co-op
  menu. Still open: testing a host before loading (today a guest learns in
  game that the host is unreachable), options (name, max players), a lobby
  list, the entry's place in the list (appended after the menu's own
  items: an insertion would need a wrap of the overloaded `AddMenuItem`).
  Labels are keyed texts (`strings.md`, done 2026-11-19); `coop pick TEXT`
  matches a label in the game's language or in English.
