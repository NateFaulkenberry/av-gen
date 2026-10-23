#!/usr/bin/env python3
"""The Living Prism: build every generated file the live scene needs, from the scripts in this folder.

    python3 examples/living-prism/build.py              # meshes (Blender, about 10 s) and the scene
    python3 examples/living-prism/build.py --scene-only # reuse the built meshes, rewrite the scene and project
    python3 examples/living-prism/build.py --preview    # also Workbench layout stills from the camera stations

Needs Blender 5.x (default /Applications/Blender.app/Contents/MacOS/Blender, or --blender / $BLENDER). No external
assets: every mesh is generated. Nothing it writes is committed except the project:
  build/living-prism/assets/      the meshes (and preview/ with --preview)
  examples/living-prism/scene/    the scene and its meshes (gitignored)
  examples/living-prism/living-prism.json   the live project, a pure function of the scripts
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
WORK = os.path.join(REPO, "build", "living-prism")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scene-only", action="store_true")
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--blender", default=os.environ.get("BLENDER", "/Applications/Blender.app/Contents/MacOS/Blender"))
    a = ap.parse_args()
    assets = os.path.join(WORK, "assets")
    os.makedirs(WORK, exist_ok=True)
    if not a.scene_only:
        if not os.path.exists(a.blender):
            sys.exit("Blender not found at %s (pass --blender or set $BLENDER)" % a.blender)
        cmd = [a.blender, "-b", "--factory-startup", "--python-exit-code", "1", "-P",
               os.path.join(HERE, "blender", "prism_assets.py"), "--", assets]
        if a.preview:
            cmd += ["--preview", os.path.join(WORK, "preview")]
        log = os.path.join(WORK, "prism_assets.log")
        with open(log, "w") as f:
            r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, cwd=REPO)
        if r.returncode != 0:
            sys.exit("prism_assets.py failed (exit %d); see %s" % (r.returncode, os.path.relpath(log, REPO)))
    elif not os.path.exists(os.path.join(assets, "shell.glb")):
        sys.exit("--scene-only needs a full build first (no meshes in %s)" % os.path.relpath(assets, REPO))
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_scene.py"), "--assets", assets], cwd=REPO)
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
