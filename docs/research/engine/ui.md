# HUD, UI, archives, tweaks, saves, Steam, menus

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 9. HUD / UI / archives / tweaks / saves / Steam

- **inkhud override staleness** (`MAP:2407`): adding a HUD entry means replacing the whole
  `base\gameplay\gui\prototype_hud.inkhud` in your archive; every game patch makes it stale. NCO's 2024
  copy on 2.5 lacked `police`, `reaper_mq062`, `cyberware_mq062` and aimed the car HUD at a slot 2.5
  renamed. Procedure: unbundle from `basegame_1_engine.archive` (WolvenKit CLI), `convert serialize`,
  insert entry (`multiplayer_ui` after `new_phone`), `convert deserialize`, repack. 2.5's file has 72
  entries (73 with ours); the speedometer slot was renamed `BottomLeftSpeedometer`; 8 other entries
  changed contexts/margins (`1308b165`). [broken-on-2.5 if stale; rebuilt file unverified in play]
- `AsyncSpawnFromExternal` of a CLI-authored `.inkwidget` hard-crashed the client seconds after spawn;
  spawning library widgets from a game-authored file works (`6f6ed6b9`, 2.31). An `inkImage` binds an
  `.inkatlas`, never a raw xbm; ship extra content as a separate `zz_*.archive` (`1a4d06a8`).
- HUD widgets have no UI context: `navigate_up`/`proceed` never fire there; custom input actions
  appended to `UIBase` in the input_loader XML do fire in gameplay (`9fb1bfe1`). A modal on a HUD widget
  (`PushGameContext(ModalPopup)` + cursor visibility + `radialMenu` dilation) blocked typing but gave no
  cursor/freeze — a real cursor needs a menu controller (`6e1c52a6`). Never make a full-screen canvas
  interactive (soft-lock, `d6240d9d`).
- Ink coordinates: menus author at 1920×1080 (menu root is a fixed 3840×2160 virtual space on 1080p and
  1440p); `GetScreenSpacePosition` returns real display pixels (clicks were 1.333× off at 1440p);
  `GetWindowSpacePosition` is dead (`a90ef259`, `3082f066`). `ToString(inkActionName)` prints the type,
  not the value (`d22e0fe9`).
- `@addMethod/@addField/@wrapMethod` attach only to pre-compiled game classes (`d64ffe2f`); adding a field
  to a _native_ class = "constant pool error"; no `continue`; no Int64→Int32 cast; class params need
  `ref<>`/`wref<>`; a native struct event field needs a matching `public native let` (`8a50a9c1`).
- `FTLog` output reaches no collectable file and `LogChannel` needs CET → route script logs through a
  native function into your own log (`534ca258`, `4a41f34c`).
- Steam: a Steam copy launched while Steam is closed boots Steam and relaunches itself **bare**,
  dropping custom command-line args (`9788e383`). The exe's ProductVersion reads "2.5" (RED4ext says
  "2.50"); FileVersion is 3.0.80.x (`af2a3661`, `d3a04459`).
- CET was treated as incompatible: CET's ImGui overlay plus the mod's DX12 ImGui overlay on the same
  present path hard-locked the GPU (`46f61a5b`, `d014c945`). ImGui 1.92 stopped filling
  `ImDrawData::CmdListsCount` (use `CmdLists.Size`) (`75c5f080`).
