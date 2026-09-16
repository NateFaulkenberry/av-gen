# Render Quality Lab

An engineering instrument for objective, repeatable, machine-readable analysis of AV Gen's rendered
output. **Phases 0 and 1 (reconnaissance and research) are complete; nothing is implemented.**

Read in this order:

| document | what it settles |
|---|---|
| [repository-reconnaissance.md](repository-reconnaissance.md) | what already exists — and it is most of the plumbing and none of the instruments |
| [research.md](research.md) | every technique considered, the §47 thirteen-question comparison table, and what was rejected with the reason |
| [architecture.md](architecture.md) | the proposed system: a native C++ tool outside the engine, every external dependency optional |
| [metrics.md](metrics.md) | the quality vector, each field's definition, and what each number may **not** be used to conclude |
| [artifact-detection.md](artifact-detection.md) | the detectors, their inputs, their failure modes, and their control arms |
| [reference-rendering.md](reference-rendering.md) | what a reference is, the four things it is not, and the measured defect on its path |
| [benchmark-scenes.md](benchmark-scenes.md) | what exists, what to build, and what must not be optimized against |
| [experiments.md](experiments.md) | the unit of work, the non-vacuity check, and how cost is reported honestly |

Decisions: **[ADR-250](../decisions/ADR-250-the-instrument-is-not-the-engine.md)** (the instrument is
not the engine) and **[ADR-251](../decisions/ADR-251-the-supersampled-exr-is-a-corner-of-the-frame.md)**
(the supersampled EXR is a corner of the frame).

## The three things that shaped it

1. **ADR-243.** A correctly-implemented temporal detector was measured *anti-correlated* with a human
   reviewer on the artifact viewers actually report. Every design choice here is downstream of that:
   a vector rather than a score, metrics named after what they compute, a spatial measure beside
   every temporal one, and a human gate placed early rather than at the end.
2. **The machine has no ffmpeg, no libvmaf, no numpy and no OpenCV**, and every Python tool in this
   repo is stdlib-only. The metric engine is therefore native C++ linking `avgen_core`, which already
   has tinyexr, stb and the image types. Every external tool is optional and degrades to
   *"unavailable"* with a reason.
3. **ADR-242's refusal did not need relaxing.** The candidate carries AOVs at native resolution; the
   reference carries supersampling and no AOVs. Nothing needs an AOV at supersampled resolution.

## Human decisions this phase collected, and did not take

Listed in full at [research.md §10](research.md#10-unresolved-questions-carried-into-the-next-phase):
whether to install ffmpeg/libvmaf; whether the engine gets a shadow AOV; the human-validation
protocol and its reviewer budget; and whether the ADR-251 readback defect is fixed by refusing the
combination or by resolving it properly.
