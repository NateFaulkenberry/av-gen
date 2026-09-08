#pragma once

// Scene data model for milestone 0.1: plain structs, no ECS (architecture-options §9). Nothing
// outside scene/ iterates entities directly; the renderer receives a const Scene&.
// Conventions: right-handed, +Y up, metres, glTF-compatible (ADR-005).

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    [[nodiscard]] glm::mat4 matrix() const;
};

struct Camera {
    glm::vec3 position{0.0f, 1.6f, 6.0f};
    glm::vec3 target{0.0f, 0.6f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.87f; // ~50 degrees
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const; // depth 0..1 (WebGPU/Metal)
};

struct DirectionalLight {
    glm::vec3 direction{-0.4f, -1.0f, -0.35f}; // towards the scene
    glm::vec3 color{1.0f, 0.95f, 0.9f};
    float intensity = 3.0f;
};

struct Material {
    glm::vec3 baseColor{0.75f, 0.2f, 0.9f};
    glm::vec3 emissiveColor{0.9f, 0.45f, 1.0f};
    float emissiveIntensity = 0.0f;
    float roughness = 0.35f;
    float metallic = 0.0f;
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    [[nodiscard]] bool valid() const;
};

using MeshId = std::uint32_t;
constexpr MeshId kInvalidMesh = 0xFFFFFFFFu;

enum class MeshStyle : std::uint8_t { Lit, Grid };

struct Entity {
    std::string name;
    Transform transform;
    MeshId mesh = kInvalidMesh;
    Material material;
    MeshStyle style = MeshStyle::Lit;
    bool visible = true;
};

struct Environment {
    glm::vec3 backgroundColor{0.012f, 0.012f, 0.02f};
    float brightness = 1.0f;     // global exposure multiplier applied in tone mapping
    float gridIntensity = 0.6f;
};

struct Scene {
    Camera camera;
    DirectionalLight light;
    Environment environment;
    std::vector<MeshData> meshes;
    std::vector<Entity> entities;
    std::uint64_t meshVersion = 0; // incremented when meshes change (renderer re-uploads)

    MeshId addMesh(MeshData mesh);
    Entity& addEntity(std::string name, MeshId mesh);
};

} // namespace avgen::scene
