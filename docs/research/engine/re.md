# Reverse engineering the running game: RTTI, slots, crashes, script calls

Index, sources and tags: [`../engine-knowledge.md`](../engine-knowledge.md). Paths (`data/…`, `nco-commits.md`) are relative to `docs/research/`.

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

<!-- prettier-ignore -->
| Slot | Function on 2.50 (hash) | What it does there | Verdict |
| --- | --- | --- | --- |
| `vehicleWheeledBaseObject` +0x328 | `0x1421BBE60` (3954780831) | `(this, out handle)`: resolves a weak handle through its own slot +0x318 | **not** "engine on (bool)": do not call |
| `vehicleBaseObject` +0x280 | `0x1421BBAA0` (1485377386) | `*(this + 0x6B4) = 0` | **not** `ResetWithNewTransform`: do not call |
| `vehicleMoveSystem` +0x1F0 (vt `0x1432803D8`) | `0x142BA87C8` (2599754135) | `(this, EntityID*, bool)`: finds the vehicle's record and sets its field +0x9D4 to 2 (true) / 3 (false) | consistent with `SetSimpleMovement` |
| `gamePlayerManager` +0x200 (vt `0x1430CF520`) | `0x1419543B0` (1062672294) | returns `[this + 0x58]` | consistent with "local player" getter |

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
- **An array intrinsic or a struct field on a call's result reads
  something else.** `ArrayContains(system.PuppetPlayers(), p)` was false
  for a player in the list on every police tick (2026-11-21: each remote
  V's level was forgotten on the tick it was learnt), and
  `Labels()[i]` read an empty String (2026-11-20). The VM takes these
  operands by address and a returned value has none. redscript knows it:
  its `INVALID_TEMP_USE` diagnostic
  (`compiler/src/diagnostics/invalid_temp_use.rs`, 0.5.32) refuses
  `ArraySize`, `ArrayContains`, `ArrayCount`, `ArrayFindFirst`,
  `ArrayFindLast`, `ArrayLast`, `ArrayPop` on a temporary, and a struct
  field or an element of one; but the game's scc runs no diagnostic pass
  (`scc/lib/src/lib.rs`: `CompilationUnit::new(…, vec![])`), and the
  vanilla scripts never write either. `tools/scripts-temporaries.py`
  (run by `task scripts:check` and CI) refuses both. The array cases are
  [2.5-verified]; a struct field of a call's result (`GetWorldPosition().X`)
  is refused on redscript's word alone [2.5-untested].
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
  the entity first exists. [2.5-verified] The script native
  `entAnimationControllerComponent.GetAnimationDuration` (`0x1429b6ca8`)
  only reads its CName and calls the same function: no way around the wait
  (read in the 2.50 exe, 2026-11-22). [2.5-verified]
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

### 13.98 Two engine crashes on an entity gone: the transform update, the AI workspot's end (2026-11-21)

Source: a study of another co-op mod (NightCoop), facts only (addresses,
hashes, which argument, what its guards test); each re-read here in the
2.50 exe with `tools/re/` (`asm.py`, `xref.py`, `addr.py`). No code of it
was read or copied.

**The transform update of an expired entity.** The job `0x140D408A4`
(hash 469381152, `0xF4` bytes) walks an array of weak entity handles
(`[[rcx] + 0x12500][i]`). For each it zeroes a 16-byte handle at
`[rsp+0x20]` (+0x76: `f3 0f 7f 44 24 20`), locks the weak handle into it
(the strong count at `[refcount]`, a `lock cmpxchg` loop; a null refcount
or a count of 0 skips the copy), then, **whether the lock worked or not**,
calls the per-entity update with the handle's pointer + 0x2E0 (+0xA9:
`48 8b 4c 24 20 48 81 c1 e0 02 00 00 e8` + rel32 → `0x1406107B0`) and
releases the handle. So an entity that expired between the list's
building and the job passes exactly `0x2E0`. The update `0x1406107B0`
(hash 2451640480, `0x1406107B0..0x140610A32`, its only caller the job)
starts `48 8b c4 48 89 58 10 48 89 70 18 55 57 41 56 48 8d 68 98 48 81 ec
50 01 00 00`, and its first use of the argument is the write
`mov byte [rcx+0x39], 1` (`c6 41 39 01`, `0x1406107CA`): a fault at
address `0x319`. Nothing before it has a side effect (the prologue only),
and the caller ignores its return value (`rax` is the last callee's), so
skipping the call for `0x2E0` is the update of an entity that is gone,
not done. The argument's `+8` points at an object holding fixed-point
world coordinates at `+0xE0..+0xE8` and a vector at `+0xF0` (read, scaled
and stored at `+0x10` of the argument). [2.5-static] The other mod saw
the fault under Proton only; the code path is the same on every platform
(timing, or what Windows maps there, may hide it). Tandem's deletions of NPC
proxies (region hand-overs) may make expired handles in that list
likelier.

The guard (`plugin/src/engine/guards.cpp`): only when `ntdll` exports
`wine_get_version`; the 30 bytes of the update's start (prologue and the
faulting write), the job's zeroing and call bytes and its rel32 landing
on the update are checked first, and any difference leaves it off
(`transform guard: … differs from 2.50 (exe+0x…: bytes)`). On, a call
whose first argument is `0x2E0` returns 0 without calling the original;
the first 5 hits and every 1000th are a `transform guard: skipped the
update of an expired entity (N so far)` warning, the total at unload. The
startup line ends `transform guard on` or `off (why)`. [unverified]

