# Dimensional portals: the plan (Gate R)

This plan follows from `14-portals-research.md` (the audit, the references, the debate and the proofs). ADR range:
1872–1899. Media goes to `portals-a/`, `portals-b/` and `portals-c/`. Each gate stops for the owner.

## 1. The architecture

### 1.1 Worlds

A project can hold **several worlds**. A world is an ordinary scene composition, attached with its own parameter
prefix (`Composition::attach(prefix)` already namespaces parameters). There are three:

| World | What it is | Engine status |
|---|---|---|
| `canyon` | today's scene file, unchanged look | exists |
| `moon` | new `lunarWorld` block, black sky, a hard low sun | new generator (scene-specific) |
| `canyon-dusk` | the same `attractorWorld` block (the generation is cached by hash, so it is baked once), with a dusk environment, sun, water and cirrus | a second scene file |

- Every world updates every frame from the same clock, so each is a pure function of time and seekable.
- Each world that is drawn has its own `SceneRenderer` (lazily created). Its mesh uploads and temporal state then
  never churn.
- Only the current world and the one behind a visible, open portal are rendered in a given frame.

### 1.2 Portals

A portal is a scene-level record in the world it is seen from. It has:
- an entry pose (centre, normal, up);
- a target world and exit pose;
- a shape: semi-axes, a noise edge, and an `open` parameter from 0 to 1;
- perimeter styling.

The engine computes `T = exit · entry⁻¹` in double precision and derives the **virtual camera** `T·C` each frame.

### 1.3 The portal pass

A full-screen pass in the primary renderer, after the opaque lit pass and before water and the refraction copy. Per
pixel:
- ray–plane hit;
- inside the aperture and nearer than the scene's linear depth → the destination's HDR;
- write the plane's depth;
- add the rim to emission.

Water, SSR, fog, bloom, motion blur and the temporal family then treat the Moon in the aperture like any surface.
The river reflects it.

### 1.4 The crossing

The world changes on the frame the camera's signed distance to the entry plane changes sign. This is computed from the
deterministic camera, so seeks and offline renders agree. The destination renderer becomes the primary. Its camera is
already the real one.

### 1.5 Clipping the destination

Exits are placed with clear air behind them, so nothing sits between the virtual camera and the exit plane. If a
composition ever needs it, the destination's geometry passes get a user clip plane through Dawn `ClipDistances`. Not
an oblique projection: it breaks the depth readers (§2.3 of the research).

### 1.6 The corruption field

One scene-level field: a focus point (the portal), radius and falloff keyed over time, plus audio modulation.
**Every Matrix layer reads it:**
- meshWire v2 fronts emanate from it;
- the surface-code overlay;
- FXL pull, strip and clip displacements;
- the shard pass;
- mosh v2's world mask;
- DF lens strength at the aperture.

One knob drives the progression, and the layers stay coherent.

### 1.7 Generic versus scene-specific

**Engine (generic, with tests):**
- worlds;
- portals (the virtual-camera maths, the pass, the hand-over);
- clip distances (only if needed);
- the corruption field;
- meshWire v2;
- the mesh overlay passes (code and shards);
- FXL pull and strip modes;
- mosh v2.

**Scene (the Strange Attractor):**
- the journey camera across worlds;
- the `lunarWorld` generator, plus the RTIN mesher factored out of `attractor_world.cpp` so both worlds use it;
- the regolith program;
- the dusk environment;
- portal placement;
- palettes;
- the timeline;
- audio routes.

Planned ADRs (numbers indicative):

| ADR | Subject |
|---|---|
| 1872 | meshWire v2: fronts from a world origin, a palette ramp across the front, depth-varying width, RTIN-level density hierarchy |
| 1873 | the corruption field |
| 1874 | the mesh code overlay: world-anchored glyph cells, stateless rain, power-of-two cell LOD, SDF glyph strokes |
| 1875 | the mesh shard pass (in place, de-indexed per triangle) with the matching FXL clip |
| 1876 | FXL pull and strip displacement |
| 1877 | mosh v2: world mask, motion-vector drag, re-pick rate, row tears |
| 1878 | worlds |
| 1879 | portals and the portal pass |
| 1880 | the crossing hand-over |
| 1881 | the journey camera |
| 1882 | `lunarWorld` and the shared RTIN mesher |
| 1883 | the regolith program |
| 1884 | the dusk world |

## 2. Beat sheet (about 60 s, Feline Footwear from 0 s)

Timings follow the brief's suggestions, with the crossings slightly later so the formation can breathe. Exact cuts
land on bar boundaries from the beat clock at Gate A. The world positions of the portals are chosen at Gate A
(portal 1) and Gate B (portal 2).

