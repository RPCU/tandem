# The host plays an older copy of the shared campaign than its guest has: both are told, the host that it hosts an older copy
# shellcheck shell=bash disable=SC2317,SC2154
ensure join campaign=on
lineage="$dir/red4ext/plugins/tandem/campaigns/lineage"
# Our copy d3 went on from the host's b2.
record=$'tandem-campaign 1\ncampaign = 00000000000000ca\ncopy = 00000000000000d3\ngeneration = 3\nancestors = 00000000000000b2,00000000000000a1,00000000000000a0\nsaved = 1795000000\nwho = V\nquest = Lucretia My Reflection\n'
printf '%s' "$record" | remote "mkdir -p '$lineage' && cat > '$lineage/00000000000000ca-00000000000000d3.txt'"
host_world recordings/npcs.rec --mode idle --campaign ca:b2:2:a1,a0 --campaign-quest 'The Heist'
expect 'in the session as player' 30
expect "campaign: the session's copies: one ahead \(1 head\(s\)\); the host's is behind" 20
expect 'coop notice: Campaign: your copy is the newest.*\[Campaign-OursNewer\]' 20
expect_peer 'campaign: .* has copy d3 of campaign ca, generation 3, after "Lucretia My Reflection": ours behind' 20
expect_peer "campaign: verdict fast-forward to .*; ours is behind; the host's is behind" 20
pkill -x tandem-peer
expect 'session ended' 60
remote "rm -f '$lineage'/00000000000000ca-*.txt"
reject 'watchdog: game thread silent'
