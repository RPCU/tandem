#!/usr/bin/env python3
"""Check tools/scenarios/*.sh against what they drive, without the game.

A scenario is a few lines of helpers (tools/scenario.sh) and runs only
against the real game: a mock-player flag that was renamed, a `cmd` the
scripts no longer take or a helper that does not exist shows up minutes into
an in-game run. Checked here:

- every `--flag` a scenario passes exists in the mock player's command line
  (a "--flag" literal in peer/), in the pilot's, or in tools/;
- the first word of every `cmd ...` (a partner game's `pcmd ...`) is a command the scripts take (a string
  literal that is, or begins with, that word in scripts/tandem);
- every helper a scenario calls is a function of tools/scenario.sh (or a
  shell word);
- every `query VAR WHAT` and `cmd test query WHAT` asks a query
  scripts/tandem/TestQuery.reds answers, every `expect_value` has a known
  operator and `expect_changed` a known change, and the variables they
  compare are set in the scenario.

Usage: tools/scenarios-check.py   (exit 1 on a finding)
"""

import re
import shlex
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SHELL = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac", "return", "local", "sleep",
    "echo", "printf", "grep", "pkill", "pgrep", "true", "false", "test", "[", "[[", "wait", "kill", "rm", "mkdir",
    "cat", "head", "tail", "sed", "awk", "ssh", "scp", "export", "read", "shift", "set", "unset", "trap", "break",
    "continue", "exit", "source", ".", "!", "{", "}", "cp", "mv", "touch", "date", "seq", "tr", "cut", "wc", "sort",
    "tools/game.sh", "tools/instance.sh", "build/linux/peer/tandem-peer", "python3", "timeout", "nohup", "until",
}


def code_lines(path: Path):
    """(number, line) without comments and continuation breaks."""
    pending, start = "", 0
    for number, line in enumerate(path.read_text().splitlines(), 1):
        stripped = re.sub(r"(^|\s)#.*$", "", line) if "'" not in line and '"' not in line else line
        if stripped.lstrip().startswith("#"):
            continue
        if not pending:
            start = number
        if stripped.endswith("\\"):
            pending += stripped[:-1] + " "
            continue
        yield start, pending + stripped
        pending = ""


CHANGES = re.compile(r"down|up|same|changed|[+-]?[0-9]+(\.[0-9]+)?|\$\{?\w+\}?|\$\(\(.*\)\)")
OPERATORS = {"=", "!=", "~", "<", "<=", ">", ">="}


def check_queries(where: str, line: str, queries: set, assigned: set, globals_: set) -> int:
    """`query VAR WHAT`, `cmd test query WHAT`, `expect_value`, `expect_changed`:
    a query the scripts answer, into none of the harness's own variables ($out
    is the evidence folder), a known operator or change, and variables the
    scenario sets (a typo'd one would compare an empty value)."""
    try:
        words = shlex.split(line, comments=True)
    except ValueError:
        return 0
    if not words:
        return 0
    problems, what = 0, None
    if words[0] in ("query", "pquery"):
        if len(words) < 3 or not re.fullmatch(r"[A-Za-z_]\w*", words[1]):
            print(f"{where}: query VAR WHAT [ARGS]: {' '.join(words[1:])}", file=sys.stderr)
            return 1
        if words[1] in globals_:
            print(f"{where}: query {words[1]} would overwrite tools/scenario.sh's own ${words[1]}", file=sys.stderr)
            problems += 1
        what = words[2]
    elif words[0] in ("cmd", "pcmd") and words[1:3] == ["test", "query"] and len(words) > 3:
        what = words[3]
    if what is not None and what not in queries:
        print(f"{where}: no query '{what}' in scripts/tandem/TestQuery.reds ({', '.join(sorted(queries))})", file=sys.stderr)
        problems += 1
    if words[0] in ("expect_value", "expect_changed"):
        if len(words) != 4:
            print(f"{where}: {words[0]} takes 3 arguments", file=sys.stderr)
            return problems + 1
        if words[0] == "expect_value" and words[2] not in OPERATORS:
            print(f"{where}: expect_value has no operator '{words[2]}' ({' '.join(sorted(OPERATORS))})", file=sys.stderr)
            problems += 1
        if words[0] == "expect_changed" and not CHANGES.fullmatch(words[3]):
            print(f"{where}: expect_changed has no change '{words[3]}' (down, up, same, changed, N, +N, -N)", file=sys.stderr)
            problems += 1
        for name in re.findall(r"\$\{?(\w+)", " ".join(words[1:])):
            if name not in assigned and not name.isdigit():
                print(f"{where}: ${name} is set nowhere in the scenario (query {name} ...?)", file=sys.stderr)
                problems += 1
    return problems


