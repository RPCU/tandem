"""Address-library lookups (RED4ext's cyberpunk2077_addresses.json, copied to .cache/re).

    tools/re/run tools/re/addr.py 0x1401b3c08     # address -> hash (for UniversalRelocFunc)
    tools/re/run tools/re/addr.py 1245708413      # hash -> address
"""

import sys

from mem import addresses

A = addresses()
arg = sys.argv[1]
if arg.startswith("0x"):
    va = int(arg, 16)
    hits = [(h, s) for h, (a, s) in A.items() if a == va]
    for h, s in hits:
        print(f"{va:#x}  hash {h} ({h:#x})  {s or ''}")
    if not hits:
        print("not in the address library (not a function start?)")
else:
    a, s = A[int(arg)]
    print(f"hash {arg}  {a:#x}  {s or ''}")
