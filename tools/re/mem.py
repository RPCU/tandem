"""Read-only access to the running game's memory, and to the exe on disk.

    from mem import Mem, Exe
    m = Mem()                      # ssh to GAME_HOST, starts memsrv on the game's pid
    m.u64(0x140000000)

Nothing here writes to the game or stops it (see memsrv/main.go). Run with
tools/re/run (Python with capstone and pefile).
"""

import json
import os
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / ".cache" / "re"
HOST = os.environ.get("GAME_HOST", "anya")
IMAGE = 0x140000000


def fnv1a64(s: str | bytes) -> int:
    """CName hash."""
    if isinstance(s, str):
        s = s.encode()
    h = 0xCBF29CE484222325
    for c in s:
        h = ((h ^ c) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Mem:
    def __init__(self, host: str = HOST, cached: bool = False):
        self.cached = cached  # unpack/u64/u32/cstr go through cread()
        srv = CACHE / "memsrv"
        src = Path(__file__).parent / "memsrv" / "main.go"
        if not srv.exists() or srv.stat().st_mtime < src.stat().st_mtime:
            CACHE.mkdir(parents=True, exist_ok=True)
            mod = CACHE / "memsrv-src"
            mod.mkdir(exist_ok=True)
            (mod / "main.go").write_bytes(src.read_bytes())
            (mod / "go.mod").write_text("module memsrv\n\ngo 1.22\n")
            subprocess.run(
                ["go", "build", "-o", str(srv), "."],
                cwd=mod,
                check=True,
                env={**os.environ, "CGO_ENABLED": "0"},
            )
        subprocess.run(["ssh", host, "mkdir -p /tmp/tandem-re"], check=True)
        subprocess.run(["rsync", "-a", str(srv), f"{host}:/tmp/tandem-re/memsrv"], check=True)
        self.p = subprocess.Popen(
            ["ssh", host, 'exec /tmp/tandem-re/memsrv "$(pgrep -f "[C]yberpunk2077.exe" | head -1)"'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )

    def _recv(self, n: int) -> bytes:
        out = b""
        while len(out) < n:
            chunk = self.p.stdout.read(n - len(out))
            if not chunk:
                raise EOFError("memsrv closed (is the game running?)")
            out += chunk
        return out

    def read(self, addr: int, n: int) -> bytes | None:
        """n bytes at addr, or None when unreadable."""
        if addr < 0x10000 or addr >= 1 << 47:
            return None
        self.p.stdin.write(b"R" + struct.pack("<QI", addr, n))
        self.p.stdin.flush()
        return self._recv(n) if self._recv(1) == b"\x01" else None

    def stable(self, addr: int, n: int) -> bytes | None:
        """n bytes that two reads in a row agreed on (memsrv retries): for buffers
        the game rewrites every frame. None when unreadable or never stable."""
        self.p.stdin.write(b"C" + struct.pack("<QI", addr, n))
        self.p.stdin.flush()
        return self._recv(n) if self._recv(1) == b"\x01" else None

    def maps(self) -> str:
        self.p.stdin.write(b"M")
        self.p.stdin.flush()
        return self._recv(struct.unpack("<I", self._recv(4))[0]).decode()

    def scan(self, pattern: bytes, lo=0x10000, hi=1 << 47, align=1, limit=1000, writable=False):
        self.p.stdin.write(
            b"S" + struct.pack("<QQIIBH", lo, hi, align, limit, int(writable), len(pattern)) + pattern
        )
        self.p.stdin.flush()
        n = struct.unpack("<I", self._recv(4))[0]
        return list(struct.unpack(f"<{n}Q", self._recv(8 * n)))

    def refs(self, vlo: int, vhi: int | None = None, lo=0x10000, hi=1 << 47, limit=1000, writable=True):
        """(address, value) of aligned 8-byte values in [vlo, vhi): pointers to a range."""
        vhi = vlo + 1 if vhi is None else vhi
        self.p.stdin.write(b"X" + struct.pack("<QQQQIB", lo, hi, vlo, vhi, limit, int(writable)))
        self.p.stdin.flush()
        n = struct.unpack("<I", self._recv(4))[0]
        flat = struct.unpack(f"<{2 * n}Q", self._recv(16 * n))
        return list(zip(flat[::2], flat[1::2]))

    def cread(self, addr: int, n: int) -> bytes | None:
        """read() through a cache of 64 KiB blocks: for data that does not change (RTTI, names)."""
        if not hasattr(self, "_blocks"):
            self._blocks = {}
        out = b""
        while n > 0:
            base = addr & ~0xFFFF
            if base not in self._blocks:
                block = self.read(base, 0x10000)
                if block is None:  # a partly mapped block: page by page
                    block = b"".join(self.read(base + i, 0x1000) or b"\0" * 0x1000 for i in range(0, 0x10000, 0x1000))
                self._blocks[base] = block
            take = min(n, base + 0x10000 - addr)
            out += self._blocks[base][addr - base : addr - base + take]
            addr += take
            n -= take
        return out

    def unpack(self, addr: int, fmt: str):
        data = (self.cread if self.cached else self.read)(addr, struct.calcsize(fmt))
        return struct.unpack(fmt, data) if data else None

    def u64(self, addr):
        v = self.unpack(addr, "<Q")
        return v[0] if v else None

    def u32(self, addr):
        v = self.unpack(addr, "<I")
        return v[0] if v else None

    def cstr(self, addr, limit=256) -> str | None:
        read = self.cread if self.cached else self.read
        data = read(addr, limit) or read(addr, 32)
        if not data:
            return None
        return data.split(b"\0", 1)[0].decode("latin-1")


class Exe:
    """The exe on disk (copied to .cache/re), addressed by virtual address."""

    def __init__(self, path: Path = CACHE / "Cyberpunk2077.exe"):
        import pefile

        self.data = path.read_bytes()
        pe = pefile.PE(data=self.data, fast_load=True)
        self.sections = [
            (
                IMAGE + s.VirtualAddress,
                s.Misc_VirtualSize,
                s.PointerToRawData,
                s.SizeOfRawData,
                s.Name.rstrip(b"\0").decode(),
            )
            for s in pe.sections
        ]

    def off(self, va: int) -> int | None:
        for base, vsize, raw, rawsize, _ in self.sections:
            if base <= va < base + vsize and va - base < rawsize:
                return raw + va - base
        return None

    def read(self, va: int, n: int) -> bytes | None:
        o = self.off(va)
        return None if o is None else self.data[o : o + n]

    def u64(self, va):
        b = self.read(va, 8)
        return struct.unpack("<Q", b)[0] if b and len(b) == 8 else None

    def section(self, va: int) -> str | None:
        for base, vsize, _, _, name in self.sections:
            if base <= va < base + vsize:
                return name
        return None

    def disasm(self, va: int, n: int = 0x200, stop_at_ret=True):
        import capstone

        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = True
        out = []
        for i in md.disasm(self.read(va, n) or b"", va):
            out.append(i)
            if stop_at_ret and i.mnemonic in ("ret", "int3"):
                break
        return out

    def functions(self):
        """Sorted (begin, end) of every function, from the exception table (.pdata)."""
        if not hasattr(self, "_funcs"):
            base, vsize, raw, rawsize, _ = next(s for s in self.sections if s[4] == ".pdata")
            n = min(vsize, rawsize) // 12
            flat = struct.unpack_from(f"<{3 * n}I", self.data, raw)
            self._funcs = sorted({(IMAGE + flat[i], IMAGE + flat[i + 1]) for i in range(0, 3 * n, 3) if flat[i]})
            self._starts = [f[0] for f in self._funcs]
        return self._funcs

    def func_of(self, va: int):
        """(begin, end) of the function chunk containing va, or None."""
        import bisect

        funcs = self.functions()
        i = bisect.bisect_right(self._starts, va) - 1
        return funcs[i] if i >= 0 and funcs[i][0] <= va < funcs[i][1] else None

    def xrefs(self, target: int) -> list[int]:
        """Addresses in .text of 32-bit rip-relative displacements reaching target
        (calls, jumps, lea, mov): the address is the displacement's, so the
        instruction starts 1 to 4 bytes before. Byte scan: rare false hits."""
        import numpy as np

        base, vsize, raw, rawsize, _ = self.sections[0]
        size = min(vsize, rawsize)
        out = []
        for k in range(4):
            n = (size - k) // 4
            disp = np.frombuffer(self.data, dtype="<i4", count=n, offset=raw + k).astype(np.int64)
            pos = base + k + 4 * np.arange(n, dtype=np.int64)
            out += (pos[disp + pos + 4 == target]).tolist()
        return sorted(out)

    def callers(self, target: int):
        """Functions (begin) referencing target, with the referencing addresses."""
        out = {}
        for x in self.xrefs(target):
            f = self.func_of(x)
            out.setdefault(f[0] if f else 0, []).append(x)
        return out

    def show(self, va: int, n: int = 0x200, stop_at_ret=True):
        for i in self.disasm(va, n, stop_at_ret):
            print(f"{i.address:x}  {i.mnemonic} {i.op_str}")


_addresses = None


def addresses() -> dict:
    """hash -> (va, symbol or None), from RED4ext's address list."""
    global _addresses
    if _addresses is None:
        raw = json.loads((CACHE / "cyberpunk2077_addresses.json").read_text())
        exe = Exe()
        seg = {i + 1: s[0] for i, s in enumerate(exe.sections)}
        _addresses = {}
        for a in raw["Addresses"]:
            s, o = a["offset"].split(":")
            _addresses[int(a["hash"])] = (seg[int(s)] + int(o, 16), a.get("symbol"))
    return _addresses


def names() -> dict:
    """CName hash -> text, from the name pool dumped by rtti_dump.py."""
    out = {}
    with open(CACHE / "names.txt") as f:
        for line in f:
            if len(line) > 17 and line[16] == "\t":
                try:
                    out[int(line[:16], 16)] = line[17:-1]
                except ValueError:
                    pass
    return out
