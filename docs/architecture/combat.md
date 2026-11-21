# Fights and quickhacks

## Inside the plugin

- **Fights** (`scripts/tandem/Combat.reds`, milestone 5): a guest's hit on
  a proxy runs the game's damage pipeline as usual (aim, weapon, perks,
  the proxy's armour) up to `DamageSystem.DealDamages`, where the damage
  is computed without being applied (`StatPoolsManager.ApplyDamage` with
  `forReal` false) and sent as `NpcHit`. Instant kills
  (`ProcessInstantKill`, the `Kill` flag) go as lethal hits. The hit
  marker and damage numbers still show at once. The host lowers the real
  NPC's health (`RequestChangingStatPoolValue`, the guest's puppet as
  instigator) or kills it, and sends its health (`NpcHealth`, on every 1 %
  change, kept for late joiners) and its death (`NpcGone` reason 1). The
  guests set the proxy's health and `Kill` it, so it dies the game's way;
  the corpse stays until the host's goes. Verified 2026-11-18 (scenarios
  `npcs-hit`, `npcs-punch`): 40 physical damage became 9.7 after the
  proxy's armour; three hits of 35 killed an NPC on the host.
  **Game rules hold for every hit**: the attacker's game applies the
  attacker's side (weapon, perks, crits, weak spots, its own status
  effect chances), the target owner's game the target's side, and the
  killer's game gets the kill (on-kill perks, XP). Status effects a
  guest's hit applies to a proxy go to the host (`NpcEffect`), which puts
  them on the real NPC for their behaviour (stun, panic, blind, burning);
  their damage ticks stay the guest's (they reach the host as hits), and
  the host's own ticks for them deal nothing. Stealth bonuses depend on
  the attacker's combat state, so they follow per-player detection
  (`stealth.md`). The guest also queues the skill XP of each hit on a proxy, as
  the game does when it drains health (`RPGManager.AwardExperienceFromDamage`
  with the share of the proxy's health pool the hit takes, which mirrors the
  host's); the queue is paid when the proxy dies: by the game's own
  neutralize event when the kill is ours (`Kill` with our V), by a
  `ProcessQueuedCombatExperience` request when another player landed it
  (assists pay too).
  **A hit carries its whole outcome** (`scripts/tandem/Outcomes.reds`,
  `core/src/combat.cpp`): the guest's measured hit on a proxy waits for the
  end of its pipeline (`DamageSystem.PostProcess` drains the proxy's poise
  for player attackers) and goes as `NpcStrike` (it supersedes `NpcHit`,
  which the host still takes): health and poise lost, the attack's
  minimum health (`GetMinimumHealthPercent`), flags (weak spot, critical,
  melee, instant kill, non-lethal, "leave 1 HP" when the proxy's game
  capped it for finisher grace or the bleeding perk, killing blow,
  headshot, forced dismemberment), the attack type and record, and the hit
  shape it landed on (reaction zone, dismemberable part, flesh/metal). The
  host turns it into a hit of the guest's puppet on the real NPC (the
  puppet holds the guest's weapon) with a hit shape built from those
  fields, queued in its damage system, which skips the damage-computing
  stages for it (`ConvertDPSToHitDamage`, `ModifyHitData`, `PostProcess`);
  `Process` applies the guest's numbers instead (`StrikeLoss`: instant
  kill, 1 HP left of the host's health, the minimum health, 1 % for an
  immortal; the poise as vanilla's `PostProcess` does) and sends the
  game's `gameDamageReceivedEvent`. The host's invulnerability and death
  checks still run. A lethal strike kills the NPC (`Kill`, the guest's
  puppet as killer: the health pool alone stops at the defeat mechanic's
  0.1 %, and the strike's hit reaction did not finish it off, 2026-11-19),
  and its limb is cut by the hit reaction's own rules (a forced
  dismemberment, an explosion, or a blade's melee kill on the part it
  landed on; clean for blades). **A V keeps its limbs**: vanilla's V has
  no hit reaction or dismemberment component, but a puppet is an NPC body
  with both and no record field or stat to turn them off, so on every
  game a V (ours, a puppet) refuses the hit reaction's wounds and
  dismemberment, every dismemberment request and event (explosion, guts)
  and the crippled and dismembered status effects
  (`TandemOutcomes.KeepsLimbs`; scenario `outcomes-limbs`,
  `tools/game.sh cmd puppet limbs`). **The Relic tree**
  (`research/coop-perks.md` §4, Relic tree) runs on its owner's game
  through these same paths, with these additions: the marks a guest's arm
  attacks put on a proxy with no instigator (the Mantis leap's
  dismemberment, the Gorilla Arms' and grand finale's marks) are forwarded
  like its hit's other effects and put on the host's NPC even dead (they
  act on the dead); the host keeps the one-punch slow motion of a guest's
  mark off its own V and a guest's instant dismemberment's blood off its
  screen; an instant dismemberment never takes a V; scenario `relic-guest`,
  `tools/game.sh cmd relic mark`. So the real NPC plays its own hit
  reaction by body part, weak spot and strength (frame damage from the
  puppet's weapon's factors), its wounds and dismemberment; its damage
  history names the guest's puppet (kill credit) and holds the non-lethal
  flag, so the defeat mechanic knocks it out
  (`BaseStatusEffect.Unconscious`) instead of killing it. Back to the
  guests: poise like health (`NpcPoise`, on 5 % steps: the proxy's guard is
  the host's; the finisher a broken guard opens is offered by the host's
  `PoiseBroken` cue to whoever broke it, Reactions.reds), each
  dismemberment the host's NPC gets (`DismembermentComponent
.RequestDismemberment`, wrapped: `NpcWound`, cut the same on every proxy;
  a proxy's own are suppressed), and a knock-out (`NpcGone` reason 3: the
  proxy is made unconscious and ragdolls, still a target; its XP queue is
  paid; a takedown still playing on it lays it down itself).
  **One game computes each hit**: an area effect (explosion,
  grenade, ground slam, chain lightning) runs on the attacker's game, each
  proxy's share sent as a strike; on the host, any hit whose instigator is
  a guest's puppet or a guest's car copy and is not a strike (the guest's
  effects, explosions, collisions, computed there already) is dropped
  before its pipeline; on a guest, a proxy's own hit on our V (a death
  explosion) is dropped (the host's NPC attacks, as `PlayerHit`); on every
  game, a remote V's puppet deals no damage but through a strike or an
  `AreaHit` (`TandemOutcomes.IsRemoteComputed`: the copies of its thrown
  objects too), and on a guest a proxy deals none but the shots the host
  asked it to fire at our V (`IsProxyComputed`, `TandemShots.IsSimulatedHit`;
  until 2026-11-19 those were dropped too). Our V's
  explosion or pressure wave on another V's puppet (on any game) runs the
  attacker's side only and goes as `AreaHit`, through the host, to that
  V's game, which runs it on its V from our puppet by its own rules; so do
  its shots and blades, all of it only while the session's rules allow
  friendly fire (`Rules`, the host's `friendly_fire`, off by default; `Rules`
  also carries enemy scaling, `npcs.md`:
  combat-coverage.md). It is one rule for every hit and every game: the
  host's settings page changes it live, a guest's shows it and cannot (the
  per-player setting the `polish` branch had, decided by the target's own
  game, is gone). Every way of fighting, by attacker and target, with
  its state: [combat-coverage.md](combat-coverage.md); thrown objects
  (`Throw`, `ThrowEnd`) are there too.
  **Difficulty**: the host sends its `gameDifficulty` (`Difficulty`, kept
  for late joiners); a guest switches to it
  (`StatsDataSystem.SetDifficulty`) and back when the session ends. NPC
  damage is scaled on the host (the attacker's side); the target-side
  stages that read the difficulty on a guest (one-shot protection,
  explosions' fixed share, grenades) then read the host's; `PlayerHit`
  also carries the attack's `IgnoreDifficulty` and `CannotKillPlayer`
  flags. Not yet verified in game (scenarios `outcomes-knockout`,
  `outcomes-blade`, `outcomes-poise`, `outcomes-guest`). A guest's own
  prediction of the kill (on-kill perks, coop-perks.md §2.3) is never made
  on an NPC its owner cannot kill now, and undone when no death follows
  (npcs.md, "A kill seen first"). Open: the
  finisher offered to the host's V for a guard a guest broke
  (`NPCPoiseListener` reads the local player), NPCs beyond the guest's 60 m
  out of its area effects, a guest's save made at the host's difficulty.
  **NPCs hitting guests**: on the host, a hit of one of its NPCs (or a
  device, a car) on a guest's puppet runs the attacker's side only: the
  target's stages are skipped for puppets (`ProcessDamageReduction`, dodge,
  evasion, mitigation, the player incoming multiplier,
  `CalculateTargetModifiers` with armour, `ProcessBlockAndDeflect`). At
  `DealDamages` what is left goes as `PlayerHit` (the NPC's session id, the
  damage per type, attack type, flags, armour penetration, where and which
  way, the attack and weapon records) to that guest alone; instant kills
  too. Damage ticks of effects on the puppet are not sent (the guest runs
  its own copy of the effect). The guest builds a hit on its own V from
  the NPC's proxy (instigator and source; none when it has no proxy) with
  the host's numbers and queues it in its damage system, which skips the
  attacker's stages (`ConvertDPSToHitDamage`, `CalculateSourceModifiers`,
  `ProcessEffectiveRange`; armour penetration is the host's) and runs the
  rest by its own rules: armour, resistances, dodge, block and deflect,
  perks, difficulty, god mode, death, the attack record's status effects,
  hit reaction, direction indicator and sounds. A melee hit waits until the
  NPC's proxy is drawn landing it (`TandemFight.MeleeHold`,
  `combat::MeleeHoldUs`), so a block or parry is timed to the swing on the
  guest's screen; one blocked or parried there makes the host's NPC recoil
  (`Defended`). **Never more than the host computed**: a forwarded hit
  (`PlayerHit`, `AreaHit`) takes at most the damage its sender sent, per
  type (1 % margin, the 1 point vanilla leaves of each type), at
  `DealDamages` (`TandemCombat.CapForwarded`): our rules may lower it and
  raise it only where vanilla means to for a V as target (a known record's
  `PlayerIncomingDamageMultiplier`, the police's, an explosion's or
  grenade's fixed share of V's health); with no weapon on either side the
  one-shot cap our game skips is applied there. A record our game lacks (a
  mod's; none from the mock player) is stood in by `Attacks.ForwardPush`
  (melee) or `Attacks.REMOVE_BulletWithDamage` (`TandemCombat.AttackRecord`,
  logged once a session per record), whose own multiplier for a V is not
  applied: a 10-physical melee hit through the shove took 392.96. Not yet
  run in game
  (scenarios `npcs-hurt-guest`, `combat-melee-guest`: one hit, in health
  points, at most the 10 sent; `combat-defend`). A hostile NPC a guest hits turns on that
  guest's puppet (`TargetTrackingExtension.InjectThreat`,
  `scripts/tandem/Reactions.reds`). Open: aggro of NPCs not yet hostile to
  them, loot, the proxy's own weapon (armour needs an attacking weapon
  object: the proxy's if it holds one, else the guest's own stands in; with
  neither, no armour), a stand-in melee record cannot be blocked (only an
  `Attack_Melee` record can, `ProcessDefensiveState`),
  status effects on the host's puppet (they stay there, unsynced).
- **Quickhacks** (`scripts/tandem/Hacks.reds`, milestone 5): a guest's
  quickhack on a proxy runs in its own game as in solo (panel, RAM, its
  cooldown and queue, upload bar, the effect on the proxy, whose effects
  reach the host as `NpcEffect` like a hit's). When its upload starts
  (`ScriptedPuppet.OnUploadProgressStateChanged`, `STARTED`) it sends
  `NpcHack` (action record, NPC, the upload time its perks give, whether
  it is traceable); the host keeps the real NPC being hacked for that long
  (`AIQuickHackStatusEffect.BeingHacked`, `…WithTraceableHack`), uploads
  from several players at once, ended in arrival order. A guest's
  non-stealth quickhack effect makes the host's NPC do what the game does
  for the host's V (`NonStealthQuickHackVictimEvent`): alert
  (`NPCStatesComponent.AlertPuppet`), turn on the guest's puppet
  (`AIActionHelper.TryChangingAttitudeToHostile`) and trace that V
  (`PlayerHack` with `AIQuickHack.HackRevealPosition`). The host sends
  everyone every upload on its NPCs (`NpcHackState`, the host's V's too)
  and every `Quickhack`-tagged effect that comes and goes (`NpcStatus`);
  guests put them on their proxies, so the proxy looks hacked and the
  menu's "already affected" lockouts (the action's target-active prereqs,
  checked on the proxy) are the host's. **NPC netrunners**: a host NPC's
  hack on a puppet (`ScriptedPuppet.OnHackTargetEvent`) is not run there
  but sent to that guest (`PlayerHack`), whose game runs it on its own V
  through the same event, with its proxy as the netrunner: its ICE, the
  upload bar, the effect; an ICE counterhack lands on the proxy and goes
  back as an effect. **Spreading** runs on the caster's game only, over
  its proxies (the vanilla spread takes the local player's stats, spread
  map and executor: on the host it would be the host's V's); each copy it
  lands is forwarded like any effect, and the host never runs a guest's
  completion effects, so it does not spread them again. **Cars**: the
  brake, accelerate and explode hacks on the copy of a car another V
  drives (`VehicleComponentPS.OnVehicleOverride…`) are not applied to the
  copy but sent (`VehicleHack`, through the host) to the driver's game,
  which applies them to its car (`ForceBrakesUntilStoppedOrFor`,
  `ActivateNetrunnerQuickhack`); taking control of such a car is refused
  for now (it needs the car handed over to the hacker). Not yet verified
  in game (scenarios `hacks-upload`, `hacks-guest`, `hacks-car`).
  **Breach Protocol** (`scripts/tandem/Breach.reds`, devices.md "Breach
  Protocol"; built as planned, `Breach{access point, programs}` to the
  host, not yet run in game): the minigame is the breacher's, with its
  rewards (the loot stays the guest's, paid once the host grants the
  breach); its outcome (`AccessPointMiniGameStatus`
  and the `HackingMinigame` blackboard, so a mod that skips the minigame is
  covered) goes to the host, which breaches the network once for everyone
  and refuses a second breach; the host runs the daemons on the network's
  devices and its NPCs with the breacher's puppet as executor
  (`ProcessMinigameNetworkActions`, `ScriptedPuppetPS.OnSetExposeQuickHacks`,
  the game's own program actions), every other game on the NPCs of it that
  it runs, and the Breach Protocol quickhack on an NPC goes to that NPC's
  owner (`Breach` with its NPC). The daemons' effects come back as
  `NpcStatus` (Hacks.reds forwards them with the quickhack ones), so
  proxies look weakened and linked partners see them tinted (comms.md).
  Open: an upload of the same hack by another
  player shown as locked in the menu while it runs, the host NPC
  netrunner's own AI not told how its forwarded hack ended, take control.

## Open problems

- **Quickhacks from two players on one target.** Both Vs can hack the same
  NPC, device, camera, turret or car at once, as two netrunners would:
  uploads in progress at the same time, effects stacking or not by the
  game's rules (two Short Circuits damage twice, two Overheats refresh one
  burn, Reboot Optics while blinded), the target's queue of uploads and its
  per-target cooldowns (a quickhack already running on it, the "already
  affected" lockouts), RAM costs paid by each V on their own, and the
  target's trace (an NPC tracing back to one netrunner, not to both). The
  owner of the target (host for NPCs and devices, the driver for a car)
  applies the uploads in the order they land and tells both players what
  is running on it, so each one's scanner shows the true state.
  Done (2026-11-18, not yet verified in game): what Inside the plugin
  lists under **Quickhacks**.
  A V can also quickhack the car another V is driving (vehicle quickhacks:
  forced acceleration, sudden brake, loss of control, overheat). The
  driver's game owns that car, so the hack is a request to that game: the
  driver feels it exactly as from an NPC netrunner (the car fights their
  input), and every other game sees the result through the car's usual
  sync.
