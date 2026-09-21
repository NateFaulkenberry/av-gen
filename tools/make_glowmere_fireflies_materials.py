#!/usr/bin/env python3
"""Write the two Glowmere firefly material programs from one op list.

    python3 tools/make_glowmere_fireflies_materials.py examples/materials

Why two files and not one. `localPosition` is in the asset's own units, and the eight tree assets
disagree about what a unit is (0.35 to 1.10 m) and about where their leaves start (2.1 to 5.8
units up), so one foliage band and one spot size cannot fit all of them. Why two and not eight:
the GPU holds eight material programs (`MAT_MAX_PROGRAMS`), `glowmere-valley-2-multicam` already
used six, and a ninth is dropped with one log line and renders as no program at all. So the trees
are grouped by scale -- `crown` for the ~1 m/unit broadleaf and pines, `scaled` for everything
normalised down -- and the band of each group starts above the lowest leaf of every tree in it,
which `test_glowmere_tree_fireflies.cpp` checks against the meshes' real vertices.

Why a generator. The two files must not differ in anything but the four numbers that are about
their meshes: the firefly gate, the flash and the colour have to be the same effect on every tree,
and two hand-edited 300-line JSON files will not stay that way.

What the program does. `gate` restricts it to trees whose `instanceRandom.w < P`, 30-340 m from the
camera; every other tree, and every fragment outside the band, takes the program-less path exactly (docs/procedural-materials.md, "The instance gate")
-- the same pixels as before this existed, at the same cost bar four derivatives the draw takes
anyway. On the trees it runs on it leaves the base emission alone (output -1), so the tree's own
glow is kept, and adds one layer: drifting voronoi points in the foliage band, faded in past the
particle swarms' reach (30-50 m) and out before a point is under a pixel (180-340 m), flashing on
fly-chorus's oscillator (1.6 Hz, sharpness 9, depth 0.94, a per-tree phase spread of 0.12 of a
period for pulseSync 0.88) in fly-chorus's two colours. The per-tree phase and colour read
instanceRandom.x, which wind also reads (flutter phase); the gate reads .w, which nothing else
does, so whether a tree carries fireflies is independent of how it moves.
"""
import json
import sys
FAM = {
 # name: (program name, voronoi freq [1/asset unit], gate lo, gate hi [asset units], drift scale)
 # crown:  CommonTree_1 (leaves from 2.35 u, 1.10 m/u), Pine_1 (2.16 u, 0.93), Pine_3 (3.19 u, 1.08)
 # scaled: CommonTree_4 (5.81 u, 0.75), TwistedTree_2 (4.49 u, 0.42), TwistedTree_4 (2.13 u, 0.35),
 #         DeadTree_1 (9.2 u tall, 0.68), DeadTree_4 (12.4 u tall, 0.42) -- the upper branches
 "crown":  ("glowmereFirefliesCrown",  2.2, 3.2, 3.9, 1.0),
 "scaled": ("glowmereFirefliesScaled", 1.1, 5.9, 6.7, 0.55),
}
# instanceRandom.w below this carries fireflies: (1/3) / (1 - emissiveSparsity 0.14), so the lit trees it
# lands on are one in three of all trees. The tree-fireflies particle node's scatterAnchor.randomBelow
# must be the same number; the test checks the two agree instance for instance.
P = 0.3876
def ops(freq, lo, hi, s):
    o = []
    def op(kind, dst, **kw):
        d = {"kind": kind, "dst": dst}; d.update(kw); o.append(d)
    # r0 position, r1 instanceRandom, r2 time; r3 accumulates the mask
    op("input", 0, input="localPosition")
    op("input", 1, input="instanceRandom")
    op("input", 2, input="time")
    # drifting points: the voronoi field slides through the crown, offset per tree by instanceRandom
    # (up to one asset unit, one to two cells -- every tree's pattern is its own)
    op("constant", 4, constant=[0.035 * s, 0.05 * s, 0.028 * s, 0.0])
    op("multiply", 4, srcA=2, srcB=4)
    op("add", 4, srcA=4, srcB=1)
    op("add", 4, srcA=0, srcB=4)
    op("voronoi", 4, srcA=4, value=freq, seed=91)
    op("smoothstep", 4, srcA=4, constant=[0.26, 0.02, 0.0, 0.0])  # descending: 1 at a cell's point
    # the foliage band, from the undrifted height
    op("swizzle", 3, srcA=0, constant=[1, 1, 1, 1])
    op("smoothstep", 3, srcA=3, constant=[lo, hi, 0.0, 0.0])
    op("multiply", 3, srcA=3, srcB=4)
    # distance: in past the swarms' reach, out before a point is smaller than a pixel
    op("input", 5, input="cameraDistance")
    op("smoothstep", 6, srcA=5, constant=[30.0, 50.0, 0.0, 0.0])
    op("smoothstep", 5, srcA=5, constant=[340.0, 180.0, 0.0, 0.0])
    op("multiply", 5, srcA=5, srcB=6)
    op("multiply", 3, srcA=3, srcB=5)
    # the flash: fly-chorus's oscillator. palette(t) = 0.5 + 0.5 cos(2 pi (1.6 t - 0.25))
    # = 0.5 + 0.5 sin(2 pi 1.6 t), the particles' own phase; each tree is 0.12 of a period off it
    # at most (pulseSync 0.88), then sharpness 9 and depth 0.94 as pulseGain has them.
    op("constant", 6, constant=[0.075, 0.075, 0.075, 0.075])
    op("multiply", 5, srcA=1, srcB=6)
    op("add", 5, srcA=2, srcB=5)
    op("palette", 5, srcA=5, value=0.0, constant=[0.5, 0.5, 0.5, 0.0], constant2=[0.5, 0.5, 0.5, 0.0],
       constant3=[1.6, 1.6, 1.6, 0.0], constant4=[-0.25, -0.25, -0.25, 0.0])
    op("power", 5, srcA=5, value=9.0)
    op("remap", 5, srcA=5, constant=[0.0, 1.0, 0.06, 1.0], value=1.0)
    op("multiply", 3, srcA=3, srcB=5)
    # colour: fly-chorus's two ends, picked per tree
    op("constant", 6, constant=[1.0, 0.92, 0.42, 1.0])
    op("constant", 7, constant=[0.75, 1.0, 0.35, 1.0])
    op("mixBy", 7, srcA=6, srcB=7, srcC=1)
    return o
