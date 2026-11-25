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
| `--osc-port <n>` | the OSC port (overrides the project's); in a headless or scripted run it also opens the listener |
| `--osc-bind <addr>` | the interface OSC listens on. Default `127.0.0.1` (this machine only). `0.0.0.0` listens on every interface: anyone on the network can then set any parameter, so a warning is logged |
| `--no-osc` | open no OSC listener, even in the editor |
| `--midi <filter>` | MIDI sources for this run (name substring or `*`); in a headless or scripted run it also opens MIDI input |
| `--list-audio-devices`, `--list-midi` | enumerate and exit |
| `--play` | start playing immediately |
| `--parked-clock <ambient\|frozen>` | the editor's world clock while the transport is parked: `ambient` (default) keeps the world moving; `frozen` holds it, so a parked frame costs a static one. Never changes a render |
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
| `--ab <phase>`, `--ab-blocks <n>` | headless A/B: the baseline and the phase switched off (or a quality arm), interleaved in one process; `--ab none` is the noise floor |
| `--live-frame` | with `--headless` (and so `--ab`): measure the frame the **live editor** renders (live sky schedule, velocity on demand, compiled materials, live antialiasing) instead of the offline frame. Use it for live performance decisions (ADR-1809) |
| `--canvas-scale <f>` | render at this fraction of the canvas's pixels, 0.25 to 1 |
| `--stress <seed>` | apply random slider-like actions every frame |
| `--ui-script <arms>` | drive the editor repeatably: `hover`, `sliders`, `panels`, `select`, `scrub`, `camera`, `tabs`, `viewpoint` (take the editor viewpoint, drag, play and seek; fails if anything but the drag moves the frame), or `idle` or `all` |
| `--frames <n>` | exit after n frames |
| `--log <level>` | `trace`, `debug`, `info`, `warn`, `error` |

## Session

`--size <w>x<h>` sets the window size in points. **Without it the window opens maximised.**

**Live control is the editor's** (ADR-2133). The editor opens OSC on `127.0.0.1:9000` and MIDI input.
Every other run (`--headless`, `--render`, `--capture`, `--capture-ui`, `--ui-script`, `--ai-script`,
the audits) opens neither unless `--osc-port`/`--osc-bind` or `--midi` asks.

`--save-project <f>` writes the project on exit; `--export-bundle <d>` writes a bundle. With
`--drop-pinned-overrides` the save leaves out values that only repeat the scene's (what a save before
ADR-2132 wrote), so scene-file edits reach them again.

## Version and contracts

These need no window, no GPU and no project, and print to stdout.

| Command | Does |
|---|---|
| `--version` | one line: engine version, API level, the revision the build came from (with `-dirty` if the tree had uncommitted changes) and the build type |
| `--describe <what> [--out <dir>]` | the published contracts as JSON: `tools` (the tool catalogue), `errors` (the error codes), `schema:project` and `schema:scene` (the document schemas, JSON Schema 2020-12), `features` (the feature ids this build can parse: scene keys, node kinds, effect kinds, render outputs, AOVs; printed only), `version`, `baseline` (the contract at this API level, for the breaking-change check), or `all`. With `--out` it writes the artefacts under `<dir>` instead: `schemas/tool-catalogue.json`, `docs/api/tools.md`, `docs/api/errors.md`, `docs/api/protocol.md`, `schemas/avgen-project.schema.json`, `schemas/avgen-scene.schema.json` (`tools/gen_api.sh` runs it on the repository) |

## Driving the engine from another program: `serve` and `call`

Two sub-commands run the engine for another program, through the same tools Studio's assistant uses. Both come
first on the command line (`avgen serve ...`, `avgen call ...`), and neither starts the application: **no window,
no settings file, no OSC port and no MIDI input**. A GPU device is created only when the first render job starts
(`render.capture`, `render.start`), and each job takes the machine's GPU lock -- the same lock directory
`tools/gpu-lock.sh` uses -- for as long as it renders, so a render and an agent's locked GPU job never share the
GPU. A session that never renders never creates a device. The log goes to stderr; stdout carries only the protocol
or the result, and anything else the engine prints is sent to stderr.

| Command | Does |
|---|---|
| `serve --stdio` | a JSON-RPC 2.0 server on stdin and stdout, one message per line: `initialize`, `tools/list`, `tools/call`, `shutdown`, with `notifications/progress` and `notifications/log` (`docs/api/protocol.md`). One session for the life of the process. `shutdown`, or the end of stdin, rolls back an open transaction and exits; nothing is saved unless a client calls `project.save` |
| `call <tool> ['<json>']` | one tool call in a fresh session; prints its envelope (`success`, `result`, `summary`, `changes`, `warnings`, `transaction`, `job`, and `error` on failure) as JSON |
| `call --calls <file.json>` | a list of calls, `[{"tool": "...", "arguments": {...}}, ...]`, in one session; prints `{"success", "calls": [{"tool", "envelope"}]}`. Every call runs, even after one fails |

