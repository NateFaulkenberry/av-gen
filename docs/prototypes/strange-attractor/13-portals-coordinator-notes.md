# Dimensional portals pass: coordinator notes

The governing brief is `12-dimensional-portals-brief.md` (owner, verbatim). These notes add the
working rules and the stops. Where the two differ, the brief wins.

## The owner's emphasis: deep research before building

The owner asked for deep research before any building starts. Treat Gate R as real VFX
pre-production, not a formality:

- Study the actual techniques at source level where possible: shader breakdowns, GDC/SIGGRAPH
  talks, open-source implementations, and tutorials. That covers digital rain and code-glyph
  rendering; datamosh (P-frame / motion-vector smear and its real-time imitations); portal
  rendering (stencil and oblique near-plane clipping, recursion, render-to-texture, the
  camera-crossing problem); geometry disintegration and voxel/fragment break-up; and lunar
  photography (Apollo surface photography, LRO imagery, opposition surge, regolith BRDF such as
  Hapke or Lommel-Seeliger, the lack of atmosphere, earthshine).
- Debate the approaches. Write down at least two candidate techniques for each of the four hard
  problems (Matrix scan/breakdown, the portal view, the crossing, the Moon), with their costs,
  risks and what they would look like.
- Prove the risky parts cheaply before planning around them, with throwaway stills or
  very short clips:
  - the portal view: one environment seen through an aperture in another, with correct
    perspective and depth;
  - a camera crossing a portal plane with no cut;
  - a 2-3 s look test of the scan evolving into breakdown on real canyon geometry;
  - a lunar terrain and lighting still.
- Audit what AV Gen already has (brief Phase 1) before proposing anything new. That covers
  `meshWire` (ADR-1862), Digital Mosh (ADR-1160..1166) and the temporal post effects (frame echo,
  datamosh, feedback, slit-scan), the deformers including `blocks`, the Environment seam
  (ADR-1200/1221), refraction and the frame copy (ADR-1819/1820), render targets, and multiple
  cameras.

## Stops (stop and hand back at each one; the owner reviews)

- **Gate R: research, audit and plan.** Write `14-portals-research.md` (references, techniques
  and the approach debate, with links) and `15-portals-plan.md` (the chosen architecture, the
  sequence beat sheet with timings, the risks and the gate plan). Include the proof stills or
  clips. No production code beyond throwaway proofs. STOP.
- **Gate A: the Matrix effect** (the top priority). The scan refinement plus the breakdown
  layers, with the intensity progression running on the real canyon flight up to the portal
  approach, with sound. STOP.
- **Gate B: portal 1 and the Moon.** The portal and the crossing into the lunar environment, plus
  lunar exploration. STOP.
- **Gate C: portal 2, the dusk return, and the full ~60 s sequence** with sound, an offline render,
  and both suites at exit 0. STOP.

## Rules

- One agent, no subagents.
- Branch `scene/strange-attractor` in `../av-gen-attractor`, at main ec422142. ADR range
  1872-1899.
- Media goes in `~/Desktop/av-gen-review/101-strange-attractor/portals-r/`, then `portals-a/`,
  `portals-b/` and `portals-c/`. Give exact file names in each hand-back.
- Lattice hold lifted: the owner is now directing the art, and this brief explicitly asks to
  evolve the triangle tracing (meshWire).
- Do not redesign or replace the canyon terrain. The canyon look (cirrus bands, water, shadows,
  ADR-1864..1871) must stay at least as good as now. Prove it with before/after frames of the
  opening at each gate.
- Generic engine features go in the engine with tests (portal views, environment switching,
  disintegration). Scene-specific art goes in the scene. No compat shims.
- Downloads: web research is fine. Cloning open-source repos for study is approved. Do not
  download or commit image, video, texture or DEM data without asking first. If public-domain
  NASA/LRO data would materially help the Moon, propose it at Gate R with the exact source and
  licence. Never commit stems, commercial assets or third-party assets; the repo is public.
- The scene stays out of `examples/index.json`.
- Every avgen/GPU run goes through `tools/gpu-lock.sh`. Never `write --add` the live baseline.
- Audio: the existing Feline Footwear project (read-only source files). The sequence grows to
  about 60 s.
- Owner sensitivities: flicker and temporal instability, which must be measured on video, not
  stills; no visible world edges; no vortex or funnel heroes. A portal "pulling geometry toward a
  focal point" is fine; a spiral tornado is not.
- Offline quality first; live performance is a soft constraint.
- Keep `PROGRESS.md` current.
