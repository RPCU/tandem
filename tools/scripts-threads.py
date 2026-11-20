#!/usr/bin/env python3
"""Fail on a script hook that may run on a job thread and changes script state.

The engine runs the script callbacks of entities, components and persistent
states on job threads, many at once while a save loads and the world streams
in. Wraps that pushed into a system's array and hash map from there
corrupted the heap: an access violation in the engine's allocator during a
load, 5 launches in 6 (2026-11-20: Device and VehicleObject OnGameAttached
and OnDetach, ScriptableDeviceComponentPS.Notify, the loot's Entity/Attach).
scc compiles all of it.

Checked, as a hook that may run on a job thread:

- a @wrapMethod or @replaceMethod of a class that exists once per entity,
  component or persistent state (its ancestry in tools/scripts-threads.classes
  reaches Entity, IComponent or PersistentState), of a `cb func` of any
  class, or of a lifecycle method (OnGameAttached, OnDetach, OnRestored...);
- a `cb func` of our own classes (a blackboard or input listener, a
  Codeware callback: RegisterCallback(n"Entity/Attach", this, n"Method")).

Such a hook, and every scripts/tandem function it calls, may read, call the
wrapped method, the engine and our natives (they only enqueue), and change
what is its own: its locals, objects it made, the fields of the wrapped
object itself. Refused: writing a field of any other script object of ours,
changing an array or a hash map held in one, and a `Take...` native (the
game thread's side of a queue).

Fix: queue what happened (TandemStreaming.QueueStreamed, or another native
that only enqueues) and act on the tick. Or say why not in the comment above
the hook:

    // game thread only: <how it was proven>
    // job thread safe: <why what it changes cannot be reached from two threads>

tools/scripts-threads.known lists the hooks that changed state when this
check was written and are neither proven nor reworked yet: reported by
--list, not failed. A hook listed there that no longer needs it fails (the
list only shrinks).

Usage: tools/scripts-threads.py [DIR]         (exit 1 on a finding)
       tools/scripts-threads.py --list [DIR]  every checked hook and its verdict
       tools/scripts-threads.py --all [DIR]   and every change each can reach
       tools/scripts-threads.py --classes VANILLA.reds [DIR]
           rewrite tools/scripts-threads.classes from the decompiled scripts
           (~/.cache/tandem/ref/gamescripts/vanilla/final.reds): the ancestry
           of every class scripts/tandem wraps. A wrap of a class that is
           not in it fails the check.
"""

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLASSES = Path(__file__).with_suffix(".classes")
KNOWN = Path(__file__).with_suffix(".known")
OPT_OUT = re.compile(r"//\s*(game thread only|job thread safe):\s*(\S.{9,})")

# One instance per entity, component or persistent state.
PER_ENTITY_ROOTS = {"Entity": "an entity", "IComponent": "a component", "PersistentState": "a persistent state"}
LIFECYCLE = re.compile(
    r"^(On\w*(Attach|Attached|Detach|Detached|Deattach)\w*|OnRestored|OnPersitentStateInitialized|"
    r"OnPersistentStateInitialized|OnPreUninitialize|OnLogicReady|OnRequestComponents|OnTakeControl)$"
)
ARRAY_WRITES = {
    "ArrayPush", "ArrayInsert", "ArrayErase", "ArrayEraseFast", "ArrayRemove", "ArrayClear",
    "ArrayResize", "ArrayPop", "ArrayGrow", "ArraySort", "ArraySortInts", "ArraySortFloats",
}
# Engine containers we hold in fields.
MAP_TYPES = re.compile(r"HashMap")
MAP_WRITES = {"Insert", "Set", "Remove", "Clear"}
STATEMENT_WORDS = {"if", "while", "for", "switch", "return", "case", "else", "in", "break", "default", "as"}
ASSIGN = {"=", "+=", "-=", "*=", "/=", "|=", "&="}
ENGINE = "!"  # a type that is not ours


