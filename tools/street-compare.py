#!/usr/bin/env python3
"""Compare two games' `street list` blocks (docs/architecture/vehicles.md,
"How a copy is moved": the table's rows on vehicles).

    tools/street-compare.py HOST.log GUEST.log [--near 100]

`tools/game.sh cmd street list` (or `tools/instance.sh cmd NAME street
list`) on both games at about the same moment; their logs here. Blocks are
paired in order. Both games on one machine (two instances) share its clock:
a vehicle listed a moment earlier on one is carried on by its own velocity
to the other's moment before the two are compared.

For each pair: the shared vehicles with a body on both games and how far
apart the two bodies are (median, largest); every vehicle within --near m of
either V (shared, or a game's own one it shares with nobody) and how many of
them both games show; and each one missing on a side, with why.
"""

import argparse
import datetime
import math
import re
import statistics

STAMP = r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]"
ROW = re.compile(STAMP + r".*street list: (\d+) (ours|proxy) (\S+) (?:(\S+) at (\S+) (\S+) (\S+) yaw (\S+) flags (\d+) off (\S+)(?: v (\S+) (\S+))?|no body)")
OWN = re.compile(STAMP + r".*street list: own (\S+) at (\S+) (\S+) (\S+) unshared \(([^)]*)\)(?: v (\S+) (\S+))?")
VAT = re.compile(STAMP + r".*street list: our V at (\S+) (\S+) (\S+)")
END = re.compile(STAMP + r".*street list: (\d+) vehicle\(s\), (\d+) with a body")


def when(stamp):
    return datetime.datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def blocks(path):
    out = []
    cur = {"t": None, "v": None, "rows": {}, "own": []}
    for line in open(path, errors="replace"):
        if m := VAT.search(line):
            cur["v"] = tuple(float(x) for x in m.group(2, 3, 4))
            cur["t"] = when(m.group(1))
        elif m := ROW.search(line):
            cur["t"] = cur["t"] or when(m.group(1))
            pos = tuple(float(x) for x in m.group(6, 7, 8)) if m.group(6) else None
            vel = (float(m.group(12)), float(m.group(13))) if m.group(12) else (0.0, 0.0)
            cur["rows"][int(m.group(2))] = {"kind": m.group(3), "rec": m.group(4), "pos": pos, "vel": vel}
        elif m := OWN.search(line):
            vel = (float(m.group(7)), float(m.group(8))) if m.group(7) else (0.0, 0.0)
            pos = tuple(float(x) for x in m.group(3, 4, 5))
            cur["own"].append({"rec": m.group(2), "pos": pos, "why": m.group(6), "vel": vel})
        elif m := END.search(line):
            cur["t"] = cur["t"] or when(m.group(1))
            out.append(cur)
            cur = {"t": None, "v": None, "rows": {}, "own": []}
    return out


def carried(pos, vel, dt):
    return (pos[0] + vel[0] * dt, pos[1] + vel[1] * dt)


def flat(a, b):
    return math.dist(a[:2], b[:2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--near", type=float, default=100.0)
    args = ap.parse_args()
    for a, b in zip(blocks(args.a), blocks(args.b)):
        # Everything carried to the later list's moment.
        t = max(a["t"], b["t"])
        da, db = t - a["t"], t - b["t"]
        vs = [carried(x["v"], (0.0, 0.0), 0) for x in (a, b) if x["v"]]
        print(f"== {a['t']:.3f} / {b['t']:.3f} ({abs(a['t'] - b['t']):.2f} s apart): "
              f"{len(a['rows'])} and {len(b['rows'])} shared, {len(a['own'])} and {len(b['own'])} own unshared")

        def near(p):
            return min((flat(p, v) for v in vs), default=float("nan"))

        apart, missing, total, both = [], [], 0, 0
        for i in sorted(set(a["rows"]) | set(b["rows"])):
            x, y = a["rows"].get(i), b["rows"].get(i)
            px = x and x["pos"] and carried(x["pos"], x["vel"], da)
            py = y and y["pos"] and carried(y["pos"], y["vel"], db)
            ref = px or py
            d = near(ref) if ref else float("nan")
            inside = ref and d <= args.near
            total += 1 if inside else 0
            if px and py:
                apart.append(flat(px, py))
                both += 1 if inside else 0
            elif inside:
                missing.append(f"shared {i} ({(x or y)['rec']}): host {'body' if px else 'none'}, guest {'body' if py else 'none'}, {d:.0f} m from a V")
        for side, other, blk, dt in (("host", "guest", a, da), ("guest", "host", b, db)):
            for o in blk["own"]:
                p = carried(o["pos"], o["vel"], dt)
                d = near(p)
                if d <= args.near:
                    total += 1
                    missing.append(f"{side}'s own {o['rec']} ({o['why']}): not on the {other}, {d:.0f} m from a V")
        if apart:
            print(f"  shared with a body on both: {len(apart)}, median {statistics.median(apart):.3f} m apart, largest {max(apart):.3f} m")
        print(f"  within {args.near:.0f} m of either V: {both} of {total} on both games")
        for line in missing:
            print("  missing: " + line)


if __name__ == "__main__":
    main()
