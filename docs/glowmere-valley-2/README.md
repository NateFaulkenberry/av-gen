# Glowmere Valley 2

A successor showcase scene: a valley and river that traverse the whole map, ecologically structured
vegetation, six procedurally searched hero mushrooms, and an Auto-director that can hold one
continuous shot.

**Status: Phase 8 complete.** The scene exists, is listed, its
geography and its habitat field are tested, and it renders at **13.44 ms** against a 16 ms ceiling —
parity with the scene it succeeds, on four times the readable ground. The shared candidate-search
framework is published with its generic half implemented.

| | |
|---|---|
| [01-audit.md](01-audit.md) | What Glowmere Valley actually is, file by file, and the reuse matrix |
| [02-research.md](02-research.md) | The research, the selected methods and why, the candidate-search architecture |
| [03-baseline.md](03-baseline.md) | Build, test and render baseline for the original — and why its timings are not evidence |
| [04-plan.md](04-plan.md) | Phased plan, risk register, and the eight questions Phase 2 needs answered |
| [05-candidate-search-interface.md](05-candidate-search-interface.md) | **The shared candidate-search framework.** Read this if you are writing a searched procedural generator — mushrooms or trees |
| [06-phase-2.md](06-phase-2.md) | Phase 2: the geography — three authoring errors worth reading, and a defect that turned out to be the measurement |
| [07-phase-3.md](07-phase-3.md) | Phase 3: the vegetation — the riparian ladder, and why negative space *is* the performance budget |
| [08-phase-4.md](08-phase-4.md) | Phase 4: the hero mushrooms — the generator, the search, and the five defects the contact sheet found |
| [09-phase-5.md](09-phase-5.md) | Phase 5: the heroes placed, two art-direction overrides, and ADR-173's falsifier run |
| [10-phase-6.md](10-phase-6.md) | Phase 6: hero materials, and a 25% between-invocation variance that made an earlier A/B meaningless |
| [11-phase-7.md](11-phase-7.md) | Phase 7: the dangling-name check, the upland, and why interleaving must also be counterbalanced |
| [12-phase-8.md](12-phase-8.md) | Phase 8: the Auto-director — the rename, shot modes, the settings panel, and why the camera used to stop at every cut |

## The five findings worth reading first

1. **There is no single Glowmere Valley.** Seven index entries over two scene files, and the one
   literally named *Glowmere Valley* is the wrong one (`01-audit.md` §0).
2. **The terrain system already stamps authored rivers onto noise** — which is the architecture this
   design selected before finding it built. The work is authoring plus two terms, not a new system
   (`01-audit.md` §1.1, `02-research.md` §1.2).
3. **The water already follows a descending river course.** The open question was answered by reading
   (`01-audit.md` §1.3).
4. **The camera director already has a `continuous` flag, on by default and invisible.** The path is
   C⁰; it is velocity that breaks. That makes the continuous shot a much smaller job than the brief
   assumes (`02-research.md` §5.1).
5. **The engine is missing exactly one primitive**: per-angle radius modulation on the sweep. It is
   why the elder's cap is a perfect ellipse (`02-research.md` §3.1).
