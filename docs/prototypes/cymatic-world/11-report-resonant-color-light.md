# Cymatic World: resonant colour, lighting and optics (report)

The brief is [10-brief-resonant-color-light-optics.md](10-brief-resonant-color-light-optics.md). It builds on the shape-language work ([09](09-report-resonant-shape-language.md), head 74edc063). The work is on `proto/cymatic-split`, one commit per stage ("Cymatic World light pass, Stage X: ...").

**Audio.** Feline Footwear (`assets/audio/feline-footwear.wav`), fed with `avgen --audio`. The scene carries no track (ADR-1408). Scenario 10 also uses Trench and Hyperborea.

**Media.** `~/Desktop/av-gen-review/58-cymatic-world/light-pass/`, one folder per stage.

**Performance target.** Live 1080p, 30 fps, Auto.

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
