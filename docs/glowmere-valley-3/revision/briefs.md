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

## Wave 2

The Director work is three streams, so the part with no wave-1 dependency can start first:

| Stream | ADRs | Depends on | Worktree / branch |
|---|---|---|---|
| setpieces (set pieces and the evaluator hook) | 928–931 | nothing in wave 1 | `av-gen-setpieces`, `agent/setpieces` |
| render (post and offline quality) | 917–919 | nothing in wave 1 (camera owns the cut reset) | `av-gen-render`, `agent/render` |
| song (musical durations and arcs) | 920–923 | signals | `av-gen-song`, `agent/song` |
| reactivity (the reactivity planner) | 924–927 | signals, routes, emission | `av-gen-reactivity`, `agent/reactivity` |

Every stream creates its worktree from the main of the moment it launches, and keeps the two
headless paths GV3 depends on working:
`avgen --project X --director mode=song --song-plan P --save-project OUT`, and `--render` with `--range`.
**GV3 consumes each stream through JSON**, so each stream ends with a headless dump that a Python
generator can read.

### setpieces (ADRs 928–931): set pieces in plans, and the evaluator hook
- **Read:** the brief's §9, §10, §15, §16 and §17; reports/director.md §2 (rows "More UFO events",
  "Hero moments"), §4 and recommendations 4–5; `src/stage/staging.hpp` (the whole file header);
  `tools/gv3/cast.py` (how GV3 stages its one abduction today, as data); the Critic's
  `~/Documents/GitHub/creative-critic/INTEGRATION_GUIDE.md` §3–4.
- **What exists:** `stage::StagingDesc` can already express a lot: several scenarios, each with its
  own seed, `startOn`/`stopOn` events, `maxCycles`, region queries (`center` + `radius`), claims,
  several bound roles, parallel cues and the `stillRoles` gate. What is missing is a way to *plan*
  them. The Plan cannot author a scenario (`validator.cpp:495`, `:622-653` only reads them), and
  every GV3 set piece was hand-written beat by beat in Python.
- **Deliverables:**
  1. **Set-piece templates in `stage/`.** A template is a parameterised `ScenarioDesc` with named
     slots, instanced with overrides. Ship at least:
     - `abduction`: 1–3 animals lifted in parallel (one role and one cue each), with the `stillRoles` gate kept;
     - `flyby`: a crossing on a path;
     - `survey`: the beam sweeps a field and lifts nothing.
     
     Variation comes from parameters:
     - place (a point, or a region plus a tag query with `clearance`);
     - approach bearing, hover height and duration;
     - beam colour and intensity;
     - animal count;
     - lift speed and spin.
     
     One craft can play several set pieces in sequence. The compiler orders them on its timeline, and refuses overlaps and travel it cannot make in the gap.
  2. **`PlanSetPiece`,** a `directing::Plan` item modelled on `PlanCue` (`plan.hpp:191-203`).
     - Its time is a musical anchor (bar, section or event) or seconds; its place and variation as above.
     - It compiles to staging scenarios next to the authored ones.
     - The validator refuses: an unknown template, a target with no clear air, a time outside the song, and one craft in two places.
     - It warns when two set pieces share a place or a framing distance ("do not duplicate the same abduction shot").
     - It round-trips and supports undo.
  3. **Staging events per set piece**, e.g. `setpiece/<id>/beam`, `.../lift` and `.../depart`, on the bus and in seek replay. Event cameras (ADR-245), cue placement (ADR-767) and routes can then key on them.
  4. **The evaluator hook** (`director.evaluate`, `director.compare`):
     - It renders a scratch copy of a requested span, then calls the Critic CLI with `critic submit ... --video-start <a> --wait --json --strict`, using the adapter's scene and intent export.
     - It returns an `EvaluationReport`: findings keyed by time span and plan item, plus metrics.
     - It stores evaluations per plan revision beside `directingPlans`. `director.compare` diffs two revisions.
     - Autonomy policy: iterate in scratch, show only the winner (ADR-757 must not stop every iteration).
     - The Critic's path is configurable. A missing Critic is a clear error, never a silent pass.
     - Tests use a stub evaluator. One ScriptedProvider test runs propose → evaluate → Modify → compare.
  5. **A headless path GV3 can use:** a plan file with set pieces compiles into the saved project's staging (for example `avgen --project X --plan P.json --save-project OUT`, or whatever fits the existing CLI). A trace of where each set piece's craft and animals actually were comes from `avgen_cast_trace`.
