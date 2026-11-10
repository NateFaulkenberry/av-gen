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

**The test flight and perf (2026-11-09).** The clip is `05-camera/eng-p5-test-flight-feline-30-60s.mp4`: Feline
30–60 s, a 7-waypoint route from the bay across the sea, onto the land and up a climb, with the moving ground, the
routes and `aoRadius` 0.6.

Live, 1080p ultra at 30 fps, 600 frames:

| GPU median | p95 | p99 | Frames on time | CPU work median | Scene update |
|---|---|---|---|---|---|
| 21.2 ms | 30.5 | 34.2 | 98.8% | 1.9 ms | 0.38 ms |

A flight table is built once per change; it is a lookup each frame.

### Engineering → art, Phase 5 (2026-11-09): ground to the horizon (ADR-1962), feature waypoints and the top-down leg

**Ground to the horizon:** two switches together.
1. `"environment": {"horizonFill": 1.0}`. Below the horizon the sky becomes the fog's colour at infinite distance,
   which is what a ground that went on forever would look like. It costs nothing.
2. **One far ring** past your coarsest: 64 m cells out to about 4.5 km, where the fog is opaque, so its edge melts. A
   plain material (no program) is enough, because it is all fog out there.

   ```json
   {"name": "mgTiles64", "kind": "procedural", "procedural": {
     "source": {"kind": "box", "size": [64, 6, 64]}, "slabs": true, "castsShadow": false, "lod": {"cull": true},
     "material": {"baseColor": [0.6, 0.45, 0.7], "roughness": 0.8},
     "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1, "cellSize": 64,
       "viewDistance": 4480, "align": 4, "finePitch": 16, "skirt": 40,
       "hole": {"cellSize": 16, "viewDistance": 1120, "align": 4}, "tiles": { ...the same tiles block... }}}}}
   ```

   - The ring's `hole` must name your 16 m ring's cellSize, viewDistance and align.
   - The 16 m ring's `align` must be a multiple of 64 / 16 = 4.
   - The camera's `far` must reach it (`"camera": {"far": 200000}`).
   - Cost: +2.8 ms GPU at 120 m.

The canaries show the sky's ground colour painted magenta. Before: 309,446 pixels (120 m) and 467,277 (180 m). After:
0 and 0. Stills are in `05-camera/eng-p5-horizon-*`.

**Feature waypoints** in `flight.route`:
- `{"follow": "path", "from": 0.0, "to": 0.55, "offset": 0, "altitude": 12}` flies along the path; `"river"` works the
  same way. `from` and `to` are fractions of the polyline's length, and `offset` is metres to the right.
- `{"snap": "shore", "position": [20, -470], "snapRadius": 250, "altitude": 10}` crosses the shore at the nearest
  shoreline point to the hint. `"crown"` snaps to the nearest crown top.

