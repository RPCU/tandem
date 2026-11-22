# Engine access that fails soft

The rule (`../ARCHITECTURE.md`, "Talking to the engine safely"): what goes
by an address-library hash, a raw offset or a vtable slot is checked against
the running game, and what does not match turns **one feature** off with one
log line. The game always starts. Code: `plugin/src/engine/access.{hpp,cpp}`.

## The lookup

`engine::Resolve(hash)` answers the address, or 0 when the game's address
library lacks the hash. It calls RED4ext's own export
(`RED4ext_ResolveAddress` of `RED4ext.dll`, the function RED4ext.SDK's
`UniversalRelocBase::Resolve` calls); the SDK's wrapper ends the process on
0 (`vendor/RED4ext.SDK/include/RED4ext/Relocation-inl.hpp`), so
`UniversalRelocFunc`, `UniversalRelocPtr` and `UniversalRelocBase::Resolve`
are not used in `plugin/src/`.

`engine::Find<Fn>(feature, what, hash)` is `Resolve` for a feature: on 0 it
turns the feature off and logs
`engine: FEATURE is off: WHAT (hash N) is not in this game's address library`.

A new call or hook by hash:

```cpp
static const auto move = engine::Find<MoveToFn>(feature, "MoveTo", kMoveTo);
if (!move)
  return false; // logged once, the feature is off
```

and its check called from `CheckAccess` (`access.cpp`), so that the startup
line lists it.

## Features

