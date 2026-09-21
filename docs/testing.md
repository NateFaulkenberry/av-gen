# Testing

Strategy: ADR-009. Tests are Catch2 v3, discovered into CTest with labels `unit` (GPU-free,
`avgen_tests`) and `gpu` (`avgen_render_tests`, skips itself when no adapter is available).

```sh
ctest --preset release               # everything -- the configuration to verify a change in
ctest --preset debug                 # everything, with assertions and a debugger
ctest --preset debug -L unit         # GPU-free only
ctest --preset debug -L gpu          # rendering tests
cmake --preset asan && cmake --build --preset asan && ctest --preset asan   # ASan + UBSan
cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan   # ThreadSanitizer
./build/debug/src/avgen --audio track.wav --play --frames 900 --stress 7    # random UI-like actions
./build/debug/tests/avgen_tests "[analysis]"   # Catch2 tag filter
./build/debug/tests/avgen_tests "[audio][device]"  # tests needing an output device (SKIP if none)
```

## Which build to run

**Verify a change against `--preset release`.** Debug is for stepping through a failure, not for
deciding whether the suite is green.

Both configurations pass. The difference is what the `[performance]`-tagged tests can measure: a
wall-clock ceiling means nothing without the optimiser, and those are checked only in an optimised
build (see `kOptimised` in `tests/integration/test_world_navigation.cpp`, which records the measured
gap -- the same navigation grid builds in 161 ms release and 5,080 ms debug, 31x apart). A ceiling
loose enough to hold in both would catch nothing.

What those tests assert unconditionally is the half that does not vary: cells expanded per route,
routes found, object counts. Those are properties of the algorithm rather than of the code
generator, they are identical to the unit in both builds, and they are the stronger check -- "the
search stopped being bounded" is caught exactly by a count, and only through a proxy by a
millisecond.

Catch2 tags are not CTest labels here: only `unit` and `gpu` are registered as labels
(`catch_discover_tests` in `tests/CMakeLists.txt`), so `ctest -L performance` selects nothing. Reach
a tag through the test binary instead: `./build/release/tests/avgen_tests "[performance]"`.

## What is covered (milestone 0.1)

