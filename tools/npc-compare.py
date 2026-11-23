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
# What an NPC does (NpcAnims.reds ActivityOf, ProxyActivity): its owner's
# workspot (or "none", "unread"), a proxy's played one and what it was told.
SPOT = re.compile(r" spot (\S+)(?: said (\S+))?")


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
            s = SPOT.search(line, m.end())
            cur["rows"][int(m.group(2))] = {"kind": m.group(3), "rec": m.group(4), "pos": pos, "state": state,
                                            "spot": s.group(1) if s else None, "said": s.group(2) if s else None}
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
    # Per NPC over the lists: whether its proxy played its owner's workspot
    # (True), another or none (False), while the owner was in one.
    history = {}
    for a, b in zip(blocks(args.a), blocks(args.b)):
        vs = [x["v"] for x in (a, b) if x["v"]]
        print(f"== {a['t']:.3f} / {b['t']:.3f} ({abs(a['t'] - b['t']):.2f} s apart), Vs {flat(a['v'], b['v']):.0f} m apart: "
              f"{names[0]} {sum(r['kind'] == 'ours' for r in a['rows'].values())} ours {sum(r['kind'] == 'proxy' for r in a['rows'].values())} proxies, "
              f"{names[1]} {sum(r['kind'] == 'ours' for r in b['rows'].values())} ours {sum(r['kind'] == 'proxy' for r in b['rows'].values())} proxies")

        def near(p):
            return min(flat(p, v) for v in vs)

        apart, close, off, seated, missing, total, both, dead_one_sided = [], [], [], 0, [], 0, 0, 0
        # Activity: an NPC on both games, its owner in a workspot or its
        # proxy in one, and whether the proxy plays the owner's.
        acts, act_off = 0, []
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
                own, prx = (x, y) if x["kind"] == "ours" else (y, x)
                if own["kind"] == "ours" and prx["kind"] == "proxy" and own.get("spot") and prx.get("spot"):
                    if own["spot"] != "unread":
                        # (seated: a driver's seat workspot, the seat's own on the proxy;
                        # every list, the owner out of one too, so its own getting up
                        # and sitting again is not the copy's flicker)
                        same = prx["spot"] in (own["spot"], "seated") or (prx["spot"] == "entering" and own["spot"] != "none")
                        history.setdefault(i, []).append((own["spot"], same))
                    if own["spot"] != "none" or prx["spot"] not in ("none", "seated"):
                        acts += 1
                        if own["spot"] != prx["spot"] and prx["spot"] != "seated" and not (prx["spot"] == "entering" and prx["said"] == own["spot"]):
                            act_off.append(f"{i} ({own['rec']}): owner {own['spot']}, proxy {prx['spot']} (told {prx['said']}), "
                                           f"{near(ref):.0f} m from a V")
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
        if acts:
            print(f"  activity: {acts} in a workspot on either game, {len(act_off)} not the same on both")
        for line in act_off:
            print("  activity: " + line)
        for line in off:
            print("  apart: " + line)
        for line in missing:
            print("  one-sided: " + line)
        totals.append((both, total))
    if len(totals) > 1:
        print(f"== all: {sum(t[0] for t in totals)} of {sum(t[1] for t in totals)} on both games over {len(totals)} lists")
    # A proxy that went out of its owner's workspot and back in between
    # lists while the owner stayed: the seated NPC that stood up and sat again.
    if any(len(h) > 1 and any(o != "none" for o, _ in h) for h in history.values()):
        # (a change while the owner changed workspots too is the copy
        # following it, not a flicker)
        # (the lists of the two games are taken a moment apart: a list
        # beside the owner's change, before or after, is not counted)
        def flips(h):
            return sum(1 for k in range(1, len(h)) if h[k][1] != h[k - 1][1] and h[k][0] == h[k - 1][0]
                       and (k + 1 >= len(h) or h[k + 1][0] == h[k][0]) and k >= 2 and h[k - 2][0] == h[k][0])
        sitting = {i: h for i, h in history.items() if len(h) > 1 and any(o != "none" for o, _ in h)}
        flicker = {i: flips(h) for i, h in sitting.items()}
        steady = sum(1 for h in sitting.values() if all(m for o, m in h if o != "none"))
        print(f"== activity over the lists: {len(flicker)} NPCs in a workspot, {steady} shown in it every time, "
              f"{sum(1 for n in flicker.values() if n)} flickered ({sum(flicker.values())} changes)")
        for i, n in sorted(flicker.items()):
            if n:
                print(f"  flicker: {i}: {n} changes in {len(history[i])} lists")


if __name__ == "__main__":
    main()
