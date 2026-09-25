# ADR-821: A clip is measured for what it is, and a cue says how to play it

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-540 (the pack is in place), ADR-546/547 (contacts and phase), ADR-337 (the travel
joint), ADR-758/820 (performances), ADR-089 (clip cues are applied, not baked); Director spec
§30–§31 (plan-time events)
**Implemented by:**
- `scene::clipSemantics`, `ClipSemanticsTable`, `ClipSemanticsCache`, `clipSemanticsJson`;
- `SkinnedRig::semantics()` and `Composition::clipSemanticsFor`;
- `AnimationPlayer::setLooping`;
- `seq::ClipPlayback`, `ClipCue::{playback, then}`, `seq::resolveCue`, `seq::clipEventSeconds`,
  `seq::clipLookupFor`;
- `avgen_motion semantics <glb> [--json] [--contacts]`.

**Tests:** `tests/unit/test_clip_semantics.cpp` (`[motion][semantics]`)

## Context

The Director's Slice 3 compiles "Rook jumps; the Umbra pulse fires at the peak". It needs to know,
for each clip a character carries:
- whether the clip loops, and how long it is;
- whether the body leaves the ground, and when it takes off, peaks and touches down;
- when each foot plants and lifts;
- whether the clip travels, and how fast;
- when it may be cut away from.

None of this is authored. glTF has no loop flag and no markers, and the alien pack carries neither.

The player's one rule, that every state loops (`addDefaultStates`), was right for a behaviour's walk
and wrong for a stunt.

The engine already measures most of this, but nothing combined it:
- `measureLoopClosure` (loop or not);
- `analyseClip` (contacts per foot, root travel, horizontal speed);
- the travel joint (ADR-337).

## Decision

### The table is derived, not declared

`clipSemantics(skeleton, clips)` builds each clip's row from the existing analysis:
- **loop:** from `measureLoopClosure`;
- **contact events:** plants and releases from `analyseClip`'s contact tracks;
- **root motion and speed:** from `analyseClip`'s travel reading;
- **the ground:** measured against **one datum for the whole rig**, the rest pose's lowest foot.

The contact detector's own datum is each clip's lowest point. That is right for "which foot is
planted" and wrong for "is anything on the ground". A fall loop's still feet read as two 100%
plants.

Support counts any extremity, not only the feet:
- A body counts as supported when a foot, a hand, the head or the hips is within 4% of its rest
  height of the datum. All four are found by the same role guess the feet use.
- Measured with feet alone, the three deaths came back as flights with a negative peak: the legs
  rise as the torso goes down.
- A run of unsupported samples shorter than 0.12 s is a stride, not a flight, so a run is
  grounded.

Each clip is then classified by its ground behaviour, and its events follow from that:

| Class | Rule | Events |
|---|---|---|
| **Grounded** | the body stays supported | plants and releases only |
| **Leaves** | there is a flight | `takeoff`, `peak` (highest body joint in the flight), `touchdown` |
| **Airborne** | supported for less than 35% of the clip | no `peak`: a fall loop's highest sample is a bob, not an event |

For the **Leaves** events:
- A flight that runs off either end of a one-shot omits its takeoff or its touchdown rather than
  inventing one. `Landing` opens in the air, so it has a touchdown and no takeoff.
- A one-shot's flight is **committed**: it cannot be interrupted. A loop has no committed window.

Events are in clip seconds from the clip's start. On the timeline an event falls at
`cue time + event / speed`.

### Computed once per asset, on first request

The glTF loader gives each loaded rig a shared `ClipSemanticsCache`. Every node that instances that
rig shares it, so a scene that never asks pays nothing.

Measured cost: `avgen_motion semantics` runs over the scout's 26 clips in 0.38 s of wall clock
under load, which includes the process start and the glTF load.

### A cue says how its clip plays

`ClipCue` gains two fields:

- **`playback`:**
  - `auto`, the default, asks the table;
  - `loop` and `once` override it.
- **`then`** takes effect when a one-shot ends, at `time + length / speed`:
  - empty: hold the last frame;
  - `"gait"`: hand the rig back to a performer's gait (ADR-758/820). An actor that performs no
    entity holds instead;
  - a state name: play that state next, by the same rules.

One pure function, `seq::resolveCue`, answers "what does this cue play at t". Two things use it:
- `applyAnimation`, which pushes the loop override through `setNodeAnimation` to a per-play
  `AnimationPlayer::setLooping`;
- a performer's clip ownership.

A clip that nothing measured keeps the state's own default. Its `then` never fires, because a
length that isn't known gives no end time.

### Autonomous clips are unchanged

The loop override is **per play**. A behaviour's request passes none, so every entity's clips loop
exactly as before.

Applying one-shot semantics to the aliens' own clips would change Glowmere's look. For example,
`react` plays `Crazy`, which measures as a one-shot, so it would hold its last frame instead of
repeating. That decision waits on the owner's review render.

## Measured on the scout

`avgen_motion semantics assets/aliens/alien-scout.glb` gives the following.

| Clip | Loop | Class | Events |
|---|---|---|---|
| `Jumping` | yes (it closes) | Leaves | takeoff 0.567 s, peak 0.767 s (+0.42), touchdown 1.100 s |
| `Jump_running` | yes | Leaves | takeoff 0.067 s, peak 0.367 s (+0.76), touchdown 0.667 s |
| `Landing` | no | Leaves | touchdown 0.267 s, no takeoff |
| `Fall_loop`, `Floating`, `Flying_jet` | yes | Airborne | none |
| `Crazy`, the deaths | no | Grounded | plants and releases only |
| `Running`, `Walking` | yes | Grounded | plants and releases only |

The test checks these events against the pose, **sampled independently**:
- mid-flight, both feet are more than 0.1 off the ground;
- the body joint is at its highest of the whole clip at `peak`.

## Consequences

- The two jump clips measure as loops, because their ends join their starts. A stunt therefore
  says `playback: once`, as the Director plans to (`ClipCue{once, then: gait}`).
- The table is the truthful source for the Director's capability card: its `loops` field, and each
  clip's events.
- `interruptibleAt` is a start. It knows flights and nothing else. A reach or a sit that must not
  be cut is not yet detected, because contacts are only read for the feet.
