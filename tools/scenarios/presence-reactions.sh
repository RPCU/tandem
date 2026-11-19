# As a guest of a replayed host: the host's NPCs' hit reactions (impact, stagger, knockdown, ragdoll and its end) play on our proxies
# shellcheck shell=bash disable=SC2317
ensure join
peer host --mode idle --body male --replay recordings/npcs.rec --react-npcs 3
expect 'in the session as player' 30
expect 'puppet 6[0-9]{4}: controller handed to the engine' 30
expect_peer 'npc [0-9]+ plays hit reaction 2' 20
expect 'reaction: proxy [0-9]+ plays the host.s impact' 10
expect 'reaction: proxy [0-9]+ plays the host.s stagger' 10
expect 'reaction: proxy [0-9]+ plays the host.s knockdown' 10
expect 'reaction: proxy [0-9]+ plays the host.s ragdoll \(' 10
expect 'reaction: proxy [0-9]+ plays the host.s ragdoll end' 10
cmd presence report
expect 'reaction: sent 0 \(host\), shown [1-9][0-9]* \(guest: twitch [0-9]+, impact [1-9]' 5
# To look at (a person there): each reaction's animation on the proxy, which
# stays where the host has it; the ragdoll, and getting up from it.
reject 'watchdog: game thread silent'
