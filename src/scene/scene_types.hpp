#pragma once

// Scene value types (transforms, cameras, textures, materials, meshes, entities, lights,
// environment) shared by scene.hpp and the procedural geometry header (ADR-023).

// Scene data model (milestone 0.2): plain structs, no ECS (architecture-options §9). Nothing
// outside scene/ iterates entities directly; the renderer receives a const Scene&.
// Conventions: right-handed, +Y up, metres, glTF 2.0 semantics (ADR-005): metallic-roughness
// materials, punctual lights that shine down -Z, cameras that look down -Z.


#include "core/wind.hpp"

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

// ---- picking identity (ADR-030's identifier target) ---------------------------------------------
//
// The identifier target is one R32Uint: the low 16 bits an object id, the high 16 a material id.
// Three renderers write into that one object id and each of them numbers from zero -- the scene
// renderer by entity, the procedural renderer by procedural, the SDF renderer by SDF object. So the
// number alone cannot say what it counts, and a click on a scattered tree used to be resolved as
// though it were an entity index: it selected whichever node happened to own that entity, or
// nothing. In a scatter world, where almost everything is procedural, almost every click was wrong.
//
// So the id carries a two-bit tag saying which numbering it belongs to. Two bits because there are
// three writers, not two -- a one-bit tag would have left the SDFs still colliding with somebody.
//
// That leaves 14 bits, so 16,383 of each. For scale: Glowmere flattens to 278 entities and 30
// procedurals, and its 150,000 instances are instances *of* procedurals, not procedurals. `packPickId`
// saturates rather than wrapping, because an id that wraps selects a real and entirely unrelated
// object, which is worse than one that selects nothing.
enum class PickSpace : std::uint32_t {
    Entity = 0,     // scene_.entities, resolved by Composition::nodeForEntity
    Procedural = 1, // scene_.procedurals, resolved by Composition::nodeForProcedural
    Sdf = 2,        // scene_.sdfs; no node resolver yet, so a click on one selects nothing
};

inline constexpr std::uint32_t kPickIndexBits = 14;
inline constexpr std::uint32_t kPickIndexMask = (1u << kPickIndexBits) - 1u;
inline constexpr std::uint32_t kPickMaxIndex = kPickIndexMask;

[[nodiscard]] inline constexpr std::uint32_t packPickId(PickSpace space, std::size_t index) {
    const auto clamped = static_cast<std::uint32_t>(index < kPickMaxIndex ? index : kPickMaxIndex);
    return (static_cast<std::uint32_t>(space) << kPickIndexBits) | clamped;
}
[[nodiscard]] inline constexpr PickSpace pickSpaceOf(std::uint32_t id) {
    return static_cast<PickSpace>((id >> kPickIndexBits) & 0x3u);
}
[[nodiscard]] inline constexpr std::uint32_t pickIndexOf(std::uint32_t id) {
    return id & kPickIndexMask;
}

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

// One vertex's skin influences (ADR-086): four joint indices into a Skeleton's palette and the
// weights they carry. Kept in a second array beside the vertices rather than inside Vertex so a
// static mesh costs nothing for skinning it will never do -- a terrain chunk's vertex stays 32
// bytes, and the skinned pipeline reads this as a second vertex buffer.
struct SkinInfluence {
    std::uint16_t joints[4] = {0, 0, 0, 0};
    glm::vec4 weights{0.0f}; // normalised on import; sums to 1
};

struct MeshData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Empty for a static mesh; otherwise exactly as long as `vertices`.
    std::vector<SkinInfluence> skin;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool skinned() const { return !skin.empty() && skin.size() == vertices.size(); }
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> bounds() const; // min, max (zeros when empty)
    void computeNormals(); // smooth normals from triangle geometry (used when a file has none)
};

using MeshId = std::uint32_t;
constexpr MeshId kInvalidMesh = 0xFFFFFFFFu;

// A skinned rig in the scene (ADR-086; scene/animation.hpp holds the type). Lives here because
// Entity names one and Entity is defined in this header.
using RigId = std::uint32_t;
constexpr RigId kInvalidRig = 0xFFFFFFFFu;

// How an entity is drawn. `Water` (ADR-099) is a surface the scene pass draws through its own
// pipeline: it needs the scene's depth to know how thick it is, the environment to reflect, and a
// normal it computes itself, none of which the shared metallic-roughness path can give it. It is a
// style rather than a material flag because it decides which pipeline draws the entity, which is
// exactly what the other two values of this enum decide.
enum class MeshStyle : std::uint8_t { Lit, Grid, Water };

