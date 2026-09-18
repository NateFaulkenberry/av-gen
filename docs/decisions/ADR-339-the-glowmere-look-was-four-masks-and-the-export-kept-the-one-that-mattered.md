# ADR-339: The Glowmere look was four masks, and the export kept the one that mattered

Status: accepted
Date: 2026-09-18
Supersedes the material half of ADR-338; its placement and import findings stand except where
corrected in §3 below.

## Context

ADR-338 stood the Tree of Life up on the floating island and shipped it. Looking at the result
next to `~/Desktop/tree_mdl/hero_pass/renders/Glowmere.png`, three things were wrong: the canopy
was flat and uniformly cyan where the reference has cream-white breaking through teal, the tree
read as floating rather than planted, and the background was black space with stars.

ADR-338 read the palette **off a render**. That was the right move with the information it had --
it had established that the GLB's seven materials are flat factors and that a scene cannot
override them -- and it produced seven better flat factors. The trouble is that no number of flat
factors can hold a gradient, and none at all can hold "6% of the leaves are lit and the other 94%
are not".

## What the .blend actually says

Both `Tree_of_Life_Hero.blend` and `Tree_Glowmere.blend` were read with Blender 5.2.1 headless.

**First finding, and it corrects the brief that sent us: the four .blend files are not four looks.**
`Tree_Glowmere.blend` and `Tree_of_Life_Hero.blend` are byte-identical in content -- every object,
every material, every node, every socket default, every colour-ramp stop, the world and all three
lights. A full structural diff of the two is six lines long and all six are the filename in the
header. The presets in `validation.json` are render-time settings of the `Value` nodes labelled
*Magic*, *Bioluminescence*, *Natural / Tree of Life*, *Vein brightness* and *Root glow*, all of
which are saved at 1.0 -- which **is** the Glowmere state. There is no separate Glowmere source to
recover. Either file is the source.

**Second finding: the Glowmere look is not a palette. It is four masks.**

| GLB mesh | Blender material | emission | mask |
|---|---|---|---|
| 3, 4 leaves | `Leaves \| jade, chartreuse and moonlit tips` | `[0.06, 0.45, 0.70]` | `(ObjectInfo.Random > 0.94) * 1.1 + 0.035` |
| 2 tracery | `HERO \| light beneath fractured bark` | height ramp on `z/43` | `noise(pos, 2.6, detail 3) > 0.53`, strength `1.6 * 1.2` |
| 0, 1 fungi | `HERO \| copper caps and pale luminous gills` | `[0.05, 0.65, 0.32]` | `Normal.z < -0.25` -- the gills, not the caps |
| 5, 6 wood | `GLOWMERE \| mineral charcoal, old plates and sapwood` | **strength 0.0** | none; this is the dark that makes the rest read |

The leaf base colour is also masked: `ramp01(clamp((z - 20)/28) + ObjectInfo.Random * 0.23)`,
running from a dark teal-green at the bottom of the canopy to olive on the broadleaf mesh and to a
warm terracotta on the peripheral one. That vertical warm/cool gradient is the tonal depth the
first pass lost, and the per-instance random is the scattered speck.

One more correction worth writing down: the wood material has a second, *earlier* chain whose
colour ramp is multiplied by `[0.38, 0.46, 0.5]`, a cool blue. It is tempting to read that as the
source of the reference's mauve-grey bark. Its output socket is not connected to anything. It is
dead, and the bark colour comes from `Color Ramp.001` via `Mix (Legacy).001`.

## Decision

### 1. `ObjectInfo.Random` survived the export, and recovering it is the whole pass

`ObjectInfo.Random` is per *instance*. The hero GLB's leaf meshes turn out to be contiguous
instance blocks: mesh 3 is 51,633 leaves of 18 vertices / 6 triangles each and mesh 4 is 122,767
of the same, and **every triangle lies inside one block** -- checked, not assumed, and re-checked
at build time so the tool fails loudly if a future export changes. So `instance = vertex // 18`
recovers exactly the grouping Blender randomised over.

