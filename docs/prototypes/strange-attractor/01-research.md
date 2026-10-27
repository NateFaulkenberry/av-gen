# The Strange Attractor: research (Stage 0)

One researcher covered workstreams A (procedural mathematics), B (the AV Gen architecture) and C (art direction), per
the owner's rule. Research stopped once a decision could be defended. It then moved to cheap rendered proofs in AV
Gen itself, because the renderer is the judge. Section 5 records what those proofs decided.

**How to read this.** Every point is tagged:
- **[E] Established.** Published, reproducible, or measured here.
- **[H] Hypothesis.** An artistic or technical bet that the renders will confirm or kill.

## 1. Procedural mathematics (workstream A)

### 1.1 Fractal terrain and multiscale noise

- **[E] fBm.** fBm (fractional Brownian motion) sums octaves of a band-limited noise, with amplitude falling by a gain
  per doubling of frequency. Its spectrum follows a power law, which gives it a fractal dimension (Mandelbrot, *The
  Fractal Geometry of Nature*, 1982).
  - Plain fBm is statistically the same everywhere: "homogeneous and isotropic". That is why it reads as generic
    noise terrain, the brief's first failure mode.
  - Sources: [Quilez, "fBm"](https://iquilezles.org/articles/fbm/);
    [Musgrave, Kolb, Mace 1989](https://history.siggraph.org/?p=119354).
- **[E] Ridged and hybrid multifractals.** Musgrave's multifractals (Ebert et al., *Texturing & Modeling: A Procedural
  Approach*, 3rd ed., ch. 16) make the roughness depend on the location. Each octave is weighted by the previous
  octave's signal, so detail gathers on ridges while valleys stay smooth. This is the standard cure for uniform
  noise.
- **[E] Domain warping.** Sample the noise at `p + fbm(p)`, or nest it (`fbm(p + fbm(p + fbm(p)))`). The result is
  flowing, folded patterns that plain noise cannot produce
  ([Quilez, "warp"](https://iquilezles.org/articles/warp/)).
- **[E] Erosion-like detail without simulation.** Recent "erosion filter" noises make gullies by striping along the
  local downhill direction, phased from jittered per-cell pivots and blended between cells. Every point can then be
  evaluated on its own.
  - Sources: [runevision, "Fast and Gorgeous Erosion Filter" (2026)](https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html),
    building on Clay John's and Fewes's Shadertoy erosion noises.
  - The code is MPL-2.0. **We did not copy it.** `attractor_world.cpp` implements the idea from the description:
    downhill stripes, cell pivots, and the "unit beyond 0.5" normalisation.
- **[E] Readability depends on frequency against viewing scale.**
  - Detail below about 2-3 px at the viewing distance is aliasing, not detail.
  - At 1080p with a 38 degree vertical field of view, one pixel is about 0.6 milliradians: 0.6 m at 1 km, 6 m at
    10 km.
  - So a 2048² height field over 16 km (7.8 m texels) carries the middle and far distance, and the near field needs
    analytic detail in the shader.
- **[H] Height field or not.** A height field is enough for the terrain of the first prototype, because canyon
  country is 2.5D. Overhangs and arches (Stage 2) need SDFs on top of it, not a different terrain representation.
  **Confirmed for the terrain (section 5).** Not yet tested for the landmarks.

### 1.2 Strange attractors and nonlinear dynamics (for Stage 3; chosen now)

| System | Equations (canonical parameters) | What its geometry offers | Verdict |
|---|---|---|---|
| Lorenz (1963) | `x' = s(y-x), y' = x(r-z)-y, z' = xy-bz` (s 10, r 28, b 8/3) | the "butterfly": two lobes, instantly recognisable | too familiar, reads as a logo ([Lorenz 1963](https://doi.org/10.1175/1520-0469(1963)020%3C0130:DNF%3E2.0.CO;2)) |
| Rössler (1976) | `x' = -y-z, y' = x+ay, z' = b+z(x-c)` (0.2, 0.2, 5.7) | a flat spiral band with one fold up and back | good: one sheet, mostly planar, one dramatic fold. A natural arch or ribbon ([Rössler 1976](https://doi.org/10.1016/0375-9601(76)90101-8)) |
| Thomas (1999) | `x' = sin y - bx` (cyclic), b about 0.18-0.21 | a lattice of loops, cyclically symmetric; chaotic below b about 0.208 | good for a nested, labyrinthine knot of loops ([Thomas 1999](https://doi.org/10.1142/S0218127499001383); [summary](https://en.wikipedia.org/wiki/Thomas%27_cyclically_symmetric_attractor)) |
| Aizawa | 6 parameters | a sphere pierced by a tube | reads as an ornament, not a landform |
| Halvorsen | `x' = -ax - 4y - 4z - y²` (cyclic) | three interlocked lobes | busy, self-intersects at any thickness |

- **[E] Numerics.**
  - Fixed-step RK4 is the standard choice for drawing these. Use a step of about 0.005-0.01 in system time.
  - Discard a transient of about 1,000-5,000 steps so the trajectory is on the attractor.
  - Normalise by the bounding box of the kept samples, and resample by arc length so the tube's segments are even.
  - Guards: refuse non-finite states, and clamp the state magnitude. Then a parameter outside the chaotic range
    degrades to a limit cycle or a fixed point, never to infinity.
- **[H] Which system.**
  - **Rössler** is first choice: one folded band, read as a stone ribbon that rises out of the gorge, loops and goes
    back into the cliff.
  - **Thomas** is the alternative, for a nested, recursive-looking knot.
  - Each is chosen for its geometry, and the stage will test both.

### 1.3 Recursive and self-similar geometry (for Stage 2)

- **[E] References.**
  - L-systems and branching: Prusinkiewicz and Lindenmayer, [*The Algorithmic Beauty of Plants*](http://algorithmicbotany.org/papers/#abop).
  - Space colonisation: [Runions, Lane, Prusinkiewicz 2007](http://algorithmicbotany.org/papers/colonization.egwnp2007.html).
  - Recursive SDF folds (IFS-style `p -> fold(p) * s - t`), which AV Gen's `recurse` node already implements (ADR-1001).
- **[H] Recursion as erosion, not ornament.** A recursive landmark that reads as geology rather than as a fractal
  ornament keeps one generator across scales:
  - an arch cut through a fin, holding a smaller arch cut through its own buttress, and so on, with depth 3-4 and the
    scale ratio about 0.45;
  - stepped buttes whose every step repeats the plateau's own cliff-and-talus profile.

  The plateau field of section 5 is already self-similar in plan (its edge is a fractal coastline). The landmarks
  should repeat its section.

### 1.4 Spatial distortion

- **[E] Two kinds of distortion.**
  - **Geometric:** warping the coordinates the world is built from.
  - **Optical:** perspective, occlusion and scale cues that mislead (forced perspective, false horizons).
  - Domain warping does the first. The second is composition.
- **[H] The surreal move is geological impossibility, not visual noise.** Strata that fold in waves no sediment would
  make. Layers that bend around the attractor's ribbon as if the rock had flowed around it. Arches nested inside
  arches. The camera and light stay naturalistic, as Tanguy, Dalí and Kay Sage kept a believable sky over impossible
  objects.

## 2. AV Gen architecture (workstream B)

- **[E] The Environment seam (ADR-1200; used by ADR-1221 and ADR-1289).**
  - A scene-level JSON block, a struct on `scene::Scene`, and parameters registered by the Composition.
  - An `EnvironmentRenderer` records its own passes after the lit pass. It writes HDR and depth, so the medium,
    bloom, tonemap, outputs and live quality ladder treat its pixels like any other.
  - `rendering::CloudRenderer` is the smallest model: one fullscreen pass, 231 lines.
  - **This is how a scene owns a custom renderer, and it is what the Strange Attractor uses (ADR-1850).**
- **[E] What exists and was not used.**
  - `world::generateTerrain` (ADR-090): a game-world terrain of rivers, ponds and biomes. Its vocabulary (ridges,
    valleys, ponds) is not this scene's, and it exposes no warp or strata.
  - `SdfTree` (ADR-027): a 96-node interpreted or compiled SDF raymarcher. It has no height-field or curve primitive.
  - Splines, tubes and the procedural instancing: mesh representations. They would show hard intersections with a
    ray-marched terrain.
  - Each stays available. For Stage 2, the plan is to evaluate SDF landmarks inside the same pass as the terrain, so
    they can blend into it with smooth-min fillets.
- **[E] Parameters and modulation.**
  - Every knob is a `params::Parameter` under `attractorWorld/...`, so routes, MIDI, macros, states and the timeline
    reach it.
  - Routes are applied before `Composition::update`, which reads the finals. This is the astral pattern.
- **[E] Offline.**
  - `avgen --headless --composition <scene> --render <dir|mp4> --range a:b --size WxH` is the offline path.
    `--tier offline` lifts the quality ladder.
  - There is no separate path tracer for an Environment, so offline quality means more samples and more resolution
    in the same pass.
- **[E] Audio (for Stage 5, read before mapping).**
  - The current vocabulary: `response.level`, `response.bass`, `response.mid`, `response.high` (fast, ADR-1723),
    `response.sustain`, `response.onset`/`response.onsetEnv`, `response.low`, `response.note`, `beat.phase`,
    `beat.pulse`, `beat.source` (ADR-1724..1785), and the stem roles.
  - The drum classifier and the kick, snare and hat signals are cut (ADR-1780..1785).
  - Docs: `docs/audio-response-lab.md`, `docs/prototypes/audio-response-lab/`, `docs/prototypes/beat-clock/`.

## 3. Art direction (workstream C)

**References**, used as principles, not as images to copy:
- canyon country's stratigraphy: [the Grand Canyon's sequence](https://en.wikipedia.org/wiki/Geology_of_the_Grand_Canyon_area),
  [The Wave's cross-bedding](https://en.wikipedia.org/wiki/The_Wave_(Arizona)) and
  [Danxia's banded red beds](https://en.wikipedia.org/wiki/Danxia_landform);
- twilight optics: [the Belt of Venus over Earth's shadow](https://en.wikipedia.org/wiki/Belt_of_Venus) and
  [aerial perspective](https://en.wikipedia.org/wiki/Aerial_perspective);
- surrealist landscape: [Yves Tanguy](https://en.wikipedia.org/wiki/Yves_Tanguy), Kay Sage, and Max Ernst's
  [decalcomania](https://en.wikipedia.org/wiki/Decalcomania) landscapes;
- technique precedents: Quilez's ray-marched terrains
  ([terrain marching](https://iquilezles.org/articles/terrainmarching/),
  [soft shadows](https://iquilezles.org/articles/rmshadows/), [fog](https://iquilezles.org/articles/fog/)).

**Answers to the brief's questions (2.3):**
- **A compelling silhouette.** [E] A few large shapes at distinct depths, overlapping, each stepped back in value by
  the air. Flat-topped mesas against a sky give the strongest skyline in canyon country, and are what the far tier is.
- **Mathematical but not sterile.** [H] Generate the *process* (an erosion front, stratification), not the *shape*.
  The fractal is then the escarpment's coastline and the strata's laminations, which the eye accepts as geology.
- **Repetition that feels organic.** [H] Repeat one profile at several scales with irregular spacing: cliff, talus,
  bench, inner gorge. Formation, band, lamination.
- **A limited palette with variation.** [E] Assign colour by a spatial coordinate that matters (stratigraphic depth),
  then vary value, not hue, inside it. Haze and the twilight sky give a second, depth-driven axis for free.
- **Foreground, middle ground, background.** [E] Dark and textured near, lit and saturated in the middle, pale,
  cool and simple far away.
- **Legible at 1080p.** [E] By the pixel arithmetic in section 1.1: formations of 50-150 m, terrace bands of 15-22 m
  and gorges of 50+ m read at 1-5 km. Laminations read only near.
- **Uncanny without sci-fi cliché.** [H] No glow, no neon, no floating rocks. The strangeness lives in the rock's own
  logic: folded strata, nested arches, a stone ribbon that is plainly a trajectory.

## 4. Two architectures considered

| | A. Meshes in the lit pass | B. One ray-marched Environment pass (chosen) |
|---|---|---|
| Terrain | a generated mesh (a terrain node or procedural geometry) | a baked height field (R32F), marched per pixel |
| Landmarks | SDF meshed by surface nets, or tubes | SDFs evaluated in the same march, blended into the terrain |
| Attractor | a tube mesh along the curve | an SDF of the curve's segments (baked distance, or a bounded segment search) |
| Lighting | the engine's PBR, cascades and IBL | the scene's own: sun, soft height-field shadows, sky ambient, bounce, analytic fog |
| Pros | reuses the engine's whole shading stack | seamless fillets where rock meets rock; per-pixel shadows over 16 km; full art control; one pass to tier |
| Cons | hard intersections; cascade shadows over 16 km are coarse; mesh LOD and draw cost; the engine's look | its own lighting code; no engine shadow casting (ADR-1221 already accepts this); march cost per pixel |

**Chosen: B (ADR-1850).** The rendered evidence follows in section 5. The engine's post (ACES tonemap, bloom) still
applies downstream.

## 5. What the cheap renders decided

All the renders were made in AV Gen, through the real pass. The rejected ones are in the review folder's `gate1/`
history files.

1. **Masses added to a plain look generic.** The first generator placed mesa, fin and spire masses on a gently
   warped plain. In the render, the camera saw a huge empty playa, candy-striped blobs and alpine rim peaks.
   - Files: `12-history-first-render-rejected.png`, `13-history-first-layout-rejected.png`.
   - **Rejected.**
2. **A plateau dissected by an erosion front reads as a world.** One nested-domain-warped field F: the plateau stands
   where F > 0.
   - Composition terms bias F: the camera's promontory, a basin along the valley, the near escarpment, the stranded
     massif, and the far ramp.
   - F's zero contour is a fractal coastline of fingers, amphitheatres and stranded buttes. The overhead render
     (`10-overhead-layout-seed-1.png`) shows it.
   - **Adopted.**
3. **Cliffs need a metric width.** A smoothstep across F gave cliffs whose width depended on the field's local
   gradient: sometimes under one texel, which drew saw-teeth along diagonal faces.
   - `F / |grad F|` (a first-order distance to the edge) gives every cliff a set width.
   - Dividing by a noisy gradient grew spikes in the basin. A wide stencil and a gradient floor removed them, as the
     A/B renders showed.
4. **Strata need three scales.** Random 20 m bands read as candy stripes. Formation (5 bands), band and lamination,
   with the formation's colour biased by its depth in the stack, read as rock.
5. **An inner gorge structures the basin floor.** A plain basin left half the frame empty. A bench with a narrow
   meandering gorge gives the leading line, and is where Stage 3's branching will live.
6. **Tonemap.** ACES (the engine's tonemap 0) kept the twilight saturation and contrast that AgX flattened.
7. **The fold.** A sine wave added to the strata coordinate (`strataFold`) bends the terraces and the colour bands
   together.
   - At 0.45 it reads as quietly wrong. At 1.0 it breaks the terraces.
   - Files: `01-…` (with the fold) against `04-…` (without).
