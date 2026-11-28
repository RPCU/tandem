# Two games: Phantom Liberty's droid mini-boss (sts_ep1_12) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.sts_ep1_12_droid_miniboss.
# To look at: the droid on both screens, the boss bar on both.
boss_fight "${BOSS_RECORD:-Character.sts_ep1_12_droid_miniboss}" boss-pl-droid
