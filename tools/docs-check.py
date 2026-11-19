#!/usr/bin/env python3
"""Check that editing the docs lost nothing.

    tools/docs-check.py                      links, then facts against HEAD
    tools/docs-check.py links                every doc path, link and index row resolves;
                                             no line the mdsh hook chokes on
    tools/docs-check.py facts [--base REV] [FILE...]
    tools/docs-check.py split --base REV --old PATH... --new PATH...
    tools/docs-check.py tokens [FILE...]     size of the docs, in tokens (~bytes/4)

facts: every "fact token" of the docs at REV (default HEAD) is still somewhere
in the docs of the working tree. Fact tokens are inline-code spans, lines of
fenced blocks, numbers with two digits or more, quantities ("5 s"), hex
values, § references, bracket tags ([2.5-verified]), file paths, and
identifiers in CamelCase or with an underscore, a dot or "::". With FILEs,
each file is compared with its own version at REV instead (stricter: use it
while tightening one file). A token is present when the new text contains it
(numbers and § references: as a whole token).

split: for a move with no rewording. The multiset of non-blank lines under
--old at REV equals the one under --new in the working tree; whitespace runs
and table rules are normalised, since the formatter pads tables. Lines only
on one side are printed.

The docs are AGENTS.md, README.md and every .md under docs/.
"""

import argparse
import collections
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def git(*args):
    return subprocess.run(
        ["git", "-C", ROOT, *args], check=True, capture_output=True, text=True
    ).stdout


def is_doc(path):
    return path in ("AGENTS.md", "README.md") or (
        path.startswith("docs/") and path.endswith(".md")
    )


def docs_at(rev):
    names = git("ls-tree", "-r", "--name-only", rev).splitlines()
    return {p: git("show", f"{rev}:{p}") for p in names if is_doc(p)}


def docs_now():
    names = ["AGENTS.md", "README.md"] + glob.glob("docs/**/*.md", root_dir=ROOT, recursive=True)
    out = {}
    for p in names:
        full = os.path.join(ROOT, p)
        if os.path.isfile(full):
            out[p] = open(full, encoding="utf-8").read()
    return out


# --- fact tokens -----------------------------------------------------------

FENCE = re.compile(r"^ *```.*?^ *```[^\n]*$", re.S | re.M)
SPAN = re.compile(r"`([^`]+)`")
HEX = re.compile(r"\b0x[0-9A-Fa-f]+\b")
HASH = re.compile(r"\b(?=[0-9a-f]*\d)(?=[0-9a-f]*[a-f])[0-9a-f]{7,40}\b")
SECTION = re.compile(r"§ ?\d+(?:\.\d+[a-z]?)*")
TAG = re.compile(r"\[[A-Za-z0-9][\w.\-]*\](?!\()")
NUMBER = re.compile(r"(?<![\w.])\d+(?:[.,:]\d+)*(?![\w])")
QUANTITY = re.compile(
    r"(?<![\w.])(\d+(?:\.\d+)?) ?(ms|µs|us|s|m|km|cm|Hz|%|bytes?|KB|MB|GB|min|h|fps|frames?|×|x)(?![\w])"
)
PATH = re.compile(r"(?:[\w.\-*~$<>{}]+/)+[\w.\-*<>{}]*[\w*>}]")
IDENT = re.compile(r"(?<![\w.])[A-Za-z_]\w*(?:(?:\.|::)\w+)*")
ABBREVIATIONS = {"e.g", "i.e", "vs.the"}


def squash(text):
    return re.sub(r"\s+", " ", text).strip()


def table_free(text):
    """Table padding and rules carry nothing."""
    text = re.sub(r"\|[ :|\-]*-{3,}[ :|\-]*\|", " ", text)
    return text


def facts(text):
    """{token: kind} for a document."""
    out = {}
    for block in FENCE.findall(text):
        for line in block.splitlines()[1:-1]:
            if line.strip():
                out[squash(line)] = "fenced"
    text = FENCE.sub(" ", text)
    text = squash(table_free(text))
    for m in SPAN.finditer(text):
        out[squash(m.group(1))] = "code"
    plain = text.replace("`", " ")
    for rx, kind in ((HEX, "hex"), (HASH, "hash"), (TAG, "tag"), (PATH, "path")):
        for m in rx.finditer(plain):
            out[m.group(0)] = kind
    for m in SECTION.finditer(plain):
        out[m.group(0).replace(" ", "")] = "section"
    for m in QUANTITY.finditer(plain):
        out[m.group(1) + " " + m.group(2)] = "quantity"
    for m in NUMBER.finditer(plain):
        if sum(c.isdigit() for c in m.group(0)) >= 2:
            out[m.group(0)] = "number"
    for m in IDENT.finditer(plain):
        t = m.group(0)
        if t in ABBREVIATIONS:
            continue
        if "_" in t.strip("_") or "." in t or "::" in t or re.search(r"[a-z0-9][A-Z]", t):
            out[t] = "ident"
    return out


