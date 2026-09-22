# ADR-623: The matcher is wired, opt-in per character, with the clip provider behind it

**Status:** Accepted
**Date:** 2026-09-21
**Supersedes:** ADR-615's "staged and dark" ruling, **for the fallback seam only** (the matcher and
the chain it sits on). ADR-615's other rows (the motion controller, vector intent, the trajectory
seam, root-motion adaptation, trajectory prediction) are unchanged and stay dark.
**Decision owner:** the project owner, delegated to the coordinator on 21 Sep ("whatever this
fallback decision is just go with your best recommended"). The coordinator ruled: wire it, opt-in
per character, default off.
**Related:** ADR-541 (the provider seam and its fallback corollary), ADR-556 (advance and pose),
ADR-360 (scrub equals play), ADR-618 (a key a save drops), ADR-650 (one database per skeleton),
Phase C §35, §64, §65, §66, §91, §92, §94

---

## The decision

A character opts in with one scene key:

```json
"motionMatching": { "joints": ["foot.l", "foot.r", "head.x"],
                    "contacts": ["foot.l", "foot.r"],
                    "trajectory": [0.2, 0.4, 0.6] }
```

The key installs `MatchMotionProvider` in front of `ClipMotionProvider` on that body's chain. The
clip provider stays at the back, as the fallback for every failure (ADR-541, corollary 1).

- `joints` is required. Which joints carry a character's identity is a property of the character
  (§8).
- `contacts` defaults to `joints`. `trajectory` defaults to the three horizons §24 validated.
- The key implies `proceduralMotion`, because providers only run on that path.
- A malformed block is a **load error**, not a silent no-op.

The database is built at load from the rig's own clips, held in memory, and never written. It is
shared by every body on the same skeleton and config (ADR-650). A body whose database cannot be
built logs a warning and runs on the clip provider alone.

**Default off, and no shipping scene has the key.** Glowmere stays on the clip provider until the
owner has seen a matcher-driven alien and approved it.

## The fallback, and the failure each case exists for

Each case is a test in `tests/unit/test_motion_matching_wired.cpp` that shows the chain landing on
the clip provider. A control case with a healthy matcher shows the chain does *not* fall through.

| §35 failure | how the matcher declines | falls to |
|---|---|---|
| database unavailable | `NotReady` (no database) | clip provider |
| empty database | `NotReady` | clip provider |
| all candidates filtered | `NoContent` (every sample airborne) | clip provider |
| skeleton mismatch | `NotReady`: the database digest is not the rig's (`setExpectedSkeleton`) | clip provider |
| invalid query | `Unsupported`: the intent or the facing is not finite | clip provider |
| corrupted database | refused on load by `readMotionDatabase` (anim-cinfra), so no database | clip provider |
| database swapped in flight | **not a fallback**: the matcher is healthy and re-selects in the new database, and a stale memory never poses | the matcher |

## Four defects the wiring found, fixed here

1. **A fallback read the matcher's memory.** A memory's `selection` indexes its own provider's
   space: a database sample for the matcher, a clip for the clip player. `MotionChain` handed
   whatever memory it had to every provider, so a fallback read sample 20 as clip 20, and a
   matcher coming back read a clip index as a sample. The chain now gives a provider its own memory
   or a fresh one, never another provider's.
2. **A seek did not replay provider memory at all.** `EntityWorld::seek` advanced the provider once,
   after the replay, from a reset memory, at the target time, although its comment said "on every
   replay step". Every scrubbed provider pose therefore disagreed with the played one. The clip
   provider had the same defect in `alien-foot-lab`, the only scene that sets `proceduralMotion`,
   and nothing looked. It now advances inside the replay, on every step, at that step's own time.
   A replay from the beginning gets the t = 0 advance a play's first frame makes, and a body on the
   provider chain is replayed in full, because its memory has no bounded history.
3. **The chain was built at the first *pose*, after the first advance.** Frame 0 of a play was
   simulated without providers, and a seek (which has them) disagreed. The chain is now prepared
   before the entities step.
4. **Units.** A request is in world metres per second and the database is in the asset's own
   units. A Glowmere alien drawn at 1.94x asking for 3 m/s is asking its clips for 1.55. The sink
   gives the provider the node's scale.

## Default off, shown rather than asserted

`tests/unit/test_motion_matching_default_off.cpp` reduces Glowmere's simulation to one digest over
3 s of play plus a backward replay. The digest covers every entity's position, yaw, speed and
activity, every provider memory, and every rig's final local pose.

- **Run on the pre-wiring commit** (`f8fe1b4b`, built in a separate worktree) and on this one, both
  digests are **`5599cff790bc8a34`**, and the same on a second run. The test asserts that value.
- **The control arm:** the same scene with `rook` opted in gives **`1dbac44e1bf4fa02`**, so the
  instrument can see the matcher when it is there.

## The demo

`examples/labs/motionmatch/alien-match-lab.scene.json` stands two scouts on the Glowmere valley, both
wandering on the same seed. `alien-match` runs on the matcher; `alien-clip` is its clip-provider
twin. The lab test checks that `alien-match` takes every frame from the matcher and the twin none.
A scrub to frame 200 lands on the played sample, clip time and blends exactly (ADR-360); this is
where the §26/§73 time-based continuation pays for itself.

**What it does not yet show is good selection.** Over six seconds on this unfiltered 26-clip
corpus, the matcher body played `Button_push`, `Floating`, `Take_from_table` and `Walking_low_grav`,
and never `Idle` or `Walking`. Standing, it picks stand-still action clips. Walking, it picks the
low-gravity walk. That is §65's work, together with
tag-based candidate filtering (§13/§14). It is not a wiring defect, and it is why no shipping scene
gets the key.
