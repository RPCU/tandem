# As host: a wanted guest is chased by a police car of ours with its crew (at five stars, the MaxTac AV too), hostile to its puppet and not to our V, whose health stays whole; they stand down with its stars
# shellcheck shell=bash disable=SC2317,SC2154
# Needs roads near V that the spawn system can bring a car on: from the
# test field it found none (2026-11-21: "no place for a car out of player
# N's sight (0 found)", units on foot only). POLICE_AT (a fixture or "X Y Z
# [YAW]", default drake-ave, testing.md "Fixtures") is a street.
ensure host
# Cars and the AV are off by default until proven in game: on for this test.
cmd police cars on
expect 'police: cars and the AV for the others on' 5
at "${POLICE_AT:-drake-ave}"
# Our V's health now, and the watch of its lowest from here: police sent
# for the guest never take our V on (Police.reds, Spare).
query before health
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --heat 3 --heat-at 8 --heat-for 80
expect 'joined \(player [0-9]+' 20
expect 'police: player [0-9]+ wanted level 0 -> 3' 30
# A place on the roads around the puppet, out of its sight; the game's own
# chase vehicle with its crew, sent after the puppet.
expect 'police: a place for a car asked for player [0-9]+' 30
expect 'police: car .* asked for player [0-9]+' 40
expect 'police: car .* chasing player [0-9]+' 30
cmd police report
expect 'police report: our wanted level 0 .* \(0 on our V\), [1-9] car\(s\)' 10
shot police-host-car
# Our own V is still clean.
pause 15
reject 'police: our wanted level [1-5] '
query wanted wanted
expect_value "$wanted" = 0
shot police-host-car-arrived
# The guest's stars go: the car drives off and is taken away.
expect 'player [0-9]+: wanted level 0 ' 90
expect 'police: car for player [0-9]+ stands down' 10
expect 'police: car .* for player [0-9]+ (taken away|gone|destroyed)' 40
# Five stars: the MaxTac AV, one for that V, near its body.
mark
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female --heat 5 --heat-at 5 --heat-for 70
expect 'police: player [0-9]+ wanted level 0 -> 5' 40
expect 'police: AV .* asked for player [0-9]+' 30
expect 'police: AV .* out for player [0-9]+' 60
shot police-host-av
expect 'player [0-9]+: wanted level 0 ' 90
expect 'police: AV (for player [0-9]+ stands down|[^ ]+ for player [0-9]+ gone)' 10
reject 'police: our wanted level [1-5] '
query wanted wanted
expect_value "$wanted" = 0
# Units, cars and MaxTac hunted the guest: not one hit on our V.
query low health low
expect_changed "$before" "$low" same
reject 'police: our V attacked a unit'
reject 'watchdog: game thread silent'
