# Character perception and attention (Phase D §7–§11, §25–§28, §72)

## What a character knows

`GridPerception` (ADR-270/290) runs per character at `perception.hertz`, over two `PointGrid`
snapshots (interest points and bodies), and keeps the top `capacity` percepts by salience:

- **range** and **fieldOfView** (horizontal), with **proximityRange** inside which facing does not
  matter;
- **occlusion**, budgeted by `occlusionTestsPerSecond` (off by default: a sightline costs 1.8 ms on
  Glowmere, 180 ms a frame for 100 characters);
- **target categories**: per-kind `weights`, and per-tag `tags` (Phase D §9) so that a semantic
  class — a saucer — ranks above twelve nearer shore points in the capacity cut. Measured: without
  it the demo's warden dropped the saucer from its working set once a second and its watch
  flickered on and off.

Each percept carries its semantic tag mask (§25). Nothing downstream reads the world's interest list
directly (character_ai.hpp P4).

## What it hears (§9, §26–§28)

A `WorldEvent` is raised by any named action/interaction completion, and — for audio (§28) — by a
scene-level mapping from a signal-bus event (`"worldEvents": [{"name": "drop", "signal":
"music.drop", "at": "visitor"}]`). A character hears an event within its `radius` (scaled by the
decider's `hearing`), attenuated linearly, strictly after the step it was raised in. Behaviour
never reads a band level: the analyser's structure arrives as a named event.

## What it attends to (§10, §11)

`AttentionModel` scores every percept and remembered event as a sum of named terms — salience,
novelty, semantic (authored tag weights), movement, sound, relevance to the current behaviour —
scaled by confidence (freshness × visibility). Phase B's `chooseAttention` then applies the dwell,
margin, maximum hold and refractory period. The winner is the `AttentionFocus`: id, position, score,
**reason** (largest term), confidence, last seen.

§11's split: attention decides what matters; the decider *glances* by publishing the look target
only when nothing else named one this step; `lookAt` may then turn the body when idle; the aim layer
turns the head. So a character notices a sound and looks toward it while it keeps walking.

## Memory and novelty (§21, §22)

`ObjectMemory`: bounded entries (last seen, attention seconds with exponential recovery,
investigated time with quadratic recovery, failure time), bounded events (idempotent on sequence).
Novelty = recovery × exp(−attention / (habituation × attention-span)). Attention accumulates only
while the body is still, so a character does not arrive already bored. Eviction never drops an
entry inside its failure or recovery window.

## Determinism, threading, cost

- All of it is reconstructed by `seek`'s replay; nothing is persisted (D4). Ties break on subject id.
- Single-threaded, inside the entity step. Parallel perception (§45) is possible because
  `GridPerception` reads snapshots — but its scratch buffers are per-instance and it is not safe to
  call from two threads today. Not built; not needed at the measured cost.
- Attention is O(percepts + events) per step, ≤ 24 items.

## Limitations

- Hearing is not occluded; vision occlusion is budgeted and off by default.
- A body's "eye" is a stand-in height, not a socket.
- Signal-raised events do not replay under a scrub.
