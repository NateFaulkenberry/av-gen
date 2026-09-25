#include "scene/mesh_generators.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace avgen::scene {

namespace {

glm::vec2 sphericalUv(const glm::vec3& unit) {
    const float u = 0.5f + std::atan2(unit.z, unit.x) / (2.0f * glm::pi<float>());
    const float v = 0.5f - std::asin(std::clamp(unit.y, -1.0f, 1.0f)) / glm::pi<float>();
    return {u, v};
}

Vertex sphereVertex(const glm::vec3& direction, float radius) {
    const glm::vec3 unit = glm::normalize(direction);
    return Vertex{unit * radius, unit, sphericalUv(unit)};
}

} // namespace

MeshData makeIcosphere(float radius, int subdivisions) {
    subdivisions = std::clamp(subdivisions, 0, 6);
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const std::array<glm::vec3, 12> seeds{{{-1.0f, t, 0.0f},
                                           {1.0f, t, 0.0f},
                                           {-1.0f, -t, 0.0f},
                                           {1.0f, -t, 0.0f},
                                           {0.0f, -1.0f, t},
                                           {0.0f, 1.0f, t},
                                           {0.0f, -1.0f, -t},
                                           {0.0f, 1.0f, -t},
                                           {t, 0.0f, -1.0f},
                                           {t, 0.0f, 1.0f},
                                           {-t, 0.0f, -1.0f},
                                           {-t, 0.0f, 1.0f}}};
    // Counter-clockwise when viewed from outside.
    const std::array<std::uint32_t, 60> seedFaces{
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
        3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1};

    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(10 * (1 << (2 * subdivisions)) + 2));
    for (const auto& seed : seeds) {
        mesh.vertices.push_back(sphereVertex(seed, radius));
    }
    mesh.indices.assign(seedFaces.begin(), seedFaces.end());

    std::unordered_map<std::uint64_t, std::uint32_t> midpoints;
    auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
        if (const auto it = midpoints.find(key); it != midpoints.end()) {
            return it->second;
        }
        const glm::vec3 mid = (mesh.vertices[a].position + mesh.vertices[b].position) * 0.5f;
        const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(sphereVertex(mid, radius));
        midpoints.emplace(key, index);
        return index;
    };

    for (int level = 0; level < subdivisions; ++level) {
        std::vector<std::uint32_t> next;
        next.reserve(mesh.indices.size() * 4);
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const std::uint32_t a = mesh.indices[i];
            const std::uint32_t b = mesh.indices[i + 1];
            const std::uint32_t c = mesh.indices[i + 2];
            const std::uint32_t ab = midpoint(a, b);
            const std::uint32_t bc = midpoint(b, c);
            const std::uint32_t ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        mesh.indices = std::move(next);
        midpoints.clear();
    }
    return mesh;
}

