# Cymatic World: resonant colour, lighting and optics (report)

The brief is [10-brief-resonant-color-light-optics.md](10-brief-resonant-color-light-optics.md). It builds on the shape-language work ([09](09-report-resonant-shape-language.md), head 74edc063). The work is on `proto/cymatic-split`, one commit per stage ("Cymatic World light pass, Stage X: ...").

**Audio.** Feline Footwear (`assets/audio/feline-footwear.wav`), fed with `avgen --audio`. The scene carries no track (ADR-1408). Scenario 10 also uses Trench and Hyperborea.

**Media.** `~/Desktop/av-gen-review/58-cymatic-world/light-pass/`, one folder per stage.

**Performance target.** Live 1080p, 30 fps, Auto.

## Summary (the brief's §17)

The light pass ran Stages A-H. Mid-way, the owner's multi-palette brief ([12](12-brief-multi-palette.md)) replaced its single-palette framing, so the scene now shifts palettes as well as shapes. Geometry and motion are untouched.

1. **The original limitations (Stage A).**
   - One fixed hue scheme.
   - Colour mostly from lighting, not from the resonant state.
   - Every contour outlined alike.
   - Fuchsia in soft patches.
   - After Stage B, the frame lost its darkness.
2. **The colour-field approach (Stage B).** The large figure's signed standing pattern F is baked once a frame and maps one lobe sign to the primary surface, the nodes to deep shadow, and the other sign to the secondary surface. Colour regions are the figure's own lobes, bounded by its nodal lines, and evolve with it.
3. **Colour tied to geometry and resonant state.**
   - Hollow floors fall to near-black through the sand; the lobe colours live on the cell walls.
   - The grit marks the ridges.
   - The glow lives only in excited cells, on their inner walls, in one lobe sign.
   - All colours are palette roles (Stage P), so the hierarchy holds in all ten palettes.
4. **Ridge and nodal illumination (Stage C).**
   - The large crest line (sand¹³ × excitation) is the brightest thing in the frame.
   - Glints are the fine tier.
   - The medium-crest light was built and then turned off for budget (Stage G, `CW_LINE2=1`), which leaves large > fine with the medium carried by the relief.
5. **Travelling and optical effects.**
   - *Kept:* the travelling light (Stage D, ADR-1409: ripples-only readers, as dashes along the crests from each hit, dark in silence) and the palette view layer (J's oil sheen, E's grazing metal).
   - *Rejected* (Stage E, each judged alone): tuned bloom, anamorphic glare, halation and chromatic aberration.
6. **Audio (Stage F).** Each layer has its own signal and time scale (table in Stage F). Bass sets region extent, held sound shifts the surface, snares light the crests, hats the glints, kicks the glow in ringing cells, onsets the travelling light, and phrase boundaries the palette. No hue routes, and no whole-screen flash (measured).
7. **Post-processing.** The vignette went to 0 in Stage B, and the depth prepass was turned off in Stage G (re-measured). Bloom, AgX, grain and contrast are unchanged. Motion blur and trails stay off.
8. **Evidence.** One folder per stage under `light-pass/`. The ten validation scenarios are clips in `validation/`, at normal speed with audio. The palette evidence (sheets, Silver alone, phrase transitions, manual lock) is in `palettes/`.
9. **Cost at live 1080p / 30 fps** (loud Feline):
   - High: GPU median 31.8 ms, p95 33.3 (from 35.7 / 36.1 before Stage G).
   - The palette system is GPU-neutral and costs +0.1-0.4 ms CPU.
   - Auto with ADR-1363 holds High, then drops to Medium on the loudest passage, with 0.4% missed frames.
10. **Build, tests and limitations.** See "Checks" at the end.

**Implemented and tested:**
- ADR-1409, with unit and GPU parity tests;
- the palette system, on ADR-1043 and ADR-1420 (whose tests are theirs);
- the generator's 16-field assert;
- every stage's look, verified in renders.

**Not tested by an automated test:** the scene's look and the palettes' character (judged in stills and clips).

**Experimental, not shipped:** the medium-crest light (`CW_LINE2=1`) and the depth prepass for this scene (`CW_PREPASS=1`).

