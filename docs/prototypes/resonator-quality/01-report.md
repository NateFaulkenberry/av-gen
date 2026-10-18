# Resonator quality pass: report

The briefs are [00-brief.md](00-brief.md) (brightness, geometry, Silk composition) and the owner's addendum
[02-addendum-procedural-color-fields.md](02-addendum-procedural-color-fields.md) (procedural colour fields). The work
is on branch `art/resonator-quality`, from main `dd27374c`.

- **Audio.** Feline Footwear (`assets/audio/feline-footwear.wav`) is fed with `--audio`, since the scenes carry no
  track. Trench and Hyperborea are used for variety.
- **Media.** `~/Desktop/av-gen-review/64-resonator-quality/<scene>/`:
  - `before/`, `colour-pass/` (the mid-way state) and `after/`: stills at Feline 15, 50, 120 and 190 s, at 1080p;
  - `<scene>-before-after-*.png`: those stills side by side;
  - `geometry/` and `composition/`: the diagnostics;
  - `colour-field/`: the addendum's §9 layers;
  - one 8 s clip per scene, `<scene>-after-motion-feline-40-48s.mp4`.

**Viewing note.** Review the stills at 1:1. A downscaled 1920x2200 sheet shows stepped contour rings in a viewer that
are not in the files (see "Banding" below and `banding-check/`).

## Summary

| | Cymatic World | Waveform Dimension | Resonant Silk |
|---|---|---|---|
| Darkness came from | a painted light whose ambient floor was 0.07 of the key, hollow floors held at 10% of their colour, dark albedo roles (×0.28), and a palette-tinted key that multiplied every hue back into one | a 0.35 fill from below and 0.03 of a near-black sky as ambient: every face turned from the grazing key went to black | the fill light was bound to the palette's near-black `shadow` role (no fill at all), a dark base role, and 0.35 metallic eating the diffuse |
| Colour | a four-stop OKLab gradient between per-palette gradient roles, on the figure's signed F, a drifting world band and a ridge shift | travelling chromatic waves: a cyclic OKLab ramp along a world axis, bent by the displacement, a step per sheet, phase from the music | an iridescent cyclic gradient along the cloth and across the pleats, with a fold-angle shift |
| Geometry | the serrated lips were the square grid's alternating diagonals on near-vertical walls; now an offset-lattice triangulation (same vertex count) | shards, notched cut edges and horns at the cut's corners; now exact crossings, a smooth-min corner, and fragments dropped | the camera came 74 m nearer, the sail comes round and down, and the waist widens from 2.5 m to 12 m; the stepped terraces at the waist (music creases) are tamed |
| Program ops | 43 → 41 | 29 → 30 | 43 → 37 |
| Engine | ADR-1470 (`oklabRamp`), shared by all three | | |

## The shared change: ADR-1470, a four-stop OKLab ramp

`oklabRamp` blends four stops (`constant`..`constant4`, linear RGB) in OKLab. A negative `value` makes it cyclic
(`fract`, with the first stop again at 1).

- **Why an op.** The only gradient op, `ramp`, has three stops and blends in linear RGB, so the midpoint of two
  distant hues greys. The chain of ops it would take to do better did not fit Cymatic World's 43 of 48.
- **Palettes keep working.** The stops stay in linear RGB, so ADR-1043 role bindings write them like any colour
  constant. Palette shifting, the ADR-1420 selector, CC 9 / CC 10 and the ADR-1421 fallback are untouched. The
  palette owns the colours; the program owns where each one falls.
- **Cost.** The packer uploads the stops already converted to OKLab (they are constants per frame), so a fragment
  mixes Lab and converts back once. Converting per fragment cost about 2 ms on Cymatic World at High.
- **Proof.**
  - Unit test: `[adr1470]`.
  - GPU parity: `oklabRamp` and `oklabRampCyclic` probes in the `[material]` batch.
  - Existing programs render bit-identically with the old and new `material.wgsl`: Ember Forest and Cymatic World
    frames have equal md5s.
  - Ember Forest at live High (a non-Resonator scene): GPU p50 25.9 ms with the old shader, 25.6 / 25.6 ms with
    the new.
