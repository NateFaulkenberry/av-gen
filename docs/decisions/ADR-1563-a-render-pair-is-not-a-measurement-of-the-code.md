# ADR-1563: The water-cascade identity tests count a difference only if it repeats

**Status:** Accepted, 2026-10-20 · **Range:** 1560-1569 (eng/perf-reliability)

## Context

On 2026-10-19 one case failed once, in a full GPU-suite run under load:

> "the QA scene's river is byte-identical to the shader with no cascade code wherever it is flat"
> (`CHECK( movedFlat == 0 )`, `1 == 0`)

The case renders the QA river three ways, each with its own `SceneRenderer`:
- the live water shader;
- the shader with the cascade blocks stripped;
- a "flag" shader that marks the pixels the cascade path takes.

It then requires the first two to be bit-identical at every pixel the flag render does not mark. It passed 3 of 3
runs when run alone.

**Investigation.**

- **Alone, the case is stable.** Six runs gave the same image hashes for all twelve renders.
- **Repeating it under contention did not fail it.** Four parallel loops with eight CPU hogs, then random order
  across the whole `[cascade]` and `[water]` sets, made about 900 runs with no failure.
- **Hashing every render under contention found the cause.** Four parallel loops plus CPU hogs, each run's twelve
  hashes compared:
  - In 316 runs, one render of the **live** arm differed: pose 1 at 3 s, hash 15743… against 10907… in every other
    run.
  - A second stress run of 600 test runs dumped one odd render of the **flag** arm for the same frame. The live and
    stripped renders of that run were identical to the reference.
  - The odd flag render differs from the reference at **21 pixels**, all bright sun-glint pixels on flat water near
    the horizon (columns 143-205, rows 59-70). The luminance differs by up to 0.1, for example 0.252 against 0.157.
  - None of the 21 is a cascade pixel; the nearest is 13-69 px away.
- **So the renderer itself is not bit-repeatable under GPU contention**, about once in several thousand renders. It is
  not the cascade code, and it has been seen in two of the three arms. Had either odd render been the live or the
  stripped arm's in the failing comparison, a few glint pixels "moved on flat water", which is the reported failure.
- **A probe did not narrow it.** `[.probe-repeat]` renders the same frame with fresh renderers, one at a time and
  three alive at once. It ran 2,000+ renders across 440 processes under the same contention without a single odd
  frame, so the trigger also depends on something the probe does not do. The glint is the directional light's GGX
  term times `shadowFactor`, so the shadow atlas is the first suspect, but this was not proved.

## Decision

1. **A flat difference must repeat to count.** The QA-river case counts a flat pixel against the cascade code only if
   it differs in two independent renders of both shaders, each with its own flag render. A difference the code makes
   is in every render; render noise is in one.
   - When the first pair differs and the second does not, the case `WARN`s with the pixel coordinates and values,
     so the noise stays visible in the log.
   - The slab case ("flat water is byte-identical…") gets the same rule for its whole-image hash.
2. **The test still catches a real leak.** Checked by mutation: one line inside a cascade block scales the bed depth
   on every pixel (`vertical *= 1.01`), and the case fails at all four frames.
3. **The probe is kept** as a hidden test (`[.probe-repeat]`; `AVGEN_PROBE_OFF`, `AVGEN_PROBE_N`,
   `AVGEN_PROBE_DUMP`) for whoever takes up the nondeterminism.

## Consequences

- The flake no longer fails the suite. The noise is reported as a `WARN` instead.
- **Open:** the renderer's rare non-repeatability under GPU contention is not fixed. It touches a few dozen pixels in
  thousands of renders and was seen only on sun-glint water pixels.
  - Any other test that requires two renders to be byte-identical can trip on it the same way.
  - Seek-equals-play comparisons in the same process have not shown it.
