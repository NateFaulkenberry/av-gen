# ADR-2129: an offset cue fades in from its own second

- Status: Accepted (2026-11-23), avagent/neon-dreams (Neon Dreams, iteration 20; owner msg 297 at 46.9 s: "land alien
  snaps directly from landing animation into another animation")
- Code: `seq/director.{hpp,cpp}` (`ResolvedCue::blendFromSeconds`, `resolveCue`, `applyAnimation`),
  `scene/composition.{hpp,cpp}` (`CompositionNode::animationBlendFrom`, `setNodeAnimation`'s `blendFrom`, the push
  into the rigs)
- Tests: `tests/unit/test_skeleton.cpp` ("a scene file declares an animated character end to end"),
  `tests/unit/test_clip_semantics.cpp` (the ADR-2129 section)

## Context

ADR-823 gave a sequencer cue an `offset`: the clip picks up `offset` clip-seconds in, so its phase origin is
`time - offset / speed`. The director handed the composition that ORIGIN as the request's second, and the composition
passed the same second to `AnimationPlayer::play` -- which starts the cross-fade there. A cue at 47.83 s with an
offset of 0.8 s and a 0.28 s blend therefore faded in over 47.03-47.31 s, before it existed: at 47.83 s the fade was
already complete and the rig cut from the old pose to the new one. Measured on Neon Dreams' landed flyers
(`avgen_foot_probe --node-joints`): a hand 63 cm in one frame (Landing -> ND_Cheer at 47.83 s, Landing -> ND_Sway at
45.03 s), against 1-10 cm a frame either side; a cue with no offset (ND_Sway -> ND_Arms_up) faded properly.

## Decision

The cross-fade starts at the cue's own second; the phase origin stays where ADR-823 puts it.
`ResolvedCue::blendFromSeconds` is the cue's time (or a one-shot's end, for its `then`), `setNodeAnimation` takes it
as an optional `blendFrom`, the node keeps it (`animationBlendFrom`, NaN = the request's second, as before), and the
push calls `play(state, blendFrom, ...)` before `restart(origin)` and `setSpeed(origin)`.

## Consequences

- Every offset cue now fades in over its blend instead of cutting. Cues without an offset, and every behaviour's
  request, are unchanged (`blendFrom` is their own second).
- A seek into a fade still fades from whatever the rig was in when seeked (unchanged; the player's stateless rule).
