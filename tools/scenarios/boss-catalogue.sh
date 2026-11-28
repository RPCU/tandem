# One game: every boss record of the game's data (rarity Boss, or a Chimera's or Cerberus's type), with its boss bar's phase thresholds, for bosses.md's catalogue
# shellcheck shell=bash disable=SC2317
# To look at: nothing; the list is in the log (bosses: catalogue ...).
ensure host
cmd boss catalogue
expect 'bosses: catalogue: [1-9][0-9]* boss record\(s\)' 60
note "$(since | grep -cE 'bosses: catalogue [A-Za-z]' || true) boss records"
for kind in kind:chimera kind:cerberus name:kurt name:woodman name:placide; do
  cmd boss find "$kind"
  expect "bosses: catalogue $kind: " 30
done
reject 'watchdog: game thread silent'