- **Proof:** a GPU or trace test in which three abductions at three places, with 1, 2 and 3 animals, play in one film with one craft. Each lift happens under a still craft. Seek lands on the same frame as play.
- **Stay out of:** the route chain (routes), Song Mode (song).

### render (ADRs 917–919): post and offline quality at any resolution
- **Read:** the brief's §13 and §14; reports/render-post.md, all of it (engine gaps 3, 6 and 7 are
  this stream's); `docs/image-formation.md`.
- **Deliverables:**
  1. **Post radii that hold across resolution** (gap 3): bloom level count and reach, anamorphic stretch, halation radius and motion-blur tiles scale with the output's reference size, so a 960×540 preview, a 1080p×2 final and a 4K×2 final show the same look.
     - Pick one reference (the preview) and scale from it.
     - Record every scene whose look changes.
     - Test: GPU renders of one scene at two resolutions, compared after downsampling, with a control that fails on today's code.
  2. **Fog colour from the sky** (gap 6): aerial perspective takes the sky's radiance, including the aurora, as an option on the existing fog.
     - The distant rim must read as air, not as a dark cut-out.
     - Test: a difference image against a constant-colour control.
  3. **The offline tier raises authored sample counts** (gap 7): volume steps, anisotropy and the sky cube's resolution get tier floors, and a log line says what was raised.
     - Test: the counts reach the renderer (a log or measured value), and the sky's banding falls at 4K (measure it).
  4. **A report on the world edge** (gap 2): what it would take for `WorldMap` to be larger than 640 m, or to draw a backdrop ring beyond it. Say whether it fits in this stream.
     - If it is contained (hours, not days), build it with tests; otherwise report.
     - GV3 will close its valley ends with ridge features in scene data either way.
- **Stay out of:** motion-blur reset at cuts (camera), water (water).

### song (ADRs 920–923): musical shot durations and arcs
- **Read:** the brief's §7 and §8 and the Director assessment; reports/director.md, all of it;
  01-music.md; signals' ADRs 896–899 as merged.
