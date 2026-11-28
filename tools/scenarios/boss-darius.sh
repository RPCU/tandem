# Two games: Darius (mq062) fought by the partner: the host's boss, attacking the partner, with the same phase, bar, broken weak spot and death on both screens
# shellcheck shell=bash disable=SC2317,SC2154
# needs two games
# BOSS_RECORD: another Character record (or kind:boss|chimera|cerberus,
# name:PART, as `boss spawn` takes it); default Character.mq062_boss_darius.
# To look at: the exo on both screens, the boss bar on both.
boss_fight "${BOSS_RECORD:-Character.mq062_boss_darius}" boss-darius
