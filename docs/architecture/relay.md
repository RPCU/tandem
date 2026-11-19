# The relay

`relay/` — Go, standard library only. It forwards opaque ENet datagrams
between a session's host and guests; it never parses game traffic.

- **Sessions**: a 16-byte secret the host's game draws at random for each
  session. Guests register with the secret; the relay gives each a channel
  number and stamps it on what it forwards to the host.
- **Who may host**: only hosts presenting a token from the relay's token
  file (a Kubernetes Secret), so the relay is not an open bandwidth donor.
- **Join codes**: a registered host asks for a code naming its session
  (`RequestCode`, accepted only from the host's own address with its
  secret): 6 characters of Crockford's base32 (no I, L, O, U; read in any
  case, with or without the dash, O as 0, I and L as 1), drawn with
  `crypto/rand`, shown as `K7Q-2M9`. A code lives 10 minutes (`--code-ttl`)
  and dies with its session; the host's game asks again every 20 s (its
  time left, or a new code once it expired), and at once after a relay that
  went silent answers again (`ReclaimCode`: the code it had, see Replicas).
  A guest
  trades the code for the session's secret (`ResolveCode` → `Secret`),
  registers with the secret, and keeps it to join again after the code
  expired (a host that crashed and came back has a new secret: the code it
  shows then is needed). The plugin tries a code that led to no session 3
  times, then stops; once in the session it joins again by the secret
  alone, so a stale code never spends lookups.
- **Codes cannot be guessed cheaply**: 2^30 codes, at most one live per
  session (≤ `max-sessions`, 256). Lookups are limited per source (an IPv4
  address or an IPv6 /64: 5 at once, then one per 6 s; told once that it is
  over, then ignored) and in total (120 failed lookups a minute from
  everyone; past that every lookup waits for the next minute, so many
  sources together are no faster). Even with 256 live sessions and the
  total cap saturated, one hit takes ~24 days on average, against codes that
  change every 10 minutes. Cost: a flood of wrong codes delays honest joins
  by up to a minute; sources tracked are capped (4096).
- **Abuse limits**: unknown addresses never get an answer to data (no
  reflection); only registration-type requests (register, resolve a code),
  padded to 64 bytes, are answered from an unregistered address, and every
  answer is smaller than its request (at most 24 bytes, a redirect to an
  IPv6 replica: no amplification);
  per-client packet and byte rate limits; at most `max-sessions`; idle
  clients forgotten after two minutes. A session takes 5 guests at once
  (`MaxGuests`): the host's 3 players and its 2 spare connections (a player
  coming back while its old channel idles out, one to tell the session is
  full); the host enforces the 4 players.

## Replicas

Decided 2026-11-19 (the user asked for more replicas, resilience, and to
stay efficient). **Share nothing**: each replica owns its sessions in its
own memory and never talks to the others; they know each other's public
addresses from configuration only (`--peers`, an ordered list of `ip:port`,
the same on every replica; `--shard`, this replica's index, a StatefulSet
ordinal). No state is replicated, no egress is needed, and a replica's
loss costs its own sessions a short hold (below), nobody else's.

- **The code names its owner**: `shardOf(code)` is the code's 30 bits
  modulo the number of replicas, so replicas hold disjoint codes and every
  one of them computes the same owner. A replica issues only codes it owns
  (drawn with `crypto/rand` until one falls in its shard: N draws on
  average). Codes stay 6 characters, read and typed as before. A shard
  character (7 characters) was the alternative: it keeps the space per
  replica but makes every code longer for players, and buys nothing
  against guessing (next point).
- **Guessing costs the same**: what bounds a guesser is the failed lookups
  the whole fleet allows per minute against the codes live in the whole
  fleet. `--max-code-failures` (120) is that fleet-wide budget: each
  replica allows its share, ⌈120 / N⌉ a minute. A given session is then
  exactly as hard to hit as on one relay (1 in 2^30 per guess, 120 guesses
  a minute in all), and hitting _some_ session is as hard as on one relay
  holding as many sessions (N × 256 live codes: ~24/N days). The space did
  not shrink: a guess for a code of another shard is only redirected.
