# Stealth, takedowns and reactions

## Inside the plugin

- **Takedowns, finishers, carried bodies** (`scripts/tandem/Takedowns.reds`,
  not yet run in game): the actor's game plays its V's side at once on its
  proxy (the takedown state machine, the finisher, the carry) and sends
  `NpcAction` (grab, release, takedown, knock-out, other takedown
  animation, finisher, pick up, drop, throw, dispose) to the host. Its
  requests are caught where vanilla runs the NPC's side:
  `TakedownGameEffectHelper.FillTakedownData`, the grapple's break-free and
  release, `FinisherAttackEvents.PlayFinisherGameEffect`, the body's
  `CarriedObject` states and `DisposalDevice`. The host validates (the NPC
  is there, alive for a grab or takedown, a body for a pick up, nobody else
  holds it, within 6 m, the finisher's health no higher than the guest saw
  or its guard broken), answers the asker alone (`NpcActionReply`) and plays
  the NPC's side on the real one with the guest's puppet as activator: the
  same `takedowns` or `playFinisher` game effect, the mount on the
  `leftShoulder` slot with the carried pose, the throw's kill, ragdoll and
  impulse, the disposal's kill and hide; the kill or knock-out is applied
  4–5 s later if the effect did not do it, credited to the puppet. Who holds
  what goes to everyone (`NpcHold`, kept for late joiners, released when
  the holder leaves), so each game puts its proxy in the same grapple or on
  the same shoulder of that V, holds the proxy's movement controller
  meanwhile (`TandemNative.HoldPuppet`) and switches its takedown prompts
  off. A refused grab breaks free on the guest, vanilla's end for a failed
  grapple; a refused pick up is a forced drop. A disposed body is gone for
  everyone (`NpcGone` reason 2: proxies deleted). A guest's proxy killed by
  the host during its own takedown dies once its animation is over.
- **"The player" reactions on the host** (`scripts/tandem/Reactions.reds`):
  NPC code that reacts to "the player" uses whoever acted. An NPC entering
  combat with a guest's puppet gives the host's V no second chance or gag;
  a guard a guest broke offers the host's V no finisher; both go to that
  guest's game as `NpcCue`, which runs them on its own V with its proxy (the
  second chance's memory wipe comes back as an `NpcEffect`; the proxy's
  missing senses are replaced by the host's detection bit). A guest's
  Bait/Whistle walks the NPC to that guest's puppet. TODO list in
  `research/engine-knowledge.md` §13.17.

## Open problems

- **Each player is detected on their own.** Stealth, detection and
  combat are per player, never shared: a host NPC can hunt one V while
  another sneaks past it unseen, each player with their own stealth
  meter, detection, "in combat" state and combat music. The host's game
  measures each NPC's detection of every V (its own, and each guest's
  puppet: `SenseComponent.GetDetection`, the NPC's threats) and sends it
  per player: `NpcEntry::detected` has a bit per player for "has detected
  them" (full detection meter, or in combat with them as a hostile threat:
  `NPCPuppet.IsInCombatWithTarget`), and `NpcAwareness` (5 Hz) the level
  of each NPC's meter for each guest it is noticing. A guest's game drives
  its own V from that (`scripts/tandem/Stealth.reds`): while any host NPC
  has detected it, its V is "tracked as hostile" through the game's own
  switch (the player combat controller's `OnStartedBeingTrackedAsHostile`,
  the call a local NPC's target tracker makes), so the PSM combat state
  and everything on it follow: combat music and HUD, the fast travel lock,
  stealth bonuses (`AttackData.IsPlayerInCombat`), combat-only perks; it
  leaves combat the same way when no host NPC has it any more (a local
  tracker, if any, keeps it in). The awareness levels drive the stealth
  meter: `UI_Stealth.highestDetectionOnPlayer` (what the detection perks
  listen to), each proxy's detection percentage, and the proxy's sense
  mappin (the indicator over an NPC) while it notices us. What host NPCs
  know of another V never counts. Verified 2026-11-18 (scenario
  `stealth-guest`: in and out of combat, the meter's log); whether the
  native meter draws a proxy's level is open. Open: player ids grow
  within a session (players coming back), but `detected` has bits for
  players 1–8 only, so a guest numbered 9 or more is never detected.
  The other way, **a guest's stealth reaches the host's NPCs**: every game
  sends its V's optical camo (the camo status effect, the relic's fully
  blocking state), its `Visibility` stat and whether it crouches as
  `Stealth` (reliable, kept for late joiners); the host puts it on the
  puppet where its NPC senses look (camo: the grafted visible object off,
  and the camo effect itself, which every game also shows; the visibility
  as the detection multiplier; crouch height for the visible point; fully
  blocking: its NPCs drop the puppet as a threat). A guest's noises (every
  stimulus its V broadcasts: gunshots, silenced shots…) go to the host as
  `Noise` and are broadcast again from its puppet. Grenade and other
  projectile explosions are broadcast by the projectile, not the V: still
  open. Not yet verified in game (scenario `stealth-host`). Each puppet
  has an attitude group of its own (`tandem_player_N`, Attitudes.reds),
  because in `player` hostility to one V becomes hostility to all: so
  hostility to one guest's V reaches neither the host's V nor another
  guest's.
