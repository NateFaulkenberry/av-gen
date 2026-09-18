# ADR-337 — The look was in the .blend file and the GLB came out brown

Status: Accepted
Date: 2026-09-18

## Context

Two externally authored assets arrived and had to get into AV Gen: a Tree of Life hero export
(`~/Desktop/tree_mdl/hero_pass/Tree_of_Life_Hero_static.glb`, 139.6 MB, 3,162,186 triangles across
seven meshes) and a floating island (`~/Desktop/Island/FloatingIsleModel.fbx`, 2.4 MB, 64,171
triangles across nine objects). The brief was a proof of concept, in its own words: *"does a giant
glowing Tree of Life standing on a slowly rotating floating island in cosmic space actually look
fucking cool inside AV Gen?"* — with a repeated instruction not to overbuild it.

Four things stood between the assets and that question, and three of them were not visible until
something had been measured or rendered.

### The island is an FBX, and this engine reads glTF

`src/assets/` has `gltf_loader.{hpp,cpp}` and nothing else. The island is a Kaydara binary FBX
written by 3ds Max 2023, with ten Unity URP `.mat` sidecars and 28 PNGs beside it.

### The island's materials are Unity's, and its mask map is not a colour map

Each material's `FloatingIsle_<name>_MaskMap.png` packs four unrelated scalars in the Unity HDRP
order: **R metallic, G ambient occlusion, B unused detail, A smoothness**. glTF wants **R
occlusion, G roughness, B metallic** — a different order *and* the opposite sense of the last
channel. There is a second trap behind the first: URP's Lit shader still multiplies the
`_Smoothness` scalar into the sampled alpha when a mask map is bound
(`specGloss.a *= _Smoothness`), and all nine of these materials set it to 0.5.

### The island is a cottage

Nine objects: `RockyBase` and `GeoSphere001` are the landmass and its grass cap. The other
seven — `House`, `Roof`, `Chimney`, `Windmill`, `RoundFence`, `Stairs` and a `Decor` mesh — are a
village. The brief's §8 says use the island as supplied; its §12, §33 and §34 say nothing may
compete with the tree. Those cannot both hold, and the arithmetic says which one breaks: at the
island scale this scene needs, the house alone is **57 units tall against the tree's 78**, and it
stands at the trunk.

### The Glowmere look did not survive the tree's export

This is the one that mattered most and was least visible. The hero GLB's own `validation.json`
calls it a *"Mature static snapshot; simple PBR, no shader baking or growth export"*, and it means
every word. There are **no textures and no UVs** — only `POSITION` and `NORMAL`. Six of the seven
materials are one of two flat factors, `[0.12, 0.10, 0.08]` brown bark or `[0.09, 0.23, 0.11]`
forest green leaf, and exactly one carries emission: the recessed tracery at `[0.02, 0.15, 0.10]`.
The cyan-teal canopy, cream-white key highlights and scattered bright specks in
`hero_pass/renders/Glowmere.png` are a Blender shading setup that stayed in the `.blend`.

And the scene cannot put it back. A `"material"` block on a `kind: "gltf"` node is parsed,
validated, and then **dropped with a warning** — a glTF node's surface comes from the asset's own
materials. The two levers that do reach a glTF node, `emissiveBoost` and `roughnessScale`, are
scalar, whole-instance, and multiplicative; boosting emission on leaves that emit nothing
multiplies zero.

## Decision

**One scripted, committed import, and a variant per genuine conflict rather than a choice made
silently.** `tools/import_tree_isle.py` runs under Blender and emits everything;
`assets/treeisle.manifest.json` is the provenance record the repository keeps, following the
convention the city, alien, farm and HDRI packs already set — the 194 MB of GLB is gitignored and
the manifest carries both source paths, the conversion, the counts and the one command.

**The Unity mask map is repacked, not reinterpreted.** Occlusion from its G with
`_OcclusionStrength` folded in by glTF's own `1 + s(ao - 1)`, roughness as `1 - _Smoothness * A`,
metallic from its R, into one ORM texture bound as both `metallicRoughnessTexture` and
`occlusionTexture`. `_Metallic` is correctly ignored: URP does not apply it in this path.

**The island ships twice, from one import.** `floating-isle.glb` is everything the artist built;
`floating-isle-bare.glb` is the landmass alone. A second export with a different selection, not an
edited source: the artist modelled each structure as its own object and no vertex of the landmass
is shared with any of them, so the split is a selection and nothing tears. Nothing on the Desktop
is written to. The owner chose bare.

