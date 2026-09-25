# ADR-769: A still is critiqued by arithmetic, not by a model

**Status:** Accepted
**Date:** 2026-09-25
**Related:** ADR-764 (preview stills); spec §55 ("optional vision critique"); §38 (no test depends on a
real model)

**Implemented by:**
- `app::Framing` and `app::frameSubject`;
- the critique in `renderShotStills` and `StillsSession`;
- the framing note under each still in the panel.

**Tests:**
- the pure framing case in `tests/rendering/test_director_stills_gpu.cpp`;
- the benchmark still's framing check.

## Decision

- **What the spec's optional "vision critique" asks** is whether a proposed shot shows what it is
  about. The part of that which matters most can be answered exactly: where the shot's subject is
  in the film camera's frame at the still's instant, and how much of the frame it fills.
- **Every still carries a `Framing`**, computed from the camera matrices the frame was rendered with
  and the subject's position at that instant:
  - a character's body (2 m tall, centred 1 m up), or
  - a place's anchor (a hero at its declared height).

  A problem produces a note, and the note is shown under the still:
  - behind the camera;
  - outside the frame, and on which side;
  - in frame but under 4% of the frame's height ("too small to read").
- **No model is consulted.** A vision model's opinion of taste stays optional and is not built. It
  would need a provider at test time, which §38 forbids, and it would say less than this does about
  the question this answers.

## Consequences

- **Measured on the benchmark:**
  - Rook's chase in the benchmark proposal: in frame at (0.00, −0.13), 6.0 m away.
  - A shot of Rook on UFO Watch's camera at 0:14: 127 m away and 2.3% of the frame's height,
    flagged "too small to read" under its still.
  - The chase at 0:20: 13.7 m away, in frame.
- **Proven red:** moving the subject point 40 m sideways fails the in-frame check.
- **Not covered:** occlusion. A subject hidden behind a mushroom is in frame by this measure. Finding
  it needs a depth readback, which is not done.