struct Entity {
    std::string name;
    Transform transform; // world space (hierarchies are flattened on import in 0.2)
    MeshId mesh = kInvalidMesh;
    // The rig that poses this entity's mesh (ADR-086), or kInvalidRig for a static one. When set,
    // `transform` places the character in the world and the joint matrices do the rest: a skinned
    // glTF node's own transform is ignored on import, as the specification requires.
    RigId rig = kInvalidRig;
    Material material;
    // The name the source asset gave this entity's material, when it gave one. Carried on the
    // entity rather than on the Material because Material is compared by value to decide what can
    // share a draw, and a name is a label rather than a property of the surface: two materials
    // that shade identically must keep merging into one part however they were named. What the
    // name buys is *addressing* -- it is how a scene file can say "parts/Blue/emissiveGain"
    // instead of an index nobody can predict.
    std::string materialName;
    MeshStyle style = MeshStyle::Lit;
    bool visible = true;
    // Whether the shadow passes draw this entity. Separate from `visible` because the camera's
    // visibility is the wrong question for a shadow map: a chunk of ground half a kilometre away is
    // on screen and casts nothing anyone can see, and one behind the camera is off screen and may
    // cast across the whole frame. Terrain sets this per chunk from its own shadow distance.
    bool castsShadow = true;
    // Set per frame by whatever culls against the *camera* frustum (terrain does, per chunk).
    // "Off screen" and "not in the scene" are different claims, and only the second is a reason to
    // stop casting: a hill behind the camera throws its shadow across the whole frame. An entity
    // that is `visible` and `cameraCulled` is skipped by the camera passes and still offered to the
    // shadow passes, which test it against each cascade's own frustum. Runtime only: never
    // serialised, and cleared every frame by whatever set it.
    bool cameraCulled = false;
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
    bool stylized = false;
    glm::vec3 backgroundColor{0.012f, 0.012f, 0.02f};
    float brightness = 1.0f;     // global exposure multiplier applied in tone mapping
    float gridIntensity = 0.6f;
    TextureId environmentMap = kInvalidTexture; // equirectangular Rgba32Float, 2:1
    float environmentIntensity = 1.0f; // multiplies the IBL that lights surfaces
    float environmentRotation = 0.0f;  // radians about +Y; rotates the visible sky and its lighting
                                       // together, so the moon and the moonlight cannot separate
    bool showSkybox = true;
    float skyboxBlur = 0.0f; // 0 = sharp, 1 = fully prefiltered
    // ADR-049. What the sky looks like and how much light it casts are two looks, not one: a night
    // valley wants a sky dark enough to read as night and an IBL bright enough to keep shadowed
    // rock off black. `skyIntensity` scales only the background pass; `environmentIntensity` only
    // the shading. One number cannot be both, and before this it was one number.
    float skyIntensity = 1.0f;
    // How much of the sky reaches the selective-bloom mask (ADR-039's emission target). 0 is the
    // pre-ADR-049 behaviour -- a bright environment never glows -- and is still the default,
    // because on a sky with a real sun in it anything else blows out the frame. A moon wants
    // about 0.3.
    float skyBloom = 0.0f;
    // ADR-049: aim the key light away from the environment map's brightest direction. The moon you
    // can see and the moonlight falling on the terrain then come from the same pixel by
    // construction, instead of by an author keeping two numbers in step by hand.
    bool lightFromEnvironment = false;
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
    // ADR-058: how much of the volumetric's mist layer the *surface* fog sees. At 0 the distance
    // above is uniform, which is what it has always been; at 1 the view ray is integrated through
    // the same flat-topped layer the volumetric marches (uniform up to `fogHeight`, thinning by
    // `fogHeightFalloff` per metre above it), so a ridge or a canopy crown standing clear of the
    // mist is seen through less of it than the valley floor behind it. The two describe the same
    // air and share its geometry deliberately: a scene whose fog bank has one height in the march
    // and another on the surfaces inside it does not read as one atmosphere.
    float fogHeightAmount = 0.0f; // 0 = uniform distance fog (the default, bit-identical)
    // ADR-058: the styled path's hemisphere ambient, and the least of it a fully occluded surface
    // keeps. These were shader constants until a measured need moved them: across the whole of
    // Glowmere Valley the ambient was the brightest term in the image, no pixel in any frame fell
    // into the shadow band, and 98% of every frame sat in the midtones -- so the scene's own
    // glowing flora had nothing darker to glow against, and distance fog lighter than the ground
    // could not separate a far ridge from a near one. The defaults below are exactly the constants
    // the shader used to carry, so a styled scene that names none of them renders as it did.
    glm::vec3 styledSkyAmbient{0.38f, 0.56f, 0.65f};   // reaches an up-facing normal
    glm::vec3 styledGroundAmbient{0.12f, 0.10f, 0.22f}; // reaches a down-facing one (bounce)
    float styledAmbientFloor = 0.68f;                   // ambient left where occlusion is total
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
    // ADR-053: how strongly the clustered local lights light the air. 0 skips them entirely --
    // the march is half-resolution but still steps every pixel, so sampling a froxel's lights
    // costs about as much again as the same lights cost on the surfaces they lit, and a scene
    // with thin air gets nothing for it. Scenes with real fog around glowing things turn it up.
    float volumeLocalLights = 0.0f;
    float volumeNoiseAmount = 0.0f;
    float volumeNoiseScale = 0.1f;
    float volumeNoiseSpeed = 0.1f;
    float volumeEmission = 0.0f;
    int volumeSteps = 32;                  // raymarch samples per pixel
    float volumeMaxDistance = 200.0f;
    std::string volumeDensityField;        // scalar field name ("" = none)
    std::string volumeColorField;          // colour field name ("" = fogColor)
    // ADR-055: what the air is doing over this world. It sits on the environment because it is a
    // property of the weather rather than of any one object, and because every consumer (vegetation
    // now, particles and cloth later) has to agree about it or the world stops being one place.
    wind::WindParams wind;
};

} // namespace avgen::scene
