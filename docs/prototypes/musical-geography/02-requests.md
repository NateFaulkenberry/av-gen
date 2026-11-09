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

### Engineering → art, CP-A r3 (2026-11-08): the blue dashes, what the frames show

The renders below use your 3b7856e4 revealC, with skirt 3, plain material, no program and no shadows. Each arm
carries its own background tint as a canary. The stills are in `02-terrain/`.

- `eng-r5-revealC-face-debug.png`: each face in its own colour. The dashes are **side faces** (the step risers), and
  the far-tile rule has already flattened them (yellow).
- `eng-r5-revealC-plain-ao-on.png` against `eng-r5-revealC-plain-ao-off.png`: **turning screen-space AO off
  (`environment.ambientOcclusion: false`) removes most of the dash contrast.** GTAO reconstructs its normals from
  depth, so it sees every riser as a crease, whatever normal the slab path shades the riser with.
- `eng-r5-revealC-plain-unlit.png`: unlit, the dashes are gone entirely. A faint remainder after AO-off comes from
  the lit path, and I haven't pinned it down.
- Ruled out by frames: your program, sun shadow maps (frames are identical with and without casters), contact
  shadows, depth-prepass precision, the compiled-materials variant, holes, roughness and IBL.

**What to try on your side:** `"ambientOcclusion": false` in the environment. Or keep AO near and accept the far
crease shading; since ADR-1951 there is an AO distance fade (below). The lighter 1×/2× rectangle is the same mechanism (the 1 m
tiles have twice as many creases), and it weakens with AO off as well.

**Diagnostic switches** (procedural node; not for production):
- `"slabDebugFaces": true` colours each face: top red, x side green, z side blue, a flattened side toward yellow.
- `"slabSidePixels"` sets the side threshold. The default is 2 × `slabFarPixels`; raising it did not change the
  dashes, because they are already flattened.

### Open engine question (2026-11-08): sun shadow maps reach no visible pixel in revealC

In your 3b7856e4 revealC (camera at 120 m, `shadowRange` 220), the frame with shadows on (2 casting lights, the
1× and 2× rings as casters, 3 cascades, 6 draws, 1.63 ms) was bit-identical to the frame with both lights'
`castsShadow` off. The shadow passes cost time and change no pixel. Two readings, not yet told apart:

1. **Nothing is occluded.** The sun is at about 42° and the hills are gentle, so no tile top facing the sun would be
   behind another tile. A riser's own shadow is about 1.3 × its step height, under one shadow texel at this range.
2. **Procedural slabs don't receive, or the cascades miss them.** That would be an engine bug, and it may affect other
   scenes.

**Answer (the same day): it's reading 1, plus the range, and not an engine bug.** I rendered canaried arms with a low
sun (`dayPhase` 0.29); the stills are in `02-terrain/`. Each count is ground pixels differing by more than 8 from
shadows off:

| Arm | Far rows | Mid rows | Near rows | Shadow cost |
|---|---|---|---|---|
| `shadowRange` 220 (`eng-r6-sunlow-shadows-range220.png`) | 357 | 2 | 1,544 | 2.14 ms |
| `shadowRange` 1000 (`eng-r6-sunlow-shadows-range1000.png`) | 3,678 | 7,128 | 2,502 | 2.86 ms |

The difference images are `eng-r6-shadow-diff-range220-x8.png` and `-range1000-x8.png`.

- Procedural slabs do receive shadows. The shadows are riser specks, as expected.
- At 120 m altitude the nearest visible ground is about 187 m away, so a 220 m range covers only the bottom strip of
  the frame. At a 42° sun nothing in that strip is occluded.
- The 4× ring has `castsShadow: false`, so it never casts.

**For the art side:** at the reveal, the 1.63 ms of shadow passes buys no pixel at midday. Either drop
`castsShadow` there, or keep it for a low sun and raise `shadowRange` if you want the far ground shadowed (+0.7 ms).

### Engineering → art (2026-11-08): the AO distance fade (ADR-1951), what to switch on

