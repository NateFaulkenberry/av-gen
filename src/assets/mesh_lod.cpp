#include "assets/mesh_lod.hpp"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace avgen::assets {
namespace {

// meshoptimizer reads positions as "float3 in the first 12 bytes of each vertex" and weld/shadow
// index generation compares raw bytes, padding included. Both are true of this layout and neither
// is true of a layout with a gap in it, so the assumption is pinned here rather than discovered as
// a mesh that welds nothing on a compiler that pads differently.
static_assert(sizeof(scene::Vertex) == 8 * sizeof(float));
static_assert(offsetof(scene::Vertex, position) == 0);
static_assert(offsetof(scene::Vertex, normal) == 3 * sizeof(float));
static_assert(offsetof(scene::Vertex, uv) == 6 * sizeof(float));

const float* positionData(const scene::MeshData& mesh) {
    return &mesh.vertices.front().position.x;
}

// Normal and UV are five contiguous floats, so the simplifier's attribute stream is the vertex
// buffer itself offset by twelve bytes -- no repacking, no second allocation per level.
const float* attributeData(const scene::MeshData& mesh) {
    return &mesh.vertices.front().normal.x;
}
constexpr std::size_t kAttributeCount = 5;

unsigned int simplifyOptions(const LodChainSettings& s) {
    unsigned int options = 0;
    if (s.lockBorder) {
        options |= meshopt_SimplifyLockBorder;
    }
    if (s.prune) {
        options |= meshopt_SimplifyPrune;
    }
    return options;
}

// Vertex-cache order over the given index buffer, in place. Reorders indices only.
void cacheOptimise(std::vector<std::uint32_t>& indices, std::size_t vertexCount) {
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertexCount);
}

// Turns an index buffer that still refers to `source`'s vertices into a standalone mesh holding
// only the vertices it uses. This runs whether or not the caller asked for optimisation: a level
// that kept the source's whole vertex buffer would be a triangle reduction with no memory
// reduction, which is not a LOD.
scene::MeshData compact(const scene::MeshData& source, std::vector<std::uint32_t> indices, std::string name) {
    scene::MeshData out;
    out.name = std::move(name);
    out.indices = std::move(indices);
    out.vertices.resize(source.vertices.size());
    const std::size_t unique =
        meshopt_optimizeVertexFetch(out.vertices.data(), out.indices.data(), out.indices.size(),
                                    source.vertices.data(), source.vertices.size(), sizeof(scene::Vertex));
    out.vertices.resize(unique);
    out.vertices.shrink_to_fit();
    return out;
}

std::string levelName(const std::string& base, std::size_t level) {
    return level == 0 ? base : base + "#lod" + std::to_string(level);
}

} // namespace

scene::MeshData optimiseMesh(const scene::MeshData& mesh, const MeshOptimiseSettings& settings) {
    if (!mesh.valid()) {
        return mesh;
    }
    scene::MeshData welded;
    const scene::MeshData* input = &mesh;
    if (settings.weld) {
        std::vector<std::uint32_t> remap(mesh.vertices.size());
        const std::size_t unique =
            meshopt_generateVertexRemap(remap.data(), mesh.indices.data(), mesh.indices.size(),
                                        mesh.vertices.data(), mesh.vertices.size(), sizeof(scene::Vertex));
        welded.name = mesh.name;
        welded.indices.resize(mesh.indices.size());
        welded.vertices.resize(unique);
        meshopt_remapIndexBuffer(welded.indices.data(), mesh.indices.data(), mesh.indices.size(),
                                 remap.data());
        meshopt_remapVertexBuffer(welded.vertices.data(), mesh.vertices.data(), mesh.vertices.size(),
                                  sizeof(scene::Vertex), remap.data());
        input = &welded;
    }

    scene::MeshData out;
    out.name = input->name;
    out.indices = input->indices;
    cacheOptimise(out.indices, input->vertices.size());
    if (settings.overdrawThreshold > 0.0f) {
        // Documented as requiring cache-optimised input, which is why it sits between the two.
        meshopt_optimizeOverdraw(out.indices.data(), out.indices.data(), out.indices.size(),
                                 positionData(*input), input->vertices.size(), sizeof(scene::Vertex),
                                 settings.overdrawThreshold);
    }
    return compact(*input, std::move(out.indices), out.name);
}

