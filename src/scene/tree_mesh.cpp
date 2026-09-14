#include "scene/tree_mesh.hpp"

#include "core/noise.hpp"
#include "scene/procedural.hpp"
#include "spatial/spline.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avgen::scene {
namespace {

enum Channel : std::uint32_t {
    kCardPos = 400,
    kCardYaw = 401,
    kCardPitch = 402,
    kCardSize = 403,
    kCardRoll = 404,
    kFoliageTint = 405,
};

constexpr float kEpsilon = 1e-6f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEpsilon ? v / len : fallback;
}

int sidesForTier(BranchTier tier, const TreeMeshSettings& s) {
    switch (tier) {
    case BranchTier::Trunk: return s.trunkSides;
    case BranchTier::Primary: return s.primarySides;
    case BranchTier::Secondary: return s.secondarySides;
    case BranchTier::Tertiary: return s.tertiarySides;
    }
    return s.tertiarySides;
}

MeshData* meshForTier(TreeMeshes& out, BranchTier tier) {
    switch (tier) {
    case BranchTier::Trunk: return &out.trunk;
    case BranchTier::Primary: return &out.primary;
    case BranchTier::Secondary: return &out.secondary;
    case BranchTier::Tertiary: return &out.tertiary;
    }
    return &out.tertiary;
}

// One axis becomes one spline. The control points are the axis's nodes, plus a socket point pushed
// back inside the parent tube so the join is covered.
spatial::Spline axisCurve(const TreeGraph& graph, const TreeAxis& axis, const TreeMeshSettings& settings) {
    spatial::Spline curve;
    curve.kind = spatial::SplineKind::CatmullRom;
    curve.generator = spatial::SplineGenerator::Points;
    curve.points.reserve(axis.nodes.size() + 1);

    const TreeNode& first = graph.nodes[axis.firstNode];
    if (first.parent != kNoNode) {
        const TreeNode& parent = graph.nodes[first.parent];
        spatial::SplinePoint socket;
        // Back along the PARENT's direction, not the child's: the point of the socket is to begin
        // inside the parent's solid, and the parent's axis is where that solid is.
        socket.position = parent.position - parent.direction * (parent.radius * settings.socketDepth);
        socket.scale = first.radius * settings.socketFlare;
        curve.points.push_back(socket);
    }
    for (std::uint32_t id : axis.nodes) {
        spatial::SplinePoint p;
        p.position = graph.nodes[id].position;
        p.scale = graph.nodes[id].radius;
        curve.points.push_back(p);
    }
    // A one-node axis has no length to sweep along; duplicate the tip a hair further on so the
    // Catmull-Rom has two distinct points and makeTube does not return an empty mesh.
    if (curve.points.size() < 2) {
        spatial::SplinePoint tip = curve.points.back();
        tip.position += graph.nodes[axis.firstNode].direction * 0.02f;
        tip.scale *= 0.6f;
        curve.points.push_back(tip);
    }
    return curve;
}

// Analytic bark. The sweep is already built, so this displaces the finished ring vertices radially
// by a low-frequency field of position. Doing it after the sweep rather than by perturbing the
// spline's scale is what keeps it *around* the trunk as well as along it -- a scale perturbation can
// only make the whole ring fatter, which reads as a bulge, not as bark.
void applyBark(MeshData& mesh, std::size_t firstVertex, const std::vector<glm::vec3>& axisPoints, float amount,
               float scale, float swell, float swellScale, std::uint32_t seed) {
    if ((amount <= 0.0f && swell <= 0.0f) || axisPoints.empty()) {
        return;
    }
    for (std::size_t i = firstVertex; i < mesh.vertices.size(); ++i) {
        Vertex& v = mesh.vertices[i];
        // The ring centre is the nearest point on the axis polyline. Cheap and good enough: rings
        // are dense compared with the axis's curvature.
        glm::vec3 centre = axisPoints.front();
        float best = std::numeric_limits<float>::max();
        for (const glm::vec3& p : axisPoints) {
            const float d = glm::dot(p - v.position, p - v.position);
            if (d < best) {
                best = d;
                centre = p;
            }
        }
        const glm::vec3 radial = v.position - centre;
        const float radius = glm::length(radial);
        if (radius < kEpsilon) {
            continue;
        }
        // Two octaves with very different wavelengths, and they do different jobs: the short one is
        // grain, the long one is the swelling and irregular taper that reads as age.
        const float grain = noise::fbm3(v.position * scale, seed) * 2.0f - 1.0f;
        const float shape = noise::fbm3(v.position * swellScale, seed ^ 0x51u) * 2.0f - 1.0f;
        v.position += (radial / radius) * (grain * amount + shape * swell) * radius;
    }
}

