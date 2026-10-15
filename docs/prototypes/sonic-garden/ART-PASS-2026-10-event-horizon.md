# Art pass 2026-10: Event Horizon

The art agent, 2026-10-14, branch `art/sonic-vfx-pass`. The owner's note: "it looks like it was possibly half
completed, it has potential but needs a stronger art direction (a black hole and an object/s spinning around it,
being absorbed into it, lots of cosmic chaos)".

## The concept found

- `SCENE-CATALOG.md` §1: "A black hole drinks a disk of light: every note is a star that falls into it, and the bass
  is the hole's own mass bending the sky." It specifies the following:
  - the composition: shadow on the right third, the disk on a shallow diagonal lensed over the shadow, a basalt
    fragment out of focus for scale;
  - the palette: void, ember, white-hot, jet blue;
  - the kill condition: "the disk still reads as dots or as a plate". DNGR's "anemic" thin disk was the target.
- `ART-RESTART-ANALYSIS.md` §16 judged it "an Interstellar homage", "an object in space" with "frontal, poster"
  framing. It noted that nothing gives a sense of scale and that the only foreground is one out-of-focus rock.
- `PROGRESS-vfx-art.md` row 16 adds only "the DF black-hole lens, the disk; the kick sends a shock ring".

What was on screen before this pass (`before/` stills):
- the disk was one orange on both sides;
- the lensed arc was a coiled spring of aliased rings;
- a fat cream photon ring floated inside a dark moat;
- the jets were popcorn;
- a single dull blob of rock sat in the frame;
- the sky was a muddy red-black nebula.

Nothing was happening to anything: the "object being absorbed" was not in the scene.

## Research

- **Interstellar / DNGR.** James, von Tunzelmann, Franklin and Thorne, "Gravitational lensing by spinning black holes
  in astrophysics, and in the movie Interstellar" (CQG 2015, arXiv:1502.03808).
  - The disk's far side is lensed over the shadow and under it.
  - A thin photon ring hugs the shadow.
  - The film deliberately removed Doppler beaming and the colour shift. A fully realistic disk was "exceedingly
    lopsided": the approaching side very bright and blue, the receding side dim and red.
  - The film added an IMAX veiling flare.
  - The disk is "anemic": thin and marginally optically thick.
- **EHT (M87\*, Sgr A\*).** The ring is brightest on its approaching side, from Doppler beaming. The asymmetry is the
  signature of a real black hole image.
- **Tidal disruption events** (Rees 1988, the NASA Goddard TDE visualisation of 2023, the Hubble AT2024tvd six-panel
  illustration):
  - A star that passes inside the tidal radius is stretched along its orbit ("spaghettification").
  - Its debris becomes a stream: about half is bound and swings back round the hole into a disk, and the rest
    escapes.
  - The fall-back heats the gas, and the event flares.
- **Relativistic jets** leave the poles. In beamed images the approaching jet is bright and the receding one faint.

## The art direction

**"Gargantua feeding."** A black hole is eating a star.
- **The shadow** is large and clean. A razor photon ring hugs it, and the lensed far side of the disk wraps over and
  under it as a smooth halo.
- **The disk is Doppler-beamed**, as the film's physics wanted and the film left out. The approaching side (left,
  sweeping into the frame toward the viewer) blazes gold-white. The receding side (right) sinks to deep red.
- **The victim** is a blue-white star on an inclined orbit (14.5 m, 14°, one lap in 40 s). It is stretched along its
  path and boils, with a soft corona. It bleeds a tidal stream of plasma that leaves along the orbit, swings round
  the hole and spirals into the disk, heating from star-blue to the disk's gold. Once a lap it crosses in front of
  the shadow, and once it goes behind and the lens smears it.
- **The wreckage** of its planets is scanned rock (Polyhaven boulders and stones) on slow inclined orbits. It tumbles,
  rimmed gold by the disk and blue by the star. It gives the scene scale, and something solid.
- **The chaos** comes from several sources:
  - jets from both poles;
  - sparks over the disk;
  - flares off the hot spot;
  - fragments torn off on every note;
  - a shock ripple on the kick.
- **The palette** is void black, a deep indigo sky with the galaxy's band, gold and ember in the disk, and the star
  and the jets as the cold counterweight.

## What changed

### `tools/sonic_vfx/scenes/event_horizon.py`

- **The disk is unlit.** The diagnosis that unlocked the pass: the first disk was mostly the disk light's specular
  sheen on black bands (F0 0.04 at a grazing angle under a 1600-unit point light). That is why it was the same
  orange on both sides, and why its own emission, and so any beaming, barely showed. Bisected by turning particles
  off and then the ramp off. The bands are now `unlit` and emit about 5 units.
- **Beaming in the disk program.** It reads the world position (fixed while the bands turn): D = saturate(0.5 -
  1.1 x/r), through a three-stop ramp, from red ×0.16 through gold to blue-white ×2.4–3.0.
