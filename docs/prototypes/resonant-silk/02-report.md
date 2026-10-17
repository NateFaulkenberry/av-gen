# Resonant Silk: report

*Branch `proto/resonant-silk` (worktree `../av-gen-silk`, from main `ef3774da`, origin/main merged through `6cedd14c`).*

- **Briefs:** `00-brief.md` (the scene) and `03-brief-palettes.md` (the palette system, which supersedes 00 §4).
- **Audit and log:** `01-resonance-audit.md`, `PROGRESS.md`.
- **Review media:** `~/Desktop/av-gen-review/59-resonant-silk/`.
- **Registration:** the live example **Resonant Silk**, in the owner's **Resonator** live section (`examples/resonant-silk/resonant-silk.json`, `"live": true`, a
  `sonic.live` project, no audio track).
- **Generator:** `tools/resonant_silk/build.py`.

## What it is

One sheet of satin, about 340 m of cloth, hangs in a dark void as a single continuous mesh.
- **The wall.** The sheet enters from the left as a broad wall of pleats.
- **The gather.** Every pleat converges on an invisible ring right of centre: the *gather*, a narrow seam of light.
  It is the focal point.
- **The sail.** Beyond the ring, the same cloth opens into a thin, broad sail with a curved, fluttering hem. The
  sail rises, billows and turns over into the dark.

The music excites the sheet as a resonator:
- held bass bows it, and rings it down slowly;
- the bass of the last seconds runs out along the wall as a swell;
- the middle band draws curved creases across the pleats;
- a kick launches a front from the gather across both halves, carrying a band of light;
- a snare launches a quicker front across the wall;
- treble lays a fine sheen on the folds' turning edges.

In silence the sheet keeps breathing. The resonator's idle and a slow, coherent drift move it, so it never freezes.

There are eight palettes, each its own material: A Midnight Aurora, B Garnet Veil, C Liquid Champagne, D Opal Dream,
E Deep Emerald Satin, F Electric Cobalt, G Burnished Copper, H Oil-Slick Iridescence. A palette selector built here
for the engine (ADR-1420, now on main and shared with Cymatic World) chooses, locks and blends them directly in
OKLab. It also moves through them on phrase boundaries.

## 1. The existing resonance system, and what was reused (brief §2)

`01-resonance-audit.md` has the full table. In short, everything the scene needed already existed on main:
- the ADR-1400 resonance field: persistent modes with stable phase, per-band attack and release (ADR-1404), drift
  (ADR-1403), idle, limiter and competition;
- the ADR-1116 audio fields, which give travel without stepped state: `spectrum` with `audioSpeed` for the swell, and
  `onset` for the fronts;
- Field, Noise and Sine deformers, material programs, thin film (ADR-1143), `response.*` routes, `control.midi`.

Every one of these is a pure function of the clock and the audio history, so seeks are exact. The scene adds no
second analysis pipeline and no second resonance manager.

**The one engine change: ADR-1420, the palette selector** (`src/params/palette.*`, two hooks in `src/app/engine.cpp`).
- **Why.** The project palette (ADR-1043) could only blend along its ordered states, so Emerald to Champagne crossed
  Opal. It had no select, no lock and no musical advance.
- **What it adds:**
  - `palette/select` retargets from wherever the blend stands, straight in OKLab, with no jump;
  - `palette/transition` sets the blend's duration, `palette/hold` the least time between changes;
  - `palette/lock` stops automatic changes;
  - `advance` steps an authored order on a bus signal, with a threshold and a hold.
- **Seeks.** An offline seek replays the selector from zero, so a mid-track render lands on the palette a play shows.
  This is tested bit for bit against a play, with a control arm.
- **Process.** Approved by the coordinator before it was written. Landed on main as `5e5adf51`, with a reload-order
  fix as `60a57489`. Tests: `tests/unit/test_palette_selector.cpp` and
  `tests/integration/test_palette_selector_seek.cpp`.

**Gaps found and left alone:**
- the anisotropy tangent on procedural meshes is the object's +Y, not the UV direction;
- project `parameters` used to be read before the palette existed. This one was fixed by the reload-order fix.