**The top-down leg:** `"nadir": 5` on a waypoint (a follow leg puts it at the leg's middle). The camera eases to
straight down over 5 s and back, with screen-up the direction of travel and no flip.

**At 4 m tiles,** use `"aoRadius": 1.0` (the gaps are 0.34 m).

The feature test flight is `05-camera/eng-p5b-feature-flight-4m-tiles-feline-30-70s.mp4`: 4 m tiles, the horizon fill,
a plain 64 m far ring, `aoRadius` 1.0, a shore snap, a path follow with a 5 s top-down leg, and a climb.

Live, 1080p ultra at 30 fps:

| GPU median | p95 | p99 | Frames on time |
|---|---|---|---|
| 17.2 ms | 31.3 | 36.2 | 98.6% |

### Engineering (2026-11-09): the flight's GPU tail is the GPU clocking down, not the route

The feature flight's GPU p95 of 31.3 ms and p99 of 36.2 ms came from a 600 s run that mostly held the flight's last
pose. Bucketed into 9 s windows along the route (live, 1080p ultra, 30 fps, 4 m tiles, horizon fill, 64 m ring):

| Flight seconds | GPU median | p95 | p99 |
|---|---|---|---|
| 0–9 | 12.3 | 15.4 | 18.0 |
| 10–19 | 12.2 | 15.6 | 17.6 |
| 20–29 (top-down leg) | 12.9 | 17.1 | 21.9 |
| 30–39 | 13.0 | 14.7 | 16.2 |
| 40–49 | 11.5 | 15.2 | 15.8 |
| 50–59 (the climb to 90 m) | 18.3 | 23.6 | 25.5 |

None of the suspects (the top-down leg, the 64 m ring, shadows at altitude) puts a window near the budget. The tail
appears only in runs longer than about 10 s, and it is the GPU lowering its clock in the idle half of each 33 ms
frame.

Canary, on the same 30 s of flight:

| Paced at | GPU p95 | p99 | Frames on time |
|---|---|---|---|
| 30 fps | 29.9 ms | 33.7 | 99.1% |
| 60 fps | 13.8 ms | 14.9 | 99.9% |

The work is about 15 ms. The 30 fps figure is the same work at a lower clock, which still fits the frame. No content
change is needed. The few frames that miss their deadline are CPU stalls (the profile's worst frames have CPU work up
to 270 ms on a GPU time of 16 ms), not the GPU. Judge GPU cost by the 60 fps run, or by the median.

### Engineering (2026-11-09): the live CPU stalls are the app shell, not the scene

New diagnostic: `AVGEN_SLOW_FRAME_MS=<ms>` logs, for every frame over the threshold, each phase that took over 1 ms.

On 60 s of the feature flight (live, 30 fps, Feline):
- The one long frame (62–70 ms, at the same place every run, about 13 s after the profile starts) is `events.poll` at
  26–30 ms: the OS event pump (SDL and Cocoa), in which `engine.update` and `render.record` take under 2 ms.
- The rest of the 38–41 ms frames are a `ui.build` of 5–9 ms every ~90 frames (3 s), arriving on top of a slow GPU
  frame (the clock-down above).

Control: the same project with an empty scene (GPU 1.5 ms) has the same `events.poll` stall at ~13 s (26.5 ms), the
same 3 s `ui.build` spikes, and the same 99.1% of frames on time.

None of the suspects shows in any slow frame:
- the flight table (built once, before measurement);
- ring re-centring and uploads (`rr.uploads` is 0 after the first frame);
- pipeline compiles (no compile in the log, and `compiledMaterials` stays steady);
- the contour pass (GPU only).

The stalls belong to the app's shell (the event pump and a periodic UI rebuild). They're reported, not fixed here.

### Engineering → art, Phase 6 / CP-C (2026-11-09): characters on the tiles (ADR-1954, 1955, 1957, 1958)

**What's built:**

- **Landing springs (ADR-1954).** A landing makes its tile dip, rebound and settle, and a smaller, later ripple
  spreads to the tiles around it. The motion is a function of the clock, so a seek gives the same result as play.
- **The ground seam (ADR-1955).** Bodies stand on the moving tiles as they are drawn: the waves, the breathing, the
  audio-driven amplitude, the live rings and other bodies' springs.
- **TileNav (ADR-1957).** Bodies walk steps of up to 0.6 m and never wade into the sea. They hop 1 to 3 tiles,
  rising up to 3 m or dropping up to 8 m.
- **The `hop` behaviour (ADR-1958).** A body pauses on its tile, picks a crown, sea slab or path tile 4 to 12 tiles
  away, then walks and hops there. Each landing springs the ground.

**Switch-on.** Each character needs a gltf node and an entity whose behaviour list is just `hop`. Leave out `ground`
and `decide`: they read a WorldMap, and this scene has none. Also leave out the `foot` IK layers for the same reason.
The clips map the hop's activities to the alien GLBs:

```json
{"name": "hopper0", "node": "hopper0", "seed": 7001, "tags": ["alien"],
 "clips": {"idle": "Idle", "walk": "Walking", "run": "Running", "turn": "Idle_turn", "observe": "Idle",
           "react": "Fight_idle", "jump": "Jumping", "fall": "Floating", "land": "Landing"},
 "gait": {"walkSpeed": 1.7, "runSpeed": 3.6},
 "behaviors": [{"kind": "hop", "hopChance": 0.45, "homeRadius": 28, "goalMin": 3, "goalMax": 7,
                "pauseMin": 0.8, "pauseMax": 2.6, "airTime": 1.2, "apex": 2.4}]}
```

The node is the usual alien gltf: scale 1.94, y 0. The hop sets the height.

The hop settings and their defaults:

- `walkSpeed` 1.7, `runSpeed` 3.6
- `airTime` 1.35 s, `apex` 2.2 m, `landTime` 0.4 s
- `hopChance` 0.35: also hop a walkable step, for joy
- `strength` 1: how hard a landing springs
- `pauseMin` 1, `pauseMax` 3.5
- `goalMin` 4, `goalMax` 12 (tiles)
- `preferCrowns` 1, `preferWater` 0.6, `preferPath` 0.6
- `homeRadius` 60 m
- `stepHeight` 0.6, `jumpTiles` 3, `jumpRise` 3, `jumpDrop` 8

`walkSpeed`, `runSpeed`, `airTime`, `apex`, `hopChance` and `strength` are live parameters under
`<entity>/hop/...` and can be routed to audio. The springs go in `environment.tileDynamics.spring`:
`{"depth": 0.6, "frequency": 1.6, "damping": 0.18, "rippleSpeed": 12, "rippleRadius": 5}`. CP-C used depth 0.7.
The node chosen is the flight's `ground` (`mgTiles1`).

**CP-C fixture.** Your CP-B r2 day build (`build.py --state day --flight`, unchanged) with six aliens placed beside
the lit path, where the flight passes at 10 m. The aliens are scout, ranger, elder, pilot, diver and trooper, in
`assets/aliens`, referenced and not copied. The camera rule holds: the flight is your r2 route, untouched. It looks
where it travels and passes over the characters, with no framing, orbit or turn toward them. They read clearly at 10 m
and are still visible from 28 m. In the stills they gather on the path (`preferPath`). Lower it if you want them on
the crowns.

**CP-C live perf** (1080p ultra, live mode, 30 fps target, Feline 40–76 s, 36 s measured, warm-up 4 s, same day route
with and without the six aliens):

| | Missed deadlines | GPU p50 / p95 / p99 | CPU work p50 / p99 |
|---|---|---|---|
| With the aliens | 1 / 1080 | 15.7 / 25.7 / 28.6 ms | 1.93 / 2.74 ms |
| Without | 1 / 1080 | 16.4 / 26.5 / 28.7 ms | 1.41 / 1.95 ms |

The six characters cost about 0.5 ms of CPU and no measurable GPU time. The one miss is in both arms (a 48 ms frame).

**Clip:** `~/Desktop/av-gen-review/105-musical-geography/06-characters/cpc-hop-day-feline-40-78s.mp4`. The fixture
script is `examples/musical-geography/eng/make_cpc.py`.

### Engineering → art, Phase 7 (2026-11-09): the audio wiring (ADR-1963, ADR-1964)

Everything goes through the existing modulation: project routes, the response bus, the beat clock, the levels, entity
reactions on the aliens, and the stems as feeds. Nothing is hardcoded, and there is no drum classifier. Every target
is a registered parameter, so it can be routed, keyed on the timeline, MIDI-mapped or saved in a preset.

**New engine pieces:**

- **`env/tiles/glow/{path,contour,crest,pads}` (ADR-1963).** Four emission gains, 1 = as authored. A tile program
  reads them through the new material input `tileGlow` (x path, y contour, z crest, w pads). A rest value can be set in
  the scene as `environment.tileDynamics.glow {"path", "contour", "crest", "pads"}`.

  To use them, add this to `look.tile_program` (11 ops at most; the programs stay under 96):

  ```python
  def glow(reg, lane, tmp):   # after `add = o.append`
      add({"kind": "input", "input": "tileGlow", "dst": tmp})
      add({"kind": "swizzle", "dst": tmp, "srcA": tmp, "constant": B(lane)})
      add({"kind": "multiply", "dst": reg, "srcA": reg, "srcB": tmp})
  ```

  Then call it in four places:
  - `glow(4, 3, 6)` after the pads' `r4 = emission`;
  - `glow(3, 0, 7)` after the path's `multiply dst 3` and before its `add`;
  - `glow(1, 1, 7)` after the contour's `multiply` by the edge colour and before its `add`;
  - `glow(1, 2, 7)` likewise for the crest.

  These are exactly the edits the 07-audio clips used.
- **`<alien>/hop/trigger` (ADR-1964).** A rising edge sets a resting alien off with a hop, or turns a walking alien's
  next step into a hop. Drive it with an entity **reaction**, not a project route: a seek replays reactions, so a
  scrub hops the way a play does. `hop/apex` and `hop/strength` (the landing spring) can be reaction targets the same
  way.
- **The aliens plan where to go on the ground without the audio-driven terms (ADR-1964),** so a route on
  `env/tiles/*` never changes where they go, only how they ride the ground.

**The mapping the 07-audio clips use** (`examples/musical-geography/eng/make_audio.py`; take it as a starting point,
and the amounts and choices are yours):

| Source | Target | Amount | Shape | Reads as |
|---|---|---|---|---|
| `response.low` | `env/tiles/amplitude` | +0.6 | | the swell rises with kicks and bass notes |
| `response.onset` | `env/tiles/rings/trigger` | 1 | | a ring 150 m ahead on each attack |
| `audio.energy` | `env/tiles/breathing/amplitude` | +0.25 | already 1 s smooth | dense passages breathe deeper |
| `beat.bar` | `env/tiles/contour/offset` | +4 m (the interval) | | contours climb the hills one interval per bar (seamless wrap) |
| `beat.pulse` | `env/tiles/glow/contour` | +1.5 | peak hold, falls 4/s | contours flash on every beat |
| `response.mid` | `env/tiles/glow/path` | +1.6 | 30 ms attack / 250 ms decay | the path breathes with the melodic register |
| `response.high` | `env/tiles/glow/pads` | +1.0 | 20 / 300 ms | the crown pads with the highs |
| reaction `beat.pulse` | `hop/trigger` | 1 | | aliens hop on the beat |
| reaction `response.level` | `hop/apex` and `hop/strength` | +1.6 / +0.8 | 80 / 400 ms | louder means higher hops and harder landings |

**Silence is calm.** Every route adds to a rest value and is unipolar, so silence shows the rest. Rest glow in the
clips: path 0.4, contour 0.5, crest 1, pads 0.5. The waves, breathing and rolling have no audio factor, so a quiet
passage still moves.

**Stems.** On Feline the project loads all three stems. The feeds are `beat` and `hits` from `stem.drums` and
`levels` from `stem.bass`; the log reports "the beat clock follows stem 'drums': 120.97 bpm". Every route above keeps
working unchanged. Per-part routes can read `role.bass.level`, `role.synth.onset` and so on.

**MIDI.** All the new targets are ordinary parameters, so they can be MIDI-learned like any other.

**Clips** (`~/Desktop/av-gen-review/105-musical-geography/07-audio/`): your night state, flight and 14-alien cast,
with the tileGlow edit and the mapping above. `p7-night-feline-footwear-silent-45-65s.mp4` is the same flight on the
same timeline with digital silence.

How strongly each clip's contour flashes lock to its track's tempo, measured from the frames as the strength of the
contour-pixel signal at the beat frequency, against the silent clip:

| Clip | Track | At 1× tempo | At 2× tempo |
|---|---|---|---|
| `p7-night-feline-footwear-45-65s.mp4` | Feline Footwear: dense, 121 BPM, stems | 342 vs 16 | 355 vs 9 |
| `p7-night-night-shift-55-80s.mp4` | Night Shift: sparse, quiet, 96 BPM, verse into the break | 222 vs 46 | 413 vs 40 |
| `p7-night-glowmere-valley-45-65s.mp4` | Glowmere Valley score: slow, 72 BPM | 88 vs 35 | 620 vs 311 |
| `p7-night-feline-footwear-silent-45-65s.mp4` | silence | n/a | n/a |

Frame-to-frame brightness flicker is 0.49 in silence and 1.3–1.7 with music.

**One art note:** at night `path_emit` 5 saturates the path. Even at a rest gain of 0.4 it clips, so the path route
barely shows in the night clips. Lowering `path_emit` and letting `glow/path` carry the variation would make the path
visibly move with the mids.

**Phase 7 live perf** (1080p ultra, live mode, 30 fps target; night state, 14 aliens, Feline 40–76 s, 36 s measured):

| | Missed deadlines | GPU p50 / p95 / p99 | CPU work p50 / p99 |
|---|---|---|---|
| Wired (3 stems, feeds, 7 routes, 48 reactions) | 1 / 1081 | 22.5 / 30.6 / 31.6 ms | 5.1 / 10.6 ms |
| Silence | 1 / 1081 | 23.3 / 30.9 / 31.7 ms | 3.6 / 8.0 ms |

The wiring costs about 1.5 ms of CPU, which covers the stem analysis, the routes and the rings, and no GPU time. The
single miss is in both arms.
### Art → engineering, CP-D draft2 (2026-11-09): one tile-gap column wider than the rest (please look)

In the full flight there is one gap column between 1× tiles, running straight along the view, that reads about twice
the width of its neighbours. It is at about 200 s of `build.py --full --characters`; the camera is near
(1490, −2000) heading north, and the seam is a little right of centre.

- **It is not the underlay.** With `nodes/mgUnderlay/visible` false it is unchanged.
- **It is not the state.** It is the same width in the dawn colours and with the timeline off (day colours):
  `10-full/cpd-draft2-seam-dawn-vs-day-200s.png`, dawn on the left, day on the right.
- **What we see down it** is the slab sides and skirt. The program colours those from the bottom of the land ramp, so
  draft1's green dawn (`land[0]` `#2E9C78`) lit it teal, and the coordinator saw it.
- **Status:** draft2's rose dawn hides the colour, but the geometry is unchanged.
- **My guess, unverified:** a world-locked lattice line, possibly a 64 m boundary (x ≈ 1536) where a ring's
  alignment and the fine pitch disagree.
- **Ask:** is it a generator seam, and can the fine ring close it?

### Engineering → art, CP-D (2026-11-09): the "one wider gap column" is the gap under the camera, not a seam

**Cause.** It is not a lattice seam and not at a fixed x. It is whichever 1× gap line runs under the camera: the only
gap you look straight down. At its bottom you see the underlay's top, which sits 2.8 m below the tiles
(`world.UNDERLAY`). Every other gap shows only its near wall, because the deep skirts hide the floor once a gap is
more than about 0.5 m to the side of the camera.

**Canaries at 200 s** (`build.py --full --characters`):

| Test | Result | Rules out or shows |
|---|---|---|
| The flight's eye | x 1560.7, over the gap at x 1560. Art's x ≈ 1536 is six tiles away | It is not the 64 m boundary |
| The slab fast path off | Identical | Not ADR-1959 |
| The underlay hidden | The column turns from purple to a bright see-through line. Over the song, the column is the only place an underlay-off frame differs (20 frames, every 10 s) | The underlay is what shows; it must stay |
| The ground shifted 1 m and 2 m | The column stays on the camera's nadir line | It follows the camera, not the world |

**Fix (9911ce6d, in your files, small).** The underlay's height ramp now reads the colour at the tiles' tops instead
of at its own lower top: `tile_program(..., ramp_offset=look.UNDER_RAMP * TILE_SCALE)`, with `UNDER_RAMP` = 0.7 (in
units of S). Both the per-state build and the `--full` arc programs use it.

At 200 s the column floor goes from (125, 71, 117), a purple, to (133, 93, 98), against walls of (171, 104, 115). It
now reads as one more gap line, slightly darker, which matches `UNDER_SHADE`'s "gaps read as shadow". Raise
`UNDER_SHADE` if you want it to vanish completely. I only checked the frame at 200 s; please confirm in your night and
sea frames.

### Engineering (2026-11-09): the whole-song GPU tail is the GPU clocking down at 30 fps, not the song

**Runs.** Draft3 (`build.py --full --characters`), the whole song. Feline 0–204 s, 1080p ultra, live, warm-up 4 s.
Two arms back to back: a 30 fps target with `AVGEN_SLOW_FRAME_MS=45`, and a 60 fps target with
`AVGEN_SLOW_FRAME_MS=25`.

**Quiet GPU.** No other avgen or GPU-lock job ran during either arm: `ps` before and after, and samples every 5 s.
One other process was busy: av-agent's pytest, CPU only, at more than 15% CPU in 78 of 83 samples.

| Arm | Missed deadlines | GPU p50 / p95 / p99 / max | Worst frame after warm-up |
|---|---|---|---|
| 30 fps | 1 / 6122 | 27.0 / 31.0 / 31.6 / 39.1 ms | 46.5 ms |
| 60 fps | 1 / 12241 | 13.3 / 14.9 / 15.8 / 22.5 ms | 32.8 ms |

**By song time, in 10 s buckets:**
- The 30 fps arm's GPU p50 sits at about 16 ms until 110 s, then steps to about 30 ms and stays there to the end of
  the song, through night and dawn.
- The 60 fps arm, on the same content, is flat at 12.7–14.9 ms in every bucket, including 110–210 s.

The same frames take half the time when the GPU is kept busy. So the 30 fps tail is the GPU dropping its clock: it
idles between frames and stretches each frame toward the budget. This is the clock-down already measured on the flight.

**What the tail is not:**
- **A hotspot by song time:** the 60 fps arm is flat.
- **Contention:** the GPU was quiet. Art's 43–45 ms p99 does not reproduce on a quiet GPU: 31.6 ms here.
- **A stall:** after warm-up, the only frames over the thresholds are 46.5 ms (30 fps arm, at about 15 s) and
  32.8 ms (60 fps arm, at about 15 s), both `events.poll`, which is the app shell.

The 277 ms frame at 157–158 s in draft2 does not reproduce. The largest frame anywhere near that time is 20 ms. The
only frames over 100 ms are the two warm-up frames: frame 0, the load, at 485 ms, and frame 11 of the 60 fps arm,
pipelines, at 180 ms.

**For CP-D's perf line, use the 60 fps arm's GPU numbers** (p99 15.8 ms over the whole song) as the cost. At a
30 fps target the GPU time is stretched by the clock. It still meets the deadline: 1 miss in 6122 frames.

### Art → engineering, CP-D r2 (2026-11-10): terrain variety and tile clouds (owner's CP-D notes 1 and 3)

The owner said of draft3: "pretty good start", but "terrain needs more variety: hills, valleys, rivers and ponds", and
"the sky reads too plain, add cinematic clouds". Stills are in
`~/Desktop/av-gen-review/105-musical-geography/10-full/cpd-r2-terrain-*` and `cpd-r2-sky-*`. ADR numbers are
yours to assign; the art range 1970–1979 is used up.

#### CPD-R1. Water bodies and valleys in TileTerrain

Today TileTerrain has one `river` (at most 16 points) that carves its valley down to the sea floor, and `water` is a
single class. The stills fake each feature with that one polyline:
- a pond is a short, wide capsule: `riverWidth` 80, `valleyWidth` 190;
- a dry valley is a 1 m channel.

Both read well, with contour rings around the basin at night. What I need is several features at once, at their own
levels:

1. **`rivers`: several polylines.** Each has `points` (up to 32), `width`, `valleyWidth` and `bedDepth`.
   - `bedDepth` is in metres below the land on either side. Today the bed always goes down to the sea floor.
   - The bed should ease down from the source to the mouth (sea level), so an upland stream runs at its own height.
   - `river` stays as `rivers[0]`.
   - The flight's `follow` and the characters take a river's index or name.
2. **`ponds`: small lakes set into the land.** Each has `centre`, `radius`, `basinWidth`, `level` and `shoreNoise`.
   - `level` is in metres, or `"auto"`: the basin rim's lowest point minus 1 m, so a pond can sit up on a plateau.
   - `shoreNoise` (0–1) makes the shoreline irregular.
   - Tiles within the radius are water at `level`. The basin eases the land down over `basinWidth` with the river's
     smoothstep profile: rolling, with no walls.
3. **`valleys`: dry valleys.** These are polylines with a valley and no water. Each has `valleyWidth` and a `depth`
   (in metres, or a fraction of the local relief), so the valley does not cut down to sea level.
4. **A water-body channel.** The material and the motion need to know which body a water tile belongs to: sea 0,
   river 1, pond 2. That could be `instanceColor.r` or a fifth lane. It would let the material colour ponds apart
   from the sea, with their own glow at night. The ADR-1953 waves need a weight per class:
   - the sea keeps its swell;
   - a river gets a slow drift downstream along its polyline;
   - a pond stays still, with rings on onsets.

**Data-side today:** the hills can vary more by region (`regionVariation` 0.45 over a 1400 m `regionWavelength`).
That gives broad low plains and taller ranges (`cpd-r2-terrain-maps-now-A-B-C.png`). The real variety, though, comes
from ponds, more rivers and valleys, and that needs R1.

#### CPD-R2. Tile clouds: let the `tiles` kernel omit water cells

**The look is proven.** Each cloud layer is a second `tiles` ground lifted to altitude through `distributionTransform`.
- Its continent field is the cloud cover and its hills are the billows.
- A program cuts the "sea" cells (`opacity = 1 − water`, `alphaMode` mask).
- The result is stepped square-slab clouds in the world's own language. The scene's sun, moon and fill light them,
  so they carry the arc, and the fog layers them.
- There are two layers: low scud at 170 m (16 m cells), a scale cue above the 40 m ceiling, and cumulus at 650 m
  (48 m cells, stepped towers up to 110 m).
- Stills: `cpd-r2-sky-{day,dusk,night,dawn}.png`.

**The cost is the problem.** Measured at 1080p ultra on the full-song build, 40–60 s:

| Variant | GPU p50 | Change | Where |
|---|---|---|---|
| No clouds | 12.8 ms | | |
| Masked clouds | 57–61 ms | +44 ms | depth prepass 28 ms, scene 21 ms |
| The same layers drawn opaque | 20.4 ms | +7.6 ms | |

Every cell rasterises, and the empty half of the lattice runs the masked program in the prepass and then discards.

**The ask:** a generator flag, for example `"omit": "water"` on the `tiles` kernel, so cells whose `water` ≥ 0.5 are
never emitted. Then only the cloud cells raster, and the alpha mask goes away.
- **Target:** at most 2 ms for the two layers. Please measure it.
- **Also wanted, if cheap:**
  - a layer can opt out of `tileDynamics` and the contour neighbours (clouds need neither);
  - confirm that a generator window stays centred on the camera when the node's transform is keyed (wind drift on the
    timeline).
