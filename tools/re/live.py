"""Look at live objects of the running game (read-only).

    tools/re/run tools/re/live.py player              # the local V: entity, components, animation
    tools/re/run tools/re/live.py entity 0x596824660  # components of any entity
    tools/re/run tools/re/live.py anim 0x5973428f0    # animation objects of an entAnimatedComponent
    tools/re/run tools/re/live.py instances gamePuppet [max]   # live objects of a native class
    tools/re/run tools/re/live.py dump 0x19f4fb280 0x100       # annotated qwords

Needs .cache/re/rtti.json (rtti_dump.py). Layout facts used here are in
docs/research/engine-knowledge.md §13 (game 2.50).
"""

import json
import struct
import sys
from functools import cache

from mem import CACHE, Mem, names

PLAYER_RIG = 7304775167351189356  # first-person player rig (path hash)

m = Mem()
R = json.loads((CACHE / "rtti.json").read_text())
VT = {v: n for n, x in R["classes"].items() for v in x.get("vtables", [])}
TYPE = {x["type"]: n for n, x in R["classes"].items()}
RDATA = (0x142FF3000, 0x143903000)


@cache
def N():
    return names()


def cls(a):
    """Class of the object at a: IScriptable's class pointer (+0x30), else its vtable."""
    head = m.read(a, 0x38) if a else None
    if not head:
        return None
    vt, t = struct.unpack_from("<Q", head)[0], struct.unpack_from("<Q", head, 0x30)[0]
    if vt in VT:
        return TYPE.get(t) or VT[vt]
    return f"vt:{vt:#x}" if RDATA[0] <= vt < RDATA[1] else None


def note(v):
    if RDATA[0] <= v < RDATA[1]:
        return "-> rdata " + VT.get(v, "")
    if 0x140001000 <= v < RDATA[0]:
        return "-> code"
    if v > 0xFFFFFFFF and v in N():
        return f'name "{N()[v]}"'
    if 0x10000 < v < 1 << 47 and m.read(v, 8) is not None:
        return "-> " + (cls(v) or "ptr")
    return ""


def dump(a, n):
    d = m.read(a, n)
    if not d:
        print("unreadable")
        return
    for o in range(0, n - 7, 8):
        v = struct.unpack_from("<Q", d, o)[0]
        if v:
            f = struct.unpack_from("<2f", d, o)
            what = note(v) or f"f {f[0]:.4g} {f[1]:.4g}"
            print(f"  +{o:#06x} {v:#018x} {what}")


def instances(name, limit=20000):
    out = []
    for vt in R["classes"][name].get("vtables", [])[:1]:
        out += m.scan(struct.pack("<Q", vt), align=8, limit=limit, writable=True)
    # A live ISerializable points at itself at +0x08 (its weak self reference).
    return [a for a in out if m.u64(a + 8) == a]


def player_component():
    for c in instances("entAnimatedComponent"):
        if m.u64(c + 0x138) == PLAYER_RIG:
            return c
    return None


def components(entity):
    # entEntity +0x70 componentsStorage, its DynArray<Handle<IComponent>> at +0x30.
    ptr, _, count = m.unpack(entity + 0xA0, "<QII")
    raw = m.read(ptr, 16 * count) if count and count < 4096 else b""
    return [struct.unpack_from("<Q", raw, 16 * i)[0] for i in range(len(raw) // 16)]


def show_entity(entity):
    print(f"entity {entity:#x}  {cls(entity)}")
    for c in components(entity):
        name = N().get(m.u64(c + 0x40) or 0, "")
        print(f"  {c:#x}  {cls(c)}  {name}")


def show_anim(comp):
    rig_token, graph_token = m.u64(comp + 0x140), m.u64(comp + 0x158)
    rig = m.u64(rig_token + 0x28) if rig_token else 0
    graph = m.u64(graph_token + 0x28) if graph_token else 0
    gi, obj = m.u64(comp + 0x270), m.u64(comp + 0x178)
    print(f"component {comp:#x}  entity {m.u64(comp + 0x50):#x}")
    print(f"  rig {rig:#x} ({m.u32(rig + 0x5C) if rig else 0} bones, path hash {m.u64(comp + 0x138)})")
    print(f"  graph {graph:#x} (path hash {m.u64(comp + 0x150)})")
    if gi:
        print(
            f"  graph instance (+0x270) {gi:#x}: graph {m.u64(gi + 0x20):#x}, instance data {m.u64(gi + 0x28):#x}, "
            f"rig {m.u64(gi + 0x40):#x}, updates {m.u32(gi + 0x2540)}, samples {m.u32(gi + 0x2544)}"
        )
    if not obj:
        print("  no animated object (+0x178): not this entity's root animated component")
        return
    skel, pose = m.u64(obj + 8), m.u64(obj + 0x18)
    n, tracks = m.u32(skel + 8), m.u32(skel + 0x48)
    print(f"  animated object (+0x178) {obj:#x}: id {m.u32(obj):#x}, graph instance {m.u64(obj + 0x60):#x}")
    print(f"  skeleton {skel:#x}: {n} transforms, {tracks} tracks, signature {m.u64(skel + 0x60):#x}")
    print(f"  pose {pose:#x}: transforms {m.u64(pose):#x} ({m.u32(pose + 8)}), tracks {m.u64(pose + 0x18):#x}")
    bone = [N().get(h, hex(h)) for h in struct.unpack(f"<{n}Q", m.read(m.u64(skel + 0x20), 8 * n))]
    parent = struct.unpack(f"<{n}h", m.read(m.u64(skel + 0x10), 2 * n))
    data = m.read(m.u64(pose), 48 * min(n, 32))
    for i in range(min(n, 32)):
        t = struct.unpack_from("<12f", data, 48 * i)
        print(
            f"    {i:3} {bone[i]:<18} parent {parent[i]:3}  "
            f"T {t[0]:7.3f} {t[1]:7.3f} {t[2]:7.3f}  R {t[4]:6.3f} {t[5]:6.3f} {t[6]:6.3f} {t[7]:6.3f}"
        )
    if "-bones" in sys.argv:
        for i in range(n):
            print(f"    {i:3} {bone[i]} <- {parent[i]}")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "player":
        comp = player_component()
        if not comp:
            sys.exit("no component with the player's rig: not in a loaded game?")
        show_entity(m.u64(comp + 0x50))
        show_anim(comp)
    elif cmd == "entity":
        show_entity(int(sys.argv[2], 16))
    elif cmd == "anim":
        show_anim(int(sys.argv[2], 16))
    elif cmd == "instances":
        found = instances(sys.argv[2])
        print(len(found), "instances")
        for a in found[: int(sys.argv[3]) if len(sys.argv) > 3 else 40]:
            print(f"  {a:#x}  {cls(a)}")
    elif cmd == "dump":
        dump(int(sys.argv[2], 16), int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x100)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
