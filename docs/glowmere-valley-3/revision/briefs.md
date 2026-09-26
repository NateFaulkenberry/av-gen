# Engine agent briefs: the GV3 revision's system development

**Rules for every agent:** [ENGINEERING-RULES.md](ENGINEERING-RULES.md).
**Evidence:** [audit/reports/](audit/reports/), with file:line references against main 83a12334
plus ADR-893–895.
**Worktrees:** each wave-1 stream has one at `/Users/natefaulkenberry/Documents/GitHub/av-gen-<topic>`,
on branch `agent/<topic>`, created from main `0b623b88`. Assets are linked with
`bash /Users/natefaulkenberry/Documents/GitHub/av-gen/tools/link-worktree-assets.sh <worktree>`.

**To relaunch a stream:** give a fresh agent the rules, its brief below, and this instruction:

> The worktree already holds partial work. Inspect `git -C <wt> log main..HEAD` and
> `git -C <wt> diff`, and finish from there.

## Wave 1

### signals (ADRs 896–899): music analysis and musical time
- **Read:** the brief's Phase 2, §3, §5, §8 and §16; reports/modulation.md §1, §6, §7 A and C; reports/director.md §2–3 and recommendation 1; 01-music.md.
- **Deliverables:**
  1. **One definition of musical time.**
     - Estimate the downbeat phase, with a serialized override in beats.
     - Apply it everywhere a beat count becomes a bar, phrase or downbeat: `beat.bar/phrase`, `music.downbeat/bar/phrase`, LFO beat-sync, the timeline's beats base, the shaders' bar input, and effect triggers. The Beat source counts from 0 while the bus counts from 1; make them agree.
     - Make phrase length configurable (8 bars for Rebuild).
     - Validate on `~/Desktop/Rebuild.mp3` (130 BPM, first downbeat 0.480 s): downbeats within ±30 ms. Skip cleanly when the file is absent.
  2. **Features that survive a flat master.**
     - Density as an onset rate.
     - A loudness-independent energy composite.
     - Long-term, non-auto-gained band levels under new names.
     - Band-limited onsets as events: low, mid, high.
     - Validate the low onsets against `tools/gv3/music.py kicks()` (475 kicks), aiming for precision and recall above 90% within ±30 ms.
  3. **Sections.**
     - Publish `section.index/progress/energy` and `section.change` from `sequence.sectionTimeline`, visible to seek replay.
     - Expose per-section energy, band and brightness in `song::SectionCue`, `SongPlanSection`, `directing::MusicalContext` and `director.inspect_scene`.
- **Stay out of:** the route chain and audit files (routes owns them).

