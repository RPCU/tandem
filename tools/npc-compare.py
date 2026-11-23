#!/usr/bin/env python3
"""Compare two games' `npc list` blocks (docs/architecture/npcs.md, "The
same people on both screens"; the vehicles' twin is tools/street-compare.py).

    tools/npc-compare.py HOST.log GUEST.log [--near 100] [--close 30]

`tools/instance.sh cmd NAME npc list` on both games at about the same
moment; their logs here. Blocks are paired in order.

For each pair: every living NPC within --near m of either V that a game
shows (one it runs and shares, a proxy of another's with a body, or one of
its own it shares with nobody and has not hidden) and how many of them
both games show; each one shown on one game only, with why the other does
not show it (not shared to it, its proxy has no body yet or lost it, held
out of sight, the owner's own unshared one and why); how far apart the
two bodies of each NPC on both are (median, largest), and for those within
--close m of a V how many are more than 0.5 m apart (each listed). A
seated body's place reads as nonsense (§13.154's mounts), so the NPCs more
than 1 km apart are counted apart. Corpses are counted apart.
"""

import argparse
import datetime
import math
import re
import statistics

STAMP = r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]"
VAT = re.compile(STAMP + r".*npc list: our V at (\S+) (\S+) (\S+)")
ROW = re.compile(STAMP + r".*npc list: (\d+) (ours|proxy) (\S+) (?:at (\S+) (\S+) (\S+) (\w+)( native)?|no body(?: \((\w+)\))?)")
SHARED = re.compile(STAMP + r".*npc list: (\d+) shared (\S+) at (\S+) (\S+) (\S+) no proxy \((\S+) s\)")
OWN = re.compile(STAMP + r".*npc list: own (\S+) at (\S+) (\S+) (\S+) unshared \(([^)]*)\) (shown|hidden)")
END = re.compile(STAMP + r".*npc list: (\d+) ours, (\d+) proxies")


def when(stamp):
    return datetime.datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def fresh():
    return {"t": None, "v": None, "rows": {}, "shared": {}, "own": []}


def blocks(path):
    out, cur = [], fresh()
    for line in open(path, errors="replace"):
        if m := VAT.search(line):
            cur = fresh()
            cur["t"] = when(m.group(1))
            cur["v"] = tuple(float(x) for x in m.group(2, 3, 4))
        elif m := ROW.search(line):
            pos = tuple(float(x) for x in m.group(5, 6, 7)) if m.group(5) else None
            state = m.group(8) if pos else "no body" + (f" ({m.group(10)})" if m.group(10) else "")
            cur["rows"][int(m.group(2))] = {"kind": m.group(3), "rec": m.group(4), "pos": pos, "state": state}
        elif m := SHARED.search(line):
            pos = tuple(float(x) for x in m.group(4, 5, 6))
            cur["shared"][int(m.group(2))] = {"rec": m.group(3), "pos": pos, "for": float(m.group(7))}
        elif m := OWN.search(line):
            pos = tuple(float(x) for x in m.group(3, 4, 5))
            cur["own"].append({"rec": m.group(2), "pos": pos, "why": m.group(6), "shown": m.group(7) == "shown"})
        elif END.search(line) and cur["t"] is not None:
            out.append(cur)
            cur = fresh()
    return out


def flat(a, b):
    return math.dist(a[:2], b[:2])


def shows(row):
    """Shown on this game: a body that is alive (or knocked down), not held
    out of sight."""
    return row is not None and row["pos"] is not None and row["state"] in ("alive", "down", "loading")


