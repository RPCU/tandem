# As a guest: V breaches the host's network (its outcome faked as a minigame-skipping mod does); the host runs it, refuses its own second breach
# shellcheck shell=bash disable=SC2317
# Where: BREACH_AT ("X Y Z": an access point within 40 m; V's flat has none).
# To look at: V's eddies and components arrive a ping after the breach (the
# guest's rewards, paid on the host's grant); the access point's lights go out.
ensure join
cmd teleport "${BREACH_AT:-home}"
pause 4
mark
cmd device near ap
expect 'devices: nearest ap [0-9a-f]+ AccessPointControllerPS at ' 10
ap=$(since | grep -oE 'devices: nearest ap [0-9a-f]+' | tail -1 | awk '{ print $4 }')
note "access point $ap"
# The mock host tries the same network 25 s in: too late.
peer host --mode idle --body female --breach "$ap@25" --programs NetworkLowerICEMajor
expect 'in the session as player' 30
mark
cmd breach ap NetworkCameraShutdown,NetworkLowerICEMedium,NetworkDataMineLootAll
expect "breach: faking a won breach of access point $ap" 5
expect "breach: ours of network $ap won \(MinigameAction.NetworkCameraShutdown,MinigameAction.NetworkLowerICEMedium,MinigameAction.NetworkDataMineLootAll; traps none\): for the host" 10
expect_peer "breach by player [0-9]+: network $ap npc 0, 3 daemon\(s\), 0 trap\(s\): applied to its network and our NPCs" 10
expect "breach: ours of $ap is held for the host's grant \(hold [0-9]+, 1 asked\)" 10
expect "breach: the host grants ours of network $ap" 10
expect "breach: ours of $ap pays \(hold [0-9]+, granted\)" 5
shot breach-host-after
expect_peer "our breach of network $ap npc 0: refused \(breached already\), by player [0-9]+" 40
reject 'breach: the host refuses ours'
reject 'watchdog: game thread silent'
