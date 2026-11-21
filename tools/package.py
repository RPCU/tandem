#!/usr/bin/env python3
"""Make a release of tandem for players: `task package`.

  build/package/tandem-<build>.zip           extract into the game's folder
  build/package/tandem-<build>-symbols.zip   the .pdb files, for crash reports
  build/package/INSTALL.md                   the same INSTALL.md as in the zip
  build/package/SHA256SUMS

The zip has the game's layout and only what a player needs:

  red4ext/plugins/tandem/   tandem.dll, tandem-report.exe, tandem.default.ini,
                            moments.txt, INSTALL.md, LICENSE.txt, NOTICE.txt,
                            THIRD-PARTY-LICENSES.txt
  r6/scripts/tandem/        *.reds
  r6/tweaks/tandem/         *.tweak, *.yaml

Never tandem.ini itself (the player's own: the plugin creates it from
tandem.default.ini), nor symbols, recordings, logs or test files. The
packer refuses to go on when the DLL was not built from this commit, when
the tree has changes the commit does not have, or when INSTALL.md names
other versions than core/include/tandem/compat.hpp checks at startup.

Usage: tools/package.py [--out DIR] [--allow-dirty]
"""

import argparse
import hashlib
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLUGIN = "red4ext/plugins/tandem"
SCRIPTS = "r6/scripts/tandem"
TWEAKS = "r6/tweaks/tandem"

# Nothing like these may end up in the player's zip, whatever the lists
# below become.
FORBIDDEN = re.compile(
    r"(\.pdb|\.tplay|\.tpose|\.anims|\.cmd|\.log|\.lib|\.exp|\.ilk)$"
    r"|(^|/)(tandem\.ini|recent\.txt|last-session\.txt)$"
    r"|(^|/)(recordings|reports)/",
    re.IGNORECASE,
)

LICENCES = [
    ("RED4ext.SDK", "vendor/RED4ext.SDK/LICENSE.md"),
    ("RED4ext.SDK: third parties", "vendor/RED4ext.SDK/THIRD_PARTY_LICENSES.md"),
    ("RedLib", "vendor/RedLib/LICENSE"),
    ("nameof (in RedLib)", "vendor/RedLib/vendor/nameof/LICENSE"),
    ("ENet", "vendor/enet/LICENSE"),
    ("zpp_bits", "vendor/zpp_bits/LICENSE"),
]


def fail(message):
    sys.exit(f"package: {message}")


