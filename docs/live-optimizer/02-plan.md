# Live optimizer: implementation plan (the coordinator, 2026-10-03)

**The principle (the owner's):** "maximum perceptual visual quality per millisecond". Profiling, optimization, scalability and runtime adaptation stay separate pieces behind one performance API.

**Base:** `live/optimizer` = `live/quality` (ADRs 1080-1089). Phase 4 is mostly delivered there. **ADRs for this stream: 1090-1109.** Read `01-research.md` first; extend what exists and never build a parallel system.

**Stages, in order.** Each ends in a commit, a pushed branch, measurements, and a PROGRESS entry. Phase 5 is out of scope for this agent except where noted.

## Stage 1: `avgen --live-profile`, the measurement tool (Phase 1, MUST)

### 1a. The command
- **A GPU command on the headless-bench path,** with two modes:
  - `--mode headless`: the fixed-step clock, deterministic, fast. For agents iterating on a scene.
  - `--mode live`: the real windowed editor loop with present and Fifo, optionally projecting to an output of a given size (the ADR-1088 projection path). This is what predicts a touring frame: deadline misses, the swapchain waits and the output copy.
  - Both share one record builder.
- **Arguments:**
  - `--project`, `--target-fps`, `--size WxH` (the output size), `--start s`;
  - `--warmup s` and `--measure s` (fast defaults 3 + 5, `--deep` 5 + 20);
  - `--quality <level|auto|tier>`, `--camera`, `--no-audio`, `--midi`, `--capture <png>`;
  - `--json <f>` and `--text`.

### 1b. Warm-up and pre-warm
- **Measurement order:** load, then upload assets, then pre-warm, then warm-up frames until steady state, then measure.
  - **Steady state:** a rolling median that stops moving, with a time cap.
- **Report the cold costs separately:** the load time, the pre-warm time and the first-frame time.

### 1c. Frame statistics against the budget
- The existing `Distribution`, plus: the budget, frames over budget, percent under budget, deadline misses in vsync terms (live mode), and the worst N frames with timestamps.
- A **critical-path verdict:** GPU, CPU or sync/present, from the measured shares.

### 1d. CPU breakdown
- From `PhaseProfiler` (live) and `CpuFrameBreakdown` (headless).
- Each category is labelled `measured` or `coarse`. **Don't invent categories:** the brief lists audio analysis, signal bus, timeline and others; map them to the phases that really exist, and say which are not separately measurable.

### 1e. GPU breakdown
- Medians per timeline label, plus the brief's **categories through one mapping table:** label → {geometry/opaque, shadows, lighting/clusters, SDF, particles, volumetrics, post FX (split into bloom, DoF, motion blur, other), temporal, composite/tonemap, other}. Unmapped labels go to "other". The table is unit-tested.
- **State the limits in the report:** frame overlap (ADR-1085), no timestamp on Metal for an empty pass, and the UI and output copy untimed.

### 1f. Resource statistics
- Everything `BenchmarkCounters` has, plus new:
  - **byte accounting** in the gpu layer: textures (with the largest N), render targets, buffers. Count at creation and destruction, so the cost stays off the frame;
  - **distinct pipeline and shader-variant counts,** including SDF variants;
  - **shadow casters by light, and shadow passes;**
  - **particles:** active against capacity, and simulation steps;
  - **post passes with their sizes.**

### 1g. Conditions
- Add: host model, CPU, OS version, display refresh, window or fullscreen, the output size, audio and MIDI state, the project's seed, and the live quality level and strategy.
- The report says plainly that results are specific to this hardware.

### 1h. Optimization candidates
- **A rule table** mapping measured costs to EXISTING levers (quality arms, the `--disable` arms, the new Stage 2 levers). Each candidate gives the cost, the suggested change, an **estimated** saving (from the fitted per-pass scaling laws; labelled estimated) and a visual risk class.
- **`--verify-candidates`** runs the top N through the existing `--ab` machinery and reports **measured** savings with noise floors. Estimated and measured savings are never mixed.

### 1i. Output
- JSON schema `avgen.liveprofile/1` (with a definitions block, like `avgen.benchmark/1`) and the brief's human text report.
- **A thin `tools/live_scene_profile.py` wrapper** for agents: run the command, print the text, and return the JSON path.

### 1j. Agent tool
- An AI tool `performance.profile_scene`. It runs the command as a subprocess off the main thread, the way `director.evaluate` runs the critic, and returns the summary.
- A small `performance.get_live_quality` / `set_live_quality` pair over the ADR-1080/1083 settings.

**Check the tool on the three reference scenes:**
- `examples/sonic-garden/sonic-live.json`
- `examples/world/glowmere-valley-2.json`
- `examples/liminal/all-you-got.json` from 60 s

Do it in both modes. Its live-mode numbers must agree, within noise, with `../av-gen-live`'s matrix.

## Stage 2: the missing scalability levers (Phase 2, MUST)

Add only what's missing, as `QualitySettings` fields. Each lever is LIVE-capable, off in Offline (`assertOfflineIsUncompromised`), measured, and has an A/B arm.
1. **`lodBias`:** scales the screen-error and distance thresholds for entity mesh LOD and procedural `LodSettings`.
2. **Draw-distance multiplier:** on procedural `maxDistance` and entity and rig cull distances, through `DetailLimits`.
3. **Shadow-caster limits:** a projected-size and/or distance floor for non-hero casters.
4. **Post-FX resolution:** a half-resolution option for DoF, motion blur and bloom (the pyramid's start), with a quality field per effect group.
5. **Particle quality:** spawn scale (exists), plus distance culling of emitters and an optional simulation substep or rate.
6. **Hero wiring:** set `ImportanceInput::hero` from `Composition::heroes()` / starred nodes. Heroes are exempt from levers 1-5 (`heroFloor`).
7. **Optional per-node `importance`** (hero, foreground, normal, background, ambient), defaulting to inferred values: heroes are hero, everything else normal. Only levers 1-5 read it.

**Profiles:** the brief's QUALITY / BALANCED / PERFORMANCE as named, explicit `QualitySettings` overlays.
- **Reconcile with the ADR-1083 ladder; don't duplicate it.** The ladder's rungs and the profiles must be one table family, and a profile is a ceiling the controller works under.
- **The bar:** the same scene runs at each profile with measured, predictable differences. Measure all three reference scenes.

## Stage 3: the Live Performance panel (Phase 3, SHOULD)

**Extend the existing Performance panel and the Live panel; don't add a third panel.**
- **Graphs:** a budget line on the frame and GPU graphs (5-30 s of history; ImPlot is linked), spike and deadline-miss markers, and a CPU / GPU / FRAME toggle.
- **GPU breakdown:** grouped into Stage 1's categories, rolling medians, and headroom.
- **Quality:** a section showing every lever (Stage 2), the level and the strategy.
- **Resource inspector:** expandable lines for shadows (per light, size and casters), particles, post (with sizes) and memory.
- **An OPTIMIZE button** opens a review of Stage 1's candidates.
  - Tick boxes, then Apply selected / Cancel.
  - **Applying is undoable and non-destructive:** it writes the project's `"live"` block or quality overrides, never the scene geometry.
- **Save live profile:** `"live": {targetFps, profile, qualityStrategy, minimumLevel}` in the project, extending ADR-1084's block.
- **The owner's UI rule:** anything visible must be controllable, named in plain words. Ask the coordinator for screenshots: agents can't see ImGui.

## Stage 4: the Phase 4 gaps (SHOULD)

1. **Pre-warm (critical for touring):** compile every SDF variant and pipeline the project can reach before playback starts (at load, behind the loading indicator), so no compile happens during a performance.
   - Where reachability isn't knowable, compile on a background job and fall back to the interpreted path until it's ready, instead of blocking the main thread for 3.9 s.
   - Measure Liminal before and after.
2. **A per-project minimum level** (`live.minimumLevel`) and a **"LIVE TARGET UNSUSTAINABLE"** status, shown in the Live panel when the minimum is reached and still over budget.
3. **Recovery:** after a raise, if the next window misses the budget, revert and lengthen that step's hold.
4. **Degradation priority:** the strategy may name an ordered list of lever groups, still data-driven (extend ADR-1084).
5. **The owner's open tuning decision on a 60 target** (the raise margin, or a frame cap): **wait for the coordinator.** Don't decide it.

## Stage 5 (Phase 5): only the groundwork, if time allows

- **Data only:** per-entity projected area, distance and hero flag, as a section of the profile JSON, so a later Phase 5 can rank contributors.
- **Not in scope:** the search, the A/B critic loop, and automatic application.

## Out of scope

- multi-GPU or distributed rendering;
- temporal upscaling;
- shader algorithm rewrites;
- a renderer redesign;
- making the editor depend on the optimizer, or the optimizer depend on the critic.

## Definition of done for this agent

1. `avgen --live-profile` produces the brief's report (text and JSON) on the three reference scenes in both modes, with measured candidate savings under `--verify-candidates`.
2. The Stage 2 levers and profiles work, with measured differences.
3. The panel extensions and Optimize review are in, pending the owner's visual check.
4. Pre-warm removes Liminal's mid-run compile.
5. ADRs, tests that don't depend on commercial assets, and both full suites exiting 0, run at the end under the lock.
6. A report at `~/Desktop/av-gen-review/31-live-optimizer/REPORT.md`, or in the final message if the harness refuses the file.
