# Engine facts behind per-player regions

What [`docs/architecture/regions.md`](../../architecture/regions.md) relies
on in the game, each with its source and a tag. The desk study that came
first is [`../coop-regions.md`](../coop-regions.md); this file lists only
what the code now uses.

**None of this has run in Cyberpunk yet.** The scripts compile against the
game's scripts (`task scripts:check`), the names below were read in the
decompiled vanilla scripts of 2.50, and the behaviour is still to be seen in
game with the scenarios `regions-apart`, `regions-meet`, `regions-vanish`.
Update the tags here when they have run.

Tags: `[2.5-compiled]` the name exists in 2.50's scripts and tandem's use of
it compiles; `[2.5-verified]` seen working in game (see engine-knowledge);
`[2.5-untested]` compiles, behaviour not seen yet; `[inferred]` reasoned
from other facts.

`final.reds:LINE` is `~/.cache/tandem/ref/gamescripts/vanilla/final.reds`.

### 13.30 Per-player regions: brains, state, pins, crowd (2026-11-19, scripts only)

The parts below are 13.30 (a) to (f).

#### (a) Switching an NPC's brain off, and back on

| Fact                                                                                                                                                                                           | Source                                                                            | Tag                                                                                                                                             |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `entIComponent.Toggle(on: Bool)` and `IsEnabled()` exist on every component; `AIHumanComponent` (from `GetAIControllerComponent()`) takes them                                                 | `final.reds:19458-19460`                                                          | `[2.5-compiled]`                                                                                                                                |
| `Toggle(false)` on the AI component, `SenseComponent.ToggleComponent(false)` + `RemoveSenseMappin()`, bump `Toggle(false)`, `ClearThreats()` make an NPC a still body (`TandemProxies.Pacify`) | engine-knowledge §13.9, used on every proxy since milestone 5                     | `[2.5-verified]`                                                                                                                                |
| The reverse (`Toggle(true)`, `ToggleComponent(true)`, `CreateSenseMappin()`, bump on) brings the behaviour tree back (`TandemProxies.Wake`)                                                    | `SenseComponent.ToggleComponent`, `CreateSenseMappin`: `final.reds:155542-155650` | `[2.5-untested]` — the open question of coop-regions §2.1 (a). `regions-meet` and `regions-vanish` answer it                                    |
| `IsEnabled()` on the AI component tells whether the brain is on 1.5 s after `Wake` (`TandemProxies.Awake`); when it says no, the NPC is spawned again in place                                 | —                                                                                 | `[2.5-untested]` — it may report the component enabled while the tree never restarts; watch for a woken NPC that stands still in `regions-meet` |
| A proxy is a full NPC of the real record spawned through Codeware's `DynamicEntitySystem` (inventory, stats, hit shapes); an NPC spawned that way with its brain on fights a V and a puppet    | engine-knowledge §13.11 (the test ganger)                                         | `[2.5-verified]` for a fresh spawn; `[inferred]` for one whose brain was off first                                                              |
| Deleting an NPC proxy with `DeleteEntity` is safe                                                                                                                                              | engine-knowledge §13.9                                                            | `[2.5-verified]`                                                                                                                                |

#### (b) Reading and restoring an NPC's state

| State                 | Read (giver)                                                                                                         | Applied (receiver)                                                                                                                                                  | Source                                                            | Tag                                                                            |
| --------------------- | -------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| health, poise         | `StatPoolsSystem.GetStatPoolValue(id, Health/Poise, true)` (percent)                                                 | `RequestSettingStatPoolValue(id, pool, percent, null, true)`                                                                                                        | as `NpcHealth`, `NpcPoise` since milestone 5                      | `[2.5-verified]`                                                               |
| status effects        | `StatusEffectHelper.GetAppliedEffects(obj)`, each `GetRecord().GetID()`, `GetRemainingDuration()`, `GetStackCount()` | `StatusEffectHelper.ApplyStatusEffect(obj, record)` unless `StatusEffectSystem.ObjectHasStatusEffect`, then `SetStatusEffectRemainingDuration(id, record, seconds)` | `final.reds:169206`, `:83690-83712`, `:51210`                     | `[2.5-untested]` — stacks are read and sent, not applied yet (one application) |
| only timed effects    | an effect with `GetRemainingDuration() > 0.1` is carried, at most 8                                                  | —                                                                                                                                                                   | —                                                                 | `[inferred]`: permanent effects come with the record                           |
| in combat             | `NPCPuppet.IsInCombat(npc)`                                                                                          | follows from the injected threat                                                                                                                                    | `final.reds:130014`                                               | `[2.5-compiled]`                                                               |
| whom it fights        | `NPCPuppet.IsInCombatWithTarget(npc, v)` per V (ours, and each puppet)                                               | `GameObject.ChangeAttitudeToHostile(npc, v)` + `TargetTrackingExtension.InjectThreat(npc, v)`                                                                       | `final.reds:130026`, `:11816`, `:156710`; the test ganger, §13.11 | `[2.5-verified]` on a fresh NPC, `[2.5-untested]` on a woken one               |
| hostile to which Vs   | `GameObject.GetAttitudeBetween(npc, v) == AIA_Hostile` per V                                                         | `ChangeAttitudeToHostile(npc, v)`                                                                                                                                   | as `Npcs.reds` uses it for proxies                                | `[2.5-verified]`                                                               |
| high-level state      | `ScriptedPuppet.GetHighLevelStateFromBlackboard()`                                                                   | sent, not forced: the tree decides                                                                                                                                  | `final.reds:20698`                                                | `[2.5-compiled]`                                                               |
| defeated, unconscious | `ScriptedPuppet.IsDefeated`, `IsUnconscious`                                                                         | a body that is down is never handed over                                                                                                                            | `final.reds:20986`                                                | `[2.5-compiled]`                                                               |
| yaw                   | `Deg2Rad(GetWorldYaw())`                                                                                             | (the body is already there)                                                                                                                                         | `final.reds:108165`                                               | `[2.5-compiled]`                                                               |

