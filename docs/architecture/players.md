# Players and their puppets

## Inside the plugin

- **Body poses** (`engine/posestream.cpp`, `core/src/pose.cpp`): the local
  V's pose is read after the engine's per-entity animation job, turned into
  local rotations on the third-person body rig and sent as `PoseState` at
  30 Hz; on the others it is written into the puppet's root graph output
  where the engine commits it, so the puppet's own deformation and face
  graphs follow (`docs/research/pose-streaming.md`). Both hooks run on job
  threads and only touch the pose buffers.
- **Weapons** (`scripts/tandem/Weapons.reds`): what each V holds travels as
  `Held` (the right and left hand items' TweakDB ids, reliable, cached for
  late joiners); the puppet gets the same items in the same slots and the
  pose stream moves them. Each shot, reload and melee swing (wraps of the
  player state machine's `ShootEvents.OnEnter`, `ReloadEvents.OnEnter/OnExit`,
  `MeleeAttackGenericEvents.OnEnter`, `QuickMeleeEvents.OnEnter`) travels as `Shot` (unreliable) and
  plays the weapon's own effects on the puppet
  (`WeaponObject.TriggerWeaponEffects`: flash, shells, report, reload; the
  NPC melee sound `AI_PlayMeleeAttackSound`), no projectile. Hits are
  separate (milestone 5). Verified in game 2026-11-18: held weapon, shots
  (28 in one session), reloads (start and end), quick melee; melee-weapon
  swings and every sound still to check.
- **Looks** (`engine/look.cpp`, `scripts/tandem/Outfit.reds`): a V's face
  and body travel as the appearance keys the game's appearance changer takes
  (a resource path hash and a name hash each: the same in every copy of the
  game), its clothes as the TweakDB ids of what the wardrobe shows. Sent as
  `Appearance` (reliable) when the clothes change; the host keeps everyone's
  latest for late joiners. A puppet wears the clothes as preview items, the
  way the inventory paper doll dresses V. V's templates have no meshes of
  their own, so clothes are never shown on no body: a look with no keys
  (the mock player's random look) or keys that fail get a stand-in body,
  the last real look of that body seen this game (ours or another
  player's), else ours when our body is theirs, else the donor's whole NPC
  body (`Character.TandemStandIn{Wa,Ma}`: Panam's or Kerry's, V's skeleton,
  in its own clothes), built again as their V once their look has keys. A
  puppet is hidden for want of a look (`PuppetVisibility.reds`, reason
  `nolook`) only while that body is built, or if it cannot be. The mock
  player's random look wears a real V body: the keys of every look it hears
  are kept per body (`~/.cache/tandem/looks/{female,male}.keys`,
  `--look-keys DIR`). Hiding fades the body and its item entities
  together, then hides every mesh of both.
- **Player markers** (`scripts/tandem/Markers.reds`,
  `tweaks/tandem/markers.tweak`): every remote player has a marker in the
  world, on the minimap and on the world map, the way the game marks your
  car: a mappin registered with `MappinSystem.RegisterMappinWithObject`
  on the puppet as soon as it exists (the `roleMappin` slot if the body has
  one, else 2 m above its feet), moved to the car the puppet sits in (the
  vehicle's `vehMappin` slot, as for your car's marker) and back, and
  unregistered when the player leaves (`TandemSystem.Leave`). The mappin
  system moves it with the entity. Its variant is the game's own for a
  remote player, `CPO_RemotePlayerVariant` (from the cut multiplayer; never
  registered in single player), so the UI changes touch no other marker:
  the world container draws it with the quest marker widget and the car's
  profile (`WorldMappinUIProfile.Vehicle`, spawned at any distance), the
  minimap with its point-of-interest widget, both with the `fixer` icon of
  the mappin atlas; the world map shows it whatever the zoom and filters.
  The player's name is the world label and the world map tooltip's title;
  the icon has the player's colour (`TandemMarkers.ColorOf`, by id: the
  same in every game, so four players tell their markers apart).
  Its definition, `Mappins.TandemPlayerMappin`, is the game's remote player
  definition with `showInWorld`, `showOnMinimap`, `showOnMap` forced on.
  `tools/game.sh cmd markers` logs the definition's flags and where each
  marker is; scenario `markers`. **Not yet verified in game**: that it
  shows at all in each of the three places (the definition's other fields,
  e.g. `visibilityRange`, are the base's), the icon and its colour (no
  mappin group knows the variant: the widgets fall back to their quest
  state), the name label in the world (the quest widget's display name or
  distance text, rewritten after each native update: watch for flicker)
  and on the map tooltip, and that the marker follows a seated player's car
  copy.

## Open problems

- **Players linking up (later).** One V can send the other a request to
  connect, in the game's own style (a neural link, like jacking in with a
  partner); once accepted, the two share what a linked crew would: the Ping
  quickhack's highlights and what each one scans or tags shows on both
  HUDs, and later more (shared marks, a netrunner seeing through the
  other's optics, quickhacks relayed through a partner's link). Either side
  can break the link at any time. The link is a per-pair state the host
  relays; what it shares is decided feature by feature.