**The dismemberment guard** (§13.142's crash; `guards.cpp`, every
platform; built, not run in game). Hooked: `0x14025302C` (hash
2784304484), found through `engine::Find`. Installed at plugin load only
when the function's first 0x20 bytes are 2.50's (prologue, the two reads
of `this`, the faulting `mov rdx, [rdi+0x68]`; none depends on the load
address), else `dismemberment guard: the notification's start differs from
2.50 (exe+0x…: bytes)` and the feature is off. The detour passes through
until the first frame's check has found `gameDismembermentComponent` at
0xB0 bytes (RTTI), then returns without calling the engine when
`[this+0xA8]` is null. On job threads it reads one pointer and adds to an
atomic; the game thread says the count when it changed, at most every 600
frames (`dismemberment guard: N detached notifications skipped`), and the
total at unload. Feature `dismemberment guard`
(`../../architecture/engine-access.md`); `tools/game.sh bt` names the site.

Why skipping is exact, and what else was read for it (exe 2.50, no game
running). [2.5-static]

- The function is void for its caller: `0x1403D1BF8` calls slot +0x198 of
  each component in a loop and reads nothing back. The exe holds its
  address once, in that vtable slot (`0x14314EA00`); no direct call.
- All it does: lock the dismemberment system's data (`[this+0xA8]`, byte
  counter at `[+0x68]`), look the component's entry up by the id at +0x90
  (`0x1402530CC`: index and generation) and set bit `0x20` at entry
  +0x40B. The detach (`0x140252F44`) has already bumped that id's
  generation and put it on the free list: with the pointer kept, the
  lookup would fail and nothing be marked. Null `[this+0xA8]` also means
  "not initialized": slot +0x170 sets it (`entity+0x50 → +0xB8 → +8 →
+0x180`), slot +0x178 zeroes it.
- The detach zeroes +0x98, +0xA0 and +0xA8 only when the byte at
  `0x143C2B514` is 0 (it is set for the engines named
  `serverServerGameEngine`, `HeadlessTestServerEngine`,
  `HeadlessGameEngine`): always, in the game.
- The component's other overridden slots: +0x1B0 (`0x1402A5268`) tests
  +0x98, +0xA0 and +0xA8 before use; +0x180 (attach) reads +0xA8 after
  +0x170 set it. Only +0x198 reads it untested.
