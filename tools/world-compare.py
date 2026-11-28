#!/usr/bin/env python3
"""Compare what two games show of one world at one moment
(docs/architecture/parity.md, "The checker").

    tools/world-compare.py A.log B.log [--near 100] [--apart 1.5]
    tools/world-compare.py --instances a c [--near 100] [--shots NAME]
    ... [--only CATEGORY[,CATEGORY]] [--entity HEX]

Each game's `parity [M]` dump (scripts/tandem/Parity.reds: `tools/game.sh
cmd parity`, `tools/instance.sh cmd NAME parity`) is read from its log: the
last one of each, or, with --instances, a fresh one asked of both games at
once (and, with --shots, a screenshot of each at that moment, kept in the
instance's run/ folder as NAME-INSTANCE.png).

Reported per category, each line a difference a player could see:
  world     time of day, weather, rain, puddles, the city's lights
  heat      each V's wanted level as its own game has it
  placed    entities with a static id (the same in every game): on one side
            only, dead on one side, apart, another look or radio station
  spawned   entities with a dynamic id (crowds, traffic, proxies, bodies,
            item drops): paired by class and record, nearest first; those
            left with no twin
  devices   every device the session follows: on one side only, another
            status, state bits or value (door open, lift floor, station)
  glass     the session's record of what broke, as each game has it
then the vehicle and NPC comparisons of tools/street-compare.py and
tools/npc-compare.py on the same dump (each run when present).
Within --near m of either V (the V positions of both dumps). --only counts
(and exits 1 on) those categories alone, without the vehicle and NPC
comparisons (tools/scenario.sh's `compare`); --entity HEX keeps only that
device of the devices category.
"""

import argparse
import datetime
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
STAMP = r"\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d(?:\.\d+)?)\]"
BEGIN = re.compile(STAMP + r".*parity: begin within (\S+) m, player (\d+), our V at (\S+) (\S+) (\S+)")
PUPPET = re.compile(STAMP + r".*parity: puppet (\d+) at (\S+) (\S+) (\S+)")
WORLD = re.compile(STAMP + r".*parity world: time (-?\d+) weather (\S*) rain (\S+) type (-?\d+) puddles (\S+) moisture (\S+) lights (-?\d+) paused (\S+)")
HEAT = re.compile(STAMP + r".*parity heat: ours (-?\d+)")
ENTITY = re.compile(STAMP + r".*parity entity: (\S+) (static|dynamic) (\S+) (\S+) (\S+) at (\S+) (\S+) (\S+) (\S+) radio (\S+) (\S+)")
DEVICE = re.compile(STAMP + r".*parity device: (\S+) (\S+) at (\S+) (\S+) (\S+) status (-?\d+) flags (-?\d+) value (-?\d+)")
GLASS = re.compile(STAMP + r".*parity glass: (.*)")
END = re.compile(STAMP + r".*parity: end")
BITS = {1: "open", 2: "locked", 4: "sealed", 8: "broken", 16: "exploded", 32: "hacked", 64: "friendly",
        128: "exposed", 256: "breached", 1024: "glitching"}
STATUS = {-2: "disabled", -1: "unpowered", 0: "off", 1: "on"}
LIGHTS = {0: "timetable", 1: "forced on", 2: "forced off"}


def when(stamp):
    day, _, frac = stamp.partition(".")
    t = datetime.datetime.strptime(day, "%Y-%m-%d %H:%M:%S").timestamp()
    return t + (float("0." + frac) if frac else 0.0)


