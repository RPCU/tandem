# The game loads as host, plays, answers commands, and stays up
# shellcheck shell=bash disable=SC2317
ensure host
cmd where
expect 'where: '
pause 10
reject '\[error|watchdog: game thread silent'