- **Every way a V moves, on the puppet.** Walking on flat ground works; the
  rest must too: stairs and slopes, ladders, climbing and vaulting
  (mantling ledges), jumps and double jumps, falls and landings, sliding,
  dodges and air dashes, swimming on the surface and diving, elevators and
  moving platforms, being carried or knocked down. The pose stream gives
  the body its animation; placement is the hard part: the movement
  controller moves an NPC body along the ground and may refuse or snap
  positions an NPC could not reach (off the navmesh, mid-air, in water, on a
  ladder). Options per case: let the controller follow the streamed
  position freely (no ground snap) while the player is airborne, climbing
  or swimming (`LocomotionDetailed` says which); teleport for jumps up a
  ledge; check that an NPC body can enter water at all (else a dedicated
  "swim" placement). Each case gets a recorded-pose test and a scripted
  mock-player path (stairs, ladder, water). Facts:
  `research/coop-player-vehicles.md` §5 (locomotion specials).
  **State (2026-11-18, built and unit-tested, not yet tried in game):** our
  V sends how it moves (`PlayerState::flags`, see "How a V moves" in `smoothing.md`);
  the controller follows the stream freely in the air, climbing, on a
  ladder, swimming, in an elevator or a workspot and when knocked down,
  eases the height back on landing, and has the game thread teleport a
  body left behind (a ledge, a refused move); a body in the air keeps
  falling while its states are late; no walk cycle under the pose on a
  ladder, in the water or climbing; puppets are spared the NPC rule that
  kills a body with its head under water (`NPCPuppet.KillIfUnderwater`).
  What the engine does with the controller's delta (kinematic, or a
  character controller with gravity and collisions) is not known: the
  health line (`puppet N: … m from target, …, <placement>, K teleports`)
  and the `teleport #` lines tell. To check in game:
  `tandem-peer join <game> --mode course` (scenario `movement-course`),
  then `--save-path` a real jump/ladder/swim session and replay it with
  `--mode path`. Open: elevators (the cab is each game's own device), being
  carried (scenes), landing and splash effects, the collision capsule.
- **Cyberware, skills and perks in a shared world.** Every ability a V has
  must work in co-op, on real enemies and on the other players' screens,
  with the game's own sound and visual feedback ("seen and heard"): arm
  cyberware (mantis blades, gorilla arms, monowire, projectile launcher),
  legs (double jump, charge jump), Sandevistan, Kerenzikov, Berserk, optical
  camo, quickhacks and the RAM they use, skill and perk effects (slow
  motion, damage bonuses, finishers, health and stamina effects). Rule to
  start from: the player's own game runs the ability for that V (input,
  animation, cost, cooldown, its local effects); what it does to shared
  state goes through the owner of that state (a quickhack or perk effect on
  an NPC is a request to the host, like a hit); the others see and hear the
  ability on the puppet (status effect VFX, weapon and cyberware visuals,
  sounds). Abilities that bend time follow the time rules (`time.md`). Facts per
  ability: `research/coop-player-vehicles.md` §4, `research/coop-time.md`,
  `research/coop-npcs-combat.md` (quickhacks, finishers).

  Every perk, relic perk, cyberware ability and quickhack has been read for
  co-op risk (`research/coop-perks.md`, 2026-11-18, scripts only). Most
  perks are attacker-side modifiers and already work through the guest's
  hit pipeline. The blockers have a few root causes: on the host a guest's
  puppet has no perks (`PlayerDevelopmentSystem.GetData` is null for it)
  and NPC-side code reacting to "the player" acts for the host's V (second
  chance, gag, finisher offers, Bait, quickhack alerts and reveals, spread
  stats, NPC netrunners hacking only the host); a V's defensive side
  (blade deflect, armour cyberware, dodge, health floors) must run on the
  guest for NPC hits (done: the host sends the attack, the guest runs the
  hit on its own V; see Fights, `combat.md`); camo and visibility must reach the host's senses;
  carrying, throwing, finishers and Breach Protocol daemons change host
  NPCs and devices. Rules to adopt with them: a hit carries its whole
  outcome (poise, non-lethal, cap at 1 HP, reaction, dismemberment); the
  guest predicts kills so on-kill perks fire at the hit, and queues its
  skill XP itself (done: see Fights, `combat.md`); area and spreading effects run on one game;
  the session plays at the host's difficulty.
