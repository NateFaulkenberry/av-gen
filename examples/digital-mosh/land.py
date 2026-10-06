"""DIGITAL MOSH: the land -- a vast, sculpted, Dali/Tanguy desert, as two terrain nodes over one geography.

Imported by build.py. The geography is the brief's §3 in ground: a pale salt pan for the stage (Dali's beach, a floor
for long shadows), a dry riverbed that meanders into it (a leading line, and the riverbed the dream will later flood
with ink), long low swells and dunes, a Cap de Creus escarpment and two eroded mesas at the edge of the pan's world,
and ranges dissolving into the haze kilometres away.

Two nodes, one description:
  near  900 m, 1.25 m quads -- everything the moving camera travels over;
  far   8 km, 12.5 m quads  -- the escarpment, the mesas and the ranges; under the near land it is cut down into a
        basin (a `flat` feature 25 m below), and it sits 0.8 m lower, so the two never fight.
Heights for placing things are asked of the engine (tools/gv3/ground.py -> avgen_world_preview), never re-derived.
"""
from __future__ import annotations

import copy
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "gv3"))

NEAR_SIZE = 900.0
FAR_SIZE = 8000.0

LAYERS = [
    # long low swells: the land breathes over hundreds of metres
    {"frequency": 0.0022, "amplitude": 26.0, "ridged": 0.0, "warp": 320.0},
    # dune fields: the swells broken into long rises a few hundred metres apart
    {"frequency": 0.0055, "amplitude": 16.0, "ridged": 0.35, "warp": 170.0},
    # dunes: crested, wind-drawn, tall enough to throw a shadow across the next one in a low sun
    {"frequency": 0.013, "amplitude": 24.0, "ridged": 0.35, "warp": 60.0},
    # ripples
    {"frequency": 0.05, "amplitude": 0.5, "ridged": 0.0, "warp": 4.0},
    {"frequency": 0.21, "amplitude": 0.07, "ridged": 0.0, "warp": 0.0},
]

# The stage: a small salt pan, level and glassy, the Tanguy object hovering on it (Tanguy's objects stand on a floor).
PAN = {"name": "pan", "kind": "flat", "path": [[4.0, 0.0, -8.0]], "width": 60.0, "falloff": 0.5,
       "flatten": 1.0, "roughness": 0.06, "smoothing": 0}
# The olive stands on a knoll on the riverbed's bank, above the pan (the owner: on a feature, not on the flat).
KNOLL_XZ = (-34.0, 12.0)
KNOLL = {"name": "knoll", "kind": "ridge", "path": [[KNOLL_XZ[0], 0.0, KNOLL_XZ[1]]], "width": 26.0,
         "amplitude": 6.5, "falloff": 1.3, "roughness": 0.4}
# The dry riverbed: comes in from the front left, cuts past the knoll's foot and spills into the pan -- a deep
# leading line the camera can travel along.
RIVERBED = {"name": "riverbed", "kind": "valley",
            "path": [[-420.0, 0.0, 260.0], [-250.0, 0.0, 170.0], [-150.0, 0.0, 120.0], [-84.0, 0.0, 62.0],
                     [-62.0, 0.0, 34.0], [-52.0, 0.0, 10.0], [-38.0, 0.0, -10.0], [-20.0, 0.0, -20.0]],
            "width": 9.0, "amplitude": 4.2, "falloff": 1.1, "roughness": 0.2, "smoothing": 3}
# The middle ground: Cap de Creus brought close -- an eroded escarpment wall 200 m behind the pan, and mesas and a
# butte at 130-160 m, where the camera sees their height and their shadows.
ESCARPMENT = [
    {"name": "escarp", "kind": "ridge",
     "path": [[-340.0, 0.0, -110.0], [-190.0, 0.0, -170.0], [-40.0, 0.0, -195.0], [120.0, 0.0, -205.0],
              [300.0, 0.0, -150.0]],
     "width": 70.0, "amplitude": 34.0, "falloff": 2.0, "roughness": 0.7, "smoothing": 3},
    {"name": "escarpTop", "kind": "flat",
     "path": [[-340.0, 30.0, -125.0], [-190.0, 30.0, -185.0], [-40.0, 30.0, -210.0], [120.0, 30.0, -220.0],
              [300.0, 30.0, -165.0]],
     "width": 46.0, "falloff": 3.0, "flatten": 0.85, "roughness": 0.3},
]
MESAS = [
    {"name": "mesaE", "kind": "ridge", "path": [[140.0, 0.0, -50.0], [168.0, 0.0, -20.0]], "width": 34.0,
     "amplitude": 24.0, "falloff": 3.2, "roughness": 0.6},
    {"name": "mesaEtop", "kind": "flat", "path": [[140.0, 21.0, -50.0], [168.0, 21.0, -20.0]], "width": 28.0,
     "falloff": 3.0, "flatten": 1.0, "roughness": 0.15},
    {"name": "butteW", "kind": "ridge", "path": [[-130.0, 0.0, -58.0]], "width": 22.0, "amplitude": 17.0,
     "falloff": 3.2, "roughness": 0.6},
    {"name": "butteWtop", "kind": "flat", "path": [[-130.0, 15.0, -58.0]], "width": 16.0, "falloff": 3.0,
     "flatten": 1.0, "roughness": 0.15},
    {"name": "mesaS", "kind": "ridge", "path": [[110.0, 0.0, 95.0], [150.0, 0.0, 120.0]], "width": 30.0,
     "amplitude": 13.0, "falloff": 3.2, "roughness": 0.6},
    {"name": "mesaStop", "kind": "flat", "path": [[110.0, 11.0, 95.0], [150.0, 11.0, 120.0]], "width": 24.0,
     "falloff": 3.0, "flatten": 1.0, "roughness": 0.15},
]
NEAR_FEATURES = [PAN, KNOLL, RIVERBED] + ESCARPMENT + MESAS