| Area | File | Highlights |
|---|---|---|
| core | `tests/unit/test_core.cpp` | fixed-step and realtime clocks, seek; PCG32 reference values; SPSC ring incl. concurrent producer/consumer; triple buffer |
| headers | `tests/unit/test_headers.cpp` | every public header compiles standalone; `Parameter<T>` for all supported types |
| audio | `test_audio_file.cpp`, `test_analysis_stream.cpp`, `test_audio_player.cpp` | WAV round trip via miniaudio, mono downmix, zero-padding, missing/invalid files, odd sample rates; discontinuity markers are exact under concurrency; device playback: position advance/pause/seek/end-of-file/restart (skipped without a device) |
| analysis | `test_fft.cpp`, `test_analyzer.cpp`, `test_analysis_track.cpp`, `test_analysis_runner.cpp` | 440 Hz peak bin and magnitude scale, Parseval, silence; band dominance for 60 Hz/440 Hz, RMS of a sine, white-noise centroid, impulse-train and click-track onsets, chunking invariance, bit-identical determinism, frame stamping; offline track lookup; runner thread |
| signals/params | `test_signal_bus.cpp`, `test_parameters.cpp`, `test_processor.cpp`, `test_modulation.cpp`, `test_serialization.cpp` | defaults, clamping, soft ranges, duplicates; every chain stage, time-constant accuracy, frame-rate independence, envelopes; route binding errors, all ops, priority, component targeting, master gain, reset; JSON round trips, version/format validation, unknown paths, malformed files |
| scene | `test_scene.cpp`, `test_scene_model.cpp` | mesh generator validity and counts, normals, TRS order and matrix decomposition, projection depth range 0..1, bounds, textures/lights/clear, OrbScene registration/routes/determinism |
| assets | `test_image.cpp`, `test_gltf_loader.cpp` | PNG/HDR round trips, sRGB tagging, error paths; in-memory GLB fixture: hierarchy transforms, vertex data, material factors and texture refs, embedded PNG decode, punctual lights, cameras, bounds, failure leaves the scene untouched, id offsets on repeated loads; Khronos samples when `AVGEN_SAMPLE_ASSETS` is set |
| beat tracking | `test_beat_tracker.cpp` | tempo estimation on synthetic envelopes (120/90/160 BPM, flat input), offline Ellis tracker accuracy on a click track, live tracker convergence, tempo-change following, determinism, reset; AnalysisTrack stamping |
| sources/presets | `test_sources.cpp`, `test_presets.cpp` | LFO shapes and seek exactness, beat sync, ADSR timing, noise determinism/continuity, random sample-and-hold sequences, timeline interpolation and looping, macros, rack JSON round trips, modulators of modulators; preset capture/apply/blend, bank JSON |
| timeline | `test_timeline.cpp`, `tests/integration/test_timeline_engine.cpp` | key insertion and sorting, every interpolation, looping, vector and single-component tracks, apply modes write finals not base, bind/unbind and unknown targets, recordKey, cues and morph progress, JSON round trip and malformed input, determinism; engine: offline keyed values at exact times, routes add on top of automation, beat-based loop follows the click track, cue recalls a preset and morphs, seek re-syncs cues, project v3 round trip, scene swap rebinds tracks |
| project system | `tests/integration/test_project_system.cpp`, migration cases in `test_serialization.cpp`, `test_recent_files.cpp` | relative asset references and full session restore after moving the folder, missing assets as warnings with parameters still applied, composition projects by path or inline, bundle export reopens after deleting the originals, new project resets; v1/v2 documents migrate step by step, too-new rejected, caller's document untouched; recent list order/dedupe/limit/persist/prune |
| offline rendering | `test_render_settings.cpp`, `test_exr.cpp`, `tests/rendering/test_render_job.cpp`, `test_video_writer.cpp` | range/frame-count resolution, pattern and JSON validation, the EXR kind and its default pattern; EXR half/float round trips and error paths; GPU: an exact PNG sequence at the requested size, bit-identical sequence hash on a fresh engine + renderer, bounded stepping and cancellation with partial output, bad settings rejected, video with audio when a backend exists, the readback ring's per-frame hashes equal the synchronous `renderToImage` path over 20 frames, an EXR sequence (readable, brighter than 1.0 in linear light, equal to the sync float readback, byte-identical on re-render); native/ffmpeg video writers probed after writing |
| live control | `test_osc.cpp`, `test_midi.cpp`, `test_control_map.cpp`, `test_audio_input.cpp`, `tests/integration/test_control_engine.cpp` | OSC encode/decode for every type, bundles, truncation fuzz, pattern table, UDP loopback; MIDI byte parser (running status, real-time interleave, sysex), inbox, CoreMIDI virtual source end to end; binding matches, direct scheme, map JSON; capture device (skipped without one); engine: injected MIDI drives routes and parameters, direct OSC sets parameters/signals/presets, real UDP into the hub, project round trip and scene swaps |
| outputs and sharing | `test_output_mapping.cpp`, `tests/rendering/test_output_mapper_gpu.cpp`, `test_window_smoke.cpp`, `tests/rendering/test_texture_share.cpp` | homography/blend maths and JSON; GPU mapper identity/crop/flip/blend/warp with 0 GPU errors; display enumeration; Syphon self-receive through a real client (pixels, stats, burst + resize), NDI skipped without the runtime |
| control follow-ups | `test_midi_clock.cpp`, `test_hash.cpp`, extended control/composition/project tests | MIDI clock tempo with jitter and dropped ticks, engine tempo source; OSC query/feedback over UDP; node parenting; SHA-256 vectors and relinking moved assets |
| procedural geometry | `test_procedural.cpp`, `test_procedural_params.cpp`, `tests/rendering/test_procedural_gpu.cpp`, `tests/integration/test_procedural_engine.cpp` | primitive counts/normals/winding/determinism; every distribution incl. radial orientation modes and spiral end points; hashed variation (same seed same world); transform order; each deformer against known points; stack order; CPU/GPU noise identity; rebuild dirtiness; JSON round trips; parameter registration and apply; GPU: instance coverage, deformer effects, determinism across renderers, 4096 instances with 0 GPU errors, fog; engine: audio route → procedural parameter → rendered frame, offline hash equality, project round trip; benchmark presets |
| ui logic | `test_ui_logic.cpp` | regression: route slider bounds independent of the value (0.2 crash) |
| stress | `test_engine_stress.cpp` (`[device][stress]`) | rapid seeks/param/route/volume/transport edits during live playback; run under ASan and TSan |
| modulation integration | `tests/integration/test_modulation_sources.cpp` | LFO drives the orb without audio, seek exactness, modulators of modulators, project round trip through the engine (sources, routes, presets, values, morph), beat clock from a click track |
| shader format | `test_shader_format.cpp` | header parsing (all input types, passes, defaults, errors), WGSL layout offsets, packing, generated module structure, size expressions, example shaders stay valid |
| shader layers | `tests/integration/test_shader_layers_set.cpp`, `tests/rendering/test_shader_layers.cpp` | parameter registration/packing/reload/remove, project round trip and scene swap; GPU: background colour from an input, post inversion, persistent feedback accumulation, broken shader → error pattern + diagnostics → fixed on reload, engine shader reload |
| particles | `test_particles.cpp`, `tests/rendering/test_particles_gpu.cpp` | parameter registration and rest-relative application; GPU: emission brightens the frame, particles die after their lifetime, disabled systems are skipped, bursts flash immediately and are clamped to capacity; hidden `[.perf]` one-million-particle probe |
| post-processing | `test_post_settings.cpp`, `tests/rendering/test_post_gpu.cpp` | 38 `post/*` parameters register/apply; GPU: bloom spreads light beyond a bright object, zero saturation yields grey, vignette darkens corners, all five tone operators stay in range and differ, DoF softens an out-of-focus edge, camera motion blur smears edges, transient pool reuse and ageing |
| image formation (ADR-037/039) | `tests/unit/test_camera.cpp`, `tests/rendering/test_image_formation_gpu.cpp` | EV100 against hand-computed values, one stop per stop, metering that seeds from the manual EV, converges, respects the EV clamps and reproduces bit-identically over two runs, the analytic circle of confusion and focal-length-to-field-of-view, focus tracking (fixed/point/focal point, rate limit), 20 `camera/*` parameters; GPU: all five tone operators against a CPU mirror at three known inputs, AgX flatter than ACES and keeping the blue channel, manual stops and compensation applied *before* the bloom threshold, automatic metering bit-identical across two runs, bloom energy conservation (1 + intensity, independent of level count), halation red-weighted and anamorphic streaks horizontal, the whole chain deterministic, and the Hyperspace core frame no longer saturated; hidden `[.perf]` 1080p/4K probe (`docs/performance/image-formation.md`) |
| file watcher | `test_file_watcher.cpp` | modified/removed/recreated files, poll interval |
| asset registry | `test_asset_registry.cpp` | path resolve/relativise against a base directory, scene and image caching (same object twice), errors not cached, reload bumps the version and yields a new object, per-sRGB image keys, loaded path listing, clear |
| composition | `test_composition.cpp`, `tests/integration/test_composition_engine.cpp` | nodes of every kind flatten correctly (shared meshes per glTF asset, entities per instance), unique names, node transform/visibility/emissive parameters, root transform, particle node parameters, nested scene parameter prefixes, remove unregisters, JSON/file round trips, nested scene files, self-inclusion and depth errors, malformed JSON, missing asset skipped with a warning, determinism, detach/reattach; engine: `.json` routing by `format`, composition swap keeps audio driving, save then reload |
| scene controllers | `tests/integration/test_gltf_scene.cpp` | GltfScene import + parameter surface + root transform maths; engine scene swap keeps audio driving the new surface; failed loads keep the old scene |
| integration | `tests/integration/test_pipeline.cpp` | synthetic audio → Engine (offline) → scene: bass raises scale, treble raises emissive, RMS raises brightness, onsets pulse impulse, bit-identical across runs, rotation integrates modulated speed, error path |
| texture sharing | `tests/rendering/test_texture_share.cpp` (+ `tests/support/syphon_test_client.*`) | describe/closed-publish rejection; GPU (macOS): Syphon server publishes a solid colour then a gradient from RGBA8 and BGRA8 sources, an in-process `SyphonMetalClient` discovered by name receives both and pixels match within 2/255, stats count frames, close/reopen with a second client, 30-frame burst followed by a size change lands the new frame; NDI: 30-frame burst when the runtime is installed, else SKIP; hidden `[.perf]` publish-cost probe at 1080p |
| procedural geometry (GPU) | `tests/rendering/test_procedural_gpu.cpp` | instances from local `InstanceRecord` grids: pixel coverage grows with 1/8/32/128 instances, frame hashes stable, stats (objects, instances, logical triangles, uploads); world twist amount 0 and a disabled deformer render bit-identically to no deformer, amount 1 changes the hash and moves the silhouette; all five deformer kinds in one stack are deterministic across renderers and contexts and differ between t = 1 and t = 2; 4096 and 5000 instances with 0 GPU errors, re-upload only on `structureVersion`/growth; distance fog pulls a far cube towards the fog colour and density 0 is a no-op; hidden `[.perf]` 1k/10k-instance probe (`docs/performance/procedural-geometry.md`) |
| volumetrics and simulation (ADR-032) | `test_grid_field.cpp`, `tests/rendering/test_volume_gpu.cpp`, `tests/rendering/test_simulation_gpu.cpp` | grid JSON round trip (settings only, never the cell values), validation (resolution, bounds, table budget, sub-step settings), trilinear sampling at cell centres and between them with both wrap rules, a `FieldKind::Grid` field read through `spatial::sample*`, the reference step (injection then advection downwind, diffusion conserving mass, dissipation, Gray-Scott in range); GPU volume: fog off encodes no pass and two fresh renderers hash equal, fog darkens a 40-unit box far more than a 4-unit one, a density field concentrates it, height falloff makes a monotone vertical gradient, identical frames from two renderers and different frames per frame index; GPU simulation: injection + advection within 1e-3 of the CPU reference, diffusion conserves mass within 1%, reaction-diffusion stays in range and non-uniform, two runs give bit-identical buffers; hidden `[.perf]` probes at 1080p (32/64 steps) and for 32^3/64^3 grids |
| rendering (GPU) | `tests/rendering/test_gpu.cpp` | headless context; shader errors with file/line; clear + readback exact; lit cube renders deterministically (hash equal), differs when the scene changes; resize; invalid meshes skipped without GPU errors; image-based lighting from a synthetic sky brightens and tints a rough white cube, skybox shows the sky, deterministic |

