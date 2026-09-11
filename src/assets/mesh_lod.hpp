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
    float error = 0.0f;
    // False when the simplifier stopped short of `targetRatio` -- topology it would not break, or
    // `maxError` reached. The level is still usable; it is simply larger than it was asked to be.
    bool reachedTarget = true;
    // Whether the sloppy simplifier produced this level, either because `sloppyFallback` fired or
    // because the preserving one returned nothing at all.
    bool sloppy = false;
};

struct LodChain {
    std::vector<LodLevel> levels;
    // A position-only index buffer over `levels[0].mesh.vertices`, for depth-only and shadow
    // passes. It is a re-indexing, not a second mesh: vertices that differ only in normal or UV
    // collapse to one, so the pass transforms fewer of them, and nothing else about the geometry
    // changes. Empty unless `generateShadowIndices` asked for it.
    std::vector<std::uint32_t> shadowIndices;
    std::uint32_t sourceTriangles = 0;
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
[[nodiscard]] MeshCacheStats analyseMesh(const scene::MeshData& mesh);

// The two calibrations ADR-078 measured. A hero is looked at; a fern is one of eighty thousand.
[[nodiscard]] LodChainSettings heroLodSettings();
[[nodiscard]] LodChainSettings vegetationLodSettings();

} // namespace avgen::assets
