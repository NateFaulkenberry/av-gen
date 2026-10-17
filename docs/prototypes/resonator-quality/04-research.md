# Resonant Visual Worlds: research (step A)

The brief is [03-brief-resonant-visual-worlds.md](03-brief-resonant-visual-worlds.md). The owner's instruction was to
research first and think like a VFX artist. This file collects what professionals do in these genres, which of
those techniques this engine can actually express, and the specific moves the plan ([05-plan.md](05-plan.md))
borrows. The candidate approaches and the arguments between them are in [05-plan.md](05-plan.md) §1.

## 1. Cymatics and modal patterns: what makes them read as *different families*

**How real patterns work.**
- A vibrating plate's figure is a superposition of eigenmodes. Sand collects on the nodal set, where the plate does
  not move.
- The figure that appears is the weighted sum of the modes near the driving frequency, each weighted by its value
  at the drive point. Two near-degenerate modes in combination are what make the striking asymmetric figures. This
  is Grinsted's and Waller's observation on compounded modes.
  - Sources: [Buchanan, CNU capstone](https://pcs.cnu.edu/dgore/Capstone/files/BuchananJ.pdf);
    [NASA plate-vibration report](https://ntrs.nasa.gov/api/citations/19870004346/downloads/19870004346.pdf).
- **Two families come from the plate's shape:**
  - **Rectangular / Chladni-square:** the nodal condition `cos(nπx/L)cos(mπy/L) − cos(mπx/L)cos(nπy/L) = 0`. That
    is a *difference* of two products, so n≠m gives curved networks of crossing lines, diagonals and closed loops,
    and nothing like a target
    ([Barbe generative diary](https://barbegenerativediary.com/en/tutorials/how-to-create-a-chladni-figure-with-processing/);
    [Chladni's square-plate table](https://web.mit.edu/redingtn/www/netadv/SP20140804.html)).
  - **Circular:** `J_m(k r)·cos(mθ)`, with m nodal diameters and n nodal circles. This is the target and flower
    family ([PSU membrane animations](https://www.acs.psu.edu/drussell/Demos/MembraneCircle/circle.html);
    [Oberlin applet](https://www2.oberlin.edu/faculty/brichard/Apples/CirclePage.html)).
- **What professionals do in practice.**
  - SideFX's own Chladni node mixes *two waves of non-integer, anisotropic frequency* (b is a scaled copy of a,
    with separate horizontal and vertical frequencies).
  - It offers three outputs: **nodal lines**, **|amplitude|** and **signed amplitude**. Artists choose per shot
    whether the *line art*, the *lobes* or the *sign* carries the look
    ([SideFX Chladni COP](https://sidefx.com/docs/houdini/nodes/cop/chladni.html)).
  - Method Studios (Black Panther) found no published recipe, built cymatics "from a math formula" on a point cloud
    so they could animate it, and art-directed which modes appeared
    ([SideFX: Black Panther method](https://www.sidefx.com/community/black-panther-method/)).
- **Interference landscapes in motion design** displace a plane by the *amplitude of several circular waves* from
  different centres. Loris Cecchini's *Wallwave Vibrations* is the art-world reference: the figure's richness comes
  from several centres and frequencies, not from one
  ([Entagma: interference patterns](https://entagma.com/interference-patterns/);
  [Cecchini](https://mymodernmet.com/loris-cecchini-wallwave-vibrations-extruding-bodies-sculptures/)).

**Why Cymatic World always looks radial** (from the code, `tools/cymatic_world/build.py`):
- All three of the large and medium sources are `point` sources. Their modes are `R_m(kr)·cos(m(α−θ))`, the
  circular family.
- Their centres sit within 10 m of each other, so their rings nest.
- The only non-radial source, the `lattice`, is the faint fine carpet (amplitude 0.1).
- The focus sweep and competition change *which order* leads, but every candidate is circular, so the figure is
  always a target with petals.

The engine already has the second family: a `lattice` source is `n^-1/2 Σ cos(k x·d_j)` over n directions. Two
directions half a turn apart give `cos(kx)+cos(ky)`-type nets; three give hexagonal ones. It is simply not used at
the large scale.

**Borrowed:**
- **Several distinct families,** each a source with its own centre and geometry:
  - **rectangular** (lattice, n = 2, two wavenumbers);
  - **hexagonal** (lattice, n = 3);
  - **circular** (point, kept but *one* of several);
  - **dipole / asymmetric** (point, low order, centre far off-frame, so arcs not rings).
- **Near-degenerate pairs** (two modes of close wavenumber, slightly different orientation) for asymmetric figures.
- **The SideFX choice of output per family.** Some families read as nodal lines (sand ridges), some as signed lobes.
- **A family mix that moves on the phrase scale,** so the topology, not just the amplitude, changes.

## 2. Material: avoiding the plastic look

**Diagnosis.** CG reads as "plastic" when a single smooth, neutral-white highlight sits on a uniformly glossy
surface whose colour comes from reflection rather than from the body. For dielectrics the highlight takes the
*light's* colour, so a white key on a coloured gloss is the plastic recipe.
- The fixes artists use: a gloss/roughness mask so only some areas catch highlights, broader (rougher) lobes, and
  diffuse rather than ambient carrying the colour
  ([NYU CG lecture](https://cs.nyu.edu/~dzorin/cg12/lecture8.pdf);
  [Unity: coloured rim lighting](https://discussions.unity.com/t/colored-rim-lighting/466236);
  [Blender wikibook: metal vs plastic](https://en.wikibooks.org/wiki/Blender_3D:_Noob_to_Pro/Metal_Versus_Plastic)).
- **Wrap lighting** (`(N·L + w)/(1 + w)`) lowers diffuse contrast, so less fill is needed and recesses keep colour.
  Green shifts the terminator's colour, a cheap scattering look
  ([GPU Gems ch.16](https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-16-real-time-approximations-subsurface-scattering);
  [Kaplanyan/Nowrouzezahrai, JGT](https://cim.mcgill.ca/~derek/files/publication-files/jgt_wrap_final.pdf)).
- **Cloth and silk:** the industry model is a **sheen lobe** (Imageworks' microfacet sheen, glTF
  `KHR_materials_sheen`). It is back-scattering at grazing angles, which is what makes velvet and satin glow at their
  folds instead of shining in a dot
  ([Estevez & Kulla 2017](https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_sheen.pdf);
  [glTF sheen PR](https://github.com/KhronosGroup/glTF/pull/1688)).
  - Satin specifically is **anisotropic**: highlights stretch *along* the threads into soft bands.

**What this engine has** (read in `shaders/pbr_shade.wgsl`, `docs/decisions`):
- **The "styled" lit path** (`environment.stylized`): diffuse plus a **hemisphere ambient whose sky and ground
  colours are scene parameters** (`styledSkyAmbient`, `styledGroundAmbient`, `styledAmbientFloor`), F0 0.04, and no
  IBL mirror reflection. This is a ready-made chromatic fill: up-facing and down-facing surfaces take different
  palette colours.
- **Reflection-only bands** (ADR-1151): soft-box and strip "lights" seen *only in reflections*, coloured
  (`warmth`), wide, optionally dashed and rotating. These are soft, broad, coloured highlights instead of GGX dots,
  the studio-softbox look of product and fabric renders.
- **Thin film and anisotropy** (ADR-1143): an anisotropic lobe exists, but its tangent is the object's +Y (Silk's
  report). Thin film is already on Silk.
- **Cel/toon** (ADR-1071): not wanted here.
- **Material programs:** can paint any light term (Cymatic World is unlit with painted light), and a Fresnel input
  gives a cheap sheen approximation (`pow(1 − N·V)` × colour, added as emission or albedo lift at grazing).

**Borrowed:**
- the body colour carries the image (already started in the quality pass);
- coloured wrap and hemisphere fill instead of white key plus black;
- highlights from **coloured reflection bands** rather than a hard white key;
- a **painted sheen lobe** on Silk;
- **roughness that varies with the field** (programs output roughness), so gloss sits on ridges and crests, matte
  in hollows, and not uniformly.

## 3. Environments that are not black voids

**Painters' and concept artists' atmospheric perspective.** Distant forms take the sky's colour, lose value
contrast, and desaturate. Layer the space as an opaque near layer, a hazed middle and a soft, muted background. The
fog colour carries the mood
([GameDev.net: atmospheric perspective](https://gamedev.net/tutorials/visual-arts/easy-atmospheric-perspective-in-photoshop-r2521);
[Pluralsight: atmospheric depth](https://pluralsight.com/courses/atmospheric-depth-illustrations-photoshop-924)).

**Iñigo Quilez's fog recipes** are the real-time version
([iq: fog](https://iquilezles.org/articles/fog/)):
- exponential fog;
- **fog coloured toward the light direction** (`mix(blue, yellow, pow(max(dot(rd, L), 0), 8))`), so the air itself
  carries a gradient;
- **per-channel extinction**, so distance shifts hue, not just value;
- **height fog** with a closed form.

**What this engine has:**
- a procedural **sky** (zenith, horizon and ground colours, haze, and a sun disc and glow of any colour) that can
  stand behind the scene (`background`) and be the IBL;
- **fog with `fogSky`** (fog takes the sky's colour toward the horizon), height fog, `horizonDensity`;
- volumetric fog with **`volumeColorField`** and **`volumeDensityField`**: a *field* can colour and shape the air;
- ADR-207 world waves, which travel through the world's lighting;
- and every scene field can be read by a material anywhere, including on very large distant geometry.

**Borrowed:**
- **The background is the world continuing,** not a backdrop. Cymatic World extends the field to the horizon with
  aerial perspective. Waveform's distant sheets dissolve into wave-banded air. Silk's far cloth layers fade into a
  coloured haze.
- **A designed sky gradient** in palette colours (bound by role), with a coloured "sun" glow giving the frame a
  light direction.
- **Fog coloured by the palette and pushed per channel,** so recesses and distance are *coloured* dark.

## 4. Waves and travelling interference

- **Wave sums in real-time water.** Sums of directional sine and Gerstner waves with different wavelengths,
  directions and speeds are the standard real-time route. Large waves set the form; small waves ride on them
  ([GPU Gems ch.1](https://developer.nvidia.com/gpugems/GPUGems/gpugems_ch01.html)).
- **Interference "on surfaces"** (Entagma, Houdini 19.5) evaluates circular-wave interference on a curved surface,
  using geodesic distance, so the pattern follows the form
  ([Entagma: interference patterns on surfaces](https://entagma.com/interference-patterns-on-surfaces/)).

**What this engine has:**
- Waveform's resonator (point and lattice sources);
- ADR-1116 `onset` fronts (spherical shells that travel at `audioSpeed`);
- `spectrum` fields with a travelling history (the bass swell of last seconds laid out in space);
- `wave` fields (spherical or plane, `waveSpeed`).

So true *propagating* fronts, which the current scene uses only for a kick shell, are available.

**Borrowed:**
- a **hierarchy of wave scales**: a large slow swell sets the form, medium crossing families make interference, and
  fine ripples ride on top;
- **fronts that visibly travel** (spectrum history, onsets) as the primary event;
- **interference made visible** in colour and brightness at constructive crossings only (`|A1+A2|` versus
  `|A1|+|A2|`);
- **the same wave logic in the background at a slower, larger scale.**

## 5. Composition for abstract work

- **Hierarchy comes from contrast:**
  - an off-centre focal event;
  - scale contrast between large and small forms;
  - leading lines (repeated curves read as movement);
  - **negative space by design**: a figure/ground that is stable rather than accidental
  ([Kadenze: design principles in motion graphics](https://blog.kadenze.com/creative-technology/applying-design-principles-to-motion-graphics/);
  [Willamette art & design notes](https://people.willamette.edu/~gorr/classes/ids101/links/artDesign.htm)).
- **Notch and TouchDesigner shows** (Rich & Miyu's *Resonant Matter* for Yamaha) are built from procedural forms
  whose material changes carry the arc, not particles. Background clouds and fields sync to the music at a slower
  rate than the foreground
  ([Notch: Resonant Matter](https://www.notch.one/madewithnotch/resonant-matter-yamaha);
  [Notch: FKJ world tour](https://www.notch.one/madewithnotch/fkj-world-tour-22-24)).

**Borrowed:**
- every frame is planned in three value zones: a bright focal event, a coloured mid-value field and a dark but
  hued distance;
- **one focal event at a time** per scene, off-centre;
- the background moves slower and broader than the foreground;
- the arc lives in *which* structure leads, not in camera motion.

## 6. Engine constraints that shape every choice (measured in the quality pass)

- **Material interpreter:** about 0.6–0.8 ms per op at High at full coverage, a 48-op cap, and at most 4 fields
  per program (hoisted, so cheap).
- **Fields:** 16 GPU fields per scene. Cymatic World uses all 16.
- **Vertices are expensive** wherever deformers run per vertex: Cymatic World measured about 30 ns a vertex at
  High; Silk is vertex-bound. Large distant geometry must therefore be cheap (few vertices, no deformers, or a
  baked field).
- **Live 30 fps at 1080p, Auto.**
  - The machine is shared and noisy: absolute timings varied by up to 15% across a day.
  - The quality pass's paired before/after protocol stays.
