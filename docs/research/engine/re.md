# Reverse engineering the running game: RTTI, slots, crashes, script calls

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md).

## 13. Reverse engineering of the running game (2026-11-17, game 2.50, read-only)

Measured with `tools/re/` (see `tools/re/README.md`): the live process read
through `/proc/PID/mem` on the game machine (nothing written, nothing
paused), the exe disassembled with capstone and decompiled with Ghidra.
Tags here: [2.5-verified] = read from the live process and consistent over
every object checked; [2.5-static] = read from the exe's code, not yet
exercised by a hook of ours: confirm in game before relying on it.

The image loads at its preferred base `0x140000000` (Proton, no relocation):
addresses below are the exe's own. Every function has an address-library
hash (`tools/re/addr.py`): use the hash, never the address, in the plugin.

### 13.1 RTTI, as the game holds it

- `CRTTISystem` singleton `0x144763320`; name pool `0x144334B80` (entry
  pointers at +0x08, count at +0x14; entry: hash, next, length byte, text at
  +0x14; 580 240 names); native handler table `0x143B7BD30`
  (`handlers[regIndex]`, `regIndex` at +0xF8 of a class function, +0xF0 of a
  global one). [2.5-verified]
- 16 329 classes, 1 606 enums, 4 898 global functions, 13 727 native class
  functions with a handler. Dump: `tools/re/rtti_dump.py` →
  `.cache/re/rtti.json`; query: `tools/re/rtti.py`. Kept in the repository:
  `data/rtti-vtables-2.50.tsv` (7 053 classes with their **instance
  vtable**) and `data/rtti-natives-2.50.tsv` (native function → handler
  address). [2.5-verified]
- A type's kind: `IType::GetType` (type vtable slot 0x20) is `mov al, N; ret`.
  An instance vtable is found from slot 0: `ISerializable::GetNativeType` is
  `mov rax, [rip+g]; ret` with `g` a global holding the class. [2.5-verified]
- A native handler `h(context, frame, out, outType)` reads its parameters
  from the script frame, then calls the real function: the real
  `entSlotComponent::GetSlotTransform` is `0x140CA22E4`, behind its handler
  `0x140CA2200`. Handlers are the way into any engine function a script can
  call. [2.5-static]
- An `IScriptable`'s class is the pointer at +0x30 (the script class for
  scripted types: `PlayerPuppet` on an object whose vtable is `gamePuppet`'s);
  +0x08 is its weak reference to itself (a cheap "is this a live object"
  test when scanning for a vtable). [2.5-verified]

### 13.5 NCO's hand-numbered slots, checked against 2.50

Sizes from the live RTTI match the 2.5 SDK: `gameuiCharacterCustomizationSystem`
0x3D8, `moveComponent` 0x2C0, `vehicleBaseObject` 0xBC0 (`vehicleWheeledBaseObject`
0xC10), `gamePuppetPS` 0x78, `entIComponent` 0x90, `gamePlayerManager` 0xE0,
`inkWidget` 0x210, `questQuestsSystem` 0x300. [2.5-verified]

Vtable slots (instance vtables from `data/rtti-vtables-2.50.tsv`, the slot's
function decompiled): [2.5-static]

| Slot                                          | Function on 2.50 (hash)    | What it does there                                                                                      | Verdict                                      |
| --------------------------------------------- | -------------------------- | ------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| `vehicleWheeledBaseObject` +0x328             | `0x1421BBE60` (3954780831) | `(this, out handle)`: resolves a weak handle through its own slot +0x318                                | **not** "engine on (bool)": do not call      |
| `vehicleBaseObject` +0x280                    | `0x1421BBAA0` (1485377386) | `*(this + 0x6B4) = 0`                                                                                   | **not** `ResetWithNewTransform`: do not call |
| `vehicleMoveSystem` +0x1F0 (vt `0x1432803D8`) | `0x142BA87C8` (2599754135) | `(this, EntityID*, bool)`: finds the vehicle's record and sets its field +0x9D4 to 2 (true) / 3 (false) | consistent with `SetSimpleMovement`          |
| `gamePlayerManager` +0x200 (vt `0x1430CF520`) | `0x1419543B0` (1062672294) | returns `[this + 0x58]`                                                                                 | consistent with "local player" getter        |

By name instead of the first slot: `vehicleBaseObject.TurnEngineOn(on: Bool)`
and `TurnVehicleOn(on: Bool)` are script natives (handlers `0x1414CE088`,
`0x14059A934`), with `IsEngineTurnedOn`, `GetLinearVelocity`,
`ForceBrakesFor`… on the same class (`tools/re/rtti.py class vehicleBaseObject`).

### 13.8 Crashes that look like freezes, and three ways to cause them (2026-11-18)

