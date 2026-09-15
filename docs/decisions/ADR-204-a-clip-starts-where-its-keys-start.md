# ADR-204: A clip starts where its keys start, a facing is not an odometer, and a stride speed is a claim about a clip

**Status:** Accepted
**Date:** 2026-09-14

Three defects in the four animated aliens, found by measuring three things nothing had measured.
They are one record because they were found in one pass and because the third is the interesting
one: it is ADR-182's "a diagnostic arm that cannot fail" arriving in a shipped scene.

## 1. The playable range does not begin at zero

`AnimationClip` carried one number for its extent -- `duration`, the **last** key time -- and the
player looped over `[0, duration]`. That is correct exactly when the first key is at zero.

Every clip in the alien pack begins at **1/30 s**. All 26, on all four characters, to the last
decimal: Blender's exporter wrote the frame range it was given and these takes are authored from
frame 1. The Mixamo character this engine was built against begins at 0, which is why nothing
ever saw this.

| clip | first key | last key | engine's cycle | the animation's cycle |
|---|---:|---:|---:|---:|
| Walking | 0.0333 | 1.0667 | 1.0667 s | **1.0333 s** |
| Running | 0.0333 | 0.7333 | 0.7333 s | **0.7000 s** |
| Idle | 0.0333 | 2.0000 | 2.0000 s | **1.9667 s** |

Two consequences, both visible and neither nameable.

**A held pose at the top of every loop.** For the first 1/30 s of each cycle the local time is below
the first key, every channel clamps to it, and the character stands still in the middle of its
stride -- 3.1% of every walk cycle, 4.5% of every run. Sampled at the rig's own 30 Hz this shows up
as **3 identical consecutive poses in every 31-step cycle**.

**A cycle 3.2% too long.** The feet cross the ground 3.2% slower than the file says they do, on top
of whatever else is wrong.

`AnimationClip::start` is the first key time across every channel and `length()` is the span; the
player loops over `[start, start + length]`. A clip whose keys begin at zero is byte-for-byte what
it always was.

The loops close exactly on that span, which is why it is the right one: `Walking`, `Running`,
`Idle_turn` and `Fall_loop` have the **same key at both ends**, to the float. Reading `last` as the
period asks for a cycle that is one key longer than the one the animator drew.

## 2. A facing is not an odometer

> "he sometimes looks like he is walking forwards while moving backwards"

Reproduced on the shipped Glowmere wanderer at **31.77% of moving frames**, worst single step
0.183 m, first offence at 198 simulated seconds.

The decomposition matters, because three plausible suspects are innocent. The body's displacement is
accounted for by its own `speed` along its own `yaw` to within **7 micrometres** on the offending
frames: crowd separation, the penetration resolve and ADR-162's off-the-navigable-set escape
contribute nothing. The body goes exactly where it says it is going.

It is **drawn** somewhere else. `EntityState::yaw` is written as `yaw += turned` by every behaviour
and action that steers, so it is the *total a body has turned*, not the direction it faces. That was
invisible until it reached the node's rotation parameter -- whose hard range is ±360 degrees, and
whose `setFinalComponent` **clamps**. Past a net revolution the parameter stopped tracking the body
and the character walked while pointing wherever the clamp left it. Measured gap between the drawn
forward and the travelling forward: up to **3.14 radians**.

The fix is to say what yaw is. It is canonicalised into (-π, π] once, after everything that steers
has had its turn and before anything reads it, so the node's rotation, the sockets and the
`LocomotionState` the animation layer is handed all see one angle. Safe because nothing reads it as
an accumulation: every comparison goes through `angleDelta`, which normalises, and everything else
takes its sine and cosine. `spin` has done exactly this to its own angle since it was written.

And the composed euler is folded into (-180, 180] at the write site, so the clamp is unreachable by
construction rather than by everybody remembering. Euler degrees are 360-periodic: this is the same
orientation and cannot be anything else. What it removes is a silent clamp, which is not a rotation
at all.

**31.77% to 0.00%** on the scene the report was filed against, over the same ten simulated minutes:
31,761 moving frames, zero drawn travelling backwards, worst step 0 m.

