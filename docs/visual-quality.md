# Visual quality: the bar, the rubric and the review loop

Status: living document for the cinematic phase. The tests say whether the engine works; this
says whether the images are good. A frame that renders without errors and still looks like a
procedural demo is a failure.

## 1. The four master scenes

| Scene | What it must prove |
|---|---|
| Hyperspace | travel, depth layers, atmospheric perspective, camera choreography |
| The Infinite Temple | monumental scale, composition, volumetric shafts, material richness |
| The Living Machine | coordinated mechanical motion, energy, dust and haze, roughness variation |
| Metallic Reassembly | metal that reads as metal, hierarchical disassembly, motion at several scales |

Each must eventually work as a still (a single frame worth showing), as a thirty to sixty second
sequence with a beginning, development, climax and resolution, live, and offline with identical
results.

## 2. The rubric

Score each scene one to ten. Anything at five or below is a defect to fix, not a taste question.

| Criterion | The question |
|---|---|
| Composition | Is the frame deliberately arranged, or is the camera just somewhere? |
| Hierarchy | Is there an obvious subject, with support and background behind it? |
| Lighting | Does light describe form and set mood, or is everything evenly lit? |
| Materials | Do surfaces feel physically different from one another? |
| Detail | Is there macro, meso and micro detail, or perfect primitives? |
| Depth | Does the world feel spatially deep? |
| Atmosphere | Does the air occupy space? |
| Scale | Is the size of things believable and varied? |
| Motion | Do things move at different rates, or all on the beat? |
| Audio relationship | Musical rather than reactive? |
| Temporal coherence | Stable under motion: no boiling, crawling or popping? |
| Cinematography | Does the camera feel authored? |
| Originality | Does it feel made, rather than generated? |
| Performance | Inside its budget at the intended tier? |

## 3. The review loop

1. Render the fixed review frames (below) at 1920x1080 from the Release build.
2. Look at every one. Score against the rubric.
3. Fix the lowest-scoring criterion first, in the engine when it is a capability gap and in the
   scene when it is an authoring gap.
4. Re-render the same frames and compare against the previous set.

Fixed review frames, chosen to sample each scene's arc:

| Scene | Frames (at 30 fps) |
|---|---|
| Hyperspace | 150, 600, 1200, 1800 |
| The Infinite Temple | 300, 1200, 2400, 3600 |
| The Living Machine | 200, 900, 1800 |
| Metallic Reassembly | 90, 600, 1200, 1650 |

`tools/review_frames.py` renders the whole set into a dated folder.

## 4. Failure modes to hunt

Uniform randomness, uniform density, uniform lighting, uniform motion, uniform materials, uniform
scale, infinite sharpness, everything glowing, everything reacting to audio, perfect primitives,
empty backgrounds, cluttered backgrounds, obvious procedural repetition, and a camera that only
observes. Each one has a fix in this phase's architecture; if a frame shows one, the fix is
available rather than a matter of taste.

## 5. Measuring a change without pretending to measure beauty

`tools/image_stats.py <frame.png>` reports luminance mean, RMS contrast, the 1st/50th/99th
percentiles, the fraction of the frame in shadow/mid/highlight, clipping, mean saturation and the
centroid of the bright mass. It has no dependencies and reads PNGs directly.

Use it to confirm that a change moved the image the way the change intended, and for nothing else.
A number here is never a score. "Mean rose from 0.09 to 0.21 across the arc with no clipped pixels"
is a useful sentence about a shot whose light is supposed to grow; "mean 0.21" on its own says
nothing about whether the frame is worth looking at. That judgement comes from opening the frame.

## 6. Recorded state

Baselines before the phase began are in the session's scratch folder, and the observations that
started this work were: Hyperspace blew out to white at its core; the Infinite Temple read as a
wall of evenly lit grey cylinders with pastel cubes; nothing anywhere cast a shadow.

Hyperspace has since been re-authored as a shot; `docs/shot-hyperspace.md` records the composition,
the lighting and the six traps that cost the most time. Its arc now measures as a mean of 0.07,
0.09, 0.10, 0.21 across the four review frames with rising contrast and nothing clipped. Judged by
eye it is a legible, atmospheric corridor with a real dramatic arc, and it still reads as stylised
motion graphics rather than as a photographed place: the gate plates are untextured boxes with a
flat warm tone, the tunnel slats read as a graphic spiral, and the core is a smooth bright disc
rather than a structured source. Those are the next things to fix in it.
