---
id: reference/command-line
title: Command Line
category: Reference
summary: The common flags, every AVGEN environment variable, and the exit codes. avgen --help lists the rest of the flags.
order: 92
audience: expert
tags: cli, command line, flags, headless, environment, arguments
keywords: command line options; headless render; how do i render from the terminal; avgen flags; environment variables
related: rendering/offline-render, performance/instruments, performance/diagnosis
features: subsystem.cli
---

# Command Line

This page covers the flags most work needs; it is not complete. `avgen --help` prints the full flag list. The
`AVGEN_*` environment variables are all listed below (a test keeps the list complete).

## Loading

| Flag | Does |
|---|---|
| `--audio <file>` | load an audio file at startup |
| `--scene <file>` | load a glTF/GLB scene |
| `--env <file>` | load an equirectangular `.hdr` |
| `--composition <file>` | load a scene composition (`avgen-scene` JSON) |
| `--project <file>` | load a project |
| `--example <name>` | open a built-in example by name |
| `--generate <file>` | compose a world from a recipe |
| `--shader <file>` | add a background shader layer (repeatable) |
| `--post <file>` | add a post shader layer (repeatable) |

## Rendering

| Flag | Does |
|---|---|
| `--render <out>` | offline render, headless, to a directory or a video file |
| `--format <kind>` | `png`, `exr` or `video` |
| `--range <a>:<b>` | time range in seconds; either side may be empty |
| `--codec <id>` | `prores4444`, `prores422`, `h264`, `hevc`, or an ffmpeg encoder name |
| `--quality <0-100>` | video quality |
| `--queue <file>` | run a render queue, headless |
| `--fps <n>` | offline frame rate (default 60) |
| `--capture <file>` | write the last frame; `.png` gives a PNG, anything else a PPM |
| `--tier <t>` | `preview`, `realtime`, `high` or `offline` |
| `--headless` | no window; `--render` and `--queue` imply it |

## Audio and control

| Flag | Does |
|---|---|
| `--input [name]` | analyze a live capture device; the name is optional |
| `--osc-port <n>` | override the project's OSC port |
| `--list-audio-devices`, `--list-midi` | enumerate and exit |
| `--play` | start playing immediately |
| `--start-at <s>` | live editor: put the playhead at second *s* before the first frame (a diagnostic, for profiling one shot) |
| `--direct` | cut the camera to the loaded track |

## Output and sharing

| Flag | Does |
|---|---|
| `--output <d>[:fullscreen\|:WxH]` | an output window on display `d` (repeatable) |
| `--syphon <name>` | publish the frame as a Syphon server (macOS) |
| `--ndi <name>` | publish as an NDI source (needs the NDI runtime) |

## Diagnostics

| Flag | Does |
|---|---|
| `--profile-cpu` | print the main thread's phase distribution on exit |
| `--profile-csv <f>` | one row per frame; implies `--profile-cpu` |
| `--debug-target <t>` | show an auxiliary target: `normal`, `roughness`, `velocity`, `emission`, `ids`, `occlusion`, `depth` |
| `--disable <list>` | switch phases off: `shadows`, `ao`, `volume`, `post`, `shadowmask` |
| `--canvas-scale <f>` | render at this fraction of the canvas's pixels, 0.25 to 1 |
| `--stress <seed>` | apply random slider-like actions every frame |
| `--ui-script <arms>` | drive the editor repeatably: `hover`, `sliders`, `panels`, `select`, `scrub`, `camera`, `tabs`, `viewpoint` (take the editor viewpoint, drag, play and seek; fails if anything but the drag moves the frame), or `idle` or `all` |
| `--frames <n>` | exit after n frames |
| `--log <level>` | `trace`, `debug`, `info`, `warn`, `error` |

## Session

`--size <w>x<h>` sets the window size in points. **Without it the window opens maximised.**

`--save-project <f>` writes the project on exit; `--export-bundle <d>` writes a bundle.

## Environment variables