class Corpus:
    def __init__(self, texts):
        joined = "\n\n".join(texts)
        self.text = squash(table_free(joined))
        self.plain = self.text.replace("`", " ")
        self.plain_squashed = squash(self.plain)
        self.numbers = set(m.group(0) for m in NUMBER.finditer(self.plain))
        self.sections = set(m.group(0).replace(" ", "") for m in SECTION.finditer(self.plain))
        self.quantities = set(
            m.group(1) + " " + m.group(2) for m in QUANTITY.finditer(self.plain)
        )
        self.idents = set(m.group(0) for m in IDENT.finditer(self.plain))

    def has(self, token, kind):
        if kind == "number":
            return token in self.numbers
        if kind == "section":
            return token in self.sections
        if kind == "quantity":
            return token in self.quantities
        if kind == "ident":
            if token in self.idents:
                return True
            if token not in self.plain:
                return False
            return re.search(r"(?<![\w.])" + re.escape(token) + r"(?!\w)", self.plain) is not None
        return token in self.text or token in self.plain_squashed


def where(text, token):
    """First line of text holding the token's first word, for the report."""
    head = token.split(" ")[0]
    for n, line in enumerate(text.splitlines(), 1):
        if head in line:
            return n
    return 0


def check_facts(base, files):
    old = docs_at(base)
    new = docs_now()
    missing = []
    if files:
        for f in files:
            f = os.path.relpath(os.path.abspath(f), ROOT)
            if f not in old:
                print(f"{f}: not in {base}, nothing to compare")
                continue
            corpus = Corpus([new.get(f, "")])
            for token, kind in facts(old[f]).items():
                if not corpus.has(token, kind):
                    missing.append((f, where(old[f], token), kind, token))
    else:
        corpus = Corpus(list(new.values()))
        for f, text in old.items():
            for token, kind in facts(text).items():
                if not corpus.has(token, kind):
                    missing.append((f, where(text, token), kind, token))
    missing.sort()
    for f, line, kind, token in missing:
        print(f"{f}:{line}: {kind}: {token}")
    scope = "its own file" if files else "the docs"
    print(f"facts: {len(missing)} token(s) of {base} missing from {scope}", file=sys.stderr)
    return 1 if missing else 0


# --- split -----------------------------------------------------------------


def norm_line(line):
    line = squash(line)
    line = re.sub(r"-{3,}", "---", line)
    line = re.sub(r"\| (---( \| )?)+\|?", "|---|", line)
    return line


def lines_of(texts):
    c = collections.Counter()
    for t in texts:
        for line in t.splitlines():
            n = norm_line(line)
            if n and n != "---" and n != "|---|":
                c[n] += 1
    return c


def expand(paths, names):
    out = []
    for p in paths:
        p = p.rstrip("/")
        out += [n for n in names if n == p or n.startswith(p + "/")]
    return out


def check_split(base, old_paths, new_paths):
    old_docs = docs_at(base)
    new_docs = docs_now()
    old = lines_of(old_docs[p] for p in expand(old_paths, old_docs))
    new = lines_of(new_docs[p] for p in expand(new_paths, new_docs))
    lost = old - new
    added = new - old
    for line, n in sorted(lost.items()):
        print(f"- {n}x {line}")
    for line, n in sorted(added.items()):
        print(f"+ {n}x {line}")
    print(
        f"split: {sum(old.values())} lines before, {sum(new.values())} after; "
        f"{sum(lost.values())} only before (-), {sum(added.values())} only after (+)",
        file=sys.stderr,
    )
    return 1 if lost else 0


# --- links -----------------------------------------------------------------

LINK = re.compile(r"\]\(([^)#\s]+)(#[^)\s]*)?\)")
DOC_PATH = re.compile(r"\bdocs/(?:ARCHITECTURE|architecture/[\w.\-]+|research/[\w./\-]+)\.md\b")
OWN_DOC = re.compile(r"\bdocs/[\w\-]+\.md\b")
INDEX = "docs/research/engine-knowledge.md"
ROW = re.compile(r"^\| *(\d+(?:\.\d+[a-z]?)?) *\| *(.*?) *\| *`([\w\-]+\.md)` *\|$")


