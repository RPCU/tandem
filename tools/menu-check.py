#!/usr/bin/env python3
"""The co-op menu shows what it means to (docs/architecture/coop-menu.md).

Checks scripts/tandem against plugin/src/config.cpp and core/src/settings.cpp:

- Settings.reds' Keys(), Labels(), Abouts(), Kinds() and Whens() have one
  entry per setting, in the same order; no key twice; one short page (at
  most MAX_ROWS rows, no groups: the user, 2026-11-28);
- HostRules() are rows of the page, the host's rules sent in proto::Rules
  (friendly_fire, enemy_scaling, level_gap); IniKeys() are not rows;
- every setting, and every IniKeys() key, is one tandem.ini's parser takes
  (ApplySetting) and the page may save (settings::IsEditable);
- every label, description and choice text is a key of Text.reds in English
  and French; Choices() and ChoiceTexts() name page keys and pair up;
- no two of the menu's entries (Menu-* texts) read the same, nor as one of
  the game's own main or pause menu entries or another mod's (Settings,
  Mods, Mod Settings...): a second "Settings" reads as a duplicate menu;
- nothing registers with the Mod Settings mod (decision 2026-11-19);
- no value read straight from an array a call returned (`Labels()[i]`): the
  script VM gave an empty String for it (every settings row was ": V").

Usage: tools/menu-check.py [DIR]   (default scripts/tandem; exit 1 on an error)
"""

import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
scripts = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "scripts" / "tandem"
CONFIG = root / "plugin" / "src" / "config.cpp"
EDITABLE = root / "core" / "src" / "settings.cpp"
# The page's rows at most: one short page, no groups (2026-11-28).
MAX_ROWS = 10
# The host's rules, as proto::Rules carries them to every game.
HOST_RULES = ["friendly_fire", "enemy_scaling", "level_gap"]

# The game's own entries in its main and pause menus, and the Mod Settings
# mod's (English, lower case).
VANILLA = {
    "continue", "new game", "load game", "save game", "settings", "mods", "mod settings",
    "credits", "close game", "exit game", "exit to main menu", "quit", "resume", "back",
    "additional content", "photo mode", "load", "save",
}
VANILLA_FR = {"paramètres", "mods", "paramètres des mods", "charger", "sauvegarder", "continuer"}


def body(text, signature):
    """The text of the function whose declaration matches, to its closing brace."""
    m = re.search(signature, text)
    if not m:
        return None
    start = text.index("{", m.end() - 1)
    depth = 0
    for i in range(start, len(text)):
        depth += {"{": 1, "}": -1}.get(text[i], 0)
        if depth == 0:
            return text[start : i + 1]
    return text[start:]


def array_of(text, name):
    b = body(text, rf"static func {name}\(\)\s*->\s*array<\w+>\s*\{{")
    if b is None:
        return None
    m = re.search(r"\[(.*?)\]", b, re.S)
    return [v.strip().strip('"') for v in m.group(1).split(",") if v.strip()] if m else []


def packages(text):
    found, current = {}, None
    for line in text.splitlines():
        m = re.match(r"public class (\w+) extends ModLocalizationPackage", line)
        if m:
            current = found.setdefault(m.group(1), {})
            continue
        m = re.search(r'this\.Text\("Tandem-([^"]+)",\s*"((?:[^"\\]|\\.)*)"\)', line)
        if m and current is not None:
            current[m.group(1)] = m.group(2)
    return found