def mask(text: str) -> str:
    """The text with comments and string contents blanked (same length, same
    lines): what they hold is no code. The expressions of an interpolated
    string, \\(...), are kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == '"':
            out.append('"')
            i += 1
            while i < n and text[i] != '"':
                if text.startswith("\\(", i):
                    depth, j = 0, i + 1
                    while j < n:
                        if text[j] == "(":
                            depth += 1
                        elif text[j] == ")":
                            depth -= 1
                            if depth == 0:
                                break
                        j += 1
                    out.append("  " + text[i + 2 : j] + " ")
                    i = j + 1
                elif text[i] == "\\":
                    out.append("  ")
                    i += 2
                else:
                    out.append("\n" if text[i] == "\n" else " ")
                    i += 1
            out.append('"')
            i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


@dataclass
class Func:
    path: Path
    line: int
    owner: str  # the class it is a method of ("" for a global)
    name: str
    hook: str  # "wrapMethod", "replaceMethod", "addMethod" or ""
    cb: bool
    static: bool
    native: bool
    params: str
    returns: str
    body: str  # masked
    body_line: int
    comment: str  # the comment lines right above it
    writes: list = field(default_factory=list)  # (line, what, a field of its own object)
    calls: list = field(default_factory=list)  # (line, [Func], text, how the receiver is held)
    reason: list = None  # why it changes state: [(Func, line, what)...]

    @property
    def label(self):
        prefix = f"@{self.hook}({self.owner}) " if self.hook else (self.owner + "." if self.owner else "")
        return prefix + self.name

    @property
    def key(self):
        return f"{self.path.name} {self.owner}.{self.name}"


@dataclass
class Class:
    name: str
    parent: str
    fields: dict = field(default_factory=dict)  # name -> type
    struct: bool = False


TYPE_DECL = re.compile(
    r"^[ \t]*(?:(?:public|private|protected|native|abstract|final|importonly)\s+)*(class|struct)\s+(\w+)"
    r"(?:\s+extends\s+(\w+))?\s*\{",
    re.M,
)
ENUM_DECL = re.compile(r"^[ \t]*(?:(?:public|private)\s+)?enum\s+(\w+)", re.M)
FUNC_DECL = re.compile(
    r"^[ \t]*((?:(?:public|private|protected|static|final|native|cb|exec|quest|const)\s+)*)func\s+(\w+)\s*\(",
    re.M,
)
ANNOTATION = re.compile(r"^[ \t]*@(\w+)\(\s*(\w+)\s*\)")
FIELD_DECL = re.compile(
    r"^[ \t]*(?:(?:public|private|protected|native|const|persistent|static)\s+)*let\s+(\w+)\s*:\s*([^;=]+);", re.M
)
TOKEN = re.compile(r'"[^"]*"|[A-Za-z_]\w*|\d[\w.]*|->|[-+*/|&=!<>]=|&&|\|\||\S')


def matching(text: str, start: int, open_: str, close: str) -> int:
    """Index after the bracket that closes the one at `start`."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == open_:
            depth += 1
        elif text[i] == close:
            depth -= 1
            if depth == 0:
                return i + 1
    return len(text)


def base(kind: str) -> str:
    """ref<TandemLoot> -> TandemLoot."""
    kind = (kind or "").strip()
    while m := re.match(r"^(?:ref|wref|script_ref)<(.*)>$", kind):
        kind = m.group(1).strip()
    return kind


def element(kind: str) -> str:
    """array<ref<X>> -> ref<X>; "" when it is no array."""
    kind = (kind or "").strip()
    if m := re.match(r"^array<(.*)>$", kind):
        return m.group(1).strip()
    if m := re.match(r"^\[(.*);\s*\d+\]$", kind):
        return m.group(1).strip()
    return ""


def is_handle(kind: str) -> bool:
    return (kind or "").strip().startswith(("ref<", "wref<", "script_ref<"))


