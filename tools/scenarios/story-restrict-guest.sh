# As a guest beside the host: its story's restrictions (weapons away, no jump) come onto our V, and go when it lifts them
# shellcheck shell=bash disable=SC2317
ensure join
# front: the host stands 3 m in front of our V, so we share its moment.
peer host --mode front --body male --restrict GameplayRestriction.NoCombat,GameplayRestriction.NoJump --restrict-at 4 --restrict-for 12
expect 'in the session as player' 30
expect 'story: the host has 2 story restriction\(s\) on' 15
expect 'story: in the host.s story moment' 5
expect 'story: GameplayRestriction.NoCombat on, with the host.s moment' 5
expect 'story: GameplayRestriction.NoJump on, with the host.s moment' 5
expect 'story: the host has 0 story restriction\(s\) on' 20
expect 'story: GameplayRestriction.NoCombat off' 5
expect 'story: GameplayRestriction.NoJump off' 5
reject 'story: refused a guest.s hit'
reject 'watchdog: game thread silent'
# A human checks once: "tandem-peer's scene: weapons away." on screen, V's
# weapon holstered and the wheel blocked while it lasts, the jump refused,
# and both back after the lift.