out_dir = sys.argv[1] if len(sys.argv) > 1 else "examples/materials"
for fam, (name, freq, lo, hi, s) in FAM.items():
    prog = {
      "name": name,
      "_note": ("Glowmere fireflies, " + fam + " family, written by "
                "tools/make_glowmere_fireflies_materials.py -- edit that, not this. gate: the "
                f"program runs only on trees whose instanceRandom.w < {P} -- (1/3) / (1 - "
                "emissiveSparsity 0.14), so the lit trees it lands on are one in three of all trees, "
                "the same test that picks the trees the 'tree-fireflies' swarms orbit -- and every "
                "other tree, and every fragment nearer than 30 m or past 340 m, takes the program-less "
                "path exactly. On the trees it runs on it leaves the "
                "base emission alone (register -1) and adds one layer of drifting, flashing points in "
                "the foliage band. Positions, frequency and the band are in the asset's own units."),
      # the band matches the program's own fades (in 30-50 m, out 180-340 m), so crossing an edge
      # of it changes nothing on screen -- it only stops paying for a program that draws nothing
      "gate": {"lane": 3, "below": P, "near": 30.0, "far": 340.0},
      "ops": ops(freq, lo, hi, s),
      "layers": [{"name": "fireflies", "ops": [], "mask": 3, "height": -1, "blendRange": 1.0,
                  "baseColor": -1, "metallic": -1, "roughness": -1, "emission": 7,
                  "emissionIntensity": 16.0, "normal": -1, "occlusion": -1}],
      "emission": -1, "emissionIntensity": 1.0, "baseColor": -1, "metallic": -1, "roughness": -1,
      "opacity": -1, "normal": -1, "occlusion": -1,
    }
    with open(f"{out_dir}/glowmere-fireflies-{fam}.material.json", "w") as f:
        json.dump(prog, f, indent=1); f.write("\n")
    print(fam, len(prog["ops"]))