def parse(directory: Path):
    funcs, classes, enums = [], {}, set()
    for path in sorted(directory.rglob("*.reds")):
        raw = path.read_text()
        code = mask(raw)
        lines = raw.splitlines()
        code_lines = code.splitlines()
        starts = [0]
        for line in code_lines:
            starts.append(starts[-1] + len(line) + 1)

        def line_of(pos, starts=starts):
            lo, hi = 0, len(starts) - 1
            while lo < hi:
                mid = (lo + hi + 1) // 2
                if starts[mid] <= pos:
                    lo = mid
                else:
                    hi = mid - 1
            return lo + 1

        enums.update(ENUM_DECL.findall(code))
        spans = []
        for m in TYPE_DECL.finditer(code):
            end = matching(code, m.end() - 1, "{", "}")
            cls = classes.setdefault(m.group(2), Class(m.group(2), m.group(3) or "", struct=m.group(1) == "struct"))
            spans.append((m.end(), end, cls))
        bodies = []
        for m in FUNC_DECL.finditer(code):
            mods = m.group(1).split()
            close = matching(code, m.end() - 1, "(", ")")
            params = code[m.end() : close - 1]
            rest = re.match(r"[ \t]*(?:->\s*([^{;\n]+?))?\s*(\{|;|$)", code[close:], re.M)
            returns = (rest.group(1) or "").strip() if rest else ""
            number = line_of(m.start())
            body, body_line = "", number
            if rest and rest.group(2) == "{" and "native" not in mods:
                start = close + rest.end() - 1
                end = matching(code, start, "{", "}")
                body, body_line = code[start:end], line_of(start)
                bodies.append((start, end))
            owner = next((c.name for a, b, c in spans if a <= m.start() < b), "")
            hook = ""
            if not owner:
                # Its annotation, on the lines above.
                i = number - 2
                while i >= 0 and (not code_lines[i].strip() or code_lines[i].lstrip().startswith("@")):
                    if a := ANNOTATION.match(code_lines[i]):
                        hook, owner = a.group(1), a.group(2)
                        break
                    i -= 1
            # The comment right above it (and above its annotations).
            comment, i = [], number - 2
            while i >= 0 and (lines[i].strip().startswith("//") or lines[i].lstrip().startswith("@")):
                comment.append(lines[i].strip())
                i -= 1
            funcs.append(
                Func(path, number, owner, m.group(2), hook, "cb" in mods, "static" in mods, "native" in mods,
                     params, returns, body, body_line, "\n".join(reversed(comment)))
            )
        # Fields: in a type and in no function, or under @addField(Class).
        for m in FIELD_DECL.finditer(code):
            if any(a <= m.start() < b for a, b in bodies):
                continue
            owner = next((c for a, b, c in spans if a <= m.start() < b), None)
            if owner is None:
                i = line_of(m.start()) - 2
                while i >= 0 and (not code_lines[i].strip() or code_lines[i].lstrip().startswith("@")):
                    if (a := ANNOTATION.match(code_lines[i])) and a.group(1) == "addField":
                        owner = classes.setdefault("+" + a.group(2), Class(a.group(2), ""))
                        break
                    i -= 1
            if owner is not None:
                owner.fields[m.group(1)] = m.group(2).strip()
    return funcs, classes, enums