def dumps(lines):
    """Every complete parity dump in `lines`, oldest first: its lines kept
    (the street and npc lists inside it are read by their own tools)."""
    out, cur = [], None
    for line in lines:
        if m := BEGIN.search(line):
            cur = {"t": when(m.group(1)), "radius": float(m.group(2)), "you": int(m.group(3)),
                   "v": tuple(float(x) for x in m.group(4, 5, 6)), "puppets": {}, "world": None,
                   "heat": None, "placed": {}, "spawned": [], "devices": {}, "glass": "", "lines": []}
        if cur is None:
            continue
        cur["lines"].append(line)
        if m := PUPPET.search(line):
            cur["puppets"][int(m.group(2))] = tuple(float(x) for x in m.group(3, 4, 5))
        elif m := WORLD.search(line):
            cur["world"] = {"time": int(m.group(2)), "weather": m.group(3), "rain": float(m.group(4)),
                            "type": int(m.group(5)), "puddles": float(m.group(6)), "moisture": float(m.group(7)),
                            "lights": int(m.group(8)), "paused": m.group(9)}
        elif m := HEAT.search(line):
            cur["heat"] = int(m.group(2))
        elif m := ENTITY.search(line):
            e = {"hex": m.group(2), "cls": m.group(4), "rec": m.group(5), "look": m.group(6),
                 "pos": tuple(float(x) for x in m.group(7, 8, 9)), "state": m.group(10), "radio": m.group(11),
                 "body": m.group(12) == "body"}
            if m.group(3) == "static":
                cur["placed"][e["hex"]] = e
            else:
                cur["spawned"].append(e)
        elif m := DEVICE.search(line):
            cur["devices"][(m.group(2), m.group(3))] = {
                "pos": tuple(float(x) for x in m.group(4, 5, 6)), "status": int(m.group(7)),
                "flags": int(m.group(8)), "value": int(m.group(9))}
        elif m := GLASS.search(line):
            cur["glass"] = m.group(2)
        elif END.search(line):
            out.append(cur)
            cur = None
    return out


def flags(f):
    return ",".join(n for b, n in BITS.items() if f & b) or "none"


def clock(s):
    s %= 86400
    return f"{s // 3600:02d}:{s // 60 % 60:02d}:{s % 60:02d}"


class Report:
    def __init__(self):
        self.counts = {}

    def diff(self, category, text):
        self.counts[category] = self.counts.get(category, 0) + 1
        print(f"  {category}: {text}")


