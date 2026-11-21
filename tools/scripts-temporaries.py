#!/usr/bin/env python3
"""Fail on a value read through a temporary in scripts/tandem.

The game's scc compiles them without a word, but the script VM reads such
an operand by address, and a call's result has none: it reads some other
variable. redscript's own diagnostic for this (INVALID_TEMP_USE,
compiler/src/diagnostics/invalid_temp_use.rs) is not among the passes the
game's scc runs. Seen in game: `ArrayContains(system.PuppetPlayers(), p)`
was false for a player in the list, so the police forgot every remote V's
level on the tick they learnt it (2026-11-21); `Labels()[i]` read an empty
String (2026-11-20, tools/menu-check.py). The vanilla scripts never do
either. Refused here:

- an array intrinsic (ArraySize, ArrayContains, ArrayCount, ArrayFindFirst,
  ArrayFindLast, ArrayLast, ArrayPop) on anything but a variable, a field
  or an element of one: `ArraySize(f())`, `ArrayContains(a.GetTags(), t)`;
- a field of a struct a call returned: `GetWorldPosition().X`,
  `TandemNative.SessionStatus().role` (the callee's declared return type is
  looked up in our scripts, the vanilla dump when it is there, and the
  math structs).

Put the value in a local first: `let tags = record.GameplayTags();`.
A field of a class a call returned (`GetAllBlackboardDefs().UIGameData`)
is fine: the handle is the address.

Usage: tools/scripts-temporaries.py [DIR]   (exit 1 on a temporary read)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VANILLA = Path.home() / ".cache/tandem/ref/gamescripts/vanilla/final.reds"
INTRINSICS = ("ArraySize", "ArrayContains", "ArrayCount", "ArrayFindFirst",
              "ArrayFindLast", "ArrayLast", "ArrayPop")
MATH = {"Vector2", "Vector3", "Vector4", "Quaternion", "EulerAngles", "WorldPosition",
        "WorldTransform", "Transform", "EntityID", "ItemID", "Color", "HDRColor"}
STRUCT = re.compile(r"^\s*(?:(?:public|private|protected|final|importonly|native|abstract)\s+)*struct\s+(\w+)", re.M)
FUNC = re.compile(r"\bfunc\s+(\w+)\s*\([^()]*\)\s*->\s*([\w<>]+)")
CALL = re.compile(r"\b(" + "|".join(INTRINSICS) + r")\s*\(")
FIELD = re.compile(r"\b(\w+)\s*(\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\))\s*\.\s*(\w+)\b(?!\s*\()")
VARIABLE = re.compile(r"^(?:this|\w+)(?:\s*\.\s*\w+|\s*\[[^\[\]]*\])*$")


def strip(text: str) -> str:
    """The code without comments and string literals, same offsets and lines."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text[i] == '"':
            # Blanked, but for the interpolations of s"...\(expr)...": code.
            out.append(" ")
            j = i + 1
            while j < n and text[j] != '"':
                if text.startswith("\\(", j):
                    depth, k = 1, j + 2
                    while k < n and depth:
                        depth += {"(": 1, ")": -1}.get(text[k], 0)
                        k += 1
                    out.append("  " + text[j + 2:k - 1] + " ")
                    j = k
                elif text[j] == "\\":
                    out.append("  ")
                    j += 2
                else:
                    out.append("\n" if text[j] == "\n" else " ")
                    j += 1
            out.append(" ")
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def first_argument(code: str, start: int) -> str:
    """The text of the first argument of the call whose '(' is at start."""
    depth, i = 0, start
    while i < len(code):
        c = code[i]
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
            if depth == 0:
                return code[start + 1:i]
        elif c == "," and depth == 1:
            return code[start + 1:i]
        i += 1
    return code[start + 1:]


def main() -> int:
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "scripts/tandem"
    files = sorted(directory.rglob("*.reds"))
    sources = {path: strip(path.read_text()) for path in files}
    structs = set(MATH)
    returns = {}
    texts = list(sources.values())
    if VANILLA.exists():
        texts.append(VANILLA.read_text(errors="replace"))
    for text in texts:
        structs.update(STRUCT.findall(text))
        for name, kind in FUNC.findall(text):
            returns.setdefault(name, set()).add(kind)
    found = 0
    for path, code in sources.items():
        def line_of(offset: int) -> int:
            return code.count("\n", 0, offset) + 1

        for m in CALL.finditer(code):
            argument = first_argument(code, m.end() - 1).strip()
            if argument and not VARIABLE.match(argument):
                print(f"{path}:{line_of(m.start())}: {m.group(1)}({argument}...) reads a "
                      f"temporary: put it in a local first", file=sys.stderr)
                found += 1
        for m in FIELD.finditer(code):
            name, field = m.group(1), m.group(3)
            if name in INTRINSICS or name in ("if", "while", "for", "switch", "return"):
                continue
            kinds = returns.get(name, set()) - {"Void"}
            if kinds and all(k in structs for k in kinds):
                print(f"{path}:{line_of(m.start())}: {name}(...).{field} reads a field of a "
                      f"returned struct: put it in a local first", file=sys.stderr)
                found += 1
    if found:
        print(f"scripts-temporaries: {found} temporary read(s)", file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
