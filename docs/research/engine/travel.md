# Fast travel and regroup

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

### 13.18 Fast travel and regroup (2026-11-18, 2.50 scripts read)

Read from the 2.50 scripts (`final.reds` line numbers); none of it run in
game yet (scenarios `travel`, `regroup`).

- **`FastTravelSystem`** (scriptable system, 50235–50901): persistent
  `m_fastTravelNodes: [FastTravelPointData]` (the points this save has
  discovered) and `m_fastTravelLocks`. `PerformFastTravel(player,
nodeData)` (50415, private, reached from `PerformFastTravelRequest`,
  237972, `OnPerformFastTravelRequest` 50687) acts only for a point in
  that list (`HasFastTravelPoint`), and only when it is not the starting
  point: `RemoveAllGameplayRestrictions`, blackboard `FastTRavelSystem`
  `DestinationPoint`, `ResetPitch`, `TeleportationFacility.TeleportToNode(
player, markerRef)` (192804), `UISystem.NotifyFastTravelStart()`
  (50161), and an autosave after the loading screen
  (`m_requestAutoSafeAfterLoadingScreen`). Tandem wraps it to see our own
  fast travel, and repeats its steps for a point not on our map (no
  registration: the point stays undiscovered). Whether `TeleportToNode`
  resolves a marker that is not streamed in is unverified. [2.5-untested]
- **A fast travel needs its loading screen type.** Every vanilla caller
  queues `PerformFastTravelRequest` from a menu: the world map
  (`WorldMapMenuGameController.FastTravel`, 358971) then broadcasts
  `inkSetNextLoadingScreenEvent` with `inkLoadingScreenType.FastTravel`
  (337318, enum 7866) and closes itself; the hub's fast travel list
  (331840) closes the hub. Seen in game (2026-11-19, `regroup` and
  `travel`): a request queued from gameplay with neither (tandem's
  `travel` command) ran the loading screen (the game thread silent 16 s),
  then the screen stayed black with no HUD while the game ran on; a
  second one 70 s later, to a point near a place still streamed, showed
  no loading screen at all, and the 80 m hop tandem made 1.5 s after it
  kept it black. Tandem now sends that event (through
  `UISystem.QueueEvent`) with every travel it starts and waits for
  `FastTravelLoadingScreenFinished` before any further teleport. Whether
  the event reaches the loading screen that way (the map broadcasts it
  from a controller) is the next run's check. [2.5-untested]
- **`FastTravelPointData`** (50954): `pointRecord` (TweakDB id, the same in
  every game), `markerRef` (`NodeRef`, a 64-bit hash: the same in every
  game; read and rebuilt by the natives `NodeRefHash` / `NodeRefOf`),
  `mappinID` (the world map pin; `MappinSystem.GetMappin(id)
.GetWorldPosition()` places the point). `DisplayName()` of its
  `FastTravelPoint_Record` is a localization key. [2.5-untested]
- **Locks**: `IsFastTravelEnabled()` is false while any lock is held
  (combat, vehicle, quests, `BlockFastTravel`); `IsFastTravelEnabledOnMap()`
  also needs a data terminal's map. Regroup uses the first. [2.5-untested]
- **The world map** (`WorldMapMenuGameController`, 358072): holding
  `world_map_menu_fast_travel` calls `TryFastTravel` (358864) for the
  selected pin whatever opened the map (only the press checks the
  terminal); its hints go to container `WorldMapInputHints`
  (`RefreshInputHints`, 359339). Tandem's wrap of `TryFastTravel` regroups
  with a player whose marker (`CPO_RemotePlayerVariant`) is selected.
  [2.5-untested]
- **The phone key**: `PhoneInteract` released answers a ringing holocall,
  held (`BUTTON_HOLD_COMPLETE`) opens the phone; `PhoneReject` held
  rejects (389308–389366). A regroup offer takes a release that was not a
  hold and no call is ringing (`UI_ComDevice.PhoneCallInformation`
  `callPhase` `IncomingCall`). [2.5-untested]
- **Puppets after a jump**: `TeleportationFacility.Teleport` does not move
  a puppet (§3, broken on 2.5), so a remote player's fast travel reaches
  the puppet as one large step of the movement controller (core snaps the
  track instead of flying it). Whether the controller applies a
  kilometre-long delta in one frame is unverified. [2.5-untested]
