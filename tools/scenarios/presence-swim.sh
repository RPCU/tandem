# A player swims the course's water (in, strokes on the surface, a dive, back up, out): each is heard on its puppet, and its water stops with it
# shellcheck shell=bash disable=SC2317
ensure host
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode course --body female
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
expect_peer 'course: swim' 60
expect 'audio: player [0-9]+ into the water \(sound w_melee_water_medium_splash\)' 15
expect 'audio: player [0-9]+ swimming stroke 1 \(on the surface' 10
expect_peer 'course: dive' 15
expect 'audio: player [0-9]+ dive \(sound w_bul_hit_water\)' 10
expect 'audio: player [0-9]+ back to the surface \(sound w_bul_hit_water\)' 15
expect 'audio: player [0-9]+ out of the water \(sound w_bul_hit_water\)' 10
cmd presence report
expect 'presence: player [0-9]+: health.*water 0, strokes [1-9]' 5
# Heard and seen (a person there): a splash going in, a lighter one each
# stroke, V's breath coming up; `presence swim fx` / `sound` tell which of
# the two layers the puppet's body carries (docs/architecture/presence.md).
reject 'watchdog: game thread silent'
