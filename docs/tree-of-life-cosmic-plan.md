# Tree of Life COSMIC — environment & VFX overhaul: research findings and plan

Scene: `examples/treeisland/tree-of-life-floating-island.{json,scene.json}`.
Companion decision record: `docs/decisions/ADR-359-the-tree-ignores-the-wind-twice-over.md`.

## Phase 0 — research, and what it found

### External references: mostly unreachable, and it did not matter

- `https://webgpu.github.io/webgpu-samples/samples/particles/` — the page is a client-rendered SPA
  and fetches as empty. The **source** is reachable
  (`raw.githubusercontent.com/webgpu/webgpu-samples/main/sample/particles/particle.wgsl`) and was
  read: one storage buffer of particles, an LCG-style `init_rand(invocation_id, seed)` / `rand()`,
  a `simulate` kernel that ages and respawns from a probability texture, and camera-facing quads
  built from `right`/`up` in the render params.
- Shadertoy (`WfGSRD`, `XtBXDy`, `McKXWW`, `tfBGD3`, `ldSGzR`) returns **HTTP 403** to this
  environment. No citation is invented for them; the vortex design below is written from the
  technique the spec itself describes (polar warp + domain-warped FBM + layered temporal rates),
  which is standard and does not need a specific shader to justify.
- The Unreal forum threads were not fetched. The concept taken from them — a single global wind
  vector propagated to both foliage and the falling-leaf system rather than each owning its own —
  is exactly what ADR-055's field already is, so nothing was needed from them either.

### Internal: AV Gen already has most of the requested infrastructure

| The spec asks for | AV Gen already has | Gap |
|---|---|---|
| A. GPU compute particles, deterministic randomisation, instanced HDR rendering | **ADR-015/040**, `shaders/particles.wgsl` + `src/rendering/particle_renderer.cpp`. Fixed pool, dead/alive lists, **deterministic prefix-sum compaction with no atomics**, indirect draw, keyframed size/colour/opacity curves, velocity stretch, trail ribbons, fog coupling, glow injection into the volume march. Strictly more advanced than the WebGPU sample. | Billboards and ribbons only — **no mesh/card instancing**, no per-particle orientation, no texture. `softness` is authored, serialised, uploaded and never read. No mesh emitter. |
| B. Curl noise / flow fields | `turbCurl` in `cs_simulate` (Bridson-style divergence-free curl of three hash potentials), plus `attractorStrength` (radial) and `orbit` (tangential/vortex) around a point. | Not wired to the wind field at all. |
| C/D. Ray-marched volumetrics | **ADR-032/139/140**, `shaders/volume.wgsl`: half-res march (`volumeResolutionScale`, 1.0 offline), Henyey-Greenstein phase, clustered local lights, arbitrary `FieldKind` density **and** colour fields, depth-aware bilinear upsample and composite. | Gated entirely on `Environment::volumeDensity > 0`, which this project sets to 0. |
| E. Wind-driven foliage | **ADR-055** `src/core/wind.hpp` + `shaders/wind.wgsl`: travelling gust fronts, regional variation, direction-turning turbulence, per-species damped-oscillator response. | Consumed **only** by `procedural.wgsl`. Two of ~17 fields are parameters. `enabled` is unreachable. See ADR-359. |
| Comet | **ADR-230** `AtmosphereKind::Comet`, great-circle arc, core/halo/tail/sparkle/rainbow + ground illumination. Already authored here as `atmos/Bioluminescent Comet/*`. | Keep. It is good and the spec says so. |
| Parameter/modulation/UI | **ADR-011**: registering a parameter makes it a Parameters-panel row, a modulation target, a timeline key target, a preset member and a save/load entry — all automatic. | Bespoke panel rows are hand-written. `ui_logic.hpp`'s authoring-layer prefix lists decide whether a prefix is visible below Advanced. |

Correction to the brief: **ADR-225 is about `AutoDirectorSettings`**, not the parameter registry. Its
one-liner ("a setting the application does not keep is not a setting") is the rule, but the sweep
that enforces it across subsystems is **ADR-350**, which also prescribes the two tests to write:
(a) assert every path is registered, *with a negative control that an unregistered path is not
found*; (b) save -> reload -> save and assert a non-default value survives both round trips.
There is no `docs/adr` directory; ADRs live in `docs/decisions/`.

## Phase 1 — the audit, answered

Both halves are measured, not argued. See ADR-359 for the four-arm render table. Short form:

1. `wind.enabled` is a gate with no parameter, read from a **top-level** scene key `"wind"` (not
   `environment.wind`), and written back only when it is already true. The application cannot turn
   the wind on. The owner's `scene/windSpeed = 1.319` has never done anything.
2. Even with the field forced on in the scene file, the frame is **byte-identical**: the shared
   mesh vertex stage `vs_main` in `shaders/common.wgsl` is `object.model * position` and nothing
   else. There is no deformation path for imported meshes, and the Tree of Life is five imported
   GLBs.
3. Particles do not read the wind field either.

## The plan

Stages are ordered so each one is separately verifiable and separately revertible.

**S1 — wind reach.** Register the remaining `WindParams` fields as `scene/wind/*` (enabled, speed,
direction, strength, gust amount/scale/speed/sharpness, turbulence/scale/speed, region
scale/amount/drift, flutter scale). Make the scene writer emit the block whenever any field is
non-default, not only when enabled. Add the ADR-350 pair of tests. Default `enabled = false` so no
existing scene moves. *Verify:* a render with `scene/wind/enabled` on differs from one with it off —
which it will not yet, because S2 has not landed; that is the correct intermediate state and the
test asserts reach, not pixels.

**S2 — mesh wind deformation.** A `windMesh` term in `vs_main`, gated by an object flag that is 0
for every existing draw, so untouched scenes stay byte-identical. Displacement is a continuous
function of **world position** (height above the node group's base, radial distance from its axis),
never of mesh identity — six meshes that touch must not separate, and `tree_rig.hpp` already
documents why per-mesh bases cannot work. Hierarchy comes from the profiles: low frequency and small
amplitude near the axis and low down, higher frequency and larger amplitude out at the tips, plus a
flutter term that only bites where the radial weight is high. Pure function of time (ADR-091).
*Verify:* the four-arm render again; `wind0` vs `wind4` must now differ, with `ctl-emis` still the
control.

**S3 — leaf cards.** A second particle render pipeline: instanced quads with a per-particle
orientation basis and a procedural leaf silhouette in the fragment shader (no texture asset), driven
by the existing pool. Emitter: a new canopy shape derived from the foliage node's bounds — using
`Scene::meshBounds()`, never `MeshData::bounds()` (ADR-355). Wind coupling: bind the frame uniform
to the particle pipelines and sample `windSampleAt` in `cs_simulate`.

**S4 — the vortex.** A world-space term in `volume.wgsl`'s `volumeDensityAt` / `inScatterAt`:
polar warp about a centre anchored to the island node, domain-warped FBM at three temporal rates,
dark inner void, luminous filaments. This is the right home because it already gives world space,
perspective, depth occlusion by the island, parallax, and a resolution scale for the quality ladder.
It needs its own enable so the pass can run with `volumeDensity` still 0.

**S5 — tree energy, canopy shimmer, tree particles, island underside, comet coupling.** All pure
functions of time except the particles.

**S6 — UI and the ADR-350 tests for every family added.** Bespoke panel sections; prefix entries in
`ui_logic.hpp` so the controls are visible below the Advanced layer.

## The `emissiveBoost` decision the owner left open

**Recommendation: 0.5. Superseded my own first answer of 2.2 within the same session** — see the
addendum to ADR-359 for the numbers and for why the first probe was the wrong one. Short form:
`tools/light_probe.py` partitions subject pixels by the sign of n.L against the key out of the
normal AOV, and it reports key-to-shadow **5.537 at 2.2 against 8.368 at 0.5**, because the
emission lifts the shadow side by 60% and the key side by 6%. `shadow_floor` stays at 0.0001 either
way, so 0.5 does not crush the shadow side to the "black silhouette tree" failure, and
`shadow_detail` is slightly better at 0.5. It wins on every axis the key-light brief's section 17
names. The file is not changed: 2.2 is the owner's live value, and this is the number and the
criterion for them to apply.

## Status

Landed: the wind reach (S1), the mesh deformation (S2), the key-light brief's section 18 gaps that
were not a deliberate refusal. Still to come: leaf cards (S3), the cosmic vortex (S4), tree energy /
shimmer / tree particles / island underside / comet coupling (S5), and the bespoke panel sections
(S6) — every parameter added so far is a row in the generic Parameters panel, grouped by its prefix
and visible at the default authoring layer, which satisfies reachability but is not yet the
organised panel the brief's section 16 asks for.