def main() -> int:
    literals = set()
    for folder, pattern in (("peer", "*.cpp"), ("peer", "*.hpp"), ("tools/pilot", "*.go"), ("tools", "*.sh"), ("tools", "*.py")):
        for path in (ROOT / folder).rglob(pattern):
            if "scenarios" in path.parts:
                continue
            literals.update(re.findall(r"--[a-z0-9][a-z0-9-]*", path.read_text(errors="replace")))
    commands = set()
    for path in (ROOT / "scripts/tandem").rglob("*.reds"):
        for text in re.findall(r'"([a-z][a-z0-9-]*)[ "]', path.read_text()):
            commands.add(text)
    harness = (ROOT / "tools/scenario.sh").read_text()
    helpers = set(re.findall(r"^\s*(\w+)\(\)\s*\{", harness, re.M))
    queries = set(re.findall(r'Equals\(what, "([a-z]+)"\)', (ROOT / "scripts/tandem/TestQuery.reds").read_text()))
    harness_vars = set(re.findall(r"(?:^|[\s;])(\w+)=", harness))
    globals_ = {name for line in harness.splitlines() if re.match(r"\w+=", line)
                for name in re.findall(r"(?:^|\s)(\w+)=", line)}
    problems = 0
    for path in sorted((ROOT / "tools/scenarios").glob("*.sh")):
        text = path.read_text()
        own = set(re.findall(r"^\s*(\w+)\(\)\s*\{", text, re.M))
        assigned = harness_vars | set(re.findall(r"(?:^|[\s;])(\w+)=", text))
        assigned |= set(re.findall(r"^\s*p?query\s+(\w+)\s", text, re.M))
        for names in re.findall(r"\bread\s+(?:-\w+\s+)*([\w ]+)", text):
            assigned |= set(names.split())
        assigned |= set(re.findall(r"\bfor\s+(\w+)\s+in\b", text))
        heredoc = None
        for number, line in code_lines(path):
            if heredoc:
                if line.strip() == heredoc:
                    heredoc = None
                continue
            if m := re.search(r"<<-?\s*['\"]?(\w+)['\"]?", line):
                heredoc = m.group(1)
            where = f"{path.relative_to(ROOT)}:{number}"
            words = line.split()
            if not words:
                continue
            first = words[0]
            plain = re.sub(r"'[^']*'|\"[^\"]*\"", " ", line)  # flags inside expected log lines are text
            if first in ("peer", "peer2", "play", "host_world") or "tandem-peer" in line:
                for flag in re.findall(r"(?<![\w-])(--[a-z0-9][a-z0-9-]*)", plain):
                    if flag not in literals:
                        print(f"{where}: {flag} is no flag of tandem-peer, the pilot or tools/", file=sys.stderr)
                        problems += 1
            if first in ("cmd", "pcmd") and len(words) > 1:
                word = words[1].strip("'\"")
                if re.fullmatch(r"[a-z][a-z0-9-]*", word) and word not in commands:
                    print(f"{where}: no script takes the command '{word}' (cmd {' '.join(words[1:3])})", file=sys.stderr)
                    problems += 1
            problems += check_queries(where, line, queries, assigned, globals_)
            if re.fullmatch(r"[a-z_][a-z0-9_]*", first) and "=" not in first:
                if first not in helpers and first not in own and first not in SHELL and not line.lstrip().startswith(first + "="):
                    print(f"{where}: '{first}' is no helper of tools/scenario.sh", file=sys.stderr)
                    problems += 1
    if problems:
        print(f"scenarios-check: {problems} problem(s)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
