# ADR-1100: The project's live block holds the target, profile, minimum, priority and Optimize's ceilings

**Status:** Accepted (live optimizer, Stage 3/4). **Date:** 2026-10-03

`"live": {"qualityStrategy", "targetFps", "profile", "minimumLevel", "priority": [...], "overrides": {...}}` extends
ADR-1084's block. Every field is optional and written only when stated (a project that never chose round-trips
unchanged); absent fields are unset, never inherited from the last project. `Engine::liveSettings()` holds them with a
revision counter; `Application::serviceLiveQuality` recomputes the ladder's base when it moves: the tier, then the
profile (ADR-1099), then `overrides` (ceilings, `applyCeiling`), then the level. The target is the run's flag, else the
project's, else the machine's setting. Nothing in the block edits the scene.

The Live panel shows and sets the profile and the lowest level, shows every lever as the frame uses it, and "Save live
profile" writes the target, profile and strategy into the project. The Performance panel gained a Live performance
section: target, budget, headroom; a Frame / GPU / CPU graph over 5-30 s with the budget line, spikes and over-budget
frames; GPU time by the profiler's categories (rolling medians); a resource inspector (shadows, particles, post,
geometry, memory on request). Both are visually unverified (agents cannot see ImGui).
