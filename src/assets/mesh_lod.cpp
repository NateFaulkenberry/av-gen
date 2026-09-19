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

constexpr std::uint32_t kNoShell = 0xFFFFFFFFu;

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

// ---- shells: the disconnected pieces of a mesh, and removing whole ones -----------------------
//
// See `ShellThinning` in the header for why this exists. Everything here is position-based:
// meshopt_generateShadowIndexBuffer collapses vertices that share a position and differ only in
// normal or UV, so a hard-shaded leaf carrying three vertices per corner is one shell rather than
// the several an index-space union would find.

struct ShellSet {
    std::vector<std::uint32_t> triangleShell; // per triangle: which shell it belongs to
    std::uint32_t count = 0;
};

ShellSet findShells(const scene::MeshData& mesh) {
    ShellSet out;
    const std::size_t triangles = mesh.indices.size() / 3;
    if (triangles == 0 || mesh.vertices.empty()) {
        return out;
    }
    std::vector<std::uint32_t> positionIndices(mesh.indices.size());
    meshopt_generateShadowIndexBuffer(positionIndices.data(), mesh.indices.data(), mesh.indices.size(),
                                      positionData(mesh), mesh.vertices.size(), sizeof(glm::vec3),
                                      sizeof(scene::Vertex));
    std::vector<std::uint32_t> parent(mesh.vertices.size());
    for (std::uint32_t i = 0; i < parent.size(); ++i) {
        parent[i] = i;
    }
    // Path-halving find, union towards the smaller root. The trees stay shallow because every union
    // is between corners of one triangle and the halving flattens what is left.
    const auto find = [&parent](std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    const auto unite = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint32_t ra = find(a);
        const std::uint32_t rb = find(b);
        if (ra != rb) {
            parent[std::max(ra, rb)] = std::min(ra, rb);
        }
    };
    for (std::size_t t = 0; t < triangles; ++t) {
        unite(positionIndices[t * 3], positionIndices[t * 3 + 1]);
        unite(positionIndices[t * 3], positionIndices[t * 3 + 2]);
    }
    // Numbered in first-appearance order, so a shell's id does not depend on how many vertices
    // happen to precede it in the buffer.
    std::vector<std::uint32_t> shellOf(mesh.vertices.size(), kNoShell);
    out.triangleShell.resize(triangles);
    for (std::size_t t = 0; t < triangles; ++t) {
        const std::uint32_t root = find(positionIndices[t * 3]);
        if (shellOf[root] == kNoShell) {
            shellOf[root] = out.count++;
        }
        out.triangleShell[t] = shellOf[root];
    }
    return out;
}

