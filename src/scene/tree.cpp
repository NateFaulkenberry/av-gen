#include "scene/tree.hpp"

#include "core/noise.hpp"
#include "spatial/point_grid.hpp"

#include <glm/gtc/constants.hpp>

#include <array>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace avgen::scene {
namespace {

// Hash channels. Every draw in this file names its channel here rather than passing a literal, so
// that two unrelated draws cannot silently share a stream and correlate.
enum Channel : std::uint32_t {
    kMarkerX = 300,
    kMarkerY = 301,
    kMarkerZ = 302,
    kMarkerAccept = 303,
    kNodePhase = 310,
    kFoliageRadius = 311,
    kFoliagePhase = 312,
    kRootAngle = 320,
    kRootLength = 321,
    kRootWander = 322,
    kRootSplit = 323,
};

constexpr float kEpsilon = 1e-6f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > kEpsilon ? v / len : fallback;
}

// A stable orthonormal basis around `d`. "Stable" here means a pure function of `d` with no
// hysteresis: two calls with the same direction give the same basis, which is what keeps
// phyllotaxis reproducible.
void frameAround(const glm::vec3& d, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 reference = std::abs(d.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = safeNormalize(glm::cross(d, reference), glm::vec3(1.0f, 0.0f, 0.0f));
    v = glm::cross(d, u);
}

BranchTier tierOf(std::uint8_t order) {
    switch (order) {
    case 0: return BranchTier::Trunk;
    case 1: return BranchTier::Primary;
    case 2: return BranchTier::Secondary;
    default: return BranchTier::Tertiary;
    }
}

// A bud is where growth can happen. It is not a node: it sits on one, and moves to the tip of the
// shoot it produces. Terminal buds continue their axis; lateral buds start a new one of the next
// order the first time they grow.
struct Bud {
    std::uint32_t node = kNoNode;
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    std::uint32_t axis = 0;
    std::uint8_t order = 0;
    bool terminal = true;
    bool active = true;
    bool started = false; // a lateral bud has not yet opened its own axis
    std::uint16_t birth = 0;
    std::uint16_t starved = 0; // consecutive iterations receiving less than one metamer's worth
    bool everFed = false;      // has this bud ever found real space? see the quiescent floor
    std::uint32_t axisIndex = 0; // position along its axis, for phyllotaxis
    float q = 0.0f;
    float vigor = 0.0f;
    glm::vec3 optimal{0.0f};
};

} // namespace

const char* branchTierName(BranchTier tier) {
    switch (tier) {
    case BranchTier::Trunk: return "trunk";
    case BranchTier::Primary: return "primary";
    case BranchTier::Secondary: return "secondary";
    case BranchTier::Tertiary: return "tertiary";
    }
    return "trunk";
}

Result<void> TreeParams::validate() const {
    if (markerCount < 64 || markerCount > 400000) {
        return fail("tree: markerCount {} out of range (64..400000)", markerCount);
    }
    if (iterations < 1 || iterations > 200) {
        return fail("tree: iterations {} out of range (1..200)", iterations);
    }
    if (internodeLength <= 0.0f) {
        return fail("tree: internodeLength must be positive");
    }
    if (crown.radius <= 0.0f || crown.halfHeight <= 0.0f) {
        return fail("tree: crown radius and halfHeight must be positive");
    }
    if (crown.baseHeight >= crown.centreHeight + crown.halfHeight) {
        return fail("tree: crown baseHeight {} leaves no volume below the top of the envelope {}",
                    crown.baseHeight, crown.centreHeight + crown.halfHeight);
    }
    if (perceptionDistance <= occupancyRadius) {
        // A perception cone no deeper than the zone it clears sees nothing but ground it has
        // already colonised, so every bud reports Q = 0 and only the quiescent trickle grows. The
        // result is a bare stick, which looks like a bug in the envelope rather than in the ratio.
        return fail("tree: perceptionDistance {} must exceed occupancyRadius {}", perceptionDistance,
                    occupancyRadius);
    }
    if (pipeExponent < 1.2f || pipeExponent > 4.0f) {
        return fail("tree: pipeExponent {} out of range (1.2..4.0)", pipeExponent);
    }
    if (tipRadius <= 0.0f) {
        return fail("tree: tipRadius must be positive");
    }
    if (maxNodes < 16) {
        return fail("tree: maxNodes {} too small", maxNodes);
    }
    return {};
}

std::vector<glm::vec3> generateMarkers(const TreeParams& params) {
    const CrownEnvelope& crown = params.crown;
    std::vector<glm::vec3> markers;
    markers.reserve(static_cast<std::size_t>(params.markerCount));

    // Rejection sampling inside the bounding box of the envelope. The attempt index is the hash
    // index, so the accepted set is a pure function of the seed and the shape -- not of how many
    // attempts happened to be rejected before it.
    const int maxAttempts = params.markerCount * 24;
    const float top = crown.centreHeight + crown.halfHeight;
    const float crownBottom = std::max(crown.baseHeight, crown.centreHeight - crown.halfHeight);
    // Sampling spans the corridor as well as the crown, in one box, so the two regions share the
    // marker budget in proportion to their volumes rather than by an authored split. The corridor
    // is a thin sliver of the box and ends up with about one marker in seventy, which is the right
    // answer: it should be just dense enough to lead the terminal bud upward.
    const float bottom = std::min(crownBottom, crown.trunkCorridorBottom);
    if (top <= bottom) {
        return markers;
    }
    const float corridor = std::max(crown.trunkCorridorRadius, 0.0f);
    for (int attempt = 0; attempt < maxAttempts && static_cast<int>(markers.size()) < params.markerCount;
         ++attempt) {
        const auto index = static_cast<std::uint32_t>(attempt);
        const float rx = noise::hashIndex(params.seed, index, kMarkerX) * 2.0f - 1.0f;
        const float ry = noise::hashIndex(params.seed, index, kMarkerY);
        const float rz = noise::hashIndex(params.seed, index, kMarkerZ) * 2.0f - 1.0f;

        const float y = bottom + ry * (top - bottom);
        const glm::vec2 disc(rx * crown.radius, rz * crown.radius);
        const float dist = glm::length(disc);

        // Below the crown, the only colonisable space is the trunk corridor.
        if (y < crownBottom) {
            if (corridor <= 0.0f || dist > corridor || y < crown.trunkCorridorBottom) {
                continue;
            }
            markers.emplace_back(disc.x, y, disc.y);
            continue;
        }

        // Normalised height within the ellipsoid, -1 at the bottom, +1 at the top.
        const float h = (y - crown.centreHeight) / crown.halfHeight;
        if (h < -1.0f || h > 1.0f) {
            continue;
        }
        // Ellipsoid profile, then the shoulder term: it lifts the widest point above the middle and
        // narrows the underside, which is the difference between a ball and a canopy.
        float profile = std::sqrt(std::max(0.0f, 1.0f - h * h));
        profile *= 1.0f + crown.shoulder * (0.5f - 0.5f * h * h * h - 0.25f * h);
        const float maxR = crown.radius * std::max(profile, 0.0f);
        if (maxR <= 0.0f) {
            continue;
        }
        if (dist > maxR) {
            continue;
        }
        if (crown.coreHollow > 0.0f && dist < maxR * crown.coreHollow) {
            continue;
        }

        const glm::vec3 p(disc.x, y, disc.y);
        if (crown.lumpiness > 0.0f) {
            // Low-frequency acceptance: whole regions of the envelope are denser than others, so
            // the crown grows lopsided in a way that is coherent at limb scale rather than noisy at
            // twig scale. This is the asymmetry knob.
            const float field = noise::fbm3(p * crown.lumpScale, params.seed ^ 0x5BD1u);
            const float keep = 1.0f - crown.lumpiness + crown.lumpiness * field * 2.0f;
            if (noise::hashIndex(params.seed, index, kMarkerAccept) > std::clamp(keep, 0.0f, 1.0f)) {
                continue;
            }
        }
        markers.push_back(p);
    }
    return markers;
}