Synthetic signals live in `tests/support/synth.hpp` (sine, silence, seeded noise, impulse train,
click track). Test WAV fixtures are generated at test time into the temp directory; no real
recordings are needed.

## Seventeen ways a green suite has lied

Every one of these has happened on this project, most of them on 2026-09-19/20 when several agents
were building concurrently. They divide into **three** families, and the third is the one to read if
you are short of time, because it is the only one the exit code cannot save you from.

- **Family A — the run did not happen as you think** (entries 1-3, 12).
- **Family B — the run happened and you read it wrong** (entries 4-8, 11).
- **Family C — the scan, the filter or the control was looking where the effect could not reach**
  (entries 13-17; 9 and 10 are its older members, from before it had a name).

Families A and B are failures of *reporting*: the run lies about itself, and **the binary's exit
code catches every one of them.** Family C is a failure of *aim*: the run is honest, the exit code
is 0 and correct, and the thing you measured is not the thing you meant. **No exit code catches
those.** They pass every check you would think to run, which is why two of them arrived on the same
night from two agents who never spoke to each other.

### The run did not happen as you think

1. **Stale binary.** The test glob is *configure-time*, so an incremental build after a merge
   silently omits test files the merge added and the suite passes without ever compiling them.
   Always `cmake -S . -B build/release` after a merge.
