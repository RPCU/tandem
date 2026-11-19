#!/usr/bin/env python3
"""Check that scripts/tandem/Natives.reds matches plugin/src/natives.cpp, and
StoryNatives.reds matches story_natives.cpp and CombatNatives.reds combat_natives.cpp
(checked together: one name space).

redscript compiles a native declaration without seeing the DLL, so a type
that differs (a Quaternion where the C++ takes a float) builds fine and
breaks the script VM's frames in game. Native structs must also stay 8-byte
aligned: the VM aligns struct locals to 8, and a 16-byte aligned field
(Vector4, Quaternion) makes the zeroing movaps fault.

Usage: tools/check-natives.py   (exit 1 on a mismatch)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP = [
    ROOT / "plugin/src/natives.cpp",
    ROOT / "plugin/src/story_natives.cpp",
    ROOT / "plugin/src/combat_natives.cpp",
    ROOT / "plugin/src/sync_natives.cpp",
    ROOT / "plugin/src/elsewhere_natives.cpp",
    ROOT / "plugin/src/progress_natives.cpp",
    ROOT / "plugin/src/trade_natives.cpp",
]
REDS = [
    ROOT / "scripts/tandem/Natives.reds",
    ROOT / "scripts/tandem/StoryNatives.reds",
    ROOT / "scripts/tandem/CombatNatives.reds",
    ROOT / "scripts/tandem/SyncNatives.reds",
    ROOT / "scripts/tandem/ElsewhereNatives.reds",
    ROOT / "scripts/tandem/ProgressNatives.reds",
    ROOT / "scripts/tandem/TradeNatives.reds",
]

SCALARS = {
    "void": "Void",
    "bool": "Bool",
    "float": "Float",
    "double": "Double",
    "std::int32_t": "Int32",
    "std::uint32_t": "Uint32",
    "std::int64_t": "Int64",
    "std::uint64_t": "Uint64",
    "std::uint8_t": "Uint8",
    "Red::CName": "CName",
    "Red::CString": "String",
    "Red::TweakDBID": "TweakDBID",
    "Red::EntityID": "EntityID",
    "Red::Vector4": "Vector4",
    "Red::Quaternion": "Quaternion",
    "Red::CRUID": "CRUID",
    "Red::NodeRef": "NodeRef",
}
# 16-byte aligned types: never fields of a native struct.
ALIGNED16 = {"Red::Vector4", "Red::Quaternion"}


def reds_of(cpp: str) -> str:
    cpp = re.sub(r"\s+", " ", cpp.replace("const ", "").replace("&", "")).strip()
    if cpp in SCALARS:
        return SCALARS[cpp]
    if m := re.fullmatch(r"Red::DynArray<(.+)>", cpp):
        return f"array<{reds_of(m.group(1))}>"
    if re.fullmatch(r"Red::(Weak)?Handle<.+>", cpp):
        return "ref"  # any ref<…>/wref<…> in the script
    if m := re.fullmatch(r"(Tandem\w+)", cpp):
        return m.group(1)
    return f"?{cpp}"


def normal(reds: str) -> str:
    reds = reds.strip()
    if re.fullmatch(r"w?ref<.+>", reds):
        return "ref"
    if m := re.fullmatch(r"array<(.+)>", reds):
        return f"array<{normal(m.group(1))}>"
    return reds


def split_params(text: str) -> list[str]:
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


def cpp_natives(src: str) -> dict[str, tuple[list[str], str]]:
    body = src[src.index("struct TandemNative") :]
    out = {}
    for m in re.finditer(r"^  static ([^(]+?)\s*\n?\s*(\w+)\(([^)]*)\)\s*\{", body, re.M):
        ret, name, params = m.group(1), m.group(2), m.group(3)
        types = [reds_of(p.rsplit(" ", 1)[0]) for p in split_params(params)]
        out[name] = (types, reds_of(ret))
    return out


def reds_natives(src: str) -> dict[str, tuple[list[str], str]]:
    out = {}
    for m in re.finditer(
        r"native func (\w+)\(([^)]*)\)(?:\s*->\s*([\w<>]+))?", src, re.M | re.S
    ):
        name, params, ret = m.group(1), m.group(2), m.group(3) or "Void"
        types = [normal(p.split(":", 1)[1]) for p in split_params(params)]
        out[name] = (types, normal(ret))
    return out


def cpp_structs(src: str) -> dict[str, list[tuple[str, str]]]:
    out = {}
    for m in re.finditer(r"^struct (Tandem\w+) \{\n(.*?)^\};", src, re.M | re.S):
        if m.group(1) == "TandemNative":
            continue
        fields = []
        for line in m.group(2).splitlines():
            line = re.sub(r"//.*", "", line).strip().rstrip(";")
            if not line or "(" in line:
                continue
            line = re.sub(r"\{[^}]*\}", "", line)
            kind, names = line.split(" ", 1) if " " in line else (line, "")
            for name in names.split(","):
                fields.append((kind, name.strip()))
        out[m.group(1)] = fields
    return out


def reds_structs(src: str) -> dict[str, list[tuple[str, str]]]:
    out = {}
    for m in re.finditer(r"native struct (\w+) \{(.*?)^\}", src, re.M | re.S):
        out[m.group(1)] = [
            (normal(f.group(2)), f.group(1))
            for f in re.finditer(r"native let (\w+): ([\w<>]+);", m.group(2))
        ]
    return out


def main() -> int:
    cpp = "\n".join(path.read_text() for path in CPP if path.exists())
    reds = "\n".join(path.read_text() for path in REDS if path.exists())
    errors = []
    a, b = cpp_natives(cpp), reds_natives(reds)
    registered = set(re.findall(r"RTTI_METHOD\((\w+)\)", cpp))
    for name in sorted(set(a) | set(b)):
        if name not in b:
            if name in registered:
                errors.append(f"{name}: in natives.cpp, not in Natives.reds")
        elif name not in a:
            errors.append(f"{name}: in Natives.reds, not in natives.cpp")
        elif a[name] != b[name]:
            errors.append(
                f"{name}: C++ ({', '.join(a[name][0])}) -> {a[name][1]}, "
                f"script ({', '.join(b[name][0])}) -> {b[name][1]}"
            )
    for name in sorted(b):
        if name in a and name not in registered:
            errors.append(f"{name}: declared, but no RTTI_METHOD")
    sa, sb = cpp_structs(cpp), reds_structs(reds)
    for name in sorted(set(sa) & set(sb)):
        for kind, field in sa[name]:
            if kind in ALIGNED16:
                errors.append(f"{name}.{field}: {kind} is 16-byte aligned (the VM aligns to 8)")
        want = [(reds_of(k), f) for k, f in sa[name]]
        if want != sb[name]:
            errors.append(f"{name}: C++ fields {want}, script fields {sb[name]}")
    for message in errors:
        print(f"natives: {message}")
    print(f"natives: {len(b)} functions, {len(sb)} structs checked, {len(errors)} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
