# ADR-1084: Which lever a project gives up first is project data: `live.qualityStrategy`

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §6-7).
**Date:** 2026-10-03

## Context

The measured scenes have different cost structures (`evidence-live-projection-2026-10-02.md` §D): Liminal's GPU frame
is 97% per-pixel, Glowmere's 86% with a volume march and motion blur that are large at every size, and Sonic's only
about half, with vertex work and shadows fixed. Sonic's thin rings also bead below 0.71 (ADR-1024). One order cannot be
right for all three.

## Decision

1. **A project key**, `"live": {"qualityStrategy": "resolution_first" | "balanced" | "effects_first"}`, read by
   `Engine::loadProject` into `Engine::liveQualityStrategy()` and written back by `projectDocument` only when the
   project stated it. Absent means `balanced`, not the last project's. An unknown value warns and uses `balanced`.
2. **Each strategy is a ladder** (`app::liveQualityLadder`), the five levels of ADR-1083 in a different order:
   - `resolution_first`: 1.0, 0.85, 0.71 + quarter-resolution volumes, 0.5 + half steps and no motion blur, DoF or
     third cascade, 0.38 + quarter steps and plain shadows. The effects are kept until the scale has done its work.
   - `balanced`: 1.0, 0.85 + quarter volumes, 0.71 + no motion blur, 0.5 + half steps, no DoF, two cascades, 0.42 +
     quarter steps and plain shadows.
   - `effects_first`: 1.0 + quarter volumes, no DoF, two cascades; 0.85 + no motion blur, half steps, plain shadows;
     0.71; 0.5 + quarter steps. The picture stays sharp longest.
3. **The three baseline projects state theirs:** `examples/liminal/all-you-got.json` resolution_first,
   `examples/world/glowmere-valley-2.json` balanced, `examples/sonic-garden/sonic-live.json` effects_first.
4. **No code names a scene.** The controller is told a strategy; it never sees a project's name.
5. A strategy change (a project load) restarts the controller at Ultra and forgets what it learned (ADR-1085).

## Alternatives considered

- **Automatic scene analysis** (fit the cost curve at runtime and choose). The brief's §7 forbids it for now; the
  controller's learned step ratios (ADR-1085) are the small part of that idea worth having.
- **Per-project ladders in full** (every value in the project file). A large surface for authors with no measured
  need; three named orders cover the measured scenes.
- **A machine setting.** The order follows from what the scene costs, which is the project's property.

## Consequences

- A project without the key behaves as `balanced`, close to the old resolution-only controller for its first rungs.
- The Live panel's status tooltip says which order is in force and whether the project chose it.

## Revisit triggers

- A fourth cost shape (e.g. a CPU-heavy scene where no GPU lever helps) that none of the three fits.
