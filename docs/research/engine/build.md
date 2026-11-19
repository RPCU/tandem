# Build gotchas and the address-hash table

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

## 10. Build gotchas (NCO, Windows/MSVC)

- Prerequisite set for patch 2.5 (`MAP:2537-2540`, `5e749fa6`): **RED4ext 1.31.0, Codeware 1.21.2,
  ArchiveXL 1.28.6, TweakXL 1.12.0, redscript 0.5.32, input_loader 0.2.6** (needs RED4ext ≥1.31.0) —
  "the whole set moved together for game patch 2.5". 2.31-era set was RED4ext 1.29.1, Codeware 1.18.0,
  ArchiveXL 1.26.0, TweakXL 1.11.1. A 2.5 game with RED4ext 1.29.1 + ArchiveXL 1.26.0 died before any
  plugin loaded (`d06f33ad`). The 2.5 patch wiped winmm.dll, RED4ext.dll, the plugin folder and scc.exe.
  Codeware submodule at `a94441072`.
- RED4ext 1.30.0 removed the max-SDK-version check and added plugin API v1 (`RED4ext::v1`: PluginHandle,
  Sdk, EMainReason, PluginInfo, GameState); addresses come from `cyberpunk2077_addresses.json`
  (`8eca2871`). SDK 2.5 changes: callback inline buffer 32→64, some hashes changed, game-system virtual
  ABI changed; API renames `DynArray.size`→`Size()`, `->entries`→`Data()`,
  `Red::Detail::GetFunction`→`Red::GetFunction`, `CallFunctionWithArgs(func,ctx,args)`→
  `Red::CallFunction(ctx,func,args)` (`e797867b`).
- Install the crash reporter first thing in plugin Main (a startup AV ~300 ms in was otherwise
  unrecorded) (`b3580d49`). Crash dumps land in `REDEngine\ReportQueue`; `MINIDUMP_THREAD` stack
  descriptor at +24; `/OPT:ICF` folds helpers (`08ac5b15`). AMD TDRs masqueraded as mod crashes
  (`98ae4734`). In a vectored handler, a C++ throw's ExceptionAddress is the runtime throw site; ~20
  benign engine C++ exceptions happen in the first 250 ms (`0be76dc0`).
- RedLib type-name bug (2.31): a failing concept made the system register as base `gameIGameSystem` →
  script validation failed, RED4ext refused to start (`9732c461`). RED4ext loads every plugin
  subdirectory DLL, renamed folders included (`3c7904f8`).
- Toolchain: MSVC 14.44, Windows SDK 10.0.22621, xmake 3.1.0 (`02ab05a2`); xmake under Git Bash detects
  mingw → pass `--plat=windows` (`f12def17`); spdlog's fmt needs `/utf-8` (`fc8a950c`).
- RED4ext.SDK vendored: WopsS `5246e659` "Update types for patch 2.5 (#232)" (2026-10-24) + the
  CyberpunkMP team's decodes (tiltedphoques fork `f52af6a3`); RedLib `a88797f`; jackhumbert SDK
  `155d285a` for 2.5 vehicle offsets (`vendor/RED4ext.SDK/NCO-PROVENANCE.md`). Upstream WopsS alone lacks
  the MP decodes; tiltedphoques fork alone lacks 2.5.
- C++20 (`xmake.lua:8`); xmake with `--vs_sdkver=10.0.22621.0` (**10.0.26100.0 breaks**; switching SDKs
  needs `xmake global --clean` + delete `.xmake/` and `build/`); `-j 4` or lower (`C3859` PCH OOM);
  protobuf-cpp **29.3** forced on all transitive deps (3.19.4 too old, 35.x removed
  `FieldDescriptor::is_optional`); abseil **20250127.0**; flecs **v4.0.3**; mimalloc 2.1.7; libopus 1.5.2
  (`xmake.lua:22-73`, `CONTRIBUTING.md:36-50`).
- CppSharp 1.1.84.17100 on net9.0 for the C# SDK generator; the generator swallows exceptions and exits
  0 (`CONTRIBUTING.md:45,52-57`).
- `xmake install` without `-o distrib` writes elsewhere and prints ok (`CONTRIBUTING.md:61-76`).
- GCC/libstdc++ (Linux server) catches missing `std::` includes MSVC forgives — 34 found at once
  (`MAP:429-444`).
