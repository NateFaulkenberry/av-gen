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

### Day pass after r6 (r7 check)

The coordinator reviewed r6: sunset and night are kept; day was dominated by blue-lavender.

**Changes, day only:**
- **Land ramp:** magenta-orchid lowlands and gold hills: `#A848C8`, `#DC56AE`, `#FF7486`, `#FF9442`, `#FFC03A`.
- **Height stops:** now `stops` per state. Day uses 0 / 2 / 4 / 6 / 12 m, so the gold covers whole hill masses, not
  only the crests. Sunset and night keep 0 / 3.3 / 6.7 / 10 / 20 m.
- **Blue cast:** the sky fill is down to 0.25. The horizon and fog are a neutral lavender-white (`#C4C4F4`) instead of
  pale blue. The violet fill is now magenta-violet (`#C060FF`) at 1.4.

**Checks:** `cpa-41b` and `cpa-42b` (`02-terrain/`). Blue now stays on the water: the bay and the river.

**Cost, and which lever goes first.** The r6 reveal is 30.7 ms of the 33.3 ms budget, which leaves about 2.6 ms for
Phase 3. Cut in this order:
1. **Shadow range:** 480 m back to about 320 m. That saves about 0.5 ms. The long shadows matter most in the middle
   ground, and they fall off past 300 m.
2. **AO fade band:** pull it nearer, for example 120–260 m on the reveal. On the reveal, AO pays mainly for detail at
   the near edge.
3. **Violet fill:** fold it into the sky fill colour if the engine exposes one. This needs a request.
4. **High live tier** for the reveal leg only.

### CP-A r7 (2026-11-08, on engineering de9e8ce6)

**Changes:**
- **Contact shadows off.** They were the dark dashes.
- **The 1× ring paints the same prefiltered tile grid as the 2× and 4× rings** (tileBorder 0.72). Before this, the 1×
  ring painted no grid lines, which is why it read lighter and showed as a rectangle.
- **The day pass from the r7 check** (magenta lowlands, gold hill masses).

The stills are `02-terrain/cpa-51..56-*-r7.png`, with the comparison sheet `cpa-57`. The day reveal costs 23.2 ms GPU
at p50.

**Result:**
- **Day reveal and valley:** the dashes and the rectangle are gone.
- **Still showing:**
  - **Night:** the 1×/2× rectangle is still visible, a lighter rectangle in the lower left.
  - **Coast ridge:** a scatter of bright orange flecks on the sunlit risers.
  - **Sunset valley:** light flecks in the foreground. These are risers catching the low sun, not dark dashes.

### After r7: night grid strength and the 1×/2× pairs (while the owner reviews CP-A)

**Night grid:**
- The 1× grid strength is now per state; night uses 0.5.
- On the GPU check (`02-terrain/cpa-r7check-night-grid-0.5.png`), the night rectangle is quieter but still visible.
- What remains is a texture difference, not a brightness one. The 1× ring shows real dark gap lines under the moon,
  while the 2× ring has only painted ones. More grid strength won't remove it; engineering should look.

**The other states' 1×/2× pairs:**
- **Day and sunset:** no rectangle in r7.
- **Dawn** (`cpa-r7check-dawn-reveal.png`): no rectangle either.
- **Dawn has not been tuned on the engine yet.** It reads washed out and grey-green, the way day did before r5–r7.
  It needs the same pass: side light, lower fill, manual exposure and its own height stops.

**Kept for Phase 4: crisper tiles at mid distance.**
- In the engine the tiles read softer at mid distance than in the mock-ups, which show a crisp square texture there.
- Levers to try:
  - a stronger `tileBorder` on the 2× ring;
  - a slightly larger per-tile value jitter at distance;
  - a sharpen in post (`post/output/sharpen`).
- Judge each one against moiré.

### CP-A r8 (2026-11-08, on engineering 20077b4c: the side ease now reaches live frames)

**Side threshold (`slabSidePixels`) per state:**
- Sunset 26. It kills the low-sun flecks.
- Night 12.
- Day and dawn use the engine default (12 px). At 18 the day hills lost their tile texture and read as smooth
  gradients, which breaks §11's "tiles read at reveal altitude".

**Dawn:**
- The day treatment: its own height stops, fill 0.3, contrast 1.25 and thinner fog.
- Fresher teal-green to gold to cream colours, and a bluer zenith.

The stills are `02-terrain/cpa-61..68-*-r8.png` (six frames plus two dawn), with the comparison sheet `cpa-69`.

**What the r8 frames show:**
- **Day and sunset:** clean. No flecks, dashes or rectangle.
- **Day after the fix:** the mid-distance tiles read more softly than before. This is the Phase 4 crispness item.
- **Night:** a faint dash texture under the moon. The rectangle is barely visible.
- **Dawn still falls short.** It reads hazy and olive, because its sun sits low in the north-west, straight ahead of
  these views. It is back-lit into haze. One day/night arc cannot give both day and dawn side light on the same
  cameras, so dawn needs either its own camera headings or the dawn sun moved (a request for a per-key sun azimuth).

**Cost:** the day reveal is GPU p50 23.9 ms, with p95 31.6 and p99 37.6 ms. The spikes appear in this run only; it is
worth re-measuring when the GPU is quiet.

### CP-A approved by the owner (2026-11-09)

The approval covers the r7 and r8 stills.

**Still to do from CP-A:** dawn gets re-lit from the side once engineering's per-state sun direction lands, and only
cpa-67 and cpa-68 are re-rendered.

**Phase 3, the art side:**
- **Crest light on the wave trains**, from engineering's crest weight (request 1971).
- **Contour edges** (request 1970):
  - on at dusk and night (sunset gain 0.6, a dusk key at 4.0, night 7.0);
  - faint or off by day, as the owner approved.
- **Cost:** about 10 ms of headroom over the r7 reveal (23.2 ms of 33.3 ms).
- **The end-of-Phase-3 clip** is made together with engineering.

### Tile-scale test (2026-11-09; owner: "the squares read way too small")

**What the test held fixed:** the tile scaled 4× and 8× (pitch, size, thickness, gap ratio, skirt, underlay), with the
camera heights, the land and the ring distances unchanged in metres. Also one frame flown lower (about 25 m) at 1×.
`build.py --tile-scale S` reproduces it.

The stills are `02-terrain/tilescale-*.png`, with the sheet at `tilescale-sheet.png`.

**GPU p50 at 1080p Ultra:**

| | Reveal | Valley |
|---|---|---|
| 1× | 23.5 ms | 23.0 ms |
| 4× | 12.3 ms | 13.9 ms |
| 8× | 12.7 ms | 14.0 ms |
| 1× flown at about 25 m | 20.5 ms | |

**What the frames show:**
- At 4× and 8× the squares read clearly from the reveal and valley heights. This is the reference feel the owner is
  asking for.
- Flying lower at 1× doesn't fix the problem: past the foreground the tiles still collapse into texture.
- At 8× the hills read as large stairs. The block-world risk is real there.
- 4× keeps the land rolling, with visible steps that stay readable.

**What breaks at a larger tile:**
- **The coarse rings.**
  - The 2× and 4× rings are 2× and 4× the *tile*, so at 4× they become 8 m and 16 m blocks.
  - With painted 4 m sub-squares inside, they read as "blocks of tiles" with big steps on the hills.
  - This is not an engine assumption. The ring distances are mine and are set in metres, so they should scale with
    the tile.
  - **Fix (art):** push the fine ring out, to about 240–300 m at 4×, so the coarse rings sit in the haze. The cost will
    rise from 12 ms toward the old figure.
- **River and path:**
  - At 8 m wide, the river is only 1–2 tiles. It needs about 16 m at 4×.
  - The path is already scaled (1.5 tiles).
- **Engine:** nothing found hard-coded to 1 m cells. tileBorder, the skirt, the underlay and the rings all followed
  the scale.

**Recommendation:** 4 m tiles (4×). Set the fine ring to about 240–300 m, the river to about 16 m, and keep the camera
heights. Not 8×, which looks stair-stepped and blocky. Not flying lower, which doesn't fix the reveal.

### Phase 3 look (2026-11-09, on engineering aa751fdf)

**The moving ground, authored for restraint** (`look.tile_dynamics`):
- **Waves:** two crossing wave trains on the water.
  - Wavelengths 46 m and 29 m, speeds 6 and 4.5 m/s, 60° apart.
  - The land takes 8% of them.
- **Breathing:** amplitude 0.12 m, period 18 s.
- **Drift:** a slow roll (0.4).
- **Rings:** amplitude 1.6, speed 45 m/s, decay 2.4.
- **Overall amplitude:** 0.35, with `response.low` adding up to +0.6.
- **Onsets:** `response.onset` drops a ring 150 m ahead (`env/tiles/rings/ahead`).
- **The underlay:** `childrenMin`.

