"""Disassemble a function of the exe, annotated with what its calls and
rip-relative operands reach (functions, RTTI vtables, strings).

    tools/re/run tools/re/asm.py 0x140ca2200 [max bytes, default 0x600]
"""

import json
import re
import sys

from mem import CACHE, Exe, addresses

exe = Exe()
try:
    R = json.loads((CACHE / "rtti.json").read_text())
except FileNotFoundError:
    R = {"classes": {}, "globals": []}
labels = {a: s for a, s in addresses().values() if s}
for n, x in R["classes"].items():
    for i, v in enumerate(x.get("vtables", [])):
        labels[v] = f"vtable {n}" + (f"#{i}" if i else "")
    labels[x["type"]] = f"class {n}"
    for f in x["funcs"] + x["staticFuncs"]:
        if f.get("handler"):
            labels.setdefault(f["handler"], f"native {n}.{f['name']}")
for f in R["globals"]:
    if f.get("handler"):
        labels.setdefault(f["handler"], f"native {f['name']}")


def describe(a):
    if a in labels:
        return labels[a]
    sec = exe.section(a)
    if sec == ".text":
        f = exe.func_of(a)
        return f"sub_{a:x}" if f and f[0] == a else (f"sub_{f[0]:x}+{a - f[0]:#x}" if f else "code")
    if sec in (".rdata", "_RDATA"):
        raw = exe.read(a, 64) or b""
        s = raw.split(b"\0")[0]
        if len(s) >= 4 and all(32 <= c < 127 for c in s):
            return '"' + s.decode() + '"'
        v = exe.u64(a)
        if v and exe.section(v) == ".text":
            return f"rdata (vtable? first -> {v:#x})"
        return "rdata"
    return sec or ""


def main():
    va = int(sys.argv[1], 16)
    limit = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x600
    f = exe.func_of(va)
    end = min(f[1], va + limit) if f else va + limit
    print(f"; {describe(va)}  {va:#x}..{end:#x}")
    for i in exe.disasm(va, end - va, stop_at_ret=False):
        note = ""
        m = re.search(r"\[rip ([+-]) (0x[0-9a-f]+)\]", i.op_str)
        if m:
            t = i.address + i.size + int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)
            note = f"  ; {t:#x} {describe(t)}"
        elif i.mnemonic in ("call", "jmp") and i.op_str.startswith("0x"):
            t = int(i.op_str, 16)
            if not (f and f[0] <= t < f[1]):
                note = f"  ; {describe(t)}"
        print(f"{i.address:x}  {i.mnemonic} {i.op_str}{note}")


if __name__ == "__main__":
    main()
