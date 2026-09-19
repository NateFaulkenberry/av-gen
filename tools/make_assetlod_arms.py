#!/usr/bin/env python3
"""Build the asset-LOD study's arms: one project per (view, ladder) over the Tree of Life.

ADR-344. §11 of the asset-LOD brief asks for validation at extreme closeup, hero, medium, wide and
small-in-frame, and §12 asks for before/after at the *same* camera positions. Both need a family of
projects that differ in exactly one thing, and building them by hand is how two arms come to differ
in two ways.

Each view scales the shipping camera's position about its target, so every arm looks at the same
place from the same direction at a different distance -- which is what makes the frames comparable.
The projected radius printed beside each one is the tree's bounding sphere at 1080p and fov 36, so
"hero" and "small in frame" are measurements rather than adjectives.

Each ladder is a `lod` block. `off` writes none, which is the before arm and is byte-identical to
the shipping scene. `auto` writes the default five rungs and lets the selector choose. The `r<n>`
ladders write a single rung, which is how a frame at exactly that rung is produced without a debug
switch in the renderer: with one rung below LOD0, the selector has one demotion available and takes
it at any size worth measuring.

    tools/make_assetlod_arms.py                 # every view x every ladder into examples/assetlod
    tools/make_assetlod_arms.py --list          # print what it would write
"""
import argparse
import copy
import json
import math
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(REPO, "examples", "treeisland")
OUT_DIR = os.path.join(REPO, "examples", "assetlod")
SOURCE_PROJECT = "tree-of-life-floating-island.json"
SOURCE_SCENE = "tree-of-life-floating-island.scene.json"

TREE_NODES = {"tree-wood", "tree-twigs", "tree-tracery", "tree-foliage", "tree-lumens"}
# The tree's world-space bounding sphere, from the five layers' glTF accessor bounds.
TREE_CENTRE = (-1.9, 37.3, -4.4)
TREE_RADIUS = 62.0

# name -> multiplier on the shipping camera's distance from its target.
VIEWS = {
    "closeup": 0.28,
    "hero": 1.0,
    "medium": 2.4,
    "wide": 6.0,
    "small": 20.0,
}

LADDERS = {
    "off": None,
    "auto": {"enabled": True},
    "r1": {"enabled": True, "ratios": [1.0, 0.5]},
    "r2": {"enabled": True, "ratios": [1.0, 0.2]},
    "r3": {"enabled": True, "ratios": [1.0, 0.07]},
    "r4": {"enabled": True, "ratios": [1.0, 0.02]},
    # The control §4 needs: the same ratios with thinning off, so "the foliage needed thinning" is a
    # pair of frames and not a claim.
    "r2-nothin": {"enabled": True, "ratios": [1.0, 0.2], "thinning": False},
}


def projected_radius(distance, height=1080.0, fov_degrees=36.0):
    """The tree's bounding sphere in pixels at that distance."""
    pixels_per_unit = height / (2.0 * math.tan(math.radians(fov_degrees) * 0.5))
    return pixels_per_unit * TREE_RADIUS / max(distance, 1e-3)