Result<void> LodChainSettings::validate() const {
    if (ratios.empty()) {
        return fail("a LOD chain needs at least one ratio");
    }
    float previous = std::numeric_limits<float>::infinity();
    for (const float r : ratios) {
        if (!std::isfinite(r) || r <= 0.0f || r > 1.0f) {
            return fail("LOD ratio {} is not a fraction of the source in (0, 1]", r);
        }
        if (r >= previous) {
            // Not pedantry: levels are indexed by distance, and a chain whose levels do not
            // descend is one where the far level is the expensive one.
            return fail("LOD ratios must strictly decrease; {} follows {}", r, previous);
        }
        previous = r;
    }
    if (!std::isfinite(maxError) || maxError <= 0.0f) {
        return fail("LOD maxError must be a positive fraction of the mesh extent, not {}", maxError);
    }
    if (!std::isfinite(attributes.normal) || attributes.normal < 0.0f || !std::isfinite(attributes.uv) ||
        attributes.uv < 0.0f) {
        return fail("LOD attribute weights must be finite and non-negative");
    }
    if (!std::isfinite(sloppyFallback) || sloppyFallback < 0.0f) {
        return fail("LOD sloppyFallback must be a non-negative multiple of the target, not {}",
                    sloppyFallback);
    }
    if (sloppyFallback > 0.0f && sloppyFallback < 1.0f) {
        // Below 1 it would fire on a level that *reached* its target, which is not a fallback.
        return fail("LOD sloppyFallback of {} would fire on a level that already hit its target",
                    sloppyFallback);
    }
    if (minTriangles < 1) {
        return fail("LOD minTriangles must be at least 1");
    }
    return {};
}

Result<LodChain> buildLodChain(const scene::MeshData& mesh, const LodChainSettings& settings) {
    if (auto ok = settings.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    // `valid()` covers the three ways an index buffer lies about itself; say which one, because
    // "mesh is invalid" sends the reader back to a loader they have no reason to suspect.
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return fail("mesh '{}' has no geometry to simplify ({} vertices, {} indices)", mesh.name,
                    mesh.vertices.size(), mesh.indices.size());
    }
    if (mesh.indices.size() % 3 != 0) {
        return fail("mesh '{}' is not a triangle list: {} indices", mesh.name, mesh.indices.size());
    }
    if (!mesh.valid()) {
        return fail("mesh '{}' has an index past its {} vertices", mesh.name, mesh.vertices.size());
    }
    for (const scene::Vertex& v : mesh.vertices) {
        // The simplifier computes quadrics over these; one NaN spreads to every error it is
        // compared against, and the result is a mesh that simplified to nothing for no visible
        // reason. Cheaper to refuse it here than to debug it as a missing object in a frame.
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z)) {
            return fail("mesh '{}' has a non-finite vertex position", mesh.name);
        }
    }

    const MeshOptimiseSettings optimiseSettings{.weld = settings.optimise, .overdrawThreshold = 0.0f};
    const scene::MeshData source = settings.optimise ? optimiseMesh(mesh, optimiseSettings) : mesh;
    const auto sourceTriangles = static_cast<std::uint32_t>(source.indices.size() / 3);

    // Absolute error needs the scale the simplifier measured its own extents with, not the
    // bounding box: they differ, and a caller comparing `error` against a screen-space threshold
    // needs the simplifier's own units or the comparison is off by whatever the difference is.
    const float errorScale =
        meshopt_simplifyScale(positionData(source), source.vertices.size(), sizeof(scene::Vertex));

    LodChain chain;
    chain.sourceTriangles = sourceTriangles;
    chain.levels.reserve(settings.ratios.size());

    const float attributeWeights[kAttributeCount] = {settings.attributes.normal, settings.attributes.normal,
                                                     settings.attributes.normal, settings.attributes.uv,
                                                     settings.attributes.uv};
    // One attempt's output, and a second buffer for the fallback so the first is not lost when the
    // fallback turns out to be the worse of the two.
    std::vector<std::uint32_t> primary(source.indices.size());
    std::vector<std::uint32_t> alternative(source.indices.size());

    struct Attempt {
        std::size_t produced = 0;
        float relativeError = 0.0f;
        bool sloppy = false;
    };

    for (std::size_t level = 0; level < settings.ratios.size(); ++level) {
        const float ratio = settings.ratios[level];
        const auto wanted = static_cast<std::uint32_t>(
            std::max<float>(std::round(static_cast<float>(sourceTriangles) * ratio),
                            static_cast<float>(settings.minTriangles)));
        const std::uint32_t targetTriangles = std::min(wanted, sourceTriangles);

        LodLevel out;
        out.targetRatio = ratio;
        if (targetTriangles >= sourceTriangles) {
            // Nothing to remove. Reported honestly rather than as a 100%-accurate simplification.
            out.mesh = source;
            out.mesh.name = levelName(source.name, level);
            chain.levels.push_back(std::move(out));
            continue;
        }

        const std::size_t targetIndices = static_cast<std::size_t>(targetTriangles) * 3;
        Attempt best;
        if (settings.attributes.any()) {
            best.produced = meshopt_simplifyWithAttributes(
                primary.data(), source.indices.data(), source.indices.size(), positionData(source),
                source.vertices.size(), sizeof(scene::Vertex), attributeData(source), sizeof(scene::Vertex),
                attributeWeights, kAttributeCount, nullptr, targetIndices, settings.maxError,
                simplifyOptions(settings), &best.relativeError);
        } else {
            best.produced = meshopt_simplify(primary.data(), source.indices.data(), source.indices.size(),
                                             positionData(source), source.vertices.size(),
                                             sizeof(scene::Vertex), targetIndices, settings.maxError,
                                             simplifyOptions(settings), &best.relativeError);
        }

        // Either simplifier can return nothing at all -- the preserving one when pruning eats the
        // last component, the sloppy one when its grid search cannot land near the target. An empty
        // level is not a small level; it is an object that disappears at a distance. Both results
        // are checked for it, and a level that cannot be built falls back to the source with
        // `reachedTarget` false rather than to a mesh with no triangles in it.
        const bool stalled = best.produced > targetIndices;
        const bool trySloppy =
            best.produced < 3 ||
            (settings.sloppyFallback > 0.0f && stalled &&
             static_cast<float>(best.produced) > static_cast<float>(targetIndices) * settings.sloppyFallback);
        if (trySloppy) {
            Attempt sloppy;
            sloppy.sloppy = true;
            sloppy.produced =
                meshopt_simplifySloppy(alternative.data(), source.indices.data(), source.indices.size(),
                                       positionData(source), source.vertices.size(), sizeof(scene::Vertex),
                                       nullptr, targetIndices, settings.maxError, &sloppy.relativeError);
            if (sloppy.produced >= 3 && (best.produced < 3 || sloppy.produced < best.produced)) {
                best = sloppy;
                primary.swap(alternative);
            }
        }

        if (best.produced < 3) {
            out.mesh = source;
            out.mesh.name = levelName(source.name, level);
            out.reachedTarget = false;
            chain.levels.push_back(std::move(out));
            continue;
        }

        std::vector<std::uint32_t> indices(primary.begin(),
                                           primary.begin() + static_cast<std::ptrdiff_t>(best.produced));
        if (settings.optimise) {
            cacheOptimise(indices, source.vertices.size());
        }
        out.mesh = compact(source, std::move(indices), levelName(source.name, level));
        const auto levelTriangles = static_cast<std::uint32_t>(out.mesh.indices.size() / 3);
        out.achievedRatio = static_cast<float>(levelTriangles) / static_cast<float>(sourceTriangles);
        out.relativeError = best.relativeError;
        out.error = best.relativeError * errorScale;
        out.reachedTarget = levelTriangles <= targetTriangles;
        out.sloppy = best.sloppy;
        chain.levels.push_back(std::move(out));
    }

    if (settings.generateShadowIndices && !chain.levels.empty()) {
        const scene::MeshData& lod0 = chain.levels.front().mesh;
        chain.shadowIndices.resize(lod0.indices.size());
        meshopt_generateShadowIndexBuffer(chain.shadowIndices.data(), lod0.indices.data(),
                                          lod0.indices.size(), positionData(lod0), lod0.vertices.size(),
                                          sizeof(glm::vec3), sizeof(scene::Vertex));
        cacheOptimise(chain.shadowIndices, lod0.vertices.size());
    }
    return chain;
}

