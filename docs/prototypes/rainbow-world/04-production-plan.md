# Rainbow World 04: assets, licensing, workflow, technical assessment and staged plan

Gate 1 of the brief, covering §5-§8, §12 and §15.5-12. The plan builds on [03-art-direction.md](03-art-direction.md).

## 1. Asset breakdown (§5.4)

Importance and visibility are rated 1-5. The runtime costs are estimates at 1080p and are measured from Stage 7.

| Asset | Class | Importance | Visibility | Build | Complexity | Runtime cost (est.) | Licence |
|---|---|---|---|---|---|---|---|
| **The knot band**: terrain and rims, in 8-16 chunks | A hero | 5 | 5 (in every frame) | **Custom** in Blender: lofted sections along a hand-shaped closed path, sculpted swells, rims | High | 0.5-1.5 M triangles; 2-3 ms with culling | Own (CC0 or repo licence) |
| Ground material (gradient, curvature, breakup) | A | 5 | 5 | **Custom**: Blender-baked colour and AO gradients, or an AV Gen material program | Medium | One program, under 1 ms | Own; CC0 breakup texture |
| **Hero rainbow arches** (great arch, twin arcs) | A hero | 5 | 4 | **Engine-native** look (soft emissive volume); shape authored in Blender as arc curves | Medium-high | 0.5-1.5 ms (a fullscreen analytic pass or blended mesh) | Own |
| Rainbow light spill on the land | C | 4 | 3 | **Engine-native**: analytic per-pixel term from arc segments, or a few coloured lights | Medium | Under 0.5 ms | n/a |
| **The Great Tree** (landmark, 2 variants) | A hero | 5 | 4 | **Custom**: trunk and branches by curve or skin modifier, canopy masses, leaf cards, hull normals | High | About 50-150 k triangles each, a few instances | Own; CC0 bark breakup |
| Weeper, Blossom Puff, Ridge Pine (2-3 variants each) | A | 4 | 4 | **Custom** from the same toolkit | Medium | Instanced, 100-400 instances | Own |
| **Hero clouds** (8-15 masses) plus the cloud sea | A hero | 5 | 5 | **Custom** lobe skeletons in Blender, rendered by ADR-1289 lobes, or fallback cards/impostors | Medium | **Unknown, 2-8 ms (U4)** | Own |
| Distant echoes of the knot | C | 3 | 3 | **Procedural**: low-LOD instances of the band, hazed | Low | 0.5-1 ms | Own |
| Grass clumps (3-4 clump meshes) | B | 4 | 5 (near) | **Custom** (simple blade clumps with dome normals), or adapt Poly Haven `grass_medium_02` if it fits | Medium | **2-6 ms**: the biggest risk with the clouds | Own / CC0 |
| Flowers and ground cover (4-6 types) | B | 3 | 3 | **Custom** low-poly stylised, plus CC0 adapted | Low-medium | Instanced | Own / CC0 |
| Boulders and outcrops (6-8) | B | 3 | 3 | **Source and adapt**: Poly Haven CC0 rocks, decimated, softened, recoloured by the material program; or custom | Low | Small | CC0 |
| Rock spires (2-3 landmarks) | A | 4 | 3 | **Custom** sculpt | Medium | Small | Own |
| Bark, stone and soil textures | B | 2 | 2 | **Source**: ambientCG / Poly Haven CC0 at 1-2 K | Low | Texture memory | CC0 |
| Sky dome (gradient, zenith, cloud-sea glow, optional physical bow) | C | 5 | 5 | **Engine-native** procedural sky (ADR-036) plus an Environment pass if needed | Low-medium | Under 1 ms | n/a |
| Pollen and sparkle motes | C | 2 | 2 | **Engine-native** particles (audio) | Low | Under 1 ms | n/a |
| Fisheye lens | C | 5 | 5 | **Engine change** (§4) | Medium | 0.3 ms plus overscan | n/a |

**Why custom versus sourced.** The band, the rainbows, the trees and the clouds *are* the identity, and no free asset
has their silhouettes or their painterly normals. Rocks, ground cover and texture breakup are where sourcing saves
time without showing. Photoreal scans are adapted (softened, recoloured, decimated) or rejected.

## 2. Effort allocation

The effort follows importance times visibility:
- band and terrain: 30%
- rainbows: 15%
- clouds: 15%
- trees: 15%
- lighting and atmosphere: 10%
- grass and supporting assets: 10%
- audio and MIDI: 5%

## 3. Licensing approach and manifest (§6)