class Analysis:
    def __init__(self, funcs, classes, enums, ancestry):
        self.funcs, self.classes, self.enums, self.ancestry = funcs, classes, enums, ancestry
        self.by_key, self.by_name = {}, {}
        for f in funcs:
            self.by_key.setdefault((f.owner, f.name), []).append(f)
            self.by_name.setdefault(f.name, []).append(f)
        self.ours = {name for name in classes if not name.startswith("+")}
        for f in funcs:
            if f.body:
                Scan(self, f).run()
        # What changes state, and through what, to a fixed point. Twice per
        # function: called on anyone's object, and called on one only the
        # caller holds (writes to its own fields are then nobody else's).
        self.reason = {}
        for f in funcs:
            for line, what, own in f.writes:
                self.reason.setdefault((id(f), False), [(f, line, what)])
                if not own or f.hook:
                    self.reason.setdefault((id(f), True), [(f, line, what)])
        changed = True
        while changed:
            changed = False
            for f in funcs:
                for owned in (False, True):
                    if (id(f), owned) in self.reason:
                        continue
                    for line, targets, text, held in f.calls:
                        mode = held == "owned" or (held == "own" and owned)
                        g = next((g for g in targets if g is not f and (id(g), mode) in self.reason), None)
                        if g:
                            self.reason[(id(f), owned)] = [(f, line, f"calls {text}")] + self.reason[(id(g), mode)]
                            changed = True
                            break
        for f in funcs:
            f.reason = self.reason.get((id(f), False))

    def reach(self, f: Func) -> list:
        """Every change of state `f` can reach: (function, line, what)."""
        seen, out, todo = {(id(f), False)}, [], [(f, False)]
        while todo:
            g, owned = todo.pop()
            out += [(g, line, what) for line, what, own in g.writes if not (owned and own and not g.hook)]
            for _, targets, _, held in g.calls:
                mode = held == "owned" or (held == "own" and owned)
                for h in targets:
                    if (id(h), mode) not in seen and (id(h), mode) in self.reason:
                        seen.add((id(h), mode))
                        todo.append((h, mode))
        return out

    def parents(self, cls: str):
        """cls and its ancestors: ours, then the game's (the classes file)."""
        seen = []
        while cls and cls not in seen:
            seen.append(cls)
            if cls in self.classes and self.classes[cls].parent:
                cls = self.classes[cls].parent
            elif cls in self.ancestry and len(self.ancestry[cls]) > 1:
                cls = self.ancestry[cls][1]
            else:
                break
        return seen

    def field_type(self, cls: str, name: str) -> str:
        for c in self.parents(cls):
            for key in (c, "+" + c):
                if key in self.classes and name in self.classes[key].fields:
                    return self.classes[key].fields[name]
        return ""

    def methods(self, cls: str, name: str):
        for c in self.parents(cls):
            if found := self.by_key.get((c, name)):
                return found
        return []

    def is_ours(self, kind: str) -> bool:
        return base(kind) in self.ours

    def has_ours(self, cls: str) -> bool:
        """An engine class we gave fields or methods to."""
        return any("+" + c in self.classes for c in self.parents(cls))


