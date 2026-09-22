#include "scene/scatter_anchors.hpp"

#include "scene/procedural_detail.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <limits>

namespace avgen::scene {

std::string scatterObjectName(std::string_view terrain, std::string_view layer) {
    std::string name;
    name.reserve(terrain.size() + 1 + layer.size());
    name.append(terrain).append("_").append(layer);
    return name;
}

bool scatterAnchorGate(const spatial::InstanceRecord& record, const ScatterAnchor& spec) {
    if (!(record.random.w < spec.randomBelow)) {
        return false;
    }
    if (spec.litOnly) {
        const glm::vec3 e(record.emissive);
        return e.x > 0.0f || e.y > 0.0f || e.z > 0.0f;
    }
    return true;
}

namespace {

const ProceduralGeometry* findObject(const std::vector<ProceduralGeometry>& objects, const std::string& name) {
    for (const ProceduralGeometry& g : objects) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

// The lead object and the material parts that share its instances (ADR-108).
std::vector<const ProceduralGeometry*> partsOf(const std::vector<ProceduralGeometry>& objects,
                                               const ProceduralGeometry& lead) {
    std::vector<const ProceduralGeometry*> parts{&lead};
    for (const ProceduralGeometry& g : objects) {
        if (!g.partOf.empty() && g.partOf == lead.name) {
            parts.push_back(&g);
        }
    }
    return parts;
}

// The crown's centre in the lead's source space (before `sourceTransform`).
glm::vec3 crownCentre(const std::vector<ProceduralGeometry>& objects,
                      const std::vector<const ProceduralGeometry*>& parts) {
    const GenerationContext ctx{&objects, nullptr, 0};
    glm::vec3 bestCentre(0.0f);
    glm::vec3 bestHalf(0.0f);
    float bestBottom = std::numeric_limits<float>::lowest();
    for (const ProceduralGeometry* part : parts) {
        glm::vec3 c(0.0f);
        glm::vec3 h(0.0f);
        detail::sourceBox(*part, ctx, c, h);
        const float bottom = c.y - h.y;
        if (bottom > bestBottom) {
            bestBottom = bottom;
            bestCentre = c;
            bestHalf = h;
        }
    }
    if (parts.size() == 1) {
        // One part reaches the ground: take the upper half of it, which on a bare tree is where
        // the branches are.
        bestCentre.y += bestHalf.y * 0.5f;
    }
    return bestCentre;
}

std::uint64_t mixVersion(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

} // namespace

std::uint64_t scatterAnchorVersion(const std::vector<ProceduralGeometry>& objects, const ScatterAnchor& spec) {
    std::uint64_t h = 0x51ED2701ull;
    for (const std::string& layer : spec.layers) {
        const ProceduralGeometry* lead = findObject(objects, scatterObjectName(spec.terrain, layer));
        if (lead == nullptr) {
            h = mixVersion(h, 0xFFFFFFFFull);
            continue;
        }
        for (const ProceduralGeometry* part : partsOf(objects, *lead)) {
            h = mixVersion(h, part->structureVersion);
            h = mixVersion(h, part->meshHash);
        }
        h = mixVersion(h, lead->instances.size());
    }
    return h;
}

ScatterAnchorSet scatterAnchorPoints(const std::vector<ProceduralGeometry>& objects, const ScatterAnchor& spec) {
    ScatterAnchorSet set;
    set.version = scatterAnchorVersion(objects, spec);
    for (std::size_t li = 0; li < spec.layers.size(); ++li) {
        const std::string name = scatterObjectName(spec.terrain, spec.layers[li]);
        const ProceduralGeometry* lead = findObject(objects, name);
        if (lead == nullptr || lead->instances.empty()) {
            set.missing.push_back(spec.layers[li]);
            continue;
        }
        const std::vector<const ProceduralGeometry*> parts = partsOf(objects, *lead);
        // Through the source transform exactly as the vertex stage takes a source-space point
        // (procedural.wgsl `instancePoint(inst, proc.sourceMatrix * p)`): the layer's height
        // normalisation lives there.
        const glm::vec3 local = glm::vec3(lead->sourceTransform.matrix() * glm::vec4(crownCentre(objects, parts), 1.0f));
        for (std::size_t i = 0; i < lead->instances.size(); ++i) {
            const spatial::InstanceRecord& r = lead->instances[i];
            ++set.considered;
            const glm::vec3 e(r.emissive);
            if (e.x > 0.0f || e.y > 0.0f || e.z > 0.0f) {
                ++set.lit;
            }
            if (!scatterAnchorGate(r, spec)) {
                continue;
            }
            const glm::quat q(r.rotation.w, r.rotation.x, r.rotation.y, r.rotation.z);
            ScatterAnchorPoint p;
            p.centre = glm::vec3(r.position) + q * (local * glm::vec3(r.scale));
            p.key = (static_cast<std::uint64_t>(li) << 32) | static_cast<std::uint64_t>(i);
            set.points.push_back(p);
        }
    }
    return set;
}

std::vector<glm::vec3> nearestScatterAnchors(std::span<const ScatterAnchorPoint> points, const glm::vec3& eye,
                                             float viewDistance, std::uint32_t maxAnchors) {
    struct Near {
        float d2;
        std::uint64_t key;
        glm::vec3 centre;
    };
    std::vector<Near> near;
    const float reach2 = viewDistance * viewDistance;
    for (const ScatterAnchorPoint& p : points) {
        const glm::vec3 d = p.centre - eye;
        const float d2 = glm::dot(d, d);
        if (d2 <= reach2) {
            near.push_back({d2, p.key, p.centre});
        }
    }
    const auto closer = [](const Near& a, const Near& b) { return a.d2 < b.d2 || (a.d2 == b.d2 && a.key < b.key); };
    if (near.size() > maxAnchors) {
        std::nth_element(near.begin(), near.begin() + maxAnchors, near.end(), closer);
        near.resize(maxAnchors);
    }
    std::sort(near.begin(), near.end(), closer);
    std::vector<glm::vec3> out;
    out.reserve(near.size());
    for (const Near& n : near) {
        out.push_back(n.centre);
    }
    return out;
}

} // namespace avgen::scene