- **The far side's streaks fade to their mean.** The screen-space lens magnifies the grazing sliver of the far disk
  into the whole arc over the shadow, and magnified streaks aliased into rings (the "coiled spring"). The lensed
  halo is now smooth.
- **The disk's turn matches its gas.** The twist's sense and the particles' orbit force turned opposite ways (the
  particle tangent is cross(up, toward-attractor)). Every orbit force is now negative, so all of it turns one way:
  the right side recedes.
- **The lens.** `horizonScale` went from 0.42 to 0.8 and `ringWidth` from 0.045 to 0.014: a big shadow and a razor
  ring that the halo hugs, where before a fat ring floated in a moat. The feather is 0.8.
- **New: the star.** A procedural sphere with an `ehStar` program: boiling granulation, hot torn tips and limb
  darkening. It is stretched 1.7× along its velocity. Its orbit position and yaw are seamless loop tracks; the yaw
  is unwrapped, so its last key is the first plus 360°. It has a corona (soft additive particles) and a point light
  that rims the rock blue.
- **New: the tidal stream** (`stream`, `streamers` with trails). Particles are born at the star, moving along its
  orbit (position and direction tracks), pulled by the hole with the orbit force, and spiral in.
- **New: the wreckage.** Five scanned rocks (`boulder`, `red_boulder`, `stone`) on 80–160 s inclined orbits, tumbling
  whole turns per lap. The old noise-blob rocks are gone.
- **The jets** are thin, faint, long beams (1400/s, stretched) instead of popcorn. The kick fires them.
- **The sky.** The matte nebula plane is gone: it was a full-screen interpreted fBm costing 8 ms of the frame. The
  sky's `stars` effect, with the galaxy's band, replaces it over an indigo-to-teal sky gradient, and it now fills
  the whole sphere (ADR-1350, below).
- **The note fragments** are torn off the star rather than spawned at a pitch-placed point. That point sat in the
  lens and smeared into white ladders.

### Engine: ADR-1350, Stars can fill the whole sky

The Stars effect drew only above the horizon (it was built for planet skies), so without the nebula plane the stars
stopped at a line across the middle of the frame.

- A `wholeSky` row draws the field on the whole sphere with no fade. The lower hemisphere is projected from the
  north pole.
- It is passed as `starsA.x` = 2. Off, every existing scene is unchanged.
- Test: `test_star_field.cpp` `[adr1350]`.

## The mappings (`sonicScene.vocabulary`)

| signal | what the world does |
|---|---|
| `response.sustain` (+ energy) | the disk heats: bands brighter, the gas denser and whiter |
| `response.note` / `notes.lastPitch` | each note tears a clump of fragments off the star (it flares). The pitch sets the fragments' speed: a low note swings wide, a high note plunges |
| `notes.lastVelocity` | how many fragments, and how bright |
| `notes.polyphony` | a chord tears a wider clump |
| `response.bass` | the hole's mass: the Einstein radius and the shadow swell, and the star flares and bleeds faster |
| `response.kick` | the photon ring flashes, the jets fire, and a shock ripple races out from the hole |
| `response.snare` | a flare from the hot spot (approaching side), and the star sheds a burst of plasma |
| `response.hat` | sparks over the disk |
| `notes.density` / rhythm | the gas churns: finer streaks and a faster orbit |
| `notes.tension` | the lens's colours split |
| `sonic.brightness` | the inner edge turns from gold to blue-white |
| `sonic.roughness` | the streaks clump and the gas boils |
| silence | the hole keeps eating: the star circles and bleeds, the disk turns, and the jets whisper |

## Frame time

GPU at 1080p, realtime tier, Trench from 60 s, under `tools/gpu-lock.sh`.

| | p50 | p95 |
|---|---|---|
| before (600 frames) | 18.3 ms | 20.6 ms |
| after (600 frames, more than the star's whole lap) | 13.4 ms | 18.0 ms |

The scene is inside the 60 fps budget at the median, where before it was not. Its p95 is over 16.7 ms when the star
and its stream are large in the frame, and it is well inside the 30 fps live target. Most of the gain is the nebula
plane: 8 ms by ablation. The star, the stream, the corona and the wreckage cost about 3 ms of it back.

## Media

`~/Desktop/av-gen-review/49-sonic-vfx-art/event-horizon/`:
- `before/`: stills with Trench at 12, 62 and 112 s;
- `after/`: stills with Trench at 12, 25, 62 and 112 s, and with the full test mix and MIDI at 4, 8 and 12 s;
- `event-horizon-trench-50-80s.mp4` and `event-horizon-full-midi-0-18s.mp4` (1080p30, with audio).

## Open

- When the star passes behind the hole, the DF lens can show its smeared image with hard, blocky edges near the
  lens's reach (the distortion offsets are half resolution). It is the lens's behaviour, not the scene's. A lower
  inclination (14°) keeps the star's pass behind the hole close to the disk, where the disk hides most of it.
