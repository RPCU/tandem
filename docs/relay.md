# Running a tandem-relay

`tandem-relay` lets two players behind NAT play together without a port
forward. It forwards opaque UDP datagrams between a session's host and its
guests, and hands the host a short **join code** to give to friends. It
never parses game traffic and keeps nothing on disk. How it works inside is
in [ARCHITECTURE.md, "The relay"](ARCHITECTURE.md#the-relay); this page is
for whoever runs one.

You need a machine players can reach on **one UDP port**: a small VPS, a
Kubernetes cluster, or a home server with that port forwarded. One relay
serves a group of friends easily (256 sessions at most by default).

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
  live in memory, so a restart ends them; the games notice within seconds,
  register again (hosts every 20 s) and get a new join code, and guests
  already in a session find it again by themselves.
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

Without Nix, any builder that copies the static binary above into an empty
image does the same job; keep it non-root, with no shell.

### The manifests

[`deploy/kubernetes/base`](../deploy/kubernetes/base) is a kustomize base:
a ServiceAccount (no token mounted), a Deployment, two Services and a
NetworkPolicy. It is not deployable alone: an overlay provides the
namespace, the image's registry and the token Secret.

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
```

The Secret is named `tandem-relay-host-tokens`, with one key,
`host-tokens`, holding the token file. Keep it out of git in clear: SOPS,
sealed-secrets or an external secret operator. For a first try:

```sh
kubectl -n tandem create secret generic tandem-relay-host-tokens \
  --from-file=host-tokens=./host-tokens
kubectl -n tandem rollout restart deployment/tandem-relay   # after any change
```

What the base decides, and why:

- **One replica, `Recreate`.** Sessions live in the relay's memory and a
  client is known by its address: two replicas would split a session. A
  rollout ends the running sessions (the games reconnect).
- **A `LoadBalancer` Service on UDP 27460 with
  `externalTrafficPolicy: Local`**, so the relay sees each player's own
  address: it knows clients by address and port and limits each one. Your
  load balancer must support UDP; add its annotations in the overlay. A
  `NodePort` or `hostPort` works too, as long as source addresses are
  preserved.
- **Locked down**: non-root (65532), read-only root filesystem, every
  capability dropped, no privilege escalation, seccomp `RuntimeDefault`.
- **NetworkPolicy**: UDP 27460 in from anywhere; TCP 8080 in only from the
  namespace labelled `kubernetes.io/metadata.name: monitoring` (change it
  in the overlay if your Prometheus lives elsewhere); **no egress at all**:
  the relay only ever answers on a flow a player opened, and needs no DNS.
- **Resources**: 50m CPU and 32Mi requested, 1 CPU and 128Mi at most.

Check a change to the manifests with `task k8s:check` (renders the base and
validates it with kubeconform).

## What to open on a firewall

| Direction | Port          | From                 | Why                                                |
| --------- | ------------- | -------------------- | -------------------------------------------------- |
| in        | **UDP 27460** | anywhere             | players (hosts and guests)                         |
| in        | TCP 8080      | your monitoring only | `/healthz`, `/metrics`; never from the internet    |
| out       | nothing       |                      | replies to players go out on the flows they opened |

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

| Flag                  | Default                         | What                                                                                      |
| --------------------- | ------------------------------- | ----------------------------------------------------------------------------------------- |
| `--udp`               | `:27460`                        | UDP address players reach                                                                 |
| `--http`              | `:8080`                         | HTTP address for `/healthz` and `/metrics`                                                |
| `--host-tokens`       | `/etc/tandem-relay/host-tokens` | the token file                                                                            |
| `--max-sessions`      | `256`                           | sessions open at once; the next host is refused                                           |
| `--idle`              | `2m`                            | a client silent this long is forgotten (a session ends with its host)                     |
| `--code-ttl`          | `10m`                           | how long a join code lives (the host's game renews it)                                    |
| `--code-lookup-burst` | `5`                             | join-code lookups one source may make at once; then one per 6 s                           |
| `--max-code-failures` | `120`                           | failed lookups per minute from everyone together; past it every lookup waits for the next |
| `--allow-any-host`    | off                             | no token check: local testing only                                                        |

Fixed in the code (`relay/internal/relay/relay.go`, `DefaultConfig`):

- 3 guests per session (a session is 4 players);
- each client: 2000 datagrams and 4 MiB per second, the rest is dropped
  (`rate_limited`);
- a source is an IPv4 address or an IPv6 /64; at most 4096 sources are
  tracked for the lookup limits.

What these protect against:

- **Reflection and amplification**: an address that is not registered
  never gets an answer to data, only to a registration or a code lookup,
  which must be padded to 64 bytes while the answer is 21 bytes at most.
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

- `GET /healthz` answers `ok` (the Kubernetes probes use it).
- `GET /metrics` is the Prometheus text format:

  | Metric                                    | Type    | What                                                                            |
  | ----------------------------------------- | ------- | ------------------------------------------------------------------------------- |
  | `tandem_relay_sessions`                   | gauge   | open sessions                                                                   |
  | `tandem_relay_clients`                    | gauge   | registered hosts and guests                                                     |
  | `tandem_relay_forwarded_packets_total`    | counter | datagrams forwarded                                                             |
  | `tandem_relay_forwarded_bytes_total`      | counter | payload bytes forwarded                                                         |
  | `tandem_relay_dropped_total{reason}`      | counter | drops: `malformed`, `bad_token`, `unknown_client`, `rate_limited`, `no_channel` |
  | `tandem_relay_codes_issued_total`         | counter | join codes issued                                                               |
  | `tandem_relay_code_lookups_total{result}` | counter | code lookups: `found`, `unknown`, `limited`                                     |

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

From outside, `nc -u -v relay.example.org 27460` cannot tell much (the
relay does not answer strangers, on purpose). The quickest end-to-end test
is the mock player, from a checkout of tandem on two machines:

```sh
task peer -- host --relay relay.example.org --relay-token 5f0c0e…   # logs "join code K7Q-2M9"
task peer -- join --code K7Q-2M9 --relay relay.example.org
```
