# Revision pass: the capability audit, and the plan it produced

This follows the owner's revision brief ([00-brief.md](00-brief.md)), Phase 1 and the system
development gate.

Seven read-only audits covered:
- the Director;
- characters and animals;
- cameras;
- audio analysis and modulation;
- the small mushrooms and wind;
- offline quality and post;
- water.

Each cites code evidence, and several measured the real GV3 data. The full reports are kept
outside the repository with the other working material. This page is the synthesis.

**The gate.** No GV3 scene has been edited during this phase. The quality evaluator it depends on is
being built as a separate, standalone project (`~/Documents/GitHub/creative-critic`).

## 1. What the audit found

### The Director
- **The first pass was not directed by AV Gen's Director.** A Python generator authored every cut,
  look and route, and the engine's director output was stripped.
- **Left to itself on this song, the engine's Director produced a poor cut.**
  - 34 of its 39 shots lasted 5.90–5.92 s.
  - None of its 38 cuts fell on a downbeat.
  - It detected no pull-back, break, riser or drop.
  - It buried the drop 0.44 s into a travelling shot.
- **Its inputs were the cause.** Its music analysis misreads a flat-loudness master:
  - density is structurally zero;
  - energy is plain loudness;
  - the bar phase is off by one beat.
- **What it lacks.** No director plans audio reactivity, set pieces, or shot duration from content.
  The LLM Director Agent can neither hear the music nor see the image.
- **Answer to the owner's question:** no, it could not reliably make another high-quality
  audio-reactive Glowmere video from scratch.
- **Why GV2 looked good:** the owner hand-cut the timing, the hero rotation happened to be
  beautiful, and hand-authored routes made the heroes pulse.

### Characters and animals
Measured on the first pass's cast trace:
- Sage stands still for 67 s at a stretch and runs a 15-second out-and-back loop.
- Every body moves at a single speed.
- Turns have a radius under one body length.
- Animals turn 27–44% of their yaw in place, on a frozen walk frame.
- 52% of animal stops are on ground steeper than 12°.

The causes are four engine rules:
- destinations drawn with no forward bias;
- travel scaled by max(0, cos heading error), so every turn over 90° stops and pivots;
- no limit on standing;
- no slope limit short of 63° cliffs.

Scene data cannot fix the pivots, the reversals or the single speed.

### Cameras
The wobble in the follow shots is the stride bob, made larger by audio, carried into the camera:
- the lag delays only the eye, turning the bob into a pitch nod (1.1–1.2° RMS on s19 and s38);
- rigs without lag bob rigidly by 24–35 cm;
- the clearance floor kinks.

There is no position, target or rotational smoothing. Every hard cut also opens with a frame of full
motion blur, because only a seek resets motion history.

### Audio reactivity
- **The engine can already produce a hierarchical, staggered response.** GV3 used little of it, and
  GV2's apparent reactivity was partly dead.
  - Seven of GV2's event routes delivered 1% or less of their amount.
  - Three routes targeted parameters that never reach the pixels.
  - Its 16 hero pulses were dormant without its old cut.
- **Four gaps stop a Director from producing a musical, non-visualiser response on its own:**
  - no bar alignment;
  - no section signals;
  - no per-route delay or depth control;
  - the valley's thousands of small mushrooms addressable only as one shared material.

### The mushrooms and wind
- **The mushrooms are three scatter layers of one material program.** Their authored colour never
  reaches the pixels, and their per-instance brightness is applied twice.
- **A wave of light can already travel through them** (ground pulses, travelling fields).
- **Wind is on, but it moves a few centimetres over 9.5-second gusts,** so it reads as shape, not
  motion.

### Offline quality and post
The renderer's distance culls were already off in the final. What reads as limited draw distance is:
- the valley's own 640 m edge in frame;
- moon shadows that stop at about 77 m;
- terrain LOD never lifted;
- fog that isn't marching at all, because the project zeroes its distance.

The ACES tone mapper skews Glowmere's saturated emitters, and the bloom threshold keeps most violets
from blooming at all.

### Water
The owner's reference, thin angular seams of compressed ripples, is real. It is the flow
advection shearing the ripples wherever the baked flow jumps between vertices. Because it grows with
film time, it is invisible early in a film and aliases late.

## 2. The plan

The owner chose to build the missing Director capabilities and have GV3 use them. General engine
changes land in main first, then GV3 continues from the updated main.

**Done:** the first pass's engine fixes (ADR-893–895) and review tools are merged into main
(`0b623b88`). The first pass itself is committed on `gv3/production`.

### Wave 1 (running in parallel, one worktree and branch each)

| Stream | Builds | ADRs |
|---|---|---|
| signals | One definition of musical time: downbeat estimate, bar and phrase alignment everywhere. Density as a rate. A loudness-independent energy composite. Long-term band levels. Kick, snare and hat onsets. Section signals, and section data for the Director. | 896–899 |
| routes | Per-route delay; depth from a signal (section-aware modulation); event-safe smoothing; timeline event mode. One liveness audit (dead targets, swallowed events, phase-rate traps), used at load, as a CLI dump for the evaluator, and as the Director's dead-target source. | 900–902 |
| emission | `emissiveBoost` as one post-program multiplier for every node kind (the owner's instruction). Per-layer gain, hue and emissive fields for the small mushrooms. The doubled per-instance brightness fixed. Layer and ecology-light parameters. Fields timed from the last musical event. | 903–906 |
| characters | Forward-biased, slope-aware destinations. Walk-through turns. Eased stops. No long stillness and no out-and-back loops. Speed variety. Character-quality metrics as the regression gate. No frozen-frame pivots. | 907–910 |
| camera | A filtered follow reference from the history bank: smooth, seek-exact, eye and aim sharing it. A smooth clearance floor and filtered heading. Camera poses in the cast trace. Motion history reset on every cut. | 911–913 |
| water | Bounded flow advection (the accidental seams go). Deliberate, wind-coupled, routable tear lines matching the owner's reference, identical to today at zero. | 914–916 |

### Wave 2 (after the signal, route and emission streams merge)
- **Director:**
  - a reactivity planner: a catalogue of reactive targets, dead targets refused, and a default
    micro/meso/macro proposal;
  - musical shot durations and arcs, with beat-snapped cuts and accelerating risers;
  - set pieces in plans, such as several abductions;
  - an evaluator hook.
- **Render and post:** post radii that hold across resolutions, plus whatever the 4K final needs
  beyond configuration.

### Then the GV3 revision (Phase 3)
It starts once the evaluator passes the Phase 0 gate and the Director work has merged.
- The Director plans structure, pacing and reactivity.
- The evaluator judges every iteration.
- The scene gets:
  - hero effects;
  - the mushrooms listening;
  - wind;
  - more, varied UFO events;
  - water tears;
  - smooth cameras;
  - re-homed animals;
  - a drop that changes the world's state without changing its style;
  - the offline configuration for a maximum-quality 4K render: shadows, fog march, terrain LOD,
    AgX, bloom, depth layers and grain.
