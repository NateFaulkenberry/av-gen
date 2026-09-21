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

## Twenty-eight ways a green suite has lied

Every one of these has happened on this project, most of them on 2026-09-19/20 when several agents
were building concurrently. They divide into **three** families, and the third is the one to read if
you are short of time, because it is the only one the exit code cannot save you from.

- **Family A — the run did not happen as you think** (entries 1-3, 12, 18).
- **Family B — the run happened and you read it wrong** (entries 4-8, 11).
- **Family C — the scan, the filter or the control was looking where the effect could not reach**
  (entries 13-17, 19; 9 and 10 are its older members, from before it had a name).
- **Family D — the ask was malformed** (entries 20-21). Neither a bad measurement nor a bad reading:
  the instrument worked, the probe looked in the right place, and the answer was spoiled by the
  *form of the question* (20) or by the *size of the window* (21).

Families A and B are failures of *reporting*: the run lies about itself, and **the binary's exit
code catches every one of them.** Family C is a failure of *aim*: the run is honest, the exit code
is 0 and correct, and the thing you measured is not the thing you meant. **No exit code catches
those.** They pass every check you would think to run, which is why two of them arrived on the same
night from two agents who never spoke to each other.

### The run did not happen as you think

1. **Stale binary.** The test glob is *configure-time*, so an incremental build after a merge
   silently omits test files the merge added and the suite passes without ever compiling them.
   Always `cmake -S . -B build/release` after a merge.

   **The same thing happens without a merge, on a timescale of minutes.** A suite started before
   your last edit is not a suite of your last edit: the process has the old binary mapped and goes
   on running it however many times you rebuild underneath it. Reporting that run's exit code as a
   guard for the code you are about to commit is this entry with extra steps. If you edit while a
   suite is running, the run is spent — restart it.
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

   **DIAGNOSED, 2026-09-21, and it is NOT the mechanism above.** `avgen_render_tests` takes an
   intermittent SIGBUS, and the cause is in the crash reports, which nobody had opened.
   `~/Library/Logs/DiagnosticReports/avgen_render_tests-*.ips` held **25 of them**, spanning
   2026-09-18 to 2026-09-21, and **every single one has the same signature**:

   - `EXC_BAD_ACCESS`, `SIGBUS`, `KERN_PROTECTION_FAILURE`;
   - the faulting address is **exactly the first byte past the end of a `MALLOC_SMALL` region** --
     `vmRegionInfo` reports `bytes after start: 0` of the reserved space that follows it, in all
     twenty-five;
   - the faulting frame is a Catch2 test body on the main thread, not Metal and not Dawn.

   That is **a heap buffer overrun on the CPU**, not GPU memory exhaustion, and it explains every
   property that made it look mysterious: a small overrun normally lands in malloc's own slack
   inside the same region and does nothing at all. It faults only when the allocation happens to
   sit at the very end of that region's page run. So the crash **moves between runs, is
   independent of the RNG seed, is independent of machine load, and disappears when you replay the
   seed that produced it** -- all of which was measured before the reports were read, and none of
   which pointed at the cause.

   **Read the crash report first.** Four separate sessions treated this as a scheduling or memory
   mystery and took samples; the answer was sitting in a file the OS had already written, and the
   one fact that settles it -- *fault address is one past a malloc block* -- takes a minute to get:

   ```sh
   ls -t ~/Library/Logs/DiagnosticReports/ | grep avgen
   python3 -c 'import json,sys; b=json.loads(open(sys.argv[1]).read().split("\n",1)[1]); \
       print(b["exception"]); print(b.get("vmRegionInfo","")[:600])' <report>.ips
   ```

   **FOUND AND FIXED, same session, by an ASan build.** `-fsanitize=address` named it on the first
   run, in one line of `tests/rendering/test_lighting_lab_gpu.cpp`:

   ```cpp
   CHECK(*std::max_element(falloffProfile(*control).begin(),
                           falloffProfile(*control).end()) > 0.5f);
   ```

   `falloffProfile` returns a `std::vector<float>` **by value**, and it is called **twice**. So
   `.begin()` is an iterator into one temporary and `.end()` an iterator into a *different* one,
   and `max_element` walks from the first vector's start toward an unrelated address in the heap.
   Where that walk crosses a reserved page, `KERN_PROTECTION_FAILURE`.

   Every property follows from that and none of them needed a new theory:

   - **intermittent** -- it depends on where two temporaries land. If the second is at a *lower*
     address, `begin >= end`, the loop ends immediately, and nothing happens at all;
   - **the crash site moves** -- the victim is whoever owns the pages the walk crosses;
   - **seed- and load-independent** -- it is allocator layout, not scheduling;
   - **replaying the crashing seed passes** -- same reason;
   - **one byte past a MALLOC_SMALL region, in all twenty-five reports** -- because that is where a
     forward walk through the heap first meets a page it may not touch.

   **Sampling could not have found this.** Eight full runs, a seed replay, a concurrency arm and a
   load check produced a precise description of the *symptom* and not one step toward the cause.
   The crash report gave the mechanism (a heap overrun) in about a minute, and ASan gave the line
   in about an hour of unattended build-and-run. Reach for both before the third sample.

   ```sh
   cmake -S . -B build/asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
   cmake --build build/asan -j 10 --target avgen_render_tests
   ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 tools/gpu-lock.sh ./build/asan/tests/avgen_render_tests
   ```

   It reports `container-overflow` rather than `heap-buffer-overflow`, because ASan's container
   annotations catch the read between `size()` and `capacity()` of the second vector before the
   walk reaches unmapped memory. **A `container-overflow` on a range built from two calls is
   almost always this**: look at whether the two ends came from the same object.

   The general rule, which is not about this suite:

   > **A range needs one object. Two calls that each return by value are two objects**, and a
   > compiler will not stop you building an iterator pair out of them. Name the result.

   The samples below are what sampling got, and they are kept because the shape of the data is
   what a heap overrun looks like from the outside -- worth recognising the next time:

   | run | result |
   |---|---|
   | 1 | 397 cases, 396 passed, 1 skipped |
   | 2 | **SIGBUS** (exit 138), ~300 lines in, after *"A generated tree renders, and renders the same way twice"* |
   | 3 | **SIGBUS** (exit 138), at a **different** point -- a skinning/culling table |
   | 4 | run 2's own seed replayed: **397 cases, exit 0** |

   So: **not seed-determined, not order-determined in any fixed way, and not a function of machine
   load.** Replaying a crashing run's seed passes. A quiet-machine explanation was proposed and
   withdrawn on this data. By the end of the session it was 4 crashes in 8 full runs, one of them
   with a CPU suite running alongside and one with the machine otherwise idle.

   **It predates the fog branch by three days**: fourteen of the twenty-five reports are from
   2026-09-18, before any of this work existed. The victim test varies because the victim is
   whoever owns the allocation that happens to be at the end of a region -- which is also why
   naming the test it died in has never been informative.

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

   **And when you go to kill it, do not use a pattern.** Two traps compound here.

   The binary's own command line is **relative** — `./build/release/tests/avgen_tests` — while the
   wrapper's contains the absolute worktree path. So a pattern specific enough to identify *your*
   worktree matches the wrapper and **can never match the child**: parent dead, child running,
   which is this entry arriving in the file that documents this entry. It happened that way on
   2026-09-20.

   Loosen the pattern and it stops identifying an owner at all. On a machine with four agents in
   four worktrees, `ps -Ao pid,comm` prints the **identical string** for every one of them, because
   they are the same binary built from the same relative path. One such sweep had five candidates
   and **three belonged to other agents**, including a suite three minutes into a verification run.

   **The working directory is the only thing that distinguishes them.** Resolve it per candidate
   and kill by PID:

   ```sh
   for pid in $(pgrep -f tests/avgen_tests); do
     cwd=$(lsof -a -p "$pid" -d cwd -Fn 2>/dev/null | grep ^n | head -1)
     case "$cwd" in *my-worktree*) kill -TERM "$pid";; esac
   done
   ```

   `pkill -f` is not a targeting mechanism on a shared machine. It is a coin toss weighted by who
   happens to be running.

