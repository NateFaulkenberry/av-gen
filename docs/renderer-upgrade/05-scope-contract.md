# Deliverable 10 — Scope contract

Every section of the upgrade spec, with its disposition and the reason. Agreed 13 September 2026.
**Three rulings from the spec author govern this table:** the measured ordering supersedes the spec's
phase order; impossible requests are ignored rather than attempted; and work radically outside AV
Gen's needs does not have to be implemented.

Dispositions: **doing** · **active** (in flight now) · **done** · **deferred** (with the evidence that
would justify it) · **impossible** (verified against the platform) · **out of scope** (a real
capability AV Gen does not need).

## Done before development began

| § | Item | Evidence |
|---|---|---|
| 1–2 | Design principle, research | `02-research-*.md`, `03-research-*.md` — cited against primary sources |
| 3 | Scalability audit | `01-audit-and-baseline.md` — six of the spec's own figures were stale and are corrected there |
| 42 | Offline determinism | `SYM-TERRAIN-1` fixed. Re-rendering a frame changed it; ambient occlusion dropped its history on a repeated frame index. Debug and release now agree, both suites green. |
| 57 | The Nanite research questions | Answered. Cluster visibility, geometric error, and what requires capabilities WebGPU lacks — see below. |

## Active now (wave 1)

| § | Item | Agent |
|---|---|---|
| 4, 43 | Performance lab: machine-readable output, p95/p99/1% low, A/B in one session | lab |
| 23 | Cluster occupancy instrumentation (**instrument only — the 32-light cap is not to be raised until overflow is shown to be real**) | lab |
| 10 | Multi-material instance duplication, fixed at the representation level | geo |
| 12 | LOD0 meshoptimizer path | geo |
| — | `uploadMeshes` version-key collision | geo |
| 25 | Shadow-mask normal mismatch (~5.6% residual) — **correctness, not optimisation** | shadowfix |
| 26 | Wide-scene cascade coverage | shadowfix |
| 31, 51 | Overdraw and fragment-density visualisation | viz |
| 44 | GPU timing tests under contention | viz |
| 6 | Limits inventory, documented | viz |

## Doing, in order

| § | Item | Phase | Why here |
|---|---|---|---|
| 27 | Contact shadow optimisation | B | **Measured: 2.36 ms, 22% of the scene pass** — the largest nameable item in the frame, and memory-bound on twelve dependent texture loads rather than arithmetic. Two optimisations were implemented and reverted; the obvious early-out is bit-exact and **4.4% slower** (ADR-118). |
| 37 | Transient resources / attachments | B | Apple: load and store actions "consume the majority of your app's system bandwidth" |
| 40 | Shader audit (`pbr`, `shadow_mask`, `volume` first) | B | the pass is fragment-bound; this is where per-invocation cost lives |
| 17 | Screen-space importance | C | the central system the spec asks for, and the input to everything below |
| 11 | Screen-space-error LOD selection | C | replaces distance thresholds with a measurable criterion |
| 13–14 | HLOD and its generation pipeline | C | **the main line** — quad overdraw confirmed at 4.9× |
| 20 | Material quality tiers | D | per-invocation cost for pixels that do not deserve full PBR |
| 18–19 | Subsystem quality budgets, not a global slider | D | the spec is explicit that binary quality is not professional scalability |
| 34 | Fixed render scale (**not** dynamic) | D | dynamic was already rejected on measurement |
| 35 | Editor / playback / offline profiles | D | one coherent policy object, not a pile of booleans |
| 24 | Importance-driven shadow budgets | D | |
| 6 | Lift `kMaxObjects` — replace the cap with a mechanism | E | Glowmere flattens to **278 entities** against a 256 cap |
| 33 | Volumetric scalability | F | 64% of Constellation, and a wholly separate problem from Glowmere |
| 45–48 | Stress scenes, scaling curves, certification | G | scoped below |
| 50 | Visual regression | G | frame-state baselines already committed |
| 53 | ADRs per architectural decision | all | ranges pre-assigned; collisions between concurrent agents are silent |

## Deferred, each with the number that would justify it

