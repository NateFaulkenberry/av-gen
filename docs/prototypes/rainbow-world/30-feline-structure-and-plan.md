# Rainbow World 30: Feline Footwear, the structure map and the video plan

Brief: `29-brief-feline-video.md`. Branch: `art/rainbow-world-feline`.

- **Plot:** `stems/06-feline-structure.jpg` (full size: `~/Desktop/av-gen-review/77-feline-footwear-video/structure-map.png`).
  It shows the drums, bass and synth levels, the synth's brightness, the phrase lines and the proposed scans.
- **Scripts:** `scripts/feline_swells.py`, `feline_swell_table.py` and `feline_structure_plot.py`.

> Status: approved 2026-10-21 (decisions: the offline example plays the Feline mix only; 18 scans at three sizes; the
> look table as proposed; the landing by `splineOffset` first). Built to gate 1, which has four clips in
> `~/Desktop/av-gen-review/77-feline-footwear-video/`. The project and its generator are in `~/Desktop/Feline
> Footwear/`.

## 1. What a "swell" is in this synth stem (measured)

I cannot hear the stem; this is read from its envelope and spectrum. A level smoothed over a beat removes the kick's
sidechain pump. Brightness is the share of energy above 2 kHz, plus the centroid.

- **Intro (20-47.8 s): the swells are the calls.** Each one is a level rise from silence to -23 dB with a brightness
  peak. The 34-37 s call is the largest of them: it rises from 33.5 s, peaks at 35.8 s and is gone by 37.1 s.
- **Grooves (47.85-182.7 s): the bed's level is flat (-18 dB).** Each swell is a **filter swell**: a brightness bump
  rising 0.3-1.9 s to a peak 2-4x the bed's brightness, lasting 1-4 s. Most phrases have one.
  - **On the phrase in groove A:** onset 0.4-1.1 s after the downbeat (bars 1 + 4k), peak 1-2 s after it. A short
    level duck sits on the downbeat (2-8 dB) and the level climbs back as the filter opens.
  - **Before the phrase in groove B (119-135 s):** a pickup peaking 0.5-1 s before the downbeat.
- **The owner's list matches the stem's swells phrase for phrase.** Three need a reading:
  - **"~2:29" is 2:39:** the brightest swell in the song, rising from about 157 s to a peak at 161.7 s and gone by
    163.7 s.
  - **2:31 is not on the downbeat**, as the owner heard: a bright zap on the 150.99 s downbeat, a 3 dB duck, then
    the body swells late, with brightness bumps at 152.8 and 153.2 s, releasing by 154 s. So 151.2 rising to
    153.2 s.
  - **1:47-1:51 is the build into section B:** the drum fill (bar 56, 109.3 s) and the bass's octave fill, landing
    on 111.32 s. The synth's own swell follows at 111.9 rising to 112.9 s. The scan rises with the build from
    107.3 s and peaks on the swell.

## 2. The structure map

Bar *n* starts at 0.2425 + (n - 1) x 1.98347 s. Phrases are 4 bars (7.93 s), at 121 BPM in F minor.

| Section | Time | Bars | What happens | Energy |
|---|---|---|---|---|
| Intro | 0.24-8.18 | 1-4 | drums alone | 1 |
| Bass pedal | 8.18-20.1 | 5-10 | the bass enters on an F pedal | 2 |
| Calls | 20.1-39.9 | 11-20 | falling synth calls (20.1, 28.0, 31-32, **34-37**); the clap enters at 28.0 | 3 |
| Build | 39.9-47.85 | 21-24 | the bass starts F-Ab-Bb-F; calls at 44-45 | 4 |
| **Drop: groove A** | **47.85**-101.4 | 25-51 | the chord bed at full level, the tresillo bass, a filter swell every phrase; the low synth layer joins from 72 s | 7 |
| Fills | 101.4-111.3 | 52-56 | drum fills at 101.4 and 109.3 (kick and clap drop out); the **1:47-1:51 build** | 6 to 8 |
| **Groove B** | **111.32**-150.99 | 57-76 | 16th hats; the swells become pickups | 7 |
| **Lift** | 150.99-166.86 | 77-84 | the synth brightens to the song's peak (160.9 s); **2:31 late, 2:39 the biggest** | 9 |
| Pedal | 166.86-182.72 | 85-92 | the bass back on F; the synth darkens (172-182 s) | 6 |
| **Drums stop** | 182.72-194.6 | 93-98 | a riser (183-190 s); the bass stops at 193.5 | 5 rising |
| Synth alone | 194.6-210.5 | 99-106 | the synth fades from about 200 s | 3 to 0 |

## 3. The scans: 18, one per swell, three sizes

Each scan is baked from its swell's envelope:
- the scan's amount rises over the swell's rise, holds through its peak and releases with it;
- the front's speed is the swell's rise: it reaches its radius as the swell peaks;
- grid snap: an onset within 120 ms of a 16th is snapped to it, except 2:31, which follows the stem.

