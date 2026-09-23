#!/usr/bin/env python3
"""Move every parameter and fail if any of them made no difference to the frame.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op --
so a slider can be stone dead while everything compiles, links and renders.

Both plugins are swept: the source for everything it has, the Over effect for
its own group (on the harness's night-scene card).

## The context table

A few controls only act when something else is true: the Mask Threshold only in
the Dark Areas mask, the audio amounts only with audio playing. The table
supplies exactly that, as raw harness arguments; it was checked by emptying it
-- all three go dead without it. (The wind and the star motion were expected
to need a longer run and did not: 2.5 s at 4x is already a visible change.)

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N]
"""

import argparse
import concurrent.futures
import pathlib
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

SIZE = "320x180"
FRAMES = "150"

BEAT = ["--beat"]
CONTEXT = {
    "Audio Substorm": BEAT,
    "Audio Flux": BEAT,
    "Mask Threshold": ["--set", "Sky Mask=2"],
}

# The awkward values are load-bearing: an angle swept at 0, 0.5 and 1 can land
# on the same picture.
SWEEP_VALUES = [0.0, 0.137, 0.611, 1.0]

# Discrete parameters, by the values they take (--list reports the kind but
# not the element count, so these track Controls.h by hand).
DISCRETE = {
    "Hemisphere": [0, 1],
    "Camera": [0, 1],
    "Observer": [0, 1],
    "Horizon": [0, 1, 2],
    "Detail": [0, 1, 2, 3],
    "Preset": [0, 2, 3, 4, 5, 6, 7],
    "Sky Mask": [0, 1, 2],
    "Arcs": [1, 2, 3, 5],
    "Seed": [0, 1, 2, 3],
    "Star Motion": [0, 1],
}

SKIP_KINDS = {"buffer", "event", "text"}
OVER_GROUP = {"Sky Mask", "Mask Threshold", "Illumination", "Mix"}


def read_png(path):
    data = path.read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        if data[pos + 4:pos + 8] == b"IDAT":
            idat += data[pos + 8:pos + 8 + length]
        pos += 12 + length
    return zlib.decompress(idat)


def parameters(harness, over):
    args = [str(harness), "--list"] + (["--over"] if over else [])
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    found = []
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 4:
            found.append((" ".join(parts[1:-2]), parts[-2]))
    return found


def render(harness, tmp, over, setting, extra, index):
    out = tmp / f"sweep-{index}.png"
    args = [str(harness), "--out", str(out), "--size", SIZE, "--frames", FRAMES]
    args += (["--over"] if over else []) + extra + ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"brtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()

    harness = REPO / args.build / "brtest"
    if not harness.exists():
        print(f"no brtest at {harness} -- build first", file=sys.stderr)
        return 2

    work = []
    for name, kind in parameters(harness, False):
        if kind not in SKIP_KINDS:
            work.append((False, name))
    for name, kind in parameters(harness, True):
        if kind not in SKIP_KINDS and name in OVER_GROUP:
            work.append((True, name))

    dead, checked = [], 0
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        tmp = pathlib.Path(tmp)
        jobs = {}
        index = 0
        for over, name in work:
            values = DISCRETE.get(name, SWEEP_VALUES)
            futures = []
            for value in values:
                futures.append(pool.submit(render, harness, tmp, over, f"{name}={value}", CONTEXT.get(name, []), index))
                index += 1
            jobs[(over, name)] = futures
        for (over, name), futures in jobs.items():
            frames = [f.result() for f in futures]
            checked += 1
            label = f"{name}{' (Over)' if over else ''}"
            if all(f == frames[0] for f in frames[1:]):
                dead.append(label)
                print(f"  DEAD {label}")
            elif args.verbose:
                print(f"  ok   {label}")

    print()
    if dead:
        print(f"sweep: {checked} parameters, {len(dead)} made no difference:")
        for entry in dead:
            print(f"  - {entry}")
        return 1
    print(f"sweep: {checked} parameters, all live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
