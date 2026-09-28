# Owner's brief (2026-09-28), abridged to its requirements

This is the owner's brief as given on 2026-09-28. Its sections are kept, with the wording condensed. Where the two
disagree, the owner's original wins.

**Title:** AV Gen: QA, CI Hardening, Performance Investigation & Cleanup Pass.

Feature development stops for a dedicated QA, testing, cleanup and performance investigation pass. **No new
user-facing features.** The repository should be left cleaner, more testable and more reliable, with CI doing as
much of the validation as reasonably possible, and the severe performance problems in **Glowmere Valley 3**
investigated.

## Situation
GV3 makes the app extremely slow:
- single-digit FPS is common, even at low resolution;
- playback is possible but inconsistent;
- some close-ups exceed about 30 FPS, while wide shots collapse to single digits;
- navigating and manipulating the world is also slow.

**The hypotheses:**
- A: scene complexity or renderer limits. The difference between shots points to view-dependent cost.
- B: GV3 has accumulated bad project state from being copied and iterated from earlier Glowmere projects: stale
  or duplicated resources, obsolete entities, hidden but expensive objects, broken references, legacy config,
  duplicated effects, extra cameras, bad serialization, pathological transforms, deep hierarchies, orphans.
- C: an engine regression.
- Or a combination of these.

Do not guess. Instrument and compare.

## Primary goals
1. clean, reliable GitHub CI;
2. long-running ASan in CI;
3. long-running TSan where technically appropriate;
4. long-running UBSan;
5. GPU testing on Actions where feasible;
6. a workable solution for proprietary GPU-test assets;
7. tests on the minimum practical asset set;
8. legitimate bugs found and fixed;
9. legitimate performance regressions found and fixed;
10. stale, broken or duplicated project state cleaned up;
11. the Glowmere Valley projects properly integrated into the repository;
12. GV3 formally investigated;
13. a clear determination of what limits GV3's performance;
14. no new features;
15. a clean final report.

## No feature development (hard constraint)
**Not allowed:** new rendering or scene features, effects, UI systems, optimization systems unrelated to
demonstrated regressions, new architecture, speculative abstractions, creative or agent functionality.

**Allowed:**
- fixing bugs and regressions;
- removing dead or duplicated state;
- correcting resource lifetime;
- removing unnecessary work;
- improving an existing algorithm when that directly addresses a demonstrated performance problem;
- tests, CI, and diagnostics or instrumentation;
- cleaning project data and removing unnecessary assets or state;
- fixing serialization, visibility or resource handling;
- a refactor needed to fix an identified problem;
- code quality work that materially reduces maintenance or testing problems.

If something looks like a feature, defer it.

## Sub-agents
- **At most 3 at once**, managed deliberately and optimised for tokens, each with a bounded task and explicit
  deliverables.
- **Suggested split:**
  - Agent 1: performance and the Glowmere investigation;
  - Agent 2: CI, testing and sanitizers;
  - Agent 3: repository and project cleanup.
- One agent eventually monitors GitHub Actions runs and responds to failures, so the primary agent does not poll.

## Phase 0: formal plan
Before any substantial change:
- **Inspect:** the repository, CI, the test architecture, the Glowmere projects and the profiling tools.
- **Establish** a baseline.
- **Write** a plan with workstreams, hypotheses, baseline measurements, experiments, expected evidence,
  implementation tasks, CI tasks, cleanup tasks, validation criteria and stopping criteria.
- **Save** it in the repository's docs.

## Phase 1: integrate the Glowmere Valley projects
- The projects are on the Desktop and go into `examples/`, beside the existing Glowmere projects.
- **First, inspect:** the structure and the conventions, references outside the project, absolute and
  machine-specific paths, and assets not in the repository.
- **Clean before committing**, so the repository holds a reproducible project.
- Document whatever cannot be included, and why.

## Phase 2: investigate GV3
- Do not conclude "large, therefore slow".
- **Baseline three scenes:** GV2, GV3 and a small representative scene. Use the same machine, resolution, settings
  and method.
- **Measure:**
  - FPS and frame time;
  - CPU and GPU time;
  - submission, traversal, culling, animation, audio, procedural generation, shadows, lighting and post;
  - draw calls, triangles and vertices;
  - visible entities and meshes, lights and shadow casters, materials and passes;
  - allocations, uploads and sync stalls.
- Use existing instrumentation first, and add diagnostics only where it is insufficient.

**View-dependent investigation.** Between a close-up and a wide, find what changes: geometry, entities, meshes,
triangles, lights, shadows, effects, particles, transparency and overdraw, post, clipping, animation, culling,
reflection and GI, volumetrics and fog, water. Is the collapse more visible objects, specific expensive objects,
specific passes, CPU scene processing, GPU load, or sync and resource management? Do not assume.

**GV2 against GV3.** Do a structured diff:
- counts of entities, assets, meshes, procedurals, lights, cameras, effects and materials;
- animation, audio-reactive config, hierarchy, render, shadow and post settings;
- water, atmospherics and hero effects;
- duplicated, stale or hidden resources, unused cameras or assets, orphaned references, and serialized state
  that no longer matches the scene.