- **Redirect** (`Redirect`, type 11: an address family, the address, a
  port; 12 or 24 bytes, under the 64 of the request it answers). A
  `ResolveCode` for another shard's code is answered with the owner's
  address; it spends one of the source's lookups where it arrived (the
  owner spends another), so a flood spread over replicas is no cheaper, but
  is no failure (it says nothing about whether the code is live; the owner
  counts its failures). A replica never redirects to itself. The client
  follows **one redirect within a fleet** (one more to cross regions, see
  below; `core/src/relay.cpp`), so replicas that
  disagree on the peers list cannot send it round in circles: it fails
  with "the relay's replicas sent us on twice" (a host asking for a kept
  code then asks for any code instead).
- **The entrance** (`--front`, a second UDP port): what players write in
  `tandem.ini`, a load balancer over every ready replica. It only answers
  registration-type requests, each with a redirect: a new host to its
  replica (a replica owns the sessions registered on it), a code to its
  owner, a secret to this replica if it holds the session (else "no
  session": a secret does not say where its session is, so a guest keeps
  the replica's address, `RelayStatus::at`, and joins again there). No
  session's datagram ever crosses it, so a load balancer free to move flows
  between replicas breaks nothing, and a replica that is down only drops out
  of it (readiness). A host or guest may also be given a replica's own
  address: the same redirects apply there.
- **Drain** (SIGTERM): `/readyz` turns 503 (the entrance stops sending
  players here), a new session is redirected to the next replica (or
  refused, `ErrDraining`, 8, on a relay of its own), running sessions go on
  (their refreshes, new guests, data) until they end or `--drain-timeout`
  (60 s) passes; then the process exits. A second signal exits at once.
- **A replica that restarts** (a crash, a rolling update, its node lost):
  the StatefulSet brings it back with the same ordinal, so the same shard
  at the same address, knowing nothing. The games get it all back without
  a new handshake:
  - the host re-registers its secret, then `ReclaimCode` (type 13: secret,
    the code, its seconds left; padded to 64) asks for the code it had. The
    replica grants it when it is one of its shard, nobody holds it, the
    session has no live code, and it started less than a code's lifetime
    ago (any code it issued since, it knows), for the time the code had
    left. Otherwise it answers like `RequestCode` (a new code).
  - each guest registers again with `RejoinGuest` (type 12: secret, the
    channel it had; padded to 64) and gets that channel back when free, so
    the host's proxy (and ENet) knows it at the same local address.
  - meanwhile the session **holds** its relayed players (below), so
    nobody leaves: positions freeze for the gap, then play goes on. Test
    "a relay that restarts within the hold" (`tests/relay_test.cpp`) kills
    a relay for 8 s mid-session.
- **Before** (until 2026-11-19): one replica, a Deployment with
  `Recreate` (two would have split a session), every answer at most 21
  bytes, and a restart ended every session (the games registered again and
  got a new code). An operator restarted it with

  ```sh
  kubectl -n tandem rollout restart deployment/tandem-relay # after any change
  ```

  which is now `rollout restart statefulset/tandem-relay`.

- **Changing the number of replicas** changes `shardOf`: live codes may
  then belong elsewhere. The peers list is in every replica's
  configuration, so the change rolls them all; hosts reclaim their codes
  where they still belong and get new ones where they do not (their guests,
  already in, join again by secret and are unaffected).

## Direct paths (hole punching)

Approved 2026-11-19 with the three features after it. Through the relay,
host and guest also try to reach each other directly; the relay stays the
fallback, and ENet never knows: it keeps talking to the proxy on
127.0.0.1, which only changes where it sends.

- **Introduction**: a registered client tells the relay its LAN address
  (the interface that reaches the relay, with the port of the socket it
  uses for it) and its NAT flags (`Addresses`, 14, padded to 64). The
  host does as it registers (kept, not answered); a guest once the
  connection check is done, and its `Addresses` is answered with two
  `Introduce` (15), one to it and one to its host: the other's public
  address as the relay sees it, its LAN address, a nonce the relay drew
  for the pair. 24 bytes each, 48 for the 64 of the request, sent only to
  members of the guest's own session, at most once per 2 s; LAN addresses
  are kept only when private or loopback, so a guest cannot have its host
  punch at a stranger.
- **Punching**: both send `Punch` (16: the nonce, the guest's channel, an
  ack bit) every 100 ms for 3 s, from the socket they use for the relay,
  to the other's public and LAN addresses (and to whatever address its
  punches come from). A punch is answered with an ack; an ack, or the
  other side's data from the address a punch came from, means both ways
  work: the path is up and the proxy sends ENet's datagrams there (`Data`,
  as on the relay). Only addresses a punch with the nonce came from are
  heard.
