"""Turn a pose recording (record_pose.py) into the mock player's pose file.

    tools/re/run tools/re/pose_export.py recordings/pose-play   # -> recordings/pose-play.tpose

Format (little endian): "TPOS", u32 version 1, u32 frames, f32 reference hips
height, then per frame: f32 time (s), f32 hips x y z (model space, m), and
71 model-space rotations x y z w in the order of core/src/pose.cpp's bone
table (the third-person body rig), each taken from V's shadow_ bone when
there is one, as the plugin does.
"""

import json
import struct
import sys

import numpy as np

BONES = None  # read from the C++ table so the two never disagree


def bone_table():
    import re

    from mem import ROOT

    text = (ROOT / "core/src/pose.cpp").read_text()
    block = text[text.index("kBoneNames = {") : text.index("};", text.index("kBoneNames = {"))]
    return re.findall(r'"([^"]+)"', block)


def main():
    base = sys.argv[1]
    h = json.load(open(base + ".json"))
    raw = np.fromfile(base + ".bin", dtype="<f4").reshape(-1, h["floatsPerFrame"])
    bones = h["bones"]
    table = bone_table()
    assert len(table) == 71, len(table)
    src = []
    for name in table:
        if "shadow_" + name in bones:
            src.append(bones.index("shadow_" + name))
        elif name in bones:
            src.append(bones.index(name))
        else:
            sys.exit(f"the recording has no {name}")
    names = h["skeleton"]["names"]
    hips_name = "shadow_Hips" if "shadow_Hips" in names else "Hips"
    ref_hips = h["skeleton"]["reference"][names.index(hips_name)][2]
    pose = raw[:, 9:].reshape(len(raw), len(bones), 7)
    with open(base + ".tpose", "wb") as f:
        f.write(b"TPOS" + struct.pack("<IIf", 1, len(raw), ref_hips))
        hips = src[table.index("Hips")]
        for i in range(len(raw)):
            f.write(struct.pack("<f3f", raw[i, 0], *pose[i, hips, :3]))
            f.write(pose[i, src, 3:].astype("<f4").tobytes())
    print(f"{base}.tpose: {len(raw)} frames, {raw[-1, 0] - raw[0, 0]:.0f} s, reference hips {ref_hips:.3f} m")


if __name__ == "__main__":
    main()