**Three sizes keep 18 from repeating:**
- **Full (3):** the drop, 1:47-1:51 and 2:39. A world scan as now: the front to 980 m, a full hold, every tree in
  reach twirling and bursting.
- **Wide (8):** the front to about 450 m, a partial amount (0.6-0.8), a shorter hold. The fireworks only within the
  front's reach.
- **Local (7):** a sweep to about 180 m round the plane, amount 0.4-0.5, released with the swell. A ripple of colour,
  and twirls on the nearest trees only.

The intensity within each size follows the swell's measured brightness ratio.

| # | Owner | Onset - peak - end (s) | Size |
|---|---|---|---|
| 1 | 0:34-0:37 | 33.5 - 35.8 - 37.1 | wide (the intro's one, a preview) |
| 2 | 0:48-0:50 | 47.85 - 48.7 - 50.2 | **full** (the drop) |
| 3 | 0:56-0:58 | 56.2 - 56.6 - 58.5 | wide |
| 4 | 1:04 | 64.8 - 66.0 - 66.8 | local |
| 5 | 1:12 | 72.3 - 73.9 - 74.6 | local |
| 6 | 1:20 | 80.2 - 81.4 - 82.5 | wide |
| 7 | 1:28 | 88.6 - 89.8 - 90.4 | local |
| 8 | 1:36 | 96.4 - 97.0 - 98.0 | local |
| 9 | 1:44 | 104.1 - 105.7 - 106.3 | wide |
| 10 | 1:47-1:51 | 107.3 - 112.9 - 113.7 | **full** (the build into B) |
| 11 | 1:59 | 117.8 - 118.1 - 119.6 | local (a pickup) |
| 12 | 2:07 | 125.7 - 126.5 - 127.6 | wide |
| 13 | 2:15 | 133.8 - 134.2 - 135.5 | local |
| 14 | 2:23 | 143.8 - 144.6 - 146.4 | wide |
| 15 | 2:31 | 151.2 - 153.2 - 154.0 | wide, off the grid |
| 16 | 2:39 | 157.0 - 161.7 - 163.7 | **full** (the song's brightest) |
| 17 | 2:47 | 166.5 - 167.0 - 168.5 | wide |
| 18 | 2:53 | 171.0 - 172.3 - 173.2 | local; it hands over to the ending |

The scan's 12 s refractory time is live-only. The baked timelines have no such limit; the shortest gap here is 4.6 s
(1:44 to 1:47).

## 4. The video, by section (all baked in the Feline project; seek equals play)

- **Camera (the ride's timeline, retimed per section):**
  - a calm glide in the intro and the calls (about 0.8x);
  - faster into the build;
  - the rim flip landing on the drop (it does now);
  - groove pace through A and B, the soar in the lift;
  - slowing through the riser into the landing.

  The fireworks' camera timelines (camX/Y/Z) are regenerated from the retimed ride, so the fields stay on the camera.
- **Looks:** the look changes on section boundaries, baked instead of the live auto-advance:

  | Section | Look |
  |---|---|
  | Intro | Bright Noon |
  | Calls and build | Golden Hour |
  | Drop | Prism |
  | Groove B | Electric |
  | Lift | Candy |
  | Pedal | Lavender Dusk |
  | Ending | After the Storm |

  The exact assignment is to be tuned at the gate stills.
- **Energy:** a baked per-section energy curve (section 2's last column) scales the arch pulse density and gain, the
  cloud splashes' amount and the fireworks' brightness. It works through route depth, so the reactive layer is
  unchanged and only its scale is baked.
- **The arc:** calm and sparse, then the calls, the drop as the first big moment, groove A's pulses, the build into
  B, groove B, the lift peaking at 2:39, darkening, the ending.

## 5. The ending, 2:53-3:30 (172.5-210.5 s)

All of this exists already; no engine change is planned. It escalates as the plane lands.

| Time | Escalation |
|---|---|
| 172.5-182.7 | It begins under the last groove: a light posterise (`post/display/posterize` 32 to 12 levels with `dither`) and the first glitch blocks (`post/glitch/amount` 0.05) |
| 182.7 (drums stop) | Temporal datamosh on (`temporal/mosh`: amount, block, smear, ADR-1049); pixelate grows (`post/display/pixelate` 1 to 24 px); RGB split (`post/split`, spectral); a pixel sort pours down (`post/sort`) |
| 183-190 (riser) | **Scene distortion:** the world-space sine deformers on the land and everything on it (the retired heave, re-enabled at amount 0 in the offline scene) driven up by a timeline, plus `post/lens/distortion` and `temporal/feedback` zoom. Tears grow (`post/glitch/tear`) |
| 194.6-210.5 (synth alone) | The posterise falls toward 3-4 levels, the mosh holds the frame, and `post/grade/gain` fades to black by 210 s |

- **Landing:** from about 190 s the ride slows (its timeline keys) and descends with `camera/splineOffset`'s normal
  component, onto a meadow stretch chosen low on the spline. It touches about 1.5 m above the grass at about 205 s
  and rolls to a stop as the picture crushes away.
- **Seek equals play:** the post glitch and display passes are stateless. The temporal mosh is an FIR over clean
  frames (ADR-410), so a seek warms it up.
- **The flare:** a nose-up is not a parameter today. The spline camera's aim is offset with its position. If the
  landing reads flat without one, it would take a small additive camera parameter, flagged first.

## 6. Layout (the owner's structure)

- **In the repo:**
  - **`rainbow-world.json`, live:** as now (live ceilings, latched triggers, no audio file), Examples > Live.
  - **New `rainbow-world-offline.json`:** full quality and song-agnostic. Its reactivity is the routes on whatever it
    is fed (stems optional, mix fallbacks), with the latched trend scans as live has, and no baked Feline timings. It
    goes under Examples > Worlds.
  - **`rainbow-world-feline.json` is removed outright,** and `test_rainbow_world_music`'s "showcase = live minus the
    scans" check is replaced by "offline = live minus the live ceilings".
  - The index entries, the menu check, `test_examples` and the README (how to build, and the "needs generating" state
    before `build.py`) are covered.
- **`~/Desktop/Feline Footwear/`, not committed:**
  - `feline-footwear.json`, with absolute paths to the generated offline scene and to the track and stems in the main
    checkout;
  - its generator `make_project.py`, which reads the structure map and writes the baked layer;
  - `README.md`.

  Loading a project whose scene and audio live elsewhere already works: every review project this pass used
  absolute paths from `build/`. I will confirm it from the Desktop folder first.
- **Scene distortion** needs the land's deformers back in the offline scene at amount 0. It is a scene change, kept
  small, and coordinated with the perf branch, which touches `build_world.py` LOD only.

## 7. Questions

1. **The offline example's audio.** Index entries need a track. I propose `feline-footwear.wav`, the mix only and
   reacting live-style, without the stems or any baked timing. It is gitignored like every track. Or Trench or
   Hyperborea instead?
2. **The scan count.** Is 18 at three sizes OK, or should some locals become none?
3. **The look per section.** Is the table in section 4 a starting point, or does the owner have looks in mind?

## 8. Gates

Gate clips, after approval:
- the first swell (30-40 s);
- the drop (44-58 s);
- 1:47-1:51 (104-116 s);
- the ending from 2:50 (170-210.5 s).

Then the full render.

## 9. Gate 2: the owner's gate-1 notes (29 section 7)

**Fireworks without a visible scan at 109 s.** The bug was in the Feline layer's scan baking:
- **The keys collided.** The 1:44 scan's front had a key for reaching 450 m at 108.13 s. That was after its own
  reset, so once the keys were sorted it fell inside the 1:47 scan's front. The front jumped to 450 m and back at
  108.1 s while the colour was at 4%, so every tree within 450 m twirled and burst with no colour.
- **The front stopped short.** A front that stopped at a partial reach, or jumped back to rest, left the trees just
  inside it frozen mid-burst, or set them off as it skipped past them.

The fix (the Feline layer):
- **No reach and no jump:** every scan's front now runs from the eye all the way to rest.
- **Colour covers the run:** the colour holds at its peak until the front is out, then releases.
- **Sizes differ in other ways:** brightness, speed (full 162 m/s, wide 230, local 320) and length, not reach.
- **No collisions:** a scan is over at least 0.25 s before the next one's front leaves, and the bake asserts that no
  scan's keys interleave with another's.
- **Retimed:** the 1:44 scan became a local ripple, and the 1:47-1:51 scan leaves out of the fill (109.3 s), so it is
  about 120 m out on B's downbeat.

The baked table is `~/Desktop/Feline Footwear/scans.txt`.

**Shorter scans: a default for both versions** (the owner's rule, 29 section 8):
- the live and offline scan holds 5 s and falls over 2 s, where it held 6.5 s and fell over 3 s;
- the front runs out in 6 s instead of 7;
- 7 s a scan, not 9.5.

The live trigger is otherwise unchanged (the trend, the 12 s refractory time).

**Cloud colour popping: the clap splashes, now smoothed in both versions.**
- Each clap's splash used to attack in 8 ms. It now blooms over 150 ms and fades over 750 ms; the phrase splashes
  bloom over 400 ms.
- The patches' edges are softer (remap 0.38-0.64, where it was 0.42-0.58).
- It was not LOD: the clouds have no flat rung.

**Media:**
- `gate2-1m47-1m51-build-104-116s.mp4`
- `gate2-107-113s-before-left-after-right.mp4`
- `gate2-109s-before-left-after-right.png`