def why_not(row, shared, owner_side):
    if row is None:
        if shared is not None:
            return f"shared, no proxy for {shared['for']:.1f} s"
        return "not shared to it" if owner_side else "not in its session record"
    if row["pos"] is None:
        return f"its {row['kind']} has {row['state']}"
    return f"its {row['kind']} is {row['state']}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--near", type=float, default=100.0)
    ap.add_argument("--close", type=float, default=30.0)
    ap.add_argument("--names", default="host,guest", help="the two games' names in the output")
    args = ap.parse_args()
    names = args.names.split(",")
    totals = []
    for a, b in zip(blocks(args.a), blocks(args.b)):
        vs = [x["v"] for x in (a, b) if x["v"]]
        print(f"== {a['t']:.3f} / {b['t']:.3f} ({abs(a['t'] - b['t']):.2f} s apart), Vs {flat(a['v'], b['v']):.0f} m apart: "
              f"{names[0]} {sum(r['kind'] == 'ours' for r in a['rows'].values())} ours {sum(r['kind'] == 'proxy' for r in a['rows'].values())} proxies, "
              f"{names[1]} {sum(r['kind'] == 'ours' for r in b['rows'].values())} ours {sum(r['kind'] == 'proxy' for r in b['rows'].values())} proxies")

        def near(p):
            return min(flat(p, v) for v in vs)

        apart, close, off, seated, missing, total, both, dead_one_sided = [], [], [], 0, [], 0, 0, 0
        for i in sorted(set(a["rows"]) | set(b["rows"]) | set(a["shared"]) | set(b["shared"])):
            x, y = a["rows"].get(i), b["rows"].get(i)
            sx, sy = shows(x), shows(y)
            ref = next((r["pos"] for r in (x, y) if r and r["pos"] and r["kind"] == "ours"), None)
            ref = ref or next((r["pos"] for r in (x, y) if r and r["pos"]), None)
            ref = ref or next((s["pos"] for s in (a["shared"].get(i), b["shared"].get(i)) if s), None)
            if ref is None or near(ref) > args.near:
                continue
            if any(r and r["state"] == "dead" for r in (x, y)):
                if (x is None or x["pos"] is None) != (y is None or y["pos"] is None):
                    dead_one_sided += 1
                continue
            if not (sx or sy):
                continue
            total += 1
            if sx and sy:
                both += 1
                d = flat(x["pos"], y["pos"])
                if d > 1000.0:
                    seated += 1
                    continue
                apart.append(d)
                if near(ref) <= args.close:
                    close.append(d)
                    if d > 0.5:
                        off.append(f"{i} ({x['rec']}): {d:.2f} m apart, {near(ref):.0f} m from a V")
                continue
            shown, other, row, blk = (0, 1, y, b) if sx else (1, 0, x, a)
            src = x if sx else y
            missing.append(f"{i} ({src['rec']}, {src['kind']} on the {names[shown]}): not on the {names[other]}, "
                           f"{why_not(row, blk['shared'].get(i), src['kind'] == 'ours')}, {near(ref):.0f} m from a V")
        for side, other, blk in ((0, 1, a), (1, 0, b)):
            for o in blk["own"]:
                if o["shown"] and o["why"] != "dead" and near(o["pos"]) <= args.near:
                    total += 1
                    missing.append(f"{names[side]}'s own {o['rec']} ({o['why']}): not on the {names[other]}, {near(o['pos']):.0f} m from a V")
        if apart:
            print(f"  on both: {len(apart)}, median {statistics.median(apart):.2f} m apart, largest {max(apart):.2f} m"
                  + (f" ({seated} seated, their places unread)" if seated else ""))
        if close:
            print(f"  within {args.close:.0f} m of a V: {len(close)}, median {statistics.median(close):.2f} m apart, "
                  f"{len(off)} more than 0.5 m apart")
        print(f"  within {args.near:.0f} m of either V: {both} of {total} on both games ({total - both} one-sided)"
              + (f"; {dead_one_sided} corpse(s) on one game only" if dead_one_sided else ""))
        for line in off:
            print("  apart: " + line)
        for line in missing:
            print("  one-sided: " + line)
        totals.append((both, total))
    if len(totals) > 1:
        print(f"== all: {sum(t[0] for t in totals)} of {sum(t[1] for t in totals)} on both games over {len(totals)} lists")


if __name__ == "__main__":
    main()
