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
replaced: a namesake joins as another player. A guest numbers its states
from the host clock's millisecond at its `Welcome`, so what it sends after
coming back is newer than what it sent before.

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

**Reaching each other**: direct first (LAN, Tailscale/NetBird — already on
every nixbook machine), then the relay when NAT blocks UDP. A host with a
relay configured is reachable both ways at once: directly on its port, and
through the relay by its **join code** (see [The relay](relay.md)). A guest
joining by code goes through the relay; given the host's address too, it
tries that first and switches to the relay when the host has not answered
within half the timeout (2.5 s), without reporting an end in between
(`JoinTarget` in `endpoint.hpp`).

## Inside the plugin

- **Solo is vanilla** (decided by the user, 2026-11-19). Installed with no
  session (the default `role = off`, or after the co-op menu's Leave),
  tandem does nothing to the game: no socket, no immortality or god mode,
  no crowd, attitude, quest freeze, save lock, time or pause change, no
  puppets, nothing on the HUD but the co-op menu's entry. What is
  installed stays idle: the engine hooks pass through (the idle
  controller's finds no puppet, the pose stream reads only with a source,
  the quest node hook only notes which scene runs, its freeze off), the
  running-state frame call needs a session, and every script wrap checks
  a session before it departs from vanilla (audited 2026-11-19, every wrap
  and system handler; fixed then: the fast travel's epoch and map hint,
  the reward ledger and the story's listeners after a Leave, and a V's
  own limbs). The startup line says so: `tandem: solo, passive (hooks
idle: …); recorder on|off`. `TandemSystem`'s idle tick (every 0.25 s)
  only reads `tandem.cmd` (developer commands: `where`, `teleport`,
  `god`, `coop …`, `recorder …`). Starting a session (the co-op menu, `coop-menu.md`, or
  `role = host|join`) turns co-op on; leaving gives everything back.
  Scenario `solo`.
- Configuration: `tandem.ini` next to the DLL (role, address, port, name,
  pose, story, quest_freeze, quest_freeze_allow, scene_privacy,
  private_actors, private_scenes, public_scenes, time_host; `relay`,
  `relay_token`, `join` for the relay and join codes; `recorder`,
  `recorder_max_mb`, `recorder_segment_minutes`, `recorder_pose`, see
  `recordings.md`): the defaults, and the test harness's session. The
  default is solo.