def build(view_name, ladder_name, write=True):
    project = json.load(open(os.path.join(SOURCE_DIR, SOURCE_PROJECT)))
    scene = json.load(open(os.path.join(SOURCE_DIR, SOURCE_SCENE)))

    camera = scene["camera"]
    target = camera["target"]
    position = camera["position"]
    offset = [position[i] - target[i] for i in range(3)]
    scale = VIEWS[view_name]
    camera["position"] = [target[i] + offset[i] * scale for i in range(3)]
    # The shipping camera looks at the island's origin, which frames the whole island. Every view
    # but the hero recentres on the tree, so a "wide" arm is a wide shot of the tree rather than of
    # whatever the island's origin happens to have under it. The hero keeps the framing that
    # shipped, because that is the shot §13 is about and it is not ours to second-guess.
    if view_name != "hero":
        camera["target"] = list(TREE_CENTRE)
        camera["position"] = [TREE_CENTRE[i] + offset[i] * scale for i in range(3)]

    ladder = LADDERS[ladder_name]
    for node in scene["nodes"]:
        if node["name"] in TREE_NODES and ladder is not None:
            node["lod"] = copy.deepcopy(ladder)

    name = f"tree-{view_name}-{ladder_name}"
    project["app"]["name"] = f"Tree of Life LOD study: {view_name} / {ladder_name}"
    project["assets"]["scene"]["path"] = f"{name}.scene.json"
    # The island's slow spin makes two arms rendered at the same timeline second comparable only if
    # nothing else moves it. The routes are what move it, and a study of geometry does not need
    # them: dropped, in every arm equally.
    project["routes"] = []
    project["sources"] = []
    project["parameters"] = {
        k: v for k, v in project["parameters"].items() if not k.startswith("sources/")
    }
    if write:
        with open(os.path.join(OUT_DIR, f"{name}.scene.json"), "w") as f:
            json.dump(scene, f, indent=1)
        with open(os.path.join(OUT_DIR, f"{name}.json"), "w") as f:
            json.dump(project, f, indent=1)
    distance = math.dist(camera["position"], camera["target"])
    return name, distance, projected_radius(distance)


# The dolly (§11: "camera moving toward", "camera moving away"). One project per step, rather than
# one project with an animated camera, and the reason is the measurement rather than convenience:
# with the clock frozen and only the camera moved, *any* difference between consecutive frames is
# the LOD change and nothing else. An animated camera would put the island's spin, the LFOs and the
# temporal post into every pair, and the pop would have to be separated from them.
#
# The range brackets the rung 1 -> rung 2 boundary, which the eight-pixel floor puts at about 520 m
# for this asset. `off` renders the same steps with no ladder at all: that pair-to-pair difference
# is the baseline a pop has to be seen against, because a camera that moves at all changes the frame.
DOLLY_STEPS = 13
DOLLY_NEAR = 420.0
DOLLY_FAR = 660.0


def build_dolly(index, ladder_name, write=True):
    project = json.load(open(os.path.join(SOURCE_DIR, SOURCE_PROJECT)))
    scene = json.load(open(os.path.join(SOURCE_DIR, SOURCE_SCENE)))
    camera = scene["camera"]
    offset = [camera["position"][i] - camera["target"][i] for i in range(3)]
    length = math.sqrt(sum(o * o for o in offset))
    unit = [o / length for o in offset]
    t = index / (DOLLY_STEPS - 1)
    distance = DOLLY_FAR + (DOLLY_NEAR - DOLLY_FAR) * t
    camera["target"] = list(TREE_CENTRE)
    camera["position"] = [TREE_CENTRE[i] + unit[i] * distance for i in range(3)]

    ladder = LADDERS[ladder_name]
    for node in scene["nodes"]:
        if node["name"] in TREE_NODES and ladder is not None:
            node["lod"] = copy.deepcopy(ladder)

    name = f"dolly-{ladder_name}-{index:02d}"
    project["app"]["name"] = f"Tree of Life LOD dolly {index} / {ladder_name}"
    project["assets"]["scene"]["path"] = f"{name}.scene.json"
    project["routes"] = []
    project["sources"] = []
    project["parameters"] = {
        k: v for k, v in project["parameters"].items() if not k.startswith("sources/")
    }
    if write:
        with open(os.path.join(OUT_DIR, f"{name}.scene.json"), "w") as f:
            json.dump(scene, f, indent=1)
        with open(os.path.join(OUT_DIR, f"{name}.json"), "w") as f:
            json.dump(project, f, indent=1)
    return name, distance, projected_radius(distance)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true", help="print the arms without writing them")
    args = ap.parse_args()
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"{'arm':34} {'distance':>10} {'radius px':>10}")
    for view in VIEWS:
        for ladder in LADDERS:
            name, distance, radius = build(view, ladder, write=not args.list)
            print(f"{name:34} {distance:10.1f} {radius:10.1f}")
    for ladder in ("auto", "off"):
        for index in range(DOLLY_STEPS):
            name, distance, radius = build_dolly(index, ladder, write=not args.list)
            print(f"{name:34} {distance:10.1f} {radius:10.1f}")


if __name__ == "__main__":
    main()
