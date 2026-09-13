#include "scene/mesh_metrics.hpp"

#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::scene {

float MeshMetrics::areaScale(glm::vec3 scale) {
    const glm::vec3 s = glm::abs(scale);
    return (s.x * s.y + s.y * s.z + s.z * s.x) / 3.0f;
}

MeshMetrics meshMetrics(const MeshData& mesh) {
    MeshMetrics out;
    if (mesh.vertices.empty() || mesh.indices.size() < 3) {
        return out;
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const Vertex& v : mesh.vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    out.boundsCenter = (lo + hi) * 0.5f;
    float radiusSq = 0.0f;
    for (const Vertex& v : mesh.vertices) {
        const glm::vec3 d = v.position - out.boundsCenter;
        radiusSq = std::max(radiusSq, glm::dot(d, d));
    }
    out.boundsRadius = std::sqrt(radiusSq);

    const std::size_t count = mesh.indices.size() / 3;
    // Accumulated in double: a terrain chunk is tens of thousands of triangles whose areas differ by
    // orders of magnitude, and a float sum of those loses the small ones -- which are exactly the
    // ones this number exists to find.
    double area = 0.0;
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t t = 0; t < count; ++t) {
        const std::uint32_t i0 = mesh.indices[t * 3 + 0];
        const std::uint32_t i1 = mesh.indices[t * 3 + 1];
        const std::uint32_t i2 = mesh.indices[t * 3 + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue; // an index past the buffer: counted as a triangle, contributes no area
        }
        const glm::vec3 a = mesh.vertices[i1].position - mesh.vertices[i0].position;
        const glm::vec3 b = mesh.vertices[i2].position - mesh.vertices[i0].position;
        area += 0.5 * static_cast<double>(glm::length(glm::cross(a, b)));
    }
    out.triangles = static_cast<std::uint32_t>(count);
    out.surfaceArea = static_cast<float>(area);
    return out;
}

bool MeshMetricsCache::matches(const Scene& scene) const {
    return identity_ == scene.identity && meshVersion_ == scene.meshVersion &&
           metrics_.size() == scene.meshes.size();
}

void MeshMetricsCache::clear() {
    metrics_.clear();
    identity_ = 0;
    meshVersion_ = 0;
}

void MeshMetricsCache::rebuild(const Scene& scene) {
    metrics_.clear();
    metrics_.reserve(scene.meshes.size());
    for (const MeshData& mesh : scene.meshes) {
        metrics_.push_back(meshMetrics(mesh));
    }
    identity_ = scene.identity;
    meshVersion_ = scene.meshVersion;
    ++rebuilds_;
}

const MeshMetrics& MeshMetricsCache::metrics(const Scene& scene, MeshId mesh) {
    static const MeshMetrics kNone;
    if (!matches(scene)) {
        rebuild(scene);
    }
    if (mesh == kInvalidMesh || mesh >= metrics_.size()) {
        return kNone;
    }
    return metrics_[mesh];
}

} // namespace avgen::scene