def main():
    errors = []
    settings = (scripts / "Settings.reds").read_text()
    texts = packages((scripts / "Text.reds").read_text())
    english, french = texts.get("TandemTextEnglish", {}), texts.get("TandemTextFrench", {})

    arrays = {n: array_of(settings, n) for n in ("Keys", "Labels", "Abouts", "Kinds", "Whens")}
    for name, values in arrays.items():
        if values is None:
            errors.append(f"Settings.reds: no {name}()")
    if errors:
        return report(errors)
    keys = arrays["Keys"]
    for name, values in arrays.items():
        if len(values) != len(keys):
            errors.append(f"Settings.reds: {name}() has {len(values)} entries, Keys() {len(keys)}: one per setting, in order")
    for key in sorted({k for k in keys if keys.count(k) > 1}):
        errors.append(f"Settings.reds: {key} listed twice in Keys()")
    for name in ("Labels", "Abouts"):
        values = arrays[name]
        for v in sorted({v for v in values if values.count(v) > 1}):
            errors.append(f"Settings.reds: {v} used twice in {name}()")
    for key, label, about in zip(keys, arrays["Labels"], arrays["Abouts"]):
        for kind, text in (("label", label), ("description", about)):
            for lang, table in (("English", english), ("French", french)):
                if text not in table:
                    errors.append(f"Settings.reds: {key}'s {kind} {text} is not a key of Text.reds in {lang}")
    for name, allowed in (("Kinds", {"0", "1", "2"}), ("Whens", {"0", "1", "2"})):
        for key, v in zip(keys, arrays[name]):
            if v not in allowed:
                errors.append(f"Settings.reds: {key}'s {name[:-1].lower()} {v} is not 0, 1 or 2")

    # One short page: no groups, a few rows; the host's rules among them.
    if len(keys) > MAX_ROWS:
        errors.append(f"Settings.reds: {len(keys)} settings on the page, at most {MAX_ROWS}: a technical knob stays in tandem.ini (IniKeys)")
    if re.search(r"static func Group", settings):
        errors.append("Settings.reds: groups again: the page is one short list")
    host_rules = array_of(settings, "HostRules")
    if host_rules != HOST_RULES:
        errors.append(f"Settings.reds: HostRules() is {host_rules}, not the host's rules proto::Rules carries {HOST_RULES}")
    for key in host_rules or []:
        if key not in keys:
            errors.append(f"Settings.reds: host's rule {key} is not a row of the page")
    ini_keys = array_of(settings, "IniKeys") or []
    for key in ini_keys:
        if key in keys:
            errors.append(f"Settings.reds: {key} is both a row (Keys) and tandem.ini only (IniKeys)")

    # Choices and their texts, key by key.
    def by_key(func):
        b = body(settings, rf"static func {func}\(key: String\)") or ""
        found = re.findall(r'Equals\(key, "(\w+)"\)\s*\{\s*return \[([^\]]*)\]', b)
        for k in sorted({k for k, _ in found if [x for x, _ in found].count(k) > 1}):
            errors.append(f"Settings.reds: {func}() lists {k} twice")
        return {k: [x.strip().strip('"') for x in v.split(",")] for k, v in found}

    choices = by_key("Choices")
    choice_texts = by_key("ChoiceTexts")
    for key in sorted(choices.keys() | choice_texts.keys()):
        if key not in keys:
            errors.append(f"Settings.reds: Choices() has {key}, not a key of the page")
        if len(choices.get(key, [])) != len(choice_texts.get(key, [])):
            errors.append(f"Settings.reds: {key} has {len(choices.get(key, []))} choices, {len(choice_texts.get(key, []))} texts")
        for text in choice_texts.get(key, []):
            for lang, table in (("English", english), ("French", french)):
                if text not in table:
                    errors.append(f"Settings.reds: {key}'s choice text {text} is not a key of Text.reds in {lang}")
    kinds = dict(zip(keys, arrays["Kinds"]))
    for key in keys:
        if kinds.get(key) == "1" and key not in choices:
            errors.append(f"Settings.reds: {key} is a choice (kind 1) with no Choices() entry")

    # The plugin takes and saves each.
    config = body(CONFIG.read_text(), r"bool ApplySetting\(") or ""
    applied = set(re.findall(r'key == "(\w+)"', config))
    editable_body = body(EDITABLE.read_text(), r"bool IsEditable\(") or ""
    editable = set(re.findall(r'"(\w+)"', editable_body))
    for key in keys + ini_keys:
        if key not in applied:
            errors.append(f"Settings.reds: {key} is not a key ApplySetting takes (plugin/src/config.cpp)")
        if key not in editable:
            errors.append(f"Settings.reds: {key} is not in settings::IsEditable (core/src/settings.cpp)")

    # The menu's entries read differently from each other and from the game's.
    for lang, table, vanilla in (("English", english, VANILLA), ("French", french, VANILLA_FR)):
        entries = {k: v for k, v in table.items() if k.startswith("Menu-") and "{" not in v}
        seen = {}
        for key, value in sorted(entries.items()):
            word = value.strip().lower()
            if word in seen:
                errors.append(f"Text.reds: {key} and {seen[word]} both read \"{value}\" in {lang}: one entry, or two names")
            seen[word] = key
            if word in vanilla and key != "Menu-Back":
                errors.append(f"Text.reds: {key} reads \"{value}\" in {lang}, as an entry of the game's menus or of Mod Settings")
    coop = (scripts / "Coop.reds").read_text()
    if re.search(r'Vanilla\("UI-Labels-(Settings|ModSettings|Mods)"\)', coop):
        errors.append("Coop.reds: an entry labelled with the game's own Settings/Mods word: a duplicate-looking menu")

    for path in sorted(scripts.glob("*.reds")):
        for number, line in enumerate(path.read_text().splitlines(), 1):
            code = line.split("//", 1)[0]
            if re.search(r"ModSettings|mod_settings|runtimeProperty\(\"ModSettings", code):
                errors.append(f"{path.name}:{number}: registers with Mod Settings: tandem's settings are its own page only")
            if re.search(r"\w\((?:[^()]|\([^()]*\))*\)\[", code):
                errors.append(f"{path.name}:{number}: indexes an array a call returned: put it in a local first")
    return report(errors, len(keys))


def report(errors, count=0):
    for e in errors:
        print(f"menu:check: {e}", file=sys.stderr)
    if errors:
        sys.exit(1)
    print(f"menu:check: ok, {count} settings")


main()
