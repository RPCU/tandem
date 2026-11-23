#!/usr/bin/env python3
"""What a play session left behind (the observer, docs/architecture/observer.md).

    tools/observer-report.py [PATH] [--session STAMP] [--trail] [--list]

PATH: a folder of observer files (default .cache/observer, which
`tools/game.sh fetch-observer` fills from the Steam game; an instance's:
.cache/instances/NAME/observer) or one session-STAMP.tsv. Prints the newest
session (or STAMP's): its span, the sessions, districts, scenes and story
changes of its trail, the triggers by kind, the worst moments, each capture,
and the plugin's last summary. --trail prints every row of the trail too;
--list the sessions there are.
"""

import argparse
import collections
import pathlib
import re
import sys

ROW_GAP = re.compile(r" \(\+(\d+) since the last row\)")
TRIGGER = re.compile(r"^(?P<kind>[a-z-]+) \((?P<severity>\d+)\)(?P<rest>.*)$")


def read_trail(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as file:
        for number, line in enumerate(file):
            if number == 0 and line.startswith("time\t"):
                continue
            parts = line.rstrip("\n").split("\t", 3)
            if len(parts) == 4:
                rows.append({"time": parts[0], "t": parts[1], "kind": parts[2], "text": parts[3]})
    return rows


def read_capture(path):
    head = {"file": path.name, "kind": "?", "severity": 0, "at": "?", "what": "", "lines": 0}
    with open(path, encoding="utf-8", errors="replace") as file:
        for line in file:
            if line.startswith("tandem observer capture "):
                match = re.search(r": (\S+) \(severity (\d+)\)", line)
                if match:
                    head["kind"], head["severity"] = match.group(1), int(match.group(2))
            elif line.startswith("at: "):
                head["at"] = line[4:].strip()
            elif line.startswith("what: "):
                head["what"] = line[6:].strip()
            elif line.startswith("-- the log, "):
                match = re.search(r"\((\d+) lines", line)
                head["lines"] = int(match.group(1)) if match else 0
                break
    return head


def short(text, width=150):
    return text if len(text) <= width else text[: width - 1] + "…"


def report(folder, trail_path, show_trail):
    stamp = trail_path.stem.removeprefix("session-")
    rows = read_trail(trail_path)
    print(f"== observer session {stamp} ({trail_path}) ==")
    if not rows:
        print("empty trail")
        return
    print(f"from {rows[0]['time']} to {rows[-1]['time']} ({float(rows[-1]['t']) / 60:.1f} min), {len(rows)} rows")

    kinds = collections.Counter(row["kind"] for row in rows)
    print("rows: " + ", ".join(f"{kind} {count}" for kind, count in kinds.most_common()))

    print("\n-- sessions")
    for row in rows:
        if row["kind"] in ("session", "observer"):
            print(f"  {row['time']}  {row['text']}")
    districts = [row["text"].split(" (from")[0] for row in rows if row["kind"] == "district"]
    print("\n-- districts, in order: " + (" > ".join(districts) if districts else "none"))
    scenes = [row for row in rows if row["kind"] in ("scene", "scene-kind")]
    print(f"\n-- scenes ({len(scenes)} rows)")
    for row in scenes[-12:]:
        print(f"  {row['time']}  {row['kind']}: {short(row['text'])}")
    story = [row for row in rows if row["kind"] in ("tracked", "restrictions", "prefabs", "gates", "fast-travel", "tier", "story-flags", "call", "reward", "effect", "epoch")]
    print(f"\n-- story changes ({len(story)} rows; journal rows {kinds['journal']}, quest phases first run {kinds['phase']})")
    for row in story[-15:]:
        print(f"  {row['time']}  {row['kind']}: {short(row['text'])}")

    counts = collections.Counter()
    moments = []
    for row in rows:
        if row["kind"] != "trigger":
            continue
        match = TRIGGER.match(row["text"])
        if not match:
            continue
        more = ROW_GAP.search(match.group("rest"))
        counts[match.group("kind")] += 1 + (int(more.group(1)) if more else 0)
        moments.append((int(match.group("severity")), row["time"], match.group("kind"), match.group("rest")))
    print("\n-- triggers: " + (", ".join(f"{kind} {count}" for kind, count in counts.most_common()) if counts else "none"))
    print("-- worst moments")
    for severity, time, kind, rest in sorted(moments, key=lambda m: (-m[0], m[1]))[:10]:
        print(f"  {time}  {kind} ({severity}){short(rest, 140)}")

    captures = sorted((folder / "captures").glob(f"{stamp}-*.txt"))
    print(f"\n-- captures ({len(captures)})")
    for path in captures:
        head = read_capture(path)
        print(f"  {head['at']}  {head['kind']} ({head['severity']}), {head['lines']} lines: {short(head['what'], 110)}")
        print(f"      {path}")

    summary = folder / f"summary-{stamp}.txt"
    if summary.exists():
        blocks = summary.read_text(encoding="utf-8", errors="replace").split("== ")
        print("\n-- the plugin's last summary\n== " + blocks[-1].rstrip())

    if show_trail:
        print("\n-- the trail")
        for row in rows:
            print(f"  {row['time']}  {row['kind']:<12} {row['text']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path", nargs="?", default=".cache/observer")
    parser.add_argument("--session", help="a session's STAMP (YYYYMMDD-HHMMSS)")
    parser.add_argument("--trail", action="store_true", help="every row of the trail too")
    parser.add_argument("--list", action="store_true", help="the sessions there are")
    args = parser.parse_args()
    path = pathlib.Path(args.path)
    if path.is_file():
        report(path.parent, path, args.trail)
        return 0
    trails = sorted(path.glob("session-*.tsv"))
    if not trails:
        print(f"no session-*.tsv in {path} (tools/game.sh fetch-observer first?)", file=sys.stderr)
        return 1
    if args.list:
        for trail in trails:
            rows = read_trail(trail)
            span = f"{rows[0]['time']} to {rows[-1]['time']}" if rows else "empty"
            print(f"{trail.stem.removeprefix('session-')}  {span}  {len(rows)} rows")
        return 0
    if args.session:
        chosen = path / f"session-{args.session}.tsv"
        if not chosen.exists():
            print(f"no {chosen}", file=sys.stderr)
            return 1
    else:
        chosen = trails[-1]
    report(path, chosen, args.trail)
    return 0


if __name__ == "__main__":
    sys.exit(main())
