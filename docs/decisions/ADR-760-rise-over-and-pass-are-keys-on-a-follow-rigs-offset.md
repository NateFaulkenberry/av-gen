# ADR-760: `rise_over` and `pass` are keys on a follow rig's offset

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-245 (camera channels), ADR-751/752 (camera direction, undo), ADR-756 (validator),
ADR-759 (performances); spec §17 (shot tracks), §21 (camera moves)
**Implemented by:** `CameraChannels::followOffset` / `aimOffset` and `evaluateAuthoredCamera` in
`src/scene/composition.*`; `capturedCameraDirection` in `src/ui/edit_history.cpp`; the offset track
in `compilePlan` and `CameraSupport::KeyedOffset` in `src/directing/`; `installCompilation` in
`src/app/directing_context.hpp`
**Tests:** `tests/unit/test_directing_performance.cpp` ("a chase that rises over ...", "revising a
chase's distance ..."); the benchmark case in `tests/unit/test_directing_compile.cpp`

## Context

The benchmark shot is "a low-angle chase that rises over him and passes ahead". Chase already
compiled to a follow rig (a camera standing at a node plus `followOffset`). Rising and passing are
that same camera with the offset changing over the shot. The offset was a plain struct field, so
nothing could key it, and both moves reported `UNSUPPORTED`.

The two alternatives both lose editability:
- Baking the move to per-frame camera keys gives forty-eight opaque keys that stop following Rook
  the moment his path is revised.
- A new "camera behaviour" type in motion or entity code sits outside this program's ownership.

## Decision

- **Every authored camera that follows a node gets a `cameras/<slug>/followOffset` channel.** Every
  camera that aims at a node gets `cameras/<slug>/aimOffset`. They are ordinary parameters, as in
  ADR-245: keyable, routable and saved. They are registered only for rigs that follow or aim, so
  no other camera gains parameters. The evaluator reads the channel when it exists and the struct
  field otherwise.
- **The compiler writes `rise_over` / `pass` as one shot track** (spec §17: shot-relative times,
  EaseInOut) on the chase rig's `followOffset`:
  - It starts with a key at the cut, holding the chase offset.
  - `rise_over` holds until `at`, then moves over about 1 s to `(0, max(4, 2h+2), -0.5)`.
    `heightMetres` overrides the height.
  - `pass` moves over about 1 s to `(side, max(1.5, h), |d|+2)`. `distanceMetres` and
    `side` (`left` / `right`) override the defaults.
  - A beat with no `at` falls at 1/3 or 2/3 of the shot.
  - Each move arrives no later than the next beat starts.

  The result is three or four keys a person can drag, on the same camera, still following Rook.
- **Support is conditional.** These moves are supported only after a chase or follow on a
  character in the same shot (`KeyedOffset`). Otherwise they are `UNSUPPORTED`, saying why:
  - over a place, which needs a framing move's elevation and is not compiled;
  - with no follow before them.
- **Install writes the offsets.** `ParameterSet::add` keeps an existing parameter's value. Without
  this write, a rig revised in place (a new chase distance) would silently keep the old offset:
  the test for this failed before the fix.
  - The write happens inside the apply capture, so undo restores the old base.
  - Cameras now install before the sequence, so the shot track binds to a channel that already
    exists. The old order also worked, but only because `setCameraDirection` rebinds the whole
    timeline.

## Consequences

- No `src/entity/` or motion code is touched. The camera reads the node's transform, which is
  whatever the performance put there.
- **Measured, played from zero (not by seek) on the benchmark scene**, in Rook's frame:
  - chase: 4.8 m behind, 0.46 m up;
  - over: 0.47 m behind, 4.0 m up;
  - pass: 5.1 m ahead.

  Breaking the evaluator's read of the channel turns all three checks red.
- A hand edit of the rig's `followOffset` base should be visible to fingerprints, because the
  capture reads bases. A revision should therefore report it as `HAND_EDITED` rather than
  overwrite it. This is not covered by a test yet.
- Not done:
  - `aimOffset` has a channel but nothing compiles keys onto it yet.
  - Rising over a *place* is not compiled.
