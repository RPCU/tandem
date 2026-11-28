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
- **Anyone can host, by default** (open mode): no account, no token. What
  keeps it from being a free bandwidth service is below
  ([Open mode](#open-mode)); `--require-token` closes it to token holders.
  Guests never need a token, only the host's code.

## Open mode

A relay is open unless started with `--require-token`. A host with no token
opens a session like this, and the relay keeps no account of anyone:

- **A proof of work per session**: its registration is answered with a
  challenge (stateless: a stamp, the difficulty and a MAC of both and the
  host's address under a key drawn at start, 26 bytes for its 64). The
  game finds a nonce such that SHA-256(challenge, its session, nonce)
  starts with `--pow-bits` zero bits (17 by default: about 20 ms of a
  desktop's CPU, a few tens on a slow one, spread over its network thread)
  and registers with it. Each challenge opens one session, and only within
  two minutes. Opening many sessions costs that much each, which is the
  point.
- **At most `--max-sessions-per-source` (2) open sessions per source** (an
  IPv4 address, an IPv6 /64), and `--max-open-sessions` (192) in all
  (`--max-sessions`, 256, counts every session). Past them the host is told
  "the relay hosts as many sessions from your address as it may", or that
  the relay is full.
- **A bandwidth cap per guest link and direction**: `--open-kbps` (512,
  twice what tandem sends) with `--open-burst-kb` (128) at once; the rest is
  dropped (`tandem_relay_dropped_total{reason="bandwidth"}`). A session of
  3 guests may move 3 × 512 kbit/s each way.
- **Idle sessions end** after 2 minutes, as any.
- Everything else holds as before: guests need the code, the code-guessing
  limits, no answer to strangers' data, no answer larger than its request.

Hosts with a **host token** skip all of it (no proof, no caps): give tokens
to the people you host for, if you want them unlimited.

## Host tokens

Optional (open mode), or required with `--require-token`. A host token is
32 random bytes, written as 64 hex digits:

```sh
tandem-relay token
# 5f0c…(64 hex digits)
```

The relay reads a **token file** at start (`--host-tokens`, default
`/etc/tandem-relay/host-tokens`; none, an empty path or a missing file in
open mode): one token per line; empty lines and lines starting with `#` are
ignored; a malformed line stops the relay with the line's number. With
`--require-token` it refuses to start with no token at all.

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
  it goes over the network in clear (see above), so do not reuse it. Kept
  join codes (`code = keep`) need one: they are a token's.
- By hand, then as a service (the unit's drop-in, below, reads it):

  ```sh
  ./tandem-relay token > host-tokens
  ./tandem-relay serve --udp=:27460 --http=127.0.0.1:8080 --host-tokens=./host-tokens
  # {"time":"…","level":"INFO","msg":"relay up","udp":"[::]:27460","http":"127.0.0.1:8080","host_tokens":1}
  sudo install -d -m 0700 /etc/tandem-relay
  tandem-relay token | sudo tee /etc/tandem-relay/host-tokens >/dev/null
  sudo chmod 0600 /etc/tandem-relay/host-tokens
  ```

- `--allow-any-host` drops the token check and the open mode's proof and
  limits. It exists for local testing (`task relay:run`); never expose a
  relay started with it.

What players write in `red4ext/plugins/tandem/tandem.ini` (tandem ships
with the public RPCU relay set: `relay = relay.rpcu.io:27460`):

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
./tandem-relay serve --udp=:27460 --http=127.0.0.1:8080
# {"time":"…","level":"INFO","msg":"relay up","udp":"[::]:27460",…,"host_tokens":0,"open":true,"pow_bits":17,…}
curl -s 127.0.0.1:8080/healthz    # ok
```

### As a systemd service

[`deploy/systemd/tandem-relay.service`](../deploy/systemd/tandem-relay.service)
runs it open, as a throwaway user with no capability, a read-only system
and a system-call filter:

```sh
sudo install -m 0755 tandem-relay /usr/local/bin/tandem-relay
sudo install -m 0644 deploy/systemd/tandem-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tandem-relay
journalctl -u tandem-relay -f        # JSON lines; "relay up" first
```

The unit binds health and metrics to `127.0.0.1:8080`: change `--http` only
if a Prometheus on another machine must scrape it, and then firewall it.
Host tokens (optional) come with a drop-in that gives the service its own
read-only copy of the token file (`LoadCredential`, systemd 247 or newer):
the unit's header has it.

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

For arm64 nodes, `task relay:image:arm64` (`nix-build -A relayImageArm64
-o result-relay-image-arm64`) builds the same image cross-compiled (the
binary is static Go; Nix's binary cache has the cross toolchain). Push both
under one multi-arch tag:

```sh
podman manifest create tandem-relay:0.1.0
podman manifest add tandem-relay:0.1.0 docker-archive:result-relay-image
podman manifest add tandem-relay:0.1.0 docker-archive:result-relay-image-arm64
podman manifest push --all tandem-relay:0.1.0 \
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
overlay provides the namespace, the image's registry, each replica's
public address and the peers list naming them, and (optionally) the token
Secret.

```yaml
# overlays/prod/kustomization.yaml
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: tandem
resources:
  - ../../base
  - namespace.yaml # kind: Namespace, name: tandem
  # optional (the relay is open without it):
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

The Secret is optional (the relay is open without it; the volume is
`optional: true`). It is named `tandem-relay-host-tokens`, with one key,
`host-tokens`, holding the token file; for a relay closed to token holders,
add `--require-token` (or `TANDEM_RELAY_REQUIRE_TOKEN=on`) in the overlay.
Keep it out of git in clear: SOPS,
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

Both of the last two, with one StatefulSet per replica, are the edge
overlay: [Relay pods behind kernel forwarding](#relay-pods-behind-kernel-forwarding).

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

### Host IDs

- From this release the relay also keeps **host IDs** (one-click rejoin,
  the Join list's online badge: [the design](architecture/relay.md#host-ids)).
  Nothing to configure: a binding lives a minute after its host's last
  word, 4096 at most (4 new per source), and lookups by host ID have
  their own per-source limit (16, then one per 2 s), apart from the
  code lookups'. In a fleet a host ID's binding lives on its owner
  replica (from the ID's bits, as a code's), so replicas and regions
  need the same `--peers` and `--regions` as for codes. An older relay
  answers none of it: the Join list then shows no state, and a recent
  host joins by code as before.

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

### Behind kgateway

kgateway can front the relay's **HTTP side only**; players keep reaching
the UDP ports through the base's LoadBalancers. Why: the relay knows
every player by the address its datagrams come from (who registered,
the per-source lookup limits, the connection check, hole punching). A UDP
proxy in front hides that address unless it says it in a PROXY protocol
header, and Envoy's UDP proxy (what a kgateway UDP listener would be)
cannot add one; kgateway has no UDP routes at all yet (2.5.2, November
2026). Details and sources: [architecture/relay.md](architecture/relay.md#behind-a-gateway-kgateway).

[`deploy/kubernetes/kgateway`](../deploy/kubernetes/kgateway) is the base
plus:

- a Gateway (`gatewayClassName: kgateway`, one HTTP listener on 8080,
  hostname `relay-admin.example.org`: set yours) and an HTTPRoute:
  `/healthz`, `/readyz`, `/metrics` answered by any ready replica,
  `/replica-N/healthz|readyz|metrics` by replica N itself (also while it
  drains, so its readiness shows it);
- ClusterIP Services for those (`tandem-relay-http`, `tandem-relay-N-http`:
  add one per replica you add);
- the NetworkPolicy letting the gateway's pods
  (`gateway.networking.k8s.io/gateway-name: tandem-relay-admin`, in the
  relay's namespace) reach port 8080. Check that label on your kgateway's
  proxy pods.

Use it as your overlay's base in place of `../../base`. Keep the Gateway
internal or put authentication in front: the metrics say how busy the
relay is and how many lookups fail. Prometheus should still scrape the
pods (`tandem-relay-pods`): through the gateway, `/metrics` is one
replica's.

**Latency**: none added. Sessions go straight to their replica's
LoadBalancer, as without kgateway; sessions whose hole punching succeeds
do not touch the relay at all. A UDP gateway would have added one hop
inside the cluster, and lost what the relay must see.

When kgateway gets UDP routes, putting the UDP ports behind it still
needs the PROXY protocol on UDP, on every datagram or on each session's
first, from Envoy. Until then, see the alternatives in the architecture
notes (a UDP load balancer that keeps source addresses is what the base
already asks for). Without one: [Relay pods behind kernel forwarding](#relay-pods-behind-kernel-forwarding).

## Relay pods behind kernel forwarding

For a cluster with no UDP load balancer: RPCU (OpenStack, NixOS, FluxCD
and Kubernetes on Hetzner), where Hetzner's cloud load balancers carry no
UDP and nothing else may run on the hypervisors. The relay runs as pods
on **edge nodes**, the nodes that hold the public addresses, and the
kernel of each forwards the relay's UDP ports to its pod. kgateway stays
the cluster's HTTP and TCP entry, on the same addresses (it listens on
TCP, the relay on UDP: no port is shared).

[`deploy/kubernetes/edge`](../deploy/kubernetes/edge) is that setup, a
kustomize component; [`deploy/kubernetes/kgateway-edge`](../deploy/kubernetes/kgateway-edge)
is it over the kgateway overlay, the base of an RPCU overlay.

### Why the kernel, and not kgateway

- **kgateway is Envoy, a proxy in user space**: it reads each datagram on
  a socket of its own and sends a new one from another, so the relay sees
  the proxy's address. The relay knows a player by the address and port
  its datagrams come from (who registered, the per-source limits, the
  connection check, the addresses it hands out for hole punching). A proxy
  could still say who the player was in a PROXY protocol v2 header, but
  Envoy has no PROXY protocol for UDP (closed as not planned), and
  kgateway routes no UDP at all yet ([Behind kgateway](#behind-kgateway)).
- **The kernel rewrites only the destination**: a DNAT (netfilter or
  eBPF) changes where the datagram goes, never who sent it, so the pod
  sees the player's own address and port. Conntrack remembers the
  translation and undoes it on every reply, so the player hears back from
  the address it wrote to (the game drops anything else). Nothing is copied
  to user space on the way: no latency added beyond a lookup in the
  kernel, and no process to scale.

### The two rules that make it or break it

1. **No SNAT or masquerade on the way in.** Anything that rewrites the
   source makes every player one or two internal addresses: kube-proxy or
   Cilium forwarding to another node with `externalTrafficPolicy: Cluster`,
   a `masquerade` rule matching the forwarded packets on a gateway node,
   a proxy. The relay then warns (below).
2. **A symmetric return path.** A reply must leave through the box that
   made the DNAT, so that its conntrack undoes it. A reply that takes
   another way (the pod's node's own default route) leaves with the wrong
   source, the node's or the pod's address, and the player's NAT or the
   network's anti-spoofing drops it.

| Setup                                                          | Rule 1 (no SNAT in)                                                                                        | Rule 2 (symmetric)                                  |
| -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | --------------------------------------------------- |
| **`hostPort`, pod on the node with the address** (the overlay) | the DNAT matches the destination only; portmap masquerades hairpin traffic only (from the node or the pod) | the node is the pod's gateway: replies pass its NAT |
| `NodePort`, `externalTrafficPolicy: Local`, pod on that node   | Local never forwards to another node, so never SNATs                                                       | same node                                           |
| `NodePort` or `LoadBalancer`, `externalTrafficPolicy: Cluster` | **broken**: SNAT to the node on the hop to another                                                         | (symmetric, through the SNAT)                       |
| Cilium `loadBalancer.mode=dsr`, Cluster                        | kept                                                                                                       | replies leave the backend's node directly (below)   |
| Address on another node, DNAT there (below)                    | kept, as long as nothing masquerades the forwarded packets                                                 | only with policy routing on the pod's node          |

**Pinning each replica to the node that holds its address avoids the last
row entirely**: one node, one DNAT, one conntrack, no routing to add.

### hostPort or NodePort

The overlay uses **`hostPort`** (27460 and 27461 on the node, to the same
ports in the pod):

- the same ports on every edge address, the client's defaults: players
  write `relay = relay.example.org` with no port, and the peers list is
  `<address>:27460` for every replica;
- the port is open on the node running the pod only, where a NodePort is
  open on every node (dropped by all but one with Local) and takes ports
  from the cluster-wide range, a pair per replica;
- one DNAT on the path, made by the CNI's portmap plugin or Cilium,
  whatever kube-proxy's mode.

Its costs: Pod Security Admission's `baseline` and `restricted` levels
refuse `hostPort`, so the relay's namespace needs
`pod-security.kubernetes.io/enforce: privileged` (keep `warn` and `audit`
at `restricted`), or a policy engine's exception for ports 27460 and 27461
only (Kyverno, Gatekeeper); and one replica per node (the pinning gives
that anyway).

**`NodePort` with `externalTrafficPolicy: Local`** keeps the pod at
`restricted`. In the overlay, remove the hostPorts and add a NodePort
Service per replica, the pair of ports its own (`30460 + 2N` and the next,
so the connection check's port + 1 is the replica's entrance):

```yaml
# kustomization.yaml (also: resources: [nodeports.yaml])
patches:
  - target: { kind: StatefulSet, labelSelector: tandem.dev/relay-replica }
    patch: |
      - op: remove
        path: /spec/template/spec/containers/0/ports/0/hostPort
      - op: remove
        path: /spec/template/spec/containers/0/ports/1/hostPort
---
# nodeports.yaml: replica 0 (replica 1: pod-index "1", 30462 and 30463)
apiVersion: v1
kind: Service
metadata:
  name: tandem-relay-0-udp
spec:
  type: NodePort
  externalTrafficPolicy: Local
  selector:
    apps.kubernetes.io/pod-index: "0"
  ports:
    - {
        name: game,
        port: 27460,
        targetPort: game,
        nodePort: 30460,
        protocol: UDP,
      }
    - {
        name: front,
        port: 27461,
        targetPort: front,
        nodePort: 30461,
        protocol: UDP,
      }
```

The peers are then `<node 0>:30460,<node 1>:30462`, and players write the
port: `relay = relay.example.org:30460` (or widen the API server's
`--service-node-port-range` to take 27460).

### The edge overlay, step by step (RPCU)

1. **Choose the edge nodes**: one per replica, each holding a public
   address (its own, a Hetzner failover or additional IP routed to it, or
   an OpenStack floating IP whose fixed address is the node's). The node
   kgateway listens on is a good replica 0.
2. **Label them** (and, to keep other workloads off, taint them; the relay
   tolerates `tandem.dev/relay-edge`). Exactly one node per value: the
   replica goes to the node with its number.

   ```sh
   kubectl label node edge-a tandem.dev/relay-edge=0
   kubectl label node edge-b tandem.dev/relay-edge=1
   ```

3. **Fill in the peers** with those nodes' public addresses, in replica
   order, in your overlay over `kgateway-edge` (namespace, image and host
   tokens as for the base):

   ```yaml
   resources:
     - ../../kgateway-edge
   configMapGenerator:
     - name: tandem-relay-peers
       behavior: replace
       literals:
         - peers=203.0.113.10:27460,203.0.113.11:27460
   ```

4. **Open UDP 27460 and 27461** to those nodes, and nothing else new: the
   OpenStack security group of their ports, and the nodes' firewall if it
   filters forwarded packets (a hostPort's datagrams are DNATed and
   forwarded to the pod, not delivered to the node: NixOS's firewall sees
   them only with `networking.firewall.filterForward`). The base's
   NetworkPolicy already lets UDP 27460 and 27461 in from anywhere; a DNAT
   keeps the player's address, so the policy sees it as it is.
5. **Let the namespace run hostPort** (above), and check that the CNI
   provides it (below).
6. **Name the entrance** in DNS ([DNS](#dns-dnsmasq)), commit, let Flux
   apply it, and [verify](#verify-the-path).

What the overlay does: one StatefulSet per replica (`tandem-relay-0`,
`tandem-relay-1`: a StatefulSet has one pod template, so its pods cannot
each be pinned to a node of their own), each of one pod whose ordinal is
the replica's (`spec.ordinals.start`, Kubernetes 1.31 or newer), so the
`apps.kubernetes.io/pod-index` label, the shard and every Service that
selects it stay as in the base; a required node affinity on
`tandem.dev/relay-edge: "N"`; the base's UDP LoadBalancers removed. Each
edge address is a replica and an entrance at once: port 27460 takes the
replica's sessions, and 27461 (its entrance, which only redirects) is the
connection check's second port. Another replica: copy
`deploy/kubernetes/edge/replica-1` to `replica-2`, change the numbers, add
it to the component's `resources`, label a third node, and add its address
to the peers.

A node down takes its replica with it until it is back (the pod cannot
move: its address is the node's); the others go on, and players whose
`relay =` lists several edges register on one that answers.

### kube-proxy: iptables and IPVS

- **hostPort** is not kube-proxy's: the CNI makes it (portmap or its
  own). kube-proxy matters for NodePort and externalIPs.
- **iptables mode**: a NodePort with Local jumps to the node's own
  endpoints only and never marks the packet for masquerade; a node with no
  endpoint drops it. With Cluster, `KUBE-MARK-MASQ` marks what comes from
  outside and `KUBE-POSTROUTING` masquerades it: rule 1 broken. Look:
  `iptables -t nat -S | grep -e 27460 -e 30460`.
- **IPVS mode**: the same policy semantics (Local: local real servers
  only, no masquerade; Cluster: masqueraded through the same
  `KUBE-MARK-MASQ`). `ipvsadm -Ln -u` lists the forwarding method as
  `Masq`: that is IPVS's name for its DNAT, not a source rewrite.
- **conntrack**: kube-proxy deletes the UDP conntrack entries of a
  Service's removed endpoints, so a replaced pod is reached at once.

### Cilium (eBPF)

- With kube-proxy replacement, Cilium makes NodePort, externalIPs and
  hostPort in eBPF on the receiving node (`hostPort.enabled`, on with
  `kubeProxyReplacement=true`; without the replacement, chain the portmap
  plugin, `cni.chainingMode=portmap`). hostPort and NodePort with Local
  keep the source.
- **`loadBalancer.mode`**: `snat` (the default) SNATs Cluster traffic
  forwarded to another node: rule 1 broken. `hybrid` is DSR for TCP and
  **SNAT for UDP**: broken for the relay. `dsr` keeps the source, and the
  backend's node answers the player directly with the first node's address
  as source: rule 2 then rests on the network letting a node send from an
  address that is not its own, which OpenStack's port security (and most
  clouds' anti-spoofing) forbids unless the ports have that address in
  `allowed_address_pairs`. Prefer hostPort or Local.
- Masquerading (`enableIPv4Masquerade`, `bpf.masquerade`, the ip-masq
  agent) applies to connections pods open to the outside, not to replies on
  a flow a player opened (those are reverse-NATed): leave it as it is.
- BPF host routing (the default with eBPF and kube-proxy replacement)
  sends pod traffic past the host's netfilter and `ip rule`s: the policy
  routing below needs `bpf.hostLegacyRouting=true` on that node. Another
  reason to pin.
- Look: `kubectl -n kube-system exec ds/cilium -- cilium-dbg service list`
  (the hostPort and NodePort entries), `cilium-dbg bpf ct list global | grep 27460`,
  `cilium-dbg bpf nat list | grep 27460` (an entry there for a player's
  flow is an SNAT).

### The CNI portmap plugin

Flannel, Calico with its iptables data plane, the bridge plugin, and Cilium
in chaining mode make hostPort with portmap (`{"type": "portmap",
"capabilities": {"portMappings": true}}` in the CNI configuration):

- `CNI-HOSTPORT-DNAT` DNATs the port for any address of the node
  (`--dst-type LOCAL`), including the fixed address behind an OpenStack
  floating IP;
- `CNI-HOSTPORT-MASQ` masquerades only hairpin traffic (from the node
  itself or the pod's own network, `snat: true`), never a player's.
- A UDP conntrack entry lives while datagrams keep coming, and the games
  send every second: an entry made for a pod that was replaced would keep
  the player's flow on the old pod's address. Check it once (replace a
  pod, then `conntrack -L -p udp --orig-port-dst 27460` on its node must
  show the new pod's address); if it does not, the restart hold cannot
  work there (sessions start over after 20 s) and
  `conntrack -D -p udp --orig-port-dst 27460` on the node fixes it at once.
- Some eBPF data planes do not do hostPort (Calico's eBPF mode has had
  this limitation): then NodePort with Local.

Which CNI runs: `kubectl -n kube-system get ds`.

### The public address on another node

When the address is on a node that cannot run the replica (a Hetzner
floating IP on the gateway node, G, and the pod on node R), G forwards the
UDP ports to R with a DNAT, and R sends the replies back through G. Values
below: the public address 203.0.113.10 on G, G's private address 10.0.0.11,
R's 10.0.0.21; R runs the replica with its hostPorts.

On **G**, DNAT only (no masquerade: check that no rule of G's, such as
NixOS's `networking.nat`, matches the forwarded packets on their way out),
with forwarding on (`net.ipv4.ip_forward = 1`):

```
table ip tandem-relay {
  chain prerouting {
    type nat hook prerouting priority dstnat; policy accept;
    ip daddr 203.0.113.10 udp dport { 27460, 27461 } dnat to 10.0.0.21
  }
  chain forward {
    type filter hook forward priority filter; policy accept;
    ip daddr 10.0.0.21 udp dport { 27460, 27461 } accept
    ip saddr 10.0.0.21 udp sport { 27460, 27461 } accept
  }
}
```

On **R**, remember on the connection that it came through G, put the mark
on its replies, and route marked packets back via G (table 2746, mark
0x2746):

```
table ip tandem-relay {
  chain prerouting {
    type filter hook prerouting priority mangle; policy accept;
    # Players reach 10.0.0.21 on these ports only through G.
    ip daddr 10.0.0.21 udp dport { 27460, 27461 } ct state new ct mark set 0x2746
    # The pod's replies on those connections: route them by the mark.
    ct direction reply ct mark 0x2746 meta mark set 0x2746
  }
}
```

```sh
ip rule add fwmark 0x2746 lookup 2746 priority 100
ip route add default via 10.0.0.11 table 2746
sysctl -w net.ipv4.conf.all.rp_filter=2   # if R's default route is on another interface
```

(On NixOS: `networking.nftables.tables` for the tables, and
`systemd.network` routing policy rules and routes for the last two.) The
reply leaves R towards G with R's address as source (portmap undoes its own
DNAT), and G's conntrack gives it the public address back.

- **Anti-spoofing**: G sends the players' datagrams to R with their own
  source, not G's. OpenStack's port security drops that unless G's port
  has `allowed_address_pairs` covering it (`openstack port set
--allowed-address ip-address=0.0.0.0/0 <G's port>`) or port security is
  off on it; a private layer-2 network (a Hetzner vSwitch) carries it as
  is. Or join G and R with a tunnel (WireGuard, GRE: the outer source is
  G), DNAT to R's tunnel address and match `iifname` of the tunnel on R.
- **One public address for two replicas** works the same way with ports:
  peers `203.0.113.10:27460,203.0.113.10:27462`, replica 0 on G itself
  (hostPort), and on G `udp dport 27462 dnat to 10.0.0.21:27460` and
  `udp dport 27463 dnat to 10.0.0.21:27461`.
- With Cilium's BPF host routing, see above.

Pinning the replica on G instead (label G `tandem.dev/relay-edge=0`) needs
none of this.

### DNS (dnsmasq)

DNS only names the addresses: dnsmasq answers the game's lookup, and never
carries a datagram of the relay, so the routing stays the DNAT above
(hostPort, NodePort Local). The game resolves each name of `relay =` once,
as its relay link starts, IPv4 only (`getaddrinfo`, the first answer), and
talks to that address from then on: a changed record reaches a game at its
next link. The relay's own `--peers` and `--regions` stay addresses
(redirects carry addresses, and the relay makes no DNS query: no egress).

```
# /etc/dnsmasq.d/tandem-relay.conf
# The entrance players write (relay = relay.example.org): one A record. An
# AAAA is harmless; the game asks for IPv4 only.
host-record=relay.example.org,203.0.113.10
# Optional, one name per replica: for players who list several edges
# (relay = edge-0.relay.example.org, edge-1.relay.example.org: a host
# takes the one that answers first, so a node down is skipped), and for
# looking at one.
host-record=edge-0.relay.example.org,203.0.113.10
host-record=edge-1.relay.example.org,203.0.113.11
```

A **split horizon** for players on the relay's own network (the inside
dnsmasq answers the private address, the public DNS the public one):

```
# inside: the dnsmasq the LAN uses
host-record=relay.example.org,10.0.0.11
# outside: the zone's public record (or a dnsmasq with auth-zone)
host-record=relay.example.org,203.0.113.10
```

Mind what follows from it: a player inside reaches the relay from a
private address (counted by the warning below when most players do), its
connection check and introductions name that address (no hole punching
with an outside player; the relay carries them), and redirects carry the
peers' public addresses, so inside players must reach those too (the
router's hairpin NAT). A fleet used only inside is simpler with private
peers and `--expect-public-clients=false`.

### Verify the path

1. **On the edge node**, while a player connects:
   `tcpdump -ni any udp port 27460`. The player's public address is the
   source both on the uplink and on the pod's interface (`veth*`, `lxc*`,
   `cali*`), and the replies leave the uplink from the public address (or
   the node's fixed address behind an OpenStack floating IP).
2. **Conntrack**: `conntrack -L -p udp --orig-port-dst 27460`. Each flow
   reads `src=<player> dst=<node> sport=<p> dport=27460 src=<pod> dst=<player>`:
   a reply addressed to the player is no SNAT; a reply addressed to a
   node's address is one. On Cilium, the `cilium-dbg` commands above.
3. **The relay**: its logs name players by their public address and port
   (`"session opened","host":"198.51.100.7:51234"`, `"guest joined"`),
   `/metrics` has `tandem_relay_source_nat_suspected 0`, and no
   `client addresses look NATed` warning appears.
4. **The game**: the Co-op menu's connection check (and the plugin's log,
   `relay: connection check: public 198.51.100.7:51234`) shows the
   player's own public address, the one a "what is my IP" page shows.

### When the relay says the addresses look NATed

The relay looks at the addresses it is reached from: the players
registered, and the sources of registrations, code lookups and probes
(each source once a minute, at most 1024: never per packet). When at least
4 are there and most are private, loopback, link-local or in
100.64.0.0/10 (or in `--internal-cidrs`), or when at least 8 come from
one or two addresses, it logs once a minute

```json
{
  "level": "WARN",
  "msg": "client addresses look NATed: hole punching and per-player limits will be wrong; see docs/relay.md, \"Relay pods behind kernel forwarding\" (--expect-public-clients=false on a LAN)",
  "reason": "internal",
  "sources": 6,
  "internal": 6,
  "addresses": 1,
  "example": "10.42.0.1"
}
```

and `tandem_relay_source_nat_suspected` is 1 (back to 0 once a minute's
sources look right). `reason`: `internal` (mostly internal addresses: a
masquerade or a proxy; `example` names one, often the node's or the CNI
bridge's), or `few` (everyone from one or two addresses: an SNAT to a
public address). Find the hop with the steps above. On a LAN, or for
tests on 127.0.0.1, run with `--expect-public-clients=false`;
`--internal-cidrs` adds a cluster network outside the private ranges
(`--internal-cidrs=198.18.0.0/15`, or `TANDEM_RELAY_INTERNAL_CIDRS`). With `--proxy-protocol`
the check reads the clients the headers name, never the tunnel agent's
sockets.

### Through playit.gg

[playit.gg](https://playit.gg) tunnels give a relay a public UDP address
without a LoadBalancer, a port forward or a public IP: its agent connects
out to playit and receives the players' datagrams. It says who each player
is in a PROXY protocol v2 header, which the relay reads with
`--proxy-protocol`.

**At playit.gg**, for each replica:

1. Create an agent (Account → Agents → new Docker agent) and keep its
   secret key: one agent per replica, so that each forwards only its own
   replica's tunnel.
2. On that agent, create a tunnel: type **UDP** (generic UDP is free),
   local address `127.0.0.1`, local port `27460`, and **PROXY protocol
   version 2** (playit ignores version 1 on UDP).
3. Note the tunnel's address and port, and resolve its name to an IP once:
   the peers list takes addresses (the relay makes no DNS query).

**The relay** runs `--proxy-protocol` (`TANDEM_RELAY_PROXY_PROTOCOL=on`),
trusting only `--trusted-proxies` (default `127.0.0.1/32,::1/128`: the
agent as a sidecar). With the binary on a home server, run the agent next
to it and add the flag.

**Kubernetes**: [`deploy/kubernetes/playit`](../deploy/kubernetes/playit)
is the base with the agent as a sidecar in each relay pod
(`ghcr.io/playit-cloud/playit-agent:2.0.2`, playit's own image, built from
[playit-cloud/playit-agent](https://github.com/playit-cloud/playit-agent);
pin it by digest), run as the relay's non-root user on emptyDirs, no port
opened; the relay's flags; the peers ConfigMap holding the tunnels'
addresses; no LoadBalancer Services; and a NetworkPolicy that takes nothing
in but metrics and lets the pod out to DNS and the internet (not private
networks), which the agent needs. Provide:

```sh
kubectl -n tandem create secret generic tandem-relay-playit \
  --from-file=agent-0=./agent-0.key --from-file=agent-1=./agent-1.key
```

and replace `peers` in your overlay with the tunnels' `ip:port`, in
replica order. Players write a tunnel's address in `tandem.ini` (or both:
`relay = t0, t1`). There is no fleet entrance through playit (it would be
one more tunnel per replica); a guest asking the wrong replica is
redirected as usual.

**Latency**: relayed traffic takes a detour through playit's network
(player → playit's edge → the agent → the relay, and back), usually a few
to a few tens of milliseconds more than a LoadBalancer in the same region;
free tunnels use playit's anycast network, which playit says is not always
the best route (regional tunnels are a paid option). Sessions whose hole
punching succeeds do not go through the relay at all, so not through
playit either: the relay still sees each player's real address and
introduces them to each other.

**Free tier** (as playit lists it in November 2026, and it changes): 2
agents, a few ports, generic UDP included, a shared address with a port
playit picks. Two replicas fit; more need Premium. Port numbers and the
shared address are playit's: when they change, update the peers list.

**Privacy**: playit carries the game traffic, which is not encrypted, and
sees every player's address, as the relay does. Choose it only if your
players are fine with that, as with the relay's own operator.

**Check it**: `tandem_relay_dropped_total{reason="untrusted_proxy"}` and
`{reason="proxy_header"}` stay at 0 (otherwise something other than the
agent sends to the relay, or the tunnel does not send PROXY v2), and
`tandem_relay_proxy_flows` follows the players connected. The relay logs
`"proxy_protocol":true` on start. A relay restarted under a running agent
keeps serving the agent's existing flows by their socket, but cannot tell
who they are until they reconnect: no connection check or hole punching for
those players, and one lookup budget between them.

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

| Flag                        | Default                                | What                                                                                                                                         |
| --------------------------- | -------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `--udp`                     | `:27460`                               | UDP address players reach                                                                                                                    |
| `--front`                   | none (`TANDEM_RELAY_FRONT`)            | UDP address of the fleet's entrance (redirects only; needs `--peers`)                                                                        |
| `--peers`                   | none (`TANDEM_RELAY_PEERS`)            | every replica's public `ip:port`, in shard order                                                                                             |
| `--shard`                   | `0` (`TANDEM_RELAY_SHARD`)             | this replica's index in `--peers`, or a name ending in it                                                                                    |
| `--readers`                 | CPUs, at most 8                        | sockets reading the UDP port at once (`SO_REUSEPORT`, Linux)                                                                                 |
| `--drain-timeout`           | `60s`                                  | on SIGTERM, how long running sessions may go on before the relay exits                                                                       |
| `--proxy-protocol`          | off (`TANDEM_RELAY_PROXY_PROTOCOL=on`) | behind a tunnel sending PROXY v2 headers (playit.gg): each client's address from them                                                        |
| `--trusted-proxies`         | `127.0.0.1/32,::1/128`                 | with `--proxy-protocol`, the only senders taken (CIDRs)                                                                                      |
| `--http`                    | `:8080`                                | HTTP address for `/healthz`, `/readyz`, `/metrics` and `/debug/vars`                                                                         |
| `--host-tokens`             | `/etc/tandem-relay/host-tokens`        | the token file                                                                                                                               |
| `--max-sessions`            | `256`                                  | sessions open at once; the next host is refused                                                                                              |
| `--idle`                    | `2m`                                   | a client silent this long is forgotten (a session ends with its host)                                                                        |
| `--code-ttl`                | `10m`                                  | how long a join code lives (the host's game renews it)                                                                                       |
| `--code-lookup-burst`       | `5`                                    | join-code lookups one source may make at once; then one per 6 s                                                                              |
| `--max-code-failures`       | `120`                                  | failed lookups per minute from everyone together, for the whole fleet; past it every lookup waits for the next                               |
| `--allow-any-host`          | off                                    | no token check, no proof of work, no open limits: local testing only                                                                         |
| `--require-token`           | off (`TANDEM_RELAY_REQUIRE_TOKEN=on`)  | only hosts with a token (no open mode)                                                                                                       |
| `--pow-bits`                | `17`                                   | open mode: the proof of work asked of a host, in bits (each one doubles its cost)                                                            |
| `--max-open-sessions`       | `192`                                  | open mode: sessions opened with no token, at once                                                                                            |
| `--max-sessions-per-source` | `2`                                    | open mode: sessions one IPv4 address or IPv6 /64 hosts with no token                                                                         |
| `--open-kbps`               | `128`                                  | open mode: kbit/s per guest link and direction                                                                                               |
| `--open-burst-kb`           | `32`                                   | open mode: kB per guest link and direction at once                                                                                           |
| `--expect-public-clients`   | on                                     | warn when client addresses look rewritten on the way ([NATed](#when-the-relay-says-the-addresses-look-nated)); `=false` on a LAN or in tests |
| `--internal-cidrs`          | none (`TANDEM_RELAY_INTERNAL_CIDRS`)   | prefixes counted as internal besides the private, loopback, link-local and 100.64.0.0/10 ones                                                |

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

  | Metric                                    | Type    | What                                                                                                                                                       |
  | ----------------------------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
  | `tandem_relay_sessions`                   | gauge   | open sessions                                                                                                                                              |
  | `tandem_relay_clients`                    | gauge   | registered hosts and guests                                                                                                                                |
  | `tandem_relay_draining`                   | gauge   | 1 while shutting down (no new session)                                                                                                                     |
  | `tandem_relay_source_nat_suspected`       | gauge   | 1 while client addresses look rewritten on the way ([NATed](#when-the-relay-says-the-addresses-look-nated))                                                |
  | `tandem_relay_open_sessions`              | gauge   | sessions opened with no token (open mode)                                                                                                                  |
  | `tandem_relay_challenges_total`           | counter | open mode: proof-of-work challenges sent                                                                                                                   |
  | `tandem_relay_challenges_replayed_total`  | counter | open mode: proofs refused as used before                                                                                                                   |
  | `tandem_relay_proxy_flows`                | gauge   | behind a proxy: its sockets whose client a PROXY header named                                                                                              |
  | `tandem_relay_forwarded_packets_total`    | counter | datagrams forwarded                                                                                                                                        |
  | `tandem_relay_forwarded_bytes_total`      | counter | payload bytes forwarded                                                                                                                                    |
  | `tandem_relay_dropped_total{reason}`      | counter | drops: `malformed`, `bad_token`, `unknown_client`, `rate_limited`, `bandwidth` (open mode), `no_channel`; behind a proxy `untrusted_proxy`, `proxy_header` |
  | `tandem_relay_codes_issued_total`         | counter | join codes issued                                                                                                                                          |
  | `tandem_relay_codes_reclaimed_total`      | counter | codes given back to their host after a restart                                                                                                             |
  | `tandem_relay_code_lookups_total{result}` | counter | code lookups: `found`, `unknown`, `limited`, `redirected` (to the owner)                                                                                   |
  | `tandem_relay_refused_total{reason}`      | counter | registrations refused: `no_session`, `session_full`, `too_many_sessions`, `session_taken`, `draining`, `source_busy`                                       |
  | `tandem_relay_redirects_total{kind}`      | counter | registrations redirected: `host` (a new session), `guest` (a secret)                                                                                       |

  Worth an alert: `bad_token` rising (someone trying to host without a
  token, or a typo in a friend's `tandem.ini`); `unknown` or `limited`
  lookups rising steadily (someone guessing codes); `rate_limited` on a
  normal evening (a limit too low, or a misbehaving client);
  `tandem_relay_sessions` near `--max-sessions`.

- Logs are JSON lines on standard output (`journalctl -u tandem-relay`,
  `kubectl logs`).

## When players say it does not work

What the host's Co-op panel shows, and what it means here:

| The game says                                                         | Look at                                                                                                   |
| --------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| the relay did not answer                                              | DNS name, the UDP port (firewall, port forward, load balancer), is the relay running                      |
| the relay refused our host token (relay_token)                        | the token is not in the file, or the relay was not restarted after adding it (`bad_token`)                |
| no session has this join code (wrong, or expired)                     | the guest's code is old: the host's panel shows the current one                                           |
| too many join attempts: try again in a minute                         | the lookup limits; `limited` in the metrics                                                               |
| the relay has too many sessions open                                  | `--max-sessions`, or (hosts with no token) `--max-open-sessions`                                          |
| the relay hosts as many sessions from your address as it may          | open mode: `--max-sessions-per-source` (2); a host token lifts it; `source_busy`                          |
| the relay went silent: reconnecting                                   | a replica restarting (it comes back within seconds) or the path to it; `kubectl get pods`                 |
| the relay's replicas sent us on twice                                 | the replicas' `--peers` lists differ: give them all the same                                              |
| direct connections never come up, or everyone shares one lookup limit | the relay's `client addresses look NATed` warning: [NATed](#when-the-relay-says-the-addresses-look-nated) |
| the relay is restarting: try again in a moment                        | a relay on its own is draining                                                                            |

From outside, `nc -u -v relay.example.org 27460` cannot tell much (the
relay does not answer strangers, on purpose). The quickest end-to-end test
is the mock player, from a checkout of tandem on two machines:

```sh
task peer -- host --relay relay.example.org --relay-token 5f0c0e…   # logs "join code K7Q-2M9"
task peer -- join --code K7Q-2M9 --relay relay.example.org
```