2. **Stale object.** Worse, and the binary-level guard misses it. A merge wrote a source in the same
   second the compiler read it, the timestamp comparison tied, and one `.o` was never rebuilt — so
   the binary was *newer than every source* and still contained an old compiled test. It ran against
   new code and **manufactured three plausible failures**, with full `with expansion:` output,
   pointing at a line that is a closing brace, for a test name that no longer exists in the file.
   The tell is that the failure text does not match source you can read. `git clean` the test object
   directory after a merge is cheaper than the check.
3. **A log written by somebody else's process.** See the next section; this is the severe one.

### The run happened and you read it wrong

4. **`FAILED:` with no `with expansion:`** — a killed process, not a failure. `REQUIRE(a == b)` over
   a multi-megabyte buffer kills Catch2 inside the assertion handler (ADR-362). Use `byteDiff`.
5. **A truthful summary above a non-zero exit code.** SIGABRT *after* the summary prints is the trap:
   the summary honestly describes the assertions that ran and the process still died. "Read the
   summary" and "read the exit code" are not redundant — one describes the run, the other the
   process. Capture `$?` explicitly; a pipeline's exit code is the last command's, usually `grep`.
6. **Your own `kill` appears as a named failure.** A SIGTERM you issued yourself shows up as a
   `FAILED:` entry with a plausible test name.
7. **A genuinely flaky test.** `tests/integration/test_ai_control_plane.cpp` races under machine
   load: its wait loop exits on `finished()` and *then* cancels, so a loaded machine completes the
   task in the gap and the state comes back `Completed` where the test wants `Cancelled`.
8. **A `[!shouldfail]` test prints a full `FAILED:` block, with `with expansion:`, on every healthy
   run.** `test_character_lab_slopes.cpp:187` asserts an invariant the engine does not satisfy — the
   file says so — and is tagged so Catch2 expects it. In isolation it reports `1 failed as expected`
   and **exits 0**. This is the "1 failed as expected" in every summary here, and it looks exactly
   like a real failure if you are reading `FAILED:` blocks rather than the summary and exit code.

