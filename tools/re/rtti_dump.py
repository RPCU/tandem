"""Dump the running game's RTTI: types, classes, properties, functions with the
addresses of their native handlers, enums, and each native class's instance
vtable. Read-only (see mem.py).

    tools/re/run tools/re/rtti_dump.py

Writes .cache/re/rtti.json (everything) and .cache/re/names.txt (the name
pool); `rtti.py` queries them. Layouts are RED4ext.SDK's (RTTISystem.hpp,
RTTITypes.hpp, Scripting/Functions.hpp), checked against the live process.
Addresses in the exe image are stable between runs (no relocation under
Proton: the image loads at its preferred base 0x140000000).
"""

import json
import struct
import sys

from mem import CACHE, IMAGE, Exe, Mem, addresses

RTTI_GET = 0x4A610F64  # CRTTISystem::Get
NAMEPOOL_GET = 0x68DF07DC  # CNamePool::Get
HANDLERS = 0x5A7D28A9  # CBaseFunction handlers table

KINDS = [
    "Name", "Fundamental", "Class", "Array", "Simple", "Enum", "StaticArray", "NativeArray",
    "Pointer", "Handle", "WeakHandle", "ResourceReference", "ResourceAsyncReference", "BitField",
    "LegacySingleChannelCurve", "ScriptReference", "FixedArray",
]  # fmt: skip


def singleton(exe: Exe, getter: int) -> int:
    """The address a `lea rax, [rip+x]; ...; ret` getter answers."""
    last = None
    for i in exe.disasm(getter, 0x80):
        if i.mnemonic == "lea" and i.op_str.startswith("rax, [rip"):
            last = i.address + i.size + i.operands[1].mem.disp
    assert last, hex(getter)
    return last


