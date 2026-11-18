#!/usr/bin/env bash
# Sessions through a real tandem-relay process on 127.0.0.1: the "relay"
# suite of tandem-tests (given the relay in TANDEM_RELAY), then the mock
# player hosting on the relay and a second one joining by the code the first
# logged, as players would.
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

# Waits up to 5 s for sed's EXPRESSION to print something from FILE; prints
# its first line.
wait_for() {
  local file=$1 expression=$2 found=""
  for _ in $(seq 50); do
    found=$(sed -n "$expression" "$file" | head -n 1)
    [ -n "$found" ] && break
    sleep 0.1
  done
  printf '%s' "$found"
}

(cd "$source" && "$go" build -o "$work/tandem-relay" ./cmd/tandem-relay)
# Open to any host, and more code lookups than the tests make from 127.0.0.1.
"$work/tandem-relay" serve --allow-any-host --udp 127.0.0.1:0 --http 127.0.0.1:0 \
  --code-lookup-burst 50 >"$work/relay.log" 2>&1 &
relay=$(wait_for "$work/relay.log" 's/.*"msg":"relay up".*"udp":"\([^"]*\)".*/\1/p')
if [ -z "$relay" ]; then
  echo "the relay did not start:" >&2
  cat "$work/relay.log" >&2
  exit 1
fi
echo "relay on $relay"

TANDEM_RELAY=$relay "$tests" --test-suite=relay

port=$((42000 + $$ % 2000))
"$peer" host --mode idle --port "$port" --relay "$relay" >"$work/host.log" 2>&1 &
code=$(wait_for "$work/host.log" 's/.*join code \([0-9A-Z-]*\) .*/\1/p')
if [ -z "$code" ]; then
  echo "tandem-peer host: no join code" >&2
  cat "$work/host.log" >&2
  exit 1
fi
"$peer" join --code "$code" --relay "$relay" --mode idle >"$work/guest.log" 2>&1 &
joined=$(wait_for "$work/guest.log" 's/.*\(in the session\).*/\1/p')
if [ -z "$joined" ]; then
  echo "tandem-peer join --code $code: not in the session" >&2
  cat "$work/host.log" "$work/guest.log" >&2
  exit 1
fi
echo "tandem-peer joined by code $code through the relay"
