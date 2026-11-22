# As a guest of a replayed host over a bad link: proxies spawn (reliable) and follow, all gone when the host leaves
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --latency 120 --jitter 40 --loss 5 --reorder 2
expect 'in the session as player' 40
expect 'npcs: guest, own crowd off' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 40
expect 'puppet 6[0-9]{4}: clips walk' 30
cmd npc count
expect 'npcs shared: [1-9][0-9]*, own hidden'
pause 10
# Timed out under loss (the previous scenario's host saying goodbye, just
# after the mark, is no failure; nor "could not reach the host": that is a
# join that never got in, one the game began before the mock host listened).
reject 'session ended: the host is gone'
# Only what comes after: the previous host's goodbye may be just past the start.
mark
pkill -x tandem-peer
expect 'session ended' 60
cmd npc count
expect 'npcs shared: 0'
reject 'watchdog: game thread silent'