class Scan:
    """One function's body: what it writes that is not its own, and which of
    our functions it calls."""

    def __init__(self, analysis: Analysis, f: Func):
        self.a, self.f = analysis, f
        self.tokens = [(m.group(0), m.start()) for m in TOKEN.finditer(f.body)]
        self.locals = {}  # name -> type ("" unknown, ENGINE not ours)
        self.fresh = set()  # locals nobody else holds: values, objects made here
        for part in split_top(f.params):
            if m := re.match(r"\s*((?:(?:out|opt|const)\s+)*)(\w+)\s*:\s*(.+?)\s*$", part, re.S):
                self.declare(m.group(2), m.group(3), None, by_value="out" not in m.group(1))

    def line(self, i: int) -> int:
        return self.f.body_line + self.f.body.count("\n", 0, self.tokens[i][1])

    def tok(self, i: int) -> str:
        return self.tokens[i][0] if 0 <= i < len(self.tokens) else ""

    def close(self, i: int) -> int:
        """Index of the bracket closing the one at token i."""
        pairs = {"(": ")", "[": "]", "{": "}"}
        open_, close, depth = self.tok(i), pairs[self.tok(i)], 0
        for j in range(i, len(self.tokens)):
            if self.tok(j) == open_:
                depth += 1
            elif self.tok(j) == close:
                depth -= 1
                if depth == 0:
                    return j
        return len(self.tokens) - 1

    def declare(self, name: str, kind: str, value, by_value=True) -> None:
        """A local: its type, and whether it is ours alone."""
        kind = (kind or "").strip()
        if kind:
            self.locals[name] = kind if (self.a.is_ours(kind) or self.a.is_ours(element(kind))) else (
                kind if element(kind) else ENGINE)
            # A struct, an array, a number: a copy of our own.
            if by_value and not is_handle(kind):
                self.fresh.add(name)
        if value is not None:
            value_type, made = value
            if not kind:
                self.locals[name] = value_type
                if made or (value_type and value_type != ENGINE and not is_handle(value_type)
                            and (element(value_type) or self.a.classes.get(base(value_type), Class("", "")).struct
                                 or base(value_type) not in self.a.ours)):
                    self.fresh.add(name)
            elif made:
                self.fresh.add(name)
        self.locals.setdefault(name, "")

    def run(self) -> None:
        self.walk(0, len(self.tokens))

    def walk(self, i: int, end: int) -> None:
        while i < end:
            t = self.tok(i)
            if t == "let" and re.match(r"[A-Za-z_]", self.tok(i + 1)):
                i = self.let(i, end)
            elif t == "for" and self.tok(i + 2) == "in":
                name = self.tok(i + 1)
                j = i + 3
                stop = j
                while stop < end and self.tok(stop) != "{":
                    stop += 1
                _, kind, _ = self.chain(j, stop)
                inner = element(kind)
                self.locals[name] = inner if inner else (ENGINE if kind == ENGINE else "")
                if inner and not is_handle(inner):
                    self.fresh.add(name)
                self.walk(j, stop)
                i = stop
            elif re.match(r"[A-Za-z_]", t) or t == "(":
                if t in STATEMENT_WORDS:
                    i += 1
                    continue
                nxt, _, target = self.chain(i, end)
                if self.tok(nxt) in ASSIGN and target:
                    self.assign(i, target)
                i = max(nxt, i + 1)
            else:
                i += 1

    def let(self, i: int, end: int) -> int:
        name = self.tok(i + 1)
        j = i + 2
        kind = ""
        if self.tok(j) == ":":
            k = j + 1
            depth = 0
            while k < end and not (depth == 0 and self.tok(k) in ("=", ";")):
                depth += {"<": 1, ">": -1}.get(self.tok(k), 0)
                k += 1
            kind = "".join(self.tok(x) for x in range(j + 1, k))
            j = k
        value = None
        if self.tok(j) == "=":
            stop = j + 1
            depth = 0
            while stop < end and not (depth == 0 and self.tok(stop) == ";"):
                depth += {"(": 1, "[": 1, "{": 1, ")": -1, "]": -1, "}": -1}.get(self.tok(stop), 0)
                stop += 1
            value = self.expression(j + 1, stop)
            j = stop
        self.declare(name, kind, value)
        return j + 1

    def expression(self, i: int, end: int):
        """(type, made here) of tokens i..end, its calls noted."""
        if i >= end:
            return "", False
        made = self.tok(i) == "new"
        nxt, kind, _ = self.chain(i, end)
        literal = nxt == i + 1 and re.match(r'^[\d"]|^(true|false|null)$', self.tok(i))
        if re.match(r"^[ntsr]$", self.tok(i)) and self.tok(i + 1).startswith('"'):
            literal, nxt = True, i + 2
        if nxt < end:
            # More than one chain: an operator, a cast.
            self.walk(nxt, end)
            if self.tok(end - 2) == "as":
                return self.typed(self.tok(end - 1)), False
            return ("", False) if not literal else (ENGINE, True)
        if literal:
            return "Int32", True
        return kind, made

    def typed(self, name: str) -> str:
        return f"ref<{name}>" if name in self.a.ours else ENGINE

    def chain(self, i: int, end: int):
        """A postfix chain from token i: (next index, its type, the written
        target if it ends in a field or an element)."""
        f, a = self.f, self.a
        t = self.tok(i)
        kind, root, depth, last_field, text = "", None, 0, False, t
        j = i + 1
        if t == "(":
            c = min(self.close(i), end)
            self.walk(i + 1, c)
            kind = self.typed(self.tok(c - 1)) if self.tok(c - 2) == "as" else ""
            root, j = "(", c + 1
        elif t == "new":
            name = self.tok(i + 1)
            kind, root = (f"ref<{name}>" if name in a.ours and not a.classes[name].struct else name), "new"
            if name not in a.ours:
                kind = ENGINE
            j = i + 2
            if self.tok(j) == "(":
                c = self.close(j)
                self.walk(j + 1, c)
                j = c + 1
        elif not re.match(r"[A-Za-z_]", t):
            return i + 1, "", None
        elif t == "this":
            root = "this"
            kind = f"ref<{f.owner}>" if f.owner in a.ours else ("+" + f.owner if f.owner else "")
        elif t in self.locals:
            root, kind = t, self.locals[t]
        elif self.tok(j) == "(":
            # A bare call: a global function of ours, an intrinsic, a cast.
            c = self.close(j)
            if t in ARRAY_WRITES:
                self.array_write(i, j + 1, c)
            else:
                self.walk(j + 1, c)
                targets = [g for g in a.by_name.get(t, []) if not g.owner]
                if targets:
                    f.calls.append((self.line(i), targets, t, ""))
                    kind = targets[0].returns
                else:
                    kind = ENGINE if t not in ("FromVariant", "Cast") else ""
            root, j = "call", c + 1
        elif self.tok(j) == "<" and t in ("Cast", "FromVariant", "IntEnum"):
            return i + 1, "", None
        elif t in a.ours:
            root, kind = "static", "=" + t
        elif t in a.enums:
            return (i + 3 if self.tok(j) == "." else i + 1), ENGINE, None
        else:
            root, kind = "engine", ENGINE
        # .field, .Method(...), [index]
        while j < end:
            if self.tok(j) == "." and re.match(r"[A-Za-z_]", self.tok(j + 1)):
                name = self.tok(j + 1)
                text += "." + name
                if self.tok(j + 2) == "(":
                    c = self.close(j + 2)
                    kind = self.call(i, kind, name, text, root, depth)
                    self.walk(j + 3, c)
                    j, last_field = c + 1, False
                else:
                    kind = self.field(kind, name)
                    j, last_field = j + 2, True
                depth += 1
            elif self.tok(j) == "[":
                c = self.close(j)
                self.walk(j + 1, c)
                kind = element(kind) or (ENGINE if kind == ENGINE else "")
                text += "[]"
                j = c + 1
            else:
                break
        target = (root, depth, text, kind) if (last_field or text.endswith("[]")) and depth > 0 else None
        if root in self.locals and depth == 0 and text.endswith("[]"):
            target = None  # an element of a local array
        self.last = (root, depth, text)
        return j, kind, target

    def field(self, kind: str, name: str) -> str:
        a = self.a
        if kind == ENGINE or not kind:
            return kind
        cls = kind[1:] if kind[:1] in "+=" else base(kind)
        found = a.field_type(cls, name)
        if found:
            return found if (a.is_ours(found) or a.is_ours(element(found)) or element(found)
                             or MAP_TYPES.search(found)) else ENGINE
        return ENGINE if kind.startswith("+") or cls in a.ours else ""

    def owned(self, root, depth: int) -> bool:
        """True when what a chain from `root` reaches is the hook's own: a
        fresh local, or the wrapped object's own field."""
        if root in ("new",) or root in self.fresh:
            return True
        return root == "this" and bool(self.f.hook) and depth <= 1

    def assign(self, i: int, target) -> None:
        root, depth, text, kind = target
        if self.owned(root, depth) or root in ("engine", "call", "(", "static"):
            return
        holder = self.locals.get(root, "") if root != "this" else "this"
        if holder == ENGINE:
            return  # an engine object's own field, as its own scripts write it
        self.f.writes.append((self.line(i), f"writes {text}", root == "this"))

    def array_write(self, i: int, start: int, close: int) -> None:
        name = self.tok(i)
        stop = start
        depth = 0
        while stop < close and not (depth == 0 and self.tok(stop) == ","):
            depth += {"(": 1, "[": 1, ")": -1, "]": -1}.get(self.tok(stop), 0)
            stop += 1
        nxt, _, _ = self.chain(start, stop)
        root, depth, text = self.last
        self.walk(stop, close)
        if nxt != stop:
            return
        # Arrays are values: a local one is ours alone.
        if root in self.locals and depth == 0:
            return
        if self.owned(root, depth):
            return
        if root in self.locals and self.locals[root] == ENGINE:
            return
        self.f.writes.append((self.line(i), f"{name}({text}, ...)", root == "this"))

    def call(self, i: int, kind: str, name: str, text: str, root, depth: int) -> str:
        """A method call on something of type `kind`: noted when it may be
        ours; its return type."""
        a, f = self.a, self.f
        line = self.line(i)
        # How the receiver is held: the function's own object (this.Method()),
        # one only it can reach (made here; a hook's field we added to the
        # wrapped object), or anyone's.
        held = ""
        if root == "this" and depth == 0:
            held = "own"
        elif root == "new" or root in self.fresh:
            held = "owned"
        elif root == "this" and depth == 1 and f.hook and kind and base(kind) in a.ours:
            held = "owned"
        if kind.startswith("="):
            cls = kind[1:]
            targets = a.methods(cls, name)
            if name.startswith("Take") and any(g.native for g in targets):
                f.writes.append((line, f"{text}(): the game thread's side of a native queue", False))
            elif targets:
                f.calls.append((line, targets, text, ""))
            return targets[0].returns if targets else ""
        if MAP_TYPES.search(kind) and name in MAP_WRITES:
            if not self.owned(root, depth - 1 if root == "this" else depth):
                f.writes.append((line, f"{text}(): changes a map", root == "this"))
            return ENGINE
        if kind == ENGINE:
            return ENGINE
        if kind.startswith("+"):
            # The wrapped (engine) object: a method we added, or wrap.
            targets = [g for g in a.methods(kind[1:], name) if g is not f]
            if targets:
                f.calls.append((line, targets, text, held))
                return targets[0].returns
            return ENGINE
        if kind and base(kind) in a.ours:
            targets = a.methods(base(kind), name)
            if targets:
                f.calls.append((line, targets, text, held))
                return targets[0].returns
            return ""  # an engine method of a class of ours (GetGameInstance...)
        if kind:
            return ENGINE
        # Unknown: any method of ours with that name.
        targets = [g for g in a.by_name.get(name, []) if g.owner and not g.static]
        if targets:
            f.calls.append((line, targets, text, ""))
            returns = {g.returns for g in targets}
            return returns.pop() if len(returns) == 1 else ""
        return ENGINE


