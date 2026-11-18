"""Offline check of the pose stream's design on a recording (record_pose.py):
retarget to the third-person hierarchy, quantise, resample at a send rate,
interpolate, and measure the error against the original.

    tools/re/run tools/re/pose_codec.py recordings/pose-play [send Hz, default 30]

Prints sizes and errors; nothing is written. The method measured here is
the one proposed in docs/research/pose-streaming.md (sixth pass).
"""

import json
import math
import struct
import sys

import numpy as np


def qmul(a, b):
    ax, ay, az, aw = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    bx, by, bz, bw = b[..., 0], b[..., 1], b[..., 2], b[..., 3]
    return np.stack(
        [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ],
        -1,
    )


def qconj(q):
    return q * np.array([-1, -1, -1, 1], dtype=q.dtype)


def qrot(q, v):
    qv = np.concatenate([v, np.zeros(v.shape[:-1] + (1,), v.dtype)], -1)
    return qmul(qmul(q, qv), qconj(q))[..., :3]


def quantise(q, bits=15):
    """Smallest-three: drop the largest component, keep three at `bits` each (+2 bits index)."""
    q = q / np.linalg.norm(q, axis=-1, keepdims=True)
    big = np.argmax(np.abs(q), -1)
    sign = np.sign(np.take_along_axis(q, big[..., None], -1))
    q = q * sign
    scale = (1 << (bits - 1)) - 1
    out = np.round(np.clip(q * math.sqrt(2), -1, 1) * scale) / scale / math.sqrt(2)
    rest = out.copy()
    np.put_along_axis(rest, big[..., None], 0, -1)
    w = np.sqrt(np.clip(1 - (rest**2).sum(-1), 0, 1))
    np.put_along_axis(out, big[..., None], w[..., None], -1)
    return out


def slerp(a, b, t):
    d = (a * b).sum(-1, keepdims=True)
    b = np.where(d < 0, -b, b)
    d = np.abs(d).clip(0, 1)
    th = np.arccos(d)
    s = np.sin(th)
    lin = s < 1e-5
    wa = np.where(lin, 1 - t, np.sin((1 - t) * th) / np.where(lin, 1, s))
    wb = np.where(lin, t, np.sin(t * th) / np.where(lin, 1, s))
    return wa * a + wb * b


def main():
    base = sys.argv[1]
    hz = float(sys.argv[2]) if len(sys.argv) > 2 else 30
    h = json.load(open(base + ".json"))
    n = h["floatsPerFrame"]
    raw = np.fromfile(base + ".bin", dtype="<f4").reshape(-1, n)
    t = raw[:, 0].astype(np.float64)
    bones = h["bones"]
    pose = raw[:, 9:].reshape(len(raw), len(bones), 7)
    rig = next(iter(h["rigs"].values()), None)
    if rig is None:
        sys.exit("the recording has no third-person rig (no NPC was near)")
    names, parents = rig["names"], rig["parents"]
    offset = np.array([r[:3] for r in rig["reference"]], dtype=np.float32)

    # Source: shadow_ bone where it exists, else the main bone (model space).
    src = [bones.index("shadow_" + b) if "shadow_" + b in bones else bones.index(b) for b in names]
    ms_t, ms_q = pose[:, src, :3], pose[:, src, 3:]
    ms_q = ms_q / np.linalg.norm(ms_q, axis=-1, keepdims=True)

    # Local rotations on the third-person hierarchy.
    local = ms_q.copy()
    for i, p in enumerate(parents):
        if p >= 0:
            local[:, i] = qmul(qconj(ms_q[:, p]), ms_q[:, i])
    hips = names.index("Hips")
    v_hips = h["skeleton"]["reference"][h["skeleton"]["names"].index("Hips")][2]
    ratio = offset[hips][2] / v_hips

    def forward(loc, hips_t):
        """Model-space positions on the receiving rig from local rotations."""
        q = np.zeros_like(loc)
        pos = np.zeros(loc.shape[:-1] + (3,), np.float32)
        for i, p in enumerate(parents):
            if p < 0:
                q[:, i], pos[:, i] = loc[:, i], 0
            else:
                q[:, i] = qmul(q[:, p], loc[:, i])
                off = hips_t if i == hips else np.broadcast_to(offset[i], pos[:, i].shape)
                pos[:, i] = pos[:, p] + qrot(q[:, p], off)
        return pos

    hips_local = ms_t[:, hips] * np.array([1, 1, ratio], np.float32)  # Hips hangs from Root (identity)
    truth = forward(local, hips_local)

    # Retarget error against the sender's own body (proportions differ): hands and feet.
    probe = [names.index(b) for b in ("LeftHand", "RightHand", "LeftFoot", "RightFoot", "Head")]
    drift = np.linalg.norm(truth[:, probe] - ms_t[:, probe] * np.array([1, 1, 1], np.float32), axis=-1)
    print(f"{len(raw)} frames, {t[-1] - t[0]:.0f} s, {len(names)} bones on the third-person rig, hips ratio {ratio:.3f}")
    print("retarget (same pose, other proportions), distance to the sender's own bone, mean / max cm:")
    for b, col in zip(("LeftHand", "RightHand", "LeftFoot", "RightFoot", "Head"), drift.T):
        print(f"  {b:<10} {100 * col.mean():5.1f} {100 * col.max():5.1f}")

    # Which bones move at all (others need not be sent every packet).
    spread = 1 - np.abs((local * local[0]).sum(-1)).min(0)
    moving = int((spread > 1e-4).sum())
    print(f"bones whose local rotation changes over the recording: {moving} of {len(names)}")

    for bits, label in ((15, "6 bytes (3 x 15 bits + 2)"), (10, "4 bytes (3 x 10 bits + 2)")):
        q = quantise(local, bits)
        # Resample: send at `hz`, draw at the original frame times by slerp between sends.
        send = np.arange(t[0], t[-1], 1 / hz)
        idx = np.searchsorted(t, send).clip(0, len(t) - 1)
        sent_q, sent_h, sent_t = q[idx], hips_local[idx], t[idx]
        k = np.searchsorted(sent_t, t, side="right").clip(1, len(sent_t) - 1)
        span = (sent_t[k] - sent_t[k - 1]).clip(1e-6)
        w = ((t - sent_t[k - 1]) / span).clip(0, 1).astype(np.float32)
        qi = slerp(sent_q[k - 1], sent_q[k], w[:, None, None])
        hi = sent_h[k - 1] + (sent_h[k] - sent_h[k - 1]) * w[:, None]
        shown = forward(qi / np.linalg.norm(qi, axis=-1, keepdims=True), hi)
        err = np.linalg.norm(shown - truth, axis=-1)
        only_q = np.linalg.norm(forward(q, hips_local) - truth, axis=-1)
        size = len(names) * (bits * 3 + 2 + 7) // 8 + 6
        print(
            f"{label}: packet {size} bytes ({size * hz / 1000:.1f} kB/s at {hz:.0f} Hz); "
            f"quantisation alone max {1000 * only_q.max():.1f} mm; "
            f"with {hz:.0f} Hz + slerp: mean {1000 * err.mean():.1f} mm, 99th {1000 * np.percentile(err, 99):.1f} mm, "
            f"max {1000 * err.max():.1f} mm (worst bone {names[int(err.max(0).argmax())]})"
        )


if __name__ == "__main__":
    main()