### The run never started, because of what the test is called

9. **A test whose NAME begins with `--` fails on every `ctest` run and passes by every other
   route.** `catch_discover_tests` registers each case with CTest by passing its name as an
   *argument* to the test binary, and Catch2 parses a leading `--` as a flag. `TEST_CASE("--aov
   shadow refuses ...")` therefore produced, on every single `ctest --preset release`:

   ```
   1/3028 Test #1: unit.--aov shadow refuses the configurations where it would be a constant ***Failed
   Error(s) in input:
     Unrecognised token: --aov
   ```

   while `avgen_tests "*aov shadow refuses*"` passed it with 10 assertions and **exit 0**. One red
   line is enough to stop the whole suite exiting 0, so for as long as it was there, *nobody could
   read `ctest`'s exit code on main and learn anything*. It cost one agent a bisect to establish
   that two unrelated failures beside it were pre-existing, because the exit code could not
   distinguish. Fixed by renaming the case; the flag it is about now sits in parentheses at the
   end. **Do not start a `TEST_CASE` name with a dash.**

10. **A test that crashes a LATER test, and is named as the later one.** The suite reported a bus
   error in `gpu.an area light with no authored range reaches as far as one that has it`. Nothing
   was wrong with that test: the crash was in the hidden `[.perf]` million-particle probe several
   cases earlier, and its cause was a *third* test that built two `SceneRenderer`s where one would
   do — each allocates a shadow atlas, AO targets, a volume grid and a temporal ring, and the file
   already built five. The million-particle pool then had nowhere to go. The tell is that the named
   test passes alone, passes with the suspect beside it, and only dies in the full set; the method
   is to bisect the *filter*, not to read the named test. Cumulative GPU memory makes the victim and
   the culprit different tests, and ctest names the victim.

11. **A crash prints NO verdict line at all, so a failure grep reports success.** Distinct from 5,
   and the distinction is the whole point: there the summary printed and was honest. Here the
   process dies before Catch2 writes anything, so the log contains **no `test cases:` line, no
   `All tests passed`, and no `FAILED:`** — only `tools/gpu-lock.sh: line 39: 88724 Bus error: 10`
   from the shell. A guard script that counts `FAILED:` blocks and reads the summary therefore
   reports a clean run twice over: both counters are legitimately zero *because nothing ran to
   completion*. That is what happened on 2026-09-20 to the full `avgen_render_tests` run — an
   `=== FAILED ===` section that printed nothing, two zeroed counters, and **exit 138**, with the
   exit code the only dissenting signal. Absence of a verdict is not a pass. If neither
   `test cases:` nor `All tests passed` appears, the run did not finish, whatever else the log says.

12. **A task reported dead whose process is still alive, still holding the GPU lock.** This one
   belongs to the first family — *the run did not happen as you think* — and it is the nastiest
   here because **it is invisible from both ends at once**. A task harness killed the wrapper it
   had launched; `tools/gpu-lock.sh` and the test binary underneath it survived the signal and
   kept running for about five minutes. To the agent that killed it, the task had finished and
   failed, exit 144. To the agent waiting on the lock, the holder was active and healthy:
   `kill -0` succeeded, so the stale-lock reclaim in `gpu-lock.sh` **correctly declined to steal
   it**. Both agents were right about everything they could see, and the GPU sat idle inside a
   held lock.

   The trap is that `trap ... EXIT INT TERM` only fires for the process that installed it, so
   signalling the wrapper does not clean up the child. **The tell is a task reported dead whose
   PID still answers `ps`.** After any killed or failed GPU task, before assuming you released
   anything: `pgrep -fl avgen_render_tests` and read the lock's own `pid` file.

### The scan, the filter or the control was looking where the effect could not reach

13. **Command-line matching counts the watchers as workers.** `pgrep -f "avgen_render_tests"`
   returned **8 matches, every one of them `/bin/zsh`** — shells whose command lines merely quoted
   the binary's name, including the pollers that were checking whether the machine was busy. The
   machine was idle. `pgrep -f` matches the whole command line, so any script that *mentions* a
   binary counts as running it, and a loop that checks for contention is itself the thing it finds.
   **`ps -Ao pid,comm` shows the actual executable** and is the only honest check. This was nearly
   reported as GPU contention, and separately caused one agent to misread its own output.