- **Deliverables:**
  1. **Intent survives into the Director.** Carry arc, energy and visualDensity through `songPlanFromCues` (`song_plan.cpp:350-393`), and give `SectionCue::intentAt` a caller.
  2. **Musical cuts in `directSong`.**
     - Every cut lands on the beat grid, and a section boundary on its downbeat (signals' musical time).
     - A Rising arc gives monotonically shorter shots. Burst gives short ones; Suspended holds.
     - Rising and Burst may go below the global minimum.
     - A drop opens its own shot on the downbeat.
  3. **Durations from music and content,** not one global band:
     - density (signals' onset rate and energy composite);
     - how much the subject moves;
     - whether the shot establishes scale (longer, for contrast).
     
     Write the rule down in the ADR and keep it deterministic.
  4. **Peak sections go to the event subject:** a staging set piece (setpieces' events), the most important hero, or the subject of the section's event, instead of the rotation.
  5. **`camera/focus/emphasis` and `ShotSpan::emphasis`:** wire them up or cut them (ADR-442).
  6. **A headless cut report GV3's generator can read:** each shot's span, subject, arc, and the reason for its duration.
- **Tests:**
  - In `test_song_director.cpp`: Rising gives monotonic durations; every cut is on a beat; the label-scramble test still passes.
  - `installSequence` drops no baked track.
  - On `~/Desktop/Rebuild.mp3` (skip cleanly when absent):
    - cuts land on downbeats within one frame;
    - durations are not uniform (state the measure);
    - the riser (bars 89–96) accelerates;
    - the drop (177.71 s) opens a shot.

### reactivity (ADRs 924–927): the reactivity planner
- **Read:** the brief's §3, §4, §5, §6 and §16; reports/director.md recommendation 2;
  reports/modulation.md and reports/mushrooms-wind.md, all of them; signals', routes' and emission's ADRs as merged.
- **Deliverables:**
  1. **`PlanRoute`,** a Plan item compiled to `params::ModRoute` plus sources. It carries routes' `delayMs` and `depthSource`. It round-trips, supports undo, and is seek-exact.
  2. **A `ReactiveCatalog`** in `CapabilityRegistry`, generated from engine data (ADR-754):
     - emissive materials (program-owned emission and `emissiveBoost`);
     - the mushrooms' per-layer lanes and emissive fields;
     - particle systems;
     - scatter populations;
     - effect fields, including fields timed from the last event;
     - lights;
     - atmosphere and fog;
     - wind;
     - water tears.
     
     Each entry has its neutral value, a safe range, and what kind of change it makes (luminance, hue, motion, density).
  3. **The validator:**
     - It refuses dead targets through routes' liveness registry.
     - It flags a plan where everything follows one source, where every route shares one phase, where one entity is over-saturated, or where a phase-rate trap appears.
  4. **A deterministic default proposer:** catalogue × musical layers → routes at three levels:
     - **micro:** hats and snares on small, fast, local things;
     - **meso:** the kick and bar on heroes, and waves through the mushrooms along the bar;
     - **macro:** section energy on the ecology light, fog, emission gain and colour, through `depthSource`.
     
     Phases are staggered with `delayMs`. Each hero gets its own source, timing and amplitude, and depth follows the section. It must not be "everything pulses to the beat".
  5. **A headless dump** of the proposal (routes plus the reason for each) for GV3's generator to read, edit and install.
- **Tests:**
  - golden plans on a fixture scene;
  - the dead-target refusal, with a control;
  - the "everything on the kick" flag;
  - round-trip;
  - one GPU difference image per target kind, proving that a proposed route reaches the pixels.

## Phase 3: the GV3 scene revision, in parallel streams

The plan is [03-revision-plan.md](03-revision-plan.md) and the baseline is
[04-iterations.md](04-iterations.md) iteration 0. The engine features and their GV3 recipes are
in [stream-reports/](stream-reports/): signals, routes, emission, song, reactivity, water,
characters, setpieces, and checkpoints (camera).

### Common to every Phase 3 stream
- **Worktree:** `~/Documents/GitHub/av-gen-gv3-<topic>`, on branch `gv3/<topic>`, from `gv3/production` `334c4cf6`. That is the first pass plus the Phase 3 foundation: the authored energy arc and the meter pins. Assets are linked.
- **Engine:** never build the engine in your worktree. Use the shared build:
  `mkdir -p <wt>/build && ln -s /Users/natefaulkenberry/Documents/GitHub/av-gen-engine/build/release <wt>/build/release`
  - The shared engine is `040d6644`: main plus setpieces. The coordinator announces a newer build (with characters and render) as a new path.
  - `tools/gv3/ground.py` and every command below then work unchanged.
  - Never write into the shared engine worktree.
- **Generate** with `python3 tools/make_glowmere_valley_3.py` in your worktree.
  - **Never commit the generated files:** `examples/world/glowmere-valley-3.{json,scene.json}`, `docs/glowmere-valley-3/03-directives.md` and `04-shot-plan.md`. Restore them with `git checkout --` before each commit; the coordinator regenerates after merging.
  - Commit your generator changes and your notes only.
- **Render** a clip with an absolute path, through the lock:
  `tools/gpu-lock.sh build/release/src/avgen --project examples/world/glowmere-valley-3.json --render $PWD/build/gv3/<topic>/<name>.mov --size 960x540 --range a:b`
  - A range render warms particles for at most 240 frames, and a seek is not a play for particles or the cast. Start ranges early enough for what you judge.
- **Evaluate** with the Critic daemon on 127.0.0.1:8765. It is the coordinator's: never stop or restart it.
  `~/Documents/GitHub/creative-critic/.venv/bin/critic submit --inputs <inputs.json> --video <clip> --video-start a --mode preview --session gv3-<topic> --track <category> --label <iteration> --wait --json --strict`
  - Run it as a background command.
  - `work/gv3-v2/inputs.json` is the first pass's scene and intent. It is correct for the look and world streams, whose shots do not change.
  - The critic-adapter stream will document regenerating inputs from a revised project; the cut and cast streams need that.
- **Before and after (brief §17):** "before" is the same range rendered from `gv3/production` `334c4cf6`. Put stills and sheets in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/<topic>/`, and use the Critic's comparison between the two jobs.
- **Record every iteration** in `docs/glowmere-valley-3/revision/phase3/<topic>.md` (committed): what changed, what the evaluator measured, what was decided.
- **Stay inside the files you own** (below). If you need a change elsewhere, say so in your report.
- **The standing rules:**
  - never commit the song or the licensed assets;
  - never modify `glowmere-valley-2-multicam`;
  - headless only (no windowed app, no `--capture-ui`);
  - never run `tools/make_abduction_scenario.py`;
  - use log names prefixed with your topic in any shared folder;
  - at most `-j 4` for any build;
  - commit early (the account's usage limit keeps stopping agents).

### gv3-look: audio reactivity, the drop, water, wind
- **Owns:** `tools/gv3/look.py`, `tools/gv3/directives.py`, and a new `tools/gv3/reactivity.py`. It may edit the scene's water and wind blocks.
- **Audio reactivity, the highest priority (brief §3–5, §16):**
  - Make the Director's proposal a generator step: `avgen --project P --propose-reactivity OUT.json`, then install it as `install_reactivity.py` does (`~/Desktop/av-gen-review/reactivity-adr924-927/`).
  - Follow "How GV3 should install and tune it" in `stream-reports/reactivity.md`, and the mushroom lanes, the bar and drop waves and the hero boosts in `stream-reports/emission.md`.
  - Keep the elder's heartbeat and the horse's gold. Nothing lockstep, nothing on one source.
  - Prove each target group on the three tiers: configured (`--audit-routes`), behavioural (the Critic's route-locked "configured and observed"), and meaningful (sheets, judged at 1080p where small things matter).
- **The drop without the style shift (brief §7, plan §2):** hold the grade within the film's range and express the drop through depth, waves and activity. Check the evaluator's `visual_coherence`, and the hue histograms plateau against drop.
- **Water:**
  - the base: ripple 0.1, rippleScale 2.6, bass route 0.03 (`stream-reports/water.md`);
  - the tears in the scene's valley water block, with the values from that report;
  - fix `"shoreFade"` to `edgeFade`.
- **Wind (brief §6):** the scene's wind visibly moves grass and ferns without a storm. Its modulation comes from the reactivity proposal, but check its base values; the tears follow the wind.
- **Post** (AgX, bloom, grain, depth layers) waits for the render stream. Leave it until the coordinator says the render engine is in.

### gv3-cut: the Director's cut, pacing, cameras
- **Owns:** `tools/gv3/shots.py`, `rig.py`, `cuts.py`, `framing.py` and `review.py`, plus `install_cut` and the `autoDirector` block in `tools/make_glowmere_valley_3.py`.
- **Pacing (brief §8):**
  - Take cut times and arcs from the Director's Song Mode cut: `avgen_song_cut`, with the settings in `stream-reports/song.md`, which the first pass's band prevented. Keep composition authored and evaluator-checked: every span gets an authored rig.
  - Use the evaluator's novelty measure to trim shots that have said their piece.
  - There is no global duration.
- **Aliens not the whole film (brief §9):** alien-led screen time is 38% today; the target is 20% or less. Aliens appear in wides and reacting to events.
- **Frame the UFO events** E1–E5 (plan §3; times and places in `audit/data/setpieces/gv3-ufo.plan.json`). The gv3-cast stream implements them; you frame them, and feed their moments to Song Mode as plan events, so peaks go to them.
- **Camera (brief §2):** apply ADR-913's follow values (`stream-reports/checkpoints.md` § Camera) to every follow rig. Check the stability bar with `tools/camera_stability.py` and the Critic's stability.
- **Keep** the s14/s39 rhyme and the riser's compression into the drop.

### gv3-world: the world edge and the offline configuration
- **Owns:** `tools/gv3/world.py`, and a new `tools/gv3/offline.py`.
- **The world edge:** the 640 m valley shows its edge in wides. Close the valley's ends with ridge features in the terrain's `world` block (render-post.md: "closing the valley ends with ridge features"). Check that no wide shows an edge; the render stream's sky-coloured fog helps later.
- **Offline 4K configuration (brief §13)**, from `audit/reports/render-post.md`, as `offline.py`, applied only for the final render (a flag or a separate output):
  - shadow cascades 4, shadow range 160 with 300 keyed on the wides;
  - terrain view distance 1000 with LOD off;
  - the fog march on (32 steps, `volumeMaxDistance` 220, `horizonDensity` 1);
  - rigs at every frame (`updateHz` 0);
  - the render block: 3840×2160, supersample 2, tier offline, limits tier, prores422.
  
  Verify the render log lines named in render-post.md on a 2 s range. The render stream's reference-height post radii and floors come later.
- **Measure the 4K cost** on 2 s ranges; never render the whole film at 4K.

### gv3-cast: the aliens, the animals and the UFO events (starts when characters merges into main)
- **Owns:** `tools/gv3/cast.py`, and a new `tools/gv3/ufo.py` with the plan file.
- **Characters (brief §10–12):** apply `stream-reports/characters.md` "How GV3 should use this": the aliens' settings per alien, the animals' slope and turn radii, and the flank animals re-homed onto the meadows.
- **UFO events (brief §9):**
  - add the `scout` craft;
  - remove the hand-written `abduction` scenario and `horse_light()`'s keys;
  - compile the E1–E5 plan with `avgen_cast_trace --plan --save-project` as a generator step;
  - re-validate E4 after the re-homing; it needs two animals within reach.
- **The gate:** the ADR-910 metrics on the whole-film cast trace (longest still stretch, reversals, pivots, slope under stationary bodies), and the set pieces' measured beats.