def git(*args):
    return subprocess.run(
        ["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True
    ).stdout.strip()


def shown(path):
    """A path as the player of this script wrote it: from the tree when inside."""
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def versions():
    """What this release was built for, from the sources that decide it."""
    cmake = read("CMakeLists.txt")
    compat = read("core/include/tandem/compat.hpp")
    protocol = read("core/include/tandem/protocol.hpp")
    release = re.search(r"project\(tandem VERSION ([0-9.]+)", cmake)
    game = re.search(r"kGame\{(\d+), (\d+), (\d+), (\d+)\}", compat)
    patch = re.search(r'kGamePatch = "([^"]+)"', compat)
    redscript = re.search(r'kRedscriptVersion = "([^"]+)"', compat)
    proto = re.search(r"kProtocolVersion = (\d+);", protocol)
    needs = re.findall(
        r'\{"(\w+)", "[^"]+", \{(\d+), (\d+), (\d+), \d+\}, (true|false)\}', compat
    )
    if not (release and game and patch and redscript and proto and needs):
        fail("could not read the versions from CMakeLists.txt, compat.hpp, protocol.hpp")
    return {
        "release": release.group(1),
        "game": ".".join(game.groups()),
        "patch": patch.group(1),
        "redscript": redscript.group(1),
        "protocol": proto.group(1),
        "needs": [(name, f"{a}.{b}.{c}", needed == "true") for name, a, b, c, needed in needs],
    }


def check_install_md(text, v):
    """INSTALL.md is written by hand: it must say what the plugin enforces."""
    wanted = ["Phantom Liberty", v["patch"], v["game"], v["redscript"]] + [
        version for _, version, needed in v["needs"] if needed
    ]
    missing = [w for w in wanted if w not in text]
    if missing:
        fail(
            "dist/INSTALL.md does not name these versions, which "
            f"core/include/tandem/compat.hpp checks: {', '.join(missing)}"
        )


def build_section(v, build, commit_date):
    """The exact build, appended to the INSTALL.md that ships."""
    lines = [
        "",
        "## This build",
        "",
        "| What | Version |",
        "| --- | --- |",
        f"| tandem | `{build}` (protocol {v['protocol']}), commit of {commit_date} |",
        f"| Built for | Cyberpunk 2077 patch {v['patch']} (`{v['game']}`), with Phantom Liberty |",
    ]
    for name, version, needed in v["needs"]:
        lines.append(
            f"| {name} | {'needs' if needed else 'not needed; made alongside'} {version}"
            f"{' or newer' if needed else ''} |"
        )
    lines.append(f"| redscript | scripts checked with {v['redscript']} |")
    for line in git("submodule", "status").splitlines():
        commit, path, *described = line.strip().lstrip("+-").split()
        lines.append(
            f"| `{path}` | `{commit[:7]}` {' '.join(described).strip('()')} |"
        )
    return "\n".join(lines) + "\n"


def licences():
    out = [
        "Licences of the third-party code built into tandem.dll and",
        "tandem-report.exe. NOTICE.txt says what each one is used for.",
        "",
    ]
    for name, path in LICENCES:
        out += ["=" * 72, name, f"({path})", "=" * 72, "", read(path).strip(), "", ""]
    return "\n".join(out)


def write_zip(path, files, timestamp):
    """A zip that is the same for the same input: sorted, one date."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, files[name])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", default=str(ROOT / "build" / "package"))
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="package a tree with uncommitted changes, or a DLL of another commit (never to release)",
    )
    args = parser.parse_args()

    v = versions()
    commit = git("rev-parse", "--short=7", "HEAD")
    build = f"{v['release']}+{commit}"
    stamp = git("log", "-1", "--format=%cd", "--date=format:%Y %m %d %H %M %S").split()
    timestamp = tuple(int(part) for part in stamp)
    commit_date = "-".join(stamp[:3])

    dirty = git("status", "--porcelain", "--untracked-files=no")
    if dirty and not args.allow_dirty:
        fail(f"the tree has uncommitted changes (commit them, or --allow-dirty):\n{dirty}")

    binaries = ROOT / "build" / "windows" / "plugin"
    dll = binaries / "tandem.dll"
    tool = binaries / "tandem-report.exe"
    for needed in (dll, tool):
        if not needed.exists():
            fail(f"{needed.relative_to(ROOT)} is missing: build first (task build:plugin)")
    # The build's name is in the DLL (core/src/compat.cpp): the one players
    # compare at a join must be this commit's.
    if build.encode() not in dll.read_bytes() and not args.allow_dirty:
        fail(
            f"build/windows/plugin/tandem.dll is not build {build}: "
            "build this commit first (task build:plugin)"
        )

    install = read("dist/INSTALL.md")
    check_install_md(install, v)
    install += build_section(v, build, commit_date)

    files = {
        f"{PLUGIN}/tandem.dll": dll.read_bytes(),
        f"{PLUGIN}/tandem-report.exe": tool.read_bytes(),
        f"{PLUGIN}/tandem.default.ini": (ROOT / "dist" / "tandem.ini").read_bytes(),
        # The moment list (scripted-moments.md): ours, replaced by each update.
        f"{PLUGIN}/moments.txt": (ROOT / "dist" / "moments.txt").read_bytes(),
        f"{PLUGIN}/INSTALL.md": install.encode(),
        f"{PLUGIN}/LICENSE.txt": (ROOT / "LICENSE").read_bytes(),
        f"{PLUGIN}/NOTICE.txt": (ROOT / "NOTICE").read_bytes(),
        f"{PLUGIN}/THIRD-PARTY-LICENSES.txt": licences().encode(),
    }
    scripts = sorted((ROOT / "scripts" / "tandem").glob("*.reds"))
    tweaks = sorted(
        path
        for path in (ROOT / "tweaks" / "tandem").iterdir()
        if path.suffix in (".tweak", ".yaml", ".yml")
    )
    if not scripts or not tweaks:
        fail("no scripts or no tweaks to package")
    for path in scripts:
        files[f"{SCRIPTS}/{path.name}"] = path.read_bytes()
    for path in tweaks:
        files[f"{TWEAKS}/{path.name}"] = path.read_bytes()

    bad = [name for name in files if FORBIDDEN.search(name)]
    if bad:
        fail(f"these do not belong in a release: {', '.join(bad)}")

    symbols = {
        path.name: path.read_bytes()
        for path in (binaries / "tandem.pdb", binaries / "tandem-report.pdb")
        if path.exists()
    }
    if "tandem.pdb" not in symbols:
        fail("build/windows/plugin/tandem.pdb is missing")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("tandem-*.zip"):
        old.unlink()
    release_zip = out / f"tandem-{build}.zip"
    symbols_zip = out / f"tandem-{build}-symbols.zip"
    write_zip(release_zip, files, timestamp)
    write_zip(symbols_zip, symbols, timestamp)
    (out / "INSTALL.md").write_text(install, encoding="utf-8")
    sums = "".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in (release_zip, symbols_zip)
    )
    (out / "SHA256SUMS").write_text(sums)

    print(f"package: tandem {build} (protocol {v['protocol']}, game {v['patch']})")
    for name in sorted(files):
        print(f"  {len(files[name]):>9}  {name}")
    for path in (release_zip, symbols_zip):
        print(f"package: {shown(path)} ({path.stat().st_size} bytes)")
    print(f"package: {shown(out / 'INSTALL.md')}, SHA256SUMS")


if __name__ == "__main__":
    main()