**The tree also ships twice, and the second one's geometry is the first one's bytes.**
`tools/glowmere_tree_materials.py` rewrites the seven material definitions to a palette read off
`renders/Glowmere.png` and copies the GLB's **binary chunk through untouched**, then re-reads the
output and compares that chunk's SHA-256 against the input's, refusing to write if it moved. This
is §15's "minimum required conversion" in the smallest form available, and it keeps §7's "do not
modify the Tree of Life model" in the strongest sense there is: the variant's vertex data is not
equivalent to the hero's, it is the same 139,617,852 bytes.

**The tree is a child of the island's transform, and nothing animates the tree.** A `group` node
named `floating-island` carries the rotation and the bob; the island mesh and the tree are both
its children. A group rather than parenting the tree under the island mesh directly, because the
island node carries a 56× scale to bring a 1.5-unit 3ds Max export up to a 116-unit tree, and a
tree inheriting that would have to carry 1/56 to undo it.

**Motion is LFO sources routed to that group.** A saw at 1/180 Hz × 360° into its yaw — one
revolution every three minutes, and a saw's wrap from 360° to 0° is the same orientation, so there
is nothing to snap. A sine at 1/40 Hz × 2.4 units into its height, and two much slower sines for a
1° tilt. `LfoSource::update` is `renderTime * rate + phase` with no integration, so a render that
starts at t = 40 s puts the island exactly where one that started at 0 would have put it, and
there is no state that can drift.

## What the renders say

All at 1920×1080, tier offline, 2× supersampled, from the identical camera.

| arm | what it shows |
|---|---|
| `renders/treeisland/hero/` | the deliverable |
| `renders/treeisland/compare-cottage/` | the island as supplied per §8 |
| `renders/treeisland/compare-hero-materials/` | the tree's materials as exported |

The cottage frame settles §8 against §34 without an argument: the house fills the middle of the
canopy, its orange roof and glowing yellow windows take the eye, and **the trunk is not visible at
all**. The as-exported frame settles §15: a dark green tree with no bioluminescence anywhere, mean
frame luminance `(0.009, 0.011, 0.016)` against the Glowmere variant's `(0.025, 0.047, 0.056)`,
and 5.1% of pixels above black against 12.9%.

## Consequences

**A glTF node's surface is the asset's, and an import is where art direction has to happen.** This
is the second time the repository has learned it — ADR-044 wrote down that a mesh source takes its
asset's material unless the scene deliberately overrode it, and this is what that rule costs when
the asset arrives without the look. An externally authored GLB that has to be re-dressed needs a
committed re-dressing *tool*, not scene JSON, and the tool should prove it did not touch the
geometry.

**There is no starfield in this engine, and it did not need one.** The cosmos is
`environment.sky` for a near-black gradient plus one `kind: "procedural"` node: the existing
`point` primitive on a jittered grid, cut to a spherical shell 620–1090 units out by the existing
`filterDistance` and `filterProbability` ops, ~1,700 points, five lines of scene JSON. It was
checked against a no-stars control in case the far plane it forces (`max(radius × 50, 2000)`, so
54,500 rather than 3,750) degraded the shadow cascades. It does not: the plateau differs by
53 against 47 sRGB between the two arms, which is the stars' own emission, and the `shadow` pass
costs 0.26 ms in both.

**The performance baseline is comfortable, and the tree is not what costs.** 1920×1080, tier
realtime, 180 frames with the first 12 dropped as warm-up, five of six runs uncontended, 1–2%
spread between run medians: **GPU frame median 15.96 ms**, of which the scene pass is 13.04 ms and
everything else — depth 1.25, ao 0.39, shadow 0.26, bloom 0.20, tonemap 0.13, composite 0.07,
FXAA 0.07 — is 2.4 ms. Pass accounting 0.97, so the frame really is the passes. 3.16 M triangles
across nine entities is a draw-call-cheap way to spend a triangle budget, and the Glowmere variant
costs nothing measurable against the as-exported one (15.96 vs 15.79 ms, inside the spread).

**The old Tree of Life project is retired and its engine subsystem is not.** `examples/tree/`, its
light rig, the `avgen_make_tree_of_life` emitter and its index entry are gone. `src/scene/tree_*`
stays: four other test files and `src/app/engine.cpp` use it, and §29's "do not carry forward old
architecture" is about this scene's configuration, not about deleting a subsystem with four ADRs
behind it. One of the retired test file's three cases was never about the tree at all — it guards
procedural alpha-cutout parsing — and moves across with its fixture written inline, so that
retiring the *next* showcase does not take the coverage with it.
