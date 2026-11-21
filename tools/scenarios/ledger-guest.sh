# As a guest: a placed NPC the host kills alone, far from us, is gone from our game too (our own copy hidden, in the session's ledger), and a body lies where it fell when we come by
# shellcheck shell=bash disable=SC2317
# Where: LEDGER_AT (a fixture or "X Y Z", testing.md "Fixtures": the
# nearest placed NPC of our own world within 200 m of it, not a quest's,
# stands for "a placed NPC": `ledger near`; its entity is the id every game
# gives it, TandemPlaced.IdOf: no NPC there had a static id of its own,
# 2026-11-21). Default: the first of vik-clinic, kabuki-market and
# h11-basement that has one (each spot's count is noted). The mock host stands
# 300 m east of it (unlinked: it never sees ours, we never see its own) with
# an NPC of that record and entity, and kills it. LEDGER_AWAY ("X Y Z", a
# spot our V can stand on, 200 m or more from that NPC): the host stands
# there instead, and we go there afterwards to see the body.
ensure join
if [ -n "${LEDGER_AT:-}" ]; then
  spots=("$LEDGER_AT")
else
  spots=(vik-clinic kabuki-market h11-basement)
fi
for where in "${spots[@]}"; do
  at "$where"
  mark
  cmd ledger near
  expect 'ledger: (near|no) placed npc ' 10
  note "$where: $(since | grep -aoE 'ledger: (near|no) placed npc .*' | tail -1)"
  if since | grep -qaE 'ledger: near placed npc record '; then
    break
  fi
done
expect 'ledger: near placed npc record [^ ]+ entity [0-9a-f]+ at -?[0-9.]+ -?[0-9.]+ -?[0-9.]+' 2
line=$(since | grep -oE 'ledger: near placed npc record [^ ]+ entity [0-9a-f]+ at -?[0-9.]+ -?[0-9.]+ -?[0-9.]+' | tail -1)
note "$line"
record=$(sed -E 's/.* record ([^ ]+) entity .*/\1/' <<<"$line")
entity=$(sed -E 's/.* entity ([0-9a-f]+) at .*/\1/' <<<"$line")
read -r x y z <<<"$(sed -E 's/.* at (-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+)$/\1 \2 \3/' <<<"$line")"
cmd ledger goto
expect 'ledger: near placed npc' 10
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
