# The Living Prism 07: Stage 3, materials, lighting and palette looks

**Media:** `~/Desktop/av-gen-review/82-living-prism/stage3/`:
- `still-1` to `still-6`: the same six stations as Stage 2;
- `before-after.jpg`: Stage 2 against Stage 3, station by station;
- `palette-looks-master.jpg`: all six looks at the master station;
- `route-0-40s.mp4`.

## The coordinator's six notes, and what changed

| Note | Change |
|---|---|
| 1. Crystal reads as pastel plastic | **Two crystal programs** (`lp_hero` for the spire, fan and ember; `lp_xtal` for everything else):<br>- a dark saturated body that varies per facet (`quantize` on the normal gives each flat facet its own random);<br>- an emissive core, strongest low and facing the viewer, which strongly differs per facet: some facets are dark, some lit from inside;<br>- a pale fresnel rim and luminous terminations (hero);<br>- about 8% of facets as bright mirror facets;<br>- roughness per facet (0.03-0.18).<br><br>The heroes also carry a faint thin-film sheen. The node's role colour comes in as `materialEmission`, so one program serves every formation and the palette recolours all of them |
| 2. Dead space (roof, upper walls) | Crystal crust: 28 druse patches on the roof and upper walls (`crust_xtal`, 10.6k tris). Sparse luminous mineral cracks in the rock (`lp_rock`: voronoi edges, broken into segments, inside a low-frequency mask covering about a tenth of the rock). Two broad wash lights under the roof and over the back. The haze (kept, see below) lifts the far walls |
| 3. The geode reads as a disc | Rebuilt as an egg of rock broken open along a jagged line and tilted toward the viewer. Its wall shows a chalcedony band; its cavity is lined with 520 placements that grow larger toward the bottom, lit from inside. The broken-off cap lies beside it with its own small druse |
| 4. The arch reads as a portal | Rebuilt as a natural bridge: 46 rock lumps along an asymmetric span, fused, weathered at two scales and faceted. Flared, sunk feet, a deck deeper than it is tall, and patches of crust underneath |
| 5. The haze costs 13.8 ms | Steps 24 to 12 and reach 40 to 30 m: **8.0 ms** at Ultra (honest timestamps, P0-1). **Kept:** the A/B stills show that without it the space behind the spire and the far walls fall back to black, the dead space of note 2, and the arch loses its depth. The live ladder scales it further |
| 6. The pool's share | Measured by ray-casting the baked route (`scripts/pool_coverage.py`, an upper bound that ignores occluders). It was p50 21.9% and max 35.5%. Changes: the pool narrowed from 5.5 to 4.4 width and its path shortened, the front stations moved forward, and station 6 now looks down less steeply. **Now p50 16.2% and max 20.7%**, inside the spec's 10-20% at the median |

**The palette:**
- **Looks:** six, now real. Fourteen roles, including a new `deep` role for the passage's light, bind:
  - the rock tones and the seam colour (program constants);
  - every crystal node's emissive colour;
  - the ten lights;
  - the fog;
  - the pool's body colours.
- **MIDI:** CC 9 selects a look and CC 10 locks it (routes and bindings in the project; tested with a controller in
  Stage 6).
- **Changes:** a look changes in 6 s through OKLab. There is no automatic cycling.

**Refraction (P1-4):**
- The spire and the fan have their `refraction` blocks authored (`build_scene.py` `REFRACT`).
- They are written only with `LP_REFRACTION=1`, because main's parser does not know the key yet.
- When P1-4 merges, flip the default and measure.

## Perf (AC, honest timestamps; heaviest window 123.22 s, typical 65.7 s; 1080p30)

| Run | GPU p50 / p95 | Frame p99 | Ladder | Misses / 601 |
|---|---|---|---|---|
| Heaviest, live auto | 29.0 / 29.5 | 37.0 | Ultra 423, High 177 | 2 |
| Typical, live auto | 31.0 / 31.6 | 37.0 | Ultra 600 | 1 |
| Heaviest, headless Ultra | 29.2 / 30.6 | 38.1 | — | — |
| Heaviest, headless Ultra, no haze | 21.0 / 22.4 | 24.7 | — | — |

**Where the time goes:**
- **Scene pass: 18.5-20.8 ms** (Stage 2: 12.5).
  - The programs measure **3.07 ms** by the profiler's own A/B (`--verify-candidates`, outside the noise).
  - The rest of the growth is the four added lights.
- **Haze:** 8.0 ms.

**Against the thresholds:** T1-T3 are met. The headroom at Ultra is thin, so Stage 6 trims the rock program and the
lights before the effects of Stages 4-5 add load.

## Known weaknesses (to Stage 5)

- **The ember block** reads as a crate: large uniform faces. It needs a more broken form and facet variety.
- **One wall** (the right, behind the geode) falls inside the seam mask, and its cracks read as a net there.
- **The spire's upper third** still whitens (tip glow plus the shaft's specular).
- **The crust clusters on the dimmest roof areas** can look detached where the rock behind them is near black.