namespace {

float apicalControl(const TreeParams& params, int iteration) {
    const float t = params.iterations > 1
                        ? static_cast<float>(iteration) / static_cast<float>(params.iterations - 1)
                        : 1.0f;
    const float a = std::clamp(params.apicalReleaseStart, 0.0f, 1.0f);
    const float b = std::clamp(params.apicalReleaseEnd, a + 1e-3f, 1.0f);
    const float u = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
    const float smooth = u * u * (3.0f - 2.0f * u);
    return params.lambdaYoung + (params.lambdaOld - params.lambdaYoung) * smooth;
}

// The direction a new metamer leaves in: the paper's weighted sum of the default orientation, the
// optimal growth direction V, and a tropism, plus a wander term that is ours rather than theirs.
glm::vec3 growthDirection(const TreeParams& params, const Bud& bud, const glm::vec3& origin, bool hasSpace) {
    glm::vec3 d = params.defaultWeight * bud.direction;
    if (hasSpace) {
        d += params.optimalWeight * bud.optimal;
    }
    const glm::vec3 tropism = bud.order == 0 ? params.trunkTropism : params.branchTropism;
    d += params.tropismWeight * tropism;
    if (bud.order > 0 && params.outwardBias != 0.0f) {
        const glm::vec3 radial(origin.x, 0.0f, origin.z);
        if (glm::length(radial) > kEpsilon) {
            d += params.outwardBias * glm::normalize(radial);
        }
    }
    if (params.wanderAmount > 0.0f) {
        d += params.wanderAmount * noise::fbm3Vec(origin * params.wanderScale, params.seed ^ 0x51EDu);
    }
    return safeNormalize(d, bud.direction);
}

} // namespace

std::vector<RootStrand> growRoots(const TreeGraph& graph) {
    const TreeParams& params = graph.params;
    std::vector<RootStrand> roots;
    if (params.rootCount <= 0 || graph.nodes.empty()) {
        return roots;
    }

    // The canopy's mass by compass sector. Weighting by radius squared makes this a cross-sectional
    // area -- the pipe model's own currency -- so "where is the canopy heavy" is answered by the
    // same quantity that decided how thick the limbs are.
    constexpr int kSectors = 24;
    std::array<float, kSectors> sector{};
    sector.fill(0.0f);
    float sectorTotal = 0.0f;
    for (const TreeNode& node : graph.nodes) {
        if (node.order == 0) {
            continue;
        }
        const glm::vec2 radial(node.position.x, node.position.z);
        if (glm::length(radial) < 1e-3f) {
            continue;
        }
        float angle = std::atan2(radial.y, radial.x);
        if (angle < 0.0f) {
            angle += glm::two_pi<float>();
        }
        const auto bin = static_cast<int>(angle / glm::two_pi<float>() * kSectors) % kSectors;
        const float mass = node.radius * node.radius * node.length;
        sector[static_cast<std::size_t>(bin)] += mass;
        sectorTotal += mass;
    }
    // Mix the measured distribution with a flat one. Pure coupling puts every root under the heavy
    // side and leaves the light side unanchored, which looks like a mistake rather than a response;
    // pure uniformity is the identical-tentacles failure the brief names in section 8.
    const float coupling = std::clamp(params.rootCanopyCoupling, 0.0f, 1.0f);
    std::array<float, kSectors> weight{};
    float weightTotal = 0.0f;
    for (int i = 0; i < kSectors; ++i) {
        const float measured = sectorTotal > 0.0f ? sector[static_cast<std::size_t>(i)] / sectorTotal : 0.0f;
        weight[static_cast<std::size_t>(i)] = (1.0f - coupling) / kSectors + coupling * measured;
        weightTotal += weight[static_cast<std::size_t>(i)];
    }
    std::array<float, kSectors> cdf{};
    float running = 0.0f;
    for (int i = 0; i < kSectors; ++i) {
        running += weight[static_cast<std::size_t>(i)] / std::max(weightTotal, 1e-6f);
        cdf[static_cast<std::size_t>(i)] = running;
    }

    const float baseRadius = graph.nodes[0].radius;
    const float sectorWidth = glm::two_pi<float>() / kSectors;

    const auto march = [&](std::uint32_t id, std::uint32_t parent, glm::vec3 origin, glm::vec3 dir,
                           float length, float radius, std::uint32_t hashBase) {
        RootStrand strand;
        strand.id = id;
        strand.parent = parent;
        const int segments = std::max(params.rootSegments, 2);
        const float step = length / static_cast<float>(segments);
        glm::vec3 p = origin;
        glm::vec3 d = dir;
        strand.points.push_back(p);
        strand.radii.push_back(radius);
        for (int s = 0; s < segments; ++s) {
            const float u = static_cast<float>(s + 1) / static_cast<float>(segments);
            // A root leaves the trunk almost horizontally, dives, then flattens out again as it
            // runs away under the surface. Marching with a curvature term rather than sampling a
            // shape keeps the wander and the dive in the same place.
            const glm::vec3 wander =
                noise::fbm3Vec(p * 0.45f + glm::vec3(static_cast<float>(hashBase) * 0.37f), params.seed ^ 0x9A1u);
            const float dive = -params.rootCurvature * (1.0f - u) * (1.0f - u);
            d = safeNormalize(d + glm::vec3(0.0f, dive, 0.0f) * 0.5f + wander * 0.22f, d);
            p += d * step;
            // The ground is the y = 0 plane in the generator's own frame. A scene that sits the
            // tree on terrain re-projects these points; doing it here would bake one terrain into
            // a graph that is otherwise placement-independent.
            p.y = std::min(p.y, -params.rootDepth * u * u + 0.35f * std::exp(-u * 6.0f) * radius);
            strand.length += step;
            strand.points.push_back(p);
            strand.radii.push_back(radius * (1.0f - 0.86f * u));
        }
        return strand;
    };

    std::uint32_t nextId = 0;
    for (int i = 0; i < params.rootCount; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        // Inverse-CDF sample of the sector distribution, jittered inside the sector it lands in, so
        // roots cluster under the heavy limbs without ever landing on the same bearing twice.
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(params.rootCount);
        int bin = 0;
        while (bin < kSectors - 1 && cdf[static_cast<std::size_t>(bin)] < u) {
            ++bin;
        }
        const float jitter = noise::hashIndex(params.seed, index, kRootAngle);
        const float angle = (static_cast<float>(bin) + jitter) * sectorWidth;
        const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));

        const float lengthScale = 0.55f + 0.9f * noise::hashIndex(params.seed, index, kRootLength);
        const float length = params.rootSpread * lengthScale;
        const float radius = baseRadius * params.rootRadiusScale * (0.45f + 0.75f * lengthScale);
        const glm::vec3 origin = outward * (baseRadius * 0.55f) + glm::vec3(0.0f, 0.18f * baseRadius, 0.0f);
        const glm::vec3 dir = safeNormalize(outward + glm::vec3(0.0f, -0.18f, 0.0f), outward);

        RootStrand strand = march(nextId, kNoNode, origin, dir, length, radius, index);
        const std::uint32_t parentId = nextId++;
        roots.push_back(std::move(strand));

        if (noise::hashIndex(params.seed, index, kRootSplit) < params.rootSplit) {
            const RootStrand& parentStrand = roots.back();
            const std::size_t forkAt = parentStrand.points.size() / 2;
            const float side = noise::hashIndex(params.seed, index, kRootWander) * 2.0f - 1.0f;
            const glm::vec3 forkDir = safeNormalize(
                outward + glm::vec3(-outward.z, 0.0f, outward.x) * side * 0.9f + glm::vec3(0.0f, -0.1f, 0.0f),
                outward);
            roots.push_back(march(nextId++, parentId, parentStrand.points[forkAt], forkDir, length * 0.55f,
                                  parentStrand.radii[forkAt] * 0.7f, index + 977u));
        }
    }
    return roots;
}

