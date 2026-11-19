#!/usr/bin/env python3
"""Fail on a value-type local declared without an initializer in scripts/tandem.

The script VM does not clear such a local: `let record: TweakDBID;` read on
a path that never assigned it held whatever the VM's slot held before (on
2026-11-18 SendSeat sent a perk's record as V's car on foot, in every
session, and the puppets of an echoed seat sat in a phantom car).
docs/research/engine-knowledge.md §13.8 has the details. Handles (ref<>,
wref<>), arrays and strings are constructed by the VM and are not checked;
the value types below are, with enums (gamedata*, E*, ours).

Write `let x = TDBID.None();`, `n"None"`, `EntityID.FromHash(0ul)`,
`new Vector4(...)`, `0`, `false`... A local that is assigned on every path
before it is read (an out parameter filled at once, every field set) may
keep its bare declaration with the comment `// assigned on every path` on
the same line.

Usage: tools/scripts-uninitialized.py [DIR]   (exit 1 on a bare value local)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALUE_TYPES = {
    "Bool", "Int8", "Int16", "Int32", "Int64", "Uint8", "Uint16", "Uint32", "Uint64",
    "Float", "Double", "CName", "TweakDBID", "EntityID", "ItemID", "Vector2", "Vector3",
    "Vector4", "Quaternion", "EulerAngles", "WorldPosition", "WorldTransform", "Color",
}
BARE = re.compile(r"^\s*let\s+(\w+)\s*:\s*([\w<>]+)\s*;")
ENUM = re.compile(r"^\s*(?:public\s+|private\s+)?enum\s+(\w+)")
OPT_OUT = "assigned on every path"


def is_enum(name: str, ours: set) -> bool:
    return name in ours or name.startswith("gamedata") or re.match(r"^E[A-Z]", name) is not None


def main() -> int:
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "scripts/tandem"
    files = sorted(directory.rglob("*.reds"))
    ours = set()
    for path in files:
        for line in path.read_text().splitlines():
            if m := ENUM.match(line):
                ours.add(m.group(1))
    found = 0
    for path in files:
        for number, line in enumerate(path.read_text().splitlines(), 1):
            m = BARE.match(line.split("//", 1)[0])
            if not m or OPT_OUT in line:
                continue
            kind = m.group(2)
            if kind in VALUE_TYPES or is_enum(kind, ours):
                print(f"{path}:{number}: '{m.group(1)}: {kind}' has no initializer "
                      f"(the VM leaves it as it was; give it a value, or mark it "
                      f"'// {OPT_OUT}')", file=sys.stderr)
                found += 1
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
