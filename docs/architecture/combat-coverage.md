# Every way of fighting, between two players

What each way of fighting does in a session, by who attacks and what is
hit, and the rules that make it so. The mechanisms are in
[Fights and quickhacks](combat.md); NPCs in [NPCs](npcs.md); detection,
takedowns and finishers in [Stealth](stealth.md); time bending in
[Time](time.md); what each message guards against in
[Consistency](consistency.md). Perks are read in
[research/coop-perks.md](../research/coop-perks.md); the Relic tree
(Phantom Liberty's Espionage perks: breaches, relic camo, the arm
cyberware's relic attacks) is audited on its own, from
[coop-perks.md §4](../research/coop-perks.md#4-relic-tree-espionage). Phantom
Liberty is required: its weapons, cyberware, the Relic and its ultimate
quickhack (Blackwall Gateway) are rows here like the base game's.

**Rules this matrix holds to.** Perks and records are never modified. Each
player perceives single-player mechanics: whatever the attacker's game
decides (aim, lock-on, the projectile's flight, the explosion's reach, its
own perks) and whatever the target's game decides (armour, block, parry,
dodge, perks, death) runs as in solo, each side once, on the game that owns
it. A V never loses a limb (`TandemOutcomes.KeepsLimbs`).

## Legend

Columns: the attacker (the host's V, a guest's V) against a host NPC, the
other player's V, or its own V (self damage), and what the other game shows
of it.

- **W** works: seen in game, or the owner's game runs vanilla untouched.
- **D** degraded: works, with the loss named.
- **B** broken: does not work.
- **U** untested: built, holds by construction, not run in game yet.
- **–** not a thing (no such attack in the game).

Friendly fire between the Vs is the session's rule (`friendly_fire` in the
host's `tandem.ini`, `proto::Rules`, every game plays by the host's): **off
by default**, so every "other V" cell is "no damage" (W: vanilla drops a
hit on a friend, `DamageSystem.CheckForQuickExit`, and tandem drops the
rest before `DealDamages`). On, every V hit on the other V's puppet runs
the attacker's side and goes as `AreaHit` to that V's game (U). Whether off
and every hit: [Decisions](#decisions).

## The Vs attacking

Counts at the end. Evidence names a scenario, a code path or a dated check.

<!-- prettier-ignore -->
| Way of fighting | Host V → host NPC | Host V → guest V | Host V → own V | Guest V → host NPC | Guest V → host V | Guest V → own V | Seen on the other game |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Power weapons (pistols, revolvers, rifles, SMGs, shotguns, LMG/HMG, precision, snipers; ricochets) | W vanilla | W off / U on (`AreaHit`) | W vanilla (ricochets) | W gun hits and deaths (`npcs-hit`, 2026-11-18); U the whole outcome (`outcomes-*`) | W off / U on | W vanilla | W muzzle flash, shells, report, reload (28 shots, 2026-11-18); D no tracer or bullet |
| Tech weapons (charge, piercing walls and cover) | W vanilla | W off / U on | – | U the charged shot pierces on the guest's own copy of the static geometry and strikes the proxy behind it (`WeaponFullyCharged` set in its pipeline); D a door open on one game and shut on the other (devices) | W off / U on | – | D the shot, not the charge glow |
| Smart weapons (lock-on, guided rounds) | W vanilla | W off / U on | – | U locks on proxies (the host's record, its targeting components, the host's attitudes copied), the rounds home on the guest's game | W off / U on | – | U the round homes on its lock from the puppet (`Throw` Smart, scenario `combat-throw-guest`) |
| Melee weapons: blades, blunt, fists | W vanilla | W off / U on | – | W punch (`npcs-punch`, 2026-11-18); U blade wounds and the limb cut by the host's hit reaction (`outcomes-blade`), U non-lethal knock-out (`outcomes-knockout`), U poise and the finisher it opens (`outcomes-poise`) | W off / U on | – | U swing sound (`AI_PlayMeleeAttackSound`), the swing in the pose stream |
| Arm cyberware: mantis blades, gorilla arms, monowire | W vanilla | W off / U on | – | U as melee; D gorilla arms' one-punch slow motion goes to the host's V (`NPCPuppet.OnStatusEffectApplied` → `GetPlayer`, coop-perks §5.3); U monowire's upload through the quickhack path | W off / U on | – | U arms look (appearance sync) |
| Thrown knives and axes | W vanilla | W off / U on | – | U the projectile's hit is a strike | W off / U on | – | U a copy flies from the puppet, sticks where the real one stuck and goes when its owner picks the real one up (`Throw` Knife); the copy refuses every pickup |
| Grenades: frag, incendiary, EMP, biohazard, sonic (damage, status effects) | W vanilla | W off / U on | W vanilla | U the explosion runs on the guest's game, each proxy's share a strike, its status effects `NpcEffect`; D NPCs beyond the guest's 60 m proxy range are out of its reach | W off / U on | W vanilla | U a copy flies from the puppet, its fuse held, and explodes where and when the real one did (`combat-throw-host`, `combat-throw-guest`); its hits are dropped |
| Flash and smoke grenades (perception) | W vanilla | – | W vanilla | U flash: the blind status effect goes as `NpcEffect`; U smoke: the copy's vision blocker stands on the host, so its NPCs lose sight as vanilla's do | – | W vanilla | U as above |
| Recon grenade (reveal) | W vanilla | – | – | U on the guest's own proxies | – | – | D the reveal is the thrower's only (sharing it is "players linking up", players.md) |
| Cutting (laser) and piercing grenades | W vanilla | W off / U on | W vanilla | U as grenades (lasers on the guest's game) | W off / U on | W vanilla | U the copy cuts its own harmless lasers |
| Sticky and homing delivery | W vanilla | W off / U on | W vanilla | U | W off / U on | W vanilla | U the copy sticks or homes by itself, then is put where the real one ended |
| Mines | – (no player mine in 2.50: `MineDispenser` is an empty stub) | – | – | – | – | – | – |
| Arm projectile launch system (rounds, charged, tracking) | W vanilla | W off / U on | W vanilla | U the explosion on the guest's game, strikes | W off / U on | W vanilla | U a copy from the puppet's left-hand launcher (`Throw` Launcher); D none when the puppet does not hold the launcher (`Held`) |
| Sandevistan, Kerenzikov, slowed time | W vanilla on its own game | – | – | U bubbles (time.md, scenario `time`) | – | – | U the bubble on the others (time.md), and the game's soft slow-motion cue on a V it slows (`time-scan`); U the ability's status effect seen on the puppet (presence.md, "Status effects") |
| Berserk, ground slam, superhero landing | W vanilla | W off / U on | – | U the area attacks on the guest's game, strikes; U the slam's noise relayed (`Noise`) | W off / U on | – | D no slam effect on the puppet; U Berserk's own status effect look on it (`StatusVisuals`) |
| Optical camo | W vanilla | – | – | U the host's NPCs lose the puppet (`Stealth`, `stealth-guest`) | – | – | U the puppet wears the camo record itself, as transparent as the V (`Stealth`, `presence-status`) |
| Chain lightning, cyberware explosion | W vanilla | W off (puppet hits dropped) / U on | – | U strikes on proxies; the chain picks proxies with no attitude check: it can pick the puppet (dropped, off) | W off / U on | – | D no effect on the puppet |
| Combat quickhacks: Short Circuit, Overheat, Synapse Burnout, Contagion, Detonate Grenade | W vanilla | – (no hack on a friend) | – | U the upload and effect run on the guest's game, effects to the host (`hacks-upload`, `hacks-guest`); U spread on the caster's game only; U refused whole (upload, effects, ticks) on an actor of the host's scene while it lasts (story.md, the host's backstop); D Detonate Grenade's grenade (`GrenadeLvl4HackEffector`) is the host NPC's, its damage the host's | – | – | U upload bar and status on the proxy (`NpcHackState`, `NpcStatus`) |
| Covert quickhacks: Ping, Sonic Shock, Reboot Optics, Memory Wipe, Request Backup, Whistle | W vanilla | – | – | U as above; U Whistle walks the NPC to the guest (Reactions.reds); D Ping's highlights are the caster's only | – | – | U |
| Control quickhacks: Cripple Movement, Weapon Glitch, Cyberware Malfunction | W vanilla | – | – | U; refused on a host scene's actors as every quickhack | – | – | U |
| Ultimate quickhacks: Suicide, Cyberpsychosis, System Collapse, Blackwall Gateway | W vanilla | – | – | U the kill or madness on the host's NPC; D System Collapse's trace and cost buffs (`SystemCollapseModifyRevealBarEffector`) go to the host's V (coop-perks §6.2) | – | – | U |
| Breach Protocol | W vanilla | – | – | U the minigame on the guest's game, its rewards paid there once the host grants the breach (a refused one pays nothing and names who was first); its outcome a `Breach` to the host: the network breached once for everyone (the access point's record), its daemons run by the host on the network's devices and its NPCs from the guest's puppet, by every other game on the NPCs it runs (`breach-guest`, devices.md); U the quickhack on a host NPC: that NPC by its owner, its network by the host; U a mod that skips the minigame (AccessPointSkip) through the same outcome | – | – | U the puppet busy in its minigame (activities.md); the daemons' effects on proxies (`NpcStatus`), the devices through their state (`breach-host`) |
| Operating systems: cyberdeck (RAM, queue, Overclock), Sandevistan, Berserk | W vanilla | – | – | W the caster's own game (RAM, cooldowns, queue on the proxy: coop-perks §6.1) | – | – | – |
| Vehicle combat (mounted guns, missiles, rams) | W vanilla | W off / U on | – | U hits from the driver's game on proxies; D missiles not shown (vehicles.md) | W off / U on | – | D |
| Environment: explosive barrels and devices, turrets, electrified floors | W vanilla | W off / U on | W vanilla | D a barrel the guest shoots explodes on its game only (devices are the host's: devices branch); its strikes count | W off / U on | W vanilla | D |
| Environment: falls, drowning, fire, own crashes | – | – | W vanilla | – | – | W vanilla | U a burn, shock, poison or bleed on the V shows on its puppet, its look and sound only (`StatusVisuals`, `presence-status`) |
| Finishers and takedowns | W vanilla | – | – | U a request to the host, played on the real NPC (`takedowns-host`); U the finisher a broken guard offers goes to whoever broke it (`PoiseBroken`) | – | – | U the workspot on the puppet |

## The host's NPCs attacking

<!-- prettier-ignore -->
| Attack | On the host's V | On a guest's V | Seen on the other game |
| --- | --- | --- | --- |
| Gunfire | W vanilla | U fired from the proxy and decided on the guest's game (`npcs-shoot-guest`); **was B**: those hits were dropped as a proxy's own (`IsProxyComputed`), fixed here (`TandemShots.IsSimulatedHit`) | U the shot on the proxy (`NpcShot`) |
| Smart weapons | W vanilla | D the proxy fires plain rounds (no lock), the hit is still the guest's | U |
| Melee | W vanilla | U the host's hit is held until the proxy's swing lands on the guest's screen, then run on its V (`TandemFight.MeleeHold`, `combat-melee-guest`) | U the NPC's animation (`NpcAnims`) |
| Blocking an NPC's melee | W vanilla | U on the guest's game (`ProcessDefensiveState`), and the host's NPC recoils (`Defended`, `combat-defend`) | U |
| Deflect and parry | W vanilla | U as blocking; the parry's stagger on the host's NPC | U |
| Bullet block, deflect and parry with a blade | W vanilla | U the shot is the guest's game's, so its perks apply; a deflected bullet strikes the proxy (a strike) | U |
| Grenades | W vanilla | U damage as `PlayerHit`; U seen: a copy from the proxy (`kThrowByNpc`); **was B** (nothing flew) | U |
| Thrown knives, launchers | W vanilla | U damage as `PlayerHit` | D not shown on guests |
| Netrunner hacks | W vanilla | U on the guest's V (`PlayerHack`, `hacks-guest`) | U |
| Area attacks, explosions, fire | W vanilla | U `PlayerHit`, the target's side on the guest | U |

## Companions and allies

Jackie, Panam, Judy, Takemura and every other follower (`AIFollowerRole`,
`ScriptedPuppet.IsPlayerCompanion`) are the host's NPCs: the guest sees
proxies of them.

<!-- prettier-ignore -->
| What | State |
| --- | --- |
| They see the guest as an ally | U: a puppet gets V's attitudes per agent (Attitudes.reds), so they never target it, and their stray hits on it are dropped as vanilla drops a friend's |
| The guest cannot hurt them | W: friendly to the guest's V on its game (the proxies' attitudes), vanilla's friend rule |
| They fight whoever fights the guest | U: on the host, an NPC hitting a guest's puppet or hit by a guest becomes a threat to every companion hostile to it (`TandemReactions.RallyCompanions`) |
| They follow the guest | D: a follower follows the host's V only, as its quest says; never the guest's |
| They revive a downed guest | – (vanilla companions revive nobody) |

