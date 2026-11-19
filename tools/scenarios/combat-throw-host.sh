# As the host, a guest's grenades fly on our game: a copy launched from their puppet that explodes where theirs did and hurts nobody
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --throw 4
expect 'puppet [0-9]+: controller handed to the engine' 40
expect_peer 'we throw grenade [0-9]+' 20
expect "throws: player [0-9]+'s grenade [0-9]+ \(Items.GrenadeFragRegular\) launched from their puppet" 10
expect_peer 'our grenade [0-9]+ explodes' 10
expect "throws: player [0-9]+'s throw [0-9]+ ended here \(0\)" 10
# Its explosion's hits come from a remote V's puppet: never applied here.
reject 'outcomes: [0-9]+ struck by player'
reject "not shown: no thrower or item"
shot combat-throw-host
reject 'watchdog: game thread silent'
