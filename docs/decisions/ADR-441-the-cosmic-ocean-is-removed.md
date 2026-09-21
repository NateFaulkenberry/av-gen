# ADR-441: The Cosmic Ocean is removed, and the owner's eye is the acceptance test the suite cannot run

- Status: Accepted (2026-09-20).
- Supersedes ADR-390 (the Cosmic Ocean is an atmospheric effect), ADR-393 (the cell was the halo)
  and ADR-450 (the nebulae get their own resolution). ADR-392 survives: its argument about the
  family generalising in three directions is independent of this kind.
- Relates to ADR-500 (an effect is one file and four lines), which is why the removal cost what it
  did and no more.

## The decision

The owner looked at the shipped Cosmic Ocean and cut it: *"let just get rid of the cosmic ocean
world effect altogether - it looks like shit."*

That is an art-direction call on a finished implementation, and it is the one class of judgement
no test in this repository can make. Everything the suite could say about this effect was green.
It round-tripped, it conformed, it resolved into its own bucket, it packed, it drew, its nebula
pass had its own resolution lever with three arms proving the lever moved. None of that is
evidence that it was worth looking at, and it wasn't.

Recorded rather than quietly reverted, because the next agent to read ADR-390 will find a careful
twelve-question design for a subsystem that is not in the tree, and the honest reason is here
rather than in a git log.

## What went, and what it cost

Deleted outright: `world/cosmic_ocean.{hpp,cpp}`, `world/world_effects/effects/cosmic_ocean_effect.cpp`,
`rendering/cosmic_ocean_renderer.{hpp,cpp}`, `ui/cosmic_ocean_rows.hpp`, `shaders/cosmic_ocean.wgsl`,
`tests/unit/test_cosmic_ocean.cpp`, `tests/rendering/test_cosmic_ocean_gpu.cpp`, and the seven
`examples/treeisland/_oc-*.json` diagnostic arms.

**No authored project referenced it.** `tree-of-life-floating-island.json` — the project it was
designed for and measured against — never named it. The only files under `examples/` that did were
the seven arms its own A/B harness left behind. A kind that shipped, conformed and drew, and that
nothing anybody renders had adopted.

The integration surface was 93 references across eight shared files, and that number is the
receipt on ADR-500's claim rather than a refutation of it: the *kind* cost four lines to declare,
and everything above the draw came from the registry and left with the file. What did not leave
cheaply was the half ADR-500's own preamble says the registry cannot collapse — a new integrator,
a GPU struct and a renderer pass. `EffectBucket::CosmicOcean` existed precisely to name that, and
it is gone with it. The family is back to three buckets and five kinds.

## The two guards that did their job on the way out

**`static_assert(sizeof(AtmosphericFrame) == ...)` in `frameDiffers` fired, in the shrinking
direction.** 2400 back to 1640. It had already earned its keep twice in one day when the frame
*grew* and `frameDiffers` read none of the three new members; this is the same question asked in
the easier direction, and the point is that an assertion on `sizeof` catches a member leaving as
well as arriving, where a checklist catches neither.

**The exhaustive `switch` on `EffectBucket`** is what made the dispatch arm safe to delete: remove
the enumerator and every site that must lose an arm is a compile error, not a runtime fall-through
into a neighbour's bucket. ADR-392 bought that guard after a kind no arm named resolved as a
vortex and was then dropped as "the second vortex in the scene"; it paid out in reverse here.

## What was removed alongside it, and was not part of the ask

Two defects in `src/ui/world_effects_panel.cpp`, both reported by the owner in the same breath as
the cut, and both worth naming because neither is about the Cosmic Ocean specifically.

**The panel offered the same kind twice.** A hand-written "Add cosmic ocean" button sat below the
registry loop. It predates ADR-500: when ADR-390 shipped, a new kind had to add its own button
there, and when the kind was ported onto the registry the loop began drawing it from
`schema->addLabel` — so there were two, and they did not even take the same route into the scene
(`cosmicOceanEffect` directly, against `makeAtmosphericEffect(schema->kind, ...)`). A hand-written
button for a kind the registry already draws is invisible until somebody counts the buttons.

**The Add row ran off the edge of the panel.** The loop was `ImGui::SameLine()` unconditionally,
so the row got wider every time a kind was added, and at six kinds the last button could not be
clicked. This is ADR-500's promise — adding an effect means no edit in this file at all — being
kept, and the cost being paid somewhere nobody was looking. The row wraps now (`WrapRow`), and the
helper is on both button rows in the panel so the next kind does not regenerate the defect.

Note the interaction the removal does not fix: **fewer kinds is not the fix.** The row is one kind
shorter today and the Tornado (ADR-580) and the rebuilt Fog Bank (ADR-560) will both put it back.

## Consequences

- `AtmosphereKind` loses `CosmicOcean`; `kAtmosphereKinds` goes 6 to 5; `EffectBucket` goes 4 to 3.
- `AtmosphericFrame` loses `hasCosmicOcean`, `cosmicOcean`, `cosmicOceanEnvelope` and
  `anyCosmicOcean()`, and `AtmosphericCounts` loses `cosmicOceans` and its `ResolvedAtmospheric`.
- The `cosmic` pass-arm toggle is gone, and with it ADR-390 §39's acceptance test.
- The `cosmicOcean [flow-reaches]` conformance finding, open at the time of the cut, is moot. It
  was a true finding — `flowInfluence` was registered, saved, modulated and drawn on the panel for
  a kind whose packer took no flow, so the row was a setting the picture did not keep — and the fix
  for it was written and then deleted along with the kind. Worth recording only because the check
  that found it is kind-agnostic by construction and will cover the Tornado on the day it arrives.
- A scene file that still names `cosmicOcean` loads with the key ignored, which is the ordinary
  behaviour of `fromJson` for an unknown block. Nothing in the tree writes one.