Result<TreeGraph> generateTree(const TreeParams& params) {
    if (auto ok = params.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    const auto started = std::chrono::steady_clock::now();

    TreeGraph graph;
    graph.params = params;

    std::vector<glm::vec3> markers = generateMarkers(params);
    std::vector<std::uint8_t> markerAlive(markers.size(), 1);

    const float occupancy = params.occupancyRadius * params.internodeLength;
    const float perception = params.perceptionDistance * params.internodeLength;
    const float cosPerception = std::cos(std::clamp(params.perceptionAngle, 0.05f, 3.0f));
    const float corridorTop = std::max(params.crown.baseHeight, params.crown.centreHeight - params.crown.halfHeight);

    // --- Seed ----------------------------------------------------------------------------------
    std::vector<TreeNode>& nodes = graph.nodes;
    nodes.reserve(4096);
    TreeNode base;
    base.id = 0;
    base.parent = kNoNode;
    base.axis = 0;
    base.order = 0;
    base.tier = BranchTier::Trunk;
    base.position = glm::vec3(0.0f);
    base.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    nodes.push_back(base);

    std::vector<Bud> buds;
    buds.push_back(Bud{.node = 0, .direction = glm::vec3(0.0f, 1.0f, 0.0f), .axis = 0, .order = 0,
                       .terminal = true, .active = true, .started = true});

    std::uint32_t nextAxis = 1;
    std::vector<std::uint8_t> shed(1, 0);
    std::vector<float> lightAcc(1, 0.0f);   // light this node's subtree gathered, summed over time
    std::vector<float> shedTips(1, 0.0f);   // pipe-model memory of tips that were shed below here
    std::vector<std::uint16_t> axisBirth{0};

    // Scratch, hoisted so the loop does not reallocate every iteration.
    spatial::PointGrid grid;
    std::vector<glm::vec3> aliveMarkers;
    std::vector<std::uint32_t> aliveIndex;
    std::vector<std::uint32_t> hits;
    std::vector<std::uint32_t> claimBud;
    std::vector<float> claimDist;
    std::vector<float> nodeLight;
    std::vector<float> nodeVigor;
    std::vector<std::uint32_t> nodeSize; // subtree node count, for shedding

    for (int iteration = 0; iteration < params.iterations; ++iteration) {
        const float lambda = apicalControl(params, iteration);

        // --- 1. Environment --------------------------------------------------------------------
        aliveMarkers.clear();
        aliveIndex.clear();
        for (std::uint32_t i = 0; i < markers.size(); ++i) {
            if (markerAlive[i] != 0) {
                aliveMarkers.push_back(markers[i]);
                aliveIndex.push_back(i);
            }
        }
        if (!aliveMarkers.empty()) {
            grid.build(aliveMarkers, std::max(perception, occupancy));
            // Markers inside any bud's occupancy zone die: space already taken cannot be competed
            // for again, which is what stops two branches converging on the same point.
            for (const Bud& bud : buds) {
                if (!bud.active) {
                    continue;
                }
                grid.query(nodes[bud.node].position, occupancy, hits);
                for (std::uint32_t h : hits) {
                    markerAlive[aliveIndex[h]] = 0;
                }
            }
            aliveMarkers.clear();
            aliveIndex.clear();
            for (std::uint32_t i = 0; i < markers.size(); ++i) {
                if (markerAlive[i] != 0) {
                    aliveMarkers.push_back(markers[i]);
                    aliveIndex.push_back(i);
                }
            }
            grid.build(aliveMarkers, perception);
        }

        // Each surviving marker is claimed by the nearest bud whose cone contains it. Resolving the
        // contest marker-side rather than bud-side is what makes buds compete: a marker seen by two
        // buds contributes to exactly one of them.
        claimBud.assign(aliveMarkers.size(), kNoNode);
        claimDist.assign(aliveMarkers.size(), 0.0f);
        for (std::uint32_t b = 0; b < buds.size(); ++b) {
            const Bud& bud = buds[b];
            if (!bud.active || aliveMarkers.empty()) {
                continue;
            }
            const glm::vec3 origin = nodes[bud.node].position;
            grid.query(origin, perception, hits);
            for (std::uint32_t h : hits) {
                const glm::vec3 delta = aliveMarkers[h] - origin;
                const float dist = glm::length(delta);
                if (dist < kEpsilon) {
                    continue;
                }
                if (glm::dot(delta / dist, bud.direction) < cosPerception) {
                    continue;
                }
                // The trunk corridor is the trunk's own space. A lateral bud low on the bole can
                // otherwise see straight up the column, colonise it, and become a surviving branch
                // a metre and a half off the ground -- which is the one thing a monumental tree
                // must not have. Reserving the corridor for order-0 buds says what the corridor is
                // for, rather than tuning the perception cone until low branches happen to miss it.
                if (bud.order > 0 && aliveMarkers[h].y < corridorTop) {
                    continue;
                }
                if (claimBud[h] == kNoNode || dist < claimDist[h]) {
                    claimBud[h] = b;
                    claimDist[h] = dist;
                }
            }
        }

        std::vector<float> claimCount(buds.size(), 0.0f);
        std::vector<glm::vec3> claimDir(buds.size(), glm::vec3(0.0f));
        for (std::uint32_t h = 0; h < claimBud.size(); ++h) {
            if (claimBud[h] == kNoNode) {
                continue;
            }
            const std::uint32_t b = claimBud[h];
            claimCount[b] += 1.0f;
            claimDir[b] += glm::normalize(aliveMarkers[h] - nodes[buds[b].node].position);
        }
        for (std::uint32_t b = 0; b < buds.size(); ++b) {
            Bud& bud = buds[b];
            if (!bud.active) {
                bud.q = 0.0f;
                continue;
            }
            const float graded = std::min(claimCount[b] / std::max(params.perceptionCapacity, 1.0f), 1.0f);
            if (graded > 0.0f) {
                bud.everFed = true;
            }
            // The quiescent floor applies only to a bud that has NEVER found space. Its job is to
            // get the seedling out of the ground and up the corridor to the crown; it is not a
            // licence to keep growing forever. Left unconditional -- which is how this was first
            // written -- the terminal bud sails straight out through the top of the envelope and
            // keeps going, because a floor of 0.07 is still more than zero and apical control keeps
            // handing it the lion's share. The tree came out nine units taller than the volume it
            // was asked to fill, which is the envelope ceasing to be the silhouette control.
            bud.q = bud.everFed ? graded : std::max(params.quiescentQ, graded);
            bud.optimal = claimCount[b] > 0.0f ? safeNormalize(claimDir[b], bud.direction) : bud.direction;
        }

        // --- 2. Basipetal pass: light accumulates toward the base -------------------------------
        nodeLight.assign(nodes.size(), 0.0f);
        for (const Bud& bud : buds) {
            if (bud.active && shed[bud.node] == 0) {
                nodeLight[bud.node] += bud.q;
            }
        }
        // Children always have a higher id than their parent (nodes are appended as they grow), so
        // a single reverse sweep is a correct basipetal pass with no recursion and no sort.
        nodeSize.assign(nodes.size(), 1);
        for (std::uint32_t i = static_cast<std::uint32_t>(nodes.size()); i-- > 0;) {
            if (shed[i] != 0) {
                nodeLight[i] = 0.0f;
                nodeSize[i] = 0;
                continue;
            }
            const std::uint32_t parent = nodes[i].parent;
            if (parent != kNoNode) {
                nodeLight[parent] += nodeLight[i];
                nodeSize[parent] += nodeSize[i];
            }
            lightAcc[i] += nodeLight[i];
        }

        // --- 3. Acropetal pass: the resource flows back out, split by apical control -------------
        nodeVigor.assign(nodes.size(), 0.0f);
        nodeVigor[0] = params.alpha * nodeLight[0];
        std::vector<float> budVigor(buds.size(), 0.0f);
        // Buds indexed by the node they sit on, so the split at a node can see them alongside its
        // child nodes. One vector pass beats searching the bud list per node.
        std::vector<std::vector<std::uint32_t>> budsAt(nodes.size());
        for (std::uint32_t b = 0; b < buds.size(); ++b) {
            if (buds[b].active && shed[buds[b].node] == 0) {
                budsAt[buds[b].node].push_back(b);
            }
        }
        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
            if (shed[i] != 0 || nodeVigor[i] <= 0.0f) {
                continue;
            }
            // Successors are the child nodes plus the buds standing on this node. Exactly one is
            // the main axis: the child that kept this node's axis, or failing that the terminal bud.
            float mainQ = 0.0f;
            float lateralQ = 0.0f;
            std::uint32_t mainChild = kNoNode;
            std::uint32_t mainBud = kNoNode;
            for (std::uint32_t c : nodes[i].children) {
                if (shed[c] != 0) {
                    continue;
                }
                if (nodes[c].axis == nodes[i].axis && mainChild == kNoNode) {
                    mainChild = c;
                    mainQ += nodeLight[c];
                } else {
                    lateralQ += nodeLight[c];
                }
            }
            for (std::uint32_t b : budsAt[i]) {
                if (buds[b].terminal && mainChild == kNoNode && mainBud == kNoNode) {
                    mainBud = b;
                    mainQ += buds[b].q;
                } else {
                    lateralQ += buds[b].q;
                }
            }
            const float denom = lambda * mainQ + (1.0f - lambda) * lateralQ;
            if (denom <= kEpsilon) {
                continue;
            }
            const float v = nodeVigor[i];
            const float mainShare = mainQ > 0.0f ? v * lambda * mainQ / denom : 0.0f;
            const float lateralScale = lateralQ > 0.0f ? v * (1.0f - lambda) / denom : 0.0f;

            if (mainChild != kNoNode) {
                nodeVigor[mainChild] += mainShare;
            } else if (mainBud != kNoNode) {
                budVigor[mainBud] += mainShare;
            }
            for (std::uint32_t c : nodes[i].children) {
                if (shed[c] != 0 || c == mainChild) {
                    continue;
                }
                nodeVigor[c] += lateralScale * nodeLight[c];
            }
            for (std::uint32_t b : budsAt[i]) {
                if (b == mainBud) {
                    continue;
                }
                budVigor[b] += lateralScale * buds[b].q;
            }
        }

        // --- 4. Bud fate -----------------------------------------------------------------------
        const auto budCount = static_cast<std::uint32_t>(buds.size());
        bool grewAnything = false;
        for (std::uint32_t b = 0; b < budCount; ++b) {
            // A COPY, not a reference. Growing this bud appends the lateral buds it spawns to the
            // same vector, which reallocates it; a reference taken before that is dangling by the
            // second metamer. That is what this loop did when it was first written, and it did not
            // crash until the marker count went up enough for a bud to make more than one metamer.
            Bud bud = buds[b];
            if (!bud.active || shed[bud.node] != 0) {
                continue;
            }
            bud.vigor = budVigor[b];
            const int metamers = std::min(static_cast<int>(std::floor(bud.vigor)), 4);
            if (metamers < 1) {
                // The paper's fourth bud fate: a bud that keeps receiving nothing aborts. Without
                // this the bud list grows monotonically and the environment pass slows to a crawl
                // on buds that will never move again.
                if (++bud.starved > 6) {
                    bud.active = false;
                }
                buds[b] = bud;
                continue;
            }
            bud.starved = 0;
            const float segment = params.internodeLength * std::clamp(bud.vigor / static_cast<float>(metamers),
                                                                      1.0f, 2.0f);
            const bool hasSpace = bud.q > params.quiescentQ + kEpsilon;

            // A lateral bud that grows for the first time opens an axis of the next order.
            if (!bud.started) {
                bud.axis = nextAxis++;
                axisBirth.push_back(static_cast<std::uint16_t>(iteration));
                bud.started = true;
                bud.axisIndex = 0;
            }

            for (int m = 0; m < metamers; ++m) {
                if (static_cast<int>(nodes.size()) >= params.maxNodes) {
                    break;
                }
                const std::uint32_t parentId = bud.node;
                const glm::vec3 origin = nodes[parentId].position;
                const glm::vec3 dir = growthDirection(params, bud, origin, hasSpace);

                TreeNode child;
                child.id = static_cast<std::uint32_t>(nodes.size());
                child.parent = parentId;
                child.axis = bud.axis;
                child.order = bud.order;
                child.tier = tierOf(bud.order);
                child.birth = static_cast<std::uint16_t>(iteration);
                child.direction = dir;
                child.length = segment;
                child.position = origin + dir * segment;
                child.distanceFromBase = nodes[parentId].distanceFromBase + segment;
                child.distanceAlongAxis = nodes[parentId].axis == bud.axis
                                              ? nodes[parentId].distanceAlongAxis + segment
                                              : segment;
                nodes[parentId].children.push_back(child.id);
                nodes.push_back(child);
                shed.push_back(0);
                lightAcc.push_back(0.0f);
                shedTips.push_back(0.0f);
                grewAnything = true;

                bud.node = child.id;
                bud.direction = dir;
                ++bud.axisIndex;

                // A lateral bud in the axil of this metamer. Phyllotaxis keeps consecutive laterals
                // spiralling around the stem instead of stacking on one side.
                if (bud.order < 254) {
                    glm::vec3 u;
                    glm::vec3 v;
                    frameAround(dir, u, v);
                    const float phi = params.phyllotaxis * static_cast<float>(bud.axisIndex);
                    const glm::vec3 side = std::cos(phi) * u + std::sin(phi) * v;
                    const glm::vec3 lateralDir =
                        safeNormalize(dir * std::cos(params.branchAngle) + side * std::sin(params.branchAngle), dir);
                    buds.push_back(Bud{.node = child.id,
                                       .direction = lateralDir,
                                       .axis = 0,
                                       .order = static_cast<std::uint8_t>(bud.order + 1),
                                       .terminal = false,
                                       .active = true,
                                       .started = false,
                                       .birth = static_cast<std::uint16_t>(iteration)});
                }
            }
            buds[b] = bud;
        }
        if (!grewAnything) {
            break;
        }

        // --- 5. Shedding (Takenaka 1994) -------------------------------------------------------
        // Light gathered per internode. A limb that is large but no longer reaching anything is a
        // liability to the tree; dropping it is what carves negative space into the crown and
        // leaves a clean bole, rather than the uniform density space colonization alone produces.
        if (params.shedThreshold > 0.0f) {
            // nodeLight and nodeSize were measured before this iteration's growth, so only nodes
            // that existed then can be judged. Bounding the loop says so, rather than relying on the
            // grace check happening to exclude the new ones.
            const auto judged = static_cast<std::uint32_t>(nodeLight.size());

            // THE THRESHOLD IS RELATIVE, NOT ABSOLUTE, AND THIS IS THE WHOLE DESIGN.
            //
            // Takenaka's rule compares a branch's light against its size. Applied against a fixed
            // bar it works while the marker cloud is rich and then destroys the tree: every branch
            // fails the bar within an iteration or two of the cloud running out, because depletion
            // is a property of the crown rather than of any one branch. Both earlier attempts here
            // did that -- the instantaneous ratio collapsed the crown the moment markers ran low,
            // and normalising by age made it worse, since a branch that has stopped growing looks
            // worse every iteration forever, so the first shed starts a death spiral.
            //
            // Comparing each branch against the crown's own mean removes the drift entirely. It
            // says "shed the branches doing badly *relative to their siblings*", which is what
            // self-pruning actually is, and it cannot shed everything: something is always above
            // the mean.
            float ratioSum = 0.0f;
            int ratioCount = 0;
            const auto candidate = [&](std::uint32_t i) {
                if (shed[i] != 0 || nodes[i].order < params.shedMinOrder) {
                    return false;
                }
                const std::uint32_t parent = nodes[i].parent;
                if (parent == kNoNode || nodes[parent].axis == nodes[i].axis) {
                    return false; // shedding is a branch-level decision, taken at the fork
                }
                return iteration - static_cast<int>(nodes[i].birth) >= params.shedGrace;
            };
            const auto ratioOf = [&](std::uint32_t i) {
                return nodeSize[i] > 0 ? nodeLight[i] / static_cast<float>(nodeSize[i]) : 0.0f;
            };
            for (std::uint32_t i = 1; i < judged; ++i) {
                if (candidate(i)) {
                    ratioSum += ratioOf(i);
                    ++ratioCount;
                }
            }
            if (ratioCount > 0) {
                const float bar = params.shedThreshold * (ratioSum / static_cast<float>(ratioCount));
                // A cap on how much of the crown may go in one step. Shedding is meant to sculpt,
                // and a rule that can remove half the tree in one iteration is a rule whose output
                // depends on exactly which iteration it fires in.
                const auto budget = static_cast<std::uint32_t>(static_cast<float>(judged) * 0.15f) + 4;
                std::uint32_t removed = 0;
                for (std::uint32_t i = 1; i < judged && removed < budget; ++i) {
                    if (!candidate(i) || ratioOf(i) >= bar) {
                        continue;
                    }
                    float tips = 0.0f;
                    shed[i] = 1;
                    for (std::uint32_t k = i; k < nodes.size(); ++k) {
                        if (shed[k] == 0) {
                            continue;
                        }
                        bool live = false;
                        for (std::uint32_t c : nodes[k].children) {
                            shed[c] = 1;
                            live = true;
                        }
                        if (!live) {
                            tips += 1.0f;
                        }
                        ++removed;
                    }
                    // Pipe-model memory: the paper is explicit that width is NOT reduced when a
                    // branch is shed. Without this the trunk of an old, heavily self-pruned tree
                    // comes out as thin as a sapling's, because it only counts surviving leaves.
                    shedTips[nodes[i].parent] += tips;
                }
            }
            for (Bud& bud : buds) {
                if (bud.active && shed[bud.node] != 0) {
                    bud.active = false;
                }
            }
        }
    }

    // --- Finalise: compact away shed nodes -----------------------------------------------------
    {
        std::vector<std::uint32_t> remap(nodes.size(), kNoNode);
        std::vector<TreeNode> kept;
        std::vector<float> keptShedTips;
        kept.reserve(nodes.size());
        int shedCount = 0;
        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
            if (shed[i] != 0) {
                ++shedCount;
                continue;
            }
            remap[i] = static_cast<std::uint32_t>(kept.size());
            TreeNode node = nodes[i];
            node.id = remap[i];
            node.parent = node.parent == kNoNode ? kNoNode : remap[node.parent];
            node.children.clear();
            kept.push_back(std::move(node));
            keptShedTips.push_back(shedTips[i]);
        }
        for (std::uint32_t i = 0; i < kept.size(); ++i) {
            if (kept[i].parent != kNoNode) {
                kept[kept[i].parent].children.push_back(i);
            }
        }
        graph.stats.shedNodes = shedCount;
        nodes = std::move(kept);
        shedTips = std::move(keptShedTips);
    }
    if (nodes.empty()) {
        return fail("tree: generation produced no nodes");
    }

    // --- Renumber axes densely, and record them ------------------------------------------------
    {
        std::vector<std::uint32_t> axisRemap;
        std::vector<std::uint32_t> seen(nextAxis, kNoNode);
        for (TreeNode& node : nodes) {
            if (node.axis < seen.size() && seen[node.axis] == kNoNode) {
                seen[node.axis] = static_cast<std::uint32_t>(graph.axes.size());
                TreeAxis axis;
                axis.id = seen[node.axis];
                axis.order = node.order;
                axis.tier = node.tier;
                graph.axes.push_back(axis);
            }
            node.axis = node.axis < seen.size() ? seen[node.axis] : 0;
        }
        for (TreeNode& node : nodes) {
            TreeAxis& axis = graph.axes[node.axis];
            axis.nodes.push_back(node.id);
            if (axis.firstNode == kNoNode) {
                axis.firstNode = node.id;
                axis.baseDirection = node.direction;
                if (node.parent != kNoNode) {
                    axis.parentAxis = nodes[node.parent].axis;
                    axis.divergenceAngle =
                        std::acos(std::clamp(glm::dot(node.direction, nodes[node.parent].direction), -1.0f, 1.0f));
                }
            }
            axis.length += node.length;
        }
    }

    // --- Pipe model: basipetal diameter accumulation -------------------------------------------
    {
        const float k = params.pipeExponent;
        const float tipArea = std::pow(params.tipRadius, k);
        std::vector<float> area(nodes.size(), 0.0f);
        for (std::uint32_t i = static_cast<std::uint32_t>(nodes.size()); i-- > 0;) {
            if (nodes[i].children.empty()) {
                area[i] += tipArea;
            }
            area[i] += shedTips[i] * tipArea;
            if (nodes[i].parent != kNoNode) {
                area[nodes[i].parent] += area[i];
            }
        }
        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
            float r = params.radiusScale * std::pow(std::max(area[i], tipArea), 1.0f / k);
            // Buttressing: the base of an ancient tree is not the top of a cone. A short, sharp
            // flare reads as mass without needing a second geometry pass.
            if (params.trunkFlare > 0.0f && params.flareHeight > 0.0f) {
                r *= 1.0f + params.trunkFlare * std::exp(-nodes[i].position.y / params.flareHeight);
            }
            nodes[i].radius = r;
        }
        for (TreeNode& node : nodes) {
            node.parentRadius = node.parent == kNoNode ? node.radius : nodes[node.parent].radius;
        }
        for (TreeAxis& axis : graph.axes) {
            if (axis.firstNode != kNoNode) {
                axis.baseRadius = nodes[axis.firstNode].radius;
            }
        }
    }

    // --- Bounds, animation data, foliage --------------------------------------------------------
    graph.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    graph.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    float maxDistance = 0.0f;
    for (const TreeNode& node : nodes) {
        graph.boundsMin = glm::min(graph.boundsMin, node.position - glm::vec3(node.radius));
        graph.boundsMax = glm::max(graph.boundsMax, node.position + glm::vec3(node.radius));
        maxDistance = std::max(maxDistance, node.distanceFromBase);
    }
    const float invMaxDistance = maxDistance > kEpsilon ? 1.0f / maxDistance : 0.0f;

    for (TreeNode& node : nodes) {
        node.phase = noise::hashIndex(params.seed, node.id, kNodePhase) * glm::two_pi<float>();
        // How far this node is free to move. Compliance of a cantilever goes as length over the
        // second moment of area, so "far from the base and thin" is the physical statement of "moves
        // a lot" -- and it produces the hierarchy the brief asks for (trunk still, tips lively)
        // without anyone having to author four amplitude constants.
        const float reach = node.distanceFromBase * invMaxDistance;
        const float slenderness = std::clamp(params.tipRadius / std::max(node.radius, kEpsilon), 0.0f, 1.0f);
        node.animationWeight = std::clamp(reach * std::sqrt(slenderness) * 1.6f, 0.0f, 1.0f);
        node.audioResponseWeight = node.animationWeight * node.animationWeight;
    }

    for (const TreeNode& node : nodes) {
        if (!node.children.empty() || node.order < params.foliageMinOrder ||
            node.radius > params.foliageMaxRadius) {
            continue;
        }
        FoliageSite site;
        site.node = node.id;
        site.axis = node.axis;
        site.position = node.position;
        site.direction = node.direction;
        site.radius = 0.6f + 0.8f * noise::hashIndex(params.seed, node.id, kFoliageRadius);
        site.phase = noise::hashIndex(params.seed, node.id, kFoliagePhase) * glm::two_pi<float>();
        const glm::vec3 toCentre = node.position - glm::vec3(0.0f, params.crown.centreHeight, 0.0f);
        const float extent = std::max(params.crown.radius, params.crown.halfHeight);
        site.exposure = std::clamp(glm::length(toCentre) / std::max(extent, kEpsilon), 0.0f, 1.0f);
        graph.foliage.push_back(site);
    }

    // --- Stats ----------------------------------------------------------------------------------
    TreeStats& stats = graph.stats;
    stats.nodeCount = static_cast<int>(nodes.size());
    stats.axisCount = static_cast<int>(graph.axes.size());
    stats.foliageSites = static_cast<int>(graph.foliage.size());
    for (const TreeNode& node : nodes) {
        stats.maxOrder = std::max(stats.maxOrder, static_cast<int>(node.order));
        stats.totalBranchLength += node.length;
        switch (node.tier) {
        case BranchTier::Trunk: ++stats.trunkNodes; break;
        case BranchTier::Primary: ++stats.primaryNodes; break;
        case BranchTier::Secondary: ++stats.secondaryNodes; break;
        case BranchTier::Tertiary: ++stats.tertiaryNodes; break;
        }
    }
    for (const TreeAxis& axis : graph.axes) {
        switch (axis.tier) {
        case BranchTier::Primary: ++stats.primaryAxes; break;
        case BranchTier::Secondary: ++stats.secondaryAxes; break;
        case BranchTier::Tertiary: ++stats.tertiaryAxes; break;
        case BranchTier::Trunk: break;
        }
    }
    stats.height = graph.boundsMax.y - std::min(graph.boundsMin.y, 0.0f);
    stats.crownWidth = std::max(graph.boundsMax.x - graph.boundsMin.x, graph.boundsMax.z - graph.boundsMin.z);
    stats.trunkBaseRadius = nodes[0].radius;
    // Trunk height is where the trunk stops being the only thing there: the first node on axis 0
    // that has a child on another axis.
    stats.trunkHeight = stats.height;
    for (const TreeNode& node : nodes) {
        if (node.axis != 0) {
            continue;
        }
        bool forks = false;
        for (std::uint32_t c : node.children) {
            if (nodes[c].axis != node.axis) {
                forks = true;
            }
        }
        if (forks) {
            stats.trunkHeight = node.position.y;
            break;
        }
    }

    graph.roots = growRoots(graph);
    stats.rootStrands = static_cast<int>(graph.roots.size());
    for (const RootStrand& root : graph.roots) {
        for (std::size_t i = 0; i < root.points.size(); ++i) {
            const float r = root.radii[i];
            graph.boundsMin = glm::min(graph.boundsMin, root.points[i] - glm::vec3(r));
            graph.boundsMax = glm::max(graph.boundsMax, root.points[i] + glm::vec3(r));
        }
    }

    stats.generationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return graph;
}

