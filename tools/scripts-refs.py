#!/usr/bin/env python3
"""Check what scripts/tandem refers to and only the game would miss.

`task scripts:check` compiles the scripts, but needs the game's own (it
does not run in CI), and a TweakDB record is a name the compiler never
sees. Checked here, from this tree alone:

- every static call `TandemX.Name(...)` of a class of ours names a function
  that class (or a parent of ours) declares: a native removed from its
  Natives.reds, a helper renamed in one file only;
- every record of ours the scripts name ("Character.TandemPuppetWa",
  t"Mappins.TandemPingMappin") is defined in tweaks/tandem, or begins one
  that is (a name finished at run time): a missing record spawns nothing,
  silently. A file that declares a scripted tweak (a class that extends
  TweakXL's ScriptableTweak) creates records itself: the ones it names are
  defined there.

Usage: tools/scripts-refs.py   (exit 1 on a finding)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TYPE = re.compile(r"^[ \t]*(?:(?:public|private|protected|native|abstract|final|importonly)\s+)*(?:class|struct)\s+(\w+)(?:\s+extends\s+(\w+))?", re.M)
FUNC = re.compile(r"^[ \t]*(?:(?:public|private|protected|static|final|native|cb|exec|quest|const)\s+)*func\s+(\w+)\s*\(", re.M)
ENUM = re.compile(r"^[ \t]*(?:(?:public|private)\s+)?enum\s+(\w+)", re.M)


def strip(text: str) -> str:
    """Comments and string contents blanked, lines kept."""
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def main() -> int:
    scripts = ROOT / "scripts/tandem"
    parents, methods, enums, sources = {}, {}, set(), {}
    for path in sorted(scripts.rglob("*.reds")):
        raw = path.read_text()
        code = strip(raw)
        sources[path] = (raw, code)
        enums.update(ENUM.findall(code))
        # Each type's span: to the next type or annotation at column 0.
        marks = [(m.start(), m.group(1), m.group(2) or "") for m in TYPE.finditer(code)]
        for i, (start, name, parent) in enumerate(marks):
            parents[name] = parent
            depth, end = 0, len(code)
            for j in range(code.index("{", start), len(code)):
                depth += {"{": 1, "}": -1}.get(code[j], 0)
                if depth == 0:
                    end = j
                    break
            methods.setdefault(name, set()).update(FUNC.findall(code[start:end]))
        for m in re.finditer(r"^@addMethod\(\s*(\w+)\s*\)\s*\n[^\n]*?func\s+(\w+)", code, re.M):
            methods.setdefault(m.group(1), set()).add(m.group(2))

    def declares(cls: str, name: str) -> bool:
        seen = set()
        while cls and cls not in seen:
            seen.add(cls)
            if name in methods.get(cls, ()):
                return True
            cls = parents.get(cls, "")
        return False

    records = set()
    for path in (ROOT / "tweaks/tandem").rglob("*.tweak"):
        records.update(re.findall(r"^([A-Za-z_]\w*\.\w+)\s*(?::|\{)", path.read_text(), re.M))
    for path, (raw, code) in sources.items():
        if re.search(r"\bextends\s+ScriptableTweak\b", code):
            records.update(re.findall(r'"([A-Za-z_]\w*\.\w*Tandem\w*)"', raw))
    problems = 0
    for path, (raw, code) in sources.items():
        for number, line in enumerate(code.splitlines(), 1):
            for cls, name in re.findall(r"(?<![\w.])(Tandem\w*)\.(\w+)\s*\(", line):
                if cls in parents and cls not in enums and not declares(cls, name):
                    print(f"{path.relative_to(ROOT)}:{number}: {cls}.{name} is declared nowhere in scripts/tandem", file=sys.stderr)
                    problems += 1
        for number, line in enumerate(raw.splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            for record in re.findall(r'"([A-Za-z_]\w*\.\w*Tandem\w*)"', line):
                if not any(known == record or known.startswith(record) for known in records):
                    print(f"{path.relative_to(ROOT)}:{number}: record {record} is not defined in tweaks/tandem", file=sys.stderr)
                    problems += 1
    if problems:
        print(f"scripts-refs: {problems} problem(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