| Variable | Does |
|---|---|
| `AVGEN_SHADER_DIR` | prepended to the shader search path |
| `AVGEN_EXAMPLES_DIR` | prepended to the examples search path — **and to the asset browser's scan roots** |
| `AVGEN_HELP_DIR` | where this Help system looks for its content first |
| `AVGEN_TIMELINE_RAW` | dump raw GPU timestamps to standard error |
| `AVGEN_FRAME_COUNTERS` | log the submission split every thirtieth frame |
| `AVGEN_CPU_STAGES` | log the CPU stage split every thirtieth frame |
| `AVGEN_SLOW_PHASE_MS` | log what was being written on any frame slower than this |
| `AVGEN_SLIDER_FILTER` | restrict the `sliders` script arm to a path prefix |
| `AVGEN_UI_SELFTEST` | log SDL and ImGui input routing every thirtieth frame |
| `AVGEN_VIEWPORT_PROBE`, `AVGEN_VIEWPORT_DRAG` | synthesise a viewport click or drag |
| `AVGEN_NO_WARMUP` | skip the two warm-up frames before an offline render |
| `AVGEN_FFMPEG` | path to an ffmpeg executable |
| `AVGEN_NDI_LIB` | path to libndi |
| `AVGEN_QUALITY` | path to the Quality Lab CLI (`avgen_quality`) the live optimizer runs |
| `AVGEN_DAWN_TOGGLES` | `a,b`: enable Dawn device toggles (investigation only; `dump_shaders` with `--log debug` dumps MSL) |
| `AVGEN_PIPELINE_TIMING` | log every pipeline's creation time |
| `AVGEN_SHADOW_STATS` | log the shadow views every thirtieth frame |
| `AVGEN_EDITOR_TRACE` | print the editor's per-frame inputs and what it made of them |
| `AVGEN_DIRTY_TRACE` | log each unsaved-changes sample |
| `AVGEN_HEIGHT_CENSUS` | count terrain height queries and print the census at exit |
| `AVGEN_TRACE_DEBUG` | print each river course's trace while a world generates |
| `AVGEN_ASTRAL_TRACE` | log the Astral renderer's steps each frame |
| `AVGEN_HELP_SELFTEST` | step through every Help page once (a self-test) |
| `AVGEN_CAPTURE_WORLD_INSPECTOR` | `environment`, `camera` or `node:<name>`: pin the World panel's Inspector for a capture |

### Measurement arms

Each of these turns a later optimisation or fix off, so its before and after can be measured in one binary. **They
change what a frame costs or shows.** Since ADR-1803 every `AVGEN_*` variable that is set is logged as a warning when
`avgen` starts and recorded in a live profile's conditions (`environment`); `tools/live_perf_baseline.py write` refuses
a run that had any.

| Variable | Restores |
|---|---|
| `AVGEN_LEGACY_PROCGEN=1` | the pre-ADR-233 double procedural generation |
| `AVGEN_SYNC_PIPELINES=1` | pipeline creation on the calling thread (before ADR-1562) |
| `AVGEN_NO_CONTROL_REPLAY` | a seek without the control replay (before ADR-1168; the seek lands where a play does not) |
| `AVGEN_NO_SEEK_DEFERRAL` | an interactive seek that is never deferred |
| `AVGEN_SEEK_BODY_STEPS=<n>` | the seek's body-step ceiling (0 = none; default ADR-700's) |
| `AVGEN_SEEK_MODE=window` | the entity seek's re-simulation window instead of checkpoints |
| `AVGEN_SCATTER_WORKERS=<n>` | the scatter's worker count |
| `AVGEN_NO_TERRAIN_CACHE` | a terrain rebuilt for every node placement |
| `AVGEN_KEEP_ASSET_CACHE=1` | every asset cache entry kept (before ADR-1392) |
| `AVGEN_HEIGHT_CACHE=0`, `AVGEN_PATH_CUTOFF=0`, `AVGEN_LEVEL_BLEND=0` | the terrain height query without its cache, path cut-off or level blend |
| `AVGEN_NAV_FORCE_TRUST=1` | the navigation grid's terrain trust forced on |
| `AVGEN_DIAG_NO_SIGHTLINE` | a character's visibility signal without its sightline march |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 1 | bad arguments, or initialisation failed |
| 2 | a render, resize or queue failed |
| 3 | the GPU device was lost |
| 4 | `--capture` could not write its file |
| 5 | the run finished but reported GPU errors |
| 6 | `--save-project` or `--export-bundle` failed on exit |

> [!NOTE]
> `--format` reports its own error as *"--output expects png, exr or video"*, naming the wrong flag.
> The flag to fix is `--format`.