`tools/glowmere_tree_layers.py` therefore bakes the masks into glTF materials, one material per
bucket, instead of averaging them into one factor per mesh. Leaves become 6 height bands x
{lit, dim}; tracery 4 height bands x {lit, dark}; fungi {cap, gills}; wood 4 noise bands.

The two noise masks (tracery, and the twig glow) do not reimplement Blender's Perlin. A value
noise of the same spatial frequency is used and its **threshold is calibrated to the same
coverage fraction**, which is the property the picture depends on. The coverage achieved is
measured, printed, and recorded in the manifest, so the approximation can be audited rather than
believed: tracery 0.470 against a target of 0.47, leaves 5.9% and 6.1% against Blender's 6%.

### 2. Five files, because `emissiveBoost` is per node and scalar

`material/emissiveBoost` and `material/roughnessScale` remain the only material levers on a
`kind: "gltf"` node, and both are scalar and whole-instance. Splitting the tree by *emission role*
turns that one scalar into the five separate channels the brief's §8 hierarchy and §10 parameter
list ask for, with no engine change and nothing added to `src/`:

```
world-root
└── floating-island          <- the LFOs drive this, unchanged
    ├── island-surface
    └── tree-of-life         <- one group: the hierarchy of §16 is intact
        ├── tree-tracery     nodes/tree-tracery/emissiveBoost   major luminous structures
        ├── tree-twigs       nodes/tree-twigs/emissiveBoost     secondary glow
        ├── tree-foliage     nodes/tree-foliage/emissiveBoost   foliage
        ├── tree-lumens      nodes/tree-lumens/emissiveBoost    special organisms
        └── tree-wood        (non-emissive by construction)
```

**The alternative that was measured and rejected.** ADR-044 gives a `kind: "procedural"` node with
a mesh source one object *per material part*, with per-part `tint`, `emissiveGain`,
`roughnessScale`, `opacityScale` and -- crucially -- `emissiveColor`, which the code comments note
"can light a lamp whose glTF emissiveFactor is [0,0,0]". That is a genuinely better mechanism than
`emissiveBoost` and it is the right answer for a small asset. It was not taken here for two
reasons. It routes 3.16 M triangles through the procedural instancing, LOD and GPU-cull path for
an object that has exactly one instance. And the baked masks produce 43 material parts, so §10's
four channels would be spread across 43 part indices instead of four named nodes -- the control
surface gets worse, not better, exactly where the brief wants it clean. If the tree is ever
re-exported at a lower triangle count, revisit this.

Splitting costs draw calls: 9 entities become 45. Measured below; it is not where the time goes.

**Vertex data is copied byte for byte.** POSITION and NORMAL bufferViews are memcpy'd and their
SHA-256 re-checked *after* the files are written. Only index buffers are rewritten, and the check
there is that the triangle count is conserved: 3,162,186 in, 3,162,186 out.

### 3. Placement: the previous numbers were right, mine were wrong first, and the defect was smaller than reported

ADR-338's root footprint -- x [-20.21, 17.72], z [-14.87, 18.86] -- reproduces exactly. So does
the 56x island scale and the tree at `[2.0, 3.2, 1.0]`.

My own first measurement said **24.4% of root vertices were over no island at all**, a hard
violation of the brief's §13. It was wrong. It built the island's top surface by binning island
*vertices* into cells and taking the max, and the island's grass mesh is only 7,467 triangles, so
at a 1-unit cell most cells are empty and every root above one of them reads as "off the island".
Rasterising the island's triangles instead and interpolating the height gives **0.0% off-island,
before and after**. §13 was already satisfied and the brief's concern was unfounded.

