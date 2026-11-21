# Two copies of a shared campaign went on separately (a fork): both are shown, the guest chooses its own, the host hears the choice
# shellcheck shell=bash disable=SC2317,SC2154
ensure join campaign=on
lineage="$dir/red4ext/plugins/tandem/campaigns/lineage"
# Our copy c2 went on from a1, as the host's b2 did: neither is the other's.
record=$'tandem-campaign 1\ncampaign = 00000000000000ca\ncopy = 00000000000000c2\ngeneration = 2\nancestors = 00000000000000a1,00000000000000a0\nsaved = 1795000000\nwho = V\nquest = Ghost Town\n'
printf '%s' "$record" | remote "mkdir -p '$lineage' && cat > '$lineage/00000000000000ca-00000000000000c2.txt'"
host_world recordings/npcs.rec --mode idle --campaign ca:b2:2:a1,a0 --campaign-quest 'The Heist'
expect 'in the session as player' 30
expect "campaign: the session's copies: a fork \(2 head\(s\)\)" 20
expect 'coop notice: Campaign: 2 copies went on separately.*\[Campaign-Fork\]' 20
expect_peer 'campaign: .* has copy c2 of campaign ca, generation 2, after "Ghost Town": ours fork' 20
expect_peer 'campaign: verdict fork between ' 20
cmd campaign
expect 'campaign: verdict 3, 2 head\(s\)' 10
expect 'campaign: head 0: .*, ours' 10
# Our own copy goes on: the host's is set aside here, and the host hears it.
cmd campaign choose 0
expect 'campaign: we go on from copy ' 10
expect_peer 'campaign: .* has copy c2 of campaign ca.*; it goes on from ' 20
pkill -x tandem-peer
expect 'session ended' 60
remote "rm -f '$lineage'/00000000000000ca-*.txt"
reject 'watchdog: game thread silent'
