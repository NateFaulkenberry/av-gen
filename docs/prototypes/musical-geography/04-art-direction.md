# Musical Geography: art direction (Phase 1, art)

Art agent, 2026-11-08, branch `scene/musical-geography-art`. This is the look the scene is built to. It stops at
Gate 1. It is written against the engineering plan (`03-technical-plan.md` on `scene/musical-geography`), and every
request at the end maps onto a system that plan already proposes.

**Revision 2 (2026-11-08), after the owner's Gate 1 answers.**
- All tiles are thin slabs with sharp edges.
- Rolling hills are the main landform.
- There are no canyon walls, mesa cliffs or buttes.
- Daylight is brighter.
- The characters are the Glowmere aliens.
- A short top-down pass is allowed.

The current stills are in `~/Desktop/av-gen-review/105-musical-geography/01-gate1/r2/`. The first-round stills are
in the folder above it, kept for comparison only.

All stills are Blender mock-ups rendered on the CPU, so the shared GPU stayed free. They are not engine frames: the
engine has no tile terrain yet. The mock-up uses the model the plan proposes:
- one height per tile;
- flat tops;
- a fixed lattice;
- colour by height.
It is a proof of the look, not of performance.

---

## 1. The identity: "the living atlas"

**Musical Geography looks like a hand-tinted relief map that has come alive and that you can fly through.**

- **Colour is height.** Every tile is tinted by how high it stands, using a cartographer's hypsometric ramp
  (Imhof, Patterson/Jenny): cool, veiled lowlands; warm, light, crisp highlands. One ramp per time of day.
  - The colour of a place therefore *is* its geography.
  - When a wave lifts a tile, the tile changes colour as it rises. The music shows in both shape and colour, with no
    extra mapping.
- **Light is line.** Emission is not scattered dots. It sits on lines that describe the land:
  - **contour lines** traced along the tile edges where the ground crosses a height interval;
  - the **wavefront** riding one wave train's crest;
  - the **path**;
  - the **river** in its valley;
  - small **lit pads** on the high hilltops.
  At night the contours become the drawing itself: a topographic map of light over a violet land.
- **The tile is the unit.** A tile is always an object, not a pixel:
  - a thin, sharp-edged slab;
  - a small gap that shows a dark under-surface;
  - per-tile value jitter of ±2%.
  Formations are always made of many tiles (40 to 200 across), so you read the unit and the land at once, the way
  aerial photographs of salt pans and greenhouses read (Burtynsky, Bernhard Lang).
- **The land is authored.** Each formation is a designed shape with its own rule: the long uplands, the river
  valley, the coastal flats, the path, the sea. Noise only warps or roughens a formation; it never creates one.

The palette is rich across the day, not within a frame. Each state has:
- one key hue family;
- its complementary shadow;
- one accent family.

Hue variety inside a frame comes from height and from one slow world-space drift, not from a rainbow.

### What I took from the references, and what I left

| Reference | Taken | Left |
|---|---|---|
| `IMG_4270` (the nav-grid overlay) | the square as a cell you can stand on; the gap; the water cells as a region | the muted debug palette; the HUD; the 2D quads |
| Tilted tiles with selective emissive tops (orange and blue) | **selective** illumination: a few tiles lit, most subdued | the black field and the neon read |
| Dot-squares in a wave | one wave read across many tiles; the colour travelling with the wave | the black void between the squares |
| Rainbow wave-tiles with depth of field | colour bands that follow the landform; shallow focus at low altitude | the uncontrolled rainbow (here each palette has one family) |
| Stepped colour landscape | colour by height; hills you read as land | the hard hue ramp of a data plot |
| Smooth teal tile sheet | the thin-sheet read and the gap; the calm of a smooth swell | the teal (cyan/teal is capped, §3) |
| Glass tiles with gold/blue edge outlines | the **edge** as the light carrier | outlining every tile (here only the edges that mean something) |
| Platform-jumper and party-game references | buoyancy, the scale of a figure against a platform | any specific game's look, characters, props, trees, gems, clouds. The characters are AV Gen's own aliens (§8) |

---

## 2. Research that shaped it

- **Aerial geography.**
  - Low raking sun (10–30°) describes relief, and high sun describes pattern. A NASA photo-interpretation study found
    a sun elevation of about 30° gave the best mix of tone and shadow.
  - So day uses a 40° sun, and sunset and dawn use 9–13° suns for long shadows across the hillsides.
  - Burtynsky and Hegen use no horizon and soft light to turn land into colour fields. That is the model for the high
    passes over the terraces and the sea.
- **Cartography.**
  - Imhof's aerial perspective in maps: lowlands farther away, cooler and lower in contrast; contrast sharpening
    toward the peaks.
  - Swiss relief shading: light sides warm, shadow sides blue.
  - Scott Reinhard's vertical exaggeration.
  - From all of these: the hypsometric ramp, contour lines as the edge-light language, warm key and cool shadow as a
    fixed rule, and vertical exaggeration on the uplands only.