Not carried (no script access): the behaviour tree's inner state, cover,
squad, the animation in progress, a quickhack upload in flight
(coop-regions §2.3).

#### (c) Which NPCs never change owner

| Fact                                                                                                                                  | Source                                      | Tag                                                                                                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ScriptedPuppet.IsQuest()` is `m_markAsQuest`; `IsBoss()` and `IsPrevention()` exist on `ScriptedPuppet` (`TandemRegionRules.Pinned`) | `final.reds:22687`, `:20825`, `:21090`      | `[2.5-compiled]`; whether story companions (Jackie, Panam) are marked as quest is `[2.5-untested]` — if not, they need another test (their record, or being in a scene) |
| Police units are registered with the prevention system of the game that spawned them                                                  | coop-regions §2.3 (`PreventionSpawnSystem`) | `[inferred]`: pinned for that reason                                                                                                                                    |

#### (d) Crowd and spawning

| Fact                                                                                                                                                                                                                                    | Source                        | Tag                                                                                                                             |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `CommunitySystem.ChangeDensityModifier(0.0)` turns the ambient crowd off, `ResetDensityModifier()` brings it back                                                                                                                       | `Npcs.reds` since milestone 5 | `[2.5-verified]`                                                                                                                |
| The modifier is global to a game, not spatial: "suppressed inside the overlap only" is done by hiding new spawns there (`Pacify` + `HideIrreversibly`) while our V is outside the other's region, and by the modifier once it is inside | —                             | `[inferred]`                                                                                                                    |
| Switching the modifier back and forth as our V crosses the 45 m / 60 m line of another player's region                                                                                                                                  | —                             | `[2.5-untested]`: how fast the crowd comes back is not known                                                                    |
| `ScriptedPuppet.HideIrreversibly()` cannot be undone: a body given away in place and hidden when the link breaks stays hidden until it streams out                                                                                      | `final.reds:19392`            | `[2.5-verified]` for own NPCs (the old `HideOwn`)                                                                               |
| `GameObject.GetNPCsAroundObject(range)` at 90 m (it was 60 m)                                                                                                                                                                           | —                             | `[2.5-untested]`: cost of the wider query, ten times a second                                                                   |
| Where a linked player is comes from the plugin's track of its states (`Runtime::ViewOf`), not from its puppet (always spawned, but seconds late at join)                                                                                | coop-regions §1.4 point 6     | `[inferred]`; a puppet hidden or held elsewhere (another world, a metro) gives its body's place, as the host's arbitration uses |

#### (e) Entities

| Fact                                                                                                                                                | Source                          | Tag                                                                                                                                                                |
| --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `GameInstance.FindEntityByID` finds both the game's own entities and Codeware's dynamic ones (`Takedowns.reds` resolves proxies with it)            | `Takedowns.reds` `BodyOf`       | `[2.5-verified]`                                                                                                                                                   |
| `DynamicEntitySystem.GetEntity(id)` only knows its own: a body of the game's own that became a proxy in place is resolved by `TandemNpcs.ProxyBody` | Codeware                        | `[inferred]`. Stealth's meter, loot and the wound replay still resolve proxies through `GetEntity`: on a body lent in place they do nothing until it is given back |
| tandem's movement controller attaches to an NPC that was the game's own (not spawned by us)                                                         | `TandemNative.AttachController` | `[2.5-untested]`: it was only ever attached to our spawned proxies. `regions-meet` run the other way (the game walks away from its NPCs) shows it                  |
| A community NPC with its AI off keeps its workspot or gets pulled back by its community                                                             | —                               | `[2.5-untested]`: a lent body may be re-posed by the community system                                                                                              |
| An adopted ambient NPC (a woken proxy of a crowd record) stands or wanders by its record's default behaviour                                        | —                               | `[2.5-untested]`                                                                                                                                                   |

#### (f) Probes worth running first

In this order, each one decides a fallback:

1. `regions-vanish`: three still NPCs of the mock player become ours. Do
   they wake (`handover in`), and is there a `did not wake up` line? This is
   (a)'s open question with the least moving parts.
2. `regions-meet`: the same with a walk, a hit routed to the owner, and a
   target (`target player 1`): do the woken NPCs attack?
3. The game as the giver: join a mock host in `--mode region` standing by
   the game's own NPCs, walk the game's V 100 m away (`teleport`). Expect
   `handover out` and the NPCs staying visible, moved by the peer.
4. `regions-apart` then walk toward the mock host: density off at 45 m,
   back on beyond 60 m; count how long the crowd takes to return.