MeshCacheStats analyseMesh(const scene::MeshData& mesh) {
    if (!mesh.valid()) {
        return {};
    }
    // A 16-entry FIFO with 32-wide warps: meshoptimizer's own default for modern hardware, and the
    // model `optimizeVertexCache` targets, so the two agree about what they are measuring.
    const meshopt_VertexCacheStatistics cache = meshopt_analyzeVertexCache(
        mesh.indices.data(), mesh.indices.size(), mesh.vertices.size(), 16, 32, 32);
    const meshopt_VertexFetchStatistics fetch = meshopt_analyzeVertexFetch(
        mesh.indices.data(), mesh.indices.size(), mesh.vertices.size(), sizeof(scene::Vertex));
    return {.acmr = cache.acmr, .atvr = cache.atvr, .overfetch = fetch.overfetch};
}

// Heroes: the ratios the brief proposed, measured on the Quaternius rocks, fungi and ferns and
// kept. A hero is approached, so the preserving simplifier's refusal to break topology is the
// point rather than an obstacle, its attribute metric keeps the UV seams a textured object is read
// through, and it is never dropped for the sloppy one to reach a ratio -- a hero that will not
// simplify should say so and be drawn, not be replaced by a shape that merely occupies the same
// volume. (The sloppy rescue for a level that came back empty still applies; that is not a quality
// trade, it is the alternative to an object that vanishes.)
LodChainSettings heroLodSettings() {
    LodChainSettings s;
    s.ratios = {1.0f, 0.5f, 0.2f, 0.07f};
    s.attributes = {.normal = 0.5f, .uv = 0.1f};
    s.sloppyFallback = 0.0f;
    return s;
}

// Vegetation: measured on the Quaternius trees, bushes and grasses (ADR-078). Three things differ
// from a hero. The chain goes further, because a fern is one of tens of thousands and is never the
// thing being looked at. The attribute weights are lower, because the shading of a leaf at fifty
// metres is not what sells it and the seams cost reduction. And the sloppy fallback is armed,
// because a Quaternius tree's branches are full of non-manifold junctions and attribute seams, and
// the preserving simplifier will not move a vertex where either sits: on CommonTree_1 it returns
// 90% of the source at every ratio from 50% down to 7% -- correctly, and uselessly.
LodChainSettings vegetationLodSettings() {
    LodChainSettings s;
    s.ratios = {1.0f, 0.35f, 0.12f, 0.04f};
    s.attributes = {.normal = 0.2f, .uv = 0.05f};
    s.sloppyFallback = 1.5f;
    return s;
}

} // namespace avgen::assets
