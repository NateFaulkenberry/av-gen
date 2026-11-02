"""Ground heights for placement (M3b dev tool): runs the test binary's hidden sampler on a scene file.

    from sample_ground import heights; heights("strange-attractor-mars.scene.json", [(x, z), ...]) -> [h, ...]

The sampler is a temporary [.sample] test (not committed); the builders fall back to the heights they cached in
ground-cache.json, which is committed, so they run without it.
"""
import json
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(HERE, "ground-cache.json")


def heights(scene_file, points):
    path = os.path.join(HERE, scene_file)
    key_scene = json.load(open(path))
    world = key_scene.get("attractorWorld") or key_scene.get("lunarWorld")
    key = json.dumps(world, sort_keys=True)
    cache = json.load(open(CACHE)) if os.path.exists(CACHE) else {}
    entry = cache.setdefault(scene_file, {})
    if entry.get("world") != key:
        entry.clear()
        entry["world"] = key
        entry["h"] = {}
    want = [p for p in points if f"{p[0]:.3f},{p[1]:.3f}" not in entry["h"]]
    if want:
        binary = os.path.join(ROOT, "build", "release", "tests", "avgen_tests")
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump({"scene": path, "points": [list(p) for p in want]}, f)
            req = f.name
        subprocess.run([binary, "[.sample]"], env={**os.environ, "SAMPLE_REQ": req}, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        got = json.load(open(req + ".out"))
        for p, h in zip(want, got):
            entry["h"][f"{p[0]:.3f},{p[1]:.3f}"] = h
        json.dump(cache, open(CACHE, "w"), indent=0)
    return [entry["h"][f"{p[0]:.3f},{p[1]:.3f}"] for p in points]
