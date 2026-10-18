# Resonant Visual Worlds: report (the overhaul)

- **Brief:** [03-brief-resonant-visual-worlds.md](03-brief-resonant-visual-worlds.md).
- **Research:** [04-research.md](04-research.md).
- **Debate and plan:** [05-plan.md](05-plan.md).
- **Branch:** `art/resonator-quality`, after the quality pass ([01-report.md](01-report.md)).
- **Media:** `~/Desktop/av-gen-review/64-resonator-quality/overhaul/` (`proofs/`, `<scene>/stage-A/`, `stage-B/`,
  `stage-C/`, `stage-D/`, `stage-E/`).

## What changed, per scene

### Cymatic World: a resonant pattern world, not one crater

- **Geometry (the main change).**
  - **Why it was always radial.** Every large and medium source was a `point` source (`R_m(kr) cos m theta`, the
    circular family) about centres within 10 m of each other.
  - **Four families now share the large scale,** each a source of one resonator:
    - **P, plate:** square-plate Chladni figures through a new engine geometry, ADR-1471 (`kind: plate`,
      `plates: [[3,2],[5,1],[4,3],[6,1],[5,2]]`). It draws crossing nodal networks, loops and star nodes, and its
      focus crossfades neighbouring (n, m) into compound figures.
    - **H, hexagonal:** a lattice of order 3, which draws craters.
    - **R, radial:** the old figure, now one family of four.
    - **A, arcs:** low-order point modes from a source 150 m off-frame, which draw long sweeping ridges.
  - **A family director** (four sine LFOs at 97, 131, 151 and 173 s, pure functions of the clock, so seek-safe)
    decides which family leads, so the topology changes over phrases:
    - bass lifts the plate;
    - roughness brings in the arcs;
    - brightness moves the plate's focus.
  - **The medium and fine scales** moved to a second resonator, because a resonator holds four sources. The medium
    is an irregular family (far-off point modes) at low relief, never a regular grid.
- **Relief language** (the stage A review): the sand ridges are thin, over shallow signed lobes, with no antinode
  pits. The onset ripples' rings drew circles over every family, so they now live only in the light (the
  travelling light), not the relief.
- **Far edge.**
  - The serration at the top of the frame had two causes: the terraces and outcrops in frame, and the figure
    running onto coarse cells.
  - The amphitheatre is now ×1.35 wider and the floor's outcrops are gone.
  - The figure's readers fade between 55 and 95 m.
- **Composition.**
  - An oblique camera (78 m out, 44 m up, 26 mm, about 30° down).
  - The plate fills the lower two thirds, and the basin's terraces, rim and ranges sit above it.
  - A palette-bound sky (zenith, horizon, ground and a soft glow) and coloured fog: aerial perspective instead of
    black.
- **Colour and material.**
  - The OKLab gradient (ADR-1470) is driven harder (tint gain 1.6), so each palette shows its whole multi-hue range.
  - The nodal lines take an accent hue (a ridge shift toward the gradient's end), not a white outline.
  - The key is brighter, and Abyssal gets a stronger fuchsia.
  - The colour holds with the sheen off (stage C).
- **Ops:** the ground program is 41.

### Resonant Silk: a layered material world

- **Composition.**
  - Two long far drapes (about 700 m of cloth each, 1k vertices, a plain role-bound material, noise drift only) behind
    the gather, one rising above it and one sweeping below. The hero's wall is the foreground, the gather and sail
    the middle ground, the drapes the background.
  - Analytic fog (no volumetric scatter, which had washed the frame out) in the palette's environment colour.
  - A palette sky (horizon in the shadow role, lifted; ground the shadow role) and a soft sky glow in the resonant
    hue behind the gather. Measured renders show no black.
- **Material.**
  - Reflection-only bands (ADR-1151, a soft box and one strip) for the satin's soft banded highlights.
  - The key at 0.7×, roughness +0.1, metallic 0.05.
  - A painted sheen: the fold colour × Fresnel² as emission, the back-scatter at folds.
  - The body's chroma is up (2.0× the roles', capped at 0.26), and the fill is the fold colour lifted only 20%
    toward white.

### Waveform Dimension: a world of travelling waves

- **Primary event: travelling fronts.** Two families sweep through all four sheets:
  - a bass swell, the low band's history laid out as spherical crests from a source just outside the sculpture,
    × a 52 m carrier at 30 m/s;
  - a second, mid-band front was built and then cut in stage E for the live budget (about 6 ms at High each).
  - The swell keeps a few smoothing taps: with none, the history's row jitter crumpled the sheets.
  - **The GPU audio ring holds 16 s,** so the sources sit within about 300 m (a source 400 m out read history the
    ring no longer had).
- **Environment.**
  - A dome (330 m, 4k vertices, unlit, 8 ops) runs long chromatic bands in the palette's own four hues, dimmed.
  - The bass fronts sweep across it, and its phase drifts slower than the sheets' waves.
  - Thinner fog (0.0028), so the dome reads as distance, never as a flat card.
- **Material.**
  - Narrow, cool reflection bands, distinct from Silk's soft satin box.
  - Roughness from the field: glossy where the families reinforce, broad elsewhere.
  - The chromatic waves from the quality pass, unchanged.

## Shared vs scene-specific

- **Shared (engine):**
  - ADR-1470 `oklabRamp` (quality pass);
  - ADR-1471 `plate` resonance geometry. It is additive: existing sources are byte-identical, and four scenes render
    md5-identically before and after.
