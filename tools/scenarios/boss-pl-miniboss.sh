# Two games: Phantom Liberty's exo mini-boss (we_ep1_01, second phase) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.we_ep1_01_mini_boss_2nd_phase.
# To look at: the exo on both screens, the boss bar on both.
boss_fight "${BOSS_RECORD:-Character.we_ep1_01_mini_boss_2nd_phase}" boss-pl-miniboss
