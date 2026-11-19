#!/usr/bin/env bash
# Sessions through real tandem-relay processes on 127.0.0.1: a fleet of two
# replicas (the first also serving the fleet's entrance), the "relay" suite
# of tandem-tests against it (TANDEM_RELAY: replica 0; TANDEM_RELAY_FLEET,
# TANDEM_RELAY_FRONT; TANDEM_RELAY_BIN for the cases that start and stop a
# relay of their own), then the mock player hosting at the entrance and a
# second one joining at the other replica by the code the first logged, as
# players would; last, the same through a PROXY v2 forwarder
# (relay/internal/proxyfwd, playit.gg's way) in front of a relay run with
# --proxy-protocol.
#
# Usage: relay_loopback.sh GO RELAY_SOURCE TANDEM_TESTS TANDEM_PEER
set -euo pipefail
go=$1 source=$2 tests=$3 peer=$4

work=$(mktemp -d)
cleanup() {
  local pids
  mapfile -t pids < <(jobs -p)
  if ((${#pids[@]})); then
    kill "${pids[@]}" 2>/dev/null || true
  fi
  wait 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

# Waits for sed's EXPRESSION to print something from FILE (up to 15 s: a
# bound for a busy machine, it ends as soon as the line is there); prints
# its first line.
wait_for() {
  local file=$1 expression=$2 found=""
  for _ in $(seq 150); do
    found=$(sed -n "$expression" "$file" | head -n 1)
    [ -n "$found" ] && break
    sleep 0.1
  done
  printf '%s' "$found"
}

(cd "$source" && "$go" build -o "$work/tandem-relay" ./cmd/tandem-relay)

# Fixed ports (the replicas name each other), apart per process, and above
# the ones tandem-tests picks for itself (tests/ports.hpp: 41000 to 61000).
base=$((62000 + ($$ % 800) * 4))
replica0=127.0.0.1:$base replica1=127.0.0.1:$((base + 1)) front=127.0.0.1:$((base + 2))
peers=$replica0,$replica1
# Open to any host, and more code lookups than the tests make from 127.0.0.1.
# A drain of a second: the cleanup's SIGTERM would otherwise wait out the
# sessions left behind (the 60 s default, the bulk of this test's time
# before 2026-11-19: 82 s, now under 10).
for shard in 0 1; do
  udp=$replica0 extra=(--front "$front")
  if [ "$shard" = 1 ]; then
    udp=$replica1 extra=()
  fi
  "$work/tandem-relay" serve --allow-any-host --udp "$udp" --http 127.0.0.1:0 \
    --peers "$peers" --shard "$shard" "${extra[@]}" \
    --code-lookup-burst 50 --drain-timeout 1s >"$work/relay$shard.log" 2>&1 &
  up=$(wait_for "$work/relay$shard.log" 's/.*"msg":"relay up".*"udp":"\([^"]*\)".*/\1/p')
  if [ -z "$up" ]; then
    echo "relay $shard did not start:" >&2
    cat "$work/relay$shard.log" >&2
    exit 1
  fi
done
echo "relays on $peers, entrance $front"

TANDEM_RELAY=$replica0 TANDEM_RELAY_FLEET=$peers TANDEM_RELAY_FRONT=$front \
  TANDEM_RELAY_BIN=$work/tandem-relay "$tests" --test-suite=relay

port=$((42000 + $$ % 2000))
"$peer" host --mode idle --port "$port" --relay "$front" >"$work/host.log" 2>&1 &
code=$(wait_for "$work/host.log" 's/.*join code \([0-9A-Z-]*\) .*/\1/p')
if [ -z "$code" ]; then
  echo "tandem-peer host: no join code" >&2
  cat "$work/host.log" >&2
  exit 1
fi
"$peer" join --code "$code" --relay "$replica1" --mode idle >"$work/guest.log" 2>&1 &
joined=$(wait_for "$work/guest.log" 's/.*\(in the session\).*/\1/p')
if [ -z "$joined" ]; then
  echo "tandem-peer join --code $code: not in the session" >&2
  cat "$work/host.log" "$work/guest.log" "$work/relay0.log" "$work/relay1.log" >&2
  exit 1
fi
echo "tandem-peer hosted at the entrance and joined by code $code at another replica"

# Behind a tunnel that says who its clients are in PROXY v2 headers
# (playit.gg): the relay trusts only the forwarder's address (127.0.0.2),
# which keeps a socket per client and sends the header first on each.
(cd "$source" && "$go" build -o "$work/proxyv2-forward" ./internal/proxyfwd/cmd)
proxied=127.0.0.1:$((base + 3))
"$work/tandem-relay" serve --allow-any-host --udp "$proxied" --http 127.0.0.1:0 \
  --proxy-protocol --trusted-proxies 127.0.0.2/32 >"$work/proxied.log" 2>&1 &
if [ -z "$(wait_for "$work/proxied.log" 's/.*"msg":"relay up".*"proxy_protocol":\(true\).*/\1/p')" ]; then
  echo "the proxied relay did not start:" >&2
  cat "$work/proxied.log" >&2
  exit 1
fi
"$work/proxyv2-forward" 127.0.0.1:0 "$proxied" from=127.0.0.2 >"$work/forward.log" 2>&1 &
tunnel=$(wait_for "$work/forward.log" 's/^forwarding \([^ ]*\) to .*/\1/p')
"$peer" host --mode idle --port "$((port + 1))" --relay "$tunnel" >"$work/phost.log" 2>&1 &
code=$(wait_for "$work/phost.log" 's/.*join code \([0-9A-Z-]*\) .*/\1/p')
"$peer" join --code "$code" --relay "$tunnel" --mode idle >"$work/pguest.log" 2>&1 &
joined=$(wait_for "$work/pguest.log" 's/.*\(in the session\).*/\1/p')
if [ -z "$code" ] || [ -z "$joined" ]; then
  echo "through the PROXY v2 forwarder: not in the session (code '$code')" >&2
  cat "$work/phost.log" "$work/pguest.log" "$work/proxied.log" "$work/forward.log" >&2
  exit 1
fi
echo "tandem-peer hosted and joined by code $code through a PROXY v2 tunnel"