### What is left, and why it is not this

Glowmere Valley 2's `rook` still shows backwards steps -- 74 in 11,835 moving frames over ten
simulated minutes with the camera pinned to it, so nothing was culled and none of the 74 was a
coarse update. They are a different mechanism, and the decomposition says so rather than the
argument: the body's own `speed` along its own `yaw` accounts for **none** of them. Mean residual
0.167 m against a mean step of 0.093 m -- **1.8x the whole step**, worst 0.093 m, every one of them
in a walk or a turn and none in a run.

That is a body being pushed: crowd separation, the penetration resolve, or ADR-162's walk back onto
the navigable set. A body shoved sideways out of a rock moves without its facing following, and that
is exactly the guarantee those mechanisms exist to make. Making the facing chase a push would mean a
character snapping round every time it brushed a boulder, and it would be a navigation change, not
an animation one.

So the probe classifies rather than counts, and asserts the thing the fix establishes: **a step the
body's own travel accounts for is never against the way the body is drawn** -- zero, not a fraction.
The pushes are reported and bounded loosely, so a regression that made them dominate the walk would
still be caught.

## 3. A stride speed is a claim about a clip, and nothing was checking it against the clip

ADR-161 built `Gait::footSlip` for exactly this failure -- a body travelling at a speed its clip was
not authored for -- and ADR-198 used it to set the four aliens' `walkSpeed` and `runSpeed`.

`footSlip(settings, activity, speed)` returns `speed / (authored * rate)`, and with rate matching on
`rate = speed / authored`. So **when the behaviour's travel speed equals the authored stride speed it
returns 1.0 whatever the clip is doing.** It compares the authored number with itself. It cannot see
that the authored number is wrong, because the authored number is both the expected value and half
the actual one.

ADR-198 read its silence as agreement and set `walkSpeed` to "the speed the creature actually cruises
at". The diagnostic went quiet. Measured over 120 simulated seconds afterwards, `tide`'s foot slip
was **1.000 on every single frame**, minimum 1, maximum 1 -- while its feet were covering 5.69 m of
stride for every 3.20 m of ground.

### The measurement that is not self-referential

For an in-place cycle the planted foot is stationary in world space, so in the character's own frame
it travels backwards at exactly the body's speed. The **median backward speed of a toe while that toe
is in the bottom fifth of its height range** is that number, and it is stable to half a percent
across contact thresholds from 10% to 30%:

| clip | stride speed | cycle | ground per cycle |
|---|---:|---:|---:|
| Walking | **1.581** model units/s | 1.0333 s | 1.634 units |
| Running | **3.809** model units/s | 0.7000 s | 2.666 units |

Against each character's own scale:

| | scale | walkSpeed was | the clip says | error | runSpeed was | the clip says | error |
|---|---:|---:|---:|---:|---:|---:|---:|
| rook | 3.610 | 5.60 | 5.71 | −2% | 13.50 | 13.75 | −2% |
| **tide** | 3.601 | **3.20** | **5.69** | **−44%** | **8.00** | **13.71** | **−42%** |
| **sage** | 3.576 | **4.00** | **5.65** | **−29%** | **9.50** | **13.62** | **−30%** |
| **ember** | 3.344 | **4.60** | **5.29** | **−13%** | **12.00** | **12.74** | **−6%** |

The sting: the derivation ADR-198 *abandoned* -- "a human's 1.6 m/s walk times the model scale" --
was right to within 1.3%. The clip's stride speed is 1.581 units per second and 1.6 was the guess.
ADR-198 replaced a number that was accidentally correct with one that was measurably wrong, because
the instrument it consulted could not answer the question it was asked.

Measured slide of the planted foot for `tide`, over four seconds of walking at 3.2 m/s:

| | slide per stance | rate |
|---|---:|---:|
| before | **−1.18 m** (−2.30 m/s: the foot skates backwards through the ground) | 0.35, clamped |
| after | **+0.16 m** (+0.16 m/s: the toe's own roll through heel-strike and toe-off) | 0.562 |

The clamp is the other half. `rateMin` was 0.35, which bites at 1.99 m/s for a body whose walk band
runs down to 0.48 -- so the rate matcher saturated through most of the band and the diagnostic said
"on and saturated" without anybody acting on it. The band and the clamp are now checked against each
other: `rateMin` and `rateMax` cover every speed the behaviour can travel at, from `moveExit` to
`runEnter` for the walk and from `runExit` to the behaviour's own `runSpeed` for the run.

Nothing here is an animation-speed fudge. `matchRate` is ADR-161's own mechanism, doing what it was
built to do, finally fed a number that describes the clip.

## 4. And a rig that stopped being posed before the entity stopped moving

The engine already warns: *"the rig stops being posed at 320 m but the entity keeps simulating to
340 m; between them the character travels in a frozen pose."* It fired on all four aliens in the
shipped scene and the scene ignored it. The rig `cullDistance` now covers the entity's.

## What was suspected, measured, and is not a defect

**A playback-rate change does not jump the clip.** `Composition::updateCharacters` calls
`player.setSpeed(rate, node.animationAppliedAt)`, and `animationAppliedAt` looked like the second the
*state* was entered -- which would make every rate change jump the clip by `elapsed × Δrate`, most of
a stride sixty times a second on a body whose speed is always changing. It is not: `setNodeAnimation`
refreshes `animationAppliedAt` to the frame's own second on every push, so the rebase is at `now` and
the clip time is continuous. Measured over 60 frames of a continuously rising rate: the clip advances
by `dt × rate` every frame, to 1e-4, with **zero** jumps in 120 simulated seconds of the shipped
scene. A regression test now pins it, because the call reads as though it were wrong.

## Evidence

`tests/unit/test_alien_locomotion.cpp`, 15 test cases. Before the fix, 11 of them failed:

* the loop repeats on its own period -- palette deltas of 0.13 to 0.92 where 1e-4 is required
* no held pose in a cycle -- 3 frozen steps of 93 (Walking), 3 of 63 (Running)
* the authored stride speed describes the clip -- fails for `tide`, `sage`, `ember`
* rate matching covers the travelling range -- fails for all four
* the drawn facing follows the body past a revolution -- gap up to π

The transition test deserves its own note, because its first two versions were the failure this
project keeps having. "The worst blend step is a small fraction of the whole transition" is worthless
-- two poses a third of a second apart can happen to land near each other, and the criterion tightens
to nothing for reasons unconnected to the blend. "No bigger than the clips' own steps" is wrong too:
a cross-fade between two different poses legitimately moves faster than either clip does. What is
tested instead needs no tolerance at all: **halve the sample step and a continuous pose moves half as
far; a jump moves exactly as far however finely you sample it.** All six implemented transitions come
out at a ratio near 0.25.

Four aliens over 90 simulated seconds of the shipped scene, afterwards: 191-392 m travelled, **0**
frames drawn travelling backwards, **0** frozen poses while walking, **0** frames with foot slip
outside 1.1× -- and the drawn facing within each character's own authored `liveliness` sway of its
heading, which is the only thing that is allowed to be between them.

## What this does not fix, beyond the pushes above

**The gait's minimum dwell.** `GaitSettings::minDwell` holds a gait for a quarter of a second after
the body has already stopped, so for up to 0.25 s the machine says Walk at a speed the rate matcher
cannot represent -- measured on `rook` at 0.07 m/s against a 5.71 m/s clip, which is 0.2x slip and
fires the ADR-161 warning once. It is bounded and small: under two centimetres of ground crossed in
the whole window, at the end of an arrival. Removing it means either a gait that flickers or a
minimum dwell that knows about clips, and neither is worth it for two centimetres.

`Gait::footSlip` still cannot see a `walkSpeed` that does not describe its clip. Closing that in the
engine means the gait layer measuring a clip, which means it learning which joints are feet -- asset
knowledge, not engine knowledge -- so the check lives in the test, where the foot bones can be named.
A scene that rescales a character and forgets to rescale its stride speeds will be caught by
`each alien's authored stride speed describes the clip it names` and by nothing at runtime. That is a
worse answer than a runtime diagnostic and a better one than a runtime diagnostic that reports 1.0.
