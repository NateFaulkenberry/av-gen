# The Living Prism 11: the overnight summary (2026-10-22/23)

**Branch:** `scene/living-prism` on main `c7a211f5`. Each stage has its own commit. Not merged, not pushed.

**Media:** `~/Desktop/av-gen-review/82-living-prism/`, one folder per stage, each with the same six stations. Start
with `stage6/`:
- `feline-heaviest-window.mp4` and `feline-typical-window.mp4`, with audio;
- `palette-looks-with-music.jpg`;
- `still-1` to `still-6`.

**Build and play:**
- Build: `python3 examples/living-prism/build.py` (Blender, about 10 s; every mesh generated, no external assets).
- Live: `examples/living-prism/living-prism.json` (no audio file; stems on channels keyed `drums`, `bass`, `synth`, or
  a plain mix).
- Offline: `living-prism-offline.json` (Feline Footwear, analysed from its stems).

**Docs:** this file is `11` (the stage notes took 07-10).

## What was done, per stage

| Stage | Doc | In one line |
|---|---|---|
| 1 | 05 | Stems confirmed sample-aligned; windows chosen; visual direction from the 8 references; six palette looks |
| 2 | 06 | Blockout: a closed faceted grotto, a mirror pool, five heroes with distinct silhouettes, an arch, a 95 s route |
| 3 | 07 | Crystal and rock materials; a crust and lit cracks against dead space; the geode cracked open; the arch made a natural bridge; the haze kept; the pool cut to 10-20% of the frame; the palette bound |
| 4 | 08 | Nine conditioned signals (`signal.*`, stem first, mix fallback, no kick/snare/hat classes). The kick sends a front of light out from the spire; the clap lights the geode, the arch and the cracks; the hats glint; the bass moves the fan, the chandelier, the passage and the water; the synth colours and lights. Looks change only on a musical reason. CC 1/2/9/10 |
| 5 | 09 | Detail: the ember rebuilt as a terraced cluster, crystal motes on the synth, the crack regions moved |
| 6 | 10 | Lights and haze trimmed (-6.4 ms); refraction adopted on the spire and the fan; the windows profiled; the functional tests run |

## Performance (1080p30, AC, honest timestamps; 10 has the tables)

- **Live auto:** 100% of frames at High or Ultra in all three windows (9 runs). 1-3 misses per 601 frames. Frame p99
  39.5-40.4 ms, at the 40 ms line; the GPU p95 is at most 30.6 ms, so the p99 is presentation pacing.
- **Ultra GPU p50:** 29.3-29.7 ms. Emergency 14.6 ms.
- **Refraction:** 2.8-4.2 ms.
- **The pool's SSR:** 0.2-1.0 ms.
- **The haze:** 5.0 ms.
- **The scene pass:** about 22 ms, of which the four material programs are 3.1 ms (measured A/B).

## Known weaknesses

1. **The crystals.** Better than pastel plastic, with real facets, cores, glints and now refraction on the two heroes.
   The many small druses are still opaque and a little uniform in hue within one look.
2. **The pool on the new main** (water and refraction in one phase, ADR-1603) reflects less brightly at the right-bank
   station than before. It needs a short retune (`ssrGain`, `reflection`) in the next art pass.
3. **The frame p99 sits at 40 ms:** there is no headroom for more effects at Ultra. The ladder will step to High
   when it must, and High keeps the look. A next cut would be the rock program (it covers most of the frame) or
   fewer, smaller lights.
4. **Fronts restart:** one front per kick, so a new kick restarts the front rather than overlapping it.
5. **The roof's darkest areas** still carry little form at some stations, where the crust is the only structure.
6. **The CCs are untested with hardware.**
7. **The upscaler's sharpening** (ADR-1607) is untuned against the pool's ripples at low scales.

## Open decisions for the owner

1. **The look of the crystal:** keep the stylised, flat-faceted, emissive-cored language, or push toward the
   references' clear, iridescent glass (more refraction, at its cost)?
2. **Automatic look changes:** on a musical reason (at most every 45 s), or MIDI only?
3. **The heaviest window** is now 150.99-170.82 s (it measured costlier than 123.22 s). Agree?
4. **CC 1/2 are boosts** (0 = authored, up to 2x), because a control channel reads 0 until moved. Want true
   intensity knobs? That needs a rest value for control channels: a small engine change. Soft takeover (ADR-1606)
   does not help here; it applies to parameter targets only.
5. **The geode's cap** lying beside it, and **the ember's** warm orange in the cool looks: keep?
6. **The arch:** the bridge reads natural at the oblique stations. Is it memorable enough as the focal structure, or
   should the next pass give it a signature (a crack of light along it: the spec's 8.2 structure-following
   propagation)?

## Still waiting on engineering

- **P1-3:** sound-to-light latency (the spec's p50/p95 report).
- **P1-7:** the committed perf baseline to compare against.