## 2. The sculpture (brief §3, §5)

`sheet_mesh()` builds the sheet as P(u, v) = C(u) + v·w(u)·W(u) + N(u)·D(u, v):
- **C(u)** is a centripetal Catmull-Rom centreline, resampled by arc length.
- **W and N** are a parallel-transported frame turned by an authored roll profile. Twist is slow and controlled, with
  no frame flips.
- **w(u)** is the span. It is broad on the wall, narrows to 2.5 m at the gather, and grows to 62 m in the sail.
- **D** is a billow (a sail between anchors) plus three families of pleats. Their depth comes from the cloth the
  span cannot hold (`cloth - span`), so the folds are deepest where the cloth is gathered. Their phases wander along
  u, so the pleats fan, merge and part instead of running as corrugation.
- **The sail's free end** is an arc rather than a cut: the corners fall back along u and flutter.
- **Size.** The mesh is 51,700 vertices and 102k triangles, with UVs, double-sided.

**Composition** (`stage-a/research-board.png`). The Gather was chosen over the Passage (symmetric, a vanishing-point
read) and the Canopy (no focal point). The gather sits a little right of centre. The wall gives the foreground and
leads the eye in, the sail gives the payoff, and the void around them is negative space. The camera drifts slowly
over 120 s and never follows the music.

**What the coordinator's gates changed:**
- gate 1: the sail's cut hem, its size, and too little silent motion;
- gate 2: an over-lit dense passage;
- the sail as a lumpy blob;
- the soft foreground.

Each was fixed in the geometry, not with lighting:
- the cloth now follows the span in the sail, so no pleatless band crosses it;
- the wall's near end moved back 40 m, so its pleats read crisp;
- the music's displacement fades out near the camera (`nearCalm`).

## 3. Resonance and musical behaviour (brief §6)

The music reaches the sheet through these readers:

| Input | Field | What the cloth does |
|---|---|---|
| Sub and bass, held | Resonator, point source behind the wall, modes 40-250 Hz, lambda 220-80 m, attack 0.8 s / release 4 s | broad bows build, hold and ring down (`final/sustained-55hz-drone.png`) |
| Bass, moving | `spectrum` 0-0.22 band x a 150 m carrier, travelling out from the gather at 65 m/s | a swell runs along the wall over several beats |
| Middle band | the resonator's second source, beside the wall (250 Hz-4 kHz, order 3, 2.6x wavenumber: lambda 27-7 m, drifting) | creases a few tens of metres long across the pleats (`mids-330hz-creases.png`) |
| Kick | `onset` low, from the gather, 34 m/s, 14 m wide, decay 0.9/s | a front leaves the ring across both halves and dies away, lit in the resonant colour (`transient-kick-front.png`) |
| Snare | `onset` mid, from mid-wall, 55 m/s, 5 m wide, decay 2.2/s | a quick local front on the wall |
| Treble | the analysis treble, uniform | a fine sheen on the folds' turning edges only (`highs-hats-sheen.png`) |
| Song build | `response.intensity` to `modes` (8 at rest, 12 in a build) | more modes ring: a more intricate figure |
| Silence | resonator idle 0.5, noise drift 7 m at 0.06, a slow sine swell | the sheet keeps breathing and the silhouette keeps evolving (`idle-silence.png`) |

All displacement runs along each half's **mean sheet normal**: the wall's for the wall and the sail's for the sail,
told apart by plane masks through the gather, so the ring itself never moves. Displacing along the pleated
surface's own normal pushes each pleat's two faces apart and inflates them into pillows; Stage B's first pass did
exactly that. `stage-b/probe-sheet.png` and `final/responses-sheet.png` compare the responses side by side. The
probe signal is a 55 Hz drone, then kicks, 330 Hz, snares, hats with 7 kHz, and silence.

**Two pitfalls, recorded for anyone reusing this:**
- A spectrum field's `audioSmooth` caps at 32 taps. A wider window samples sparsely and aliases the row jitter into
  stripes.
- The history's rows must be at least a vertex apart (`audioSpeed / 93.75` ≥ the mesh spacing), or the jitter shows
  as stepped ribbing. That is why the swell travels at 65 m/s.

