# Each player's word on their cars reaches the others: the mock host keeps its cars to itself (lend never), ours (tandem.ini lend, crew by default) reaches it
# shellcheck shell=bash disable=SC2317
ensure join
host_world recordings/npcs.rec --mode idle --body female --lend never
expect 'in the session as player' 30
expect_peer 'our cars: lent to nobody' 10
expect_peer 'player [0-9]+ lends its cars: (crew|owner|never)' 30
pause 2
cmd garage
expect 'garage: our cars lent to (crew|owner|never); tandem-peer: never' 10
# Nobody sat at anyone's wheel: nothing refused, nothing followed.
reject 'garage: .*refused'
reject 'garage: our V takes'
reject 'watchdog: game thread silent'