def compare(a, b, near, apart, entity=None):
    """Print each difference between two dumps; the counts per category."""
    r = Report()
    vs = [a["v"], b["v"]] + list(a["puppets"].values()) + list(b["puppets"].values())

    def dist(p):
        return min(math.dist(p[:2], v[:2]) for v in vs)

    print(f"== player {a['you']} at {a['t']:.2f}, player {b['you']} at {b['t']:.2f} "
          f"({abs(a['t'] - b['t']):.2f} s apart); Vs {math.dist(a['v'][:2], b['v'][:2]):.1f} m apart")
    # world
    wa, wb = a["world"], b["world"]
    if wa and wb:
        dt = abs(wa["time"] - wb["time"])
        dt = min(dt, 86400 - dt)
        # Both clocks run ~8 game seconds a second: the dumps' own gap is no difference.
        if dt > 60 + 8 * abs(a["t"] - b["t"]):
            r.diff("world", f"time {clock(wa['time'])} / {clock(wb['time'])} ({dt} game s apart)")
        if wa["weather"] != wb["weather"]:
            r.diff("world", f"weather {wa['weather']} / {wb['weather']}")
        if abs(wa["rain"] - wb["rain"]) > 0.1 or wa["type"] != wb["type"]:
            r.diff("world", f"rain {wa['rain']:.2f} (type {wa['type']}) / {wb['rain']:.2f} (type {wb['type']})")
        if abs(wa["puddles"] - wb["puddles"]) > 0.1 or abs(wa["moisture"] - wb["moisture"]) > 0.1:
            r.diff("world", f"wet ground: puddles {wa['puddles']:.2f} / {wb['puddles']:.2f}, "
                            f"moisture {wa['moisture']:.2f} / {wb['moisture']:.2f}")
        if wa["lights"] != wb["lights"]:
            r.diff("world", f"city lights {LIGHTS.get(wa['lights'], wa['lights'])} / {LIGHTS.get(wb['lights'], wb['lights'])}")
        print(f"  world: {clock(wa['time'])} {wa['weather']} rain {wa['rain']:.2f} / "
              f"{clock(wb['time'])} {wb['weather']} rain {wb['rain']:.2f}")
    else:
        r.diff("world", "no world line on " + ("both" if not wa and not wb else ("the first" if not wa else "the second")))
    print(f"  heat: player {a['you']} {a['heat']}, player {b['you']} {b['heat']} (each V's own)")
    # placed entities
    gaps, same = [], 0
    for key in sorted(set(a["placed"]) | set(b["placed"])):
        x, y = a["placed"].get(key), b["placed"].get(key)
        ref = (x or y)["pos"]
        if dist(ref) > near:
            continue
        name = f"{(x or y)['cls']} {(x or y)['rec']} {key}"
        if not x or not y:
            side = "second" if x else "first"
            r.diff("placed", f"{name} at {ref[0]:.1f} {ref[1]:.1f}: missing on the {side} ({dist(ref):.0f} m from a V)")
            continue
        same += 1
        d = math.dist(x["pos"], y["pos"])
        gaps.append(d)
        if x["state"] != y["state"]:
            r.diff("placed", f"{name}: {x['state']} / {y['state']}")
        if d > apart:
            r.diff("placed", f"{name}: {d:.2f} m apart")
        if x["look"] != y["look"]:
            r.diff("placed", f"{name}: look {x['look']} / {y['look']}")
        if x["radio"] != y["radio"]:
            r.diff("placed", f"{name}: radio {x['radio']} / {y['radio']}")
    if gaps:
        print(f"  placed: {same} on both, median {statistics.median(gaps):.2f} m apart, largest {max(gaps):.2f} m")
    # spawned entities: by class and record, nearest first
    groups = {}
    for side, d in ((0, a), (1, b)):
        for e in d["spawned"]:
            if e["cls"] == "PlayerPuppet" or dist(e["pos"]) > near:
                continue
            # A V's own body on one game is its puppet on the other: not compared here.
            if any(math.dist(e["pos"], v) < 0.5 for v in vs):
                continue
            groups.setdefault((e["cls"], e["rec"]), ([], []))[side].append(e)
    paired, lone = 0, []
    for (cls, rec), (xs, ys) in sorted(groups.items()):
        pairs = sorted(((math.dist(x["pos"], y["pos"]), i, j) for i, x in enumerate(xs) for j, y in enumerate(ys)))
        used_x, used_y = set(), set()
        for d, i, j in pairs:
            if d > 5.0 or i in used_x or j in used_y:
                continue
            used_x.add(i)
            used_y.add(j)
            paired += 1
            if xs[i]["state"] != ys[j]["state"]:
                r.diff("spawned", f"{cls} {rec} at {xs[i]['pos'][0]:.1f} {xs[i]['pos'][1]:.1f}: {xs[i]['state']} / {ys[j]['state']}")
            if xs[i]["radio"] != ys[j]["radio"]:
                r.diff("spawned", f"{cls} {rec}: radio {xs[i]['radio']} / {ys[j]['radio']}")
        for i, x in enumerate(xs):
            if i not in used_x:
                lone.append((cls, rec, x, "first"))
        for j, y in enumerate(ys):
            if j not in used_y:
                lone.append((cls, rec, y, "second"))
    for cls, rec, e, side in lone:
        what = "our body " if e["body"] else ""
        r.diff("spawned", f"{what}{cls} {rec} {e['state']} at {e['pos'][0]:.1f} {e['pos'][1]:.1f}: "
                          f"only on the {side} ({dist(e['pos']):.0f} m from a V)")
    print(f"  spawned: {paired} paired within 5 m, {len(lone)} with no twin")
    # devices
    both = 0
    for key in sorted(set(a["devices"]) | set(b["devices"])):
        if entity and key[0] != entity:
            continue
        x, y = a["devices"].get(key), b["devices"].get(key)
        ref = (x or y)["pos"]
        if dist(ref) > near:
            continue
        name = f"{key[1]} {key[0]}"
        if not x or not y:
            side = "second" if x else "first"
            r.diff("devices", f"{name}: not followed on the {side} ({dist(ref):.0f} m from a V)")
            continue
        both += 1
        if x["status"] != y["status"]:
            r.diff("devices", f"{name}: {STATUS.get(x['status'], x['status'])} / {STATUS.get(y['status'], y['status'])}")
        # Exposed (the scanner's quickhack view) is each V's own.
        fx, fy = x["flags"] & ~128, y["flags"] & ~128
        if fx != fy:
            r.diff("devices", f"{name}: {flags(fx)} / {flags(fy)}")
        if x["value"] != y["value"]:
            r.diff("devices", f"{name}: value {x['value']} / {y['value']}")
    print(f"  devices: {both} on both")
    if a["glass"] != b["glass"]:
        r.diff("glass", f"{a['glass']} / {b['glass']}")
    return r.counts