void buildCluster(MeshData& mesh, const FoliageSite& site, const TreeMeshSettings& settings,
                  const glm::vec3& crownCentre, std::uint32_t seed) {
    const float r = site.radius * settings.clusterScale;
    if (r <= kEpsilon || settings.cardsPerCluster < 1) {
        return;
    }
    glm::vec3 u;
    glm::vec3 w;
    const glm::vec3 axis = safeNormalize(site.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 reference = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = safeNormalize(glm::cross(axis, reference), glm::vec3(1.0f, 0.0f, 0.0f));
    w = glm::cross(axis, u);

    for (int c = 0; c < settings.cardsPerCluster; ++c) {
        const auto idx = site.node * 31u + static_cast<std::uint32_t>(c);
        // A point in the cluster ellipsoid. The cube root keeps the cards from bunching at the
        // centre, which is what a uniform radial draw does and what makes a cluster read as a blob
        // with a hot core instead of a volume.
        const float rad = r * std::cbrt(noise::hashIndex(seed, idx, kCardPos));
        const float theta = noise::hashIndex(seed, idx, kCardYaw) * glm::two_pi<float>();
        const float cosPhi = noise::hashIndex(seed, idx, kCardPitch) * 2.0f - 1.0f;
        const float sinPhi = std::sqrt(std::max(0.0f, 1.0f - cosPhi * cosPhi));
        const glm::vec3 offset =
            (u * std::cos(theta) * sinPhi + w * std::sin(theta) * sinPhi) * rad +
            axis * (cosPhi * rad * settings.clusterElongation);
        const glm::vec3 centre = site.position + offset;

        const float size = settings.leafSize * (0.65f + 0.7f * noise::hashIndex(seed, idx, kCardSize));
        const float roll = noise::hashIndex(seed, idx, kCardRoll) * glm::two_pi<float>();
        // Orient the card across the outward direction, so cards near the cluster's surface face
        // out and cards in its middle are seen edge-on less often.
        const glm::vec3 outward = safeNormalize(offset, axis);
        glm::vec3 cardU = safeNormalize(glm::cross(outward, axis + glm::vec3(0.13f, 0.0f, 0.07f)), u);
        glm::vec3 cardV = glm::cross(outward, cardU);
        const glm::vec3 ru = cardU * std::cos(roll) + cardV * std::sin(roll);
        const glm::vec3 rv = cardV * std::cos(roll) - cardU * std::sin(roll);

        // Normals point out of the cluster centre, not off the card's own plane. A flat card lit by
        // its own normal reads as a flat card; a cluster of cards lit by the volume's normal reads
        // as one soft mass.
        const glm::vec3 flat = safeNormalize(glm::cross(ru, rv), outward);
        const glm::vec3 local = safeNormalize(glm::mix(flat, outward, settings.cardNormalBlend), outward);
        // Away from the crown's centre, not the cluster's: this is the term that makes the canopy
        // shade as one volume instead of as a heap of separately-lit balls.
        const glm::vec3 crownOut = safeNormalize(centre - crownCentre, local);
        const glm::vec3 normal = safeNormalize(glm::mix(local, crownOut, settings.crownNormalBlend), local);

        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 corners[4] = {centre - ru * size - rv * size, centre + ru * size - rv * size,
                                      centre + ru * size + rv * size, centre - ru * size + rv * size};
        const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        for (int k = 0; k < 4; ++k) {
            mesh.vertices.push_back(Vertex{corners[k], normal, uvs[k]});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

} // namespace

namespace {
void recordAxis(std::vector<std::uint32_t>& into, std::size_t count, std::uint32_t axis) {
    into.insert(into.end(), count, axis);
}
} // namespace

void appendMesh(MeshData& into, const MeshData& src) {
    if (src.vertices.empty()) {
        return;
    }
    const auto offset = static_cast<std::uint32_t>(into.vertices.size());
    into.vertices.insert(into.vertices.end(), src.vertices.begin(), src.vertices.end());
    into.indices.reserve(into.indices.size() + src.indices.size());
    for (std::uint32_t index : src.indices) {
        into.indices.push_back(index + offset);
    }
}

MeshData* TreeMeshes::meshFor(const std::string& role) {
    if (role == "roots") return &roots;
    if (role == "trunk") return &trunk;
    if (role == "primary") return &primary;
    if (role == "secondary") return &secondary;
    if (role == "tertiary") return &tertiary;
    if (role == "foliage0") return &foliage[0];
    if (role == "foliage1") return &foliage[1];
    if (role == "foliage2") return &foliage[2];
    return nullptr;
}

std::vector<std::pair<std::string, const MeshData*>> TreeMeshes::parts() const {
    // Order is the order the nodes appear in the outliner, so it runs base to tip.
    return {{"roots", &roots},           {"trunk", &trunk},         {"primary", &primary},
            {"secondary", &secondary},   {"tertiary", &tertiary},   {"foliage0", &foliage[0]},
            {"foliage1", &foliage[1]},   {"foliage2", &foliage[2]}};
}

Result<TreeMeshes> buildTreeMeshes(const TreeGraph& graph, const TreeMeshSettings& settings) {
    if (graph.nodes.empty()) {
        return fail("tree mesh: the graph has no nodes");
    }
    const auto started = std::chrono::steady_clock::now();
    TreeMeshes out;
    out.trunk.name = "tree.trunk";
    out.primary.name = "tree.primary";
    out.secondary.name = "tree.secondary";
    out.tertiary.name = "tree.tertiary";
    for (int i = 0; i < kFoliageTints; ++i) {
        out.foliage[static_cast<std::size_t>(i)].name = "tree.foliage" + std::to_string(i);
    }
    out.roots.name = "tree.roots";

    const glm::vec3 crownCentre(0.0f, graph.params.crown.centreHeight, 0.0f);
    std::vector<glm::vec3> axisPoints;
    for (const TreeAxis& axis : graph.axes) {
        if (axis.nodes.empty()) {
            continue;
        }
        const int sides = std::clamp(sidesForTier(axis.tier, settings), 3, 64);
        const spatial::Spline curve = axisCurve(graph, axis, settings);
        const float length = std::max(axis.length, 0.02f);
        const int rows =
            std::clamp(static_cast<int>(length * settings.rowsPerUnit) + 2, 2, settings.maxRowsPerAxis);

        // radius 1 and taper 1 so the profile is exactly the interpolated per-point `scale`, which
        // is where the pipe model's radius lives. makeTube's own `taper` is linear and would
        // overwrite it.
        MeshData tube = makeTube(curve, 1.0f, 1.0f, sides, rows, 0.0f, axis.tier == BranchTier::Trunk);
        if (tube.vertices.empty()) {
            continue;
        }
        if (settings.barkAmount > 0.0f && sides >= settings.barkMinSides) {
            axisPoints.clear();
            for (const spatial::SplinePoint& p : curve.points) {
                axisPoints.push_back(p.position);
            }
            applyBark(tube, 0, axisPoints, settings.barkAmount, settings.barkScale, settings.swellAmount,
                      settings.swellScale, graph.params.seed ^ 0xBA2Cu);
        }
        MeshData* target = meshForTier(out, axis.tier);
        const std::size_t slot = static_cast<std::size_t>(axis.tier) + 1;
        recordAxis(out.vertexAxis[slot], tube.vertices.size(), axis.id);
        for (const Vertex& v : tube.vertices) {
            out.vertexBind[slot].push_back(v.position);
        }
        appendMesh(*target, tube);
    }

    for (const RootStrand& root : graph.roots) {
        if (root.points.size() < 2) {
            continue;
        }
        spatial::Spline curve;
        curve.kind = spatial::SplineKind::CatmullRom;
        curve.generator = spatial::SplineGenerator::Points;
        for (std::size_t i = 0; i < root.points.size(); ++i) {
            spatial::SplinePoint p;
            p.position = root.points[i];
            p.scale = root.radii[i];
            curve.points.push_back(p);
        }
        const int rows = std::clamp(static_cast<int>(root.length * settings.rowsPerUnit) + 2, 2, 96);
        MeshData tube = makeTube(curve, 1.0f, 1.0f, std::clamp(settings.rootSides, 3, 64), rows, 0.0f, false);
        // Roots bind to the trunk's base joint: they do not move, and saying so explicitly is
        // cheaper than a special case in the animator.
        recordAxis(out.vertexAxis[0], tube.vertices.size(), 0u);
        out.vertexBind[0].insert(out.vertexBind[0].end(), tube.vertices.size(), glm::vec3(0.0f));
        appendMesh(out.roots, tube);
    }

    for (const FoliageSite& site : graph.foliage) {
        // The tint is decided by the generator, from a spatial field, so an accent has a location.
        const auto tint = std::min<std::size_t>(site.tint, kFoliageTints - 1);
        const std::size_t before = out.foliage[tint].vertices.size();
        buildCluster(out.foliage[tint], site, settings, crownCentre, graph.params.seed ^ 0xF01Au);
        const std::size_t added = out.foliage[tint].vertices.size() - before;
        recordAxis(out.vertexAxis[5 + tint], added, site.axis);
        out.vertexBind[5 + tint].insert(out.vertexBind[5 + tint].end(), added, site.position);
    }

    for (const auto& [name, mesh] : out.parts()) {
        out.triangles += static_cast<std::uint32_t>(mesh->indices.size() / 3);
    }
    out.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return out;
}

} // namespace avgen::scene
