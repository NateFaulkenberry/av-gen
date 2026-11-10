# Musical Geography: what the music does right now (draft3), measured

Written 2026-11-10 by engineering, for the owner's question: *"explain what sort of audio reactivity is going on in this
scene right now, cause it doesn't look like it's reacting to the music much"*, and *"also explain how the block edge
glow effect works right now and whether it's tied to audio reactivity currently."*

## The short answer

You are right: it barely reacts, and the main reason is a wiring mistake on my side, not taste.

- **Most of the music's connections are dead in draft3.** Of the seven music connections on the landscape, four
  listen to the analysed hits and levels: the low thumps, the attacks, the mids and the highs. Those signals only
  exist when the project switches the analysis on, and the Musical Geography project never did. So the swell never
  rises, no ring ever drops, the path never brightens, and the crown lights never change. The same goes for the
  aliens' "hop higher when it's loud".
- I should have caught this in the audio phase. My test clips proved the beat-driven parts, the contour flashes, but
  I never checked these four.
- **What does work:**
  - the beat;
  - the bar;
  - the overall "how busy is it" level.
- **But each of those lands where it hardly shows:**
  - the contour flash at night, on lines that are already white;
  - the contour lines creeping uphill once per bar;
  - a slow breathing of the hills.
- **One line in the project fixes the dead connections.** I tried it: the whole song rendered with the analysis on,
  and the measurements below come from that run. Even switched on, the effects are modest at the flight's distance.
  The menu at the end lists what would make the music unmistakable.

Everything below was measured on draft3's whole-song Feline Footwear run, rendered three ways from the same flight:
- **music:** draft3 exactly as shipped;
- **silent:** digital silence;
- **music with the analysis on:** the one-line fix.

## 1. Every audio-driven thing in the scene, and what it actually did

The camera flies at a typical 10–30 m above the ground, looking 30–120 m ahead. "How much the ground moved" below is
the largest height change the music caused, against silence, among nine points across the view at 30, 60 and 120 m
ahead.

| What listens | To what | What it moves | Draft3 as shipped | With the analysis on |
|---|---|---|---|---|
| The swell (all wave trains) | low thumps (kick and bass attacks) | wave height, ×0.35 at rest, up to +0.6 | **dead**: stays at 0.35 | 0.35 to 0.94; about 2.4 lifts a second; mostly the sea, because the waves barely touch land |
| Rings of ripples | each attack | a ring dropped 150 m ahead | **dead**: no rings in 210 s | 85 rings in the song; the list is full most of the time |
| The hills breathing | how busy the music is (smoothed over 1 s) | the hills rise and fall | **works**: 0.12 to 0.30, changing no faster than 0.07 a second | same |
| The contour lines' position | the bar | the lines climb one step uphill per bar | **works**: one step every bar | same |
| The contour lines' brightness | the beat | brightness ×0.7 at rest, flashes to ×1.5 and falls over a third of a second | **works**: 2 flashes a second (121 BPM) | same |
| The path's glow | the mids | brightness ×0.4 at rest, up to ×1.6 | **dead**: stays at 0.4 | 0.4 to 1.6, p95 1.5 |
| The crown lights (pads) | the highs | brightness ×0.6 at rest | **dead** | still flat at 0.6 (hypothesis: the levels are taken from the bass stem, which has no highs) |
| The aliens hop | the beat | a resting alien sets off with a hop | **works**, but see below | same |
| The aliens' hop height and landing force | loudness | higher hops, harder landings | **dead** | live |
| Camera, sky, fog, colours | nothing | the arc (day → sunset → night → dawn) is on the song's timeline, not on the sound | — | — |

**How much the ground moves, against silence (95th percentile, metres):**

| Song part | Draft3, 30 m / 60 m ahead | With the analysis on |
|---|---|---|
| Day, 0–66 s, over the sea and the shore | 0.08 / 0.13 | 0.67 / 0.78 |
| Sunset sweep, 66–108 s | 2.2 / 2.0 | 2.2 / 2.1 |
| Night climax, 128–180 s | 2.2 / 2.1 | 2.2 / 2.3 |

