# As a guest, grouped: our V uses the host's placed object; the host's game is asked and does it (the mock host says performed), our device change waits for it, a second use is asked again
# shellcheck shell=bash disable=SC2317
# Where: INTERACT_AT (a fixture or "X Y Z", default v-door, testing.md
# "Fixtures": V's own door, a placed device, within 200 m).
ensure join
peer host --mode idle --body female --interact-answer performed
expect 'in the session as player' 30
expect 'devices: following [0-9]+ around us \(guest\)' 30
at "${INTERACT_AT:-v-door}"
# Beside the nearest door (V's own at v-door): the device the uses below take.
cmd device goto door
expect 'devices: at door [0-9a-f]+ [A-Za-z]+: ' 10
cmd story interact look device
expect 'interact look: [A-Za-z]+ .*, placed true' 10
mark
# Our V uses it: asked, and the host's game did it on its copy.
cmd story interact use device
expect 'interact use: [A-Za-z]+ on [A-Za-z]+' 10
expect 'interact: asked the host about [A-Za-z]+ \(' 10
expect_peer "interact: player [0-9]+'s use of [0-9a-f]+: performed" 10
expect 'interact: the host: .* performed' 10
# Our own copy's change was not said: the host's doing is the device's word.
reject 'devices: [0-9a-f]+ [A-Za-z]+ changed here \(.*\): asked of the host \(a quest interaction that was ours\)'
shot interact-guest-pickup
# Performed is asked each time (a switch flips twice).
cmd story interact use device
expect_peer "interact: player [0-9]+'s use of [0-9a-f]+: performed" 10
cmd story interact
expect 'interact: [2-9] asked, 0 waiting; answers performed [2-9]' 10
reject 'interact: no answer about'
reject 'watchdog: game thread silent'
