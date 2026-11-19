# Down, revive, respawn

## Inside the plugin

- **Down, revive, respawn** (`scripts/tandem/Revive.reds`, milestone 8;
  the rules are under Open problems, "Down and out"). Each game owns its
  own V's life and sends it as `Vitals` (up, down, out, and whether it
  fights; reliable, stamped and kept by the host for late joiners). In a
  session with another player our V is Immortal through a god-mode
  override of our own, the way vanilla keeps a V with Second Heart alive
  (`SecondHeartStatListener`, `EnableOverride`), so its health stops at the
  floor and the player state machine enters its death state there
  (`DeathDecisions.EnterCondition` wrap, as for Second Heart at 1.1 %): V
  collapses with the game's own death animation, camera and sounds, its
  weapons away. Where vanilla picks resurrection or the death screen
  (`HighLevelTransition.SetIsResurrectionAllowedBasedOnState` wrap),
  Second Heart keeps its own rule (it revives at 20 % in a session, the
  vanilla multiplayer branch's amount); failing it, V goes down (or out),
  resurrection allowed but held (`DeathDecisionsWithResurrection.ToResurrect`
  wrap) and no death screen. While down it is invulnerable
  (`BaseStatusEffect.PlayerInvulnerable`), invisible to NPC senses (its
  visible object off, a dead body to their senses) and dropped from their
  trackers. Getting up is the game's Second Heart stand-up
  (`ResurrectEvents`), then `PlayerPuppet.OnResurrected` (wrapped) sets 40 %
  health, with 3 s of invulnerability. On the others' screens a downed
  puppet is outlined through walls (vanilla multiplayer's look,
  `ForceVisionAppearance`) with a notice; within 2 m the game's own
  interaction prompt (`UIInteractions.InteractionChoiceHub`, "[Hold F]
  Revive") appears; holding `Choice1` for 10 s with the HUD progress bar
  (`UI_HUDProgressBar`) sends `Revive` (start, cancel, done) to the downed
  V's game through the host, which drops it unless the target is down; the
  downed game shows "NAME is reviving you" on the same bar and accepts a
  done from a reviver within 4 m. Moving past 2.5 m, letting go, being hit
  or getting in a car cancels. Out: it gets up 1.5 m behind the nearest V
  that is up once that V has been out of combat for 3 s, or after 30 s.
  The host arbitrates the shared game over (`GameOver` when no V in the
  session is up; once per loss): the host's V dies the game's way (death
  screen, checkpoint reload); a guest's lies waiting and gets up beside the
  host's V once back in the host's session. **Leaving is no way out of
  being down** (decided 2026-11-18): leaving is never refused (a player can
  always kill the game), but a V that is down or out when it leaves (the
  menu's Leave, hosting or joining elsewhere) or when its session ends (the
  host quit or vanished, we were cut off) dies the game's normal way: death
  screen, the last save. With nobody else left in the session a downed V
  dies after 3 s (no V is up); after a lost session a guest gives the
  host's reload 120 s. No save is made while our V is down or out
  (`TandemSystem.IsSavingLocked`), so a game killed there reloads from
  before the fight like any death. A reviver that vanishes takes its revive
  with it (bar gone); a downed player that vanishes is removed with the
  usual "NAME left the session". **Up is one state** (fixed 2026-11-19,
  after a run left the screen dark): getting up (a revive, a respawn, the
  game's stand-up after Second Heart) clears in one place everything of
  being down, out or dying: the life sent to the others, the hold on the
  state machine, a pending death after leaving (`TandemSystem.LeftDown`,
  which also locks saving), the HUD bar, NPC senses, the death state's
  `dying` screen effect; 2 s later the state machine must be past its
  death state (`Vitals` 0), else its death effects are ended by hand. The
  leave-while-down death looks at the state at that moment and does
  nothing for a V that is up or being stood up; the god-mode floor stays
  on while V is being stood up, so a partner leaving then takes nothing
  from under it. Loopback test: down, out, up, the partner leaves (no
  loss), a partner joins (finds it up). `tools/game.sh cmd
revive down|hold|fresh|report` (`fresh` forgets the 5-minute cooldown of
  an earlier test and stands a downed V up); scenarios `revive-peer`,
  `revive-game`, `revive-out` (also: the mock player leaving an up V
  changes nothing), `revive-lost`. **Not yet verified in game.**

## Open problems

- **Down and out** (decided by the user, 2026-11-18; milestone 8). (1) A V
  that would die while another V is up (not down) goes **down** instead:
  on the ground in the game's own downed or dying look and sound, unable
  to fight; death prevention (Second Heart, Bloodswell, health floors)
  resolves first by its own rules. There is **no bleed-out**: a downed V
  waits as long as it takes. (2) Another player **revives** it through
  the game's interaction prompt on the downed V's puppet, held for **10 s**
  within **2 m**; moving away or being interrupted cancels; the revived V
  gets up with 40 % health. (3) **Cooldown, per player: 5 minutes.** A V
  that goes down again within 5 minutes of being revived cannot be
  revived: it is out. (4) A V that is out while another is up **respawns
  beside the living partner** once out of combat (or after a short timer
  if the fight drags on), with no death screen, no reload and no death
  penalty; until then it watches from where it lies. (5) The session is
  **lost only when every V is down or out at once**: then the normal death
  runs (the host's game shows the death screen and reloads its
  checkpoint; guests follow the host's reload). Authority: each game owns
  its V's life (`Vitals`), the reviver's game sends the revive request
  (`Revive`), the host arbitrates the loss (`GameOver`). Done (2026-11-18,
  not yet verified in game; see Inside the plugin): all five, with
  loopback tests (`tests/endpoint_test.cpp`) and scenarios. Choices made
  for the user's open points: 40 % on a revive or respawn; Second Heart at
  20 % in a session (vanilla's multiplayer branch, research/coop-perks.md
  §5.5); out also when killed outright (`BaseStatusEffect.ForceKill`);
  respawn 3 s after the partner leaves combat, or after 30 s out; the last
  V up leaving the session loses it for whoever is left down (host) or
  leaves the guest's V down until it is alone for 30 s. Open: the puppet
  of a downed V shows what the owner's third-person body does in its death
  state (pose stream: to check; a downed animation on the puppet is the
  fallback); a V downed in a car; the reviver's own kneeling animation;
  localized prompt texts; whether `BaseStatusEffect.SecondHeart` (applied
  by the stand-up we reuse) starts the Second Heart cooldown of a V that
  has the cyberware.
