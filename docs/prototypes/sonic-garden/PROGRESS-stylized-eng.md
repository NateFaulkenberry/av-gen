# Sonic Garden: stylized rendering engineering (progress)

The engineering half of the abstract direction (`04-brief-abstract-direction.md`): engine features the eight abstract
prototypes need that did not exist. Owner of this file: the stylized-engineering `sonic-engineer` agent. The art
agent (`sonic-art`) builds the prototypes on the same branch and reads the "Ready for the art agent" sections.

- **Worktree:** `/Users/natefaulkenberry/Documents/GitHub/av-gen-sonic`, branch `proto/sonic-garden` (shared with the
  art agent: commit only `src/`, `shaders/`, `tests/`, `tools/sonic_live_probe.cpp` and these docs, with
  `git commit -- <paths>`).
- **ADRs:** 1071-1075 are this stream's.
- **GPU:** every GPU job through `tools/gpu-lock.sh`, one per hold. Two test binaries (`avgen_tests`,
  `avgen_render_tests`).

## Status

| # | feature | ADR | state |
|---|---|---|---|
| 1 | per-material cel lighting | 1071 | **landed** (procedural nodes, SDF objects, mesh entities via `Material::toon`) |
| 2 | screen-space outline `post/outline/*` | 1072 | **landed** |
| 3 | wire lines (edges as lines) on procedural nodes | 1073 | **landed** (procedural nodes incl. instancing, deformers, culling; not glTF entities yet) |
| 4 | live probe recording limit | - | **landed**: `--record-seconds`, default covers the tour |
| 5 | live switcher steps through the open project's set (coordinator's request) | 1074 | **landed** |
| 6 | art agent's engine needs 1, 2, 5: letterbox, camera roll, tiny-scale transform bug | 1075 | **landed** |

## Resume here

1. Possible next steps: wire lines and toon for glTF mesh entities (needs a uniform of their own: ObjectUniforms is
   full); outline cost (0.72 ms) could drop by reading the linear-depth target.
2. Final hand-back: both FULL suites, under the lock, one after the other; report exit codes.

## Ready for the art agent: cel lighting (ADR-1071)

**Pin:** `eac7cb96` (toon + outline), or any later commit of this file.

**Uniform block change, loudly:** `ObjectUniforms` grew 464 -> 512 bytes (`toon0..2`). A build and the shaders of the
SAME commit are consistent; an older binary with these shaders (or this binary with older shaders) is not. Re-pin both
together.

JSON: a `toon` object inside any procedural node's `material`, or an SDF object's `material`:

```json
"material": { "baseColor": [0.9, 0.4, 0.3], "roughness": 0.5,
  "toon": { "bands": 3, "softness": 0.02, "terminator": 0.0,
            "shadowColor": [0.45, 0.4, 0.7], "ambient": 0.3,
            "rimWidth": 0.2, "rimColor": [0.6, 0.8, 1.0], "rimIntensity": 1.0,
            "specular": 1.0, "specularSize": 0.1 } }
```

