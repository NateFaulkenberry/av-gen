#!/usr/bin/env python3
"""Rainbow World: build every generated file the live scene needs, from the scripts in this folder.

    python3 examples/rainbow-world/build.py                 # everything (about 15-20 minutes)
    python3 examples/rainbow-world/build.py --scene-only    # reuse the built assets, rebuild the world

Needs Blender 5.x (default /Applications/Blender.app/Contents/MacOS/Blender, or --blender / $BLENDER) and the six
CC0 Poly Haven rocks in assets/terrain/rocks/ (tools/fetch_polyhaven.py; assets/rainbow-world.manifest.json lists
them). Nothing it writes is committed:

  build/rainbow-world/            intermediate work (adapted rocks, trees, the engine asset set)
  examples/rainbow-world/scene/   the generated scene: rainbow-world.scene.json and its GLBs (gitignored)

and it rewrites examples/rainbow-world/rainbow-world.json, the committed live project, which is a pure function of
the scripts (rebuilding it from the same scripts gives the same file).

Steps, each one Blender process:
  1. rocks       blender/rocks.py         the Poly Haven scans adapted to the painterly language (rocks.blend)
  2. assets      blender/engine_assets.py grass, flowers, the Great Tree, clouds, cloud floor, rainbow, rim rocks
  3. trees       blender/weeper.py, blossom.py, pine.py   the rest of the tree family
  4. world       build_world.py           the knot, the dressing, the ride, the scene and the live project
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
WORK = os.path.join(REPO, "build", "rainbow-world")
SCENE = os.path.join(HERE, "scene")
PROJECT = os.path.join(HERE, "rainbow-world.json")
ROCKS = ["boulder_01", "namaqualand_boulder_03", "rock_07", "rock_09", "rock_moss_set_01", "stone_01"]


def blender(exe, script, *args):
    cmd = [exe, "-b", "--factory-startup", "-P", os.path.join(HERE, script), "--", *args]
    print("+", " ".join(os.path.relpath(c, REPO) if c.startswith(REPO) else c for c in cmd), flush=True)
    env = dict(os.environ, RW_NO_PREVIEW="1")
    log = os.path.join(WORK, os.path.basename(script).replace(".py", ".log"))
    with open(log, "w") as f:
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=REPO)
    if r.returncode != 0:
        sys.exit("%s failed (exit %d); see %s" % (script, r.returncode, os.path.relpath(log, REPO)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scene-only", action="store_true", help="reuse build/rainbow-world/assets; rebuild the world")
    ap.add_argument("--blender", default=os.environ.get("BLENDER", "/Applications/Blender.app/Contents/MacOS/Blender"))
    a = ap.parse_args()
    if not os.path.exists(a.blender):
        sys.exit("Blender not found at %s (pass --blender or set $BLENDER)" % a.blender)
    missing = [r for r in ROCKS if not os.path.exists(os.path.join(REPO, "assets", "terrain", "rocks", r, r + "_1k.gltf"))]
    if missing and not a.scene_only:
        sys.exit("missing Poly Haven rocks %s: run tools/fetch_polyhaven.py (see assets/rainbow-world.manifest.json)"
                 % ", ".join(missing))
    assets = os.path.join(WORK, "assets")
    os.makedirs(assets, exist_ok=True)
    if not a.scene_only:
        blender(a.blender, "blender/rocks.py", os.path.join(WORK, "stage2", "rocks"), "64", "48", "1")
        blender(a.blender, "blender/engine_assets.py", assets, os.path.join(WORK, "stage2"))
        for tree in ("weeper", "blossom", "pine"):
            d = os.path.join(WORK, "trees", tree)
            blender(a.blender, "blender/%s.py" % tree, d, "64", "48", "1")
            for f in glob.glob(os.path.join(d, "*.glb")):
                shutil.copy(f, assets)
    elif not os.path.exists(os.path.join(assets, "great_tree_leaves.glb")):
        sys.exit("--scene-only needs a full build first (no assets in %s)" % os.path.relpath(assets, REPO))
    if os.path.isdir(SCENE):
        shutil.rmtree(SCENE)
    blender(a.blender, "build_world.py", SCENE, "--assets", assets, "--project", PROJECT)
    print("built %s and %s" % (os.path.relpath(SCENE, REPO), os.path.relpath(PROJECT, REPO)))


if __name__ == "__main__":
    main()
