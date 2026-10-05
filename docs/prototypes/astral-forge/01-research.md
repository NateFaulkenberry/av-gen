# The Astral Forge: Research Report

Brief §1, done before any implementation. Branch `proto/astral-forge`, 2026-10-05.

This report asks one question per topic: **what does this technique contribute to a field that becomes an
entity, and does AV Gen adopt it?** Each section ends with a verdict. The verdicts were written before the
prototype was built and then checked against it; where the benchmark changed a verdict, the section says so
(see `04-architecture.md` for the measurements).

---

## 0. The problem, restated as a technical question

The brief's central rule is that *the creature is not a model with particles around it; it emerges from the
field*. In representation terms, that rules out the obvious pipeline (a mesh, plus particles emitted from its
surface). What it asks for is a representation in which:

1. **Matter is primary.** The things that exist are particles (and their density). Nothing else is drawn as
   a solid unless matter is there.
2. **Form is a force, not an object.** The anatomy exists only as a *latent field* that pulls matter toward
   it. With no matter in range, the anatomy is invisible.
3. **Coherence is one scalar** that sets how strongly that latent field binds matter, and how sharply the
   reconstructed surface follows the latent shape.
4. **Collapse is a physical event:** matter that was bound is released with momentum, not faded out.

So the research questions are:

- How do particles become a surface? (implicit surfaces, metaballs, density reconstruction)
- How does a field pull particles toward a shape? (SDF attractors, constraint projection, flow fields)
- How do we make the shape anatomical but impossible? (procedural anatomy, convolution surfaces, domain
  warps, pareidolia)
- How does it look like engraved metal rather than glowing dots? (microfacet metal, anisotropy, thin film,
  diffraction, guilloché, glints)

---

## A. Particle-based implicit surfaces

### Metaballs and soft objects

- **Blinn's "blobby molecules" (1982)** and **Wyvill's "soft objects" (1986)** define a scalar field as a
  sum of radial kernels, one per point, and render the iso-surface `F(p) = T`. Neighbouring kernels add, so
  close points **merge into one continuous surface** instead of remaining separate blobs. This is exactly
  the brief's emergence property: the surface is a function of how *crowded* matter is.