def others(a, b, near):
    """street-compare and npc-compare on the same two dumps."""
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for i, d in enumerate((a, b)):
            path = os.path.join(tmp, f"{i}.log")
            with open(path, "w") as f:
                f.writelines(d["lines"])
            files.append(path)
        for tool in ("street-compare.py", "npc-compare.py"):
            path = os.path.join(HERE, tool)
            if not os.path.exists(path):
                print(f"-- {tool}: not in this tree")
                continue
            print(f"-- {tool}")
            out = subprocess.run([sys.executable, path, *files, "--near", str(near)], capture_output=True, text=True)
            sys.stdout.write(out.stdout or out.stderr)


def ask(names, radius, shots):
    """A fresh dump from each instance, asked of both at once; their logs."""
    tool = os.path.join(HERE, "instance.sh")
    procs = [subprocess.Popen([tool, "cmd", n, f"parity {radius:g}"]) for n in names]
    if shots:
        procs += [subprocess.Popen([tool, "shot", n, f"{shots}-{n}.png"]) for n in names]
    for p in procs:
        p.wait()
    time.sleep(2.0)
    logs = []
    for n in names:
        out = subprocess.run([tool, "log", n, "20000"], capture_output=True, text=True, check=True)
        logs.append(out.stdout.splitlines(keepends=True))
    return logs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="*")
    ap.add_argument("--instances", nargs=2, metavar="NAME")
    ap.add_argument("--near", type=float, default=100.0)
    ap.add_argument("--apart", type=float, default=1.5, help="placed entities farther apart than this differ (m)")
    ap.add_argument("--shots", metavar="NAME", help="with --instances: a screenshot of each, NAME-INSTANCE.png")
    ap.add_argument("--only", metavar="CATEGORIES", help="count only these categories (comma-separated)")
    ap.add_argument("--entity", metavar="HEX", help="devices: only this entity's")
    args = ap.parse_args()
    if args.instances:
        logs = ask(args.instances, args.near, args.shots)
    elif len(args.logs) == 2:
        logs = [open(p, errors="replace").readlines() for p in args.logs]
    else:
        ap.error("two logs, or --instances A B")
    found = [dumps(lines) for lines in logs]
    if not found[0] or not found[1]:
        sys.exit("world-compare: no complete parity dump in " + ("either log" if not any(found) else
                 ("the first log" if not found[0] else "the second log")))
    a, b = found[0][-1], found[1][-1]
    counts = compare(a, b, args.near, args.apart, args.entity.lower() if args.entity else None)
    if args.only:
        only = set(args.only.split(","))
        counts = {k: v for k, v in counts.items() if k in only}
    else:
        others(a, b, args.near)
    total = sum(counts.values())
    print("== differences: " + (", ".join(f"{k} {v}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])) or "none"))
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