14. **A Catch2 spec containing a comma silently excludes nothing, and says nothing.** Excluding a
   case by name:

   ```
   '~a star field is sparse, and its cells do not show'   ->  394 matching test cases
   ```

   — the full suite, no exclusion, no error. **The comma is a spec separator**, so that parses as
   `~a star field is sparse` *plus* ` and its cells do not show`, and the tail is an **inclusion**
   pattern that matches everything else. An attempt to exclude three cases returned 392 where 391
   was wanted: two names took, the comma'd one nullified itself, and the count was quietly wrong in
   the direction that still looks controlled. **Prefer tags to names** (`~[cosmic]` gave exactly
   391) and **verify with `--list-tests` before spending GPU time on a filtered run.**

15. **A `git grep` census that does not understand its own input.** A word-level scan for `"vortex"`
   returned **eight files**; the answer was **two**. Six of them carry `spatial::FieldKind::Vortex`,
   a vector field that drives particles, which shares an English word with the atmospheric effect
   and nothing else. Acting on the eight — "strip the vortex block from these files" — would have
   deleted motion fields from six scenes **and looked like a clean edit.** Parse the structure, do
   not grep the noun; the fix was twenty lines of `json.load` walking `atmosphericEffects`.

16. **A teeth-check that passes because a *second* mechanism masked the fault.**

   `AVGEN_HEIGHT_CENSUS`/ADR-483. A cache entry was guarded against a rebuilt map by two independent
   mechanisms: the map's identity was in the entry's **tag**, and it was also folded into the
   **hash index**. The stale-map test was checked for teeth by removing the identity from the tag —
   and the test **passed**, and was one sentence from being reported as verified. It passed because
   the index still sent a rebuilt map's coordinate to a different slot, so the stale entry was never
   consulted. Only removing the identity from the index *as well* produced the failure: 174 stale
   heights of 4,225, exit 42.

   This is worse than "a probe that cannot fail proves nothing" (ADR-182), and less obvious: **the
   probe can fail, and does fail, but not for the reason you are testing.** It certifies a mechanism
   that is not the one carrying the safety. A later reader who removes the index hashing as a
   simplification, trusting the tag, gets a green suite and a cache that returns confidently wrong
   values.

   **Defeat every mechanism that could mask the fault, together, not one at a time. If you cannot
   name all of them, you do not yet know which one your test is checking.**

17. **A reachability control that reports a dead knob, because it sampled where the knob does
   nothing.** A per-field probe reported `cloudWidth` "moved 0 of 80 samples" and looked exactly
   like the silently-dead control ADR-460's parity work exists to catch. The knob was live. The
   cloud's interior is a **plateau**, and the sample spread had been built from the *funnel's*
   radius while the cloud is four times wider — so every sample sat inside the plateau, and
   **widening a plateau moves no point already on it.**

   The general form, and it is the same defect as 16 from the other side: a control that cannot move
   at all is caught by ADR-182, but **a control that moves, fails on demand, and is nonetheless
   aimed at the wrong region or the wrong mechanism passes every check you would think to run.**
   When a reachability probe reports zero, suspect the sample domain before the knob.

**So `grep -c FAILED` is not a failure count, and neither is its absence.** Two of the cases above
put a well-formed `FAILED:` block into a perfectly healthy log, and one puts *nothing at all* into a
log of a process that died. Read the **exit code first, the summary second, and `FAILED:` blocks
only as a pointer to what to go and look at** — not the other way round.

**The exit code is the only check that catches every entry in families A and B.** Every other
signal there — the summary, the `FAILED:` blocks, the assertion counts — is a convenience that some
entry above defeats. Capture `$?` from the binary itself: a pipeline's exit code is the last
command's, which is usually `grep`, and `grep` is delighted to find nothing.

**And it catches nothing in family C, which is why that family is the dangerous one.** 13 through 17
all exit 0, print a truthful summary, and report a number that is not about what you think it is
about. There is no signal to read, because the run was honest; the aim was wrong. The only defences
are structural, and they are cheap:

- **Check what your scan matched, not just how many.** 13, 14 and 15 were each caught by looking at
  the matched items — eight files that were the wrong eight, 392 cases where 391 was wanted, eight
  `pgrep` hits that were all shells. Every one announced itself in output somebody nearly skipped.
- **Name every mechanism that could mask the fault before you trust a teeth-check** (16), and
  **suspect the sample domain before the knob when a reachability probe reports zero** (17).
- When a filter, a census or a probe returns a number you expected, that is when to check it. A
  surprising number gets checked for free.

## The scratchpad is shared by every agent in a session

