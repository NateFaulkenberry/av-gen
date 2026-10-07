# THE RIFT: evaluation

An honest evaluation against brief §16 (quality bar) and §19 (success criteria) of the final scene on Trench.

**Media:** `~/Desktop/av-gen-review/40-bioluminescent/`. Every checkpoint is kept (cp1 to cp4), and the final is in
`cp5-final/`.

**Grades:**
- **strong:** I would show it;
- **adequate:** it works, but is not yet memorable;
- **weak:** it needs another pass.

## The road (checkpoints)

| CP | What | What the review said |
|---|---|---|
| 1 | Reference board (45 sources, a light and lesson note each) and eight stills from the general renderer alone | the owner: "looks cool". The coordinator: teal haze everywhere, neon magenta, river blobs, square artifacts. It cost 76 ms a frame |
| 2/3 | ADR-1200 emitters, ADR-1201 medium, the arc, the drop clip | darker and denser, but it had lost CP1's colour, and the drop was weaker than CP1's 07 |
| 4 | Colour back at rest; the drop made big (a synchronised ignition, magenta fluorescence, a surge through the canopy) | beat CP1 at its own cameras; the Critic scored 0.975 (washed-out drop, clipped flash, moves off the beat, all fixed). The owner: move on |
| 5 | Live optimisation, a live-input session, the full Trench run, suites | the Critic on the full run caught a real defect (below); after the fix, 0.98 with 2 medium findings |

## §16, the quality bar

| The bar asks for | Grade | Evidence |
|---|---|---|
| Enormous perceived scale | strong | a 150 m canyon reaching kilometres into blue haze; 18-45 m crinoids; reveal shots at 70 m |
| Dense foreground detail | strong | sea-pen meadows, polyp mats, whip tufts and lanterns within metres of the lens; 19 M candidate emitters a frame |
| Rich midground activity | adequate to strong | the canopy curtains and the wall crust; fronts of light travelling through them. The wave reads in motion more than in stills |
| Distant environmental depth | strong | crowns and chains receding down the canyon; no world edge in any frame |
| Sophisticated lighting | adequate to strong | the micro scale lights nothing (true to life); the giants pool light in the haze; at the drop the haze and walls catch the wave |
| Atmospheric layering | adequate | the haze is dark with crown halos. Some body shots read flat-lavender |
| Biological variation | strong | eleven layers on seven species: flash, lag, wake, fluorescence, iridescence, drift, breathing, spontaneous pulses |
| Restrained darkness | strong at rest, adequate in the body | the body's walls stay lit for 100 s (sameness risk) |
| Beautiful colour relationships | adequate to strong | a blue-violet rest with turquoise, violet and amber; magenta as the drop's fluorescence. It leans lavender-pastel where violet and magenta overlap under bloom |
| Meaningful motion | strong | the music travels (fronts at m/s, refractory); the canopy lags; reaches awaken; spores rise with the highs; swarms on the snare |
| Cinematic composition | adequate to strong | three planes in most frames. The Critic flagged repeated compositions; the offsets were then varied |
| Extraordinary density | strong | see CP1 against CP4 at the same cameras |

**It does not look like:**
- a Unity or Unreal demo, or a procedural tech demo;
- a handful of glowing mushrooms, a neon forest, or random particles;
- primitive geometry (the organisms are generated, lean and feathered).

Its weakest resemblance is to "a music visualizer" during the body, when everything pulses.

## §19, the success criteria

