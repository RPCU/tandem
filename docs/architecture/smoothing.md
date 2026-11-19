# Time and smoothing

- **One clock: the host's.** Guests estimate the offset from pings, keeping
  the lowest round trip (`Endpoint::HostNowUs`). Every state carries
  host-clock microseconds.
- **Absolute times are integers.** A float can't hold an epoch timestamp
  (one earlier project froze every remote player that way); only
  differences become float.
- **Remote things are drawn slightly in the past**, interpolated between
  the two samples around that time: 65 ms behind at 30 Hz, widened per
  player by measured jitter and transit, capped at 400 ms (measured on 2.5
  by NCO, see the research file), and moved there at a bounded pace (the
  drawn timeline never skips back). Short gaps are extrapolated (250 ms for
  players, falling when the V is in the air; 500 ms for vehicles; an NPC
  along the velocity of its last two states), then held; the correction
  when states come back fades out over ~0.1 s. Cars follow a cubic through
  their states and velocities. Numbers and tests: `netcode.md`.
- **Teleports are put, not flown.** Two states further apart than 20 m and
  faster than 150 m/s (a fast travel, a quest teleport, a regroup) are not
  interpolated: the body stays until the moment of the jump and is then
  drawn there, and a velocity that fast (the sender's own jump over one
  sample) is not extrapolated (`sync::Track`, tested).
- **The clock offset follows the best ping.** Until 2026-11-18 the
  best-RTT filter started from `UINT64_MAX`, its slow forgetting wrapped it
  to 199 µs, and no pong over a real link ever counted: a guest's host
  clock stayed the `Welcome`'s, off by the one-way latency. Found by the
  shaped-link test (60 ms off at 40 ms each way; now within 7 ms).
- **How a V moves** (`core/include/tandem/movement.hpp`,
  `scripts/tandem/Movement.reds`): beyond walking, a V's game packs its
  player state machine into `PlayerState::flags`, a field that was always
  sent as 0 (no wire change): `LocomotionDetailed` (bits 0–4), `Swimming`
  (5–6), in a moving elevator (7), in a workspot (8), felled (9), filled
  (15; NPC states and older senders leave it clear: placed as walking).
  The puppet's controller turns it into a placement (ground, air, climb,
  ladder, swim, platform, down) and asks `movement::Placer` each frame how
  far to move the body: everywhere but on the ground, exactly where the
  stream says; on landing the height left is eased back; a body left
  behind (≥ 0.75 m for 0.2 s off the ground, a ledge ≥ 0.6 m above for
  0.3 s, ≥ 2.5 m for 0.5 s, ≥ 8 m at once; one per second) is teleported
  by the game thread (`TandemNative.TakeSnap`, the teleportation
  facility). A held puppet (`HoldPuppet`: a grapple, a carrier, an NPC
  proxy's workspot) is its holder's: no placement and no teleport while
  held (a pending one is dropped), placement afresh once let go.
- Rates: players and vehicles 30 Hz, fewer when far from every other V or
  standing still; poses 30 Hz within 30 m, fewer beyond (`netcode.md`,
  Relevance); NPC proxies 10 Hz, only those that changed (`NpcMotion`).
  The clock offset is slewed in (`Endpoint::kClockSlew`), not jumped.