- **Under Proton a crash freezes the game instead of closing it.** The
  game's own exception handler (the frames at `+0x2f3a523`…`+0x2f3ae6b` on
  top of `ntdll`'s exception dispatch) waits forever: 0% CPU, every thread
  asleep, the screen as it was (or black with the loading spinner). Before
  calling it a deadlock, read the stacks: `tools/game.sh bt` runs the
  game's own Proton `winedbg` inside its pressure-vessel namespaces
  (`nsenter -U -m -t PID`, the game's `WINEPREFIX`, `LD_LIBRARY_PATH`,
  `WINEDLLPATH`), attaches, dumps `bt all`, detaches (the game keeps
  running), and names tandem's frames from `tandem.pdb`. [2.5-verified]
- **Native structs must be at most 8-byte aligned.** The script VM places
  a function's struct locals (the `event` of `for event in
TakeNpcEvents()`) 8-byte aligned and runs the type's constructor there.
  A RedLib struct with a `Red::Vector4` or `Red::Quaternion` field is
  16-byte aligned, clang zeroes it with `movaps`, and that faults. The
  constructor is `ClassDescriptorDefaultImpl<T>::ConstructCls`, called from
  the VM's frame set-up `sub_14027887c`. `TandemNpcEvent` now carries plain
  floats; `tools/check-natives.py` (`task natives:check`, run before every
  deploy) rejects such fields. Vector4 and Quaternion **parameters and
  return values** of native functions are fine. [2.5-verified]
- **A `TweakDBID` local left unset is not None.** `let record:
TweakDBID;` then `SendSeat(record, …)` on foot sent
  `NewPerks.Intelligence_Left_Milestone_2.preventInQueueAgain` (whatever
  the VM's slot held) in every session of 2026-11-18: guests and the mock
  player saw the host "driving" that record, and a puppet whose seat was
  echoed back sat in a phantom car, its controller holding still (the
  `takedowns-host` grabs were all refused as too far). Write
  `let record = TDBID.None();`. [2.5-verified] Which other types the VM
  leaves as they were is not measured: an unset `CName` local read back
  as `None` in the same function (one sample, so possibly luck), and
  struct and class locals are run through their type's constructor at
  frame set-up (above). Whether `Bool`, `Int32`, `Float` and enums are
  cleared is unknown, so `tools/scripts-uninitialized.py` (run by
  `task scripts:check`) treats every value type alike: fundamental
  numbers, `Bool`, `CName`, `TweakDBID`, `EntityID`, `ItemID`, vectors,
  `Quaternion`, `EulerAngles`, `WorldPosition`/`WorldTransform` and enums
  need an initializer or the comment `// assigned on every path`. To
  measure later: a script function reading a bare `Int32` and `Bool`
  right after another left non-zero values in the same frame slots.
  [2.5-untested]
- **A native declared with other types than the DLL's breaks the VM.**
  redscript never sees the DLL: `SendNpcSpawn(…, orientation: Quaternion)`
  against a C++ `float yaw` compiled with 0 errors and stopped the game on
  the first call. `tools/check-natives.py` compares every function and
  struct. [2.5-verified]
- **The clip-length query blocks while the rig's animations stream.**
  `GetAnimLength` (hash 3750304414, `0x1402d4a18`, (animation controller
  component, CName) → seconds) on an NPC spawned a frame earlier never
  returned: it waits on the game thread for the animations to finish
  loading, and they load on that thread. V's rig (our puppets) is loaded
  with V, so asking at once is fine there. For NPC proxies the controller
  asks after 60 animation updates, and the script attaches it 2 s after
  the entity first exists. [2.5-verified]
- The first freeze of the day came behind the loading screen, right after
  the NPC sharing's first tick. That was most likely the native type
  mismatch above, not the loading itself. Sharing is now also held until
  the world plays (`m_playing`). [2.5-tested]

### 13.13 Calling a script static function from the plugin (2026-11-18)

A redscript class's **static** functions are neither in the class's
member list (`CClass::funcs`) nor its static list (`staticFuncs`): the
compiler registers them as **global** functions with a qualified, typed
name, e.g. `TandemPause::Frame;GameInstance`. RedLib's `CallStatic` and
`GetFunction(class, name)` therefore never find them; scan
`CRTTISystem::GetGlobalFunctions` for the qualified name (as
`plugin/src/main.cpp` does) and `Red::CallFunction` it. Its game-instance
parameter arrives empty from native code: the script takes the game from
the global `GetGameInstance()` instead. A RED4ext game-state update
(`RED4ext::v1::GameState`, running state) calls it every frame, also
while the world is paused (scripts' delayed callbacks stand still then).
[2.5-verified]
