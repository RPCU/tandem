# A real host's recorded session replayed to our V as a guest: proxies come and go, the story mirrored, no storms, orphans or stalls
# shellcheck shell=bash disable=SC2317,SC2154
ensure join
host_world - --mode idle
if [ "$sessioned" != 1 ]; then
  note 'no recorded session (recordings/session/city.tsession, docs/architecture/recordings.md "A recorded host session"): nothing to replay'
  pause 3
  return 0
fi
seconds=$(build/linux/peer/tandem-peer play-info "recordings/session/${SESSION:-city}.tsession" | sed -n 's/^play: \([0-9]*\).*/\1/p')
expect 'in the session as player' 30
expect_peer 'session: started' 30
expect 'story: joining the host.s story' 30
# Proxies of the host's NPCs come (and, as the host walks on, go).
expect 'puppet 6[0-9]{4}: controller handed to the engine' 120
# The whole session plays out (expect and pause fail on a game thread silent
# for 45 s: longer than any loading screen).
expect_peer 'session: done' $((${seconds:-600} + 120))
pause 10
# Our copy of the host's world never disagreed with it for long, nor kept a
# proxy the host had no NPC for.
at_most 3 'resync: asking the host for'
reject 'consistency: proxy [0-9]+ (has no NPC on the host|was missing)'
# The end of the session takes the host's world away: no proxy left, our own
# story back.
pkill -x tandem-peer
expect 'session ended' 60
expect 'story: leaving the host.s story' 20
cmd npc count
expect 'npcs shared: 0'
