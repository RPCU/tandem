# As a guest: the host's traffic and parked cars shown as proxies, a driver seated, a car that burns and blows up, all gone with the host
# shellcheck shell=bash disable=SC2317
ensure join
host_world - --mode idle --body female --traffic 4 --npc Character.kab_tyger_claws_biker1_ranged1_saratoga_ma --wreck-at 30
expect 'in the session as player' 30
expect 'npcs: guest, own crowd off' 20
expect 'street: vehicle [0-9]+ .* spawning at' 30
expect_peer 'street: 4 cars of .* round player' 20
# Five proxies (four driving round our V, one parked), no car of our own
# world left among them, and the driver in its seat in the first one.
pause 5
cmd street count
expect 'street count: 5 shown as proxies \(1 parked, 0 wrecks' 10
expect 'npcs: proxy [0-9]+ seated in seat_front_left' 30
shot traffic-guest
# The first car burns, then blows up on our screen too: once, the game's way.
expect_peer 'street: vehicle 0x[0-9a-f]+ burns' 40
expect_peer 'blew up: a wreck' 15
expect 'street: vehicle [0-9]+ blows up' 10
pause 3
cmd street count
expect 'street count: 5 shown as proxies \(1 parked, 1 wrecks' 10
shot traffic-guest-wreck
# The host goes: its street goes with it, and none comes back twice.
pkill -x tandem-peer
expect 'session ended' 60
cmd street count
expect 'street count: 0 '
reject 'watchdog: game thread silent'
