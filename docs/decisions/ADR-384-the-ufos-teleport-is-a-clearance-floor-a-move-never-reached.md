# ADR-384: The UFO's teleport is a clearance floor the approach never came down off

Status: proposed (Phase 1-3 findings; the fix is not in this commit)
Date: 2026-09-19
Branch: `agent/ufo`
Relates to: ADR-210 (the director), ADR-262 (`Anchor::Drawn`), ADR-264 (a project's parameters are
applied over its scene), ADR-354 (the animals-in-beam path), ADR-182 (a probe that cannot fail
proves nothing), `tests/unit/test_beam_lab.cpp`, `tests/unit/test_abduction_poc.cpp`

*Next free number at the time of writing was 384; 383 was the high-water mark.*

## The report

"The UFO craft unexpectedly jumps/teleports to a different position immediately before the tractor
beam begins." Everything below is measured, in `tests/unit/test_abduction_sequence.cpp`, against the
**project** (`glowmere-valley-2-multicam.json`, loaded through `app::Engine` in `Offline` mode, so
what is stepped is the film and not the scene it was built from -- ADR-264).

## The root cause

`StepKind::MoveTo` applies the step's `clearance` to the position it writes **every frame,
including the last**:

```cpp
cue.progress = std::min(1.0f, cue.progress + dt * rate);
glm::vec3 p = glm::mix(cue.from, goal, smoothstep(cue.progress));
...
if (clearance > 0.0f && nav.valid()) {
    p.y = std::max(p.y, nav.groundHeight(flat(p)) + clearance);   // <-- the floor
}
...
return cue.progress >= 1.0f ? StepStatus::Done : StepStatus::Running;
```

The completion test is the **tween parameter**, not arrival. So a `MoveTo` whose destination lies
below the clearance floor reports `Done` while the body is sitting on the floor, `clearance -
height` metres above the destination the step resolved. Nothing anywhere notices; the beat ends and
the next one starts.

`StepKind::Follow` -- the next step, in the next beat -- resolves the same destination, applies **no
clearance** (the shipped scenario authors none on it), and writes it straight into
`DirectorMotion::position`, which `EntityWorld` turns into `state_.travel = position - anchor` with
no easing, no blend and no continuity term. So the first frame of the `beam` beat puts the craft
exactly where the approach was supposed to have ended, in one frame.

In the film the two numbers are `cruiseClearance` 34.0 and `hoverHeight` 23.0, both authored in the
scene and both restated identically in the project's parameters. **34 - 23 = 11.**

## The measurement

Five abduction cycles, 90 s at 60 Hz, craft `state().position()` per frame:

```
t=  0.850   approach -> approach  step=  2.175  agl  31.83 ->  34.00   (the floor engaging)
t=  7.850       beam -> beam      step= 11.003  agl  34.00 ->  22.92
t= 19.000   approach -> approach  step= 10.980  agl  23.02 ->  34.00
t= 26.000       beam -> beam      step= 11.004  agl  34.00 ->  22.94
t= 37.150   approach -> approach  step= 11.009  agl  22.99 ->  34.00
t= 44.150       beam -> beam      step= 11.004  agl  34.00 ->  23.00
t= 55.300   approach -> approach  step= 11.010  agl  22.99 ->  34.00
t= 62.300       beam -> beam      step= 11.004  agl  34.00 ->  23.01
t= 73.450   approach -> approach  step= 11.007  agl  22.99 ->  34.00
t= 80.450       beam -> beam      step= 11.004  agl  34.00 ->  23.00
```

**Two** teleports per cycle, not one, and the report only mentions the second:

* the **down** jump at the first frame of `beam`, which is the one the owner sees, because it lands
  on the frame the `show beamOn` step fires;
* an **up** jump 11 m, 0.8 s into the next `approach` -- the floor re-engaging on the first frame of
  the next `MoveTo`, after `aimSeconds` 0.8 of `lookAt`. It is less visible only because the craft
  is far away and about to move anyway.