- The same notification in other components: of the 234 `entIComponent`
  classes with a vtable in the RTTI dump (212 have none there: not
  looked at), ten functions fill slot +0x198. `entAnimatedComponent`
  (`0x140220D24`) and `AIObjectSelectionComponent` (`0x140937714`) test
  their pointers; the 34 mesh classes (`0x1403D1C74`) and
  `entVisualControllerComponent` set flags and test the handle they ask
  for; `gameCrowdMemberComponent`, `gameinfluenceComponent`,
  `gameinfluenceHeatAgentComponent` and `senseComponent` go through
  `0x1403D0D40`, which acts only while the attached bit (value 2 at
  component +0x88, cleared by a detach) is set; `AICAgent`'s
  (`0x140D659A4`) reads +0xD8, which its own detach still uses after the
  tree's teardown. None but the dismemberment component's reads a field
  its detach zeroed.
- `0x140823F24`, the mask's clear, has 15 direct callers: any of them on
  an entity being detached reaches the same read. The guard covers them
  all; which bit and which caller it was last night does not matter to it.
- Not removed on our side: the stop came from a behaviour tree node of
  the NPC's own AI (frames 15 to 52 are the tree), which scripts cannot
  reach; a proxy's own reaction action (`GetHitReactionProxyAction`,
  `HitReactions.reds`) is another object, and stopping it before a delete
  is not shown to hold or free that bit. [2.5-untested]

**The AI workspot's end with its actor gone.** Two functions of a
0x18-byte listener object (no RTTI class; vtable `0x1430B35B8`, built by
`0x140AB9858`: vtable, then a weak handle to the actor at +8 whose weak
count it raises), slots +0x10 and +0x20, reached only through the
vtable (no direct caller):

- `0x140821360` (hash 3320125101, the spot user's own end): locks
  `[this+8]` (`0x14027C10C`: copy only when the refcount pointer is
  non-null and the strong count non-zero); when the result is non-null,
  `0x1401CB4CC(actor, true)`; releases. `0x1401CB4CC` writes
  `[actor+0x499]` and dereferences `[actor+0x230]`, then
  `[+0xB8] → [+8] → [+0xF8]`, with no null test.
- `0x14082139C` (hash 1948590813, its result to the actor's motion
  planner): the same lock; when non-null, finds a component in
  `[actor+0x230]`'s 16-byte handle array at `+0xA0` (count `+0xAC`;
  `0x14082140C` tests `[actor+0x230]` for null), and when found writes
  `{result, 1}` into what its vtable +0x250 returns.

Both already do nothing for an actor whose strong count is 0. The
crashes the other mod guards (with fault-safe reads of the handle at +8,
its strong count, the pointer and an entity validity test) can then
only be a freed listener (its +8 read after free) or a live actor torn
down far enough that `[actor+0x230]` or the chain behind it is gone.
Neither is a test that can be stated exactly: a fault-safe read only
catches an unmapped page, not freed memory still mapped, and "torn down"
has no flag we know. **No guard for these in tandem**: the two ranges are
named by `tools/game.sh bt` (`known crash site:`) so a freeze there is
recognized; a crash seen there in game is the evidence to design one
from. [2.5-static]

### 13.142 An NPC detached in the middle of an action: its dismemberment component read after its own detach (2026-11-22, game 2.50, exe read)