- **Kinetic grids** (MIT inFORM, ART+COM's Kinetic Rain, Ikeda, Universal Everything). What separates them from stock
  "cube wave" footage:
  - the grid depicts something;
  - forms are held long enough to read;
  - the palette is restrained;
  - motion is tied to real signal structure.
  So the terrain shape stays mostly authored and still. Music moves a minority of tiles, along geographic features.
- **Colour.**
  - Gurney on golden hour, sunset and moonlight. For night: "don't go too dark overall"; cool, lower-chroma ambient;
    warm accents; chroma only in the lights.
  - Dawn and sunset are physically indistinguishable, so dawn is told apart by design:
    - clearer air;
    - paler, lower-chroma key;
    - green lowlands;
    - the sun on the opposite azimuth.
  - Pixar colour-script practice: plan the four states as a strip along the song before tuning.
- **Atmosphere.**
  - Fog colour is the sky's horizon colour, tinted toward the sun when looking into it (Quilez), with a height falloff
    so valleys and the sea fill with haze while the high tops stay crisp.
  - Night emission goes on lines, as in Vincent Laforet's night aerials, where light runs along the road networks.
- **Drone cinematography.**
  - Slow reveals.
  - A pedestal climb stands in for a cut.
  - About 24 mm for low, fast passes; 35–50 mm for high, slow ones, to stack formations.
  - Parallax from lateral travel along a wall, not from orbiting.
  - Horizon in the upper third when travelling.
  - Planet Earth's long-lens aerials and Fricke's precise, minimal moves are the tone. The Bryant Lake Bowl one-take
    is the model for "a planned route through spaces that ends on a payoff".

Main sources:
- NASA relief-lighting study: https://ntrs.nasa.gov/api/citations/19700031809/downloads/19700031809.pdf
- Patterson and Jenny, cross-blended hypsometric tints:
  https://cartographicperspectives.org/index.php/journal/article/view/cp69-patterson-jenny
- Imhof's aerial perspective, via a secondary source: https://arxiv.org/pdf/2205.14908.pdf
- Gurney on golden hour: https://gurneyjourney.blogspot.com/2008/01/golden-hour.html
- Gurney on moonlight: https://gurneyjourney.blogspot.com/2020/07/seven-tips-for-painting-moonlight.html
- Gurney on reverse atmospheric perspective:
  https://gurneyjourney.blogspot.ch/2007/09/reverse-atmospheric-perspective.html
- Quilez on fog: https://iquilezles.org/articles/fog/
- Kinetic Rain: https://www.dezeen.com/?p=228702
- Ikeda, test pattern: https://special.ycam.jp/datamatics/en/testpattern.html
- Laforet's night aerials: https://petapixel.com/2015/05/22/aerial-photographs-of-london-glowing-at-night
- DJI on zoom compression: https://store.dji.com/event/content-how-to-use-a-zoom-camera
- The ASC on Baraka: https://theasc.com/articles/baraka-a-world-beyond-words
- The Bryant Lake Bowl one-take:
  https://www.mprnews.org/story/2021/03/12/one-take-no-cg-how-that-viral-bryant-lake-bowl-drone-video-was-made

Where the sources were thin (banking angles, lit-area ratios at night), the numbers here are my choices to test, and
are marked as such.

---

## 3. The four palettes

All values are sRGB hex.
- **Land ramp:** five stops at absolute heights 0 / 5 / 12 / 20 / 30 tiles (cartographic, never renormalised per
  frame).
- **Water ramp:** four stops over the wave height, trough to crest.
- **Under-surface:** the dark sheet seen through the gaps (§4).
- **Drift:** one slow world-space gradient (wavelength about 1,600 tiles) mixed in at up to 28%, so no two valleys
  share a tint.
  - **Rule:** the drift must be *analogous* to the colours it touches.
  - In round 1, a periwinkle drift over pale-yellow highs turned them grey-khaki. Complementary mixing desaturates.
  - In daylight the drift is therefore held to the lowlands, at most 14%.

**Teal cap.** No state may use a hue between 165° and 195° anywhere but the daylight shallows. In the frame that band
must stay under 5% of the pixels.

### Daylight: "bright atlas" (revised to the owner's "brighter")

Saturated and luminous, close to the rainbow-tile references, but ordered by height: violet lowlands → orchid → coral
→ orange → yellow uplands, over cobalt water. It is one warm-over-cool scheme, not a spectrum.

| Role | Value |
|---|---|
| Sky zenith → horizon | `#3C78FF` → `#D6E2FF` |
| Fog (= horizon) / into the sun | `#C8D6FA` / `#FFF2D6`, light (fog multiplier 1.7) |
| Sun | `#FFF4E0`, 42° elevation, from the south-west, soft (1.5° disc), strength 4.6 |
| Ambient | `#A6B6FF` (0.6) |
| Land ramp | `#8A6CF2` violet, `#C26AE4` orchid, `#FF7A8C` coral, `#FFA64E` orange, `#FFD84A` yellow |
| Water ramp | `#0B36D0` cobalt, `#1C58F2`, `#3A86FF`, `#8CC6FF` crest |
| Shallows (the only turquoise) | `#2EC2D2`, within 9 tiles of the shore, mixed in at most 33% |
| Under-surface | `#1A1E66` |
| Path | `#FFF5E2` cream, unlit |
| Drift | `#8E9CF2`, at most 14%, lowlands only |
| Edge light / emissive | off. Day is a colour field; its light is the sun's |
| Grade | AgX **Punchy** look, +30% saturation, −0.1 EV. The first-round softer grade read milky, and the owner asked for brighter |

The lowest land stop was cobalt-periwinkle in a first draft of this revision. From the top-down view it merged with
the water, so it moved to violet: land and sea must never share a hue at the shoreline.

### Sunset: "ember map" (low and directional; violet shadows, coral light)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#33206E` → `#FF8A50` |
| Fog / into the sun | `#E07462` / `#FFB060` (Gurney's reverse aerial perspective: the distance glows), multiplier 1.35 |
| Sun | `#FF9A4A`, 9° elevation, from the west; long shadows across whole hillsides |
| Ambient | `#5E46B8`, strong (0.9): the shadow side is violet, never brown |
| Land ramp | `#4E2A7E` deep violet, `#8A2C78` magenta, `#D0405A` crimson, `#F2785A` coral, `#FFB45A` orange |
| Water ramp | `#24164F`, `#4A2380`, `#8A2F8C`, `#E2507A` crest |
| Under-surface | `#1E0E2E` |
| Path | `#FFD7A0`, emissive 0.6 (the first lights come on) |
| Drift | `#6A3AA0` |
| Contour edge light | `#FFC27A`, 2.0 (raised from 0.9: on rolling hills the gold lines were too faint to judge) |
| Crest light | 0.6 |
| Exposure | −0.2 EV |

### Night: "map of light" (vivid but not dark; the lines carry the chroma)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#080A2C` → `#33237A` |
| Fog / toward the moon | `#2C1F66` / `#41308E`; multiplier 0.85 (denser). The fog is violet, so the blue lines separate from it |
| Moon (key) | `#C8B4FF`, 34° elevation, weak (1.2), still casting shadows |
| Ambient | `#5A38B0` (0.6). Mid-dark, never black: Gurney's "don't go too dark overall" |
| Land ramp | `#221A6C` deep indigo, `#36228E`, `#52289E`, `#7430B0` violet, `#9C44C8` high violet |
| Water ramp | `#0E1452`, `#18227A`, `#2443B0`, `#3F78FF` electric-blue crest |
| Under-surface | `#06061E` |
| River | `#2E6BFF`, emissive 0.45 (a lit vein, not a white bar) |
| Contour edge light | `#5A86FF` electric blue, 7.0, with bloom |
| Wavefront (crest) edges | water crest colour, 5.0 |
| Path | `#FF5FB0` pink, emissive 5.0 |
| Hilltop pads | `#FF4FA8` pink, 6.0, in small clusters on flat tops above 20 tiles, about 1 in 33 five-by-five blocks |
| Exposure | +0.6 EV |

**Lit fraction target: 5–12% of the frame.** This is an artistic target to test, not a cited figure. More than 15%
reads as a neon grid.

### Dawn: "first light" (clear, pale, green lowlands, gold edges)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#A6BCDC` → `#FFD9A4` |
| Fog / into the sun | `#F2DDB9` / `#FFE2A0`; multiplier 1.35 (clearer air than sunset) |
| Sun | `#FFC874`, 13° elevation, from the east (the opposite azimuth to sunset) |
| Ambient | `#B8C8D8`, cool (0.55) |
| Land ramp | `#6FB57E` soft green, `#9CCC84`, `#DCE08E` pale yellow-green, `#F4D68C` golden, `#FBE9C4` warm cream |
| Water ramp | `#3E5E98`, `#5A7EB8`, `#86A6D0`, `#F2DCA8` gold crest |
| Under-surface | `#2A3A3A` |
| Path | `#F2A93B` golden amber, 0.25 |
| Edge light | `#FFD27A`, 0.5 |
| Crest light | 0.4 |
| Exposure | −0.2 EV |

Dawn is not re-rendered in round 2. Its round-1 still (`../05-dawn-terraces.png`) shows the palette on the old
geometry.

### The colour arc across a song (the owner asked for my recommendation)

**Recommendation: day → sunset → night → dawn.**

Why, in one sentence: it puts the brightest colour field at the opening and the scene's most spectacular language,
the night's lines of light, where songs usually peak, and it ends on dawn as a resolution that is visibly different
from the opening.

- The keys are spaced so night lands on the song's main climax (section markers from the beat clock's
  `beat.section`).
- Dawn takes the last 15–20%.
- A short track compresses the arc. It does not drop states: sunset and dusk are where the lights switch on, and they
  are the most musical transitions.

### Transitions

Driven by `dayPhase` on the timeline and never by audio (brief §7).
- Every colour above is a curve key. Interpolate in **OKLab**: sunset to night through linear RGB passes through a
  grey-brown.
- The edge and emissive gains ramp on their own curves, so the lights switch on rather than cross-fade. The order is:
  path lights first (sunset), contours (dusk), pads (night).
- Audio rides on top. It may:
  - warm the terrain (a hue push of at most ±8° toward the accent);
  - lift emissive gain (×0.6 to ×1.6);
  - light the crest.
  It never moves the phase.

---

## 4. The tile language (revised: thin, sharp slabs)

| Property | Value | Why |
|---|---|---|
| Pitch | 1 unit (= one nav cell) | Everything below is in pitches |
| Top | 0.915 square (gap **8.5%**) | Separate objects up close; a fine texture at mid distance. The gap still works with slabs |
| Thickness | **0.30** | Thin enough to read as a floating plate (the owner's slabs). Thick enough that a side face shows where neighbours step, giving the hills a readable "stair" on the shadow side |
| Edges | **Sharp**: no chamfer, no bevel, no rounded corners | The owner's call. The top's outline is now drawn only by the gap and by side-face shading |
| Under-surface | One smooth dark sheet, 1.4 tiles below the lowest neighbouring slab, in the palette's under colour | What you see through the gaps. Without it the gaps show the sky and read as light grid lines: a neon grid by accident |
| Side shading | The side face fades to 18% of the top colour at the slab's underside | The slab edge reads as a crisp lit lip over a dark gap |
| Material, land | Roughness 0.62, specular 0.5, no texture | Matte, painted. The colour does the work |
| Material, water and flooded flats | Roughness 0.16 | Sky reflections and glints. The water is tiles that shine, not a water surface |
| Per-tile variation | Value ±2.25%, hashed on the **lattice index** | Breaks the CG uniformity; must not change when the window recentres |
| Tops | Flat, one height per tile (engineering's model) | Tile = cell |
| Edge light | A band along the top's border, 8.5% of the width, sharpened (power 1.6); **per side**, only on sides that mean something (§6) | Thin lines, never full outlines |
| Emission | **Top face only** (never the sides) | Sides lit from inside read as neon bars, which round 1 showed |

**Slope and the slab.**
- On the rolling hills, the 95th-percentile step between neighbours is 0.29 tiles and the 99th is 0.43, against a
  0.30 slab.
- So on most slopes the slabs overlap in height and read as a continuous stepped sheet. Only the steepest flanks show
  a sliver of the dark under-surface, which is the floating-platform hint without walls.
- Rule for formation authoring: **keep the 95th-percentile neighbour step at or below the slab thickness.** Engineering's plan already tests a
  per-formation step bound (§4.1); this is the number.

**Distance behaviour.**
- The 0.085 gap is a sub-pixel line beyond about 200 tiles, so it aliases, and the tiles blur into texture from the
  reveal altitude.
- Request 1976 and the look requirement in §11 ask for a separation that survives distance, not a fade.
- With sharp edges, LOD 1 is the same box, so the LOD saving is only in vertices. That is the plan's lever, not
  mine.

---

## 5. The formations and the map (revised: rolling hills first)

The owner: more rolling hill geometry than canyons, and no large pillar shapes or canyon walls. Everything high now
rises as smooth land.

```
              N
   long uplands (up to ~31 tall), broad swells on a 260-tile wavelength,
   secondary hills on 85; high flat-ish tops carry the night pads
        |      ~~~ the river valley: a wide, smooth U (no walls) ~~~
   the land swells inland (gentle near the coast, high 200+ tiles in); the lit path climbs it
   coastal flats: shallow steps no taller than a slab, every third flooded (glossy)
  ~~~~~~~~~~~~~~~~~~~~~~~ indented shoreline (bays) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   the sea: two crossing wave trains + a long swell, an illuminated wavefront
              S
```

| Formation | How it reads | Rule (mock-up values, in pitches) |
|---|---|---|
| **Water** | A broad, gently moving, glossy field. The colour climbs the water ramp with the wave, so crests are lighter; at night the wavefront is a line of lit edges riding one train's crest | Trains of wavelength 34, 21 and 95 (amplitude 0.42, 0.26, 0.55), at about 70° to each other, interfering into diamonds. Crest light brightest where two trains meet. Level −0.4. Shallows band within 9 of the shore |
| **Coastal flats** | Tidal flats and paddies: bands following the coast, alternating ±7% in value, every third flooded. Strongest from above (the top-down still) | `floor(rise / 0.32) × 0.32` (steps no taller than a slab), rise = 0.05 × distance inland. The belt width varies from 15 to 90 |
| **Rolling hills (the main landform)** | Long uplands and saddles that swell inland. Never bumps, never walls. The height ramp turns them into colour bands; from the air they read as a hand-tinted relief | `1.5 + 30 × rise × big + 9 × mid`. `big` is a domain-warped fBm at wavelength 260 (warp 50), shaped by a smoothstep. `mid` is an fBm at 85. `rise` ramps from the coast to 240 tiles inland, so the land climbs as you fly in. Neighbour step p95 0.29, p99 0.43 |
| **River valley** | A wide, soft U that the hills slope down into, with a glossy river on the floor. It replaces the canyon | Height eased to the floor over 3.5–48 tiles from the centreline (`12 + 34 sin(y/62) + 13 sin(y/27)`); river half-width 3.5; river emissive at night |
| **Path** | A one-tile causeway, raised 0.3, following its own meander from the shore up into the hills. Cream by day, amber at dawn, pink and lit at night. The line characters run | Its own spline, independent of the river |
| **Hilltop pads** | Small lit clusters on the flattest high tops (above 20 tiles). The gathering places, and the pink accents at night | Flatness: all four neighbour steps < 0.25 |

**Removed in revision 2:**
- mesas;
- stepped cliffs;
- buttes;
- the canyon and its terraced walls;
- the strata banding.

**Coverage** over a typical window: water about 29%, flats 6%, hills 64%, river 1%, path 0.2%.

**Avoiding repetition at world scale.** The world is endless, so the formations recur as **biomes along the flight**.
A slow region field varies each family's parameters:
- upland height and wavelength;
- valley width;
- terrace step;
- wave direction.
A second valley is not the first one again. The camera's route is authored through the biomes (§7), not across
random ones.

---

## 6. Selective illumination

Five light carriers. Each one describes geography; none is decoration.

1. **Contours (to be decided by the owner; see the on/off pairs).** A tile's edge lights on any side where the
   neighbour across it sits in a *lower* contour band: `floor(h / 3)` differs.
   - On rolling hills this draws clean, nested isolines: rings around summits, lines following the valley.
   - Edge gain: day 0, dawn 0.5, sunset 2.0, night 7.
2. **The wavefront.** Edges on water tiles near one wave train's crest (sharpness 0.955–0.998), brightened where a
   second train crests too.
3. **The path.** Top emission along the causeway: the line the eye follows, and the one the characters run.
4. **The river.** A low emissive vein along the valley floor.
5. **Hilltop pads.** Pink clusters at night.

**Contours on and off.** "Off" keeps carriers 2–5. There are no rims any more, because the mesas went with revision 2.
The pairs in `r2/` are the same frames with only the contour edges switched:
- **Night, on.** The land draws itself as a map: summit rings and valley lines give the hills a shape you can read in
  the dark.
- **Night, off.** Two lit lines (path and river) and the pads over a dark blue field. Calmer and more graphic, but the
  hills almost vanish: without the contours, night has no landform.
- **Sunset.** The difference is smaller. The low sun already draws the hills, so the contours are a gold accent.

**My recommendation** is contours on at night and dusk (they carry the landform when the sun cannot) and off or faint
by day and at sunset. That is what the edge-gain curves above already do. The owner decides.

**Audio on light** (routes, never hard-coded):
- high band → edge gain (×0.6–1.6) and crest sharpness;
- onsets → a ring of edge light expanding from a point ahead of the camera (the plan's event ring), not a full-field
  flash;
- overall energy → emissive gain.

---

## 7. The camera's journey

The camera follows the rule in `01-coordination.md`:
- one continuous flight;
- it looks where it travels;
- no orbits, hero shots or head turns at characters;
- dives and climbs are allowed.

The owner added **one exception: a short top-down pass.**

The route is a loop through the biomes, so a long song never runs out of world. Section changes happen at musical
section boundaries. A pedestal climb or a dive is the "cut".

| Leg | Altitude (tiles) | Focal | Speed (tiles/s) | Pitch | Composition |
|---|---|---|---|---|---|
| 1. Over the sea | 3–6, terrain-following over the waves | 24 mm | 7–9 | −5 to −8° | Horizon in the upper third, hills rising in the haze ahead; wave trains cross under the lens. *r2/01* |
| 2. **Top-down over the coast** (the exception) | climb to 150–170 | 35 mm | 10–14 | pitch eases from −40° to −90° over 6 s, holds 8–12 s, eases back | The coastline, flooded flats and wave diamonds as a pure colour field: Burtynsky and Hegen, no horizon. It ends by tilting back up toward the hills. *r2/06* |
| 3. Up the valley | descend to 10–15 over the river | 24–28 mm | 9–11 | −10° | Hillsides slope away on both sides and give lateral parallax; bank into each bend at most 6–8°. *r2/03* |
| 4. The reveal | pedestal climb to 70–100 | 35 mm, eased to 50 mm over 20 s | 15–25 (reads slow) | −20 to −30° | The uplands' colour bands stacked to the haze; the path and the valley as leading lines. The big breath; the one held move. *r2/02* |
| 5. Along the path | terrain-following, 2–4 above the path | 35–40 mm | 6–8 | −5° | Fly up the path at the characters' height, passing them; never turning to look. *r2/05* |
| 6. Over the high tops at night | 25–60 | 35 mm | 9 | −15° | Contours, pads, the pink path and the river vein. *r2/04* |
| 7. Back to the sea | descend to 4 | 24 mm | 8 | −6° | The wavefront crossing at dusk or night. *r2/07*; the loop closes |

**Rules.**
- Focal length changes only during climbs and descents, and slowly: at most 1 mm/s.
- Speed is near-constant within a leg and eases at the transitions.
- Bank is at most 8°, and only from turn rate.
- The aim is along the smoothed path ahead (the plan's 2.5 s look-ahead), with a per-leg pitch offset.
- The top-down pass is the only exception, and happens at most once per loop.

**Depth of field.** Slight, and only at low altitude (legs 1, 5 and 7). Never at altitude: a tilt-shift there would
make the world a miniature, and this world must feel enormous.

---

## 8. Characters (revised: the Glowmere aliens)

The environment is the protagonist. Characters are punctuation.
- **Assets.** The owner's choice: the six Glowmere alien variants in `assets/aliens/` (scout, pilot, ranger, trooper,
  diver, elder).
  - They are **commercial**: read from `assets/`, never committed, cached or uploaded (`ATTRIBUTION.md`).
  - Their clips cover everything the brief asks for: `Running`, `Jump_running`, `Jumping`, `Landing`, `Idle`,
    `Floating`, `Walking_low_grav`.
  - `Floating` and `Walking_low_grav` already carry the buoyant quality.
- **Scale.** About **1.0 tile tall** (the mock-up scales the 1.54-unit GLB by 0.65).
  - At that size an alien stands on one slab, and a jump between slabs reads clearly.
  - From the high legs they are specks, which is correct.
  - Note: in Glowmere the cast is scaled 1.94×. Here the scale is set by the tile, not by Glowmere.
- **Count:** 3–6, never a crowd.
- **Material.** Their own (the green body reads well against every palette but dawn's greens). For dawn, consider a
  per-state tint. That is a Phase 6 decision.
- **Motion:**
  - idle `Floating` with a ±0.05-tile bob;
  - jumps with a long, slow apex (buoyant gravity scale);
  - landing pushes the slab down about 0.22. It rebounds and rings, and a faint ripple runs into the neighbours.
- **Placement:** on the path, the flooded flats, and the hilltop pads, where they explain the floating-platform idea.

*r2/05* shows two aliens on the path at scale: a scout running on a depressed slab, and a pilot mid-jump further up.

---

## 9. The brief's "explicit artistic restrictions", and how each is avoided

| Restriction | How it is avoided |
|---|---|
| Uniformly flat terrain | Hills up to about 31 tiles swelling inland, a valley, coastal steps, a moving sea; coverage targets in §5 |
| Noise as the only source of elevation | The inland rise, the valley profile, the terrace quantisation, the path and the wave trains are designed functions. Noise only shapes the uplands' plan and roughens them |
| Generic neon grids | Emission is top-only, on meaningful edges only, at 5–12% lit area, over a lit (not black) ground. Day has no edge light. The gaps show a dark under-surface, never the sky |
| Excessive cyan/teal | The hue band 165–195° is allowed only in the daylight shallows, under 5% of the frame. Night lines are electric blue (about 220°), not cyan |
| Dark or muddy materials | Night ambient is mid-dark violet at +0.6 EV. Palettes interpolate in OKLab. Shadow sides are coloured, never brown or black |
| Minecraft-like block landscapes | Thin slabs, not blocks; continuous heights with neighbour steps around a third of a tile; gaps; no textures; no grass-on-dirt sides; no walls of stacked cubes (removed in revision 2) |
| Repetitive procedural hills | Two authored scales (260 and 85) with a domain warp, an inland rise, a valley, and biome variation along an authored route; colour bands make every hill's shape legible rather than generic |
| Conventional low-poly scenery | Nothing but tiles and the few characters |
| Trees, buildings, rocks, decoration | None. Landmarks are landforms (uplands, the valley, the coast) |
| Excessive particles | None planned. If the owner wants air, use haze only |
| Busy overlays, sci-fi HUD | None. The "map" is in the land itself |

---

## 10. Requests for engineering (relay to `02-requests.md`)

Numbered in the ADR range 1970–1979. Most fit inside systems the plan already proposes. Revision 2 dropped everything
that served walls and strata.

1. **ADR-1970: contour edges as a per-instance channel.** In the effector pass, compute four per-side weights per
   tile, `side_k = [floor(h/interval) > floor(h_neighbour_k/interval)]`, from the same `heightAt` at the four
   neighbours.
   - Store them where the material program can read them.
   - Parameters: `contour/interval`, `contour/offset`, and a gain on the DayNight curve, so the owner's on/off choice
     is a parameter, not a build.
   - The same mechanism takes the fractional **crest** weight (request 2).
   - Measure the cost of the four extra height calls per tile.
2. **ADR-1971: a crest weight per tile.** As the plan's crest channel, but per wave set, with `crestSharpness`, and a
   multiplier from a second set (the brightening where trains cross). It feeds the edge mask, not the top emission.
3. **ADR-1972: the terrain palette, a little wider than §4.4 proposes.** DayNight keys for:
   - a **5-stop land ramp** with its own height stops;
   - a **4-stop water ramp** by wave height;
   - shallows colour and distance;
   - the **under-surface colour**;
   - path, river and pad colours and gains;
   - drift colour and amount (and a height ceiling for it);
   - edge colour and gain, crest gain;
   - fog multiplier, exposure offset, and a **grade saturation** key (day uses +30%).
   OKLab interpolation for all of them. Colour is from **absolute** height.
4. **ADR-1973: geography channels the material program can read:** water, shore distance, terrace index (parity and
   flooding), river, path, and flatness (for pads). Canyon-wall height and strata are dropped.
5. **ADR-1974: per-instance roughness.** Water and flooded flats at 0.16, land at 0.62, from a channel.
6. **ADR-1975: slab mode, sharp.**
   - `column` mode off, `thickness` 0.3, `bevel` 0. The plan lists these; this asks that they be the scene's
     defaults.
   - Top-only emission and the side fade to 18% at the underside in the material program, from `localPosition`.
7. **ADR-1976: tile separation by pixel footprint.** Against grazing-angle moiré, and so that the tile still reads
   at altitude. Do not simply fade the gap: keep a readable separation. See the look requirements in §11.
8. **ADR-1977: fog and the under-surface.**
   - Fog: confirm ADR-347's horizon fog supports an exponential-squared curve, a height falloff (factor 0.45–1.0, rate
     0.035/tile) and a sun-facing tint (power 6). Tie the window radius to the 0.98 distance.
   - **The under-surface (new):** something dark must sit under the slabs so the gaps never show the sky. Two
     options, engineering's choice:
     - **(a)** a coarse heightfield mesh, two tiles per vertex, following `min(neighbour heights) − 1.4`, in the
       palette's under colour, through the same fog. This is what the mock-up does;
     - **(b)** an instanced second layer, or treating the slab's underside as the dark colour and lengthening the
       sides.
   - (a) is one cheap draw.
9. **ADR-1978: world-lattice coordinates in material programs.** Drift and per-tile jitter hash on the lattice index
   and world position, never the record index or camera-relative position.
10. **ADR-1979: flight route through named features, plus the top-down exception.**
    - `scene::Flight` control points can name a formation target (follow the river spline; follow the path at a given
      height; climb to N).
    - A per-leg pitch offset and a focal-length curve.
    - **One allowed pitch override** that eases to −90° and back, for the owner-approved top-down pass. Its test should
      still assert "looks where it travels" everywhere outside that window.

**Smaller asks, no ADR:**
- The river and path splines exposed for routing the camera and characters.
- AgX tone mapping: Punchy by day, Medium High Contrast in the other states. This could be a look key per state.
- Bloom thresholded at 1.0: 0.25 by default, 0.6 at night.
- The aliens' clip names in the `hop` behaviour: `Jump_running` and `Landing` for jumps, `Floating` for idle.
- Root motion: `Jump_running` carries root motion. The mock-up had to recentre the posed mesh onto its tile, so the
  jump planner must own the trajectory, not the clip.

---

## 11. The owner's decisions

**Gate 1, round 1:**
- slabs everywhere;
- rolling hills as the main landform;
- sharp edges;
- a brighter day;
- the Glowmere aliens;
- one short top-down pass allowed.

The colour arc (day → sunset → night → dawn) is my recommendation, in §3.

**Gate 1, round 2 (2026-11-08):**
1. **Contours: on at dusk and night, faint or off by day.** Accepted as recommended. The edge-gain keys in §3 already
   do this: day 0, dawn 0.5, sunset 2.0, night 7.
2. **The bright day: "looks good".** Day palette r2 is approved as written in §3.
3. **The aliens stay green in every state,** with no tint per time of day.

### Look requirements from the coordinator's review of r2

These are binding on the build. They are look defects, not taste questions.

1. **No banding in the sky or the fog.**
   - The sky gradients band visibly in `r2/01` and `r2/07` (8-bit steps across a slow gradient).
   - The engine's sky, fog and post must not band. Fix it with output dither (triangular noise of about ±1 LSB before
     quantising), or with a sky and fog evaluated and stored at higher precision, or both.
   - Check on a frame with a large sky area at each palette state, especially the dusk and night gradients, which are
     the slowest.
2. **The tile identity has to read at reveal altitude.**
   - From high up (`r2/02`) the tiles blur into texture: the 8.5% gap is sub-pixel beyond about 200 tiles. The brief's
     "the squares remain the primary visual identity" fails there.
   - Acceptable remedies, alone or together:
     - **edge contrast that survives distance.** Darken the gap and the side lip by pixel footprint rather than
       fading them out, so far tiles keep a visible separation. This changes request 1976 from "fade" to "keep
       readable";
     - **fewer, bigger tiles in the distance.** A far ring of tiles at 2× or 4× pitch (aligned to the lattice, merged
       in whole blocks, with fog over the seam), so the square stays several pixels wide at the horizon;
     - per-tile value jitter raised with distance (±2% up close, up to ±6% far) so neighbours stay distinct.
   - **Acceptance:** at the reveal leg (70–100 tiles up, 35–50 mm) individual squares are distinguishable across the
     whole land area of the frame, not only in the foreground third.
   - This interacts with the lattice and LOD plan (§4.2 of 03-), so engineering owns the mechanism. Art judges the
     result at CP-A.

## 12. Gate 1 stills, round 2

`~/Desktop/av-gen-review/105-musical-geography/01-gate1/r2/`, 1920×1080, Blender 5.2 Cycles (CPU). The generator is
`gate1-mockup.py` in this folder. It is a throwaway, but it holds the formation rules of §5 and the palettes of §3 as
written. It uses no third-party assets except reading the aliens from `assets/aliens/` at render time.

| File | What it shows |
|---|---|
| `01-day-low-pass-water.png` | The low pass over the sea at 24 mm: glossy cobalt slabs, crossing wave trains, the hills rising in the haze |
| `02-day-high-reveal-hills.png` | The reveal: rolling uplands in the bright day bands (violet → coral → orange → yellow), the path and the valley as leading lines |
| `03-sunset-contours-on.png` / `03-sunset-contours-off.png` | Sunset over the hills and the valley, with and without gold contour edges |
| `04-night-contours-on.png` / `04-night-contours-off.png` | Night with and without the electric-blue contours. The path, river, pads and crests are in both |
| `05-day-aliens-scale.png` | Two Glowmere aliens at 1 tile tall on the path: a scout running on a depressed slab, a pilot mid-jump |
| `06-day-top-down.png` | The allowed top-down pass over the coast: flats, flooded bands, the path's start, wave interference |
| `07-dusk-wavefront.png` | Dusk (55% sunset → night) over the sea: the wavefront edges and the path lights |

Round 1 (`01-gate1/*.png`) shows the superseded column, mesa and canyon geometry, and the dawn palette.

**Creative Critic.** In round 1 it was run on the stills and evaluated nothing: loose stills give it no scene, shots
or audio. It becomes useful from CP-A on, with engine renders. The judgements here are by eye.

**What I see in the round-2 stills, by eye:**
- **Rolling hills.** They read as land, with no walls anywhere. The day bands are bright and ordered.
- **Contour pairs.** At night the difference is decisive: with contours off, the hills disappear. At sunset the
  contours are a thin gold accent, and the hills read either way.
- **Aliens (05).**
  - The scale works: one alien to one slab.
  - Where the path climbs steeply, the slabs' 0.3 sides stack into a staircase that reads close to blocks. The
    formation step rule in §4 (p95 at or below the thickness) is what prevents this. The mock-up's path climbs faster
    than the hills around it.
  - The depressed slab under the scout is too subtle to see at this angle.
- **Top-down (06).** Strong as a colour field: wave diamonds, flooded bands, the path. A dark stepped seam marks the
  shoreline, where the land steps up by more than a slab.

---

## 13. CP-A look pass on the engine (2026-11-08)

The look is now data in `examples/musical-geography/`:
- `look.py`: the states, the tile program, fog and post;
- `world.py`: TileTerrainSpec and the rings;
- `build.py`: assembles the scene and the project;
- `terrain_mirror.py`: a numpy copy of `tileSample`, for placing cameras.

The engine stills are in `02-terrain/cpa-*-art-look-r2.png`. The side-by-side comparison with the r2 mock-up is
`cpa-10-art-r2-engine-vs-blender-mockup.png`.

### Values changed to calibrate the engine (§3 and §4 stand otherwise)

**The tile and the rings**
- **Side shade:** 0.70 at the slab's underside, not 0.18. On engine hillsides, the sides facing the slope stippled
  dark.
- **Water roughness:** 0.42, not 0.16. At 0.16 the sea mirrored the pale sky over its whole surface.
- **Underlay:** takes the land's own colour at 72%, not a fixed dark colour. The dark colour speckled through every
  gap.
- **Tile border:**
  - The 1× ring has no painted border; its geometric gap draws the edge.
  - The 2× and 4× rings paint the fine lattice with engineering's `tileBorder` at 0.62.
- **Rings:** engineering's set-up. The coarse rings have no gap, and the underlay sits under the fine ring only.

**Fog**
- The fog colour is the sky's horizon colour in every state. So is the sky's ground colour, so the window's edge never
  shows as a band.
- The fog is surface-only (`volumeMaxDistance` 0), with `horizonDensity` 9 so the near ground stays clean.

**The day/night curves**
- The scene sets its own sky intensity, haze and IBL intensity per state. The engine's defaults dim the off-noon sky
  to 0.16–0.46, which left night black and gave sunset an orange-only fill.

**The states**
- **Day:** sun 3.2, grade saturation 1.45, the path lit at 0.35.
- **Sunset:**
  - a rose horizon (`#E87A86`) and a violet zenith (`#4A22B0`);
  - sun 2.0, at a paler `#FFB27A`;
  - sky fill 1.7 and fog multiplier 3.5.
  - Without these, sunset was one orange-brown mass.
- **Night:** a moon light at 2.2, driven by DayNight's `moonLight`.

**Cost.** Headless 1080p Ultra, GPU p50: day bay 26.9 ms, day reveal 28.0 ms, sunset reveal 28.1 ms. All are inside
33.3 ms; the reveal was 34.3 ms before the fog went surface-only.

### What the engine frames still miss against the mock-up

**Engineering**
- **Grazing-angle aliasing of the geometric gaps.**
  - Concentric moiré rings on the 1× ring at 50–150 m in the low views.
  - Horizontal streaks on the flat, glossy sea.
  - The 1× ring has no painted line now, so this is the slab gaps themselves.
- **Dark speckle on the day reveal's mid-ground hillside.** It looks like the coarse rings' side faces, lit as sky.

**Phase 3, not yet built**
- Crests and waves: the sea is flat.
- Contour edges (request 1970), which the mock-up's night frame is built on.

**Art, next pass**
- **The river is gone.** Engineering's formation set has none; the owner's r2 had a valley. Ask the owner whether to
  request a valley term.
- **Composition.** The engine cameras were placed by map, not composed: too much sky, and the bay view is all sea and
  low land. The flight (Phase 5) replaces them, but the CP-A stills need composed frames.
- **Sunset.** It is no longer muddy, but it reads rose rather than the mock-up's magenta-to-coral with orange crowns.
  The crowns are under 26 m here, so the orange stops never show.

### CP-A r3 (re-rendered on engineering 0c4277c2; crowns `crownThreshold` 0.88 / `crownFlatten` 0.7 so the raised land
is not capped; the 1x ring has used `tileBorder` since the r2 pass, with no painted line)

**What changed in the land:**
- **The river:** 16 points meandering north from the bay's north shore, 8 m wide, in a 240 m valley.
- **heightScale 1.25:** the crowns reach about 31 m, so the top of the colour ramp shows.
- **Neighbour steps:** p99 0.275 against the 0.30 slab (mirror measurement).

**The composed cameras** (`build.py`): `revealC`, `coastC`, `valleyC` and `nightC`, composed with `frame_preview.py`.

The stills are `02-terrain/cpa-11..16-*-r3.png`, and the comparison is `cpa-17-r3-engine-vs-blender-mockup.png`.

**Still present in the engine frames** (reported, not worked around):
- **Speckle:** dark blue dashes over the mid-ground land in the day reveal and the valley view, and horizontal streak
  rows in the sunset foreground.
- **Moiré:** gone from the coast view's flat foreground on 0c4277c2 (it was there on 5f4ecd42).
- **Ring boundary:** a visible lighter rectangle where the 1× ring meets the 2× ring (reveal lower middle; night
  mid-left).
- **Cost:** the composed reveal is GPU p50 35.2 ms at 1080p Ultra (p95 44, p99 65 on 0c4277c2), over 33.3. Pitching down fills the frame with the
  fine ring.

**Art, still open:**
- The day lowlands dominate the reveal (violet and blue), so the warm crowns show mostly on the coast ridge.
- The coast ridge's top reads olive.
- Sunset reads magenta-rose with coral crowns. The mock-up's orange crowns are not yet reached.
- No contours or crests yet (Phase 3).

### CP-A r4 (2026-11-08, on engineering 97fe0568)

**What r4 changed:**
- **Land:** `skirt` 3 on the rings, and the fine ring at 120 m.
- **Lights and AO:** moon shadows only at night, and ambient occlusion off until GTAO's distance fade lands.
- **Day palette:** golden hilltops; the land stops lowered to 0 / 3.3 / 6.7 / 10 / 20.
- **Sunset palette:** the key light is `#FFB848` at 3.2, and the fog multiplier is 5.

The stills are `02-terrain/cpa-21..26-*-r4.png`, and the comparison sheet is `cpa-27-r4-engine-vs-blender-mockup.png`.
The composed day reveal costs 23.5 ms of GPU at p50 (1080p Ultra).

**Still visible in r4:**
- **Dark dashes on the step risers.**
  - Turning AO off reduced them but did not remove them.
  - They are strongest in the day valley view's foreground, on the coast ridge's golden top, and at night.
- **The 1×/2× ring rectangle.** It is clear at night and at the bottom centre of the day reveal.
- **Sunset hilltops.** They read coral-red, with a slightly warmer far ridge; the mock-up's orange is not reached.

### CP-A r5 (2026-11-08): the contrast pass

**Day**
- The sky's fill light is now a separate, lower curve: 0.45 by day, against the visible sky's 1.0.
- Grade contrast rises per state: day 1.25, sunset 1.22.
- Day uses a low afternoon sun from the south-west (phase 0.64), saturation 1.6, −0.5 EV and thinner fog.
  - Noon had front-lit the north-west views flat.
  - The slab rule caps the hill slopes, so grazing light is the only source of side shading.
- Land roughness goes to 0.8 against glare.

**Sunset**
- The fog and sky are orange (`#FF8A50`) under a violet zenith.
- Sky fill is 1.0, down from 1.7.

**Result**

The stills are `02-terrain/cpa-31..36-*-r5.png`, with the comparison sheet `cpa-37`. The day reveal costs 23.5 ms GPU
p50.

- **Sunset:** now warm orange-red, with orange crowns and deep red lowlands. It is the closest state to the mock-up.
- **Day valley:** more saturated, with violet lowlands and orange crowns.
- **Day haze:** still more than the mock-up.
  - The low sun puts a white glare on the sea in the reveal and coast views.
  - The coast ridge's top reads olive-green again under the blue fill.
- **Unchanged:** the dashes and the 1×/2× rectangle (engineering).

### CP-A r6 (2026-11-08, on engineering 7aa5ba43)

**What r6 changed:**
- **Sun:** the sun's arc is turned (`sunAzimuthAtDawn` 3.24). The day sun now lights the north-west views from the
  south-west; the sunset sun is in the south.
- **Fill light:** a shadowless violet fill from opposite the sun (day 2.2, sunset 0.6), so shaded slopes go violet
  instead of brown.
- **Ambient occlusion:** back on, faded by distance (ADR-1951). The fade bands are per view: reveal 200–420 m, valley
  120–300 m, the others 40–120 m.
- **Shadows:** shadow range 480 m, for the low suns' long shadows.
- **Day:** manual exposure; fill 0.38, contrast 1.3; warmer gold crowns; the low end of the colour ramp cooler.

The stills are `02-terrain/cpa-41..46-*-r6.png`, with the comparison sheet `cpa-47`. The day reveal is GPU p50
30.7 ms, up from 23.5 ms; ambient occlusion, the longer shadows and the fill account for the difference.