| Criterion | Verdict | Evidence |
|---|---|---|
| **Artistic:** a still looks like a genuinely impressive high-end bioluminescent environment | adequate to strong | the canopy curtains (60, 93, 104 s) and the drop frames (96-98 s) are the best images. Some body frames are busy-lavender |
| **Density:** extremely rich at multiple scales | strong | seven scales, from plankton (points) to crinoids to the canyon |
| **Audio:** large-scale environmental behaviour, not parameter modulation | strong | kicks launch fronts through an excitable medium. Species respond by channel and threshold, with refractoriness and wake. The arc reads the music's structure: a breath, a build, then the drop at 94.8 s on Trench, with nothing in the arc naming the track |
| **Live:** real-time response at a useful frame rate | adequate | 30 fps at 1080p, High (headless) or Medium at scale 0.85 (real loop, 0 deadline misses). A real live-input session ran the arc through the drop. **60 fps is not reached** (17.0 ms GPU at Emergency) |
| **Offline:** substantially higher quality | adequate | the same world with a 32-step medium, 2× supersampling, the full render scale and EXR. The difference is real but modest: the look is not starved live |
| **Control:** meaningful control without GPU knowledge | strong | every leaf is a parameter: `ecosystem/<layer>/...`, `grid/prop/...` (wave speed, threshold), the stage presets. MIDI CC 1-4 (sensitivity, wave speed, medium threshold, collective light); pads 36-42 force a stage |
| **Architecture:** a credible foundation for Environment mode | strong | the `EnvironmentRenderer` seam is already reused by production Astral Forge (on main); WORLD/ENVIRONMENT/HYBRID as content, not a switch |
| **"Holy shit"** | adequate to strong | the drop is a real regime change: a darkened held breath, then the canyon ignites in fluorescent violet, magenta and cyan as the camera surges through the canopy. It is not yet the jaw-drop: the wave's leading edge is not legible enough, and the colour leans pastel |

## The Critic on the full run

Run on the full 207.6 s Trench render, 20 shots, one per committed state (`critic_inputs.py`).

**The first run** (`job_1a11631baefe1a137`, 0.98) scored motion at 0.92 and flagged "camera shake" at 201-207 s,
moving 7-14% of the frame per frame. Single frames showed it was not shake: the camera pointed somewhere else for
one frame and then came back.
- **The cause:** the flight ran off the end of its open spline at 197 s. Parked on the end point, the look-ahead
  target coincided with the camera, so the aim became float rounding.
- **The fix (b72866dd):**
  - in the engine, the look-ahead continues along the end tangent;
  - in the Rift, the body's paces are about 3 m/s lower, so the flight ends at 95% of the path.
  - There is a test for each.
- The full run was re-rendered.

**The second run** (`job_1a1166133fd10a696`, 0.98) has motion at 0.97 and no findings at the end. What remains:
- **Pale for a night scene (the drop, 96-116 s, medium):** luma 0.29 with saturation 0.55. The drop is meant to be
  the one bright regime, but the pastel lean is real (see §16, colour).
- **A small wobble on the dive into the drop (93.9-95.9 s, medium):** 0.36% of the frame per frame.
- **Repeated composition in 10 of 20 shots (low):** true. The flight always looks down the canyon's one axis, so
  shots resemble their neighbours even when the behaviour changes. This is a limit of a single river spline; see the
  next-iteration list.

## Known limits and the next iteration

1. **Live at 60 fps** needs the general renderer's lit pass cut further. Bodies and terrain are geometry-bound
   (shading them unlit saved nothing): mesh impostors for the giants and a coarser canyon far field. The
   Environment's own parts are about 1 ms each.
2. **The wavefront's legibility:** a bright leading edge with a dark wake, as its own layer response (u minus e).
3. **The body's sameness:** let reaches fall dark between phrases (a wake decay per stage) so the light keeps moving
   through the world.
4. **Placement is CPU `points`** (14 MB of scene JSON). A generator distribution standing on the terrain's height
   would make the world unbounded and the file small. That is an engine gap (`scene/generator.hpp`'s value-noise
   ground).
5. **Compositional variety:** a second path family (cross-canyon traverses, a climb up a wall face, a crown-level
   orbit) so that consecutive phrases do not share the down-canyon axis.
6. **The two full renders differ before the pace change could act** (mean abs difference 2/255 at 60 s, 8/255 at
   97 s: the same shot, with the camera a little displaced). They straddle the origin/main merge, so the cause is
   not established: it may be engine changes from main or render nondeterminism. I did not chase it. The drop clip
   (`rift-the-drop-84-112.mp4`) is the earlier build's render of the same drop.
7. **No body sway:** emitters follow static host instances (ADR-1200's stated limit). Vegetation wind on both would
   be one shared function.