18. **The exit code you read was the tooling's opinion, not the binary's.**

   Twice in one session a harness running a test binary reported an exit code that disagreed with
   what the binary did. Once it reported **failure (144)** for a run whose process was still alive
   and still holding the GPU lock — the wrapper had been killed and the child kept going (entry 12
   is that incident from the lock's side). Once it reported **success (0)** for a run whose own
   summary said `1 failed`.

   Both directions are dangerous and **the second is worse, because it is the one that gets a defect
   committed.**

   So the guard is narrower than "read the exit code":

   > **Read the exit code the binary itself returned, not the one the tooling reports about the
   > binary.**

   In practice: capture `$?` immediately after the binary, in the same shell, and write it somewhere
   you will read — `./build/release/tests/avgen_tests …; echo "EXIT=$?" >> log`. A wrapper's status,
   a task runner's summary and a CI widget are all reports *about* the run and can each be wrong
   about it. And because a crashed Catch2 run prints **no verdict line at all** (11), `EXIT=` and the
   presence of a summary have to be read **together**: an exit code with no summary is a crash, and
   a summary with a disagreeing exit code is a tooling fault — **neither is a pass.**

   This is not about any one harness; it is structural to anything that wraps a process.

   **And capturing `$?` straight from the binary is load-bearing wherever it already happens.** It
   is usually not written down as a requirement, so a later cleanup that wraps an invocation "for
   consistency" silently removes the only reason the result can be trusted, and nothing announces
   it. Same shape as three divergent copies of one conversion: a property everything depends on,
   recorded nowhere, preserved by luck.

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

19. **A minimum is robust to contention ARRIVING and defenceless against the machine getting
   quieter.** ADR-170 says "report minima over repeats, never means" and stops, which reads as
   though minima solve contention. They solve **one direction** of it.

   A minimum survives load *arriving*, because interference only ever adds time and the floor is
   untouched. It has no defence at all against the load *falling*: then the floor itself moves down,
   and an arm measured later wins for nothing. **Sequential arms plus a load that decreases is the
   exact shape that produces a clean, monotone, entirely wrong ladder** — and it announces nothing,
   because every individual number is a real minimum honestly measured.

   Measured on 2026-09-20: a five-rung per-slot cost ladder, run as sequential blocks minutes apart,
   came back `5.05, 5.51, 6.03, 4.52` — three rungs of believable per-slot cost and then a fourth
   slot apparently *cheaper than none*. A separate arm design with a third of the samples had shown
   the same anomaly at the same rung. The explanation was that a second agent had gone from blocked
   to working partway through, so the last two arms were measured on a quieter machine than the
   first three.

   **The repository already encodes the fix and refuses to let you skip it.** `--ab` interleaves
   baseline and arm inside ONE process and reports drift — it voided a run that same evening with
   *"baseline 13.83 ms in the first half against 21.17 ms in the second; this run measured two
   machines rather than two arms."* ADR-460's method is "minima of 3-4 **interleaved** repeats". The
   word doing the work in that sentence is *interleaved*, not *minima*.

   **Interleave, or pair each arm with a baseline taken in the same conditions.** A number measured
   against a baseline in the same run is worth more than four numbers measured in sequence, and on a
   machine several agents share it is the only kind worth quoting.

   **A paired delta's SIGN is the robust quantity; its magnitude is not.** In the run above, the rep
   that went out under load 49 came back at less than half the other two reps' magnitude — and the
   same sign. So a contaminated rep should be read for **agreement in direction first**, and only
   then for size: a spread of 0.99 across three reps is one noisy rep among three, not evidence
   against the effect. This also gives a discard rule that can be stated in advance, which is what
   makes a discard honest rather than convenient: **discard on sign disagreement, never on spread.**

   **And it invalidates controls, not just ladders.** The same session produced a clean-looking
   refutation of a hypothesis — an arm at 4.65 ms against a 5.05 ms baseline — which was itself
   measured sequentially. Under interleaving that baseline reads 5.11 to 6.16, so the comparison
   never meant anything and the "refutation" had to be withdrawn. **When you find that a
   measurement was sequential, re-examine every conclusion drawn against it**, including the ones
   that argued against your own hypothesis. Those are the ones you are least likely to re-check.

   **Both halves of this are shared-machine rules and one is useless without the other.** The
   measurer owes: *a window is a promise about a duration, so say how long the measurement is* — it
   is the measurer who knows a five-arm ladder is not a paired A/B. The coordinator owes: *ask how
   long before granting a window, and say unprompted when the machine changes* — an agent going
   from idle to compiling is a fact the coordinator has and the measurer does not.

### The question was the wrong shape

20. **A yes/no prediction about an effect that has a size will be answered, and the answer will not
   contain the size.**

   A hypothesis — *the transmittance early-out is why four media cost less than one* — was tested
   with a control whose prediction was stated in advance as a **binary**: "four media at matched
   optical depth must cost MORE than one." The control came back cheaper, so by the letter of the
   prediction the hypothesis was **refuted**, and it was reported that way.

   The magnitudes said the opposite. Four media at full density were **−1.16 ms** against one; at
   matched depth, **−0.24 ms**. **Diluting removed 79% of the effect.** The early-out was not
   present-or-absent, it was *four fifths of the answer* — and a prediction shaped as a yes/no has
   nowhere to put a proportion, so the result arrived with four fifths of its content discarded.

   Nothing was wrong with the instrument, the arms or the hypothesis. **The question was wrong.**
   That makes this its own family: entries 13-19 are measurements that were misleading, and this is
   a measurement that was fine and a *question* that could not receive its answer.

   **When the mechanism has a size, state the prediction as a magnitude.** "Matched depth removes
   most of the saving" is falsifiable, and it can also come back "removes 79%", which is the result.
   "Costs more" can only come back yes or no, and both are wrong here.

   Corollary worth the line: a refutation you were *expecting to lose* is the one to re-read before
   reporting. This one refuted a hypothesis its author believed, which is exactly when the letter of
   a prediction is most likely to be taken at face value.

21. **A window too small for the quantity to exist in it returns a phase dressed as a statistic.**

   A macro-detail term was built to be mean-preserving -- `1 + amount * (n * 2 - 1)`, whose mean is
   exactly 1 when `E[n]` is 0.5 -- and the test that checked it **failed at +7.1%**. The code was
   right. The test averaged the field over a fog bank's interior, **a region comparable to the
   noise's own period**, so there were only a few periods in it to average and what came back was
   *where the bank happens to sit in the noise*. Measured properly across many periods, `E[fbm3]`
   is 0.50044 and the term's mean is **1.00089** -- a 0.09% shift, not 7.1%.

   **A mean is a property of a form over many periods; asking for it over one period returns a
   phase.** The same applies to any low-frequency or periodic quantity: variance, RMS, a duty cycle,
   a grain figure over a window narrower than the structure it is measuring.

   **And it reproduces, which is what makes it convincing and wrong.** The phase is deterministic,
   so the bad number is stable across reruns and survives every check that looks for flakiness. It
   is not noise. It is the right calculation over a window in which the answer does not exist.

   Count the periods in your window before you trust a statistic taken over it.

22. **Adding a dispatch silently invalidates every probe written against the pre-dispatch path.**

   A reachability probe names a field. When a kind is given its **own** density function, every test
   that sampled the shared one goes on passing *while asserting about a field nothing calls for that
   kind*. **The probe does not break — that is the whole problem.**

   Measured on 2026-09-20, after `shaders/fog.wgsl` gave the fog bank its own field: **three** probes
   in one file were mis-aimed, not the one that was noticed. The worst was the one guarding a
   *shipped fix* -- a defect that had survived in three places and been fixed with a break
   demonstration -- whose fog assertion was against the vortex's envelope. **From that commit
   onward the fix's claim was untested and passing.** A fix for an invisible defect, made invisible
   again by an architecture change, with a green suite throughout.

   **The audit is two greps and the intersection is the suspect set:**

   ```sh
   grep -rln 'sampleVortex\|vortexShapeAt\|packVortex' tests/   # calls the old field
   grep -rln 'VolumetricFog' tests/                              # constructs the dispatched kind
   ```

   | file | old-field calls | builds the kind | verdict |
   |---|---|---|---|
   | `test_fog_bank.cpp` | 9 | yes | **three cases mis-aimed** |
   | `test_field_bus.cpp` | yes | no | clean |
   | `test_vortex_parity_gpu.cpp` | yes | no | clean |
   | `test_effect_conformance.cpp` | 0 | yes | clean |
   | `test_effect_registry.cpp` | 0 | yes | clean |

   Run it **when you add the dispatch**, not when something looks wrong -- nothing will look wrong.
   And note which half each party got right: the prediction that only one *kind* was affected held;
   the guess that only one *case* was affected came from the instance that was tripped over rather
   than from a search, and was wrong. **A boundary is measured, not estimated.**

23. **Two kinds that agree on a lane are not a dispatch, and production code has no suite to go
   green.**

   Entry 22 is that hazard in *tests*. The same session found it in shipping code, and it is worse
   there because there is no summary line to misread -- the program simply does the wrong thing.

   `MediumSlot` was made per-kind: each kind gets its own packer, its own density function, its own
   lane meanings. What was not done was audit the **readers**. Within an hour of a third kind
   existing, three shared accessors turned out to be still assuming the first kind's layout: a wind
   hook writing a field the third kind does not store, three coefficient helpers reading `lane(1).w`
   as an extinction when on the third kind it is a radius control (tens, not hundredths), and a
   reserved lane that a sixty-one-float block landed on.

   **The reason none of it surfaced in the preceding day is that the first two kinds happened to
   agree on every lane those sites read.** Two implementations of an interface that agree are one
   implementation with two names; the disagreement is the control, and **the third case is the
   first one that can fail.** If you are generalising a layout, a protocol or a schema, the second
   instance does not validate the generalisation -- it is usually built by copying the first.

   The general form: **when you make a layout per-kind, every reader of that layout is a call site
   to audit, not just the probes.** Grep for the lane index, not for the feature name -- the
   feature name is what the mis-aimed reader does *not* mention.

   And all three presented the same way, which is why they are expensive: right shape, slightly
   wrong appearance. **A defect that survives because it is plausible is the costly kind** -- it
   gets tuned around rather than found. "Renders as a comet" is a bug someone fixes in a minute.

24. **A constant that was derived from one function and is still right under another is a
   coincidence, and the control that ends the coincidence is the one nobody moved.**

   A ray march clipped each medium to a bound of `3 * thickness` vertically. That is where a
   GAUSSIAN ends, and it was written when the field's vertical profile was one. The profile was
   later replaced by an exponential at an artist-controlled rate -- and the bound was not
   revisited, because at the control's DEFAULT the old constant still left only 0.37% of the
   column outside. It was right, for a reason that had stopped existing.

   At the control's low end it left **56%** of the medium outside its own bound. The same bound
   was short horizontally by a factor of `bankLength`, which is 1 by default and 6 at the top of
   its slider: **71%** of peak density outside, at a setting an artist reaches by dragging.

   Two things make this family hard to catch and worth its own entry:

   - **it is invisible at every default**, so it survives any amount of ordinary use, and the
     arms and screenshots accumulated while it is invisible become evidence that it is fine;
   - **the failure is a quieter version of the thing working.** Nothing errors. The medium is
     still there, still soft-edged, still the right colour, and simply smaller than the number
     that was typed. The artist concludes the control is weak and tunes around it.

   **The check is to put the claim and the thing it claims about in front of each other.** A bound
   is a claim about the support of a field, so sample the field and assert containment -- and add
   the assertion that stops the first one being satisfied by giving up, because a bound of infinity
   contains every field perfectly. Two halves: *nothing dense outside it*, and *it is not much
   larger than what is inside it.*

   Generalises past bounds. Any constant chosen against a distribution -- a threshold, a budget, an
   epsilon, a cache size, a step count -- is invalidated by a change to that distribution
   (ADR-389), **and it will keep passing until someone moves the control that makes the two
   distributions differ.** When you replace a function, grep for the constants that were sized
   against the old one. They do not announce themselves.

25. **A GPU suite reads its shaders from the source tree at RUN time, so editing a `.wgsl` while
   one is running makes the whole run say nothing.**

   Done on 2026-09-21, by me, an hour after re-reading the entry about editing `tools/gpu-lock.sh`
   while instances held it. The suite was started, then `shaders/particles.wgsl` was edited while
   it ran. `ShaderLibrary` loads from `AVGEN_SHADER_SOURCE_DIR` when a case asks for a module, so
   cases that ran before the write compiled one version and cases after it compiled another.

   **The run came back green, 397 cases, exit 0.** That is what makes it worth an entry rather
   than a note: there was no symptom. A mixed run is not "probably fine" -- it is a run in which
   no case can be attributed to a version of the tree, including the green ones, and a pass under
   those conditions is exactly as uninformative as a failure.

   The rule is narrower than "do not edit while tests run", because a C++ edit is harmless -- the
   binary is already linked:

   > **A running binary's inputs are frozen only if they were compiled into it.** Shaders, scenes,
   > assets and config files are read when a case asks for them. Anything the suite loads from
   > disk is live for the whole run.

   Discard the run and start it again. It costs one suite; arguing about which half of a mixed run
   to believe costs more, and believing it costs more still.

   **And the same hour, the same mistake one layer up.** While the replacement run was going, this
   very entry's companion note was written into `tools/gpu-lock.sh` -- the script that run was
   executing. Bash reads a script incrementally by byte offset, so inserting five lines above the
   offset it was holding shifted everything after it, and the wrapper died with

       tools/gpu-lock.sh: line 89: syntax error near unexpected token `('

   *after* the binary had finished and printed `397 cases | 396 passed | 1 skipped`. The file was
   restored from `git` within about thirty seconds, so the lock was not wedged this time -- but the
   run's captured exit code was **2, from the wrapper**, against a summary that said everything
   passed. Which is the header's own rule arriving as a live example: **an exit code that disagrees
   with the summary is a tooling fault, and neither of them is a pass.** The run was re-taken.

   Two corollaries worth more than the incident:

   - **`pgrep -f` matched only the orphaned waiters.** The "is anything running?" check came back
     with four processes and all four were `until ! pgrep -qf "avgen_render_tests"` loops from a
     session two days ago, matching their own command lines and therefore immortal. The idle
     machine looked busy. Match the binary's PATH and exclude the shell wrappers:
     `ps -Ao pid,command | grep 'build/release/tests/avgen_render_tests' | grep -v 'zsh -c'`.
   - **After editing a shell script, `bash -n` it.** It costs nothing and it is the difference
     between finding a syntax error now and finding it in the exit code of somebody's suite.

26. **`git checkout -- <file>` to undo a break demonstration silently deletes the uncommitted work
   in that file, and everything still builds.**

   A break demonstration edits a file, runs the suite, and restores it. Restoring from a scratch
   copy is correct. Restoring with `git checkout --` restores it to **HEAD** -- which is not where
   it was, if the file also carried an hour of uncommitted work.

   Done on 2026-09-21. Four artist rows and a packed lane went back to HEAD, the build succeeded,
   the tests that did not cover the reverted lines passed, and the next edit -- which anchored on
   one of the deleted lines -- **silently did nothing**, because `str.replace` with no match is a
   no-op. Two layers of silence: the revert and then the failed patch.

   **What caught it was a count taken from the code**: `grep -c 'storedFloat("'` came back 9 where
   the change should have made it 12. The registry's block-size assertion would have caught it at
   the next suite too, but the grep caught it one minute after it happened instead of forty.

   Three habits, in the order they pay:

   - **restore from a scratch copy, never from `git`**, when a file has uncommitted work:
     `cp file $SCRATCH/file.bak` before the break, `cp $SCRATCH/file.bak file` after;
   - **assert on every scripted edit.** `assert old in t` before `t.replace(old, new)` turns a
     silent no-op into an immediate failure, and the one replacement in that batch written without
     an assert is the one that vanished;
   - **count the thing you changed, from the code, after changing it.** It is the same move as
     re-deriving a test's expected number from `grep -c` rather than from the red output, and it
     catches a different failure: not "the test now agrees with the code" but "the code is not what
     I think it is".

27. **A census of "what the project ships" is answered in part by the project's own instrumentation,
   and two people running "the same census" will get different numbers.**

   `examples/` holds **119 tracked `_`-prefixed JSON files**, 30 of them `*.scene.json`, written by
   several agents' diagnostic tooling and committed. Any `grep -rl` over `examples/` finds them. A
   question like *"how many shipped scenes use X"* therefore has its answer inflated by the arms
   somebody generated while measuring X -- which is the measurer contaminating the measurement, in
   the one place nobody looks for it because the files look exactly like data.

   **It happened twice in one session, and the second time it happened to two people at once.**
   First: a claim that no shipped scene contained a fog bank, published with a command that
   returned 52 rather than 0, because the filter that made it true lived in the analyst's head.
   Then: two people ran a corrected census of the same question and got **120 / 57 / 57 / 0**
   against **90 / 30 / 23 / 1**. Neither was wrong. They differed on two axes that neither command
   made visible:

   - **what counts as shipped** -- all tracked scenes, or only the ones that are not probes;
   - **what counts as using a feature** -- the key is present, or its value is non-zero. `"x": 0.0`
     has the key and does not use the thing.

   Three rules, and the third is the one that actually holds:

   - **exclude probe files explicitly and say so in the command**, not in the sentence around it;
   - **state the predicate.** Presence and non-zero are different censuses and both are true;
   - **put the census in a file and cite the file.** `tools/fog_law_census.py` takes both axes as
     flags and prints the branch, the scope and the predicate above its numbers. A figure in a
     document that a reader cannot reproduce without the author's shell is a figure that will
     eventually be disbelieved -- **and it will be disbelieved for a reason that has nothing to do
     with its subject**, which is the expensive part.

   The general form, which is not about scenes: **when your tooling writes artefacts into the same
   namespace as the thing it measures, every later measurement of that namespace is contaminated
   until somebody notices.** Name them so they can be excluded, and exclude them in the command.

28. **A parity test covers the terms it reads. Add a term to the pair and it silently stops being
   a pair.**

   `test_fog_parity_gpu.cpp` compares a shader against its CPU twin over the same packed bytes, and
   it existed because the two once drifted for ten minutes (entry in ADR-565). A new term was added
   to both sides, and the shader's half was then deliberately broken to demonstrate the failure --
   **and the test passed.** It compared four quantities and the new one was not among them.

   Nothing about the test had changed; it was simply answering the question it had always answered,
   about a pair that had grown a fifth member. **The gap opens at the moment the feature lands, and
   it opens silently, because a parity harness has no way to know what it is not reading.**

   Two habits:

   - **when you add a term to a transliterated pair, add an assertion for it in the same edit.**
     Not the same day -- the same edit, the way a new `case` goes in with its `enum` value;
   - **break the new term and watch the parity test fail before you believe it covers it.** That is
     ADR-182 applied to the test you did not write, and it is the only step that would have caught
     this. The break was being run for a different reason and the pass was the surprise.

   Generalises to any harness with a fixed output shape: a golden-image test whose mask excludes
   the new region, a round-trip test whose field list is written out by hand, a conformance table
   with a row per property. **The harness's shape is a claim about what the thing has, and the
   thing grew.**

**So `grep -c FAILED` is not a failure count, and neither is its absence.** Two of the cases above
put a well-formed `FAILED:` block into a perfectly healthy log, and one puts *nothing at all* into a
log of a process that died. Read the **exit code first, the summary second, and `FAILED:` blocks
only as a pointer to what to go and look at** — not the other way round.

**The exit code is the only check that catches every entry in families A and B** — but it has to be
**the binary's own**, not a wrapper's report of it (18). Every other signal there — the summary, the
`FAILED:` blocks, the assertion counts — is a convenience that some entry above defeats. Capture
`$?` immediately after the binary, in the same shell: a pipeline's exit code is the last command's,
which is usually `grep`, and `grep` is delighted to find nothing. Then read it **with** the summary,
because an exit code and no summary is a crash and a summary disagreeing with an exit code is a
tooling fault, and neither is a pass.

**And it catches nothing in family C, which is why that family is the dangerous one.** 13 through 17
all exit 0, print a truthful summary, and report a number that is not about what you think it is
about. There is no signal to read, because the run was honest; the aim was wrong. The only defences
are structural, and they are cheap:

**The headline's first confirmed save was on the measurement of the agent who wrote it.** A per-slot
cost ladder returned `7.21 ms fixed + 2.00 ms per slot` — clean, quotable and decision-shaped — and
was checked only because it looked right. It was confounded: a fourth slot reading *cheaper* than a
third, because the four media had been placed close enough together to merge into one volume, so
"another medium" and "another medium in a region already being marched" were the same arm. Four of
the five family-C instances here were caught because something downstream objected — a compiler, a
probe's own control, a blown-out render, a person parsing instead of matching. **That one was caught
prospectively, by distrusting an expected number**, which is the only defence available when nothing
downstream is positioned to disagree.

**And one positive check is worth naming, because it is the inverse of everything above.** Most of
these entries are numbers that looked right and were not. The opposite move is to find a number that
must be **identical** and use identity as the signal: after a change that should have added
assertions to existing cases rather than adding cases, the suite's case count matching the
previously verified total *exactly* confirms the change was what its author thought. **A count that
had moved would have meant something unaccounted for.** Pick a quantity your change must not move,
and check it did not — it is cheaper than checking the quantities it should move, and it fails
loudly when your model of your own edit is wrong.

**One defence in this tree already works, and it is worth copying.** `test_renderer_layout_guards.cpp`
resolves a struct's array extents against constants scraped from named headers, and when it meets a
symbolic name it has not been shown it **fails hard rather than guessing** — its own comment says
why: *"an unresolved extent would give a plausible wrong answer."* On 2026-09-20 that refusal paid
out on a change made long after it was written: a new `media[kMaxMedia * kMediumLanes]` array named
two constants the guard did not have, and it stopped. Writing a literal `64` in the test instead
would have passed every check while the C++ and the WGSL drifted apart the next time a lane was
added. **A check that refuses to proceed on an input it cannot resolve is worth more than one that
resolves it optimistically**, and it is the only entry in this family that is a defence rather than
a wound.

- **When a filter, census or probe returns the number you expected, that is when to check it. A
  surprising number gets checked for free.**
- **An aggregate cannot separate the two things it sums.** Two instances of the same failure were
  found in one night, in two different aggregations: a grain figure that fell 15% across a ladder
  that added nothing but density, because the tone curve compresses near white; and a whole-frame
  mean luminance that read 49 against 84 for two fog banks of identical optical depth, because the
  larger one **covered more of the frame**. Neither number was wrong; both answered a question about
  the frame when the question was about the medium. **Window the measurement onto the thing that
  changed, or look at the picture.** That is the whole family in one sentence, and it would
  have caught all three of the scans below.
- **Check what your scan matched, not just how many.** 13, 14 and 15 were each caught by looking at
  the matched items — eight files that were the wrong eight, 392 cases where 391 was wanted, eight
  `pgrep` hits that were all shells. Every one announced itself in output somebody nearly skipped.
- **Name every mechanism that could mask the fault before you trust a teeth-check** (16), and
  **suspect the sample domain before the knob when a reachability probe reports zero** (17).


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
