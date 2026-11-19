# Other worlds

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

### 13.26 Other worlds: cyberspace, braindances, replacers (2026-11-18, scripts and RTTI only)

How a V leaves the city and comes back, read from the 2.50 scripts
(`final.reds` lines), Codeware's generated imports (RTTI of 2.50) and the
RTTI natives dump; used by `scripts/tandem/Elsewhere.reds` and
`core/src/elsewhere.cpp`. Nothing here has run in a session yet: the
scenarios `story-worlds` and `elsewhere-dive` check the plumbing, not the
real quests.

- **Braindance.** `PlayerPuppet.OnRewindableSectionEvent` (`:45353`) on
  `scnRewindableSectionEvent.active` adds the `BraindanceControls` state
  machine, turns camera bobbing off, queues `SetIsInBraindance` to
  `BraindanceSystem` (`:247774`, `:247950`) and moves the audio listener
  to the braindance camera (`AudioSystem.SetBDCameraListenerOverride`,
  `:45364`); the `Braindance.IsActive` blackboard (`:48378`) follows. The
  player's own entity is the braindance camera: `LocomotionBraindance`
  (`:515134`) flies it (`BraindanceFlyDecisions`, `:515724`) within ±2 m
  of the height it entered at (`LocomotionBraindanceEvents`, `:515393`,
  NPC collision filter). So the streamed position is the camera's, not the
  body's, whether or not the braindance teleports V elsewhere: tandem keeps
  the body where the braindance began. Section control from scripts:
  `scnScriptInterface.IsRewindableSectionActive`, `JumpRewindableSection`,
  `SetRewindableSectionPlaySpeed` (RTTI natives). [2.5-untested]
- **Cyberspace.** No single flag. What the scripts react to:
  - status effects tagged `Cyberspace` (the record
    `GameplayRestriction.Cyberspace`, listed from REDmod's tweak sources on
    2.31: `moddocs` reference-gameplayrestriction) block the radial menu and
    quick slots (`:528002`, `:528016`); [2.31-only] for the record name,
    [2.5-untested] for the tag check;
  - status effects tagged `CyberspacePresence` turn footstep audio and
    camera bobbing off on apply and back on on removal (`:43947`,
    `:44502`); [2.5-untested]
  - districts: `gamedataDistrict` has `LittleChina_Q101Cyberspace` (91),
    `Coastview_q110Cyberspace` (41), `CorpoPlaza_q201Cyberspace` (53)
    (`:4716`, enum). `DistrictManager` (`:194021`) keeps a stack of
    district records from `DistrictEnteredEvent`s and writes the top one's
    `EnumName()` to `UI_Map.currentLocationEnumName` (`:194095`): a script
    sees "…Cyberspace" there. Which scenes use them (q110 is likely
    Transmission at the Voodoo Boys' church in Coastview, q201 Mikoshi
    under Arasaka Tower): not checked. Phantom Liberty's cyberspace and
    Blackwall sequences have no district of that name: their signal is
    unknown (status effects above, or none). [2.5-untested]
  - rendering and HUD: `RenderSceneLayer { Default, Cyberspace, WorldMap }`
    and `IVisualComponent.renderSceneLayerMask` (Codeware imports): objects
    can be drawn in a cyberspace layer of their own; the quest node
    `questSetCyberspacePostFX_NodeType` (datamosh, threshold, blend) sets
    the look; `gameCyberspaceBoundaryNode` is a trigger area with two
    markers (the edge of a cyberspace level); the HUD has
    `CyberspaceMappinsContainerController` (`:398358`) and mappin variants
    `CyberspaceObject` / `CyberspaceNPC` (`:1198`). Whether cyberspace
    levels sit at their own world coordinates or overlap the city in that
    layer: not checked (tandem holds the body from the start either way).
    [2.5-untested]
- **Replacers (playing as Johnny or someone else).** The local player
  changes entity: `PlayerSystem.OnLocalPlayerChanged` (`:50036`) sets
  `UI_PlayerStats.isReplacer` (`:83411`) false for
  `Character.Player_Puppet_Base` and true for anything else
  (`Character.johnny_replacer`, `Character.kurt_replacer`, others).
  `PlayerPuppet.IsReplacer` (record is not `Player_Puppet_Base`, `:39344`),
  `IsVRReplacer` (`Character.q000_vr_replacer`, `:39348`),
  `IsJohnnyReplacer` (`:39362`). `GetPlayer` is the controlled object
  (`:39193`), so position and pose are the replacer's: in a takeover in
  the city that is where V's body is; in a flashback (2023) it is another
  place, reached by a jump. The quest node is `questReplacer_NodeType
{enable, objectRef, audioOverrideAppearanceName}` (Codeware import).
  [2.5-untested]
- **Quest teleports.** `questTeleportPuppetNodeDefinition` with
  `questTeleportPuppetParamsV1 {destinationRef, destinationOffset,
doNavTest, useFastTravelMechanism, healAtTeleport}` and the vehicle
  manager's `questTeleport_NodeType`; the cut multiplayer left
  `questMultiplayerTeleportPuppetParams {teleportAllPlayers, spawnPointTag,
destinationRef, destinationOffset, areaNodeTriggerRef}` (Codeware
  imports). No script sees these: tandem infers a teleport from a jump of
  more than 50 m between two 30 Hz samples outside a vehicle. A later
  refinement: the quest hook (§13.15) could name the node. [2.5-untested]
- **Sitting or lying**: `WorkspotGameSystem.IsActorInWorkspot(actor)`
  (`:53275`); a netrunner chair, a braindance chair or a bed are
  workspots (assumed, not checked per scene). [2.5-untested]
- **Hiding an entity's look without removing it**:
  `IVisualComponent.TemporaryHide(on)` (`:106206`; devices swap meshes
  with it, `:511696`). Every mesh component (`MeshComponent`,
  `entSkinnedMeshComponent`, `entMorphTargetSkinnedMeshComponent`,
  `entGarmentSkinnedMeshComponent`) is an `IVisualComponent`; what a
  puppet wears and holds are item entities in its attachment slots
  (`TransactionSystem.GetItemInSlot`, `:52211`), hidden one by one. That
  it hides skinned and garment meshes on a puppet (and their shadows):
  to see. [2.5-untested]
