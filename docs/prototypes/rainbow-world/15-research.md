# Rainbow World 15: research dossier (stem reactivity, world FX, performance)

Brief 13, Phase 3.

**Labels.** Each entry marks what it is:
- **[fact]**: verified, in the source or by measurement here;
- **[proposal]**: my adaptation;
- **[assumption]**: not yet verified.

The Stage 1 dossier (`02-research.md`) covers the world's look. This one covers making it react.

**Licences.** Nothing below is copied into the repository. Each technique is reimplemented in AV Gen's material ops,
routes and deformers. Licence notes say whether copying would have been allowed.

## 1. Turning audio into control signals

| Source | Technique and takeaway | Relevance | Adaptation | Cost | Licence |
|---|---|---|---|---|---|
| Derivative, *Audio-Reactive Visuals with GLSL* and *Audio-Reactive Visuals in TouchDesigner* (brief's links; not fetched in this session, so the summary is from the well-known TD workflow) | The pattern: analysis CHOPs (band levels, onset triggers), then Lag/Filter CHOPs with **separate attack and release** per control, then a uniform. Different elements get different lags; triggers drive "trigger CHOP" envelopes, levels drive continuous parameters. **[assumption about the page's exact content]** | This is exactly AV Gen's `signal -> route chain (attack/decay, envelope, curve) -> parameter`. Nothing new is needed. | Every effect gets its own route chain; no shared smoothing constant (§5.4 of the brief). | CPU, µs per route | Tutorials; concepts only |
| Joe Mighty, *Creative Coding: Audio-Reactive Visuals* (brief's link; not fetched) | Beginner pipeline: band split, smoothing, map to transform/colour. **[assumption]** | Baseline only. | None beyond the above. | none | concepts |
| Bello et al., *A Tutorial on Onset Detection in Music Signals*, IEEE TSAP 13(5) 2005 ([pdf](https://www.cs.bu.edu/fac/snyder/cs583/Literature%20and%20Resources/bello_onset_tutorial.pdf)) | **[fact]** A survey of onset detection functions: energy, spectral flux, phase, complex domain. Peak picking over an **adaptive threshold** (a moving median or mean) with a minimum spacing. Onset is not beat. | The vocabulary for §5: an onset fires on any attack; a beat is the grid. | My analysis script uses log flux + a moving-mean threshold + 45-120 ms minimum spacing per class (`scripts/stem_analysis.py`). | offline | paper |
| Böck & Widmer, *Maximum Filter Vibrato Suppression for Onset Detection*, DAFx 2013 ([DAFx](https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA)); madmom `superflux` ([docs](https://madmom.readthedocs.io/en/v0.16/modules/features/onsets.html)) | **[fact]** SuperFlux: log-magnitude spectral flux with a frequency max-filter on the previous frame, up to 60% fewer false positives on vibrato. Causal, so real-time capable. | AV Gen's `stem.<key>.onset` is already **log**-spectral flux (ADR-1520), the first half of SuperFlux. Measured on the drum stem it fires on every layer's step within a frame. The synth's pumping does not create false onsets because the log compresses the envelope. | No change: the engine's onset is good enough; the max filter is not needed. | none | madmom BSD-2 for code; not copied |
| Keavon, *Web-Onset* ([github](https://github.com/Keavon/Web-Onset)) (brief's link) | **[assumption, not opened]** A browser spectral-flux onset demo with an adaptive threshold. | The same principle as above. | none | - | - |
| Ellis, *Beat Tracking by Dynamic Programming*, JNMR 36(1) 2007 ([pdf](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Ellis_2007.pdf)) | **[fact]** A global tempo from the onset autocorrelation, then DP over a local onset score plus a transition cost for constant spacing. **Onset is evidence; the beat is a smooth grid through it.** | AV Gen's offline tracker is this (docs/audio.md). On Feline it gives 120.8 BPM with correct downbeats (14 §2). | Impacts are **timed by the beat clock and gated by the stem**: a grid that is musical and stable, plus evidence that the hit is really there (16 §2). | none | paper |
| AudioLabs FMP, *Beat Tracking* notebook ([FMP C6S3](https://audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S3_BeatTracking.html)) | **[fact]** A worked DP beat tracker and the novelty functions behind it. | Cross-check of the above. | - | - | CC BY-NC-SA |
| Frame-rate-independent smoothing: Holmér, *Lerp smoothing is broken*; *Improved lerp smoothing* ([gamedeveloper](https://www.gamedeveloper.com/programming/improved-lerp-smoothing-)) | **[fact]** `x += (target - x) * (1 - exp(-dt / tau))` converges the same at any frame rate; a constant lerp factor does not. | AV Gen's route chain already uses a dt-based coefficient (`smoothingCoefficient(ms, dt)`, test_route_chain). Rendering at 30 fps live and 30 or 60 offline gives the same envelopes. | No custom smoothing anywhere; all of it through route chains. | none | concept |

## 2. World-space waves and procedural detail

| Source | Technique and takeaway | Relevance | Adaptation | Cost | Licence |
|---|---|---|---|---|---|
| Finch, *Effective Water Simulation from Physical Models*, GPU Gems ch. 1 (brief's link) | **[fact, known chapter]** Sum of a few directional sine/Gerstner waves, evaluated per vertex for shape and per pixel for normals. Amplitude, wavelength, direction and speed are chosen together so the sum reads as one surface, not noise. | The bass swell: a few world-space waves, not one. | Two world-space `sine` deformers with different axes, wavelengths (70 and 115 m) and speeds, on the band and every scatter node, so ground and vegetation move together (16 §3). | per vertex, about 10 ALU per deformer | NVIDIA free text; concept |
| Perlin, *Implementing Improved Perlin Noise*, GPU Gems 2 ch. 26 (brief's link) | **[fact, known chapter]** Gradient noise on the GPU, octave sums for detail. | AV Gen's `noise` op and deformer are fbm3 already. | Used for the glint cells' per-cell randomness, through `quantize` (hash per cell) and not noise. | - | concept |
| Guardado & Sánchez-Crespo, *Rendering Water Caustics*, GPU Gems ch. 2 (brief's link) | **[fact, known chapter]** Caustic light patterns computed procedurally and projected onto receivers. | Considered for "light ripples on the meadow" and rejected: there is no water here, and a projected caustic on grass reads as water. | Not used. | - | - |
| DonBeleren, *gpu-water-rendering* (brief's link; not opened) | **[assumption]** A GLSL water example with layered waves. | As Finch. | - | - | check before reuse |
| Mitchell, *Volumetric Light Scattering as a Post-Process*, GPU Gems 3 ch. 13 ([NVIDIA](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-13-volumetric-light-scattering-post-process)) | **[fact]** Screen-space radial blur toward the sun, 2D, with occlusion from the frame. Fails when the sun is behind the camera. | The brief asks for light shafts. Rainbow World's camera rolls a full turn and the fisheye shows half the sky, so the sun is often off-frame or behind. A screen-space shaft would come and go with the camera, not the music. | Not used. "Shafts" here are the arches' light and the sky's glow, which are world-space. | 1-2 ms at 1080p if it were used | - |
| Quílez, *Palettes* ([iquilezles.org](https://iquilezles.org/articles/palettes)) | **[fact]** `a + b cos(2 pi (c t + d))`: a smooth periodic palette from four vectors. | AV Gen's `palette` op and `oklabRamp` (cyclic, ADR-1470) are the same idea. | The bands that run along the arches use a cyclic `oklabRamp` through the bow's own spectrum, so a moving band never leaves the rainbow's palette (no arbitrary hue rotation, brief §4.3). | 1 op | concept; Quílez's code is MIT on Shadertoy where marked |
| AV Gen ADR-207, `wave_effects.wgsl` | **[fact, read]** World effects: directional or radial fronts in the **ground plane** (`length((p - o).xz)`), shaded additively in `pbr_shade`. | Rainbow World's gravity turns a whole turn round the knot: an xz-plane radial ring is wrong on every face that is not horizontal. | Not used. Rings are a material op on **camera distance** (a sphere about the eye), which follows any face (16 §2). | - | - |

## 3. Vegetation and entity motion

| Source | Technique and takeaway | Relevance | Adaptation | Cost | Licence |
|---|---|---|---|---|---|
| Sousa, *Vegetation Procedural Animation and Shading in Crysis*, GPU Gems 3 ch. 16 ([NVIDIA](https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch16.html)) | **[fact]** A main bend for the whole plant plus a detail bend for leaves, per instance in world space, with per-vertex weights; phase varied per instance so a field does not move in lockstep. | "Coordinated motion that avoids every tree moving identically" (brief §6.2). | AV Gen's ADR-055/360 wind (main bend, branch, flutter, per-instance phase) is this model. **[assumption until measured]** It reaches procedural point nodes through the node's `wind` block. The bass drives its strength. | vertex | concept |
| Zioma, *GPU-Generated Procedural Wind Animations for Trees*, GPU Gems 3 ch. 6 ([NVIDIA](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees)) | **[fact]** Hierarchical bending in the vertex shader, instancing-friendly. | As above. | - | - | concept |

## 4. Composition, restraint, timing

| Source | Takeaway | Adaptation |
|---|---|---|
| The VJ and lighting-design practice the brief describes: an intensity hierarchy, rare big cues, no constant strobing | **[proposal]** Three tiers: continuous (always), rhythmic (each beat or note, bounded at about 25-40% of the frame's light), accents (phrase and drop, rare). Never two systems at maximum at once. | 16 §5's hierarchy. Accents are gated by the phrase pulse and the synth's level against its reference, not by every hit. |
| Photosensitivity guidance (WCAG 2.3.1: no more than three general flashes a second over a large area) | **[fact]** A flash covering a large share of the screen faster than 3 Hz is a seizure risk. | **[proposal]** Kick rings are rings, not the frame: under 15% of the screen lit at once. No full-frame flash anywhere; the bloom accent from Stage 7 (kick -> bloom) is removed. |

## 5. Performance references used

| Source | Takeaway | Used for |
|---|---|---|
| AV Gen `docs/performance.md`, ADR-1493 (compiled programs), ADR-1491 | **[fact]** A compiled program costs about 0.09 ms per op on a full 1080p frame (the profile's own model). A program change of constants needs no recompile; a structural change builds a variant on Dawn's workers. | Effect ops are added to existing programs as constants routes move. The structure is fixed at load (§slow frames). |
| Quad overdraw on small triangles (common GPU knowledge: 2x2 quads shade partially covered quads in full) | **[fact, measured here]** The far leaf cards at 90-150 m cost about 7 ms at the arch holds (14's A/B in `17-slow-frames.md`). | The slow-frames fix. |