- `bands`: lit tones above the shadow tone (0 = off, the default; 1 = classic two-tone). Rounded.
- `softness`: width of every band edge in N.L (0.001 hard; 0.3 nearly smooth).
- `terminator`: the N.L where the shadow side begins (-1..0.99). Raise it to shrink the lit side.
- `shadowColor` x `ambient`: the shadow tone is albedo x shadowColor x ambient (ambient = the floor's brightness).
- `rimWidth` (share of a round object's radius, 0 off), `rimColor`, `rimIntensity`: emitted, so it blooms.
- `specular` (0 off), `specularSize`: a hard highlight disc.

Parameters (all modulatable, routes and timeline): `procedural/<node>/toon/<key>` and `sdf/<name>/toon/<key>`, the
keys above. Registered for every procedural and SDF whether or not the file has a block. UI: World panel, select the
object, Inspector section **toon**; or the Parameters panel, the object's group, section `toon`.

Notes: cast shadows land as the shadow tone (hard edge). An SDF's `look` occlusion multiplies after shading: set
`look.aoStrength` 0 for flat SDF tones. Toon wins over `environment.stylized`.

GPU cost (M2 Max, 1920x1080, `stylized-eng/bench-*.scene.json`): scene pass 9.57 ms PBR -> 7.93 ms all-toon. Toon is
cheaper than PBR.

## Ready for the art agent: the outline (ADR-1072)

JSON: keys in the scene's `post` block:

```json
"post": { "outlineAmount": 1.0, "outlineColor": [0, 0, 0], "outlineIntensity": 1.0, "outlineWidth": 2.0,
          "outlineDepthThreshold": 0.08, "outlineNormalThreshold": 0.35, "outlineSilhouette": 0,
          "outlineObjectEdges": 1, "outlineFadeStart": 0, "outlineFadeEnd": 0 }
```

Parameters: `post/outline/{amount, color, intensity, width, depthThreshold, normalThreshold, silhouette, objectEdges,
fadeStart, fadeEnd}`. `amount` 0 = off (pass not encoded). Width in pixels at 1080 lines. `intensity` > 1 glows
(neon ink: colour cyan, intensity 4). `silhouette` 1 = outer outline only, no creases. Fade between `fadeStart` and
`fadeEnd` metres (fadeEnd <= fadeStart: no fade). On the beat: route `audio.onsetLow` (or a response envelope) to
`post/outline/width` or `post/outline/intensity`. UI: Parameters panel, `post` group, **outline** section.

GPU cost: `post/outline` 0.72 ms at 1920x1080, width 2.

## Live probe (feature 4)

`tools/sonic_live_probe.cpp`: `--record-seconds <s>` sets the WAV recording buffer. Default: 180 s, or for `tour`
`lead-in + 1 + 20 x scenes + 30` (371 s for 17 scenes), whichever is longer.

## Ready for the art agent: wire lines (ADR-1073)

**Pin:** the commit that adds this section (`git log -1 -- docs/decisions/ADR-1073-wire-lines.md`). `ProceduralUniforms`
grew 752 -> 768 bytes (no stride change), and `shaders/wire.wgsl` is new: re-pin binary and shaders together.

JSON, inside a procedural node's `material`:

```json
"material": { "baseColor": [0.02, 0.02, 0.03],
  "wire": { "mode": "feature", "crease": 30, "color": [0.2, 1.0, 0.9], "intensity": 3.0,
            "opacity": 1.0, "width": 2.0, "fill": 1, "occlude": 1 } }
```

- `mode`: 0/"off", 1/"feature" (boundaries + edges sharper than `crease` degrees), 2/"all" (every triangle edge).
- `color` x `intensity`: scene-linear, written to the emission target too, so > 1 blooms (neon).
- `width`: pixels at 1080 lines. `opacity` 0..1.
- `fill` 0: the surface is not drawn at all (no depth, no shadow): lines alone. `occlude` 0: lines show through
  everything (x-ray); 1: hidden lines are hidden.
- Lines follow every deformer, the instancing, wind and effect displacement. Culled (`lod.cull`) objects draw lines
  on their LOD 0 instances. Point sources draw none.

Parameters: `procedural/<node>/wire/{mode, crease, color, intensity, opacity, width, fill, occlude}`, registered for
every procedural. On the beat: route an onset to `wire/intensity` or `wire/width`. UI: World panel, select the object,
Inspector section **wire**.

GPU cost (1920x1080, `stylized-eng/bench-wire*.scene.json`, 400 spheres + 24 towers + floor): scene pass 9.70 ms
without, 10.09 ms with feature edges (+0.4 ms), 10.68 ms with every triangle edge (about 600k edges, +1.0 ms).

## Ready for the art agent: letterbox, camera roll, tiny scales (ADR-1075)

- `post` keys `displayLetterbox` (aspect, e.g. 2.39; 0 off) and `displayLetterboxAmount` (0..1 slide-in); params
  `post/display/letterbox`, `post/display/letterboxAmount`. Replaces the black-geometry bars.
- `camera/roll` (degrees) turns the camera, not the world: lights stay fixed.
- Scale 0.001 now really is tiny (the `Transform::fromMatrix` fallback); the 0.01 parking workaround is no longer needed.
- No uniform block changed size.

## Live switcher sets (ADR-1074)

PageUp/PageDown, the Live panel's Scenes row and MIDI program change step through the set the open project belongs
to: "Sonic VFX" (Sonic Live first, then the VFX examples: unchanged) or "Sonic Abstract" (its examples in index
order). Any other project: the Sonic VFX list. A set is a category named in `kLiveSceneSets` (`src/app/live_scenes.hpp`).

## Design notes: wire lines (ADR-1073)

Edge extraction on the CPU, drawn as screen-space quads by a vertex entry in the SAME shader module as the surface
(`vs_proc_wire` in procedural.wgsl, `vs_entity_wire` in pbr.wgsl), so the line runs the identical deformer chain,
instancing, wind and FXL displacement as the surface it sits on. Barycentric-in-fragment was rejected: WGSL has no
barycentric builtin, and faking it needs a de-indexed copy of every mesh and a second attribute in every surface
pipeline (depth, shadow, lit), where the edge list costs nothing to any draw that does not ask for lines.

## Log

- 2026-10-02 hand-back: FULL suites at 779c31a5, under the lock, one after the other: `avgen_tests` exit 0
  (4025 cases: 4005 passed, 19 skipped, 1 failed as expected); `avgen_render_tests` exit 0 (575 cases: 574 passed,
  1 skipped). An earlier run caught ADRs 1071-1075 missing from `docs/decisions/README.md` (fixed in 779c31a5).

- 2026-10-02 (resumed): ADR-1075 landed; `[adr1075]`, `[tinyscale]`, `[camera]`, `[adr1065]` pass. Not done: engine
  needs 3 (translucent procedurals), 4 (per-instance deformer phase), 6 (audit for live projects), wire/toon on glTF
  entities (ObjectUniforms is full; needs its own uniform).

- 2026-10-02: feature 3 (wire lines) and the switcher sets landed; `[adr1073]` GPU (1 case) and CPU (3 cases) pass,
  `[live]` CPU passes.
- 2026-10-02: features 1, 2, 4 landed; GPU tests `[adr1071]`, `[adr1072]` pass (3 cases, 66 assertions); CPU
  `[adr1071],[adr1072],[layout]` pass.
