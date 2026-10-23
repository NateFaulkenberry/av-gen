# The Living Prism, aurora revision: Phase 3, the crystal (2026-10-23)

Spec 14, sections 5 and 13 (Phase 3). The owner approved the 2i layout ("looks good, start phase 3").

Gate media: `~/Desktop/av-gen-review/82-living-prism/aurora-phase3/`

| File | Shows |
|---|---|
| `phase3-hero-vs-reference.jpg` | the reference's crystals beside the two heroes |
| `phase3-palette-pair.jpg` and `-heroes.jpg` | one palette switch, Green Veil against Rose Veil |
| `phase3-silhouettes-flat.jpg` | the geometry in flat light against the sky |
| `phase3-refraction-ab.jpg` | refraction against the shader alone |
| `phase3-frame.jpg`, `phase3-vs-reference.jpg`, `phase2i-vs-3.jpg` | the material on all 13 formations |

## 1. Silhouettes (5.1)

The shard builder (`blender/aurora_assets.py`, `shard_body`) now makes:
- **Secondary facets:** each hexagonal corner is chamfered (two vertices a little apart), so every primary face is flanked by a narrow facet that catches light on its own.
- **Kinked faces:** a mid ring breaks each long face into two planes.
- **Broken edges:** one or two corners of the upper ring are chipped.
- **Distinctive tips:** the termination is two-staged, a ring at about half the tip height with its corners at irregular heights, then a skewed apex.
- **Asymmetric branching:** long shards that are not already leaning hard (lean of 20° or less) may grow a side crystal from a third to half their height, 16-26° further out. One branch at 25-40° reproduced the owner's "boot", which is why the range is gentler.

Size: 98-637 faces per family. I checked the silhouettes in flat light against the real sky before shading (`phase3-silhouettes-flat.jpg`).

## 2. Material (5.2): one program, `au_crystal` (47 ops, `build_scene.crystal_program`)

All layers draw their colours from the aurora palette roles:

| Layer | How |
|---|---|
| Facet structure | a per-facet random from the quantised normal shuffles the environment colour and scales the interior's brightness per facet |
| Environment | the aurora colour a facet would see: up-facing facets take the high violet or pink, side-facing the low green or teal. It is weighted by Fresnel, with a scatter floor |
| Interior with depth | a base-to-tip OKLab gradient, shifted by view angle (parallax-like) and by a slow object-space noise (cloudy, nebula-like), seen where the facet faces the camera |
| Fractures | object-space Voronoi cell edges, thin, on about a third of the stone (masked by a slow noise), in the look's pale fracture colour |
| Inclusions | rare hashed cells that light up in the environment colour |
| Glowing edges | **geometry**, not shader: thin prisms along the sharp facet edges (`<family>_edgeA/B.glb`), about 40% bright and the rest dim. Only the edges whose faces the camera can see are kept, and only one line per chamfered corner. Emissive, palette-bound: bright edges in the low aurora colour, dim ones in the mid |

Implementation notes:
- **Why the edges are geometry:** the renderer has no per-pixel edge distance, and convex curvature on flat-shaded facets does not find edges.
- **Bug fixed:** `voronoiEdge` returns four different channels (edge distance, F1, hash, F2). A smoothstep acts on each separately, which tinted the cracks red, so a swizzle now copies the edge channel to all four.
- **Body:** near-black (base colour 0.012, 0.008, 0.02; roughness 0.06). The contrast between dark and lit facets is the point.

## 3. Translucency (5.3): A/B, chosen by quality per ms

Screen-space refraction (ADR-1603) plus thin film on the two largest formations (L1, R1):
- **With it:** the cliff and sky bend through the stone, and the facets read darker and more dimensional.
- **Without it:** the stone reads milky and pastel.

Refraction is kept on those two only. The other 11 formations use the shader alone, so there is one transparent layer per hero and no stacked layers. `AU_REFRACT=0` builds without it.

## 4. Aurora coupling (5.4): the shared representation

`build_scene.LOOKS` holds one palette of roles: `auroraLow/Mid/Top`, `aurora2Low/Mid/Top`, `crystalDeep`, `crystalBody` and `crystalFracture`. Its bindings drive:
- both aurora effects (`fx/<id>/lowColor`, `midColor`, `topColor`);
- the crystal program's environment, interior and fracture stops;
- every formation's edge colours;
- both fill lights;
- the water body (`shallowColor`, `deepColor`; the water's reflection is the live aurora already).

There are two states, Green Veil and Rose Veil; `AU_LOOK` picks the selected one for a build. A palette switch moves the aurora, the crystals' facets (pink and green regions trade places), the water and the light together. Every binding resolves, with no warnings in the render log.

## 5. Perf (1080p, AC power, GPU pass timestamps)

| Build | GPU p50 / p95 | Scene pass |
|---|---|---|
| 2i baseline (headless Ultra) | 24.0 / 25.9 ms | 19.4 ms |
| Phase 3, shader only (headless Ultra) | 25.0 / 26.5 ms | 20.4 ms |
| Phase 3, refraction on L1 and R1 (headless Ultra) | 28.9 / 30.6 ms | 24.3 ms |
| Phase 3, live auto | 30.5 / 32.2 ms | 25.9 ms |

Live auto: frame p99 37.2 ms, 1 deadline miss, Ultra 273 / High 177 frames (100% High or better).

- **The material, edges and geometry:** about 1 ms.
- **Refraction on the two heroes:** about 3.9 ms; they cover about 17% of the frame.
- **Headroom against 33.3 ms:** about 2.8 ms live, not the 9-10 ms Phase 3 started with. Against 8.33 ms it is about 3.7x over.
- **The lever:** refraction on one hero instead of two (about -2 ms), or none (-3.9 ms), if Phases 4 and 5 need the room.
- **Measurement note:** the last two changes (thinner cracks, prisms 50% thicker) were constants and edge-mesh width only, measured before them.

## 6. Also

- **R5:** visible share 32% → 46%, a side effect of the gentler branches. That came free, so I took it.
- **Checks:** clearance minimum 6 m (L5); terrain sweep at most 14% hidden and 19% buried.

## 7. Open

- The interior's nebula is softer and less contrasty than the reference's pink and blue clouds.
- The edge prisms still alias slightly at 1-3 px.
- The headless frame p99 shows a recurring single-frame spike, not investigated.
