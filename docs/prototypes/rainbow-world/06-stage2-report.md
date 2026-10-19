# Rainbow World 06: Stage 2 report (isolated prototypes, E4/E5 in AV Gen)

Stage 2 of the brief (§8). Each asset is a visual-development prototype, not a final asset. I rendered each one alone,
rendered them all together, critiqued them against §9, and took the risky ones into AV Gen.

Review sheets: `~/Desktop/av-gen-review/74-rainbow-world/stage2/`
- `01-band-section.png`
- `02-great-tree.png`
- `03-rock-adapted.png` and `03b-rock-hero.png`
- `04-rainbow-arch.png`
- `05-cloud.png`
- `06-together.png`
- `07-avgen-first-renders.png`

Scripts, all headless and reproducible:
- `examples/rainbow-world/blender/`: `rw_common.py`, `band.py`, `rocks.py`, `trees.py`, `rainbows.py`, `clouds.py`,
  `lookdev.py`.
- `examples/rainbow-world/experiments/`: `build_e45.py` and `run_e45.sh`.

Outputs go to `build/rw/stage2/` and `build/rw/e45/`. They are gitignored and regenerable.

## 1. The prototypes

| Asset | Built as | Verdict (§9) |
|---|---|---|
| **Band section** (420 m) | A closed cross-section swept along a centreline with a rotation-minimising frame and a 55-degree twist. Authored hills, a dell, a rolling swell and a ridge spine; no noise shapes it. Both faces are meadow, and grass caps roll over both cliff tops. The rims are **the adapted rock kit stacked as cliff columns**: the upper column's moss faces the band's up, the lower one's its down. | Gravity turns along the section, and the underside reads as a second meadow with its own "up". The rim went through six versions: v1-v5 were strata displacements that read as a layer cake and then as tiles; v6+ are rock masses. **Weak:** the cap lips are rounded "slime" bulges; the mid-distance meadow is a smooth putty; grass is too yellow-olive. |
| **Great Tree** | A twisting trunk splitting at 9.5 m, eight **cloud-pruned pads** in three tiers with sky between them (niwaki-like), and 4,480 leaf cards whose normals come from the hull (the smooth union of the pads). Our own painted leaf-cluster card. | The strongest asset. v1 was a generic oak blob. v2's pads read at 200 m by silhouette alone (`02`, silhouette panel). In AV Gen it reads the same (`07`). **Weak:** limbs are fairly straight; the root flare is plain; no translucency in the engine. |
| **Rock** (Poly Haven CC0, adapted) | Six scans compared. Each was voxel-remeshed, collapse-decimated to about 170 faces, bevelled 1 cm and smoothed by angle. The stylised material is warm stone in light, cool in cavities, with moss on the object's +Z; the scan albedo survives only as a 30% value breakup. **namaqualand_boulder_03** was chosen. | The facets read as carved painterly forms, and the same kit builds the cliffs, so all rock speaks one language. **Weak:** the stone is slightly pink and pale; needs a hint of ochre and a darker mid value. |
| **Rainbow arch** | A soft emissive **volume** (super-gaussian, evenly dense band) with a spectral ramp and an **absorbing** component, so it partly replaces the sky behind it instead of only adding to it. It also has a narrow faint skirt, an absorbing dark band, a reversed secondary, feet that fade into the ground, and coloured spill. | Six versions. Additive emission whited out under AgX (v1). Low emission over blue sky went pastel (v2-v4). **Emission held under the AgX knee plus absorption** brought the colours back (v5-v6). In context it reads as light, and is most vivid against haze or cloud (`06` wide). **Weak:** still pastel in the open sky; the tube is too fat up close. **Not yet in AV Gen** (U6). |
| **Cloud** | 419 lobes in ADR-1289's format: a flat-floored base, three leaning towers of unequal height, and two cauliflower generations biased to the sun. Exported as `hero_cloud.lobes.json` (Y-up). | The silhouette is designed and cumulus-like. In AV Gen's lobe renderer it reads as a soft white mass, too bright and with little form, but **it costs 77 ms** (E4). The Cycles preview is grey-mauve where it should be warm-white on top and blue in the core. |
| **Lighting and sky** | An art-directed all-round sky built from the view direction: cobalt zenith, haze horizon, a warm luminous cloud sea below. A warm sun at a raking angle (about 26 degrees to the band), and a shadowless warm bounce from below (the cloud sea). | It holds the orientation cue and keeps colour in the shade face, partly answering U3: the underside is green and warm-lit, not black. Shade colour still needs more blue-teal. **AV Gen's procedural sky is greyer** than the palette; it is retuned in Stage 3. |

**Together (`06`).** The band, four Great Trees, boulders, the arch across the band and the hero cloud form the first
frame that reads as the world. The surface fisheye, where a tree frames the rainbow, is the strongest composition so
far.

## 2. E4/E5 and U7: what things cost in AV Gen

Setup:
- 1920x1080, Ultra pinned, headless `--live-profile`, no audio, 6 s measured.
- Apple M2 Max, shared with other agents, so expect run-to-run noise of ±2-3 ms.
- Budget: 29.3 ms GPU at 30 fps.
- Records are in `build/rw/e45/profiles/*.json` and `*.txt`.

