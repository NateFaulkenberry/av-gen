# The Living Prism 03: integrated audit, plan and readiness verdict (lead)

- **Brief:** `00-brief.md` (the owner's, verbatim).
- **Evidence:**
  - `01-rendering-audit.md` (Sub-Agent A: rendering and GPU);
  - `02-audio-modulation-audit.md` (Sub-Agent B: audio, MIDI and modulation);
  - the lead's own probes, named below.
- **Branch:** `audit/living-prism` from main `5e0f5b02`.
- **Changes made:** none to production code, shaders or tests. Probes live under `probes/`.
- **Labels:**
  - **[fact]** verified in code or measured;
  - **[est]** an estimate, with its basis;
  - **[rec]** a recommendation.
- **Status vocabulary:** the brief's §2.2 vocabulary throughout.
- **Detail:** this report integrates the two audits and does not repeat their tables. Each section points to where the
  full evidence is.

## 1. Executive summary

**What is already strong [fact]:**
- **The live renderer** is mature, well instrumented and not the problem:
  - a single-encoder forward renderer with about 24 labelled stages;
  - no CPU wait on the GPU anywhere in the live path;
  - a live profiler with CPU/GPU splits, removal arms and an A/B tool.
- **Three of four representative live scenes hold 1080p30:**
  - Rainbow World, THE RIFT and Event Horizon run at ≤ 6 missed deadlines per 900 frames.
  - The automatic quality ladder is stable. It doesn't oscillate beyond designed probation reverts, and a level change
    causes no measurable stall (01 §4-5).
- **One modulation model serves everything:** one signal bus, one route model and one fixed-order route chain (delay,
  normalise, trend, curve, clamp, threshold, smoothing, envelope with latch, spring, remap and integrate).
  - It serves every scene, input and mode.
  - Routes are cheap: about 30 ns each, so 4,000 routes cost 0.13 ms (02 §3).
  - Seek equals play is engineered in depth.
- **Most of the "VFX vocabulary" the scene concept implies already exists and is shared:**
  - onset, wave and spectrum fields, including fronts delayed by distance;
  - seven grid simulation modes, including an excitable propagation grid and decay;
  - GPU particles with trails;
  - per-instance effectors;
  - compiled material programs;
  - 48 Effect Library kinds.
- **Stems and per-channel live sources are merged and working.**
  - On a separated drum stem the onset detector scores 53/53 with no false triggers.

**What is genuinely missing or wrong [fact]:**
1. **No same-frame, named, stateful musical signal.** A conditioned "bass breath" or "drum hit, else the mix" can only
   be copied into every route that uses it, or parked in a macro.
   - Macros publish **one frame late** (33 ms at the 30 fps target).
   - Rainbow World needs 405 routes for about 15 musical ideas: 179 read macros, chains are copied, and every stem
     effect has a mix twin.
2. **`EnvelopeSource` defect:** it reacts a frame late and never shows its own peak (0.44 of full at 30 fps for a
   60 ms release). It is a shared primitive used by traces and two scenes.
3. **The mix kick/snare/hat classes are slow and, for the kick, wrong.**
   - The mix kick's precision is 0.54-0.57: it fires on bass notes, snares and hats.
   - Class decisions arrive 28-67 ms after the onset, so live sound-to-screen for a snare is about 110-130 ms [est].
   - They are the most-routed audio signals in the live collection: 276 uses in 41 of 44 projects.
4. **Crystal surfaces can't be crystal yet.**
   - Procedural surfaces cannot be translucent or refractive: Blend is drawn opaque. Procedural is the path 40 of 43
     live projects use.
   - Screen-space reflection exists only in water.
   - Low ladder rungs upscale bilinearly, with no temporal reconstruction.
5. **The measuring instruments are partly dishonest:**
   - GPU pass times are quantised to 65.5 µs, so new effects can't be costed individually;
   - the headless profiler pays a 6.2 ms sky rebuild the live path doesn't;
   - live "over budget" counts vsync jitter;
   - a quality drop never records *why*.

**The most important recommendation [rec]:** do a short, cheap foundation pass (P0, about 1.5-2.5 weeks of
engineering [est]) before the scene specification is built. It has two parts:
- make the instruments honest;
- fix the envelope;
- add **conditioned signals**, a source kind that runs the existing route chain once and publishes a named signal in the
  same frame.

Then do **refraction**, **SSR** and **the mix kick/snare/hat phase-out** as P1, in parallel with the art specification.
Nothing here calls for a renderer rewrite, a new modulation framework or a new effects framework.

## 2. Verified architecture map

The full file:symbol detail is in 01 §1 (renderer) and 02 §1 (signals). The live frame, in order [fact]:

| Stage | Code | Notes |
|---|---|---|
| Loop and pacing | `Application::runLive` (`src/app/application.cpp` l. 4745); `liveFrameCapPeriodMs` (ADR-1107) | whole vsyncs, fixed deadline grid |
| Clock | `Engine::tick` (`src/app/engine.cpp` l. 5955) | `FrameTime{renderTime, deltaTime, frameIndex}` |
| Analysis consumed | `Engine::consumeAnalysis` (l. 6019); live channel sources `publishInputSources`; offline stems `publishStems` (ADR-1520/1521) | `audio.*`, `response.*`, `stem.<k>.*`, beat clock |
| Control | `controlHub_.update` (MIDI CC, clock, transport) | MIDI drained once per render frame |
| Time signals | `updateTimeSignals`, `updateTimelineClock` | `beat.*`, `music.*`, `timeline.*` |
| Sources | `sources_.update(bus_, …)` (l. 7214) | LFO, envelope, timeline, macro (**publishes the previous frame's finals**), interpret |
| Scene states | `states_.update` (ADR-1124) | after sources, before finals reset |
| Automation | `timeline_.apply` | first modulation layer |
| Fields | `controller_->updateFields` (ADR-097) | before routes, so fields are not a frame behind |
| Entity and staging signals | `publishEntitySignals`, `publishStagingSignals` | one step of latency by design (ADR-703) |
| **Routes** | `modulator_.applyRoutes` (l. 7247) | the chain; same-frame for every source published above |
| Palette, behaviours, sequence | after routes | |
| Flatten, Effect Library | `updateEffects` (l. 6894) | |
| Quality | `applyLiveRung` (application.cpp l. 7488) | the ladder's ceilings |
| Render | `SceneRenderer::render` (`src/rendering/scene_renderer.cpp` l. 2794-5167) | ~24 labelled passes (01 §1.2) |
| Present | `OutputManager::presentAll`; Fifo; `PresentPacer` (ADR-1089) | projection, Syphon/IOSurface, NDI |

**The parameter-to-pixel boundary** (01 §1.3) has five consumers:
- material-program constants;
- 16 field slots per frame;
- per-instance effectors;
- entity FX lanes;
- particle uniforms.

**Lead's probe, the "timeline one frame late" question** (coordinator scratchpad `lag/`, 2026-10-22) [fact]:
- A value-mode timeline step lands on its own frame, both for a global post parameter and for a material-program
  constant.
- An event-mode timeline hit fires on the first frame whose interval contains it (ADR-900). That is ceil quantisation,
  0-1 frame.
- There is **no general engine lag**. The real one-frame hops are macros (ADR-1573) and `EnvelopeSource` (finding 2).

**Documentation that disagrees with the code** (01 §1.4, 02 §1.4) [fact]:
- `docs/rendering.md`'s frame diagram is two generations old.
- The "256 objects" limit is gone.
- The coordinator's "one medium slot" note is stale: `kMaxMedia` is 4 and drops are read.
- Nothing user-facing says Blend is drawn opaque on procedural nodes.

The code is authoritative in each case.

**Living Prism extension points [fact]** (01 §10, 02 §7):

| Need | Extension point |
|---|---|
| Crystal look and its modulation | procedural node + material program (compiled live) + FXL lanes |
| Impacts, fronts, memory | `scene.fields` (Onset, Wave, Spectrum, Grid), `scene.grids` (Scalar decay, Excitable) |
| Musical drivers | `response.*`, `stem.<k>.*`, feeds (ADR-1580), beat clock; after P0, `signal.*` |
| A new ladder lever | a field on `LiveQualityRung` (precedent `astralTier`, ADR-1222) |
| A specialised renderer (only if the spec demands it) | `EnvironmentRenderer` seam (ADR-1200) |

## 3. Rendering capability matrix: summary

The full matrix, with all ten fields per category, is in 01 §2. Status counts and what matters for this scene:

| Category | Status | Living Prism relevance | Action |
|---|---|---|---|
| Opaque geometry, procedural, instancing, GPU cull, LOD | Verified existing | High | reuse; budget by LOD and instance count, not resolution (geometry is a fixed cost: Rainbow World 23.9 → 15.5 ms over the whole ladder) |
| **Transparent / refractive procedural surfaces** | **Missing** (Blend drawn opaque) | **High** | **P1: refraction from a scene-colour copy** (reuse ADR-1286) |
| Emissive and bloom | Verified existing | High | reuse |
| Lighting and shadows (clustered, CSM/PCSS) | Verified existing (shadows re-render every frame) | High | reuse; caching deferred (1.7 ms, small) |
| **Reflections** | Existing but limited (IBL + bands; SSR only in water) | **High** | **P1: material-gated half-resolution SSR**, after a cost probe |
| Fog, volumes (4 media slots) | Verified existing | Medium | reuse; quarter-resolution lever is worth about 8 ms |
| Colour grading and palette (ADR-1420 looks) | Verified existing | High | reuse |
| Image-space treatments (lens, CA, glitch, mosh, scan) | Verified existing; cheap (post 0.3-1.7 ms) | Medium | reuse |
| Particles and GPU sim | Verified existing; duplicated inside two Environments | Medium | reuse `ParticleRenderer`; no third system |
| Procedural deformation and effectors | Verified existing | High | reuse |
| Event-driven VFX (fields, effect triggers) | Verified existing | High | reuse |
| Travelling fronts and ripples | Verified existing (straight-line distance and 2D grid) | High | reuse |
| **Propagation along connected structures** | **Missing** | Medium (spec-dependent) | scene-level first (spare instance lane); promote only on a second user |
| Persistent trails and accumulation | Verified existing (trails, grid decay, onset decay) | High | reuse |
| Temporal effects (echo, mosh, feedback, slit) | Existing but limited: a render-scale change resets them | Low-medium | P2: resample instead of reset, only if the spec uses them |
| **Antialiasing at low rungs** | Existing but limited (bilinear + FXAA, no TAA) | High for thin facets | **P1: spatial upscaler**; TAA only as a P2 spike on blockout evidence |
| Occlusion culling | Missing | Low until measured | defer |
| Adaptive quality | Verified existing (deep, stable) | High | keep; add a drop reason and a lever per new effect |
| **Profiling and diagnostics (rendering)** | Existing but limited (quantised timestamps, headless sky, shadow counts, jitter) | **High** | **P0: make the instruments honest** |

## 4. Audio, MIDI and modulation audit: summary

Full detail is in 02 §2-§8 (the signal-semantics table, five onset detectors, latency budget, four end-to-end
traces).

- **Signal semantics [fact]:**
  - `response.*` is the de facto shared layer: 43 of 44 live projects route it. Its loudness reference (ADR-1510) gates
    silence to 0.
  - `stem.*` is used only by Rainbow World; `feeds` by no live project.
  - Overlapping-looking families differ deliberately (02 §2.2). Don't consolidate them.
- **Defects found [fact]:**
  - `EnvelopeSource`: one frame late, peak lost.
  - `audio.<band>` auto-gain lifts a -70 dBFS noise floor to 0.88-0.96 after about 30 s.
  - The beat tracker locked 120 BPM at 60 on a synthetic pattern (octave error); its confidence can't see that.
  - `audio.onsetLow/Mid/High` are a look-ahead detector on file but the causal one live.
- **Modulation [fact]:**
  - The chain is complete. Nothing new is needed in its stages.
  - The gap is *naming*: there is no reusable, same-frame, stateful derived signal (finding 1).
  - Astral Forge Live runs its own analysis in C++. It's duplicated, but it works, so leave it.
- **MIDI [fact]:**
  - The clock tracker is solid; MIDI to bus is about 13 ms p50 (ADR-1025).
  - CC is 7-bit only, with no soft takeover, no relative encoders and no output feedback.
- **Instrument-aware input [fact]:**
  - Stems, live per-channel sources, feeds and per-source gain all exist.
  - Live and offline stems publish the same names.
  - Gap: the GPU audio history behind onset and spectrum fields hears **only the mix**, so a travelling front can't be
    "the drums" (P1).
- **Long-timescale behaviour [fact]:** the primitives are fine, but behaviour depends on the input. On Feline Footwear,
  Rainbow World's world scan fires at completely different times on the mix than with stems, because "the swell" is
  defined by the input.
- **Diagnostics [fact]:**
  - Present: route `lastOutput`, `--audit-routes`, `--sonic-trace` and the Live panel latencies.
  - Missing:
    - a live scope from source through chain to parameter final;
    - trace columns for `audio.*`, `control.*`, route outputs and parameter finals;
    - a sound-to-light measurement.

## 5. Performance report

Full detail is in 01 §4-5. Conditions: M2 Max, Release, 1920x1080, 30 fps target, AC power logged, under the GPU lock,
3 interleaved repeats of 900 frames, one muted track for every scene.

| | Rainbow World | THE RIFT LIVE | Digital Mosh Live | Event Horizon |
|---|---|---|---|---|
| Live misses per 900 (3 reps) | 1, 5, 1 | 1, 2, 6 | **6, 10, 13** | 2, 2, 1 |
| Live frame p99 | 39.6 ms | 37.1 ms | **48.4 ms** | 39.0 ms |
| Levels held live | Ultra 90% | High 23 / Med 29 / Low 48% | down to Emergency, still missing | Ultra 100% |
| Headless Ultra GPU p50 | 19.9 ms | 38.5 ms | 40.6 ms | 10.6 ms |

**Where the time goes [fact]:**
- the lit scene pass is 55-75% of every frame;
- post is 0.3-1.7 ms;
- volumes are about 7 ms at Ultra;
- **material programs** are up to 11 ms (THE RIFT) and have **no ladder lever**.

**Cross-cutting finding, rendering × modulation [fact + est]:** the target is **30 fps**, so every one-frame hop in the
signal path costs **33 ms**, not 16.7. At 30 fps:
- event quantisation adds 0-33 ms;
- a macro hop adds 33 ms;
- the old `EnvelopeSource` adds 33 ms.

Sub-Agent B's per-frame latency figures assumed 60 Hz. At 30 fps the live estimates for routes through a macro or `env.*`
grow by about 17 ms per hop. This raises the value of P0-1 and P0-2, which remove those hops.

**Profiling gaps [fact]:**
- quantised timestamps;
- the headless sky rebuild;
- procedural shadow draws counted as 0;
- live "over budget" counting vsync jitter;
- no drop reason;
- coarse CPU buckets: THE RIFT's 5.4 ms "lights" also holds a 1,021-entity pass, and the editor UI costs 7.8 ms in
  Rainbow World with no per-panel split;
- no bandwidth counters in WebGPU at all.

**Adaptive quality [fact]:** keep it. It is driven by deadline misses confirmed by GPU spans and has bounded probation
reverts, and THE RIFT degrades gracefully. Three gaps:
- it can't anticipate a heavy section of a song;
- it doesn't record why it dropped;
- new effects need a lever (a `LiveQualityRung` field) to be degradable.

**Digital Mosh Live misses at Emergency [fact].** This is outside the Living Prism scope, but it is a live-collection
reliability issue. [rec] a separate small investigation ticket, not part of this milestone.

## 6. Reusable VFX assessment

Rendering side 01 §3; signal side 02 §7. Verdicts [rec]:

| Effect category | Status | Verdict |
|---|---|---|
| Localised impacts and bursts | reusable, production-ready (Onset fields, particle bursts, effect triggers) | reuse; drive from per-source broadband onsets (P1-1) for speed |
| Travelling fronts and spatial waves | reusable (Wave/Onset/Spectrum fields, distance delay) | reuse; feed from a source once P1-4 lands |
| Ripples on surfaces | reusable (fields sampled in material programs and effectors) | reuse |
| Glints, sparkles, emissive accents | reusable (material programs, FXL lanes, particles) | reuse |
| Beams and light trails | reusable but scene-assembled (ribbons, trails, shells) | reuse |
| Particles and persistent trails | reusable | reuse; **no third particle system** |
| Surface and material modulation | reusable (material programs, compiled) | reuse; **budget ops against screen coverage**, since there is no ladder lever |
| Colour and palette transitions | reusable (ADR-1420 looks, OKLab blends) | reuse |
| World-scale environmental movement | reusable (deformers, effectors, fields) | reuse |
| Temporary accumulated effects with decay | reusable (grid decay, onset decay, envelopes); *named stateful accumulators missing* | P0-1 conditioned signals supply the shared accumulator |
| Propagation along connected structures | missing | scene-level (baked path distance in the spare instance lane, compared against a front); engine primitive only on a second user |

**Abstraction boundary [rec]:**
- **Shared low-level effect:** fields, grids, particles, material programs and effectors, as now.
- **Shared musical driver:** `signal.*` (new, P0-1).
- **Scene-authored decision:** which driver moves which effect, how much, and when.

Don't add an effects framework.

## 7. Research findings

Cited and dated in 01 §6 (rendering, R1-R5) and 02 §9 (audio, 9.1-9.6). The decisions they drive:

| Decision | Choice | Rejected alternatives | Key sources |
|---|---|---|---|
| Translucent crystal | refraction from one scene-colour copy, gated per material (about 0.1-0.3 ms [est]) | sorted blending (P2, if crystals are seen through crystals); weighted blended OIT (soft layers only); depth peeling | McGuire & Bavoil, JCGT 2013; AV Gen ADR-1286 |
| Reflections | half-resolution SSR, material-gated, with a ladder field (about 0.5-1.5 ms [est], probe first) | dynamic cubemap probes (re-rendered every time the music moves the light) | McGuire & Mara, JCGT 2014; Stachowiak, SIGGRAPH 2015; Lagarde & Zanuttini 2012 |
| Low-rung softness | spend effects before resolution, plus a spatial upscaler (FSR 1-style) | TAA spike only on blockout evidence (ghosting on reactive content); FSR 2 port; MetalFX (platform-locked) | Karis 2014; Yang et al. CGF 2020; AMD GPUOpen 2021/2022; Apple WWDC22 |
| Bandwidth | no pass reorganisation now | memoryless rework | Apple TBDR guidance; AV Gen ADR-119 |
| GPU timing | disable Dawn's `timestamp_quantization` (a native app has no fingerprinting concern) | — | Dawn `Toggles.cpp`; Chrome WebGPU developer features |
| Fast transients | per-source broadband onsets plus a register attribute at the onset frame; classes only for tails and counts | neural detectors; live source separation (seconds of context) | Bello et al. 2005; Böck et al. (madmom) |
| Latency target | ≤ 45 ms p50 sound-to-light for impacts (owner to approve) | — | ITU-R BT.1359 detectability (+45 ms) |
| Mapping and diagnostics | named derived signals plus a per-node live viewer | a new framework; more signal names | Synesthesia, TouchDesigner, Resolume and Notch documentation (02 §9.6) |

## 8. Refactor and removal candidates

Merged from 01 §7 and 02 §11. Do these **before** the scene:

| # | Candidate | Evidence | Fix | Effort |
|---|---|---|---|---|
| 1 | `EnvelopeSource` lag and peak | 02 §2.4 (probe B1), lead verified in code | fix; retire later once `signal.*` covers its users | S |
| 2 | Quantised GPU timestamps | 432/432 pass medians on a 65.5 µs grid | `disabledToggles` in `gpu::Context` | S |
| 3 | Headless profile skips the live sky schedule | 6.2 ms vs 0.03 ms CPU | set `LiveSkyLighting` in the headless run | S |
| 4 | Profile record: shadow counts, jitter-as-over-budget, capture warm-up, drop reason | 01 §4.3, §5.2 | record fixes | S-M |

**During** the milestone:
- 5. **Load-bearing "TEMPORARY" instrumentation.** 34 markers; the profiler reads them. Both agents flagged this.
  Move what the profiler reads into `CpuFrameBreakdown` and delete the rest. Effort S-M.
- 6. **Fallback doubling and chain copies.** Port Rainbow World's control layer to `signal.*` as P0-1's acceptance test.
  Effort M.
- 7. **`macro.one` used as a constant.** 48 Rainbow World routes; give `signal.*` a constant input. Effort S.

**After**, or by owner call:
- 8. **Mix kick/snare/hat classes.** P1-8, owner-led; detail in §10.
- 9. **`audio.onsetLow/Mid/High` mean two detectors.** Make live and file the same detector. Owner call. Effort M.
- 10. **Liveness false positive on trend chains.** Effort S.
- 11. **`docs/rendering.md` drift.** Effort S.

**Leave alone:**
- Astral's own analysis and its particle systems;
- the hard-wired shader-layer audio uniforms;
- the Effect Library's breadth (offline scenes use it).

## 9. Requirements-to-capabilities map

| # | Requirement | Supported by | Demonstrated in | Missing or uncertain | New shared capability? | Nature of remaining work | Perf implication | Readiness test |
|---|---|---|---|---|---|---|---|---|
| 1 | Richly layered 3D environment | procedural + LOD + GPU cull + material programs | Rainbow World, THE RIFT | geometry is a fixed cost the ladder can't remove | no | art/assets, with an LOD and instance budget | geometry-bound; budget by LOD | blockout meets perf P4/P5 |
| 2 | Emissive and reflective response | emissive, bloom, IBL, bands | every scene (emissive) | **refraction and SSR missing** | **yes (P1)** | architectural, gated per material | +0.1-0.3 ms refraction, +0.5-1.5 ms SSR [est] | per-effect cost on the blockout with unquantised timestamps |
| 3 | Expressive lighting, atmospheric depth | clustered lights, CSM, volumes, fog | THE RIFT, Digital Mosh | — | no | scene/art | volume about 7 ms at Ultra (ladder lever) | baseline windows |
| 4 | Strong, controlled palette modulation | ADR-1420 looks, CC 9/10, OKLab | Rainbow World | — | no | scene | ~0 | MIDI CC moves look predictably (trace) |
| 5 | Localised transient effects | Onset fields, particle bursts, effect triggers | Rainbow World, Event Horizon | class hits slow/misfire on a mix | P1-1 register attribute; P1-8 phase-out | architectural (small) + scene | ~0 | B1/B4 re-run: onset to frame ≤ 1 frame, register correct |
| 6 | Continuous audio-driven change | `response.*`, `stem.*` levels, chain | every live scene | band auto-gain lifts silence | P1-3 (owner call) | small fix | ~0 | noise-floor probe B1b |
| 7 | MIDI-controlled parameters | `control.*`, CC learn, routes | Rainbow World CC 1-10 | no soft takeover, 7-bit | P1-7 (soft takeover) | small | ~0 | CC injection test, no jump |
| 8 | Propagation through spatial structures | Euclidean fronts, 2D grids | THE RIFT, RD Pins | along-structure propagation missing | not yet (scene-level first) | scene-specific | ~0 (one lane read) | spec-dependent |
| 9 | Temporary visual memory and decay | grid decay, onset decay, envelopes | THE RIFT, Rainbow World | no named stateful accumulator | **yes (P0-1)** | architectural (M) | negative (fewer chains) | signal = route chain bit-for-bit; seek = play |
| 10 | Persistent particles or trails | `ParticleRenderer`, trails, ribbons | Event Horizon | — | no | scene | particle counts ladder-controlled | baseline |
| 11 | Smooth camera through the environment | splines, baked flight (Rainbow World pass 2 research, doc 21) | Rainbow World | — | no | scene | ~0 | per-frame camera-delta trace (Rainbow World's method) |
| 12 | Many simultaneous modulation routes | route chain, ~30 ns/route | Rainbow World (405 routes) | duplication without P0-1 | P0-1 | architectural | 4,000 routes = 0.13 ms | B2 scaling probe |
| 13 | Stable frame pacing at the target | live pacing, Fifo, ladder | 3 of 4 baseline scenes | — | no | measure on blockout | — | perf P1/P2 |
| 14 | Graceful quality reduction | live ladder, 12 effect fields | THE RIFT | new effects need levers; no drop reason | lever per new effect; drop reason (P0) | small per effect | — | perf P3/P5; drop reason recorded |
| 15 | Clear diagnostics (signals and cost) | `--live-profile`, `--sonic-trace`, `--audit-routes` | — | quantised timing; no live scope; no sound-to-light number | P0 instrument fixes; P1-2 scope; P1-6 latency | architectural (S-M) | none | a developer traces a route and names the costliest pass |
| 16 | Reliable behaviour in silence, abrupt transitions, unusual input | `response.*` gates at -60 dBFS; seek = play | — | `audio.<band>` noise floor; beat octave error; late events unseen by triggers (G12, Unknown) | P1-3; P2-2; probe G12 | small | — | B1b; probe G12 before the spec relies on entity events |

**Net:**
- **Architectural work:** requirements 2, 9 and 15, plus the small pieces of 5, 6, 7 and 14.
- **Scene and art work:** everything else.
- **Unknown until the spec:** requirement 8.

## 10. Prioritised implementation plan

**Effort scale:**
- **S:** ≤ 2 days.
- **M:** ≤ 1 week.
- **L:** ≤ 3 weeks.
- **XL:** more.

All estimates include tests.

### P0: required foundation (about 1.5-2.5 weeks total [est])

| ID | Change | Problem and evidence | Alternatives considered | Benefit | Cost | Effort | Risk | Validation |
|---|---|---|---|---|---|---|---|---|
| P0-1 | **Honest instruments:** disable timestamp quantisation; headless uses the live sky schedule; count procedural shadow draws; live mode reports misses, not jitter; record a **drop reason** | 01 §4.2, §7 C1-C4, §5.3 | keep as is (makes §13's "profile new effects independently" impossible) | every later measurement is trustworthy | none | S-M | low | re-run the 4-scene baseline; pass times no longer on the 65.5 µs grid; headless CPU matches live |
| P0-2 | **Fix `EnvelopeSource`:** take the trigger before stepping; an instant attack shows full level on its frame | 02 §2.4, probe B1; lead-verified (`source.cpp` l. 320-401) | retire it now (two scene users and the trace convention depend on it) | removes a 33 ms hop; correct peaks at any frame rate | none | S | visible change to 2 scenes' `env.*` (no shim, ADR-441) | B1: rise frame = event frame; peak 1.0 at 30 and 60 fps |
| P0-3 | **Conditioned signals** (`signal.*`): a source that runs the existing `ProcessorChain` over inputs, with `firstActive` selection and constant inputs, publishing in the rack's second pass, same frame; joins ADR-1573's replay | 02 §3.3, §12 P0-1; Rainbow World's 405 routes | same-frame macros (changes knob semantics); `target: "signal:x"` routes (scatters definitions); keep generating routes in Python | one frame earlier on every macro-routed effect; about 80 Rainbow World routes become about 10 signals [est]; the scene's "memory" and "coherent composition" are shared accumulators | negative (fewer chains) | M | low (additive) | signal = same chain as a route, bit for bit; seek = play test; port Rainbow World's swell and scan as the acceptance test |

### P1: high value, in parallel with the art specification

| ID | Change | Effort | Risk | Depends | Prerequisite for the Living Prism? |
|---|---|---|---|---|---|
| P1-1 | **Fast impacts:** a register attribute (low/mid/high share of the attack) on `stem.<k>.onset` and `response.onset`, at the onset frame | S | low | — | yes for impacts |
| P1-2 | **Signal scope + complete trace:** a live ImGui plot of any signal or route (input, chain stages, final); trace columns for `audio.*`, `control.*`, `signal.*`, parameter finals; events recorded directly | M | none | P0-3 | yes (§13 authoring) |
| P1-3 | **Sound-to-light measurement:** a loopback click through to present timestamp, per transient family, p50/p95 | M | none | P0-1, P0-2 | yes (proves the latency target) |
| P1-4 | **Screen-space refraction** on procedural materials (reuse ADR-1286's copy), with a ladder field | M | low (gated) | P0-1 | **yes, for crystal** |
| P1-5 | **Material-gated half-resolution SSR**, with a ladder field. Cost probe on the blockout first | M | medium | P0-1, blockout | yes, if the spec has reflective crystal |
| P1-6 | **Spatial upscaler** in place of bilinear; projects can prioritise effects before resolution | S-M | low | — | strongly recommended (helps every scene) |
| P1-7 | **Committed live perf baseline + regression check** (4 scenes, thresholds in §11) | M | low | P0-1 | yes |
| P1-8 | **Phase out mix kick/snare/hat** (owner's request). Class hits only from a drum source; on a mix, scenes use `response.onset` + register, `beat.pulse`, or a `signal.*` choosing. Migrate 276 routes in 41 live projects, owner review per scene family | M code + M-L migration | medium (beat feel changes) | P0-3, P1-1 | no (the Prism simply doesn't author on them) but it is the owner's priority |
| P1-9 | `AudioHistory` from a source (fronts that are "the drums") | M (+ GPU rows if several at once) | medium | — | if the spec has instrument-specific fronts |
| P1-10 | One time base in GUI file playback; output-latency compensation | S-M | medium | — | no (rehearsal accuracy) |
| P1-11 | Gate `audio.<band>` auto-gain at -60 dBFS (owner call: changes scenes on `audio.*`) | S | look change | — | no |
| P1-12 | MIDI soft takeover (pickup mode) | S | low | — | recommended for performance |

### P2: optional

- retire `EnvelopeSource` after P0-3;
- `beat.confidence` from tracker agreement, plus `beat.present`;
- nearest-frame firing for event timelines;
- 1€ smoothing for CC;
- the liveness rule for trend chains;
- probe G12 (late events vs triggers) **before** the spec relies on entity or field events driving state;
- MIDI output, 14-bit and relative encoders;
- one detector for `audio.onsetLow/Mid/High` (owner call);
- noise-gate `sonic.*`;
- resample temporal rings on a scale change (only if the spec uses temporal effects);
- sorted blended procedural objects (only if crystals are seen through crystals);
- a **TAA/TAAU spike** only if the blockout sits below scale 0.7.

### Defer or reject

| Item | Verdict | Why |
|---|---|---|
| Renderer rewrite, other graphics API | reject | no limitation found that incremental work can't solve |
| New modulation, VFX or quality framework | reject | the chain, the fields and the ladder are complete; the gaps are naming, levers and diagnostics |
| More signal names | reject | the vocabulary is complete (02 §9.6) |
| Live source separation, neural detectors | reject | latency; separated sources already score 53/53 |
| **Ableton Link** | **rejected (owner, 2026-10-22)** | also a GPLv2+ SDK against an MIT repository |
| Reflection probes, MetalFX, FSR 2 port, depth peeling, pass reorganisation for bandwidth | reject for this milestone | cost or portability vs benefit |
| Hi-Z occlusion culling, cached shadow cascades, weighted blended OIT | defer | only if the blockout measures the need |
| Graph-propagation engine primitive | defer | scene-level first; promote on a second user |
| Rewriting Astral Forge onto `response.*`; a third particle system; per-stem Sonic Character | defer or reject | works, scene-specific, or covered |

**Order to minimise rework:**
1. P0-1 (instruments) and P0-2 (envelope) in parallel; both are S.
2. P0-3 (`signal.*`).
3. P1-2's trace half and P1-3 (prove latency against the threshold).
4. In parallel with the art specification: P1-4 refraction, P1-6 upscaler, P1-1 register, P1-7 perf baseline.
5. On the blockout: P1-5 SSR (after its cost probe), and the TAA decision.
6. P1-8, the kick/snare/hat phase-out, after P0-3 and P1-1. Then the owner's scene-family review.

## 11. Testing and acceptance plan

**Functional [rec]:**

| Area | Test |
|---|---|
| Signals reach their destinations | `--sonic-trace` + parameter-final columns (P1-2) on a fixture, every route's final moving on its source's frame |
| MIDI control is predictable | injected CC sweeps produce the mapped parameter curve; pickup prevents a jump after a recall |
| Event vs continuous | B1 fixture: events on their frame at full strength (`signal.*`, route envelopes, `env.*` after P0-2); continuous levels within range |
| State, accumulation, decay, reset | `signal.*` seek-equals-play (ADR-1573 pattern); bit-exact replay |
| Silent or missing input | B1b: 80 s of -70 dBFS → every audible-meaning signal < 0.05; no stuck triggers; missing stems fall back to the mix |
| Existing scenes still work | `run-suite.sh gpu` + `cpu-assets` exit 0; `review_examples` on the live collection; `--audit-routes` on all 44 live projects shows no new dead routes |

**Performance:** thresholds proposed for the owner's approval, derived from today's baselines (01 §9).

| # | Criterion | Proposed threshold |
|---|---|---|
| T1 | live auto misses per 900 frames, median of 3 | ≤ 5 |
| T2 | live frame interval p99 | ≤ 40 ms |
| T3 | quality held at the representative windows | High or better ≥ 90% of frames (or Medium, if the owner prefers THE RIFT's reality) |
| T4 | headless Ultra GPU p50 at the heaviest window | ≤ 29.3 ms, and p95 ≤ 1.5x p50 |
| T5 | fixed cost at Emergency | ≤ 14.7 ms (half the budget) |
| T6 | a level change's own cost | no more slow frames after a raise than the base rate |
| T7 | compiles while performing | 0; compiled variants ready ≤ 15 s after load |
| T8 | each new shared effect | own timeline label, ladder field, measured cost at 1080p on the blockout |
| T9 | regression | the 4-scene baseline kept in the repo; worse misses or p99 beyond the reps' spread fails review |
| T10 | **sound to light** for impacts, live, 30 fps | ≤ 45 ms p50 and ≤ 60 ms p95 (owner to approve; P1-3 measures it) |

**Authoring and diagnostics:**
- From the scope, a developer can show a signal arriving, trace a route through its chain to the parameter final, and
  name the costliest pass and the drop reason when frames are missed.

**Integration readiness:**
- The spec arrives and every requirement row in §9 maps to an existing capability or a named P0/P1 item, with no
  unplanned architecture.

## 12. Risks and open questions (decisions needed from the owner)

1. **Latency target:** approve ≤ 45 ms p50 / ≤ 60 ms p95 sound-to-light for impacts?
   - At 30 fps a single frame is 33 ms, so meeting it requires P0-2 and P0-3 (removing hops), authoring impacts on
     broadband onsets (P1-1) and, live, low-latency audio buffers.
   - Today's estimate meets it only for broadband onsets [est].
2. **Performance thresholds T1-T5:** especially T3, High ≥ 90% or Medium.
3. **The kick/snare/hat phase-out (P1-8): option A or B.**
   - **A (recommended by B):** remove all three mix classes, one rule for artists. Snare's 41-67 ms lateness makes it a
     weak live trigger anyway.
   - **B:** remove only the kick, since snare and hat measured decently on Feline (agreement with the drum stem 0.93/0.73
     and 0.85/0.94).
   - Either way it changes the feel of 41 live scenes. Decide after reviewing one migrated scene per family.
4. **Look-changing fixes:**
   - P0-2 (2 scenes' `env.*`);
   - P1-11 (band auto-gain gate, about 24 scenes on `audio.*`);
   - P2 one-detector `audio.onsetLow/Mid/High`.

   No shims, by ADR-441. Accept the change?
5. **Input assumption for the Living Prism.**
   - Will it be performed with separated sources (drum, bass and synth on input channels), or must it work on a plain
     stereo feed?
   - The instrument-aware design and the world-scan-style long behaviour differ by input. The spec must state it.
6. **Show tempo source:** on a stereo feed the beat tracker can lock at half or double tempo. Recommend MIDI clock from
   the playback rig for performances; Ableton Link is rejected.
7. **Unknown G12:** events published late in the frame (`field.*.enter`, `entity.*`) may never reach source or state
   triggers. It must be probed before the spec relies on them.
8. **Digital Mosh Live misses deadlines even at Emergency:** outside this milestone. Open a separate investigation?

## 13. Milestone readiness verdict

**Ready for the art-direction and scene-construction specification after a short, specific prerequisite pass.**
- The architecture needs **no rewrite** and **no new framework**.
- Write the specification now, in parallel with **P0 (about 1.5-2.5 weeks [est])**. P0 makes the measurements honest,
  removes the two one-frame hops and adds named conditioned signals.
- Start **P1-4 refraction** alongside, since crystal is the scene's identity.
- **SSR, the TAA decision and propagation along structures** wait on the specification and its blockout.

**The specification should state** (these decide the open rendering and signal choices):
- the share of the frame that is translucent, reflective and particulate;
- whether crystals are seen through crystals;
- whether temporal effects (echo, feedback) are used;
- whether events propagate *along* structures or radially;
- the performance input (stems/channels vs stereo, and the tempo source);
- the representative windows (heaviest and typical) for the perf gates.

## Appendix

**Evidence documents:**
- `01-rendering-audit.md`: pass list, matrix, baselines, ladder tests, research R1-R5, ranked list, perf criteria,
  files and benchmark commands (appendices A-C).
- `02-audio-modulation-audit.md`: integration map, signal table, detectors, latency budget, four end-to-end traces,
  research 9.1-9.6, gap list G1-G18, kick/snare/hat phase-out (§15), probes.

**Probes:**
- **A1** (`probes/A1-rendering-profiles/`): baseline, ladder and A/B scripts. Raw data in
  `~/Desktop/av-gen-review/80-living-prism-audit/A-data/`.
- **B1-B4** (`probes/B1-…`, `B2-…`, `B3-…`, `B4-…`): transient timing, route scaling, Rainbow World traces and the hit
  phase-out. Fixtures in the scratchpad only.
- **Lead:** the timeline-lag probe (coordinator scratchpad `lag/`). Three tiny projects (value step to a post
  parameter, value step to a material constant, event to a route envelope), rendered offline at 0.8-1.3 s.

**Temporary artifacts to clean up after review:**
- `build/render`, `build/audio` and the `build/release` symlink in this worktree;
- the generated Rainbow World and THE RIFT scenes (gitignored, regenerable).

## 14. Owner decisions (2026-10-22, verbatim answers to §12)

| # | Question | Owner's answer | Effect on the plan |
|---|---|---|---|
| 1 | Latency target | "best recommendation" | **adopted:** sound to light for impacts ≤ 45 ms p50, ≤ 60 ms p95, live, 30 fps (T10) |
| 2 | Performance thresholds | "accept medium" | **T3 = Medium or better ≥ 90% of frames** at the representative windows; T1, T2, T4-T9 as proposed |
| 3 | Kick/snare/hat phase-out | Option A, "THIS" | **P1-8 option A:** remove all three mix classes; class hits only from a drum source; one migrated scene per family shown to the owner before merge |
| 4 | Look-changing fixes | "YES" | P0-2 (`EnvelopeSource`) and P1-11 (band auto-gain gate) proceed without shims (ADR-441) |
| 5 | Living Prism input | "SEPERATE STEMS / CHANNELS" | the scene is designed for separated sources; feeds and per-source signals are primary; the mix is only a fallback |
| 6 | Show tempo source | "AGREE" | MIDI clock from the playback rig for performances (the beat tracker remains the fallback) |
| 7 | Digital Mosh Live misses | "yes" | a separate investigation ticket, outside this milestone |

The owner is writing the art-direction and scene-construction specification in parallel.
