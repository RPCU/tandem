#!/usr/bin/env python3
"""No silent fallback (docs/architecture/degradations.md): the tools' side.

  tools/degraded.py lint       (task degraded:check) every id the scripts, the
                               plugin and the scenarios name is one of
                               core/src/degraded.cpp's catalogue, said as what
                               it is there (a state or a fallback); every id
                               has its text in Text.reds, English and French,
                               and its row in degradations.md; no id is left
                               that nothing registers
  tools/degraded.py ids        the catalogue: id, class, kind, what it means
  tools/degraded.py registry [--expect ID]... [--allow ID]...  < GAME LOG
                               the last `degraded: registry` block of a game's
                               log (cmd degraded writes one), printed; exit 1
                               when it holds a state or a fallback that is
                               neither expected nor allowed, or lacks an
                               expected one; exit 2 without a block
  tools/degraded.py told [--expect ID]... [--allow ID]...  < MOCK PLAYER LOG
                               what games told the mock player they run
                               without (`degraded: player N's game runs
                               without: …`); exit 1 on an id that is neither

tools/game.sh degraded and tools/scenario.sh run the last two.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOGUE = ROOT / "core" / "src" / "degraded.cpp"
SCRIPTS = ROOT / "scripts" / "tandem"
PLUGIN = ROOT / "plugin" / "src"
TEXT = SCRIPTS / "Text.reds"
DOC = ROOT / "docs" / "architecture" / "degradations.md"
SCENARIOS = ROOT / "tools" / "scenarios"
ACCESS = PLUGIN / "engine" / "access.hpp"

ENTRY = re.compile(r'\{"([^"]+)",\s*(Core|Degraded),\s*(State|Fallback),\s*((?:"[^"]*"\s*)+)\}')
LINE = re.compile(r"degraded: (state|fallback) (.+?) \[([^\]]*)\](?: x(\d+))?: ?(.*)$")
TOLD = re.compile(r"degraded: player (\d+)'s game runs without: (.*)$")


def catalogue():
    """{id: (class, kind, means)} in the catalogue's order."""
    text = CATALOGUE.read_text()
    body = text[text.index("constexpr Info kCatalogue[] = {") :]
    body = body[: body.index("\n};")]
    found = {}
    for m in ENTRY.finditer(body):
        means = "".join(re.findall(r'"([^"]*)"', m.group(4)))
        found[m.group(1)] = (m.group(2).lower(), m.group(3).lower(), means)
    return found


def options(args):
    expect, allow, i = [], [], 0
    while i < len(args):
        if args[i] in ("--expect", "--allow") and i + 1 < len(args):
            (expect if args[i] == "--expect" else allow).append(args[i + 1])
            i += 2
        else:
            sys.exit(f"degraded: unknown argument {args[i]}")
    return expect, allow


def registry(args):
    expect, allow = options(args)
    block, current, on = None, [], False
    for raw in sys.stdin:
        at = raw.find("degraded: ")
        if at < 0:
            continue
        line = raw[at:].rstrip("\n")
        if line.startswith("degraded: registry: "):
            current, on = [line], True
        elif line.startswith("degraded: registry end") and on:
            current.append(line)
            block, on = current, False
        elif on:
            current.append(line)
    if block is None:
        print("no `degraded: registry` block in this log (cmd degraded writes one)", file=sys.stderr)
        return 2
    seen, unexpected = set(), []
    for line in block:
        print(line)
        m = LINE.match(line)
        if not m:
            continue
        kind, name = m.group(1), m.group(2)
        seen.add(name)
        if name not in expect and name not in allow:
            unexpected.append(f"{name} ({kind}, {m.group(3)}{', x' + m.group(4) if m.group(4) else ''}: {m.group(5)})")
    missing = [name for name in expect if name not in seen]
    for item in unexpected:
        print(f"unexpected: {item}")
    for name in missing:
        print(f"missing: {name} was expected (expect_degraded) and is not in the registry")
    return 1 if unexpected or missing else 0


def told(args):
    expect, allow = options(args)
    bad = []
    for raw in sys.stdin:
        m = TOLD.search(raw.rstrip("\n"))
        if not m or m.group(2).strip() == "nothing":
            continue
        for item in m.group(2).split(", "):
            name = re.sub(r" \((core|chosen|core, chosen)\)$", "", item.strip())
            if name and name not in expect and name not in allow and (m.group(1), name) not in bad:
                bad.append((m.group(1), name))
    for player, name in bad:
        print(f"unexpected: player {player}'s game told the mock player it runs without {name}")
    return 1 if bad else 0


