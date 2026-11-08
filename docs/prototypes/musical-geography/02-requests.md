# Musical Geography: engine requests (art → engineering)

The art agent writes requests here, through the coordinator. Engineering marks each one, says where it lands, and
writes its ADR (in the art range, 1970–1979) when it is built. Owner decisions of 2026-11-08 apply: thin slabs, sharp
edges, rolling land only (no canyon, mesa, terrace cliffs or strata), the Glowmere aliens, a short top-down pass.

Source: `04-art-direction.md` §10 at c0e936d1 (art branch). The status mirrors `03-technical-plan.md` §5.1.

| # | Request | Status | Phase | Notes |
|---|---|---|---|---|
| 1970 | Contour edges: four per-side weights per tile, lit where `floor(h/interval)` drops across that side; `contour/interval`, `contour/offset` | **Accepted, adjusted** | 3 | Sharp edges (no chamfer), so the band is drawn on the top face's border from `localPosition`. Four neighbour heights per tile in the effector pass; cost measured, limited to the visible set |
| 1971 | Crest weight per wave set, `crestSharpness`, brighter where trains cross | **Accepted** | 3 | The crest channel |
| 1972 | Terrain palette: a 5-stop land ramp with height stops, a 4-stop water ramp, shallows, path/pad/river colours, drift, edge and crest gains, fog multiplier, exposure offset; OKLab; absolute height | **Accepted, minus** the canyon wall colour, strata amplitude and river (their formations are gone) | 4 | DayNight curves (plan §4.4) |
| 1973 | Region channels for the material: water, shore distance, terrace index, canyon wall, river, path, plateau flatness | **Accepted, reduced** to water, shore distance, flatness (crowns/pads) and path | 2 | Terrace index, canyon wall and river are dropped with their formations |
| 1974 | Per-instance roughness (water 0.16, land 0.62) | **Accepted** | 2 | A material-program check first; built only if a program cannot already write roughness from a field |
| 1975 | Top-only emission and side shading | **Accepted, for a slab** | 2 | Emission masked to the top face; the side shade runs over the slab's normalised local height (no column) |
| 1976 | Distance fade of the gap and edge band by pixel footprint | **Superseded by request B below** | 2 | The gap no longer fades; a separation that survives distance replaces it (B) |
| 1977 | Fog: exp²-like distance curve, height falloff, sun-facing tint; **plus output dither (request A)** | **Mostly existing; dither new** | 2 check, 4 fix; dither in 2 | The single medium (ADR-705): `horizonDensity`, `fogHeightFalloff`, `fogHeightCurve`, anisotropic sun scattering. The window radius is tied to the 0.98 fog distance by a test |
| 1978 | Lattice coordinates in material programs (never the record index or a camera-relative position) | **Accepted** | 2 | With the lattice-keyed randoms (plan §4.2) |
| 1979 | Flight route through named features; per-leg pitch offset and focal curve | **Accepted, extended** | 5 | Plus the owner's top-down leg |

**Smaller asks (no ADR).**
- Plateau levels: replaced by crown flatness, because the mesas are gone.
- The path spline is exposed to the camera and the characters.
- AgX is already the default tonemap (ADR-039).
- The bloom threshold and intensity are already `post/*` parameters, which DayNight's phase can key.

## New requests

(Append below; engineering answers inline.)

### Art, 2026-11-08 (after Gate 1): two look requirements the owner's review made binding (04- §11)

(Merged from the art branch, bd223380. The coordinator folds A into 1977 and B into 1976.)

- **A. Output dither, so the sky and fog never band.**
  - The sky gradients band visibly in the r2 mock-ups.
  - The engine has no output dither today. `post/output/grain` is the only stand-in, and look.py sets it to 0.035
    meanwhile.
  - Ask: a triangular-PDF dither of about ±1 LSB, applied after the tonemap and before 8-bit quantisation (the live
    swapchain and the encoder both), or a higher-precision sky and fog.
  - Check: a large-sky frame in each state, especially dusk and night.
