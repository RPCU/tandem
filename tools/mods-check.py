#!/usr/bin/env python3
"""Which of the other mods' script hooks meet tandem's (docs/architecture/mod-compat.md).

Lists every method tandem's scripts (scripts/tandem/*.reds of this checkout)
wrap or replace, and every method the other mods hook: redscript's
@wrapMethod, @replaceMethod, @wrapConstructor, @replaceGlobal, @wrapGlobal,
and CET Lua's Override (a replace, unless its function calls the original it
is handed) and Observe / ObserveBefore / ObserveAfter (observers). Each method
hooked by more than one side is an overlap:

- conflict: a replace (or a wrap that never calls wrappedMethod) on a method
  tandem wraps, tandem replacing something another mod hooks, or two mods
  replacing the same method;
- order-sensitive: a mod's replace, or a CET Override that calls the original
  only on its own terms, on a method another side wraps or observes. With
  tandem: the wrap still runs (a redscript wrap goes round whatever body the
  method has), but what the method does underneath is the mod's;
- chain: wraps and observers only, each calls the next: harmless.

The mods' files are only read as text (regular expressions), never run.

Usage: tools/mods-check.py [--json] DIR
  DIR: a game folder (r6/scripts, bin/x64/plugins/cyber_engine_tweaks/mods),
  a copy laid out as r6-scripts/ and cet-mods/, or any folder of .reds and
  .lua files. Exit 1 when a conflict touches tandem, 0 otherwise, 2 on a
  bad DIR.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TANDEM = ROOT / "scripts" / "tandem"

ANNOTATION = re.compile(
    r"^\s*@(wrapMethod|replaceMethod|wrapConstructor|replaceGlobal|wrapGlobal)\s*\(\s*([\w.]*)\s*\)(.*)$"
)
FUNC = re.compile(r"\bfunc\s+(\w+)\s*\(([^)]*)\)?")
CET = re.compile(
    r"""\b(Override|Observe|ObserveBefore|ObserveAfter)\s*\(\s*(['"])([\w.]+)\2\s*,\s*(['"])([\w.;]+)\4"""
)
CET_DYNAMIC = re.compile(r"\b(Override|Observe|ObserveBefore|ObserveAfter)\s*\(\s*[A-Za-z_]")


def strip_comments(text):
    """Comments out of redscript or Lua text, keeping the line count."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def strip_lua_comments(text):
    text = re.sub(r"--\[(=*)\[.*?\]\1\]", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"--[^\n]*", "", text)


def param_types(params):
    types = []
    for part in params.split(","):
        if ":" in part:
            types.append(re.sub(r"\s+", "", part.split(":", 1)[1]))
    return ",".join(types)


def body_from(lines, start):
    """The text of a function from its declaration line to its closing brace."""
    depth, seen, out = 0, False, []
    for line in lines[start : start + 2000]:
        out.append(line)
        clean = re.sub(r'"(?:[^"\\]|\\.)*"', '""', line)
        depth += clean.count("{") - clean.count("}")
        seen = seen or "{" in clean
        if seen and depth <= 0:
            break
    return "\n".join(out)


def reds_hooks(path, mod):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    lines = strip_comments(text).split("\n")
    hooks = []
    for i, line in enumerate(lines):
        m = ANNOTATION.match(line)
        if not m:
            continue
        kind, cls, rest = m.group(1), m.group(2), m.group(3)
        func = None
        # The declaration: on the same line, or within the next few
        # (other annotations such as @if may come between).
        for j in range(i, min(i + 8, len(lines))):
            candidate = rest if j == i else lines[j]
            f = FUNC.search(candidate)
            if f:
                func, at = f, j
                break
        if not func:
            continue
        name = "new" if kind == "wrapConstructor" and not func.group(1) else func.group(1)
        if kind in ("replaceGlobal", "wrapGlobal"):
            cls = ""
        kind_word = {
            "wrapMethod": "wrap",
            "wrapConstructor": "wrap",
            "wrapGlobal": "wrap",
            "replaceMethod": "replace",
            "replaceGlobal": "replace",
        }[kind]
        if kind_word == "wrap" and "wrappedMethod(" not in body_from(lines, at):
            kind_word = "wrap-no-call"
        hooks.append(
            {
                "mod": mod,
                "file": str(path),
                "line": i + 1,
                "class": cls,
                "method": name,
                "signature": param_types(func.group(2) or ""),
                "kind": kind_word,
                "lang": "reds",
            }
        )
    return hooks


def lua_block(lines, start):
    """A CET hook's function: from its line to the `end)` closing it (by indent)."""
    indent = len(lines[start]) - len(lines[start].lstrip())
    out = [lines[start]]
    for line in lines[start + 1 : start + 3000]:
        out.append(line)
        stripped = line.strip()
        if re.match(r"^end\s*\)", stripped) and len(line) - len(line.lstrip()) <= indent:
            break
    return "\n".join(out)


def lua_hooks(path, mod, dynamic):
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    lines = strip_lua_comments(text).split("\n")
    hooks = []
    for i, line in enumerate(lines):
        found = False
        for m in CET.finditer(line):
            found = True
            what, cls, method = m.group(1), m.group(3), m.group(5).split(";")[0]
            kind = "observe"
            if what == "Override":
                block = lua_block(lines, i)
                # Override("C", "M", handler): the handler's own definition.
                named = re.match(r"\s*,\s*([A-Za-z_][\w.]*)\s*\)", line[m.end() :])
                if named:
                    leaf = re.escape(named.group(1).split(".")[-1])
                    for j, other in enumerate(lines):
                        if re.search(r"\bfunction\s+[\w.:]*\b" + leaf + r"\s*\(|\b" + leaf + r"\s*=\s*function\s*\(", other):
                            block = lua_block(lines, j)
                            break
                params = re.search(r"function\s*\(([^)]*)\)", block)
                names = [p.strip() for p in params.group(1).split(",")] if params else []
                original = names[-1] if names else ""
                calls = bool(original) and re.search(r"\b" + re.escape(original) + r"\s*\(", block[block.find(")") + 1 :]) is not None
                kind = "override-calls" if calls else "replace"
            hooks.append(
                {
                    "mod": mod,
                    "file": str(path),
                    "line": i + 1,
                    "class": cls,
                    "method": method,
                    "signature": "",
                    "kind": kind,
                    "lang": "cet",
                }
            )
        if not found and CET_DYNAMIC.search(line) and not re.search(r"\bfunction\b[^(]*\b(Override|Observe)", line):
            dynamic.append(f"{path}:{i + 1}")
    return hooks


def mod_of(path, base):
    rel = path.relative_to(base)
    return rel.parts[0] if len(rel.parts) > 1 else rel.stem


def roots(folder):
    """(redscript folders, CET folders) of DIR, by its layout."""
    game = folder / "r6" / "scripts", folder / "bin" / "x64" / "plugins" / "cyber_engine_tweaks" / "mods"
    copy = folder / "r6-scripts", folder / "cet-mods"
    for reds, cet in (game, copy):
        if reds.is_dir() or cet.is_dir():
            return [p for p in [reds] if p.is_dir()], [p for p in [cet] if p.is_dir()]
    return [folder], [folder]


def gather(folder):
    reds_roots, cet_roots = roots(folder)
    hooks, dynamic = [], []
    for base in reds_roots:
        for path in sorted(base.rglob("*.reds")):
            if path.resolve().is_relative_to(TANDEM.resolve()) or mod_of(path, base).lower() == "tandem":
                continue
            hooks += reds_hooks(path, mod_of(path, base))
    for base in cet_roots:
        for path in sorted(base.rglob("*.lua")):
            hooks += lua_hooks(path, mod_of(path, base), dynamic)
    return hooks, dynamic


REPLACING = {"replace", "wrap-no-call"}


def classify(entries):
    """conflict | order-sensitive | chain, and why, for hooks of one method."""
    tandem = [e for e in entries if e["mod"] == "tandem"]
    others = [e for e in entries if e["mod"] != "tandem"]
    replacers = [e for e in entries if e["kind"] in REPLACING]
    if any(e["kind"] in REPLACING for e in tandem):
        return "conflict", "tandem replaces it and another mod hooks it"
    if tandem and any(e["kind"] in REPLACING for e in others):
        return "conflict", "a mod replaces a method tandem wraps: our wrap goes round the mod's body"
    if len({e["mod"] for e in replacers}) > 1:
        return "conflict", "two mods replace it: one replacement is lost"
    if replacers or any(e["kind"] == "override-calls" for e in entries):
        return "order-sensitive", "a replace or a CET Override under the others' wraps"
    return "chain", "wraps and observers only"


def overlaps(hooks):
    by_method = {}
    for h in hooks:
        by_method.setdefault((h["class"], h["method"]), []).append(h)
    out = []
    for (cls, method), entries in sorted(by_method.items()):
        if len({e["mod"] for e in entries}) < 2:
            continue
        verdict, why = classify(entries)
        out.append(
            {
                "class": cls,
                "method": method,
                "verdict": verdict,
                "why": why,
                "tandem": any(e["mod"] == "tandem" for e in entries),
                "hooks": entries,
            }
        )
    return out


def where(e, base):
    try:
        shown = pathlib.Path(e["file"]).relative_to(base)
    except ValueError:
        shown = pathlib.Path(e["file"]).relative_to(ROOT) if pathlib.Path(e["file"]).is_relative_to(ROOT) else e["file"]
    sig = f"({e['signature']})" if e["signature"] else ""
    return f"{e['mod']}: {e['kind']}{sig} {shown}:{e['line']}"


def main(argv):
    as_json = "--json" in argv
    args = [a for a in argv if a != "--json"]
    if len(args) != 1 or args[0] in ("-h", "--help"):
        print(__doc__.strip().split("\n\n")[-1], file=sys.stderr)
        return 2
    folder = pathlib.Path(args[0]).expanduser()
    if not folder.is_dir():
        print(f"mods-check: {folder} is not a folder", file=sys.stderr)
        return 2
    ours = []
    for path in sorted(TANDEM.glob("*.reds")):
        ours += reds_hooks(path, "tandem")
    theirs, dynamic = gather(folder)
    found = overlaps(ours + theirs)
    bad = [o for o in found if o["verdict"] == "conflict" and o["tandem"]]
    if as_json:
        print(
            json.dumps(
                {
                    "tandem_hooks": len(ours),
                    "tandem_replaces": [h for h in ours if h["kind"] in REPLACING],
                    "mod_hooks": len(theirs),
                    "mods": sorted({h["mod"] for h in theirs}),
                    "overlaps": found,
                    "unresolved_cet_hooks": dynamic,
                },
                indent=2,
            )
        )
        return 1 if bad else 0

    mods = sorted({h["mod"] for h in theirs})
    replaces = [h for h in ours if h["kind"] in REPLACING]
    print(f"tandem: {len(ours)} hooks, {len(replaces)} replacing ({TANDEM.relative_to(ROOT)})")
    for h in replaces:
        print(f"  {h['class']}.{h['method']}: {where(h, ROOT)}")
    print(f"other mods: {len(theirs)} hooks in {len(mods)} mods under {folder}")
    order = {"conflict": 0, "order-sensitive": 1, "chain": 2}
    for group, title in ((True, "with tandem"), (False, "between other mods")):
        listed = sorted((o for o in found if o["tandem"] == group), key=lambda o: (order[o["verdict"]], o["class"], o["method"]))
        print(f"\n{title}: {len(listed)} overlap(s)")
        for o in listed:
            if not group and o["verdict"] == "chain":
                continue
            name = f"{o['class']}.{o['method']}" if o["class"] else o["method"]
            print(f"  {o['verdict'].upper()} {name}: {o['why']}")
            for e in o["hooks"]:
                print(f"    {where(e, folder)}")
        if not group:
            chains = [o for o in listed if o["verdict"] == "chain"]
            if chains:
                print(f"  ({len(chains)} harmless chain(s) between other mods not listed)")
    if dynamic:
        print(f"\nCET hooks with a computed class or method name, not compared ({len(dynamic)}):")
        for d in dynamic:
            print(f"  {pathlib.Path(d).relative_to(folder) if pathlib.Path(d.rsplit(':', 1)[0]).is_relative_to(folder) else d}")
    print(f"\n{len(bad)} conflict(s) touch tandem" if bad else "\nno conflict touches tandem")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
