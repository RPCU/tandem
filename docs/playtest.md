# The first real two-player session

A checklist for the first session between two real games on two machines,
from a packaged release. Until now every feature was tested against the
mock player (`tandem-peer`) on one game: this is where two games meet.

Go in order and stop at the first thing that is wrong: write down what you
did and the time, make the reports ([Collecting the logs](#collecting-the-logs)),
then decide whether to go on. Expect two or three hours for the whole
list; the first three parts are the ones that must pass before anything
else is worth trying.

Roles below: **H** hosts, **G** joins. Both keep a voice call open and say
aloud what they see: half of the list is "does G see what H does".

## Before the day

On **both** machines:

- [ ] Cyberpunk 2077 patch 2.50 **with Phantom Liberty installed and
      enabled**, RED4ext, redscript, Codeware and TweakXL at the versions
      of [INSTALL.md](../dist/INSTALL.md); the game starts and loads a save
      without tandem.
- [ ] Write down the other mods installed. For the first session, as few
      as possible, and the same on both; gameplay mods off.
- [ ] A copy of the save folder
      (`Saved Games/CD Projekt Red/Cyberpunk 2077`).
- [ ] The **same** `tandem-<build>.zip`, checked against `SHA256SUMS`,
      extracted into the game's folder (`r6/scripts/tandem` and
      `r6/tweaks/tandem` deleted first if tandem was there before).
- [ ] The saves to use: H a save in the open world, after the prologue,
      with a car; G any save after the prologue. Both standing in the
      street, not in a scene, not in a vehicle, not in combat.
- [ ] `name = …` set in each `tandem.ini` (two different names).

Whoever built the release keeps `tandem-<build>-symbols.zip` at hand, and a
checkout of that commit: a crash needs both.

Decide how G reaches H, and test it **the day before**:

- [ ] same network or a VPN: H's address (`Co-op → Invite` shows it);
- [ ] or a port forward: UDP 27450 to H's machine, H's public address;
- [ ] or a relay: the same `relay = …` in both `tandem.ini`, a host token
      in H's ([relay.md](relay.md)); `curl …/healthz` answers on the
      relay's machine.

## 1. Alone, on each machine

Each player, without the other:

- [ ] The game starts, **no redscript popup**, the main menu has **Co-op**.
- [ ] `red4ext/logs/tandem-*.log` starts with the build and the versions
      found, and has **no** `startup check:` line:

      ```
      tandem 0.2.0+… loaded (protocol 7)
      game 3.0.88.46235 (built for patch 2.50, 3.0.88.46235)
      RED4ext: 1.31.0
      Codeware: 1.21.2
      TweakXL: 1.12.0
      ```

      Read both players' first line to each other: **the same build**.
      A line `Phantom Liberty: enabled` follows once the main menu is up.

- [ ] `tandem.ini` appeared next to `tandem.dll`.
- [ ] Load a save _without_ touching Co-op and play ten minutes as usual:
      a fight, a drive, a dialogue, a fast travel, a save and a reload.
      Nothing is different from the game without tandem (frame rate
      included). This is the "solo is passive" promise.
- [ ] `Co-op → Write a bug report` says where it wrote the zip; open it
      and look at what is inside. Is there anything you would not send?

## 2. Joining

- [ ] H: main menu → Co-op → Host a game → Continue. Once in game, the
      notice says "Hosting co-op" and the pause menu's Co-op panel says
      "Hosting on port 27450" (and a join code, with a relay).
- [ ] G: main menu → Co-op → Join a game → Enter a code or an address →
      their own save. The notice says "Joined H's session".
- [ ] Both panels list both names; G's shows a ping. Write it down.
- [ ] H sees a notice "G joined the session". Neither sees a notice about
      different builds or files. A notice about **other mods** is expected
      if your mod lists differ: note it, go on.
- [ ] G appears near H as a full V (dressed, their own face and hair),
      standing on the ground. H appears to G likewise. A marker shows the
      other player on the minimap and the map.
- [ ] Time of day and weather are H's on G's screen.

Then the ways it can go wrong, on purpose:

- [ ] G leaves (pause → Co-op → Leave session): H sees "G left", the
      puppet goes; G's game carries on alone. G joins again from the pause
      menu.
- [ ] G quits the game with Alt+F4 while in the session: H sees G leave
      within a few seconds. G restarts and joins: same V, same place for H.
- [ ] H leaves: G reads "Co-op session ended" and plays on alone; G's
      panel says it is trying again; H hosts again and G is back without
      doing anything.
- [ ] A wrong address, then a wrong join code: G reads why, nothing hangs.

## 3. Free roam, on foot

Stay within sight of each other. For each line: does the _other_ player
see it, at the right place, at the right moment, with its sound?

- [ ] Walk, run, sprint, crouch, jump, climb, fall from a height, slide.
      No sliding puppet, no popping, no frozen pose.
- [ ] Stand still facing each other for a minute: idle animations, head
      and eyes.
- [ ] Draw, holster and swap weapons (pistol, rifle, melee, fists); aim;
      reload. The right weapon in the hands.
- [ ] Shoot at a wall next to the other player: muzzle flash, sound,
      tracer, impact where it was aimed.
- [ ] Change an outfit piece and a whole outfit; the other sees it within
      a few seconds.
- [ ] Open the pause menu, the inventory, the map: the other is told the
      game is paused (or not, as designed), and nothing breaks on return.
- [ ] Walk apart, 200 m and more, round a block, and come back: the
      puppet is where the player is, at the first sight.
- [ ] Go up in a lift or into an interior together, and separately.

## 4. Vehicles

- [ ] H summons a car and drives; G watches from the street: the car
      moves smoothly, wheels turn, engine sound.
- [ ] G gets in as passenger; H drives five minutes, fast, with turns and
      a jump. G's camera does not shake, G does not fall through.
- [ ] The radio: H changes station, G hears the same one.
- [ ] Swap: G drives G's own car with H as passenger.
- [ ] Both drive their own car side by side; a light collision.
- [ ] A motorbike, each.
- [ ] Get out at speed (if you dare); get out normally; the car stays.

## 5. A fight in the open world

Pick a group of gang members on a street corner (not a quest).

- [ ] Both see the **same** enemies at the same places before the fight.
- [ ] H starts: enemies react on both screens, shoot at whoever they
      target, take cover at the same places.
- [ ] G shoots one: its health drops on H's screen, it dies for both, in
      the same spot; the body stays for both.
- [ ] Each player takes damage only from shots aimed at them.
- [ ] A melee hit, a grenade, a quickhack from each player.
- [ ] One player goes down: the other can revive; then the other way.
- [ ] Loot a body each: who gets what, and does the other's copy empty.
- [ ] Police: commit a crime, get stars, lose them. Both see the same.
- [ ] After the fight, walk away 300 m and come back: the bodies and the
      loot are as you left them, for both.

## 6. Time and the world

- [ ] Skip time (H, from the menu): G's clock follows.
- [ ] H uses a Sandevistan or a time-slowing ability near G, then G near
      H, if either has one. Who slows down, on whose screen.
- [ ] Fast travel: H travels; G gets the offer to follow, accepts; then G
      travels and H follows. Then one refuses.
- [ ] The metro, if both can reach a station.
- [ ] Sleep in an apartment, use a vendor, a ripperdoc, a drop point: the
      other sees a V busy, nothing odd.

## 7. The story, lightly

Only if everything above held. Pick something short on H's save: a gig or
an NCPD scanner hustle, not a main mission.

- [ ] G sees the objective appear in the journal and on the map.
- [ ] A phone call or a text to H: does G get it, as designed.
- [ ] A dialogue with choices: G sees the lines and H's choice.
- [ ] The reward: who gets the eddies, the XP, the item.
- [ ] A private scene is the last thing to try, not the first.

## 8. Endurance

- [ ] Stay in one session for 45 minutes of ordinary play. Watch for:
      memory or frame rate drifting down, puppets that stop updating,
      "Resyncing with the host…" notices coming back again and again.
- [ ] H saves, both quit, both start again, H loads that save and hosts, G
      joins: the world is where it was.

## What to watch for, all along

Say it aloud when it happens, with the time:

- **A notice at the top of the screen** from tandem (resync, a player
  gone, differences): read it out.
- **Something only one of you sees**: an enemy, a car, a body, a door
  open, the time of day.
- **A puppet that looks wrong**: sliding, T-pose, naked, invisible,
  half in the ground, facing the wrong way, a weapon floating.
- **Sound without picture, or picture without sound.**
- **Rubber-banding**: the other player or an enemy snapping back.
- **A freeze.** A game frozen at 0 % CPU has usually crashed. Do not
  close it at once: note the time, and if you have the tools,
  `tools/game.sh bt` first.
- **The pings** in the Co-op panel when something felt late.

## Collecting the logs

On **both** machines, as soon as possible after a problem, and once more at
the end of the session:

1. `Co-op → Write a bug report` (pause menu or main menu). If the game
   crashed: start it again and do it from the main menu (the report takes
   the previous run's log as well), or run
   `red4ext/plugins/tandem/tandem-report.exe`.
2. Take `red4ext/plugins/tandem/reports/tandem-report-<date>.zip`. Rename
   it with who you were: `…-host.zip`, `…-guest.zip`.
3. With it, each player writes: the time on their clock when it happened
   (the logs are in local time; say your time zone), what they did, what
   they saw, what the other saw.
4. A screenshot or a short clip from each side is worth more than a
   paragraph, above all for "we do not see the same thing".
5. A crash: also the game's own report folder if it made one
   (`%LOCALAPPDATA%\REDEngine\ReportQueue`), and say which build.

Both reports of one problem go together: a desync is only visible in the
difference between the two logs.

For whoever reads them: `versions.txt` first (same build? what differed?),
then `last-session.txt` (how it ended, resyncs), then both `tandem.log`
around the time given, side by side.
