# ADR-191: The LOD ladder is a prefilter, not a saving

**Status:** Accepted
**Amends:** ADR-186
**Date:** 2026-09-14

## The correction

ADR-186 gave an offline render four lifted limits. Three of them were right and the fourth was not,
and the fourth was predicted to be wrong in this document's own words before anyone looked at a
frame: *"lifting the distance limits increases flickering area by 57%... scatter held at rung 0 is
high-frequency geometry where the ladder used to substitute something smoother."*

The report that followed the first offline render under it was **"small/distant objects look
particularly aliased"**, which is that sentence from the other side.

## The distinction the first version missed

Three of the four reductions **hide something a viewer would otherwise see**:

* the distance cull makes scatter vanish,
* the rig rate steps a distant character at 20 Hz and freezes it past 120 m,
* the entity bands leave a far herd standing where it stood.

Every one is a frame budget being spent on the near field at the expense of the far one, and an
offline render has no frame budget. Lifting them is right.

**The LOD ladder is not one of those.** It chooses a *representation* by projected screen size, and
at the sizes it acts on the simpler mesh is not a worse picture — it is the renderer's **only
prefilter for geometry smaller than the sampling grid.** This engine has no MSAA, no TAA and renders
one sample per pixel; a rung that replaces a two-pixel fern with something with fewer edges is doing
the job a filter would do if there were one.

Take it away and sub-pixel geometry is drawn at full frequency with one sample per pixel. That is
the definition of aliasing, and it is what was measured.

## Why keeping it costs nothing visible

**The ladder is keyed to projected size, so it is resolution-aware by construction.** Render at
1920x1080 instead of 1280x720 and every instance demotes later, in pixels, with no setting changed.
A billboard only appears once the object is small enough on screen that a billboard is an honest
description of it.

That is also why the original worry — "a finished render still had billboards in its far field" —
was the wrong thing to fix. The billboards were not the defect; the *vanishing* was.

## Measured

Glowmere, river view, 960x540, 24 frames after a 2 s warm-up, threshold 6/255, offline tier:

| `--render-limits` | flickering area | vs live |
|---|---:|---:|
| `unlimited` (ADR-186's default) | 4.224% | **+57%** |
| **`tier` (this change)** | **2.761%** | **+2.6%** |
| `live` | 2.691% | — |

The new default sits **within 2.6% of live playback** while still lifting the three reductions that
matter, and the sequence hashes for `tier` and `live` differ — so this is not a quiet revert to
playback behaviour, it is the far field being drawn in full at a representation it can be sampled at.

`--render-limits unlimited` still lifts all four, because "what does the far field look like with no
ladder at all" is a real question and there should be a way to ask it.

## The general lesson

**A reduction can be a saving, a prefilter, or both, and which one it is decides whether removing it
improves the picture.** ADR-186 enumerated four reductions and treated them as the same kind of
thing because they were all keyed to distance. Distance was the wrong axis to group them by; what
matters is whether the thing removed was *information the viewer wanted* or *frequency the sampler
could not carry*.

The first version of this had the number that would have caught it — it was recorded, in the same
document, as a cost of the change — and shipped anyway because it read as a trade rather than a
defect. A measurement written down and not acted on is worth about as much as one not taken.
