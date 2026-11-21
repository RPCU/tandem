# As a guest: the mock host's quest blocks fast travel (StoryState's no-fast-travel flag) and no spot beside it fits (a test): our V never respawns far away, it waits 10 s for a spot, then gets up where it fell, with a notice
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode front --body female --no-fast-travel
expect 'in the session as player' 30
expect "story: the host's quest blocks fast travel \(no far respawn\)" 15
pause 3
cmd revive fresh
expect 'revive fresh: no recent revive' 5
cmd revive report
expect "revive report: leaving refused, fast travel blocked \(host's quest\)" 5
cmd revive nospot
expect 'revive nospot: no spot beside a partner fits' 5
cmd revive out
expect 'revive: our V is out \(a test\)' 10
expect_peer 'player [0-9]+ is out' 5
expect "revive: no spot to get up at beside .* yet \(fast travel blocked \(host's quest\)\): 10 s for one" 15
expect 'revive: no spot to get up at beside .* \(a test\)' 15
expect "revive: far respawn refused: fast travel blocked \(host's quest\)" 5
expect 'revive: respawn near \((out of combat|30 s out)\): where we lie' 5
expect 'coop notice: No way out of here: back on your feet where you fell' 5
expect 'revive: our V is up at 40 %' 15
expect 'revive: up: the state machine is past its death state' 10
expect_peer 'player [0-9]+ is up' 10
cmd revive nospot off
expect 'revive nospot: the real spots' 5
reject "respawn at Viktor's clinic"
reject 'respawn at our fast travel point'
reject 'revive: on the way, the fast travel.s loading screen'
# Look for: our V standing where it lay, the mock's marker beside it.
reject 'watchdog: game thread silent'
