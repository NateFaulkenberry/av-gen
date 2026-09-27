# ADR-913: Camera stability is measured from a pose track, not a render

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 camera audit, which had to rebuild every rig from a 20 Hz cast trace in Python
to measure the wobble, because nothing in the engine could report where the camera was.
**Follows:** ADR-911 (the subject reference these numbers tune), the GV3 revision brief section 2
("use temporal evaluator analysis to identify excessive camera-frequency changes")
**Implemented by:** `avgen_cast_trace --camera` and `--start` (`tools/cast_trace.cpp`, which
documents the JSON); `tools/camera_stability.py`
**Tests:** the instrument is exercised by the measurements below; the claims it measures are tested
in `tests/unit/test_follow_camera.cpp` (ADR-911)

## Context

A wobbling camera is a property of a sequence of frames. A still cannot show it, and a render is the
slowest and least precise way to see it: the picture mixes the camera's motion with everything
else's. The audit worked round the gap by re-implementing the rigs from the cast trace, which
measures a model of the camera, not the camera -- and which could not have measured this ADR's
changes at all without being rewritten with them.

## Decision

**The engine reports its camera.** `avgen_cast_trace --camera` records, at every frame of the
render's fps and after `Engine::update` -- resolved, evaluated, shaken, framed, on the lens the frame
is projected with -- the eye, the target, the vertical field of view, the shot on screen (its index
in `cameraDirection.shots`), the active camera and why it has the frame, the blend, and the active
rig's raw aim point and follow point. The JSON is documented at the top of `tools/cast_trace.cpp`.
`--start S` seeks first, as a render of a range does, so a scrub can be compared with a play frame by
frame.

**One evaluator reads it:** `tools/camera_stability.py`, per shot:

| metric | definition | pass bar |
|---|---|---|
| pitch HF, yaw HF | RMS of the view angle minus its zero-phase Gaussian low-pass (sigma 0.3 s, -3 dB near 0.44 Hz), degrees | <= 0.1 |
| eye vertical HF | the same residual of the eye's height, cm | <= 2 |
| subject off-centre max | the aim point (raw, plus the aim offset) projected through the frame, % of the frame (16:9) | <= 20 |
| travel | the eye's horizontal path over the follow point's, both on a 20 Hz subsample | about 1 |

The bar is the audit's. It reads jitter, not composition: a deliberate tilt with a subject that is
itself accelerating also registers as "HF" over a shot shorter than a second (s29, below).

## Consequences

- **GV3, before and after ADR-911** (the table is ADR-911's): before, 10 of the 11 follow shots failed
  the bar, on the nod (s13, s18, s19, s20, s38: up to 1.22 degrees) or the bob (up to 11.2 cm);
  after, with the values below, all eleven pass: pitch HF at most 0.040 degrees, eye vertical HF
  0.6-2.0 cm (s19 closest, at 1.99), the subject at most 13.6% off-centre, travel 1.00-1.08.
- **The cuts, on the film itself.** Rendering the frames around three of the cuts the audit
  measured (18.92, 22.62 and 37.39 s; 960x540, the project's offline tier), the first frame of each
  new shot now has 99.7%, 103% and 100% of the sharpness of the two frames after it (variance of
  the Laplacian), against 6% at all three in the delivered final (ADR-912).
- **A seek is measurable too.** The same trace from `--start 123` against the played one: the old
  engine's s19 eye was 0.92-0.98 m off (its lag ran un-lagged after a seek), this engine's 10 um.
  And a seeked window reproduces the full film's numbers exactly (s18-s20 from `--start 101`:
  identical to three decimals), so a shot can be re-measured by tracing its own window instead of
  the whole film (the seek itself takes seconds; the full film is a 25-minute run on a quiet machine).
- **It found what a model could not:** the ground under Vane in s19 carries 2.3 cm of relief at
  walking frequencies, which a camera that follows the ground inherits (ADR-911's footprint was
  widened to 2.5 m for it); and s29's "nod" is a horse being lifted into the saucer at 5 m/s through
  an 85 mm lens -- a tilt the shot is about, which the metric cannot tell from wobble in 0.9 s.
- **The same numbers at every resolution.** Every metric is an angle, a length or a fraction of the
  frame, and every knob is in seconds or metres, so the 960x540 previews and the 1080p and 4K finals
  measure and tune alike.

**Recommended values** (the coordinator applies them; GV3's files are not this ADR's):

| kind of shot | GV3 shots | followSmoothSeconds | followVerticalSmoothSeconds | followLead | followGround | notes |
|---|---|---|---|---|---|---|
| world-offset follow of a walking character | s03, s09, s13, s18, s19, s20, s28, s36, s38 | 0.3 | 1.0 | 1 | on | remove `followLagSeconds` (s13, s18, s19, s38): with the shared reference a lag only re-introduces a nod |
| follow with a fixed target | s25 | 0.3 | 1.2 | 1 | off | raise `followOffset` y from 3.6 to 4.8: at 3.6 the eye rides the 1.2 m floor and traces the terrain under itself |
| orbit on an idle or static subject | s10, s17 | 0 | 0 | 0 | off | nothing to smooth; the walker values on s10 are harmless |
| fixed eye, aim at the flying saucer | s24, s27 | 0.6 | 1.0 | 0 | off | s27 yaw HF 0.153 -> 0.091 degrees, subject within 0.6% of centre; s24 0.024 -> 0.022, within 2.2%; never `followGround` on a craft |
| fixed eye, aim at a walking animal | s05 | 0 | 0 | 0 | off | already inside the bar (0.036 / 0.067 degrees) |
| the abduction close-up | s29 | 0 | 0 | 0 | off | the subject's rise is the shot: a 1 s vertical constant left the horse 74% of the frame off-centre |
| a compiled Director chase | (none in GV3) | 0.3 | 0.8 | 1 | on | `followHeadingSmoothSeconds` 1.2, no lag: the compiler's defaults |
