# The Living Prism 12: the owner's decisions after the overnight run (pass 2)

## The owner's answers (verbatim, 2026-10-23, to 11 section 5)

> "1. Push towards iridescent glass 2. Change on their own with music 3. Sure 4. Sure 5. Best recommendation 6. Yes"

## What each means (as the coordinator relayed it)

| # | Question | Decision | What pass 2 does |
|---|---|---|---|
| 1 | The crystal's look | **Iridescent glass** | Clear, iridescent glass: refraction (ADR-1603), thin film and Fresnel, with emissive cores kept only as accents. Paid for within the frame (cheaper haze, fewer lights, LOD on small clusters) and costed with honest timestamps |
| 2 | Automatic look changes | **On their own, with the music** | Keep the phrase-gated changes. CC 9/10 still override and lock |
| 3 | The heaviest window | **150.99-170.82 s** | Adopted |
| 4 | Real intensity knobs | **Yes** | The engineering agent gives `control.*` a rest value on its branch. Once that lands, CC 1/2 become true attenuating knobs |
| 5 | The geode's cap and the ember's warmth | **"Best recommendation"** | Keep the cap: it tells how the geode broke open. The ember stays warm in the warm looks and shifts toward each cool look's accent role, so it remains the contrast note without breaking the look |
| 6 | A signature effect on the arch | **Yes** | Light travels along the arch on phrase builds and big impacts. The scene-level approach (a baked path distance in an instance lane, per the audit) is compared with a simpler version, and the better is kept |

## The coordinator's critique of the stage-6 stills (also acted on)

- **The crystals read as simple low-poly prisms with matte faces.** They need more facet complexity per hero
  (terminations, sub-crystals, chips) and real specular glitter. The jewel quality is the spec's core ask.
- **The roof and upper-wall crust read as floating debris against black.** Seat it in lit rock: rim-lit surfaces, the
  crust embedded.
- **Large near-black areas remain.** Add low, coloured fill on the rock (bounce from the crystals and the pool) without
  flattening the contrast.
- **The refraction is too subtle to read.** The glass direction should fix it.
- **Two tuning jobs:** retune the pool's brightness at the right-bank station, and tune `upscaleSharpness` against the
  pool's ripples.

## Process

- Commit per step.
- Media goes in `~/Desktop/av-gen-review/82-living-prism/pass2/`: the six stations, and the Feline typical and heaviest
  clips with audio.
- Perf on AC.
- The summary is `13-pass2-summary.md`. The owner reviews before the merge.
