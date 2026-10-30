# Dimensional portals: Gate R research, audit and approach debate

The brief is `12-dimensional-portals-brief.md`; the working rules are in `13-portals-coordinator-notes.md`. This
covers the audit of what AV Gen already has, the reference research, the debate on the four hard problems, and the
throwaway proofs. The chosen architecture, the beat sheet and the gate plan are in `15-portals-plan.md`.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/portals-r/`. The proof scripts are copied to `proof-scripts/`
there. None of them are in the repository. Clones made for study are in `~/Documents/GitHub/av-gen-refs/` (Rezmason
`matrix`, MIT; SebLague `Portals`). Nothing from them is copied into AV Gen.

## 1. Audit: what the scene and the engine already have

### 1.1 The scene

| Part | Where | State |
|---|---|---|
| Terrain | `src/scene/attractor_world.cpp`: a 4096² CPU bake over 16 km, then an RTIN mesh (1 m grid, watertight, 2.92M triangles in 256 chunks; ADR-1857, 1867) | Accepted look. Not to be redesigned. |
| Rock material | `canyonRock` program (85 of 96 ops), written by `examples/strange-attractor/build_rock_program.py` | Strata from a world band coordinate in `uv.x`, playa in `uv.y`, contour lines (ADR-1862 mode A) |
| River | Engine water (ADR-099) with SSR, sky mirror, cirrus along the reflected ray (ADR-1865/1868/1870/1871) | Known limit: the 16 s screen-edge seam |
| Sky | Analytic sky plus cirrus bands (ADR-1864/1865) | ~0 ms |
| Shadows | 4 cascades, 3 km, fitted on the double camera basis (ADR-1866), caster reach (ADR-1858) | |
| Camera | `attractorCamera()`: a pure function of time (`buildFlight`, ADR-1853/1854), 46 s, ~80 m/s on the river run. FOV 44°. Banks through bends. | Seekable, deterministic. Re-timing it is scene work. |
| Triangle tracing | `meshWire` (ADR-1862): every real triangle edge, read in place from the mesh's own index and vertex buffers; fades by distance (120–1400 m), screen density (<14 px), shore; a `wave` ring that grows from `origin` | The base of the scan. `origin` is already a uniform. |
| Audio | `strange-attractor.json`: Feline Footwear and stems; onsetLow → wave, bass → wire intensity, high → contours | |

The flight passes low over the river from 15 to 22 s (25–35 m up), then climbs to the wide view at 40 s. Nowhere does
it fly at a wall. The approach to portal 1 needs a new camera segment from about 16 s.

### 1.2 Engine systems that bear on this work

| System | ADR | What it gives | Verdict |
|---|---|---|---|
| Mesh wire | 1862 | real topology, in place, a travelling front with a world origin | **Extend** (scan v2). The front origin becomes the portal. Add a palette ramp, depth-varying width and a density hierarchy. |
| 96-op programs | 1863 | room for contours | The rock program has 11 ops left. Do not put the glyph layer in it. |
| Temporal family | 410, 1049, 1066 | echo, **mosh** (blocks from the clean ring, dragged, channel shift), feedback (zoom/turn), slit-scan; all FIR over a clean ring, so they are seekable | **Extend mosh.** Today it is a uniform full-screen pick: the proof measured it as the main source of frame-level pops. It needs a world mask, motion-vector drag and a slower re-pick. |
| Quantize op | 1162 | macroblock cells in a program | Useful for world-space block masks |
| Entity FX lanes (FXL) | 703, 716 | per-entity clip (dissolve), displacement (inflate/travelling/smear), Worley pattern; **every pass obeys them** (depth, shadow) | **Extend** with a pull-toward-a-point displacement and strip displacement. The canyon chunks are entities, so they qualify. |
| Blocks deformer | 1674 | block displacement of cells | Procedural nodes only, not mesh entities. Its hash and epoch idea carries over. |
| Distortion (DF) | 703, 716, 719 | screen-space, depth-aware distortion: shock, ripple, **gravitational lens**, heat shimmer | **Reuse** for the "distorted spatial boundary" around the aperture |
| Environment seam | 1200, 1221 | a renderer that writes HDR and depth after the lit pass, before the medium and post | Precedent for the portal composite, but it runs **after** water. The portal must run before water so the river can reflect the Moon (see §4.2). |
| Refraction frame copy | 1603, 1819, 1820 | one copy of the lit opaque frame that water, glass and glossy SSR read | The portal composite must land before this copy |
| Multiple cameras | 245 | named cameras, one active | Not a second render. AV Gen draws one view per frame. |
| Aux targets, frame graph | 035 | HDR, emission, depth, linear depth, velocity, ids | The virtual view needs HDR and linear depth only |
| Render job | `src/app/render_job.cpp` | one `SceneRenderer` driving `engine->scene()` | Needs a second renderer for the destination world |
| Composition prefix | `Composition::attach(prefix)` | parameter namespacing, so two compositions can share one `ParameterSet` | The base for several worlds in one project |
| Day/dusk knobs | `attractorWorld/{sunAzimuth, sunElevation, sunIntensity}`, `env/sky/*` | keyable sun and sky gradient | Cirrus colours, fog and water are not parameters. Dusk is better as a second environment than as keys (§4.4). |
| Material program inputs | procedural-materials.md | `viewDirection`, `normal`, `fresnel`, `power` (with −1 = reciprocal), `gradient` (dot with a constant) | **Enough for a lunar-Lambert regolith with opposition surge, with no engine change** (proved, §5.4) |
| Clip distances | Dawn `FeatureName::ClipDistances` | per-vertex user clip planes on Metal | Present in the Dawn headers. Not enabled in `gpu/context.cpp`. It is the fallback for clipping the destination view (§4.2). |

**Not in the engine:** rendering a second scene in the same frame; any portal or clip-plane concept; glyph rendering
on 3D surfaces (`comp/glyph_atlas` is the 2D text layer); triangle-level break-up of a mesh entity; a lunar
photometric model (but the program covers it).

## 2. Reference research

### 2.1 Digital rain and code visualisation

- **The original design.** Simon Whiteley at Animal Logic hand-drew the glyphs from Japanese cookbooks: katakana plus
  numerals, mirrored, with added strokes and dots. The vertical fall was chosen to read as rain on a window. A
  mental ray shader streamed the code. Characters sometimes "stick" in place, which gives the glitchy persistence.
  For Neo's corridor shot the same shader was **mapped onto the 3D floors, walls and agents**. That is the precedent
  for code on real geometry, not a screen overlay.
  [befores & afters: Secrets of the Matrix code](https://beforesandafters.com/secrets-of-the-matrix-code/);
  [No Film School](https://nofilmschool.com/matrix-digital-rain-origin).
- **Rezmason/matrix** (MIT, WebGL and WebGPU; cloned): [github.com/Rezmason/matrix](https://github.com/Rezmason/matrix).
  Read at source (`shaders/wgsl/rainPass.wgsl`). The mechanics worth borrowing:
  - The rain is **stateless**. A column's brightness is `1 - fract((y·0.01 + columnTime) / raindropLength)`, a
    sawtooth whose tip is the "cursor", with `columnTime` hashed per column and wobbled by incommensurate sines.
    That makes it a pure function of time, so it is seekable. It fits ADR-410 directly.
  - Glyphs cycle per cell at a hashed rate, and the cursor is brighter and whiter.
  - Glyphs are MSDF, so they stay sharp at any size. It also has a "volumetric" mode that places columns in depth.
  - The palette pass maps brightness through a ramp. A copper/amber/gold ramp is a palette swap, not a new technique.
- **What we take:** the stateless sawtooth rain, per-cell glyph cycling, a brighter leading glyph, a palette ramp.
  **What we do not take:** the font, the green, the screen-space columns. Our glyphs are a procedural set we
  generate. They sit on the canyon's real surfaces, in world space, so they cannot be "a full-screen glitch filter".

### 2.2 Datamosh, real and real-time

- **Real datamosh:** remove or corrupt the I-frames of an inter-coded stream, so later P-frames' motion vectors and
  residuals are applied to the wrong base picture. The signature: old colours dragged by new motion, smearing in
  macroblocks, with blocky borders. Tools: [FFglitch](https://ffglitch.org/) (edits motion vectors with
  scripts; wrapped by [tiberiuiancu/datamoshing](https://github.com/tiberiuiancu/datamoshing)), the AE plugin
  [Datamosh 2](https://aescripts.com/datamosh) ("swap motion" between clips).
- **Real-time imitations:**
  - Per-block random offsets (the Minecraft datamosh shaders on Modrinth/CurseForge, 8×8 macroblocks).
  - Optical-flow vectors driving a feedback UV displacement (TouchDesigner,
    [Interactive & Immersive HQ](https://interactiveimmersive.io/blog/content-inputs/datamoshing-in-touchdesigner/)).
  - [ffgl-datamosh](https://github.com/legofsalmon/ffgl-datamosh) for Resolume. It estimates block motion on the GPU,
    then deliberately gets it **wrong**, because correct motion reconstructs the picture and reads as clean.
- **What we take:** a renderer already knows the true motion (AV Gen's velocity target). The convincing signature is
  stale pixels pushed by the current motion and overshot. It comes from the clean history ring (FIR, ADR-410), not
  a feedback buffer. Corruption is chosen per block in **world space**: a block is picked by the world position under
  it and its distance to the portal. Blocks are re-picked slowly, a few times a second, not 12 times. A uniform
  re-pick was the flicker source in proof A.

### 2.3 Portal rendering

- **Stencil versus render-to-texture.** Valve's Portal used a stencil method, drawing the destination into the
  portal's stencil region in the same frame. That handles recursion depth flexibly
  ([Game Developer: Inside the making of Portal](https://gamedeveloper.com/game-platforms/exclusive-inside-the-making-of-i-portal-i-)).
  [Immersive Portals](https://qouteall.fun/immptl/wiki/Implementation-Details) documents both methods:
  - Stencil, outer to inner, with occlusion queries.
  - A framebuffer per portal, then composite. Simpler, but no portal-in-portal.
  - **It clips geometry with `gl_ClipDistance`, not an oblique projection, because the oblique projection breaks
    effects that depend on depth.**
  - The camera "teleports" when it crosses the plane, before the frame renders.
- **Oblique near-plane clipping.** Lengyel replaces the near plane with the portal plane by editing the projection
  matrix. Depth precision degrades, and the far plane must be re-fitted.
  [Lengyel-Oblique.pdf](https://terathon.com/lengyel/Lengyel-Oblique.pdf),
  [code](https://terathon.com/code/oblique.html), [GDC 2007 slides](https://terathon.com/gdc07_lengyel.pdf).
  For AV Gen this is the disqualifying point. Linear depth (ADR-1870), fog, SSR, the shadow fit (ADR-1866) and the
  sky ray (ADR-1807) all read the projection's depth mapping, and an oblique matrix changes it.
- **The camera-crossing problem.** When the near plane passes through the portal quad, the quad is clipped and the
  destination blinks out. Fixes:
  - Sebastian Lague's Portals (cloned; `Portal.cs`) thickens the screen mesh by the distance to the near-plane
    corner, on the camera's side.
  - [Wikibooks Mini-Portal](https://en.wikibooks.org/wiki/OpenGL_Programming/Mini-Portal_Smooth) makes the portal a
    volume.
  - [pr0g/unity-portal-rendering](https://github.com/pr0g/unity-portal-rendering) uses the same approach.
  - Lague also disables the oblique matrix very near the portal because of artefacts.
- **Offline portals.** Pixar's "Portals in Incredibles 2" (Voyd's portals): a ray that hits the portal surface is
  transformed into the destination. Per pixel, it is exactly a ray–plane test followed by a transformed ray. Pixar
  technical memo; the old URL `graphics.pixar.com/library/PortalsIncredibles2/` now redirects to
  [pixar.com/technology-libraries](https://www.pixar.com/technology-libraries).
- **What we take:** Pixar's per-pixel ray test, done as a full-screen pass. It has no portal mesh to clip, so the
  crossing problem does not arise (§4.3). From Immersive Portals: render-to-texture, not stencil (we need no
  recursion), clip distances rather than an oblique projection if clipping is ever needed, and the switch of world
  on the frame the camera crosses.

### 2.4 Geometry disintegration and reconstruction

- **Per-triangle break-up needs unshared triangles.** Displacing shared vertices inflates a mesh rather than breaking
  it. Each triangle needs its own vertices, either pre-split or generated on the fly. This is the standard
  observation in shader-graph and Godot discussions of the effect; the usual recipe is a per-face direction and a
  progress value driving the vertex offset, plus an alpha dissolve.
- **The film approach.** Weta's "blip" disintegration in Infinity War worked on digital doubles: geometry turned into
  flakes, then volumes. Endgame needed many more, so background blips ran in Eddy inside Nuke
  ([VFX Voice: Matt Aitken](https://www.vfxvoice.com/?p=13121260);
  [Weta FX article](https://www.wetafx.co.nz/articles/how-wetas-vfx-team-brought-the-most-epic-moments-of-avengers-infinity-war-to-life)).
  The lesson for us is the order: flakes peel from an advancing edge, and the surface behind the edge is already
  gone.
- **What AV Gen already has that makes this cheap.** meshWire already draws one instance per triangle from the mesh's
  own buffers. A sister pass can draw each triangle as a free shard, with its own offset, rotation and shrink along
  a pull field. Meanwhile the FXL clip lane removes the same triangles from the terrain, in every pass including
  shadows. No pre-split copy is needed, because the shard pass de-indexes on the fly.

### 2.5 The Moon

- **Photometry.**
  - Regolith is dark: normal albedo is about 0.12 and single-scattering albedo about 0.15–0.2. Secondary bounce light
    is weak.
  - The Moon shows no limb darkening, because it follows Lommel-Seeliger, not Lambert.
  - It has a strong **opposition surge**, a brightening as the phase angle approaches 0. The surge has two parts:
    shadow hiding (broad) and coherent backscatter (narrow).
  - Real-time lunar simulators implement Hapke-Lommel-Seeliger in shaders. Full Hapke is heavier.
  Sources:
  - [CU Boulder, efficient photometric models of the lunar regolith for VR](https://www.colorado.edu/ness/sites/default/files/attached-files/efficient_photometric_models_of_the_lunar_regolith_for_virtual_reality_simulation.pdf);
  - [lunar sensor simulation, Hapke parameters B_s0 ≈ 1.8, h_s ≈ 0.07](https://arxiv.org/pdf/2410.04371);
  - [lunar dust review](https://arxiv.org/pdf/2511.08503).
- **Lunar-Lambert (McEwen 1991).** `(1-L)·μ0 + 2L·μ0/(μ0+μ)`, with L from 0 (Lambert) to 1 (Lommel-Seeliger) and a
  phase-dependent cubic `L(α) = 1 − 0.019α + 2.42e-4α² − 1.46e-6α³` (α in degrees).
  [USGS: Photometric functions for photoclinometry](https://pubs.usgs.gov/publication/70016679);
  [ISIS LunarLambertMcEwen](https://isis.astrogeology.usgs.gov/6.0.0/Object/Programmer/class_isis_1_1_lunar_lambert_mc_ewen.html);
  [rms-nav implementation](https://rms-nav.readthedocs.io/en/latest/_modules/spindoctor/sim/forward/photometry.html).
  **This corrects my proof.** I used a constant L = 0.8. The cubic gives L ≈ 0.2 at 90° phase, so cross-lit regolith
  is close to Lambert. The flat lunar look belongs to low phase (the sun behind the camera). The program will compute
  L from the phase per pixel.
- **Morphometry.**
  - Simple craters (under ~15 km) are bowls, with depth about 0.2 D and rim height about 0.04 D.
  - Ejecta thickness falls off as (r/R)^-3 (McGetchin et al. 1973, as used in Purdue's
    [CTEM](https://github.itap.purdue.edu/MintonGroup/CTEM/commit/a99bd2ad659fb8bf077ae71f313b9fd34918d506)).
  - Small craters approach equilibrium, with cumulative N(>D) ∝ D^-2. Most are degraded: shallow, with soft rims.
  - [CraterMaker's "basicmoon" profile](https://cratermaker.readthedocs.io/en/latest/auto_examples/04-topography/4.2-plot_crater_profile.html)
    documents rim and ejecta as separate functions.
  - Fresh craters have bright ejecta and rays. Old ones have none.
- **Photography.**
  - Apollo surface photos were Hasselblad 500EL/Data Camera at daylight exposures (about 1/250–1/500 s), so the sky
    records black with **no stars**.
  - Landings were timed for **low sun (about 10–15°)**, which gives long shadows and readable relief.
  - The horizon is close: about 2.4 km for a standing eye, because of the radius of 1,737 km.
  [Apollo 11 surface photography plans (MSSS)](https://www.msss.com/moon/apollo11/apollo11.html);
  [NASA Space Math: why no stars](https://spacemath.gsfc.nasa.gov/Grade67/5Page66.pdf).
  Earth from the near side hangs at a fixed spot in the sky at about 1.9° across. Earthshine only matters on the
  night side.
- **What we take:**
  - black sky, no stars by default;
  - a low sun (8–12°) with razor shadows;
  - a curvature drop and a close horizon;
  - a crater population drawn from the size-frequency law with degradation states;
  - one fresh hero crater with bright ejecta;
  - a lunar-Lambert regolith with phase-dependent L and a narrow opposition surge;
  - near-field rubble and boulders around fresh craters, which make the scale.

## 3. NASA and LRO data (a proposal, not downloaded)

The procedural Moon in the proof already reads as lunar from a distance. Real data would help in two places. I am
asking first, per the rules. Nothing is downloaded, and nothing would be committed (assets are gitignored and
symlinked, like the audio).

| Data | Source | Licence | Use | Recommendation |
|---|---|---|---|---|
| LROC NAC DTM of Linné crater, 2 m/px, 32-bit GeoTIFF (about 134 MB; 48 MB pyramid) | [data.lroc.im-ldi.com NAC_DTM_LINNECRATER](https://data.lroc.im-ldi.com/lroc/view_rdr_product/NAC_DTM_LINNECRATER) | NASA/GSFC/Arizona State University, distributed through the PDS. NASA data is generally not copyrighted, and credit is requested. The product label should be checked. | Calibration only: measure a real fresh 2.2 km crater's profile, rim and ejecta and fit the generator to it | Optional. Useful for honesty about scale. |
| NASA Blue Marble (for an Earth in the sky) | [visibleearth.nasa.gov](https://visibleearth.nasa.gov/) (the Blue Marble collection) | Public domain (NASA) | Earth's disc, about 2° across | Only if the owner wants Earth visible |
| Apollo surface photographs | [Apollo Lunar Surface Journal](https://www.hq.nasa.gov/alsj/) | NASA, public domain | Viewing reference for the look. Not ingested. | Reference only; no download needed |

## 4. The approach debate

### 4.1 The Matrix effect: scan and breakdown

**A. Image-space only.** Use the existing mosh, feedback, echo and slit-scan over the meshWire scan.
- Cost: none; it exists.
- Look (proof `03`): the scan is good, but the breakdown is a uniform field of squares over sky, river and rock alike.
- Irregularity p90 was 0.67, frame-level popping. It is exactly what the brief rules out ("not a full-screen glitch
  filter").
- **Rejected** as the main layer. Kept as the last, masked layer.

**B. Material-program only.** Put glyphs and corruption in the rock program.
- Programs cannot index a glyph atlas by cell (texture ops only project world position).
- The program has 11 ops left.
- It would recompile the 85-op pipeline for every tweak.
- **Rejected.**

**C. Layered and 3D-aware (chosen).** One scene-level **corruption field** drives four layers that each live where
they belong:
1. **Scan (meshWire v2).**
   - Fronts that originate at the portal and sweep back over the camera, not a ring around the camera.
   - A copper → amber → gold ramp across the front: a hot leading edge, a cooling tail.
   - Line width and intensity vary with depth, with a hierarchy: coarse RTIN levels light at range and fine ones
     near. This also fixes the open "wire invisible in wide shots".
2. **Surface code.** A mesh-overlay pass reading triangles in place, like meshWire:
   - world-anchored glyph cells on the rock;
   - Rezmason-style stateless rain flowing down the faces, then **along the surface toward the portal** as the field
     rises;
   - glyph cells LOD'd by powers of two so a glyph is never under about 16 px;
   - the rock darkened under it.
3. **Geometry.** The field drives:
   - FXL displacement that **pulls** vertices toward the aperture and offsets strata-aligned **strips**;
   - an FXL clip that dissolves triangles from an advancing edge;
   - a **shard pass** that draws exactly those triangles detached, tumbling and shrinking along the pull;
   - reconstruction, which is the same edge run backwards.
4. **Image space (mosh v2).** Blocks picked by world distance under them, dragged along the velocity target with
   overshoot, row tears inside the mask, and DF lens distortion at the aperture.

Cost: three engine passes, two of them sisters of meshWire, plus an FXL mode and a mosh extension.
Risk: glyph aliasing and sparkle (proof B showed small glyphs read as speckle), and layer clutter.

**D. Volumetric code** (columns of glyph particles in the air, Rezmason "volumetric"). Spectacular, but it is not the
terrain, and fills the frame uniformly. Held as an accent for the last two seconds before the aperture, if anything.

### 4.2 The portal view

**A. A second world rendered to a texture from a virtual camera, composited per pixel (chosen).**
- The destination is a full composition with its own sky, sun and post settings, rendered by its own
  `SceneRenderer` with the virtual camera `T·C`, where `T` maps the entry portal's frame to the exit's.
- The primary renderer adds one full-screen **portal pass**. For each pixel:
  - intersect the camera ray with the portal plane;
  - test the hit against the aperture shape (an irregular ellipse with a noise edge, opening by a keyed `open`
    parameter) and against the scene's own linear depth;
  - write the destination's HDR there, and write the plane's depth (so fog, water SSR, motion blur and debug treat it
    as a surface);
  - add the perimeter to emission.

The pass runs after the opaque lit pass and **before the water and the refraction copy**, so the river reflects the
Moon in the aperture.

- **Cost:** one extra world render while a portal is on screen, about +35 ms offline at 1080p for the Moon in the
  proof. A full-screen pass is cheap.
- **Risks:**
  - Geometry between the virtual camera and the exit plane would wrongly appear. Answer: place exits with clear air
    behind them (both choices in the plan do). Fallback: `ClipDistances` in the destination's geometry passes.
  - Temporal state at the hand-over (§4.3).
- **Proved** (proof 1): perspective is correct and the occlusion against the canyon wall is right.

**B. Stencil, same frame, same renderer** (Portal's method).
- It needs one renderer to draw two scenes with different suns, skies, shadows and fog inside one `render()`.
- `SceneRenderer::render` is about 2,700 lines with one environment per frame.
- Rejected as too invasive for no visual gain without recursion.

**C. Both worlds in one scene, far apart** (the Moon at +40 km), the portal as a second camera on the same scene.
- Lighting, sky and fog are global per scene, so the Moon would get the canyon's sky and sun.
- Float precision suffers at tens of kilometres.
- Rejected.

**D. A cube-map captured once at the exit.** Cheap, and it suits the very distant parts. Parallax is wrong for
anything near, and the camera flies through it. Rejected, though it could serve as a far-field LOD later.

**E. Oblique-projection render of the destination.** Breaks AV Gen's depth readers (§2.3). Rejected in favour of A
plus, if needed, clip distances.

### 4.3 The crossing

**A. A per-pixel aperture with a hand-over at the plane (chosen).**
- The aperture is analytic (a ray–plane test), so there is no portal mesh for the near plane to clip.
- When the camera's signed distance to the plane changes sign, the destination world becomes the primary.
- Its virtual camera already *is* the real camera at that instant, because `T` is applied continuously. The frame
  before the crossing is already 100% destination, since the aperture fills the screen several frames earlier.
- **Proved** (proof 2): frame-difference luminance through the plane runs 1.55, 1.60, 1.65 with no step. The world
  switch itself is invisible.
- What *is* visible is the rim sweeping off the frame edge (irregularity 0.48 at that frame). That is where the
  design goes:
  - lens the destination near the rim;
  - slow the camera;
  - let the corruption peak and release on that beat.

**B. A portal mesh with a thick screen** (Lague). It works, but it needs a portal mesh, a depth-test exemption and a
thickness tied to the near plane, all to get what the analytic test gives by construction. Rejected.

**C. A matched cut at a full-screen moment** (white-out or mosh-out). The brief forbids a cut-like transition.
Rejected, though a mosh peak *during* the continuous crossing is fine.

**Temporal hand-over.** Post and temporal effects run in whichever renderer is primary. At the hand-over, the
destination renderer's clean ring holds its own virtual views. At the crossing those equal the composite, because the
aperture has filled the frame for several frames, so the mosh history stays plausible. If a jump shows, the fix is a
shared post stage, which is a bigger refactor and a Gate B risk item.

### 4.4 The Moon

**A. A scene-specific generator, like `attractorWorld` (chosen).** `lunarWorld`:
- crater size-frequency and morphometry;
- degradation states;
- one fresh hero crater;
- an RTIN mesh at 1 m near the path, reusing the canyon's mesher, with a skirt to the horizon and curvature drop;
- boulders and rubble scattered as procedural instances around fresh craters;
- the regolith as a material program (lunar-Lambert plus surge; no new shading code);
- a black sky;
- a hard, low sun.

The proof (stills 06–09) reads as the Moon from a distance. It is weak in the near field, which is smooth at 2.5 m
with no rocks.

**B. A real LRO DTM** (for example Linné) as the hero crater, with procedural surroundings. Authentic geometry, but a
download, a stitching seam into the procedural field, and data that is not ours. Proposed at most as calibration
(§3).

**C. Blender-built terrain exported as GLB.** Possible (Blender MCP is available), but less controllable and less
deterministic than the generator, and the scene would carry a large binary. Rejected.

**Dusk.** Dusk is a second canyon world: the same generation (cached by hash), a different environment block, sun,
water and cirrus.
- It is not keyed, because cirrus, fog and water colours are not parameters.
- The switch is invisible: dusk is only ever seen from the Moon side.
- Proof 10 shows the existing knobs reach dusk on the wide shot. The gorge crushes toward black at 9 s, the old
  Nocturne problem. Fill and sky need work.

## 5. Proofs (throwaway, not in the repository)

Every avgen run went through `tools/gpu-lock.sh`. The camera override was a 30-line uncommitted hack in
`Composition::update`: `AVGEN_PROOF_CAM` read a CSV of poses. It is reverted, and kept as a patch in `proof-scripts/`.

### 5.1 The portal view (`01-…png`, `02-…mp4`, `02b-…png`)

1. The canyon flythrough 17.5–21 s was rendered by the engine with its depth AOV.
2. The Moon was rendered by the engine from the virtual camera. That is the flight translated so that the flight's
   20.0 s pose lands 26 m above a lunar plain.
3. A Python composite did per pixel what the planned pass will do: ray–plane hit, a ragged 26 × 42 m ellipse, the
   canyon depth test, a lens inside the rim, a pull outside it, and a copper rim.

**Result:**
- The Moon sits behind the aperture in correct perspective: its horizon lies where the canyon's would.
- The right wall correctly occludes the aperture's edge.
- Approaching, the Moon's surface parallaxes like a real space beyond the opening.

**Failures and limits:**
- The river does not reflect the aperture. The composite came after the render here; in the engine it must come
  before the water.
- The outside pull drew a small horizontal step where the wall meets the water.
- The aperture hangs in the air over the river rather than in the wall. Placement is Gate B work.

### 5.2 The crossing (same clip)

- No cut and no step through the plane (frame-difference values above).
- At 80 m/s the aperture goes from 10% to 100% of the frame in 0.87 s. That is too fast to savour, so the plan slows
  to 20–30 m/s.
- The one irregular frame is the rim leaving the frame edge.

### 5.3 Scan → breakdown on the real canyon (`03`, `04`, `04b`, `05`)

**A, existing tools only (03).**
- meshWire with three fronts and contours rising, then mosh, feedback (zoom 0.97) and echo keyed up.
- The scan is already a good base. The breakdown is uniform and screen-locked.
- Irregularity p90 0.673.

**B, 3D-aware prototype (04).** The same clean frames plus the depth AOV, giving world positions, all masked by world
distance to a focus on the wall ahead. The influence radius grows from 80 to 500 m as we approach. Layers:
- world-anchored glyph rain (our procedural glyphs, power-of-two cell LOD);
- block mosh picked in world space and dragged along each block's own reprojected motion;
- row tears;
- a pull toward the focus.

Result: sky, far rims and the left wall stay clean while the near wall turns to code. The progression reads.
Irregularity p90 0.054, about 12 times steadier than A at the same mean change.

**Failures:**
- The first try (fixed 1.8 m cells) was unreadable speckle. Cells must be LOD'd so a glyph is at least about 16 px.
- Glyphs need real antialiasing (SDF or MSDF strokes), not a sampled bitmap.
- The glyphs landed on the water too. Water must be excluded, or show the code only in reflection.
- By the end, too much of the frame is code. The progression needs a ceiling, and recognisable terrain has to
  survive.
- The pull and the mosh here are image-space stand-ins for the vertex pull and the shards, which are not proved.

### 5.4 The Moon (`06`–`09`)

**Setup:**
- A Python height field: 4 km at 2.5 m, about 19,000 craters from the D^-2 law with depth and rim from morphometry,
  degradation by age, and fresh-crater ejecta albedo.
- A 32 km skirt at 32 m, and a curvature drop.
- Rendered by the engine with the canyon's renderer: black background, a 9° sun, 4 cascades.
- The regolith is a 22-op material program: lunar-Lambert from `normal`, `viewDirection` and the sun direction,
  `power −1` as the reciprocal, and a narrow opposition surge on cos(phase).

**Result:**
- The hero crater from its rim (06) reads as the Moon: a hard terminator across the bowl, a bright far wall, a black
  sky.
- Lambert versus lunar-Lambert (08): the program version is flatter in the distance and less sculpted on gentle
  slopes, as real regolith looks.

**Failures:**
- The near field is smooth and soft (2.5 m facets, no rocks). Scale is lost close to the camera.
- Shadow penumbrae are too soft for the Moon. Lunar shadows are knife-edged, and the cascades and PCSS softness need
  tuning.
- There is a faint blue cast in the shadows from some default ambient term, not found yet.
- The first generator drew radial "pie" facets inside craters. The rim wobble was applied to the whole bowl; fixed.
- My constant L was wrong for cross-light (§2.5).

### 5.5 Dusk look-dev (`10`)

The existing knobs only: sun at 3° from a new azimuth, deep zenith, warm horizon, orange-lit cirrus, lavender fog,
dimmer bounce lights.
- At 40 s the far rim catches warm light over a shadowed lower canyon. The sky is pinker than the brief's
  violet-indigo, because the zenith is barely in frame.
- At 9 s the gorge crushes toward black.
- Measured numbers for every proof are in `11-measures.txt`.