namespace {

nlohmann::json vecToJson(const glm::vec3& v) {
    return nlohmann::json{v.x, v.y, v.z};
}

glm::vec3 vecFromJson(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) {
        return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

} // namespace

nlohmann::json TreeParams::toJson() const {
    // Everything is written unconditionally. A tree's parameter block is the record that lets a
    // winning candidate be reproduced exactly; omitting defaults would make the file depend on a
    // default that a later version is free to change, which is the one thing it must not do.
    nlohmann::json j;
    j["seed"] = seed;
    j["crown"] = {{"baseHeight", crown.baseHeight},   {"centreHeight", crown.centreHeight},
                  {"radius", crown.radius},           {"halfHeight", crown.halfHeight},
                  {"shoulder", crown.shoulder},       {"lumpiness", crown.lumpiness},
                  {"lumpScale", crown.lumpScale},     {"coreHollow", crown.coreHollow},
                  {"trunkCorridorRadius", crown.trunkCorridorRadius},
                  {"trunkCorridorBottom", crown.trunkCorridorBottom}};
    j["markerCount"] = markerCount;
    j["iterations"] = iterations;
    j["internodeLength"] = internodeLength;
    j["occupancyRadius"] = occupancyRadius;
    j["perceptionDistance"] = perceptionDistance;
    j["perceptionAngle"] = perceptionAngle;
    j["perceptionCapacity"] = perceptionCapacity;
    j["quiescentQ"] = quiescentQ;
    j["alpha"] = alpha;
    j["lambdaYoung"] = lambdaYoung;
    j["lambdaOld"] = lambdaOld;
    j["apicalReleaseStart"] = apicalReleaseStart;
    j["apicalReleaseEnd"] = apicalReleaseEnd;
    j["defaultWeight"] = defaultWeight;
    j["optimalWeight"] = optimalWeight;
    j["tropismWeight"] = tropismWeight;
    j["trunkTropism"] = vecToJson(trunkTropism);
    j["branchTropism"] = vecToJson(branchTropism);
    j["outwardBias"] = outwardBias;
    j["wanderAmount"] = wanderAmount;
    j["wanderScale"] = wanderScale;
    j["branchAngle"] = branchAngle;
    j["phyllotaxis"] = phyllotaxis;
    j["shedThreshold"] = shedThreshold;
    j["shedGrace"] = shedGrace;
    j["shedMinOrder"] = shedMinOrder;
    j["pipeExponent"] = pipeExponent;
    j["tipRadius"] = tipRadius;
    j["radiusScale"] = radiusScale;
    j["trunkFlare"] = trunkFlare;
    j["flareHeight"] = flareHeight;
    j["foliageMinOrder"] = foliageMinOrder;
    j["foliageMaxRadius"] = foliageMaxRadius;
    j["rootCount"] = rootCount;
    j["rootSpread"] = rootSpread;
    j["rootDepth"] = rootDepth;
    j["rootCurvature"] = rootCurvature;
    j["rootRadiusScale"] = rootRadiusScale;
    j["rootSegments"] = rootSegments;
    j["rootSplit"] = rootSplit;
    j["rootCanopyCoupling"] = rootCanopyCoupling;
    j["maxNodes"] = maxNodes;
    return j;
}

Result<TreeParams> TreeParams::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("tree parameters: expected an object");
    }
    TreeParams p;
    p.seed = j.value("seed", p.seed);
    if (const auto it = j.find("crown"); it != j.end() && it->is_object()) {
        const nlohmann::json& c = *it;
        p.crown.baseHeight = c.value("baseHeight", p.crown.baseHeight);
        p.crown.centreHeight = c.value("centreHeight", p.crown.centreHeight);
        p.crown.radius = c.value("radius", p.crown.radius);
        p.crown.halfHeight = c.value("halfHeight", p.crown.halfHeight);
        p.crown.shoulder = c.value("shoulder", p.crown.shoulder);
        p.crown.lumpiness = c.value("lumpiness", p.crown.lumpiness);
        p.crown.lumpScale = c.value("lumpScale", p.crown.lumpScale);
        p.crown.coreHollow = c.value("coreHollow", p.crown.coreHollow);
        p.crown.trunkCorridorRadius = c.value("trunkCorridorRadius", p.crown.trunkCorridorRadius);
        p.crown.trunkCorridorBottom = c.value("trunkCorridorBottom", p.crown.trunkCorridorBottom);
    }
    p.markerCount = j.value("markerCount", p.markerCount);
    p.iterations = j.value("iterations", p.iterations);
    p.internodeLength = j.value("internodeLength", p.internodeLength);
    p.occupancyRadius = j.value("occupancyRadius", p.occupancyRadius);
    p.perceptionDistance = j.value("perceptionDistance", p.perceptionDistance);
    p.perceptionAngle = j.value("perceptionAngle", p.perceptionAngle);
    p.perceptionCapacity = j.value("perceptionCapacity", p.perceptionCapacity);
    p.quiescentQ = j.value("quiescentQ", p.quiescentQ);
    p.alpha = j.value("alpha", p.alpha);
    p.lambdaYoung = j.value("lambdaYoung", p.lambdaYoung);
    p.lambdaOld = j.value("lambdaOld", p.lambdaOld);
    p.apicalReleaseStart = j.value("apicalReleaseStart", p.apicalReleaseStart);
    p.apicalReleaseEnd = j.value("apicalReleaseEnd", p.apicalReleaseEnd);
    p.defaultWeight = j.value("defaultWeight", p.defaultWeight);
    p.optimalWeight = j.value("optimalWeight", p.optimalWeight);
    p.tropismWeight = j.value("tropismWeight", p.tropismWeight);
    if (const auto it = j.find("trunkTropism"); it != j.end()) {
        p.trunkTropism = vecFromJson(*it, p.trunkTropism);
    }
    if (const auto it = j.find("branchTropism"); it != j.end()) {
        p.branchTropism = vecFromJson(*it, p.branchTropism);
    }
    p.outwardBias = j.value("outwardBias", p.outwardBias);
    p.wanderAmount = j.value("wanderAmount", p.wanderAmount);
    p.wanderScale = j.value("wanderScale", p.wanderScale);
    p.branchAngle = j.value("branchAngle", p.branchAngle);
    p.phyllotaxis = j.value("phyllotaxis", p.phyllotaxis);
    p.shedThreshold = j.value("shedThreshold", p.shedThreshold);
    p.shedGrace = j.value("shedGrace", p.shedGrace);
    p.shedMinOrder = j.value("shedMinOrder", p.shedMinOrder);
    p.pipeExponent = j.value("pipeExponent", p.pipeExponent);
    p.tipRadius = j.value("tipRadius", p.tipRadius);
    p.radiusScale = j.value("radiusScale", p.radiusScale);
    p.trunkFlare = j.value("trunkFlare", p.trunkFlare);
    p.flareHeight = j.value("flareHeight", p.flareHeight);
    p.foliageMinOrder = j.value("foliageMinOrder", p.foliageMinOrder);
    p.foliageMaxRadius = j.value("foliageMaxRadius", p.foliageMaxRadius);
    p.rootCount = j.value("rootCount", p.rootCount);
    p.rootSpread = j.value("rootSpread", p.rootSpread);
    p.rootDepth = j.value("rootDepth", p.rootDepth);
    p.rootCurvature = j.value("rootCurvature", p.rootCurvature);
    p.rootRadiusScale = j.value("rootRadiusScale", p.rootRadiusScale);
    p.rootSegments = j.value("rootSegments", p.rootSegments);
    p.rootSplit = j.value("rootSplit", p.rootSplit);
    p.rootCanopyCoupling = j.value("rootCanopyCoupling", p.rootCanopyCoupling);
    p.maxNodes = j.value("maxNodes", p.maxNodes);
    if (auto ok = p.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return p;
}