- **Shared (tools):** the OKLab helpers in `tools/sonic_vfx/kit.py`.
- **Scene-specific:** everything else lives in each generator.
  - Cymatic World: families, director, camera, basin, readers' fade.
  - Silk: drapes, sky and fog bindings, bands, sheen.
  - Waveform Dimension: fronts, dome, bands, roughness.

## Audio and temporal behaviour

- **Kept:** every existing route, MIDI map, palette, CC 9 / CC 10, the ADR-1420 selector and the ADR-1421 fallback.
- **Added:** the director's LFOs and the families' music routes (Cymatic World); the fronts and the dome's phase
  route (Waveform Dimension).
- **Stage D evidence:**
  - `stage-D/cw-journey-feline-5-185s-every-20s.png`: the figure and the palette both change over the track.
  - One 10 s clip per scene, `stage-D/<scene>-feline-95-105s.mp4`.

## What iteration revealed and changed

The review loop changed these, in order:
1. **Stage A, Cymatic World:**
   - The first render had the right families but read as blobs.
   - A top-down render against the nodal field showed the field was right and the relief and terrain were wrong.
   - Fixed with thin ridges, no pits, ripples out of the relief, and a wider basin.
   - A resonator's 4-source cap forced a second resonator.
2. **Stage A, Waveform Dimension:** the fronts read as a constant. The cause was the 16 s audio ring, so the
   sources moved nearer.
3. **Stage A/B, Silk:** volumetric fog and sky glow washed the frame milky. Analytic fog and a smaller glow fixed it.
4. **Stage C:**
   - Silk read as chrome or latex, so bands, roughness and the key were softened.
   - The coordinator then found it milky, so the chroma went up and the fill was lifted less.
   - Cymatic World was monochrome navy, so the tint gain went up, the ridges got an accent hue, and the key got
     brighter.

## Performance and checks

All runs are live 1080p (`avgen --live-profile --mode live --target-fps 30 --size 1920x1080`), Feline from 95 s, through
the GPU lock.
- **The machine was heavily loaded all day** by other agents' suites and the owner's apps. The quality-pass head's
  own numbers drifted by up to 50% (Silk at High: 35.7 ms yesterday, 53 ms today).
- So only back-to-back pairs against the quality-pass head (`623900f8`) are compared, and Auto (the live target) is
  what decides.

| Scene | Quality-pass head, Auto | Overhaul, Auto | Overhaul vs head at fixed High (GPU p50) |
|---|---|---|---|
| Cymatic World | High/Medium earlier in the day (frame p50 33.3) | **Low, frame p50 33.3 ms, 0.3% misses** | 38.0 → 43.3 ms |
| Waveform Dimension | High, 33.3 ms, 1.5% misses | **Low, frame p50 33.3 ms, 0 misses** | 33.0 → 39.2 ms |
| Resonant Silk | Emergency, 33.3 ms, 3% misses | **Emergency, frame p50 35.1 ms, 26% misses** | 53.3 → 50.9 ms (High); 32.5 → 33.6 ms (Emergency) |

**Cymatic World and Waveform Dimension hold 30 fps on Auto,** one or two levels lower than before.

**Resonant Silk does not hold 30 fps today.** Neither does its quality-pass head, which sat at the edge (3–9%
misses) under the same load.
- The overhaul adds about 1–2.5 ms at Emergency, where the scene is vertex-bound: the hero's 52k vertices × 8
  deformers, plus the two drapes.
- Measured A/B at Emergency: the drapes cost about 2.6 ms before they were lightened (now 1k vertices each, noise
  drift only); the reflection bands cost about 0.6 ms.
- On an unloaded machine Silk held High at about 36 ms yesterday, so it should hold Auto there. On this machine
  today it needs a re-check before the merge.

Where the overhaul's cost went, and what was done about it:
- **Waveform Dimension:** the fronts' spectrum read per vertex (12 smoothing taps × 14 bins, in both geometry passes
  and at four points for the normal) cost about 35 ms at High.
  - Fixed with narrow bands, 5 taps and one front, at 39 ms.
  - The dome costs little.
- **Cymatic World:** the oblique view sees ridges behind ridges. The depth prepass, off since the light pass, wins
  again (53 → 43 ms at High). The rest is more ground and the second resonator's bake.
- **Silk:** the drapes took the hero's 37-op program and are now on a plain material.

Run once at the end on head `7aaad85f` (the scenes as shipped). Each process's own exit code:
- `tools/ci/run-suite.sh cpu-assets` (under the GPU lock): **PASS**, exit 0. 4310 passed, 0 failed, 19 skipped,
  1 expected failure; ran 4330/4330.
- `tools/ci/run-suite.sh gpu`: **PASS**, exit 0. 667 passed, 0 failed, 1 skipped; ran 668/668. This includes the
  new `[adr1471]` plate parity case.
- `tools/review_examples.py --only` for Cymatic World, Waveform Dimension, Resonant Silk and Cymatic Plate: `ok 1`
  each, exit 0.
- `tools/check_project_integrity.py`: **0 problems**, exit 0.

## Remaining shortcomings

- **Silk:** the gap below the sail is a coloured gradient, but still a fairly plain one. The far drapes have no
  parallax beyond the camera drift.
- **Waveform Dimension:** the dome has no parallax by construction. E2, the far shells, was not built: the proof
  showed it cluttered the frame and cost 600k vertices.
- **Cymatic World:** a faint lip remains where the figure fades into the basin at some arc positions, and the far
  terraces are plain.
- **Interactive review:** stills and short clips only, at the owner's request ("no more clips"). A sustained-play
  review of the director's journey is the owner's to make.