The first cycle's 2.175 m at t=0.850 is the same event from the scene's authored start height
(`nodes/visitor/position` y 29.5, 31.83 m above ground) rather than from 23.

## The control arm (ADR-182)

Same film, one parameter changed: `staging/abduction/cruiseClearance` 34 -> 23.

| arm | frames with a craft step over 1.0 m | worst single-frame step |
|---|---|---|
| shipped (clearance 34, hover 23) | 4 in 30 s | **11.004 m** |
| control (clearance 23, hover 23) | 0 | **0.575 m** |

0.575 m is the travel speed at 60 Hz. The probe could have come out the other way and did not.

## Phase 3: systemic, production data, or the interaction?

**The interaction -- the spec's Case 3.** The Tractor Beam Lab (`tractor-beam-lab.scene.json`) is a
different scene, on flat ground, with a different cast, no project over it, `travelSpeed` 90 instead
of 30 and every duration different. It reproduces the teleport to the millimetre:

```
t=  3.817       beam -> beam      step= 11.004  agl  34.00 -> 23.00
t=  9.150   approach -> approach  step= 11.000  agl  23.00 -> 34.00
                          ... and so on, every cycle
lab      (cruiseClearance 34, hoverHeight 23): worst 11.004 m
lab ctrl (cruiseClearance 23, hoverHeight 23): worst  2.250 m   (= 90 m/s at 60 Hz)
```

So:

* the **mechanism** is the director's, and is not Glowmere's: a `MoveTo` that reports `Done`
  somewhere other than its destination, handing over to a `Follow` that snaps. Any authored shot
  with `clearance > height` reintroduces it, which is exactly what the brief means by "future
  authored abduction shots cannot easily reintroduce these timing bugs";
* the **magnitude** is the data's: the lab copied the film's `cruiseClearance` 34 / `hoverHeight` 23
  pair verbatim, which is why both files show 11.000 and not two different numbers.

A Glowmere-only offset would fix neither file. Changing only the data would leave the next shot to
rediscover it.

## What else the instrument found

These are measured, are not the reported defect, and are recorded so the sequencing work that
follows is aimed at facts:

1. **The craft is not stationary while the beam deploys.** During the `beam` beat the actor runs
   `follow` with `hold: false`, so it chases an animal that is still walking: 13.07 m of simulated
   path in 2.42 s, of which 11 is the teleport and ~2 m is genuine tracking. The spec's invariant
   "no frame where UFO moving AND beam deploying" is violated for the whole 2.4 s.

2. **`depart` freezes the simulation but not the picture.** `simPath` over the 3.22 s `depart` beat
   is **0.00 m** -- the actor cue is a bare `wait`, nothing rewrites `DirectorMotion`, and the held
   position persists exactly. But `drawnPath` over the same beat is 5.4-7.8 m, because the entity's
   own `drift` (radius 2.4 m, 0.031 Hz), `hover` (0.85 m, 0.055 Hz) and `bank` behaviours keep
   running and are folded on after the director. Whether that is "subtle authored hovering" or
   "drift while the beam is up" is the owner's call; the drawn bounding box during `abduct` is
   0.6 x 0.5 x 0.8 m, so it is small in extent and long in path.

3. **The beam's `visible` flag is up for 15.33 s per cycle and the craft travels 45-79 m inside
   it.** `nodes/visitor-beam/visible` goes to 1 at the start of `beam` and to 0 only
   `beamDrainSeconds` (5.0) into the *next* `approach`. The particle column itself is empty about
   1.9 s into `depart` (`spawnRate` reaches `beamRestRate` 0 over `beamFadeSeconds` 0.9, `lifetime`
   1.0), and the craft does not start moving until 4.0 s into `depart` -- so the shipped film does
   satisfy "beam finishes before the craft leaves", but by **2.1 s of slack between two unrelated
   magic numbers**, which is precisely the fragility the brief asks to remove. It is also only true
   silently: `visitor-beam` carries an `audio.bass` reaction on `spawnRate` with depth 900, so with
   the song playing the emitter refills on every bass hit for the 8.2 s the node is visible while
   the craft flies.