**"Session-specific" does not mean "yours alone."** Every agent spawned by a session inherits the
same scratchpad path, so two agents that both write `suite.log` are writing the same inode. Observed:
two `avgen_tests` PIDs holding one file open for write, two Catch2 summaries interleaved into it, and
a third file containing **zero lines from the agent that created it** — one clean summary, start to
finish, from a different worktree entirely. `/tmp` has the same problem for the same reason.

That last case is why the summary count is not sufficient on its own. Before trusting a suite log:

```sh
grep -c '^test cases:' LOG                        # must be exactly 1
grep -oE 'GitHub/av-gen[a-z0-9-]*/tests' LOG      # must show ONLY your own worktree
echo $?                                            # captured from the binary, not a pipeline
```

The worktree check is the one that catches a log that is entirely somebody else's, because such a
log has exactly one summary and looks perfect. Note that it must tolerate worktrees that no longer
exist: one contaminating process ran in `av-gen-wt-songdirector`, which is not in `git worktree
list` and is not on disk.

**Name scratchpad files by agent or worktree, never by purpose alone.** `suite.log`, `gate.log` and
`build.log` are collision bait; `temporal-suite-<timestamp>-<pid>.log` is not. Build logs collided
far less than suite logs in practice, because a build takes minutes and finishes while several
suites run for an hour and overlap.

A census of one session's scratchpad found **438 `.log` files**, about **120 of them on names two
agents could pick independently** — `suite*` 12, `build*` 14, `gpu*` 16, `base*` 12, `final*` 10.
Roughly **135 were already namespaced** by worktree or pid, so a third of the session had arrived at
this rule unprompted.

**Bumping a numeric suffix makes a collision MORE likely, not less.** `suite.log`, `suite2.log` …
`suite7.log` all existed simultaneously, because re-running and incrementing a digit is what
everybody does — so the next agent that needs a fresh name reaches for the same next digit. A pid or
a timestamp is not a tidier version of `suite2`; it is the only part that makes the name unique.

**Do not clean the directory to fix this.** Some of those files are other agents' live runs, and one
was the evidence that a log had been written by another worktree entirely. Deleting a file somebody
is tailing is a worse failure than clutter.

## Determinism requirements

GPU particle systems use stable stream compaction since 1.0 (ADR-015 revision), so headless
hashes are bit-identical with particles on; the render job's sequence hash is the check.

- `Analyzer`: bit-identical output for identical samples regardless of chunking or run.
- Offline `Engine` runs: bit-identical parameter values and matrices across runs (tested).
- Renderer: identical image hash for identical scene + time on the same GPU (tested). Cross-GPU
  equality is not promised; a perceptual comparison is planned for visual regression.

## Conventions

- A bug fix adds a test that failed before the fix.
- Tests that need hardware (audio device, GPU) call `SKIP()` rather than fail when it is absent.
- No test reads a system clock for correctness; timing-based tests (`[device]`) only check
  monotonic progress with generous margins.
- Benchmarks: `Analyzer` hop time and modulation evaluation are reported, not asserted
  (`docs/performance.md`).

## Procedural world suites (2026-09-09)

| Tag | Binary | Covers |
|---|---|---|
| `[spatial]` | unit | attributes, point clouds, operators, fields, effectors |
| `[spline]` | unit | spline kinds, generators, frames, sampling, packing |
| `[grammar]`, `[hierarchy]` | unit | shape grammar operations, recursion, procedural sources |
| `[sdf]` | unit + gpu | node tree evaluation, packed interpreter parity, surface nets, objects |
| `[material]`, `[color]` | unit | material programs, OKLab and palette maths |
| `[graph]` | unit | node registry, typing, evaluation, dirty tracking, subgraphs, library |
| `[fields][gpu]` | gpu | every field kind and falloff, CPU against GPU within 1e-4 |
| `[culling][gpu]`, `[lod]` | gpu + unit | frustum and screen-size culling against a CPU reference, LOD meshes |
| `[debug]` | gpu | debug geometry builder and the drawing pipelines |
| `[states]`, `[assetbrowser]`, `[profile]`, `[library]` | unit | states and macros, asset scanning, profiling, the preset library |
| `[examples]` | gpu | every showcase renders bit-identically twice; the flagship along its arc |

The parity tests are the backbone: any change to a field, effector, SDF node or material op has to
produce the same number on the CPU and the GPU, which is what keeps offline renders honest.

## AI control plane (ADR-094)