## Stage A: baseline and diagnosis

**Captures** (`stageA-baseline/`):
- stills at Feline 4, 18, 60, 125, 160 and 190 s;
- a 40-70 s clip with audio;
- the post settings, `baseline-settings.txt`: bloom 0.22 / threshold 1.2 / radius 0.6, AgX chroma retention 0.9, contrast 1.22, exposure +0.2, vignette 0.45, grain 0.012, motion blur 0, depth of field off.

**The shading** before this pass:
- An unlit ground with its light painted in a 37-op material program, which reads baked grids (ADR-1407) of the large figure's nodal network (sand) and the excitation |F|.
- The program samples at most four fields.
- The interpreter costs about 0.36 ms an op at High.

**Its limitations:**
1. **No colour field.** Colour was a two-state switch: teal grit on ridges, navy stone elsewhere. There were no broad, organised regions.
2. **Fuchsia was unstructured.** It sat in soft patches gated only by loudness, even in the near-silent intro.
3. **Equal outlines everywhere.** Every crest carried the same white line, there was no hierarchy, and the sheen made white hot spots.
4. **Monotony in dense passages.** One key light shaded every cell alike: at 125 s the frame is cyan-white wallpaper.
5. **No travelling light, and no colour evolution** beyond the figure's own.
6. **A vignette** the brief rules out.

**Cost** (live 1080p): Feline 100 s, Auto → High 32.4 ms, 6% misses; Medium 25.4 ms. Hyperborea 116 s, High 29.4 ms.

## Stage B: the resonant colour field

- **The field.** The large figure's signed standing pattern F (a relief reader: figure only, the large band) is baked into a 640² grid. The stone takes its colour from it through a ramp: deep violet on one sign of lobe, the navy stone at the nodes, deep blue on the other. Each lobe is a broad colour region bounded by the figure's own nodal lines. It evolves as the figure's foci, drifts and envelopes do, and quiet music (small F) settles it toward navy: negative space.
- **The grit** keeps the ridges, as its own constant, which the build's grit-hue route still turns.
- **The fuchsia belongs to the violet lobes.** The hollow glow (excitation × inner wall) is gated by the colour field, so it appears as arcs on the walls of one sign of cell, following the pattern, not as patches.
- **A field slot freed.** The crest line is now the sand raised to a power (sand¹³ = the sand at a quarter of the width), so its grid is retired and one of the program's four slots is free for Stage D.
- **Vignette** off (0).
- **Material:** 39 ops.

**Evidence** (`stageB-colour-field/`): `colour-field-alone-left-full-right-feline-40-70s.mp4` (the albedo debug view beside the full shade, with audio) and stills at the six timestamps.

## Stage C: geometry-aware illumination, and the darkness back

The coordinator's Stage B review: the colour field reads lobe by lobe, but the frame had lost its darkness, with cells bright electric blue where the correction round had navy and black hollows. Stage C gives the light a hierarchy and gives the dark back.

- **Hollow floors fall to near-black.** The albedo is multiplied by the sand, remapped so that the deepest sand (the cell floor inside its walls) keeps a tenth of its colour: `remap(sand, 0..0.4 → 0.1..1)`. The stone, violet and blue are darker as well (`#050a22`, `#2c0c6e`, `#0a2c84`). The violet and blue lobes now live on the cell walls, so the ridges and their light stand out against real negative space.
- **The light's hierarchy, large > medium > fine.**
  - *Large:* the crest line on the large figure's nodal ridges (sand¹³ × excitation), unchanged and the brightest thing in the frame.
  - *Medium:* a new secondary light. The medium network's crests multiplied by the excitation are summed by a compound before the bake (`shade-mline`, 768²), so the program spends one field slot on it. It is drawn at 0.4 in a soft blue (`#7fb6ff`), so it appears only where the medium figure is both a crest and excited. It is a faint second tier, not an outline on every contour.
  - *Fine:* glints only. The sheen is cooler and dimmer (`#9fe6ff`, 0.75 from 1.1), which tames the white hot spots on the stone flanks.
