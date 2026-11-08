# Musical Geography: art direction (Phase 1, art)

Art agent, 2026-11-08, branch `scene/musical-geography-art`. This is the look the scene is built to. It stops at
Gate 1. It is written against the engineering plan (`03-technical-plan.md` on `scene/musical-geography`), and every
request at the end maps onto a system that plan already proposes.

Gate 1 stills: `~/Desktop/av-gen-review/105-musical-geography/01-gate1/`. They are Blender mock-ups (Cycles on the
CPU, so the shared GPU stayed free), not engine frames. The engine has no tile terrain yet. The mock-up uses the same
model the plan proposes: one height per tile, flat tops, columns on a fixed lattice, colour by height. It is a proof
of the look, not of performance.

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
  - the **river** in the canyon floor;
  - small **lit pads** on mesa tops.
  At night the contours become the drawing itself: a topographic map of light over a violet land.
- **The tile is the unit.** A tile is always an object, not a pixel:
  - a small dark gap;
  - a thin 45° chamfer that catches light;
  - per-tile value jitter of ±2%.
  Formations are always made of many tiles (40 to 200 across), so you read the unit and the land at once, the way
  aerial photographs of salt pans and greenhouses read (Burtynsky, Bernhard Lang).
- **The land is authored.** Each formation is a designed shape with its own rule: the meander, the mesas, the
  terraces, the sea. Noise only roughens a formation; it never creates one.

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
| Smooth teal tile sheet | the chamfer and gap proportions; the calm of a smooth swell | the teal (cyan/teal is capped, §3) |
| Glass tiles with gold/blue edge outlines | the **edge** as the light carrier | outlining every tile (here only the edges that mean something) |
| Platform-jumper and party-game references | buoyancy, rounded friendly silhouettes, the scale of a figure against a platform | any specific game's look, characters, props, trees, gems, clouds |

---

## 2. Research that shaped it

- **Aerial geography.**
  - Low raking sun (10–30°) describes relief, and high sun describes pattern. A NASA photo-interpretation study found
    a sun elevation of about 30° gave the best mix of tone and shadow.
  - So day uses a 40° sun, and sunset and dawn use 9–13° suns for long shadows across the columns.
  - Burtynsky and Hegen use no horizon and soft light to turn land into colour fields. That is the model for the high
    passes over the terraces and the sea.
- **Cartography.**
  - Imhof's aerial perspective in maps: lowlands farther away, cooler and lower in contrast; contrast sharpening
    toward the peaks.
  - Swiss relief shading: light sides warm, shadow sides blue.
  - Scott Reinhard's vertical exaggeration.
  - From all of these: the hypsometric ramp, contour lines as the edge-light language, warm key and cool shadow as a
    fixed rule, and exaggeration only on the mesas and the canyon.
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
    so valleys and the sea fill with haze while mesa tops stay crisp.
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
- **Drift:** one slow world-space gradient (wavelength about 1,600 tiles) mixed in at up to 28%, so no two valleys
  share a tint.
  - **Rule:** the drift must be *analogous* to the colours it touches.
  - In the mock-up, a periwinkle drift over the pale-yellow mesa tops turned them grey-khaki. Complementary mixing
    desaturates.
  - In daylight the drift is therefore held to the lowlands.

**Teal cap.** No state may use a hue between 165° and 195° anywhere but the daylight shallows. In the frame that band
must stay under 5% of the pixels.