- **B. The tile has to read at reveal altitude. This changes 1976 and §6.1's far field.**
  - Plan §6.1 makes the far ring "a continuous surface" beyond the gap fade, and 1976 fades the gap with distance.
  - Both make the squares disappear exactly where the owner's review says they must still read: at 70–100 tiles up,
    with a 35–50 mm lens, across all the land in the frame.
  - Ask: keep a separation that survives distance instead of fading it. Two ways:
    - a footprint-aware darkened border on each top that stays at least about 1 px wide;
    - a far ring of tiles at 2× or 4× pitch, merged in whole lattice blocks with fog over the seam, so the far field
      stays tiles rather than a smooth mesh.
  - Per-tile value jitter that grows with distance (±2% near, up to ±6% far) helps too, and art can do that in the
    program once 1978's lattice randoms exist.
  - The far *underlay* (the dark sheet under the slabs) is unaffected and still wanted.

**Engineering, 2026-11-08:** both accepted.
- **A** becomes part of ADR-1977: a triangular-PDF dither of ±1 LSB, after the tonemap and before quantisation, in the
  output stage that both the swapchain and the encoder read.
- **B** becomes ADR-1976, as rewritten:
  1. A **footprint-aware border**. The material input carries the tile's border distance in pixels, so a program can
     hold a darkened border at least 1 px wide at any distance.
  2. A **far ring of coarser tiles**: whole 2× and 4× lattice blocks, each the mean of its children's heights, in the
     same slab draw. The far field stays tiles.

  Which of the two carries the reveal is decided by measured cost and moiré on the high-reveal still. It may be both.
  The continuous far *mesh* of plan §6.1 is dropped; the underlay stays.

### Engineering → art, 2026-11-08 (Phase 2): what the first ground stills showed, and the fog's cost

From the engineering fixture (`examples/musical-geography/eng/make_ground.py`). `world.py` on the art branch (at
6c3ad19c) has the same set-up as the fixture's first version, so the same fixes apply.

- **The underlay pokes through slopes.**
  - 8 m blocks at −1.6 m stand at their mean height. On a slope that mean is above the lower fine tiles, so dark
    slabs show on hillsides; the first hills still showed it plainly.
  - Fix in the fixture (fd110d88): the underlay sits **under the fine ring only** (the only ring with geometric gaps),
    with `cellSize` 4, `finePitch` 1, `y` −2.2 and `viewDistance` = the fine ring's.
- **The coarse rings' gaps alias into dark holes at distance.**
  - Fix: give the 2× and 4× rings **no geometric gap** (box = cell × 1.0).
  - Let their program paint the fine lattice instead. `tileBorder` takes `constant2.x` = subdivisions per side (2 for
    the 2× ring, 4 for the 4× ring), so a 4 m block draws the sixteen 1 m tiles it stands for. The lattice then reads
    the same across rings, which is request B's point.
- **`tileBorder`** (ADR-1976): `{"kind": "tileBorder", "srcA": <localPosition>, "constant": [halfX, halfZ, band,
  halfThickness], "constant2": [subdivisions, 0, 0, 0], "value": <min pixels>}`.
  - It returns (coverage, distance to the edge in px, top-face mask, (sub)tile size in px).
  - Under 4 px per tile the coverage eases to its mean, gone by 2 px, so the pattern is prefiltered rather than
    aliased.
  - The fixture's program uses band 0.03 and `value` 1.0.
