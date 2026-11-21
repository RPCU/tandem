#!/usr/bin/env python3
"""Check that every log pattern a scenario waits for can match a line the
game (or the mock player) can write, without the game.

A scenario (tools/scenarios/*.sh) reads the game's log with `expect 'RE'`,
`reject`, `at_most N`, `since | grep` and `query` (which waits for `test
query: WHAT ARGS = `), and the mock player's with
`expect_peer`, `expect_peer2` and `reject_peer`. A log line reworded in the
scripts or the plugin leaves its scenarios waiting for a line that never
comes, found only minutes into an in-game run.

The lines each side can write are collected as formats: literal text around
wildcards.
- Game: scripts/tandem/*.reds, behind "[script] ": every string expression
  (s"..." interpolations, "a" + x chains) and what Log*/Once calls are given;
  plugin/src and core/ C++: every std::format-style call (Log, log::Info...,
  std::format) and every other literal; the co-op notices, "coop notice:
  <English text of each Text.reds key> [<key>]", as TandemText.Say logs them.
- Mock player: peer/ (with its simulator) and core/ C++, the same way.
A value is followed where it is plain to see: a ternary between texts is one
of them; a local or field one of the texts assigned to it in its file (`+=`
and ArrayPush repeated); a parameter one of what its callers give it; a call
to one of our functions one of the texts it returns; an index into a list of
texts one of them; a Text.reds key's text one of the English texts; a {} of
std::format its argument. The scripts print a Float as 60.000000, an Int as
digits, a Bool as true or false (types from the declarations); C++'s {:.1f}
is digits with one decimal, {:x} hexadecimal.
Any other value is a wildcard of at most three words, none ending with a
colon (a name, a number, a record, not a sentence of the pattern); a line's
last one any number of words (a reason, a text).

A pattern passes when some format can produce a line in which it finds a
match: both become automata (the pattern through Python's regex parser, read
as grep -E reads it) and their product is searched. The match starts in the
format's own text, or, for a pattern with no ": ", at a wildcard that then
gives it one word ("tandem-peer left the session"): a value does not hold
the words a pattern begins with. Only formats with a quarter of the
pattern's literal trigrams are tried. A shell variable in a pattern ($x)
matches anything. A pattern that matches no format is reported with the
formats nearest to it; one that grep -E reads otherwise (a \\d) is too.

Usage: tools/scenarios-patterns.py [-v] [SCENARIO.sh...]   (exit 1 on a finding)
"""

import re
import sys
from collections import Counter
from pathlib import Path

try:
    import re._constants as C
    import re._parser as P
except ImportError:  # Python < 3.11
    import sre_constants as C
    import sre_parse as P

ROOT = Path(__file__).resolve().parent.parent
VAR = "\ue000"  # a shell expansion inside a pattern
OTHER = "\uffff"  # every character no format or pattern names

# Wildcards, as regexes. A value: up to three words, none ending with a
# colon (so no ": " inside).
WORD = r"[^ \n]*[^ \n:]"  # a word that does not end with a colon
# (The only capturing group of a format: its automaton knows the wildcard.)
GENERIC = r"((?:" + WORD + r"(?: " + WORD + r"){0,2})?)"
# A line's last value may be a sentence (a reason, a text): any number of words.
TAIL = r"((?:" + WORD + r"(?: " + WORD + r")*)?)"
INT = r"-?[0-9]+"
FLOAT = r"-?[0-9]+\.[0-9]{6}"
BOOL = r"(?:true|false)"
HEX = r"(?:0x)?[0-9a-fA-F]+"
LONGEST = 40000  # a value's regex beyond this is a plain wildcard
INT_TYPES = {"Int8", "Int16", "Int32", "Int64", "Uint8", "Uint16", "Uint32", "Uint64"}
# Vanilla and Codeware functions whose result is a number.
NUMERIC = {
    "ArraySize": INT, "EnumInt": INT, "StrLen": INT, "RoundF": INT, "FloorF": INT, "CeilF": INT,
    "RandRange": INT, "Clamp": INT, "Min": INT, "Max": INT, "StringToInt": INT,
    "RandRangeF": FLOAT, "AbsF": FLOAT, "MinF": FLOAT, "MaxF": FLOAT, "ClampF": FLOAT, "SqrtF": FLOAT,
    "Distance": FLOAT, "Distance2D": FLOAT, "Length": FLOAT, "ToFloat": FLOAT, "GetFloat": FLOAT,
    "StringToFloat": FLOAT,
}
TEXT_CALLS = {"English", "Get", "F", "FE", "T"}  # TandemText's: a key's text
CPP_INTS = {"int", "unsigned", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t",
            "int32_t", "int64_t", "size_t", "long"}
C_FORMATS = {"Log", "Info", "Warn", "Debug", "Always", "Error", "format", "println"}
R_LOGS = re.compile(r"Log\w*|Once")
NOT_FUNCS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "else", "do"}


class Val:
    """A string value: its regex and how it reads (… for what varies)."""

    __slots__ = ("regex", "text", "plain", "letters")

    def __init__(self, regex, text, plain=False, letters=0):
        self.regex, self.text, self.plain = regex, text, plain
        self.letters = letters  # of its own text, whatever it is

    @staticmethod
    def lit(text):
        return Val(re.escape(text), text, True, sum(c.isalpha() for c in text))


WILD = Val(GENERIC, "…")


def typed(regex):
    return Val(regex, "…")


def concat(vals):
    vals = list(vals)
    return Val("".join(v.regex for v in vals), "".join(v.text for v in vals), all(v.plain for v in vals),
               sum(v.letters for v in vals))


def either(vals):
    seen, out = set(), []
    for v in vals:
        if v.regex not in seen:
            seen.add(v.regex)
            out.append(v)
    if len(out) <= 1:
        return out[0] if out else WILD
    regex = "(?:" + "|".join(v.regex for v in out) + ")"
    if len(regex) > LONGEST:
        return WILD
    short = all(v.plain and len(v.text) <= 30 for v in out) and len(out) <= 4
    return Val(regex, "(" + "|".join(v.text for v in out) + ")" if short else "…", False, min(v.letters for v in out))


