#pragma once

// Mesh optimisation and LOD chain generation (ADR-078), over meshoptimizer.
//
// Pure, deterministic, GPU-free: the same mesh and settings always give the same chain, so a LOD
// can be built on a worker, cached by content hash, or compared against a golden. Nothing here
// knows about the renderer; Phase 3 of the renderer upgrade decides what to do with a chain.
//
// The one thing a caller must not ignore is what a level reports about itself. Asking for 7% of
// the triangles and being handed 91% is not an exotic failure: it is what the Quaternius trees do,
// because a vertex where three surfaces meet cannot move without changing which surfaces meet
// there and the preserving simplifier will not move it. A level that carries its achieved ratio
// and its error is the difference between a LOD system and a LOD system that silently draws the
// source mesh at every distance.

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <cstdint>
#include <vector>

namespace avgen::assets {

// What `optimiseMesh` does, in the order it does it.
struct MeshOptimiseSettings {
    // Merge byte-identical vertices first. glTF exporters split a vertex per material, per UV seam
    // and per hard edge, and some split more than that; without the weld the vertex cache and the
    // simplifier both see a mesh with more corners than it has.
    bool weld = true;
    // Reorder triangles front-to-back within cache clusters. Off by default, but no longer for the
    // reason first written here: that read "the scene pass is vertex- and draw-bound rather than
    // fragment-bound", which was the conclusion §2 of docs/renderer-2-architecture.md drew from a
    // confounded resolution sweep, and it is the opposite of the truth. The pass is fragment-bound.
    // It stays off because the *overdraw* it removes is small -- a software early-Z arm, discarding
    // every fragment behind the prepass depth, took 1.24 ms of a 21.36 ms pass on Glowmere, so the
    // depth prepass is already doing this job -- and the ACMR regression is not. Revisit it on a
    // scene with no prepass. The number is meshoptimizer's threshold: 1.05 permits a 5% ACMR
    // regression.
    float overdrawThreshold = 0.0f; // 0 = skip the overdraw pass
};

// Vertex-cache order, then vertex-fetch locality. Never changes which triangles exist or where
// their corners are -- only the order of the indices and the layout of the vertex buffer -- so it
// is safe to run on anything, including a mesh that is about to be simplified. An invalid mesh is
// returned unchanged rather than half-processed; use `buildLodChain` when you want the diagnosis.
[[nodiscard]] scene::MeshData optimiseMesh(const scene::MeshData& mesh,
                                           const MeshOptimiseSettings& settings = {});

// How much the simplifier's error metric cares about attributes relative to position. Zero for
// both is pure geometric error, which collapses UV seams and hard edges happily and produces a
// shading mess on anything with a texture on it.
struct AttributeWeights {
    float normal = 0.5f;
    float uv = 0.1f;
    [[nodiscard]] bool any() const { return normal > 0.0f || uv > 0.0f; }
};

// Removing whole disconnected pieces instead of collapsing edges (ADR-344).
//
// The simplifier's failure mode has a floor nobody had measured until the Tree of Life: an
// *8-triangle closed shell cannot be simplified at all*. There is no edge to collapse that does
// not change the topology, so a preserving simplifier hands back 100% of the triangles at every
// ratio it is asked for, and the sloppy one -- which quantises onto a grid -- collapses each leaf
// into a speck and throws the canopy away. That is not a tree with a worse silhouette; it is a bare
// branch structure. The Tree of Life's foliage is 1,046,400 triangles over 122,000 such shells
// (8.5 triangles a leaf), and neither simplifier can touch it.
//
// What *can* be removed from geometry like that is whole leaves, which is exactly what §4 of the
// brief asks for: "eliminate tiny leaves, preserve large masses, the canopy silhouette and its
// visually important gaps". So:
//
//   * shells are found by union-find over shared vertex *positions* (not indices -- a hard-shaded
//     leaf is one shell with several vertices per corner, and an index-space union would call it
//     several);
//   * each is scored `hash01(shell) / (area / meanArea)^sizeBias`, and the lowest scores are kept
//     until the triangle budget is met. The hash makes the removal spatially uniform, so the
//     canopy thins evenly rather than losing a region; the size term drops the smallest leaves
//     first, which is the half of §4 a pure hash would miss;
//   * every kept shell is then grown about its own centroid, because thinning alone does not
//     preserve a canopy -- it perforates one. Keeping a fraction r of the leaves and growing each
//     by r^-0.5 keeps the total leaf area constant, which is the quantity the canopy's opacity and
//     its silhouette both depend on.
//
// Two things this is not. It is not a merge: no leaf is welded to another, and a thinned level is a
// strict subset of the source's shells with a scale on each. And it is not for a mesh that is one
// piece -- on a single connected shell it can only return everything or nothing, which is why it is
// armed by a threshold rather than used unconditionally.
struct ShellThinning {
    // A level whose preserving simplification comes back this many times larger than its target is
    // rebuilt by thinning instead. 0 never thins, which is the default: a mesh that decimates
    // properly must keep doing so, and this is a different kind of approximation that a caller
    // should ask for. 1.5 is what `foliageLodSettings` arms it at -- past a 50% overshoot the
    // simplifier is not making progress towards the ratio, it is refusing.
    float fallback = 0.0f;
    // A mesh with fewer shells than this is never thinned however badly it simplified. Removing one
    // of four pieces is a 25% step with a visible object missing at the end of it; removing one of
    // 122,000 is a thinner canopy. The number is the point at which a per-shell decision starts
    // behaving like a statistic instead of like an edit.
    std::uint32_t minShells = 64;
    // ...and a mesh whose *shells are large* is not thinned either, however many of them it has.
    // This is the test that separates "the simplifier cannot work here" from "the simplifier
    // happened to stall", and it is the one that matters, because the sloppy simplifier is a better
    // answer than thinning whenever it is available.
    //
    // A shell has to be small before a preserving simplifier is out of moves on it: an 8-triangle
    // closed shell has no edge whose collapse leaves the topology alone, and nor does a 16-triangle
    // one in practice. Past roughly two dozen triangles a shell has interior structure to give up
    // and the stall has some other cause, which thinning would paper over by deleting objects.
    //
    // Measured on the Tree of Life's five layers: mean triangles per shell is 7.6 on the foliage
    // (122,000 leaves, and the preserving simplifier returns 100% at every rung), 35 on the
    // tracery, 97 on the twigs, 125 on the lumens and 1,005 on the wood. 24 is the only round
    // number that separates the layer that cannot be simplified from the four that can, and the
    // gap either side of it is a factor of three.
    std::uint32_t maxShellTriangles = 24;
    // How much of the area lost to thinning is given back by growing the kept shells. 1 keeps the
    // total shell area exactly constant (scale = r^-0.5); 0 grows nothing and leaves the canopy
    // with holes in it. Fractions interpolate the exponent.
    float areaCompensation = 1.0f;
    // A ceiling on that growth, because the compensation diverges: at 2% of the leaves it asks for
    // 7.1x, which is no longer a leaf. Past the cap the level is honestly sparser than the source
    // and `boundsError` is not the measure that says so -- the bounding box does not move when a
    // canopy thins, which is the one thing `boundsTolerance` cannot catch.
    float maxScale = 3.0f;
    // Larger shells are preferentially kept: the retention score divides by (area/meanArea) raised
    // to this. 0 is a pure spatially-uniform thin; 1 makes a shell twice the mean area twice as
    // likely to survive.
    float sizeBias = 1.0f;
    // Fixed, so a chain is the same chain on every machine and in every process. Changing it
    // reshuffles which leaves survive and nothing else.
    std::uint32_t seed = 0x9e3779b9u;
};

struct LodChainSettings {
    // Fractions of the *source* triangle count, strictly decreasing, each in (0, 1]. Every level
    // is simplified from the source rather than from the level above: errors do not compound, and
    // a chain rebuilt from a different starting ratio still agrees with itself.
    std::vector<float> ratios{1.0f, 0.5f, 0.2f, 0.07f};
    // Error the simplifier may not exceed, as a fraction of the mesh's extent. The default lets
    // the ratio govern and reports whatever error that cost; lower it when a level has a quality
    // budget rather than a triangle budget, and expect levels to stop short of their ratio.
    float maxError = 1.0f;
    AttributeWeights attributes{};
    // Hold vertices on the topological border still. Right for a mesh that is one piece of a
    // larger surface (a terrain chunk), wrong for anything standing on its own, where the border
    // is the silhouette.
    bool lockBorder = false;
    // Let the simplifier delete whole disconnected components as it goes. Measured and left off
    // (ADR-078): on the Quaternius trees it is the difference between a level that reaches its
    // ratio and one that does not, and it reaches it by throwing away branches -- twelve times the
    // geometric error of the sloppy simplifier at the same triangle count, and at 7% of
    // CommonTree_1 it deleted the entire mesh. The `sloppyFallback` below is the answer to the
    // same problem and a much better one.
    bool prune = false;
    // What counts as the preserving simplifier having given up. It stops when the topology runs
    // out -- a vertex where three surfaces meet, or where an attribute seam crosses, is one it
    // will not move, and a Quaternius tree is full of both: CommonTree_1 will not go below 90% of
    // its triangles at any ratio it is asked for. When a level comes back this many times larger than its
    // target, meshoptimizer's sloppy simplifier is tried instead: it ignores topology, reaches the target,
    // and on those same trees has a *tenth* the error of the pruning alternative. 0 never falls back, which
    // is the right setting when stopping short and saying so is preferable to a mesh whose triangles no
    // longer correspond to the source's.
    float sloppyFallback = 0.0f;
    // How many times the sloppy simplifier may be asked again with a larger request when it
    // undershoots. It quantises the mesh onto a grid and the triangle count it lands on is a step
    // function of that grid's size, so a single call overshoots its target badly and without
    // warning: asked for 35% of CommonTree_1 it returned 7.6%, which made LOD0 -> LOD1 a
    // thirteen-fold drop at 28 px of screen radius rather than the threefold one the ladder asks
    // for (ADR-085). Asking for more, repeatedly, climbs the steps. 1 is the single call.
    std::uint32_t sloppyIterations = 6;
    // Run `optimiseMesh` on the source before simplifying and on every level afterwards.
    bool optimise = true;
    // Fill `LodChain::shadowIndices`.
    bool generateShadowIndices = false;
    // A level is never asked for fewer triangles than this. Below about eight triangles a mesh has
    // no silhouette left to preserve and the simplifier is being asked to invent an impostor,
    // which is a different job (procedural.hpp's `makeLodMesh` levels 2 and 3 do that one).
    std::uint32_t minTriangles = 8;
    // How far a level's bounding box may recede from the source's, on any one face, as a fraction
    // of the source's diagonal. A level that has receded further is refused the same way a level
    // larger than its predecessor is: retried from the level above, and failing that, replaced by
    // it.
    //
    // The rule exists because the reported error does not catch this. At 2.9% of its triangles
    // CommonTree_1 comes back with the bottom of its bounding box 31% of its height above the
    // source's -- the trunk gone -- while reporting a relative error 5.3x smaller than the
    // deviation it actually has. A selector choosing by projected error would take that level far
    // too early, and the header's claim that the error "never understates" was false for it.
    //
    // A bounding box only ever shrinks under simplification, so this is one-sided by construction:
    // the level's box is compared against the source's, face by face, and the largest inward move
    // is the number. That distance is also a *lower bound* on the one-sided Hausdorff distance --
    // the source has a vertex out there and the level's surface is inside its own box -- which is
    // why `LodLevel::error` is floored by it below rather than merely warned about.
    //
    // 0 disables the guard, and the error floor with it: the chain a caller gets is then exactly
    // the one this code built before the guard existed, which is what lets the regression test have
    // an arm and a control in the same function.
    //
    // **Where 0.12 comes from.** Measured over every asset the Glowmere scatter layers use, at the
    // ratios `vegetationLodSettings` asks for -- 39 rungs, printed by `[.analysis][lod]` "What each
    // rung of the chain draws, against the source". It is the smallest round value that
    //
    //   * admits every rung in this repository that keeps at least 90% of the source's height
    //     (the largest such recession is Bush_Common lod3 at 0.102), and
    //   * refuses every rung that keeps less than 80% (Grass_Common_Short lod3 at 0.174 and 78%,
    //     Pebble_Round_2 lod3 at 0.187 and 24%, CommonTree_1 lod3 at 0.257 and 59%).
    //
    // The 80..90% band is then decided by the diagonal, which is the right measure for a squat
    // object and the wrong one for a tall thin one -- said here rather than hidden, because it is
    // the part of this number that is a judgement and not a measurement.
    //
    // Tightening it is cheap to try and expensive to ship: at 0.06 it replaces 11 of those 39 rungs
    // instead of 3, and takes rungs 2 and 3 off Bush_Common and Grass_Common_Short entirely --
    // layers whose instances are a few pixels across when they reach those rungs, where a tenth of
    // a diagonal is a fraction of a pixel. A threshold in *projected* units would separate those
    // cases properly, and this function knows nothing about the ladder that would need.
    float boundsTolerance = 0.12f;
    // Removing whole shells when the simplifier will not remove edges. Off by default; see the
    // type's own comment for what it does and when it is the right answer.
    ShellThinning thinning{};