- **The ambient floor** is folded into the key's ramp (black-to-key becomes sky-to-key), so the hemisphere's three ops are gone with no change in look.
- **Rejected on the way:** reading the excitation as |F| from the colour field. It is zero exactly on the nodal lines, so it killed the crest line. The amplitude grid came back.
- **Fields:** sand, amp, tint and mline (four of four). Stage D's light has to share a grid through a compound. **Material:** 42 ops.

**Evidence** (`stageC-geometry-light/`): `stageB-top-stageC-bottom-feline-six.png` and `stageC-feline-six.png` (Feline at 4, 18, 60, 125, 160 and 190 s).

## Stage D: travelling illumination

- **The mechanism (ADR-1409, engine).** A resonance reader's new `rows` setting is `all`, `modes` or `ripples`, so a reader can hear the onset ripples (ADR-1403's travelling ring packets) and nothing else. A ripples-only reader is exactly 0 with no ring alive. That makes it a light that exists only while a hit travels outward from where it landed. The change is additive: the default is unchanged and never written, CPU and GPU skip the rows a reader doesn't hear, and there is a unit test (all = modes + ripples) and GPU/CPU parity readers.
- **The light.** The ripples' amplitude (`shade-wave`, gain 2.2) shares the secondary light's baked grid with the opposite sign: > 0 is Stage C's medium crest, < 0 the wave. The program then lays the wave only on the large crest lines, as dashes of pale light that run along the ridges for the ring's life, carried by the ring's own wavelength, and then go.
  - It is localised: each ring has its own launch point, not a centred global ring.
  - It is restrained: crest lines only, never the cell floors.
  - It is dark in silence.
- **Rejected on the way:**
  - The wave on the whole sand mask (sand¹ and sand²). It painted pale zebra stripes across walls and floors: the carrier read as noise, not light.
  - A wider threshold. It flooded the cells.
- **A defect found and fixed: the scene had gone over the GPU's 16 field slots.**
  - The first Stage D layout had 18 fields. Slots 17 and 18 (the relief compound and its bake grid) were silently not uploaded, so the ground's relief sampled 0 and the terrain went flat, with one log line.
  - Stage D now fits 16 by folding the excitation multiply into the program and dropping a compound.
  - The generator now asserts at most 16 fields.
  - The engine's behaviour (fields past 16 sample 0 with one log line) is unchanged and noted here for the ADR follow-ups.
- **Material:** 47 ops. **Fields:** 16 of 16.

**Evidence** (`stageD-travelling-light/`): `travelling-light-off-left-on-right-feline-6-16s.mp4` (normal speed, with audio) and `travelling-light-frames-0.15s-apart.png`.

## Stage P: the ten palettes (12-brief-multi-palette.md, replacing brief 10's single palette)

The owner's correction: modulating colour means moving between distinct palettes, so the scene now shifts palettes as well as shapes. The brief's ten artist-designed palettes (A Abyssal Resonance to J Prismatic Oil) are now the scene's colour. The four shading layers from Stages B-D stay, and every layer's colour now comes from the active palette.

**Architecture (no new engine code in this scene).** The palettes are ADR-1043 project-palette states. ADR-1420's selector, built by the Resonant Silk agent and merged from main, does the choosing and blending.
- **The program is authored in grey.** Palette `M` is palette B's intensities with every colour white. Role bindings in `multiply` mode scale the program's colour constants by the active palette's roles each frame: 21 bindings onto 9 ops' vec4 constants.
- **Routes keep working.** Every route that scales a term's brightness (kick, snare, hats, sustain, flux, CC 8) still does, because the palette multiplies after them.
- **Colour stays on the GPU.** The CPU writes a few uniform constants per frame. There is no per-vertex colour work and no allocation in a transition.

**The roles.** Each of the brief's seven roles maps onto the existing layers, so the hierarchy holds in every palette:

| Layer | Program term | Palette role |
|---|---|---|
| base field (Stage B) | colour field: one lobe sign, the nodes, the other sign | primary surface, deep shadow, secondary surface |
| base field | grit on the ridges | secondary, leaning resonant (per palette) |
| light | raking key / ambient floor / counter fill | white tinted by a chosen role / primary hue / resonant |
| ridge illumination (Stage C) | large crest / medium crest | spectral highlight / secondary + spectral |
| emission | hollow glow in excited cells | resonant |
| optical finish | sheen glints, travelling light (Stage D) | rare peak |
| view layer (new) | Fresnel ramp, facing → grazing | J: petrol → violet → rose; E: none → copper → pale gold; others none |

