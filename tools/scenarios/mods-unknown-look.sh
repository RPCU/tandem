# A player wears clothes from a mod our game lacks: plain trousers for theirs, their jacket left off, one notice for both
# shellcheck shell=bash disable=SC2317
ensure host
# No game has these: a mod's jacket and trousers.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female \
  --look-piece Items.Tandem_Modded_Jacket:Clo_OuterChest --look-piece Items.Tandem_Modded_Pants:Clo_Legs
expect 'joined \(player [0-9]+' 20
expect 'mods: unknown clothing 0x[0-9a-f]+ from .*, shown as nothing' 30
expect 'mods: unknown clothing 0x[0-9a-f]+ from .*, shown as Items.Pants_02_basic_01' 10
# The pieces come together: one notice, with the count.
expect "coop notice: .*'s clothes come from a mod you don't have: shown plain or left off \(2 pieces\)" 10
expect 'player [0-9]+: their look' 10
cmd coop mods
expect 'coop: mods: Shown otherwise this session \([2-9][0-9]*\)' 5
reject 'watchdog: game thread silent'
