#include "scene/wire_edges.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace avgen::scene {

namespace {

// A welded vertex: positions quantised to a tenth of a micron of the mesh's own scale.
struct Key {
    std::int64_t x, y, z;
    bool operator==(const Key&) const = default;
};
struct KeyHash {
    std::size_t operator()(const Key& k) const {
        std::uint64_t h = 1469598103934665603ull;
        for (std::int64_t v : {k.x, k.y, k.z}) {
            h ^= static_cast<std::uint64_t>(v);
            h *= 1099511628211ull;
        }
        return static_cast<std::size_t>(h);
    }
};

struct EdgeInfo {
    std::uint32_t a = 0; // original vertex indices of the first occurrence (for the normals)
    std::uint32_t b = 0;
    glm::vec3 n0{0.0f}; // the first face's normal
    float maxAngleCos = 1.0f; // the smallest cos between the first face and any other
    std::uint32_t faces = 0;
};

} // namespace

WireEdgeMesh buildWireEdges(const MeshData& mesh, int mode, float creaseDegrees, std::uint32_t maxEdges) {
    WireEdgeMesh out;
    if (mode <= 0 || !mesh.valid()) {
        return out;
    }
    const auto [lo, hi] = mesh.bounds();
    const float extent = std::max(glm::length(hi - lo), 1e-6f);
    const float quantum = extent * 1e-6f;
    std::unordered_map<Key, std::uint32_t, KeyHash> welded;
    std::vector<std::uint32_t> weld(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const glm::vec3& p = mesh.vertices[i].position;
        const Key k{static_cast<std::int64_t>(std::llround(p.x / quantum)),
                    static_cast<std::int64_t>(std::llround(p.y / quantum)),
                    static_cast<std::int64_t>(std::llround(p.z / quantum))};
        weld[i] = welded.try_emplace(k, static_cast<std::uint32_t>(welded.size())).first->second;
    }
    std::unordered_map<std::uint64_t, EdgeInfo> edges;
    std::vector<std::uint64_t> order; // first-seen order, so the output is deterministic
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::uint32_t idx[3] = {mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]};
        const glm::vec3 p0 = mesh.vertices[idx[0]].position;
        const glm::vec3 p1 = mesh.vertices[idx[1]].position;
        const glm::vec3 p2 = mesh.vertices[idx[2]].position;
        const glm::vec3 c = glm::cross(p1 - p0, p2 - p0);
        const float len = glm::length(c);
        if (!(len > quantum * quantum)) {
            continue; // degenerate
        }
        const glm::vec3 n = c / len;
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t va = idx[e];
            const std::uint32_t vb = idx[(e + 1) % 3];
            std::uint32_t wa = weld[va];
            std::uint32_t wb = weld[vb];
            if (wa == wb) {
                continue;
            }
            const std::uint64_t key = wa < wb ? (static_cast<std::uint64_t>(wa) << 32) | wb
                                              : (static_cast<std::uint64_t>(wb) << 32) | wa;
            auto [it, inserted] = edges.try_emplace(key);
            EdgeInfo& info = it->second;
            if (inserted) {
                info.a = va;
                info.b = vb;
                info.n0 = n;
                order.push_back(key);
            } else {
                info.maxAngleCos = std::min(info.maxAngleCos, glm::dot(info.n0, n));
            }
            ++info.faces;
        }
    }
    const float creaseCos = std::cos(glm::radians(std::clamp(creaseDegrees, 0.0f, 180.0f)));
    for (const std::uint64_t key : order) {
        const EdgeInfo& info = edges.at(key);
        const bool feature = info.faces == 1 || info.maxAngleCos < creaseCos;
        if (mode == 1 && !feature) {
            continue;
        }
        if (out.edgeCount >= maxEdges) {
            break;
        }
        const Vertex& A = mesh.vertices[info.a];
        const Vertex& B = mesh.vertices[info.b];
        const auto base = static_cast<std::uint32_t>(out.vertices.size());
        out.vertices.push_back({A.position, A.normal, {-1.0f, 1.0f}, B.position, B.normal});
        out.vertices.push_back({A.position, A.normal, {1.0f, 1.0f}, B.position, B.normal});
        out.vertices.push_back({B.position, B.normal, {-1.0f, -1.0f}, A.position, A.normal});
        out.vertices.push_back({B.position, B.normal, {1.0f, -1.0f}, A.position, A.normal});
        for (std::uint32_t i : {0u, 1u, 2u, 2u, 1u, 3u}) {
            out.indices.push_back(base + i);
        }
        ++out.edgeCount;
    }
    return out;
}

} // namespace avgen::scene