## Healing and consumables

<!-- prettier-ignore -->
| What | State |
| --- | --- |
| The heal, its charges, its perks (Blood Pump, overshield) | W: the V's own game, vanilla |
| The inhaler, injector, food or drink in the puppet's hand | U: the item is in the V's left hand for the animation (`ConsumableUseEvents`), so `Held` puts it in the puppet's; D it appears when the puppet's copy has spawned, a moment late |
| The animation | U: the pose stream moves the puppet's arms |
| The heal's screen effect | – (first person only) |
| The heal's own status effect (its glow, its sound) | U: seen on the puppet, never applied to it (`StatusVisuals`, `presence-status`) |

## Counts

Each cell counted once, by the worst state it names (B, then D, then U,
then W; "W off / U on" counts as the default, W; "was B" is history),
2026-11-19:

<!-- prettier-ignore -->
| Table | Cells | W | D | B | U | – |
| --- | --- | --- | --- | --- | --- | --- |
| The Vs attacking (27 rows × 7) | 189 | 70 | 16 | 1 | 31 | 71 |
| The host's NPCs attacking (10 × 3) | 30 | 10 | 2 | 0 | 18 | 0 |
| Companions and allies | 5 | 1 | 1 | 0 | 2 | 1 |
| Healing and consumables | 5 | 1 | 1 | 0 | 2 | 1 |
| All | 229 | 82 | 20 | 1 | 53 | 73 |

