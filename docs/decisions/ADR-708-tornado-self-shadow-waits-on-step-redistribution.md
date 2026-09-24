# ADR-708: The tornado's self-shadow works, and on the hero it waits for step redistribution

**Status:** Accepted -- a finding; no behaviour change
**Date:** 2026-09-24
**Addresses:** `docs/design/effect-library/tornado-fog-production-pass.md` §2 item 3 and §7 item 2;
`docs/tornado-handoff.md` §5.3 and §10.
**Depends on:** the shared-march step redistribution (plan §2 item 5 = fog §31, plan §4 step 3),
which this ADR does not do.
**Evidence:** renders listed below; the benchmark is `--bench-json`, 60 frames, 1920x1080, two
interleaved rounds under `tools/gpu-lock.sh`.

---

## Context

ADR-570 built the shared self-shadow march, kind-dispatched, off by default (`volumeShadowSteps` 0).
The handoff's §10 turned tornado self-shadowing from a project into a slider, and the plan's brief
was: turn it on against the hero Tree of Life, look, re-tune. It was to be done after the debris
work (ADR-706) so the lighting is tuned against the final structure; it was.

## What turning it on does

**Where the march samples the storm densely, it is the largest single improvement to the read on
record.** The showcase and mode-ladder labs march 111-296 steps over their own distances. At four
shadow steps the classic cone's wall cloud becomes a heavy dark storm base, the column gains a lit
and a shadowed side, and the debris mound (ADR-706) gets a dark underside -- smoke, not glowing gas
(`sheet-item3-labs.png`). The Cosmic Storm barely moves, correctly: it scatters 0.15 of the scene's
light and is lit by its own emission. At the physical strength of 1 the wall cloud crushes to near
black, because the march has no ambient term for a shadowed sample to fall back on; **strength 0.3
to 0.6 keeps the mass dark and the form legible** (`sheet-item3-strength.png`). That is the tuning
recommendation for when it ships.

**On the hero it comes back as grain.** Four shadow steps at the project's 32 march steps turn the
side away from the celestial key into heavy salt-and-pepper speckle (`sheet-item3-hero.png`,
centre). The arms that locate it:

| arm | shadowed side |
|---|---|
| 2 / 4 / 8 / 16 shadow steps, 32 march steps | speckled at every count -- **16 is no cleaner than 8** |
| 8 or 16 shadow steps at strength 0.5 | speckled, fainter |
| 4 shadow steps, **128** march steps | still speckled, much less |
| 4 shadow steps, **256** march steps | **clean**: lit side, shadowed side, helical bands legible |

So the shadow ray is not the fault. The **primary** march is: 32 steps over the project's 4 km is
125 m per step through a funnel 140-220 m across, so each pixel gets one or two jittered samples in
the column. Without self-shadowing the in-scattered light is the same at every depth in the funnel,
so which depth a pixel happened to sample does not matter. With it, the near side is lit and the
far side is not, and a pixel's one sample decides which it shows. This is the exact case the plan's
§2 item 5 describes -- a thin medium limited by the global step count -- and the brief for this work
says not to restructure the march's sampling and to stop if an item needs it. **It does.**

## What it costs, measured

`volume.march` median and GPU frame p50, two rounds each:

| arm | `volume.march` | GPU frame p50 |
|---|---|---|
| 32 steps, shadow off (as shipped) | 3.87, 3.87 ms | 14.0, 14.4 ms |
| 32 steps, 4 shadow steps | 4.98, 4.92 ms | 15.1, 14.8 ms |
| 256 steps, shadow off | 28.8, 28.2 ms | 39.8, 38.5 ms |
| 256 steps, 4 shadow steps | 33.6, 31.9 ms | 43.7, 41.9 ms |

**The shadow itself is cheap here: +1.1 ms.** ADR-570's 2.2-2.6x per volumetric light was measured
on a wide fog bank; the tornado's bound culls the shadow ray for most of the frame. What is
expensive is the sampling that makes it clean, and raising the global step count 8x to buy it
(+25 ms) is the brute-force version of the step redistribution, which places samples inside the
medium instead of across the whole ray.

## Decision

Nothing ships. `volumeShadowSteps` stays 0 in both Tree of Life files and in the labs. Turning it
on at 32 steps puts grain into a deliverable; turning the steps up to 256 is a 7x march cost that is
a budget decision rather than an art-pass one; and turning it on only in the labs would change the
evidence scenes ADR-580 and ADR-707 cite without the hero it was for.

## Revisit when

- **The step redistribution lands** (plan §4 step 3). Then re-run the hero arm at 4 shadow steps and
  the shipped step count; it should look like the 256-step arm. Start the tuning at strength 0.3-0.6,
  not 1.
- Even the clean 256-step arm shows the shadowed side as the column's cyan **emission** rather than
  dark smoke: the hero is still "too bright" and emission-led (plan §2 item 4, ADR-580 §10.4). Tune
  the two together, after redistribution, once.