# ---------------------------------------------------------------- sources
def tokens(text, reds):
    """(kind, value, line): strings, names, words, numbers, operators."""
    out, i, n, line = [], 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif c == '"' or (reds and c in "sntr" and text.startswith('"', i + 1) and not (i and (text[i - 1].isalnum() or text[i - 1] == "_"))):
            prefix = "" if c == '"' else c
            parts, j = string(text, i + len(prefix), reds and prefix == "s")
            out.append(("str" if prefix in ("", "s") else "name", parts, line))
            line += text.count("\n", i, j)
            i = j
        elif not reds and text.startswith('R"', i):
            end = text.find("(", i)
            delim = ")" + text[i + 2:end] + '"'
            close = text.find(delim, end)
            out.append(("str", [text[end + 1:close]], line))
            line += text.count("\n", i, close)
            i = close + len(delim)
        elif not reds and c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            out.append(("num", text[i:j + 1], line))
            i = j + 1
        elif c.isalnum() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_" or (c.isdigit() and (text[j] == "." or (not reds and text[j] == "'")))):
                j += 1
            out.append(("num" if c.isdigit() else "word", text[i:j], line))
            i = j
        elif c.isspace():
            i += 1
        else:
            op = text[i:i + 2]
            if op in ("+=", "<<", "==", "!=", "&&", "||", "->", "::", "<=", ">=", "-="):
                out.append(("op", op, line))
                i += 2
            else:
                out.append(("op", c, line))
                i += 1
    return out


def string(text, i, interpolated):
    """The parts of the string literal at text[i] == '"' (interpolations as
    ("expr", text)), and the index after it."""
    parts, buf, i = [], [], i + 1
    while i < len(text) and text[i] != '"':
        if text[i] == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            if interpolated and nxt == "(":
                depth, j = 1, i + 2
                while depth and j < len(text):
                    if text[j] == '"':
                        _, j = string(text, j, True)
                        continue
                    depth += {"(": 1, ")": -1}.get(text[j], 0)
                    j += 1
                parts.append("".join(buf))
                buf = []
                parts.append(("expr", text[i + 2:j - 1]))
                i = j
                continue
            buf.append({"n": "\n", "t": "\t"}.get(nxt, nxt))
            i += 2
            continue
        if text[i] == "\n":
            break
        buf.append(text[i])
        i += 1
    parts.append("".join(buf))
    return parts, i + 1


