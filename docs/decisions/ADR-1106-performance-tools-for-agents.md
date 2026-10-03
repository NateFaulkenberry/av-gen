# ADR-1106: Agents profile with performance.profile_scene and read/set live quality

**Status:** Accepted (live optimizer, Stage 1j). **Date:** 2026-10-03

- `performance.profile_scene` writes a scratch copy of the project on the main thread and runs this executable's
  `--live-profile` on it in a child process on a thread of its own, the way `director.evaluate` runs the Critic; the
  record (avgen.liveprofile/1) is the answer. It says the GPU was shared with the running editor. Without the host's
  hook it reports itself unavailable: the editor does not depend on the profiler, and the profiler does not depend on
  the Critic.
- `performance.get_live_quality` / `performance.set_live_quality` read and change the project's live block (ADR-1100),
  including adding levers as ceilings (what Optimize's Apply does). They never edit the scene.
- `tools/live_scene_profile.py` is the shell-side wrapper: it takes the GPU lock, prints the report and the record's
  path.