| Time (s) | Beat | Camera | Picture | Corruption field |
|---|---|---|---|---|
| 0–8 | Canyon descent | today's opening, unchanged | today's look; one faint scan front at about 6 s, "the system wakes" | 0 |
| 8–16 | River approach | today's dive and low river run, slowing from ~80 to ~50 m/s | sparse fronts on low onsets (today's route); contour lines on highs | 0 → 0.1, far ahead |
| 16–24 | The Matrix develops | along the river toward an outside bend, ~40 m/s | fronts now sweep *back from the bend*. Wire density rises near it; the first glyph streams run down the walls closest to it; a few corrupted blocks on that wall only. The far canyon stays clean. | 0.1 → 0.45 |
| 24–30 | Portal formation | round the bend toward the wall, ~30 → 22 m/s, slight push-in | The wall ahead ruptures along its strata. Strips slide; triangles peel off as shards and stream inward; glyph streams turn and run along the surface toward the opening; the DF lens bends the rock around it. The aperture opens (`open` 0 → 1, about 26–29 s) onto black sky and grey regolith, against the warm canyon. Mosh and tears concentrate around it. | 0.45 → 1 at the wall |
| 30–34 | Crossing 1 | ~20 m/s through the aperture, level; the plane at about 32.5 s | The aperture fills the frame from about 31.5 s. The rim sweeps off with a brief corruption peak, then **instant silence**: clean regolith, no glitch. No cut. | peak, then 0 (Moon side) |
| 34–43 | Lunar exploration | low glide 15–25 m over the regolith, then a rise to reveal the horizon; long shadows toward the camera | Apollo-faithful: a black sky, a 9–10° sun, a close horizon, boulder fields, the bright ejecta of the fresh hero crater ahead; Earth low in the sky if approved | 0 |
| 43–49 | Crater portal formation | crest the hero crater's rim at about 44 s, then pitch down into the bowl | Lunar scan lines (same language, cooler gold) trace the crater; the **crater floor** dissolves into the opening: looking down through the Moon at the dusk canyon far below, river and sandstone | 0 → 1 on the floor |
| 49–54 | Crossing 2 and return | dive through the floor (pitched down about 50°); emerge from the dusk sky over the canyon, still diving; pull out over the river by about 53 s | no cut; the lunar rim sweeps off as the sky opens | peak, then a slow decay as stray glyphs fade off the walls |
| 54–60 | Dusk continuation | along the river at ~40 m/s, then a slow rise | deep violet zenith, warm residual light on the high strata only, shadowed gorge with fill, river reflecting the bright western sky | 0 |

**Why the crater-floor portal.** Both portals keep the camera's world up: portal 1 is vertical and exits on the Moon
as vertical; portal 2 is horizontal and exits into the canyon sky as horizontal. So `T` is only a yaw and a
translation each time. Immersive Portals had to add a corrective roll for tilted transforms; we avoid the problem by
design. The floor-as-window also meets the brief's "integrated into the crater", and the view down through the Moon
onto the river is the strongest single image in the sequence.

**Exits with clear air.**
- Portal 1's lunar exit stands in open air about 25 m above a plain. The virtual approach behind it is clear (the
  proof had 26 m clearance).
- Portal 2's canyon exit faces down from the sky about 400 m above the river. The virtual approach behind it is open
  sky.

