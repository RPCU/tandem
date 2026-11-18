"""address<TAB>name for Ghidra: address-library symbols, RTTI vtables, classes, natives."""

import json
import re

from mem import CACHE, addresses

clean = lambda s: re.sub(r"[^A-Za-z0-9_]", "_", s)
out = {}
R = json.loads((CACHE / "rtti.json").read_text())
for n, x in R["classes"].items():
    for i, v in enumerate(x.get("vtables", [])):
        out[v] = f"vtbl_{clean(n)}" + (f"_{i}" if i else "")
    out[x["type"]] = f"class_{clean(n)}"
    for f in x["funcs"] + x["staticFuncs"]:
        if f.get("handler"):
            out.setdefault(f["handler"], f"native_{clean(n)}_{clean(f['name'])}")
for f in R["globals"]:
    if f.get("handler"):
        out.setdefault(f["handler"], f"native_{clean(f['name'])}")
for a, s in addresses().values():
    if s:
        out[a] = clean(s)
out[R["rtti"]] = "g_rttiSystem"
out[R["namePool"]] = "g_namePool"
out[R["handlers"]] = "g_nativeHandlers"
for a, n in sorted(out.items()):
    print(f"{a:x}\t{n}")