MeshData makeCube(float halfExtent) {
    struct Face {
        glm::vec3 normal;
        glm::vec3 u;
        glm::vec3 v; // u x v == normal
    };
    const std::array<Face, 6> faces{{{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
                                     {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
                                     {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                                     {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}}};
    const std::array<glm::vec2, 4> corners{{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}}};

    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const glm::vec2& corner : corners) {
            const glm::vec3 position = (face.normal + face.u * corner.x + face.v * corner.y) * halfExtent;
            mesh.vertices.push_back(Vertex{position, face.normal, (corner + 1.0f) * 0.5f});
        }
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

MeshData makePlane(float halfExtent, int divisions) {
    divisions = std::max(divisions, 1);
    const auto n = static_cast<std::uint32_t>(divisions);
    MeshData mesh;
    mesh.vertices.reserve(static_cast<std::size_t>((n + 1) * (n + 1)));
    mesh.indices.reserve(static_cast<std::size_t>(n * n * 6));
    for (std::uint32_t row = 0; row <= n; ++row) {
        const float tv = static_cast<float>(row) / static_cast<float>(n);
        for (std::uint32_t col = 0; col <= n; ++col) {
            const float tu = static_cast<float>(col) / static_cast<float>(n);
            const glm::vec3 position{(tu * 2.0f - 1.0f) * halfExtent, 0.0f, (tv * 2.0f - 1.0f) * halfExtent};
            mesh.vertices.push_back(Vertex{position, {0.0f, 1.0f, 0.0f}, {tu, tv}});
        }
    }
    for (std::uint32_t row = 0; row < n; ++row) {
        for (std::uint32_t col = 0; col < n; ++col) {
            const std::uint32_t p00 = row * (n + 1) + col;
            const std::uint32_t p10 = p00 + 1;
            const std::uint32_t p01 = p00 + (n + 1);
            const std::uint32_t p11 = p01 + 1;
            // Counter-clockwise when viewed from +Y.
            mesh.indices.insert(mesh.indices.end(), {p00, p11, p10, p00, p01, p11});
        }
    }
    return mesh;
}

MeshData makeQuad(float halfExtent) {
    MeshData mesh;
    mesh.vertices = {Vertex{{-halfExtent, -halfExtent, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
                     Vertex{{halfExtent, -halfExtent, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
                     Vertex{{halfExtent, halfExtent, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
                     Vertex{{-halfExtent, halfExtent, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
    mesh.indices = {0, 1, 2, 0, 2, 3}; // counter-clockwise when viewed from +Z
    return mesh;
}

namespace {

// The ring of `segments` + 1 points (the seam duplicated, so uv.x runs 0..1) at height y.
void appendRing(MeshData& mesh, float radius, float y, int segments, const glm::vec3& normalTilt, float v) {
    for (int i = 0; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const float a = u * glm::two_pi<float>();
        const glm::vec3 radial(std::cos(a), 0.0f, std::sin(a));
        const glm::vec3 normal = glm::normalize(radial * normalTilt.x + glm::vec3(0.0f, normalTilt.y, 0.0f));
        mesh.vertices.push_back(Vertex{radial * radius + glm::vec3(0.0f, y, 0.0f), normal, {u, v}});
    }
}

// A cap at height y facing `up` (+1 or -1): a fan round a centre vertex.
void appendCap(MeshData& mesh, float radius, float y, int segments, float up) {
    const auto centre = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(Vertex{{0.0f, y, 0.0f}, {0.0f, up, 0.0f}, {0.5f, 0.5f}});
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        const glm::vec3 radial(std::cos(a), 0.0f, std::sin(a));
        mesh.vertices.push_back(Vertex{radial * radius + glm::vec3(0.0f, y, 0.0f), {0.0f, up, 0.0f},
                                       {0.5f + 0.5f * radial.x, 0.5f + 0.5f * radial.z}});
    }
    for (int i = 0; i < segments; ++i) {
        const std::uint32_t a = centre + 1 + static_cast<std::uint32_t>(i);
        // Counter-clockwise seen from the side the cap faces. The ring runs +X towards +Z, which is
        // clockwise seen from +Y.
        if (up > 0.0f) {
            mesh.indices.insert(mesh.indices.end(), {centre, a + 1, a});
        } else {
            mesh.indices.insert(mesh.indices.end(), {centre, a, a + 1});
        }
    }
}

// Quads between two rings of `segments` + 1 points starting at `lower` and `upper`, facing outward.
void joinRings(MeshData& mesh, std::uint32_t lower, std::uint32_t upper, int segments) {
    for (int i = 0; i < segments; ++i) {
        const std::uint32_t a = lower + static_cast<std::uint32_t>(i);
        const std::uint32_t b = upper + static_cast<std::uint32_t>(i);
        mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
}

} // namespace

MeshData makeDisc(float radius, int segments) {
    segments = std::max(segments, 3);
    MeshData mesh;
    appendCap(mesh, radius, 0.0f, segments, 1.0f);
    return mesh;
}

MeshData makeCylinder(float radius, float halfHeight, int segments, bool capped) {
    segments = std::max(segments, 3);
    MeshData mesh;
    const glm::vec3 flat(1.0f, 0.0f, 0.0f);
    appendRing(mesh, radius, -halfHeight, segments, flat, 1.0f);
    appendRing(mesh, radius, halfHeight, segments, flat, 0.0f);
    joinRings(mesh, 0, static_cast<std::uint32_t>(segments + 1), segments);
    if (capped) {
        appendCap(mesh, radius, halfHeight, segments, 1.0f);
        appendCap(mesh, radius, -halfHeight, segments, -1.0f);
    }
    return mesh;
}

MeshData makeCone(float radius, float halfHeight, int segments, bool capped) {
    segments = std::max(segments, 3);
    MeshData mesh;
    // The slant normal: perpendicular to the side line from the base rim to the apex.
    const float height = 2.0f * halfHeight;
    const glm::vec3 slant(height, radius, 0.0f);
    appendRing(mesh, radius, -halfHeight, segments, slant, 1.0f);
    // The apex is a ring of coincident points, one per segment, each carrying its segment's normal,
    // so the side shades smoothly round instead of pinching to one normal at the tip.
    appendRing(mesh, 0.0f, halfHeight, segments, slant, 0.0f);
    joinRings(mesh, 0, static_cast<std::uint32_t>(segments + 1), segments);
    if (capped) {
        appendCap(mesh, radius, -halfHeight, segments, -1.0f);
    }
    return mesh;
}

} // namespace avgen::scene