## 3. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **Flicker.** Glyph aliasing, wire density changes and mosh re-picks. | Glyph LOD and SDF strokes; world-picked blocks re-picked at 3–4 Hz; the coordinator's metric on every clip; proof B's p90 0.054 is the bar to hold |
| 2 | **Uniform noise.** The field spreads until the canyon is gone. | A ceiling per layer; sky and far rims always clean; Gate A judged on stills *and* video |
| 3 | **Temporal hand-over.** The mosh ring jumps at the crossing. | The ring is plausible by construction (the aperture fills the frame first). If not, a shared post stage (a refactor, budgeted at Gate B). |
| 4 | **The visible crossing moment.** The rim sweep. | Slower approach, a lens at the rim, a corruption peak on that beat; measured |
| 5 | **Speed.** The current 80 m/s is too fast to read a portal. | The journey camera re-times from 16 s; avoid abrupt acceleration (jerk-limited, ADR-1854 trace) |
| 6 | **The Moon looks procedural.** The owner's "artificial-looking" warning. | Size-frequency law, degradation states, rubble and boulders around fresh craters, a 1 m mesh near the path, lunar-Lambert with phase-dependent L, knife shadows; optional LRO calibration |
| 7 | **Lunar shadows.** A grazing sun stretches facets into teeth (ADR-1867's lesson). | A 1 m watertight RTIN near the path; caster reach for long shadows |
| 8 | **Dusk crushes the gorge** (the Nocturne precedent; proof 10 at 9 s). | Fill lights and sky, river reflection contrast, exposure; judged at Gate C |
| 9 | **Cost.** Two worlds rendered on portal frames; cold compiles of the programs (26 s seen). | Offline first (soft constraint); scissor the destination render to the aperture's bounds later |
| 10 | **Water seam and SSR.** The portal reflected in the river suffers the known screen-edge seam. | Accepted limit (owner, 2026-10-30) |
| 11 | **The canyon opening degrades.** | Before/after frames of the opening at every gate; the corruption field is 0 there |
| 12 | **Scope.** Many layers. | The owner's priority: two excellent portals and an impressive breakdown before secondary effects; volumetric code and Earth are optional |

## 4. Gates

### Gate A: the Matrix effect, top priority (`portals-a/`)

1. **The journey camera, canyon part.**
   - Re-time and extend the flight from 16 s toward an outside bend with a tall wall facing upstream (chosen from the
     river polyline).
   - Slow to about 22 m/s by 30 s. Hold the opening 0–16 s exactly.
   - Measure smoothness with the existing camera trace.
2. **The corruption field (1873)** at the chosen wall point, keyed per the beat sheet and modulated by `response.bass`.
3. **meshWire v2 (1872).** Fronts from the field's focus; the copper → amber → gold ramp; depth width; RTIN-level
   hierarchy, so the wide shots get coarse fronts.
4. **Surface code (1874).** Our procedural glyph set as SDF strokes, cell LOD, stateless rain, flow toward the focus,
   water excluded.
5. **Geometry (1875, 1876).** Pull and strip displacement; clip plus shards peeling from an advancing edge around the
   (closed) rupture; reconstruction where the front passes.
6. **Mosh v2 (1877)** masked by the field, plus DF lens at the rupture.
7. **Deliverables.**
   - A render of 0–32 s with sound, ending at the closed rupture. There is no portal yet: the rupture shows as a dark
     void edged in fragments.
   - Stills of each layer on and off.
   - Before/after frames of the opening (0–16 s).
   - The flicker metric and a frame-time log.
   - ADRs and tests for each engine piece; both suites at exit 0.
   - STOP.

### Gate B: portal 1 and the Moon (`portals-b/`)

1. **`lunarWorld` (1882).**
   - The generator from the proof, made deterministic in C++.
   - The shared RTIN mesher; a 1 m mesh near the path; the skirt and curvature.
   - The hero crater; boulders and rubble as procedural instances.
   - The regolith program (1883) with L(α) and the surge.
   - Hard shadows. Earth and stars per the owner's answers.
2. **Worlds (1878), portals and the portal pass (1879), and the crossing (1880).** Engine tests:
   - `T` round-trips;
   - the virtual camera at the crossing equals the real one;
   - the pass writes depth;
   - aperture coverage is analytic against the CPU twin;
   - the world switch is deterministic under seek.
3. **Portal 1 in the wall.** The rupture from Gate A opens; the river reflects the aperture; the rim lens; the
   crossing beat.
4. **The journey camera, lunar part.** From the exit through the reveal to the hero crater's rim (about 44 s).
5. **Deliverables.**
   - A render of 0–44 s with sound.
   - Crossing frame strips and the metric across the crossing.
   - Lunar stills against Apollo reference framing (by eye, no ingest).
   - Before/after frames of the opening.
   - Both suites at exit 0.
   - STOP.

### Gate C: portal 2, dusk and the whole sequence (`portals-c/`)

1. **The crater-floor portal.** Lunar scan lines in a cooler gold; the floor dissolves into the opening onto the
   dusk canyon seen from above.
2. **The `canyon-dusk` world (1884).** Sky, sun, cirrus, water, fill; the gorge must not crush.
3. **The journey camera, return part.** The dive out of the dusk sky, the pull-out, the river run to 60 s.
4. **Deliverables.**
   - The full ~60 s offline render with sound.
   - The metric over the full length and at both crossings.
   - Before/after frames of the opening.
   - Both suites at exit 0.
   - `PROGRESS.md` and the ADR index updated.
   - STOP.

## 5. Decisions for the owner

1. **Portal 2 as the crater floor**, looking down onto the dusk canyon, and emerging by diving out of the canyon sky.
   The alternative is a vertical aperture in the crater wall, which needs a corrective roll or a level exit.
2. **Earth in the lunar sky?** If yes: NASA Blue Marble (public domain, to download and keep out of git), or a
   procedural Earth.
3. **Stars.** Apollo-faithful none, or a faint cinematic field?
4. **LRO data.** May I download the LROC NAC DTM of Linné (2 m/px, about 134 MB) for calibration only? My
   recommendation: optional; the procedural Moon can stand without it.
5. **Glyphs.** Our own procedural glyph set on the rock (copper and amber), or a purely geometric breakdown with no
   glyphs?
6. **Speed.** The sequence slows to about 20–30 m/s at each crossing (from today's 80 m/s). Is that acceptable?
7. **Live.** On portal frames two worlds are rendered, about double the cost. Live is not targeted in this pass.
8. **Length and audio.** Feline Footwear 0–60 s uncut, crossings on bar lines?
