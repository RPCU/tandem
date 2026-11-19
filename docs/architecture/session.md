# Session and transport

**Handshake** (`core/src/endpoint.cpp`): `Hello` (magic, version, name) →
`Welcome` (your player id, host id, host clock) or `Refused`. The host then
broadcasts the `Roster`. A guest's messages count only after its `Welcome`,
and the host stamps every relayed `PlayerState` with the sender's own id: a
guest cannot speak for someone else.

**Liveness** (`core/src/session.cpp`, `Timing` in `endpoint.hpp`): a
connection that acknowledges nothing for **5 s** is dead, on either side:
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
welcomed guests and refuses the next ("the session is full"), but keeps
`kSpareConnections` (2) more ENet connections open, so a player coming back
can always reach it. A `Hello` under the name of a player whose connection
has been silent for **2 s** replaces that connection at once: the player
keeps its id, so the others keep its puppet and seat (no `PlayerLeft`,
no `PlayerJoined`; the host sees `PlayerReturned`), and the new game sends
its look, held items and seat again. A live player (heard within 2 s)
cannot be replaced: a namesake joins as another player. A guest numbers
its states from the host clock's millisecond at its `Welcome`, so what it
sends after coming back is newer than what it sent before.

**Reaching each other**: direct first (LAN, Tailscale/NetBird — already on
every nixbook machine), then the relay when NAT blocks UDP. A host with a
relay configured is reachable both ways at once: directly on its port, and
through the relay by its **join code** (see [The relay](#the-relay)). A guest
joining by code goes through the relay; given the host's address too, it
tries that first and switches to the relay when the host has not answered
within half the timeout (2.5 s), without reporting an end in between
(`JoinTarget` in `endpoint.hpp`).

## Inside the plugin

- Configuration: `tandem.ini` next to the DLL (role, address, port, name,
  pose, story, quest_freeze, quest_freeze_allow, scene_privacy,
  private_actors, private_scenes, public_scenes, time_host; `relay`,
  `relay_token`, `join` for the relay and join codes): the defaults, and
  the test harness's session.