A feature is a named piece of engine access with three states: on, off (the
first reason is kept), not asked for (`tandem.ini`, or not Wine).
`CheckAccess` runs every feature's check once, on the game thread at the
first frame of the Running state (the game's classes are registered by
then; hooks are installed earlier, at plugin load, and only call the
engine's function until their check has passed). A blocked plugin
(`release.md`, the startup check) checks nothing. Then one line:

```
engine: 12 features on, 0 off, 1 not asked for: puppet movement on, clip lengths on, …, anim trace not asked (tandem.ini trace and mirror off)
```

A warning when one is off: `…, looks OFF (the appearance changer's schedule
(hash 386815609) is not in this game's address library), …`.

- `tools/game.sh engine`: the line of the newest log (`cmd engine` asked of
  a running game first); exit 1 when a feature is off.
- Scripts: `TandemNative.EngineOn("puppet movement")`,
  `TandemNative.EngineOff()` (the names that are off, `""` when none),
  `TandemNative.EngineReport()`. At a session's start a game with a feature
  off logs `engine: off in this game: …` and shows the notice
  `Session-EngineOff`.

Layouts and hashes below were measured on 2.50.

<!-- prettier-ignore -->
| Feature | Hashes | Layout check | Off means |
| --- | --- | --- | --- |
| puppet movement (`hooks.cpp`, `animation.cpp`) | 2268141838 (idle `SetAnimation`, hooked), 4018412273 (`AttachController`), 2624656810 / 2385973466 / 2132808949 (animation driver attach, detach, send) | sizes of `moveComponent` 0x2C0, `entEntity` 0x160, `entIPlacedComponent` 0x120, `entIComponent` 0x90, `gameuiCharacterCustomizationSystem` 0x3D8; `entIComponent.name` +0x40, `.id` +0x60, `entEntity.customCameraTarget` +0x154, `entIPlacedComponent.worldTransform` +0xE0 | the idle hook reads nothing and only calls the engine; `AttachController` answers false: remote players and NPC proxies do not move |
| clip lengths (`animation.cpp`) | 3750304414 | — | `ClipLength` answers 0: the gait keeps its default pace |
| looks (`look.cpp`) | 386815609 (the appearance changer's schedule) | — | `ScheduleLook`, `ApplyLook` answer -1: nobody is dressed (scripts fall back to the stand-in body) |
| look keys (`look.cpp`) | 4921241, 3766105236, 56573295, 110771572: the functions in the customization state's vtable slots +0x120, +0x190, +0x1A0, +0x1B0 | each of the four slots of the state at hand holds that function (once per vtable, before any slot is called); slot +0x120 agrees with the gender passed | `CollectLook` is empty, `ApplyLook` -1: our own look is not read or sent; looks received are still scheduled |
| third-person mark (`natives.cpp`, `MarkThirdPerson`) | — | `gamePuppetPS` 0x78 bytes, `hasQuickHackBegunUpload` +0x71 and `hasAlternativeName` +0x74 (the byte written is +0x72), `gameObject.persistentState` exists | not marked: a puppet's clothes may stay first-person ones |
| pose stream (`posestream.cpp`) | 2251364700 (entity animation job), 2074943978 (pose commit), both hooked | `entAnimatedComponent` 0x2B0, `.rig` +0x138, `.graph` +0x150; `animRig` 0x180, `boneNames` +0x50, `referencePoseMS` +0x60, `trackNames` +0xE8, `referenceTracks` +0xF8; a graph instance's rig (+0x40) is an `animRig` by its class before it is mapped (vtable in the game's image, guarded) | nothing is bound: the hooks only call the engine, no pose is read or written |
| quest nodes (`quests.cpp`) | 3227858325 (`ExecuteNode`, hooked) | `questPhaseInstance` 0x110, `CResource` 0x40, `questNodeDefinition` 0x48, `questQuestsSystem` 0x300 | no freeze, no phase notes, no quest trace; the scene observer has no node to read |
| quest freeze (`quests.cpp`) | 3144298192 (`CreateContext`), 2987275044 and 3749393265 (`ExecutePath` of `questRootInstance` and of `questPhaseInstance`) | quest nodes on; the root phase's vtable slot +0x120 holds one of the two functions; the context (0x340 bytes, not an RTTI class) is built in a buffer with 0x200 guard bytes after it, which `CreateContext` must not touch | `FreezeAvailable` is false: a guest's own quests are never frozen. Found while giving signals back: those signals are dropped and said so |
| quest prefabs (`prefabs.cpp`) | 660547284 (`SetMode`) | `questQuestsSystem` 0x300, `questQuestPrefabsHandler` 0xF0, `questQuestPrefabsEntry` 0x10, `worldRuntimeSystemWorldStreaming` 0x3E0; at the first use, the classes of the objects at system +0x100 and handler +0xC8 | every prefab call answers "unavailable": a guest does not see the host's quest interiors |
| workspots (`workspots.cpp`) | 2234523611 (instance lookup) | `workWorkspotSystem` 0xAE0, `workWorkspotInstance` 0x2E0; each instance's class and tree handle at the read | `WorkspotOf` answers None |
| vehicle move (`vehiclemove.cpp`) | 2933986803 (the vehicle's kinematic move) | `vehicleBaseObject` 0xBC0; the body (+0x2D8) and runtime data (+0x5F0) pointers at each call | `MoveVehicle` answers false: vehicle copies are teleported (`vehicles.md`) |
| physics watch (`physics.cpp`) | 3885377886, 3646232200, 3069123790, 3811643879, 2330137828 | the bytes of four functions that give the queues' offsets | `PhysicsBacklog` answers -1: a long pause is not watched |
| transform guard (`guards.cpp`, Wine only) | 2451640480 (hooked), 469381152 | the bytes of the update's start and of its caller's call | the engine's own crash on an expired entity is not guarded |
| anim trace (`trace.cpp`, `trace` or `mirror` on) | 316482401 (`ApplyFeature`, hooked) | — | no trace, no mirror |

Checked at their first use and not in the line: the pose hook (`pose.cpp`,
a probe installed by `TandemNative.PoseHook`: `animAnimNode_Output` 0x60,
its vtable and slot +0x128 inside the game's image), the device-cast reader
(`casts.cpp`: answers Unknown), the scene classes (`scenes.cpp`: RTTI
property offsets only).

Not checkable from RTTI, and said here so nobody assumes otherwise: the
signature of a function found by hash (its arguments, the `std::function`
the appearance changer takes), the animation driver's and the movement
controller's own layouts (`animation.hpp`, `controller.hpp`: no RTTI class),
the quest context's fields (+0xF8, read guarded for the trace and the phase
notes only).

## Verifying

- A normal start on 2.50: the line says `0 off`.
- A test build with one hash changed to a missing one must start the game
  with that feature off and the others on (not run in game yet).
