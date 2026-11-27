# ADR-2231: a named camera is a reproducible input

- Status: Accepted (2026-11-27), eng/115-labs (Parallel Engine Programme, Agent 4; Stage A report
  `~/Desktop/av-gen-review/115-parallel-engine/10-stage-a/04-test-labs/REPORT.md` §2.4)
- Code: `app/render_settings.{hpp,cpp}` (`RenderCamera`, `parseCameraPose`, `renderCameraFlags`),
  `app/engine.{hpp,cpp}` (`Engine::pinCamera`, `cameraNames`), `app/render_job.cpp`, `app/application.cpp`
  (`--camera`, `--camera-pose`, the lab case and live-profile mappings), `app/live_profile_run.cpp`,
  `labs/case.{hpp,cpp}` (`LabCase::cameraName`, `reproduceCommand`)
- Tests: `tests/integration/test_render_camera.cpp` (`[adr2231]`)

## Context

Every lab needs deterministic cameras, and nothing could select one:

- A lab case's `camera {eye, aim}` was parsed and saved, and applied by nothing. `--lab-case` copied every other
  field into the options and skipped it; `reproduceCommand` did not print it. The 15 shipped case cameras (hdr 4,
  lighting 4, lod 4, shadow 3) reproduced through the fixture's own camera.
- `--live-profile --camera <name>` wrote the name into its record and selected nothing.
- Neither the CLI nor a render job could look through one of a composition's authored cameras
  (`cameraDirection.cameras`, ADR-245) while the director's shots gave the frame to another.

## Decision

1. **`RenderCamera`** is a value: `Film` (the director's answer, the default), `Named` (an authored camera by display
   name, else by slug) or `Pose` (a fixed eye and aim; field of view 0 keeps the film's).
2. **`Engine::pinCamera`** applies it through the composition's viewport view, the mechanism ADR-391 already has:
   `Named` is `ViewportCamera::Through` (the rig's own evaluated pose and lens every frame, so a keyed or following
   camera still moves, whatever the shots say), `Pose` is `ViewportCamera::Editor` with that pose, `Film` is `Film`.
   The resolver stays pure and `activeCamera()` still reports the film's camera: the pin is what the frame looks
   through, not a rewritten cut. An unknown name, or a scene with no composition, is refused and changes nothing; the
   error lists the cameras.
3. **One input, every path that draws.** `RenderSettings::camera`, applied by `RenderJob::start` before the warm-up
   and the history, so `--render`, the queue and the engine host's render jobs all honour it. The session's own
   engine takes it at start-up for the headless loops, the live profile and the editor's viewport. `--camera <name>`
   and `--camera-pose ex,ey,ez,ax,ay,az[,fov]` are the flags; a lab case's camera (a name, or `{eye, aim}`) maps to
   them, and a flag typed beside the case wins.
4. **The profiler records what it looked through** (`describeRenderCamera`), and refuses `--mode live` with a camera
   unless `--projection off`, because the projection output always shows the film (ADR-391).
5. **A render job's camera is not project data.** It is not written to the project's `render` block, like `--range`.
6. **The reproduce command reproduces**: it prints the camera flag and a `--render renders/labs/<lab>-<n>` output
   (before, a pasted case command drew one frame and wrote nothing).
   And `--lab-case <c> --render` renders the case's one frame at its time, as that command does: it used to render
   the fixture's whole default range from 0 (600 frames for a 10 s lab at 60 fps), because the case time was only a
   seek for the editor. An explicit `--range` still wins.
7. **The API half is ADR-2206's** (Agent 1, after API level 3): `render.capture` / `render.start` gain
   `camera: "shot" | "viewport" | {name} | {pose}`, filled into `RenderSettings::camera`; `{name}` resolves through
   `Engine::pinCamera`, so the CLI and the API cannot disagree about a name.

## Consequences

- ADR-391's "the film is the only value any render path sees" now reads "unless the render names a camera"; a render
  that names none is byte-identical to before.
- The 15 existing case cameras now apply: those cases' reproductions change picture, deliberately. The md5s before and
  after are in the merge note.
- A Film render is unchanged, which the Neon Dreams, Liminal, Cel Dream and Audio Response Lab hashes check.
