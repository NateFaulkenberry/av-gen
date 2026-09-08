#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <utility>

namespace avgen::scene {

glm::mat4 Transform::matrix() const {
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

glm::mat4 Camera::view() const {
    return glm::lookAtRH(position, target, up);
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspectiveRH_ZO(fovYRadians, aspect, nearPlane, farPlane);
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

MeshId Scene::addMesh(MeshData mesh) {
    const auto id = static_cast<MeshId>(meshes.size());
    meshes.push_back(std::move(mesh));
    ++meshVersion;
    return id;
}

Entity& Scene::addEntity(std::string name, MeshId mesh) {
    Entity entity;
    entity.name = std::move(name);
    entity.mesh = mesh;
    entities.push_back(std::move(entity));
    return entities.back();
}

} // namespace avgen::scene