## 4. Palettes, light and material (palette brief, brief §7)

The roles are bound through the project palette, onto the geometry and the lights. Nothing is a painted gradient.

| Role | Bound to | On the silk |
|---|---|---|
| environment | sky zenith, horizon and ground; fog | the void: dark, gently graded, no horizon line |
| shadow | the fill light | what the unlit side sinks to |
| base | the program's albedo | the cloth where it faces the eye |
| fold | the program's second albedo, mixed by Fresnel | the pleats' flanks and folds' inner walls (where the cloth turns away) |
| grazing | the key light (narrow, from behind right), the hems, the sail's low lamp | the satin highlights that slide along the pleats; the thin lit hems that draw the silhouette |
| resonant | the counter light, the sail's high lamp, the kick fronts' light | light the music puts on the cloth |
| peak | the gather's seam (a 9 m falloff), the treble sheen | narrow, rare |

**Per-palette tuning** is restrained: roughness 0.16-0.26, thin-film thickness and ior, key intensity, emission, fold
contrast, and `irid`. `irid` is a view-dependent spectral sheen at true grazing angles: strong in Oil-Slick, pastel in
Opal, none or a trace in the rest.

**Music inside a palette** (palette brief §4):
- bass lowers the fold mask's Fresnel exponent (a broader fold band);
- low mids deepen the base and fold separation;
- mids are the resonance's creases;
- treble is the fine sheen;
- kicks are travelling bands, never a whole-surface flash.

There are no routes on global saturation or emission.

**Transitions.**
- *Automatic:* `beat.phrasePulse`, with a 40 s hold and a 6 s blend, in the journey order Aurora, Cobalt, Emerald,
  Champagne, Copper, Garnet, Oil-Slick, Opal. Neighbours share a temperature, so a change turns the material rather
  than jumping across the wheel. `final/journey-trench-60-120s.mp4` shows it: Trench arrives at 60 s already in
  Cobalt, because the seek replayed the earlier advance, then blends to Emerald at about 100 s.