- Pause: `PauseMenuBackgroundGameController.OnInitialize` queues SetMenuModeEvent then calls native
  `GetSystemRequestsHandler().PauseGame()` (can't wrap); unpause after a 0.25 s delay (`197d02ca`).
- Difficulty lives in UserSettings `/gameplay/difficulty` var `GameDifficulty`, list
  `["Story","Easy","Hard","VeryHard"]` (`858d723d`).
- TweakDBID = CRC32(name) | (length << 32) (not FNV); verify by hashing and searching
  `r6/cache/tweakdb.bin` (`31590288`). Lifepath enum {Corporate 0, Nomad 1, StreetKid 2}; a fresh save
  reads `Invalid` = 4294967295 / −1 (`9e37b8ff`).
- HUD canvas stays at authored `(0,0)` until a `to_vehicle`/`from_vehicle` animation moves it — play
  `from_vehicle` at connect or the custom HUD draws top-left (`MAP:1495-1510`).
- A HUD widget spawned on the menu root swallows all input (`MAP:189`).
- `inkWidget` parentWidget 0x168, layerProxy 0x148 (2.31 decode, `NetworkWorldSystem.cpp:769`). [2.31-only]
- WolvenKit JSON traps: entry names at `hudEntryName.'$value'`; library tree at
  `item.package.Data.File.RootChunk.rootWidget`; `HandleRefId` indirection (`MAP:2406`).
- redscript: ONE compilation unit — a single bad file (BOM, hex literal `0xFF`, Int32/Uint32 mismatch,
  missing `native func` for an RTTI method) boots the game with **no scripts**; game script dialect ≠
  redscript (`ref<T>` params, `script_ref<String>`); don't run `scc.exe` directly (pops a blocking dialog)
  (`MAP:2402,2415`, `CONTRIBUTING.md:96-123`). `import X.*` can shadow game classes.
- TweakXL skips an unresolvable record and logs it (doesn't kill the file) (`tweak:336-337`).
- Input contexts: custom `playerStateMachineInputContext.ui*Context` records with `alias
["allUIContexts"]`, `forceInputContext` names (`tweak:243-323`); Input Loader required.
- Plugin must not be symlinked (resolves its own path to find assets; failure in `Load` unloads it and
  all scripts silently vanish); first diagnostic `red4ext/logs/red4ext-*.log` (`MAP:446-464`). Install
  folder `red4ext/plugins/zzz…` to load after Codeware/ArchiveXL/TweakXL (`CONTRIBUTING.md:80-81`).
- Script validator hook: `ScriptValidator::Validate` **898639042** (CW same name) used to register RPC
  classes; 2.5 script-bundle layout from CW `b054150f`: `ScriptDefinition` 0x18→**0x40**, `ScriptClass`
  +0x20 before `functions` (parent 0x48, properties 0x50, functions 0x90, flags 0xD1), `ScriptFunction`
  0xF8 (params 0x58, flags 0xC0), `ScriptBundle` leading pointer (classes 0xD8, types 0xE8)
  (`Game/ScriptValidation.h`). [2.5-verified]
- Rendering hooks: `CRenderNode_Present_DoInternal` **2468877568** (addr: `GpuApi::Present`),
  `CRenderGlobal_Resize` **239671909** (addr: `GpuApi::ResizeBackbuffer`), `CRenderGlobal_Shutdown`
  **3192982283**; get swapchain/queue from `RED4ext::GpuApi::GetDeviceData()` + `swapChains.GetData(id)`
  (1-based id, check IsValidID/IsUsedID) — the 2.31 hand-located render context (displacement at
  present+0x1F; devices 0xC97F38, queue 0x13BC4D0) moved on 2.5 (swapchains 0xCEBF30, queue 0x14104D0)
  and crashed 1.5 s after load with nothing failing at compile time (`Game/Rendering.h`, `60a8ba47`).
  [2.5-verified]
- 2.5 `ScriptFunction` `isNative` bit at 0xC0 (unasserted); game shows at most 9 "Missing native
  function" errors (`bdeed85f`). Validator-hook crash signature on the old layout: AV reading 0x0
  313 ms after scc (`f42065f8`).
- Saves: `SaveLocksManager` lock; loading is not covered.
- Boot: `-skipStartScreen` (`MAP:200-201`). Launch args must be `--key=value` (`MAP:2399`).
- Pause menu unpause must be deferred (0.25 s) — `SetMenuModeEvent` is consumed a frame later (`MAP:719-728`).
- Steam networking: NCO uses GNS directly over UDP (port 11778), no Steam relay.

**CAMP SteamTunnel / CoopConnect** (`code/client/App/World/{SteamApi,SteamTunnel,CoopConnect,ServerHost}.cpp`,
`d639f03`, `179a6ec`; 2.31, untested)

- Uses the **game's own `steam_api64.dll`** via `GetModuleHandleW` + flat exports, no SDK link:
  `SteamAPI_SteamFriends_v017`, `SteamAPI_SteamNetworkingMessages_SteamAPI_v002`, `SteamAPI_SteamUser_v021`,
  `SteamAPI_SteamNetworkingUtils_SteamAPI_v004`, `SteamAPI_ISteamUser_GetSteamID`,
  `SteamAPI_RegisterCallback`, `…ActivateGameOverlayInviteDialogConnectString`, `…SetRichPresence`,
  `…ISteamNetworkingMessages_SendMessageToUser/ReceiveMessagesOnChannel/AcceptSessionWithUser/
CloseSessionWithUser`, `SteamAPI_SteamNetworkingMessage_t_Release`, `…InitRelayNetworkAccess`
  (`SteamApi.cpp:57-73`). [unverified on 2.5 — check the versioned export names in 2.5's DLL; no
  GOG/Epic path]
- Callbacks: a `CCallbackBase` mimic dispatched by the game's own `SteamAPI_RunCallbacks` on the main
  thread; `SteamNetworkingMessagesSessionRequest_t` id **1251**, `GameRichPresenceJoinRequested_t` id
  **337** (`{u64 friendId; char connect[256]}`, packed 8). `SteamNetworkingIdentity` (packed 8, 136 B,
  type 16 = SteamID); message_t `{data@00, size@08, conn@0C, peer@10}`. Send flags `1|32`
  (UnreliableNoNagle | AutoRestartBrokenSession).
- Tunnel: GNS UDP datagrams carried unchanged over `ISteamNetworkingMessages` (Valve relay when no direct
  path); channels guest→host **0x4350**, host→guest **0x4351**; max datagram 2048 B; guest binds a
  non-blocking UDP socket on 127.0.0.1 and the client connects to it; host keeps one loopback socket per
  guest SteamID so the real server sees distinct clients; idle 120 s → closed; worker thread detached
  (a joinable one `std::terminate`s at game exit), 1 ms sleep. Windows reports ICMP port-unreachable on
  the next `recvfrom` → `WSAIoctl(SIO_UDP_CONNRESET, FALSE)` (`2f3dfae`). Under Proton, Windows-socket
  semantics come from Wine — untested.
- Embedded server: `Server.Loader.exe` started outside `red4ext\plugins` (RED4ext would load its DLLs),
  suspended + `CREATE_NO_WINDOW`, in a Job object with `KILL_ON_JOB_CLOSE`, needs .NET 8 runtime.
- Invite: `--online --steamhost=<SteamID64> --coopjoin` or `--ip= --port=`; rich presence keys
  `connect`/`status`.

### 13.25 The main menu and the pause menu, for a co-op entry (2026-11-18, read in final.reds, not run)

Line numbers are `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` (2.50).
All [2.5-untested]: written into `scripts/tandem/Coop.reds`, not yet seen
in game.

- Both menus are `gameuiMenuItemListGameController`s (`:275489`): a
  `ListController` (`m_menuListController`) fed `PauseMenuListItemData`
  (`:275579`: `label`, `eventName`, `action: PauseMenuAction`, `:7937`).
  `ShowActionsList` (`:275561`) clears the list, calls the subclass's
  private `PopulateMenuItemList`, appends Quit (`UI-Labels-CloseGame`) when
  `CanExitGame`, and refreshes. `PopulateMenuItemList` does not clear by
  itself (NCO 7b4c9531). A subclass of `PauseMenuListItemData` pushed into
  the list shows its `label` like the vanilla items.
- Main menu: `SingleplayerMenuGameController` (`:385861`):
  `PopulateMenuItemList` (`:386507`: Continue as `PauseMenuAction.QuickLoad`
  when `m_savesCount > 0`, New game `OnNewGame`, Load game `OnLoadGame`,
  Settings, Credits), `HandleMenuItemActivate` (`:386636`: QuickLoad →
  `LoadModdedSave(0)` or `LoadLastCheckpoint(false)`; other items spawn
  their `eventName` on `m_menuEventDispatcher` in the base, `:275520`),
  `OnGlobalRelease` (`:386533`: `back` → `OnBack`, the quit prompt),
  `ShowActionsList` (`:386483`: the save preview tooltip hangs on the
  list's index 0, `OnContinueButtonEnter`). NCO wrapped the same two
  methods with `ref<PauseMenuListItemData>` (facts only).
- Pause menu: `PauseMenuGameController` (`:340290`): `PopulateMenuItemList`
  (`:340378`), and its own `OnMenuItemActivated` (`:340451`, a switch on
  the action; it does not call `HandleMenuItemActivate`); Exit to main menu
  is `PauseMenuAction.ExitToMainMenu` → `GotoMainMenu`. Its back key is not
  handled in `OnGlobalRelease` (the menu scenario's `OnBack`): whether
  handling `back` there keeps the pause menu open is to see.
- Text input: the game has no script text field; Codeware's
  `GenericMessageNotification.ShowInput(controller, title, message, type)`
  is the game's message popup (`:273144`, `generic_fullscreen_message_notification`)
  with Codeware's `HubTextInput` in it, upper case; the token's
  `GenericMessageNotificationCloseData` carries `result` and Codeware's
  `input`. The vanilla benchmark results screen shows the same popup from a
  pre-game menu (`:328896`), so it should work in the main menu. To check:
  that typing a letter bound to `cancel` (C) does not close it.
- A menu's own clock: delayed callbacks stand still while the world is
  paused (§ Pause), and our system does not tick in the main menu; a
  looping `inkAnimProxy` (`loopInfinite`, `inkanimLoopType.Cycle`,
  `OnStartLoop` callback, the way Codeware's text input ticks) runs in
  menus whether the world is paused or not.
- The menu's ink space is 3840 × 2160 whatever the screen (NCO a90ef259):
  the status panel is placed top right with margins in that space; a
  full-screen interactive widget would take the menu's input (NCO d6240d9d),
  so the panel is never interactive.

### 13.30 Phantom Liberty: the game's own check (2026-11-19, read in final.reds, not run)

Line numbers are `~/.cache/tandem/ref/gamescripts/vanilla/final.reds` (2.50).
[2.5-untested]: used by `TandemCoop.ReportExpansion` (`scripts/tandem/Coop.reds`).

- `inkISystemRequestsHandler` (`:272564`) has `IsAdditionalContentOwned`,
  `IsAdditionalContentEnabled`, `IsAdditionalContentInstalled` and
  `IsAdditionalContentReleased` (`:272710`…), each taking the content's
  `CName`; the expansion is `n"EP1"`.
- The main menu's banner (`SingleplayerMenuGameController.UpdateExpansionBannerState`,
  `:386130`) reads Enabled → `ExpansionStatus.Downloaded`, else Owned →
  `Owned`, else `Available`; the logo switches on
  `IsAdditionalContentEnabled(n"EP1")` (`:340342`, `:386952`), as do the
  in-game checks (`:363555`, `:370086`, `:376636`). Enabled is the test
  the game itself uses for "this game has Phantom Liberty".
- The handler is reached from any `inkIGameController` or
  `inkMenuScenario` (`GetSystemRequestsHandler`, `:56686`, `:273464`);
  Codeware adds `GameInstance.GetSystemRequestsHandler()` (a new
  `inkMenuScenario`'s), usable from a scriptable system.
- RTTI 2.50 (`data/rtti-natives-2.50.tsv`): `IsAdditionalContentOwned`
  `0x142790df8`, `IsAdditionalContentEnabled` `0x140fa5744`.
- To see in game: that it answers true in the main menu already, and false
  with the DLC unticked in the store's client.