- **Albedo vs light roles.** Albedo roles are the brief's display colours × 0.28 × a per-palette factor: the ground shows albedo × key through AgX, about 4× on a lit flank. Light and emission roles are hues at unit peak × a per-palette gain, so a term's brightness stays the program's.
- **Character.** Each palette has its own small tuning (`PALETTE_TUNE`), shaped by the coordinator's review of the first sheet:
  - A is the darkest, with an ultramarine key; F is icy, lighter and crisp. They now differ in temperature and darkness.
  - B is dark indigo-violet with a violet key, fill and grit, and red only in the crest lines and the travelling light.
  - H is smoky and low-contrast: a pearl key, a high ambient floor, soft crests.
  - J's colour lives in its view layer: an oil sheen that turns with each wall's angle to the camera.
  - E's view layer gives grazing copper-gold and bright glints, which read as metal without reflections.
  - I proves the geometry stands on its own.

**The view layer** (Fresnel, ramp, add; three ops) is shared by all ten palettes. Its three colours are palette roles that are black where a palette has none, so a transition fades it in and out.

**Transition logic (ADR-1420 selector).**
- **Phrase-driven.** On `beat.phrasePulse` (a phrase boundary), the target moves to the next palette of a composed order: A → C → F → B → G → H → E → D → I → J, each step a contrast of temperature and darkness.
- **Hold.** A change happens only if 24 s have passed since the last one, so palettes change at phrase scale (in practice every second or third phrase), never per frame.
- **Blend.** The blend runs straight in OKLab, from wherever the palette stands, over 6 s, eased with a smoothstep. It never passes through the palettes in between, and a retarget mid-blend starts from the mid-blend colours, so nothing jumps.
- **No hue rotation.** Every intermediate is a direct OKLab line between two authored palettes.
- **Manual control.** CC 9 selects a palette (the knob in ten steps) and CC 10 locks (above half way, the phrases stop changing it). Both are plain routes onto `palette/select` / `palette/lock`, so the timeline can key them too.
- **Seek = play.** With the track analysed, an offline render started mid-track replays the selector from zero. The transition clip starting at 30 s opens on Molten Glass, the palette a play from 0 reaches.

**Music within a palette (brief §3: intents, not channel mappings).** Every route moves a regional or localised term and never a hue:
- bass narrows the colour field's span, so the lobes' colours reach further (the extent of broad regions);
- held sound shifts the colour coordinate from the primary toward the secondary surface over seconds (the dominant surface shift);
- snares lift the medium folds' light;
- hats sharpen the glints;
- kicks flare the glow only in cells that already ring;
- transients launch the travelling light on the crests.

Silence keeps the slowly evolving figure (the focus sweeps and drifts) in a dark palette.

**Comparison mode.** `tools/cymatic_world/palette_sheet.py --time <t> --out <png> [--only <i>]` renders the ten palettes locked on the same frame: the same geometry, light, camera and exposure.

**Performance** (live 1080p, High fixed, Feline 100 s; palette off = the single palette B, on = the ten-palette system):

| | GPU median / p95 | CPU work median / p95 |
|---|---|---|
| palette off | 35.52 / 36.04 ms | 4.28 / 5.75 ms |
| palette on | 35.65 / 36.11 ms | 4.40 / 6.14 ms |

The palette costs nothing measurable on the GPU (the same 48 ops; the constants change, the work does not) and about 0.1-0.4 ms of CPU for 21 bindings and the selector. **Note:** High at this loud passage is now about 2 ms over the Stage A baseline (Stages C-D: a fourth bake grid and 9 more ops). Stage G owns bringing it back.

**Material:** 48 ops, the interpreter's cap. Two swizzles were folded (a scalar op reads its source's x), which paid for the view layer. **Fields:** 16 of 16.

