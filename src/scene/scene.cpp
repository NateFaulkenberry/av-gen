#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace avgen::scene {

glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

Transform Transform::fromMatrix(const glm::mat4& m) {
    Transform t;
    glm::vec3 skew;
    glm::vec4 perspective;
    if (!glm::decompose(m, t.scale, t.rotation, t.position, skew, perspective)) {
        t = Transform{};
        t.position = glm::vec3(m[3]);
    }
    return t;
}

glm::mat4 Camera::view() const {
    return glm::lookAtRH(position, target, up);
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspectiveRH_ZO(effectiveFovY(), aspect, nearPlane, farPlane);
}

bool TextureData::valid() const {
    return width > 0 && height > 0 && data.size() == static_cast<std::size_t>(width) * height * bytesPerPixel();
}

bool MeshData::valid() const {
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(vertices.size());
    for (const std::uint32_t index : indices) {
        if (index >= count) {
            return false;
        }
    }
    return true;
}

std::pair<glm::vec3, glm::vec3> MeshData::bounds() const {
    if (vertices.empty()) {
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const auto& v : vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    return {lo, hi};
}

void MeshData::computeNormals() {
    for (auto& v : vertices) {
        v.normal = glm::vec3(0.0f);
    }
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const std::uint32_t a = indices[i];
        const std::uint32_t b = indices[i + 1];
        const std::uint32_t c = indices[i + 2];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
            continue;
        }
        const glm::vec3 n = glm::cross(vertices[b].position - vertices[a].position,
                                       vertices[c].position - vertices[a].position); // area-weighted
        vertices[a].normal += n;
        vertices[b].normal += n;
        vertices[c].normal += n;
    }
    for (auto& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

MeshId Scene::addMesh(MeshData mesh) {
    const auto id = static_cast<MeshId>(meshes.size());
    meshes.push_back(std::move(mesh));
    ++meshVersion;
    return id;
}

TextureId Scene::addTexture(TextureData texture) {
    const auto id = static_cast<TextureId>(textures.size());
    textures.push_back(std::move(texture));
    ++textureVersion;
    return id;
}

Entity& Scene::addEntity(std::string name, MeshId mesh) {
    Entity entity;
    entity.name = std::move(name);
    entity.mesh = mesh;
    entities.push_back(std::move(entity));
    return entities.back();
}

PunctualLight& Scene::addLight(PunctualLight light) {
    lights.push_back(std::move(light));
    return lights.back();
}

const std::pair<glm::vec3, glm::vec3>& Scene::meshBounds(MeshId mesh) const {
    static const std::pair<glm::vec3, glm::vec3> empty{glm::vec3(0.0f), glm::vec3(0.0f)};
    if (mesh >= meshes.size()) {
        return empty;
    }
    if (meshBoundsVersion_ != meshVersion || meshBoundsCache_.size() != meshes.size()) {
        meshBoundsCache_.resize(meshes.size());
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            meshBoundsCache_[i] = meshes[i].bounds();
        }
        meshBoundsVersion_ = meshVersion;
    }
    return meshBoundsCache_[mesh];
}

std::pair<glm::vec3, glm::vec3> Scene::bounds() const {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const auto& e : entities) {
        if (!e.visible || e.style != MeshStyle::Lit || e.mesh >= meshes.size() || !meshes[e.mesh].valid()) {
            continue;
        }
        const auto& [mlo, mhi] = meshBounds(e.mesh);
        const glm::mat4 m = e.transform.matrix();
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? mhi.x : mlo.x, (corner & 2) ? mhi.y : mlo.y, (corner & 4) ? mhi.z : mlo.z);
            const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
            any = true;
        }
    }
    for (const auto& pg : procedurals) {
        if (!pg.visible || pg.instances.empty()) {
            continue;
        }
        lo = glm::min(lo, pg.boundsMin);
        hi = glm::max(hi, pg.boundsMax);
        any = true;
    }
    if (!any) {
        return {glm::vec3(0.0f), glm::vec3(0.0f)};
    }
    return {lo, hi};
}

void Scene::clear() {
    cameras.clear();
    lights.clear();
    meshes.clear();
    textures.clear();
    entities.clear();
    particles.clear();
    procedurals.clear();
    ++meshVersion;
    ++textureVersion;
}

} // namespace avgen::scene
