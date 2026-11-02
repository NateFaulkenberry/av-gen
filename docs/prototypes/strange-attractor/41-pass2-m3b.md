# Art Pass 2, M3b: the revision pass

The owner reviewed M3 ("heading in a good direction") and asked for a revision before anything new
(`39-pass2-m3b-owner-notes.md`; the working plan is `40-pass2-m3b-plan.md`). M3b builds on M3 (e99e6b93). The sequence
now runs the whole cut, 0-210.5 s: the UFO interior is cut, and the ocean's launch bay leads home to the canyon at
dawn.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3b/`, in numbered folders.

## Timing (bar 17 at 32.0 s, 1.9835 s a bar)

| Time | What |
|---|---|
| 0-32.0 s | the canyon by day; portal 1 in the east bank wall, its rim fizzing as it opens |
| 32.0-47.9 s | the Moon: the Earth first, bigger and glowing; the domes' rave beyond the rim |
| 47.9-69.7 s | home at dusk: the encounter round the fire; up to the outer cliff's portal |
| 69.7-101.4 s | Mars: the rover driving toward the buried block pyramids; the pyramid portal |
| 101.4-121.2 s | home at night: down to the gathering at eye height; one glide into the riverbed portal |
| 121.2-147.0 s | the ocean: rock, kelp, sponges, sea fans and grass, fish, five subs; into the launch bay |
| 147.0-210.5 s | home at dawn: out of portal 1's wall, up the river past the camp, rising as the music ends |

## The notes, one by one

### 1. Cut the UFO interior; end cleanly

- The bay's portal now leads to `@root`: the camera comes out of portal 1's wall (where the journey went in), at dawn.
  The interior scene (`strange-attractor-ufo.scene.json`) and its builder are gone.
- **The time of day.** Dawn: a low warm sun, a pale violet sky, the river's sheen. The craft and the Gray have gone;
  the three Sasquatches are still at the camp, the fire a thread of smoke.
- **The exit site.** Portal 1's wall. The camera leaves at the bay's speed and turns up the river past the camp,
  then along the water, rising to a high view as the music ends (a slowing climb, no cut).
- **The flight's pace.** Every new flight (the ocean, the dawn) is timed by `canyon_scale.pace()`: a smooth speed
  profile from the arrival speed, to a cruise, to the end speed, so there is no lurch at a knot.
- **The crash it found (ADR-1899).** A world reached through a portal searched for its own way out from time 0, when
  its camera path is still the source's camera carried through the portal. The ocean's found a false crossing home
  long before the camera arrived, and the two renderers handed the frame back and forth until the stack ran out.
  A world now searches from the camera's arrival, and the renderer refuses a hand-back.

### 2. The canyon scale POC

- **What it is.** The same canyon and journey at world scale 0.2 (the current is 0.4): the terrain is half the size,
  while everything at character scale (the Sasquatches, the fire, the craft, the props, the camera's eye height and
  its distance from the gathering) stays the same. The circled riverbed sides in the owner's framing come out at
  half the height and width next to the figures.
- **How.** `SA_WORLD_SCALE=0.2` runs the same builders. Each canyon placement is now a terrain anchor (`site()`, which
  follows the terrain at any scale) with character-scale offsets in real metres (`near()`, `knot()`). They write
  `poc-*.scene.json` and `strange-attractor-poc.json`. The current canyon is unchanged and both are kept: the owner
  chooses.
- **Side by side.** `02-canyon-scale-poc/` has the full sequence, POC left, current right, and stills at the same
  beats: the opening, the camp at dusk and night, and the portal approaches.
- **The fallback improvements (in both).** The bench round the camp is opened to one footing, 15 m round
  (`attractorWorld.pads`, ADR-1897): the steps that banded under the gathering are gone. It has desert scrub and
  sandstone rocks at character scale, a cave mouth in the bank behind the camp, and a seat rock.

### 3. The Sasquatches

- **Three individuals** (`pass2/build_sasquatch.py`), each with its own build, height, fur colour and length, face
  and age:
  - A: a dark-brown adult, standing, 2.35 m;
  - B: an old silverback, broad, crouching by the fire, 2.55 m, grey through its dark fur, longer fur;
  - C: a young, lean, auburn one, sitting on a rock, 2.2 m, shorter fur.
- **The clipping.** The scratch now passes in front of the chest, not through the body.
- **The sliding.** The legs are IK chains with their targets fixed at the ankles, baked. The measured foot drift
  over the 24 s loop is A 0.024 mm, B 0.009 mm, C 0.084 mm (`feet-*.json`).
- **A base pose each:** standing, crouching, sitting. The gestures are A's chest scratch, B pointing at the fire,
  and C looking round.
- `03-sasquatch-idles/`: front, side and feet-level clips of the 24 s loop, a sheet, and the feet evidence.

### 4. The portals

- **Rim particles (ADR-1898).** A `portalRim` particle node follows whichever portal is live. It is a ring (the
  disc's new `innerRadius` keeps the centre clear), gathers with the field, thickens as the tear opens, and stops
  spawning in the last stretch, so it has cleared by the pass-through. Everything is distance-driven (seek-safe). It
  is warm in the canyon and on Mars, pale on the Moon, and cyan at the bay.
- **The water glide.** One straight line at a constant 3.2 m/s, 25 degrees down, into the opening in the river. The
  ocean's exit frame is tilted by the same 25 degrees, so the camera comes out level, upright and at the same speed.
  The ocean flight then eases from 3.4 m/s to its cruise. Nothing steps.

### 5. The Moon

- **The rave.** Laser fans spill out of the domes (12 beams in 5 colours, sweeping), with magenta, cyan and green
  point lights strobing on the beat clock, the highs and the low onsets. Envelopes fall in a quarter second, so a
  strobe is a flash, not a shimmer. With audio off, the sources rest at 0 and the domes glow steadily.
- **The flicker metric** (`05-moon/flicker.txt`, 34-47.5 s; irregularity of the frame-to-frame difference):
  - audio off, whole frame: median 0.004, p99 0.200, max 0.481 (the camera's own motion, the dive);
  - audio off, top half (the Earth, the lasers, the domes): median 0.005, p99 0.217, max 0.435;
  - audio on, whole frame: median 0.005, p99 0.520, max 0.906; top half: p99 0.945, max 1.371.

  With audio on, the peaks are the beat strobes: deliberate flashes, one per beat, not shimmer or aliasing. If the
  owner finds them too hard for flicker sensitivity, the strobe ranges in `build_journey.py` (`beat_route`) are the
  dial.
- **The Earth.** Bigger (12 degrees across), brighter, high over the arrival, so it is the first thing seen out of
  the portal. It has a soft glow halo and a beat-pulsed datamosh: red and cyan copies a step out of register,
  flashing on the beat.

### 6. Mars

- **The rover.** NASA's Curiosity (public domain), driving at 0.25 m/s toward the pyramids. It lays tracks as it
  goes (38 m of old track behind it), and its mast camera flashes every 3.1 s. All of it is keyed in the timeline,
  so it is seek-safe.
- **The pyramids.** Coursed blocks like the Great Pyramid (about 76 courses), chipped and missing at the arrises,
  with a casing cap left on one, buried deep in sand (26-36% of their height).
- **More ground.** More rocks, four more craters, footings under the pyramids and the rover's route.

### 7. The night

- **Moonlight.** The canyon's key can now be a cold moon (`sunWarmth`, ADR-1900; it was a fixed warm sun, which made
  the night a dull orange). It is raised, and the sky fill lowered, so the moon casts the figures', rocks' and
  craft's shadows.
- **The fire.** It burns again, the warm, shadow-casting key at the gathering (13 m reach).
- **The framing.** The camera comes down from the cliff to eye height, 6-10 m from the fire, in a slow arc on the
  river side. The Gray stands across the fire from the three, facing them, in profile and then three-quarter front,
  inside the frame (the camera aims at the group's centre).
- **The craft.** It is parked behind the group on the opened bench, lit by its own lights: an underside glow with
  its spill on the ground, four rim lamps, and a brighter emission.
- **The owner's three frames.**
  - The terraced banding at the camp is gone (the pad).
  - The high angle with tiny figures now lasts only through the descent.
  - The empty-bank frame now has the camp's scrub, rocks and cave mouth.

### 8. The ocean

- **Kept:** the base and its lights.
- **Added:**
  - more seabed rock;
  - 13 clusters of kelp, sponges, sea fans, sea grass and basalt along the route;
  - three fish schools circling, and loose single fish;
  - marine snow.
- **The subs.** Five now: sub-4 rises out of the bay and away over the shelf, and sub-5 comes down from above and
  goes in.
- **The exit.** Much closer to the base, so the ocean leg is one unhurried run in.

## Engine

| ADR | What |
|---|---|
| 1897 | `attractorWorld.pads`: footings eased into the canyon's height field |
| 1898 | portal rim particles (`portalRim`) and the ring emitter (`innerRadius`) |
| 1899 | a world searches for its way out from the camera's arrival; no hand-back loop |
| 1900 | `sunWarmth`: the canyon's key from the warm sun to a cold moon |

## Open

- Mars: at the establishing distance the rover is small (a few pixels at 86 s); its tracks read better than it does.
- The cave mouth reads as a rock dome with an arch more than a hole in the bank: the bank behind it is lower than the
  probe assumed after the pad opened the bench.
- At night, a few rocks and shrubs on the dark slope behind the camp seem to float, because the slope itself is lost in
  the dark.
- The bay crossing (146.5-147 s) passes through the membrane's tearing blocks against a dark field before the dawn
  canyon appears.

- The Sasquatch fur is alpha cards: at 1080p with 2x supersampling they read as fur, but close up at low resolution
  they speckle.
- The craft passes close by the camera on the night descent (a fly-by, not a clip).
- The river's screen-space reflection shows a hard vertical edge in some dawn frames (seen since Gate 2; not new).
