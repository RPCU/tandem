# Two games: the partner joins a boss fight under way (Adam Smasher at 45 %): its proxy starts in the host's phase, with the host's broken weak spots, bar and death on both
# shellcheck shell=bash disable=SC2317,SC2154,SC2034
# needs two games
# BOSS_RECORD: another Character record (kind:K, name:S); default
# Character.main_boss_adam_smasher.
# To look at: the partner's Smasher already without his front plate and arm
# (phase 2) the moment it sees him.
BOSS_LATE=1
boss_fight "${BOSS_RECORD:-Character.main_boss_adam_smasher}" boss-late-join
