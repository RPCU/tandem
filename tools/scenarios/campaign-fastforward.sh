# A guest with an older copy of the host's shared campaign: its copy is said to the host, the host's is ahead (a fast-forward), the guest is told it can continue from it
# shellcheck shell=bash disable=SC2317,SC2154
ensure join campaign=on
lineage="$dir/red4ext/plugins/tandem/campaigns/lineage"
# Our copy a1 of campaign ca (generation 1), as an earlier play left it.
record=$'tandem-campaign 1\ncampaign = 00000000000000ca\ncopy = 00000000000000a1\ngeneration = 1\nancestors = 00000000000000a0\nsaved = 1795000000\nwho = V\nquest = The Pickup\n'
printf '%s' "$record" | remote "mkdir -p '$lineage' && cat > '$lineage/00000000000000ca-00000000000000a1.txt'"
# The host plays b2, a child of our a1.
host_world recordings/npcs.rec --mode idle --campaign ca:b2:2:a1,a0 --campaign-quest 'The Heist'
expect 'in the session as player' 30
expect 'campaign: .* \(player 1\) has copy 00000000000000b2 \(generation 2\) of campaign 00000000000000ca' 20
expect "campaign: the session's copies: one ahead \(1 head\(s\)\); ours is behind" 20
expect 'coop notice: Campaign: .* is newer than your copy.*\[Campaign-Newer\]' 20
expect_peer 'campaign: .* \(player [0-9]+\) has copy a1 of campaign ca, generation 1, after "The Pickup": ours ahead' 20
cmd campaign
expect 'campaign: verdict 2, 1 head\(s\), ours behind' 10
expect 'campaign: head 0: .* generation 2' 10
cmd campaign list
expect 'campaign: [0-9]+ archive\(s\) in ' 10
pkill -x tandem-peer
expect 'session ended' 60
# What the test left: our record, and the host's copy kept as another's.
remote "rm -f '$lineage'/00000000000000ca-*.txt"
reject 'watchdog: game thread silent'
