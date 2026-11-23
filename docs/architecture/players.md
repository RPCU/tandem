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
  swings and every sound still to check. What else a puppet is heard and
  seen doing (steps, voice, bullets, flinches, the party HUD): `presence.md`.
- **Dual wielding** (akimbo, patch 2.4; `Weapons.reds`; engine-knowledge
  §13.151; scripts compiled against 2.50, not run in game). No message
  says it: a dual wielding V is one whose `Held` names two weapons the game
  pairs (both records tagged `DualWield`, `TandemWeapons.IsPair`: the
  game's own test of two weapons in hand). Equip, holster, a switch, a late
  joiner and a puppet built again all follow from that one cached,
  idempotent `Held`.
  - _The puppet's hands:_ each item in its own slot (`WeaponRight`,
    `WeaponLeft`), then what the game's equip states set on V besides the
    items (`TandemWeapons.DualWield`): the body's `dual_wield` animation
    wrapper and each weapon's `RightHand` / `LeftHand` / `dualWield`
    wrappers (the game refreshes those itself when an item lands in a
    `ScriptedPuppet`'s slot; asked again 3 s after the items, log
    `dual wielding, a weapon in each hand`). The arms are the pose
    stream's: each hand holds its own grip, so no two-handed grip to
    correct.
  - _Each hand's own:_ the game runs one weapon state machine per held
    weapon (`WeaponRight`, `WeaponLeft`), so the wraps above run once per
    hand and say which (`IsLeftWeaponLogic`): `Shot.hand` (flash, shells,
    report, the weapon's `Shoot` event, the fire loop's end, the reload),
    the bullet's `kTraceLeftHand` (from the left muzzle), a ricochet's leg
    on from that hand's last shot, a charge per hand (`Cue` Charge, the
    hand in `at.y`), a smart round from the left weapon
    (`kThrowLeftHand`). Before, every left-hand shot was drawn as one more
    of the right hand's.
  - _Hits:_ no second path. The off hand's hit is the game's own damage
    pipeline with the left weapon as the attack's weapon, sent as the same
    `NpcStrike` (the NPC's owner decides, the same friendly fire rule, the
    same referee checks); `kStrikeLeftHand` only tells the owner which of
    the puppet's weapons made it (`TandemOutcomes.StrikeWeapon`: hit
    reaction, the wound's kind), the active one when the puppet holds none
    there yet.
  - _Limits:_ `Held` names records: a weapon's attachments and skin are
    its record's defaults, as for the right hand. The left weapon put away
    alone plays no holster sound (the observer hears the right hand;
    draws are the engine's own). The mock player sends no pose: its
    puppet's arms are the NPC stance's.
  - Protocol 16. Scenarios `presence-akimbo`, `outcomes-offhand`; `cmd test
query held [puppet | player N]`; the mock player's `--hold-left`,
    `--strike offhand`.
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
- **What a partner's game lacks** ([`mod-compat.md`](mod-compat.md)): a
  held item or a piece of clothing from a mod our game does not have
  comes with a hint (`Held::rightHint`/`leftHint`, `Appearance::hints`: its
  item type and category) and is shown as a vanilla one of the same type
  (a handgun for a modded pistol, plain trousers), or left off (a hat, a
  jacket, a whole outfit); a body or face resource we lack (a modded hair)
  is left off, the rest of the look is theirs. Resolved in the natives
  (`RemoteHeld`, `RemoteItems`, `ApplyRemoteLook`), told on screen once per
  partner and kind.
- **What a look shows** (`TandemOutfit.Shown`, `Modest`): exactly what the
  sender's game renders. The paper doll's visual item per area, unless the
  slot is hidden: a scene that undresses V (a shower hides the slots or
  takes the clothes off, engine-knowledge §13.85) removes them from the
  look, and the underwear vanilla shows then (the bottom unless V has the
  genitals option, the top in a censored game) is in it, since it is in
  the paper doll's areas. Checked every 2 s, and four times a second
  during an interaction and 5 s after (`activities.md`, "V's homes"), so
  the puppet undresses and dresses with V. On the receiving side a
  censored game (`IsNudityAllowed` false) adds the basic underwear
  (`Items.Underwear_Basic_01_Top`, `_Bottom`) where the look leaves the
  body bare: never more than the sender's game shows. To check in game:
  that the puppet's record takes items in the underwear slots.
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
  distance text, rewritten after each native update: watch for flicker;
  once our `SetText` reached a controller's distance text, the native
  `UpdateDistanceText` only showed its raw format, "{0, number, 0}M", seen
  2026-11-23 on a pooled controller, a hidden name and an empty one: from
  then on that controller's distance is ours at each `OnUpdate`, whatever
  mappin it holds, `QuestMappinController.m_tandemDistance`)
  and on the map tooltip, and that the marker follows a seated player's car
  copy.
- **Identity colour** (`TandemMarkers.ColorOf`): one colour per player id,
  the same in every game. In order from id 1 to 8 (`kMaxPlayerId`: ids go
  round 2..8 as guests come and go): cyan, yellow, green, pink, orange,
  violet, red, sky blue (`0.55, 0.75, 1.0`); white only for no player. It
  tints a player's marker, nameplate and
  party entry ([`presence.md`](presence.md)), pings, waypoint, team target
  and quickhack indicators ([`comms.md`](comms.md)), and the "Hacked by
  NAME" marker on an enemy's device a partner's hack holds
  ([`devices.md`](devices.md), "A partner's hack, marked"). One definition
  (`Markers.reds`; the `polish` and comms branches each had one, merged
  into it: `tools/scripts-duplicates.py` refuses a second). To check in game: readability against the game's own
  reds and cyans, and colour-blind pairs. Red/green (ids 3 and 7) and
  yellow/green (2 and 3) are the risky pairs, which is why the name always
  comes with the colour.
  - **The 8th colour** (2026-11-19; until then id 8 fell to white, the
    fallback): of every bright, saturated colour (one channel full,
    luminance ≥ 0.2, away from white), the one whose nearest palette colour
    is farthest in CIELAB under normal vision and simulated protanopia,
    deuteranopia and tritanopia (Machado et al. 2009, full severity). Sky
    blue's nearest: cyan for normal vision (ΔE 44), deuteranopia (23) and
    tritanopia (25), pink for protanopia (20). The seven's own closest
    pairs measured the same way are tighter: yellow/orange for
    deuteranopia (18), yellow/green for protanopia (27), cyan/green for
    tritanopia (9). Blue was the obvious pick and the worst: it is
    violet's twin for a protanope (ΔE 0.3).

## Open problems

- **Players linking up.** Written (2026-11-19, to test in game):
  [`comms.md`](comms.md). A V asks another to link by aiming at them with
  the scanner's tag key (or is linked from the start, `link = auto`); once
  both consent, the Ping quickhack's reveals and what each one scans and
  tags show on both HUDs. Either side can break the link at any time. The
  link is each player's consent, kept and relayed by the host. Later, on
  the same link: a netrunner seeing through the other's optics,
  quickhacks relayed through a partner's link.
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
