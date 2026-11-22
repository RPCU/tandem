# As a guest, a vendor the host runs is a proxy here: beside it we open our own game's vendor of the same NPC (our stock, our money), the puppet shows us busy in a shop; when the host's vendor dies, ours is hidden and the shop stays shut
# shellcheck shell=bash disable=SC2317
# Where: VENDOR_AT (a fixture or "X Y Z"; testing.md "Fixtures": a vendor of
# our own world within 200 m of it, the nearest is taken), then VENDOR_AWAY
# (kabuki-market), then VENDOR_AT again: the first spot with one. Default:
# vik-clinic (Viktor, a ripperdoc; a food stall 37 m off), kabuki-market,
# vik-clinic. Each spot's NPCs with a vendor id are noted, with what each
# lacks. Ours hidden in an earlier session (behind its owner's proxy, or
# dead there: this scenario's own end, ledger-guest's Viktor, 2026-11-21) is
# on no list: the way to the next spot and back is what brings it back, if
# anything does. The mock host stands in front of us, so its region covers
# ours; its vendor is our own, by record, place and id (TandemVendors.IdOf).
ensure join
spots=("${VENDOR_AT:-vik-clinic}" "${VENDOR_AWAY:-kabuki-market}" "${VENDOR_AT:-vik-clinic}")
for where in "${spots[@]}"; do
  at "$where"
  mark
  cmd vendor near
  expect 'vendor: (near|no vendor of our own world) ' 10
  pause 1
  note "$where: $(since | grep -aoE 'vendor: [0-9]+ npcs within .*' | tail -1)"
  while read -r listed; do note "$listed"; done < <(since | grep -aoE 'vendor:   record .*' | tail -17)
  if since | grep -qa 'vendor: near '; then
    break
  fi
done
expect 'vendor: near .* record [^ ]+ entity [0-9a-f]+ at -?[0-9.]+ -?[0-9.]+ -?[0-9.]+ \(.* placed (true|false),' 2
line=$(since | grep -aE 'vendor: near .* record [^ ]+ entity [0-9a-f]+ at ' | tail -1 | grep -aoE 'vendor: near .*')
note "$line"
record=$(sed -E 's/.* record ([^ ]+) entity .*/\1/' <<<"$line")
entity=$(sed -E 's/.* entity ([0-9a-f]+) at .*/\1/' <<<"$line")
read -r x y z <<<"$(sed -E 's/.* at (-?[0-9.]+) (-?[0-9.]+) (-?[0-9.]+) \(.*/\1 \2 \3/' <<<"$line")"
# A stall's seller is the crowd's: its twin is known by record and place and
# its death is not the ledger's, so the mock host says no NpcPlaced of it.
placed=$(sed -E 's/.* placed (true|false),.*/\1/' <<<"$line")
crowd=()
[ "$placed" = true ] || crowd=(--vendor-crowd)
# Up to it (as far as 200 m, out of a basement: maybe a loading screen), and
# it is the one beside us now.
moved vendor goto
cmd vendor near
expect "vendor: near .* entity $entity at [-0-9. ]+ \([0-9]\.[0-9]+ m," 10
# The host's copy of that vendor, standing where ours stands; it dies 60 s
# after we are in.
peer host --mode front --body female --vendor "$record@$x,$y,$z#$entity" "${crowd[@]}" --vendor-dies 60
expect 'in the session as player' 30
expect_peer "vendor npc [0-9]+: $record at" 20
# Our own vendor gives way to the host's (a twin, or a spawn inside its
# region); its proxy stands in its place.
pause 6
# (Which way, for the notes: "the same placed NPC" is the twin by identity,
# "is a placed NPC another game runs: held back" the spawn that came after.)
note "$(since | grep -E 'regions: our (npc|own) .*(twin of|held back)' | tail -1)"
cmd vendor
expect 'vendor: 1 vendors. proxies' 10
note "$(since | grep -E '  npc [0-9]+: .*entity' | tail -1)"
# Beside it: the prompt, then our own vendor's screen.
expect 'vendor: beside .*: trade prompt shown|prompts: vendor offered on .* through the game.s interaction' 10
cmd vendor open
expect "vendor: shop open: .*our own vendor $entity" 10
expect 'activities: our V is busy in a shop' 10
expect_peer 'player [0-9]+ is busy: a shop' 10
pause 2
shot vendor-guest-shop
note "$(since | grep -E 'vendor: the shop.s screen is up' | tail -1)"
cmd vendor close
expect 'vendor: shop closed' 10
expect_peer 'player [0-9]+ is free again' 15
# The host's vendor dies: every game hears it; ours stays hidden and shut.
expect_peer 'vendor npc [0-9]+ died' 70
expect "vendor: $entity died in the session" 15
# ...and a placed one is in the session's ledger (regions.md §14); we saw it
# die, so no other body is laid.
if [ "$placed" = true ]; then
  expect "ledger: placed npc .* died in player 1's game" 15
fi
reject 'ledger: a body for placed npc'
cmd vendor open
expect 'vendor: no vendor.s proxy within 6 m|stays shut: dead' 10
shot vendor-guest-dead
reject 'vendor: .*streamed in again'
reject 'watchdog: game thread silent'