- **Fallback**: a path silent for 1.5 s while we send on it goes down,
  its traffic back on the relay at once (ENet resends what was lost); the
  guest asks for another introduction a minute later. The relay
  registration stays alive meanwhile (keepalives, probes), and a relay
  that goes away leaves a direct path running (a guest with one does not
  fail at the end of the hold). Two sides whose NATs both map each
  destination apart (the connection check) do not punch at all.
- **Settings**: `direct = auto|off` (`tandem.ini`, the settings page,
  applied at once: off drops every path to the relay). Tests: "through
  the relay, host and guest find a direct path, and fall back to the
  relay" (up on 127.0.0.1, then the host turns it off mid-session, the
  guest falls back, the session goes on), "a host that does not punch
  back" (the guest stays relayed, nothing lost). What only a real network
  shows: two homes behind different NATs (cone and cone: up; one
  symmetric: up from the other side's punches; both symmetric: skipped).

## Several relays and regions

- **The client's list** (`relay = a, b, c`): a host probes each (`Probe`,
  17, padded to 64, answered by anyone on every port with `Observed`, 18:
  the client's address as that port sees it, 16 or 28 bytes) every 250 ms
  for a second at most and registers on the first to answer the soonest
  (starting over, after a failure or a lost relay, it chooses again). A
  guest asks the first for its code and goes down the list when one
  answers "no such code" (relays of separate networks).
- **Regions** (`--regions`, the entrance of each, `--region`, ours):
  independent fleets, sharing nothing but each other's entrances. A
  code's value modulo the regions is its region, and the rest modulo the
  region's replicas its replica (one region: the replicas' rule above). A
  lookup in another region's code is redirected to that region's entrance,
  which redirects to the owner: the client follows **two redirects at
  most** (one within a network, one across). The failed-lookup budget is
  the world's: each replica of each region allows its share,
  ⌈`--max-code-failures` / (regions × replicas)⌉, so guessing across
  regions is as slow as on one relay holding every session.

## Kept codes

- `code = keep` (`tandem.ini`, the settings page): the host registers with
  `RegisterHostKeep` (19: secret, token, salt; padded to 64) and its session
  gets its token's **kept code**: 30 bits of SHA-256 of the token and the
  salt, moved into the region and the token's replica (`keepOwner`, a hash
  of the token alone, so every salt lands on the same replica). The other
  replicas and the entrance redirect a keep registration there. The same
  token and salt give the same code every session, so friends join again
  with it.
- It lives **while its host is registered** (`Code`'s seconds left
  `0xFFFF`), dies with the session, and is in the same space with the same
  lookup limits as any code: one more live code per session, no easier to
  find (it is not guessable from anything public, and lookups do not tell
  kept from new). The newest session of a token takes its kept code (a
  host back from a crash or a reload before its old session idled out);
  the older one gets a code of its own the next time it asks.
- **New code** (the co-op menu): a kept code's next salt, saved to
  `tandem.ini` (`code_salt`), registered again at once (the same secret:
  the relay rebinds the session); a new one's `RequestCode` renew. A
  keep registration whose replica is away (no answer, or sent on twice)
  falls back to a new code.

