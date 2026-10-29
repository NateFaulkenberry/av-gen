# ADR-1816: Contact shadows are a lever of the quality ladder and the live optimizer

- Status: Accepted (eng/render-improvements, 2026-10-28). Filament evaluation borrow item A6, approved by the owner.
  A tiering change only: the contact-shadow march itself (`lighting.wgsl` `lightVisibility`, the shadow code) is not
  touched.
- Code:
  - `src/app/interactive_resolution.{hpp,cpp}`: `LiveQualityRung::contactShadows`, `noContact`, the three ladders,
    `applyStageTwoCeilings`, `effectChanges`, the "shadows" priority group, the `nocontact` lever, the ceiling's JSON
    (`contactShadows`), the PERFORMANCE profile.
  - `src/app/live_profile.{hpp,cpp}`, `src/app/live_profile_run.cpp`, `src/app/live_performance_host.cpp`: the
    "Contact shadows" optimization candidate (lever `nocontact`).
  - `src/app/live_optimize.cpp`: `nocontact` is an image-wide lever.
  - `src/rendering/render_quality.hpp`: the Preview tier turns them off.
- Test: `tests/unit/test_interactive_resolution.cpp` (`[adr1816]`).

## Problem

The Filament evaluation measured contact shadows (a screen-space march per shadow-casting light, inside the lit pass)
at 2.53 ms of the material lab's 8.38 ms lit pass. `QualitySettings::contactShadows` and `contactSteps` existed, and
`--ab contact` could measure them, but no ladder level, no profile and no optimizer lever could turn them off: a scene
falling to Low still paid for them.

## Decision

- **A rung field, `contactShadows`**, applied like every other ladder lever: off where the rung says off, never on where
  the base had it off. `contactSteps` is left as the base set it, so climbing back restores exactly the base's march.
  Turning it off already zeroes the steps the shader receives (`shadowParams.z`) and can drop the depth prepass when
  nothing else needs it (ADR-1406's predicate). No shader changes.
- **Ladders**: Ultra and High keep contact shadows (Ultra is the tier exactly; High is the reference live picture);
  **Medium, Low and Emergency drop them** in all three strategies (balanced, resolution first, effects first). The
  PERFORMANCE profile drops them. A project-defined priority ladder (ADR-1105) drops them where it gives up the
  "shadows" group fully.
- **The optimizer**: a "Contact shadows" candidate is offered whenever the march runs, with lever `nocontact`
  (image-wide). Accepting it keeps it as a project ceiling (`"contactShadows": false` in `live.overrides`).
- **Tiers**: Preview drops them; Realtime, High and Offline keep them.
- **What is not changed**: the algorithm, its step counts per tier, and Ultra, High and Offline output.

## Consequences

- **Ultra and offline are unchanged.** The Ultra rung and the Offline tier are as before (unit test). The five reference
  scenes' offline frames (ADR-1810) before and after this change: SAME on 12 frames; Rift's three move by 1 level, Rift's
  known run-to-run instability (its clean re-capture did the same in Part 1).
- **What it buys** (`--ab contact --live-frame`, 1080p, interleaved, on top of ADR-1815's specialised shader):
  - material lab: 0.89 ms (4.07 -> 3.17 ms). Under the general shader the Filament evaluation measured 2.53 ms;
    specialisation (ADR-1815) shrank the march's cost too.
  - Rift Live: **3.11 ms** (35.22 -> 32.13 ms).
  - Talking Heads, Aurora Borealis: nothing (0.05 ms and 0.00 ms; their frames are byte-identical with and without):
    their lights ask for no contact shadows.
  - Event Horizon: -0.55 ms, inside the noise (NOT A RESULT); Rainbow World: +0.04 ms (VOID, and too small to matter).
    The first runs of both fell on another agent's load and were re-run.
- **What it costs in the picture** (realtime tier, 1920x1080, one frame, on against off): the material lab 0.68% of
  pixels (largest 125 levels: the dark line where a sphere's shadow meets the floor); Rift 1.13% (92 levels).
  Review: `~/Desktop/av-gen-review/103-render-improvements/a6-contact-shadows/` (`*-contact-ON.png`,
  `*-contact-OFF.png`, `*-crop-contact-on-off.png`, `*-difference-x4.png`).
- A ladder step that differs only in contact shadows now counts as a step (`effectChanges`), so the controller expects
  a gain from it.
