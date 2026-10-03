#!/usr/bin/env python3
"""live_scene_profile: the agent-facing wrapper around `avgen --live-profile` (ADR-1090).

Runs the profiler on one scene, prints its human report, and prints the JSON record's path as the last line
(`LIVE_PROFILE_JSON <path>`) so a calling agent can read the machine-readable record (schema avgen.liveprofile/1).

    tools/live_scene_profile.py examples/world/glowmere-valley-2.json --target-fps 60
    tools/live_scene_profile.py examples/liminal/all-you-got.json --start 60 --mode live --verify-candidates 3

Every GPU run goes through tools/gpu-lock.sh (one job per lock hold), so two agents never measure at once.
Estimated savings and measured savings stay separate fields in the record; read docs/live-optimizer/ for what
each number means. Results are specific to the machine they were taken on.

Exit code: the profiler's own (0 = profiled, no GPU errors).
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("scene", help="a project (.json) to profile")
    p.add_argument("--mode", choices=["headless", "live"], default="headless")
    p.add_argument("--target-fps", type=float, default=60.0)
    p.add_argument("--size", default="1920x1080", help="the output in pixels")
    p.add_argument("--start", type=float, default=0.0, help="seconds into the piece")
    p.add_argument("--warmup", type=float)
    p.add_argument("--measure", type=float)
    p.add_argument("--deep", action="store_true")
    p.add_argument("--quality", default="auto", help="auto | ultra..emergency | quality | balanced | performance")
    p.add_argument("--camera")
    p.add_argument("--no-audio", action="store_true")
    p.add_argument("--midi")
    p.add_argument("--capture", help="a PNG of the last measured frame")
    p.add_argument("--json", help="where to write the record (default: a temporary file)")
    p.add_argument("--verify-candidates", type=int, default=0)
    p.add_argument("--binary", default=os.path.join(REPO, "build", "release", "src", "avgen"))
    p.add_argument("--no-lock", action="store_true", help="do not take tools/gpu-lock.sh (the caller holds it)")
    a = p.parse_args()

    json_path = a.json or os.path.join(tempfile.gettempdir(), f"liveprofile-{os.getpid()}-{int(time.time())}.json")
    cmd = [a.binary, "--live-profile", "--project", a.scene, "--mode", a.mode, "--target-fps", str(a.target_fps),
           "--size", a.size, "--start", str(a.start), "--quality", a.quality, "--json", json_path]
    if a.warmup is not None:
        cmd += ["--warmup", str(a.warmup)]
    if a.measure is not None:
        cmd += ["--measure", str(a.measure)]
    if a.deep:
        cmd.append("--deep")
    if a.camera:
        cmd += ["--camera", a.camera]
    if a.no_audio:
        cmd.append("--no-audio")
    if a.midi:
        cmd += ["--midi", a.midi]
    if a.capture:
        cmd += ["--capture", a.capture]
    if a.verify_candidates > 0:
        cmd += ["--verify-candidates", str(a.verify_candidates)]
    if not a.no_lock:
        cmd = [os.path.join(REPO, "tools", "gpu-lock.sh")] + cmd
    # stdout is the report; stderr is the engine's log, kept out of the way.
    proc = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr[-4000:])
    print(f"LIVE_PROFILE_JSON {json_path}")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
