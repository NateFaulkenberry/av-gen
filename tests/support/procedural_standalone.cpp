// Standalone link support for the procedural GPU branch (ADR-023). The CPU side of
// scene/procedural.hpp (generators, distributions, rebuild, JSON) is implemented on another
// branch; until it is merged, the library's only CPU dependency, scene::makeSourceMesh, is
// provided here for test executables built with -DAVGEN_PROC_GPU_STANDALONE=ON. Never compiled
// into the engine libraries; empty in a normal build.
namespace avgen::scene {} // keeps this translation unit non-empty without the define

#ifdef AVGEN_PROC_GPU_STANDALONE

#include "scene/procedural.hpp"

#include <cmath>
#include <numbers>

namespace avgen::scene {

namespace {

// Six quads, four vertices each, faceted normals, CCW outwards; `subdivisions` is ignored.
MeshData standaloneBox(glm::vec3 size) {
    MeshData m;
    m.name = "standalone-box";
    const glm::vec3 h = size * 0.5f;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 normal = n[f];
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({(normal - u - v) * h, normal, {0, 0}});
        m.vertices.push_back({(normal + u - v) * h, normal, {1, 0}});
        m.vertices.push_back({(normal + u + v) * h, normal, {1, 1}});
        m.vertices.push_back({(normal - u + v) * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// Smooth-shaded side (radialSegments + 1 columns x heightSegments + 1 rows), flat caps with a
// centre vertex. Centred on the origin, axis +Y, CCW outwards.
MeshData standaloneCylinder(float radius, float height, int radialSegments, int heightSegments, bool caps) {
    MeshData m;
    m.name = "standalone-cylinder";
    const float halfH = height * 0.5f;
    const int cols = radialSegments + 1;
    for (int row = 0; row <= heightSegments; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(heightSegments);
        const float y = -halfH + height * v;
        for (int col = 0; col < cols; ++col) {
            const float u = static_cast<float>(col) / static_cast<float>(radialSegments);
            const float a = u * 2.0f * std::numbers::pi_v<float>;
            const glm::vec3 n(std::cos(a), 0.0f, -std::sin(a));
            m.vertices.push_back({n * radius + glm::vec3(0.0f, y, 0.0f), n, {u, v}});
        }
    }
    for (int row = 0; row < heightSegments; ++row) {
        for (int col = 0; col < radialSegments; ++col) {
            const auto a = static_cast<std::uint32_t>(row * cols + col);
            const auto b = a + 1;
            const auto c = a + static_cast<std::uint32_t>(cols);
            const auto d = c + 1;
            m.indices.insert(m.indices.end(), {a, b, d, a, d, c});
        }
    }
    if (caps) {
        for (int side = 0; side < 2; ++side) {
            const float y = side == 0 ? halfH : -halfH;
            const glm::vec3 n(0.0f, side == 0 ? 1.0f : -1.0f, 0.0f);
            const auto centre = static_cast<std::uint32_t>(m.vertices.size());
            m.vertices.push_back({{0.0f, y, 0.0f}, n, {0.5f, 0.5f}});
            for (int col = 0; col < cols; ++col) {
                const float a = static_cast<float>(col) / static_cast<float>(radialSegments) * 2.0f * std::numbers::pi_v<float>;
                const glm::vec3 p(std::cos(a) * radius, y, -std::sin(a) * radius);
                m.vertices.push_back({p, n, {0.5f + 0.5f * std::cos(a), 0.5f - 0.5f * std::sin(a)}});
            }
            for (int col = 0; col < radialSegments; ++col) {
                const auto i0 = centre + 1 + static_cast<std::uint32_t>(col);
                const auto i1 = i0 + 1;
                if (side == 0) {
                    m.indices.insert(m.indices.end(), {centre, i0, i1});
                } else {
                    m.indices.insert(m.indices.end(), {centre, i1, i0});
                }
            }
        }
    }
    return m;
}

} // namespace

Result<MeshData> makeSourceMesh(const SourceSpec& spec) {
    switch (spec.kind) {
    case PrimitiveKind::Box:
        return standaloneBox(spec.size);
    case PrimitiveKind::Cylinder:
        if (spec.radialSegments < 3 || spec.heightSegments < 1 || spec.radius <= 0.0f || spec.height <= 0.0f) {
            return fail("standalone cylinder: invalid spec");
        }
        return standaloneCylinder(spec.radius, spec.height, spec.radialSegments, spec.heightSegments, spec.caps);
    case PrimitiveKind::Sphere:
    case PrimitiveKind::Torus:
        break;
    }
    return fail("standalone procedural build: only box and cylinder sources are available");
}

} // namespace avgen::scene

#endif // AVGEN_PROC_GPU_STANDALONE
