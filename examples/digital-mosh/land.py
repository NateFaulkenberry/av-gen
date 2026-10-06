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
    {"frequency": 0.0022, "amplitude": 16.0, "ridged": 0.0, "warp": 90.0},
    # dunes: softly crested, wind-drawn
    {"frequency": 0.011, "amplitude": 4.0, "ridged": 0.25, "warp": 26.0},
    # ripples
    {"frequency": 0.05, "amplitude": 0.45, "ridged": 0.0, "warp": 4.0},
    {"frequency": 0.21, "amplitude": 0.07, "ridged": 0.0, "warp": 0.0},
]

# The stage: a salt pan, level and glassy, the tree and the Tanguy object on it.
PAN = {"name": "pan", "kind": "flat", "path": [[4.0, 0.0, -8.0]], "width": 110.0, "falloff": 0.7,
       "flatten": 0.96, "roughness": 0.06, "smoothing": 0}
# The dry riverbed: enters the pan from the front left -- a line the camera can follow in.
RIVERBED = {"name": "riverbed", "kind": "valley",
            "path": [[-420.0, 0.0, 260.0], [-250.0, 0.0, 170.0], [-150.0, 0.0, 150.0], [-80.0, 0.0, 70.0],
                     [-30.0, 0.0, 42.0], [10.0, 0.0, 30.0], [40.0, 0.0, 6.0]],
            "width": 10.0, "amplitude": 1.6, "falloff": 1.3, "roughness": 0.25, "smoothing": 3}
NEAR_FEATURES = [PAN, RIVERBED]

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
         "baseHeight": 0.0, "erosion": 0.2, "seaLevel": -1000.0, "layers": copy.deepcopy(LAYERS),
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