4. **The animal pops, and it is not even glowing when it does.** `StepKind::Retire` writes the
   role's `visible` parameter to 0 -- a hard toggle, one frame. The `glow-rise` / `glow-fade` cue on
   `emissiveBoost` runs 1.3 + 1.9 = 3.2 s of the 4.6 s lift, so the animal's glow is already back to
   zero for the last 1.4 s and it vanishes looking ordinary. There is **no opacity parameter to fade**:
   `nodes/<name>/` registers `position`, `rotation`, `scale`, `visible`, `emissiveBoost`,
   `roughnessScale`, `lightIntensity` and `lightColor`, and the only `opacityScale` in the
   parameter set is `procedural/<node>/parts/<i>/opacityScale`, which exists on procedural nodes
   only -- the farm animals are `gltf` nodes. A fade is therefore either a new generic node
   parameter (plus whatever the renderer does with `material.opacity` on an opaque mesh, which is
   not yet measured) or a scale/emissive dissolve built from parameters that already exist.

5. **The multicam does not cause the jump; it frames it.** `UFO Watch` (scene
   `cameraDirection`, camera id 3) is `followNode: visitor`, `aimNode: visitor`, `eventScenario:
   abduction`, `eventBeats: [aim, beam, abduct, depart]`, `eventBlend: 0.0` -- a **hard cut** to a
   camera rigidly parented to the craft, fired on the entry of the very beat that teleports it. In
   four of the five cycles the cut and the teleport are one frame apart:

   ```
   camera: UFO Watch (event abduction) at 25.98      teleport at t=26.000
   camera: UFO Watch (event abduction) at 44.13      teleport at t=44.150
   camera: UFO Watch (event abduction) at 62.28      teleport at t=62.300
   camera: UFO Watch (event abduction) at 80.43      teleport at t=80.450
   ```

   Because the camera follows the craft, the camera drops 11 m with it: what a viewer sees is the
   entire frame lurching, one frame after a hard cut, every cycle. The 19.00 / 37.15 / 55.30 /
   73.45 *up* teleports are also on this camera (it holds until `eventTail` past `depart`). This is
   the answer to the spec's Test 6 / "camera cuts affecting UFO position": the cut is downstream of
   the same beat entry, not upstream of the transform.

6. **No competing writer of the craft's transform.** The project has 25 routes and 8 timeline
   tracks and not one of them targets `visitor`; there is no `nodes/visitor/position` in the
   project's 5,502 parameters, so no save has photographed a run onto the craft. The spec's
   "duplicate UFO movement definitions / stale legacy animation data / shot-local transforms"
   hypotheses are all negative for this file. Camera cuts (five `UFO Watch` events in 90 s) do not
   coincide with any of the ten discontinuities.

## What is not yet decided

The fix has three candidate shapes and they are not equivalent:

* **A. Make `MoveTo` land.** Apply `clearance` to the *path* and not to the *endpoint*: clamp
  `glm::mix(from, goal, eased)` for `eased < 1` and let the last frame be the goal exactly. Honest
  about what `clearance` means ("do not fly into a hill on the way"), removes both teleports, and
  removes them for every future shot. Risk: a shot whose destination genuinely is inside a hill now
  ends inside the hill instead of above it, silently.
* **B. Make the handover continuous.** Give `Follow` an entry blend from wherever the body is. Hides
  the symptom at every beat boundary rather than fixing the step that lies about being done, and
  would make a deliberate cut impossible to author.
* **C. Refuse the description.** `setDesc` rejects a scenario in which a `MoveTo`'s `clearance`
  exceeds the `height` of a `Follow` that shares its destination. Catches the authoring mistake at
  load, changes no runtime behaviour, and does nothing for the two files that already ship.

A and C are complementary and B is a different feature. This ADR records the measurement; the
choice belongs with the sequencing work and with the owner.