- *No tempo* (ADR-1421, the Cymatic agent's extension of the selector, cherry-picked here): when no phrase has arrived
  for 30 s, a large sustained change in the music advances instead. The change is the sum of |fast - slow| over
  `sonic.energy`, `brightness` and `density`, with averages of 3 s and 24 s. It fires at 0.09 and re-arms below
  0.04. After 90 s of sameness the palette evolves anyway. Hold, blend and lock still apply, and seeks replay it.
  I checked this on 1 fps thumbnails of each whole track, classifying the palette on screen by colour. The
  no-tempo case is the selector's phrase signal replaced by one that never fires.
  - Trench: with tempo, a change about every 45-48 s (Cobalt 43 s, Emerald 92, Champagne 139, Copper 187). With no
    tempo: Cobalt 92 s (maxHold), Emerald 148 and Champagne 202 (energy changes).
  - Hyperborea, no tempo: 93 s and 154 s.
  - Feline Footwear, no tempo: 94 s and 181 s.

  Changes are never closer than the 40 s hold, and never rapid. At 0.09 the threshold fired on none of the three
  tracks, so it was lowered to 0.06 (re-arm 0.03).
- *Manual:* CC 9 selects a palette as a direct blend, and CC 10 locks it.

**Media** (`palettes/`):
- `all-8-palettes.png` (same camera, moment and exposure) and `palette-<n>-<name>.png`;
- `aurora-to-garnet-transition.mp4` and `emerald-to-champagne-transition.mp4`, with sheets;
- `oil-slick-in-motion.mp4`;
- `high-energy-cobalt-feline.mp4`, `sustained-emerald-trench.mp4`, `low-energy-opal-hyperborea.mp4`.

## 5. Live controls (brief §9)

Each control is MIDI through `control.midi`. 0 is the authored look, and turning a control up adds.

| CC | Control | Moves |
|---|---|---|
| 1 | Resonance | the resonator's sustain and transient |
| 2 | Fold Depth | the resonance deformers' amounts (x1-2.5) |
| 3 | Wave Travel | the swell and front deformers' amounts (x1-2.5) |
| 4 | Surface Detail | the middle band's amplitude (finer creases) |
| 5 | Iridescence | thin-film thickness and the spectral sheen |
| 6 | Light Response | the treble sheen and the resonant light |
| 7 | Color Drift | the palette journey changes sooner (hold) and turns faster (transition) |
| 8 | Motion Scale | an integrating route on the swell's phase (ADR-1372: a rate change never jumps), and a wider drift |
| 9 | Palette | `palette/select`, the eight palettes |
| 10 | Palette Lock | `palette/lock` |

## 6. Performance (brief §10)

Measured on an Apple M2 Max, macOS 26.7.1, release build, through `tools/gpu-lock.sh`. The GPU was shared with two
other agents under the lock.

The run used `avgen --live-profile` on the committed live project, fed `--audio assets/audio/feline-footwear.wav`
(ADR-1408), from 40 s. The window was 1920x1080 at a 30 fps target.

| Case | Internal resolution | GPU median / P95 | Frame mean (headless: serial) | CPU work median | Status |
|---|---|---|---|---|---|
| **Live** (`--mode live`, Auto, 30 fps, projection 1920x1080, Fifo) | Auto settles at **High** (1632x918, 0.85) | 27.7 / 30.3 ms | 33.32 ms (P99 35.2) | 1.6 ms | **Target achieved: 0 deadline misses** in 151 frames |
| Headless Ultra | 1920x1080 (1.00) | 30.5 / 34.0 ms | 34.1 ms | 2.2 ms | at the edge of 33.3 ms |
| Headless High | 1632x918 (0.85) | 28.1 / 31.1 ms | 32.9 ms | 2.1 ms | under |
| Headless Medium | 1362x766 (0.71) | 25.8 / 28.4 ms | 29.9 ms | 2.1 ms | under |
| Headless Low | 960x540 (0.50) | 24.4 / 25.8 ms | 27.4 ms | 2.2 ms | under |

The scene draws 3 draws and 102k triangles, with no shadow casters, no volumes and no depth prepass. GPU memory is
283 MB of textures (render targets) and 60 MB of buffers. The scene pass is everything: lighting is 0, bloom 0.13 ms,
and other post 0.07-0.13 ms. An earlier live run, before the last vertex cut, settled at Medium with 0 misses.

**Where the time goes.** Almost all of it is the scene pass, and the scene pass is almost all vertex work: GPU time
falls by only about 5 ms from Ultra to Low. Every vertex runs the deformer chain three times for its
finite-difference normal. Measured on the way, in order:

| Change | GPU at 1080p Ultra |
|---|---|
| Stage A prototype (92k vertices, drift only) | 17.4 ms |
| Stage C, all music (133k vertices, 8 deformers, depth prepass, 32-tap spectrum smoothing, three per-pixel field reads) | 190 ms |
| No AO or contact shadows, so no depth prepass (ADR-1406) | 158 ms |
| Spectrum smoothing 0.33 to 0.05 s (32 to 5 taps) | 71 ms |
| Treble as a uniform; no per-pixel resonance read | 44 ms (with 0 taps) |
| 133k to 76k vertices | 37 ms |
| 76k to 52k vertices | 32 ms |

Deformers with amount 0 still pay: the chain is evaluated regardless. Mode count and the snare deformer barely
matter. Vertex count is the lever.

**CPU work** is 1-4 ms a frame. The resonance resolve, field packing and routes are well under a millisecond.

## 7. Changed files

**Scene** (commits "Resonant Silk, Stage ..."):
- `tools/resonant_silk/build.py` and `__init__.py`: the generator;
- `examples/resonant-silk/resonant-silk.json`, `resonant-silk.scene.json`, `meshes/rs-silk.glb` (~3 MB);
- `examples/index.json`: the Resonant Silk entry;
- `docs/prototypes/resonant-silk/`: the briefs, audit, progress and this report.

**Engine** (separate commits; ADR-1420 is mine, ADR-1421 is the Cymatic agent's, cherry-picked):
- `src/params/palette.{hpp,cpp}`;
- `src/app/engine.cpp` (the step call, the seek replay, the attach order);
- `tests/unit/test_palette_selector.cpp`, `tests/integration/test_palette_selector_seek.cpp`;
- `docs/decisions/ADR-1420-...md` and its README row; ADR-1421 (`d20eada6` on proto/cymatic-split, cherry-picked as
  `ddac5683`).

## 8. Remaining weaknesses and next steps

- **Vertex-bound.** At 52k vertices the pleats are as fine as the budget allows, and live Auto settles a level
  below High. The next most valuable step is a non-uniform u spacing: dense where the cloth is near or large on
  screen, coarse along the far wall. It should buy roughly a third of the vertex cost back, either for finer
  pleats or for High. After that: a bake of the sided displacement into a UV-space grid (ADR-1407 bakes only a
  world plane today), so each vertex reads one texel instead of six compound fields at three points.
- **The fixed-axis displacement slides the cloth where the local normal leaves the mean.** That is mostly on the
  near wall, which is calmed there. A per-vertex "base normal" attribute in the procedural vertex stage would let the
  music displace along the unpleated sheet's normal everywhere. That is a small engine change, and was not asked
  for.
- **Transitions.** Green to gold passes through a faint olive midpoint. It is OKLab-correct but a little muddy. A
  chroma lift at mid-blend, which needs the selector's progress published as a signal, would keep it vivid.
- **Oil-Slick's sheen** shows some olive between violet and rose in places.
- **The geometry-only view** is lit by the authored default light colours (teal and violet), not neutral grey light.
- **No silk anisotropy.** The anisotropy tangent on procedural geometry is the object's +Y, not the UV direction.
  With UV tangents, the highlight could stretch along the fibres.
- **The middle band's creases are subtle on a pure tone.** At 330 Hz the probe shows them only faintly: the
  low modes' idle and the limiter share the energy, and a crease along the wall's mean normal reads mostly as
  shading. In real music they add up with the strikes. A material term for "resonant colour bands", which was
  dropped for its per-pixel cost, would make them read.
- **Dense, broadband passages** still crowd the wall with creases and fronts at once. A route of
  `response.intensity` onto the fronts' gain, opposite to the modes, would thin them when the mix is full.

## 9. Final checks

Run once at the end, on `1c4772b7`. That commit is the scene and generator; ADR-1420 and its fix, and ADR-1421
cherry-picked, are all included. Each figure is the binary's own exit code.

| Check | Result | Exit |
|---|---|---|
| `tools/ci/run-suite.sh cpu-assets` (under the GPU lock) | PASS: 4306 passed, 0 failed, 19 skipped, 1 expected failure, 4326/4326 ran | 0 |
| `tools/ci/run-suite.sh gpu` | PASS: 665 passed, 0 failed, 1 skipped, 666/666 ran | 0 |
| `python3 tools/review_examples.py --only "Resonant Silk"` | ok (the scene opens through the Examples menu and renders) | 0 |
| `python3 tools/check_project_integrity.py` | 0 problems | 0 |

## 10. Quality pass (2026-10-17, `art/resonator-quality`)

The owner's quality brief and colour-field addendum: `docs/prototypes/resonator-quality/` (report `01-report.md`).

- **Composition.** The sheet now covers about 74% of the frame, from about 44%:
  - the camera moved 74 m nearer the gather and a little above the wall, at 24 mm;
  - the sail comes round toward the camera and down;
  - the wall is broader;
  - the waist opens from 2.5 m to 12 m, with a longer taper and less cloth through it.
- **Colour and light:**
  - an iridescent cyclic OKLab gradient (ADR-1470) along the cloth and across the pleats, with a fold-angle shift;
  - the fill is a lifted fold colour (`fillLight`), where it was the near-black `shadow` role;
  - metallic 0.35 → 0.15.
- **The waist's stepped terraces** were the mids' resonance creases sliding the twisting cloth along the wall's mean
  normal. Mid amplitude 1.5 → 0.9 and scale 2.6 → 1.9; the halves' masks rise over 70 / 50 m.
- **Program:** 37 ops (from 43). Colour × scalar is one ramp, and the gather's seam is read from a distance field
  (`gatherSeam`).