| Variant | GPU median | The part under test |
|---|---|---|
| base: band only, on-surface camera | 10.2 ms | geometry 6.9, shadows 2.7 |
| cloudcam: band, looking at the cloud's place | 5.5 ms | control for the cloud variants |
| cloud1: one mass, 419 lobes, 96 steps, 6 light steps (ADR-1289 as authored) | **84.3 ms** | **clouds 77.3 ms** |
| cloud4: four masses, about 1,700 lobes, one on screen | 81.8 ms | clouds 76.2 ms (the cost follows the pixels marched, not the lobe count) |
| cloud1lite: 32 steps, 2 light steps | 18.3 ms | clouds 13.0 ms |
| cloudmesh: the same mass as a 24k-triangle mesh | 9.4 ms | about +2 ms geometry, +1.3 ms shadows |
| grass50k: 176-triangle clumps | **62.8 ms** | geometry 29.4, shadows 31.7 |
| grasslite60k: 24-triangle clumps, cull, 90 m reach | 14.8 ms | **+4.6 ms**; viable |
| trees: Great Tree x4 (alpha-mask leaf cards) | **35.4 ms** | geometry +10.7, shadows +14.5 |

**Findings:**
1. **The lobe clouds are not live-viable as authored.** About a third of the frame of cloud costs 77 ms at full
   quality and 13 ms at 32/2 steps. Two routes:
   - (a) a live tier for ADR-1289: a half- or quarter-resolution march, depth-aware upsampling, 24-32 steps. The
     estimate is 3-4 ms for one mass; that needs an ADR, and it is ADR-1543 in the plan.
   - (b) **sculpted cloud meshes** with a stylised cloud material: about 2 ms, already possible, and in the
     painterly spirit (02-research.md E: Sea of Thieves; the Ghibli clouds in Blender).

   I propose to try (b) first in Stage 3, using a material program for the cloud shading (wrap light, cool core,
   warm underside, fresnel rim). Lobe-march clouds would be kept only for far, small masses if the tier is built.
2. **Grass must be light.** 24-triangle clumps within a 90 m reach cost about 4.6 ms for 60k clumps. Points are
   capped at 65,536 per node (`kMaxBakedPoints`), so large fields need several nodes or chunks. Grass should not
   cast shadows, but a procedural node cannot currently opt out of shadow casting; only ecology layers can. That is
   a small additive engine change: **ADR-1545, `castsShadow` on a procedural node**.
3. **Leaf cards in the shadow cascades dominate the tree cost:** 14.5 ms of shadows for four trees. Fixes to try in
   Stage 3:
   - cards that do not cast (needs ADR-1545 too), with a cheap opaque hull proxy that does cast;
   - fewer, larger cards;
   - an impostor LOD beyond 150 m.
4. **Shadow acne on the band.** At the raking sun the band's self-shadowing showed as hard stripes, and contact
   shadows made it worse. Contact shadows off and the sun's `shadowBias` at 0.1 clear it (`07`, panel 2). That is a
   scene setting, not an engine change.
5. **The pipeline works.** Blender meshes with custom normals (grass dome normals, canopy hull normals) arrive
   intact. `distribution.points` places instances in any orientation, including on a twisted surface. Alpha-mask
   cards and material programs (OKLab ramps by object position) recolour assets without vertex colours.

## 3. Critique summary: the five weakest points, to fix before expanding (Stage 3)

1. **The meadow at mid distance is smooth putty, and the grass is yellow-olive.** Fixes:
   - a designed colour pattern at 5-40 m scale: mown-looking swathes, flower drifts, darker clump patches;
   - grass colour matched to the ground, with a distance blend;
   - grass tips greener, not yellow.
2. **The cloud representation and look.** Fixes: decide meshes versus a lobe live tier by a side-by-side in AV Gen,
   and shade warm-white sunlit tops, a blue-violet core and a warm underside.
3. **The rim cap lips.** Fixes: a sharper break at the cliff top, a grass fringe and hanging roots over the edge, and
   the meadow reaching the rocks instead of bulging over them.
4. **Rainbow chroma in the open sky and its scale up close.** Fixes: a thinner tube, against a darker backdrop by
   composition (cloud shade behind the arch), and the AV Gen representation (U6).
5. **The cost of trees and grass.** Fixes: ADR-1545 (shadow opt-out), light cards, a hull shadow proxy, and the
   AV Gen sky retuned to the palette.

## 4. Decisions taken in Stage 2

- **Rims are built from the adapted Poly Haven rock kit (CC0), not from displacement.** One rock language for the
  boulders and the cliffs.
- **The Great Tree is a cloud-pruned, tiered tree.** Its pads echo the clouds, and it reads at 200 m.
- **The rainbow is emission plus absorption, with emission under the AgX knee.** Chroma comes from contrast and from
  replacing the sky behind the bow, not from brightness.
- **Grass is light, short-reach clumps.** Distance meadow detail comes from the ground material.
- **Scene settings: contact shadows off, sun `shadowBias` 0.1.**

## 5. Asset manifest

`assets/rainbow-world.manifest.json` records the six Poly Haven CC0 rocks used. For each it gives:
- source URL, creators (from the Poly Haven API), licence and SHA-256;
- the inspection;
- the test render;
- the modifications and the verdict.

They come from the existing library (`tools/fetch_polyhaven.py`) and were read, never modified. Our own generated
assets are listed as such. Nothing was downloaded in Stage 2. `.gitignore` now covers `/assets/rainbow-world/`.
