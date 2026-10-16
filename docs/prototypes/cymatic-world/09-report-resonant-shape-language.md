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