- **The fog's cost — your call, with the numbers.**
  - At 1080p Ultra, the volumetric march (`volumeDensity` > 0, the default 32 steps, `volumeMaxDistance` 200) costs
    **8.4–8.5 ms in every view**: about a third of the reveal's 28 ms.
  - The cheaper options:
    - **surface-only fog**: `volumeMaxDistance: 0`, the same air in closed form on the surfaces, no march, no light
      shafts;
    - fewer steps: `volumeSteps` 12;
    - the High live level instead of Ultra.
  - **Measured** on the engineering fixture's high reveal (headless 1080p, 3 s, under the GPU lock, d7ae4cc9):

    | Fog | Ultra: GPU p50 / the march | High: GPU p50 / the march |
    |---|---|---|
    | Volumetric, 32 steps (default) | 28.4 ms / 8.5 ms | 20.4 ms / 2.6 ms |
    | Volumetric, `volumeSteps` 12 | 23.4 ms / 3.5 ms | 18.9 ms / 1.1 ms |
    | Surface only (`volumeMaxDistance` 0) | 20.0 ms / — | 17.9 ms / — |
    | None | 19.3 ms / — | 17.3 ms / — |

  - Surface-only fog costs 0.6 ms more than none and saves 8.4 ms at Ultra. It is the same air in closed form, with
    no light shafts. Twelve steps keep shafts for 3.5 ms. The High live level already marches at reduced resolution
    and steps (2.6 ms). Stills of each are beside the numbers in the scratchpad (`cpa3/*/frame-*.png`).

### Engineering → art, CP-A (2026-11-08): what your look shows on the fixed ground

Your scenes were rendered (`build.py`, day and sunset, reveal and land cameras) with one change, the underlay moved to
the fix above. Stills are in `105-musical-geography/02-terrain/`.

- **High reveal: the squares read across all the land.** Colour, the lit path and the hill bands carry.
- **Low land camera: concentric moiré in the middle distance.** Your program's hand-made border uses the isotropic
  `footprint`. At a grazing angle the pixel is much longer along the view than across it, so the 1-px border aliases
  into rings. `tileBorder` (ADR-1976) uses the per-axis footprint and prefilters below 4 px a tile. Swapping it in
  for the border block should remove the rings; it also gives the top mask and the tile's size in pixels.
- **Ring seams:** a faint straight line where the 1× ring meets the 2× ring (visible on the day reveal's hill). A 2×
  block stands at its children's mean height, so it steps against the fine tiles beside it. Engineering will look at
  a blend band in Phase 3, alongside the dynamic terms.
- **Cost of your look at the reveal: 34.3 ms GPU p50, headless 1080p Ultra** (day; sunset 34.2). That is over Ultra's
  budget. The scene pass is 22.5 ms (17.5 ms on the engineering fixture) and the fog 8.5 ms. The levers, in order:
  - surface-only fog (`volumeMaxDistance` 0), about 8 ms;
  - fewer program ops on the 2× and 4× rings;
  - the High live level.

  For comparison, the engineering fixture's reveal ran live at 1080p30 with 1 miss in 601 frames (p99 37.6 ms).


### Engineering → art, Phase 2 follow-up (2026-11-08)

- **The ring seam is fixed in the engine** (ADR-1976 amended, d7ae4cc9). A ring takes
  `"blend": {"cells": N, "ratio": 2}`. Within N cells of its window edge, a tile eases to its coarser block's mean
  height, which is where the next ring stands, so the two rings meet at one height. The fixture uses 8 on the 1×
  ring and 4 on the 2× ring. The ring's `align` must be a multiple of `ratio`, so 4 on the 1× ring and 2 on the 2×
  ring, as you already have. The GPU parity test covers it.
- **Where the reveal's scene pass goes.** Variants of the engineering reveal (scene pass at 1080p / 540p):

  | Variant | Scene pass, 1080p / 540p |
  |---|---|
  | Everything | 17.5 / 12.2 ms |
  | No material program, no fog | 13.8 / 11.1 ms |
  | The fine ring only (1× to 160 m) | 1.5 / 3.0 ms |
  | No shadows | 16.7 / 11.7 ms |
  | The general (non-slab) path | 18.9 / 13.5 ms, plus 1.9 ms of depth |

  - About 10 ms of the scene pass is **not per-pixel**, and it comes with the 2× and 4× rings. It is not the
    program (about 3 ms), the fog, the shadows or the slab path.
  - The root cause is not found yet. The next step is a Metal frame capture of the scene pass with the 4× ring alone.
  - Until then, the reveal's lever on your side is fewer program ops on the 2× and 4× rings (`tile_program(cell=...)`
    already takes the cell).

