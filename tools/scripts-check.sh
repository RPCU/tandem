#!/usr/bin/env bash
# Compile scripts/tandem/*.reds the way the game does, without the game.
#
# The game runs redscript's scc on every launch, over r6/scripts plus the
# script folders RED4ext plugins register (Codeware, ArchiveXL, ...), against
# the original script bundle. One error disables every script in the game.
# This runs the same scc (nix/redscript.nix, the game's version) over the same
# inputs: the game's bundle and the other mods' scripts, copied read-only from
# GAME_HOST into the shared cache, and our scripts from this working tree.
#
# Usage: tools/scripts-check.sh [DIR]   (exit 1 on a compile error; warnings pass)
#        DIR: our scripts, default scripts/tandem (another copy, to try a change)
# Env:   GAME_HOST (anya), GAME_DIR, TANDEM_REDSCRIPT_BASE (the cache).
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
host=${GAME_HOST:-anya}
game=${GAME_DIR:-/data/Steam/steamapps/common/Cyberpunk 2077}
cache=${XDG_CACHE_HOME:-$HOME/.cache}/tandem
base=${TANDEM_REDSCRIPT_BASE:-$cache/redscript-base}
lock=/tmp/tandem-build.lock
ours=$(cd "${1:-$root/scripts/tandem}" && pwd)

# What scc lets through and the game dies of: a method defined twice.
python3 "$root/tools/scripts-duplicates.py" "$ours"
# And what it lets through and the game misreads: a value-type local read
# before it was ever set (the VM does not clear it).
python3 "$root/tools/scripts-uninitialized.py" "$ours"

# True when an ancestor process holds the build lock (flock ... task deploy):
# taking it again from here would wait forever.
lock_held_by_ancestor() {
  local pid=$$ fd
  while [ "$pid" -gt 1 ]; do
    for fd in /proc/"$pid"/fd/*; do
      [ "$(readlink "$fd" 2>/dev/null)" = "$lock" ] && return 0
    done
    pid=$(awk '/^PPid:/ { print $2 }' /proc/"$pid"/status 2>/dev/null || echo 1)
  done
  return 1
}

# The compiler: built once per nix/redscript.nix, kept as a GC root in the cache.
key=$(sha256sum "$root/nix/redscript.nix" | cut -c1-12)
scc_link=$cache/redscript/scc-$key
if [ ! -x "$scc_link/bin/scc" ]; then
  mkdir -p "$cache/redscript"
  echo "scripts:check: building the redscript compiler (once)" >&2
  build=(nix-build "$root" -A redscript -o "$scc_link" --cores 8 --max-jobs 1)
  if lock_held_by_ancestor; then
    "${build[@]}" >&2
  else
    flock "$lock" "${build[@]}" >&2
  fi
fi
scc=$scc_link/bin/scc

# The base, read-only from the game: its original bundle, the other mods'
# r6/scripts, and the .reds files under red4ext/plugins. Ours are excluded.
# Parallel checks share the copy: one refreshes it at a time (exclusive), the
# others only read it while they compile (shared).
mkdir -p "$base"
exec 9>"$base.lock"
flock 9
if ! rsync --rsh='ssh -o ConnectTimeout=5' --protect-args -rtR --delete --delete-excluded --prune-empty-dirs \
  --exclude=/r6/scripts/tandem/ --exclude=/red4ext/plugins/tandem/ \
  --include=/r6/cache/final.redscripts --include=/r6/cache/final.redscripts.bk \
  --include='*/' --include='*.reds' --exclude='*' \
  "$host:$game/./r6/scripts" "$host:$game/./red4ext/plugins" "$host:$game/./r6/cache" \
  "$base/"; then
  [ -f "$base/r6/cache/final.redscripts" ] || {
    echo "scripts:check: cannot copy the game's scripts from $host, and no cached copy" >&2
    exit 2
  }
  echo "scripts:check: $host unreachable, using the cached copy in $base" >&2
fi

flock -s 9

# The original bundle: final.redscripts.bk when an older redscript left one
# (it then compiled in place), else final.redscripts (scc writes .modded).
bundle=$base/r6/cache/final.redscripts
[ -f "$bundle.bk" ] && bundle=$bundle.bk

# A throwaway r6/ for scc: scripts/ links the other mods and our tree, the
# plugins' folders go through the paths file, as RED4ext passes them.
mkdir -p "$cache/redscript-check"
work=$(mktemp -d "$cache/redscript-check/run.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/r6/scripts" "$work/r6/cache" "$work/r6/logs"
for entry in "$base"/r6/scripts/*; do
  [ -e "$entry" ] && ln -s "$entry" "$work/r6/scripts/"
done
ln -s "$ours" "$work/r6/scripts/tandem"
ln -s "$bundle" "$work/r6/cache/final.redscripts"
for entry in "$base"/red4ext/plugins/*; do
  [ -e "$entry" ] && printf '%s\n' "$entry"
done >"$work/paths.txt"

status=0
"$scc" -compile "$work/r6/scripts" -compilePathsFile "$work/paths.txt" \
  -outputCacheFile "$work/r6/cache/final.redscripts.modded" >"$work/out.log" 2>&1 || status=$?

# The game's log, minus the list of compiled files, with paths as in the game
# (r6/scripts/..., red4ext/plugins/...) and ours as in this tree. On stdout
# when it compiles (warnings), on stderr when it does not.
report() {
  awk '
    /Compiling files in/ { print; skip = 1; next }
    skip && /^\[/ { skip = 0 }
    !skip
  ' "$work/out.log" | sed \
    -e "s|$work/r6/scripts/tandem/|${ours#"$root"/}/|g" \
    -e "s|$work/r6/scripts|r6/scripts|g" \
    -e "s|$base/red4ext/plugins/|red4ext/plugins/|g" \
    -e "s|$work/r6/cache/|r6/cache/|g"
}

version=$(readlink "$scc_link" | sed 's/^.*-redscript-scc-//')
errors=$(grep -c '^\[ERROR.*\] At ' "$work/out.log" || true)
warnings=$(grep -c '^\[WARN' "$work/out.log" || true)
if [ "$status" -ne 0 ] || grep -q '^\[ERROR' "$work/out.log"; then
  report >&2
  echo "scripts:check: FAILED, $errors error(s), $warnings warning(s) (redscript $version): the game would run no scripts" >&2
  exit 1
fi
report
echo "scripts:check: ok, $warnings warning(s) (redscript $version)"