def split_top(text: str) -> list:
    """Parameters, split at the commas outside <...>."""
    parts, depth, current = [], 0, []
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(ch)
    if "".join(current).strip():
        parts.append("".join(current))
    return parts


def load_classes() -> dict:
    """class -> its ancestry (itself first)."""
    out = {}
    if CLASSES.exists():
        for line in CLASSES.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                chain = [part.strip() for part in line.split("<")]
                out[chain[0]] = chain
    return out


def load_known() -> dict:
    out = {}
    if KNOWN.exists():
        for line in KNOWN.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                out[line] = False
    return out


def write_classes(vanilla: Path, wrapped: set) -> int:
    parents = {}
    decl = re.compile(
        r"^\s*(?:(?:public|private|protected|native|abstract|final|importonly)\s+)*class\s+(\w+)(?:\s+extends\s+(\w+))?")
    with vanilla.open(errors="replace") as source:
        for line in source:
            if "class " in line and (m := decl.match(line)):
                parents.setdefault(m.group(1), m.group(2) or "")
    rows, missing = [], []
    for name in sorted(wrapped):
        if name not in parents:
            missing.append(name)
            continue
        chain = [name]
        while parents.get(chain[-1]) and parents[chain[-1]] not in chain:
            chain.append(parents[chain[-1]])
        rows.append(" < ".join(chain))
    header = (
        "# The ancestry of every game class scripts/tandem wraps (class < parent < ...),\n"
        "# from the decompiled scripts of game 2.50. tools/scripts-threads.py reads it to\n"
        "# tell what exists once per entity, component or persistent state. Rewrite it\n"
        "# with `tools/scripts-threads.py --classes VANILLA.reds` when a class is wrapped\n"
        "# for the first time, or add its line by hand (a class the decompiled scripts\n"
        "# do not declare: its name alone).\n"
    )
    kept = [line for line in (CLASSES.read_text().splitlines() if CLASSES.exists() else [])
            if line.strip() in missing]
    CLASSES.write_text(header + "\n".join(sorted(rows + kept)) + "\n")
    for name in missing:
        if name not in kept:
            print(f"scripts-threads: class {name} is not declared in {vanilla}: add its line by hand", file=sys.stderr)
    print(f"scripts-threads: {len(rows) + len(kept)} classes in {CLASSES.relative_to(ROOT)}")
    return 0