def main():
    exe = Exe()
    addr = addresses()
    m = Mem(cached=True)
    in_text = lambda a: exe.section(a) == ".text"

    rtti = singleton(exe, addr[RTTI_GET][0])
    # CNamePool::Get calls a lookup, which calls the pool's getter first.
    lookup = next(i for i in exe.disasm(addr[NAMEPOOL_GET][0]) if i.mnemonic == "call").operands[0].imm
    pool = singleton(exe, next(i for i in exe.disasm(lookup) if i.mnemonic == "call").operands[0].imm)
    handlers = addr[HANDLERS][0]
    print(f"rtti {rtti:#x}  name pool {pool:#x}  handlers {handlers:#x}", file=sys.stderr)

    # Name pool: entries[] at +0x08, count at +0x14; entry: hash, next, len|index<<8, text at +0x14.
    entries, count = m.u64(pool + 8), m.u32(pool + 0x14)
    names = {}
    for p in struct.unpack(f"<{count}Q", m.cread(entries, 8 * count)):
        head = m.cread(p, 0x14)
        names[struct.unpack_from("<Q", head)[0]] = m.cread(p + 0x14, head[0x10] + 1)[:-1].decode("latin-1")
    with open(CACHE / "names.txt", "w") as f:
        for h, s in names.items():
            f.write(f"{h:016x}\t{s.encode('unicode_escape').decode()}\n")
    print(f"{len(names)} names", file=sys.stderr)
    name = lambda h: names.get(h, f"#{h:016x}") if h else ""

    def hashmap(a):
        idx, size, cap, nodes = m.unpack(a, "<QIIQ")
        raw = m.cread(nodes, 0x18 * m.u32(a + 0x18))
        out = {}
        for head in struct.unpack(f"<{cap}I", m.cread(idx, 4 * cap)):
            while head != 0xFFFFFFFF:
                head, _, k, v = struct.unpack_from("<IIQQ", raw, 0x18 * head)
                out[k] = v
        assert len(out) == size
        return out

    def dynarray(a, fmt="Q"):
        p, _, n = m.unpack(a, "<QII")
        return struct.unpack(f"<{n}{fmt}", m.cread(p, n * struct.calcsize(fmt))) if n else ()

    types = hashmap(rtti + 0x10)
    type_name = {v: name(k) for k, v in types.items()}

    def kind(vtable):
        # IType::GetType (slot 0x20) is `mov al, N; ret`.
        code = exe.read(exe.u64(vtable + 0x20) or 0, 8) or b""
        if code[:1] == b"\xb0" and code[2:3] == b"\xc3" and code[1] < len(KINDS):
            return KINDS[code[1]]
        if code[:3] in (b"\x32\xc0\xc3", b"\x30\xc0\xc3"):
            return KINDS[0]
        return "?"

    def prop(p):
        t, n, group, _, off, flags = m.unpack(p, "<QQQQIxxxxQ")
        return {"name": name(n), "type": type_name.get(t, f"@{t:#x}"), "offset": off, "flags": flags}

    def func(f, is_global=False):
        full, short, ret = m.unpack(f + 8, "<QQQ")
        flags = m.u32(f + 0xA8)
        out = {
            "name": name(short),
            "full": name(full),
            "flags": flags,
            "params": [prop(p) for p in dynarray(f + 0x28)],
        }
        if ret:
            out["returns"] = type_name.get(m.u64(ret), "?")
        if flags & 1:  # native: handlers[regIndex]
            reg = m.u32(f + (0xF0 if is_global else 0xF8))
            h = m.u64(handlers + 8 * reg) or 0
            out["handler"] = h if in_text(h) else 0
            out["reg"] = reg
        return out

    out = {"rtti": rtti, "namePool": pool, "handlers": handlers, "classes": {}, "enums": {}, "others": {}}
    class_at = {}
    for k, t in types.items():
        vt = m.u64(t)
        kd = kind(vt)
        n = name(k)
        if kd == "Class":
            parent, _, _ = m.unpack(t + 0x10, "<QQQ")
            size, _, flags, align = m.unpack(t + 0x68, "<IIII")
            out["classes"][n] = {
                "type": t,
                "typeVtable": vt,
                "parent": type_name.get(parent, "") if parent else "",
                "size": size,
                "align": align,
                "flags": flags,
                "props": [prop(p) for p in dynarray(t + 0x28)],
                "funcs": [func(f) for f in dynarray(t + 0x48)],
                "staticFuncs": [func(f) for f in dynarray(t + 0x58)],
            }
            class_at[t] = n
        elif kd in ("Enum", "BitField"):
            if kd == "Enum":  # CEnum: hashList +0x28, valueList +0x38
                out["enums"][n] = dict(zip(map(name, dynarray(t + 0x28)), dynarray(t + 0x38, "q")))
            else:  # CBitfield: validBits +0x28, bitNames[64] +0x30
                bits = struct.unpack("<64Q", m.cread(t + 0x30, 0x200))
                out["enums"][n] = {name(b): 1 << i for i, b in enumerate(bits) if b}
        else:
            out["others"][n] = kd
    out["globals"] = [func(f, True) for f in hashmap(rtti + 0xA0).values()]

    # Instance vtables: ISerializable::GetNativeType (slot 0; GetType in slot 1
    # is the same function unless the class can be scripted) is
    # `mov rax, [rip+g]; ret`, g a global holding the class. Every .rdata slot
    # pointing at such a getter starts a vtable of that class (several for
    # templates or classes sharing a getter; the first is usually the one).
    base, vsize, raw, rawsize, _ = next(s for s in exe.sections if s[4] == ".rdata")
    getter_class = {}
    prev = 0
    for i in range(0, min(vsize, rawsize) - 7, 8):
        fn = struct.unpack_from("<Q", exe.data, raw + i)[0]
        if fn not in getter_class:
            code = exe.read(fn, 8) if in_text(fn) else None
            cls = None
            if code and code[:3] == b"\x48\x8b\x05" and code[7] == 0xC3:
                cls = class_at.get(m.u64(fn + 7 + struct.unpack_from("<i", code, 3)[0]))
            getter_class[fn] = cls
        if getter_class[fn] and prev != fn:
            out["classes"][getter_class[fn]].setdefault("vtables", []).append(base + i)
        prev = fn

    (CACHE / "rtti.json").write_text(json.dumps(out))
    c = out["classes"]
    natives = sum(1 for x in c.values() for f in x["funcs"] + x["staticFuncs"] if f.get("handler"))
    print(
        f"{len(c)} classes ({sum(1 for x in c.values() if 'vtables' in x)} with an instance vtable), "
        f"{len(out['enums'])} enums, {len(out['globals'])} global functions, {natives} native class functions",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
