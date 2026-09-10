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
#include <optional>
#include <string>
#include <string_view>
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

// Exposure (ADR-037): manual photographic settings, or metering the previous frame's luminance.
// The maths lives in scene/camera.hpp; the chain applies the resulting scale before bloom
// (ADR-039). Scene-linear values in this engine are not calibrated in nits, so the applied scale
// is the photographic one *relative to* `referenceEv100`: at the default aperture, shutter and ISO
// the scale is exactly 1 and every scene authored before ADR-037 renders unchanged, while one
// stop of aperture, shutter or ISO is still one stop.
struct ExposureSettings {
    enum class Mode : std::uint8_t { Manual, Automatic };
    Mode mode = Mode::Manual;
    float aperture = 5.6f;        // f-number
    float shutterSeconds = 1.0f / 50.0f;
    float iso = 400.0f;
    float compensation = 0.0f;    // EV offset applied in both modes (positive = brighter)
    float minEv = -4.0f;          // automatic clamp
    float maxEv = 16.0f;
    float speedUp = 3.0f;         // EV per second when the image brightens
    float speedDown = 1.0f;       // EV per second when it darkens
    float meterCenterWeight = 0.6f; // 0 = flat average, 1 = strongly centre-weighted
    // EV100 at which the applied scale is 1: EV100(f/5.6, 1/50 s, ISO 400) = 8.61471.
    float referenceEv100 = 8.614710f;
};

// A physical lens (ADR-037). Field of view comes from `focalLength` and `sensorHeight` unless
// `useExplicitFov` is set, so existing scenes that only set `fovYRadians` keep working.
struct LensSettings {
    float focalLength = 35.0f;    // mm
    float sensorWidth = 36.0f;    // mm (full frame)
    float sensorHeight = 24.0f;
    float aperture = 5.6f;        // f-number, drives the circle of confusion
    float focusDistance = 8.0f;   // metres
    float shutterAngle = 180.0f;  // degrees; motion blur length
    bool useExplicitFov = true;   // scenes authored before the lens existed
    [[nodiscard]] float fovYRadians() const;      // 2 atan(sensorHeight / (2 focalLength))
    // Diameter of the blur circle a point at `distance` metres projects onto the sensor, in
    // millimetres: c = f^2 |d - s| / (N d (s - f)) with d and s in the same units (Potmesil and
    // Chakravarty 1981; PBRT 3rd ed. 6.2.3). Zero at the focus distance, bounded behind it.
    [[nodiscard]] float circleOfConfusion(float distance) const;
    // The same circle expressed in pixels of an image `imageHeight` pixels tall.
    [[nodiscard]] float circleOfConfusionPixels(float distance, float imageHeight) const;
    // Horizontal field of view, from `sensorWidth`; the vertical one is what the projection uses.
    [[nodiscard]] float fovXRadians() const;
};

