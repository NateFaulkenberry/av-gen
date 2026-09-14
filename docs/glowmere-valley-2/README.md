# Glowmere Valley 2

A successor showcase scene: a valley and river that traverse the whole map, ecologically structured
vegetation, six procedurally searched hero mushrooms, and an Auto-director that can hold one
continuous shot.

**Status: Phase 2 in progress.** Phase 1 (research and architecture) complete; the shared
candidate-search framework is published and its generic half is implemented and tested.

| | |
|---|---|
| [01-audit.md](01-audit.md) | What Glowmere Valley actually is, file by file, and the reuse matrix |
| [02-research.md](02-research.md) | The research, the selected methods and why, the candidate-search architecture |
| [03-baseline.md](03-baseline.md) | Build, test and render baseline for the original — and why its timings are not evidence |
| [04-plan.md](04-plan.md) | Phased plan, risk register, and the eight questions Phase 2 needs answered |
| [05-candidate-search-interface.md](05-candidate-search-interface.md) | **The shared candidate-search framework.** Read this if you are writing a searched procedural generator — mushrooms or trees |

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