Before this change three of today's U were B (NPC gunfire on a guest, NPC
grenades and every V's thrown object unseen on the other game) and the
"Seen on the other game" column had no U for throws at all.

## Throws

`scripts/tandem/Throws.reds`, `proto::Throw` and `proto::ThrowEnd`.

- **The thrower's game owns the object.** Its grenade, knife or launcher
  round is the real one (vanilla's flight, bounces, hits, explosion). When
  it leaves the hand (`BaseGrenade.OnShoot`, `MeleeProjectile.OnShoot` and
  `OnShootTarget`, `ProjectileLauncherRound.OnShoot` and `OnShootTarget`,
  our V the owner) it sends `Throw`: a number, the kind, the item (the
  grenade, the knife whose `projectileTemplateName` the copies spawn, the
  launcher's round), where it left and its velocity (the event's
  `startVelocity`, else measured over 50 ms), sticky or homing, and what a
  homing one flies at (a session NPC or a V). Its end (`Detonate`, a knife
  stuck in `OnCollision`, `TryToReleaseProjectile` when its owner picks it
  up, `Release`, `ExecuteGameEffect`) goes as `ThrowEnd`.
- **The others launch a copy from the thrower's puppet.** A grenade the
  AI's way (`AISubActionThrowItem.ThrowItem`): in the puppet's left hand
  (the one it shows from `Held`, else given), then a
  `gameprojectileSetUpAndLaunchEvent` with the thrower's origin and
  direction and a parabola of its speed and the grenade's gravity
  (`ParabolicTrajectoryParams.GetAccelVelParabolicParams`). A knife or a
  round the player's way: a `gameprojectileSpawnerLaunchEvent` on the
  puppet's knife or launcher, from the thrower's origin and direction. The
  copy's fuse is held (`m_detonationTimerActive` off); at `ThrowEnd` it is
  put where the real one ended and detonated, stuck or released there. No
  end in 15 s (its thrower gone): released quietly. A throw that waited more
  than 1 s in the queue (`combat::kThrowStaleUs`, a stall) is not launched.
- **A copy deals no damage**: its hits come from a remote V's puppet, and a
  puppet never deals damage on our game but through `AreaHit` (on our V) or
  a strike (the host, on its NPCs): `TandemOutcomes.IsRemoteComputed`, on
  every game now. What it does besides damage stays: its look and sound,
  the grenade marker on the HUD, smoke's vision blocker and the explosion's
  stimuli on the host, which are its NPCs' senses of a guest's grenade.
  Vanilla also sets the quest fact `player_threw_a_grenade` (a tutorial's)
  for a copy on the host.
- **A thrown knife is its owner's.** The real one lies in the owner's world
  and is picked up there as in solo (`Cool_Right_Perk_3_1` with it). A copy
  refuses every interaction (`OnInteractionActivationEvent`) and hides its
  prompt, and goes when the owner picks the real one up.
- **Smart rounds** (`ThrowKind::Smart`, State channel, one per shot): our V's
  smart weapon fired (`ShootEvents.OnEnter`) with a lock (the HUD's
  `UI_ActiveWeaponData.SmartGunParams`, the locked target first): the
  others' puppet fires a guided round at it as an NPC's smart gun does
  (`AIWeapon.Fire`: `smartGunIsProjectileGuided`, `trackedTargetComponent`).
- **The host's NPCs' grenades** (`kThrowByNpc`, the thrower in `npc`): the
  guests launch a copy from the NPC's proxy; its damage is the host's
  (`PlayerHit`), the copy's hits are a proxy's own (dropped,
  `IsProxyComputed`, on every target now).

## NPC melee at a guest

**Decision: the host's NPC decides to attack; the guest's game decides how
the hit lands, timed to its own screen.** Moving the whole melee attack to
the guest's game, as shots were (time.md), was weighed and not done:

- A shot is a projectile the guest's game can fire from the proxy on its
  own (`AI_ShootAt`); a melee attack is an animation-driven hit window on
  the NPC's own state machine, which a proxy (AI off, its animation the
  host's) does not run. The guest would have to rebuild the attack from the
  animation it is shown, and its timing from the network.
- What single player gives the target is block, parry and dodge **against
  the swing it sees**. The host's `PlayerHit` arrives one way-trip after the
  host's hit frame, while the proxy shows that frame an interpolation delay
  after it: the hit came ~100 ms **before** the swing on the guest's screen,
  so a parry timed to the swing was too late. Holding the hit for the
  difference (`combat::MeleeHoldUs`: the proxy's track delay minus the
  transit, at most 300 ms) lands it when the swing does. The target's whole
  side then runs on the guest's game as before: `ProcessDefensiveState`
  (block, parry with its timing window, Kerenzikov), armour, perks,
  dodge invulnerability.
- The attacker's side of a defended hit is the host's NPC's, so the guest
  tells the host (`Defended`): the host plays what vanilla plays on an
  attacker whose attack was blocked or parried (`myAttackBlocked` /
  `myAttackParried`, the `MyAttackBlocked` / `MyAttackParried` AI event,
  `BlockSignal`), so it recoils and staggers. Bullets blocked or parried
  with a blade report the same.
- What it leaves: whether the swing connects at all is still decided on the
  host against the puppet, which trails the guest by a way-trip and its own
  delay: a guest that dodged out of reach on its screen can still be hit
  (then its dodge invulnerability may still save it). A range check on the
  guest's side (the proxy to our V against the attack's reach) is the next
  step if it shows in play.

## Decisions

- **Friendly fire: off by default, `friendly_fire = on` in the host's
  `tandem.ini`.** Off, no V's hit reaches another V, explosions included
  (before this change explosions did, shots and blades did not). Decided
  by the user 2026-11-19: a host rule covering every hit, off by default.
- **Melee at a guest stays decided by the host, landed by the guest**
  (above).
- **Copies, not streams.** A thrown object's flight is not streamed: the
  copy flies the same arc on the same geometry and is corrected once, at its
  end. A grenade bouncing off something only one game has (a car that moved
  differently) lands elsewhere for a moment, then explodes at the real
  place.
- **Mines**: there are none for players in 2.50; placed mines and
  explosive devices are the host's devices (the devices work).

## Open

- Everything U above: the scenarios to run are `combat-throw-host`,
  `combat-throw-guest`, `combat-friendly-fire`, `combat-friendly-fire-off`,
  `combat-defend`, `combat-melee-guest`, then `npcs-shoot-guest` again (its
  hits were dropped) and the `outcomes-*` ones.
- Gorilla arms' one-punch slow motion and System Collapse's buffs on the
  host for a guest's hit (coop-perks §5.3, §6.2); Breach Protocol in game
  (`breach-guest`, `breach-host`; built, not run);
  smart-weapon NPCs firing guided rounds at a guest; NPC knives and
  launchers shown on guests; tracers for the Vs' shots.
