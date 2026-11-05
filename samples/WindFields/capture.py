"""Render the Wind Fields review views headlessly, and optionally the wind's motion.

    python samples/WindFields/capture.py                 # one PNG per view, in Captures/
    python samples/WindFields/capture.py --motion        # also an animated GIF and a difference image per view
    python samples/WindFields/capture.py --view "Grass Close" --motion --frames 16

Uses PlutoGERenderSceneShot (build it with `cmake --build out/build/<preset> --config Release --target
PlutoGERenderSceneShot`); set PLUTOGE_BUILD to its folder if it is not in the usual place. `--time` poses the wind at a
shader-clock time, so frames at successive times show the sway. PNG and GIF output needs Pillow; without it the PPMs
are kept.
"""
from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SCENE = "Assets/Scenes/WindFields.plutoscene"
VIEWS = ("Meadow", "Grass Close", "Tree Line")


def find_tool() -> Path:
    name = "PlutoGERenderSceneShot.exe" if os.name == "nt" else "PlutoGERenderSceneShot"
    candidates = [Path(os.environ["PLUTOGE_BUILD"]) / name] if "PLUTOGE_BUILD" in os.environ else []
    engine = ROOT.parents[1]
    candidates += sorted(engine.glob(f"out/build/*/tests/Release/{name}")) + sorted(engine.glob(f"out/build/*/tests/{name}"))
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit("PlutoGERenderSceneShot not found: build it, or set PLUTOGE_BUILD to its folder")


def render(tool: Path, view: str, out: Path, time: float, size: str) -> Path:
    command = [str(tool), str(ROOT), SCENE, str(out), view, "--time", f"{time:.3f}", "--size", size]
    subprocess.run(command, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    return out


def to_png(ppm: Path):
    try:
        from PIL import Image
    except ImportError:
        return None
    image = Image.open(ppm).convert("RGB")
    image.save(ppm.with_suffix(".png"))
    ppm.unlink()
    return image


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--view", action="append", choices=VIEWS, help="views to render (default: all)")
    parser.add_argument("--size", default="1280x720")
    parser.add_argument("--time", type=float, default=2.0, help="shader-clock time of the still frames")
    parser.add_argument("--motion", action="store_true", help="also render a sequence, a GIF and a difference image")
    parser.add_argument("--frames", type=int, default=12)
    parser.add_argument("--step", type=float, default=0.12, help="seconds between motion frames")
    args = parser.parse_args()
    tool = find_tool()
    captures = ROOT / "Captures"
    captures.mkdir(exist_ok=True)
    for view in args.view or VIEWS:
        stem = view.lower().replace(" ", "_")
        still = to_png(render(tool, view, captures / f"{stem}.ppm", args.time, args.size))
        print(f"{view}: {captures / (stem + '.png')}")
        if not args.motion:
            continue
        frames = []
        for index in range(args.frames):
            ppm = render(tool, view, captures / f"{stem}_{index:02d}.ppm", args.time + index * args.step, args.size)
            frames.append(to_png(ppm))
        if still is not None and all(frames):
            from PIL import ImageChops
            frames[0].save(captures / f"{stem}_motion.gif", save_all=True, append_images=frames[1:],
                           duration=int(args.step * 1000), loop=0)
            # Bright where anything moved between the first and middle frames: the plants, never the ground or rocks.
            difference = ImageChops.difference(frames[0], frames[len(frames) // 2]).convert("L")
            difference.point(lambda value: min(255, value * 4)).save(captures / f"{stem}_motion_difference.png")
            print(f"{view}: {captures / (stem + '_motion.gif')}")


if __name__ == "__main__":
    main()
