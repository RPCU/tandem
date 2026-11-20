# As a guest: a placed NPC the host kills alone, far from us, is gone from our game too (our own copy hidden, in the session's ledger), and a body lies where it fell when we come by
# shellcheck shell=bash disable=SC2317
# Where: LEDGER_AT ("X Y Z", default V's flat: a placed vendor of our own
# world within 200 m of it stands for "a placed NPC"). The mock host stands
# 300 m east of it (unlinked: it never sees ours, we never see its own) with
# an NPC of that record and entity, and kills it. LEDGER_AWAY ("X Y Z", a
# spot our V can stand on, 200 m or more from the vendor): the host stands
# there instead, and we go there afterwards to see the body.
ensure join
cmd teleport "${LEDGER_AT:-home}"
pause 4
cmd vendor near
expect 'vendor: near .* record [^ ]+ entity [0-9a-f]+ at -?[0-9.]+ -?[0-9.]+ -?[0-9.]+' 10
line=$(since | grep -oE 'vendor: near .* record [^ ]+ entity [0-9a-f]+ at -?[0-9.]+ -?[0-9.]+ -?[0-9.]+' | tail -1)
note "$line"
record=$(sed -E 's/.* record ([^ ]+) entity .*/\1/' <<<"$line")
entity=$(sed -E 's/.* entity ([0-9a-f]+) at .*/\1/' <<<"$line")
read -r x y z <<<"$(sed -E 's/.* at (-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+)$/\1 \2 \3/' <<<"$line")"
cmd vendor goto
expect 'vendor: near ' 10
if [ -n "${LEDGER_AWAY:-}" ]; then
  read -r ax ay az <<<"$LEDGER_AWAY"
else
  ax=$(awk -v x="$x" 'BEGIN { print x + 300 }')
  ay=$y
  az=$z
fi
peer host --mode region --region-at "$ax,$ay,$az" --region-far 0 --region-walk-at -1 \
  --region-npcs 1 --region-record "$record" --region-placed "$entity" --region-kill-at 25
expect 'in the session as player' 30
expect 'regions: linked with nobody' 20
# Ours is alive and ours to run until the host's dies.
reject 'ledger: placed npc .* our copy is kept out'
expect_peer 'region: our npc 1 died \(killer: player 1\)' 40
expect "ledger: placed npc .* died in player 1's game" 15
expect 'ledger: placed npc .* our copy is kept out of our world' 10
cmd ledger
expect 'ledger: on, bodies (on|off), [1-9][0-9]* death\(s\) within 200 m|ledger: on, .* 1 of our copies kept out' 10
shot ledger-guest-gone
if [ -n "${LEDGER_AWAY:-}" ]; then
  # Where it fell in the host's game: a body, laid down, nobody's kill.
  cmd teleport "$LEDGER_AWAY"
  expect 'ledger: a body for placed npc' 20
  pause 3
  cmd ledger
  expect 'ledger: .* 1 bodies shown \(1 laid so far\)' 10
  shot ledger-guest-body
fi
reject 'watchdog: game thread silent'
