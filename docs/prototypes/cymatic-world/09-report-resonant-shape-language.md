# Cymatic World: resonant shape language (report)

The brief is [08-brief-resonant-shape-language.md](08-brief-resonant-shape-language.md). It governs [05](05-brief-resonant-simulation-overhaul.md) (motion overhaul) and [07](07-brief-motion-stability.md) (motion stability). [06-report-resonant-motion.md](06-report-resonant-motion.md) holds Passes 1-2 of 05. This report covers everything since, with 07's stability work as its §9 / Stage 6. Branch `proto/cymatic-split`.

**Audio.** Hyperborea (`assets/audio/hyperborea.wav` in the repo, gitignored) unless a section says otherwise.

**Media.** `~/Desktop/av-gen-review/58-cymatic-world/shape-language/`, one folder per stage. The stability diagnosis is in `.../motion/stability/`.

**Performance target.** Live 1080p, 30 fps, Auto quality, against the colour-pass baseline: High, GPU median 26.0 ms, 0 misses (owner direction).

## Stage 1: audit

### The twelve questions (§2)

1. **How is the surface topology generated?** One static polar mesh, `cw-ground.glb`: 130k vertices, rings 0.36 m apart out to 70 m and coarser beyond, with heights from the WorldMap's basin. A procedural Field deformer displaces it per vertex on the GPU (the procedural vertex stage), reading a `relief` reader of the resonance field (ADR-1400). Nothing else moves it.
2. **What drives displacement?** An analytic modal field: a superposition of point (asymptotic Bessel, R_m(kr) cos m(α − θ)) and lattice standing-wave modes. No noise and no simulation grid. Since ADR-1403 it also has per-mode kinematics (wandering centres, turning petals) and onset ripples (travelling ring packets).
3. **Which signals?** Each mode hears the audio history directly: a 64-bin log spectrogram, filtered through the mode's resonance curve (selectivity Q) and a spectral contrast, convolved with a rise/decay kernel, plus onset strikes. These are smoothed envelopes, not raw FFT values. Routes add the song's build (`response.intensity` → `modes`) and held sound (`response.sustain` → `memory`). Colour has its own routes (kick, snare, hat, flux, build, sustain).
4. **Spatial and temporal frequencies, phases.**
   - Spatially, mode i sits at u_i on a dispersion axis: wavelength λ_i = λmax(λmin/λmax)^u_i, heard frequency f_i = fLow(fHigh/fLow)^u_i.
   - Temporally, the vibration is aᵢ cos(Ωᵢt + ψᵢ), with Ωᵢ = 2π·breath·(fᵢ/fLow)^0.3 and ψᵢ a seeded constant.
   - The "slow figure" (the resting phase, Σ sᵢφᵢ) carries the nodal lines.
5. **Are modes persistent?** Their shapes and phases are, but **their places were not.** u_i = i/(modes − 1), so the `intensity → modes` route slid every mode along the axis during play: wavelengths, frequencies and orders shifted, and modes that crossed a source boundary changed source mid-play. Fixed by `layout` (ADR-1404).
6. **What sets the broad, medium and fine balance?** Before this work, nothing explicit. Mode amplitude came from the spectrum, competition and a per-source limiter, and at saturation the limiter cancelled a source's gain, so fine families came out as tall as broad ones. ADR-1403's `amplitude` gave a tilt. The shape language makes it explicit: one reader and one deformer per scale (below).
7. **Normals?** Finite differences in the procedural vertex stage: the deformer is sampled at the vertex and at two offset points (step `normalStep` 0.12 m, ADR-1401), giving a per-vertex normal interpolated across triangles. It follows the deformation exactly at the vertices; between them it is linear.
8. **Mesh and screen resolution.**
   - The camera sits 30 m out and 32 m up on a 30 mm lens: vertical FOV 43.6°, about 45 m to the figure's centre, so 1080 px cover about 36 m (3.3 cm a pixel).
   - The 0.36 m vertex spacing is about 11 px in the foreground and fewer towards the top of the frame.
   - The shortest drawn wavelength (2.6 m) is about 7 vertices, which is representable. A ridge or crease narrower than about 1 m is not: hence the sawtooth crests (Stage 7).