**Light, through `tileContour`** (my programs; engineering's stand-in crest is not used):
- **Contour lines:**
  - Every 3 m, on land only. The sea's waves crossing 0 m had drawn closed blobs, so the water channel masks them out.
  - Gains by state: day 0, dawn 0.5, sunset 0.45, night 7.
- **Crest light:**
  - Peaks only (smoothstep 0.72–0.98). A broad crest lit the two crossing trains as a pink diamond grid on the sea:
    the neon-grid trap (`03-dynamic/art-p3-sunset-reveal-REJECTED-crest-grid.png`).
  - Gains: sunset 0.35, night 1.4.

**Cost:** the sunset reveal is GPU p50 25.1 ms (1080p Ultra), inside budget.

**Media** in `03-dynamic/`:
- stills: `art-p3-*.png`;
- clips: `art-p3-clip-sunset-reveal-feline-30-40s.mp4` and `art-p3-clip-night-feline-30-40s.mp4`.

**Open:**
- In the sunset frame the crest peaks read as scattered pink spots on the sea. That is acceptable, but a sharper or
  thinner crest may read better as moving lines.
- The rings are hard to see from these cameras: the clip's onsets land 150 m ahead and are faint at that height.
- The tile scale is still pending the owner's answer. Nothing here depends on it.

### 4 m tiles (owner, 2026-11-09) and the CP-B route (Phase 5)

**The baseline is now 4 m tiles** (`build.py` defaults to `--tile-scale 4`):
- the fine ring at 270 m;
- the river 16 m wide;
- contours every 4 m;
- longer waves (64 m and 44 m) and rings (24 m);
- `aoRadius` 1.2;
- crests cut at dawn as well as sunset;
- the skirt and `childrenMin` scale with the tile.

Four-state stills are `02-terrain/cpa-4m-*.png`. The day reveal is GPU p50 20.1 ms.

**The CP-B route** (`build.FLIGHT`, in metres):
1. the bay at 12 m;
2. low over the moving sea at 6 m;
3. the river mouth;
4. up the valley on the river's own bends, at 10–13 m;
5. the climb to the reveal, at 80–95 m.

Camera settings:
- `pitchMax` 0: it never looks above the horizon, so the climb keeps the land in frame.
- `pitchBias` 0.22 and `lookAhead` 3 s.
- Bank at most 8°.
- No orbits and no hero shots.

**The review flight:** `05-camera/art-p5-flight-day-4m-feline-30-90s.mp4` (day, Feline 30–90 s). The 1,800-frame
offline render ran at 40 fps. Live profile: GPU p50 13.7 ms (1080p Ultra), measured in the profiler's default
window.

**Open:**
- At the top of the climb, the tiles end before the horizon. A pale band of sky-ground sits above the land's edge.
  Engineering is extending the ground to the horizon.
- The top-down leg waits for engineering's leg type.
- The path isn't on this route. A later leg can follow it.

### CP-B (2026-11-09, on engineering daeb4d0a)

**Switched on:**
- `horizonFill` 1;
- the 64 m far ring to 4.5 km (`world.ring_nodes`; the 16 m ring's `align` is now 4; camera far 200 km);
- `aoRadius` 1.0.

**The route** (`build.FLIGHT`, 90 s from 30 s):
1. the bay at 12 m;
2. low over the sea at 6 m;
3. a shore snap (radius 40 m);
4. `follow: "path"` 0–0.5 at 12 m;
5. a climb to the top-down pass at 150 m, about 37 tiles wide in frame;
6. a dive east;
7. `follow: "river"` 0.55–0.9 at 12 m;
8. the climb to the reveal at 90 m.

**Night crest cut:** on 4 m tiles it lit whole tiles as white blocks on the sea. The crest is now off in every state;
the contours and the path carry the light.

**The renders** are in `05-camera/`:
- `cpb-day-feline-30-120s.mp4`;
- `cpb-night-feline-30-120s.mp4`;
- contact sheets `cpb-*-contact-sheet.png`.

**Live perf over the whole route** (1080p Ultra, headless, 88 s, 5,280 frames):

| | GPU p50 | p95 | p99 | Frame p99 (serial) | Worst |
|---|---|---|---|---|---|
| Day | 14.1 ms | 21.4 | 23.9 | 30.4 | 51.1 ms |
| Night | 13.7 ms | 20.5 | 23.3 | 25.3 | 31.9 ms |

- **Day:** 5 serial frames go over 33 ms, at about 14–33 s into the route, which is the path leg and the climb into
  the top-down pass. GPU p99 is 23.9 ms everywhere.
- **Night:** nothing goes over 33 ms.

### The camera rule for this scene (owner, CP-B, 2026-11-09). It is binding.

Owner, on CP-B: *"the camera ends up soaring too high in the sky… I don't want anything that makes the squares seem
super tiny"*, and *"prevent doing those camera movements where it looks straight down at the ground, try to keep
things soaring, plane-like, low to the ground."*

1. **Tiles always read as tiles.**
   - **Ceiling: 40 m** above the ground. Cruise is 6–12 m; rises go to 20–35 m.
   - **Minimum on-screen tile size, at 1080p with 4 m tiles:** at least 16 px at the frame centre, even at the
     ceiling. Cruise gives 40–60 px at the centre, and 100 px and more in the lower third.
2. **Fly like a low plane, not a drone.**
   - **No top-down or straight-down looks, anywhere.** The nadir leg is removed.
   - **Pitch** stays between −14° and 0°: a forward horizon view with a gentle downward look at most (`pitchBias`
     0.08, `pitchMax` 0).
   - **Banked, sweeping turns:** bank up to 16° (`bankGain` 2.2).
   - **Smooth terrain-following rises and falls:** `climbRate` 6, `fallRate` 10.
3. **Already standing:** the camera looks where it travels, with no orbits, hero shots or head turns.

**CP-B r2 route:**
1. the bay at 12 m;
2. the sea at 6 m;
3. the shore at 9 m;
4. along the path at 10 m;
5. a rise over the crowns at 28 m;
6. a banked sweep at 22 m and 18 m;
7. up the river at 10 m;
8. a last rise to 35 m.

**Night river glow:** the night water glows faintly in its crest colour (`water_emit` 0.35), so the river reads as a
lit vein. The sea glows with it.

**Renders:** `05-camera/cpb-r2-{day,night}-feline-30-120s.mp4`, with contact sheets.

**Whole-route perf** (1080p Ultra, 88 s):

| | GPU p50 | GPU p95 | GPU p99 | Frame p99 (serial) | Worst frame |
|---|---|---|---|---|---|
| Day | 15.5 ms | 18.5 | 20.6 | 43.0 | 74.8 ms |
| Night | 12.3 ms | 15.3 | 16.5 | 21.0 | 32.6 ms |

### CP-C r2: the inhabitants (2026-11-09, on engineering 3a8df7cf)

**The cast:** `build.py --characters` (`CAST`, `PREFS`, `SPRING`) places 14 Glowmere aliens along the whole r2 route,
each with its own hopping habit:
- 3 on sea slabs, preferring water;
- 4 on the shore and the path (`preferPath` lowered to 0.3);
- 4 on the hills and the sweep, preferring crowns;
- 3 on the river banks.

Each sits 8–16 m off the flight line, so it crosses the lower frame. The camera rule is unchanged: they are passed,
never framed.

**Hop:** chance 0.5, home radius 30 m, goals 2–5 tiles, apex 3 m, landing strength 1.2.

**The landing spring at 4 m tiles:** depth 1.1 m, frequency 1.4, damping 0.16, ripple 16 m/s over 14 m. The ripple
rings read at cruise height (the day sheet at +43 s).

**Renders:** `06-characters/cpc-r2-{day,night}-feline-30-120s.mp4`, with contact sheets.

**Whole-route perf** (1080p Ultra, 88 s):

| | GPU p50 | GPU p95 | GPU p99 | CPU work p50 | Worst frame |
|---|---|---|---|---|---|
| Day | 14.3 ms | 16.8 | 18.4 | 2.1 ms | 31.6 ms |
| Night | 14.7 ms | 17.3 | 19.0 | 3.0 ms | 33.0 ms |

**Open:**
- At night the aliens are dark silhouettes and hard to read off the path. A faint self-glow or rim, in the night
  accent, would fix it (this is mine to do).
- The river-bank and crown aliens are seen less often than the sea and path ones, because the flight is higher and
  turning there.

### CP-C r3: night glow and closer hill aliens (2026-11-09, on c4159a48)

The coordinator reviewed r2 and approved the sea, shore and path legs. This pass made the two proposed fixes.

**Night.** The aliens' own emissive lamps are lifted (`emissiveBoost` 2 at night, 1 by day). On their own they lit only
small yellow and cyan spots; the body stayed a silhouette. So each alien also carries a small shadowless point light,
riding its node at 1.5 node units (about 2.9 m) above the feet: `NIGHT_HALO`, the night path pink `#FF4FA8`,
intensity 60, range 7 m. It puts a pink rim on head and shoulders and a faint pink pool on the slabs around the
feet, so each alien reads off the lit path and shows as a pink beacon on the dark hills. The intensity was chosen
between probes at 40 (too faint on the hills) and 150 (the helmet blew out pink).

**Hills, sweep and river** (clip +43 to +79 s). These legs fly higher and turn, so their aliens were specks.
- **The aliens moved.** The nine aliens on these legs (two added, 16 in all) now sit 6 m off the flight line, or
  11 m on the river leg so they stay off the water. They are on the inside of each turn, the side the camera banks
  toward: right through the crowns and the sweep, left down to the river and onto it. Their positions come from a
  numpy mirror of the flight table (Catmull-Rom path plus the Gaussian smoothing), in the scratch script `track.py`.
- **They stay put.** `HOME_RADIUS` is 6 m for the crown and bank aliens (it was 30 m), so they cannot hop away from
  the line.
- **The flight is lower, with the same shape.** The three waypoint altitudes dropped: 28→22, 22→17 and 18→14 m.
  No XZ position changed.

**Result:**
- An alien is in frame in almost every hills, sweep and river frame, near the centre, at roughly 60–100 px tall at
  1080p, where r2 had specks of about 10 px.
- The closest passes are at +71, +73 and +78 s, where the alien is clearly a figure.
- On the hills (+43 to +55 s) they are smaller, because the camera still sees them 50–80 m ahead. The next lever, if
  wanted, is lowering the crown waypoint further, to about 14 m.
- One gap remains, around +60 to +66 s: the sweep's return leg has no alien in frame.

**Renders:** `06-characters/cpc-r3-{day,night}-feline-30-120s.mp4`, with full-run sheets and the hills, sweep and
river sheets (`cpc-r3-*-hills-sweep-river-sheet.png`).

**Whole-route perf** (1080p Ultra, 88 s, 16 aliens; r2 in brackets):

| | GPU p50 | GPU p95 | GPU p99 | CPU work p50 | Worst frame |
|---|---|---|---|---|---|
| Day | 15.1 ms (14.3) | 17.6 (16.8) | 19.2 (18.4) | 2.9 ms (2.1) | 36.5 ms (31.6) |
| Night | 15.1 ms (14.7) | 17.7 (17.3) | 19.7 (19.0) | 3.1 ms (3.0) | 39.0 ms (33.0) |

Day also grew by about 0.8 ms, and the day scene has no halo lights. So most of the growth comes from the lower legs,
where there is more near ground in frame, and from the two extra skinned aliens. The 16 halos add about 0.4 ms at
night.

### CP-D groundwork: the whole song as one flight, through the arc (2026-11-09)

The owner approved CP-C r3 "for now". This is the first draft of the full piece:
- the whole of Feline Footwear (210.5 s) as one continuous flight, with no cuts;
- the Gate 1 colour arc (day → sunset → night → dawn) changing in flight.

The audio reaction is left alone for now, because engineering is wiring Phase 7.

Build it with `build.py --full --characters`. The arc is defined in `arc.py`.

**Sections** (measured from the audio's energy bands, because the beat clock's sections are not available to a
script):

| Time | Section |
|---|---|
| 0–8 s | intro |
| 8–28 s | build |
| 28–112 s | main |
| 112–130 s | the lift (highs rise) |
| 130–184 s | the climax (the highs peak) |
| 184–202 s | outro (highs, then lows, drop away) |
| 202–210.5 s | fade |

**The arc on the timeline** (`arc.ARC`). Night lands on the climax, and dawn takes the outro (§3).

| Time | State |
|---|---|
| 0–66 s | day |
| 66–100 s | day → sunset |
| 100–108 s | sunset |
| 108–128 s | sunset → night, through dusk |
| 128–180 s | night |
| 180–196 s | night → dawn |
| 196 s to the end | dawn |

**How the arc is driven.** There is no engine change. 1972's terrain curves were never built, so everything is
timeline tracks: 118 tracks, all "live" in `--audit-routes`.
- **`env/dayNight/dayPhase`:** this moves the sun and every DayNight curve. Its keys run forward only: 0.61, then
  0.72, then 1.0, which is the same as 0.0, then 0.27. The ring seam is crossed with a one-frame `step` key, and
  both sides of the seam are night.
- **One program per ring for the whole song:** `mgTile{1,2,4}x_arc` and `mgUnder_arc`, built with
  `look.tile_program(always=True)`. The night water glow and the contour band are always present, at gain 0 when
  off, so the structure never changes (this is asserted). The timeline keys each op's constants:
  `material/<prog>/op/<i>/<kind>/<field>`.
- **Everything else DayNight does not own is keyed too:**
  - the post grade (exposure, saturation, contrast, bloom);
  - the ambient (`env/intensity`);
  - the violet fill light, now present all song with its intensity keyed;
  - the far ring's colour;
  - the aliens' `emissiveBoost` and their night halos.
- **Blending:**
  - Colours mix in **OKLab**, at the DayNight curves' own smoothstep, so the land turns with the sky.
  - The lights switch on rather than cross-fade. Toward night, the path lights come first (0–50% of the transition),
    then the contours (30–75%), then the pads and halos (55–100%). Toward dawn they go off in the reverse order.
- **Keys:** the arc is sampled every 1 s through each transition. Holds are two keys.
- **Fixed for the whole song:** `slabSidePixels` is structural, so it is 12 throughout (the night's value; day was
  0 and sunset 26). The moon light casts shadows all song, because a light's shadow flag is not keyable.

**The route** (`FLIGHT_FULL`, starting at 0 s and running 210.5 s).
- **The approved CP-B/C legs keep their shape and their song time** (30 s to about 128 s), with two changes:
  - The crown waypoint is lowered 22 → 14 m, and the sweep 17 → 15 m (the coordinator's weak spot 1).
  - There is a new alien on the sweep's return leg at 92 s (weak spot 2).
- **New legs:**
  - **0–30 s (intro, day):** a long, easy S north up the open bay over the moving sea, from (−60, 380).
  - **About 128–184 s (night, the climax):** out of the river valley, east along the north coast's low hills under
    the contour lines, low across the inlet's moving sea, and onto the far shore.
  - **184–210 s (dawn):** bank north along the east shore, down the low peninsula, and out over the northern sea.
- **The eastern ridge was dropped.** It is 1.5 km past the river end and out of the song's reach at plane speed.
- **Altitudes** stay at 8–30 m, under the 40 m ceiling. There is no nadir, and the camera looks where it travels.
- **Aliens:** 28 in all. The 16 from CP-C r3 are kept, and 12 are added: 2 on the bay intro, 1 on the sweep's
  return, and 9 on the night and dawn legs. Each sits 7 m off the line, on the inside of its turn.

**Render:** `10-full/cpd-draft-feline-full-1080p.mp4`, the whole song with audio, plus `cpd-draft-sheet.png` (a frame
every 8 s).

**Whole-song perf** (1080p Ultra, 208 s, measured on its own run):

| GPU p50 | GPU p95 | GPU p99 | CPU work p50 | Worst GPU frame |
|---|---|---|---|---|
| 15.1 ms | 17.5 ms | 19.4 ms | 3.3 ms | 28.7 ms |

88.7% of frames are within 16.7 ms.

The first profile, which ran straight after the 5-minute render, read p50 15.5 and p99 46.9, with p90 at 40 ms. It
did not reproduce in separate 10 s windows across the whole song (p99 18–20 ms in every window), in a 120 s run, or
in the repeat 208 s run. I read it as a thermal or contention artefact of running right after the render, not the
scene.

**Findings and open points:**
- **The per-state fog density has never applied (a silent no-op).** `look.fog_medium`'s `volumeDensity` (`fog_mult`)
  is overwritten by DayNight. Our `day_night()` sets no `volumeDensity` curve, so `applyDefaults` fills it with the
  Tree of Life defaults: noon 0.000158, sunset 0.000341, midnight 0.000225. `applyDayNight` then writes that over
  `env.volumeDensity` every frame. Every approved look (CP-A to CP-C) was made under those default densities, so I
  have left it alone. If we ever want `fog_mult`, it belongs in a `volumeDensity` curve.
- **Dawn reads flatter than the other states:** an olive-gold haze, from the approved dawn palette under the low
  north-east sun. It may want its own pass once the owner has seen it in sequence.
- **Underlay cracks:** a thin teal line is visible in the 200 s dawn frame. It looks like the underlay showing
  through a crack between slab rows, and the dawn underlay colour makes it visible.
- **The intro is 30 s of open sea.** It is quiet on purpose, under the intro and build, but the owner may want land
  sooner.

### CP-D draft2: the coordinator's four notes on a036ac93 (2026-11-09)

**1. The intro.** 0–36 s had been the same open sea in every frame.
- **The new intro:** the bay opens the piece, then the flight banks onto the bay's **west shore**, reaching it by
  about 11 s, and runs along its hills with the bay on the right (12–20 s). It then swings east across the bay to the
  approved route.
- **The approved legs** keep their shape. They now run about 10 s later: the bay waypoint is reached at 40 s instead
  of 30.
- **The arc stays on the song's sections, not the route,** so a state now lands about 10 s further along the ground:
  - sunset falls on the crowns and the sweep;
  - night falls on the river (whose water glows at night);
  - the climax runs from the valley exit across the north coast and the inlet;
  - dawn comes in on the far shore, and the piece ends on the low peninsula.
- **The aliens:** the intro's three moved onto the new line (6, 14 and 22 s), and the dawn end one moved to about
  203 s.
- **Still sea:** 28–52 s is still sea, which is the approved bay and low-sea legs.

**2. Dawn: "pale gold and rose, clean, never green."**
- **What made it muddy:** the old dawn palette's green ramp, teal shallows, olive sky floor (`#B8B888`) and green
  underside, under the densest fog of the arc. Our curve was empty, so the Tree of Life default applied: sunrise
  0.00035.
- **New palette:**

  | Part | New value |
  |---|---|
  | Land | `#C07A9A` → `#E694A6` → `#F6B49A` → `#FAD08A` → `#FFE8B8` (mauve-rose, rose, peach, pale gold, cream) |
  | Water | lavender `#4E5AA0`, `#6C78BC`, `#A09CD6`, with a rose crest `#FFD8C8` |
  | Shallows | rose `#F0B8C0` |
  | Underside | mauve `#6A4A70` |
  | Sky | floor `#D8A8B0`; horizon and fog peach-rose `#FFD6BC` / `#FFD4C0` |
  | Sun | pale gold `#FFD08A` |
  | Path | `#F8C060` |
  | Grade | saturation 1.5 → 1.3, exposure −0.3 → −0.2 |
- **The fog, which is what I changed in DayNight:** `day_night()` now writes its own `volumeDensity` curve.
  - Day, sunset and night keep exactly the Tree of Life default they were approved under. `look.fog_density_default`
    samples that curve at their phases.
  - Dawn alone gets its own key, `fog_density` 0.00019, in place of 0.00035.
  - This also ends the silent no-op logged in CP-D groundwork. The curve is now ours, and `fog_mult` is still unused.
- **Result:** rose and peach land under a pale gold and lavender sky, clean, with no green.

**3. The teal crack.**
- **The underlay is ruled out:** hiding `mgUnderlay` changed nothing.
- **The column is in every state:** it is one gap column, wider than its neighbours, and it shows in the day colours
  too.
- **Why it read teal:** draft1's green dawn coloured the slab sides and skirt seen down it, from the bottom of the
  land ramp.
- **Status:** the rose dawn removes the teal. The wider gap itself looks like geometry, so I filed it with engineering
  (02-requests, "one tile-gap column wider than the rest") instead of hiding it in data.

**4. The night glare strip** (148–164 s).
- **Cause:** a far hill in the 4× ring, 600–1000 m out, where 4 m tiles are a few pixels wide. There the contour band
  covered whole tiles and fused into one blown-out strip.
- **Fix:** the contour emission now fades with distance: `cameraDistance` remapped over 250 m → 800 m, down to 0.15.
  That adds 3 ops (the 1× ring is at 62).
- **Result:** the 148–164 s horizon now reads as faint lines.
- **Still bright:** a steep near hill at 172–180 s, about 150–300 m out, still stacks bright white lines. They read as
  separate contours, not a stripe, but they are hot. Phase 7's rest contour glow of 0.5 halves them in draft3.

**Renders:** `10-full/cpd-draft2-feline-full-1080p.mp4` and `cpd-draft2-sheet.png`.

**Whole-song perf** (1080p Ultra, 208 s, profiled before the render):

| GPU p50 | GPU p95 | GPU p99 | CPU work p50 | Frames within 16.7 ms |
|---|---|---|---|---|
| 13.9 ms | 19.6 ms | 25.9 ms | 1.8 ms | 91.9% |

The worst frames are one hitch cluster at 157–158 s, with a single 277 ms frame. It did not show in draft1's runs. The
wider tail is in line with engineering's finding that the flight's GPU tail is the GPU clocking down (02-requests),
which I have not separated here.

### CP-D draft3: the music switched on across the whole arc (2026-11-09, Phase 7 merged)

**The merge.** I merged `scene/musical-geography` f028d0c0 (ADR-1963 tile glow gains, ADR-1964 hop on the music) and
rebuilt. 02-requests.md conflicted only at its end, and both sections are kept.

**The programs.** `look.tile_program` now has a `glow(reg, lane, tmp)` helper, called in engineering's four places:
- the pads after `r4 = emission`;
- the path before its `add`;
- the contours after the distance fade and before their `add`;
- the crest.

The 1× ring is now 71 ops and the 2× and 4× rings 56, all under `kMaxMaterialOps` 96.

**The mapping** (`look.audio_routes`, `look.ALIEN_REACTIONS`, `look.GLOW_REST`): engineering's mapping, tuned.

**Rest glow** (what silence shows): path 0.4, contour **0.7** (engineering had 0.5), crest 1, pads **0.6** (was 0.5).

| Source | Target | Amount | Shape |
|---|---|---|---|
| `response.low` | `env/tiles/amplitude` | +0.6 | as before |
| `response.onset` | `env/tiles/rings/trigger` | 1 | as before |
| `audio.energy` | `env/tiles/breathing/amplitude` | +0.25 | |
| `beat.bar` | `env/tiles/contour/offset` | +4 m | one interval per bar, so the lines climb |
| `beat.pulse` | `env/tiles/glow/contour` | **+0.8** (eng +1.5) | peak hold, falls **3/s** (eng 4) |
| `response.mid` | `env/tiles/glow/path` | **+1.2** (eng +1.6) | 30 / 250 ms |
| `response.high` | `env/tiles/glow/pads` | **+0.8** (eng +1.0) | 20 / 300 ms |

- **Aliens:** reactions as engineering set them. `beat.pulse` drives `hop/trigger`, and `response.level` drives
  `hop/apex` and `hop/strength`.
- **Stems and feeds:** Feline's three stems are loaded. The beat clock and the hits follow the drums, and the levels
  follow the bass. The log reads "the beat clock follows stem 'drums': 120.97 bpm".
- **Audit:** 91 routes and 118 timeline tracks, all live.

**Why these amounts:**
- **The contour flash.** Engineering's +1.5 over a rest of 0.5 tripled the night lines on every beat. At 121 BPM
  that reads as a strobe. +0.8 over 0.7 with a 3/s fall makes each beat a swell. The rest lines stay visible between
  beats, and they climb one interval per bar (strip: `10-full/cpd-draft3-night-145s-beat-strip.png`, 145–146.5 s at
  10 fps; the beats are at 146.0 and 146.5).
- **Night `path_emit` 5 → 2.** At rest the night path is now 0.8 in place of a clipped 2.0, and on the mids it
  reaches 3.2. The other states' `path_emit` is raised by 1/0.4, so their path looks exactly as approved at the rest
  glow: day 0.875, sunset 1.5, dawn 0.625.
- **A lighter day.** The glow lanes multiply each state's own emission. The day has a faint path and no contours or
  pads, so its touch is light by construction: the swell, the rings and the hops. The night carries the most
  movement.
- **Silence is calm.** Every route is unipolar and adds to a rest value. The intro (0–8 s) and the fade
  (202–210.5 s) show the rest: waves and breath, with no flashes.

**Renders:** `10-full/cpd-draft3-feline-full-1080p-audio.mp4` (the whole song, audio reactive) and
`cpd-draft3-sheet.png`.

**Whole-song perf.** Read this with care:
- **Two 208 s runs** read GPU p50 15.0 / 15.4 ms, but with a heavy tail: p90 36–37, p99 43–45 ms.
- **Back-to-back A/B of draft2 against draft3** over the night climax (130–160 s, 30 s each, run twice) shows no
  difference:

  | | GPU p50 | GPU p90 | GPU p99 |
  |---|---|---|---|
  | draft2 | 15.3 / 15.2 ms | 17.0 / 16.9 ms | 19.3 / 18.9 ms |
  | draft3 | 15.3 / 15.2 ms | 17.5 / 16.8 ms | 20.0 / 19.5 ms |

- **So the wiring costs no GPU,** as engineering measured. The long runs' tail is environmental: `gpu-lock` waited
  60 s for another session's GPU job during this pass, and it matches engineering's GPU-clocking finding.
- **CPU work p50 is 2.5–3.2 ms** (2.2–3.3 ms before), which is the stems and the routes.

### CP-D r2, stills only: terrain variety and clouds (2026-11-10; owner's draft3 notes 1 and 3)

**Terrain** (`cpd-r2-terrain-sheet.png`, plus the single stills and the maps).
- **What the data can do now:** more regional variety in the hills (`--tiles '{"regionVariation":0.45,
  "regionWavelength":1400}'`). That gives broad low plains against taller ranges.
  - Stronger variants (more octaves, warp) streak into swirls on the map, so I rejected them.
  - On its own, the variety tweak reads only a little different in flight.
- **What the generator cannot do, so I prototyped it on the one river polyline:**
  - **A pond:** a short capsule 80 m wide in a 190 m basin. The low water tiles sit in rolling land, and at night the
    contours ring the basin.
  - **A dry valley:** a 1 m channel in a 170 m valley, which carves a clear trough that the contours follow.
  - **The real river's winding valley.**
- **Request CPD-R1 in 02-requests asks for:**
  - several rivers with their own bed depth;
  - ponds at their own level;
  - dry valleys;
  - a water-body channel for colour and motion.
- **The plan, once R1 lands:** a distinct landform on every leg of the flight:
  - the bay and the west-shore hills;
  - the lit path through rolling hills;
  - a pond plateau on the crowns;
  - the river valley;
  - a dry valley through the sweep;
  - the night coast with a chain of ponds;
  - the inlet;
  - a stream down the peninsula at dawn.

**Sky** (`cpd-r2-sky-sheet.png`; day 50 s, dusk 118 s, night 150 s, dawn 200 s).
- **The approach:** tile clouds. Each layer is a second `tiles` ground at altitude: its continent field is the cover
  and its hills are the billows, with the "sea" cells cut away by `look.cloud_program` (opacity = 1 − water, mask).
  Stepped square-slab cumulus, in the world's own language.
- **Layers:**
  - low scud at 170 m, a sparse scale cue above the 40 m ceiling;
  - cumulus masses at 650 m, stepping up to about 110 m.
- **Light:** normal-shaded (undersides at 0.45), and lit by the scene's own sun, moon, fill and sky.
- **Across the arc:**
  - day: white masses on blue;
  - dusk: pink-gold-lit decks;
  - night: dark violet silhouettes, with moonlit side edges that streak a little;
  - dawn: peach-gold tops.
- **Cost:** the masked stopgap costs about +44 ms of GPU (the empty cells raster and discard), so it is not shippable.
  The same layers drawn opaque cost +7.6 ms. Request CPD-R2 asks the `tiles` kernel to omit water cells, with a
  target of at most 2 ms.
- **The build:** `--clouds` (off by default). Nothing in the approved build changes.

**CP-D r2 sky, night v2** (the owner picked the slab clouds; note: at night "too much like a mirror reflection of the
ground ... too dark on the bottom"). `look.CLOUD_STATE` is keyed through the arc:

| Setting | Day | Night |
|---|---|---|
| Undersides (of the tops) | 0.45 | 0.85 |
| Side faces (normal y²) | 1.0 | 0.55, which removes the moonlit streaks |
| Cores only | off | cells with billows above about a third of the relief |
| Cool bounce | none | `#6A74D8` × 0.1 |

The result is sparser, broken masses at night, a little lighter than the sky. They no longer read as a flipped ground
(`10-full/cpd-r2-sky-night-v2-sheet-before-after.png`, and `-streaks-before-after.png`). A silver lining on the
moon-facing edges needs a key-light-direction input, requested as the CPD-R2 addendum. The day frames are unchanged.

### CP-D r2: the music actually on, the landing glow, the crown-pad canary (2026-11-10)

I merged eng's ddc129ca and 67ca7d4e (ADR-1952, the landing tail; ADR-1960, the landing glow) and rebuilt.

- **The sonic block.** `project_doc` now writes `"sonic": {"live": true}`. Without it, `response.*` never published,
  so in draft3 four of the seven terrain routes and the loudness hops were dead (eng's `10-audio-reactivity-now.md`).
  `--audit-routes` now passes: 91 routes and 130 tracks, all live.
- **The crown pads were flat: the cause is proven, and fixed.** I ran `--sonic-trace` over the whole song, twice:

  | Levels fed from | `response.high` mean / p90 / max | `response.mid` mean |
  |---|---|---|
  | The bass stem (`"levels": "stem.bass"`) | 0 / 0 / 0 | 0.27 |
  | The mix | 0.43 / 0.93 / 1.0 | 0.57 |

  The bass stem has no highs, so the route that lights the pads on the highs never moved. `look.FELINE_FEEDS` now
  takes the levels from the mix. The beat clock and the hits still follow the drum stem.
- **The landing glow.** I added eng's 4 ops after the contour lines' `add`. The outline band is
  `LANDING_BAND` 0.075 per S (0.3 m at 4 m tiles), the spread is `spring.glowRadius` 3 m, and the glow is keyed
  through the arc (`look.LANDING_GLOW` → `env/tiles/glow/landing` and `landingColor`):

  | State | Gain | Colour | Why |
  |---|---|---|---|
  | Day | 12 | `#FFD99A` warm white | reads on the lit ground |
  | Sunset | 8 | `#FFC070` | |
  | Night | 2.5 | `#FF6FC0`, the night accent | |
  | Dawn | 10 | `#FFD8B0` | |
- **The spring is the original:** 1.4 Hz, damping 0.16. The owner rejected eng's soft arm ("the other bounces look much
  better").

Clips are in `11-pass-cpd-r2/`:
- `cpd-r2-react-{day,night}-{music,silent}-*.mp4`;
- music over silence in one frame: `cpd-r2-react-*-music-over-silent-*.mp4`;
- the strip and the day landing-glow frames (1 fps).

### CP-D r2 smooth: no beat pulses on the edges; motion that flows with the song (2026-11-10)

The owner's verdict on the react clips was "quite twitchy ... not so sure about beat pulsing any of the edge glow".
Then: "make glowing edges drift in according to the music intensity". I measured first, then changed the routes.

**How it was measured.** `--sonic-trace --trace-finals`, giving the 99th-percentile frame-to-frame step at 30 fps as a
fraction of each target's range. The full table, before and after for both windows, is
`11-pass-cpd-r2/cpd-r2-smooth-route-jitter-before-after.txt`. Night, 140–160 s:

| Route | Before | After |
|---|---|---|
| Swell (`response.low` → amplitude) | 0.99 | 0.03 |
| Contour flash (`beat.pulse`) | 1.00 | removed; the edges now drift with energy, 0.01 |
| Contour climb (per bar) | 0.99 | 0.004 (per phrase) |
| Pads | 0.70 | 0.01 |
| Path | 0.26 | 0.01 |
| Rings | 108 triggers in 20 s | 6 |
| Breathing | 0.05 | 0.05 (unchanged) |

**A root cause that was not the mapping.** Every route's chain had been written flat, so every attack, decay and peak
hold was silently ignored. Draft3's contour "flash" was raw beat spikes. The chains are now nested under `"chain"`,
and the silent drop is filed with engineering.

**The new mapping** (`look.audio_routes`):

| Target | Source and shaping |
|---|---|
| Edge glow (path, contours, pads; crest gains are 0) | `audio.energy` remapped 0.30–0.68 → 0–1, slewed 1.5 s up and 4.5 s down, eased by a 0.25 Hz critically damped spring; rests 0.3 / 0.35 / 0.3, plus up to 0.9 |
| Swell | `response.low`, 0.6 s attack, 2.5 s release, 0.4 Hz spring, amount 1.0 |
| Rings | a latching envelope on onsets: at most one every 2.5 s |
| Contour offset | one interval per phrase |
| Landing glow | unchanged |
| Hops | unchanged, until eng's E lands |

- **Why `audio.energy`:** it carries the sections, about 0.3 in the build and 0.67 in the climax. `response.level` is
  normalised nearly flat.
- **Day and night:** the glow lanes multiply each state's own emission, so the day stays light (a faint path, no
  contours) and the night floor still shows the contours (0.35 × 7).
- **Plot:** `cpd-r2-smooth-edge-glow-vs-loudness-whole-song.png`. The glow dims through the build, rises through the
  main section into the climax and eases down at the outro.
- **Clips:** `cpd-r2-smooth-{day,night}-music-over-silent-*.mp4`.

### CP-D r2 final: B, E, the two late chains and the day edges (2026-11-10, on eng a5a4b691)

- **B, the swell on the land.** `look.LAND_SWELL` is 0.5 on both wave trains (draft3 had 0.08). It is authored in
  `tile_dynamics`, so the flight's clearance reads it. The camera skims swelling tiles on the low sea and path legs
  and never cuts into them in either clip.
- **E, the hops.** `cueOnly` 1, `cueEvery` 4, `cueGap` 2.5 s, `airBeats` 2, `maxWait` 10 s, `hopChance` 0. The
  `beat.pulse → hop/trigger` reaction is kept. The cast hops in turns, each alien at most once per 2.5 s and in the
  air for two beats.
- **The two late chains.**
  - `hop/apex` and `hop/strength` from `response.level`: now 300/1200 ms. The whole-song p99 step is 0.017 of the
    range.
  - `breathing/amplitude` from `audio.energy`: now 400/2000 ms. It reads 0.02–0.025 in both windows, where it was
    0.05.
  - The route reader now refuses flat chain keys, and this build loads clean.
- **The day edges.** The day `edge_gain` goes from 0 to 0.6 (`#FFF4C8`), so the contours read faintly on the day
  hills. They ride the same energy drift, with no beat term.
- **Feet:**
  - **Watched:** the close landings at 50.4–50.9 s, at 5 fps, at full resolution.
  - **Seen:** the alien stands on its own sprung tile, which sits below its neighbours. The neighbours' front faces
    sometimes hide its feet, which at a glance reads like clipping, but it is the pit.
  - **Not seen:** feet through a tile top.
- **Jitter:** `11-pass-cpd-r2/cpd-r2-final-route-jitter.txt`, comparing draft3-react, the approved smooth pass and
  this pass.

### CP-D r2: the tile field drawn 0.6 m lower (eng ADR-1991, the real feet fix), re-checked (2026-11-10)

**My earlier feet call was wrong.** I read the r2-final feet as the landing pit, with the neighbours hiding the feet.
In fact they were 0.6 m inside their own tile: the kernel centred each box on the height, so the top was drawn half
a box above the surface the bodies stand on. I called it from a look, not a measurement.

**What I checked after merging 18bced09 and rebuilding,** before and after at 40, 100, 145 and 200 s
(`11-pass-cpd-r2/cpd-r2-feetshift-*`):
- **Overall brightness:** the mean frame luminance moves by at most 1 of 255. The differences that remain are
  motion: rings, hops and cloud timing.
- **The ramp:** the `worldPosition.y` ramp is now read 0.6 m lower, about 5% of a day stop. There is no visible
  colour shift, and `UNDER_RAMP` and `UNDER_SHADE` need nothing.
- **The horizon:** no new band at the fill or the ring seam.
- **The flight:** clearance over the swelling land holds; no frame cuts into a tile.
- **The clouds:** they are tiles too, now 3–7 m lower, which cannot be seen at 170 m and 650 m.
- **The feet:** the aliens stand on the tile tops at 100 and 200 s.

Nothing needed retuning.

### CP-D r2 land: a distinct landform on every leg (2026-11-10, ADR-1965)

These are in `world.TILES`. The scene is now on `rivers` everywhere, the ground and the cloud layers alike; the
`river` alias is no longer written. Every leg has its own form at its time of day (`11-pass-cpd-r2/cpd-r2-land-sheet.png`
and the top map `cpd-r2-land-map.png`):

| Song time | Light | Landform |
|---|---|---|
| 0–20 s | day | the bay and the west-shore hills, with `westStream` falling off them into the bay |
| 56–80 s | day | the lit path through rolling hills |
| 83–92 s | sunset | two lowland ponds under the crowns (`low1`, `low2`) |
| 96–108 s | sunset | the dry valley `sweep`, which the flight runs down (90 m wide, `depthFraction` 0.7) |
| 108–128 s | dusk | the river valley (`rivers[0]`, "river") |
| about 140 s | night | the plateau `tarn` on the high flat ground where the night comes |
| 145–160 s | night | a chain of three ponds along the coast |
| 160–172 s | night | the inlet's sea |
| 185–210 s | dawn | `dawnStream` down the low peninsula into the northern sea |

All the ponds and streams use `level` and `sourceLevel` `"auto"`.

**Water by body** (`look.WATER_BODIES`, keyed through the arc). The tile program now reads `waterBody` and ramps
sea, river and pond colours and glows by its class (+3 ops; the 1× ring is at 78 of 96):

| State | River | Pond |
|---|---|---|
| Day | cyan `#1FC6F2` | aqua `#3EE4D2` |
| Sunset | rose `#F08AC4` | peach `#FFB08C` |
| Night | blue `#3A6CFF`, glow 0.6 | violet `#B060FF`, glow 1.1 |
| Dawn | periwinkle `#9CB8FF` | blush `#FFC4D4` |

**The water's motion:** `riverWaves` 0.25, `pondWaves` 0.1, and a downstream drift (0.35 m, 24 m wavelength, 3 m/s)
with no audio term. Within the smoothness spec by construction.

**The terrain itself** is unchanged (seed 3, heightScale 1.25). The `regionVariation` prototype was not adopted: it
flattened the middle legs.

**The camera rules hold.** The route is unchanged, and the clearance follows the carved ground.

### CP-D r2 land2: enclosure ("I don't wanna see long endless horizons"), 2026-11-10

**The metric** (`11-pass-cpd-r2/cpd-r2-land2-enclosure-before-after.txt`): the share of the frame's 74° horizontal
view cut by land standing at least 1.5° above the eye line within 2 km. It uses the planning mirror on the flight
line, with the eye 12 m above the ground. Over the 11 stills it goes from a mean of 0.22 to 0.85, and from 2 to 10
stills at 0.6 or above.

**What changed:**
- **Relief.** `heightScale` 1.25 → 2.2 (hills to about 65 m). `inlandBand` 0.15 → 0.07, so the land stands up just
  behind the shore and the coasts become headlands. `hillLow` 0.22 → 0.12. The colour stops and the contour interval
  scale with the relief: `RELIEF` 1.76 on every stop, and the contour interval goes from 4 to 6 m.
- **Valleys the flight runs down.** Four dry valleys follow the flight line, 150 m wall to wall, at `depthFraction`
  0.85: the path's dale, the sweep (run on into the river valley, after it left the flight climbing a 60 m flank
  head-on), the coast vale and the cape vale. That uses the whole 32-point budget.
- **Ponds about 1.6× larger.** The sunset pond colour is now plum `#C8407E`, in place of a pale peach that read as a
  patch.

**Still weak:**
- The open-sea legs (30–50 s) still show a far, low coast strip.
- The dawn peninsula is 0.41.

Both need land the generator cannot place, so I asked for it as request CPD-R3 (ridges and rises, islands).

**Smoothness:** no route changed, and the flight's clearance follows the carved ground.

### CP-D r2 clouds2: the cheap clouds, the silver lining, and the 148 s "bunching" (2026-11-10, ADR-1966/1967)

**Clouds.** Both layers are now opaque with `"omit": "water"`, and `dynamics` and `contours` are off. The program's
opacity and the material's mask are gone. The night's cores-only cut is now the per-cell `omitBelow`, keyed through
the arc: night 7.25 m on the low layer and 48.75 m on the mid layer, which is eng's conversion less 2 m so the day
keeps every cell (`look.cloud_omit_below`). The approved v2 look is kept: undersides 0.85, sides 0.55, and the cool
bounce.

**The silver lining** (`keyLightDirection`, `keyLightColor`):
- **What it lights:** horizontal n·L, from x and z only. The tops are never seen from under the deck, so the lining
  is the edges and sides that face the key light.
- **Shape:** smoothstep 0.05–0.6, × the key light's colour × a keyed gain.
- **Gain by state:** day 0.08, sunset 0.14, night 0.5 (silver `#D8E0FF`), dawn 0.10.
- **The night result:** the moon-facing edges of the decks catch a cold rim; the faces and undersides are unchanged
  (`cpd-r2-clouds2-night-lining-off-on.png`).

**148 s.** I measured instead of guessing, and it was two things:
- **A coast pond auto-levelled on a valley wall** (land2 put it there). Its flat water stood up the slope as one slab.
  **Fix:** all six ponds now sit on their valley floors, on the flight line.
- **The steep walls' contour band seen edge-on.** It was gone with the edge gain at 0, and unchanged by the
  coarse-ring and distance cuts. **Fix:** the band fades with `viewDirection.y` over 0.03–0.15 (`GRAZE`, +4 ops; the
  1× ring is at 82 of 96).

Also changed: the coarse rings carry 0.25 of the contour gain, the interval is 9 m (it was 6), the night edge gain is
5 (it was 7), and `CONTOUR_FADE` runs 150 → 500 m.

**GPU** (1080p Ultra, live profile, 20 s windows, run twice):

| | Clouds on, p50 | Clouds off, p50 | Clouds cost |
|---|---|---|---|
| Day, 40–60 s | 16.54 / 16.49 ms | 14.10 / 14.09 ms | +2.4 ms |
| Night, 140–160 s | 14.78 / 14.75 ms | 13.93 / 13.96 ms | +0.8 ms |

The day costs more than eng's +0.65 ms. This build has the lining's ops on every cloud pixel and more cover over this
window, and I have not split the two.

### CP-D r2 land3: ridges, rises and islands (2026-11-10, ADR-1992)

**What I placed** (`world.TILES`): 8 ridges (26 points) and 6 rises.

| Where | Landform |
|---|---|
| The bay's mouth | headlands `westHead` and `eastHead`, 45 m |
| Inside the bay | a spit and a north-shore ridge |
| Beside the 30–50 s sea legs | islands `bayIsle` (46 m), `southIsle`, `pairIsle`, `stack`, `westIsle` |
| North of the sweep | `sweepNorth` |
| Behind the tarn | `tarnRidge`, whose core makes a seaward headland along the night coast |
| Down both sides of the dawn peninsula | ridgelines `capeEast` (48 m) and `capeWest` |
| The inlet | an island, `inletIsle` |

**Enclosure** (`cpd-r2-land3-enclosure-v1-v2.txt`). The coordinator asked me to tighten the metric, because 30 s had
scored 0.95 while the still showed a low strip.

| Version | Rule | land2 | land3 |
|---|---|---|---|
| v1 | ≥ 1.5° within 2 km | mean 0.86 | mean 0.99, all 16 legs ≥ 0.6 |
| v2 | ≥ 3° within 1 km | mean 0.60, 6 of 16 legs ≥ 0.6 | mean 0.86, all 16 legs ≥ 0.6 |

The weakest legs on v2 are the inlet (167 s) at 0.68 and the bay (35 s) at 0.70. The bay now reads as enclosed in
the stills and in `cpd-r2-land3-clip-bay-30-50s.mp4`.

**Open: islands and headlands stand on a cliff.** A raised core becomes land at half its profile, so the islands read
as domes on stilts, with ramp bands up the cliff. Filed as CPD-R3b. Their coast also still has no beach or shallows;
the coordinator has asked eng for that.

### CP-D r2 land4: the bay islands reshaped (2026-11-10, on ADR-1993 soft coast and ADR-1994 noise)

The owner said "some of these lumps look odd". Each island is now built from overlapping rises, all with noise 0.6:

| Island | Parts | Character |
|---|---|---|
| The bay island | peak 38 m, saddle 28 m, tail 17 m | elongated north–south across the 30–35 s view; a steep, short-edged north end and a long, gentle tail |
| The south island | a 31 m knoll and a 19 m shoulder (edge 80) | |
| `stack` | 20 m, radius 14 | small and steep |
| `northIsle` | 32 m, radius 40, edge 65 | broad and low, in the bay's north-east |
| The inlet's island | 18 m, edge 80 | long and low |

- **Ridges:** every ridge now carries noise 0.5. The spit and the north-shore ridge are raised to 42 m, and the spit
  reaches further east.
- **Rises:** `westIsle` and `pairIsle` were dropped. All 8 rises are used.
- **Enclosure** (`cpd-r2-land4-enclosure.txt`), v2: 30 s goes from 0.78 to 0.70 and 35 s from 0.70 to 0.65. Both stay
  at 0.6 or above.
- **Popping:** I saw none in the 30–50 s clip at 2 fps (eng is investigating it).

### CP-D r2 clouds3: the overhead "platform" undersides (2026-11-10)

The owner said the near overhead clouds' dark flat undersides "read a bit more like a platform"; the distant clouds
were fine. I tried four options at 30 s and 35 s, plus a dusk (118 s) and a night (150 s) check
(`11-pass-cpd-r2/cpd-r2-clouds3-options-*`):

| Option | What it is | Result |
|---|---|---|
| v1 | Data only: an "overhead" sky fill, low slabs 6 → 3 m, day undersides 0.45 → 0.75 | Lighter, but still an opaque plate |
| v2 | v1 plus translucency (refraction phase at opacity 0.55) | The undersides take the sky through them and stop reading as a platform. Best. |
| v3 | Translucency with the 6 m slabs | Close to v2 |

**v1 in detail.** The sky fill uses `viewDirection.y` over 0.25–0.85: an overhead cloud gives up albedo for
`skyAmbient` emission, `cloud_sky_mix` day 0.75 and night 0.

**Adopted:** v2, with the opacity keyed through the arc (day 0.55, sunset 0.65, dawn 0.6, night 1.0), so the approved
night v2 is unchanged in look. **Cost:** +3.0 ms day and +1.6 ms night against the opaque clouds.

**Side effect:** the horizon's clouds are also a little thinner. Asked for a per-pixel opacity so only the overhead
clouds thin, as request CPD-R2b.

### CP-D r2: the pop fix applied, and draft4 (2026-11-10, ADR-1995/1996)

**What changed:**
- **The ring morph**, eng's values (`world.RING_MORPH`):

  | Ring | Width | Ratio | Pitch |
  |---|---|---|---|
  | `mgTiles1` | 64 | 2 | |
  | `mgTiles2` | 128 | 2 | |
  | `mgTiles4` | 256 | 4 | 16 |

- **The 1× ring hands over to the 2× ring's look** over `cameraDistance` 200 → 270 m (`HANDOVER`; 13 ops, so the 1×
  ring is at 95 of 96):
  - the tile jitter eases out;
  - the pads fade;
  - the contour share falls to `COARSE_CONTOUR`;
  - the border's darkness goes to `BORDER_DARK`.

  I did not split these four term by term; together they are the hypothesis eng named.
- **Contour fade:** 0.15, eng's default, with the edge gain × 1.15 (`CONTOUR_FADE_COMP`). I did not pick 0.35, which
  eng's still shows breaking the lines into dashes, and I did not compare fade values by eye at 111–114 s.
- **Clouds, per pixel (R2b):** opacity = remap(`viewDirection.y`, −0.6 → −0.2, `cloud_opacity` → 1). The material
  gate is 0.99 by day and 1.0 at night, which is the opaque path.

**Checked at the owner's moment** (`cpd-r2-pop-final-bay-30-37s.mp4`). I counted the pixels in the islands' band that
change by more than 40 levels between frames:

| Moment | Changed pixels | Neighbouring frames |
|---|---|---|
| 31.2 s | 335 | 364 / 363 |
| 31.87 s | 338 | 338 / 347 |
| 32.5 s | 382 | 321 / 341 |
| 33.17 s | 629 | 529 / 607 |

None of them stands out from its neighbours. Frame by frame, 33.07–33.17 s shows no visible change in the islands'
tiles.

**Draft4:** `11-pass-cpd-r2/cpd-draft4-feline-full-1080p-audio.mp4` and `cpd-draft4-sheet.png`.

**Noted in it:** at 4 s and 12 s a near translucent low cloud (170 m) passes in front of the now much taller west
hills, and reads as a soft lavender smudge on the hill face. That is possible since land2 (relief to about 110 m).
Raising the low layer, or thinning it over high ground, is an option.

### CP-D r3 (pass 12): the night vale opened, banks requested, the low clouds raised (2026-11-10)

**The night vale** (137–165 s; the owner: "too claustrophobic … ends with a giant mound … the pond-like features
… don't look great"):
- **The valley:** `coastVale` is wider (150 → 190 m) and runs on out onto the inlet (three more points), instead of
  ending against the hill.
- **The ridge:** `tarnRidge` is set back about 15 m.
- **The ponds:** the four night ponds (`tarn`, `coast1`–`coast3`) are removed.
- **v2 enclosure:** 1.0 at every point before; now 0.73 / 0.68 / 0.57 at 143 / 148 / 152 s. It then opens out: 0.35
  at 156 s and 0.16 at 160 s, onto the inlet with its island far off. 137–140 s is still the river valley's exit at
  1.0.

**Water banks:**
- **Checked:** the shore channel covers the sea's and raised land's coasts only, not ponds or rivers. Requested as
  CPD-R4: an inland shore channel, `bankWidth`, a `lip` and a `step`.
- **Meanwhile:** more motion on inland water (river waves 0.4, pond waves 0.3, drift 0.6 m).

**Clouds:**
- **The low layer** goes from 170 to 260 m, above the tallest land (about 155 m with the ridges). The 4 s and 12 s
  smudge was a cloud in front of the west hills. It is gone. What is left there is the hill's own colour ramp into
  the haze; the same frame is identical with the clouds opaque or off (`cpd-r3-04s-veil-is-terrain-not-cloud.png`).
- **The 140 s corner** is clear now.

**The inlet island** (owner: "move it aside a bit and lower it"):
- **Where and how tall:** it now sits about 120 m off the flight line, on the left (north) as the vale opens. It is
  lower (13 m and 7 m, where it was 18 m) and built as a peak plus a shoulder, with noise 0.8.
- **The rise slot** came from dropping the bay's small `stack`. The bay's v2 enclosure is unchanged: 0.70 / 0.65 at
  30 / 35 s.
- **v2 at 156 / 160 / 165 s:** 0.35 / 0.16 / 0.65, the same as before. The exit is now open in the frame.
- **Stills:** `cpd-r3-inlet-*`.

**Tilt-shift** (owner: "add tilt shift post-processing effect"). It uses the existing ADR-079 band, through
`build.py --tiltshift` (`look.TILT`):

| Setting | Value |
|---|---|
| Centre | (0.5, 0.6): the mid-ground, where the aliens and the near-mid tiles sit |
| Band width | 0.16 of the frame height |
| Falloff | 0.28 |
| Max radius | 6 (12 px at 1080p) |
| Grade lift | saturation × 1.06, contrast + 0.04 |

- **The read:** the horizon, the hills and the sky soften, and the near foreground a little, a toy-table read. The
  night contour lines soften at the horizon, but the near ones stay crisp. The clouds soften without smearing.
- **Cost** (the defocus pass): 0.74 ms. GPU median, two runs each:

  | | Off | On |
  |---|---|---|
  | Day | 20.6 ms | 21.3 / 21.8 ms |
  | Night | 16.1 / 16.2 ms | 17.1 / 16.9 ms |

- **Not keyed per state yet**; the same band in every state.
- **Depth-aware band:** requested as optional CPD-R5.

**Tilt-shift 2** (owner: "make it stronger"). I tried two settings (`cpd-r3-tiltshift2-*`):

| Setting | Centre y | Band | Falloff | Radius | Grade | Defocus pass |
|---|---|---|---|---|---|---|
| Strong | 0.58 | 0.10 | 0.18 | 10 (20 px) | saturation × 1.10, contrast + 0.06 | 2.25 ms |
| Very strong | 0.58 | 0.07 | 0.12 | 14 | saturation × 1.12, contrast + 0.07 | 4.3 ms |

- **Picked: strong.** It reads clearly as a miniature: the near foreground blurs now, and the hills, horizon and
  clouds are soft. The aliens in the band stay sharp, and the night contours soften without turning to mush.
- **Why not very strong:** it also blurs aliens just below the band (dawn 200 s) into bokeh blobs, and it costs
  twice as much.
- **Strong, GPU median:** day 22.8 ms (20.6 with it off), night 18.0 ms (16.2 off).

### CP-D r3: the walking cast, crest throws, off the water; tilt-shift at 70% (2026-11-10, ADR-1997)

**The cast** (on ADR-1997):

- **Removed:**
  - the cue keys (trigger, cueOnly, cueEvery, cueGap, airBeats, maxWait);
  - `preferWater`;
  - the `beat.pulse → hop/trigger` reaction.
- **Added:** hopChance 0.05, tossThreshold 0.2, tossCooldown 3, goalMin 2, goalMax 6, pauseMin 0.8, pauseMax 3.
- **Home radius:** at least 14 (crown, bank and inlet 6/6/8 → 14; sea and path stay at 30).
- **Throw gain:** tossGain 30, tossMax 11 m/s (eng's clip values were 16 and 9).
- **Moved off the water.** Each new position was checked on `terrain_mirror` (water and height):

  | Alien | New position | Where |
  |---|---|---|
  | 00 | (228, −200) | the bay saddle |
  | 01 | (160, −450) | the north isle |
  | 03 | (30, −640) | shore |
  | 08 | (−276, −1043) | off the low1 pond |
  | 10 | (−184, −1166) | off the low2 pond |
  | 16 | (60, 10) | the south island's shoulder |
  | 17 | (−485, −25) | the west shore |
  | 18 | (−510, −200) | the west shore |
  | 23 | (1180, −1735) | dry ground near inletPeak; every spot on the inlet's north shore I tried was water |

**Measured throw heights.**

- **How:** the hidden `[measure][adr1997]` case, over all 28 aliens, 0–215 s, on the real scene file.
- **Height from airtime:** h = g·T²/8. Flights of exactly the hop's 1.3 s are counted as hops.

| Gain / cap | Throws | p10 | Median | p90 | Max | Share ≥ 3 m |
|---|---|---|---|---|---|---|
| 16 / 9 (eng's values) | 512 | 0.6 m | 1.0 m | 2.5 m | 6.6 m | 6% |
| **30 / 11 (picked)** | 512 | 2.4 m | 3.9 m | 6.2 m | 9.3 m | 71% |
| 40 / 11 | 527 | 3.8 m | 5.9 m | 6.4 m | 8.8 m | 100%, mostly at the cap |

- **The pick:**
  - At 16, a throw is under a third of the body's height (3.2 m), which is invisible from the flight.
  - At 30, the median throw is above the body's height, and a bigger crest still throws visibly higher.
  - At 40, nearly every throw hits the cap, so they all look the same.
- **Frequency:** each alien is thrown about every 12 s (11–27 throws per alien in the song).
- **Max above the cap's 6.2 m apex:** some landings are lower than the launch.

**The close clip** (`cpd-r3-cast-close-throw-41-50s.mp4`):

- **Setup:** alien09 (the crown alien at (−266, −1112)), static camera about 40 m off.
- **The throw:** at 44.1 s, landing about 46.4 s with the spring and glow; airtime 2.3 s, about 6.5 m.
- **Pixel track:** standing body 115 px = 3.2 m; the head rises about 260 px, about 7 m. This is a rough track: the tilt blur desaturates the alien above the band.

**Known gap (not worked around):** throws follow the authored waves, not the music (ADR-1997).

**Tilt-shift 3** (owner: "70% of the strong tilt shift"):

| Setting | Band | Falloff | Radius | Grade |
|---|---|---|---|---|
| Was (strong) | 0.10 | 0.18 | 10 | saturation × 1.10, contrast + 0.06 |
| Now (70%) | 0.12 | 0.22 | 7 | saturation × 1.07, contrast + 0.04 |

- **Centre:** unchanged at (0.5, 0.58).
- **Stills:** `cpd-r3-tiltshift3-*`.
- **Cost:** the defocus pass takes 1.09 ms, against 2.25 ms at strong. GPU median, two runs each:

  | | Off | Strong | 70% |
  |---|---|---|---|
  | Day (40 s) | 20.7 / 20.7 ms | 23.7 / 22.7 ms | 22.0 / 21.6 ms |
  | Night (140 s) | 16.7 / 16.8 ms | 18.1 / 18.4 ms | 17.0 / 17.4 ms |

- **The read:** still a clear miniature. The near foreground and the hills soften less, and the band is a little
  wider.

**Tilt-shift 4** (owner: "reduce tilt shift more, the focus is too narrow. It's okay if aliens blur"):

| Setting | Band | Falloff | Radius | Grade |
|---|---|---|---|---|
| Was (tilt-shift 3) | 0.12 | 0.22 | 7 | saturation × 1.07, contrast + 0.04 |
| Now | 0.22 | 0.30 | 5 | saturation × 1.05, contrast + 0.03 |

- **Centre:** unchanged at (0.5, 0.58).
- **The read:** a light miniature. The sky and the farthest hills soften, and so does the bottom fifth of the frame;
  the mid-ground is sharp.
- **Cost:** the defocus pass takes 0.52 ms. GPU median, two runs each:

  | | Off | Tilt-shift 4 |
  |---|---|---|
  | Day (40 s) | 20.4 / 20.7 ms | 21.0 / 21.0 ms |
  | Night (140 s) | 15.6 / 15.8 ms | 16.1 / 16.5 ms |

- **Stills:** `cpd-r3-tiltshift4-*`.
- **CPD-R5:** dropped. The owner accepts a near alien blurring.

### CP-D r3 final: throws on the music, banks, draft5 (2026-11-10, ADR-1998/1999)

**The throw** (owner: "it should look like the wave is throwing the alien upwards as it passes through"). I started
from eng's canary (onLand 1.0, threshold 0.5, gain 8, cap 9, landing strength 0.5).

| Setting | Value | Was |
|---|---|---|
| `onLand` (both swell trains) | 1.0 | 0.5 |
| Throw threshold | 0.4 | 0.2 |
| Throw gain | 14 | 30 |
| Throw cap | 10 m/s | 11 |
| Cooldown | 3 s | 3 s |
| `hopChance` | 0 | 0.05 |
| Landing spring strength | 0.5 | 1.2 |
| `response.level → hop/strength` | depth 0.3 | 0.8 |

- **No random hops:** a hop the wave did not cause is a false cue. The walk still hops up steps.
- **Damped springs:** eng measured that the springs, not the swell, moved a resting alien's tile.
- **Measured.** The hidden `[measure][adr1997]` case, all 28 aliens, 0–215 s; height from airtime (h = g·T²/8):
  - Flights of the hop's own 1.3 s are step hops, so I left them out.
  - 338 throws, about 12 per alien in the song: p10 1.7 m, p25 2.3, median 3.1, p75 4.5, p90 5.1, max 5.8 m.
  - Height follows the crest's size. Half the throws are above the body's height (3.2 m) and 12% above 5 m.
  - The comparisons:

    | Setting | Throws | Median | p90 | Max |
    |---|---|---|---|---|
    | Eng's canary (gain 8 / cap 9 / threshold 0.5) | 237 | 1.0 m | 1.7 m | 3.5 m |
    | Gain 12 / cap 10 / threshold 0.4 | 359 | 1.7 m | 4.2 m | 5.2 m |

  - **Too low to read from the flight:** at the canary the median throw is a third of the body's height.
- **alien09 in the close clip:** thrown at 43.8 s (about 5.1 m) and 52.1 s (about 3.9 m).
- **Camera clearance with onLand 1.0:** the flight solves its clearance on the moving ground at each sample's time
  (`flight.cpp`, `buildFlightTable`: 4 m over the highest ground within 10 m, plus the ring margin). That moving
  ground is `tileFlightGround` with the tiles' dynamics, `onLand` included, and amplitude headroom 2. So the larger
  land swell is inside the solve, and the draft5 contact sheet is the visual check.

**Banks** (ADR-1999):

| Water | Lip | Step | Why |
|---|---|---|---|
| Ponds low1, low2 | 0.6 m | 0.8 m | a rim, the water set in |
| The river (16 m) | 0.5 m | 1.0 m | |
| westStream, dawnStream | 0.3 m | 0.6 m | narrow streams, a low lip |

- **bankWidth:** stays at 8 m.
- **avoidBanks:** 0.3.
- **terrain_mirror.py:** mirrors the lip, the bank shore and the step, and returns `bank`. It ignores pond shore noise.

**The bank colour:**
- The bank's colour is the existing shallows term (shore → shallows at 33%).
- **Not tuned on its own:**
  - The shore channel is shared with the sea coast on the land side; the material cannot tell a bank tile from a coast
    tile there.
  - The 1x ring's program is at 95 of 96 ops, so there is no room for a bank-only term.
  - So I left `SHALLOW_AMT` alone, and the coast's look is unchanged.
- **The read:** the bank comes from the lip and step (a rim, the water set in) and a light shallows ring. By day the
  pond's rim reads clearly. At night the water's own glow carries it.
- **If the owner wants a distinct bank colour:** it needs a bank channel in the material and op room, or a cheaper
  1x program.

**Review scenes:** they keep the flight with `"enabled": false` and park the camera, so the film's rings still exist
and throw.

**Media:** `cpd-r3-final-*` and `cpd-draft5-*` in 12-pass-cpd-r3/.

### CP-D r4: a lived-in cast, close passes, one connected path (2026-11-11)

The owner on draft5: "the aliens feel too intentionally placed along the camera path and the camera doesn't ever
really get close enough when passing by the aliens … Also fix the 'path' thats visible around 1 min in … make it a
continual path."

**The path.**
- **What the strip was:** the 0:55 white strip is the lit path (`TILES["path"]`), not water. Along it,
  `terrain_mirror` reads water 0 and path 1. The aliens on it had been placed there on purpose (the path habitat).
- **The route:** it now runs from the bay's north shore to the shore of pond low1, one walk from water to water.
  Before, it climbed on into the north-west hills and ended on a bare hilltop.
- **The flight:** it follows the path to 0.85 of its length (was 0.5) and arrives over the pond.
- **The day colour:** warmed from #FFF5E2 to #FFDDA0, so it no longer reads as white water. It is still the same lit
  path, and the coordinator kept it.
- **Before/after:** `cpd-r4-path-*`.

**The other water, checked for dead ends:**

| Water | Before | Now |
|---|---|---|
| The river | Ended in dry land at the valley head (280, −1460) | Runs on to the north sea at (200, −1720); the flight's river follow is re-fractioned to 0.4294–0.7026, the same stretch |
| dawnStream | Its mouth ran into 50 m hills at (1440, −2330) | Carved through to the northern sea at (1490, −2880) |
| westStream | Hill source into the bay | Unchanged |

The sources stay in the hills.

**The cast** (28): it lives in the land now, not along the line.
- **Groups:** five groups of 2–3, on slopes and the valley sides.
- **Loners:** six, 30–160 m off the route, four of them on ridges at 24–67 m.
- **The bay:** two on the islands.
- **Gaps:** there are stretches where nobody is, such as 155–185 s on the night coast.
- **Habitats:** "wild" (home 24 m), "ridge" (home 14 m), "sea" (home 24 m), and "pass" for the close-pass aliens
  (home 2.5 m, so they cannot walk under the lens).
- **Checks:** every home was checked dry and off the banks on the mirror. The map is `cpd-r4-cast-map-before-after.png`;
  the planning script is scratch `r4/scatter.py`, seed 7.

**Close passes.**
- **The flight:** five low stretches at altitude 4–6 m: about 50 s (over the bay), 64 s (the path), 101 s (the
  sweep), 148 s (the night coast) and 196 s (dawn).
  - The extra waypoints sit on the existing curve, so the camera still looks where it travels: no turns toward
    anyone, no orbits.
- **The aliens:** each close-pass alien is 4.5–9 m off the line.
- **Largest on-screen alien height, as a share of the frame:**

  | Pass | Share | Note |
  |---|---|---|
  | 1 (day, over the sea) | about 0.19–0.22 | stays 9 m off, because 6 m would be water |
  | 2 (day, on the path, 64.4 s) | 0.41 (440 px) | lower left, at head height, as the camera sweeps past |
  | 3 (sunset, 101.2 s) | about 0.25 | |
  | 4 (night, 148.2 s) | about 0.26 | |
  | 5 (dawn, 195.7 s) | about 0.18 | |

- **How measured:** read off full-resolution frames with a ruler. The green-skin detector misses the white suits and
  fails in sunset and night light.
- **Why 3–5 stay smaller than 2:** they read smaller than the flight mirror predicts, and moving them from 6 m to
  4.5 m off the line barely changed them. The likely reason is the camera's height:
  - on rough or sloping ground the clearance (4 m over the highest ground within 10 m, plus the ring margin) holds
    the camera higher;
  - so the alien leaves the bottom or the side of the frame while still about 10 m away.
  - This is not yet proven. It needs CPD-R6's camera and body positions.
- **Limit:** no alien is closer than 4.5 m to the line (coordinator).
- **Lens and tiles:** no throw came near the lens, and the camera clipped no tiles, in any pass frame I checked.

**On water:** the count needs CPD-R6, the per-body position trace (02-requests.md).

**r4b: why passes 3–5 were small, measured on the CPD-R6 trace.**

- **What it wasn't:** the aliens weren't wandering.
- **Cause 1, the clearance held the camera high.** Over rough ground, `minClearance` 4 over the highest ground within
  `clearanceRadius` 10 (plus the ring margin) kept the camera 5–9 m above a close-pass alien's feet. The alien left the
  bottom of the frame while still 10–17 m away.
  - **Change:** `minClearance` 2.5, `clearanceRadius` 6. These are scene values.
  - **Guarantee:** the engine still holds the camera at least 3.6 m over the highest ground within 6 m.
  - **Effect:** the route over the ground is unchanged. The camera is 0.6 m lower on average and at most 3.3 m lower,
    only where the clearance used to bind.
- **Cause 2, passes 4 and 5 sat on the outside of their turns.** The camera aims about 2.5 s ahead along the curve, so
  it swung away from them and they left the side of the frame early.
  - **Change:** both moved to the inside of their turns.
- **Cause 3, bodies stand on 4 m tile centres.** That snap is what read as "wandering": 2–3 m from home. An alien could
  end up 4.0 m from the line.
  - **Change:** homes now sit on tile centres, with a 1 m home radius.
- **Pass 1:** the bay waypoint moved 5 m east, so pass 1 now passes its island alien at 5.5 m instead of 10.4 m.

**Distances on the trace:** the close-pass aliens pass at 4.9–6.2 m, and no body comes nearer the camera than 4.93 m.

**Largest on-screen height** (share of frame height, full-resolution frames with a ruler):

| Pass | Time | Light | Share |
|---|---|---|---|
| 1 | 48.9 s | day | ~0.27 |
| 2 | 64.4 s | day | 0.41 (the r4 clip) |
| 3 | 101.4 s | sunset | ~0.29 (feet below the frame) |
| 4 | 148.1 s | night, mid-throw | ~0.30 |
| 5 | 195.9 s | dawn | ~0.27 |

**Water** (trace on the final build): 0 frames grounded on water, 0 landings on water, 0 frames airborne over water.
The largest bank channel under any body is 0.

**Skimming** (`cpd-r4b-skimming-*`, draft6 against r4b at the five spots that dropped most outside the passes):
- **72.5 and 81 s** (the path leg, flown at 5 m): the nearest path tiles now fill the lower third, but the land ahead
  and the horizon still read. Nothing brushes the lens.
- **111, 189 and 204 s:** barely changed.

### CP-D r6: more aliens, nearer and running; the vale river; a coast-hugging intro (2026-11-11)

The owner on draft7: the aliens "are still too far away from the camera", there should be "more aliens", "too many of
them look like they're still just standing there idle", the throws look like "jumping randomly". Also: "adjust the
camera path to be a little closer to coastlines", and the path "still just ends in the middle of nowhere and its too
thin and doesnt read like a river".

**The timing audit.** I classified every airborne event in the song by cause.
- **Step hops:** on draft7's build, 52% were not throws at all. They were walking step hops: the walker hopping a
  `Jump` edge in its path.
- **The fix:** `"stepHeight": 3, "jumpTiles": 0`. The aliens now walk up steps, and step hops drop from 625 to at
  most 42; most of those 42 are crest throws that happen to last 1.3 s.
- **What is left is engine-side (CPD-R9).**
  - Every crest throw launches within 0.05 s of the planning surface's crest top.
  - The drawn tile under the alien differs. Landing springs and bodies running across tiles move it, so about 30% of
    throws pop 0.1–0.7 s off the visible top.
  - Engineering has it.

**The cast: 62 aliens.**
- **The near band (50):** loose groups and loners 9–26 m off the flown line all along the song, placed on the CPD-R6
  trace of the camera.
- **The rest:** 12 far ones and the 4 r4b close passes.
- **Home radius:** each near alien's is its distance off the line minus 5 m, so it can run but not under the lens.
- **Cost:** about 0.065 ms of frame time per alien. At 85 s the frame p99 sits at the 33 ms budget, so 62 is the
  ceiling.

**Running.** runChance 0.7, pauses 0.2–1 s, goals 1–8 tiles.

| | draft7 | r6 |
|---|---|---|
| Idle share | 0.35 | 0.21 |
| Running | 7% | 31% |
| Frames with an alien within 25 m of the camera | 7% | 25% |

**Water.** 0 frames grounded on water, 0 landings on water, 0 frames airborne over water. No body comes nearer the
camera than 4.92 m.

**The vale river.**
- The lit path is now `valeRiver`: 20 m wide in a 90 m vale, with banks (lip 0.5 m, step 1.0 m).
- It runs out of pond low1 (its source) down to the bay (its mouth). The flight follows it upstream.
- There is no lit path any more.

**The coastline.**
- The bay intro hugs the west shore and the north arm about 15 m off; the inlet follows its north shore.
- Median distance from the camera to the shore on the water legs went from 32–132 m to 12–28 m; the inlet went from
  88 to 40 m.

**Media:** `16-pass-cpd-r6/`.

**r6b** (coordinator: the aliens must read big, not just busy):
- **The near band** is now 5–14 m off the flown line. Its homes roam at least 4 m (three aliens are held at 1 m) and
  its goals are 1–3 tiles away.
- **Coast aliens:** eight on the shore beside the low bay and inlet legs.
- **Cast:** 63 aliens, with the far ones cut to 3.
- **Measured** (CPD-R6 trace on the final build, c63c):

  | | Share |
  |---|---|
  | Frames with an alien within 15 m of the camera | 16% |
  | Frames with an alien within 25 m | 32% |
  | Largest on-screen alien within 25 m | 0.21 median, 0.31 p90 |
  | Idle | 0.26 |
  | Running | 25% |

  Water 0 on every count; no body comes nearer the camera than 4.92 m.
- **Live at 85 s** (c63b, the same cast with the earlier roaming): frame p99 31.7–33.6 ms against the 33 ms budget.
- **Getting idle toward 0.2** without nearing the lens needs a lane-shaped home or a keep-out line (CPD-R10).
- **Sunset water:** the river is now #6A5CF2 and the pond #7E6CF0. The day-to-sunset blend had made the vale river
  grey-lilac at about 80 s; it now stays blue. As a side effect the main river at sunset is blue-violet, not pink.

### Draft9 (2026-11-11): the river ends, the shorelines, the spacing

The owner on draft8b: the river still "stop[s] in the middle of nowhere"; more aliens are needed along the shorelines
at 1:00–1:30 and 2:50–3:10; and the paired-up aliens should be spaced out.

**The river ends.**
- **What the owner saw:** the screenshot matches draft8b at about 58.4 s, the vale river's mouth into the bay. By day
  the sea near the shore is the same blue as the low land, so the cyan river reads as ending on open ground.
- **The vale river now runs lake to lake:** from low1 (enlarged to 90 m across) to a new lagoon by the bay (84 m
  across, banked). The flight's river leg starts at the lagoon.
- **The dawn stream** now runs from the inlet's sea, where its source used to sit on open ground.
- **The audit:** every 2 s, each river end is projected with the trace camera, out to 500 m. The before/after lists
  are in the hand-back.
- **Still unchanged:** the west stream's source (415–464 m away, in haze) and its narrow mouth into the bay.

**Shorelines.** 16 aliens were added on the river banks, the lagoon and low1 at 60–90 s, and on the inlet's north
shore at 170–190 s.

| Window | Within 15 m | Within 25 m |
|---|---|---|
| 60–90 s | 0% → 7% | 0% → 50% |
| 170–190 s | 0% → 11% | 0% → 16% |

**Spacing.** No two homes are within 20 m of each other. Eight aliens were relocated.

**Totals.**
- **Cast:** 53 aliens. Idle 0.28.
- **Water:** zero on every count.
- **Live at 85 s:** frame p99 33.5 ms, at the 33 ms budget.

**The dotted vertical line in the owner's screenshot.** It is the side wall of a tile step seen edge-on. It shows a
dot pattern in live captures (quality high and ultra), but not in the offline render. That makes it engine-side, in
the live renderer.
