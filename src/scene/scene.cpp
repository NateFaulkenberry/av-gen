#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace avgen::scene {

glm::mat4 Transform::matrix() const {
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    const glm::mat4 r = glm::mat4_cast(rotation);
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
    return t * r * s;
}

glm::mat4 Camera::view() const { return glm::lookAtRH(position, target, up); }

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspectiveRH_ZO(fovYRadians, aspect, nearPlane, farPlane);
}

bool MeshData::valid() const {
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0) {
        return false;
    }
    const auto count = static_cast<std::uint32_t>(vertices.size());
    for (const auto index : indices) {
        if (index >= count) {
            return false;
        }
    }
    return true;
}

MeshId Scene::addMesh(MeshData mesh) {
    meshes.push_back(std::move(mesh));
    ++meshVersion;
    return static_cast<MeshId>(meshes.size() - 1);
}

Entity& Scene::addEntity(std::string name, MeshId mesh) {
    Entity entity;
    entity.name = std::move(name);
    entity.mesh = mesh;
    entities.push_back(std::move(entity));
    return entities.back();
}

} // namespace avgen::scene