- flecs: `FLECS_DEBUG` changes `ecs_stack_cursor_t` layout; don't enable to debug the cursor
  (`InterpolationSystem.cpp:1017-1019`).
- No Proton/Wine-specific notes exist in either repo.

## 11. Address-hash table

All hashes found anywhere in either repo (source grep of every 7–10 digit decimal, filtered against
`addr.tsv`; commit bodies add one; two come from the vendored SDK). **Every one is present in the 2.50
list** — none are missing. Only four have symbol names in `addr.tsv`. Hash presence proves the symbol
still exists in 2.50, not that its signature or the struct layouts around it are unchanged. "CW name" = the constant in `codeware/src/Red/Shared/AddressLib.hpp`
(proof the hash means the same function on 2.5). "Used" = actually called/hooked in NCO HEAD (not just
declared). Repos: N = NCO, C = CAMP.

<!-- prettier-ignore -->
| Hash | addr.tsv symbol | CW name (2.5) | What the repos use it for (signature as used) | Repo / file:line | Used | Status |
| --- | --- | --- | --- | --- | --- | --- |
| 1828854026 | — | `IPlacedComponent_SetTransform` | write placed transform `(IPlacedComponent*, WorldTransform const&)` | N `InterpolationSystem.cpp:103` | yes | [2.5-verified] call; doesn't move NPC-record bodies |
| 3227858325 | — | `QuestPhaseInstance_ExecuteNode` | hook + call quest node `u8(PhaseInstance*, questNodeDefinition*, QuestContext&, Socket const&, DynArray<Socket>&)` | C `QuestMirror.cpp:71` | yes | [2.5-verified] |
| 3144298192 | — | `QuestsSystem_CreateContext` | `(QuestsSystem*, QuestContext*, 1, 0, -1, -1)` | C `QuestMirror.cpp:64` | yes | [2.5-verified] |
| 386815609 | — | `AppearanceChangeSystem_ChangeAppearance2` | "ScheduleSynchronizedAppearanceChanges" `(changer, WeakHandle<Object>&, Span<Key>* old, Span<Key>* new, std::function const&, u32 prio)` — remote face/body | N/C `CharacterCustomizationSystem.h:63` | yes | [2.5-verified] (remotes dress correctly on 2.5) |
| 633871992 | — | `MorphTargetManager_ApplyMorphTarget` | SetTargetWeightWithNormalize `(comp, CName target, CName region, float, bool)` | N/C `MorphTargetManagerComponent.h:11` | no | [unverified] |
| 898639042 | — | `ScriptValidator_Validate` | hook to register RPC classes during script validation | N `RpcValidator.cpp:245`, C `:185` | yes | [2.5-verified] (needs 2.5 ScriptBundle layout) |
| 2468877568 | `GpuApi::Present` | — | `CRenderNode_Present_DoInternal` hook `void*(i32*, u8, UINT)` (ImGui overlay) | N/C `Game/Rendering.h` | yes | [2.5-verified] |
| 239671909 | `GpuApi::ResizeBackbuffer` | — | `CRenderGlobal_Resize` hook | N/C `Game/Rendering.h` | yes | [2.5-verified] |
| 3192982283 | — | — | `CRenderGlobal_Shutdown` hook | N/C `Game/Rendering.h` | yes | [2.5-verified] (part of the 2.5 overlay fix `60a8ba47`) |
| 2268141838 | — | — | `IdleController::SetAnimation(Controller*, AnimationData&)` — hooked; attaches MMC to registered puppets | N `Game/Entity.h:25` | yes | [2.5-verified] (default path since test.87) |
| 4018412273 | — | — | `move::Component AttachController(Component*, Controller*) → float`; reads ctrl type @+0x8, pushes on component+0xA8 | N `InterpolationSystem.cpp:1296`, C `VehicleSystem.cpp:268` | yes | [2.5-verified] (static check `e3c5e455` + test.85/86) |
| 2624656810 | — | — | `AnimationDriver::Attach(Entity*)` (driver struct 0x30) | N/C `AnimationDriver.cpp:11` | yes | [2.5-verified] via MMC |
| 2385973466 | — | — | `AnimationDriver::Detach()` | N/C `AnimationDriver.cpp:17` | yes | [2.5-verified] via MMC |
| 2132808949 | — | — | `AnimationDriver::SendParameters(AnimationData&)` (0x3C) | N/C `AnimationDriver.cpp:23` | yes | [2.5-verified] via MMC |
| 3750304414 | — | — | `AnimationControllerComponent` get anim length `float(comp*, CName)` | N/C `AnimationDriver.cpp:29` | yes | [2.5-verified] via MMC |
| 316482401 | — | — | `ApplyFeature(AnimationControllerComponent*, CName, Handle<AnimFeature>)` | N/C `MultiMovementController.cpp:11` | no | [unverified] |
| 2710710832 | — | — | `CreateHandle_CharacterCustomizationState(Handle*)` | N/C `CharacterCustomizationSystem.h:15` | yes | [2.5-verified] (remote apply path) |
| 108403442 | — | — | `CharacterCustomizationState::Serialize(state*, CMPStream*)` (read & write) | N/C `CharacterCustomizationSystem.h:22` | yes | [2.5-verified] for deserialize into a fresh state |
| 1993027039 | — | — | `CharacterCustomizationState_InitFromPreset(state*, Handle<Preset>*)` | N/C `:19` | no | [unverified] |
| 56564470 | — | — | `gameTransactionSystem::AddItemToSlot(sys*, AddItemToSlotContext const*)` (commented out) | N/C `:25` | no | [unverified] |
| 394336209 | — | — | `GetGameSettings() → ink::GameSettingsResource const*` | N/C `:28` | no | [unverified] |
| 1713639304 | — | — | `ItemID::SetUniqueCounterValue` (commented out) | N/C `:31` | no | [unverified] |
| 3020231866 | — | — | `ItemID_Create(ItemID*, TweakDBID, i64 seed)` | N/C `:33` | 2.31 path only | [2.31-only] (native dressing gated off on 2.5) |
| 3236045910 | — | — | `GetItemRecord` → `Handle<Item_Record>*` (member fn) | N/C `:35` | 2.31 path only | [2.31-only] |
| 1934369954 | — | — | `Item_Record::GetPlacementSlot(Handle<AttachmentSlot_Record>*, i32)` | N/C `:38` | 2.31 path only | [2.31-only] |
| 523315084 | — | — | TweakDB `Val::EvalArrayTweakDBID` | N/C `:41` | no | [unverified] |
| 3648983380 | — | — | TweakDB `Val::EvalCName` | N/C `:44` | 2.31 path only | [2.31-only] |
| 4124714348 | — | — | `ResolveSuffix(void*, CString*, Handle<Item_Record>&, bool isMale)` | N/C `:47` | no | [unverified] |
| 1337337421 | — | — | `ResolveVisualTags(CName, CString*, Handle<Item_Record>&)` | N/C `:50` | no | [unverified] |
| 3029088864 | — | — | `GetItemAppearanceName(CString*, Handle<Object>, Handle<Object>, Handle<Item_Record>, ItemID)` | N/C `:53` | 2.31 path only | [2.31-only] |
| 1054086242 | — | — | MorphTargetManager `ResetWeights(comp*)` | N/C `MorphTargetManagerComponent.h:14` | no | [unverified] |
| 2624990479 | — | — | `RegisterRemotePlayerMappin(IMappinSystem*, NewMappinID*, MappinData const&, WeakHandle<Object>*)` | N/C `Game/MappinSystem.h:8` | no | [unverified] |
| 3577160229 | — | — | `RegisterMappin(IMappinSystem*, NewMappinID*, MappinData const&, Vector3*)` | N/C `Game/MappinSystem.h:12` | no | [unverified] |
| 4039776020 | — | — | vehicle `SetIsPlayerControlled(BaseObject*, bool)` (from DriveAction::OnStart) | N `VehicleSystem.cpp:36,68`, C `:269` | yes | [unverified on 2.5] |
| 1620777158 | — | — | vehicle `SetFlags(BaseObject*, u32)` (0x10, 0x80) | N `VehicleSystem.cpp:37`, C `:270` | yes | [unverified on 2.5] |
| 1585713002 | — | — | vehicle `SetKinematic(BaseObject*, bool)` | N `VehicleSystem.cpp:38,69`, C `:271` | yes | [unverified on 2.5] |
| 2933986803 | — | — | vehicle `ForceMoveTo(BaseObject*, WorldTransform const&, float dtMs)` | N `InterpolationSystem.cpp:800,912`, C `:131` | yes | [unverified on 2.5] |
| 457775029 | — | — | vehicle `ForceTransform(BaseObject*, WorldTransform const&, bool)` | N `:913`, C `:132` | no (commented) | [unverified] |
| 2592348558 | — | — | vehicle `EnablePhysics(BaseObject*, u32 flag=0x100, bool)` | N `:914`, C `:133` | yes | [unverified on 2.5] |
| 3713079867 | — | — | `DriveToPoint::OnUpdate_Internal` — comment only (model for the vehicle drive sequence) | N `:920`, C `:137` | no | reference |
| 1160782872 | — | — | `MountingFacility::IsMountedToObject(fac*, Entity const&, MountingSlotId const&, bool)` | N `NetworkWorldSystem.cpp:3484`, C `:294` | no | [unverified] |
| 3120376212 | — | — | `MountingFacility::GetMountingInfo(fac*, Entity const&, MountingSlotId const&, MountingInfo&)` | N `:3488`, C `:298` | no | [unverified] |
| 3535541945 | — | — | `Player::ProcessInput(cpPlayer*, RawInputBuffer&)` — hook + call | C `ChoiceInput.cpp:69` | yes (CAMP) | [2.5-present]; CAMP's 0x40 RawInputData is [broken-on-2.5] (0x48) |
| 2582323196 | — | — | function containing `exe+0x3E7863` (range 0x3E7848–0x3E789B), crash site of test.74 (MMC attach) and test.83 — named in commit `b88e1040` only | commit | n/a | crash locator |
| 404169501 (0x1817231D) | — (present, unnamed) | — | SDK `Detail::AddressHashes::CBaseFunction_InternalExecute`, used by RedLib call-with-frame `Red/TypeInfo/Invocation.hpp:18`. **Note**: CW's constant of the same name is **2744524784** (addr.tsv: `rtti::Function::InternalCallNative`) — the two disagree; check which RedLib path you use | N/C via vendored SDK `Detail/AddressHashes.hpp:14` | yes | used on 2.5 by NCO (mod runs), [check] |
| 1482886888 (0x58630EE8) | `rtti::ClassType::DeepCompare` | — | SDK `TTypedClass_IsEqual`, used in `Red/TypeInfo/Definition.hpp:730` | N/C via SDK `:240` | yes | [2.5-verified] (named in the 2.50 list) |
| 2744524784 | `rtti::Function::InternalCallNative` | `CBaseFunction_InternalExecute` | not used by NCO/CAMP; Codeware's call-native entry | CW `AddressLib.hpp` | — | [2.5-verified] |
| 673454961 | — | `InkSystem_ProcessInputEvents` | not used by either repo; suggested 2.5 alternative to ProcessInput for input injection | CW `Red/InkSystem.hpp:42-44` | — | [2.5-verified] |

