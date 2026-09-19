#!/usr/bin/env python3
"""Write the A/B arms that price the multicam's modernisation (ADR-344), into examples/world/.

The arms are *data*, not code: every one of them is the same engine binary reading a different
`glowmere-valley-2-multicam` project, so a difference between two of them cannot be a compiler
flag, a shader edit or a stale `src/avgen`. They are generated rather than committed because
twenty near-identical scene files is what the repository already decided not to carry
(f49a8718), and because the only thing worth keeping is the recipe that makes them.

    tools/make_mcperf_arms.py                 # every arm
    tools/make_mcperf_arms.py --clean         # remove them again

History arms, one per suspect commit, newest last:

    base      ca57a3a4~1   before any of it
    cast194   ca57a3a4     the cast at 1.94x
    wide50    4c8a1323     the 50%/20% dolly
    trees     570e41c2     valley 3's eight tree layers and the `decide` cast
    rebake    9433044d     the cut re-baked on corrected hero anchors
    farm      3dff0d28     slopeAlign/footprint/bodyRadius on the sixteen farm animals
    head      HEAD

Isolation arms, each the HEAD project with exactly one thing put back, so the history arms'
steps can be attributed to a cause rather than to a commit:

    oldtrees  HEAD with the pre-570e41c2 thirteen-layer scatter table
    nobake    HEAD with the camera bake removed -- no `cameraAimFollow`, no `cameraShotSpans`
              and none of the six director-owned camera tracks. This is byte-for-byte what the
              application writes after a viewport drag has handed the camera back
              (`releaseDirectedCamera`), which is the state the owner's file was found in.

Each arm's project points at its own scene copy, with the fingerprint recomputed, so the loader
resolves the scene by path and never by the relocation search.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
WORLD = REPO / "examples" / "world"
PROJ = "examples/world/glowmere-valley-2-multicam.json"
SCENE = "examples/world/glowmere-valley-2-multicam.scene.json"
PREFIX = "_mcperf-"

HISTORY = [
    ("base", "ca57a3a4~1"),
    ("cast194", "ca57a3a4"),
    ("wide50", "4c8a1323"),
    ("trees", "570e41c2"),
    ("rebake", "9433044d"),
    ("farm", "3dff0d28"),
    ("head", "HEAD"),
]

# The six targets `directedCameraTargets()` owns. A track on one of these is the director's and is
# what `releaseDirectedCamera` erases; `cameras/valleywide/*` is a different camera and survives.
DIRECTED_TRACKS = {
    "camera/position",
    "camera/target",
    "camera/mode",
    "camera/lens/focalLength",
    "camera/lens/aperture",
    "camera/lens/focusDistance",
}


def show(rev: str, path: str) -> bytes:
    out = subprocess.run(["git", "-C", str(REPO), "show", f"{rev}:{path}"], capture_output=True)
    if out.returncode != 0:
        sys.exit(f"git show {rev}:{path}: {out.stderr.decode().strip()}")
    return out.stdout


def write(tag: str, project: dict, scene_bytes: bytes) -> None:
    name = f"{PREFIX}{tag}.scene.json"
    (WORLD / name).write_bytes(scene_bytes)
    project["assets"]["scene"]["path"] = {
        "path": name,
        "sha256": hashlib.sha256(scene_bytes).hexdigest(),
        "size": len(scene_bytes),
    }
    (WORLD / f"{PREFIX}{tag}.json").write_text(json.dumps(project, indent=2))
    print(f"  {PREFIX}{tag}.json")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="delete the arms and exit")
    args = ap.parse_args()
    if args.clean:
        for p in sorted(WORLD.glob(f"{PREFIX}*")):
            p.unlink()
            print(f"  removed {p.name}")
        return

    print("history arms:")
    head_project, head_scene = None, None
    for tag, rev in HISTORY:
        project = json.loads(show(rev, PROJ))
        scene = show(rev, SCENE)
        write(tag, project, scene)
        if tag == "head":
            head_project, head_scene = json.loads(show(rev, PROJ)), scene

    print("isolation arms:")
    # oldtrees: HEAD in every respect but the terrain's scatter table.
    scene = json.loads(head_scene)
    old = json.loads(show("570e41c2~1", SCENE))
    old_scatter = next(n for n in old["nodes"] if n.get("kind") == "terrain")["scatter"]
    for node in scene["nodes"]:
        if node.get("kind") == "terrain":
            node["scatter"] = old_scatter
    write("oldtrees", json.loads(json.dumps(head_project)), json.dumps(scene, indent=2).encode())

    # treelod: HEAD with the distance gates the port flattened, put back.
    #
    # `570e41c2` copied valley 3's eight-layer table in wholesale, and one of the eight was a layer
    # this film already had: `deadwood` arrived carrying viewDistance 520 / minScreenRadius 1.0 over
    # the 380 / 1.6 it had been authored with. That was not a decision, it was what came with the
    # table. The four SECONDARY species -- the rungs ADR-344 gave the canopy *between* the tree line
    # and the floor, 5.4 to 7.1 m tall -- get the same treatment, because at 400 m a 5.4 m tree is
    # silhouette the three primary layers are already drawing. `canopy`, `twisted` and `pine-upper`
    # keep 520 / 1.0: they are the skyline the owner asked for.
    scene = json.loads(head_scene)
    gates = {
        "deadwood": (380.0, 1.6),
        "deadwood-rim": (380.0, 1.6),
        "canopy-broad": (420.0, 1.6),
        "twisted-low": (420.0, 1.6),
        "pine-rim": (420.0, 1.6),
    }
    for node in scene["nodes"]:
        if node.get("kind") != "terrain":
            continue
        for layer in node["scatter"]:
            if layer["name"] in gates:
                layer["viewDistance"], layer["minScreenRadius"] = gates[layer["name"]]
    write("treelod", json.loads(json.dumps(head_project)), json.dumps(scene, indent=2).encode())

    # nobake: HEAD with everything `releaseDirectedCamera` erases, erased.
    project = json.loads(json.dumps(head_project))
    project.pop("cameraAimFollow", None)
    project.pop("cameraShotSpans", None)
    tracks = project.get("timeline", {}).get("tracks", [])
    project["timeline"]["tracks"] = [t for t in tracks if t.get("target") not in DIRECTED_TRACKS]
    write("nobake", project, head_scene)


if __name__ == "__main__":
    main()
