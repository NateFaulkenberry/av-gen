## Director Agent audit: GV3 revision, Phase 1 (read-only)

I ran one existing binary: the engine's Auto-director on GV3, headless, under `tools/gpu-lock.sh`. Its log and the saved project are in the scratchpad at `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/audit/` (`edited.log`, `gv3-edited.json`). No file in either repo was touched.

### 1. What "the Director" is

There are four engine pieces. None of them made a creative decision in GV3.

- **Auto-director, Continuous and Edited modes** (`src/app/cinematic.cpp`, `camera_director.cpp`; ADR-062/071/075/202/203/892). It folds the audio from RMS and centroid events into `signals::MusicalStructure`, then bakes keys for one camera.
- **Song Mode** (`song_director.cpp`, `song_plan.cpp`; ADR-247/249). Each section's intent is six numbers. From them it picks the camera, the shot count and the framing, and writes a camera shot track.
- **Director Agent**: an LLM working through `src/ai` (ADR-094). It writes a `directing::Plan`, which `src/directing` compiles (ADR-750–770).
  - It works one request at a time, and every plan waits for the person's approval.
  - The owner paused it on 2026-09-25 (`docs/development/director-system-progress.md:37`).
- **Satellites**:
  - World Director knobs, which drive macro routes (ADR-041);
  - `seq::SectionPerformanceTable`, which declines every section kind by design (`src/seq/section_performance.hpp:10-16`);
  - `stage::Staging` scenarios (ADR-210).

**What they decide is the camera, nothing more.** That means the camera parameters, the aim-follow table, Song Mode's camera track, and the shot spans that gate HeroFocus and CameraTravel effects (`camera_director.cpp:1042-1061`). The Plan adds shots, markers, character orders, parameter and effect cues, and retimes (`directing/plan.hpp:231-257`). **No director writes modulation routes, looks or staging scenarios.**

GV3 stripped all director output (`tools/make_glowmere_valley_3.py:107-121`). Every cut, look and route came from `tools/gv3/*.py`. So when `08-final-evaluation.md:152` says "the Director's camera collection", that is `scene::CameraDirection` filled in by `rig.py`, not a Director decision.

### 2. Capability table

Classes: **(a)** implemented and used by GV3, **(b)** implemented but unused by GV3, **(c)** partial, **(d)** missing.

| Capability | Evidence | Class |
|---|---|---|
| Sections | Fold at `cinematic.cpp:1242-1306`; detector (ADR-206); section timeline (ADR-247). On "Rebuild" both are wrong (§3, and 01-music §1.6) | c; GV3 bypassed them |
| Intensity | `energy` is RMS rescaled to the track's min–max (`analysis/structure.cpp:596-618`). On Rebuild the break reads 1.0 and the suspension 0.0 | c; misleading |
| Density | Per beat, it is the median of a per-hop onset flag (`structure.cpp:495`, `:229-263`), which comes out 0. All 10 Rebuild sections read 0.0. The test only range-checks it (`test_song_structure.cpp:297-298`) | c; dead input |
| Transitions | Song: a hard cut if energy jumps more than 0.18, otherwise a blend (`song_director.cpp:553-565`). Fold: a drop opens its own shot on the beat (`cinematic.cpp:1251-1262`) | c |
| Build / riser | Fold: Build → Discovery, FinalBuild → Ascent (`cinematic.cpp:1079-1124`). `ShotIntent` has Rising/Burst/Suspended arcs, but `songPlanFromCues` drops arc, energy and visualDensity (`song_plan.cpp:350-393`), and `SectionCue::intentAt` (`section_cue.cpp:18-20`) has no caller | c; accelerating cuts are d |
| Drops | Fold only; it found none on Rebuild | c |
| Quiet passages | Breakdown → a slow, close shot (`cinematic.cpp:1702`) | c |
| Repetition | `occurrence` makes repeats *differ* (ADR-249); it never makes them *rhyme*. Rebuild has a single repetition group | c |
| Visual variation | Golden-angle bearings and hashed camera choice: geometry only, nothing about the image | c |
| Shot density / duration | One global min–max band, equal division inside a section, no beat snapping, no content input (`cinematic.cpp:1294-1303`; `song_director.cpp:340-360`, `417-428`). Locked autonomy means one shot per section | c; weak |
| Visual hierarchy | Spotlight emphasis is emitted to `camera/focus/emphasis`, which is never registered, so install drops it (`camera_director.cpp:37`, `:936-945`; my run logs "left out"). `ShotSpan::emphasis` is saved (`engine.cpp:1548`) but nothing reads it | c; effectively a no-op |
| Environmental / secondary activity | None, apart from gating effects by the cut (ADR-207) | d |
| Audio-reactive opportunities | No director reads or writes routes. The Plan has no route item. The capability registry lists characters, cameras, events and effects only (`directing/capabilities.hpp`) | d |
| Recurring motifs | None | d |
| Hero moments | Fold: "a drop goes to whoever's turn it is" (`cinematic.cpp:1549`). Event cameras exist (ADR-245) but Continuous mode ignores them (ADR-892). Cues can be placed on watched events (ADR-767) | c / b |
| Escalation | Only FinalBuild/FinalDrop. `finalOfKind` is computed but never read (`section_cue.cpp:37`). ADR-041's 80/15/5 guideline is "enforced by review rather than by code" | d |
| Character orders | Plan performances and goals (ADR-758/766/824/828) | b |
| More UFO events | The Plan cannot author scenarios; it only sees them when checking camera precedence (`validator.cpp:495`, `:622-653`) | d |
| Framing critique (ADR-769) | One still per shot; checks in-frame and size, not occlusion (finishing-list item A4). GV3 re-implemented it per frame in `tools/gv3/framing.py` | b / c |