### Daylight: "porcelain atlas" (soft, bright, cool lowlands under a warm sun)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#5C7EE0` → `#E6E0F4` (lavender haze) |
| Fog (= horizon) / looking into the sun | `#D9D2EE` / `#FFF0D8` |
| Sun | `#FFF1DC`, 40° elevation, from the south-west, soft (1.5° disc) |
| Ambient | `#9EAAF0`, low (0.45) |
| Land ramp | `#8088E6` periwinkle, `#A890E4` lavender, `#EBA290` warm peach, `#EDB872` apricot, `#F0D480` pale yellow |
| Water ramp | `#1630A8` deep cobalt, `#2448D0`, `#3F70E8`, `#8DB4F6` crest |
| Shallows (the only turquoise) | `#3FB0C8`, within 9 tiles of the shore, mixed in at most 60% |
| Canyon wall | `#EE987C`, ±8% strata banding every 2.2 tiles of height |
| Path | `#FFF5E2` cream, unlit |
| Drift | `#8E9CF2`, at most 14%, and only below 18 tiles of height (see the drift rule below) |
| Edge light / emissive | off (0). Day is a colour field; light is the sun's |
| Exposure | −0.4 EV against the others (the bright state must not clip into AgX white) |

### Sunset: "ember map" (low and directional; violet shadows, coral light)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#33206E` → `#FF8A50` |
| Fog / into the sun | `#E07462` / `#FFB060` (Gurney's reverse aerial perspective: the distance glows) |
| Sun | `#FF9A4A`, 9° elevation, from the west; long shadows across whole columns |
| Ambient | `#5E46B8`, strong (0.9): the shadow side is violet, never brown |
| Land ramp | `#4E2A7E` deep violet, `#8A2C78` magenta, `#D0405A` crimson, `#F2785A` coral, `#FFB45A` orange |
| Water ramp | `#24164F`, `#4A2380`, `#8A2F8C`, `#E2507A` crest |
| Canyon wall | `#C8445E`, ±12% strata |
| Path | `#FFD7A0`, emissive 0.6 (the first lights come on) |
| Drift | `#6A3AA0` |
| Edge light | `#FFC27A`, 0.9 (contours appear as thin gold lines) |
| Crest light | 0.6 |
| Fog multiplier / exposure | 1.35 (the haze lives in the sky and the far ring, not the canyon) / −0.2 EV |

### Night: "map of light" (vivid but not dark; the lines carry the chroma)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#080A2C` → `#33237A` |
| Fog / toward the moon | `#2C1F66` / `#41308E` (fog is violet, so the blue lines separate from it); fog multiplier 0.85 (denser) |
| Moon (key) | `#C8B4FF`, 34° elevation, weak (1.2), still casting shadows |
| Ambient | `#5A38B0` (0.6). Mid-dark, never black: Gurney's "don't go too dark overall" |
| Land ramp | `#221A6C` deep indigo, `#36228E`, `#52289E`, `#7430B0` violet, `#9C44C8` high violet |
| Water ramp | `#0E1452`, `#18227A`, `#2443B0`, `#3F78FF` electric-blue crest |
| River | `#2E6BFF`, emissive 0.45 (a lit vein, not a white bar) |
| Contour edge light | `#5A86FF` electric blue, 7.0, with bloom |
| Wavefront (crest) edges | water crest colour, 5.0 |
| Path | `#FF5FB0` pink, emissive 5.0 |
| Mesa-top pads | `#FF4FA8` pink, 6.0, in small clusters on flat mesa tops only, about 1 in 50 five-by-five blocks |
| Exposure | +0.6 EV |

**Lit fraction target: 5–12% of the frame**, which is an artistic target to test, not a cited figure. More than 15%
reads as a neon grid.

### Dawn: "first light" (clear, pale, green lowlands, gold edges)

| Role | Value |
|---|---|
| Sky zenith → horizon | `#A6BCDC` → `#FFD9A4` |
| Fog / into the sun | `#F2DDB9` / `#FFE2A0`, thinner than sunset (fog multiplier 1.35: clearer air) |
| Sun | `#FFC874`, 13° elevation, from the east (the opposite azimuth to sunset) |
| Ambient | `#B8C8D8`, cool (0.55) |
| Land ramp | `#6FB57E` soft green, `#9CCC84`, `#DCE08E` pale yellow-green, `#F4D68C` golden, `#FBE9C4` warm cream |
| Water ramp | `#3E5E98`, `#5A7EB8`, `#86A6D0`, `#F2DCA8` gold crest |
| Canyon wall | `#E8A44A` amber, ±8% strata |
| Path | `#F2A93B` golden amber, 0.25 |
| Edge light | `#FFD27A`, 0.5 |
| Crest light | 0.4 |
| Exposure | −0.2 EV |

### Transitions

Driven by `dayPhase` on the timeline and never by audio (brief §7).
- The keys sit at four phases: dawn → day → sunset → night, then back to dawn.
- Every colour above is a curve key. Interpolate in **OKLab**: sunset to night through linear RGB passes through a
  grey-brown.
- The edge and emissive gains ramp on their own curves, so the contours "switch on" during dusk rather than
  cross-fading. The order is: path lights first (sunset), contours (dusk), pads (night).
- Audio rides on top. It may:
  - warm the terrain (a hue push of at most ±8° toward the accent);
  - lift emissive gain (×0.6 to ×1.6);
  - light the crest.
  It never moves the phase.

---

## 4. The tile language

| Property | Value | Why |
|---|---|---|
| Pitch | 1 unit (= one nav cell) | Everything below is in pitches |
| Top | 0.915 square (gap **8.5%**) | Reads as separate objects up close; the gap closes into a fine texture at mid distance |
| Chamfer | **0.045** at 45°, one segment | Catches a 1–2 px highlight up close. Any wider and it is a building block or furniture |
| Body | **Column** down to a floor, not a slab | Steps read as land. A slab field reads as floor tiles |
| Side shading | Fades to 18% of the top colour at the column's foot | Gaps and cliff faces get depth without an AO pass |
| Material, land | Roughness 0.62, specular 0.5, no texture | Matte, painted. The colour does the work |
| Material, water and flooded terraces | Roughness 0.16 | Sky reflections and glints. The water is tiles that shine, not a water surface |
| Per-tile variation | Value ±2.25%, hashed on the **lattice index** | Breaks the CG uniformity; must not change when the window recentres |
| Tops | Flat, one height per tile (engineering's model) | The stepped geometric read; tile = cell |
| Edge light | A band on the chamfer, 11% of the top's width, sharpened (power 1.6); **per side**, only on the sides that mean something (§6) | Thin lines, never full outlines |
| Emission | **Top face only** (never the column sides) | Sides lit from inside read as neon bars. That was seen and rejected in the mock-up |

**Distance behaviour.** Engineering flags aliasing at grazing angles as a risk, and the mock-up confirms the gap is
what aliases: a 0.085 gap at 5 px per tile is a sub-pixel line. Ask: fade the gap darkening and the edge band by
pixel footprint (an `fwidth` style fade), so far tiles average to their colour instead of moiré. LOD 1 (an unbevelled
box) is fine visually beyond about 150 tiles.

---

## 5. The formations and the map

The mock-up world is laid out like this. The engine version should keep the *relations*, not the coordinates.

```
              N
   mesas (3 levels, 22.5 / 29.5 / 36.5) with stepped cliffs,
   buttes standing off them; the canyon cuts through them
        |            ~~~ canyon (meander, river floor) ~~~
   rolling hills (domain-warped, 3–23 tall), the lit path climbing them
   coastal terraces: 1.1-tall steps, every third flooded (glossy, water-coloured)
  ~~~~~~~~~~~~~~~~~~~~~~~ indented shoreline (bays) ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   the sea: two crossing wave trains + a long swell, an illuminated wavefront
              S
```

| Formation | How it reads | Rule (mock-up values, in pitches) |
|---|---|---|
| **Water** | A broad, gently moving, glossy field. The colour climbs the water ramp with the wave, so crests are lighter, and the wavefront is a line of lit edges riding one train's crest | Trains of wavelength 34, 21 and 95 (amplitude 0.42, 0.26, 0.55), at about 70° to each other, so they interfere into diamonds. Crest light is brightest where two trains meet. Level −0.4. A shallows band within 9 of the shore |
| **Coastal terraces** | Tidal flats and paddies. Stepped bands following the coast, alternating ±7% in value, every third flooded: the strongest "art-directed" read in the mock-up | `floor(rise / 1.1)`, rise = 0.13 × distance inland. The belt width varies from 15 to 90 |
| **Rolling hills** | Broad swells and saddles, never bumps. The hypsometric ramp turns them into lavender-to-peach bands | Domain-warped fBm at wavelength 150 (warp 40), height 3–23, a small 45-wavelength roughening of ±1.5 |
| **Mesas and buttes** | Landmarks. Flat tops at three levels, cliffs 1–3 tiles wide, stepped every 2.2 (strata), buttes left standing off the main tables | A sharp mask (transition 3%), levels 22.5 + 7k. The levels are offset from the contour interval, so flat tops never draw stray contour marks |
| **Canyon** | The hero formation of the reveal. A meander with a river floor and terraced walls in the wall colour with strata | Centreline `12 + 34 sin(y/62) + 13 sin(y/27)`; floor half-width 4; rim half-width 11 + 0.3 × local height; 7 terraces in the profile; river emissive at night |
| **Path** | A one-tile-wide causeway, raised 0.35, following its own meander from the shore up the hills. Cream by day, amber at dawn, pink and lit at night | Its own spline, independent of the canyon |

**Coverage.** Over a typical window the mock-up gives water about 29%, terraces 6%, hills 38%, mesas 23%, canyon and
river 3.5%, and path 0.2%. That ratio is about right. Mesas above 30% become one plateau with holes, which was seen
and rejected.

**Avoiding repetition at world scale.** The world is endless, so these formations recur as **biomes along the
flight**. A slow region field picks which formation family dominates. Each family keeps its rule but varies its
parameters by region, so a second canyon is not the first one again:
- meander wavelength;
- mesa levels;
- terrace step;
- wave direction.

This is the plan's "region noise", but the art asks that it be **route-aware**: the camera's route is authored
through the biomes, not across random ones (§7).

---

## 6. Selective illumination

Five light carriers. Each one describes geography; none is decoration.

1. **Contours.** The edge of a tile lights on a side where the neighbour across that side sits in a *lower* contour
   band: `floor(h / interval)` differs. The interval is 3 tiles.
   - The result is continuous isolines, drawn on tile edges, that slide when waves move the ground.
   - This is the signature: the scene draws its own map.
   - Edge gain by state: day 0, dawn 0.5, sunset 0.9, night 7.
2. **The wavefront.** Edges on water tiles near one wave train's crest (sharpness 0.955–0.998), brightened where a
   second train crests too. The lit lines travel and cross. This is the brief's "wavefront of illumination following
   a crest".
3. **The path.** Top emission along the causeway. It is the line the eye follows through the frame, and the line the
   characters run.
4. **The river.** A low emissive vein in the canyon floor.
5. **Mesa pads.** Small clusters of lit tops on flat mesa tops only: the "gathering places" for characters, and the
   pink accents at night.

**Audio on light** (routes, never hard-coded):
- high band → edge gain (×0.6–1.6) and crest sharpness;
- onsets → a ring of edge light expanding from a point ahead of the camera (the plan's event ring), not a full-field
  flash;
- overall energy → emissive gain.

Most of the frame is always unlit.

---

## 7. The camera's journey

The camera follows the rule in `01-coordination.md`:
- one continuous flight;
- it looks where it travels;
- no orbits, hero shots or head turns at characters;
- dives and climbs are allowed.

The route is a loop through the biomes, so a long song never runs out of world. Section changes happen at musical
section boundaries. A **pedestal climb or a dive is the "cut"**.

| Leg | Altitude (tiles) | Focal | Speed (tiles/s) | Pitch | Composition |
|---|---|---|---|---|---|
| 1. Over the sea | 3–6, terrain-following over the waves | 24 mm | 7–9 | −5 to −8° | Horizon in the upper third, formations rising in the haze ahead; wave trains cross under the lens. *Still 01* |
| 2. The shore | climb 6 → 25 | 28 mm | 8 | −12° | Cross the flooded terraces diagonally, so the bands sweep through frame. *Still 05* |
| 3. Into the canyon | dive to rim height − 8, then follow the meander | 24–28 mm | 9–11 | along the floor | Walls give lateral parallax; bank into each bend at most 6–8°, proportional to the turn rate. Long sunset shadows across the floor. *Still 03* |
| 4. The reveal | pedestal climb out of the canyon to 70–100 | 35 mm, eased to 50 mm over 20 s | 15–25 (it reads slow) | −25 to −40° | The canyon, buttes and plateaus stacked. The big breath of the piece; the one held move. *Still 02* |
| 5. Over the tables | descend to 4–6 above a mesa top, then off the rim | 35 mm | 8 | −10°, dipping with the rim drop | Characters gathered on a pad are passed, not looked at. The rim drop is a dive. *Still 07* |
| 6. Hills and the path | terrain-following, 8–12 | 28 mm | 9 | −15° | Follow the lit path at night; contours flow toward the lens. *Still 04* |
| 7. Back to the sea | descend to 4 | 24 mm | 8 | −6° | The wavefront crossing at night. *Still 06*; the loop closes |

**Rules.**
- Focal length changes only during climbs and descents, and slowly: at most 1 mm/s.
- Speed is near-constant within a leg and eases at the transitions.
- Bank is at most 8°, and only from turn rate.
- The aim is along the smoothed path ahead (the plan's 2.5 s look-ahead), with a per-leg downward pitch offset.
- No top-down god's-eye pass is planned. It would break "looks where it travels" (see the open questions, §11).

**Depth of field.** Slight, and only at low altitude (legs 1 and 7): the foreground tiles soften, the reference's
macro feel. Never at altitude. A tilt-shift at altitude makes the world a miniature, and this world must feel
enormous.

---

## 8. Characters

The environment is the protagonist. Characters are punctuation.
- **Count:** 3–6, never a crowd.
- **Scale:** about **0.8 of a tile** tall, so a figure is smaller than the unit it stands on. From the high legs they
  are specks, and that is correct.
- **Silhouette:** soft, rounded, *buoyant* bodies:
  - a tapered capsule torso;
  - a separate head floating a small gap above it, with no neck. That gap is the "buoyant" signature, read at any
    distance;
  - short limbs, or none.
  No hats, clothes, props or faces. Not a mannequin, and not any game's mascot.
- **Material:** warm porcelain white (`#FFF3E6`), with a faint self-glow that follows the state's accent. At night
  they are small lights moving on the map.
- **Motion:**
  - idle float ±0.05 tile;
  - jumps with a long, slow apex (buoyant gravity scale);
  - landing on a tile pushes it down 0.3, it rebounds and rings, and a faint ripple runs into the neighbours.
- **Placement:** on mesa pads, the path, and terrace edges, where they explain the floating-platform idea.

*Still 07* shows the scale and a sprung tile, with placeholder figures (two spheres). The real body is a Phase 6
decision. If the existing Quaternius mannequin is reused, it gets the porcelain material and the floating-head
treatment, or it is not used.

---

## 9. The brief's "explicit artistic restrictions", and how each is avoided

| Restriction | How it is avoided |
|---|---|
| Uniformly flat terrain | Six formation families with their own height rules, 0–36.5 tiles of relief, and coverage targets (§5) |
| Noise as the only source of elevation | Every formation is a designed function (meander curve, terrace quantisation, mesa levels, wave trains). Noise only warps or roughens |
| Generic neon grids | Emission is top-only, on meaningful edges only, at 5–12% lit area, over a lit (not black) ground. Day has no edge light at all |
| Excessive cyan/teal | The hue band 165–195° is allowed only in the daylight shallows, under 5% of the frame. Night is indigo/violet with *electric blue* (about 220°) lines, not cyan |
| Dark or muddy materials | Night ambient is a mid-dark violet with +0.6 EV. Palettes interpolate in OKLab. Shadow sides are coloured (violet, blue), never brown or black |
| Minecraft-like block landscapes | Continuous heights (not block-quantised), gaps and chamfers, no textures, no grass-on-dirt sides, a column side shaded from its top colour, columns that read as land at 40–200 tiles per formation |
| Repetitive procedural hills | Domain-warped long wavelengths; hills are one family among six; region parameters vary by biome; the route is authored |
| Conventional low-poly scenery | Nothing but tiles and the few characters |
| Trees, buildings, rocks, decoration | None. Landmarks are formations (buttes, mesas, the meander) |
| Excessive particles | None planned. If the owner wants air, use haze only |
| Busy overlays, sci-fi HUD | None. The "map" is in the land itself |

---

## 10. Requests for engineering (relay to `02-requests.md`)

Numbered for the ADR range 1970–1979. Most fit inside systems the plan already proposes.

1. **ADR-1970: contour edges as a per-instance channel.** In the effector pass, compute four per-side weights per
   tile, `side_k = [floor(h/interval) > floor(h_neighbour_k/interval)]`, from the same `heightAt` at the four
   neighbours.
   - Store them where the material program can read them: four floats or a packed mask.
   - Parameters: `contour/interval`, `contour/offset`.
   - The material program then draws a chamfer band per lit side, from `localPosition`.
   - The same mechanism takes a fractional **crest** weight on water tiles (request 2).
   - Neighbour evaluation is four extra height calls per tile. It can be cut to the visible window, and its cost
     should be measured.
2. **ADR-1971: a crest weight per tile.** As the plan's crest channel, but per wave set, with `crestSharpness`, and a
   multiplier from a second set (the brightening where trains cross). It feeds the edge mask, not top emission.
3. **ADR-1972: the terrain palette, a little wider than §4.4 proposes.** DayNight keys for:
   - a **5-stop land ramp** with its own height stops;
   - a **4-stop water ramp** (by wave height);
   - shallows colour and distance;
   - canyon wall colour and strata amplitude;
   - path, river and pad colours and gains;
   - drift colour and amount;
   - edge colour and gain;
   - crest gain;
   - fog multiplier;
   - an exposure offset.
   OKLab interpolation for all colours, as proposed. Colour is from **absolute** height, never renormalised.
4. **ADR-1973: geography region channels the material program can read.** Water, shore distance, terrace index (for
   parity and flooding), canyon wall (with height for strata), river, path, and plateau-flatness (for pads). Most are
   in the plan's channel list; terrace index, shore distance and flatness are additions.
5. **ADR-1974: per-instance roughness.** Water and flooded terraces at 0.16, land at 0.62, from a channel. If the
   material program can already branch roughness on a field, this is only a check.
6. **ADR-1975: top-only emission and side shading.** In the material program: emission masked to the top and chamfer
   from `localPosition`, and the side colour fading to 18% toward the column's foot. This needs the column length
   known to the material, or a normalised local height.
7. **ADR-1976: a distance fade for the gap and the edge band** by pixel footprint, against the grazing-angle moiré.
8. **ADR-1977: fog.** Confirm ADR-347's horizon-coloured fog supports:
   - an **exponential-squared** distance curve, so near and mid ground stay clean and the far ring fades fully;
   - a height falloff (factor 0.45–1.0, rate 0.035/tile);
   - a sun-facing tint (power 6) with its own colour key.
   Tie the window radius to the 0.98 distance, as the plan says.
9. **ADR-1978: world-lattice coordinates in material programs.** Drift and per-tile jitter hash on the lattice index
   and world position, never the record index or camera-relative position. The plan already does this for randoms;
   it must also hold for `worldPosition` in the material.
10. **ADR-1979: flight route through named biomes.** `scene::Flight` control points that can name a formation target
    (follow the canyon's spline; climb to N over a mesa), so the art can author the route of §7 against features, not
    raw coordinates. Plus a per-leg pitch offset and a focal-length curve.

**Smaller asks, no ADR:**
- Plateau levels as a list (they are in the plan).
- The canyon and path splines exposed for routing the camera and characters.
- Tonemap: AgX with a medium-high-contrast look. The mock-up uses it, and base contrast was too milky in daylight.
- Bloom thresholded at 1.0 and low by default (0.25), raised to 0.6 at night.

---

## 11. Open questions for the owner

1. **The contour signature.** Is a topographic map drawn in light (still 04) the right signature, or too "data
   visualisation"? The alternative is lighting only the wavefronts, the path and mesa rims.
2. **Columns or floating slabs?** I chose columns because they read as land. The floating-platform physics might want
   **slabs over the water only** (a thin tile with dark water beneath), so landings there visibly float. The two can
   coexist: columns on land, slabs at sea.
3. **Daylight saturation.** The day state is deliberately soft and pastel (porcelain, lavender, apricot). Does the
   owner want it brighter and more saturated, closer to the rainbow-tile references?
4. **Characters.** Abstract floating-head figures (proposed), or the existing mannequin re-skinned?
5. **A god's-eye pass.** A slow top-down pass over the terraces or the sea is the most "geographic" shot, but it
   looks down, not where it travels. Allowed as a short exception, or not?
6. **Where the day starts.** The brief's §15 moment starts at sunset and runs to night. Should a song's arc be
   sunset → night → dawn (ending on first light), or dawn → day → sunset → night?

---

## 12. Gate 1 stills

`~/Desktop/av-gen-review/105-musical-geography/01-gate1/`, 1920×1080, Blender 5.2 Cycles (CPU), AgX Medium High
Contrast, light bloom. The generator is `gate1-mockup.py` in this folder: a throwaway. It implements the formation
rules of §5 and the palettes of §3 exactly as written, so engineering can read the numbers in context. It uses no
third-party assets.

| File | What it proves |
|---|---|
| `01-day-low-pass-water.png` | The low pass over the sea at 24 mm: glossy cobalt tiles, crossing wave trains, formations in the haze |
| `02-day-high-reveal.png` | The high reveal: the canyon meander with its river through mesas and a butte; hypsometric lavender-to-yellow |
| `03-sunset-canyon.png` | Sunset inside the canyon: magenta lowlands, coral cliffs, violet shadows, gold contour lines, a slight bank |
| `04-night-edge-light.png` | Night: electric-blue contours on violet land, the pink path, the river vein, pink pads on the tables |
| `05-dawn-terraces.png` | Dawn: green and gold terraces, flooded paddies, the amber path, side light from a low eastern sun |
| `06-dusk-wavefront.png` | Dusk (55% sunset → night): the illuminated wavefronts crossing the water, the path lights already on |
| `07-sunset-plateau-figures-scale.png` | Tile proportions up close (gap, chamfer, column), figure scale at 0.8 tile, and a sprung landing tile |

**What the stills do not show yet:**
- In still 07 the floating-head gap on the placeholder figures is too small to read.
- The sprung tile under the left figure is only 0.32 deep, so it barely reads at that angle.
- Both are Phase 6 work.

**Creative Critic.** It was run on the seven stills (`critic submit --mode fast --images`, job
`job_1a1bb87a0a703cad4`). It evaluated nothing, because with no scene, shots or audio no rule applies to loose
stills. It becomes useful from CP-A on, with engine renders. The judgement above is by eye.