- **Nice to have:** cheap cloud shadows on the ground. That could be the cloud layer's cover field evaluated
  analytically along the sun direction at shading time, with no shadow-map cascade.

**Alternatives I looked at first:**
- **ADR-1289 volumetric lobes:** 18–26 ms for streaks, and authored, static masses.
- **ADR-1864 analytic cirrus:** cheap, but only a high streak layer, and its lit and shade colours are fixed rather
  than following the arc.
- **Rainbow World's mesh clouds (ADR-1590/1591):** smooth billowed meshes, a different language from the tiles, with
  about 900 heads.
- **The `cells` kernel:** it skips cells, but every element gets a random yaw (`generator.wgsl` line 175), so the slabs
  scatter like confetti, and its cluster fertility is a density dither rather than a mass with an edge.

#### CPD-R2, addendum (2026-11-10): the night clouds (owner: "too much like a mirror reflection of the ground")

**Done in data, with no engine change.** The cloud program's constants are keyed through the arc, like the terrain's
(`look.CLOUD_STATE`, `arc.full_state`). At night:
- **Undersides:** lifted from 0.45 to 0.85 of the tops.
- **Side faces:** held down to 0.55 (through the normal's y squared). These were the stray bright streaks: the moon
  caught them at grazing angles.
- **Cores only:** a cell is kept only where its billow rises above about a third of the layer's relief, by a
  smoothstep on the slab's centre height. The night sky becomes separate, broken masses with clear sky between,
  instead of a deck that mirrors the tile ground.
- **Bounce:** a faint cool self-light (`#6A74D8` × 0.1).

All 128 tracks are live in `--audit-routes`. Stills: `10-full/cpd-r2-sky-night-v2-*`.

**What data cannot do, and the ask:**
1. **A key-light direction input for material programs** (`keyLightDirection`: the sun's direction, or the moon's when
   the sun is down; plus its colour × intensity). With it, a program can give the cloud slabs a silver lining. That
   means brightening the edges and faces that look toward the moon (`dot(normal, L)`), plus forward scatter where
   the view runs toward it (`dot(viewDirection, L)`).

   Today the only way an edge catches the moon is the moon light itself, which is also what lit the side faces into
   streaks. The tops are rarely seen from under the clouds, so the rim is what reads.
2. **(Optional) the sky's ambient colour as a material input,** so the undersides take the sky's own tint at every
   phase rather than a keyed constant.
3. **Cell size:** nothing needed. The cloud layers' 16 m and 48 m cells against the ground's 4 m tiles, with the
   cores-only night, already break the reflection rhythm.
### Engineering → art, CP-D r2 (2026-11-10): the landing tail (ADR-1952) and the landing glow (ADR-1960)

**What I changed:**

- **The landing tail.** At most 8 landings used to be drawn. Draft3 had up to 104 ringing at once, so a landed tile
  was dropped about 0.6 s after it landed and snapped flat. Now every landing still ringing is drawn, up to 256. The
  plot is `11-pass-cpd-r2/cpd-r2-landing-displacement-before-after.png`. Nothing for you to switch on.
- **Your spring now rings all the way out,** so its settings show for the first time. Draft3's spring is 1.4 Hz with
  ζ 0.16: bouncy, ringing about 5 s. A gentler setting, frequency 0.7 and damping 0.35 (depth unchanged), gives one
  slow dip, one small rebound and a smooth settle. It is the third arm of the clip. The choice is yours.

**The landing glow, switch-on (4 ops in `tile_program`).** The glow outlines the tiles a landing pushes down and fades
with them. It is off until you add the ops below. Add them after the contour lines' `add` into r4; r3 still holds
`tileContour`'s result there:

```python
add({"kind": "swizzle", "dst": 1, "srcA": 3, "constant": B(2)})       # the landed tiles' outline x their glow
add({"kind": "input", "input": "landingGlow", "dst": 7})               # colour x gain
add({"kind": "multiply", "dst": 1, "srcA": 1, "srcB": 7})
add({"kind": "add", "dst": 4, "srcA": 4, "srcB": 1})
```

**Its width and strength:**
- **Width:** `tileContour`'s `constant.w` is the outline's band in local metres; 0 uses the contour lines' band.
  The clip uses `[half, half, CONTOUR_BAND, 0.3]`.
- **Gain and colour:** `environment.tileDynamics.glow.landing` (gain) and `glow.landingColor` (`[r, g, b]`). As
  parameters they are `env/tiles/glow/landing` and `env/tiles/glow/landingColor`, so you can key them per arc state
  like the other gains.
- **Spread:** `spring.glowRadius` (default 3 m) is how far the glow spreads around the impact. Keep it near a tile.
  The ripple's 14 m lit the whole field, because about 100 landings ring at once.
- **The clip's values:** gain 12, colour `[1.0, 0.85, 0.6]`, chosen to read in daylight. Night will want much less.
- **Clips** (`06-characters/`):
  - `cpd-r2-landing-before-vs-after-glow-50-70s.mp4`: top is draft3, bottom is the fix plus the glow;
  - `cpd-r2-landing-before-after-soft-50-70s.mp4`: draft3, then the fix, then the fix with the softer spring.

**One more thing, which the reactivity explainer covers in full (`10-audio-reactivity-now.md`):** the project needs
`"sonic": {"live": true}`. Without it, `response.low`, `response.onset`, `response.mid`, `response.high` and
`response.level` never publish, so four of the seven terrain routes and two hop reactions are dead in draft3. I have
not changed `build.py`; this is one line in `project_doc`.

#### Art → engineering, CP-D r2 smooth (2026-11-10): a route's chain written flat is silently dropped

`make_audio.py` (Phase 7) and my `look.audio_routes` wrote the processor chain's keys flat on the route:
`attackMs`, `decayMs`, `envelope` and `envelopeFallPerSecond` sat beside `source` and `target`. The route reader takes
the chain only from `"chain": {...}` (`params/serialization.cpp`), so every one of those settings was ignored. No
warning was logged and `--audit-routes` showed `attackMs` 0. In draft3, then:
- the contour "flash" was the raw `beat.pulse` spike, not a peak hold;
- the path's and the pads' slews were off.

That is part of the twitch the owner saw. The routes are now nested and verified (the audit reads back their chains).

**Ask:** the route reader should refuse, or warn on, any unknown top-level key. That would match the scene loader's
key lists (ADR-278).

The smoothing itself needed no engine work. Chains (slew, spring, remap and clamp, the latching envelope) cover every
route. For B (the swell reaching the land) and E (choreographed hops), please give the outputs the same treatment:
- **No per-beat steps:** p99 frame step at most about 0.05 of range at 30 fps. `--sonic-trace --trace-finals` measures
  it.
- **Hop triggers:** at most one every couple of seconds per alien. Today the beat sets off every resting alien on every
  beat (`hop/trigger` steps the full range).

### Engineering → art, CP-D r2 (2026-11-10): feet, route keys, B and E (ADR-1990)

**Feet.** The bodies stand on the drawn tile top. A new GPU readback test holds it to 1 mm in every frame of every
landing (`test_tile_feet_gpu`).

The clipping the owner saw came from draft3's 8-spring cap, already fixed by ADR-1952. Each body rode every landing
still ringing, but the GPU drew only the newest 8 and snapped an evicted tile back up past the feet. You can see it in
`11-pass-cpd-r2/cpd-r2-feet-trace-before-after.png`; the close clip is `cpd-r2-feet-close-landings-50-60s.mp4`. Your
merge of the landing work fixed it. The original spring stays.

**Route keys.** A processor-chain key written on the route itself is now refused, naming the key. Any other unknown
key is warned about. Your da5fa4eb chains, nested, load clean.

**B: the swell on the land.** Each wave's land reach is now the parameter `env/tiles/waveN/onLand`, which you can key
per state or route.
- Author the base value in `look.tile_dynamics`, because the flight's clearance reads the authored waves. The demo
  used 0.5 on both trains, up from 0.08.
- Your `response.low → amplitude` route, with its 600/2500 ms chain and spring, measures 0.015 of its range per frame
  at p99. That is inside the 0.05 spec.

**E: choreographed hops.** The new hop settings:

| Setting | Demo value | What it does |
|---|---|---|
| `cueOnly` | 1 | Rest until a cue. Each answered cue is exactly one hop. |
| `maxWait` | 10 s | So silence is not frozen. |
| `cueEvery` | 4 | Each alien answers every 4th beat, at its own place in the cycle, so they go in turns. |
| `cueGap` | 2.5 s, live | The least time between two of one alien's hops. |
| `airBeats` | 2, live | A hop lasts two beats, launching on a beat and landing on one. |
| `hopChance` | 0 | No extra walking hops. |

Keep your `beat.pulse → hop/trigger` reaction. With the values above, each alien hops at most once every 2.5 s
(guaranteed, and tested), against 2 cues a second on Feline.

**Measured in the demo, p99 step per frame as a share of the range:**
- `hop/apex` from `response.level`: 0.055, just over the spec. It is only read at take-off, but slowing its chain to
  about 300/1200 ms brings it under.
- `breathing/amplitude` from `audio.energy`, which has no chain: p99 0.018, but there is one 0.97 jump. A chain of
  about 400/2000 ms removes it.

**Day edges.** A steady day level is your data, not an engine change: the day state's edge gain, or a key on
`env/tiles/glow/contour`. No beat flash was built.

### Engineering → art, CP-D r2 (2026-11-10): CPD-R1 is built (ADR-1965)

All of it lives in `tiles` (every tiles node carries the same spec) and in `tileDynamics.water`. Stills, in
`11-pass-cpd-r2/`:
- `cpd-r2-r1-pond.png`: a pond with an auto level on the plateau east of the bay;
- `cpd-r2-r1-dry-valley-without-with.png`: a dry valley across the ridge;
- `cpd-r2-r1-upland-stream.png`: a stream from the high ground to the sea.

Their spec is in the scratch demo, not in your scene.

```json
"tiles": {
  "rivers": [
    {"name": "river", "points": [[270, -530], ...], "width": 16, "valleyWidth": 240},
    {"name": "stream", "points": [[820, -600], [740, -540], [660, -500], [580, -470], [500, -445], [440, -430]],
     "width": 10, "valleyWidth": 36, "bedDepth": 1.5, "sourceLevel": "auto"}],
  "ponds": [{"name": "tarn", "centre": [680, -300], "radius": 18, "basinWidth": 22, "level": "auto", "shoreNoise": 0.35}],
  "valleys": [{"name": "dale", "points": [[590, -170], [700, -150], [800, -110]], "valleyWidth": 70, "depthFraction": 0.6}]
},
"tileDynamics": {"water": {"riverWaves": 0.25, "pondWaves": 0.1, "driftAmplitude": 0.35, "driftWavelength": 24,
                           "driftSpeed": 3}}
```

**Rivers**
- At most 4 rivers, each 2–32 points, 64 points together.
- `bedDepth` < 0 (the default) keeps the CP-A behaviour: the bed sits at the sea floor all along.
- With `bedDepth` ≥ 0, the source is `sourceLevel` (metres) or `"auto"` (the land at the first point, less
  `bedDepth`). The level eases down to the sea floor at the last point.
- Your `river`, `riverWidth` and `valleyWidth` still read as `rivers[0]`. Writing both `river` and `rivers` is refused.

**Ponds and valleys**
- At most 8 ponds. `level` is in metres or `"auto"`: the basin rim's lowest land, less 1 m.
- At most 4 dry valleys, 32 points together.
- A valley's `depth` is in metres; `depthFraction` (0–1), when set, is a share of the relief above the shore instead.
  A valley never goes below the shore and never holds water.

**The water body**
- The material reads the `waterBody` input: (sea, river, pond) one-hot, and w = the class (0, 1, 2).
- `tileContour` ignores it.
- `riverWaves` and `pondWaves` multiply each wave's `onWater` on that body; the sea's weight is 1.
- The drift is a ripple travelling downstream along each river.
- Onset rings still reach every body.

**Other notes**
- The flight's `follow` takes a river's name, `"riverN"` or an index.
- "Auto" levels are worked out once, on the static ground, when the scene loads.
- The tile field draws 0.6 m lower since ADR-1991 (the box's top is now the height), so a `worldPosition.y` ramp
  shifts by that much.

#### CPD-R3 (art → engineering, 2026-11-10): raised land where the route needs a wall, and islands

The owner asked for no "long endless horizons": land should rise to meet or cut the horizon in most frames.

**What data did** (`cpd-r2-land2-*`):
- `heightScale` 1.25 → 2.2;
- `inlandBand` 0.15 → 0.07, so the land rises sooner from the shore;
- `hillLow` 0.22 → 0.12;
- four dry valleys that follow the flight line, so it flies down them between walls.

By my enclosure metric, that takes the mean from 0.22 to 0.85, and the legs at 0.6 or above from 2 to 10 of 11.

**What data cannot place:**
- **Over the open sea** (the bay at 30–50 s), the far shore stays a low strip, because the hills only rise inland of
  the continent field's shore.
- **The dawn peninsula** (185–210 s, enclosure 0.41) is low land between two seas. The hills cannot stand up there
  without raising the whole world.
- **The valley budget** (4 valleys, 32 points) is spent.

**The ask: two additive terms on TileTerrain's land height, applied before the river, valley and pond carving:**
1. **`ridges`:** polylines with a `height` (metres), a `width` (metres, wall to wall) and a smoothstep cross-profile.
   They raise land along a line, for example a ridgeline beside the route or a headland running out into the bay.
   Where a ridge crosses the sea it makes land: the continent's land channel is lifted to 1 under its core.
2. **`rises`:** discs, with `centre`, `radius`, `height` and a soft edge. They raise land, and in the sea they make an
   island.

At most 8 of each would be plenty.

**Also wanted, if cheap:** a larger valley budget (8 valleys, 64 points), so the coast and the cape can have their own
side valleys.

**First uses:**
- two headlands across the bay's mouth and an island off the west shore (0–50 s);
- a ridgeline down the east side of the peninsula (185–210 s);
- a ridge behind the plateau tarn.
