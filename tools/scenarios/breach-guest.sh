# A guest's breach of the host's network: run here from its puppet, breached once for everyone
# shellcheck shell=bash disable=SC2317
# Where: BREACH_AT ("X Y Z": an access point within 40 m, with cameras and
# NPCs on its network; V's flat has none, so name one).
# To look at: the network's cameras go dark and its NPCs show broken ICE
# (scanner), as if V had breached it; no eddies for V (the guest's).
ensure host
cmd teleport "${BREACH_AT:-home}"
pause 4
mark
cmd device near ap
expect 'devices: nearest ap [0-9a-f]+ AccessPointControllerPS at ' 10
ap=$(since | grep -oE 'devices: nearest ap [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "access point $ap"
shot breach-guest-before
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male \
  --breach "$ap@3" --programs NetworkCameraShutdown,NetworkLowerICEMedium,NetworkDataMineLootAll
expect 'joined \(player [0-9]+' 20
expect_peer "breached network $ap npc 0 with 3 daemon\(s\) \(--breach\)" 15
# The host grants it and runs it here, from the guest's puppet: not the loot.
expect_peer "our breach of network $ap npc 0: granted, by player [0-9]+" 10
expect "breach: player [0-9]+'s won breach of $ap: [1-9][0-9]* run here \(MinigameAction.NetworkCameraShutdown,MinigameAction.NetworkLowerICEMedium\)" 10
expect_peer "device $ap \(0x[0-9a-f]+\): on, exposed\+breached, value -1 by player [0-9]+, played" 10
pause 3
shot breach-guest-after
# Our own V cannot breach it again: the host refuses, and says who was first.
cmd breach ap NetworkLowerICEMajor
expect "breach: the host refuses ours of network $ap: breached first by player [0-9]+" 10
expect "breach: ours of $ap pays nothing \(hold [0-9]+\): player [0-9]+ was first" 5
reject 'breach: .*has no state in our world'
reject 'watchdog: game thread silent'