| § | Item | Justified when |
|---|---|---|
| 7 | Spatial hierarchy | traversal or culling is a measured cost. Today the entire cull pass is 0.33 ms and geometry work is 1.7% of the scene pass. |
| 8 | Hierarchical occlusion culling | measured occlusion ratio > ~30% on a canonical scene **and** Unity's other two preconditions (shared meshes enabling single draws; high-vertex occluded objects). Unity's own docs: "If occlusion culling doesn't have a big effect on your scene, rendering time might increase." An open valley under a low horizon is close to the worst case. |
| 9 | GPU-driven visibility | CPU encode exceeds ~10% of frame **and** Dawn-on-Metal ships multi-draw indirect. It is currently commented out in Dawn's Metal backend, so every draw stays one CPU call and the headline benefit is unavailable. Today: 0.66 ms CPU over 136 draws. |
| 28 | Temporal rendering, broadly | **Promoted, then demoted on measurement (ADR-132).** It was made a Phase C prerequisite because seamless LOD is said to depend on TAA and this engine has only FXAA. Measured, `lodSpread` already takes a simultaneous switch from 100% of a stand to 7.5% in the worst frame, so the stated reason does not hold and it returns to deferred. Justified when a transition is actually reported as visible. Temporal volumetrics still ride with Phase F and are evaluated on their own evidence. |
| 29 | Motion vectors audit | **Re-scoped.** It was scheduled with Phase C "which consumes them for transitions"; ADR-132 established that transitions need no cross-frame blending, so no part of Phase C reads them. The audit is still worth doing against their real consumer, motion blur. Auditing them for a consumer that does not exist would have been busywork with a tick next to it. |
| 30 | Particle scalability | Constellation's particles are **flat under resolution change** (0.52 ms at every size) — simulation-bound, not fragment-bound. Not a measured problem. |
| 32 | Water scalability | removing water changes the scene pass by +0.5 ms, i.e. nothing. Not a measured cost. |
| 36 | Frame graph / pass decomposition | **Deferred, and the trigger has now been tested rather than merely stated.** The condition was "the Phase B audit finds more than one or two wrong load/store actions". The audit ran (ADR-119) and found **none** wrong: discarding 24.6 MB/frame of auxiliary stores moved the frame &minus;0.99%, which is not a result. One target (normal+roughness) is written every fragment and read by nothing on the normal path — measured at zero, recorded so it is not mistaken for free money. |
| 38 | Pipeline/bind-group cost | the CPU is not the bottleneck; the spec says so itself |
| 39 | Material/draw sorting | pending a measurement showing sorting state changes costs something here |
| 52 | Performance dashboard | **Needs a human.** The data exists (`--bench-json`: percentiles, counters, conditions, session id). The panel is an ImGui surface, and this agent cannot see ImGui output, so it cannot honestly certify one as working. Buildable on request; somebody has to look at it. |

## Impossible — verified, not merely hard

| § | Item | Why |
|---|---|---|
| 2, 57 | Nanite virtualized geometry (the rasterizer) | It depends on a 64-bit atomic max into the visibility buffer packing 30 bits of depth over a 34-bit payload. **WGSL has neither 64-bit atomics nor texture atomics of any width.** Nanite's *hardware* path writes through the same atomics so there is no atomics-free variant. Its author: "Without that we wouldn't be able to do fast software rasterization." |
| — | Virtual shadow maps | Same wall, for the same reason — co-designed with Nanite's visibility buffer; writing into the sparse page pool depends on scattered atomic writes. Epic lists SM6.6 atomics as a platform requirement. |
| 21, 22, 40 | "Use actual Metal/Xcode profiling" | Ruled out by the spec author: no Xcode tooling in this repository. **The central question it was needed for is already answered in-engine** — a tessellation sweep at constant coverage, which is reproducible and committed where a capture would not have been. |

**What *is* portable from Nanite, and is being taken:** the DAG/LOD machinery — per-cluster-group screen-space error with locally-evaluable selection. Its rasterizer is not. These are separable, and separating them is the most useful result of the research.

## Measured as not the problem — the spec's hypothesis was wrong

| § | Claim | Measurement |
|---|---|---|
| 21 | "Do not assume five targets is free" | Correct not to assume — and having measured, it is **not the constraint**: 32 bytes/sample against the M2's 128 B/px tile budget, 25% of it. *This was also my own hypothesis; the measurement killed it.* Transient attachments remain worth doing for bandwidth. |
| 22 | Depth prepass may not justify itself | Apple confirms HSR makes a *performance-only* prepass redundant. **This one is kept**: it feeds the linear-depth target consumed by AO, water and fog, and costs 0.26 ms. |
| — | Shadow mask as overhead | Removing it makes the scene pass **5.8 ms worse**. It stays. |

## Not scheduled — tracked, not removed

**Nothing is removed from the spec.** These stay on the board with their reason, and any of them can
be scheduled on request. They are real capabilities judged outside what AV Gen needs *today* — a
statement about sequencing and about what this engine is, not a refusal.

| § | Item | Why not scheduled |
|---|---|---|
| 15 | World streaming with residency levels | AV Gen renders *authored cinematic scenes*, not open worlds. No scene exists that does not fit memory. The infrastructure cost is large and the need is hypothetical. **Revisit if a scene ever exceeds memory — or on request.** |
| 16 | Cinematic camera-aware streaming | Rides with streaming — but noted as AV Gen's genuine structural advantage over a game engine, and the first thing to build if §15 is ever justified. The camera path is known in advance, which almost no game engine can say. |
| 45 | Stress scenes: City, Light Hell, Particle Hell | Game-engine stress profiles. AV Gen's content is organic and cinematic. **Building 4 of the 10 first:** Dense Forest (culling, LOD, HLOD, overdraw), Open Vista (distant representation, cascades), Character, and the AV Gen Showcase — the four that exercise what this engine actually renders. The other three remain on the board. |
| 46 | Scaling curves to 64× | Curves are being measured; the multiplier is scoped to what the content plausibly reaches. |

## Governing constraints, carried into every phase

- **§41 audio-reactive behaviour is not negotiable.** Nothing may introduce nondeterminism that breaks
  offline rendering. The determinism contract was just repaired; it stays repaired.
- **§49 no benchmark cheating.** Every quality reduction must be explicit, measurable, exposed through
  a policy and applicable generally. No scene-specific hacks.
- **§56 do not overengineer.** If a sophisticated optimisation saves 0.1 ms and adds major complexity,
  reject it. This is the rule that deferred most of the table above.
- **The measured noise floor.** Within-session GPU spread is 1.0%, wall 3.0%. A difference below 2%
  GPU or 4% wall is not a result. Cross-session comparison is invalid.