// A value in [0, 1) from a shell's centroid, so which leaves survive depends on where they are and
// not on the order the exporter wrote them in. Two shells at the same place collide and take the
// same decision, which is right: they are the same leaf twice.
float shellHash01(glm::vec3 centroid, std::uint32_t seed) {
    const auto mix = [](std::uint32_t h) {
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    };
    std::uint32_t h = seed;
    for (int axis = 0; axis < 3; ++axis) {
        // Quantised to a thousandth of a unit before hashing: a re-export that moves a leaf by a
        // float's last bit must not reshuffle the whole canopy.
        const auto q = static_cast<std::int32_t>(std::lround(static_cast<double>(centroid[axis]) * 1000.0));
        h = mix(h ^ static_cast<std::uint32_t>(q));
    }
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

struct ThinResult {
    scene::MeshData mesh;
    std::uint32_t keptShells = 0;
    float scale = 1.0f;
};

// Keep whole shells until the triangle budget is met, then grow each kept shell about its own
// centre to give back the area the dropped ones took with them.
ThinResult thinShells(const scene::MeshData& source, const ShellSet& shells,
                      std::uint32_t targetTriangles, const ShellThinning& thinning) {
    ThinResult out;
    const std::size_t triangles = source.indices.size() / 3;
    if (shells.count == 0 || triangles == 0) {
        return out;
    }
    struct Shell {
        std::uint32_t triangles = 0;
        float area = 0.0f;
        glm::vec3 lo{std::numeric_limits<float>::max()};
        glm::vec3 hi{std::numeric_limits<float>::lowest()};
    };
    std::vector<Shell> info(shells.count);
    for (std::size_t t = 0; t < triangles; ++t) {
        Shell& s = info[shells.triangleShell[t]];
        const glm::vec3 a = source.vertices[source.indices[t * 3]].position;
        const glm::vec3 b = source.vertices[source.indices[t * 3 + 1]].position;
        const glm::vec3 c = source.vertices[source.indices[t * 3 + 2]].position;
        s.triangles += 1;
        s.area += 0.5f * glm::length(glm::cross(b - a, c - a));
        s.lo = glm::min(s.lo, glm::min(a, glm::min(b, c)));
        s.hi = glm::max(s.hi, glm::max(a, glm::max(b, c)));
    }
    double totalArea = 0.0;
    for (const Shell& s : info) {
        totalArea += static_cast<double>(s.area);
    }
    const auto meanArea = static_cast<float>(totalArea / static_cast<double>(shells.count));

    std::vector<std::uint32_t> order(shells.count);
    std::vector<float> score(shells.count);
    for (std::uint32_t i = 0; i < shells.count; ++i) {
        order[i] = i;
        const glm::vec3 centroid = 0.5f * (info[i].lo + info[i].hi);
        const float relative = meanArea > 0.0f ? std::max(info[i].area / meanArea, 1e-4f) : 1.0f;
        score[i] = shellHash01(centroid, thinning.seed) /
                   std::pow(relative, std::max(thinning.sizeBias, 0.0f));
    }
    std::ranges::sort(order, [&](std::uint32_t a, std::uint32_t b) {
        // The index breaks ties, so the result does not depend on the sort being stable.
        return score[a] != score[b] ? score[a] < score[b] : a < b;
    });

    std::vector<char> keep(shells.count, 0);
    std::uint32_t kept = 0;
    std::uint32_t keptTriangles = 0;
    for (const std::uint32_t shell : order) {
        if (keptTriangles >= targetTriangles) {
            break;
        }
        keep[shell] = 1;
        ++kept;
        keptTriangles += info[shell].triangles;
    }
    if (kept == 0 || keptTriangles >= triangles) {
        return out; // nothing kept, or nothing removed: the caller keeps what it had
    }
    out.keptShells = kept;

    const float ratio = static_cast<float>(keptTriangles) / static_cast<float>(triangles);
    out.scale = thinning.areaCompensation > 0.0f
                    ? std::clamp(std::pow(ratio, -0.5f * std::clamp(thinning.areaCompensation, 0.0f, 1.0f)),
                                 1.0f, std::max(thinning.maxScale, 1.0f))
                    : 1.0f;

    scene::MeshData built;
    built.name = source.name;
    built.indices.reserve(static_cast<std::size_t>(keptTriangles) * 3);
    std::vector<std::uint32_t> remap(source.vertices.size(), kNoShell);
    std::vector<std::uint32_t> vertexShell;
    for (std::size_t t = 0; t < triangles; ++t) {
        const std::uint32_t shell = shells.triangleShell[t];
        if (keep[shell] == 0) {
            continue;
        }
        for (int corner = 0; corner < 3; ++corner) {
            const std::uint32_t original = source.indices[t * 3 + static_cast<std::size_t>(corner)];
            if (remap[original] == kNoShell) {
                remap[original] = static_cast<std::uint32_t>(built.vertices.size());
                built.vertices.push_back(source.vertices[original]);
                vertexShell.push_back(shell);
            }
            built.indices.push_back(remap[original]);
        }
    }
    if (out.scale > 1.0f) {
        // About each shell's own centre, so the canopy's shape is unchanged and only its leaves are
        // larger. Uniform, so the normals stay correct without renormalising.
        for (std::size_t v = 0; v < built.vertices.size(); ++v) {
            const Shell& s = info[vertexShell[v]];
            const glm::vec3 centroid = 0.5f * (s.lo + s.hi);
            built.vertices[v].position = centroid + (built.vertices[v].position - centroid) * out.scale;
        }
    }
    out.mesh = std::move(built);
    return out;
}

// One level's two attempts over `src`: the preserving simplifier, then meshoptimizer's sloppy one
// when the first stalled far short of the target or returned nothing at all. `indices` is empty
// when neither produced a triangle -- an empty level is not a small level, it is an object that
// vanishes at a distance, and the caller must be able to tell the two apart.
// How near the target a sloppy level has to land before the search stops asking for more. A level
// between here and the target is doing its job; below it, the ladder has a hole in it.
constexpr float kSloppyClose = 0.7f;

struct SimplifyAttempt {
    std::vector<std::uint32_t> indices;
    float relativeError = 0.0f;
    bool sloppy = false;
};

SimplifyAttempt simplifyTo(const scene::MeshData& src, std::size_t targetIndices,
                           const LodChainSettings& settings,
                           const float (&attributeWeights)[kAttributeCount]) {
    SimplifyAttempt result;
    if (src.indices.size() < 3 || src.vertices.empty()) {
        return result;
    }
    std::vector<std::uint32_t> primary(src.indices.size());
    std::size_t produced = 0;
    float relativeError = 0.0f;
    if (settings.attributes.any()) {
        produced = meshopt_simplifyWithAttributes(
            primary.data(), src.indices.data(), src.indices.size(), positionData(src), src.vertices.size(),
            sizeof(scene::Vertex), attributeData(src), sizeof(scene::Vertex), attributeWeights,
            kAttributeCount, nullptr, targetIndices, settings.maxError, simplifyOptions(settings),
            &relativeError);
    } else {
        produced = meshopt_simplify(primary.data(), src.indices.data(), src.indices.size(),
                                    positionData(src), src.vertices.size(), sizeof(scene::Vertex),
                                    targetIndices, settings.maxError, simplifyOptions(settings),
                                    &relativeError);
    }
    bool sloppy = false;
    const bool stalled = produced > targetIndices;
    const bool trySloppy =
        produced < 3 ||
        (settings.sloppyFallback > 0.0f && stalled &&
         static_cast<float>(produced) > static_cast<float>(targetIndices) * settings.sloppyFallback);
    if (trySloppy) {
        // The sloppy simplifier quantises onto a grid, so the triangle count it returns is a step
        // function of the grid size it chose and a single call lands wherever the steps fall:
        // asked for 35% of CommonTree_1 it returns 7.6%. That is not a small level, it is the
        // wrong level -- the ladder asks for a threefold drop and gets a thirteenfold one, at the
        // distance band where a moving camera crosses it. So the request is raised by however far
        // short the last answer fell and asked again, and the candidate nearest the target from
        // below is kept.
        std::vector<std::uint32_t> alternative(src.indices.size());
        std::vector<std::uint32_t> best;
        float bestError = 0.0f;
        std::size_t bestProduced = 0;
        // Under the target is the whole point of a budget; over it is a miss, and counted four
        // times as badly so a candidate is never traded down across the line.
        const auto score = [targetIndices](std::size_t n) {
            return n <= targetIndices ? targetIndices - n : (n - targetIndices) * 4;
        };
        const auto attempt = [&](std::size_t request) {
            float sloppyError = 0.0f;
            const std::size_t got =
                meshopt_simplifySloppy(alternative.data(), src.indices.data(), src.indices.size(),
                                       positionData(src), src.vertices.size(), sizeof(scene::Vertex),
                                       nullptr, request, settings.maxError, &sloppyError);
            if (got >= 3 && (bestProduced == 0 || score(got) < score(bestProduced))) {
                best.assign(alternative.begin(), alternative.begin() + static_cast<std::ptrdiff_t>(got));
                bestProduced = got;
                bestError = sloppyError;
            }
            return got;
        };
        // Bisect the *request*, not the result. The grid the sloppy simplifier picks is a function
        // of what it is asked for, and the triangle count it lands on is a step function of that
        // grid, so the answer is monotonic in the request but nowhere near proportional to it.
        // Asking once, at the target, is what returned 7.6% of CommonTree_1 for a 35% request.
        // `lo` is a request known to come back at or under the target, `hi` one known to come back
        // over it, and each step halves the gap.
        std::size_t lo = targetIndices;
        std::size_t hi = src.indices.size();
        const auto attempts = std::max<std::uint32_t>(settings.sloppyIterations, 1u);
        std::size_t got = attempt(lo);
        for (std::uint32_t i = 1; i < attempts && hi > lo + 2; ++i) {
            // Close enough from below: the level is doing its job and more calls buy nothing.
            if (bestProduced >= 3 && bestProduced <= targetIndices &&
                static_cast<float>(bestProduced) >= static_cast<float>(targetIndices) * kSloppyClose) {
                break;
            }
            const std::size_t mid = lo + (hi - lo) / 2;
            got = attempt(mid);
            if (got >= 3 && got <= targetIndices) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        if (bestProduced >= 3 && (produced < 3 || bestProduced < produced)) {
            primary.swap(best);
            produced = bestProduced;
            relativeError = bestError;
            sloppy = true;
        }
    }
    if (produced < 3) {
        return result;
    }
    result.indices.assign(primary.begin(), primary.begin() + static_cast<std::ptrdiff_t>(produced));
    result.relativeError = relativeError;
    result.sloppy = sloppy;
    return result;
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

    // How far a candidate level's bounding box has receded from the source's, in mesh units: the
    // largest inward move of any of the six faces. Simplification can only ever shrink a box -- a
    // simplified vertex is a weighted position of vertices that were inside it -- so a positive
    // number here is geometry that is gone, and it is a lower bound on the one-sided Hausdorff
    // distance: the source has a vertex on that face and the level's surface lies inside its own
    // box, so nothing on the level is nearer to it than the gap.
    const auto [sourceLo, sourceHi] = source.bounds();
    const float sourceDiagonal = std::max(glm::length(sourceHi - sourceLo), 1e-6f);
    const auto boundsRecession = [&](const scene::MeshData& level) {
        if (level.vertices.empty()) {
            return sourceDiagonal;
        }
        const auto [lo, hi] = level.bounds();
        float worst = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            worst = std::max(worst, lo[axis] - sourceLo[axis]);
            worst = std::max(worst, sourceHi[axis] - hi[axis]);
        }
        return worst;
    };
    // `boundsTolerance` is a fraction of the diagonal, so it means the same thing on a 14 m tree and
    // on a 0.4 m pebble. 0 switches the guard and the error floor off together.
    const float boundsLimit = settings.boundsTolerance > 0.0f
                                  ? settings.boundsTolerance * sourceDiagonal
                                  : std::numeric_limits<float>::infinity();

    LodChain chain;
    chain.sourceTriangles = sourceTriangles;
    chain.levels.reserve(settings.ratios.size());

    // Only when thinning is armed. It is a union-find over every triangle, and the answer is 1 for
    // most meshes in this engine, so it is not a diagnostic worth paying for unconditionally.
    ShellSet shells;
    if (settings.thinning.fallback > 0.0f) {
        shells = findShells(source);
        chain.sourceShells = shells.count;
    }
    // The strategy is chosen once, from the geometry, rather than per level from what the
    // simplifier happened to return -- because the two fallbacks are not interchangeable and the
    // per-level version cannot express that. On the Tree of Life's foliage the *sloppy* simplifier
    // reaches every ratio it is asked for (0.483 / 0.186 / 0.062 / 0.017), so a per-level rule would
    // never see an overshoot and thinning would never fire; what it reaches them by is quantising
    // 8-triangle leaves onto a grid coarse enough to collapse each one to a speck, which is a bare
    // branch structure where a canopy was. So on shell geometry the sloppy simplifier is taken off
    // the table and thinning replaces it, and everywhere else nothing changes at all.
    const bool thinStrategy = settings.thinning.fallback > 0.0f &&
                              shells.count >= settings.thinning.minShells &&
                              sourceTriangles / std::max(shells.count, 1u) <=
                                  settings.thinning.maxShellTriangles;
    LodChainSettings simplifySettings = settings;
    if (thinStrategy) {
        simplifySettings.sloppyFallback = 0.0f;
    }

    const float attributeWeights[kAttributeCount] = {settings.attributes.normal, settings.attributes.normal,
                                                     settings.attributes.normal, settings.attributes.uv,
                                                     settings.attributes.uv};
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
        // Every level is simplified from the source, so errors do not compound down the chain.
        SimplifyAttempt best = simplifyTo(source, targetIndices, simplifySettings, attributeWeights);

        // A chain must *descend*. `validate()` enforces that of the ratios asked for; nothing
        // enforced it of the ratios achieved, and on CommonTree_1 the achieved ones do not descend:
        // the sloppy simplifier reaches 7.6% at the 35% rung and then returns nothing at all at the
        // 12% and 4% rungs, leaving both of those stalled on the 93% the preserving simplifier
        // could not get past. The result was a ladder of 100 / 7.6 / 93 / 93 -- the far levels the
        // expensive ones, which is precisely the arrangement the ratio validator exists to refuse,
        // and a fifteen-triangle pop between adjacent distance bands on top of it.
        //
        // When a level comes back *larger* than the one before it -- or cannot be built at all,
        // which used to fall back to the whole source and is the same inversion in its worst form
        // -- it is tried again from that level rather than from the source. One step of compounding
        // error is the price, and it is paid only by a level that had already failed; the level
        // above has usually had exactly the topology the source stalled on removed from it, so the
        // second attempt tends to succeed where the first could not. If it still cannot, the
        // previous level's mesh is reused: two distance bands drawing the same thing is wasteful,
        // and very much better than the far one drawing more than the near one.
        //
        // Strictly larger, not "no smaller": a chain whose levels come back *equal* is a chain that
        // stalled, and a hero that will not simplify is supposed to say so and be drawn (the
        // calibration note on heroLodSettings). The retry does not reach for the sloppy simplifier
        // either -- it runs whatever `settings` already permits -- so a hero is never quietly
        // swapped for an approximation by this path.
        //
        // The same guard, applied to *bounds*. A level that has dropped below `boundsTolerance` of
        // the source's diagonal on any face has not simplified the object, it has lost a part of
        // it -- and it says nothing about that in the error it reports, so the inversion check
        // above cannot see it. Measured on CommonTree_1 at 2.9% of its triangles: the bottom 31%
        // of the bounding box, which is the trunk, against a reported relative error 5.3x smaller
        // than the deviation. The retry from the level above usually succeeds, because the level
        // above has already had the geometry the source stalled on removed from it.
        //
        // It costs a rung: the far levels of an asset that cannot be simplified without losing its
        // silhouette now repeat the level above rather than drawing a headless approximation of it.
        // That is the same trade the inversion guard already made, for the same reason.
        const scene::MeshData* previous = chain.levels.empty() ? nullptr : &chain.levels.back().mesh;
        const auto trianglesOf = [](const scene::MeshData& m) { return m.indices.size() / 3; };
        // Built once, because both the acceptance test and the error floor need the candidate's
        // actual geometry rather than its index list.
        const auto candidateOf = [&](const scene::MeshData& from, std::vector<std::uint32_t> indices) {
            if (settings.optimise) {
                cacheOptimise(indices, from.vertices.size());
            }
            return compact(from, std::move(indices), levelName(source.name, level));
        };
        scene::MeshData candidate;
        float candidateRecession = 0.0f;
        if (!best.indices.empty()) {
            candidate = candidateOf(source, best.indices);
            candidateRecession = boundsRecession(candidate);
        }
        const bool inverted = previous != nullptr && !best.indices.empty() &&
                              best.indices.size() / 3 > trianglesOf(*previous);
        const bool lostGeometry = !best.indices.empty() && candidateRecession > boundsLimit;

        // ---- the last resort: remove whole shells ---------------------------------------------
        //
        // Armed only by `ShellThinning::fallback`, and tested against what the simplifiers actually
        // produced rather than against a guess about the geometry. It runs *after* them and not
        // instead of them, because a mesh that is both shell-structured and simplifiable -- a
        // hundred rocks in one part -- should be simplified. Thinning such a mesh would delete
        // ninety of the rocks to reach a ratio that collapsing edges reaches with all hundred still
        // there.
        //
        // Three things count as the simplifier having failed, and the third is the one that was
        // missing at first. An **overshoot** past `fallback` times the target is the ordinary case.
        // **Nothing at all** is an overshoot of infinity: neither simplifier produced a triangle,
        // which on shell geometry is the same refusal said differently. And a level that **lost
        // geometry** -- reached the ratio but took the bounding box with it -- is a failure the
        // triangle count cannot see, and it is what a cloud of small shells does under a simplifier
        // that is willing to delete them: it hits 25% exactly, by removing the outermost shells and
        // shrinking the object. Without this third case the chain fell all the way back to the
        // source on that geometry, with thinning armed, having declined to thin a level the
        // simplifier had already been refused.
        if (thinStrategy) {
            const std::size_t got = best.indices.size() / 3;
            const bool overshot = best.indices.empty() || lostGeometry ||
                                  static_cast<float>(got) >
                                      settings.thinning.fallback * static_cast<float>(targetTriangles);
            if (overshot) {
                ThinResult thin = thinShells(source, shells, targetTriangles, settings.thinning);
                const bool better = best.indices.empty() || lostGeometry || thin.mesh.indices.size() / 3 < got;
                if (!thin.mesh.indices.empty() && better) {
                    out.mesh = settings.optimise ? optimiseMesh(thin.mesh) : std::move(thin.mesh);
                    out.mesh.name = levelName(source.name, level);
                    const auto levelTriangles = static_cast<std::uint32_t>(out.mesh.indices.size() / 3);
                    out.achievedRatio =
                        static_cast<float>(levelTriangles) / static_cast<float>(sourceTriangles);
                    out.thinned = true;
                    out.shells = thin.keptShells;
                    out.shellScale = thin.scale;
                    // Thinning's granularity is one shell: it stops at the first shell that meets
                    // the budget, so it can only ever overshoot by that shell's triangles. Judging
                    // it by `<= targetTriangles` reported "did not reach" on a level that landed on
                    // 0.500 of a requested 0.500, which is a false alarm and the kind that trains a
                    // reader to ignore the flag.
                    const auto grain = static_cast<std::uint32_t>(
                        std::max<std::size_t>(1, sourceTriangles / std::max(shells.count, 1u)));
                    out.reachedTarget = levelTriangles <= targetTriangles + grain;
                    // Thinning's deviation is not the simplifier's and must not be reported as it.
                    // What a thinned level is wrong by, at worst, is a whole shell: any point on a
                    // removed leaf can be that leaf's radius away from anything that is still
                    // there, and grown neighbours do not close that gap, they cover it. So the
                    // error is the largest kept shell's radius, floored by the bounding-box
                    // recession the same way a simplified level's is. It is an over-estimate for a
                    // dense canopy and the right order for a sparse one, and it is deliberately
                    // *not* small: a selector must not take a thinned rung while the object is
                    // large on screen.
                    out.boundsError = boundsRecession(out.mesh);
                    const auto [lo, hi] = out.mesh.bounds();
                    float shellRadius = 0.0f;
                    if (thin.keptShells > 0) {
                        // Mean shell extent, from the level's own geometry: total kept area over
                        // kept shells gives a mean area, and a disc of that area has this radius.
                        double area = 0.0;
                        for (std::size_t t = 0; t + 2 < out.mesh.indices.size(); t += 3) {
                            const glm::vec3 a = out.mesh.vertices[out.mesh.indices[t]].position;
                            const glm::vec3 b = out.mesh.vertices[out.mesh.indices[t + 1]].position;
                            const glm::vec3 c = out.mesh.vertices[out.mesh.indices[t + 2]].position;
                            area += 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
                        }
                        shellRadius = static_cast<float>(
                            std::sqrt(area / (3.14159265358979 * static_cast<double>(thin.keptShells))));
                        shellRadius = std::min(shellRadius, glm::length(hi - lo));
                    }
                    out.error = std::max(shellRadius, out.boundsError);
                    out.relativeError = errorScale > 0.0f ? out.error / errorScale : 0.0f;
                    chain.levels.push_back(std::move(out));
                    continue;
                }
            }
        }

        if (previous != nullptr && (best.indices.empty() || inverted || lostGeometry)) {
            SimplifyAttempt again = simplifyTo(*previous, targetIndices, simplifySettings, attributeWeights);
            scene::MeshData retry;
            float retryRecession = 0.0f;
            if (!again.indices.empty()) {
                retry = candidateOf(*previous, again.indices);
                retryRecession = boundsRecession(retry);
            }
            if (!retry.indices.empty() && trianglesOf(retry) < trianglesOf(*previous) &&
                retryRecession <= boundsLimit) {
                out.mesh = std::move(retry);
                const auto levelTriangles = static_cast<std::uint32_t>(trianglesOf(out.mesh));
                out.achievedRatio = static_cast<float>(levelTriangles) / static_cast<float>(sourceTriangles);
                out.boundsError = retryRecession;
                out.relativeError = settings.boundsTolerance > 0.0f
                                        ? std::max(again.relativeError, retryRecession / errorScale)
                                        : again.relativeError;
                out.error = out.relativeError * errorScale;
                out.reachedTarget = levelTriangles <= targetTriangles;
                out.sloppy = again.sloppy;
                chain.levels.push_back(std::move(out));
                continue;
            }
            out.mesh = *previous;
            out.mesh.name = levelName(source.name, level);
            const auto levelTriangles = static_cast<std::uint32_t>(trianglesOf(out.mesh));
            out.achievedRatio = static_cast<float>(levelTriangles) / static_cast<float>(sourceTriangles);
            out.relativeError = chain.levels.back().relativeError;
            out.error = chain.levels.back().error;
            out.boundsError = chain.levels.back().boundsError;
            out.reachedTarget = levelTriangles <= targetTriangles;
            out.sloppy = chain.levels.back().sloppy;
            chain.levels.push_back(std::move(out));
            continue;
        }
        // No level above to fall back to, and the only candidate lost geometry: the source is the
        // honest answer, exactly as it is for a level that could not be built at all.
        if (lostGeometry) {
            out.mesh = source;
            out.mesh.name = levelName(source.name, level);
            out.reachedTarget = false;
            chain.levels.push_back(std::move(out));
            continue;
        }

        // Either simplifier can return nothing at all -- the preserving one when pruning eats the
        // last component, the sloppy one when its grid search cannot land near the target. An empty
        // level is not a small level; it is an object that disappears at a distance, so a level that
        // cannot be built at all falls back to the source with `reachedTarget` false.
        if (best.indices.empty()) {
            out.mesh = source;
            out.mesh.name = levelName(source.name, level);
            out.reachedTarget = false;
            chain.levels.push_back(std::move(out));
            continue;
        }

        out.mesh = std::move(candidate);
        const auto levelTriangles = static_cast<std::uint32_t>(trianglesOf(out.mesh));
        out.achievedRatio = static_cast<float>(levelTriangles) / static_cast<float>(sourceTriangles);
        out.boundsError = candidateRecession;
        // The floor. Whatever the simplifier believes, the level cannot be nearer to the source
        // than the geometry it dropped off the end of the bounding box -- so this is the smaller of
        // the two claims being discarded, not a new estimate being invented.
        out.relativeError = settings.boundsTolerance > 0.0f
                                ? std::max(best.relativeError, candidateRecession / errorScale)
                                : best.relativeError;
        out.error = out.relativeError * errorScale;
        out.reachedTarget = levelTriangles <= targetTriangles;
        out.sloppy = best.sloppy;
        chain.levels.push_back(std::move(out));
    }

    // The header promises that `error` "rises monotonically with aggressiveness", and a selector
    // that picks the coarsest admissible rung depends on it. Nothing enforced it, and shell
    // thinning breaks it: the deviation of a thinned level is the size of a shell, and on a part
    // whose thinning hit the growth cap at one rung and not the next, a coarser level can come back
    // with a *smaller* mean shell than the one above it. Measured on two of the Tree of Life's 22
    // foliage parts: rung 2 reporting 1.59 against rung 1's 1.84.
    //
    // Floored rather than recomputed, because the claim `error` makes is an upper bound and raising
    // an upper bound keeps it true. A level cannot be nearer to the source than a coarser
    // description of the same object built from the same source.
    for (std::size_t level = 1; level < chain.levels.size(); ++level) {
        chain.levels[level].error = std::max(chain.levels[level].error, chain.levels[level - 1].error);
        chain.levels[level].relativeError =
            std::max(chain.levels[level].relativeError, chain.levels[level - 1].relativeError);
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

scene::MeshData sourceLodMesh(const scene::MeshData& mesh, int triangleBudget,
                              const LodChainSettings& settings) {
    if (mesh.skinned() || mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0 ||
        !mesh.valid()) {
        return mesh;
    }
    const auto triangles = static_cast<int>(mesh.indices.size() / 3);
    LodChainSettings lod0 = settings;
    lod0.optimise = true; // the whole point: LOD0 through the documented sequence
    lod0.generateShadowIndices = false;
    lod0.ratios = {1.0f};
    const bool budgeted = triangleBudget > 0 && triangles > triangleBudget;
    if (budgeted) {
        // Below 1 so validate() accepts a strictly descending pair; the clamp only bites on a
        // budget within one triangle of the source, where there is nothing to do anyway.
        lod0.ratios.push_back(std::min(static_cast<float>(triangleBudget) / static_cast<float>(triangles),
                                       0.999f));
    }
    auto chain = buildLodChain(mesh, lod0);
    if (!chain || chain->levels.empty()) {
        return mesh;
    }
    const std::size_t level = budgeted && chain->levels.size() > 1 ? 1 : 0;
    scene::MeshData out = std::move(chain->levels[level].mesh);
    if (out.indices.empty() || !out.valid()) {
        return mesh;
    }
    out.name = mesh.name; // a source mesh keeps its own name; only a LOD rung is renamed
    return out;
}

MeshCacheStats analyzeMesh(const scene::MeshData& mesh) {
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
// LOD0 of an imported mesh source with a triangle budget (ADR-110). Measured on an M2 Max against
// Glowmere at 1280x800, three interleaved pairs per arm, from the frame the old path produced
// (432,271 camera triangles, 18.68 ms GPU, 15.73 ms scene pass):
//
//   this calibration                    264,303 tris   14.48 ms GPU   11.86 ms scene
//   preserving only (sloppyFallback 0)  the trees stall: valley_canopy's leaves come back at
//                                       3,937 triangles against a share of a 900 budget, five
//                                       times what the old grid clustering produced. Refused.
//   weld + vertex-cache order off        14.42-14.81 ms over three pairs, against 14.48-14.68
//                                       with them on: no measurable difference either way.
//   overdraw pass at 1.05                15.01-15.20 ms, every pair slower. Left off.
//
// So: the reduction is the whole of the win, and it comes from the simplifier reaching the budget
// the author wrote, which a grid clustering cannot do. The ordering steps are kept because welding
// is what lets the simplifier work at all on an exporter-split mesh and neither costs anything
// measurable here -- not because either was measured to pay. The overdraw pass is left off with a
// number against it: meshoptimizer's own documentation warns it behaves differently on tiled GPUs,
// and on this one it costs 4%. The depth prepass is already doing that job.
//
// The attribute weights are the hero calibration's, not vegetation's: LOD0 is the mesh the near
// field draws and its shading is looked at. The sloppy fallback is armed, which the hero
// calibration refuses, because a hero has no triangle budget to reach and a scattered asset does:
// a budget that is quietly not met is the defect this replaces, not a quality setting.
LodChainSettings lod0Settings() {
    LodChainSettings s;
    s.attributes = {.normal = 0.5f, .uv = 0.1f};
    s.sloppyFallback = 1.5f;
    return s;
}

LodChainSettings vegetationLodSettings() {
    LodChainSettings s;
    s.ratios = {1.0f, 0.35f, 0.12f, 0.04f};
    s.attributes = {.normal = 0.2f, .uv = 0.05f};
    s.sloppyFallback = 1.5f;
    return s;
}

// An imported hero asset that is partly instanced foliage (ADR-348). Measured on the Tree of Life's
// five layers; see tests/unit/test_asset_lod_analysis.cpp "[.analysis][assetlod]" for the table.
//
// Five rungs, because §3 of the brief asks for five, and 1 / 0.5 / 0.2 / 0.07 / 0.02 rather than
// vegetation's ladder because this is an asset somebody looks at: the near rungs have to be close
// together or the hero shot pops.
//
// The attribute weights are the hero calibration's. This asset has no UVs at all -- the Tree of
// Life's materials are per-bucket constants with no texture behind them (ADR-339) -- so the uv
// weight costs nothing here and is kept for the assets that do.
//
// **Both fallbacks are armed and the geometry decides which one runs** -- `ShellThinning::
// maxShellTriangles` makes that choice, and the Tree of Life needs both answers in one asset. The
// sloppy simplifier is the right one for the twigs and the tracery, which are connected
// non-manifold structures where it ignores topology and reaches the ratio. It is the wrong one for
// 122,000 separate leaves: it reaches the ratio there too -- 0.483 / 0.186 / 0.062 / 0.017, which
// looks like success in a table -- by quantising onto a grid coarse enough to collapse each
// 8-triangle leaf into a speck, leaving a bare branch structure where a canopy was. Thinning
// removes whole leaves and grows the survivors, which keeps the canopy's area, its outline and its
// gaps. Both thresholds are 1.5, deliberately the same number: past a 50% overshoot the simplifier
// is not making slow progress towards the ratio, it is refusing.
LodChainSettings foliageLodSettings() {
    LodChainSettings s;
    s.ratios = {1.0f, 0.5f, 0.2f, 0.07f, 0.02f};
    s.attributes = {.normal = 0.5f, .uv = 0.1f};
    s.sloppyFallback = 1.5f;
    s.thinning.fallback = 1.5f;
    return s;
}

std::uint32_t countShells(const scene::MeshData& mesh) {
    if (!mesh.valid()) {
        return 0;
    }
    return findShells(mesh).count;
}

} // namespace avgen::assets
