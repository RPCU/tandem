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
  time left, or a new code once it expired or the relay restarted). A guest
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
  answer is smaller than its request (at most 21 bytes: no amplification);
  per-client packet and byte rate limits; at most `max-sessions`; idle
  clients forgotten after two minutes.

Wire format (`relay/internal/relay/wire.go`, big-endian, `TNR1` + type):

| Type              | Body                                  | Direction                    |
| ----------------- | ------------------------------------- | ---------------------------- |
| 1 `RegisterHost`  | secret 16, host token 32, pad to 64   | host → relay                 |
| 2 `RegisterGuest` | secret 16, pad to 64                  | guest → relay                |
| 3 `Registered`    | channel 2                             | relay → client               |
| 4 `Data`          | channel 2, ENet datagram              | both ways                    |
| 5 `Keepalive`     | —                                     | client → relay               |
| 6 `Error`         | code 1 (bad token, no session, full…) | relay → client               |
| 7 `RequestCode`   | secret 16, renew 1, pad to 64         | host → relay                 |
| 8 `Code`          | code 6, seconds left 2                | relay → host                 |
| 9 `ResolveCode`   | code 6, pad to 64                     | guest → relay (unregistered) |
| 10 `Secret`       | secret 16                             | relay → guest                |

- **Observability**: JSON logs, `/healthz`, Prometheus `/metrics`.
- **Running one** (tokens, systemd, the image, firewall, limits): [`docs/relay.md`](../relay.md).
- **Shipping**: `nix-build -A relayImage` builds an OCI image with only the
  static binary and CA certificates, running as uid 65532.
- **Kubernetes** (`deploy/kubernetes/base`): one replica (sessions live in
  memory; `Recreate`), non-root, read-only root, no capabilities, seccomp
  `RuntimeDefault`, no service-account token, a NetworkPolicy with UDP in
  from anywhere, metrics only from `monitoring`, and **no egress**
  (everything the relay sends is a reply on a flow a player opened). UDP
  LoadBalancer with `externalTrafficPolicy: Local` to keep players'
  addresses. The host-token Secret comes from the overlay's secret store.

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
hosting directly and tries the relay again every 20 s; a guest's failure
ends its join with the relay's reason ("no session has this join code
(wrong, or expired)").
