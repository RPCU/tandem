"""Record the local V's pose from the running game (read-only), for offline
work on retargeting, quantisation and interpolation.

    tools/re/run tools/re/record_pose.py [seconds, default 20] [name, default v]

Writes recordings/pose-NAME.json (bone names, parents, reference poses of
V's skeleton and of a third-person body rig found on a nearby NPC) and
recordings/pose-NAME.bin: frames of little-endian float32

    time (s), sample counter, position x y z (m), rotation x y z w,
    then per bone: translation x y z, rotation x y z w   (model space)

for the bones listed in the JSON ("bones": the third-person rig's names that
exist in V's skeleton, then V's shadow_* bones). Layout facts:
docs/research/engine-knowledge.md §13. A frame is kept only when two reads
in a row agree (memsrv's 'C' request: the engine rewrites the buffer every
frame) and it is not in its short local-space state.
"""

import json
import math
import struct
import sys
import time

sys.argv, argv = ["live.py"], sys.argv
from live import N, components, cls, instances, m, player_component  # noqa: E402
from mem import ROOT  # noqa: E402

PROBES = ("Head", "LeftHand", "RightHand", "LeftFoot", "RightFoot", "Spine3")
BODY_RIGS = (14264052059588999261, 10037276762472186253)  # man / woman base


def skeleton(comp):
    obj = m.u64(comp + 0x178)
    skel, pose = m.u64(obj + 8), m.u64(obj + 0x18)
    n = m.u32(skel + 8)
    names = [N().get(h, hex(h)) for h in struct.unpack(f"<{n}Q", m.read(m.u64(skel + 0x20), 8 * n))]
    parents = list(struct.unpack(f"<{n}h", m.read(m.u64(skel + 0x10), 2 * n)))
    raw = m.read(m.u64(skel), 48 * n)
    ref = [list(struct.unpack_from("<3f4x4f", raw, 48 * i)) for i in range(n)]
    return obj, pose, names, parents, ref


def main():
    seconds = float(argv[1]) if len(argv) > 1 else 20
    name = argv[2] if len(argv) > 2 else "v"
    comp = player_component()
    if not comp:
        sys.exit("no local V: not in a loaded game?")
    obj, pose, names, parents, ref = skeleton(comp)
    gi = m.u64(comp + 0x270)

    header = {"game": "2.50", "skeleton": {"names": names, "parents": parents, "reference": ref}, "rigs": {}}
    body = None
    for c in instances("entAnimatedComponent"):
        rig = m.u64(c + 0x138)
        if rig in BODY_RIGS and str(rig) not in header["rigs"] and m.u64(c + 0x178):
            _, _, n2, p2, r2 = skeleton(c)
            count = m.u32(m.u64(m.u64(c + 0x140) + 0x28) + 0x5C)
            header["rigs"][str(rig)] = {"names": n2[:count], "parents": p2[:count], "reference": r2[:count]}
            body = body or n2[:count]
    if body is None:
        print("no NPC on a body rig nearby: recording V's first 73 bones", file=sys.stderr)
        body = names[:73]
    bones = [b for b in body if b in names] + [b for b in names if b.startswith("shadow_")]
    index = [names.index(b) for b in bones]
    header["bones"] = bones
    header["frame"] = "time, sample, position[3], rotation[4], then per bone translation[3], rotation[4]; float32"

    out = ROOT / "recordings"
    out.mkdir(exist_ok=True)
    span = (max(index) + 1) * 48
    probes = [names.index(b) for b in PROBES]
    frames = rejected = rebuilds = 0
    skel = m.u64(obj + 8)
    last = None
    start = time.time()
    with open(out / f"pose-{name}.bin", "wb") as f:
        while time.time() - start < seconds:
            # The skeleton and pose objects are replaced when the entity's parts
            # change (equipment, entering a vehicle): follow them.
            if m.u64(obj + 8) != skel:
                skel = m.u64(obj + 8)
                _, pose, names, _, ref = skeleton(comp)
                if any(b not in names for b in bones):
                    time.sleep(0.01)
                    continue
                index = [names.index(b) for b in bones]
                span = (max(index) + 1) * 48
                probes = [names.index(b) for b in PROBES]
                rebuilds += 1
            a = m.stable(m.u64(pose), span)
            sample = m.u32(gi + 0x2544)
            world = m.read(comp + 0xE0, 0x20)
            # Local-space values (after the commit, before the conversion) show
            # as bones sitting exactly at their reference offset: skip those.
            if a is None or sample == last or any(
                math.dist(struct.unpack_from("<3f", a, 48 * i), ref[i][:3]) < 1e-4 for i in probes
            ):
                rejected += a is None
                time.sleep(0.002)
                continue
            last = sample
            x, y, z, qx, qy, qz, qw = struct.unpack("<3i4x4f", world)
            row = [time.time() - start, float(sample & 0xFFFFFF), x / 131072, y / 131072, z / 131072, qx, qy, qz, qw]
            for i in index:
                row += struct.unpack_from("<3f4x4f", a, 48 * i)
            f.write(struct.pack(f"<{len(row)}f", *row))
            frames += 1
    header["frames"] = frames
    header["floatsPerFrame"] = 9 + 7 * len(bones)
    (out / f"pose-{name}.json").write_text(json.dumps(header))
    print(
        f"{frames} frames in {seconds:.0f} s ({frames / seconds:.0f}/s), {rejected} unstable reads dropped, "
        f"{rebuilds} skeleton rebuilds, {len(bones)} bones"
    )


if __name__ == "__main__":
    main()