1. **Allowed sources** (owner's brief): Poly Haven (CC0), ambientCG (CC0), and individual Sketchfab/Fab assets
   whose licence is verified on the asset page. Only **CC0 or CC-BY** are accepted. NC, ND, the Fab Standard License
   and anything unclear are rejected, because the repository is public.
2. **For each asset**, recorded in `assets/rainbow-world.manifest.json` (committed, small, following the
   `treeisle.manifest.json` and `environments.manifest.json` pattern):
   - the source URL and creator;
   - the licence and a link to the licence text;
   - attribution text;
   - date downloaded and SHA-256;
   - local path;
   - inspection notes (triangle count, UVs, textures, scale);
   - a representative test render;
   - modifications;
   - a verdict: unchanged, adapted or rejected.
3. **Storage.** Downloads go under `assets/rainbow-world/` (gitignored; a `.gitignore` entry is added). Each download
   gets its own empty directory, and is read with `python3 -I`. The fetch is reproducible through
   `tools/fetch_polyhaven.py`, or a small fetcher keyed by the manifest.
4. **Never committed:** third-party binaries, .blend files containing them, and audio. CC0 *derived* files may be
   committed if small, but the default is to regenerate them from scripts.
5. **Attribution.** CC-BY entries are copied into `CREDITS.md` at merge time.

## 4. Technical assessment in AV Gen (§2.2, §12, §15.10)

What exists (inspected in source):

| Need | Existing system | Fit |
|---|---|---|
| Import Blender meshes | glTF 2.0 via fastgltf (`src/assets/gltf_loader.cpp`): PBR base colour, metallic-roughness, normal, occlusion and emissive maps, alpha mask and blend, skinning | Good. **No vertex colours and one UV set**, so painted colour must be baked into textures or computed in material programs. |
| Instances with arbitrary orientation | Procedural nodes, `distribution.points` = [position, quaternion, scale] (`procedural.cpp`); GPU culling and LOD (`docs/gpu-culling-lod.md`); one instanced draw per object | Good. Grass, trees and rocks are placed in Blender on the twisting surface and exported as points. |
| Stylised surface colour | Material programs (48 ops, 8 registers): `OklabRamp`, `CurvatureMask`, `HeightBlend`, `Triplanar`, `Noise` and `Voronoi`; inputs include curvature, cavity, occlusion, view direction, instance random and audio | Good. Painterly gradients without textures. |
| Foliage translucency | None in `pbr_shade.wgsl` | **Gap.** Approximate with a material program: an emissive back-light term from view direction and a constant sun direction. If that is not good enough, a small additive ADR adds a wrap/translucency term. |
| Sculpted clouds | ADR-1289 `clouds` block: authored lobes, depth-aware, key, two-colour ambient and warm horizon; Blender-authored | Good look path. **Static, and live cost unmeasured (U4)**. It may need a live tier (fewer steps, half resolution) and drift, as an ADR. |
| Translucent rainbow | Blended entities (`litBlend`), camera-facing ribbons (ADR-703), additive particles | Partial. A soft volumetric band is best as an analytic pass in the Environment seam (ADR-1200), like the clouds. **Candidate ADR.** |
| Fisheye | Post lens distortion is a crude `uv * (1 + k r^2)` with clamping (`post.wgsl fs_lens`): it smears the border. Camera FOV up to 170 degrees vertical. | **Gap.** See below. |
| Camera that rolls with the land | Spline cameras exist (`CameraPlacement::Spline`), and splines carry rotation-minimising frames with per-point roll (`spatial/spline.hpp`). But the spline camera **does not take its up from the spline frame**: it uses world up plus `camera/roll`, which degenerates when the camera looks straight up or down. | **Gap. Candidate ADR**: the spline camera's up follows the spline's normal. |
| Shadows, AO, haze | CSM shadow atlas, contact shadows, GTAO, height fog, volumetric medium (one slot) | Good. |
| Tone map | AgX available (`tonemap.wgsl` operator 1) | Good. Validate the rainbow under AgX, as E3 warns. |
| Look switching | Palette states blended in OKLab with selector, lock, hold and advance (ADR-1043/1420/1421); CC 9/CC 10 used the same way in Phase Hall | Good. Bind the roles to sky, sun, haze, cloud and material parameters. |
| Audio | `response.*` (ADR-1510 adaptive), `response.kick/snare/hat`, `beat.*`; routes and modulators; live stems (ADR-1520/1521) | Good. |
| Profiling | `avgen --live-profile --mode live --target-fps 30 --size 1920x1080 ...`, the Auto quality ladder; budget 29.3 ms GPU | Good. |
| Seek equals play | A transport-driven camera; the palette selector replays (ADR-1420) | Good, provided the camera stays a pure function of time. |

**The fisheye: options and the evidence from E1 (`08-E1-lens-study.png`).**

| Option | Quality | Cost | Engine impact |
|---|---|---|---|
| Post remap of a wide rectilinear frame (stereographic or equisolid; a new mode beside `fs_lens`) | **Matches a true fisheye up to about 150 degrees diagonal** (E1: D against F). It cannot exceed the rendered rectilinear angle. Above about 140 degrees horizontal the rectilinear render wastes its resolution at the edges, and the centre is upsampled 1.15-1.5x. | One fullscreen pass, plus whatever overscan the softness needs (1.15x scale is 1.32x pixels) | One additive ADR (post pass and render scale) |
| Multi-view (3-5 rectilinear views resampled) | Reaches 180 degrees and more, the clip's bowl horizon (E1 G and H) | 2.5-5x the scene cost; screen-space effects (GTAO, SSR, temporal) break at the seams | Large; not live-viable |
| Vertex-shader fisheye | Any angle | Every pipeline must be changed; large triangles must be tessellated | Very large; rejected |

**Recommendation:** start with the post remap at about **150 degrees diagonal**, stereographic (it keeps round
shapes round, and its edge squeeze is the gentlest), with a modest overscan. The bowl that a 180-degree lens gives is
recovered physically: the world itself curves, since the band twists and strands rise overhead. U1 tests whether
150 degrees plus real curvature reads like the clip.

**Candidate engine changes (ADR range 1540-1549; each additive, one commit, a test and an ADR):**
- **1540**: a fisheye lens remap (stereographic, equisolid or equidistant), with the culling frustum and render
  scale chosen to match.
- **1541**: a spline camera's up can follow the spline's own frame (and `camera/roll` adds to it).
- **1542**: soft rainbow arcs as an Environment pass. They are analytic and depth-tested, with spectral ramp, inner
  skirt, dark gap, secondary and spill. This one is only built if a blended mesh fails in Stage 2.
- **1543** (only if needed): a live tier and drift for the lobe clouds.
- **1544** (only if needed): a foliage translucency term.

Nothing else is planned. Each change is justified by a Stage 2-4 render before it is built.

**First-pass GPU budget (1080p, 29.3 ms; estimates to be replaced by measurement):**

| Item | ms |
|---|---|
| band | 2-3 |
| grass | 3-6 |
| trees | 2-4 |
| clouds | 2-8 |
| rainbows | 0.5-1.5 |
| CSM shadows | 3-4 |
| GTAO | 1 |
| sky and haze | 1 |
| post and bloom | 1.5 |
| fisheye | 0.3 |
| overscan (1.15x) | +20-30% on pixel-bound passes |
| **total** | **17-30 ms**, tight at the top |

The two unknowns that decide whether the scene fits are **grass and clouds**. They are measured first (E4, E5).

## 5. Blender workflow and repository structure (§7)

Adapted to this repository's conventions: compare `examples/bioluminescent/` (generators plus small GLBs) and
`examples/biovalley/blender/`.

```
docs/prototypes/rainbow-world/       the brief, this dossier, stage reports, gate notes
  scripts/                            analysis and contact-sheet tools (clip_sheet, clip_motion, remap_fisheye,
                                      sheet_grid, palette_board, ref_board)
  blender/                            Gate-1 throwaway experiments (exp_*.py)
examples/rainbow-world/               the scene
  rainbow-world.json                  the live project (control map, palette, routes, sonic.live)
  rainbow-world.scene.json            the composition
  build.py                            regenerates every derived file from the Blender scripts (one command)
  blender/                            the production asset scripts, one per asset family:
    band.py  rims.py  trees.py  rocks.py  clouds.py  rainbows.py  scatter.py  export.py  lookdev.py  turntable.py
  meshes/  lobes/  points/            generated outputs; committed only when small and our own, otherwise
                                      gitignored and rebuilt by build.py
assets/rainbow-world.manifest.json    the licence manifest (committed)
assets/rainbow-world/                 downloads and .blend files (gitignored)
~/Desktop/av-gen-review/74-rainbow-world/<stage>/   review media (stills, contact sheets, 10-20 s clips)
build/rw/                             scratch (gitignored, survives reboots)
```

- **Headless Blender** (`Blender -b --factory-startup -P script.py -- args`) does all generation, export, turntables
  and contact sheets, so the results are repeatable. Each asset script builds one family into its own collection and
  saves a milestone `.blend` under `assets/rainbow-world/blend/`.
- **Blender MCP**, which is connected (Blender 5.2.1 LTS, checked at Gate 1), is used to inspect silhouettes,
  normals and proportions interactively. Any change found that way is written back into the script, never left in
  session state.
- **Export.** glTF binary, Y-up. Instance placements are exported as `points` JSON and clouds as lobe JSON. Each
  export carries a version stamp.

## 6. Stage plan with dependencies and acceptance criteria

Each stage ends with a gate note to the coordinator: image paths and a short summary.

| Stage | Content | Depends on | Acceptance (what I must see before moving on) |
|---|---|---|---|
| **2. Isolated prototypes** | One band section (about 300 m) with a rim; the Great Tree; one boulder (Poly Haven CC0, adapted); the great rainbow arch (soft volume); one cloud mass (lobes); the lighting and sky setup. Turntables and contact sheets, individually and together. **Plus E4 and E5** (below) in AV Gen. | Gate 1 approval | Each asset passes §9 for its category at the distances it is seen. The tree reads by silhouette at 200 m. The rainbow reads as light, not a sticker, *under AgX*. The cloud has a designed silhouette and lit depth. Grass and cloud cost known. |
| **3. Look-dev scene** | A small assembly with five views (wide, close terrain, rainbow-dominated, clouds/atmosphere, fisheye). A labelled contact sheet. The 3-5 worst weaknesses found and fixed. | 2 | The sheet compares well against R1/R3 for colour, depth and clouds, and the clip for lens. The fixes are shown before and after. |
| **4. World and camera prototype** | The full knot in grey-to-mid fidelity. The journey path with surface-normal up (ADR-1541 if needed) and fisheye (ADR-1540). Every view tested from several orientations. A 10-20 s Blender preview of one clip-like sequence, then the same in AV Gen. | 3, 1540/1541 | Continuity with no cuts; land overhead legible; no frame with more than about 25% empty sky; no visible cut-off edge; sequences resolve upright at landmarks; seek equals play. |
| **5. Production assets** | The band to final; tree family; rocks, flowers and grass; cloud set; echoes. | 4 | No new visual style without updating 03. Every asset logged in the manifest. |
| **6. Lighting and VFX** | Rainbow spill, light pools on the shade faces, haze, motes, the five looks. | 5 | §9 answered for every look at multiple orientations. |
| **7. AV Gen integration** | Live project, palette with CC 9/10, audio routes, MIDI map, an `examples/index.json` entry (`"live": true`; a proposed category). A baseline live profile. Blender against AV Gen comparison stills. | 6 | Discrepancies listed and corrected. A baseline under 29.3 ms GPU p50 at 1080p30, or a documented plan. |
| **8. Final review and optimisation** | Profiled bottlenecks fixed. Representative stills and 10-20 s clips. The final checks (cpu-assets, gpu, review_examples, integrity), all exiting 0. | 7 | The §14 acceptance criteria, each met or documented as a limitation. |

## 7. MIDI proposal (to agree before Stage 7)

| Control | Function |
|---|---|
| CC 9 | Look select (5 states) |
| CC 10 | Look lock |
| CC 1 | Rainbow intensity |
| CC 2 | Camera energy (bounded lift, lean, FOV breath; never speed) |
| CC 3 | Wind |
| CC 4 | Audio influence |
| CC 5 | Haze |
| CC 6 | Light (sun and exposure trim) |
| CC 7 | Sparkle (motes and shimmer) |
| Pads 36-40 | Look 1-5, direct |

Every mapping is documented in the scene's generator and in its `examples/index.json` description.

## 8. Uncertainties and the experiments that resolve them (prioritised)

| # | Uncertainty | Experiment | When |
|---|---|---|---|
| U1 | Does about 150 degrees of remapped fisheye plus real curvature feel like the clip's 160-190 degrees? | E1 done (the remap equals a true fisheye up to 150 degrees). Next: ADR-1540 in AV Gen on the Stage 4 band; side by side with the clip's dense sheets | Stage 4 |
| U4 | Live cost of the lobe clouds at 1080p | **E4**: an AV Gen scene with one Blender-authored cloud mass and the hero set (about 1,500 lobes), `--live-profile` | Stage 2 |
| U5 | Live cost of grass at the density the look needs | **E5**: an AV Gen patch of 50-300 k grass-clump instances with GPU culling and LOD, profiled | Stage 2 |
| U2 | Does the knot read as a place, not as an abstract sculpture? | Stage 3/4 contact sheets from inside, with landmarks; owner review | Stage 4 |
| U3 | Do the shade faces read as a mood rather than as dullness? | Stage 3 view pair: lit face against shade face with skylight, rainbow pools and glowing flowers | Stage 3 |
| U6 | Does the soft rainbow survive AgX and the engine's bloom without a new pass? | E3 done in Blender: the volume tube works, flat stripes fail, and a pale backdrop desaturates the bow. Next: a blended-mesh version in AV Gen; ADR-1542 only if it fails | Stage 2 |
| U7 | Does the foliage look painterly without translucency? | Stage 2 tree renders in AV Gen, front-lit and back-lit | Stage 2 |
| U8 | Do sway and wind behave on surfaces that are not world-up? | Stage 4: grass on an inverted strand under wind | Stage 4 |
| U9 | Does a strong roll stay comfortable for minutes of live play? | The Stage 4 journey with 20-40 s calm stretches; owner review | Stage 4 |
