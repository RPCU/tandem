# A player holds a weapon from a mod our game lacks: a vanilla one of its type in the puppet's hand, said once
# shellcheck shell=bash disable=SC2317
# On purpose: a record of a mod our game lacks.
allow_fallback "record substituted"
ensure host
# No game has Items.Tandem_Modded_Pistol: a mod's handgun, as a mod would add it.
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female \
  --held-record Items.Tandem_Modded_Pistol:Wea_Handgun:Weapon --shoot 4
expect 'joined \(player [0-9]+' 20
expect 'mods: unknown weapon 0x[0-9a-f]+ from .*, shown as Items.Preset_Lexington_Default' 20
expect 'player [0-9]+: holds Items.Preset_Lexington_Default' 20
expect "coop notice: .*'s weapon comes from a mod you don't have" 10
# The partner is told: its game sees a substitute for one of its records.
expect_peer 'mods: .* sees substitutes for 1 of ours' 10
cmd coop mods
expect "coop: mods: .*'s weapon 0x[0-9a-f]+ - Items.Preset_Lexington_Default" 5
reject 'watchdog: game thread silent'