Flags for both:

| Flag | Does |
|---|---|
| `--root <dir>` | a folder the session may read and write; a bare project name (`project.create {"name": "demo"}`) is made in the first one. Repeatable |
| `--read-root <dir>` | a folder it may read (models to import, recipes). Repeatable |
| `--write-root <dir>` | a folder it may write, and so read. Repeatable |
| `--allow-subprocess` | tools may start child processes (`performance.profile_scene` and its family) |

`call` also takes `--project <p.json>` (open it first; its folder becomes readable and writable) and `--save`
(save the project after the calls, if they all succeeded). A render job a `call` started and did not `job.wait`
for is run to the end before the process exits.

**With no root, every path is refused** with `PERMISSION_DENIED`: a session is never given a folder by guessing.
Each root must exist, and is made absolute once, on the command line; inside the session a relative path means
the open project's folder (or the first write root), never the directory you started in.

`call`'s exit codes: 0 success; 1 a tool failed (the envelope is still printed); 2 bad arguments; 7 the host
could not start (or `--project` would not open). `serve` exits 0, 2 for bad arguments, 7 if the session could
not be created.

## Environment variables

| Variable | Does |
|---|---|
| `AVGEN_SHADER_DIR` | prepended to the shader search path |
| `AVGEN_AGENT` | `serve` and `call`: the name a render job writes into the machine GPU lock's `holder` file (else `avgen-<pid>`), as `tools/gpu-lock.sh` does |
| `AVGEN_GPU_LOCK_TIMEOUT` | `serve` and `call`: seconds a render job waits for the machine GPU lock before it fails `BUSY` (default 3600, as `tools/gpu-lock.sh`) |
| `AVGEN_EXAMPLES_DIR` | prepended to the examples search path — **and to the asset browser's scan roots** |
| `AVGEN_HELP_DIR` | where this Help system looks for its content first |
| `AVGEN_TIMELINE_RAW` | dump raw GPU timestamps to standard error |
| `AVGEN_FRAME_COUNTERS` | log the submission split every thirtieth frame |
| `AVGEN_CPU_STAGES` | log the CPU stage split every thirtieth frame |
| `AVGEN_SLOW_PHASE_MS` | log what was being written on any frame slower than this |
| `AVGEN_SLOW_FRAME_MS` | log every phase over 1 ms of any frame whose interval passes this many ms (ADR-1980) |
| `AVGEN_SLIDER_FILTER` | restrict the `sliders` script arm to a path prefix |
| `AVGEN_UI_SELFTEST` | log SDL and ImGui input routing every thirtieth frame |
| `AVGEN_VIEWPORT_PROBE`, `AVGEN_VIEWPORT_DRAG` | synthesise a viewport click or drag |
| `AVGEN_NO_WARMUP` | skip the two warm-up frames before an offline render |
| `AVGEN_NO_TEMPORAL_PREROLL` | skip the temporal pre-roll: a render of a range then opens with an empty frame history instead of the history a render from the start had. A measurement arm only |
| `AVGEN_FFMPEG` | path to an ffmpeg executable |
| `AVGEN_NDI_LIB` | path to libndi |
| `AVGEN_QUALITY` | path to the Quality Lab CLI (`avgen_quality`) the live optimizer runs |
| `AVGEN_DAWN_TOGGLES` | `a,b`: enable Dawn device toggles (investigation only; `dump_shaders` with `--log debug` dumps MSL) |
| `AVGEN_PIPELINE_TIMING` | log every pipeline's creation time |
| `AVGEN_TEXTURE_SHARING` | `off`: every asset uploads its own copy of each image it references, as before ADR-2110 (the measurement arm); by default an image file is one texture however many assets reference it |
| `AVGEN_PIPELINE_CACHE` | `off` (or `0`): run without the persistent pipeline cache; `<dir>`: keep it there instead of `~/Library/Caches/avgen/pipeline-cache` (ADR-1811). Startup only: it never changes a frame |
| `AVGEN_SHADOW_STATS` | log the shadow views every thirtieth frame |
| `AVGEN_EDITOR_TRACE` | print the editor's per-frame inputs and what it made of them |
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
| `AVGEN_RENDER_INTERPRETED=1` | an offline render (`--render`, a render job) draws its material programs through the interpreter (before ADR-1812). In the headless benchmark the same comparison is the `--ab interpreted` arm |
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
| `AVGEN_NO_TILE_MOTION` | a tiles ground's bodies read the planning surface, not the song's recorded ground (before ADR-1998) |
| `AVGEN_THROW_ADR1998` | a crest throw at the swing's fastest rise, with no crouch (ADR-1998's timing, before ADR-2000) |
| `AVGEN_THROW_LANDSCAPE` | a crest throw read on the landscape alone, not the drawn tile (ADR-2001, before ADR-2002) |

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