```json
"environment": { "aoFadeStart": 200.0, "aoFadeEnd": 420.0 }
```

This is view depth in metres, smoothstepped. AO is full before `aoFadeStart` and gone after `aoFadeEnd`. The default
is off. The near ground keeps its creases and the far slab risers stop reading as AO dashes. Set the band per shot:
the values above suit revealC, where the nearest visible ground is about 187 m away. A low shot wants a nearer band,
for example 40 to 120 m.

Stills in `02-terrain/` (art revealC, canaried orange):

- `eng-r6-revealC-ao-fade-200-420.png`: the whole frame.
- `eng-r6-far-crop-ao-on-fade-off.png` and `eng-r6-near-crop-ao-on-fade-off.png`: AO on, faded, AO off, stacked top
  to bottom. Far, the faded frame matches AO off; near, it matches AO on.

The faint lit remainder of the dashes is in AO off too, so the fade doesn't remove it. That part is still open.

### Engineering → art, CP-A r7 (2026-11-08): the dash remainder is contact shadows; the rectangle is the 1× program

Tested on your 8ee329d8 revealC day (turned sun, AO fade 200–420 m, shadow range 480 m) with 13 canaried arms. The
stills are in `02-terrain/eng-r7-*`.

The dash metric is the high-frequency energy (mean |pixel − 7×7 box blur|) in the far, mid and near rows:

| Arm | Far | Mid | Near | GPU |
|---|---|---|---|---|
| Your scene as is | 7.72 | 4.24 | 6.91 | 30.6 ms |
| AO off | 7.39 | 3.77 | 6.69 | 29.3 ms |
| AO off, `contactShadows: false` | 4.98 | 2.26 | 6.47 | 21.9 ms |
| …and sun shadow maps off | 4.96 | 2.26 | 6.47 | 19.1 ms |
| …and `slabSidePixels` 1000 | 4.99 | 2.23 | 6.44 | 19.1 ms |
| Plain material, everything off (the floor) | 4.53 | 1.66 | 0.90 | 16.1 ms |
| **Your scene, `contactShadows: false` only** | 5.15 | 2.58 | 6.67 | **23.2 ms** |

**1. The dark dashes are screen-space contact shadows.** The contact march is about 5% of view depth long, so at
reveal distance it runs across many sub-pixel steps. Under the side sun, every riser in its path occludes in blotches.
`eng-r7-far-crop-art-vs-contact-off.png` shows your scene as it is on top and with contact shadows off underneath: the
blotches go. The far field then sits within 0.6 of the plain-material floor, and that 0.6 is your program's own
texture.

It is also the frame's biggest cost lever: −7.4 ms. The sun's shadow map (range 480 m) still shades the hills. The
earlier r5 "contact shadows ruled out" was a void arm: contact shadows only run on a light that casts shadows, and that
arm had shadows off.

**Switch on:** `"environment": { "contactShadows": false }`.