## The connection check

Once registered, the client probes two relay ports from its relay socket:
the replica in use and a second one (the fleet's entrance, or the relay's
port + 1, an entrance's by default; a host with several relays also has
their answers from choosing). `RelayStatus` then has its public address
and port as the relay sees them, whether the port is the socket's own, and
its NAT's mapping: the same public port for every destination (`Stable`:
"direct connections likely") or one per destination (`Varies`, a symmetric
NAT: "relay only"); fewer than two answers leave it unknown. The co-op
menu shows it; it travels in `Addresses`' flags and `Introduce`, and two
`Varies` sides skip punching.

## The packet path

Measured on the laptop (Ryzen 7 6800U, 16 threads, Go 1.27, `go test
-bench . ./internal/relay/`), before → after:

| Benchmark                                   | Before                    | After          |
| ------------------------------------------- | ------------------------- | -------------- |
| `Forward` (one datagram, no socket)         | 165 ns, 1 alloc (208 B)   | 72 ns, 0 alloc |
| `ForwardParallel` (256 sessions, all cores) | 250 ns, 1 alloc, one lock | 46 ns, 0 alloc |
| `Loopback` 1 pair, 1 reader (forwards/s)    | 121 k                     | 136 k          |
| `Loopback` 8 pairs, 1 reader / 4 readers    | 90 k / —                  | 102 k / 275 k  |

- **No allocation per packet**: a `Data` datagram is forwarded in place
  (same size, only its channel rewritten) from the reader's buffer; every
  other answer is appended to a buffer the reader reuses.
- **Locks**: the data path takes only read locks: the client table is in
  64 shards keyed by `netip.AddrPort`, a session's guests behind its own
  `RWMutex`. Registrations, codes and lookups keep one control lock. Each
  client's rate window has its own lock, never contended (below).
- **Several readers** (`--readers`, default min(CPUs, 8)): as many sockets
  bound to the port with `SO_REUSEPORT` (`net.ListenConfig.Control`, the
  standard `syscall` package), the kernel spreading clients over them by
  address, so a client always lands on the same reader. Forwarding counters
  are per reader, on their own cache lines. Elsewhere than Linux, one.

Wire format (`relay/internal/relay/wire.go`, big-endian, `TNR1` + type):

| Type                  | Body                                               | Direction                    |
| --------------------- | -------------------------------------------------- | ---------------------------- |
| 1 `RegisterHost`      | secret 16, host token 32, pad to 64                | host → relay                 |
| 2 `RegisterGuest`     | secret 16, pad to 64                               | guest → relay                |
| 3 `Registered`        | channel 2                                          | relay → client               |
| 4 `Data`              | channel 2, ENet datagram                           | both ways                    |
| 5 `Keepalive`         | —                                                  | client → relay               |
| 6 `Error`             | code 1 (bad token, no session, full…)              | relay → client               |
| 7 `RequestCode`       | secret 16, renew 1, pad to 64                      | host → relay                 |
| 8 `Code`              | code 6, seconds left 2                             | relay → host                 |
| 9 `ResolveCode`       | code 6, pad to 64                                  | guest → relay (unregistered) |
| 10 `Secret`           | secret 16                                          | relay → guest                |
| 11 `Redirect`         | family 1 (4 or 6), address 4 or 16, port 2         | relay → client               |
| 12 `RejoinGuest`      | secret 16, channel 2, pad to 64                    | guest → relay                |
| 13 `ReclaimCode`      | secret 16, code 6, seconds left 2, pad 64          | host → relay                 |
| 14 `Addresses`        | flags 1, count 1, count × (IPv4 4, port 2), pad 64 | client → relay               |
| 15 `Introduce`        | channel 2, nonce 4, flags 1, public 4+2, LAN 4+2   | relay → host and guest       |
| 16 `Punch`            | nonce 4, channel 2, ack 1                          | client ↔ client              |
| 17 `Probe`            | nonce 4, pad to 64                                 | client → relay (anyone)      |
| 18 `Observed`         | nonce 4, family 1, address 4 or 16, port 2         | relay → client               |
| 19 `RegisterHostKeep` | secret 16, host token 32, salt 4, pad 64           | host → relay                 |

