# Digital Ocean, Pass 3: the anime water (report)

*The art agent's report on the owner's brief `06-brief-anime-water-shader.md`. Branch `art/digital-ocean-2`, after
Art Pass 2. The full log, with every wrong turn, is the "Pass 3 (water)" section of `PROGRESS.md`. Media is in
`~/Desktop/av-gen-review/55-digital-ocean-water/`.*

## What changed

The water is now an illustrated surface rather than a toon filter. The renderer is unchanged: the water is a
material program on the displaced sea mesh (`water_program` in `tools/sonic_vfx/scenes/digital_ocean.py`), 47 ops
near and 39 far, out of the engine's 48. Nothing in the water posterizes a colour. Every band, highlight, foam cap
and spark is a cut of one scalar computed from the wave geometry.

| | Pass 2 | Pass 3 |
|---|---|---|
| Body | reflected-sky colours, `quantize`d into bands | four soft colour regions (deep, main, lit faces, crest accent) drawn by the swell's height and the faces' orientation |
| Highlights | broad crest light, a white slab under bass | thin strokes just under each crest, which ride the waves because they are cut from the crest |
| Foam | caps everywhere at high energy | broken caps on the reinforced crests only; a tall sea raises the cut, so foam stays a line along a breaker |
| Sun path | a smooth path with crisp glints | the reflection test broken by a streaked noise into horizontal sparks; far away, one smooth path |
| Anti-aliasing | none | a footprint-driven contrast gain on every edge |
| Distance | a hard seam between the near and far programs | one shared Fresnel blend into the horizon colour; the far sea converges on its middle tone |
| Controls | waves, energy, water colour | plus speed, direction, band contrast, highlights, foam and detail (CC 9-14) |

## The systems

- **The illustration's light `t`.** The displaced height over a 2.4 m span (crests +, troughs -), mixed 35 % with
  the face's orientation. Then 7 % of a detail noise of the rest position, stretched 3.3x across the view so it
  reads as wind streaks, not blotches. The noise samples the rest position, which the swell's orbits carry, so the
  detail rides the water instead of sliding over it.
- **Anti-aliasing (derivative-based).** The engine's per-pixel `footprint` (world metres per pixel, from screen
  derivatives) becomes a gain g that falls from 1 to 0.35 as a pixel covers more water. `t` is multiplied by g
  before any edge, so no edge is sharper than its pixels and the far bands converge on the middle tone. The detail
  simplifies toward the horizon by itself. Foam is multiplied by g too. Far glints jitter by the centred noise
  times g, so they merge into one path rather than flicker. Measured: the far band's frame-to-frame second
  difference averages 1.0 grey level.
- **Colour regions.** Three smoothstep edges on `t g`, with half-width `bandSoft` (the band contrast).
- **Strokes.** A narrow band of `t` just under the crest, in the palette's `hl`.
- **Foam.** `mix(noise, t g, 0.5) g`, cut at 0.37.
- **Glints.** The reflected ray against the sun, `r.l/2 = (n.v)(n.l) - v.l/2`, jittered by 6 % of the raw noise. A
  perfect reflection sparks only where the noise is above 0.62, so a big face turned to the sun stays broken.
- **Horizon.** Fresnel to the 7th power into the reflected horizon colour, in both programs, so the pieces meet
  without a seam.
- **Interaction foam** (Pass E):
  - a new surf line round the island: a low ribbon that rides the swell, masked by a 25 m noise the swell carries
    up and down;
  - the yacht's V-wake retuned to the new foam;
  - larger dolphin splashes.

  Whatever moves the swell now moves what floats on it. `wave_route` sends each swell route to the sea pieces, the
  yacht's parts, its wake and the surf line.

## Palettes

Every palette names the water's colours whole: `deep`, `wmain`, `sss` (lit faces), `wcrest`, `hl` (strokes),
`foam`, `glint` and `rhor` (horizon).

- **Violet / indigo sunset**, the primary: indigo troughs, violet body, lavender lit faces, orchid accent, pink
  strokes.
- **Turquoise / cobalt afternoon**: cobalt body, turquoise faces, aqua accent, cream foam.
- **Deep electric blue**: ultramarine body, cyan faces and accent, white glitter.
- **Midnight**: the bioluminescent night, with teal crests.
- **Twilight**: the daybreak pad's path.

The palette blends smoothly on one axis, `visual.mood` = CC 1 + 0.35 x the piece's 12 s intensity + the daybreak
envelope, smoothed over 1.5 s.

## Controls (MIDI, any device and channel)