The guest of a two-game session (instance b; it had ridden in the host's
car, then fought on foot) crashed at 00:09:39: `0xC0000005` at `+0x253048`,
a read of address `0x68`, on a job thread. The commit that followed took it
for the street's bodies (181 asked again in 90 s, §13.141's cause). The
stack (`tools/game.sh bt`, 63 frames; the crash log's first 20 agree) says
it was an NPC.

- **The stack, named**, innermost first. [2.5-static]
  - `0x14025302C` (hash 2784304484): slot +0x198 of
    `gameDismembermentComponent` (vtable `0x14314E868`, in `rax` at the
    fault). It takes `[this+0xA8]`, then locks and reads `[that+0x68]`, with
    no test: `this+0xA8` was null. Slot +0x170 (`0x142B33CF8`) sets that
    field; slot +0x190, the component's detach (`0x140252ED4`), releases what
    it points at (`0x140252F44`, by the id at +0x90) and zeroes +0x98, +0xA0
    and +0xA8. Found, the entry (0x430 bytes, by that id) gets bit `0x20`
    at +0x40B.
  - `0x1403D1BF8` (hash 2569998565), the entity telling its components that
    its mask changed: clears bit `0x20` of `entity+0x15C`, calls slot +0x198
    of every component (16-byte handles at +0xA0, count +0xAC), then its own
    slot +0x170 with the `u16` at +0x158.
  - `0x140823F24` (hash 237772099) `(runtime, entity handle, u16 bits)`:
    clears `bits` in the entity's mask (pending at +0x68: a count in the
    low 4 bits, the mask above, bit 31 "pending"; current at +0x158) under
    the runtime's lock at +0x229F58. While the count at +0x229F5C is 0 the
    change is queued (a 0x38-byte record, `0x14074BE2C`); otherwise, when
    the flag of value 8 at `entity+0x15C` is clear, it is applied in place
    and `0x1403D1BF8` called.
  - `0x140823D7C` (hash 1672482762): the stop of an action object, slot
    +0x28 of two vtables without an RTTI name (`0x1430965C8`,
    `0x143096760`) that lie on either side of
    `gameActionHitReactionState`'s (`0x1430966A8`): it gives its entry back
    (`0x140823E70`) and clears the mask bit of value 8 of its owner. Called by
    `0x1408234A4` (hash 3287092066: state to 5, and the stop when it was 2
    or 3), from `0x141085E34`, from `0x1402346B0` (hash 3694205479), the
    deactivation shared by the AI behaviour tree's action nodes (some fifty
    vtable slots hold it: by the nearest RTTI vtable, those of
    `AIbehaviorActionMountNodeDefinition`, `…UnmountNodeDefinition`,
    `…ActionDieTreeNodeDefinition`, `…DriveChaseTargetTreeNodeDefinition`
    among them).
  - below it some 35 frames of nested tree nodes being deactivated, down to
    `0x1401AC7FC` (hash 3765574005), the tree's teardown, called by
    `0x1405EE548` (hash 1937315105), `AICAgent`'s slot +0x190, through
    `AIHumanComponent`'s (`0x1410E6ACC`, hash 1258428571).
  - `0x140206A14` (hash 2263681375), the entity's detach: state byte +0x156
    to 5, **slot +0x190 of every component from the last to the first**,
    then the entity's +0x168; from the detach job `0x140DBB958` (§13.101's).
- **So**: the engine detached an NPC whose behaviour tree still had an
  action running that held that bit of the entity's mask. The dismemberment
  component, later in the list, was detached first; the AI component's
  detach then stopped the action, the entity told every component of the
  change, and the dismemberment component read the pointer its own detach
  had just zeroed. An order of the engine's own; it needs the runtime's
  count at +0x229F5C to be non-zero at that moment (else the change is
  queued). [2.5-static]
- **What it is not**: not §13.140's site (no stub, no pooling update), not
  a body freed too early (§13.6, §13.101: the entity is alive), not a
  vehicle. Deleting the old body first and backing off, as the street's
  check now does, and the engine's `CancelStub` both leave it as it was.
- **Which NPC, and which action: not known.** In the 3 s before, ten NPC
  proxies had their controller detached (bodies going), one NPC had been
  knocked out by our V 3 s earlier, and the guest was in combat; the vtables
  around the stop are the hit reaction's. A proxy's AI component is
  switched off (`TandemProxies.Pacify`), which may leave an action begun
  before it running for good: inference, to check live (`AICAgent+0x4EE`
  and the tree at +0xE0 of a proxy that was hit). [2.5-untested]
- **What would cover it** (not built): a guard in the plugin, of the kind
  of §13.98's, on `0x14025302C`: return when `[this+0xA8]` is null (the
  component is detached: there is no entry to mark), the function's bytes
  checked at startup. Stated exactly, no side effect skipped. From scripts
  there is no lever: nothing reads whether an action runs, and a proxy
  deleted a few seconds later is only less likely to be in one.
