# Art Pass 2: coordinator notes

The governing brief is `30-art-pass-2-brief.md` (the owner's, verbatim). These notes add the
working rules, the owner's later direction, and the stops. Where the two differ, the brief wins.

Starting point: main 214dafd8, which holds the merged Art Pass 1 (canyon → Matrix corruption → wall
portal → Moon → crater-floor portal → wall exit into the dusk canyon, about 60 s, ADR-1872..1884).

## The owner's direction since the brief

- **Portal sites: use the outer canyon walls.** "Canyon walls" includes the big walls set back
  from the river on either side, not only the river's own walls. Prefer those for this pass's
  canyon portals, because flying toward them, or out of them, shows more of the canyon scenery.
  Every portal must be part of real terrain or structure (a canyon wall, the pyramid, the
  launch-bay opening, the crater). None float in open air.
- **Portals open up as the camera nears.** A seam or crack grows into a torn opening, driven by
  camera distance (seek-safe and monotonic). The destination is visible through it from the
  slit onward, and the centre stays clear, with no camera-facing fragments in the middle.
- **The Matrix language is approved:** dense, world-anchored glyphs that start early around
  portals; mesh-wire scan fronts; data mosh ON THE WORLD GEOMETRY (blocks, strips, echoes), not
  as a screen filter. The ejection from the UFO (brief §6.6) is the one place where a strong
  image-space mosh is explicitly wanted, layered with the geometric corruption.
- **No look-back at the canyon-side tear of portal 2** (owner, 2026-10-31).

## Research first, then milestone stops

The owner wants deep research and planning before building, the same as Art Pass 1.

- **Gate R2: research, audit, sourcing and plan. STOP for the owner.**
  - Audit what exists (portals, multi-world rendering, corruption, mesh wire, Moon generator,
    dusk world, skinned characters, the GLB importer, glTF glass, glossy reflections, volumetric
    fog/beams, particles and fire, water) and what the asset library already holds (Quaternius,
    alien/farm packs, etc.).
  - Research each new environment and effect, with references and techniques:
    - the lunar terminator and domes;
    - campfire and ember lighting;
    - Mars/Cydonia geology and haze;
    - deep-ocean light and particulates;
    - the alien interior (Mothership Zeta's feel only);
    - the beam effects;
    - the observer camera treatment;
    - the violent ejection mosh.
  - **Asset sourcing:** propose a cast sheet before any build. For each candidate model (Sasquatch,
    Gray alien, Nordic humanoids, suited man, goat, UFOs and submersibles, gurney, interior props,
    pyramid or stone references), give:
    - the source URL;
    - the licence;
    - the polycount and textures;
    - rigged or not;
    - a turntable or still render in AV Gen, or in Blender if that's quicker.

    Include fallback build plans (Blender headless or AV Gen procedural) where no good licensed
    asset exists.
  - **Timeline and music:** the sequence will run several minutes. Propose the beat sheet and
    timings per environment, and how the music works: the full Feline Footwear, which you should
    measure, or the owner choosing another track. Flag this as a decision.
  - **Cheap proofs** of the riskiest pieces:
    - the lunar terminator crossing;
    - a dome light spill;
    - Sasquatch fur at a distance with fire light;
    - an underwater light-shaft look;
    - the interior lighting;
    - the beam plus triangle.
- **Milestones M1–M5** as in the brief (§11). STOP for the owner after each, with stills, short
  motion tests, a render of the sequence so far with sound, the flicker metric and both suites at
  exit 0.
- **Merge:** the owner merges as milestones are approved. Keep the branch rebased, and prepare
  each approved milestone for a fast-forward.

## Rules

- One agent, no subagents.
- Branch `scene/strange-attractor` in `../av-gen-attractor`. ADR range 1885–1929.
- Media goes in `~/Desktop/av-gen-review/101-strange-attractor/pass2-r/`, then `pass2-m1/` …
  `pass2-m5/`. Give exact file names in each hand-back.
- **Assets:** downloads are approved by the owner from legitimate sources with clear,
  commercially usable licences (the brief's list: Sketchfab per individual licence, Poly Haven,
  AmbientCG, Quaternius, Kenney, and similar). Record every asset's source, author and licence in
  `docs/prototypes/strange-attractor/assets-pass2.md`.
  - The repo is public, so **never commit third-party assets**. Put them in a gitignored path
    (e.g. `assets/strange-attractor/`), the same pattern as `assets/space/`.
  - No game or movie rips.
  - If a download needs a login or account, or a licence is unclear, stop and ask.
  - Blender is at /Applications/Blender.app (not on PATH); run it headless for builds and
    conversions.
- Reuse the canyon terrain for all four times of day: the existing day, dusk, night and dawn
  worlds share one terrain. No duplicated canyon geometry.
- Content: the exam scene is unsettling and mysterious, never gore or graphic violence (brief
  §6.3).
- The canyon's quality must not regress. Show before/after frames of the opening (0–16 s) at each
  milestone.
- Generic engine features go in the engine with tests. Scene art goes in the scene. No compat
  shims.
- Every avgen/GPU run goes through `tools/gpu-lock.sh`. Never `write --add` to the live baseline.
  The scene stays out of `examples/index.json`.
- Owner sensitivities:
  - flicker and temporal instability, measured on video;
  - no visible world edges;
  - no vortex or funnel heroes;
  - characters: well-composed and subtly animated beats elaborate but weak animation.
- Offline quality first; live performance is a soft constraint.
- Keep `PROGRESS.md` current.
