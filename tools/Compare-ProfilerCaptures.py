"""Compare exported editor captures without mixing shadow reuse/update frames.

Usage: python tools/Compare-ProfilerCaptures.py before.txt after.txt
GPU observations are asynchronous; no CPU/GPU frame correlation is inferred.
"""

import argparse
import collections
import math
from pathlib import Path
import re
import statistics


METRICS = (
    "Captured CPU frame", "RHI scene GPU frame", "Scene / Mesh submission",
    "Renderer / Command sort", "Renderer / Instance culling",
    "RHI command translation", "RHI shadow recording CPU",
    "RHI geometry recording CPU", "RHI post-process recording CPU",
    "RHI descriptor preparation CPU", "RHI frame fence wait",
)
CONTEXT = ("Debugger attached", "VSync", "Editor viewport resolution",
           "RHI resolution", "RHI geometry diagnostic mode", "RHI occlusion mode")


def read_capture(path):
    text = Path(path).read_text(encoding="utf-8-sig")
    frames = []
    for block in re.split(r"========== FRAME \d+ ==========\s*", text)[1:]:
        frame = dict(re.findall(r"^([^:\n]+): (.*)$", block, re.MULTILINE))
        frame["shadow_group"] = "reused" if "VSM frame reused" in frame else "active"
        frames.append(frame)
    if not frames:
        raise ValueError(f"{path}: no retained frame records found")
    return frames


def values(frames, name):
    return [float(match[1]) for frame in frames
            if (match := re.match(r"^([\d.]+) ms", frame.get(name, "")))]


def summarize(samples):
    samples = sorted(samples)
    return statistics.mean(samples), samples[math.ceil(.95 * len(samples)) - 1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before")
    parser.add_argument("after")
    args = parser.parse_args()
    before, after = read_capture(args.before), read_capture(args.after)
    print(f"Retained frames: {len(before)} before / {len(after)} after")
    for name in CONTEXT:
        old = collections.Counter(frame.get(name, "missing") for frame in before)
        new = collections.Counter(frame.get(name, "missing") for frame in after)
        print(f"{name}: before {dict(old)}; after {dict(new)}")
        if set(old) != set(new) or len(old) != 1 or len(new) != 1:
            print("  WARNING: conditions differ or vary within a capture; timing deltas are not controlled.")
    print("Build configuration, camera path, hardware, quality settings and debugger configuration must also match.")
    print("Times below are milliseconds. Negative deltas mean lower time. Scopes can overlap; do not sum rows.")
    for group in ("all", "reused", "active"):
        old = [f for f in before if group == "all" or f["shadow_group"] == group]
        new = [f for f in after if group == "all" or f["shadow_group"] == group]
        print(f"\n{group}: {len(old)} before / {len(new)} after")
        print(f"{'Metric':38} {'mean before':>11} {'mean after':>11} {'delta':>9} {'p95 before':>11} {'p95 after':>11}")
        for name in METRICS:
            a, b = values(old, name), values(new, name)
            if not a or not b:
                continue
            am, ap = summarize(a)
            bm, bp = summarize(b)
            print(f"{name:38} {am:11.3f} {bm:11.3f} {bm-am:9.3f} {ap:11.3f} {bp:11.3f}")
    print("\nShadow groups are based on the CPU frame's reuse status, not GPU observation alignment.")


if __name__ == "__main__":
    main()
