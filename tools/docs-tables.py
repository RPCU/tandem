#!/usr/bin/env python3
"""Take the padding out of wide Markdown tables.

    tools/docs-tables.py [--width N] [FILE...]    (default: every doc, 100)

prettier aligns table columns, so one long cell pads every row of its table
to that width: a quarter of docs/research/ was spaces. A table with a line
longer than N columns is rewritten with single spaces and marked
`<!-- prettier-ignore -->` so that the formatter leaves it alone; narrower
tables stay aligned. Cell text is untouched (tools/docs-check.py split
compares lines with whitespace normalised). Run it again after adding rows
to a marked table.
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = "<!-- prettier-ignore -->"
ROW = re.compile(r"^( *)\|")
RULE = re.compile(r"^ *\|[ :|\-]+\| *$")


def compact_row(line):
    indent = ROW.match(line).group(1)
    body = line.strip()
    if RULE.match(line):
        body = re.sub(r"-{3,}", "---", body)
    # keep the inside of code spans as written
    parts = re.split(r"(`[^`]*`)", body)
    parts = [p if p.startswith("`") else re.sub(r" {2,}", " ", p) for p in parts]
    return indent + "".join(parts)


def compact(text, width):
    lines = text.split("\n")
    out = []
    i = 0
    fenced = False
    changed = 0
    while i < len(lines):
        line = lines[i]
        if line.lstrip().startswith("```"):
            fenced = not fenced
        if fenced or not ROW.match(line):
            out.append(line)
            i += 1
            continue
        j = i
        while j < len(lines) and ROW.match(lines[j]):
            j += 1
        table = lines[i:j]
        marked = bool(out) and out[-1].strip() == MARK
        if len(table) >= 2 and RULE.match(table[1]) and (marked or max(map(len, table)) > width):
            new = [compact_row(r) for r in table]
            if not marked:
                out.append(ROW.match(table[0]).group(1) + MARK)
                changed += 1
            elif new != table:
                changed += 1
            out.extend(new)
        else:
            out.extend(table)
        i = j
    return "\n".join(out), changed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--width", type=int, default=100)
    a = ap.parse_args()
    files = a.files or (
        [os.path.join(ROOT, "AGENTS.md")]
        + sorted(glob.glob(os.path.join(ROOT, "docs/**/*.md"), recursive=True))
    )
    for f in files:
        text = open(f, encoding="utf-8").read()
        new, changed = compact(text, a.width)
        if new != text:
            open(f, "w", encoding="utf-8").write(new)
            print(f"{os.path.relpath(f, ROOT)}: {changed} table(s), {len(text) - len(new)} bytes less")
    return 0


if __name__ == "__main__":
    sys.exit(main())
