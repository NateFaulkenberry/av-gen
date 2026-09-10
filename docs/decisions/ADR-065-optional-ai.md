# ADR-065: AI is optional, replaceable, and never in the frame loop

Status: Accepted

## Context

The upgrade brief asks for AI as an augmentation: assets, materials, depth, segmentation, offline
VFX — while deterministic realtime rendering stays the foundation. The failure mode it names is AI
becoming an uncontrollable authority over scene structure, and the failure mode it does not name is
the one this repository would actually hit first: a large binary dependency added before anything
consumes it.

## Decision

**Three rules, enforced by shape.**

There is no synchronous "run this now" entry point, so inference cannot be called from the render
or audio thread even by accident. It goes through the job system; the renderer consumes whatever
result last finished.

`loadModel` returning an error is an ordinary state. A build with no backend able to read a model
file simply has no such model and the caller carries on. An optional failure must never become a
total failure.

`InferenceBackend` is an interface. ONNX Runtime, Core ML or anything else is one implementation
chosen at load time, and nothing above that line names one.

**No third-party inference backend is bundled.** What ships is the interface, the registry, the
cache, the budget, and one built-in analytic backend. That backend is *not a stub*: its depth is a
real luminance-and-gradient cue, its segmentation a real threshold against the image's own mean, its
embedding a real descriptor that tells two images apart. It is enough to build and test the whole
offline pipeline end to end without a download, a licence or a network — and it is described
everywhere as analytic, never as a learned model's output.

Adding ONNX Runtime is now a contained change: implement the interface, register it before the
built-in, done. Doing it *first* would have been buying a dependency for no consumer.

**The cache keys on model identity, version, parameters and input.** Leaving the version out is the
classic way to serve last week's model's answers after an upgrade. Entries are written beside and
renamed, so a crash mid-write cannot leave a half-entry that later reads as a hit; a truncated
entry is a miss rather than a poisoned render.

**The realtime budget defaults to off.** A default that quietly spends frame time is the one thing
a realtime engine must not ship with. When enabled it runs at most every N frames and skips
entirely if the last run overran: a frame that waits for inference is a dropped frame, and an
engine that drops frames unpredictably is worse than one that never inferred at all.

## Consequences

Inference is deterministic, which it has to be for the offline pipeline to remain a deterministic
render target — the whole point of the engine owning reality and AI only decorating it.

`Tensor` is deliberately small: NHWC float32, because every task in scope takes an image and returns
an image or a short vector. A general tensor library would be a dependency bought for no use.

Generative image synthesis is not in `ModelTask`. It belongs in an offline job that produces an
asset, not in a per-frame service.
