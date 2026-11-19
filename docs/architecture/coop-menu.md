# Co-op menu and invites

## Inside the plugin

- **Co-op menu** (`scripts/tandem/Coop.reds`, milestone 7): the session is
  chosen at runtime. The main menu gets a "Co-op" entry whose pages live in
  the menu's own list (wraps of `SingleplayerMenuGameController`'s
  `PopulateMenuItemList` / `HandleMenuItemActivate`): Host a game → the
  vanilla Continue or Load game (the game's own load list,
  `SpawnEvent(n"OnLoadGame")`); Join a game → recent hosts (`recent.txt`
  next to the DLL, written when a host welcomes us) or "Enter an address"
  (the game's message popup with Codeware's text field,
  `GenericMessageNotification.ShowInput`), then the V's save; Play solo
  drops the choice. The first time a player hosts or joins (main menu Host
  a game / Join a game, pause menu Host this game / Join a game) while
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
  end (`StoryInbound::Ended`) so a guest's own story comes back. Both menus
  show a status panel (state, everyone with their ping:
  `Endpoint::LinkStatsOf`, `TandemNative.SessionStatus` /
  `SessionPlayers`) and notices in game ("NAME joined", "left", "session
  ended"). Menus keep their own clock (a looping ink animation: our
  system's delayed tick does not run in the main menu nor while paused),
  which also takes `tools/game.sh cmd coop ...` commands: `coop status`,
  `coop host [PORT]`, `coop join ADDRESS[:PORT]`, `coop leave` (in game,
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

### Settings

- **The page** (`scripts/tandem/Settings.reds`, both menus' Co-op page →
  Settings, the game's own `UI-Labels-Settings`): one item per setting,
  "Name: V". Picking an on/off or a choice moves to the next value; a
  typed one (name, relay, relay host token: never shown, "set" or "not
  set") opens the message popup with Codeware's text field. Each change
  goes through `TandemNative.SetSetting(key, value)`: the plugin checks it
  with tandem.ini's own parser (`ApplySetting`, `plugin/src/config.cpp`),
  saves that one line (`tandem::settings::SetKey`, `core/`, tested: the
  other keys, the comments and the line endings stay; a key only in a
  comment is appended, not uncommented; a value with a line break, `;` or
  `#` is refused) through a file written aside, and applies it.
  `TandemNative.Setting(key)` reads one back.
- **When it applies**: at once for `recorder`, `friendly_fire`, `notices`,
  `markers`, `nameplates`, `party_hud` (the scripts read them as they need
  them; the nameplates and the party list every frame);
  `name`, `relay`, `relay_token`, `story`, `time_host` are what a running
  session was set up with, so a change during one waits for its end (the
  label says "from the next session"); `quest_freeze` and `scene_privacy`
  choose hooks installed at launch ("after a restart").
- **What they do**: `friendly_fire` is the host's rule for the whole
  session (`friendly_fire = off` by default; `combat.md`, `Rules`): on a host's page it
  changes the rule at once (the host sends `Rules` again, every game
  follows); on a guest's page in a session it shows the host's value,
  "(the host's rule)", and cannot be changed there (its own tandem.ini's
  value counts when it hosts). `notices = important` hides news (rides, calls, pauses,
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
- **Only the page's keys** (`settings::IsEditable`): never `role`,
  `address`, `port` or `join`, which the co-op menu chooses, nor the test
  harness's keys; `TANDEM_PORT` and `TANDEM_CONFIG` keep working (the page
  writes the file the game read, `Config::file`). The harness's
  `tools/game.sh role` files swap around it: a setting changed while a test
  `tandem.ini` is in place lands in that one. Tests: `coop settings` logs
  them all, `coop set KEY VALUE` changes one (scenario `settings`).
- Not yet verified in game. Not done: the Mod Settings mod's page (it keeps
  its own saved values, which would compete with tandem.ini's: one source of
  truth was kept).

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

The flow: **host** — the menu's "Host co-op" loads one of the player's saves
with `role = host` and shows `RelayCode()` (with a copy button and the
code's state from `RelayStatus()`), refreshing it when it changes; the code
is what the player sends a friend. **Join** — "Join co-op" asks for the code
(6 characters, a dash optional), calls `JoinByCode`, loads the guest's own
save and waits for event 4, showing `RelayStatus()` and the end reason on
failure. `scripts/tandem/Coop.reds` does this (the co-op menu above, which
also hosts and joins by address). Still missing: a "new code" request (the
wire has `renew`; `RelayLink` does not expose it yet), and a relay address
shipped by default (today `relay =` is set by hand).

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
