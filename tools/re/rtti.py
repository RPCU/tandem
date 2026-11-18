"""Query the RTTI dump (.cache/re/rtti.json, written by rtti_dump.py).

    tools/re/run tools/re/rtti.py class entAnimatedComponent   # layout, functions, vtable
    tools/re/run tools/re/rtti.py find 'anim.*Pose'            # class and enum names (regex)
    tools/re/run tools/re/rtti.py func 'GetSlotTransform'      # functions by name (regex)
    tools/re/run tools/re/rtti.py vtable 0x1430c1160           # the class of a vtable address
    tools/re/run tools/re/rtti.py enum animLookAtStyle
    tools/re/run tools/re/rtti.py export                       # docs/research/data/*.tsv
"""

import json
import re
import sys
from functools import cache

from mem import CACHE, ROOT


@cache
def load():
    return json.loads((CACHE / "rtti.json").read_text())


def chain(name):
    c = load()["classes"]
    while name:
        yield name
        name = c[name]["parent"]


def signature(f):
    params = ", ".join(f"{p['name']}: {p['type']}" for p in f["params"])
    ret = f" -> {f['returns']}" if "returns" in f else ""
    where = f"  @ {f['handler']:#x}" if f.get("handler") else ("  native, no handler" if f["flags"] & 1 else "")
    return f"{f['name']}({params}){ret}{where}"


def show_class(name, inherited=False):
    c = load()["classes"]
    x = c[name]
    vt = " ".join(f"{v:#x}" for v in x.get("vtables", [])) or "-"
    print(f"{name} : {' : '.join(list(chain(name))[1:])}")
    print(f"  size {x['size']:#x}  align {x['align']}  flags {x['flags']:#x}  type {x['type']:#x}  instance vtable {vt}")
    for n in reversed(list(chain(name))) if inherited else [name]:
        for p in sorted(c[n]["props"], key=lambda p: p["offset"]):
            print(f"  +{p['offset']:#06x}  {p['name']}: {p['type']}" + (f"   ({n})" if n != name else ""))
    for f in x["funcs"] + x["staticFuncs"]:
        print("  " + signature(f))


def main():
    r = load()
    cmd, arg = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else "")
    if cmd == "class":
        show_class(arg, inherited="-i" in sys.argv)
    elif cmd == "find":
        for kind in ("classes", "enums"):
            for n in sorted(r[kind]):
                if re.search(arg, n, re.I):
                    x = r[kind][n]
                    print(f"{kind[:-2]:5} {n}" + (f"  size {x['size']:#x}  : {x['parent']}" if kind == "classes" else ""))
    elif cmd == "func":
        for n, x in sorted(r["classes"].items()):
            for f in x["funcs"] + x["staticFuncs"]:
                if re.search(arg, f["name"], re.I):
                    print(f"{n}.{signature(f)}")
        for f in r["globals"]:
            if re.search(arg, f["name"], re.I):
                print(signature(f))
    elif cmd == "vtable":
        a = int(arg, 16)
        best = max(((v, n) for n, x in r["classes"].items() for v in x.get("vtables", []) if v <= a), default=None)
        print(f"{best[1]} vtable {best[0]:#x} + {a - best[0]:#x}" if best else "none")
    elif cmd == "enum":
        for k, v in r["enums"][arg].items():
            print(f"  {v:6}  {k}")
    elif cmd == "export":
        data = ROOT / "docs" / "research" / "data"
        with open(data / "rtti-vtables-2.50.tsv", "w") as f:
            f.write("# class\tparent\tsize\tinstance vtable(s)   (game 2.50, tools/re/rtti_dump.py)\n")
            for n, x in sorted(r["classes"].items()):
                if "vtables" in x:
                    f.write(f"{n}\t{x['parent']}\t{x['size']:#x}\t{' '.join(f'{v:#x}' for v in x['vtables'])}\n")
        with open(data / "rtti-natives-2.50.tsv", "w") as f:
            f.write("# function\thandler address   (game 2.50; handler(context, frame, out, outType))\n")
            for n, x in sorted(r["classes"].items()):
                for fn in x["funcs"] + x["staticFuncs"]:
                    if fn.get("handler"):
                        f.write(f"{n}.{fn['name']}\t{fn['handler']:#x}\n")
            for fn in sorted(r["globals"], key=lambda fn: fn["name"]):
                if fn.get("handler"):
                    f.write(f"{fn['name']}\t{fn['handler']:#x}\n")
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