| Tag | Binary | Covers |
|---|---|---|
| `[ai][tools]` | unit | the tool registry against a real `app::Engine`: schema validation, structured errors, every tool's behaviour, annotation invariants |
| `[ai][tools][regression]` | unit | the three silent-failure guards — an unbound timeline target, a `Replace` track or route overriding a set, a camera pose written in orbit mode — plus the summary-versus-result check |
| `[ai][provider]` | unit | each vendor adapter against recorded wire payloads, in both directions, with the credential asserted to be in a header and never in the body or the URL |
| `[ai][credentials]` | unit | that a `ProviderConfig` cannot carry a secret, that a file carrying one is refused, and that status text never contains a value |
| `[ai][transaction]` | unit | snapshot round-trips, rebinding after a restore, rollback and commit |
| `[integration][ai]` | unit | the whole path: `submit` → job worker → agent loop → main-thread queue → real tools → real engine, with transactions, rollback, cancellation, budgets and the acceptance scenarios |
| `[integration][ai][threading]` | unit | that tools only ever run on the pumping thread, and that a task the frame loop never services times out rather than deadlocking |

**The only permitted mock is the provider** (spec §52): every tool runs against the real engine, so
a suite that passed against a stand-in would prove only that the stand-in works (§54). Tests use
`ai::MemoryCredentialStore` so nothing ever writes to a developer's real keychain.

The end-to-end path is also runnable outside the test binary:

```
avgen --headless --frames 2 --composition examples/helix/helix.scene.json \
      --ai-script examples/ai/atmosphere.ai.json --save-project /tmp/after.json
```

Everything but the model's judgement is real, so the resulting project and the captured frame can
be diffed against a run without the pass.

## Diagnosing input

`AVGEN_UI_SELFTEST=1 ./build/debug/src/avgen --play` logs two lines every thirty frames: what ImGui
sees of the pointer (position, buttons, the hovered window, whether it captured the mouse) and what
SDL reports (window position, global and window-relative pointer, focus flags, and counts of the
raw mouse events the application received). It separates "the widgets are broken" from "the events
never arrived", which is how the event-queue regression in `OutputManager::pumpEvents` was found.
The counter for filtered events also shows when the application's own window filter is dropping
input meant for the UI.

## Checking the world editor

The editor's *decisions* are ImGui-free and are in `tests/unit/test_world_editor.cpp`: selection,
groups, transforms, duplication, the ghost's placement validity, the gizmo's drag arithmetic,
undo/redo, and the round trip through both a scene file and a project. `edit_history.cpp`,
`world_edit.cpp`, `world_probe.cpp`, `brush.cpp`, `gizmo.cpp` and `world_editor.cpp` are on the unit
test target for exactly that reason (ADR-092).

The editor's *wiring* is not reachable that way, and this project cannot screenshot an ImGui frame.

```sh
./build/release/src/avgen --generate examples/recipes/glowmere-low.recipe.json \
    --ui-script edit --frames 200 --size 1280x800
```

`--ui-script edit` restores the default layout, arms a brush, paints a stroke across the canvas,
undoes it, redoes it, clicks to select, groups two objects and undoes that — all of it through real
SDL events on the process queue, routed by `Window::pumpEvents`, seen by ImGui, gated by the canvas's
hover state. It prints what the scene did on the way out:

```
edit: library has 44 assets; armed 'mushroom_red'; 8 nodes to start
edit: ghost armed=true ... ground=(110.4, -2.5, 93.2) slope 1 deg, 12 instance(s), 12 valid, 0 blocked
edit: first ghost at (111.6, -2.7, 87.9), 0.61 m tall, footprint 0.30 m, ok
edit: painted 24 node(s) in one stroke; undo stack 1 deep, top 'Place 24 x mushroom_red'
edit: after undo, 8 nodes (started at 8)
edit: after redo, 32 nodes
```

**The press has to come before the pointer moves, and that is a property of Dear ImGui rather than
of this arm.** `ImGui_ImplSDL3_UpdateMouseData` replaces the pointer position with the operating
system's cursor at the top of every frame in which the window is focused and no mouse button is
held, so a synthetic motion on its own is overwritten before the frame that would have used it.
While a button is down it leaves the position alone. Any scripted check of the pointer therefore has
to happen mid-drag; the first version of this arm reported "no ground under the cursor" while the
physical mouse sat over a panel, and an earlier one opened an example project because its click
landed in the Assets list of a layout left over from another session.

`AVGEN_EDITOR_TRACE=1` prints the editor's inputs and what it made of them, once per frame: the
mode, whether the pointer is over the canvas, where in normalised device coordinates, whether the
button is down, and whether the ghost is armed and found ground. That is the line that separates
"the brush is broken" from "the pointer never reached the canvas".
