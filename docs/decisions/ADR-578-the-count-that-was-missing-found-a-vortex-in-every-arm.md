# ADR-578: the one number §39 asked for that nothing reported, and what it found in an hour

Status: accepted. Date: 2026-09-21. Phase I of the Fog Bank brief (§39), and a correction to this
branch's own diagnostic arms.

## Context

§39: *"Profile density generation, compute/advection, ray marching, shadow integration, temporal
reprojection, memory, 3D texture updates. Capture GPU ms, CPU ms, memory, resolution, step count,
**active volume count**. Do not guess where the cost is."*

Audited against the engine rather than assumed, most of that list already exists and is good:

| §39 asks for | where it is |
|---|---|
| GPU ms, per pass | the headless log's `gpu N ms over M passes: volume.march=... volume.composite=...`, split by ADR-140 precisely so the march and the composite are two decisions |
| CPU ms | `--profile-cpu`, `--profile-csv` |
| resolution, step count | the `workload:` line's `volumeSteps` and `volumeTarget` |
| a machine-readable record | `--bench-json` |
| an honest comparison | `--ab <phase>`, interleaved in one process with a calibrated noise floor |
| **active volume count** | **nothing** |

One item, and it is the one ADR-560's headline defect was about: a second medium in a scene
rendered as nothing and no record said so.

## Decision

`VolumeStats` gains three numbers and the `workload:` line prints them:

    media=2+0dropped fogShadow=4steps

- **`media`** -- how many placed media the march marched. §39's missing item.
- **`mediaDropped`** -- and how many did not fit (ADR-560's number, which until now had exactly one
  reader in the tree and it was a CPU conformance finding, so in a running editor it did not
  exist).
- **`shadowSteps`** -- ADR-570's self-shadow march, which is a 2.4x--5.4x multiplier on this pass
  and was not in any record either.

**With a test, because instrumentation without a guard is the same family as every dead knob this
branch has found.** A counter that is written and never read, or read and never written, looks
exactly like a working one from the outside. The case renders one medium, then two, then a frame
with three dropped, and checks the record follows; the break -- replacing the count with the
constant 1 -- fails it at `1 == 2`.

## What the number found, in about an hour

**The first arm rendered after it reached the log printed `media=2` where one had been placed.**

`examples/treeisland/tree-of-life-floating-island.scene.json` declares a `Cosmic Vortex`, and a
project's `atmosphericEffects` list **merges** with the scene's rather than replacing it. So every
fog arm this branch has ever rendered -- the six §46 C primitive silhouettes, the four §46 D height
arms, the self-shadow before-and-after -- **had the Cosmic Vortex in the frame as well**, and three
generator files said in their own comments that they did not. `make_height_fog_arms.py` was the
most wrong: *"No placed medium in any of them: this is the ENVIRONMENT's own layer."*

**What it does and does not invalidate, stated precisely:**

- **The comparisons stand.** The vortex was *constant across every arm in a set*, so a difference
  between two arms is still the parameter that differs. This is the one case where an artefact is
  harmless: it is constant across the thing being varied.
- **The descriptions do not.** Any sentence claiming an arm contained one medium, or none, was
  false. Those are corrected where they live -- in the three generators -- rather than only here.
- The generators now switch the scene's vortex off by parameter (`atmos/Cosmic Vortex/enabled`),
  and a re-render confirms `media=1`.

**Nothing else in the record would have said so.** Not the frame, which looked like a cosmic scene
because it is one; not the timings, which were being read as deltas; not the pass list. A count of
the thing being marched was the only instrument that could distinguish "one medium" from "one
medium and a nebula", and §39 asked for it in so many words.

## Consequences

- **This is the strongest argument in the brief for §39 that the brief itself could not make.**
  *"Do not guess where the cost is"* is usually read as being about optimisation. Here the missing
  number was not about cost at all -- it was about **what was in the frame**, and its absence had
  been quietly corrupting the descriptions of every diagnostic arm this branch produced.
- **It is also the fourth time an artefact of the measuring apparatus has turned up in a
  measurement here**, after the probe scenes in the shipped-content census (`docs/testing.md` 27),
  the mid-run shader edit (25) and the concurrent suites (29). The generalisation in 27 covers it:
  *when your tooling writes artefacts into the same namespace as the thing it measures, every later
  measurement of that namespace is contaminated until somebody notices.* Here the namespace was the
  scene's effect list and the artefact was somebody else's.
- **A record is a claim like any other.** The generators' comments were the stated reason; the
  count was the evidence; they disagreed. ADR-385, on tooling this time.

## Revisit when

- **Memory reaches the record.** §39 lists it and nothing reports it. It is the one item on the
  list still missing, and it is a different kind of work -- Dawn does not hand out an allocation
  total, so it means counting the renderer's own buffers and textures.
- **A second project merges effects with a scene.** The merge is the behaviour that made this
  possible, and it is not documented anywhere a project author would meet it.