- A compact-support polynomial kernel (Wyvill's `(1 − r²/R²)³`) keeps the sum local, so it can be gathered
  on a grid.
- **Its weakness:** an iso-surface of isotropic kernels is lumpy ("blobby") at the particle spacing. It
  cannot represent a thin plate or a sharp ridge unless particles are much denser than the feature.

### Particle-to-surface reconstruction (fluids)

- **Müller et al. (2003, SPH)** reconstruct fluid surfaces from an SPH colour field: the same metaball sum,
  with SPH kernels.
- **Zhu and Bridson (2005)** replace the sum by a *weighted average position and radius*, then take the
  distance to a sphere at that average. It is much flatter than metaballs on sparse particles.
- **Yu and Turk (2010), anisotropic kernels:** run PCA over each particle's neighbours and stretch its kernel
  along the local distribution. This gives **thin sheets, sharp features and filaments** from the same
  particles. It is the right idea for "filaments, tendons, sheets" at the meso scale. Its cost is a
  neighbour search per particle.
- **Screen-space fluid rendering (van der Laan, Green and Sainz 2009):** splat particle depth, smooth the
  depth buffer (bilateral or curvature flow), shade the result. Very cheap, but it is a 2.5-D surface. It
  has no back side and no self-shadowing, and it falls apart in the brief's INTERNAL and IMPOSSIBLE
  cameras, which need a real 3-D field.
- **GPU Gems 3 ch. 7 (point-based visualisation of metaballs):** particles constrained to a metaball
  surface, splatted as discs. This is the Witkin-Heckbert idea (below) as a renderer.

### Density fields on the GPU

- **Splat particles into a 3-D grid** (cloud-in-cell / trilinear weights), blur it once, and treat the grid
  as a sampled scalar field. This is the standard volumetric-particle path (Houdini's VDB from particles,
  and Niagara's Grid3D), and it is **order-independent if accumulated with integer atomics**, which AV Gen
  already does for agent deposits (ADR-1120, u32 fixed point).
- A mip pyramid of the grid gives **empty-space skipping** for the raymarch and a cheap ambient occlusion
  and volumetric self-shadow (sample density along the normal and toward the light).

### The six states (brief §1 A), as one representation

With a density grid `ρ(p)` reconstructed from the particles, and a threshold `T`:

| State | What the particles do | What the field shows |
|---|---|---|
| 1. Independent particles | free, curl-noise advected | `ρ` is everywhere below `T`; only the particles render |
| 2. Clustered clouds | weak attraction, curl noise | `ρ` peaks locally; small blobs cross `T` in places |
| 3. Filaments | bound to *curves* of the latent anatomy (skeletal lines), with tangential flow | thin tubes of high `ρ`; anisotropic splats would show them; isotropic ones show beads |
| 4. Dense surfaces | bound to the latent *surface* | `ρ` crosses `T` over sheets |
| 5. Recognisable forms | as 4, plus the latent shape sharpens the surface | the iso-surface follows the anatomy |
| 6. Fragmenting | binding released with impulse | sheets tear into blobs, then dust |

**Verdict: ADOPTED.** A hybrid particle/implicit representation is appropriate, and it is the core of the
prototype. Particles are splatted into a u32 fixed-point 3-D grid (order-independent and deterministic),
blurred, and raymarched as an iso-surface. Anisotropic kernels (Yu-Turk) are **deferred**: the latent field
already knows the local surface orientation, so the prototype stretches each particle's *rendered* shard
along that orientation instead, at zero neighbour-search cost. Screen-space fluid rendering is **rejected**
because it has no volume for the internal and impossible cameras.

---

## B. Signed distance fields and ray marching

- **Sphere tracing (Hart 1996)** marches a ray by the distance bound. It needs a Lipschitz-bounded field;
  domain warps break the bound, so a warped field must be marched with a step factor below 1 (we use
  0.6-0.8, measured).
- **Smooth unions (Quílez's polynomial `smin`)** blend primitives with a radius `k`. Here `k` is a
  meaningful *anatomical* parameter: the tissue between features. A large `k` gives a melted, embryonic
  form; a small `k` gives a precise, assembled one. **`k` falls as coherence rises.**
- **Domain operators** (twist, bend, repetition, polar repetition, fold/mirror, domain warping with noise)
  act on the *input coordinates*, so they deform everything evaluated through them, including every
  coordinate the material reads. That is how the engraving follows the deformation (§E).
- **Fractal and deformed distance fields** (Mandelbulb-style iteration, kaleidoscopic IFS) give unlimited
  detail, but they are the brief's banned "generic fractal". They are used only as a *micro-scale* surface
  displacement, never as the form.

**The brief's question:** can an SDF act as a continuously deforming attractor or density field that
guides particles toward anatomy? **Yes, and it is the cleanest formulation found.** For any particle at
`p`, the SDF gives both the distance `d(p)` and the direction to the surface, `−∇d`. So
`p − d(p)·∇d(p)` is the particle's projection onto the latent surface. A spring toward that projection,
scaled by coherence, *is* the attractor. One evaluation of the same function serves the attraction, the
filament flow (the tangent plane is known), and the shard orientation (the normal is known). Because the
SDF never renders directly, the anatomy remains a *force* until matter arrives.

**Verdict: ADOPTED, as the latent layer, never as the visible geometry.** The visible surface comes from
particle density. The SDF additionally *sharpens* that surface as coherence rises (§Architecture E): at
coherence 1 the reconstructed surface is pulled to the SDF's zero set, so the form becomes "terrifyingly
precise", but only where particle density already exists. With no matter, there is no surface.

---

## C. Particle-driven geometry

| Technique | Source | What it gives here | Verdict |
|---|---|---|---|
| Implicit-surface constraint | **Witkin and Heckbert, "Using particles to sample and control implicit surfaces", SIGGRAPH 1994** | particles locked onto a moving implicit surface, with repulsion so they spread evenly, and fission/death by local density | **Adopted in relaxed form.** A spring to the SDF projection, not a hard constraint. A hard constraint has no inertia, and the collapse needs inertia. Repulsion is approximated through the density grid (particles are pushed down the density gradient), which costs one grid sample instead of a neighbour search |
| Curl noise | Bridson, Houriham and Nordenstam, SIGGRAPH 2007 | divergence-free turbulence: matter swirls without clumping or vanishing | **Adopted** for chaos. Its amplitude is `(1 − coherence)`, so chaos is literally what remains when the god lets go |
| Flow fields along the surface | (classic) | filaments that *migrate* over the formed anatomy (brief §6) | **Adopted:** bound particles get a tangential velocity along the engraving direction (§E), so the matter crawls along the same lines the engraving draws |
| Position-based dynamics | Müller et al. 2007; Macklin et al. (unified particles, 2014) | stiff constraints, rigid shards | **Rejected for now.** Distance constraints between neighbours need neighbour lists. A spring to an analytic target gives the look at a fraction of the cost. Revisit for "plates" that hold their shape while flying apart |
| Density-gradient repulsion | SPH pressure, simplified | keeps bound matter from collapsing into a few voxels | **Adopted** through the grid |
| Surface-constrained particles | Meyer et al. 2005 (robust particle sampling) | even coverage of high-curvature features | **Partly:** particles carry a per-particle *feature affinity*, so eyes and teeth get more matter than the cheek. That puts detail where the face is read |
| GPU advection at scale | Niagara, the spike's 1M-4M agents | budget evidence: 1M simple updates ≈ 0.2 ms; with field sampling, ≈ 3 ms (ADR-1120's agents) | Informs the budget in §Performance |

**The brief's emergence curve as dynamics.** Each particle owns a binding threshold `θᵢ ∈ [0,1]`. Its
binding is `bᵢ = smoothstep(θᵢ − w, θᵢ + w, C)`. As coherence `C` rises, more of the matter binds, so
the form *accumulates* rather than fading in. Particles with low `θᵢ` (the first to bind) are assigned to
the features read first: the eyes and the mouth line (see G, pareidolia). A fast drop in `C` releases the
bound particles with an impulse proportional to `−dC/dt` along the surface normal, plus a twist: **the
collapse is an explosion, not a fade.**

---

## D. Metallic and holographic rendering

- **Microfacet conductor BRDF** (Cook-Torrance, GGX/Trowbridge-Reitz, Walter et al. 2007) with a complex
  Fresnel. Gulbrandsen's (2014) artist-friendly metal Fresnel maps *reflectivity* and *edge tint* to `n, k`.
  Dark metals (gunmetal, black chrome) are low-reflectivity conductors with a bright grazing response.
  That is the "strong Fresnel" and "metallic edge lighting" of the brief, physically.
- **Anisotropic GGX** (Burley 2012; Kulla and Conty 2017) stretches the highlight along a tangent. Brushed
  and turned metal **is** anisotropy: grooves blur the reflection *across* the groove. Our tangent field is
  the engraving direction (§E), which gives the guilloché its characteristic moving bands of light.
- **What a metal reflects in a void.** A mirror in a black room is black. Metal reads as metal only through
  the structure of what it reflects. Reflection probes need an environment, and the brief forbids one. The
  answer is a **reflection-only environment**: an analytic set of very bright, thin, moving light *bands*
  (great-circle strips and ring lights in direction space) that the camera never sees directly. It is lit
  only in reflections. This is how product photographers shoot black chrome: strip softboxes against a
  black sweep. Cost: a few dot products per shading point. **Adopted.** It also gives the brief's "moving
  specular sources" and "grazing lights", and it can be driven by the music (a band sweeping across the
  entity on a snare).
- **Glints** (Jakob et al. 2014; Chermain et al.; Zirr and Kaplanyan 2016). Sparse microfacet sparkle is
  what makes metal flake look like metal and not like glowing dots. **Adopted at the particle scale.** Each
  dust particle is an oriented flake with its own normal, and it is only bright when that normal bisects
  the view and one of the light bands. Most of the dust is therefore dark at any moment, and it *glitters*.
  That single choice is the main defence against the "random glowing dots" trap.
- **Rejected:** image-based environment maps (nothing to capture); full LTC area lights (cost, not needed
  for thin bands); screen-space reflections (the entity is mostly convex and self-reflection is minor).

---

## E. Engraving and guilloché

- **What guilloché is, mechanically.** A rose-engine lathe moves the workpiece against a cutter along a
  path modulated by a *rosette* cam. The cut lines are curves of the form `r(θ) = r₀ + a·cos(nθ + φ)`:
  rose curves and epi/hypo-trochoids, repeated with a small phase or radius step between passes. The
  visual magic is **moiré**: two families of nearly parallel modulated lines interfere, and with
  anisotropic reflection each groove family lights at a different angle. As the piece turns, light
  *flows* along the pattern.
- **Translation to 3-D.** A flat texture would be pasted on, which the brief forbids. Instead the
  engraving is a **set of iso-lines of scalar functions defined in the entity's own (warped) coordinate
  system**:
  1. *Feature-centred rose lines.* Around every eye and mouth centre: `fract(k·(r − a·cos(nθ + φ(t))))`,
     with `r, θ` in the feature's local frame. This gives the concentric rings and rosettes that make an
     eye read as *engineered*.
  2. *Contour engraving.* `fract(k·g(p))`, where `g` is a smooth secondary field such as the distance to
     the anatomy's skeleton or the latent SDF evaluated at an offset. These are lines that wrap the form
     like a topographic engraving, or intaglio hatching that follows the volume.
  3. *A second, slightly rotated family*, for the moiré.

  Because these functions are evaluated in the *same warped domain* as the latent SDF, every fold, twist
  and breath of the anatomy carries the engraving with it.
- **Shading the groove.** A V-groove profile perturbs the normal *across* the line (by the derivative of
  the groove profile). The groove direction is the gradient of the line function, crossed with the normal,
  which gives the anisotropy tangent. The lines "crawl" (brief §6) when their phase is advanced in time.
  Antialiasing comes from the pixel footprint: line contrast fades with
  `fwidth(k·g)`, so the engraving does not alias into noise at a distance.
- **Diffraction from the grooves.** A groove spacing `d` near the wavelength of light is a diffraction
  grating, and the grating equation `d(sin θₒ − sin θᵢ) = mλ` (Stam 1999, "Diffraction shaders") gives the
  wavelength reflected toward the viewer, measured *perpendicular to the grooves*. Real engine-turned
  metal shows this as thin rainbow lines that slide as you turn the piece. Werner, Velinov, Jakob and
  Hullin (2017, "Scratch iridescence") show the same physics for individual scratches, with a real-time
  follow-up by Velinov et al. (2018).

**Verdict: ADOPTED.** Rose and contour line fields in the warped latent domain, a V-groove normal, the
anisotropy tangent from the line direction, and a first-order grating term for colour. That gives
**colour only where engraving and geometry make it physically plausible, and only at specific angles.**
This is the brief's "holographic metal artifact, not RGB glow".

---

## F. Holographic and diffraction imagery

- **Fresnel** (above). The grazing rim is where metal reflects the most; it is the natural place for
  "edge energy" (brief §9).
- **Thin-film interference.** **Belcour and Barla (2017)** derived an analytic, RGB-prefiltered thin-film
  term for microfacet BRDFs (the basis of glTF's `KHR_materials_iridescence` and Blender's Principled BSDF
  thin film). A film of thickness `t` and index `η` shifts the reflected colour with the view angle, since
  the optical path is `2ηt·cos θₜ`.
- **The key finding for the colour language: temper colours.** Heated steel grows an oxide film, and its
  colour is thin-film interference. The sequence with increasing thickness is **straw → bronze → rose/purple
  → blue → grey-blue** (Bhadeshia, Cambridge, "Oxide on steel and tempering colours"). So *the brief's
  palette is physically the palette of heat-treated metal*: violet, deep blue, gold and occasional
  orange, on grey. A forge produces exactly these colours, without any neon. Mapping **energy state to
  oxide thickness** makes the colour mean something: a cold entity is gunmetal; a charged one blooms
  through gold into violet and blue. It is the same metal, tempered by the music.
- **Spectral dispersion and microstructure colour.** These are covered by the grating term (E). A full
  spectral renderer is unnecessary: a handful of wavelength samples (6-8), converted to RGB with a CIE fit,
  is enough for the grating and the thin film.

**Verdict: ADOPTED.** A thin-film term with thickness driven by energy and archetype (the temper scale), a
grating term on the engraving, and Fresnel rims. **No hue is ever assigned directly.** Colour exists only
through these three mechanisms, which is why the result must still read in greyscale (it is tested).

---

## G. Procedural creatures and generative anatomy

### Building anatomy from fields

- **Convolution surfaces** (Bloomenthal and Shoemake 1991): integrate a kernel along a skeleton (segments,
  arcs, triangles). The result is smooth organic blending of limbs, horns and tendrils from *lines*. The
  SDF equivalent is a capsule or cone distance along a curve, smooth-unioned. **Adopted:** horns, spines,
  tendrils and wing ribs are curved capsules along analytic arcs and helices.
- **Facial features as fields.** An eye is a sphere pressed into a socket with *nested* concentric rings.
  A mouth is a slit (a flattened ellipsoid subtracted from a plate) with teeth as tapered cones along the
  lips. The brow is a ridge. A face is a curved *plate* (a thick shell of an ellipsoid), not a head. This
  is deliberately a mask without a skull behind it, which keeps it away from the stock skull and demon
  head.
- **Domain repetition** gives repeated anatomy (rows of teeth, ranks of eyes, choirs of faces) for the
  cost of one feature (Quílez's `opRep`, polar repetition).
- **Asymmetry.** Mirror `x` for a symmetric face; for the half-human, half-geometry face, evaluate the left
  half smooth and the right half through a *quantised domain* (facets: snap the normal direction to a
  polyhedral set, or use a max-norm distance). The midline seam is a smooth blend.

### Pareidolia: what a face actually needs (the perceptual research)

- Illusory faces are triggered by **two eyes above a mouth, in a T-shaped configuration** (the classic
  account). **Hadjikhani et al. (2009, MEG)** found that objects seen as faces activate the fusiform face
  area early (about 165-170 ms), like real faces. **Wardle et al. (2020, Nature Communications)** found
  that the brain represents an illusory face as a face very quickly, and then re-codes it as an object
  within a few hundred milliseconds. More recent single-unit work (Nature Communications 2024) finds that
  face cells respond mainly to the *parts*, especially the eye-region features, more than to the
  configuration.
- **What this means for design** (the brief's final target, "that's a face... wait, that isn't a face", is
  *literally* the Wardle time course):
  1. **Bind the eyes first.** The lowest binding thresholds `θᵢ` go to eye matter, then the mouth line,
     then the brow and plate, and the rest of the head never. A face reads at low coherence if the eyes
     are coherent, so the face appears *early* and the rest stays ambiguous.
  2. **A face flash should last about 150-400 ms** on a transient: long enough for the face response,
     short enough that the object re-coding ("wait, that isn't a face") arrives when the form collapses.
  3. **Concentric pupils and nested faces** are free: the eye response is driven by *eye-like parts*, so
     an eye with three pupils still reads as an eye, and then wrongly.

### Archetypes and references to avoid

The archetypes are compositions of the same parts (plates, eyes, mouths, horns, wings, tendrils) with
different symmetries, binding orders and colours. To avoid the brief's banned imagery:

- No skull topology (no cranium, no nasal cavity, no jaw hinge). Faces are masks or plates.
- No horns on a head; horns grow from the field and fold through the plate (§7).
- The Seraph's wings are fans of thin blade *plates* of engraved metal, not feathers.
- Ring-of-eyes imagery is used once and sparingly (it is a 2020s meme as "biblically accurate angels").
- **Not recreating band imagery.** The obvious nearby references are the large-scale visionary-art concert
  visuals of progressive metal (anatomical light bodies, many-eyed faces, sacred-geometry grids). The
  prototype uses **no sacred-geometry overlays, no translucent anatomical "energy body" rendering, and no
  flame or electricity**. Its signature is the opposite: *opaque engraved metal assembled from dust*.

---

## H. Systems this design borrows from

| System | What it does | What we take |
|---|---|---|
| Media Molecule's *Dreams* (Evans, "Learning from Failure", SIGGRAPH 2015) | CSG/SDF sculptures turned into dense point clouds, and splats rendered in compute; abandoned pure SDF raymarching because it "left very little room for imagination" | The strongest precedent for **SDF as the definition and points as the image**. Also a warning: a pure SDF render looks like a math demo. Our latent SDF never renders on its own |
| Schütz, Kerbl and Wimmer (2021), compute point rasterisation | Software splatting of points with 64-bit atomics, faster than the hardware pipeline for tiny points | **Adopted in WebGPU form:** WGSL has no 64-bit atomics, so dust is splatted with **u32 additive fixed-point atomics** (order-independent, deterministic), depth-tested against the surface. Opaque large shards would need the depth-packed `atomicMax`, which is deferred |
| Niagara Grid3D / Houdini VDB-from-particles | particles into volumes | The density-grid path |
| AV Gen ADR-1120 | u32 fixed-point agent deposits into a grid | Same accumulation technique, so it ports to production directly |

---

## I. What existed in AV Gen before this work

A read-only survey of `gpu/productionization` (6f05c17a). The short answer: **production has most of the
supporting pieces and none of the core.**

| Need | In production? | Where / limit |
|---|---|---|
| SDF raymarcher, smooth ops, twist, bend, repeat, polar repeat, fold, warp, recurse, audio-driven displacement | **Yes** | `shaders/sdf.wgsl`, `src/spatial/sdf.*`; ≤ 96 nodes, depth 16; node fields are parameters |
| SDF as a force or attractor on particles | **No** | `sdfDistance` was removed (ADR-704) |
| GPU particles: curl noise, ≤ 4 field forces, one point attractor, trails, velocity stretch | **Yes** | `particle_renderer.cpp`, `particles.wgsl`; unlit emissive billboards only (no metal, no orientation shading) |
| Particles → density → surface (metaballs, splatting) | **No** | particles never write a grid. Grids are gather-only, at most 128³; agents are 2-D only |
| Thin film / iridescence on surfaces | **No** | only the Bubble effect has thin film; glTF iridescence is detected, not rendered |
| Anisotropic highlights | Partial | a material-program op that only adjusts roughness |
| Engraving line patterns | **No** | material programs have no sine op and no feature-local coordinates |
| Metal reflection in a void | Partial | `environment.sky` with `background:false` gives a reflection-only cube, but a static one |
| Face or anatomy generator | **No** | SDF mannequins (capsules) in `tools/liminal/`, without faces |
| Post: bloom, halation, ACES/AgX, grain, CA, DOF, motion blur, glitch | **Yes** | `post_settings.*` |
| Song structure: sections, phrases, builds, drops, downbeats | **Yes** | `analysis/structure.hpp` (ADR-206, Foote novelty), `signals/musical_events.hpp` |
| Spectrum and onset audio fields per element | **Yes** | ADR-1116 |

**Consequence.** The emergence mechanic (particles bound to a latent SDF, a density field reconstructed
from them, a metal material with engraving and thin film) needs **four** things production lacks. Building
them in the engine first would be a large, destabilising change made before anyone knows whether the look
works. The prototype is therefore **standalone and isolated** (`prototypes/astral-forge/`, behind
`-DAVGEN_ASTRAL_FORGE_PROTOTYPE=OFF`), like the GPU-world spike. It reuses production's audio analysis
(`AnalysisTrack`, `detectStructure`) unchanged. `04-architecture.md` lists what would have to move into
the engine, in what order, if the look is kept.

---

## Summary: adopted, deferred, rejected

| Technique | Status | Why |
|---|---|---|
| Particles splatted into a u32 3-D density grid, blurred, raymarched as an iso-surface (metaballs) | **Adopted: the visible surface** | matter is primary; order-independent and deterministic; supports internal and impossible cameras |
| SDF anatomy as the *latent attractor*, never drawn | **Adopted: the form** | one evaluation gives distance, normal and tangent: attraction, flow and orientation |
| SDF sharpening of the density surface, scaled by coherence | **Adopted** | gives "terrifyingly precise" at coherence 1 without drawing a model |
| Per-particle binding thresholds (eyes first) | **Adopted** | emergence by accumulation; pareidolia ordering |
| Curl noise scaled by (1 − C), collapse impulse ∝ −dC/dt | **Adopted** | chaos and violent collapse |
| Domain warps (twist, fold, bend, polar repetition) applied to latent and engraving together | **Adopted** | the dimensional-fold layer, carried by the material |
| Anisotropic GGX with tangents from engraving lines, V-groove normals | **Adopted** | metal, not shiny plastic |
| Guilloché: rose and contour line fields in the warped domain | **Adopted** | engraving follows the surface |
| Grating (diffraction) term on grooves; thin-film temper colours | **Adopted** | colour from physics; reads in greyscale |
| Reflection-only light bands (black-sweep studio) | **Adopted** | metal in a void |
| Particles as oriented flakes with glint shading | **Adopted** | glitter, not glowing dots |
| Compute splatting (Schütz) of dust | **Adopted** | cheap tiny points |
| Anisotropic reconstruction kernels (Yu-Turk) | Deferred | neighbour search cost; orientation from the latent instead |
| PBD rigid plates | Deferred | needs neighbour constraints |
| Screen-space fluid surfaces | Rejected | 2.5-D; fails the internal camera |
| Fractal forms (Mandelbulb, IFS) as the shape | Rejected | the brief's generic-fractal trap; micro displacement only |
| Image-based environments, SSR | Rejected | nothing to reflect in a void |

---

## Sources

- Blinn, "A Generalization of Algebraic Surface Drawing", ACM TOG 1982. Wyvill, McPheeters and Wyvill,
  "Data Structure for Soft Objects", The Visual Computer 1986.
- Witkin and Heckbert, "Using particles to sample and control implicit surfaces", SIGGRAPH 1994,
  <https://dl.acm.org/doi/10.1145/192161.192227>,
  <https://www.ri.cmu.edu/pub_files/pub1/witkin_andrew_1994_1/witkin_andrew_1994_1.pdf>
- Meyer, Georgel and Whitaker, "Robust particle systems for curvature dependent sampling of implicit
  surfaces", SMI 2005, <https://users.cs.utah.edu/~miriah/publications/particle_sampling.pdf>
- GPU Gems 3, ch. 7, "Point-Based Visualization of Metaballs on a GPU",
  <https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-7-point-based-visualization-metaballs-gpu>
- Müller, Charypar and Gross, "Particle-based fluid simulation for interactive applications", SCA 2003.
  Zhu and Bridson, "Animating sand as a fluid", SIGGRAPH 2005.
- Yu and Turk, "Reconstructing surfaces of particle-based fluids using anisotropic kernels", SCA 2010,
  <https://dl.acm.org/doi/10.1145/2421636.2421641>
- van der Laan, Green and Sainz, "Screen space fluid rendering with curvature flow", I3D 2009.
- Hart, "Sphere tracing", The Visual Computer 1996. Quílez, distance functions and smooth minimum articles
  (iquilezles.org).
- Bridson, Houriham and Nordenstam, "Curl-noise for procedural fluid flow", SIGGRAPH 2007.
- Müller et al., "Position based dynamics", 2007; Macklin et al., "Unified particle physics for real-time
  applications", SIGGRAPH 2014.
- Bloomenthal and Shoemake, "Convolution surfaces", SIGGRAPH 1991.
- Walter et al., "Microfacet models for refraction through rough surfaces", EGSR 2007. Burley, "Physically
  based shading at Disney", 2012. Gulbrandsen, "Artist friendly metallic Fresnel", JCGT 2014. Kulla and
  Conty, "Revisiting physically based shading at Imageworks", 2017.
- Jakob et al., "Discrete stochastic microfacet models", SIGGRAPH 2014. Zirr and Kaplanyan, "Real-time
  rendering of procedural multiscale materials", I3D 2016.
- Stam, "Diffraction shaders", SIGGRAPH 1999, <https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/diff.pdf>
- Werner, Velinov, Jakob and Hullin, "Scratch iridescence: wave-optical rendering of diffractive surface
  structure", SIGGRAPH Asia 2017, <https://arxiv.org/abs/1705.06086>. Velinov et al., "Real-time rendering
  of wave-optical effects on scratched surfaces", CGF 2018,
  <https://onlinelibrary.wiley.com/doi/10.1111/cgf.13347>
- Belcour and Barla, "A practical extension to microfacet theory for the modeling of varying
  iridescence", SIGGRAPH 2017,
  <https://belcour.github.io/blog/research/publication/2017/05/01/brdf-thin-film.html>; Khronos
  `KHR_materials_iridescence`.
- Bhadeshia, "Oxide on steel and tempering colours", University of Cambridge,
  <http://www.phase-trans.msm.cam.ac.uk/2008/Oxide/Oxide.html>
- Guilloché and engine turning: <https://en.wikipedia.org/wiki/Guilloch%C3%A9>; rose curves
  <https://mathworld.wolfram.com/RoseCurve.html>; epitrochoids <https://en.wikipedia.org/wiki/Epitrochoid>;
  "Trochoids, Roses, and Thorns: Beyond the Spirograph",
  <https://www.researchgate.net/publication/276029259_Trochoids_Roses_and_Thorns-Beyond_the_Spirograph>
- Hadjikhani, Kveraga, Naik and Ahlfors, "Early (M170) activation of face-specific cortex by face-like
  objects", NeuroReport 2009. Wardle, Taubert, Teichmann and Baker, "Rapid and dynamic processing of face
  pareidolia in the human brain", Nature Communications 2020,
  <https://linateichmann1.github.io/wardle_2020_NatCom.pdf>. "Face cells encode object parts more than
  facial configuration of illusory faces", Nature Communications 2024,
  <https://www.nature.com/articles/s41467-024-54323-w>
- Evans, "Learning from failure: a survey of promising, unconventional and mostly abandoned renderers for
  Dreams PS4", SIGGRAPH 2015 Advances,
  <https://www.mediamolecule.com/blog/article/alex_at_umbra_ignite_2015_learning_from_failure_video>
- Schütz, Kerbl and Wimmer, "Rendering point clouds with compute shaders and vertex order optimization",
  CGF 2021, <https://www.cg.tuwien.ac.at/research/publications/2021/SCHUETZ-2021-PCC/SCHUETZ-2021-PCC-paper.pdf>