9. **Reusable infrastructure.**
   - The resonance mode table: 48 rows, rewritten by the CPU each frame, evaluated per vertex (deformers) and per pixel (the material's `field` op).
   - GPU grid simulation (`rendering/simulation.cpp`: Scalar, Vector, ReactionDiffusion, Agents and Excitable modes on a GridField, fixed sub-steps, sampled by `FieldKind::Grid`). It has no wave-equation mode.
   - Procedural vertex displacement with finite-difference normals.
   - The live profiler (`tools/live_scene_profile.py`).
10. **Cymatic Plate.** One Chladni function, f = cos(nπu)cos(mπv) − cos(mπu)cos(nπv), in a material program. Routes move the **mode numbers** (n, m) continuously from pitch and brightness, and the picture is the **nodal set** (sand where |f| is small). Two lessons carry over: one coherent figure at a time whose identity moves with the music, and nodal lines as the subject.
11. **Why blobs?** See below.
12. **Cost.**
    - Colour pass (before motion): live High 26.0 ms GPU, 0 misses.
    - After 05's Pass 2 (four moving families, 16 modes plus up to 6 ripples): live High 32.1 ms, 10% misses.
    - Nearly all of it is the ground's scene pass: the material evaluates every mode per pixel, and the deformer per vertex three times.

### Why the surface read as blobs (the structural limitation)

- **The sum, not the modes.** Each mode alone draws a clean Chladni-like figure in this geometry (`stage1-audit/modes-isolated-hyperborea-50s.png`: rings, an eight-spoke star with rings, a trefoil, standing bands, squares). Five or six of them from four sources, with comparable weights and unrelated centres, add up to what is statistically a Gaussian random field, which is blobs.
  - Competition could not separate them: a dense mix drives every mode alike (driven levels 2.3-2.5 at 50 s).
  - ADR-1403's wandering made it worse, because each mode wandered its own way, so the figures slid apart instead of nesting.
- **Height from the vibration.** Pass 2 led the relief with D, the signed oscillating sum: smooth lobes with no crisp feature. The nodal lines, which are the organised part, were a minor term.
- **No scale hierarchy.** One reader summed all scales, so fine modes broke up the large figure's nodal lines.

So yes, there was a structural limitation, but it was in how the field was weighted and mapped to height, not in the analytic representation itself. The fix is a designed bank with a focus, plus nodal geometry per scale. It is not a new solver.

## Stage 2: geometry-only prototypes

All three prototypes share one designed bank ([ADR-1405](../../decisions/ADR-1405-a-designed-resonance-bank-with-a-focus-and-nodal-relief.md)):
- **P, the plate's large figure.** A point source at the centre, u 0-0.3, orders [0, 3, 4, 2, 6, 5].
- **M, the plate's medium figure.** The same centre, u 0.3-0.66, orders [0, 8, 4, 6, 3, 5], so its figures nest inside P's.
- **S, a weak second centre** to the west (u 0.4-0.48): controlled asymmetry.
- **F, faint fine standing bands, squares and hexagons** (lattice, u 0.66-1).

Throughout:
- Layout 18, so every mode keeps its place.
- P and M each have a **focus**: a Gaussian window on the modes' weights that sweeps the bank (P every 47 s, M every 29 s), so one or two modes lead and the figure crossfades from one designed mode to the next.
- λ 16 → 2.6 m; competition 1.5; selectivity 4.

The prototypes differ only in how a scale's figure becomes height. There are three readers (large u < 0.3, medium 0.28-0.7, fine > 0.68), one deformer each:

| | Large | Medium | Fine |
|---|---|---|---|
| **P1, structured modal** | signed figure F | F | vibration |
| **P2, nodal boundaries** | sand ridge on the nodal lines, −0.7·\|F\| basins at the antinodes | sharper sand creases, −0.35·\|F\| | vibration |
| **P3, hybrid** | 0.8·F + 0.55·sand ridge | P2's creases | vibration |

**Result:** `stage2-prototypes/prototypes-P1-P2-P3-compare-hyperborea-40-70s.mp4` (geometry-only, real scene camera, same passage, audio), plus each at 960×540.
- **P1** stays soft: lobes with no edge, so it reads as blobs under a neutral material.
- **P2** reads as cymatics in grey: spoked radial cells crossed by rings, nested concentric rings around an inner figure, a spoked rosette. The nodal lines are real ridges and the antinodes real basins.
- **P3** muddies P2's edges with the signed lobes.

**P2 is selected.** It is the only one whose organisation survives with colour and emission off (the §5 acceptance test). P1's softness is the surface itself: there is no temporal AA in the engine, and depth of field and motion blur are off.

**Open after Stage 2:**
- sawtooth on the ridge crests (mesh, Stage 7);
- some medium-scale clutter when the S and M figures overlap;
- the fine layer's square grid can read as a texture in quiet frames.

## Motion stability (07, = 08 §9 / Stage 6)

**Diagnosis.** `motion/stability/diagnosis-pass2-hyperborea-44-56s.mp4` shows each family's contribution, the ripples, the total vibration, the sand and the relief as top-down panels, with every mode's envelope, its oscillator cos(phase), the limiter and the ripples' amplitude as strip charts. It is made from the hidden `[.resonance-trace]` diagnostic on Pass 2's scene, colour off. Four separate causes; the oscillators themselves were clean (≤ 1.7% of their energy above 3 Hz):

1. **Ripple evictions.** The cap of 6 rings was full in every frame, so each new onset dropped a living ring in one frame: 21 drops in 12 s, up to 223% of the ripple layer's rms (14 times its median frame step).
2. **Strike steps through the limiter.** An onset's strike peaked within 30 ms, and the per-source limiter then dipped that source's other modes in the same frame: the rings family stepped up to 27% of its rms (5.7 times its median).
3. **The layout sliding under a route** (question 5 above). With the build route ramping `modes`, six modes switched source mid-play, centres jumped up to 54 m, and petals stepped 75% of their rms in one frame.
4. **Fine detail moving fast for its size.** The carpet wandered at about 2.5 m/s on 3-5 m wavelengths, half a wavelength a second.

Not causes: normals, frame-rate dependence (everything is a function of the clock), and integration (there is none).

**Fixes ([ADR-1404](../../decisions/ADR-1404-resonance-modes-keep-their-places-and-respond-per-band.md)):**
- Ripples are capped per band, and a displaced ring fades out over 0.35 s; all rings taper over their last life.
- Per-band attack and release (low, mid, high). A mode's attack is also its strike's rise, so a strike is a bounded two-time-constant impulse, never a 30 ms step.
- A fixed `layout`: `modes` fills the bank and never re-places it.
- A per-source breath, and slower fine wander.

On the same passage with the same `modes` ramp:

| Measure | Before | After |
|---|---|---|
| Relief, worst one-frame step against its median | 9.2× | 2.3× |
| Total vibration, worst step (% of rms) | 37% | 9.7% |
| Ripple one-frame drops (evictions) | 21 | 0 |
| Ripple layer, worst step (% of rms) | 223% | 43% |

None of this came from slowing the animation: the median frame-to-frame motion of the large layers is unchanged or larger.

## Stage 3: the shape and interference system (P2 built out)

### The bank

| Source | Role | Centre | Band (u) | Orders | λ | Focus sweep | Notes |
|---|---|---|---|---|---|---|---|
| A, large | lobes and stars | (7, 3, −7), wanders 12 m / 100 s | 0-0.15 | [3, 4, 2, 6] | 16-11 m | every 47 s | |
| B, large | a second centre | (−20, 2, 15) | 0.15-0.3 | [3, 2, 5] | 11-7.5 m | every 61 s | amplitude 0.42: it bends A's figure, never replaces it |
| M, medium | rings and petals nested in the large lobes | (3, 3, −3.5), eccentric to A | 0.3-0.66 | [0, 8, 4, 6, 3, 5] | 7.5-3.8 m | every 29 s | |
| F, fine | standing bands, squares, hexagons | | 0.66-1 | | | | amplitude 0.1 |

The rest of the resonator:
- layout 22 and base modes 17 (the song's build fills the bank to 22, never re-placing it);
- λ 16 → 2.6 m; competition 1.5; selectivity 4; per-band attack 0.18 / 0.08 / 0.04 s and release 3 / 1.4 / 0.6 s;
- ripples at 0.5, two a band, fading over 0.35 s.

### Geometry, per scale (P2)

| Scale | Shaping | Amount |
|---|---|---|
| Large | nodal ridge (sand, sharpness 0.5) + antinode basins (−0.55·\|F\|, rounded) + 0.2 vibration | 3.4 m |
| Medium | sharper creases (sharpness 0.5) + −0.35·\|F\| | 0.8 m |
| Fine | the vibration alone | 0.25 m |

### What changed after the Stage 2 gate, and why

- **The centred bullseye.**
  - The plate's centre now wanders across the frame, and B, off to the south-west, bends its figure.
  - The large figure carries lobes (orders 2-6) and rings live only in the medium, about a centre eccentric to A's. A trace at 96 s showed that a large ring mode and a medium ring mode about one centre made a bullseye, and from the oblique camera an apparent spiral (the rejected vortex).
  - Tried and rejected: moving both large centres 24 m off-frame with longer wavelengths (30 m). The frame then saw only their outer folds, which read as drapery, not structure.
- **Medium clutter.** M's amplitude is 0.55 and its deformer 0.8 m. The separate petal source is gone, so the medium figure always nests in the large one.
- **The rounded crease (ADR-1405 `reliefRound`).** The antinode basins' −|F| put a cusp on every nodal line, a crest no mesh can carry, which was the main source of the sawtooth. |F| is now √(F² + (cσ)²) − cσ with c = 0.35 (large) and 0.3 (medium), σ the figure's local rms. A test checks that the crest is round (zero second difference at the node) where the exact |F| has its kink. Sawtooth from the mesh itself remains for Stage 7.

**Result:** `stage3-shape-system/shape-system-geometry-only-hyperborea-40-70s.mp4` and `shape-system-stills-geometry-only.png` (10, 44, 52, 60, 68, 96, 116, 134 s). In neutral grey:
- a trefoil star with rings over the fine hexagons (quiet, 10 s);
- sculpted cells (44, 52 s);
- spokes crossing bands (60 s);
- a radial fan (68 s);
- off-centre rosettes (96, 116 s);
- nested arcs (134 s).

The nodal network is the subject, with large forms leading and medium figures nested in them.

### Debug views (§16)

These are build-time, like the palettes; `CW_DEBUG=` selects one:

| View | Shows |
|---|---|
| `mode:<i>` | one mode's figure in the geometry |
| `scale:large\|medium\|fine` | one scale's deformer alone |
| `signed` | the signed standing figure, blue to red, on the real geometry |
| `nodal` | the nodal lines, white on black |
| `envelope` | the excitation \|F\| |

The envelopes, phases, limiter and ripples over time come from the `[.resonance-trace]` diagnostic and `trace_video.py`.

Sheets: `stage1-audit/modes-isolated-hyperborea-50s.png` and `stage3-shape-system/debug-views-60s-116s.png`.

## Stage 4: pattern evolution

The world moves between configurations through **persistent mode identities** and continuous weights; nothing is reset or re-drawn:
- **The foci** (ADR-1405) sweep each figure's bank at incommensurate periods: A every 47 s, B every 61 s, M every 29 s. They are narrow (width 0.032-0.04 in u, against a mode spacing of 0.048), so one designed mode leads at a time and the figure crossfades into the next: a trefoil into a star, into spokes and rings.
- **The centres wander:** A by 12 m over 100 s, M about its own eccentric centre.
- **The petals turn:** 0.03 and 0.045 rad/s, some against the others.
- **Envelopes rise and decay** per band (ADR-1404).
- **Phase is Ω·t at fixed Ω,** continuous by construction. The layout fixes every mode's frequency.

**Capture:** `stage4-evolution/evolution-geometry-only-hyperborea-100-160s.mp4` (60 s at normal speed, geometry-only) and `evolution-stills-geometry-only.png`. The figure passes through off-centre spokes and rings, Chladni webs of cells and spokes, radial fans, and cell networks with the medium figure nested in the large.

**One finding changed the design:** B's constant interference turned the large figure into a labyrinth. With B silent, 125 s is a clean web of spokes and rings; at 0.22, a maze (`stage4-evolution` notes, and the B comparison in the commit history). So **interference is now an event.** B rests at zero, and the sound's roughness raises it (Stage 5).

## Stage 5: audio as excitation

The mapping is deliberately small. It decides which figure leads and how hard each scale is excited, never the finished height directly:

| Feature | Acts on | How |
|---|---|---|
| every mode's own band of the spectrum (ADR-1400) | its envelope | selectivity 4, contrast 0.7, per-band attack and release (0.18 / 0.08 / 0.04 s, 3 / 1.4 / 0.6 s) |
| onsets (low, mid, high) | the modes their band reaches | a strike: a bounded two-time-constant impulse (ADR-1404) |
| onsets | a travelling ring each | kick broad, slow and in the medium scale (rippleU 0.36); snare faster and local; hat fine; two a band, fading out when displaced |
| `sonic.brightness.slow` | A's and B's focus (+0.08, +0.06) | a brighter sound leads higher-order lobes, over seconds |
| `sonic.brightness` | M's focus (+0.14) | petal subdivision follows the timbre |
| `sonic.roughness.slow` | B's amplitude (0 → 0.45) | turbulence brings the second centre's interference |
| `response.bass` | the large layer (× 1 + 0.35) | |
| `response.sustain` | the medium layer (× 1 + 0.3) | held sound adds intricacy |
| `response.hat` | the fine layer (× 1 + 1.5) | fine vibration only |
| `response.intensity` (the song's build) | `modes` (+5, filling the 22-mode bank) | finer modes join; none is re-placed |
| `response.sustain` | `memory` | |
| MIDI | | CC 1-4 (excitation, complexity, persistence, scale), CC 5 terrain depth, CC 8 light, pad 36 strike; unchanged |

**Reproducible test signals** (`stage5-audio/test-signals-geometry-only-0-60s.mp4`, generated, 60 s): silence, 55 Hz sustained bass, kicks at 2 Hz, off-beat snares, a sustained 440/554/660 Hz chord, 8 Hz hats, and a 40 Hz → 8 kHz sweep.
- Silence keeps a clean idle rosette.
- Bass deepens and broadens the large cells.
- Kicks heave the large ridges and send rings through the medium.
- Snares sharpen the medium network.
- The mid chord brings a finer network while the large forms stay quiet.
- Hats bring fine detail.
- The sweep carries the figure through the bank.

## Stage 6: stability on the final system

The trace on the kick-heavy passage (Hyperborea 82-94 s), with the build route ramping `modes` 17 → 22:
- **Large relief:** median one-frame change 2.5% of its rms; worst 11% (4.5× the median). The large figure holds and then morphs as a focus passes between modes. That is a deliberate transition at the sweep's pace, not a pop.
- **Medium relief:** 1.9× its median, no jumps.
- **Ripples:** 0 evictions.
- **Oscillators:** still clean.

The root causes from §9 (evictions, strike steps, layout shifts) stay fixed by ADR-1404.

## Stage 7: geometry and rendering quality

**What the sawtooth and the softness were.** Four findings, three of them in order:

1. **A mesh bug, present since before the colour pass.**
   - The polar ground's rule "rings may only keep or double their count outward" was enforced by carrying the horizon ring's count inward, so every ring had 256 vertices.
   - Cells were about 0.36 × 1 m at 40 m and 0.36 × 2.5 m at 100 m. Long, thin cells drew every crest crossing them as a staircase.
   - The stitched rows where a polar count doubles drew seams of their own.
   - **Fix:** a tensor-product grid of square 0.36 m cells (alternating diagonals) over ±66 m, which is all the camera ever sees: the frame's top edge meets the ground inside 61 m all round the arc. Spacing then grows geometrically to the 2.6 km edge. 235k vertices.
2. **Creases narrower than the mesh.** The scale views (`sawtooth-diagnosis-large-medium-fine-before.png`) put the remaining sawtooth in the medium layer. Its creases (sand sharpness 0.5 on 4-7 m waves) were about 0.3 m wide, one vertex. The exact |F| in the antinode basins also had a cusp on every nodal line.
   - **Fix:** the crease rounded (`reliefRound` 0.45 large, 0.5 medium, ADR-1405), and the medium crest widened (sharpness 1.0, about 0.7 m, two vertices).
   - This filters out detail the geometry cannot carry; the crisp line itself belongs to the material.
3. **The cost of evaluating the field per vertex.**
   - Three readers × three samples for the normal × up to 34 rows, and again in a depth prepass that the unlit ground did not need. Fixing the mesh would have cost 82-116 ms at High.
   - **Fixes:**
     - ADR-1406: the unlit scene declares no AO or contact shadows, and authors one dark non-casting light, so no prepass runs. Silent rows are skipped.
     - ADR-1407: **bake grids.** The summed relief and the three values the material reads are each sampled once a frame into a 768² plane over ±64 m (0.17 m cells, 0.6-0.7 ms in all), and the single deformer and the material sample those. The normal's step is one cell.
4. **The softness is not a filter.**
   - Ultra versus High, bloom on versus off, and the canvas at scale 1 all render the same pixels.
   - The geometry-only test material is soft by nature: broad Gaussian ridges, rounded creases, sky fill, no specular.
   - The owner's crispness has to come from the plasma material's crest lines and sheen (Stage 8), now that the crest grid is sharp.

**Before/after:** `stage7-geometry-quality/sawtooth-before-after-1080p-crop-52s.png`, a 1080p crop at the same second. The staircase on every crest and the seams are gone.

**Cost (live 1080p, High, 116 s):**

| Mesh and evaluation | GPU median |
|---|---|
| Old polar mesh, per-vertex readers, prepass | 39.6 ms |
| Fixed polar 0.36 m, per-vertex, prepass | 116 ms |
| Fixed polar 0.36 m, baked, no prepass | 29.4 ms |
| Square 0.36 m, baked | 33.0 ms |
| Square 0.3 m, baked | 36.9 ms |

With the field baked, what remains is the bare mesh (about 40 ns a vertex) and the material interpreter: 40 ops at about 0.36 ms each at High, raised by quad overshading on small triangles. Stage 8 trims the material.

## Stage 8: the plasma material on the new geometry

The colour pass's palette, lighting, bloom and AgX settings, and every one of its colour routes, are kept:
- the build turns the grit's hue;
- held sound and kicks drive the hollow glow;
- snares drive the nodal lines;
- hats drive the sheen;
- spectral flux drives the fuchsia counter-fill;
- CC 8 drives the light.

The routes find their ops by label, so they follow the program.

What changed:
- **The material reads the same network the geometry draws.** Its three inputs are baked grids (ADR-1407) of the large and medium scales only (u < 0.7): the sand ridge, the crest line and the excitation |F|.
- **The sand masks are normalised** (`sandNormalized`, ADR-1405): the nodal network is a 0-1 mask whatever the loudness. In the shape language the old masks (scaled by the field's energy) left quiet passages black; 10 s was nearly black before this. Loudness drives only the emission: the crest line's brightness, the hollow glow's presence, and the routes.
- **Thresholds on the new scales:**
  - grit hugs the ridges (mask 0.5-0.92), so navy stone shows between them;
  - the nodal line is a thin bright crest (crest reader sharpness 0.25, mask 0.7-0.96);
  - the fuchsia glow rises on the inner walls of cells whose excitation passes 0.35-1.1.
- **Seven ops removed** (40 → 33): the Fresnel rim and the distance haze. Seen from 35-70 m, nearly straight down, the ground grazes and hazes almost nothing. That is about 2.5 ms of interpreter at High.

**Stills:** `stage8-art/plasma-stills-hyperborea-*.png` and `before-art-pass-same-times.png` (the same timestamps before this stage), plus `plasma-1080p-52s.png`. Quiet, dense and transitional passages are all coloured and legible, and the crispness the owner asked for now comes from the thin crest lines and the grit edges.

**Cost:** live 1080p, High: GPU median 28.8 ms, 0 deadline misses. Auto holds High. The colour-pass baseline was 26.0 ms.

## Stage 9: performance (live 1080p, 30 fps target)

Final scene on Hyperborea at 116 s (a dense passage), M2 Max:

| Quality | Internal resolution | GPU median (P95) | CPU work | Deadline misses |
|---|---|---|---|---|
| Emergency | 960×540 | 17.0 ms (20.0) | 5.4 ms | 1.3% |
| Low | 960×540 | 16.9 ms (17.3) | 5.1 ms | 0.7% |
| Medium | 1362×766 | 22.5 ms (22.9) | 5.0 ms | 0 |
| High | 1632×918 | 26.9 ms (27.5) | 1.1 ms | 0 |
| Ultra | 1920×1080 | 33.7 ms (34.6) | 1.2 ms | 0.7% |
| **Auto → High** | 1632×918 | **26.8 ms (27.4)** | 1.1 ms | **0** |

Headless Ultra: GPU 32.2 ms, CPU work 0.5 ms.

**Against the colour-pass baseline** (High, 26.0 ms, 0 misses): +0.8 ms. That buys the motion system, the shape language, a mesh with square cells (235k vertices against 130k), and four bake grids (0.5 ms).

**Where it goes now:**
- the material interpreter: 33 ops, about 0.36 ms each at High;
- the bare mesh: about 40 ns a vertex;
- field evaluation: per grid cell in the bakes, no longer per vertex or per pixel.

**Quality scaling** uses the engine's existing ladder, with no new settings:
- Auto holds High;
- lower tiers scale the render, and the material scales with it;
- the field cost is fixed and small.

**Measured along the way** (High, 116 s), the history that led here:

| State | GPU median |
|---|---|
| Colour pass | 26.0 ms |
| 05's Pass 2 | 32.1 ms |
| Stage 3 bank, three per-vertex deformers | 39.6 ms |
| Fixed mesh, per-vertex | 82-116 ms |
| Baked, square mesh | 33.0 ms |
| 33-op material | 28.8 ms |
| Final | 26.8 ms |

## Stage 10: validation (§14)

All in `stage10-validation/`. Each scenario is geometry-only, shaded, and the two side by side with audio, from the real scene camera. A contact sheet of every pair at its 5th second is in `validation-frames-at-5s-geometry-left-shaded-right.png`.

| # | Scenario | Passage (why) | What it shows |
|---|---|---|---|
| 1 | Near silence | Hyperborea 0-10 s (low band 65 dB down) | A faint rosette held by the idle modes, coloured by the normalised network. No autonomous chaos. |
| 2 | Sustained bass | 156-166 s (low and mid near peak, few onsets) | A large spoked ring figure with a trefoil star, steady and legible. |
| 3 | Kick-heavy | 84-94 s (16-20 kicks per 6 s) | Large cells heave on each kick and settle (two-time-constant strikes); medium rings pass through. |
| 4 | Snare and transients | 120-130 s | The medium network sharpens and local rings cross it; the large figure holds. |
| 5 | Sustained midrange | 62-72 s (mid at peak, bass 18 dB down) | Arcs and spokes; the medium figures lead, the large forms are quiet. |
| 6 | High frequency | 174-184 s (about 49 hat onsets per 6 s) | A fine cell network over a held large figure; the fine layer stays small. |
| 7 | Dense full band | 20-30 s | A rosette: large, medium and fine together, organised. |
| 8 | Quiet to loud | 10-20 s | The figure grows from a faint rosette into deep cells as the bass enters at 16 s. |
| 9 | Loud to quiet | 54-66 s | Cells relax and the excitation (fuchsia) drains while the network stays. |
| 10 | Other material | Feline Footwear 40-50 s, Trench 50-60 s | The same vocabulary from different music: spoked arcs with fuchsia hollows (Feline), a rosette network (Trench). |

Also:
- **Stability before/after** (`stage6-stability/`): Pass 2 against the final system, Hyperborea 40-70 s, geometry-only. The final system holds its figure and morphs where Pass 2 jumped.
- **Before/after stills** (`final/before-after-stills-hyperborea-10-48-116-134s.png`): the colour pass (before any motion work) against the final scene, same seconds.

## §19 summary

1. **Root causes of the blobs.**
   - Five or six modes from four unrelated, independently wandering sources at comparable weights. Their sum is a random field.
   - A relief led by the soft signed vibration.
   - One reader for every scale.
2. **Prototyped.** P1, the signed modal figure; P2, nodal ridges between antinode basins; P3, a hybrid. All on one designed bank. A GPU wave simulation was considered: the engine's grid simulation has no wave mode, and the analytic system gave the required reorganisation first.
3. **Selected: P2.** It is the only prototype whose organisation survives in neutral grey: spoked cells, nested rings, rosettes.
4. **How the vocabulary arises.**
   - A designed bank (`orders`) of shared-centre point modes, a second centre and lattice bands.
   - Per-scale readers turn each scale's nodal lines into ridges (sand) with antinode basins (−|F|, rounded).
   - A focus window per source leads one or two modes at a time, so the figure is a recognisable Chladni form.
5. **Evolution and phase.** Persistent modes at fixed places (`layout`). Foci sweep the banks continuously, centres wander, petals turn. Phase is Ω·t at fixed Ω, so there are no resets, swaps or re-placements. Everything is a pure function of the clock and the history, exact under seek.
6. **Audio as excitation.**
   - Each mode hears its band.
   - Onsets strike as bounded impulses and launch rings.
   - Brightness moves the foci; roughness brings the second centre's interference.
   - Bass, held sound and hats excite their own scales.
   - The build fills the bank.
7. **Stability.** Four diagnosed causes (evictions, strike steps, layout shifts, fast fine detail), each fixed at the cause (ADR-1404).
8. **Mesh, normals, screen detail.**
   - The polar mesh bug (256 vertices a ring) is replaced by a square 0.36 m grid.
   - Creases are rounded and widened beyond the mesh's resolution.
   - The field is baked once a frame into grids (ADR-1407); normals are differenced across bake cells.
   - The unlit scene drops the depth prepass (ADR-1406).
   - The softness is the surface's own, not a filter; crispness comes from the material's thin crest lines.
9. **Evidence.** Stages 1-10 in `shape-language/`.
10. **Performance.** Live 1080p, Auto → High: 26.8 ms, 0 misses (baseline 26.0). Per level as above.
11. **Tests.** See the final report and the commit. The CPU and GPU suites, `review_examples` and `check_project_integrity` are run at the end.
12. **Limitations and next.**
    - The bake samples the field at one height, so the raised sources' vertical structure is gone.
    - Live MIDI on `breath` or `spin` would slew phase; the scene does not route them.
    - Very dense onsets still put up to 12 rings on screen.
    - The softness of broad forms is a design choice that could be revisited with a per-pixel normal from the bake.
    - The most valuable next iteration is a per-cell memory of excitation (the hollows remember where the music has been), and a live-performer CC on the foci (choose the figure by hand).

### Doc edits waiting for owner approval (not made)

- **02-report.md and PROGRESS.md:**
  - the terrain-only scene;
  - the Crystals and Creature cut;
  - the colour pass (04);
  - the motion overhaul (05/06), stability (07) and shape language (08/09);
  - ADR-1403 to 1407.
- **docs/resonance-field.md:**
  - drop the druse-coupling example;
  - document kinematics, ripples, layout, per-band response, orders, focus, the relief terms, `sandNormalized`, the silent-row skip;
  - point at bake grids for costly consumers.
- **ADR-1402:** note that it has no current scene user.
- **README / examples rows:** Cymatic World's description. The generator already rewrites `examples/index.json`; its CC list is unchanged.
- **docs/decisions/README.md:** already carries rows for the new ADR-1403 to 1407, which the hygiene test requires; there are no edits to existing rows.

## Correction round (owner review of the final stills)

The owner's verdict on `final/before-after-stills-*`: against the colour pass, the final scene had regressed in four ways.
1. It was **flatter**: a web of caustic light rather than sculpted ground.
2. The **fuchsia was mostly gone.**
3. The quiet and sustained frames read as a **wobbly net** rather than lobes, rings and petals.
4. The camera was **too close**.

What to keep: the wandering, no-bullseye evolution; no sawtooth; the twitch fixes; the stability.

Changes (commit 8005c378):
- **Depth.**
  - The large relief's ridges hold a constant height whatever the loudness: `sandNormalized` now also applies to a relief reader's sand terms (ADR-1405).
  - The antinodes sink into basins that deepen with the excitation (−1.2·|F|, from −0.55).
  - The crease is rounded less (0.4), which the square grid carries.
  - Large amount 2.8 m.
  - So the quiet state keeps a deliberate, sculpted form, and loud passages carve it deeper.
- **Shape strength.** The medium net is at about half its height (0.45 m), and the grit and nodal-line colour follow the large figure only. The large cells and ridges lead.
- **Fuchsia.** The hollow glow is back in excited cells (excitation 0.3-1.0, wall band 0.02-0.14, intensity 0.4).
- **Framing.** Measured along the whole loop (farthest ground reached by the top edge at 16:9 and 21:9; the basin walls start at about 90 m):

  | Option | Distance / height / lens | Reach |
  |---|---|---|
  | A | 30 m / 32 m / 30 mm | 56 / 61 m |
  | **B (chosen)** | 36 m / 42 m / 28 mm | 69 / 80 m |
  | C | 40 m / 48 m / 26 mm | 82 / 92 m: reaches the walls at 21:9 |

  The bake grids and the fine mesh were extended to ±86-88 m. The square grid is now graded: 0.36 m within 36 m, about 2.5% larger per step out to 88 m. That gives 172k vertices.
- **Cost.**
  - The deep cells seen from further back hide one another, and without a prepass every hidden fragment ran the full material: 35.3 ms.
  - The scene now asks for the depth prepass (`environment.depthPrepass`, ADR-1406). With the deformation baked it costs 6 ms and saves 11.
  - Live 1080p, Auto → High: **29.8 ms, 0 misses** (colour-pass baseline 26.0 ms).

**Evidence** (`correction/`):
- `shaded-before-colourpass-now-*.png` (pre-colour-pass, colour pass, now; Hyperborea 10, 48, 116, 134 s);
- `geometry-only-colourpass-vs-now-*.png`;
- `framing-options-A-B-C-*.png`.

Honest status:
- Relief depth now exceeds the colour pass in neutral grey.
- Fuchsia is restored, if sparser at 116 s than the colour pass's.
- The large figure leads. It reads as a web of deep cells with nested ridges more than as the colour pass's single centred petal-star, which is the centred mandala the earlier direction moved away from.