**Found on the way:** a render logs `project references unknown parameter 'palette/select'` (and lock, hold, position and so on) on its second project load. The parameters work, as the transitions and the lock show. It looks like an ordering issue where the project's parameters are applied before the palette registers its own, and it is reported to the coordinator, not fixed here (shared files).

**Evidence** (`palettes/`):
- `all-10-palettes-first-pass-feline-60s.png` (the first pass) and `all-10-palettes-tuned-feline-60s.png` / `-125s.png`;
- `monochrome-silver-alone-feline-60s.png` / `-125s.png`;
- `phrase-transitions-molten-glacial-crimson-feline-30-90s.mp4` (normal speed, with audio; Molten → Glacial → toward Crimson on phrase boundaries);
- `manual-select-locked-chlorophyll-ultraviolet-silver-feline-40-80s.mp4` (locked; picks at 40, 52 and 66 s keyed on the timeline as a performer's CC would).

## Stage E: emission and optical finish (each effect on its own)

Each candidate was rendered alone against the baseline on two locked palettes: Abyssal at Feline 18 s (dim, cool) and Molten Glass at 125 s (hot highlights). Each was judged full-frame and in native-resolution crops.

| Candidate | Setting | Verdict |
|---|---|---|
| Bloom, tuned | intensity 0.30-0.32, threshold 0.9-1.0 (from 0.22 / 1.2) | **Rejected**: indistinguishable. The crests are thin lines, not sources, so bloom has little to spread; the existing tight bloom stays |
| Glare (anamorphic streaks) | intensity 0.18 and 0.4 | **Rejected**: no visible effect. Nothing in the frame is a point source bright enough to streak, and forcing it would be glare for its own sake |
| Halation | intensity 0.3 and 0.5 | **Rejected**: imperceptible on cool palettes and a faint warm haze on hot ones; adds nothing to the geometry |
| Chromatic aberration | 0.15 and 0.04 | **Rejected**: 0.04 is invisible, and 0.15 puts red/cyan fringes along the frame's bottom edge, which reads as a lens fault, not light |
| Iridescence | the palette view layer (Stage P) | **Kept**, restrained, and only where a palette calls for it: J's oil sheen, E's grazing metal |
| Vignette, motion blur, trails | | Stay off (brief) |

The image's interest stays where the brief puts it: geometry, shading and negative space. No new full-screen effect, and nothing added to the frame cost.

**Evidence** (`stageE-optics/`): `optics-candidates-abyssal-feline-18s.png`, `optics-candidates-molten-feline-125s.png`, and their `-crops-native.png`.

## Stage F: audio integration

**Each layer answers on its own time scale.** None of these routes touches a hue (the palette owns hues), and each moves a regional or localised term:

| Layer | Signal | Attack / decay | Where it shows |
|---|---|---|---|
| base field: extent of the broad regions | bass | 120 ms / 1.2 s | the colour field's span narrows, so lobes reach further |
| base field: dominant surface | held sound (sustain) | 2.5 s / 5 s | the colour coordinate leans from primary toward secondary |
| ridge light: large crest | snare | 0 / 260 ms | crest lines only |
| emission: hollow glow | sustain (×1.2), kick (×1.6) | 1.5 s / 4 s; 0 / 380 ms | only in cells that already ring, on their inner walls, in one lobe sign |
| optical: glints | hats | 0 / 180 ms | sheen lobes on the smooth stone |
| light: counter fill | spectral flux | 300 ms / 1.5 s | the far flanks |
| travelling light (Stage D) | each onset's ring | the band's attack; the ring's life, with displaced rings fading over 0.35 s | dashes along the crests near the hit |
| palette | phrase boundary | 24 s hold, 6 s blend | the whole palette, at phrase scale only |

- **No whole-screen kick flash.** Measured over the phrase-transition clip (Feline 30-90 s, 1800 frames) with `flash.py`:
  - the frame's mean luma moves a median 0.13 points per frame (p99 2.1, max 2.7);
  - the share of pixels changing by more than 15% per frame is a median 0.8% (p99 4.8%).
- **The largest steps are not flashes.** They recur about once a second and do not decay within 0.3 s. Their frame difference shows edge motion over the whole frame, not a brightness lift: it is the bass relief pump (brief 08, approved geometry), which the light pass leaves alone.
- **No band-to-RGB mapping.** Bands move extents, terms and intensities; the palette chooses the colours.
- **Persistent state under sustained energy.** Held sound shifts the surface colour and builds the glow over seconds and lets them settle back over five. The resonance memory keeps the figure (ADR-1400 / 1404), so a sustained passage holds a coherent, settled colour state instead of pumping.

**Evidence** (`stageF-audio/`): `largest-luma-step-is-the-relief-pump-before-after-diff-43.4s.png` and `flash.py` (the measurement).

## Stage G: performance and regression

**Why Auto held High at "6% misses": a controller blind spot, now fixed by the coordinator (ADR-1363, merged from main).**
- **The measurement.** Live profile, Auto, 30 fps, 1080p, Feline 95-135 s, 40 s measured with `--trace`. All 1126 frames ran at High with a GPU span at or over the period: median 34.8 ms, p95 36.0. Yet every presented interval was a steady ~35.5 ms (34-38.6 ms), never quantised onto a later vsync.
- **Why nothing counted.** ADR-1362 counts a frame as late only above period + max(period/4, refresh/2) = 41.7 ms. So nothing counted: 0 late, 0 main-thread late, and the backoff never engaged. The scene ran at ~28 fps at High indefinitely.
- **Not the CPU or the main thread.** CPU work median ~4.4 ms; the main thread sat in GPU waits.
- **Not the earlier figure.** The "6%" came from a 5 s default measure; it is not the mechanism.
- **The fix.** The coordinator's ADR-1363 also counts a paced frame as the GPU's miss when its interval is more than 2% past the period, the window's median span is more than 2% past the period, and its own span reached the period.

**Scene side: loud High brought back to budget.**

| Loud Feline (100 s), High fixed | GPU median / p95 |
|---|---|
| Stage A baseline (shape-language head) | ~33.3 ms (median) |
| after Stages C-D, palette (48 ops, prepass) | 35.65 / 36.11 ms |
| 43 ops, prepass on | 34.21 / 34.73 ms |
| **43 ops, prepass off (the scene now)** | **31.78 / 33.29 ms** |

- **Ops 48 → 43.**
  - Stage C's medium-crest light is off (4 ops, `CW_LINE2=1` restores it). That tier was the faintest; the medium relief still reads through the shading. **This is the trade:** the light hierarchy is now large crest > glints, with the medium carried by geometry.
  - A redundant stone multiply in the hollow glow is gone: its wall band is stone already.
- **The depth prepass is off.** ADR-1406's per-scene switch; `CW_PREPASS=1` restores it.
  - It won at the correction round (35.3 → 29.8 ms) because of the costly per-pixel program and the deep cells.
  - Re-measured now (43 ops, baked fields, the near-to-far mesh order doing the early rejection), it costs 5.8 ms and saves only 3.4 ms.
- **Palette on/off** is GPU-neutral (Stage P table).
- **A measurement gotcha.** A `--variant` build regenerates the example scene file with the build's environment, so variants built back to back share the last one's scene. One profile in this stage was mislabelled that way and re-run.

**Auto after ADR-1363 and the trims** (Feline 95-135 s, 40 s, `--trace` `autoG2.csv`):
- **Where it settles:** Auto holds High for 37 s, then drops to Medium at 38 s, when the stretched-frame rule sees the loudest passage's spans.
  - High (1038 frames): GPU median 32.3 ms, p95 33.3.
  - Medium (163 frames): GPU median 26.3 ms, p95 27.0.
- **Missed-frame rate:** 5 deadline misses in 1201 frames (0.4%).
- **Frame intervals:** a median of 33.3 ms at both levels. About a third of intervals land a little over 34 ms at either level, which is the cap's wake-up jitter: the same share at Medium, with 7 ms of GPU headroom.

**Regression:** see the checks at the end of this report.

## Stage H: final art pass

- **Every palette holds its character on the final program** (43 ops, no medium-crest light, no prepass):
  - darks are dark and the bright roles sit on the ridges;
  - A and F differ in temperature and darkness;
  - B, H and J are three different worlds;
  - I carries the form alone.
- **The palette journey was recomposed after validation.** The first order put complementary palettes next to each other: blue → orange, magenta → green, green → rose. A straight OKLab blend between opposite hues passes through grey, and in the validation clips green → rose read as silver mid-blend, the "muddy intermediate" the brief forbids. The order is now a temperature arc around the wheel with no neighbour across it: Abyssal → Chlorophyll → Glacial → Silver → Oxidized → Molten → Crimson → Rose → Oil → Ultraviolet → Abyssal. Silver sits where a grey midpoint is its own identity.
- **The layer views** (`CW_DEBUG=albedo`, `light`, `emission`; documented beside the shape-language views in `build.py`) show each shading layer alone on the real program and palette.

**Evidence** (`stageH-final/`): `all-10-palettes-final-feline-60s.png` and `layer-views-crimson-feline-18s-full-albedo-light-emission.png`. A transition clip in the final order is `palettes/phrase-transitions-final-order-feline-40-110s.mp4`.

## Validation (the ten scenarios, normal speed, with audio)

The scenarios were rendered from the shipped project, with palettes advancing on their own, into `validation/` (960×540, 30 fps). Clips 01-10b were rendered before the palette order was recomposed in Stage H; the final order's transitions are in `palettes/phrase-transitions-final-order-feline-40-110s.mp4`.

| Scenario | Clip | Observed |
|---|---|---|
| 1 near silence, 2 quiet → loud, 3 kick, 4 snare | `01-04-...-feline-0-24s` | Dark Abyssal, with the figure slowly evolving in silence. The crests brighten with snares, the glow flares only in ringing cells on kicks, and the travelling light runs along the crests from each hit. No whole-frame flash |
| 5 sustained bass + mid | `05-...-54-64s` | A held, settled colour field. A palette transition passes on a phrase boundary over 6 s, with no jump |
| 6 dense | `06-...-120-130s` | An intricate figure. The bright roles stay on the ridges, and the darks hold through the busiest passage |
| 7 high frequency | `07-...-154-164s` | Fine glints on the stone; no extra colour wash |
| 8 sustained mid, 9 loud → quiet | `08-09-...-186-208s` | Oxidized Metal: grazing copper-gold on the walls, settling dark as the track quietens |
| 10 second tracks | `10a-...-trench-50-60s`, `10b-...-hyperborea-110-120s` | The same hierarchy on other music |

## Checks (head `810fa734` plus this report)

- `tools/ci/run-suite.sh cpu-assets`: **PASS**, 4301 passed, 0 failed, 19 skipped, 1 expected failure (ran 4321/4321).
- `tools/ci/run-suite.sh gpu`: **PASS**, 665 passed, 0 failed, 1 skipped (ran 666/666).
- `tools/review_examples.py --only Cymatic`: **ok 2**. Sonic VFX - Cymatic Plate is ok and unaffected: the branch changes no Cymatic Plate file, and the engine changes are additive (ADR-1409's default is unchanged and unwritten). Cymatic World is ok.
- `tools/check_project_integrity.py`: **0 problems**.

**Known limitations:**
- The fine serrated edge on some large crests (the coordinator's earlier note) is the mesh carrying a sharp crest. It is geometry, which this pass leaves alone.
- The hollow glow is still soft-edged inside its wall band at some moments.
- The palette advance listens to `beat.phrasePulse`. Live, with no tempo, phrases may not fire, and the palette then holds until CC 9 / CC 10 or a tempo exists (ADR-1420's report on tempo-less live scenes applies).
- The material is at 43 of 48 ops and 16 of 16 GPU fields: there is little room left for another layer without removing one.

## Doc edits waiting for owner approval (not made)

- **02-report.md / PROGRESS.md:** the light pass and the ten palettes (this report). Mention the scene's palette controls (CC 9, CC 10) and the layer debug views.
- **ADR-1406:** a note that Cymatic World has since turned its depth prepass off, re-measured at the light pass (the switch is unchanged).
- **README / examples row:** the Cymatic World description could name the ten palettes and the CC 9 / CC 10 controls. The generator rewrites `examples/index.json` from `DESCRIPTION_TAIL` and the CC list, which already includes CC 9 / CC 10.