def matching(toks, i):
    """The index of the bracket closing the one at toks[i]."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    opening, closing, depth = toks[i][1], pairs[toks[i][1]], 0
    for j in range(i, len(toks)):
        if toks[j][0] == "op":
            if toks[j][1] == opening:
                depth += 1
            elif toks[j][1] == closing:
                depth -= 1
                if not depth:
                    return j
    return len(toks) - 1


def opening(toks, close):
    """The index of the bracket opening the one at toks[close]."""
    pairs = {")": "(", "]": "[", "}": "{"}
    depth = 0
    for j in range(close, -1, -1):
        if toks[j][0] != "op":
            continue
        if toks[j][1] == toks[close][1]:
            depth += 1
        elif toks[j][1] == pairs[toks[close][1]]:
            depth -= 1
            if not depth:
                return j
    return -1


def split_top(toks, seps):
    """toks split at the top level on the operators seps."""
    out, depth, start = [], 0, 0
    for k, (kind, value, _) in enumerate(toks):
        if kind != "op":
            continue
        if value in "([{":
            depth += 1
        elif value in ")]}":
            depth -= 1
        elif depth == 0 and value in seps:
            out.append(toks[start:k])
            start = k + 1
    out.append(toks[start:])
    return out


def comparisons(toks):
    """toks with the brackets of Cast<T> and array<T> out of the way."""
    out, k = [], 0
    while k < len(toks):
        if toks[k][1] == "<" and k + 2 < len(toks) and toks[k + 1][0] == "word" and toks[k + 2][1] == ">":
            out.append(("word", toks[k + 1][1], toks[k][2]))
            k += 3
            continue
        out.append(toks[k])
        k += 1
    return out


def expression_end(toks, i, stops=(";",)):
    """The end of the expression starting at toks[i]."""
    depth = 0
    for j in range(i, len(toks)):
        kind, value, _ = toks[j]
        if kind != "op":
            continue
        if value in "([{":
            depth += 1
        elif value in ")]}":
            if depth == 0:
                return j
            depth -= 1
        elif depth == 0 and value in stops:
            return j
    return len(toks)


class World:
    """What all the sources say: types, the texts our functions return."""

    def __init__(self):
        self.names, self.funcs, self.fields = {}, {}, {}
        self.returns = {}  # (scripts?, function name) -> [(source, tokens)]
        self.calls = {}  # (scripts?, function name) -> [(source, arguments)]
        self.texts = WILD

    def declare(self, text, names=None):
        """The types text declares (into names, else the world's)."""
        names = self.names if names is None else names
        text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
        for name, kind, inner in re.findall(r"\b(\w+)\s*:\s*(array<\s*(\w+)\s*>|\w+)", text):
            names.setdefault(name, set()).add(("[]" + inner) if inner else kind)
        for name, kind in re.findall(r"\blet\s+(\w+)\s*=\s*(-?[0-9]+\.[0-9]+|-?[0-9]+)\s*;", text):
            names.setdefault(name, set()).add("Float" if "." in kind else "Int32")
        if names is not self.names:
            return
        for name, kind in re.findall(r"\blet\s+(\w+)\s*:\s*(\w+)\s*;", text):
            self.fields.setdefault(name, set()).add(kind)
        for name, kind in re.findall(r"\bfunc\s+(\w+)\s*\([^)]*\)\s*->\s*(\w+)", text, re.S):
            self.funcs.setdefault(name, set()).add(kind)

    @staticmethod
    def regex(kind):
        if kind in ("Float", "Double"):
            return FLOAT
        if kind in INT_TYPES:
            return INT
        if kind == "Bool":
            return BOOL
        return None

    def typed_name(self, name, indexed=False, local=None, field=False):
        kinds = (field and self.fields.get(name)) or (local or {}).get(name) or self.names.get(name, set())
        if len(kinds) != 1:
            return None
        kind = next(iter(kinds))
        if indexed:
            kind = kind[2:] if kind.startswith("[]") else ""
        return self.regex(kind)


class Source:
    """One source file: its tokens, what its names are given, its lines."""

    def __init__(self, path, text, reds, world):
        self.path, self.reds, self.world = path, reds, world
        self.toks = tokens(text, reds)
        self.vars = {}  # name -> [(op, tokens)]
        self.params = {}  # name -> {(function, index)}
        self.memo, self.busy = {}, set()
        self.names = {}
        if reds:
            world.declare(text, self.names)
        self.scan()

    def where(self, line):
        return f"{self.path}:{line}"

    def scan(self):
        toks, stack = self.toks, []
        for k, (kind, value, _) in enumerate(toks):
            if kind == "op" and value == "{":
                stack.append(self.function_at(k))
            elif kind == "op" and value == "}" and stack:
                stack.pop()
            elif kind == "word" and value == "return":
                owner = next((name for name in reversed(stack) if name), None)
                end = expression_end(toks, k + 1)
                if owner and end > k + 1:
                    self.world.returns.setdefault((self.reds, owner), []).append((self, toks[k + 1:end]))
            elif kind == "word" and k + 1 < len(toks):
                if toks[k + 1][1] == "(" and value not in NOT_FUNCS and not (self.reds and k and toks[k - 1][1] == "func"):
                    close = matching(toks, k + 1)
                    args = split_top(toks[k + 2:close], [","])
                    self.world.calls.setdefault((self.reds, value), []).append((self, args))
                    if self.reds and value == "ArrayPush" and len(args) == 2 and len(args[0]) == 1:
                        self.vars.setdefault(args[0][0][1], []).append(("push", args[1]))
                if not self.reds and k and toks[k - 1][1] in (".", "->"):
                    continue  # a member, or a designated initializer
                self.assignment(k)

    def function_at(self, k):
        """The name of the function whose body opens at toks[k], if one does."""
        toks, j = self.toks, k - 1
        while j >= 0 and (toks[j][0] == "word" or toks[j][1] in ("->", "::", "<", ">", "&", "*", ",")) and toks[j][1] not in NOT_FUNCS:
            j -= 1
        if j < 0 or toks[j][1] != ")":
            if j >= 0 and toks[j][1] == "]" and not self.reds:  # [&] { ... }
                return self.lambda_name(j)
            return None
        j = opening(toks, j)
        if j <= 0:
            return None
        start = j
        before = toks[j - 1]
        if before[1] == "]" and not self.reds:
            return self.lambda_name(j - 1)
        if self.reds and not (j >= 2 and toks[j - 2][1] == "func"):
            return None  # if Equals(a, b) {
        if before[0] == "word" and before[1] not in NOT_FUNCS:
            follows = (":",) if self.reds else (",", ")", "=")
            close = matching(toks, start)
            header = [a for a in split_top(toks[start + 1:close], [","]) if a]
            for index, arg in enumerate(header):
                names = [t[1] for t, nxt in zip(arg, arg[1:] + [("op", ")", 0)]) if t[0] == "word" and nxt[1] in follows]
                if names:
                    self.params.setdefault(names[0], set()).add((before[1], index))
            return before[1]
        return None

    def lambda_name(self, j):
        toks = self.toks
        j = opening(toks, j)
        if j >= 2 and toks[j - 1][1] == "=" and toks[j - 2][0] == "word":
            return toks[j - 2][1]
        return None

    def assignment(self, k):
        toks = self.toks
        name, j = toks[k][1], k + 1
        if self.reds and toks[j][1] == ":" and k and toks[k - 1][1] == "let":
            while j < len(toks) and toks[j][1] not in ("=", ";"):
                j += 1
        elif not self.reds and toks[j][1] == "[" and k and toks[k - 1][0] in ("word", "op"):
            j = matching(toks, j) + 1
        if j >= len(toks):
            return
        op = toks[j][1]
        if toks[j][0] == "op" and op in ("=", "+="):
            start = j + 1
            if start < len(toks) and toks[start][1] == "[" and not self.reds:
                return  # a lambda: its returns are seen
            end = expression_end(toks, start)
            self.vars.setdefault(name, []).append((op, toks[start:end]))
        elif self.reds and k and toks[k - 1][1] == "for" and op == "in":
            end = expression_end(toks, j + 1, ("{",))
            self.vars.setdefault(name, []).append(("in", toks[j + 1:end]))

    # ---- values
    def value(self, toks, depth=0):
        """What the expression toks can be, as text."""
        while len(toks) >= 2 and toks[0][1] == "(" and matching(toks, 0) == len(toks) - 1:
            toks = toks[1:-1]
        if not toks or depth > 8:
            return WILD
        if len(split_top(toks, ["?"])) > 1:
            branches = self.ternary(toks)
            if branches:
                return either([self.value(b, depth + 1) for b in branches])
        if len(split_top(toks, ["==", "!=", "<=", ">=", "&&", "||"])) > 1 or toks[0][1] == "!":
            return typed(BOOL)
        if self.reds and len(split_top(comparisons(toks), ["<", ">"])) > 1:
            return typed(BOOL)
        pieces = split_top(toks, ["+"])
        if len(pieces) > 1 and all(pieces):
            vals = [self.value(p, depth + 1) for p in pieces]
            if any(any(t[0] == "str" for t in p) or " " in v.text or v.plain for p, v in zip(pieces, vals)):
                return concat(vals)
            return self.numeric(vals)
        pieces = split_top(toks, ["-", "*", "/", "%"])
        if len(pieces) > 1 and all(pieces):
            return self.numeric([self.value(p, depth + 1) for p in pieces])
        if all(t[0] == "str" for t in toks):
            return concat(self.literal(t[1], depth) for t in toks)
        if len(toks) == 1 and toks[0][0] == "num":
            return typed(FLOAT if "." in toks[0][1] and self.reds else INT)
        last = toks[-1]
        if last[1] == ")":
            at = opening(toks, len(toks) - 1)
            if at > 0:
                return self.call(toks, at, depth)
            return WILD
        if last[1] == "]":
            at = opening(toks, len(toks) - 1)
            if at > 0 and toks[at - 1][0] == "word":
                name = toks[at - 1][1]
                if name in self.vars or name in self.params:
                    return self.var(name, depth)
                if self.reds and (r := self.world.typed_name(name, indexed=True, local=self.names)):
                    return typed(r)
                return WILD
            if at == 0 and self.reds:  # a list of texts
                return either([self.value(p, depth + 1) for p in split_top(toks[1:-1], [","]) if p])
            return WILD
        if last[1] == "}" and toks[0][1] == "{" and not self.reds:
            return either([self.value(p, depth + 1) for p in split_top(toks[1:-1], [","]) if p])
        if last[0] == "word" and all(t[0] == "word" or t[1] in (".", "->", "::", "&", "*") for t in toks):
            name = last[1]
            # A local, a parameter or this.field; another object's field: by type.
            plain = len(toks) == 1 or (self.reds and toks[0][1] == "this" and len(toks) == 3)
            if plain and (name in self.vars or name in self.params):
                return self.var(name, depth)
            if self.reds and (r := self.world.typed_name(name, local=self.names, field=len(toks) > 1)):
                return typed(r)
        return WILD

    def ternary(self, toks):
        """(then, else) of the ternary toks is, if it is one."""
        depth, seen, nested = 0, False, 0
        for k, (kind, value, _) in enumerate(toks):
            if kind != "op":
                continue
            if value in "([{":
                depth += 1
            elif value in ")]}":
                depth -= 1
            elif depth == 0 and value == "?":
                if not seen:
                    seen, cond = True, k
                else:
                    nested += 1
            elif depth == 0 and value == ":" and seen:
                if nested:
                    nested -= 1
                else:
                    return toks[cond + 1:k], toks[k + 1:]
        return None

    @staticmethod
    def numeric(vals):
        if any(v.regex == FLOAT for v in vals):
            return typed(FLOAT)
        if all(v.regex == INT for v in vals):
            return typed(INT)
        return WILD

    def literal(self, parts, depth):
        if not self.reds:
            return Val.lit(parts[0])
        vals = []
        for p in parts:
            if isinstance(p, tuple):
                vals.append(self.value(tokens(p[1], True), depth + 1))
            elif p:
                vals.append(Val.lit(p))
        return concat(vals) if vals else Val.lit("")

    def call(self, toks, at, depth):
        name = toks[at - 1][1]
        args = [a for a in split_top(toks[at + 1:-1], [","]) if a]
        if self.reds:
            if name in TEXT_CALLS and at >= 3 and toks[at - 3][1] == "TandemText":
                return self.world.texts
            if name == ">" and at >= 2:  # Cast<Float>(x)
                return typed(self.world.regex(toks[at - 2][1]) or GENERIC)
            if name in NUMERIC:
                return typed(NUMERIC[name])
            if name == "StrSplit" and args:  # its lines, read as one
                return self.value(args[0], depth + 1)
        else:
            if name == "format" and args:
                return self.format_call(args, depth)
            if name in ("string", "string_view") and len(args) == 1:
                return self.value(args[0], depth + 1)
            if name in ("to_string", "size", "count", "ssize"):
                return typed(INT)
            if name == ">" and at >= 2 and toks[at - 2][1] in CPP_INTS:  # static_cast<int>(x)
                return typed(INT)
        returns = self.world.returns.get((self.reds, name))
        if returns and depth < 6:
            vals = []
            for source, ret in returns[:24]:
                key = (name, id(ret))
                if key in source.busy:
                    vals.append(WILD)
                    continue
                source.busy.add(key)
                vals.append(source.value(ret, depth + 2))
                source.busy.discard(key)
            if len(returns) > 24:
                vals.append(WILD)
            return either(vals)
        kinds = self.world.funcs.get(name, set())
        if self.reds and len(kinds) == 1 and (r := self.world.regex(next(iter(kinds)))):
            return typed(r)
        return WILD

    def var(self, name, depth):
        """What a local or field can be: one of its texts, then its += ones."""
        if name in self.memo:
            return self.memo[name]
        if name in self.busy or depth > 6:
            return WILD
        self.busy.add(name)
        first, more = [], []
        for op, expr in self.vars.get(name, ()):
            (more if op == "+=" else first).append(self.value(expr, depth + 1))
        for function, index in sorted(self.params.get(name, ())):
            calls = self.world.calls.get((self.reds, function), [])
            for source, args in calls[:30]:
                if index < len(args) and args[index]:
                    first.append(source.value(args[index], depth + 2))
        self.busy.discard(name)
        numbers = {INT, FLOAT}
        if first + more and all(v.regex in numbers for v in first + more):
            return self.memo.setdefault(name, self.numeric(first + more))
        # Texts are repeated; a wildcard twice at most (repeated, any line).
        texts = [v for v in more if v.letters >= 2]
        values = [v for v in more if v not in texts]
        # (A C++ name's declarations without "=": a parameter, a binding.)
        if name in self.params or not first or not self.reds:
            first.append(WILD)
        val = either(first)
        if texts:
            val = Val(val.regex + "(?:" + either(texts).regex + ")*", val.text + "…")
        if values:
            val = Val(val.regex + "(?:" + either(values).regex + "){0,2}", val.text + "…")
        if len(val.regex) > LONGEST:
            val = WILD
        self.memo[name] = val
        return val

    def format_call(self, args, depth):
        """std::format(...)'s line: its {} filled with what it is given."""
        if not all(t[0] == "str" for t in args[0]):
            return WILD
        text = "".join(t[1][0] for t in args[0])
        vals, last, n = [], 0, 1
        for m in re.finditer(r"\{\{|\}\}|\{([0-9]*)(?::([^{}]*))?\}", text):
            vals.append(Val.lit(text[last:m.start()]))
            last = m.end()
            if m.group(0) in ("{{", "}}"):
                vals.append(Val.lit(m.group(0)[0]))
                continue
            index = int(m.group(1)) + 1 if m.group(1) else n
            n += 1
            arg = self.value(args[index], depth + 1) if index < len(args) else WILD
            vals.append(placeholder(m.group(2) or "", arg))
        vals.append(Val.lit(text[last:]))
        return concat(vals)

    # ---- formats
    def formats(self, out):
        """(value, where) of every line this file can log."""
        toks, used = self.toks, set()
        for k, (kind, value, line) in enumerate(toks):
            if kind == "op" and value == "(" and k and toks[k - 1][0] == "word" and k not in used:
                name = toks[k - 1][1]
                close = matching(toks, k)
                args = [a for a in split_top(toks[k + 1:close], [","]) if a]
                if args and not self.reds and name in C_FORMATS and all(t[0] == "str" for t in args[0]):
                    out.append((self.format_call(args, 0), self.where(line)))
                    used.update(range(k, close + 1))
                elif args and self.reds and R_LOGS.fullmatch(name):
                    for arg in args:
                        if not any(t[0] == "str" for t in arg):
                            out.append((self.value(arg), self.where(line)))
            if kind != "str" or k in used:
                continue
            # A string expression: the whole sum it is in.
            start = k
            while start >= 2 and toks[start - 1][1] == "+":
                start = self.operand_start(start - 2)
            end = k + 1
            while end < len(toks):
                if toks[end][1] == "+":
                    end = expression_end(toks, end + 1, (",", ";", "?", ":", "+", "==", "!=", "&&", "||", "+="))
                elif toks[end][0] == "str" and not self.reds:
                    end += 1
                else:
                    break
            used.update(range(start, end))
            val = self.value(toks[start:end])
            if start and toks[start - 1][1] == "+=":
                val = concat([WILD, val])
            out.append((val, self.where(line)))

    def operand_start(self, j):
        """The first token of the operand that ends at toks[j]."""
        toks = self.toks
        if toks[j][1] in (")", "]"):
            j = max(opening(toks, j), 0)
        while j > 0 and (toks[j - 1][0] == "word" or toks[j - 1][1] in (".", "::", "->")):
            j -= 1
            if toks[j][1] in (")", "]"):
                j = max(opening(toks, j), 0)
        return j


def placeholder(spec, arg):
    """A std::format {:spec} given arg."""
    if arg is not WILD:
        return arg
    if f := re.search(r"\.([0-9]+)[fFeg]?$", spec):
        digits = int(f.group(1))
        return typed(r" *-?[0-9]+" + (r"\.[0-9]{%d}" % digits if digits else ""))
    if spec.endswith(("x", "X")):
        return typed(" *" + HEX)
    if spec.endswith("d"):
        return typed(" *" + INT)
    return WILD


def worth(val):
    """A format with a few letters of its own (not "{}: {}")."""
    return len(re.findall(r"[A-Za-z]", val.text.replace("…", ""))) >= 3


class Format:
    __slots__ = ("regex", "text", "where", "grams", "shown", "nfa")

    def __init__(self, val, where):
        self.regex, self.text, self.where = val.regex, val.text, where
        if self.regex.endswith(GENERIC):
            self.regex = self.regex[: -len(GENERIC)] + TAIL
        # Trigrams of all its literal text, and of the text it shows.
        self.grams, self.shown = set(), set()
        for piece in literal_pieces(self.regex):
            self.grams |= grams(piece)
        for piece in val.text.split("…"):
            self.shown |= grams(piece)
        self.nfa = None


def literal_pieces(regex):
    """The runs of literal text in one of our regexes (alternatives too)."""
    s = re.sub(r"(?<!\\)\[(?:\\.|[^\]])*\]|(?<!\\)\{[0-9]*,?[0-9]*\}|\(\?:", "\x01", regex)
    return [re.sub(r"\\(.)", r"\1", piece) for piece in re.findall(r"(?:\\.|[^\\()|*+?.^$\x01])+", s)]


def grams(text):
    return {text[i:i + 3] for i in range(len(text) - 2)}


def lines_of(val):
    """A value split into log lines (a usage text, a report of several)."""
    if "\\\n" not in val.regex:
        return [val]
    if "(?:" in val.regex:  # a newline inside alternatives: read as a space
        return [Val(val.regex.replace("\\\n", " "), val.text.replace("\n", " "))]
    texts, regexes = val.text.split("\n"), val.regex.split("\\\n")
    if len(texts) != len(regexes):
        return [Val(r, "…") for r in regexes]
    return [Val(r, t) for r, t in zip(regexes, texts)]


def collect():
    """(game formats, mock-player formats)."""
    world = World()
    reds = {p: p.read_text() for p in sorted((ROOT / "scripts/tandem").rglob("*.reds"))}
    for text in reds.values():
        world.declare(text)
    sources = [Source(p.relative_to(ROOT), t, True, world) for p, t in reds.items()]
    cpp = []
    for folder in ("plugin/src", "core/src", "core/include", "peer"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix in (".cpp", ".hpp", ".h"):
                cpp.append(Source(path.relative_to(ROOT), path.read_text(errors="replace"), False, world))
    # The English texts, for a key's text in a line.
    english = reds[ROOT / "scripts/tandem/Text.reds"].split("class TandemTextEnglish", 1)[1].split("\npublic class ", 1)[0]
    texts = []
    for m in re.finditer(r'this\.Text\("Tandem-([^"]+)",\s*"((?:[^"\\]|\\.)*)"\)', english):
        key, body = m.group(1), m.group(2).replace('\\"', '"')
        pieces = [WILD if piece in ("{1}", "{2}") else Val.lit(piece) for piece in re.split(r"(\{[12]\})", body) if piece]
        texts.append((key, concat(pieces)))
    world.texts = Val("(?:" + "|".join(v.regex for _, v in texts) + ")", "…")
    game, peer = [], []
    for source in sources:
        found = []
        source.formats(found)
        for val, where in found:
            for one in lines_of(val):
                if one.text.startswith("coop notice: …") or not worth(one):
                    continue  # TandemText.Say: one format per key below
                game.append(Format(concat([Val.lit("[script] "), one]), where))
    for key, val in texts:
        for shown in ("", " (not shown: notices)"):
            game.append(Format(concat([Val.lit("[script] coop notice: "), val, Val.lit(f" [{key}]{shown}")]), f"scripts/tandem/Text.reds ({key})"))
    game.append(Format(concat([Val.lit("[script] coop notice: "), WILD, Val.lit(" []")]), "scripts/tandem/Text.reds (no key)"))
    for source in cpp:
        found = []
        source.formats(found)
        top = source.path.parts[0]
        for val, where in found:
            for one in lines_of(val):
                if one.text.startswith("[script] …") or not worth(one):
                    continue  # the scripts' own lines, collected above
                if top in ("plugin", "core"):
                    game.append(Format(one, where))
                if top in ("peer", "core"):
                    peer.append(Format(one, where))
    return game, peer


# ---------------------------------------------------------------- automata
class Alphabet:
    def __init__(self, chars):
        self.chars = sorted(set(chars) | {chr(c) for c in range(32, 127)} | {"\t", OTHER})
        self.index = {c: i for i, c in enumerate(self.chars)}
        self.all = (1 << len(self.chars)) - 1
        self.sets = {}  # charsets already made

    def bit(self, c):
        return 1 << self.index.get(c, self.index[OTHER])

    def where(self, test):
        mask = 0
        for i, c in enumerate(self.chars):
            if test(c):
                mask |= 1 << i
        return mask


class NFA:
    """Thompson's automaton of a parsed regex, over the alphabet's bits."""

    def __init__(self, alphabet):
        self.a = alphabet
        self.edges, self.eps = [], []
        self.ignore_case = False
        self.wild, self.inside = 0, set()  # inside a format's wildcard
        self.start = self.node()
        self.accept = None

    def node(self):
        self.edges.append([])
        self.eps.append([])
        if self.wild:
            self.inside.add(len(self.edges) - 1)
        return len(self.edges) - 1

    def char(self, code):
        c = chr(code)
        if self.ignore_case:
            return self.a.bit(c.lower()) | self.a.bit(c.upper())
        return self.a.bit(c)

    def category(self, cat):
        name = str(cat)
        tests = {"DIGIT": str.isdigit, "SPACE": str.isspace, "WORD": lambda c: c.isalnum() or c == "_"}
        for key, test in tests.items():
            if name.endswith("CATEGORY_NOT_" + key):
                return self.a.all & ~self.a.where(test)
            if name.endswith("CATEGORY_" + key):
                return self.a.where(test)
        return self.a.all

    def charset(self, items):
        key = (tuple(items), self.ignore_case)
        if key not in self.a.sets:
            self.a.sets[key] = self.charset_of(items)
        return self.a.sets[key]

    def charset_of(self, items):
        mask, negate = 0, False
        for op, av in items:
            if op is C.NEGATE:
                negate = True
            elif op is C.LITERAL:
                mask |= self.char(av)
            elif op is C.RANGE:
                mask |= self.a.where(lambda c: av[0] <= ord(c) <= av[1] or (c == OTHER and av[1] > 127))
            elif op is C.CATEGORY:
                mask |= self.category(av)
            elif op is C.NOT_LITERAL:
                mask |= self.a.all & ~self.char(av)
        return self.a.all & ~mask if negate else mask

    def build(self, items, cur):
        for op, av in items:
            cur = self.one(op, av, cur)
        return cur

    def edge(self, cur, mask):
        nxt = self.node()
        self.edges[cur].append((mask, nxt, self.wild > 0))
        return nxt

    def one(self, op, av, cur):
        if op is C.LITERAL:
            return self.edge(cur, self.char(av))
        if op is C.NOT_LITERAL:
            return self.edge(cur, self.a.all & ~self.char(av))
        if op is C.ANY:
            return self.edge(cur, self.a.all)
        if op is C.IN:
            return self.edge(cur, self.charset(av))
        if op is C.BRANCH:
            end = self.node()
            for seq in av[1]:
                start = self.node()
                self.eps[cur].append(start)
                self.eps[self.build(seq, start)].append(end)
            return end
        if op is C.SUBPATTERN:
            if av[0] is None:
                return self.build(av[-1], cur)
            self.wild += 1  # a wildcard of the format
            end = self.build(av[-1], cur)
            self.wild -= 1
            self.inside.discard(end)
            return end
        if op in (C.MAX_REPEAT, C.MIN_REPEAT) or str(op) == "POSSESSIVE_REPEAT":
            low, high, sub = av
            for _ in range(min(low, 40)):
                cur = self.build(sub, cur)
            if high == C.MAXREPEAT or high - low > 40:
                loop = self.node()
                self.eps[cur].append(loop)
                self.eps[self.build(sub, loop)].append(loop)
                return loop
            for _ in range(high - low):
                # A fresh end: skipping the copy must not land inside it.
                end = self.node()
                self.eps[self.build(sub, cur)].append(end)
                self.eps[cur].append(end)
                cur = end
            return cur
        if str(op) == "ATOMIC_GROUP":
            return self.build(av, cur)
        if op is C.GROUPREF:
            loop = self.node()
            self.eps[cur].append(loop)
            self.edges[loop].append((self.a.all, loop, False))
            return loop
        # Anchors, lookarounds: read loosely (never a reason to fail).
        return cur


def compile_regex(regex, alphabet, search):
    parsed = P.parse(regex)
    nfa = NFA(alphabet)
    nfa.ignore_case = bool(parsed.state.flags & re.IGNORECASE)
    first = nfa.start
    if search:  # found anywhere in the line: from first on
        nfa.edges[nfa.start].append((alphabet.all, nfa.start, False))
        first = nfa.node()
    nfa.first = first
    nfa.accept = nfa.build(parsed, first)
    return nfa


def intersect(fmt, pat, first_word):
    """Can the format produce a line in which the pattern finds a match?

    The match starts in the format's own text, or where a wildcard starts
    when the pattern has no ": " (a name: "tandem-peer left the session"),
    the wildcard then giving it one word (first_word: what that word may
    hold). A value does not hold the words that begin a pattern ("street
    count" before a format's ": ")."""
    # The pattern's states as bits, on each state of the format: those on
    # its first word (one[]) and the others (on[]), grown to a fixpoint.
    out, accept, opens = pattern_steps(pat)
    fe, fx, inside = fmt.edges, fmt.eps, fmt.inside
    count = len(fe)
    begin = out[-1]
    if begin & accept:
        return True
    # Only where the pattern's first character can come next.
    leads = format_leads(fmt)
    on, one = [0] * count, [0] * count
    work = []
    for a in range(count):
        if leads[a] & opens and a not in inside:
            one[a] = begin
            work.append(a)

    def step(states, mask):
        got = 0
        while states:
            low = states & -states
            states ^= low
            for mb, reach in out[low.bit_length() - 1]:
                if mb & mask:
                    got |= reach
        return got

    while work:
        a = work.pop()
        here, first = on[a], one[a]
        moves = [(t, here, first) for t in fx[a]]
        for ma, ta, wa in fe[a]:
            if wa:
                moves.append((ta, step(here, ma), step(first, ma & first_word)))
            else:
                moves.append((ta, step(here, ma) | step(first, ma), 0))
        for ta, got, got_first in moves:
            if (got | got_first) & accept:
                return True
            if got & ~on[ta] or got_first & ~one[ta]:
                on[ta] |= got
                one[ta] |= got_first
                work.append(ta)
    return False


def pattern_steps(pat):
    """Per state of the pattern, its edges as (mask, states reached with
    their epsilon closure); last, the closure of its first state; and the
    accepting bit. Kept on the pattern."""
    if getattr(pat, "steps", None):
        return pat.steps
    count = len(pat.edges)
    closure = []
    for s in range(count):
        seen, stack = {s}, [s]
        while stack:
            for t in pat.eps[stack.pop()]:
                if t not in seen:
                    seen.add(t)
                    stack.append(t)
        closure.append(sum(1 << t for t in seen))
    out = [[(mb, closure[tb]) for mb, tb, _ in pat.edges[s] if tb != pat.start] for s in range(count)]
    out.append(closure[pat.first])
    opens = 0
    for s in range(count):
        if closure[pat.first] >> s & 1:
            opens |= sum(mb for mb, _ in out[s])
    pat.steps = (out, 1 << pat.accept, opens)
    return pat.steps


def format_leads(fmt):
    """Per state of a format, the characters that can come next (through
    its epsilon moves). Kept on the format."""
    if getattr(fmt, "leads", None):
        return fmt.leads
    leads = [0] * len(fmt.edges)
    for a, edges in enumerate(fmt.edges):
        for ma, _, _ in edges:
            leads[a] |= ma
    changed = True
    while changed:
        changed = False
        for a in range(len(leads) - 1, -1, -1):
            got = leads[a]
            for t in fmt.eps[a]:
                got |= leads[t]
            if got != leads[a]:
                leads[a] = got
                changed = True
    fmt.leads = leads
    return leads


# ---------------------------------------------------------------- scenarios
GAME_CALLS = {"expect": 1, "reject": 1, "at_most": 2}
PEER_CALLS = {"expect_peer": 1, "expect_peer2": 1, "reject_peer": 1}
COMMAND_START = {";", "&&", "||", "then", "else", "do", "{", "!", "|", "(", "if", "while", "until"}


def shell_words(line):
    """The words of a shell line: (text with expansions as VAR, quoted)."""
    words, i, n = [], 0, len(line)
    while i < n:
        if line[i].isspace():
            i += 1
            continue
        if line[i] == "#":
            break
        if line[i] in ";|&()":
            j = i + 1
            while j < n and line[j] in ";|&" and line[i] not in "()":
                j += 1
            words.append((line[i:j], False))
            i = j
            continue
        buf, quoted = [], False
        while i < n and not line[i].isspace() and line[i] not in ";|&()":
            c = line[i]
            if c == "'":
                end = line.find("'", i + 1)
                end = n if end < 0 else end
                buf.append(line[i + 1:end])
                quoted, i = True, end + 1
            elif c == '"':
                quoted, i = True, i + 1
                while i < n and line[i] != '"':
                    if line[i] == "\\" and i + 1 < n and line[i + 1] in '$`"\\':
                        buf.append(line[i + 1])
                        i += 2
                    elif line[i] == "$" and i + 1 < n and (line[i + 1].isalnum() or line[i + 1] in "_{(?#@*!"):
                        i = expansion(line, i)
                        buf.append(VAR)
                    else:
                        buf.append(line[i])
                        i += 1
                i += 1
            elif c == "$" and i + 1 < n and (line[i + 1].isalnum() or line[i + 1] in "_{(?#@*!"):
                i = expansion(line, i)
                buf.append(VAR)
            elif c == "\\" and i + 1 < n:
                buf.append(line[i + 1])
                i += 2
            else:
                buf.append(c)
                i += 1
        words.append(("".join(buf), quoted))
    return words


def expansion(line, i):
    """The index after the $... expansion at line[i]."""
    if line.startswith("$(", i) or line.startswith("${", i):
        first = line[i + 1]
        close = {"(": ")", "{": "}"}[first]
        depth, j = 0, i + 1
        while j < len(line):
            if line[j] == first:
                depth += 1
            elif line[j] == close:
                depth -= 1
                if not depth:
                    return j + 1
            j += 1
        return j
    if line[i + 1] in "?#@*!" or line[i + 1].isdigit():
        return i + 2
    j = i + 1
    while j < len(line) and (line[j].isalnum() or line[j] == "_"):
        j += 1
    return j


def patterns(path):
    """(line, side, pattern) of every log pattern a scenario reads."""
    pending, start = "", 0
    for number, raw in enumerate(path.read_text().splitlines(), 1):
        if not pending:
            start = number
        if raw.endswith("\\"):
            pending += raw[:-1] + " "
            continue
        line, pending = pending + raw, ""
        if line.lstrip().startswith("#"):
            continue
        words = shell_words(line)
        calls = GAME_CALLS | PEER_CALLS
        for k, (word, quoted) in enumerate(words):
            if quoted:
                continue
            command = k == 0 or words[k - 1][0] in COMMAND_START
            if command and word in calls and k + calls[word] < len(words):
                side = "peer" if word in PEER_CALLS else "game"
                yield start, side, words[k + calls[word]][0]
            # query VAR WHAT ARGS waits for "test query: WHAT ARGS = VALUE".
            if command and word == "query" and k + 2 < len(words):
                asked = []
                for w, _ in words[k + 2:]:
                    if w in COMMAND_START or w in (";", "&&", "||", ")"):
                        break
                    asked.append(re.sub(r"([\\.\[\]()*+?{}|^$])", r"\\\1", w))
                yield start, "game", "test query: " + " ".join(asked) + " = "
            if word == "grep" and k >= 2 and words[k - 1][0] == "|" and words[k - 2][0] == "since":
                rest = [w for w, _ in words[k + 1:]]
                rest = rest[:next((n for n, w in enumerate(rest) if w in ("|", ";", ")", "&&", "||")), len(rest))]
                options = [w for w in rest if w.startswith("-")]
                values = [w for w in rest if not w.startswith("-")]
                letters = "".join(o.lstrip("-") for o in options if not o.startswith("--"))
                if values and not set(letters) & set("efv"):
                    yield start, "game", values[0]


def ere_to_python(pattern):
    """grep -E's pattern as Python reads it, or why it cannot be read."""
    if re.search(r"(?<!\\)(?:\\\\)*\\d", pattern):
        return None, "has \\d, which grep -E reads as a d: use [0-9]"
    classes = {"digit": "0-9", "alpha": "A-Za-z", "alnum": "A-Za-z0-9", "space": " \\t", "upper": "A-Z",
               "lower": "a-z", "xdigit": "0-9A-Fa-f", "punct": "!-/:-@\\[-`{-~"}
    pattern = re.sub(r"\[:(\w+):\]", lambda m: classes.get(m.group(1), m.group(0)), pattern)
    pattern = re.sub(r"\\[<>]", "", pattern)
    pattern = pattern.replace(VAR, "(?:.*)")
    try:
        re.compile(pattern)
    except re.error as error:
        return None, f"does not parse ({error})"
    return pattern, None


def required_runs(regex):
    """The literal runs every match of the regex contains."""
    runs, buf = [], []

    def flush():
        if buf:
            runs.append("".join(buf))
            buf.clear()

    def walk(items):
        for op, av in items:
            if op is C.LITERAL:
                buf.append(chr(av))
                continue
            if op is C.SUBPATTERN and not any(o is C.BRANCH for o, _ in av[-1]):
                walk(av[-1])
                continue
            flush()
            if op in (C.MAX_REPEAT, C.MIN_REPEAT) and av[0] >= 1:
                walk(av[2])
                flush()

    walk(P.parse(regex))
    flush()
    return runs


def lead(regex):
    """The literal text the regex starts with."""
    out = []
    for op, av in P.parse(regex):
        if op is not C.LITERAL:
            break
        out.append(chr(av))
    return "".join(out)


def nearest(regex, formats, shared):
    """The formats nearest to a pattern: longest common start, then trigrams."""
    head = lead(regex)
    scored = []
    for n, f in enumerate(formats):
        best = 0
        if len(head) >= 4:
            for m in re.finditer(re.escape(head[:4]), f.text):
                rest, k = f.text[m.start():], 0
                while k < len(head) and k < len(rest) and head[k] == rest[k]:
                    k += 1
                best = max(best, k)
        scored.append((best, shared.get(n, 0), n))
    scored.sort(key=lambda s: (-s[0], -s[1]))
    return [formats[n] for best, score, n in scored[:3] if best or score]


def unique(formats):
    """One format per regex (the first place that writes it)."""
    seen = {}
    for f in formats:
        seen.setdefault(f.regex, f)
    return list(seen.values())


class Checker:
    """Patterns against one side's formats."""

    def __init__(self, formats, alphabet):
        self.formats, self.alphabet = formats, alphabet
        self.index, self.done = {}, {}
        for n, f in enumerate(formats):
            for g in f.grams:
                self.index.setdefault(g, []).append(n)

    def check(self, regex):
        """(the format that matches, or None and the nearest ones); None
        for a pattern with no literal to check."""
        if regex not in self.done:
            self.done[regex] = self.search(regex)
        return self.done[regex]

    def search(self, regex):
        runs = [r for r in required_runs(regex) if len(r) >= 3 and VAR not in r]
        if not runs:
            return None
        formats, alphabet = self.formats, self.alphabet
        trigrams = set().union(*(grams(run) for run in runs))
        score = Counter()
        for g in trigrams:
            score.update(self.index.get(g, ()))
        pat = compile_regex(regex, alphabet, True)
        first_word = 0 if any(": " in run for run in runs) else alphabet.all & ~alphabet.bit(" ")
        # The likeliest first: most of the pattern's trigrams shown, then the
        # plainest. One with less than a quarter of them would have to make
        # the rest of the pattern out of its values.
        wanted = len(trigrams) / 4
        candidates = [n for n in score if score[n] >= wanted]
        visible = {n: len(trigrams & formats[n].shown) for n in candidates}
        for n in sorted(candidates, key=lambda n: (-visible[n], -score[n], len(formats[n].regex))):
            f = formats[n]
            if f.nfa is None:
                f.nfa = compile_regex(f.regex, alphabet, False)
            if intersect(f.nfa, pat, first_word):
                return f, []
        return None, nearest(regex, formats, score)


def main(argv):
    verbose = "-v" in argv
    names = [a for a in argv if not a.startswith("-")]
    paths = [Path(a).resolve() for a in names] if names else sorted((ROOT / "tools/scenarios").glob("*.sh"))
    game, peer = (unique(side) for side in collect())
    found = [(path, line, side, pattern) for path in paths for line, side, pattern in patterns(path)]
    chars = set()
    for f in game + peer:
        chars.update(f.text)
    for _, _, _, pattern in found:
        chars.update(pattern)
    alphabet = Alphabet(chars)
    checkers = {"game": Checker(game, alphabet), "peer": Checker(peer, alphabet)}
    problems = checked = skipped = 0
    for path, line, side, pattern in found:
        where = f"{path.relative_to(ROOT)}:{line}"
        shown = pattern.replace(VAR, "$…")
        regex, why = ere_to_python(pattern)
        if why:
            print(f"{where}: '{shown}' {why}", file=sys.stderr)
            problems += 1
            continue
        result = checkers[side].check(regex)
        if result is None:
            skipped += 1
            continue
        checked += 1
        hit, near = result
        if hit:
            if verbose:
                print(f"{where}: '{shown}' <- {hit.where}: {hit.text[:160]}")
            continue
        problems += 1
        log = "the game's log" if side == "game" else "the mock player's log"
        print(f"{where}: no line of {log} matches '{shown}'", file=sys.stderr)
        for f in near:
            print(f"    nearest: {f.where}: {f.text[:200]}", file=sys.stderr)
    if verbose:
        print(f"{len(game)} game formats, {len(peer)} mock-player formats; {checked} patterns checked, {skipped} with no literal to check")
    if problems:
        print(f"scenarios-patterns: {problems} pattern(s) match no log line", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