Result<void> TreeGraph::validateTopology() const {
    if (nodes.empty()) {
        return fail("tree: graph has no nodes");
    }
    if (nodes[0].parent != kNoNode) {
        return fail("tree: node 0 must be the root but has parent {}", nodes[0].parent);
    }
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        const TreeNode& node = nodes[i];
        if (node.id != i) {
            return fail("tree: node at index {} carries id {}", i, node.id);
        }
        if (i > 0) {
            if (node.parent == kNoNode) {
                return fail("tree: node {} has no parent but is not the root", i);
            }
            // Parents preceding children is what makes the basipetal and acropetal sweeps single
            // linear passes, and it is also a sufficient proof of acyclicity: an edge can only ever
            // point from a lower index to a higher one, so no cycle can close.
            if (node.parent >= i) {
                return fail("tree: node {} has parent {}, which does not precede it", i, node.parent);
            }
        }
        for (std::uint32_t c : node.children) {
            if (c >= nodes.size()) {
                return fail("tree: node {} lists child {} out of range", i, c);
            }
            if (nodes[c].parent != i) {
                return fail("tree: node {} lists child {}, whose parent is {}", i, c, nodes[c].parent);
            }
        }
        if (node.axis >= axes.size()) {
            return fail("tree: node {} references axis {} of {}", i, node.axis, axes.size());
        }
        if (node.parent != kNoNode) {
            const TreeNode& parent = nodes[node.parent];
            if (node.axis != parent.axis && node.order != parent.order + 1) {
                return fail("tree: node {} starts a new axis at order {} under order {}", i, node.order,
                            parent.order);
            }
            if (node.axis == parent.axis && node.order != parent.order) {
                return fail("tree: node {} continues axis {} but changes order", i, node.axis);
            }
            // The pipe model can only widen a branch toward the base, so a child wider than its
            // parent means the accumulation ran the wrong way. A small tolerance because the base
            // flare is applied after accumulation and is a function of height, not of topology.
            if (node.radius > parent.radius * 1.02f + 1e-4f) {
                return fail("tree: node {} radius {} exceeds parent {} radius {}", i, node.radius, node.parent,
                            parent.radius);
            }
        }
    }
    for (std::uint32_t a = 0; a < axes.size(); ++a) {
        for (std::uint32_t n : axes[a].nodes) {
            if (n >= nodes.size() || nodes[n].axis != a) {
                return fail("tree: axis {} lists node {} which does not belong to it", a, n);
            }
        }
    }
    return {};
}

std::uint64_t TreeGraph::contentHash() const {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 0x100000001B3ULL;
    };
    const auto mixFloat = [&mix](float f) {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(f));
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };
    for (const TreeNode& node : nodes) {
        mix(node.parent);
        mix(node.axis);
        mix(node.order);
        mixFloat(node.position.x);
        mixFloat(node.position.y);
        mixFloat(node.position.z);
        mixFloat(node.radius);
    }
    for (const FoliageSite& site : foliage) {
        mix(site.node);
        mixFloat(site.radius);
    }
    for (const RootStrand& root : roots) {
        for (const glm::vec3& p : root.points) {
            mixFloat(p.x);
            mixFloat(p.y);
            mixFloat(p.z);
        }
    }
    return h;
}

} // namespace avgen::scene