In the hills, the 2 m is the hills' slow breathing (driven by how busy the music is), not a beat you can see. Over the
sea, which is where the swell lives, draft3 moves the ground 8 cm. A 4 m tile 30 m away is about 140 pixels wide at
1080p, so 8 cm is about 3 pixels: invisible.

**The aliens (measured).** They land about 3.1 times a second in total with music and 2.8 with silence, across the
whole cast. They already hop on their own every 0.6–2.2 s, so the beat cue only adds about 12% more hops, scattered
across the field. It does not read as "they jump on the beat".

**Frame differences, music against silence at the same instant:** `07-audio/cpd-r2-reactivity-music-vs-silent-draft3.png`
and `07-audio/cpd-r2-reactivity-with-sonic-block-vs-silent.png`. Each row is a timestamp (30, 60, 90, 150, 200 s in
the first; 30, 60, 150 s in the second). The columns are the music arm, the silent arm, and the difference ×2.

| Time | Draft3: average change, % of pixels changed | With the analysis on |
|---|---|---|
| 30 s, day sea | 1.1, 0.9% | 7.3, 25% |
| 60 s, day path | 4.5, 14% | 10.3, 28% |
| 150 s, night | 14.9, 32% | 18.3, 37% |
| 200 s, dawn | 4.3, 16% | 5.2, 19% |

Most of draft3's difference is the hills sitting at a slightly different height (the breathing) and the contour
lines sitting a step further along (the bar). Those are position differences in a still frame, not motion you see in
time with the music.

## 2. Why it looks weakly reactive

| | Finding | Measured or hypothesis |
|---|---|---|
| 1 | Four of the seven landscape connections, and the aliens' size reaction, receive nothing: the analysis is never switched on. | **Measured** (silent above), and the one-line fix shows they move when it is on |
| 2 | The beat flash lands on lines that are already at full white at night, so doubling them changes almost nothing. In the day its brightness is ×0 (the day has no contour glow), so 0–66 s shows no beat at all. | **Measured**: at night the glowing-pixel share is 0.94% with music and 0.95% silent, and their frame-to-frame swing is 0.17 against 0.14 |
| 3 | The swell, even when on, mostly moves the water. Over land the waves are set to 8% of their size, so the hills do not pump. | **Measured** (configuration, and the movement table) |
| 4 | What does move on land is slow: the breathing is smoothed over a second and changes at about 0.07 a second, and the contours step once per bar. | **Measured** |
| 5 | The rings land 150 m ahead, where the fog and the tile size make them small. | **Hypothesis** (not separated in a frame yet) |
| 6 | The aliens' beat hop is diluted by their own constant hopping. | **Measured**: +12% landings |
| 7 | The crown lights would stay flat even with the analysis on. | **Measured**; why is a **hypothesis**: the highs are taken from the bass stem |

## 3. The tile edge glow: how it works now, and whether it listens to the music

**What it is.** There are three different glows on the tiles. Only one is the "edge" glow.

1. **Contour edges, the glowing lines along tile sides.**
   - The engine divides the ground into height bands, 4 m apart.
   - Wherever one tile sits in a higher band than its neighbour, the engine marks that shared side.
   - The tile material then draws a thin bright line, about 6% of the tile's half-width, along the top edge of each
     marked side.
   - So which edges light is decided by height: a line runs wherever the ground crosses a 4 m band. These lines move
     when the ground moves: waves, breathing, a landing.
   - The sea is excluded, because its waves crossing the zero line drew closed blobs.
2. **The crest light:** a glow on the highest wave crests. It is off in every state; art set it to 0 because it read
   as polka dots or white blocks.
3. **The path glow and the crown pads.** These are not edges: whole tile tops on the lit path, and some tiles on the
   flat crowns at night.

**How bright an edge is** is the product of:

1. **The time of day** (art, `look.py`), as the edge brightness:
   - day: 0, so there are no edge lines in daylight at all;
   - sunset: 0.45;
   - dusk: 4;
   - night: 7;
   - dawn: 0.5.

   The timeline blends between them along the song.