### routes (ADRs 900–902): the route chain and the liveness audit
- **Read:** the brief's §3, §5 and §16; reports/modulation.md §2, §4, §6, §7 B, C and D; reports/mushrooms-wind.md §3–4; reports/director.md recommendation 2.
- **Deliverables:**
  1. **The chain.**
     - A `delayMs` first stage: time-stamped, independent of frame rate, events preserved.
     - `depthSource`: a signal scaling the deviation from the op's neutral value.
     - Event-safe smoothing: a one-frame event reaches its full amount. Re-baseline the affected tests.
     - An event mode for timeline sources, so a scored pulse can trigger envelope sources.
     - Serialization; seek-exact.
  2. **One C++ registry of liveness rules.** Apply it at `Modulator::bind` (log once), and dump it headless with `avgen --project X --audit-routes out.json`: every route, track and default effect route, with target, verdict and reason. The rules:
     - event routes that pass less than 50%;
     - dead targets:
       - `material/<p>/emissionIntensity` on a program with no emission output;
       - material layer intensities that are not registered;
       - `emissiveIntensity` where a program owns the emission;
       - `emissiveBoost` only on nodes that emit nothing. After the emission stream, `emissiveBoost` is live on program-lit and procedural nodes.
     - phase-rate traps (a routed speed whose phase is time × rate: ColorCycling speed, LFO rate, aurora flow/drift, the wind's gust/turbulence/region rates, particle pulseRate, volumeNoiseSpeed…);
     - unregistered targets;
     - effect activations that can never fire;
     - other cases of the same shape.

     Run it on GV3 and on GV2 multicam. The JSON schema is the evaluator's "configuration" tier and the Director validator's dead-target source.

### emission (ADRs 903–906)
- **Read:** the brief's "EMISSIVE BOOST", §3, §4 and §5; reports/mushrooms-wind.md, all of it; reports/modulation.md §3, §5 and the emissiveBoost part of §7B; 07-technical §7.7–7.8.
- **Deliverables:**
  1. **`emissiveBoost`** is one post-program multiplier for every drawable node kind: mesh, procedural (including sub-parts) and SDF. Delete the mesh-only path (composition.cpp, `range.restEmissive[k] * emissiveBoost`).
     - The owner's instruction: no backwards compatibility, fix it cleanly, regression-test it.
     - GPU difference images for a program-lit procedural node, a program-less procedural node, a mesh node and a routed boost, each with a control.
     - Record the scenes whose look changes: the tree-of-life night film's stars at 1.9–2.2, glowmere-stylized's filaments, and GV2's elder routes, which become live.
  2. **The small mushrooms** (scatter layers fungi, shelf-fungi and beacons):
     - registered per-layer `emissionGain` and `hueOffset`, applied after the program;
     - scatter `emissiveField` and `emissiveFieldAmount`.
  3. **The m² fix:** the per-instance multiplier applied twice when a program reads `instanceEmissive`.
  4. **Register** material-program layer `emissionIntensity`.
  5. **`scene/ecologyLight`**, registered and routable.
  6. **Fields timed from the last event:** a `trigger` on FieldSpec, reusing the effects' Trigger and TriggerClock, seek-exact. Demonstrate a wave of light passing through the mushrooms on a musical event.

### characters (ADRs 907–910)
- **Read:** the brief's character assessment, §10, §11 and §12; reports/characters.md, all of it (its scripts are in audit/scripts).
- **Deliverables:**
  1. Forward-biased, slope-aware destinations: a heading cone and maxSlope in pickDestination/wander; lean toward home rather than re-centring; a per-entity maxSlope key.
  2. Walk-through turns: `turnRadius` in wander and GaitSettings, and a `turnRate` replacing the hard-coded 2.45 rad/s. While moving, turn rate ≤ speed/radius with minimum travel kept; pivot only from rest.
  3. Eased stops (an arrive radius).
  4. Anti-idle and anti-loop: `maxStillSeconds` in the stall breaker; holdPost duration; return to half tolerance; no A→B→A.
  5. Speed variety: a seeded `speedRange`; react speed scaled by urgency.
  6. The metrics in the ADR-826 analyzer: longest still stretch, reversals, pivot yaw, turn radius, slope under stationary bodies, facing uphill. These are the regression gate.
  7. Single-clip animals play their clip while pivoting.

  Prefer fixing the defaults. Justify the choice in the ADR, and keep the goldens meaningful.
- **Measure before and after** with `avgen_cast_trace` on GV2 multicam and on GV3. Recommend GV3 scene settings.

### camera (ADRs 911–913)
- **Read:** the brief's §2; reports/camera.md, all of it; reports/render-post.md, engine gap 1.
- **Deliverables:**
  1. A follow reference filtered from HIST:
     - `followSmoothSeconds` (XZ), `followVerticalSmoothSeconds` (Y), `followLead` (XZ only);
     - a causal, finite, critically-damped kernel;
     - eye and aim sharing it;
     - seek-exact.
  2. `followLagSeconds` read from HIST. Delete FollowTrail.
  3. A softplus clearance floor.
  4. `followLocal` uses the filtered heading. Point the Director compiler's chase rig at the new knobs.
  5. `aimFollowSmoothingMs`: wire it up or delete it.
  6. A per-frame camera-pose track in `avgen_cast_trace`.
  7. Reset motion history on every hard cut, with a GPU test.
- **Pass bar on GV3's follow shots:**
  - pitch and yaw high-frequency ≤ 0.1°;
  - eye vertical high-frequency ≤ 2 cm;
  - subject off-centre maximum ≤ 20% of frame;
  - eye travel ≈ subject travel.

### water (ADRs 914–916)
- **Read:** the brief's §1; reports/water.md, all of it; the owner's reference image at `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-reference-from-owner.png`; 05-quality.md §5.4; 07-technical §7.10.
- **Deliverables:**
  - **A. Bounded advection:** a two-phase flow offset with a variance-preserving crossfade, in the ripple, sparkle, foam and glow code. Its own ADR, re-baselined, with no switch.
  - **B. Deliberate tears**, byte-identical at zero:
    - the parameters `tears`, `tearShear`, `tearCoverage`, `tearCell`, `tearSpacing`, `tearStretch`, `tearDirection`, `tearDrift` and `tearWind`;
    - a rigid stepped lattice;
    - wind coupling through `windSampleAt`;
    - resolution-aware fades;
    - an integer hash;
    - the routable ones registered.
  - **The unregister-list fix.**
  - **The tests in water.md §3.**
  - **Before/after stills** of GV3's water shots, kept outside the repository.

## Wave 2: not launched yet
It starts after signals, routes and emission merge.

### director (ADRs 920 and up)
From reports/director.md recommendations 2–5. The owner chose "Build it; GV3 uses it".
- **A reactivity planner:**
  - a `PlanRoute` plan item, compiled to `ModRoute` plus sources;
  - a `ReactiveCatalog` generated from engine data (emissive materials, particle systems, scatter populations, effect fields, lights, atmosphere, wind);
  - a validator that refuses dead targets through the routes stream's registry and flags "everything on the kick";
  - a deterministic default proposer: catalogue × musical layers → micro/meso/macro routes with staggered phases, delays and section-aware depth.
- **Song Mode musical durations and arcs:**
  - carry arc, energy and visualDensity through `songPlanFromCues`;
  - beat-snapped cuts;
  - risers that accelerate the cutting;
  - peak sections given to the most important or event subject;
  - the dead `camera/focus/emphasis` and `ShotSpan::emphasis` wired or cut.
- **Scenario items in plans:** set pieces such as several abductions at chosen places and times, with variation.
- **An evaluator hook:**
  - `director.evaluate` and `director.compare`, calling the Creative Critic CLI;
  - evaluations stored per revision;
  - a policy of iterating in scratch and showing only the winner.

### render and post (ADRs 917–919)
From reports/render-post.md engine gaps 3, 6 and 7:
- post radii that hold across resolutions: bloom levels and reach, anamorphic stretch, halation and motion-blur tiles;
- optionally, fog colour taken from sky radiance, and the offline tier raising authored sample counts.

(The motion-blur reset at cuts is in the camera stream.)
