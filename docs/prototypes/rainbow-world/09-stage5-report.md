# Rainbow World 09: Stage 5 report (pacing, crossings, cloud sea, density)

Stage 5 answers the coordinator's Stage 4 notes and the owner's request: "definitely want faster camera movements".
The world is rebuilt by `examples/rainbow-world/build_world.py` (v17).

Review images are in `~/Desktop/av-gen-review/74-rainbow-world/stage5/`:

| File | What it shows |
|---|---|
| `clip-pacing-28-48s.mp4` | 20 s at 1280x720: a 30 m/s run-in, a surge round the rim at 45 m/s, then a hold under a rainbow arch |
| `02-pacing-clip-sheet.png` | The same clip, every 0.5 s |
| `01-ride-stills-v16.png` | 16 stills round the ride (v16; same world as v17, timing before the surge fix) |
| `03-crossing-and-cloud-sea-before-after.png` | Stage 4 against Stage 5 at the crossings |
| `04-stripes-diagnosis-gtao.png` | Why the meadow had contour stripes (GTAO), and the fix |

## 1. Faster ride (the owner's note)

The ride is now a looping timeline source (`timeline.ride`, linear keys every 15 m of ride) mapped onto
`camera/splineT`. It is still a pure function of the transport, so seek equals play.

| | Stage 4 | Stage 5 |
|---|---|---|
| Base speed | 12 m/s, constant | **30 m/s** |
| Crossings | 12 m/s | surge to **45 m/s**, ramped over 140 m either side, so the swing over the rim takes about 4 s |
| Holds | none | **9 m/s** under each rainbow arch (lap A) or at a Great Tree landmark (lap B), eased over ~150 m |
| Loop before repeating | 600 s, one lap | **618 s, two laps with different holds** (mean 25 m/s) |

Supporting changes:
- **Swoops.** Ride height now varies from 6 to 17 m (a squared sine, 520 m period). The camera dives at the meadow
  and rises over it, and at 30 m/s it never skims so low that the grass strobes.
- **Quicker weave.** The weave periods are now 820 m and 300 m (were 1150 m and 410 m): side-to-side every 10-27 s.
- **Tighter rim arc.** 40 m out and 11 m clear (was 54 m and 14 m), and the ride is drawn to the rim over 160 m
  (was 220 m). Less time staring off the edge.
- **Motion blur.** Not used. The engine has it (`post/motionBlur/*`), but at the current GPU cost there is no room
  for it. That is a Stage 8 decision, once the budget is met.

## 2. The coordinator's Stage 4 notes

1. **The cloud floor was dead space.**
   - It now has its own program (`rw_cloudfloor`) led by height: deep blue-violet troughs, lavender flanks, and
     white-gold heads only where the billows rise. Before, every up-facing billow took the top stop, so the floor
     was one flat cream wall.
   - On top of the floor sit **900 cloud heads** out to 9 km, with a four-rung mesh LOD from 1.2 km. At crossings the
     view down is now a sea of cumulus (`03`).
2. **Rim crossings.**
   - No bare rock where the ride swings over a rim: rocks within 160 m of a crossing, on that side, are removed.
   - The rim chunks there use a moss variant of the ground program.
   - The cliff itself is planted as a **hanging garden**: 54,000 turf tufts grow straight out of the rock face (on a
     cliff, up is outward), and 13,000 flowers spill over both lips. Blossom Puffs and Weepers stand at the lip.
3. **Density and richness.**
   - Every ~170 m along the ride there is one deliberate cluster, on alternating sides, with rest between. The
     clusters are a grove (3-5 big trees), a flower field (450-800 heads), or a rock garden ringed with flowers.
   - Flower heads are 1.2-2.0x (were 0.9-1.5x).
4. **Rhythm.** See section 1.
5. **My own list:**
   - The Weeper has 30% fewer curtain strands, and its colour is now lime (`#5E9426` to `#E2EE6A`).
   - Cloud stops are white-gold over blue-violet.
   - The cloud floor follows each palette look (new `floor0-3` roles, derived from each look's cloud colours).

## 3. A defect found: contour stripes on the meadow

Low-sun slopes showed regular contour bands, and the rim bulges showed a dark staircase. I checked six suspects with
stills at the same two moments (`04`): shadow bias, no shadows, the band LOD, the cavity input, micro-detail, and AO.
Only turning off **ambient occlusion** removes them.

The cause is GTAO's radius: 5% of the distance from camera to target. With a 70 m look-ahead that is ~3.5 m, about
the spacing of the band's rings, so it shades each ring's facet. Ambient occlusion is now off for this scene. Contact
darkening comes from the cavity term and the shadows, and the frame lost nothing visible (`04`, third column).

## 4. Engine change: ADR-1546

`"lockBorder": true` in a procedural node's `lod` block holds a mesh's open border still when its LOD levels are
simplified. With it, the 48 band chunks take a four-rung ladder without cracks at their seams. Each chunk is now
exported about its own centre: in world space it was never culled, because the procedural culler's bound is the
mesh's extent about its origin.

One commit, with a test (`[adr1546]`, 22 assertions, including a control that fails without the flag).

## 5. Cost (live profile, Ultra, 1920x1080, headless)

| t | GPU | geometry | shadows | triangles |
|---|---|---|---|---|
| 42 (crossing) | 36.4 | 29.4 | 6.6 | 2.63 M |
| 47 (arch, lip garden) | 43.3 | 34.6 | 7.7 | 2.49 M |
| 120 | 33.6 | 26.3 | 6.6 | 2.01 M |
| 200 | 24.4 | 18.0 | 6.3 | 1.62 M |
| 300 | 31.9 | 23.7 | 7.9 | 2.38 M |
| 450 | 36.7 | 30.5 | 5.8 | 2.09 M |

**Over the 29.3 ms budget at five of the six points.** The world is much richer than Stage 4 (about 400 trees, 440k
tufts, 42k flowers, 900 cloud heads), and the faster, higher camera sees more of it.

Measured by removing node groups (A/B at t=47 and t=450):

| Group | Cost |
|---|---|
| Trees (leaf cards most of it) | 12-21 ms |
| The band alone | 16-21 ms |
| Material programs | 7-12 ms |
| Clouds | 2-4 ms |
| Rainbows | 1-4 ms |
| Grass | 1-3 ms |
| Flowers | ~0 ms |

These levers did not help:
- The depth prepass setting changes nothing (within 0.6 ms).
- Quality High saves under 2 ms.
- Turning the fisheye off saves nothing.

Stage 8 levers, in order:
1. Leaf-card count per tree. A far-card variant needs authoring, since a mesh LOD would sloppy-collapse the cards.
2. Tree count near the crossings.
3. The ground program's cost.
4. A render scale of 0.85 as the last resort.

## 6. Weak points now

1. **GPU over budget** (section 5).
2. **Near cloud masses.** At 31-37 s in the clip, a large cloud close to the ride reads as a flat cream shape with
   no billow shading. The cloud program's top stop dominates when a mass is near and faces the sun.
3. **The cliff faces at crossings** are planted but still read as a smooth green fold from outside (t=87).
   Overhanging trailers, like the Weeper's strands hanging off the lip, would sell the hanging garden.
4. **Lap repetition.** Two laps with different holds; the path is the same each lap. A per-lap variant of the
   weave would need a second camera spline.