If contact shadows are wanted near the ground later (characters' feet), the engine can give them a distance fade like
ADR-1951's. Ask, and it's a small change.

**2. The lighter 1×/2× rectangle is the program.** Give the 1× ring `mgTile2x_day` (otherwise the same as the
contact-off arm) and the rectangle all but disappears. Only a faint edge is left:
`eng-r7-rectangle-1x-own-program-vs-2x-program.png`, with the 1× ring on its own program on top and on the 2× program
underneath. It isn't AO, because it shows with AO off. With plain material it's absent. So `mgTile1x_day` comes out
lighter than `mgTile2x_day` for the same ground, and evening the two programs' mean is on your side.

**3. What's left at the floor** (`eng-r7-plain-remainder-zoom-vs-face-debug.png`): with plain material and AO,
contact shadows and shadow maps all off, the flattened risers (yellow in the face-debug view) read a few levels
lighter than the tops, and a few sparse spots read darker.

- It is not the underlay: painted pure green, it reached 0 pixels.
- It is not the tile gaps: `slabFarPixels` 1000 leaves it unchanged.
- It is not the side normals: `slabSidePixels` 1000 leaves it unchanged.

It is faint next to the program's own texture, and I left it there.

### Engineering → art, CP-A r8 (2026-11-08): the orange flecks on sunlit steps

These are the risers that face the sun. Two things made them, and one was an engine bug, now fixed (ADR-1959
amendment, this branch). Each arm was canaried and run on your 8760494b sunset valleyC. The face-debug view
(`eng-r9-valley-plain-face-debug-zoom.png`) shows the flecks are the z sides (blue), the risers facing the camera.

1. **Bug (fixed): the side rule never reached live frames.** In the live profiler and editor, nothing reads the
   velocity target, and the slab vertex stage returned before it eased a small side's normal to the top's. So every
   side kept its own normal and `slabSidePixels` did nothing.
   - Before the fix, flattening every side changed 0 flecks (53,434 both ways). After it, 16 remain.
   - The same bug made the floor test's "risers lighter than tops": revealC's plain-material far dash energy is 4.53
     before and 0.77 after (`eng-r9-revealC-plain-floor-fixed.png`).
   - Offline renders were not affected.
2. **Physical: near risers under a low sun.** A riser wider than the side threshold (default 2 × `slabFarPixels` =
   12 px) is a real face. At a ~5° sun it takes nearly full sun while the tops graze. That is correct lighting, and it
   is what's left in the fixed frame. The flecks drop from 11,199 to 7,805 with your scene unchanged.

`eng-r9-valley-foreground-asis-fixed-side40.png` stacks the foreground as it was, with the fix, and with the fix plus
`slabSidePixels: 40`. The full frames are `eng-r9-valley-sunset-before.png`, `-fixed.png` and `-fixed-side40.png`.

**Your call:**
- `"slabSidePixels"` on the tile nodes sets how big a riser must be to keep its own lighting.
- At 40, the flecks go (14 remain), but the far slopes lose their stepped relief and read smooth.
- Something between 12 and 40, or set per state (sunset only), may be the balance.
- Shadows, contact shadows and your program are each ruled out: frames are unchanged without each of them.

### Engineering → art (2026-11-09): the sun per state and along the timeline (ADR-1961)

Per state, in `environment.dayNight`, radians, keyed by phase:

```json
"sunAzimuthOffsetCurve":   [ {"phase": 0.27, "value": 1.5708}, {"phase": 0.5, "value": 0.0}, {"phase": 0.72, "value": 0.0} ],
"sunElevationOffsetCurve": [ {"phase": 0.27, "value": 0.10},   {"phase": 0.5, "value": 0.0} ]
```

The offsets are added to the arc's own azimuth and elevation, and they interpolate between keys like every other
DayNight curve. Give each state you want left alone a 0 key, or the neighbouring key bleeds into it.

Along the timeline: the parameters `env/dayNight/sun/azimuthOffsetDeg` and `env/dayNight/sun/elevationOffsetDeg`
(degrees) are keyable and routable. They are added on top of the curves. A constant can also be set in the scene as
`dayNight.sunAzimuthOffset` / `sunElevationOffset` (radians).

Stills in `02-terrain/`, on revealC:
- `eng-r10-dawn-reveal-arc.png`: dawn on the arc, canaried green.
- `eng-r10-dawn-reveal-sun-turned-90.png`: the same dawn with the curve above, canaried magenta.
- `eng-r10-dawn-arc-vs-turned.png`: the two stacked.

The day frame with the curve is bit-identical to the day frame without it, because the 0 keys leave day alone.

### Engineering → art, Phase 3 (2026-11-09): the moving ground (ADR-1953), contours (1970), crest (1971)

One block in `environment`, which every tiles ring and the underlay follow:

```json
"tileDynamics": {
  "amplitude": 0.55,
  "waves": [
    {"kind": "planar", "amplitude": 1.0, "wavelength": 44, "speed": 9, "direction": 200, "sharpness": 0.6,
     "onLand": 0.3, "onWater": 1, "crest": 0.5},
    {"kind": "planar", "amplitude": 0.7, "wavelength": 28, "speed": 7, "direction": 255, "sharpness": 0.4,
     "onLand": 0.3, "onWater": 1, "crest": 0.5},
    {"kind": "circular", "amplitude": 0.5, "wavelength": 34, "speed": 12, "centre": [-34.5, -497.5]}
  ],
  "breathing": {"amplitude": 0.15, "period": 14, "wavelength": 700},
  "rolling": [0.8, 0.0],
  "rings": {"amplitude": 2.5, "speed": 50, "wavelength": 14, "width": 12, "decay": 2.2, "crest": 1},
  "events": [ {"time": 31.5, "position": [x, z], "strength": 1} ],
  "contour": {"interval": 2.0, "offset": 0.0}
}
```

What the terms do:
- At most 4 waves and 16 visible events.
- `direction` is in degrees, with 0 along +X and 90 along +Z.
- `sharpness` runs from 0 (a sine) toward 1 (narrow crests, flat troughs).
- `onLand` and `onWater` weight a train by the tile's water channel.
- Every key is optional, and an unknown key is refused.

**Audio** goes through routes onto the parameters, which are registered when the block is present:
- `env/tiles/amplitude`;
- `env/tiles/waveN/{amplitude,speed,wavelength,direction,crest}`;
- `env/tiles/breathing/{amplitude,period}` and `env/tiles/rolling/{x,z}`;
- `env/tiles/rings/{amplitude,speed,decay}`;
- `env/tiles/contour/{interval,offset}`;
- the trigger set: `env/tiles/rings/trigger`, which drops a ring on a rising edge through 0.5, at `rings/x,z`, or
  `rings/ahead` metres ahead of the camera when that is above 0, with `rings/strength`.

The clip's routes are `response.low` onto `env/tiles/amplitude` (add 0.9) and `response.onset` onto
`env/tiles/rings/trigger`.

**The underlay:** set `"childrenMin": true` on the underlay's generator. Its cells then stand at the lowest fine tile,
so the moving sea's troughs never uncover it; without it, the troughs showed dark-blue specks.

**A note on the look:** with a low camera, the 1 m tile gaps seen end-on near the camera read as dark lines down to the
underlay. This is static and not motion; it is yours to judge (a lighter underlay, or a smaller gap near).

**Material:**
- **The crest.** With no contours, the crest is `instanceEmissive.w` (0..1). With contours on, use the op.
- **The `tileContour` op.** It takes `srcA` = localPosition and `srcB` = instanceEmissive. Its constant is
  `[halfX, halfZ, band, 0]` and its value is the minimum band in pixels, the same convention as tileBorder. It returns
  `x` = contour coverage on the flagged sides only, `y` = crest, `z` = flagged sides / 4, `w` = any.
- **`instanceIndex` on a tiles ring** now reads the tile's top height in metres.

**Prove-it stills** are in `03-dynamic/` (`eng-p3-*`), each with its own background tint:
- static;
- waves ×5;
- interference ×5, before and after the sea skirt;
- breathing;
- rolling;
- a ring;
- contours;
- crest crossings.

**The clip and perf (2026-11-09).** The clip is `03-dynamic/eng-p3-clip-feline-30-45s.mp4`: Feline Footwear 30–45 s,
the bay at 22 m, the routes above, two authored rings at 33 s and 39 s, onset rings 150 m ahead, and the crest added
to your programs' emission as a stand-in (yours to replace).

Live, 1080p ultra at 30 fps (600 frames, quiet GPU), the same camera with and without the moving ground:

| | GPU median | p95 | p99 | Frames on time | Generators |
|---|---|---|---|---|---|
| Moving | 24.9 ms | 31.2 | 33.5 | 98.9% | 0.88 ms |
| Static | 26.7 ms | 31.1 | 33.1 | 98.9% | 0.77 ms |

The moving ground costs +0.11 ms in the generators and nothing measurable in the scene pass. The frame-interval p99
of 49 ms and the ~290 ms worst frames happen in both runs, so they predate this work.

### Engineering → art, Phase 5 (2026-11-09): the flight (ADR-1956), the dark gap lines, and the "flat" sunset sky

**The flight.** Add a top-level `"flight"` block to the scene. It drives the camera; no other camera block is needed.

```json
"flight": {
  "ground": "mgTiles1",          // the tiles node it follows (its own tile pitch, so 1 m or 4 m tiles both work)
  "startTime": 30.0,             // timeline seconds at the first waypoint
  "duration": 120.0,             // seconds the table covers
  "speed": 22.0,                 // m/s (a waypoint's own "speed" overrides from there)
  "fov": 50.0,                   // 0 = the scene's own
  "route": [
    {"position": [60, -250], "altitude": 14},
    {"position": [10, -440], "altitude": 7},
    {"position": [-330, -840], "altitude": 70, "speed": 30},
    {"position": [-540, -940], "altitude": 20}
  ],
  "minClearance": 4, "clearanceRadius": 10, "amplitudeHeadroom": 2,
  "climbRate": 18, "fallRate": 30, "lookAhead": 2.5, "pitchBias": 0.12,
  "pitchMin": -28, "pitchMax": 8, "bankLimit": 11.5, "bankGain": 1.6, "loop": false
}
```

How it flies:
- **Altitude** is metres above the smoothed ground. It is the same rule for hugging flight (5–10) and reveals (60+).
- **Clearance** is checked against the moving ground at each moment: your waves, at `amplitudeHeadroom` × their
  authored amplitude, plus the rings. Climbs and dives are limited by `climbRate` and `fallRate`, then smoothed.
- **Aim and bank:** the camera looks `lookAhead` seconds along its own path, a little down, and banks into turns.
  There are no orbits and no head turns.
- **Continuity:** past the last waypoint it flies straight on, or `"loop": true` comes round. There are no cuts.

Not built yet: named targets (follow the path, cross the shore) and the top-down leg. Both are for CP-B.

**The dark lines down the ground** (low camera, along the columns): the cause is the AO radius, not the underlay.
- The underlay is ruled out by a canary: painted magenta, it reached 4 pixels.
- The AO's automatic radius is 5% of the distance to the camera's target, capped at 4 m. A flight's target is a
  look-ahead point far out, so the radius runs at 4 m and every 8 cm gap becomes a well.
- **New:** `environment.aoRadius` (metres; 0 = automatic).

Measured on the same frame, near-black pixels in the lower half:

| Arm | Near-black pixels |
|---|---|
| Automatic radius | 976 |
| `aoRadius` 1.2 | 161 |
| `aoRadius` 0.6 | 1 |
| AO off | 0 |

**Switch on `"aoRadius": 0.6` for the flight.** The fainter lines left are the sun's shadow in the gaps.

**The flat sunset sky:** this is not an engine fault, and the gradient is drawn.
- **The sky renders.** A canary zenith of pure green shows clearly, and the original violet does too once the frame
  holds sky above the horizon (`03-dynamic/eng-p5-sky-*.png`). Fog has no effect on it: the frame is identical with
  `horizonDensity` 0 and `volumeDensity` 0.
- **What reads as flat sky is the sky *below* the horizon.** From 120 m the tiles end about 10° below the horizon (the
  4× ring stops at 480–640 m). Between there and the horizon you see the sky's ground fill, which is flat by design
  and is your `groundColor` curve, set to the horizon orange.
- **Your tilted arm** probably held only a few degrees of real sky.

Levers, in order of how much they change the frame:
1. Fly lower, or frame more sky. The flight's `pitchMax` lets it look up on climbs.
2. Give the DayNight `groundColor` curve its own value: a darker or more violet tone reads as a distant ground band
   under the fog.
3. Reach further: a wider 4× ring, or another ring, under the horizon fog.

The engine could also blend horizon to ground below the horizon (a `groundHaze` width); it's not built. Ask if you
want it.
