#!/usr/bin/env bash
# The game's own scripts as text, at the game's version.
#
# Decompiles the bundle `task scripts:check` compiles against (the game's
# original final.redscripts, copied read-only from the game machine into the
# shared cache by that task) with redscript's own decompiler, into
# ~/.cache/tandem/ref/gamescripts/game/final.reds. The public dump beside it
# (vanilla/final.reds) predates patch 2.4: it knows nothing of dual
# wielding, and its line numbers are not this file's. Cite this one as
# `final.reds (2.50):LINE`.
#
# Usage: tools/scripts-decompile.sh [-f]   (prints the file's path; -f: again)
# Env:   TANDEM_REDSCRIPT_BASE (the cache), as tools/scripts-check.sh.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cache=${XDG_CACHE_HOME:-$HOME/.cache}/tandem
base=${TANDEM_REDSCRIPT_BASE:-$cache/redscript-base}
bundle=$base/r6/cache/final.redscripts
[ -f "$bundle.bk" ] && bundle=$bundle.bk
[ -f "$bundle" ] || {
  echo "scripts-decompile: no bundle at $bundle (task scripts:check copies it)" >&2
  exit 2
}
out=$cache/ref/gamescripts/game/final.reds
if [ "${1:-}" != -f ] && [ -f "$out" ] && [ "$out" -nt "$bundle" ]; then
  echo "$out"
  exit 0
fi

# The decompiler: the compiler's own sources (nix/redscript.nix), built once.
key=$(sha256sum "$root/nix/redscript.nix" | cut -c1-12)
cli=$cache/redscript/cli-$key
if [ ! -x "$cli/bin/redscript-cli" ]; then
  mkdir -p "$cache/redscript"
  echo "scripts-decompile: building the redscript decompiler (once)" >&2
  nix-build "$root" -A redscriptCli -o "$cli" --cores 8 --max-jobs 1 >&2
fi
mkdir -p "$(dirname "$out")"
"$cli/bin/redscript-cli" decompile -i "$bundle" -o "$out.tmp" >&2
mv "$out.tmp" "$out"
echo "$out"
