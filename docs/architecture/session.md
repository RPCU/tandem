# Session and transport

**Handshake** (`core/src/endpoint.cpp`): `Hello` (magic, version, name) →
`Welcome` (your player id, host id, host clock) or `Refused`; the host then
broadcasts the `Roster`. A guest's messages count only after its `Welcome`,
and the host stamps every relayed `PlayerState` with the sender's own id: a
guest cannot speak for someone else.

**Liveness** (`core/src/session.cpp`, `Timing` in `endpoint.hpp`): a
connection that acknowledges nothing for **5 s** is dead, on either side.
ENet pings an idle peer every 0.5 s, so a live one is heard from several
times a second and a hiccup of 1–2 s passes. `Session::Poll` checks it
itself (time since the peer's last acknowledgement) because ENet's own
timeout only fires when a backed-off resend is due (5 to 30 s); ENet's
stays as a backstop at 10 s. A host that never answers a connect is given
up after the same 5 s ("could not reach the host"). After a stall of our
own (`Poll` not called for a while) peers get a moment to be heard before
they are judged. A goodbye (`Refused`, `Bye`) is delivered before the
disconnect that follows it, and a guest's `Bye` removes it at once.

**Slots and returning players**: the host takes up to `kMaxGuests` (3)
welcomed guests, 4 players in all (`kMaxPlayers`), and refuses the next
with "the session is full (4 players at most)" (`kSessionFull`, shown as
the end notice), but keeps
`kSpareConnections` (2) more ENet connections open, so a returning player
can always reach it. A `Hello` under the name of a player whose connection
has been silent for **2 s** replaces that connection at once: the player
keeps its id, so the others keep its puppet and seat (no `PlayerLeft`, no
`PlayerJoined`; the host sees `PlayerReturned`), and the new game resends
its look, held items and seat. A live player (heard within 2 s) cannot be
replaced: a namesake joins as another player, shown as "a (2)". A returning
player is matched by the name it asked for (`Hello::name`, sanitized), not
the one the session shows it under. A guest numbers its states
from the host clock's millisecond at its `Welcome`, so what it sends after
coming back is newer than what it sent before.

**Names** (`core/include/tandem/names.hpp`, [`presence.md`](presence.md),
Names): the host sanitizes the name each `Hello` brings (controls and
comment signs dropped, 16 characters at most, nothing left is `V`) and
makes it unique: a name another player has (ASCII case aside) gets " (2)",
" (3)", the first free number. The `Roster` carries the shown names; a
guest that sees its own differ from what it asked for tells its player
(`events::NamedAs`). No protocol change: the fields keep their meaning.
Tests: "names are unique in a session…", "a returning player is matched by
the name it asked for…" (`tests/endpoint_test.cpp`); scenario
`names-duplicate`.

**Player ids** are 1 (the host) to 8 (`proto::kMaxPlayerId`): the host
hands guests the next free id after the last one it gave, 2..8 in turn
(`Endpoint::NextFreePlayer`), so an id that just left is not handed out
again at once (a guest's game is still taking its puppet down) and every
id is one bit of a byte (`NpcEntry::detected`, the stealth masks) however
many players came and went. Everything kept per player is dropped when it
leaves, on the host (`RemovePlayer`), on guests (the roster) and in the
plugin (`Runtime::ForgetRemote`), so a recycled id starts afresh. Tests:
"four players: …", "player ids are handed out again, …"
(`tests/endpoint_test.cpp`); scenario `three-players` (two mock players
and the game).

**One session after another** (a guest dropped by its host that joins the
next one without a reload, `TandemSystem`'s session-ended event): what the
old session named by its numbers goes when it ends, as a reused player or
NPC id is someone else's (2026-11-21, not yet run in game). The plugin
drops its combat book (vendors, car lends, the host's rules:
`Combat::EndSession`), the host's clock and weather, the player names and
the scenes' NPC poses and rig tracks (`Runtime::ResetSession`), and says
our seat and ride to the next session only when they name no other
player's car (`consistency::CarriesOver`). The scripts make the parked car
copies nobody's (`ForgetCarOwners`: a new player 3 does not inherit the old
one's car, its damage or its hits' blame) and forget the important items
still on their way (`Loot.reds`). Test: "a new session gets our seat and
ride again only when they name no other player"
(`tests/consistency_test.cpp`).

**Reaching each other**: direct first (LAN, Tailscale/NetBird — already on
every nixbook machine), then the relay when NAT blocks UDP. A host with a
relay configured is reachable both ways at once: directly on its port, and
through the relay by its **join code** (see [The relay](relay.md)). A guest
joining by code goes through the relay; given the host's address too, it
tries that first and switches to the relay when the host has not answered
within half the timeout (2.5 s), without reporting an end in between
(`JoinTarget` in `endpoint.hpp`). Direct by hand: the co-op menu's Join by
address (`coop-menu.md`), as `role = join` with `address` would, its last
address kept as `last_address`. IPv4 only (ENet): an IPv6 address is
refused in words (`hosts::Check`, `tests/hosts_test.cpp`).

## Inside the plugin

- **Solo is vanilla** (decided by the user, 2026-11-19). Installed with no
  session (the default `role = off`, or after the co-op menu's Leave),
  tandem does nothing to the game: no socket, no immortality or god mode,
  no crowd, attitude, quest freeze, save lock, time or pause change, no
  puppets, nothing on the HUD but the co-op menu's entry. What is
  installed stays idle: the engine hooks pass through (the idle
  controller's finds no puppet, the pose stream reads only with a source,
  the quest node hook only notes which scene runs, its freeze off; under
  Proton the transform guard skips only an engine call that would crash,
  `engine/re.md` §13.98), the
  running-state frame call needs a session, and every script wrap checks
  a session before it departs from vanilla (audited 2026-11-19, every wrap
  and system handler; fixed then: the fast travel's epoch and map hint,
  the reward ledger and the story's listeners after a Leave, and a V's
  own limbs). The startup line says so: `tandem: solo, passive (hooks
idle: …); recorder on|off; transform guard on|off (why)`. `TandemSystem`'s idle tick (every 0.25 s)
  only reads `tandem.cmd` (developer commands: `where`, `teleport`,
  `god`, `coop …`, `recorder …`). Starting a session (the co-op menu, `coop-menu.md`, or
  `role = host|join`) turns co-op on; leaving gives everything back.
  Scenario `solo`.
- Configuration: `tandem.ini` next to the DLL, read at startup; the
  default is solo (`role = off`). Every key with its default is in
  `dist/tandem.ini` (shipped as `tandem.default.ini`) and
  `dist/INSTALL.md`; each is explained in the topic file it belongs to:
  role, address, port, name, `last_address` here; `relay`, `relay_token`,
  `join`, `code`, `direct` (`relay.md`); `level_gap` (`progression.md`); pose (`players.md`); story,
  quest_freeze, quest_freeze_allow, `quest_freeze_gigs`, `quest_interact`
  (`story.md`); scene_privacy, private_actors, private_scenes,
  public_scenes, `scene_replay` (`scene-replay.md`); time_host
  (`time.md`); `friendly_fire` (the host's session rule,
  `combat-coverage.md`); `enemy_scaling`, `enemy_scaling_health` (the
  host's too, `npcs.md`); `link`, `pings` (`comms.md`); `notices`
  (`coop-menu.md`), `markers` (`players.md`), `nameplates`, `party_hud`
  (`presence.md`); `death_ledger`, `death_bodies` (`regions.md` §14), `overlap`,
  `overlap_despawn` (`regions.md` §6);
  `leave`, `checkpoint` (`progression.md`); `vehicle_rewards`, `lend` (`garage.md`);
  `trade` (`trade.md`); `native_prompts` (`prompts.md`); `race_car` (`activities.md`, "Races"); `recorder`, `recorder_max_mb`,
  `recorder_segment_minutes`, `recorder_pose` (`recordings.md`);
  `log = warn|info|debug` (`netcode.md`); `compat` (`release.md`). The
  co-op menu's Settings page changes many of them (`coop-menu.md`).
  Releases, startup and join-time compatibility checks, bug reports:
  `release.md`.
