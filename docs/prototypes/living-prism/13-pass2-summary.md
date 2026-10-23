# The Living Prism 13: the pass-2 summary (the owner's decisions of 12)

**Branch:** `scene/living-prism` on main `479a2277`. Not merged; the owner reviews first.

**Media:** `~/Desktop/av-gen-review/82-living-prism/pass2/`:
- the six stations;
- `before-after.jpg` (stage 6 against pass 2);
- `closeup-glass-spire.jpg`;
- `feline-typical-window.mp4` (65.70-85.53 s) and `feline-heaviest-window.mp4` (150.99-170.82 s), with audio;
- the arch comparison: `arch-travel-71s.mp4`, `arch-flash-71s.mp4` and `arch-travel-vs-flash-frames.jpg`;
- `perf.txt` and `sound-to-light.txt`.

## The owner's decisions

| # | Decision | What was done |
|---|---|---|
| 1 | Iridescent glass | **Refraction (ADR-1603) on three heroes:** spire, fan and chandelier, mostly clear (opacity 0.28-0.3) with light, near-neutral absorption, so the palette's share colours them.<br><br>**New crystal programs:**<br>- an iridescent rim: the pale tip colour turned round the OKLCH hue wheel by view angle and facet, on grazing facets;<br>- glitter: a view-dependent noise over normal and view, so facets flash as the camera moves;<br>- emissive cores as accents only (about a third of the facets);<br>- crisper roughness (0.02-0.12).<br><br>The heroes keep their thin-film sheen.<br><br>**Paid for:**<br>- haze from 8 to 6 steps and 30 to 26 m (5.0 → 3.9 ms);<br>- the back wash light removed: the rock's new bounce term replaces it. |
| 2 | Looks change with the music | Kept: on phrase boundaries where the music's character moves, at most every 45 s. CC 9/10 override and lock |
| 3 | Heaviest window 150.99-170.82 s | Adopted in every measurement and clip |
| 4 | Real intensity knobs | **Waiting** on the engineering agent's rest value for `control.*`. CC 1/2 are still boosts (at rest, the authored strength) |
| 5 | Best recommendation | **Kept the geode's fallen cap.** The ember is already bound to each look's `accent` role: coral-amber in Ultraviolet, gold in Solar Mineral, pale lavender in Glacier, cyan in Abyssal. So it is warm in the warm looks and the look's own contrast note in the cool ones. No change was needed |
| 6 | A signature effect on the arch | The arch's crust is now one small druse instanced 84 times along the underside. Its placements are ordered by their distance along the span (baked in Blender, `arch_crust.json`), so `instanceIndex` is the path distance.<br><br>A band of light with a trail runs from the left foot over the crown to the right foot in 1.25 s, on each phrase boundary, as strong as the music has built (`signal.arc`, `section` as depth).<br><br>**Compared with the simpler version** (the whole crust flashing at once, `arch-flash-71s.mp4`): the run reads as light travelling along the structure, with an origin and a direction, mirrored in the pool. The flash reads as a global blink. **The run is kept.** |

## The coordinator's critique

| Point | Change |
|---|---|
| **Simple prisms, matte faces** | A `gem` builder replaces the plain prism on every hero and druse:<br>- a two-tier termination (shoulder, then apex): 12 tip facets instead of 6;<br>- edge chips cut by planes, each a new facet;<br>- sub-crystals, stout and leaning 20-35°, seated in their parent (the spire went from 220 to 1,136 triangles).<br><br>Glitter and the iridescent rim give the specular sparkle |
| **Crust floating against black** | The rock program has a rim light on silhouettes and a coloured bounce on the low rock (`rimColor`, `bounce`, both palette-bound). The crust is seated deeper (embed 0.6). The upper frame now reads as lit rock with crust in it |
| **Near-black areas** | The same rim and bounce, as emission, cost about 10 ops (no lights). The fill is held to a few percent so the contrast survives |
| **The refraction too subtle** | Three heroes at low opacity: the passage and the rock read through the spire's large prisms (`closeup-glass-spire.jpg`) |
| **The pool at the right bank** | `ssrGain` 1.5 → 2.2, `reflection` 1.0 → 1.4, `fresnel` 0.16 → 0.2: the spire's reflection reads again at station 6 |
| **`upscaleSharpness`** | **Not tunable from a scene:** it is a `QualitySettings` field that no project parameter or live override reaches. The smallest extension is a `live.overrides` key (precedent `lodBias`) or a `post/...` parameter |

## Performance (AC, honest timestamps, 1080p30; records in `perf/pass2-*`)

| Run | GPU p50 / p95 (ms) | Ladder | Misses /601 | Frame p99 (ms) |
|---|---|---|---|---|
| Typical, live auto (x3) | 28.7-29.1 / 29.8-30.0 | 100% High/Ultra | 1, 1, 1 | 36.1-36.8 |
| **Heaviest (150.99 s)**, live auto (x3) | 28.8-28.9 / 30.0 | 100% High/Ultra | 1, 1, 3 | 36.7-39.8 |
| Heaviest, headless Ultra | 28.7 / 30.1 | — | — | — |
| Heaviest, Low / Emergency | 14.0 / 13.5 | — | — | — |

**Against the thresholds:** every one is now met.

| Threshold | Result |
|---|---|
| T1 (at most 5 misses per 900) | 1.5-4.5 per 900 |
| T2 (p99 at most 40 ms) | 36.1-39.8 ms |
| T3 (Medium or better at least 90%) | 100% |
| T5 (fixed cost at Emergency at most 14.7 ms) | 13.5 ms |

**The effects, measured independently** (headless Ultra, GPU p50):

| Effect | Cost |
|---|---|
| Refraction on three heroes | 3.0 ms (typical), 2.9 ms (heaviest) |
| The pool's SSR | 0.9 / 0.6 ms |
| The haze | 3.9 ms |

**Sound to light** (ADR-1605's loopback, live, 80/80 transients; `perf/pass2-sound-to-light.txt`). The drum stem's path
through a signal to a route:

| Measure | p50 (ms) | p95 (ms) |
|---|---|---|
| To the GPU's end | 64 | 78 |
| Estimated to light | 72 | 86 |

- **On the mix's onset:** 53 / 70 ms to the GPU's end (61 / 79 to light).
- **Against the 45/60 target:** both miss. The measurement and its shortfall are engine-wide (ADR-1605's title says
  the target is missed). The scene's own path is a single hop: band, signal, route, parameter, on the same frame.

**Functional tests, rerun** (`perf/pass2-functional.txt`):
- Silence: every signal is 0, the arch's run included.
- A missing drum stem or stereo only: the fallbacks work.
- The drums stopping: nothing latches.

## Known weaknesses

- **The crystals' bodies:** the big flat faces of the largest prisms still shade as one tone at some angles. Glass
  reads at the edges, in the glitter and in what shows through; a facing face can still look solid.
- **Ultraviolet** is now quite purple overall (the rock fill takes the look's colour). The other looks are less so.
- **The upper frame in the dark looks** (Abyssal, at stations 5-6) is still mostly navy with crust; better than before
  but sparse.
- **A thin cyan-green fringe** sometimes runs along a spire edge (the iridescent hue turn reaching green). It is in
  character for thin film, but the owner may want it narrower.
- **CC 1/2 knobs:** wait on the rest-value change.
- **`upscaleSharpness`:** the scene can't reach it (above).

## For the owner

1. Is the glass far enough? A further step is lower opacity or refraction on the druse clusters, at about 1 ms each
   group.
2. Keep the arch's run on every phrase, or only on the bigger builds (raise its depth floor)?
