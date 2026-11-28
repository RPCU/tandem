# Two games: a cyberpsycho (ma_std_rcr_11) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.ma_std_rcr_11_cyberpsycho.
# To look at: the boss bar on both (a cyberpsycho is a boss), its attacks on the partner.
boss_fight "${BOSS_RECORD:-Character.ma_std_rcr_11_cyberpsycho}" boss-cyberpsycho