What is real is the float and the overhang, both measured per root cell against the rasterised
surface (the lowest root point in each 2.5-unit cell, against the island's top there):

| | before | after |
|---|---|---|
| root cells off the island | 0.0% | 0.0% |
| median gap, root tip to surface | **+0.78** | **-0.30** |
| root cells more than 1 unit clear | 46.0% | 15.0% |
| root cells embedded (gap <= 0) | 8.0% | 58.4% |
| canopy overhang (leaf r99 / island r max) | **1.64** | **1.15** |
| leaf vertices beyond the island's radius | 46.3% | 8.9% |

The remaining 15% that stand clear are the apexes of the root cathedral, which is what a root
cathedral is; the close-up in `renders/treeisland/contact/` is the check that matters.

Two changes got there: island scale 56 -> 80 (with its recentring translate scaled with it,
9.16 -> 13.086) and the tree lowered 3.2 -> 3.0. **Scaling the island up rather than the tree
down** is deliberate -- §14 forbids shrinking the tree, and the previous brief warned against the
island reading as "a tiny floating plate". The tree's x and z were left alone: a sweep over a 3x4
grid of positions moved the overhang only between 1.144 and 1.168, so moving it buys nothing and
costs a diff.

### 4. The cosmos is a background shader, because large point sprites are squares

The first attempt built the brief's §18 haze layers out of procedural point clouds with a large
`pointSize`. Procedural points are opaque camera-facing quads with no alpha falloff, so a
`pointSize` of 118 is a 118-unit **square**, and the frame came back as a grid of flat rectangles
-- §21's "obvious particle clouds", achieved literally. Even `pointSize` 3.4 reads as a square at
1080p. Every point layer in the scene is now <= 1.45 and varies by *brightness*, not size.

The haze is `shaders/glowmere-cosmos.wgsl`, a background user-shader layer (milestone 0.4): deep
indigo base, two warped fbm fields in cyan and indigo, a stretched violet filament, two distant
diffuse regions, a soft halo behind the island, and an edge falloff. Stars stay as scene geometry
at three depths so that the island occludes them; a star drawn in a background shader is a star
that shines through the rock.

Two mistakes in that shader are worth recording because both produced a *plausible* frame:

- The hash was `fract(p * vec2(127.1, 311.7))` before the dot product instead of after. For
  integer lattice coordinates that leaves almost no entropy, the fbm came out nearly constant,
  and the frame was a smooth gradient that looked like a deliberately subtle background.
- The masks were `smoothstep(0.42, 0.92, fbm)`. Five octaves of value noise at amplitudes
  0.5, 0.25, ... sum to a field centred near 0.48 with a standard deviation around 0.16, so that
  selects roughly the top 0.3% of it. The thresholds now straddle the distribution.

Neither failed. Both rendered. The frame was simply flat, and "subtle" is what a broken
background shader looks like.

### 5. The cream-white in the reference is a light, not a material

The single most-missed feature of the reference -- pale cream highlights through the teal -- is
not emission and not base colour. `Tree_of_Life_Hero.blend` lights the tree with three area
lights: a **warm canopy key** at `(1.00, 0.85, 0.67)` and 43,000 W, a **cool silhouette rim** at
`(0.32, 0.72, 0.66)` and 44,000 W, and a neutral trunk reveal. The scene had a single cool key at
`(0.80, 0.88, 1.00)`, which is why the canopy came back uniformly cyan however the materials were
tuned.

The key is now `(1.00, 0.92, 0.78)` on its original overhead direction, so it stays celestial
rather than becoming a studio lamp (§23), plus a teal rim at 0.85 that also lifts the island's
underside off black (§22) and an indigo fill at 0.30.

## Consequences

The tree is five files instead of one, and five nodes instead of one. That is the price of the
control channels and it is paid in the scene file, not in `src/` -- **this pass changes no engine
code at all.**

`assets/treeisle/` is gitignored, so the five GLBs must be regenerated at merge:

```
/Applications/Blender.app/Contents/Resources/5.2/python/bin/python3.13 \
    tools/glowmere_tree_layers.py --in assets/treeisle/tree-of-life-hero.glb --out assets/treeisle
```

`tools/glowmere_tree_materials.py` is kept, not deleted: `examples/treeisland/_compare-original.*`
is ADR-338 unchanged and still loads the single-file variant, and it is the "before" arm of every
comparison here.

The comparison arms are derived from the deliverable by `tools/make_treeisland_arms.py` rather
than edited by hand, so that an edit to the shipping scene cannot silently leave the controls
comparing something that is no longer shipping.
