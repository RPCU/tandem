#!/usr/bin/env python3
"""Check tools/scenarios/*.sh against what they drive, without the game.

A scenario is a few lines of helpers (tools/scenario.sh) and runs only
against the real game: a mock-player flag that was renamed, a `cmd` the
scripts no longer take or a helper that does not exist shows up minutes into
an in-game run. Checked here:

- every `--flag` a scenario passes exists in the mock player's command line
  (a "--flag" literal in peer/), in the pilot's, or in tools/;
- the first word of every `cmd ...` is a command the scripts take (a string
  literal that is, or begins with, that word in scripts/tandem);
- every helper a scenario calls is a function of tools/scenario.sh (or a
  shell word).

Usage: tools/scenarios-check.py   (exit 1 on a finding)
"""

import re
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
    problems = 0
    for path in sorted((ROOT / "tools/scenarios").glob("*.sh")):
        own = set(re.findall(r"^\s*(\w+)\(\)\s*\{", path.read_text(), re.M))
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
            if first == "cmd" and len(words) > 1:
                word = words[1].strip("'\"")
                if re.fullmatch(r"[a-z][a-z0-9-]*", word) and word not in commands:
                    print(f"{where}: no script takes the command '{word}' (cmd {' '.join(words[1:3])})", file=sys.stderr)
                    problems += 1
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