    [[nodiscard]] Result<void> validate() const;
};

struct LodLevel {
    scene::MeshData mesh;
    float targetRatio = 1.0f;   // what was asked for
    float achievedRatio = 1.0f; // triangles(mesh) / triangles(source)
    // Error reported by the simplifier, as a fraction of the mesh's extent. 0 at a level that was
    // not simplified.
    //
    // It is an upper bound on the deviation, not a measured distance, and with non-zero attribute
    // weights it blends positional and attribute error into one number -- on Rock_Medium_1 it read
    // 0.337 where the largest actual vertex-to-surface distance was 0.066 (ADR-078). What it is
    // good for is ordering and thresholding: it rises monotonically with aggressiveness and never
    // understates, so a selector that switches levels when the error projects below a pixel is
    // conservative rather than wrong.
    float relativeError = 0.0f;
    // The same error in the mesh's own units, for a caller choosing a level by screen-space size:
    // a level whose error projects to less than a pixel is a level that costs nothing to use.
    //
    // Floored by `boundsError` below, so the "never understates" claim above is a property of this
    // number rather than a hope about the simplifier's.
    float error = 0.0f;
    // How far this level's bounding box receded from the source's, in the mesh's own units: a
    // measured lower bound on the deviation, owing nothing to the simplifier's own estimate.
    //
    // Reported separately as well as folded into `error` because the two answer different
    // questions. `error` is what a selector should threshold on; `boundsError` is what says whether
    // the shape still has the same silhouette, and a level where it dominates `error` is a level
    // that lost a part of the object rather than smoothed it.
    float boundsError = 0.0f;
    // False when the simplifier stopped short of `targetRatio` -- topology it would not break, or
    // `maxError` reached. The level is still usable; it is simply larger than it was asked to be.
    bool reachedTarget = true;
    // Whether the sloppy simplifier produced this level, either because `sloppyFallback` fired or
    // because the preserving one returned nothing at all.
    bool sloppy = false;
    // Whether shell thinning produced this level: whole disconnected pieces removed, rather than
    // edges collapsed. Separate from `sloppy` because it is a different kind of claim about the
    // result. A sloppy level is the same object with worse corners; a thinned level is a *subset*
    // of the source's pieces, possibly grown, whose triangles are the source's exactly. A
    // diagnostic that reports one as the other sends a reader looking for shading artefacts that
    // are not there, and past the missing leaves that are.
    bool thinned = false;
    // How many disconnected shells this level kept, out of `LodChain::sourceShells`. Zero on a
    // level that was not thinned.
    std::uint32_t shells = 0;
    // The uniform scale each kept shell was grown by about its own centroid; 1.0 is none. See
    // `ShellThinning::areaCompensation`.
    float shellScale = 1.0f;
};

struct LodChain {
    std::vector<LodLevel> levels;
    // A position-only index buffer over `levels[0].mesh.vertices`, for depth-only and shadow
    // passes. It is a re-indexing, not a second mesh: vertices that differ only in normal or UV
    // collapse to one, so the pass transforms fewer of them, and nothing else about the geometry
    // changes. Empty unless `generateShadowIndices` asked for it.
    std::vector<std::uint32_t> shadowIndices;
    std::uint32_t sourceTriangles = 0;
    // Disconnected pieces of the source, by shared vertex position. 0 means nobody counted: the
    // count is only taken when `ShellThinning` is armed, because it is a union-find over every
    // triangle and the answer is 1 for most meshes in this engine.
    std::uint32_t sourceShells = 0;
};

// Fails on a mesh that is not a valid indexed triangle list, on non-finite positions, and on
// settings that do not describe a chain. Never fails because simplification went badly: that is
// reported per level and is the caller's decision to make.
[[nodiscard]] Result<LodChain> buildLodChain(const scene::MeshData& mesh,
                                             const LodChainSettings& settings = {});

// What a mesh's index order costs a GPU, under meshoptimizer's simplified cache models. Not a
// prediction of Metal's behaviour; a comparable number, so "the optimisation pass did something"
// is a measurement rather than a belief.
struct MeshCacheStats {
    float acmr = 0.0f;      // vertex shader invocations per triangle; 0.5 is the floor, 3.0 the worst case
    float atvr = 0.0f;      // invocations per vertex; 1.0 means every vertex was transformed once
    float overfetch = 0.0f; // bytes fetched / bytes of vertex buffer; 1.0 means each cache line read once
};
[[nodiscard]] MeshCacheStats analyzeMesh(const scene::MeshData& mesh);

// LOD0 of an imported mesh source: meshoptimizer's documented order -- index (weld), vertex cache,
// vertex fetch -- and, when `triangleBudget` is positive and the mesh is over it, a simplification
// that actually reaches it.
//
// The budget used to go through scene::decimateMesh, a vertex clustering on a uniform grid. A grid
// cannot reach a triangle count: it snaps vertices into cells and keeps whatever triangles survive.
// The levels below LOD0 stopped using it (ADR-078) and LOD0 never did, so the mesh the near field
// draws was the only rung of the ladder that was neither simplified properly nor ordered for the
// GPU -- and the near field is where a scatter's triangles actually are.
//
// `settings.ratios` is ignored: this builds LOD0, and the ratio it needs comes from the budget.
// Everything else (attribute weights, the sloppy fallback, maxError) is honoured, so a caller
// chooses between a level that stops short and says so and one that reaches its number.
//
// A skinned mesh is returned untouched: vertex-fetch optimisation reorders the vertex buffer and
// MeshData::skin is parallel to it. So is a mesh the chain builder refuses to touch -- a
// non-finite position, an index past the vertex buffer -- because nothing good comes of a grid
// clustering over one of those either.
[[nodiscard]] scene::MeshData sourceLodMesh(const scene::MeshData& mesh, int triangleBudget,
                                            const LodChainSettings& settings);

// The two calibrations ADR-078 measured. A hero is looked at; a fern is one of eighty thousand.
[[nodiscard]] LodChainSettings heroLodSettings();
// LOD0 of an imported mesh source with a triangle budget (ADR-110). See the definition for what
// was measured and why the sloppy fallback is armed here and the overdraw pass is not.
[[nodiscard]] LodChainSettings lod0Settings();
[[nodiscard]] LodChainSettings vegetationLodSettings();
// An imported hero asset that may be partly instanced foliage (ADR-344): the five rungs §3 of the
// asset-LOD brief asks for, and shell thinning armed so the half of the asset that will not
// decimate is reached by the only means that reaches it. The rungs are targets; every level
// carries what it actually achieved.
[[nodiscard]] LodChainSettings foliageLodSettings();

// How many disconnected pieces a mesh has, by shared vertex position. O(indices) union-find. Public
// because it is the measurement that says whether `ShellThinning` is the right tool for an asset,
// and a caller deciding that should not have to build a chain to find out.
[[nodiscard]] std::uint32_t countShells(const scene::MeshData& mesh);

} // namespace avgen::assets
