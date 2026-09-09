#!/usr/bin/env python3
"""Render the fixed visual-review frames for the master scenes.

Usage: python3 tools/review_frames.py [output_dir] [--build release] [--audio track.wav]

Renders each scene at the frames listed in docs/visual-quality.md, at 1920x1080, into
`output_dir/<scene>_<frame>.png`, so two runs can be compared side by side.
"""
import argparse
import pathlib
import subprocess
import sys

SCENES = {
    "Hyperspace": [150, 600, 1200, 1800],
    "The Infinite Temple": [300, 1200, 2400, 3600],
    "The Living Machine": [200, 900, 1800],
    "Metallic Reassembly": [90, 600, 1200, 1650],
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", nargs="?", default="review")
    parser.add_argument("--build", default="release")
    parser.add_argument("--audio", default="/tmp/track.wav")
    parser.add_argument("--size", default="1920x1080")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    binary = root / "build" / args.build / "src" / "avgen"
    if not binary.exists():
        print(f"no binary at {binary}; build it first", file=sys.stderr)
        return 1
    audio = pathlib.Path(args.audio)
    if not audio.exists():
        print(f"no audio at {audio}; run tools/make_test_audio.py first", file=sys.stderr)
        return 1
    out = pathlib.Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    failures = 0
    for scene, frames in SCENES.items():
        stem = scene.lower().replace(" ", "_")
        for frame in frames:
            target = out / f"{stem}_{frame:05d}.png"
            command = [str(binary), "--headless", "--example", scene, "--audio", str(audio),
                       "--frames", str(frame), "--fps", "30", "--size", args.size,
                       "--capture", str(target)]
            result = subprocess.run(command, capture_output=True, text=True)
            status = "ok" if result.returncode == 0 and target.exists() else "FAILED"
            if status != "ok":
                failures += 1
                print(result.stderr.strip()[-300:], file=sys.stderr)
            print(f"{scene} @{frame}: {status}")
    print(f"\n{sum(len(v) for v in SCENES.values()) - failures} frame(s) written to {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