def lint():
    known = catalogue()
    errors = []
    if not known:
        sys.exit(f"{CATALOGUE}: no catalogue found")
    for name, (klass, kind, means) in known.items():
        if not re.fullmatch(r"[a-z0-9 -]{1,32}", name):
            errors.append(f"{CATALOGUE.name}: id '{name}': 1 to 32 of a-z, 0-9, space, dash")
        if kind == "fallback" and klass == "core":
            errors.append(f"{CATALOGUE.name}: '{name}' is a fallback: never core")

    # Who registers what: scripts (TandemDegraded.State|Chosen|Over|Fallback),
    # the plugin (Degradations::Get().Start|End|Fallback), and the engine's
    # features (access.hpp: FeatureOff registers the feature's name).
    used = {}

    def use(name, kind, where):
        used.setdefault(name, set()).add(kind)
        if name not in known:
            errors.append(f"{where}: '{name}' is not an id of {CATALOGUE.name}'s catalogue")
        elif kind != "any" and known[name][1] != kind:
            errors.append(f"{where}: '{name}' is a {known[name][1]} in the catalogue, registered as a {kind}")

    call = re.compile(r'TandemDegraded\s*\.\s*(State|Chosen|Over|Fallback)\(\s*"([^"]*)"')
    lesser = re.compile(r'let lesser = [^;]*\? "([^"]+)" : "([^"]+)";')
    for path in sorted(SCRIPTS.glob("*.reds")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            for m in call.finditer(line):
                use(m.group(2), "fallback" if m.group(1) == "Fallback" else "state", f"{path.name}:{number}")
            # An id picked first, then registered (System.reds, StandIn).
            for m in lesser.finditer(line):
                use(m.group(1), "fallback", f"{path.name}:{number}")
                use(m.group(2), "fallback", f"{path.name}:{number}")
    native = re.compile(r'Degradations::Get\(\)\s*\.\s*(Start|End|Fallback)\(\s*"([^"]*)"', re.S)
    for path in sorted(PLUGIN.rglob("*.cpp")):
        text = path.read_text()
        for m in native.finditer(text):
            number = text.count("\n", 0, m.start()) + 1
            use(m.group(2), "fallback" if m.group(1) == "Fallback" else "state", f"{path.name}:{number}")
    for m in re.finditer(r'inline constexpr std::string_view k\w+ = "([^"]+)";', ACCESS.read_text()):
        use(m.group(1), "state", ACCESS.name)
    for name in known:
        if name not in used:
            errors.append(f"{CATALOGUE.name}: '{name}' is registered nowhere (scripts, plugin, engine features)")

    # Texts: one per id, in every package.
    text = TEXT.read_text()
    packages = re.split(r"public class (\w+) extends ModLocalizationPackage", text)[1:]
    for package, body in zip(packages[0::2], packages[1::2]):
        keys = set(re.findall(r'this\.Text\("Tandem-Degraded-Id-([^"]+)"', body))
        for name in known:
            if name.replace(" ", "-") not in keys:
                errors.append(f"Text.reds: {package} has no Tandem-Degraded-Id-{name.replace(' ', '-')}")
        for key in sorted(keys - {name.replace(" ", "-") for name in known}):
            errors.append(f"Text.reds: {package}: Tandem-Degraded-Id-{key} is no id of the catalogue")

    # The doc's table: a row per id, with its class and kind.
    rows = {}
    if DOC.exists():
        for m in re.finditer(r"^\| `([^`]+)` \| (core|degraded) \| (state|fallback) \|", DOC.read_text(), re.M):
            rows[m.group(1)] = (m.group(2), m.group(3))
    else:
        errors.append(f"{DOC.relative_to(ROOT)}: missing")
    for name, (klass, kind, _) in known.items():
        if name not in rows:
            errors.append(f"{DOC.name}: no row for `{name}`")
        elif rows[name] != (klass, kind):
            errors.append(f"{DOC.name}: `{name}` is {rows[name][0]} {rows[name][1]}, the catalogue says {klass} {kind}")
    for name in rows:
        if name not in known:
            errors.append(f"{DOC.name}: row `{name}` is no id of the catalogue")

    # Scenarios declare ids that exist.
    declared = re.compile(r"^\s*(expect_degraded|allow_fallback)\s+(?:\"([^\"]*)\"|'([^']*)'|(\S+))")
    count = 0
    for path in sorted(SCENARIOS.glob("*.sh")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            m = declared.match(line)
            if not m:
                continue
            count += 1
            name = m.group(2) or m.group(3) or m.group(4)
            if name not in known:
                errors.append(f"{path.name}:{number}: {m.group(1)} '{name}': no id of the catalogue")
    for message in errors:
        print(f"degraded:check: {message}", file=sys.stderr)
    states = sum(1 for v in known.values() if v[1] == "state")
    core = sum(1 for v in known.values() if v[0] == "core")
    if errors:
        print(f"degraded:check: {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"degraded:check: ok, {len(known)} ids ({states} states, {core} of them core, "
          f"{len(known) - states} fallbacks), {count} declared by scenarios")
    return 0


def main():
    verb = sys.argv[1] if len(sys.argv) > 1 else "lint"
    if verb == "lint":
        return lint()
    if verb == "ids":
        for name, (klass, kind, means) in catalogue().items():
            print(f"{name}\t{klass}\t{kind}\t{means}")
        return 0
    if verb == "registry":
        return registry(sys.argv[2:])
    if verb == "told":
        return told(sys.argv[2:])
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