- **Other shared code.** None in the renderer, pbr, tone map, AO or field code. `tools/sonic_vfx/kit.py` gained
  OKLab helpers (additive), now used by all three generators.

## Banding (the coordinator's question)

There is no banding in the renders:
- the pipeline is RGBA16Float up to the 8-bit output;
- bake grids are float32;
- no program uses `quantize` or `threshold`;
- the gradients are OKLab.

**Evidence.** A grain-off Waveform frame steps by at most one code value per pixel across smooth gradients (runs of
≤ 11 px of one value). The 1080p stills hold 117k–167k distinct colours. `banding-check/native-1to1-crops.png` shows
the regions that looked banded, at 1:1.

**What was seen.** The rings in the sheets were mostly the viewer posterising the large downscaled image. **One real
cause:** Waveform's glow was a contour band (|D| between 3 and 6 m, in and out, plus separate cores), whose
flat-edged rings read as banding beside the new gradient. It is now a single smooth rise with |D| (3.5 → 10 m).

## Cymatic World

**What caused the darkness** (from the program's layer views, `CW_DEBUG=albedo|light|emission`):
- The ground is unlit, with its light painted in the program.
- The ambient floor under the raking key was 0.07 of a unit hue, against the key's 2.3, so a face turned from the
  key kept about 3% of its light.
- Stage C remapped the hollow floors to 10% of their colour.
- The surface roles were the palettes' darkest (shadow, primary, secondary) × 0.28.
- The key and the floor were tinted by the palette, so they multiplied the surface back into the palette's one hue.

The albedo view showed indigo, violet and fuchsia regions; the lit frame was all ultramarine.

**Material and light changes:**
- **The chromatic field (addendum §2).** A four-stop OKLab gradient (ADR-1470) between ten per-palette gradient
  roles `g0..g3` (OKLCH stops, `GRADIENTS` in the generator), on a coordinate of three layers:
  - **A, the large figure's signed F** (the existing baked `shade-tint` grid, gain 0.9): broad regions that follow
    the lobes and re-form with the figure.
  - **B, a broad cosine band across the basin** (about 130 m, weight 0.45). Its phase is a persistent integral of
    `sonic.energy` plus a floor (ADR-1041), so it never resets: about one band every 60–90 s, quicker in loud
    passages, still drifting in silence. It crosses the cells, so colour boundaries do not all sit on the geometry.
  - **D, nodal proximity (sand)** shifts a ridge a step along the gradient, so a ridge is the next hue, not a white
    or cyan outline.
- **Lighting complements the field.** The key and the ambient floor lean 45% to white. The floor goes from 0.07 to
  0.6 of the key, which is cut from 2.3 to 1.6.
- **Depth.** The hollow floors keep 35% (was 10%) over a wider sand range.
- **Emission and highlights.**
  - The crest line goes from 1.9 to 0.35, and is wider (sand⁷ instead of sand¹³).
  - The travelling light takes the spectral hue instead of the near-white peak (4.0 → 2.5); in loud passages it
    had painted the whole ridge network white.
  - The sheen narrows (power 12 → 18) and dims (0.75 → 0.3).
- **The ten palettes keep their families.** Abyssal goes indigo → cobalt → violet → a narrow fuchsia, Crimson stays
  indigo-violet with the red at the ridges, and Molten, Oxidized, Glacial, Chlorophyll, Rose, Silver and Oil keep
  theirs. Glacial and Rose Quartz had their ambient multipliers cut (1.4 → 0.9, 2.2 → 1.25) and Silver and Crimson
  their lightness (the coordinator's review: they went flat).
- **The view layer (J's oil, E's metal) is gone.** Its three ops paid for the field. Those two palettes now carry
  oil and metal in their own gradients.
- **A dead-route bug, fixed.** The colour routes for bass (region extent) and sustain (surface shift) named the
  field op instead of its remap, so they never bound, and every load logged four "unresolved" warnings.
  - Bass now raises the colour field's gain.
  - Sustain shifts the band's phase.
  - The scene loads with 0 warnings.

**Ops:** 41 of 48 (from 43). **Fields:** unchanged.

**How vivid colour and shadow were kept.** The colour is in the pigment (the albedo debug view shows the full
gradient), not in emission or exposure. Exposure, AgX, bloom and grain are unchanged. The hollow floors and the
key's falloff still make the dark.

**Jagged geometry, the root cause** (Phase C; `geometry/`):
- **Not the field.** Raising the large ridge's width (sharpness 0.75 → 1.05) and rounding its crease (0.4 → 1.0)
  changed nothing. Evaluating every vertex directly with no bake (`CW_BAKE=0`) showed the same teeth, so neither
  the bake grid nor the normal step is the cause.
- **Not the normals or the silhouette.** A mesh-grid view lit by the key alone, from the same close camera, is
  smooth.
- **What shows the teeth.** Every program mask that reads the sand in xz (floor darkening, grit, crest line) on a
  near-vertical wall.
- **Why.** The square grid's alternating diagonals interpolate a steep lip as a two-cell zigzag. The teeth follow the
  mesh, not the bake: a finer mesh made them finer, never gone. A 0.30 m mesh (342k vertices) cost +5 ms at High.

**The fix.** An offset-lattice ("hexagonal") triangulation of the same graded grid:
- odd rows sit at the midpoints of the row below, so there are no quad diagonals;
- same vertex count (172k);
- triangles emitted strip by strip, front to back, alternating up and down.

The serrations are gone at both the scene camera and a close-up. Also changed:
- the large ridge is a little broader (sharpness 0.9);
- the crest line is wider;
- the floor mask is wider.

**Tried and set aside:**
- equilateral rows (√3/2 cell): +9% vertices and +3 ms;
- a 48 m fine core: +5 ms.

**Remaining:**
- A faint roughness on the very steepest lips in extreme close-up.
- The far rim of the frame (60–85 m out, cells up to 1.4 m) is coarser than the middle.

## Waveform Dimension

**What caused the darkness:**
- Lit PBR with a grazing key (3.0) and a 1.6 rim from behind.
- A 0.35 fill from below in the palette's dark `fill`.
- Image-based ambient at 0.03 of a near-black sky.
- Every face turned from the key fell to black, and the visible colour came mostly from the glow's emission.
- Vignette 0.4.
- **One colour per sheet.** Each nested sheet had a single colour (a three-stop ramp by sheet index).

**Material and light changes:**
- **Travelling chromatic waves.** A cyclic OKLab ramp (ADR-1470) through four per-palette analogous roles,
  derived from the palette's secondary, primary and band hues at a common lightness and 1.2× chroma.
  - **Coordinate:** the position along a world axis (4 cycles across the sculpture), plus the signed displacement D
    (0.08 cycles a metre), plus 0.3 per nested sheet.
  - **Movement.** The bands bend where the field moves the sheets, but their boundaries are phase-offset from the
    crests. The phase integrates `sonic.energy` plus a floor, so the waves travel and never reset.
  - **No complementary accents in the ramp.** Ultramarine Gold's gold in the ramp gave an olive-grey midpoint; the
    accents stay in the glow.
- **Light.**
  - A broad light from the camera's side of the arc (1.2, white leaning 35% to the palette's key, a new `front`
    role), so every face the camera sees takes some diffuse light.
  - The fill goes from 0.35 to 1.2.
  - Vignette 0.4 → 0.2.
- **The glow.**
  - A smooth rise with |D| (3.5 → 10 m), replacing the contour band and its cores (see Banding).
  - Gains 0.12 / 0.4 → 0.04 / 0.2, so the pigment, not the emission, carries the colour.
- **Ops:** 30 (from 29).

**Jagged geometry, the root cause.** The surface itself is accurate:
- surface-nets vertices sit within 1 cm (mean) of the level set;
- the normals are analytic, and match the faces within 0.5° (median).

The roughness was all in the CUT (`levelsets.py`):
- **Torn shards** floating in the openings: up to 7 disconnected fragments per sheet, some of 0–2 m².
- **Notched edges.** The crossing points were placed by linear interpolation of the cut value q, which is not
  linear across a 1.8 m triangle, so the edge kinked between them.
- **Horns** where the opening's cut meets the outer bound (`min()` makes a cusp).

**Fixes:**
- every edge crossing is found by bisection on the cut itself and put back on the sheet;
- `region()` uses a polynomial smooth minimum (8 units) at that corner;
- pieces under 400 m² are dropped.

**Tried and dropped:** snapping near-cut vertices onto the cut. It tripled the sliver triangles.

**Unchanged:**
- the 1.8 m cell;
- the vertex count, 112k (from 114k);
- the volume bake.

**Remaining.** One thin tongue survives where two openings nearly meet (a saddle of the aperture field).

## Resonant Silk

**What caused the darkness:**
- The `fill` light was bound to the palette's `shadow` role (#15152C in Midnight Aurora), so there was effectively
  no fill.
- The base role is dark (#29264F).
- Metallic 0.35 removed a third of the diffuse.
- The key grazes from behind.

**Material and light changes:**
- **An iridescent gradient.** A cyclic OKLab ramp through four analogous stops around each palette's base, fold and
  resonant hues (1.5× chroma, a common lightness). Its coordinate:
  - the cloth's length (1.6 cycles from wall to sail, the phase integrated from the music);
  - a cosine band across the world that crosses the pleats at an angle, so no strip of cloth has a colour of its
    own;
  - a small shift where the cloth turns from the eye (the satin's own iridescence; the low mids widen it).
- **Light and finish.**
  - The fill is the fold colour lifted halfway to white (a new `fillLight` role), at 1.1.
  - Metallic 0.35 → 0.15.
  - Vignette 0.4 → 0.2.
  - Roughness, thin film, the key and the `irid` sheen are unchanged per palette.
- **Ops:** 37 (from 43). Three ops are the gradient's net cost. Nine were saved: colour × scalar as one ramp, and the gather's seam read from a distance field (`gatherSeam`) instead of computed from the world position.

**Composition (Phase E).** Camera first, measured with a CPU preview of the static sheet for frame coverage
(`composition/`):
- **Camera.** It moves from (4, 2, 60) → (−16, 8, −60) at 26 mm to (4, 14, −14) → (−4, 4, −130) at 24 mm: 74 m
  nearer the gather and a little above the wall. On its own that took coverage from 44% to about 55%, leaving an
  hourglass with large empty wedges.
- **Fold development:**
  - the sail comes round toward the camera and down, its free end now reaching the bottom right;
  - the wall is broader (span 30 → 36 m);
  - the waist opens from a 2.5 m pinch to 12 m, with a longer taper either side (spans 26 / 12 / 30 m at
    u 0.5 / 0.62 / 0.7);
  - the cloth closes in through the waist (40 → 20 m at u 0.6), so the pleats do not fold over themselves.
- **Coverage** is about 74%. The dark wedges above and below the gather remain as negative space. The drift (120 s,
  a few metres) keeps the framing.
- **The stepped terraces at the waist.** The new close view showed them; they were already in the owner's contact
  sheet. They were the mids source's rings (λ 7–27 m) displacing the twisting cloth along the wall's mean normal:
  - they vanish with the resonance deformers off, and stay with the swell off;
  - the mids' amplitude goes 1.5 → 0.9 and their wavenumber scale 2.6 → 1.9 (longer creases);
  - the halves' masks rise over 70 / 50 m instead of 35 / 30;
  - the creases remain as music; the stairs do not.
- **No objects were added.** Vertex count is unchanged (51,700).

## Performance

All runs are live 1080p (`avgen --live-profile --mode live --target-fps 30 --size 1920x1080`) on the M2 Max,
through the GPU lock, Feline from 95 s for 20 s unless noted.

**The machine was shared and noisy.** During these runs the owner's Logic Pro, a VM and Steam were up, and another
agent was using the GPU, so the same build varied by up to 15% across the day. Only **paired, back-to-back** runs
are compared, and each is repeated. The quality pass's own baseline (31.7 ms High on CW) was taken before the
contention and is not comparable with these.

**Fixed High** (1632×918), GPU p50 in ms, pairs run back to back:

| Scene | Before | After | Change | Why |
|---|---|---|---|---|
| Cymatic World | 35.7 / 35.8 / 32.6 | 37.3 / 37.5 / 34.7 | **+1.6 to +2.1** | The offset lattice (about +1 ms at the same vertex count; triangles interleaved for cache reuse) and the colour field's op mix (41 ops, but `oklabRamp` and `palette` are heavier than the ops they replaced) |
| Waveform Dimension | 32.7 / 32.8 | 33.5 / 35.7 | **+0.8 to +2.9** | The front light, and one op more |
| Resonant Silk | 31.6 / 31.5 | 35.7 / 35.8 | **+4.2** | The sheet now covers about 74% of the frame instead of 44% (pixel cost on a lit, thin-film surface). The program was cut from 46 to 37 ops to pay part of it |

**Auto** (the live target), Feline 95–115 s, plus Trench and Hyperborea:

| Scene | Before | After |
|---|---|---|
| Cymatic World | Medium, frame p50 33.3 ms, 1.5% misses | Medium or Low, frame p50 33.3, 0–0.3% misses (Trench: Low, 0.2%; Hyperborea: Medium, 1.7%) |
| Waveform Dimension | High, 33.3 ms, 1.0% misses | High, 33.3, 0.3% (Trench: Medium, 0.5%; Hyperborea: High, 0%) |
| Resonant Silk | Medium, 33.3 ms, 1.3% misses (Trench: Emergency) | Low, 33.3, 0% (Trench: Low, 0.8%; Hyperborea: Medium, 0.2%) |

**Every scene holds 30 fps on Auto** (a frame p50 of 33.3 ms, misses ≤ 1.7%). The Auto level reached also moves
with the machine's load, so a Medium/Low difference between two runs is within this day's noise. At fixed High the
regressions above are real.
- Silk's is the price of filling the frame. The overhaul's far layers must stay cheap for that reason.
- CPU work is unchanged (1.3–5 ms in every run).
- Records: `/tmp/claude-501/rq/prof/final/*.json` (scratch). The method is `avgen --live-profile`.

## Audio, MIDI and palettes

- **Preserved:**
  - all field routes, MIDI maps (CC 1–11 per scene) and the Resonator category;
  - the palette selector (ADR-1420), CC 9 select / CC 10 lock, and the ADR-1421 fallback.
- **The palettes now drive gradients.** The per-palette gradient roles are extra colours in each ADR-1043 state, so
  the selector's OKLab blends move the gradients between palettes as well.
- **New routes, all bounded and smoothed, none on a hue:**
  - an integrating phase route per scene (`sonic.energy` × gain + a floor), which never resets;
  - Cymatic World's bass now changes the colour field's gain, and its sustain shifts the band's phase (the two
    routes that never bound);
  - Silk's low mids widen the fold shift.
- **Cymatic World loads with 0 warnings**, from 4 unresolved routes.

## Checks

Run once at the end, on head `cee3e1e7` (the scenes as committed), each process's own exit code:

- `tools/ci/run-suite.sh cpu-assets` (under the GPU lock): **PASS**, exit 0. 4309 passed, 0 failed, 19 skipped,
  1 expected failure; ran 4329/4329.
- `tools/ci/run-suite.sh gpu` (`AVGEN_GPU_LOCK_TIMEOUT=14400`): **PASS**, exit 0. 666 passed, 0 failed, 1 skipped;
  ran 667/667.
- `tools/review_examples.py --only` for Cymatic World, Waveform Dimension, Resonant Silk and Cymatic Plate: `ok 1`
  each, exit 0.
- `tools/check_project_integrity.py`: **0 problems**, exit 0.

Targeted checks along the way:
- `[adr1470]`;
- the `[material]` CPU and GPU batches;
- `[resonance]` (every scene's routes bound, and Cymatic World's load warnings gone, from 4 to 0);
- the bit-identity check for programs that do not use the new op.

## Remaining limitations

- **Cymatic World:**
  - a faint residual roughness on the steepest lips in extreme close-up;
  - the outer cells (60–85 m) are coarser than the middle;
  - the ridge network reads pale blue-lavender in Abyssal (the restrained icy highlight the addendum allows) rather
    than taking the gradient's hue.
- **Waveform Dimension:**
  - a thin tongue where two openings nearly meet;
  - the glow can still show a pale mixed tint where a palette's band and highlight are complementary (Jade Coral);
  - Ultra still misses 30 fps (as before).
- **Resonant Silk:** residual fine ribbing near the waist in loud passages, and the fixed-axis displacement
  limitation from the scene's report.
- **The gradient references** in the addendum were never supplied. The palettes were developed from the text.
