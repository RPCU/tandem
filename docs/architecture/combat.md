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
  `tools/game.sh cmd puppet limbs`). So the real NPC plays its own hit
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
  explosion) is dropped (the host's NPC attacks, as `PlayerHit`). Our V's
  explosion or pressure wave on another V's puppet (on any game) runs the
  attacker's side only and goes as `AreaHit`, through the host, to that
  V's game, which runs it on its V from our puppet by its own rules.
  **Difficulty**: the host sends its `gameDifficulty` (`Difficulty`, kept
  for late joiners); a guest switches to it
  (`StatsDataSystem.SetDifficulty`) and back when the session ends. NPC
  damage is scaled on the host (the attacker's side); the target-side
  stages that read the difficulty on a guest (one-shot protection,
  explosions' fixed share, grenades) then read the host's; `PlayerHit`
  also carries the attack's `IgnoreDifficulty` and `CannotKillPlayer`
  flags. Not yet verified in game (scenarios `outcomes-knockout`,
  `outcomes-blade`, `outcomes-poise`, `outcomes-guest`). Open: a guest's
  own prediction of the kill for on-kill perks (coop-perks.md §2.3), the
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
  hit reaction, direction indicator and sounds. Not yet verified in game
  (scenario `npcs-hurt-guest`). A hostile NPC a guest hits turns on that
  guest's puppet (`TargetTrackingExtension.InjectThreat`,
  `scripts/tandem/Reactions.reds`). Open: aggro of NPCs not yet hostile to
  them, loot, the proxy's own weapon (armour needs an attacking weapon
  object: the proxy's if it holds one, else the guest's own stands in),
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
  in game (scenarios `hacks-upload`, `hacks-guest`, `hacks-car`). Open:
  Breach Protocol (`Breach{access point, programs}` to the host, which
  runs the daemons with the puppet as executor and sets the breached flag;
  the loot stays the guest's), an upload of the same hack by another
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
