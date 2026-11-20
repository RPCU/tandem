# As a guest, grouped: the host's game says our use is its own to do (the mock host says host-only); the notice names the host, the second use is refused here without asking, and the prompt is greyed
# shellcheck shell=bash disable=SC2317
# Where: INTERACT_AT ("X Y Z", default V's flat).
ensure join
peer host --mode idle --body male --interact-answer host-only
expect 'in the session as player' 30
expect 'devices: following [0-9]+ around us \(guest\)' 30
cmd teleport "${INTERACT_AT:-home}"
pause 4
mark
cmd story interact use device
expect 'interact: asked the host about ' 10
expect_peer "interact: player [0-9]+'s use of [0-9a-f]+: host only" 10
expect 'interact: the host: .* host only' 10
expect 'coop notice: .* must do this \[Interact-HostOnly\]' 10
# Known now: refused here at once, the host not asked again.
cmd story interact use device
expect 'interact: .* is the host.s to do' 10
pause 2
cmd story interact
expect 'interact: 1 asked, 0 waiting; answers performed 0, host only 1' 10
shot interact-guest-hostonly
reject 'interact: no answer about'
reject 'watchdog: game thread silent'
