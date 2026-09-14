# Phase 7 — the dangling-name check, the upland, and the limit of the measurement

---

## 1. The dangling material-program check, adopted

The Tree of Life's ADR-176 names the mechanism behind Phase 5's green gills exactly:
`pbr_shade.wgsl` does `matEmissive = vec4(program.emission, 1.0)`, so **a program replaces emission
and the material's intensity lane is discarded**. There is no way to author emission as an input
without changing the shader — which retires the "clean fix" Phase 6 tried and failed at, and makes
the two-programs-per-colour-family answer the correct structural one rather than a workaround.

The half worth adopting here is the **dangling-name** rule, because it is the failure that would have
caught the regression before a render did. `MaterialProgramTable::slotOf` returns −1 for a name it
does not have; the program is skipped and the surface renders with its *authored* material as though
nothing were wrong — indistinguishable from a program that ran and did nothing.

`scene::danglingMaterialPrograms(scene)` returns the names a surface references and the scene does not
carry, sorted and deduplicated, and `Composition` logs them once per rebuild. Five tests, including
two that matter more than the happy path:

- **a water program is not dangling.** A water surface finds its settings by material-program name
  (ADR-099), so a name matching a water resolves even though no `MaterialProgram` carries it — the
  negative control against this rule's own false positive.
- **Glowmere Valley 2 names nothing it does not carry.** The scene file is walked, every
  `material.program` collected, every referenced program loaded from disk by name. A regression guard
  on the real scene rather than on a fixture.

## 2. The upland

The Phase 3 ladder did its job too well. With groundcover banded off the upper slopes, the east wall
carried pines and nothing else, and wide shots read bare on that side once the heroes drew the eye
west. Two bands widened: `bushes` 24 → 38 m, `grass` 11 → 17 m. Bushes are the right species for an
upland — small, sparse, silhouette-forming — and 38 m reaches the wall's shoulder without reaching its
crest.

The result is open but no longer empty. It is a smaller change than the problem seemed to warrant,
which is usually the right size for a composition complaint.

## 3. The limit of the measurement, found by pushing it

Phase 6 established that arms must be interleaved inside one process. **That turned out to be
necessary and still not sufficient**, and finding out cost one more impossible result.

The first interleaved version co-located the arms but ran them in a fixed order — "no heroes" always
second. It reported that hiding six mushrooms made the frame **2.3 ms slower**. Same impossible sign
as the two-invocation version, from a *bias* rather than from noise: if the machine drifts during a
run, an arm that always goes second always pays for it. So:

> **Interleaving must be counterbalanced.** Co-locating arms in one process is not enough; the arm
> order has to alternate, over an even number of runs, so each arm goes first half the time.

Counterbalanced, over four runs, the valley axis answers cleanly — **+0.85 ms for six heroes**,
consistent with Phase 6's +0.66 ms. **The opening view still returns an impossible −3.5 ms.**

So the honest statement is not a delta:

> **The six hero mushrooms cost less than this method can resolve.** Every plausible reading puts
> them between +0.2 and +0.9 ms, and the readings that are not plausible put the noise above that. A
> number below the floor of the instrument is not a number.

What would fix it is small and is named rather than built: **a control arm re-measured at the end of
the run.** Measure arm A first, run everything, measure arm A again; if the two differ by more than
the effect being claimed, the run is void. That is a drift *detector*, which is the thing this
project does not have and which counterbalancing only averages over.

### What is measurable, and it is enough

Absolute frame times over every run in this phase: **9.6 – 14.7 ms**, against a 16 ms ceiling. The
scene fits, by more than the noise, which is why the budget conclusion survives a measurement that
does not.

This also exercised Phase 6's own corollary in the right direction. Widening two habitat bands can
only *add* vegetation, so its sign is known in advance and the delta was never the question — the
question was whether the absolute still fits, which one interleaved run answers without a second
scene and a harness to alternate them.

## 4. Housekeeping: an ADR number collides

**This branch's ADR-174 and the Tree of Life's ADR-174 are different documents.** Mine is
"Vegetation is banded on height above the water table"; theirs is not. Renumbering was expected and
was flagged from Phase 1, but it needs doing *at merge* rather than discovering afterwards, because
both files are called `ADR-174-*.md` and one would quietly win. ADR-175 and ADR-176 are so far
distinct — 175 is mine (`PrimitiveKind::Generated`), 176 is theirs (emission ownership).