### 3. Key findings

**1. The engine cutting GV3 itself (measured).** I ran `--director mode=edited` with the project's own settings (minShot 4.6 s, maxShot 6.5 s, dwell 2).
- **The fold found no musical events.** Its 8 sections are an intro plus seven "Verse" sections. Verse means the beat clock's 16-bar counter moved. Every boundary (29.58, 59.09 … 206.78 s) lands one beat (441–467 ms) before the true downbeat. No pull-back, break, riser or drop was detected.
- **The pacing is uniform.** 34 of 39 shots last 5.90–5.92 s; the rest are the 29.58 s intro split five ways and four 4.88 s shots at the end.
- **The cuts miss the beat.** None of the 38 cuts lands on a downbeat; the median cut is 99 ms off the nearest beat.
- **The drop is buried.** 177.71 s falls 0.44 s into a travelling shot handing off from ridge-cap to vane.
- **The limits it was given are not met.**
  - The view-rate cap asked for 7°/s; the cut peaks at 640°/s.
  - 20 of 210 keys that hold a hero were left obstructed, the worst by 892 m.
- **It decided nothing about effects, routes, the look or the characters.**

**2. GV2 multicam (the owner's previous film, same song) was Song Mode at Locked autonomy.**
- **Its pacing was hand-made.** Its 45 shot spans sit on the 44 sections the owner hand-edited: each is `authored: true` with `edited: [type, shot-intent, start, end]`, and they arrived in the owner's "tweaks" commits of 2026-09-17. The median shot is 3.70 s.
- **The subjects came from rotation, not the music.**
  - The riser is a single 7.39 s shot of scree-cap.
  - The drop shot went to spire-cap because it was spire-cap's turn.
- **The hero pulses were hand-authored.** Its 18 `beat.pulse` routes and 16 HeroFocus ground pulses were written by hand; the Director only gated them with its cut.

**3. The musical inputs are unreliable on exactly this kind of track.** "Rebuild" is a flat-loudness master (1.1 LU range).
- Energy is derived from loudness.
- Density is structurally zero.
- The bar phase is off by one beat (02-art-direction §2.6).
- The richest intent semantics (arc, visualDensity) are dropped before any director sees them.

**4. The LLM Director Agent is blind to both the music and the image.**
- **Music:**
  - `director.inspect_scene` returns only `{type, occurrence, start, end}` per section (`director_tools.cpp:125-160`).
  - `MusicalContext` carries no energy (`time_ref.hpp:99-114`).
  - `audio.get_analysis` returns a single frame (`engine_tools.cpp:2976-3003`).
- **Image:** rendering to a file and vision are both declared unavailable (`src/ai/capabilities.cpp:78-85`).
- **Limits:**
  - Each task is capped at 12 turns, 64 calls and 300 s (`orchestrator.hpp:117-119`).
  - Each task starts a fresh conversation.
  - The feasibility report's benchmark marks "make the whole song feel more cinematic" as "quality uncertain" (§24, row 16).

**5. Validation covers feasibility, not quality.** None of the issue codes (`directing/issue.hpp:28-54`) covers pacing, repetition, hierarchy, off-beat cuts or dead routes.

### 4. Can it take feedback from an external evaluator?

Not today.

**What exists and could be reused:**
- plan identity and revisions, with `produced` fingerprints (ADR-755);
- Modify/Regenerate, which re-brief the model with the current plan (ADR-770);
- scratch-copy sessions (ADR-753/764);
- deferred host hooks that play a scratch copy and return structured results (`director_tools.cpp:290-349`).

**What a hook would need:**
- **`director.evaluate`**, a host hook like the watcher. It would render a scratch copy at a requested depth, call the evaluator (through a CLI, since no MCP server exists), and return an `EvaluationReport`: findings keyed by time span and plan item, plus metrics.
- **History:** evaluation results stored per revision beside `directingPlans`, and a `director.compare` tool.
- **Audio-reactivity evidence at three levels:**
  - configuration: the route inventory;
  - behaviour: a parameter trace over a full play (extend `tools/cast_trace.cpp`);
  - visible effect: a render with each route on versus off.

  Only the on/off render catches routes like F23/F24's, which loaded with zero warnings and did nothing.
- **An autonomy policy:** iterate in scratch and show the person only the winner. Otherwise the approval gate (ADR-757) stops every iteration.
- **Room to run:** larger budgets, or evaluation deferred the way watches already are.

### 5. The owner's question

**No, the engine's Director could not reliably make another high-quality audio-reactive Glowmere video from scratch.** GV3's first pass is not evidence that it could, because the Director made no decisions in it.

Left to itself on this song, it produces a uniform 5.9 s rotation of heroes, off the beat and blind to the drop. GV2 looked good for three reasons:
- the owner hand-cut the timing;
- the rotation happened to cycle through beautiful heroes;
- hand-authored routes made those heroes pulse.

So it was less luck than this: the environment and the human or agent authoring carried both films, and the Director contributed camera geometry.

What is missing:
- a trustworthy model of the music;
- intent that survives into the director (arcs, escalation);
- shot durations that respond to what is on screen;
- a planner for audio reactivity;
- a way to plan set pieces;
- an evaluation loop.

### 6. Recommendations (ranked; each extends an existing piece)

1. **Fix the musical inputs.**
   - `structure.cpp:495`: measure density as an onset rate (mean or count), not the median of a flag.
   - Replace raw-RMS energy with the composite from 01-music §1.3 (high band, centroid, flux, stereo width).
   - Detect the downbeat phase in the beat tracker and the transport.
   - Expose per-section band and brightness data in `song::SectionCue`, `SongPlanSection`, `directing::MusicalContext` and `director.inspect_scene`.
   - *Tests:* synthetic tracks from `tools/make_test_audio.py` containing a layer entry, a two-bar drop-out and a filtered break. Assert boundaries within 30 ms of true downbeats, density that varies, and a break that is not the loudest section.
2. **A reactivity planner in the Plan.** This is what would "find the mushrooms" automatically.
   - Add a `PlanRoute` item, modelled on `PlanCue` (`plan.hpp:191-203`), compiled to `params::ModRoute` plus sources.
   - Add a `ReactiveCatalog` to `CapabilityRegistry`, generated from engine data (ADR-754): emissive materials, particle systems, scatter populations, effect fields, lights, atmosphere, wind.
   - Have the validator refuse targets known to be dead, such as `emissiveBoost` on a procedural node (F23), and flag a plan where everything reacts to the kick.
   - Add a deterministic default proposer: catalog × musical layers → micro/meso/macro routes with staggered phase.
   - *Tests:* golden plans, the dead-target refusal, undo and round-trip.
3. **Musical shot durations and arcs in Song Mode.**
   - Carry arc, energy and visualDensity through `songPlanFromCues`.
   - In `directSong`: snap cuts to the beat grid, and shape the cut rate with `intentAt` so a riser accelerates.
   - Let Rising and Burst arcs go below the global minimum shot length.
   - Give peak sections to the most important or event subject instead of the rotation.
   - Wire up or cut the dead `camera/focus/emphasis` track and `ShotSpan::emphasis` (ADR-442: no compatibility shims).
   - *Tests:* in `test_song_director.cpp`, a Rising section yields monotonically shorter shots, every cut is on a beat, and the existing label-scramble test still passes. Add a test that `installSequence` drops no baked track.
4. **The evaluator hook from §4.** Files: `director_tools.cpp`, `ai/tool_context.hpp`, `app/directing_record.*`. *Test:* a ScriptedProvider test that runs propose → evaluate (with a stub evaluator) → Modify → compare.
5. **Scenario items in the Plan.** Let a plan place the abduction at a chosen place and time, with variation, so UFO activity can happen throughout the valley.
