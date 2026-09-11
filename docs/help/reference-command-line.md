---
id: reference/command-line
title: Command Line
category: Reference
summary: Every flag, every AVGEN environment variable, and the exit codes.
order: 92
audience: expert
tags: cli, command line, flags, headless, environment, arguments
keywords: command line options; headless render; how do i render from the terminal; avgen flags; environment variables
related: rendering/offline-render, performance/instruments, performance/diagnosis
features: subsystem.cli
---

# Command Line

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
| `--input [name]` | analyse a live capture device; the name is optional |
| `--osc-port <n>` | override the project's OSC port |
| `--list-audio-devices`, `--list-midi` | enumerate and exit |
| `--play` | start playing immediately |
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
| `--ui-script <arms>` | drive the editor repeatably: `hover`, `sliders`, `panels`, `select`, `scrub`, `camera`, `tabs`, or `idle` or `all` |
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