Produce a scene-complexity report if possible.

**Cleanup experiment.** On copies, never the original, remove unnecessary state, one evidence-driven step at a
time. For each step, record what was removed, why, the performance impact and any visual change.

**Regression investigation.** Use git history on the renderer, traversal, culling, animation, procedurals,
effects, audio reactivity, resources, cameras, lighting, shadows and post. Compare known-good revisions. For a
reproducible regression: identify it, reproduce it, explain it, fix it, benchmark it before and after, and add a
regression test or benchmark.

**Performance fix standard.** Record the baseline, the change, the result, the workload and the method. No
micro-optimisation without profiling. **Priority:** pathological work, unnecessary work, repeated work, sync and
stalls, resource lifetime, culling and visibility, then large CPU or GPU bottlenecks.

## CI
**Cleanup.** Audit all of Actions:
- remove redundant workflows and duplicated setup;
- simplify the matrices;
- improve caching, artifacts and failure visibility;
- give each workflow a clear responsibility;
- keep local and CI behaviour consistent;
- remove flaky tests;
- separate quick PR validation from long-running validation.

The goal is reliable feedback, not raw speed.

**Sanitizers.**
- Long-running ASan, UBSan and TSan workflows.
- Not every test can run under every sanitizer: document the exclusions and why.
- Build with the right flags and run the complete relevant suite.
- Keep logs, fail on findings, upload diagnostics, and make failures easy to investigate.
- Nightly, not blocking every small PR.

**GPU CI and private assets.**
- Some GPU-test assets cannot be distributed publicly. The intended path: the public repository's CI checks out a
  private test-asset repository, then symlinks or mounts the assets into the GPU test fixture.
- **Requirements:**
  - no restricted assets in the public repository, and none ever committed to it;
  - authentication by GitHub Secrets, with no credentials in workflow files and none exposed;
  - a predictable asset location, and a sensible equivalent workflow locally;
  - a clear, understandable failure when the assets are unavailable.
- If Actions cannot do this, document the limitation and implement the best viable alternative.

**Minimise test assets:**
- tiny purpose-built fixtures where practical;
- no duplicated fixtures, and no loading of massive Glowmere assets for unit tests.
- **Tiers:** a unit test uses a tiny fixture; an integration test a minimal representative fixture; a GPU test the
  minimal required GPU fixture; Glowmere validation the real Glowmere assets.
- Do not sacrifice meaningful coverage to save size.

**Coverage audit.** Prioritise real risk: serialization, asset loading, resource lifetime, entity management,
renderer resources, cameras, the effect lifecycle, animation, audio analysis and modulation, GPU resource
creation and destruction, error handling, malformed data, missing assets, invalid references, and project
copying or migration. No arbitrary coverage percentages.

**CI monitoring.** One agent monitors runs, triages each failure (infrastructure, flaky, environment or defect),
fixes the legitimate ones, re-runs, and reports back.

**Overnight.** Separate PR, GPU integration, sanitizer and extended/nightly validation as appropriate, so the owner
can leave CI running overnight and return to meaningful results.

## Progress document
Keep one persistent document with these sections:
- Current Status;
- Completed;
- In Progress;
- Findings;
- Performance Baselines;
- Bugs Found (symptom, cause, fix, validation);
- Performance Regressions (revision, cause, fix, before and after);
- CI Status (PR, GPU, ASan, TSan, UBSan, nightly);
- Remaining Problems;
- Decisions Required From Human (only what investigation cannot resolve).

Update it at the end of each major phase.

## Final report
It answers:
1. Why is GV3 slow? Separate scene complexity, project-specific problems, engine regressions and unavoidable
   renderer cost.
2. Did GV3 contain bad or stale state? If so, what was found, what was removed and the measured impact; if not,
   the evidence.
3. Were there engine regressions? If so, the cause, the fix and the before and after.
4. What is the practical performance envelope?
5. Which testing gaps were fixed?
6. What CI infrastructure was added or cleaned?
7. Which sanitizer workflows exist?
8. How does GPU CI reach the restricted assets?
9. How were the test assets minimised?
10. What remains unresolved?
11. What should be investigated next? Recommendations only; do not implement them.

## Success
- The Glowmere projects are integrated, and GV3 is reproducible from repository state.
- The causes of GV3's performance are understood.
- Real project-state problems are cleaned up, and demonstrable regressions and important bugs are fixed.
- CI is materially cleaner.
- ASan, UBSan and TSan (where appropriate) run in CI.
- GPU CI works through the private-asset strategy or the best documented alternative.
- Fixtures are minimal where practical, and nightly validation runs without the owner's machine.
- A progress report and a final report exist, and no features were added.

**Do not declare victory because tests pass.** The primary question is whether we can explain GV3's behaviour. "AV
Gen cannot render this complexity interactively" is a valid result, if it is demonstrated and distinguished from
bugs, stale state and regressions. Be empirical: measure first, change one meaningful thing at a time, keep the
repository clean and the investigation documented.
