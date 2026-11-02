# CHRONOS: Temporal Singularity, coordinator notes

The governing brief is `20-singularity-brief.md` (the owner's, verbatim). It replaces "The Devourer
of Souls" (10-, 11-). These notes add the working rules and the stops. Where the two differ, the
brief wins.

## What carries over from the Devourer, and what goes

- **Keep the engine:**
  - the temporal engine on main (ADR-1931 ring and composed sampler, ADR-1932 pre-roll);
  - **ADR-1934** (identity-gated history: per-slot trail, fracture and squeeze). ADR-1934 is the
    natural tool for brief §5G, localised destruction per figure; extend it rather than
    duplicate it.
- **Keep the soul figures as a starting point.** The eight MPFB CC0 adults and `build_souls.py`
  are recognisable human figures already proven in the pipeline. The brief wants a small set of
  distinct, well-posed figures (§4); re-pose them or reclothe them as the art needs.
- **Remove the creature.** The owner has abandoned the creature, face and mouth (brief §1, §14).
  Per ADR-441 (no compat shims), remove the Devourer scene art in a clean first commit:
  - the creature build (`build_scan_creature.py`), the D1/D2 scene files and their scripts;
  - anything in tests or docs that only exists for the creature.

  Git history keeps it. Keep the D0-D2 docs as history, marked superseded in PROGRESS.md. The
  Sketchfab head scans in the gitignored `assets/chronos/sketchfab/` are no longer needed; leave
  them on disk and do not reference them.
- **The vortex rule is overridden for CHRONOS only.** The owner's standing "no vortex/funnel hero"
  rule still applies to every other scene. Here a vortex is the required subject, but not a
  generic static symmetric spiral (brief §1, §3, §10).

## Stops (stop and hand back at each one; the owner reviews)

- **Gate S0: recon, plan, and Test A. STOP.**
  - Do brief §11's inspection and write a SHORT plan (`22-singularity-plan.md`). Cover:
    - how the singularity is built (shader layers, particles/ribbons, geometry, or a mix);
    - how the gravitational field is shared by figures, trails, light and background (§6: one
      field, not unrelated filters);
    - how ADR-1934's slots map to figures;
    - budgets.
  - Then build Phase 1: the singularity and its luminous structures alone, with the full-screen
    background (§8). Test A must carry the scene with no figures.
  - Show stills and a 30-60 s clip with sound. Audio mapping is not required yet; the scene must
    look good with audio off.
- **Gate S1: Phases 2-4.** The figures, their temporal transformation, and gravitational
  distortion (Tests B, C, D). STOP.
- **Gate S2: Phases 5-6.** Feedback and chaos, the drum stem, polish, and the profile (Tests E, F,
  G), with a full render with sound and both suites at exit 0. STOP, ready for merge.

Research like a VFX pro before building (references, techniques, cheap proofs), but keep S0's
written plan short; the owner wants to see the singularity.

## Rules

- **Branch and ADRs:** same worktree and branch (`../av-gen-chronos`, `scene/chronos`), now on
  main 60d65ba8 with ADR-1934 on top. ADRs continue from 1935 up to 1969.
- **Media** goes in `~/Desktop/av-gen-review/104-chronos/singularity-s0/`, then `singularity-s1/`
  and `singularity-s2/`. Give exact file names in each hand-back.
- **Audio:** drum stem only (Feline Footwear's Drums stem, as in D2). Use the existing response,
  beat and stem signals. No kick/snare/hat classifiers.
- **Owner sensitivities:**
  - flicker and temporal instability, measured on video;
  - no empty dead space; every region of the frame should contribute (brief §2, §8);
  - no synchronised flashing (Test F).
- **Content:** unsettling, never gore. Figures have neutral, minimal clothing and are never
  sexualised.
- **Assets:** free, clearly licensed only, logged in `assets.md`, kept in a gitignored path and
  never committed (the repo is public). Stop and ask if a download needs a login.
- **Owner references:** `~/Desktop/chronos/` stays read-only and is never committed.
- **GPU and suites:**
  - every avgen/GPU run goes through `tools/gpu-lock.sh`; the GPU is shared with the Strange
    Attractor agent;
  - never `write --add` to the live baseline;
  - the scene stays out of `examples/index.json`;
  - suites run in the clean CI worktree, and the exit code is the only signal.
- **Engine and process:**
  - no major renderer refactor (brief §11), and no full-resolution intermediates without need;
  - generic engine features go in the engine with tests;
  - one agent, no subagents;
  - keep `PROGRESS.md` current.
