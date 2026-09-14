# ADR-181: Counterbalance the arms, and detect the drift the counterbalancing hides

**Status:** Accepted
**Date:** 2026-09-14

## What was wrong with the harness that exists to prevent this

`--ab` interleaves its arms inside one process, which is right and is why this project refuses
cross-session comparison. But it ran them in **fixed order** — `baseline` then `arm`, every block —
and that is a different defect wearing the same symptom.

An arm that always runs second always pays for whatever the machine did during the block: thermal
drift, a background process waking, the GPU clocking down. Drift therefore enters the delta as a
**bias rather than as noise**, and averaging more blocks converges on the wrong answer instead of the
right one. More data makes a biased estimate more confident, not more correct.

It is not hypothetical. A fixed-order interleaved measurement of six hero mushrooms reported that
**hiding them made the frame 2.3 ms slower** — the same causally impossible sign that a
two-invocation A/B had produced from ordinary noise, arriving this time from a mechanism that extra
blocks would not have fixed.

## Two changes, because they answer two different questions

**1. Counterbalance the schedule.** The arm order alternates between blocks, so each arm runs first
as often as it runs second and position effects cancel in the mean instead of accumulating in one
arm. This removes the *bias*.

**2. Detect the drift.** Counterbalancing averages drift; it does not measure it, so a counterbalanced
run can still return an impossible sign and say nothing about why. The baseline arm is measured
throughout the run, so **its own first half against its own second half** is a direct reading of how
far the machine moved while the experiment was happening — in run order, which is what makes it a
drift measurement rather than a second noise estimate. It costs nothing: those blocks were measured
anyway.

A run whose drift is at least as large as the effect it claims is reported **VOID**, not "noisy".
The distinction is the point: a noisy run measured one machine imprecisely, and a drifting run
measured two machines and subtracted them. Equality voids, because an effect exactly the size of the
drift is indistinguishable from it.

## Noise and drift are different failures and now read differently

The demonstration run makes the case better than the argument does. A null A/B on Glowmere — both
arms the baseline — over four pairs reported:

* **noise floor 7.73%**, from baseline blocks varying by 5.74% against a calibrated 2.00%
* **drift −0.63%**, "the machine held still"

Both are correct and they are not the same statement. The machine was *noisy* — another agent was
holding the GPU, which `pgrep` confirmed before and after, so ADR-170's clause did its job and the
absolute numbers from that run are worthless. But it was not *drifting*: it was equally noisy
throughout. A harness that reported one number could not have said that, and the two failures have
different fixes — wait for a quiet machine, versus stop trusting a run that has already happened.

## What this does not do

It does not make a single absolute frame time meaningful. Three invocations of a byte-identical scene
still spanned 25% on this machine, and that is what `--ab` exists for. It does not instrument thermal
state, which remains the leading uninstrumented candidate for slow drift and is now at least
*detectable* by its effect rather than only suspected.

And it cannot help a measurement taken outside the harness. `--ab` takes a project file, so a scene
assembled in C++ cannot use it and must hand-roll a counterbalanced loop — recorded as an open gap in
`docs/renderer-level3/01-assessment.md`.

## Evidence

`tests/unit/test_render_stats.cpp`: a steady machine voids nothing; a machine that climbs 3 ms across
the run voids a 1 ms claim and **not** a 9 ms one, because a drifting machine does not invalidate
every measurement, only the ones it could account for; equality voids; a single baseline block reports
itself unmeasurable rather than reporting zero drift, because an unmeasurable check must neither void
every short run nor silently pass.
