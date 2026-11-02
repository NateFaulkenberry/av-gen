# ADR-1937: The feedback has a centre

- Status: Accepted (CHRONOS Gate S2, 2026-11-02), scene/chronos.
- Amends: ADR-1066, the feedback (zoom, turn, drift and hue as an FIR over the clean ring).
- Code:
  - `FeedbackSettings::centerX`/`centerY` (`temporal/feedback/centerX|centerY`, uv, written to JSON only when moved),
    in `src/scene/temporal_settings.{hpp,cpp}`;
  - the centre passed in `more.xy`, in `src/rendering/temporal_effects.cpp`;
  - `fs_feedback`, in `shaders/temporal.wgsl`.
- Tests:
  - `[gpu][temporal][adr1937]` in `tests/rendering/test_temporal_gate_gpu.cpp`;
  - `[temporal][adr1937]` in `tests/unit/test_temporal_gate.cpp`.

## Context

CHRONOS pours its feedback into the singularity's core (brief §5F). The core wanders the frame, so the feedback
cannot stay pinned to the middle of the screen: zooming and turning about the frame's centre read as a generic tunnel.

## Decision

1. **A centre.** The feedback zooms and turns about (`centerX`, `centerY`), in uv with y down. It is (0.5, 0.5) by
   default, which is byte-identical to before, so every existing scene renders as it did and saves as it did. The
   centre is routable and keyable, so it can follow a moving subject.

## Consequences

- **Tests.** The default centre is byte-identical to a feedback with no centre set, and a moved centre is a different
  picture. The feedback is FIR: a renderer started 20 frames early lands on the same frame. The 10-tap test uses a
  20-frame lead, because a fresh renderer's first frames carry the 1-LSB warm-up effect (ADR-1932) into the ring, and
  a 12-frame lead read one of them.
- **What this ADR is not about.** A black-frame fault found during this gate was first suspected in the feedback's
  hue term; a bisect showed it was not. It was CHRONOS's own field: Metal's `tanh` overflowed. That is fixed in the
  scene (`examples/chronos/singularity/field.wgsl`), and the feedback is unchanged apart from its centre.
