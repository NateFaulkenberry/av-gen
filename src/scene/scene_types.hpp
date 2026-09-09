#pragma once

// Scene value types (transforms, cameras, textures, materials, meshes, entities, lights,
// environment) shared by scene.hpp and the procedural geometry header (ADR-023).

// Scene data model (milestone 0.2): plain structs, no ECS (architecture-options §9). Nothing
// outside scene/ iterates entities directly; the renderer receives a const Scene&.
// Conventions: right-handed, +Y up, metres, glTF 2.0 semantics (ADR-005): metallic-roughness
// materials, punctual lights that shine down -Z, cameras that look down -Z.


#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avgen::scene {

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    [[nodiscard]] glm::mat4 matrix() const;
    // Decomposes an affine matrix (translation * rotation * scale). Shear is discarded.
    static Transform fromMatrix(const glm::mat4& m);
};

struct Camera {
    std::string name;
    glm::vec3 position{0.0f, 1.6f, 6.0f};
    glm::vec3 target{0.0f, 0.6f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.87f; // ~50 degrees
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const; // depth 0..1 (WebGPU/Metal)
};

// ---- textures ------------------------------------------------------------------------------

enum class TextureFormat : std::uint8_t { Rgba8Unorm, Rgba8Srgb, Rgba32Float };

// CPU-side image. Rgba8*: 4 bytes per pixel; Rgba32Float: 16 bytes per pixel. Row-major, top-left.
struct TextureData {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8Unorm;
    std::vector<std::uint8_t> data;
    [[nodiscard]] std::size_t bytesPerPixel() const { return format == TextureFormat::Rgba32Float ? 16 : 4; }
    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool isHdr() const { return format == TextureFormat::Rgba32Float; }
};

using TextureId = std::uint32_t;
constexpr TextureId kInvalidTexture = 0xFFFFFFFFu;

enum class WrapMode : std::uint8_t { Repeat, Clamp, Mirror };

struct TextureRef {
    TextureId texture = kInvalidTexture;
    std::uint32_t uvSet = 0; // only set 0 is used in 0.2
    WrapMode wrapU = WrapMode::Repeat;
    WrapMode wrapV = WrapMode::Repeat;
    bool linearFilter = true;
    [[nodiscard]] bool valid() const { return texture != kInvalidTexture; }
};

// ---- materials -----------------------------------------------------------------------------

enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };

// glTF metallic-roughness material. Textures multiply the factors.
struct Material {
    glm::vec3 baseColor{0.75f, 0.2f, 0.9f};
    float opacity = 1.0f;
    glm::vec3 emissiveColor{0.9f, 0.45f, 1.0f};
    float emissiveIntensity = 0.0f; // emissive factor * KHR_materials_emissive_strength
    float roughness = 0.35f;
    float metallic = 0.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool unlit = false;
    TextureRef baseColorTexture;         // sRGB, rgb * baseColor, a * opacity
    TextureRef metallicRoughnessTexture; // linear, g = roughness, b = metallic
    TextureRef normalTexture;            // linear, tangent space
    TextureRef emissiveTexture;          // sRGB
    TextureRef occlusionTexture;         // linear, r
};

// ---- geometry ------------------------------------------------------------------------------

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> bounds() const; // min, max (zeros when empty)
    void computeNormals(); // smooth normals from triangle geometry (used when a file has none)
};

using MeshId = std::uint32_t;
constexpr MeshId kInvalidMesh = 0xFFFFFFFFu;

enum class MeshStyle : std::uint8_t { Lit, Grid };

struct Entity {
    std::string name;
    Transform transform; // world space (hierarchies are flattened on import in 0.2)
    MeshId mesh = kInvalidMesh;
    Material material;
    MeshStyle style = MeshStyle::Lit;
    bool visible = true;
};

// ---- lights ---------------------------------------------------------------------------------

struct PunctualLight {
    enum class Type : std::uint8_t { Directional, Point, Spot };
    std::string name;
    Type type = Type::Directional;
    glm::vec3 position{0.0f};
    glm::vec3 direction{-0.4f, -1.0f, -0.35f}; // direction the light travels (normalised by users)
    glm::vec3 color{1.0f};
    float intensity = 1.0f;       // directional: lux (scene-linear multiplier); point/spot: candela
    float range = 0.0f;           // 0 = infinite
    float innerConeAngle = 0.0f;  // spot, radians
    float outerConeAngle = 0.7854f;
    bool enabled = true;
};

// ---- environment ------------------------------------------------------------------------------

struct Environment {
    glm::vec3 backgroundColor{0.012f, 0.012f, 0.02f};
    float brightness = 1.0f;     // global exposure multiplier applied in tone mapping
    float gridIntensity = 0.6f;
    TextureId environmentMap = kInvalidTexture; // equirectangular Rgba32Float
    float environmentIntensity = 1.0f;
    float environmentRotation = 0.0f; // radians about +Y
    bool showSkybox = true;
    float skyboxBlur = 0.0f; // 0 = sharp, 1 = fully prefiltered
};

} // namespace avgen::scene