Error codes: 1 bad token, 2 no session, 3 full, 4 too many sessions, 5
taken, 6 no such code, 7 slow down, 8 draining (no replica to send a new
session to).

- **Observability**: JSON logs; on `--http`, `/healthz` (alive),
  `/readyz` (503 while draining), Prometheus `/metrics` (sessions, clients,
  draining, packets, bytes, drops, codes issued and reclaimed, lookups,
  refusals and redirects by kind) and the same counters as JSON on
  `/debug/vars` (`expvar`).
- **Running one** (tokens, systemd, the image, firewall, limits, replicas): [`docs/relay.md`](../relay.md).
- **Shipping**: `nix-build -A relayImage` builds an OCI image with only the
  static binary and CA certificates, running as uid 65532.
- **Kubernetes** (`deploy/kubernetes/base`): a StatefulSet of 2 (a stable
  ordinal is the shard and picks the replica's own Service; a Deployment's
  pods have no identity to hang an address on), `Parallel` pod management,
  rolling updates one at a time, a PodDisruptionBudget of one unavailable,
  preferred anti-affinity across nodes, readiness on `/readyz`, 75 s of
  grace for the 60 s drain. One UDP LoadBalancer Service per replica
  (`apps.kubernetes.io/pod-index` selector) and one for the entrance, all
  `externalTrafficPolicy: Local` to keep players' addresses; the peers
  list from a generated ConfigMap the overlay replaces (its hash rolls the
  replicas). Non-root, read-only root, no capabilities, seccomp
  `RuntimeDefault`, no service-account token, a NetworkPolicy with UDP in
  from anywhere, metrics only from `monitoring`, and **no egress**
  (everything the relay sends is a reply on a flow a player opened, and
  replicas never talk to each other). The host-token Secret comes from the
  overlay's secret store.

**On the game side** (`core/src/relay.cpp`, `tandem/relay.hpp`) the session
talks to the relay through a small UDP proxy on 127.0.0.1, so ENet never
knows the relay exists. A guest's ENet connects to the proxy's local
socket; the proxy wraps what it sends in `Data` and unwraps what comes back
(datagrams ENet sends before the relay registered it wait, a few at most).
A host's ENet keeps listening on its port for direct guests; the proxy opens
one local socket per relayed guest (relay channel) and sends from it to
ENet's port, so ENet sees each relayed guest as its own address
(127.0.0.1:N); a socket silent for a minute closes. The proxy is pumped
around each ENet service in `Session::Poll`, which waits on ENet's socket
and the proxy's together, so relayed traffic takes no extra frame. Only the
relay's address is heard on the outer socket, and only ENet's own socket on
the local ones. A host whose relay fails (no answer, refused token) keeps
hosting directly and tries the relay again every 20 s, from the configured
address; a guest's failure ends its join with the relay's reason ("no
session has this join code (wrong, or expired)").

**A relay that goes silent** (`RelayLink`, `Session::Poll`): registered,
the link asks the relay again (its registration, answered at once) after
1 s without a datagram from it (5 s for a host with no relayed guest), and
calls it lost 2 s later. While lost (`RelayLink::Holding`, at most
`kHoldMs`, 20 s) the session does not judge the peers it carries (their
ENet backstop is set past the hold when they connect), the link asks every
0.5 s, and the relay's refusals are not taken as final (a restarted replica
says "no session" until the host is back). When it answers, the host asks
for its code back and each relayed peer gets a ping and a full timeout to
be heard again. Past the hold a guest's link fails (its session ends, and
the plugin joins again by secret at `RelayStatus::at` every 5 s,
`net_thread.cpp`), and a host's starts over at the configured address
(another replica, a new code). Before this, a relay gap longer than the
5 s session timeout ended every relayed session: guests came back as new
players after the host's 20 s refresh.