struct Camera {
    std::string name;
    glm::vec3 position{0.0f, 1.6f, 6.0f};
    glm::vec3 target{0.0f, 0.6f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYRadians = 0.87f; // ~50 degrees; used when lens.useExplicitFov
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    LensSettings lens;
    ExposureSettings exposure;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const; // depth 0..1 (WebGPU/Metal)
    [[nodiscard]] float effectiveFovY() const; // lens or explicit, per lens.useExplicitFov
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
    std::string program;                 // procedural material program name (ADR-030; empty = none)
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
    // Whether the shadow passes draw this entity. Separate from `visible` because the camera's
    // visibility is the wrong question for a shadow map: a chunk of ground half a kilometre away is
    // on screen and casts nothing anyone can see, and one behind the camera is off screen and may
    // cast across the whole frame. Terrain sets this per chunk from its own shadow distance.
    bool castsShadow = true;
};

// ---- lights ---------------------------------------------------------------------------------

// A light (ADR-033). Punctual kinds keep their original meaning; the area kinds add a size that
// softens both the diffuse falloff and the specular highlight (linearly transformed cones for
// Rect and Disk, representative point for Tube and Sphere). `temperature` and `tint` multiply
// `color` when the light is packed, so an artist works in Kelvin and the shader stays unchanged.
struct PunctualLight {
    enum class Type : std::uint8_t { Directional, Point, Spot, Rect, Disk, Tube, Sphere };
    // The part a light plays in a rig; used by rigs, presets and the inspector, never by shading.
    enum class Role : std::uint8_t { Key, Fill, Rim, Back, Ambient, Practical };
    std::string name;
    Type type = Type::Directional;
    Role role = Role::Key;
    glm::vec3 position{0.0f};
    glm::vec3 direction{-0.4f, -1.0f, -0.35f}; // direction the light travels (normalised by users)
    glm::vec3 up{0.0f, 1.0f, 0.0f};            // orientation of Rect/Disk/Tube emitters
    glm::vec3 color{1.0f};
    float intensity = 1.0f;       // directional: lux; point/spot: candela; area: nits over the emitter
    float temperature = 6500.0f;  // Kelvin; 6500 is neutral and leaves `color` unchanged
    float tint = 0.0f;            // -1 green .. +1 magenta, perpendicular to the Planckian locus
    float range = 0.0f;           // 0 = infinite
    float innerConeAngle = 0.0f;  // spot, radians
    float outerConeAngle = 0.7854f;
    // Area emitters: Rect uses width and height, Disk and Sphere use radius, Tube uses both.
    float width = 1.0f;
    float height = 1.0f;
    float radius = 0.25f;
    // What this light participates in beyond direct shading.
    bool castsShadow = false;      // a shadow map is allocated for it (ADR-034)
    bool contactShadow = true;     // screen-space contact march when it casts
    float shadowStrength = 1.0f;   // 0 = no shadowing, 1 = full
    float shadowBias = 0.0015f;    // normal-offset scale in world units
    float softness = 1.0f;         // multiplies the penumbra width of a soft-shadowed light
    // In-scattering into the volumetric atmosphere (ADR-032 volume pass), read by the march
    // through GpuLight/LightUniform. 1 = this light lights the air as much as it lights
    // surfaces, 0 = it does not touch the fog. The default is 1 so a hand-authored light
    // behaves the obvious way; light rigs set it per light and default their own to 0, so a rig
    // opts each source into the haze deliberately.
    float volumetricStrength = 1.0f;
    bool diffuseOnly = false;
    bool specularOnly = false;
    bool enabled = true;
};
[[nodiscard]] const char* lightTypeName(PunctualLight::Type type);
[[nodiscard]] std::optional<PunctualLight::Type> lightTypeFromName(std::string_view name);
[[nodiscard]] const char* lightRoleName(PunctualLight::Role role);
[[nodiscard]] std::optional<PunctualLight::Role> lightRoleFromName(std::string_view name);
// Linear sRGB of a black body at `kelvin` (1500..12000), normalised to luminance 1, with `tint`
// applied perpendicular to the locus. 6500 K with tint 0 returns white.
[[nodiscard]] glm::vec3 colorTemperatureToRgb(float kelvin, float tint = 0.0f);

// ---- environment ------------------------------------------------------------------------------

// The sky's authored parameters. `enabled` defaults on; the renderer only reaches for the sky when
// the scene has no environment map, so an existing scene gains reflections and keeps its look.
struct SkySettings {
    bool enabled = true;
    glm::vec3 zenithColor{0.055f, 0.105f, 0.235f};
    glm::vec3 horizonColor{0.300f, 0.340f, 0.420f};
    glm::vec3 groundColor{0.045f, 0.042f, 0.038f};
    float hazeWidth = 0.25f;         // turbidity-like: how far up the horizon colour reaches (0..1)
    glm::vec3 sunColor{1.0f, 0.93f, 0.82f};
    float sunIntensity = 8.0f;       // radiance of the disc, relative to the sky gradient
    float sunAngularRadius = 0.045f; // radians (~2.6 degrees; wider than the real sun so a 128 px
                                     // prefiltered cube resolves it)
    float sunGlowWidth = 0.18f;      // radians; the exponential halo around the disc
    float intensity = 1.0f;          // multiplies the whole sky, and is the IBL intensity in shading
    bool showBackground = false;     // draw the sky behind the scene instead of the background colour
    bool useKeyLight = true;         // take the sun direction from the scene's key light
    glm::vec3 sunDirection{0.35f, 0.75f, 0.55f}; // fallback direction *towards* the sun
};

struct Environment {
    glm::vec3 backgroundColor{0.012f, 0.012f, 0.02f};
    float brightness = 1.0f;     // global exposure multiplier applied in tone mapping
    float gridIntensity = 0.6f;
    TextureId environmentMap = kInvalidTexture; // equirectangular Rgba32Float
    float environmentIntensity = 1.0f;
    float environmentRotation = 0.0f; // radians about +Y
    bool showSkybox = true;
    float skyboxBlur = 0.0f; // 0 = sharp, 1 = fully prefiltered
    // Shadow cascades, overriding the quality tier when non-zero. It belongs to the scene because
    // it is a property of the world's scale rather than of the machine: on a 640 m landscape lit by
    // a low moon, dropping the third cascade cost 0.67% of pixels a difference of more than 6/255
    // and saved about 11 ms of an 85 ms frame at 2880x1800 -- by a distance the largest single
    // saving found in this renderer. On an object-scale scene the third cascade earns more.
    std::uint32_t shadowCascades = 0; // 0 = whatever the tier says
    // Procedural sky (ADR-036): used as the image-based lighting source whenever `environmentMap`
    // is unset, so metals and rough surfaces always have something to reflect. See scene/sky.hpp.
    SkySettings sky;
    // Distance fog (exponential-squared by view distance) applied to lit/unlit surfaces after
    // shading; the skybox is untouched. Default colour = the default background colour.
    glm::vec3 fogColor{0.012f, 0.012f, 0.02f};
    float fogDensity = 0.0f; // 0 = off; factor = exp(-(distance * density)^2)
    // Volumetric atmosphere (ADR-032): raymarched after the lit pass (rendering::VolumeRenderer).
    // Density = volumeDensity * heightFalloff(y) * (1 + noise) * densityField(p) where the height
    // term is exp(-max(0, y - fogHeight) * fogHeightFalloff), the noise term is
    // volumeNoiseAmount * (fbm3(p * volumeNoiseScale + t * volumeNoiseSpeed) * 2 - 1) and the
    // field term is the named scalar field's sample (1 when unset). Lit by the key light with a
    // Henyey–Greenstein phase (volumeAnisotropy) and self-emitting with volumeEmission × the
    // named colour field (or fogColor).
    float volumeDensity = 0.0f;            // 0 = off
    float fogHeight = 0.0f;                // height above which density falls off
    float fogHeightFalloff = 0.0f;         // 0 = uniform
    float volumeScattering = 1.0f;         // in-scatter strength
    float volumeAbsorption = 0.5f;         // extinction multiplier
    float volumeAnisotropy = 0.3f;         // HG g in (-1, 1)
    float volumeNoiseAmount = 0.0f;
    float volumeNoiseScale = 0.1f;
    float volumeNoiseSpeed = 0.1f;
    float volumeEmission = 0.0f;
    int volumeSteps = 32;                  // raymarch samples per pixel
    float volumeMaxDistance = 200.0f;
    std::string volumeDensityField;        // scalar field name ("" = none)
    std::string volumeColorField;          // colour field name ("" = fogColor)
};

} // namespace avgen::scene