2. **The distance** (art): full brightness to 250 m, falling to 15% by 800 m, so the far field does not glare.
3. **A live brightness dial for the edges** (engine; its rest value is art's choice, 0.7).
   - In draft3 this dial is the one thing the music touches: each beat flashes it to 1.5, falling back over about a
     third of a second.
   - There are equivalent dials for the path, the crest and the pads.
4. **The top face only:** the glow is on the tile tops, never on the sides.

**Engine vs art.**
- **Engine:**
  - finding which sides cross a band, on the GPU, every frame, as the ground moves;
  - the line-drawing step in the material;
  - the four live brightness dials;
  - the music connections themselves.
- **Art (`look.py`):**
  - colours;
  - per-state edge brightness;
  - band spacing (4 m);
  - line width;
  - distance fade;
  - the dials' rest values.

**Shading and dither.**
- The shading in the gaps (ambient occlusion) darkens the tiles' lit surfaces near the gaps, close up only, and fades
  out with distance. It does not dim the glow lines (I believe emission is added unoccluded; not re-checked for this
  note).
- The output dither only breaks up colour banding in smooth gradients; it has no visible effect on the lines.

**Is it tied to the music now?** Two ways, both weak (measured):

- **Position:** the bands slide up the hills by one band per bar, so each bar a different set of edges lights. This
  is visible as a creeping of the lines (the night row of the difference image), not as a pulse.
- **Brightness:** the beat flash. At night the lines are already saturated white at the rest value, so the flash
  barely changes them: 0.94% against 0.95% glowing pixels, music against silence. In the day the edge brightness is 0,
  so the flash multiplies nothing. At sunset and dawn it is 0.45–0.5, too dim to read at flight distance.

**What wiring a clearly musical edge glow would take:** no new engine work.

- Lower night's edge brightness so the beat flash has headroom: rest about 2–3, flashing to 7.
- Give the day and sunset edges a small brightness so there is something to flash.
- Optionally drive the band spacing or offset from the low thumps, so lines ripple through the hills on the kick.

All of these are art settings plus routes.

## 4. What would make the music clearly visible (not built)

GPU costs are estimates against the measured whole-song cost: GPU p99 15.8 ms at 60 fps.

| Option | What you'd see | Who | GPU cost |
|---|---|---|---|
| A. Switch the analysis on (one line in the project) | The swell, the rings, the path glow and the hop height start working. Measured above: the sea moves 0.7 m on the kicks instead of 8 cm, and the path pulses 0.4 to 1.6 | engineering (1 line in art's `build.py`) | 0 (the analysis runs on the CPU, well under 1 ms) |
| B. Let the swell reach the land | Raise the waves' land share from 8% to about 40–60%, so the whole lattice pumps on the low end, not only the sea | art | 0: same kernel |
| C. Give the beat flash headroom, and edges in the day | Rest lower, peak higher, day/sunset edges above 0. The contour lines visibly pulse 2× a second | art | 0 |
| D. A beat-locked ring at the camera's feet | Instead of 150 m ahead, drop the ring 40–60 m ahead (where tiles are big) on the kick or downbeat | art (one setting) | 0: the rings exist |
| E. Choreographed hops | Longer rests and all aliens waiting for the downbeat, so they jump together on the bar | art (hop settings) | 0 GPU, negligible CPU |
| F. A spectrum across the land | Lattice rows near the camera rise with the spectrum bands, an equaliser in the terrain | engineering (new ground term reading the spectrum) | about +0.1–0.3 ms (estimate) |
| G. Light and post on the kick | A short exposure or bloom lift and a fog-brightness pump on low thumps | art (routes) | about 0 |

**My recommendation:** A, B and C first. They cost nothing on the GPU, and together they should make the day sea and
the night lines move with the kick and the beat. Judge a clip of those before F.

## Evidence

All under `~/Desktop/av-gen-review/105-musical-geography/07-audio/`:

- `cpd-r2-reactivity-music-vs-silent-draft3.png`: draft3 as shipped against silence, 5 timestamps, with the difference
  ×2.
- `cpd-r2-reactivity-with-sonic-block-vs-silent.png`: with the analysis on, against silence.
- Per-frame values of every connection's output, music and silent arms, whole song: engineering's scratch traces from
  a temporary diagnostic build (not committed). The numbers in the tables come from them.
