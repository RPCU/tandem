# A partner burns, goes camo and heals: its puppet plays each status effect's own visuals, never their gameplay; Stealth's camo is worn for real; a record unknown here is skipped with a notice; a vanish takes everything
# shellcheck shell=bash disable=SC2317
ensure host
cmd god on
# The mock player's V: burning from 4 to 10 s, its camo from 12 to 17, a heal
# from 19 on (still on when it vanishes), a record no game has from 23 to 26.
# The heal is a MaxDoc's (FirstAidWhiffV0: one VFX record in 2.50's
# tweakdb_ep1.bin; QuickhackConsumableHealing, named in final.reds, is no
# record there).
peer join "$(getent ahostsv4 "${GAME_HOST:-anya}" | awk 'NR == 1 { print $1 }')":27450 --mode front --body female \
  --status BaseStatusEffect.PlayerBurning@4-10,BaseStatusEffect.Burning@4-10,BaseStatusEffect.FirstAidWhiffV0@19,Tandem.NoSuchStatusEffect@23-26 \
  --camo 12 --camo-record BaseStatusEffect.OpticalCamoCoolPerkShort --vanish-at 32
expect 'joined \(player [0-9]+' 20
expect 'controller handed to the engine' 20
# Burning: both records' visuals on the puppet, as the game plays them on a
# character (their VFX and sounds); no damage, no stats.
expect_peer 'status visuals on our V: BaseStatusEffect.PlayerBurning, BaseStatusEffect.Burning' 20
expect 'player [0-9]+: 2 status effect\(s\) show on its V' 5
expect 'status: player [0-9]+ shows BaseStatusEffect.Burning on its puppet \([0-9]+ effect' 5
expect 'status: player [0-9]+ shows BaseStatusEffect.PlayerBurning on its puppet' 5
shot presence-status-burning
expect_peer 'status visuals on our V: none' 15
expect 'status: player [0-9]+ no longer shows BaseStatusEffect.Burning on its puppet' 5
# Camo: Stealth puts the record itself on the puppet (its look, its vision
# blocker); nothing of it travels as a status visual.
expect_peer 'optical camo on \(BaseStatusEffect.OpticalCamoCoolPerkShort\)' 10
expect 'stealth: player [0-9]+ camo on \(BaseStatusEffect.OpticalCamoCoolPerkShort\)' 5
shot presence-status-camo
expect_peer 'optical camo off' 10
expect 'stealth: player [0-9]+ camo off' 5
# The heal.
expect_peer 'status visuals on our V: BaseStatusEffect.FirstAidWhiffV0' 10
expect 'status: player [0-9]+ shows BaseStatusEffect.FirstAidWhiffV0 on its puppet' 5
shot presence-status-heal
# A record this game does not have (a mod's on one side): skipped, said once.
expect_peer 'status visuals on our V: .*Tandem.NoSuchStatusEffect' 10
expect 'status: player [0-9]+ shows .*, a record this game does not have \(a mod.s\?\): skipped' 5
expect 'coop notice: .* \[Status-Unknown\]' 5
cmd presence status report
expect 'status: player [0-9]+.s puppet shows [0-9]+:' 5
# Our own V: a real status effect on it (god mode on) goes to the partner.
cmd presence status apply BaseStatusEffect.Burning
expect 'presence status: BaseStatusEffect.Burning on our V \(shows: true' 5
expect 'status: our V shows [1-9][0-9]* effect\(s\) to the others:.* BaseStatusEffect.Burning' 5
expect_peer 'player 1 status visuals: [1-9][0-9]* record' 5
cmd presence status remove BaseStatusEffect.Burning
expect 'status: our V shows [0-9]+ effect\(s\) to the others:' 10
# It vanishes with the heal on: the puppet's effects go with it.
expect_peer 'vanishing \(--vanish-at\)' 30
expect 'status: player [0-9]+.s effects cleared \(puppet gone\)' 15
cmd god off
# Seen (a person there): flames and their crackle on the puppet, not on our
# screen; the camo's shimmer as transparent as V's own; the heal's glow.
reject 'watchdog: game thread silent'
