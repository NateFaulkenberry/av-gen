# ADR-1360: The Live panel's Target is the one live frame-rate target

- Status: Accepted (2026-10-14), the owner's request
- Supersedes: the project frame-rate target of ADR-1100 (`live.targetFps`). The rest of ADR-1100's live block stays.
- Code: `src/app/interactive_resolution.hpp` (`LiveProjectSettings`), `src/app/engine.cpp` (the `live` block's load
  and save), `src/app/application.cpp` (`liveQualitySettings`), `src/app/live_performance_host.cpp` (Save live
  profile), `src/ai/engine_tools.cpp` (`performance.get_live_quality` / `set_live_quality`).
- Tests: `tests/integration/test_project_system.cpp` ("A project's live.targetFps is not read, and says so").

## Context

The target the live quality controller holds was chosen in this order: `--live-target`, then the project's
`live.targetFps`, then the Live panel's Target. Three live projects stated 60: Astral Forge Live, Digital Mosh Live
and THE RIFT LIVE. So choosing 30 fps on the panel (ADR-1341) did nothing in exactly the heaviest scenes, and the
panel gave no sign of it. The owner: "can we make all live scenes adhere to the expected settings in the live panel".

The panel's other quality settings already apply. Profile and Lowest edit the open project's own live block, and
Quality and Cap at target are machine settings with no project override.

## Decision

- **There is no project frame-rate target.** The live target is `--live-target` for a run, else the Live panel's
  Target, for every scene.
- **The field is gone, not shimmed (ADR-441).** `live.targetFps` is removed from the seven projects that stated it
  and from the two generators that wrote it (`examples/bioluminescent/build.py`, `examples/digital-mosh/build.py`).
  A project that still states it loads with a warning naming this ADR, rather than having its number ignored without
  a word.
- **"Save live profile" no longer writes a target.** The AI tools' live-quality read and write no longer carry one.

## Consequences

- Whatever the panel says is what every live scene holds. A heavy scene played at 30 is held to the 30 fps budget
  (29.3 ms GPU), not to 60.
- A project can still state its profile, lowest level, priority, strategy and Optimize ceilings: how to degrade, not
  how fast to run.
