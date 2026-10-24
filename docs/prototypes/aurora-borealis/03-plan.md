# Aurora Borealis: the implementation plan (coordinator, 2026-10-24)

Synthesises 01-research-scene.md (Agent A) and 02-research-signals.md (Agent B). Those reports hold the measurements;
this file holds the decisions. Owner may overrule any of them.

## Decisions on the open questions

| Question | Decision | Why |
|---|---|---|
| E1 aurora in the water: E1 vs twins+mirror | **E1** (SSR-miss sky tap from the pre-water copy). Fallback to twins only if E1 fails visually | ~12 WGSL lines, one tap; reflects the real rendered aurora; fallback is ~3 ms unmeasured |
| E2 ribbon-plane ladder lever | **Yes** (`LiveQualityRung::auroraStepScale`) | Medium/Low need fewer planes; without it the ladder can only drop resolution |
| E3 travelling kick pulse through the ribbons | **Yes, first pass, small** | brief: events drive accents; a global flash is the weak version. Keep it one uniform-driven front, no new pass |
| E4 / E5 | E4: never route aurora speeds. E5 only if grain shows in motion at 40-48 planes | A's measurements |
| Live vs stem twin | Rainbow World pattern: one live project with `sonic.live`, stems declared by role (drums/bass/synth) for testing now; live channels later replace the sources, bindings unchanged | B: `role.*` follows a live input first |
| Live-only phase lead | **No** | must stay offline-identical; revisit only if T10 measurement demands it |
| Swells | secondary accent only | B: 9/18 detectable |
| Palette stepping | continuous blend from `arrangement`/`synthBright`; discrete look steps only on `beat.phrasePulse` | B |
| Camera | timeline loop, ~200 s circuit, 12-20 m along the river, pitch up 3-6°, fov 50-55°; not section-aligned (live) | A; live rule: world fills every frame, no vortex |
| Menu | Examples > Live (`"live": true`, `sonic.live`), its own entry, not under Sonic Abstract | it is a production live scene |
| Stars | keep, dim, inverse to synth | A |
| Feeds | `"feeds": {"beat": "role.drums", "hits": "role.drums"}` | B: grid within 24 ms vs 177 ms drift on the mix |
| Aurora mix leak | `audio/sensitivity` 0, `spectrumShape` 0, no "+ Add aurora" routes | B, A |
| Bass | onset-held size (`bassSize` held from the onset frame), 0 ms attack | B: fixes both owner complaints from Rainbow World |

## Architecture (A §4, adopted)
Caldera's pipeline: generated faceted GLB meshes (`tools/sonic_vfx/meshes.py`), unlit painter's programs (≤30 ops, haze
in program), no lights/shadows/fog volume, procedural sky + dim stars, aurora ribbons as hero (40-48 planes Ultra), a
closed-loop S-bend river on a coarse terrain node with cinematic water (SSR 16×1, flow streaks in aurora colours),
faceted banks hiding the terrain ground, 3 outer ranges. One palette state drives aurora colours, the rocks' aurora-lit
facet term, and water tint/streaks; the value driving `fx/aurora/intensity` drives facet glow on the same frame.
`depthPrepass: true` with AO off.

GPU budget ~20 ms at Ultra 1080p (A's table); live acceptance: ≤5 misses/900 frames, p99 ≤40 ms, Medium or better ≥90%;
headless Ultra GPU p50 ≤15 ms is the stretch target that buys sound-to-light latency.

## Bindings (B §3 table, with A's names)
Context: `arrangement` → curtainHeight, intensity floor, horizonGlow, stars (inverse); `synthBright` → palette axis B /
topColor; `synthBed` → ribbonWidth, base reflection; `hatDrive` → filaments, sparkle. Accents: `kickWave` → E3 pulse +
tiny bloom; `snareFold` → edgeBrightness + reflection ripple; `bassHit` (onset-held) → ribbonEdge/fold depth, water
reflection; `bassBody` → ribbonSwirl (slow, not a speed); `swell` → ribbon spread/brighten. Smoothing only in signals;
routes carry amounts. Control channels `drums`/`bass`/`synth` resting at 1 as per-stem depth (ADR-1612).

## Work split (2 agents, owner cap)
- **Engine agent**, branch `eng/aurora-borealis` (worktree ../av-gen-borealis-eng), ADR-1650..1659: E1, E2, E3, each
  with tests (GPU tests in avgen_render_tests), a docs/decisions ADR each, both suites exit 0. Merges into
  scene/aurora-borealis when done, then to main.
- **Scene agent**, branch `scene/aurora-borealis` (this worktree), ADR-1660..1669 if any: Milestones A-E of the brief.
  A static composition, B hero aurora, C river and light integration (picks up E1 when merged), D stem integration, E live
  optimization + measured acceptance (tools/ci/live-perf.sh on the 60 Hz display, AC power) and the final report.
