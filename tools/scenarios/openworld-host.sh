# As host: our world's done NCPD activities go to the guest: the same set our own map shows done (an explicit none when there are none)
# shellcheck shell=bash disable=SC2317,SC2154
# Unattended: the mock guest logs the set it hears; its count must be the
# count of pins our map shows done, and each of its first five markers must
# be one of our pins, read from the game as done.
ensure host
mark
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body male
expect 'joined \(player [0-9]+' 20
# What our world said (once a session, and kept for a guest that comes
# later: the line may be an earlier scenario's), from the game's report.
pause 5
cmd story activities
expect 'openworld: [0-9]+ NCPD activity pins here, [0-9]+ shown done; .*we said [0-9]+ done' 5
said=$(since | grep -oE 'we said [0-9]+ done' | tail -1 | awk '{ print $3 }')
shown=$(since | grep -oE 'NCPD activity pins here, [0-9]+ shown done' | tail -1 | awk '{ print $5 }')
expect_value "$said" = "$shown"
if [ "$said" = 0 ]; then
  expect_peer 'open-world done: none of player 1' 10
else
  expect_peer "open-world done: $said of player 1:" 10
  heard=$(grep -aoE "open-world done: $said of player 1:( [0-9a-f]+)+" "$peer_log" | tail -1 | cut -d: -f3)
  for hex in $(tr ' ' '\n' <<<"$heard" | grep . | head -5); do
    cmd story activity "$((16#$hex))"
    expect "openworld: activity $((16#$hex)) phase 0 at " 10
  done
fi
reject 'openworld: setting a map pin.s phase faulted'
reject 'watchdog: game thread silent'
