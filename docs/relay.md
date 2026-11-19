# Running a tandem-relay

`tandem-relay` lets two players behind NAT play together without a port
forward. It forwards opaque UDP datagrams between a session's host and its
guests, and hands the host a short **join code** to give to friends. It
never parses game traffic and keeps nothing on disk. How it works inside is
in [architecture/relay.md](architecture/relay.md); this page is
for whoever runs one.

You need a machine players can reach on **one UDP port**: a small VPS, a
Kubernetes cluster, or a home server with that port forwarded. One relay
serves a group of friends easily (256 sessions at most by default);
several replicas share the load and outlive each other's restarts
([Replicas](#replicas)).

What to know before offering one to others:

- **Game traffic is not encrypted.** The relay (and anyone on the path)
  can read it: positions, names, the story's state. Run a relay for people
  who trust you, and play through one run by someone you trust.
- **It sees the players' IP addresses**, and logs them: a host's when its
  session opens, closes or gets a join code, a guest's when it joins.
- **Only holders of a host token can open a session**, so the relay is not
  a free bandwidth service; guests need no token, only the host's code.

## Host tokens

A host token is 32 random bytes, written as 64 hex digits:

```sh
tandem-relay token
# 5f0c…(64 hex digits)
```

The relay reads a **token file** at start (`--host-tokens`, default
`/etc/tandem-relay/host-tokens`): one token per line; empty lines and lines
starting with `#` are ignored; a malformed line stops the relay with the
line's number. It refuses to start with no token at all.

```
# alice
5f0c0e…64 hex digits…
# bob
91ab7d…64 hex digits…
```

- Give **each person who hosts their own token**. To revoke one, delete its
  line and restart the relay.
- The file is read **once**: restart the relay after any change. Sessions
  live in memory, but a restart that is over within 20 s ends none of
  them: the games notice within seconds, hold their players, register
  again and get their join code back (see
  [Restarts and drain](#restarts-and-drain)).
- A host puts their token in their game's `tandem.ini`
  (`relay_token = …`). It is never sent to guests. Treat it as a password:
  it goes over the network in clear (see above), so do not reuse it.
- `--allow-any-host` drops the token check. It exists for local testing
  (`task relay:run`); never expose a relay started with it.

What players write in `red4ext/plugins/tandem/tandem.ini`:

```ini
relay = relay.example.org:27460
; hosts only:
relay_token = 5f0c0e…
```

## Run it: one binary on a home server or a VPS

The relay is one static binary (Go, standard library only, no cgo) with
two subcommands, `serve` and `token`.

Build it, with Go 1.25 or newer (`relay/go.mod`):

```sh
cd relay
CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o tandem-relay ./cmd/tandem-relay
```

or with Nix, from the repository's root (`result/bin/tandem-relay`):

```sh
nix-build -A relay
```

Try it by hand:

```sh
./tandem-relay token > host-tokens
./tandem-relay serve --udp=:27460 --http=127.0.0.1:8080 --host-tokens=./host-tokens
# {"time":"…","level":"INFO","msg":"relay up","udp":"[::]:27460","http":"127.0.0.1:8080","host_tokens":1}
curl -s 127.0.0.1:8080/healthz    # ok
```

### As a systemd service

[`deploy/systemd/tandem-relay.service`](../deploy/systemd/tandem-relay.service)
runs it as a throwaway user with no capability, a read-only system, a
system-call filter and its own copy of the token file:

```sh
sudo install -m 0755 tandem-relay /usr/local/bin/tandem-relay
sudo install -d -m 0700 /etc/tandem-relay
tandem-relay token | sudo tee /etc/tandem-relay/host-tokens >/dev/null
sudo chmod 0600 /etc/tandem-relay/host-tokens
sudo install -m 0644 deploy/systemd/tandem-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tandem-relay
journalctl -u tandem-relay -f        # JSON lines; "relay up" first
```

The unit binds health and metrics to `127.0.0.1:8080`: change `--http` only
if a Prometheus on another machine must scrape it, and then firewall it.
It needs systemd 247 or newer (`LoadCredential`).

## Run it: Kubernetes

### The image

```sh
task relay:image          # nix-build -A relayImage -o result-relay-image
```

builds an OCI image archive, `tandem-relay:<version>` (the version of
`nix/relay.nix`, 0.1.0 today): the static binary and CA certificates, no
shell, no package manager, user `65532:65532`, entrypoint
`tandem-relay`, command `serve`.

Load it locally, or push it to your registry:

```sh
task relay:load                                   # podman load < result-relay-image
skopeo copy docker-archive:result-relay-image \
  docker://registry.example.org/tandem/tandem-relay:0.1.0
```

Without Nix, [`relay/Dockerfile`](../relay/Dockerfile) builds the same
image (a static binary on `gcr.io/distroless/static-debian12:nonroot`: CA
certificates, no shell, user `65532:65532`, the same entrypoint and
command):

```sh
docker build -t tandem-relay relay/
```

CI (`.github/workflows/image.yaml`) builds that Dockerfile and pushes
`zot.rpcu.io/public/tandem-relay:<8-char commit>` on every push to
`main`, plus `:<x.y.z>` for a version tag; `nix-build.yaml` checks the Nix
image builds whenever its inputs change. Any other builder that copies the
static binary into an empty image does the same job; keep it non-root,
with no shell.

### The manifests

[`deploy/kubernetes/base`](../deploy/kubernetes/base) is a kustomize base:
a ServiceAccount (no token mounted), a StatefulSet of 2 replicas, a UDP
Service per replica, one for the fleet's entrance, a headless one for
metrics, a PodDisruptionBudget, the peers ConfigMap and a NetworkPolicy
(how replicas work: [Replicas](#replicas)). It is not deployable alone: an
overlay provides the namespace, the image's registry, the token Secret,
each replica's public address and the peers list naming them.

```yaml
# overlays/prod/kustomization.yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: tandem
resources:
  - ../../base
  - namespace.yaml # kind: Namespace, name: tandem
  - host-tokens.yaml # the Secret below, from your secret store
images:
  - name: tandem-relay
    newName: registry.example.org/tandem/tandem-relay
    newTag: 0.1.0
# Every replica's public address, in ordinal order (no names: the relay
# makes no DNS query).
configMapGenerator:
  - name: tandem-relay-peers
    behavior: replace
    literals:
      - peers=203.0.113.10:27460,203.0.113.11:27460
# A static address for each replica's Service, matching the list above (the
# annotation depends on your load balancer: MetalLB, a cloud's...).
patches:
  - target: { kind: Service, name: tandem-relay-0 }
    patch: |
      - op: add
        path: /metadata/annotations
        value: { metallb.universe.tf/loadBalancerIPs: 203.0.113.10 }
  - target: { kind: Service, name: tandem-relay-1 }
    patch: |
      - op: add
        path: /metadata/annotations
        value: { metallb.universe.tf/loadBalancerIPs: 203.0.113.11 }
```

Players write the entrance's address (the `tandem-relay` Service, give it a
DNS name) in `tandem.ini`; the replicas' addresses only travel in the
relay's redirects.

The Secret is named `tandem-relay-host-tokens`, with one key,
`host-tokens`, holding the token file. Keep it out of git in clear: SOPS,
sealed-secrets or an external secret operator. For a first try:

```sh
kubectl -n tandem create secret generic tandem-relay-host-tokens \
  --from-file=host-tokens=./host-tokens
kubectl -n tandem rollout restart statefulset/tandem-relay   # after any change
```

What the base decides, and why:

- **A StatefulSet, not a Deployment**: a replica's ordinal is its shard
  (`TANDEM_RELAY_SHARD`, from the `apps.kubernetes.io/pod-index` label,
  Kubernetes 1.28 or newer) and selects its own Service, so a replica
  replaced comes back as the same shard at the same address, and its
  sessions find it again. A Deployment's pods have random names: there is
  nothing stable to hang an address on (a Deployment per replica would
  do the same job with more YAML). `Parallel` pod management (replicas do
  not wait for each other), rolling updates one replica at a time.
- **One `LoadBalancer` Service per replica, and one for the entrance**,
  all UDP with `externalTrafficPolicy: Local`, so the relay sees each
  player's own address: it knows clients by address and port and limits
  each one. Your load balancer must support UDP. Other ways to give each
  replica its own address: [Addressing the replicas](#addressing-the-replicas).
- **PodDisruptionBudget** of one unavailable, and **anti-affinity**
  (preferred) to put replicas on different nodes: a node drain or loss
  takes one shard at a time. Make it required in the overlay when the
  cluster always has as many nodes as replicas.
- **Readiness** is `/readyz`, false while draining, so the entrance stops
  sending players to a replica shutting down; liveness `/healthz`.
  `terminationGracePeriodSeconds: 75` covers the 60 s drain.
- **Locked down**: non-root (65532), read-only root filesystem, every
  capability dropped, no privilege escalation, seccomp `RuntimeDefault`.
- **NetworkPolicy**: UDP 27460 (sessions) and 27461 (entrance) in from
  anywhere; TCP 8080 in only from the namespace labelled
  `kubernetes.io/metadata.name: monitoring` (change it in the overlay if
  your Prometheus lives elsewhere); **no egress at all**: the relay only
  ever answers on a flow a player opened, needs no DNS, and replicas never
  talk to each other.
- **Resources**: 50m CPU and 32Mi requested, 1 CPU and 128Mi at most, 2
  reader sockets (`--readers`) per replica.

## Replicas

A fleet of relays shares nothing: each replica keeps its own sessions in
memory, and they never talk to each other. What ties them together is one
list every replica gets, `--peers` (or `TANDEM_RELAY_PEERS`): each
replica's **public** `ip:port`, in order; and `--shard` (or
`TANDEM_RELAY_SHARD`), this replica's place in it (a number, or a name
ending in it, as `tandem-relay-1`).

- A host registers on any replica, which owns its session from then on and
  gives it a code of its own **shard** (the code says which replica holds
  it).
- A guest may ask any replica for a code: one that does not hold it
  answers with a **redirect** to the one that does, and the game follows
  it (once at most).
- `--front ADDR` opens the fleet's **entrance** on another port: it only
  ever redirects (a new host to this replica, a code to its owner), so it
  can sit behind one load balancer over all replicas, and is the one
  address players need. A replica down only leaves it (its readiness).
- Each replica allows 256 sessions (`--max-sessions`): the fleet, that
  many times the replicas. `--max-code-failures` is for the whole fleet:
  each replica allows its share, so more replicas make guessing no faster.

Without a Kubernetes cluster, the same works with the binary: run one
`tandem-relay serve --peers A,B --shard 0 --front :27461` on machine A and
`--shard 1` on B (the same `--peers`), and give players either machine's
entrance (or a DNS name with both).

### Addressing the replicas

Every replica needs a public address of its own, the one in `--peers`,
that reaches it and no other, whichever node it runs on, and that keeps
the players' source addresses. In Kubernetes:

| Option                                                        | Address in `--peers`               | Fits                                                                                                                                     |
| ------------------------------------------------------------- | ---------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| **`LoadBalancer` per replica** (the base), static IP each     | the Service's reserved IP          | clouds, MetalLB; the simplest                                                                                                            |
| `NodePort` per replica, `externalTrafficPolicy: Local`        | a node's IP and the replica's port | only if the replica is pinned to that node (a node selector per ordinal is not possible in one StatefulSet: one StatefulSet per replica) |
| `hostPort` (or `hostNetwork`) on nodes reserved for the relay | the node's IP                      | bare metal with fixed relay nodes, one replica per node                                                                                  |

`externalTrafficPolicy: Cluster` (the default) hides players' addresses
behind the node's: do not use it, the relay would see every player as a
few addresses and limit them together. The entrance can be any UDP load
balancer over every replica's `front` port (27461) that keeps source
addresses; the replicas' own addresses must not move between replicas.

### Scaling

Change the StatefulSet's `replicas`, add a Service per new replica
(`service-replicas.yaml`, its `apps.kubernetes.io/pod-index`), and the
peers list, in one change. The list's ConfigMap hash rolls every replica,
and a code's owner is computed from the number of replicas, so live codes
may move: hosts get their code back where it still belongs and a new one
where it does not (the Co-op panel shows it); players already in a session
are not affected. Scale when few people play.

### Restarts and drain

- **SIGTERM** (a rolling update, a node drain, `systemctl stop`): the
  replica turns unready (`/readyz` 503), sends new sessions to the next
  replica (or refuses them, "the relay is restarting", if it is alone),
  and keeps relaying the running ones until they end or `--drain-timeout`
  (60 s) passes, then exits. A second signal exits at once.
- **The games hold their players** through a relay that goes silent for up
  to 20 s: positions freeze, nobody leaves. When the replica is back (the
  same ordinal, the same address), the host registers again and gets its
  code back, the guests get their place back, and play goes on. Longer
  than that, guests are dropped and join again by themselves when the host
  is back; hosts start over at the entrance with a new code.
- So a rolling update costs each session on the replica being replaced a
  pause of a few seconds (the time its pod takes to come back), not the
  session. Keep the image on the nodes (`imagePullPolicy: IfNotPresent`,
  the default for a tagged image) so the pod starts at once.

Check a change to the manifests with `task k8s:check` (renders the base and
validates it with kubeconform).

### Several regions

Players far apart are better served by a relay near the host. Run one
fleet per region (each its own replicas, peers list and entrance, as
above), and tell every replica of every region the regions' entrances, in
the same order everywhere:

```sh
# Europe, replica 0 (and 1, with --shard 1), region 0 of 2
tandem-relay serve --peers 203.0.113.10:27460,203.0.113.11:27460 --shard 0 --front :27461 \
  --regions 203.0.113.5:27460,198.51.100.5:27460 --region 0
# America: its own --peers, --region 1, the same --regions
```

In Kubernetes, one overlay per region, each adding to the peers ConfigMap
(the StatefulSet reads them as `TANDEM_RELAY_REGIONS` and
`TANDEM_RELAY_REGION`):

```yaml
configMapGenerator:
  - name: tandem-relay-peers
    behavior: replace
    literals:
      - peers=203.0.113.10:27460,203.0.113.11:27460
      - regions=203.0.113.5:27460,198.51.100.5:27460
      - region=0
```

- Regions share nothing: a region down costs only its own sessions, and
  no traffic crosses between them. A code says its region: a guest asking
  any relay of any region is sent to the right one (two redirects at most).
- Players list the regions' entrances, nearest first or not:
  `relay = eu.relay.example.org, us.relay.example.org`. A host registers on
  the one that answers its probe the soonest; a guest asks the first, and
  the others in turn if it does not know the code (relays of networks that
  do not know each other work too).
- `--max-code-failures` is then the world's budget: each replica of each
  region takes its share. Give every region the same value.
- Adding or removing a region changes which region a code names: update
  `--regions` everywhere in one change; hosts get new codes.

### Kept codes and the connection check

- A host with `code = keep` in `tandem.ini` gets the same code every
  session (derived from its host token, while it is registered; the
  co-op menu's New code moves on to another). All the kept codes of one
  token live on one replica of a region: if it is down, the host gets a
  new code elsewhere for that session. Revoking the token ends them.
- The game's connection check probes two of the relay's ports (the
  replica's and the entrance's): keep the entrance's port open next to the
  replica's (with `--front`) so it can tell whether direct connections
  should work. A relay on its own can run `--peers <its public ip:port>
--front :27461` for that alone.

## What to open on a firewall

| Direction | Port          | From                 | Why                                                                       |
| --------- | ------------- | -------------------- | ------------------------------------------------------------------------- |
| in        | **UDP 27460** | anywhere             | players (hosts and guests)                                                |
| in        | UDP 27461     | anywhere             | the fleet's entrance, with `--front` only                                 |
| in        | TCP 8080      | your monitoring only | `/healthz`, `/readyz`, `/metrics`, `/debug/vars`; never from the internet |
| out       | nothing       |                      | replies to players go out on the flows they opened                        |

- A stateful firewall needs only the inbound UDP rule: replies are part of
  the same flow. The relay never starts a conversation.
- Behind a home router: forward UDP 27460 to the server, and give players
  the router's public address or a dynamic DNS name.
- Examples:

  ```sh
  sudo ufw allow 27460/udp
  sudo firewall-cmd --permanent --add-port=27460/udp && sudo firewall-cmd --reload
  sudo nft add rule inet filter input udp dport 27460 accept
  ```

- Another port: `--udp=:PORT` on the relay, and players write
  `relay = host:PORT`.
- Players need nothing opened on their side: both the host and the guests
  only send _to_ the relay.

## Limits

Flags of `tandem-relay serve`, with their defaults:

| Flag                  | Default                         | What                                                                                                           |
| --------------------- | ------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `--udp`               | `:27460`                        | UDP address players reach                                                                                      |
| `--front`             | none (`TANDEM_RELAY_FRONT`)     | UDP address of the fleet's entrance (redirects only; needs `--peers`)                                          |
| `--peers`             | none (`TANDEM_RELAY_PEERS`)     | every replica's public `ip:port`, in shard order                                                               |
| `--shard`             | `0` (`TANDEM_RELAY_SHARD`)      | this replica's index in `--peers`, or a name ending in it                                                      |
| `--readers`           | CPUs, at most 8                 | sockets reading the UDP port at once (`SO_REUSEPORT`, Linux)                                                   |
| `--drain-timeout`     | `60s`                           | on SIGTERM, how long running sessions may go on before the relay exits                                         |
| `--http`              | `:8080`                         | HTTP address for `/healthz`, `/readyz`, `/metrics` and `/debug/vars`                                           |
| `--host-tokens`       | `/etc/tandem-relay/host-tokens` | the token file                                                                                                 |
| `--max-sessions`      | `256`                           | sessions open at once; the next host is refused                                                                |
| `--idle`              | `2m`                            | a client silent this long is forgotten (a session ends with its host)                                          |
| `--code-ttl`          | `10m`                           | how long a join code lives (the host's game renews it)                                                         |
| `--code-lookup-burst` | `5`                             | join-code lookups one source may make at once; then one per 6 s                                                |
| `--max-code-failures` | `120`                           | failed lookups per minute from everyone together, for the whole fleet; past it every lookup waits for the next |
| `--allow-any-host`    | off                             | no token check: local testing only                                                                             |

Fixed in the code (`relay/internal/relay/relay.go`, `DefaultConfig`):

- 3 guests per session (a session is 4 players);
- each client: 2000 datagrams and 4 MiB per second, the rest is dropped
  (`rate_limited`);
- a source is an IPv4 address or an IPv6 /64; at most 4096 sources are
  tracked for the lookup limits.

What these protect against:

- **Reflection and amplification**: an address that is not registered
  never gets an answer to data, only to a registration or a code lookup,
  which must be padded to 64 bytes while the answer is 24 bytes at most
  (a redirect to an IPv6 replica).
- **Guessing join codes**: a code is 6 characters of base32 (2^30), valid
  ten minutes, at most one per session. With every limit above saturated,
  one hit takes weeks on average. The price: a flood of wrong codes can
  delay honest joins by up to a minute.
- **Open hosting**: no token, no session.

Bandwidth is the players': the relay forwards what a session sends, to each
other player, and holds nothing. Watch
`tandem_relay_forwarded_bytes_total` on your own sessions before sizing a
metered server.

## Health, metrics and logs

- `GET /healthz` answers `ok` (liveness); `GET /readyz` answers `ok`, or
  503 while draining (readiness: the entrance's load balancer leaves it).
- `GET /debug/vars` has every counter below as JSON (`expvar`).
- `GET /metrics` is the Prometheus text format (each replica its own:
  scrape the pods, `tandem-relay-pods`, not a load-balanced address):

  | Metric                                    | Type    | What                                                                                                  |
  | ----------------------------------------- | ------- | ----------------------------------------------------------------------------------------------------- |
  | `tandem_relay_sessions`                   | gauge   | open sessions                                                                                         |
  | `tandem_relay_clients`                    | gauge   | registered hosts and guests                                                                           |
  | `tandem_relay_draining`                   | gauge   | 1 while shutting down (no new session)                                                                |
  | `tandem_relay_forwarded_packets_total`    | counter | datagrams forwarded                                                                                   |
  | `tandem_relay_forwarded_bytes_total`      | counter | payload bytes forwarded                                                                               |
  | `tandem_relay_dropped_total{reason}`      | counter | drops: `malformed`, `bad_token`, `unknown_client`, `rate_limited`, `no_channel`                       |
  | `tandem_relay_codes_issued_total`         | counter | join codes issued                                                                                     |
  | `tandem_relay_codes_reclaimed_total`      | counter | codes given back to their host after a restart                                                        |
  | `tandem_relay_code_lookups_total{result}` | counter | code lookups: `found`, `unknown`, `limited`, `redirected` (to the owner)                              |
  | `tandem_relay_refused_total{reason}`      | counter | registrations refused: `no_session`, `session_full`, `too_many_sessions`, `session_taken`, `draining` |
  | `tandem_relay_redirects_total{kind}`      | counter | registrations redirected: `host` (a new session), `guest` (a secret)                                  |

  Worth an alert: `bad_token` rising (someone trying to host without a
  token, or a typo in a friend's `tandem.ini`); `unknown` or `limited`
  lookups rising steadily (someone guessing codes); `rate_limited` on a
  normal evening (a limit too low, or a misbehaving client);
  `tandem_relay_sessions` near `--max-sessions`.

- Logs are JSON lines on standard output (`journalctl -u tandem-relay`,
  `kubectl logs`).

## When players say it does not work

What the host's Co-op panel shows, and what it means here:

| The game says                                     | Look at                                                                                    |
| ------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| the relay did not answer                          | DNS name, the UDP port (firewall, port forward, load balancer), is the relay running       |
| the relay refused our host token (relay_token)    | the token is not in the file, or the relay was not restarted after adding it (`bad_token`) |
| no session has this join code (wrong, or expired) | the guest's code is old: the host's panel shows the current one                            |
| too many join attempts: try again in a minute     | the lookup limits; `limited` in the metrics                                                |
| the relay has too many sessions open              | `--max-sessions`                                                                           |
| the relay went silent: reconnecting               | a replica restarting (it comes back within seconds) or the path to it; `kubectl get pods`  |
| the relay's replicas sent us on twice             | the replicas' `--peers` lists differ: give them all the same                               |
| the relay is restarting: try again in a moment    | a relay on its own is draining                                                             |

From outside, `nc -u -v relay.example.org 27460` cannot tell much (the
relay does not answer strangers, on purpose). The quickest end-to-end test
is the mock player, from a checkout of tandem on two machines:

```sh
task peer -- host --relay relay.example.org --relay-token 5f0c0e…   # logs "join code K7Q-2M9"
task peer -- join --code K7Q-2M9 --relay relay.example.org
```