FAR_ONLY = [
    # the escarpment (Cap de Creus): a long eroded wall to the north-west
    {"name": "escarpment", "kind": "ridge",
     "path": [[-1300.0, 0.0, -200.0], [-900.0, 0.0, -650.0], [-560.0, 0.0, -820.0], [-200.0, 0.0, -980.0]],
     "width": 130.0, "amplitude": 70.0, "falloff": 2.2, "roughness": 1.4, "smoothing": 3},
    # two mesas to the north-east: flat-topped, sharp-lipped, eroded below
    {"name": "mesaA", "kind": "ridge", "path": [[620.0, 0.0, -700.0], [700.0, 0.0, -760.0]], "width": 150.0,
     "amplitude": 60.0, "falloff": 6.0, "roughness": 1.3},
    {"name": "mesaAtop", "kind": "flat", "path": [[620.0, 56.0, -700.0], [700.0, 56.0, -760.0]], "width": 120.0,
     "falloff": 4.0, "flatten": 1.0, "roughness": 0.15},
    {"name": "mesaB", "kind": "ridge", "path": [[880.0, 0.0, -380.0], [960.0, 0.0, -280.0]], "width": 95.0,
     "amplitude": 36.0, "falloff": 6.0, "roughness": 1.3},
    {"name": "mesaBtop", "kind": "flat", "path": [[880.0, 34.0, -380.0], [960.0, 34.0, -280.0]], "width": 75.0,
     "falloff": 4.0, "flatten": 1.0, "roughness": 0.15},
    # the ranges, far: they are colour, not detail
    {"name": "rangeN", "kind": "ridge",
     "path": [[-3500.0, 0.0, -2600.0], [-1500.0, 0.0, -2900.0], [600.0, 0.0, -3100.0], [2600.0, 0.0, -2500.0],
              [3600.0, 0.0, -1200.0]],
     "width": 700.0, "amplitude": 260.0, "falloff": 1.4, "roughness": 1.8, "smoothing": 3},
    {"name": "rangeS", "kind": "ridge",
     "path": [[-3600.0, 0.0, 1800.0], [-1200.0, 0.0, 2900.0], [1800.0, 0.0, 3000.0]],
     "width": 600.0, "amplitude": 150.0, "falloff": 1.4, "roughness": 1.6, "smoothing": 3},
    # the near land's place: cut down, so the far mesh is never above the near one
    {"name": "under", "kind": "flat", "path": [[0.0, -25.0, 0.0]], "width": 445.0, "falloff": 0.35,
     "flatten": 1.0, "roughness": 0.0},
]


def world(far=False):
    w = {"name": "dream-far" if far else "dream", "seed": 20261005, "size": [FAR_SIZE if far else NEAR_SIZE] * 2,
         "baseHeight": 11.0, "erosion": 0.2, "seaLevel": -1000.0, "layers": copy.deepcopy(LAYERS),
         "features": copy.deepcopy(NEAR_FEATURES if not far else NEAR_FEATURES + FAR_ONLY), "biomes": []}
    # NB: both lands must share every layer, or they would disagree at the near land's edge; the ranges get their
    # roughness from their features' `roughness` (which multiplies the base noise inside them) instead.
    return w


_ground = None


def ground():
    """Engine-backed height queries over the NEAR land (where everything is placed)."""
    global _ground
    if _ground is None:
        from ground import Ground  # tools/gv3/ground.py
        _ground = Ground(world(False))
    return _ground


def height(x, z):
    return ground().height(x, z)


def heights(points):
    return [h for h, _w in ground().probe(points)]