**Non-hash offsets / vtable slots** (summary; details in the sections above)

<!-- prettier-ignore -->
| What | Value | Version / status | Source |
| --- | --- | --- | --- |
| `gameuiCharacterCustomizationSystem` size | 0x388 (2.31) / **0x3D8** (2.5) | fingerprint for `IsGame231Layout` | `CharacterCustomizationSystem.h:117-126` |
| creator state handle | ccSystem **+0x78** | [2.31-only]; holds template body on 2.5 | `:131-160`, `463bc4d1` |
| `IComponent::owner` | **0x50** (2.5 SDK assert); IComponent size 0x90; old hand offset +0x90 | [2.5-verified] | `InterpolationSystem.cpp:1310`, `7bef6e17` |
| `move::Component` | size 0x2C0; representation @0x138; worldTransform 0x1C0; position 0x1E8; speed 0x220; controller list **+0xA8** | 2.5 SDK + `e3c5e455` | `moveComponent.hpp:29-56` |
| MMC controller layout | vptr, `u32 type` @+0x8 (kMulti = 1<<10), bools, `move::Component*` @+0x10; 19 vfuncs | [2.5-verified] | `MultiMovementController.h`, `e3c5e455` |
| engine `AnimationDriver` / `AnimationData` | 0x30 / 0x3C | [2.5-verified] | `e3c5e455` |
| `ITransactionSystem` vtable (GiveItem/AddItemToSlot) | 0x250 and on (SDK virtuals) | [broken-on-2.5] crash `exe+0x887DC4` | `AppearanceSystem.cpp:395-412`, `929d924f` |
| `WheeledBaseObject` vtable "engine on" `(bool)` | **+0x328** | **[broken-on-2.5]** (§13.5): the slot is another function; use `TurnEngineOn` by name | `VehicleSystem.cpp:46` |
| `vehicle::IMoveSystem` vtable SetSimpleMovement `(EntityID const&, bool)` | **+0x1F0** | [2.5-static] plausible (§13.5), still to run | `InterpolationSystem.cpp:95-98` |
| `vehicle::BaseObject` vtable ResetWithNewTransform `(WorldTransform const&)` | **+0x280** | **[broken-on-2.5]** (§13.5): the slot is `*(this+0x6B4) = 0` | `campaign/.../InterpolationSystem.cpp:60-63` |
| `vehicle::BaseObject` fields (2.5 SDK) | physicsState 0x260 (flag 0x100), rigidBody 0x2D8, worldTransform 0x350, runtimeData 0x5F0, engineData 0xBD0 (`unk61`), size 0xBC0; CW RecordID 0x658 | SDK asserts / CW | `NCO-PROVENANCE.md:71` |
| `PuppetPS.unk72[0] = 1` | "checked during item add for &TPP" | used on 2.5, unexplained | `AppearanceSystem.cpp:373-380` |
| `MorphTargetManagerComponent` weights | +0x98 | unused [unverified] | `MorphTargetManagerComponent.h:8` |
| `game::Puppet` (old ExPuppet) Teleport vfunc after 34 slots from 0x108; moveComponent @0x420 | — | [broken-on-2.5] retired | `Game/Puppet.h` |
| old move-component stub: controller* @0xA8, SpinLock @0x128 | — | 2.31 stub; 0xA8 re-confirmed on 2.5 | `Game/Movement.h`, `e3c5e455` |
| `inkWidget` parentWidget / layerProxy | 0x168 / 0x148 | [2.31-only] | `NetworkWorldSystem.cpp:769` |
| entity status byte | +0x156 | CW uses on 2.5 | `CW Red/Entity.hpp:57` |
| Script bundle (2.5) | ScriptDefinition 0x40; ScriptClass parent 0x48 / properties 0x50 / functions 0x90 / flags 0xD1; ScriptFunction 0xF8 (params 0x58, flags 0xC0); ScriptBundle classes 0xD8 / types 0xE8; ScriptProperty type 0xB8 | [2.5-verified] | `Game/ScriptValidation.h` |
| Render device (2.31 → 2.5) | devices 0xC97F38 → swapchains 0xCEBF30; queue 0x13BC4D0 → 0x14104D0 | use `GpuApi::GetDeviceData()` | `60a8ba47` |
| `QuestsSystem` | +0x60 lock, +0x68 root phase, +0xF8 FactManager* | [2.5-verified] CW | `QuestMirror.cpp:57-58` |
| `QuestContext` | size 0x340, phaseStack 0xF8, nodeID 0x108, PhaseContext 0x110 | [2.5-verified] CW | `QuestMirror.cpp:35-43` |
| `RawInputData` | 0x40 (CAMP) vs **0x48** (CW 2.5) | [broken-on-2.5] in CAMP | `ChoiceInput.cpp:12-26`, `CW Red/Input.hpp:32` |
| `gamePlayerManager` vtable → local cpPlayer | +0x200 | [2.5-static] a getter of `this+0x58` (§13.5), still to run | `ChoiceInput.cpp:72-82` |
| `worldStreamingSector` node setups | +0x40, stride 0x90 | [unverified/risky on 2.5] | `SceneSpots.cpp:67-111` |
| Seat CName | FNV1a64("seat_front_left") = 0xb000b1d029d0cea0 | engine CName hash | `MAP:913-915` |
| TweakDBID | CRC32(name) \| (len << 32) | engine | `MAP:2403`, `31590288` |