def check_links():
    bad = []
    docs = docs_now()
    for f, text in docs.items():
        for m in LINK.finditer(text):
            target = m.group(1)
            if re.match(r"[a-z]+:", target):
                continue
            if not os.path.exists(os.path.normpath(os.path.join(ROOT, os.path.dirname(f), target))):
                bad.append(f"{f}: link to {target}: no such file")
    tracked = git("ls-files").splitlines()
    for f in tracked:
        if f.startswith("vendor/") or f.endswith((".tsv", ".png", ".tplay", ".tsession")):
            continue
        try:
            text = open(os.path.join(ROOT, f), encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        # docs/research/ also cites other projects' docs/ by the same names
        found = set(DOC_PATH.findall(text))
        if not f.startswith("docs/research/"):
            found |= set(OWN_DOC.findall(text))
        for path in sorted(found):
            if not os.path.exists(os.path.join(ROOT, path)):
                bad.append(f"{f}: {path}: no such file")
    # every row of the engine index names a file that has that section
    for line in docs.get(INDEX, "").splitlines():
        m = ROW.match(line)
        if not m:
            continue
        num, title, name = m.groups()
        target = docs.get(f"docs/research/engine/{name}")
        if target is None:
            bad.append(f"{INDEX}: §{num}: engine/{name} does not exist")
        elif not re.search(
            r"^#+ " + re.escape(num) + r"\.? " + re.escape(title[:20]), target, re.M
        ):
            bad.append(f"{INDEX}: §{num} {title!r} is not a heading of engine/{name}")
    # and every numbered section of an engine file is in the index
    rows = {(m.group(1), m.group(3)) for m in map(ROW.match, docs.get(INDEX, "").splitlines()) if m}
    for f, text in docs.items():
        if not f.startswith("docs/research/engine/"):
            continue
        for m in re.finditer(r"^#+ (\d+(?:\.\d+[a-z]?)?)\.? ", text, re.M):
            if (m.group(1), os.path.basename(f)) not in rows:
                bad.append(f"{f}: §{m.group(1)} is not in the index ({INDEX})")
    # the core's index lists every topic file, and only files that exist
    core = docs.get("docs/ARCHITECTURE.md", "")
    topics = core.partition("## Topic files")[2].partition("\n## ")[0]
    listed = set(re.findall(r"^- `([\w\-]+\.md)`", topics, re.M))
    have = {os.path.basename(f) for f in docs if f.startswith("docs/architecture/")}
    for name in sorted(listed - have):
        bad.append(f"docs/ARCHITECTURE.md: topic file {name} does not exist")
    for name in sorted(have - listed):
        bad.append(f"docs/ARCHITECTURE.md: docs/architecture/{name} is not in Topic files")
    rows = [x for x in core.partition("## Research")[2].splitlines() if x.startswith("|")]
    for name in re.findall(r"`([\w\-]+\.md)`", "\n".join(rows)):
        if f"docs/research/{name}" not in docs:
            bad.append(f"docs/ARCHITECTURE.md: research file {name} does not exist")
    # mdsh (a commit hook) takes an unindented line that is only a code span
    # for a command, and panics
    for f, text in docs.items():
        for n, line in enumerate(FENCE.sub(lambda m: "\n" * m.group(0).count("\n"), text).splitlines(), 1):
            if re.match(r"^`[^`]+`$", line):
                bad.append(f"{f}:{n}: a line that is only a code span stops the mdsh hook: reflow it")
    for b in bad:
        print(b)
    print(f"links: {len(bad)} broken", file=sys.stderr)
    return 1 if bad else 0


def count_tokens(files):
    docs = docs_now()
    names = [os.path.relpath(os.path.abspath(f), ROOT) for f in files] or sorted(docs)
    total = 0
    for n in names:
        size = len(docs[n].encode())
        total += size
        print(f"{size // 4:8d}  {n}")
    print(f"{total // 4:8d}  total (~bytes/4)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("mode", nargs="?", choices=["links", "facts", "split", "tokens"])
    ap.add_argument("files", nargs="*")
    ap.add_argument("--base", default="HEAD")
    ap.add_argument("--old", nargs="+", default=[])
    ap.add_argument("--new", nargs="+", default=[])
    a = ap.parse_args()
    if a.mode == "links":
        return check_links()
    if a.mode == "facts":
        return check_facts(a.base, a.files)
    if a.mode == "split":
        return check_split(a.base, a.old, a.new)
    if a.mode == "tokens":
        return count_tokens(a.files)
    return check_links() | check_facts(a.base, [])


if __name__ == "__main__":
    sys.exit(main())