### Engineering → art, CP-A engine items (2026-11-08): what to switch on

These are the three items the coordinator held CP-A for, plus the height-scale finding.
1. **Gap aliasing at grazing angles, the sea streaks and the hillside speckle** (ADR-1959, the far-tile rule).
   - **On by default; nothing to switch on.** Each tile is measured on screen along both axes, and the smaller one
     counts:
     - under `slabFarPixels` (default 6) its gap closes to the pitch;
     - under twice that, its side faces take the top's local height and normal.
   - A node can set `"slabFarPixels"` (0 turns it off).
   - What it cannot fix is a line your own program paints. Your 1× ring's hand-made border uses the isotropic
     `footprint`, so it still rings at a grazing angle. Use `tileBorder` on the 1× ring too (subdivisions 1), as you
     do on the coarse rings: it measures each axis and prefilters below 4 px.
2. **The river valley** (ADR-1950). In `world.py`'s `TILES`, add:
   - `"river": [[x, z], ...]`: up to 16 points; route it so it reaches the sea;
   - `"riverWidth"`: the water channel, default 8;
   - `"valleyWidth"`: the smooth-sided valley, default 200, no walls.

   The channel's tiles carry water = 1, so your water colour and roughness apply to them as they are. The same
   polyline is what the Phase 5 flight and the Phase 6 characters can follow, like `path`.
3. **How tall the world stands.** Add `"heightScale"` to `TILES` (default 1; 1.3 lifts a 20 m summit to 26 m). It
   scales the relief above the shore and leaves the sea alone. What also caps the tops is `crownThreshold` and
   `crownFlatten`: crowns flatten everything above the threshold. Raise the threshold, or lower the flatten, if the
   tops of your colour ramp still don't show.

### Engineering → art, CP-A r3 findings (2026-11-08): the skirt, and the cost lever

- **The rings, the streak rows and part of the speckle were cracks, not gaps.**
  - The far-tile rule works. A GPU test proves it closes gaps, and your land view at a 1000 px threshold has every
    gap closed.
  - The mid-distance rings survived anyway. Where neighbours differ by more than a slab's thickness (0.3 m on your
    1× ring), a thin slab leaves an open slit under its upper edge, and at a slant you see sky or fog through it.
    The raised land makes those steps common.
  - The fix is `generator.skirt` (metres; engine 58a9c907, ADR-1950 amendment). Each land tile's box reaches that
    far further down with its top unchanged, so every step's riser is solid. Water tiles keep their thickness (it is
    scaled by `1 − water`), so the sea still floats.
  - **Switch on:** `"skirt": 3` in each ring's generator (`world.py` `ring_nodes`); not on the underlay.
  - Before and after, in `02-terrain/` as `eng-r4-<view>-before-skirt.png` and `eng-r4-<view>-skirt3.png`, on your
    3b7856e4 scenes:
    - the land view's rings fade to faint;
    - the sunset valley's streak rows mostly go;
    - **the dark dashes on the reveal and the lighter 1× rectangle are still there.** I am testing whether they come
      from the program (a program-off render is queued) and will report.
- **Cost of your composed reveal** (revealC, 35.15 ms GPU p50, headless Ultra):
  - **Fine ring 120 m** (the 2× hole and the underlay to match): **32.80 ms**, under the 33.3 ms budget.
  - Fine ring 96 m: 32.48 ms.
  - The fine ring's edge then sits nearer the camera, so the 1×/2× boundary is more in frame.
  - **The High tier live at 1080p30** (15 s) does hold 30 fps, but only just: frame-interval p99 39.9 ms (gate 40),
    3 deadline misses in 450 frames (the T1 gate allows 5 in 900), GPU p50 30.5 ms.

  The 120 m ring is the cleaner lever.
