#!/usr/bin/env python3
"""Every string tandem shows the player comes from Text.reds.

Checks scripts/tandem (docs/architecture/strings.md):

- a literal with words in a player-facing place (a notice, a widget's text, a
  popup, an input hint, a choice, the progress bar, a regroup label sent to
  the others) is refused: it goes through TandemText and a key. A line that
  is only for tests says so with a `// text: test only` comment;
- a key the scripts use (a literal shaped like "Area-Name") exists in the
  English package;
- every English key has a French one and the other way round, with the same
  {1} {2} placeholders; no key is defined twice in a package.

Keys nothing uses are listed as a warning (some are built at run time).

Usage: tools/check-strings.py [DIR]   (default scripts/tandem; exit 1 on an error)
"""

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
scripts = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "scripts" / "tandem"
TEXT = scripts / "Text.reds"

# Where a literal reaches the screen.
SINK = re.compile(
    r"\.(message|localizedName|localizedLabel|title|text|speakerName|label)\s*=[^=]"
    r"|SetText\(|GenericMessageNotification\.|ShowBar\(|\bNotice\w*\(|\bSay\(|\bHint\("
    r"|UI_HUDProgressBar\.(Header|BottomText|CompletedText|FailedText)|OfferRegroup\("
    # A text made first and shown after: name the variable so.
    r"|\b(let\s+)?(text|label|header|bottom|title)\s*(:\s*String\s*)?(=|\+=)[^=]"
)
# A string literal (not a name, TweakDB id or resource path), with its prefix.
LITERAL = re.compile(r'(?<![A-Za-z0-9_])([nrt]?|s)"((?:[^"\\]|\\.)*)"')
KEY = re.compile(r"^[A-Z][A-Za-z]*(-[A-Z][A-Za-z0-9]*)+$")
ALLOW = "// text: test only"


def code_of(line):
    """The line without its // comment (outside strings)."""
    out, quoted, i = [], False, 0
    while i < len(line):
        c = line[i]
        if quoted and c == "\\":
            out.append(line[i : i + 2])
            i += 2
            continue
        if c == '"':
            quoted = not quoted
        elif not quoted and line.startswith("//", i):
            break
        out.append(c)
        i += 1
    return "".join(out)


def packages(text):
    """{class name: [(key, value, line)]} of Text.reds' packages."""
    found, current = {}, None
    for number, line in enumerate(text.splitlines(), 1):
        m = re.match(r"public class (\w+) extends ModLocalizationPackage", line)
        if m:
            current = found.setdefault(m.group(1), [])
            continue
        m = re.search(r'this\.Text\("([^"]+)",\s*"((?:[^"\\]|\\.)*)"\)', line)
        if m and current is not None:
            current.append((m.group(1), m.group(2), number))
    return found


def main():
    errors, warnings = [], []
    text = TEXT.read_text()
    pkgs = packages(text)
    english = pkgs.get("TandemTextEnglish")
    if not english:
        sys.exit(f"{TEXT}: no TandemTextEnglish package")
    tables = {}
    for name, entries in pkgs.items():
        table = {}
        for key, value, number in entries:
            if not key.startswith("Tandem-"):
                errors.append(f"{TEXT.name}:{number}: {key}: keys start with Tandem-")
            if key in table:
                errors.append(f"{TEXT.name}:{number}: {key} defined twice in {name}")
            table[key] = value
        tables[name] = table
    source = tables["TandemTextEnglish"]
    placeholders = lambda v: sorted(set(re.findall(r"\{\d\}", v)))
    for name, table in tables.items():
        if name == "TandemTextEnglish":
            continue
        for key in source.keys() - table.keys():
            errors.append(f"{TEXT.name}: {key} has no translation in {name}")
        for key in table.keys() - source.keys():
            errors.append(f"{TEXT.name}: {key} in {name} is not an English key")
        for key in source.keys() & table.keys():
            if placeholders(source[key]) != placeholders(table[key]):
                errors.append(f"{TEXT.name}: {key}: {name} has {placeholders(table[key])}, English {placeholders(source[key])}")
    known = {k[len("Tandem-") :] for k in source}

    used = set()
    for path in sorted(scripts.glob("*.reds")):
        lines = path.read_text().splitlines()
        in_package = False
        for number, raw in enumerate(lines, 1):
            if path == TEXT:
                # Only the code of Text.reds uses keys; its packages define them.
                if re.match(r"public class \w+ extends ModLocalizationPackage", raw):
                    in_package = True
                if in_package:
                    continue
            line = code_of(raw)
            sink = SINK.search(line) and ALLOW not in raw and path != TEXT
            for m in LITERAL.finditer(line):
                prefix, value = m.group(1), m.group(2)
                if prefix in ("n", "r", "t"):
                    continue
                before = line[: m.start()]
                if prefix == "" and KEY.match(value):
                    if before.rstrip().endswith("Vanilla("):
                        continue
                    if value in known:
                        used.add(value)
                        continue
                    if re.search(r"TandemText\.\w+\([^()]*$|\bNotice\w*\(|\bAdd\(", before):
                        errors.append(f"{path.name}:{number}: no key {value} in Text.reds")
                    continue
                if not sink:
                    continue
                words = re.sub(r"\\\([^)]*\)", "", value)
                if re.search(r"[A-Za-z]{2,}", words):
                    errors.append(f'{path.name}:{number}: player-facing literal {prefix}"{value}": use a key (Text.reds)')
    for key in sorted(known - used):
        warnings.append(f"{TEXT.name}: Tandem-{key} is not used by name (built at run time?)")

    for w in warnings:
        print(f"strings:check: warning: {w}", file=sys.stderr)
    for e in errors:
        print(f"strings:check: {e}", file=sys.stderr)
    if errors:
        sys.exit(1)
    print(f"strings:check: ok, {len(source)} keys, {len(tables) - 1} translation(s)")


main()
