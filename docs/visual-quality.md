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

> **Status, 15 September 2026 (ADR-244).** These four remain the spec's statement of the bar and are
> kept here as written. They are **not** the current quality target: the owner has described them as
> tests likely to be discarded, and only Glowmere was reviewed for §7. Do not score the engine
> against these four, and do not read work into them that nobody has asked for.

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

## 6. Where the four scenes stand

All four have been re-authored as shots. Each has its own note — `docs/shot-hyperspace.md`,
`docs/shot-infinite-temple.md`, `docs/shot-living-machine.md`,
`docs/shot-metallic-reassembly.md` — recording the composition, the lighting and what still does
not work. A representative frame from each:

| Scene | mean | RMS contrast | in shadow | clipped |
|---|---|---|---|---|
| Hyperspace, t=28 s | 0.087 | 0.106 | 64% | 0.00% |
| The Infinite Temple, t=56 s | 0.122 | 0.140 | 54% | 0.00% |
| The Living Machine, t=20 s | 0.098 | 0.154 | 64% | 0.03% |
| Metallic Reassembly, t=30 s | 0.136 | 0.164 | 47% | 0.03% |

Nothing clips, everything holds a shadow, and each carries an arc rather than a single state. By
eye, The Living Machine is the closest to reading as a photographed place; Hyperspace is the most
legible but still the most graphic; the Temple's architecture works and its far layer does not;
Reassembly's mechanic finally runs and its debris is still uniform.

## 7. Silent no-ops: a recurring family

Five features in this codebase were parsed, validated, hashed, serialised — and read by nothing.
Every one of them let a scene ask for something and silently get something else, and each cost
hours to find because the authoring looked correct.

| Feature | What happened |
|---|---|
| `sourceTransform` | Applied on the CPU, ignored by the vertex shader. Gate rings kept the generator's default axis. |
| `lightRig` at a scene's top level | Only read inside `environment`. Three examples ran on the default key light. |
| `PunctualLight::volumetricStrength` | Packed into the GPU light and read by no shader; the fog was lit by light 0 at full strength. |
| `targetScreenPosition` / `framingStrength` | Parsed and never read. Three scenes were authored against them. |
| `screenVelocity()`'s input | Fed framebuffer pixels instead of clip space, so the velocity target held hundreds of screens per frame. |

All five are fixed with regression tests, and so is the sixth: the composition block's depth
`layers`, which two independent agents reported as the single most misleading thing in the scene
format. `density` and `detail` now act on instances in `cull.wgsl`, and `contrast` and `saturation`
grade pixels by distance in the composite pass — atmospheric perspective as a scene decision rather
than a global grade.

A seventh sat inside the fix itself and is worth recording, because it looked like a working
feature: the composite pass never bound the scene depth, so it read the placeholder, every pixel
measured as one unit away, and the whole frame silently received the *nearest* band. The temple
looked different, so the change appeared to work. Only a test that asserted a near subject and a
far subject were graded *differently* caught it.

The lesson for the next feature: a field that a scene file can set, that round-trips through JSON
and hashing, and that no test asserts an *observable* consequence for, is a field that probably
does nothing. Assert the consequence, not the round trip.

## 8. Recorded state

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
