#!/usr/bin/env python3
"""Fail on a method or function defined twice in scripts/tandem.

redscript 0.5 compiles a class with two definitions of the same method
without a word, and the game then dies in the script VM's frame allocator
the first time it is called ("Detected integer overflow for offset…",
dynamicBuffer.cpp: a frozen or crashed game right after loading, twice on
2026-11-18, from a merge that kept both sides' copy of one method).
Annotated functions (@wrapMethod, @addMethod…) are told apart by their
annotation's class.

Usage: tools/scripts-duplicates.py [DIR]   (exit 1 on a duplicate)
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FUNC = re.compile(r"\s*(?:(?:public|private|protected|static|final|native|cb|exec|quest)\s+)*func\s+(\w+)\s*\(")
TYPE = re.compile(r"\s*(?:(?:public|private|protected|native|abstract|final|importonly)\s+)*(?:class|struct)\s+(\w+)")
ANNOTATION = re.compile(r"\s*@(\w+)\(([^)]*)\)")


def main() -> int:
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "scripts/tandem"
    seen = defaultdict(list)
    for path in sorted(directory.rglob("*.reds")):
        owner, depth, annotation = None, 0, None
        for number, line in enumerate(path.read_text().splitlines(), 1):
            code = line.split("//", 1)[0]
            if m := ANNOTATION.match(code):
                annotation = f"@{m.group(1)}({m.group(2).strip()})"
            if depth == 0 and (m := TYPE.match(code)):
                owner = m.group(1)
            if depth <= 1 and (m := FUNC.search(code)) and code.lstrip().startswith(("public", "private", "protected", "static", "final", "native", "cb", "exec", "quest", "func")):
                scope = owner if depth == 1 else (annotation or "")
                # A wrap may be repeated on purpose (each wraps the one before).
                if not (annotation or "").startswith(("@wrapMethod", "@replaceMethod")) or depth == 1:
                    seen[(scope, m.group(1))].append(f"{path.relative_to(directory)}:{number}")
                annotation = None
            elif code.strip() and not ANNOTATION.match(code):
                annotation = None
            depth += code.count("{") - code.count("}")
            if depth == 0:
                owner = None
    problems = {key: where for key, where in seen.items() if len(where) > 1}
    for (scope, name), where in sorted(problems.items()):
        print(f"scripts: {scope + '.' if scope else ''}{name} is defined {len(where)} times: {', '.join(where)}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