| CC | Control | What it does |
|---|---|---|
| 1 | palette | violet, then afternoon, then electric |
| 3 | waves | the swell's height. The yacht and the surf line rise with it, and the foam cut rises so caps stay caps |
| 4 | wave energy | chop, the tide's depth and livelier foam |
| 7 | water colour | the body's and lit faces' richness |
| 9 | wave speed | from the authored pace to 2.5x, without a crest jumping (an integrated phase, ADR-1372) |
| 10 | wave direction | the cross swell from the right gives way to the wind sea from the left (amplitudes cross-fade; nothing sweeps sideways) |
| 11 | band contrast | from soft painted bands to crisp cel shading |
| 12 | highlights | the strokes brighten (1.5x) and widen |
| 13 | foam | more and brighter caps |
| 14 | surface detail | streaks across the bands (detail share 0.07 to 0.16) |
| pad 37 | hero wave | a 6 m-wide, 2.3 m pulse rolls in from 240 m and foams along its crest for its approach |

CCs 2, 5, 6 and 8 and pads 36 and 38-41 are unchanged from Pass 2.

## Audio mappings

No new analysis path, and nothing sampled in the shader. The routes read the response signals the scene already
used:

| Signal | Water response |
|---|---|
| level | the long swell grows; the waves quicken (+35 % at full, integrated); the crest span widens and the foam cut and crest-accent edge rise, so a loud sea keeps thin accents and caps |
| intensity | the palette mood; the band edges tighten a little |
| bass | the tide field rolls the bass's history in as swell; the body deepens, the lit faces lift (gentler than Pass 2) |
| kick | the onset front rolls in; the lit faces and foam flash briefly |
| hats / highs | the glints and the crest strokes flare; a touch of mint on the lit faces |
| mids | the wind sea roughens |
| sustain | the halo and the sky; the water is untouched (its old sun-path colour is gone) |

With no audio, the swell rolls, the sun path sparkles, the yacht crosses and the pods leap. Every look is set by
its default parameters.

## Engine change

**ADR-1372: a sine deformer's phase is unbounded and wrapped.** The `phase` parameter's hard range was +-100
radians. An integrated route (ADR-1041) driving a wave's speed reached that within minutes and froze the wave. The
range is now +-1e6, and `applyProceduralParameters` wraps the final to (-pi, pi] in double, so the f32 sine keeps
its precision. A unit test covers it. A `clock` signal source was written first and dropped, because the route
chain's `integrate` stage already does this job.

## Performance (M2 Max, 30 fps = 33.3 ms)

| Case | Result |
|---|---|
| Headless 1080p Ultra, silent | GPU 26.5 ms p50 (Pass 2: 27.1). The new water is 0.7 ms cheaper than Pass 2's |
| Live path 1080p, auto (High, 0.85 scale, 120 Hz display) | interval median 33.4 ms (30 fps held), GPU 29.6 ms, 3.3 % of frames miss one 8.3 ms vsync, status "at risk" |
| Live path pinned at Medium (0.71) | GPU 25.3 ms, the same 3.1 % misses |
| MIDI stress, 902 messages in 83 s | GPU p50 26.6-30.8 ms in every window, nothing accumulates, 0 GPU errors |

The feature costs by elimination are in PROGRESS (Pass G). The sea is 16.7 ms of the frame: about 8 ms program and
3 ms deformers. Strokes and foam cost 1.05 ms, glints 1.38, detail 0.92, rays 0.85 and clouds 2.36.

The occasional missed vsync is not the GPU. It persists at Medium with 4 ms less GPU time, and removing routes does
not move it. The live CPU breakdown puts "UI build and record" at 8.7-9.8 ms a frame (the editor). Pass 2's live
run had 2.2 ms of CPU and no misses, but on a 60 Hz display and before ADR-1362. This is a finding for the
live-controller and editor owners.

## Limitations

- **Missed vsyncs.** The live path's ~3 % single-vsync misses (above) come from editor CPU time, not the scene's GPU.
- **Grey-lavender water at intermediate moods.** Under quiet music the mood can rest between palettes, and the water
  drifts grey-lavender: two half-blended palettes plus the horizon blend's two colours. This is the same family as
  Pass 2's pale-sky note.
- **The near long-swell crest** at some moments is a large lit band broken by the detail noise. It is legible, but
  busier than the rest.
- **Integrated routes and seeks.** The routes on live input (speed, and the level's quickening) are not replayed by
  a seek (ADR-901), so a seek drops their accumulated phase. Offline, a scored timeline is replayed exactly.
- **Program budget.** The near program uses 47 of 48 ops, so one more water feature means removing one.

## Checks

- Integrity check: 0 problems.
- `cpu-assets`: exit 0, 4251 passed. The first run failed main's live-switch carryover test, a load-sensitive flake
  that passes alone and on the rerun.
- `gpu`: exit 0, 658 passed.
- `review_examples` on Digital Ocean: ok.
