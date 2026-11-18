"""Who references an address of the exe (calls, jumps, lea, mov through rip).

    tools/re/run tools/re/xref.py 0x1403a9c80
"""

import sys

from mem import Exe

exe = Exe()
for f, xs in sorted(exe.callers(int(sys.argv[1], 16)).items()):
    chunk = exe.func_of(f) if f else None
    size = chunk[1] - chunk[0] if chunk else 0
    print(f"{f:#x}  size {size:#x}  at {' '.join(f'{x:#x}' for x in xs[:6])}")