def main() -> int:
    args = sys.argv[1:]
    everything = "--all" in args
    listing = "--list" in args or everything
    args = [a for a in args if a not in ("--list", "--all")]
    vanilla = None
    if "--classes" in args:
        i = args.index("--classes")
        vanilla = Path(args[i + 1]).expanduser()
        del args[i : i + 2]
    directory = Path(args[0]) if args else ROOT / "scripts/tandem"
    funcs, classes, enums = parse(directory)
    hooks = [f for f in funcs if f.hook in ("wrapMethod", "replaceMethod")]
    if vanilla:
        return write_classes(vanilla, {f.owner for f in hooks})

    ancestry = load_classes()
    analysis = Analysis(funcs, classes, enums, ancestry)
    known = load_known()
    problems, counts = 0, {"ok": 0, "said": 0, "known": 0}

    def show(f: Func) -> str:
        try:
            return f"{f.path.relative_to(ROOT)}:{f.line}"
        except ValueError:
            return f"{f.path}:{f.line}"

    targets = []
    for f in funcs:
        if f.hook in ("wrapMethod", "replaceMethod"):
            chain = ancestry.get(f.owner)
            if chain is None:
                print(f"{show(f)}: {f.label}: class {f.owner} is not in {CLASSES.relative_to(ROOT)} "
                      f"(tools/scripts-threads.py --classes VANILLA.reds, or add its ancestry by hand)",
                      file=sys.stderr)
                problems += 1
                continue
            root = next((name for name in chain if name in PER_ENTITY_ROOTS), None)
            if root:
                targets.append((f, f"{f.owner} is {PER_ENTITY_ROOTS[root]}"))
            elif f.cb:
                targets.append((f, "a cb func, called by the engine"))
            elif LIFECYCLE.match(f.name):
                targets.append((f, "a lifecycle method"))
        elif f.cb and f.owner and not f.hook:
            targets.append((f, "a cb func, called by the engine"))

    for f, why in targets:
        opt = OPT_OUT.search(f.comment)
        verdict = "ok"
        if opt:
            verdict = f"{opt.group(1)}: {opt.group(2)}"
            counts["said"] += 1
        elif f.reason is None:
            counts["ok"] += 1
        elif f.key in known:
            known[f.key] = True
            verdict = "KNOWN, neither proven nor reworked"
            counts["known"] += 1
        else:
            problems += 1
            verdict = "CHANGES STATE"
            steps = "; ".join(f"{g.label} {what} ({g.path.name}:{line})" for g, line, what in f.reason)
            print(f"{show(f)}: {f.label} may run on a job thread ({why}) and changes script state: {steps}. "
                  f"Queue it and act on the tick, or say why not above it: '// game thread only: <how it was "
                  f"proven>' or '// job thread safe: <why>'.", file=sys.stderr)
        if f.key in known and (opt or f.reason is None):
            known[f.key] = "stale"
        if listing and everything and f.reason is not None and not opt:
            print(f"{show(f)}: {f.label} [{why}]: {verdict}")
            for g, line, what in analysis.reach(f):
                print(f"    {g.label} {what} ({g.path.name}:{line})")
        elif listing:
            reason = ""
            if f.reason is not None and not opt:
                g, line, what = f.reason[-1]
                reason = f" ({g.label} {what}, {g.path.name}:{line})"
            print(f"{show(f)}: {f.label} [{why}]: {verdict}{reason}")
    for key, used in known.items():
        if used is not True:
            print(f"{KNOWN.relative_to(ROOT)}: '{key}' no longer needs to be listed "
                  f"({'it is clean or says why' if used == 'stale' else 'no such hook'}): remove the line",
                  file=sys.stderr)
            problems += 1
    summary = (f"{len(targets)} hooks checked: {counts['ok']} clean, {counts['said']} say why, "
               f"{counts['known']} known and unproven ({KNOWN.name})")
    if problems:
        print(f"scripts-threads: {problems} problem(s); {summary}", file=sys.stderr)
        return 1
    if listing:
        print(f"scripts-threads: ok, {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
