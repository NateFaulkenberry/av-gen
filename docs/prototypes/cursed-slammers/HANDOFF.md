# Cursed Slammers: The Apparition — handoff (start here)

**What it is.** A full-screen, lenticular-inspired live scene. A huge Kong-inspired ape emerges from a dark, ridged
"print", grips foil bars that sit in another optical layer, holds, is disturbed, and dissolves back. One cycle loops.
1080p30.

**State, 2026-10-25.**
- Research and the throwaway probe are done. The plan is written.
- **Waiting on the coordinator's go and the owner's download decision (03 §8 D1).**
- Branch `scene/cursed-slammers` in `../av-gen-slammers` (base main daf93949).
- No engine code changed.

## Read, in order

1. `00-brief.md`: the owner's brief and follow-up note, verbatim. It governs.
2. `03-plan.md`: the slice, the rendering approach, the 64 s cycle, the audio rows, the budget, the ADRs (none
   needed yet), the tasks, the decisions.
3. `01-research-optical.md`:
   - §1 how lenticulars and foil behave (sourced);
   - §2 what the engine already has;
   - §3 approaches;
   - §6 the probe's readings.
4. `02-research-asset.md`:
   - §1 the candidates, verified on the live listings;
   - §2 the download list;
   - §3 the Blender route;
   - §5 the animations.

## Starting material

| What | Where |
|---|---|
| Probe subject generator (a crude head, an eye, a gripping hand and its mirror; Blender 5.2.1 headless) | `probe/make_probe_subject.py` |
| Probe scene arms P1-P4 (kit-based; programs for the ridges, the foil and the flip are here, ready to promote) | `probe/optical_probe.py` |
| Probe clips and contact sheets | `~/Desktop/av-gen-review/cursed-slammers/probe/` |
| Candidate thumbnails (review only, never in the repo) | `~/Desktop/av-gen-review/cursed-slammers/prep/` |

**To re-run the probe:**
1. `Blender -b --factory-startup -P probe/make_probe_subject.py -- --out <dir>`
2. `python3 probe/optical_probe.py <dir>`
3. `tools/gpu-lock.sh build/release/src/avgen --project <dir>/P3.json --render <dir>/P3.mp4 --range 0:16 --fps 30 --tier high --codec h264`

## Rules that bite

- **Build:** `cmake --preset release -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen-borealis-eng/.cache/cpm`,
  then `cmake --build build/release --target avgen`.
- **GPU:** every GPU job goes through `tools/gpu-lock.sh`, one job per hold (other agents share it). On AC: check
  `pmset -g batt`.
- **Assets:** no downloaded or commercial asset in git. `assets/cursed-slammers/` is gitignored with a manifest. Our
  own generated meshes go in `examples/cursed-slammers/meshes/`.
- **Signals:** the mix kick, snare and hat are retired. Use `response.low` / `onset` / `onset{Low,Mid,High}` /
  `bass` / `intensity`, `stem.*` / `role.*`, and `beat.*`. Never `macro.*` (one frame late).
- **Procedural nodes draw blended materials opaque** (ADR-1572). Any translucent layer must be a gltf node.
- **Judge it moving.** Every review artefact for the optics is a side-to-side clip. A still alone proves nothing
  (the owner).
- **Never touch** `../av-gen` (the owner's checkout), `../av-gen-borealis` or `../av-gen-talkingheads`.
