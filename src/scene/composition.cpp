#include "scene/composition.hpp"

#include "params/timeline.hpp"

#include "core/json_keys.hpp"
#include "core/log.hpp"
#include "core/phase_profiler.hpp"
#include "assets/asset_library.hpp"
#include "assets/mesh_lod.hpp"
#include "entity/gait.hpp"
#include "entity/obstacles.hpp"
#include "scene/camera.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/asset_parts.hpp"
#include "scene/mesh_metrics.hpp"
#include "scene/particle_io.hpp"
#include "scene/scatter_anchors.hpp"
#include "scene/sky.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_stack.hpp"
#include "spatial/detail.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <numbers>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace avgen::scene {

// Every key `Composition::fromJsonImpl` reads, in the three objects a scene file is mostly made of.
// Kept beside the parser rather than derived from it because there is no way to derive it: the
// parser reads a key wherever it happens to need it, and a list that is generated from the code is
// a list that agrees with the code about a typo. Adding a key to the parser and not to this list
// costs one spurious warning; the reverse costs what ADR-278 is about.
constexpr std::string_view kSceneKeys[] = {
    "format",     "version",        "name",           "camera",        "cameraDirection",
    "lightRig",   "lights",         "navBodyRadius",  "navCellSize",   "navWadeDepth",
    "wind",       "post",           "environment",    "composition",   "heroes",
    "effects", "entityProfiles", "entities", "fields",
    "staging",    "graph",          "grids",          "materialPrograms", "nodes",
    "worldEvents"};
constexpr std::string_view kEnvironmentKeys[] = {
    "map", "lightRig", "intensity", "fogDensity", "stylized", "rotation", "skyIntensity",
    "dayNight",
    "skyBloom", "ecologyLight", "ecologyLightRange", "ecologyGlowCell", "skybox",
    "proceduralSkyBackground",
    "lightFromEnvironment", "fogColor", "background", "fogHeightAmount", "styledSkyAmbient",
    "styledGroundAmbient", "styledAmbientFloor", "volumeDensity", "fogHeight", "fogHeightFalloff",
    "fogUpperDensity", "fogHeightCurve",
    "volumeScattering", "volumeAbsorption", "volumeAnisotropy", "volumeLocalLights", "volumeNoise",
    "volumeNoiseScale", "volumeNoiseSpeed", "volumeEmission", "volumeMaxDistance",
    "shadowCascades", "shadowRange", "volumeSteps", "volumeJitter", "volumeDensityField",
    "volumeShadowSteps", "volumeShadowStrength",
    "volumeColorField", "sky",
    "vortex"};
constexpr std::string_view kSkyKeys[] = {
    "enabled", "background", "useKeyLight", "zenithColor", "horizonColor", "groundColor",
    "sunColor", "sunDirection", "haze", "sunIntensity", "sunSize", "sunGlow", "intensity"};

// Every key a `"lights"` entry may carry. Named after the `PunctualLight` field it sets, except
// where `LightRig`'s file format already had a name for the same quantity -- `color`, `temperature`,
// `tint`, `intensity`, `castsShadow`, `contactShadow`, `shadowStrength`, `softness`, `volumetric` --
// which are spelled the rig's way. Two spellings for one quantity across two lighting formats is
// how `coneDegrees` happened, one format over.
constexpr std::string_view kAuthoredLightKeys[] = {
    "name",        "type",          "role",           "node",       "position",   "direction",
    "up",          "color",         "intensity",      "temperature", "tint",      "range",
    "innerCone",   "outerCone",     "width",          "height",     "radius",     "castsShadow",
    "contactShadow", "shadowStrength", "shadowBias",  "softness",   "volumetric", "diffuseOnly",
    "specularOnly", "enabled", "id"};

std::span<const std::string_view> sceneFileKeys() { return kSceneKeys; }
std::span<const std::string_view> sceneEnvironmentKeys() { return kEnvironmentKeys; }
std::span<const std::string_view> sceneSkyKeys() { return kSkyKeys; }
std::span<const std::string_view> sceneLightKeys() { return kAuthoredLightKeys; }

namespace {
// Ecology lights are rebuilt every frame and identified by name, because the rig removes its own
// lights by resizing from the back and the two sets must not be able to eat each other.
constexpr std::string_view kEcologyLightPrefix = "ecology.glow.";
// The clustered path takes 256 lights in total (kMaxSceneLights); leave room for the rig. ADR-703:
// and for LIGHTMOD's effect pool (a Glow's spill light), whose 16 slots are reserved here rather
// than fought over -- ecology takes everything that is left, so a second allocator asking for "the
// rest" would win or lose by where the camera stood. The pool is appended by the renderer after
// these, so authored + ecology + effect lights still total 224 at most.
constexpr std::size_t kMaxEcologyLights = 224 - world::kEffectLightBudget;
constexpr int kMaxTerrainLodIndex = world::kMaxTerrainLods - 1;
} // namespace

// A 64-bit digest of the scene's texture table: name, dimensions, format and every pixel.
//
// What it is for. `Scene::textureVersion` is the renderer's "re-upload everything" signal, and a
// flatten used to bump it unconditionally -- so every structural edit re-uploaded the whole table
// whether or not a single texel had changed. Measured on Glowmere: 528 uploads over 12 flattens for
// starring a hero and 704 over 16 for a brush edit, which is 44 a flatten in both, because the
// count is a property of *any* flatten and not of what the edit was. Neither of those edits touches
// a texture.
//
// Word-wise rather than the byte-at-a-time FNV-1a the rest of this file hashes structure with. The
// structure hashes run over a few hundred floats; this runs over megabytes of pixels, and a digest
// that costs more than the upload it is avoiding is not a saving. The metadata still goes in, so
// two different images that happen to share a pixel buffer length are not confused for each other.
//
// A 64-bit digest can in principle collide, and a collision here is a stale texture on the GPU. The
// alternative -- keeping the previous table alive and comparing it byte for byte -- doubles the
// resident texture memory of every flatten to remove a probability of about 2^-64 per edit, which
// is the wrong trade. tests/unit/test_composition.cpp changes one texel and requires the version to
// move, which is the arm that would fail if this were hashing nothing (ADR-182).
std::uint64_t textureTableDigest(const std::vector<TextureData>& textures) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 0x9e3779b97f4a7c15ULL;
        h = (h << 31) | (h >> 33);
    };
    mix(textures.size());
    for (const TextureData& texture : textures) {
        mix(texture.width);
        mix(texture.height);
        mix(static_cast<std::uint64_t>(texture.format));
        mix(texture.data.size());
        for (const char c : texture.name) {
            mix(static_cast<std::uint8_t>(c));
        }
        const std::size_t words = texture.data.size() / 8;
        const std::uint8_t* bytes = texture.data.data();
        for (std::size_t i = 0; i < words; ++i) {
            std::uint64_t word = 0;
            std::memcpy(&word, bytes + i * 8, 8);
            mix(word);
        }
        for (std::size_t i = words * 8; i < texture.data.size(); ++i) {
            mix(bytes[i]);
        }
    }
    return h;
}

namespace {

using nlohmann::json;

// ---- parameter descriptors --------------------------------------------------------------------

params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi, float softLo,
                                   float softHi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = softLo;
    d.softMax = softHi;
    return d;
}

params::ParamDesc<glm::vec3> vec3Desc(std::string path, glm::vec3 def, float lo, float hi, float softLo,
                                      float softHi) {
    params::ParamDesc<glm::vec3> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = glm::vec3(lo);
    d.hardMax = glm::vec3(hi);
    d.softMin = glm::vec3(softLo);
    d.softMax = glm::vec3(softHi);
    return d;
}

params::ParamDesc<bool> boolDesc(std::string path, bool def) {
    params::ParamDesc<bool> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = false;
    d.hardMax = true;
    return d;
}

// Node names and prefixes become parameter path segments; keep them free of separators.
glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : fallback;
}

std::string sanitise(std::string name) {
    for (auto& c : name) {
        if (c == '/' || c == ' ' || c == '.') {
            c = '_';
        }
    }
    return name;
}

bool hasRouteTo(const params::Modulator& modulator, std::string_view target) {
    const auto& routes = modulator.routes();
    return std::any_of(routes.begin(), routes.end(),
                       [&](const params::ModRoute& r) { return r.target == target; });
}

// Every pointer in `ParticleParameters`, in declaration order.
//
// Its only caller unregisters, removing each parameter by its own `path()`, so this is correct by
// construction PROVIDED it visits everything -- and an omission is silent: the parameter simply
// outlives the node it belonged to. Seven did. `softness` (ADR-367) and `windInfluence`,
// `tumbleRate`, `leafAspect`, `twoSided`, `stretch`, `trailWidth` (ADR-370, ADR-040) were declared
// between `emissive` and `enabled`, and this walked from one straight to the other, so deleting a
// particle node left seven `particles/<name>/...` parameters registered against a node that no
// longer existed. Found by an invariant in `test_composition.cpp`, which had asserted a hard-coded
// count of 26 and so could only ever report the wrong total, never which ones.
//
// **Keep this in declaration order and add to it when the struct grows.**
template <typename F>
void forEachParticleParam(ParticleParameters& p, F&& f) {
    f(p.spawnRate);
    f(p.burst);
    f(p.lifetime);
    f(p.speed);
    f(p.spread);
    f(p.position);
    f(p.extent);
    f(p.gravity);
    f(p.drag);
    f(p.turbulence);
    f(p.turbulenceScale);
    f(p.turbulenceSpeed);
    f(p.attractorPosition);
    f(p.attractorStrength);
    f(p.orbit);
    for (auto*& fs : p.fieldStrength) {
        f(fs);
    }
    f(p.size);
    f(p.colorStart);
    f(p.colorEnd);
    f(p.emissive);
    f(p.softness);
    f(p.windInfluence);
    f(p.tumbleRate);
    f(p.leafAspect);
    f(p.twoSided);
    f(p.stretch);
    f(p.trailWidth);
    // ADR-520, in declaration order. See the paragraph above: an omission here is silent.
    f(p.collisionHeight);
    f(p.splashSize);
    f(p.sizeVariance);
    f(p.sizeSkew);
    f(p.dragSizeBias);
    f(p.pulseRate);
    f(p.pulseDepth);
    f(p.pulseSync);
    f(p.pulseSharpness);
    f(p.clusterRadius);
    f(p.pauseRate);
    f(p.pauseFraction);
    f(p.scatterStrength);
    f(p.scatterAnisotropy);
    f(p.enabled);
}

// ---- transforms --------------------------------------------------------------------------------

bool uniformScale(const glm::vec3& s) {
    return std::abs(s.x - s.y) < 1e-6f && std::abs(s.x - s.z) < 1e-6f;
}

// outer * inner: applies `inner` first. Exact TRS composition when the outer scale is uniform,
// matrix decomposition otherwise.
Transform compose(const Transform& outer, const Transform& inner) {
    if (uniformScale(outer.scale)) {
        Transform t;
        t.position = outer.position + outer.rotation * (inner.position * outer.scale.x);
        t.rotation = outer.rotation * inner.rotation;
        t.scale = inner.scale * outer.scale.x;
        return t;
    }
    return Transform::fromMatrix(outer.matrix() * inner.matrix());
}

glm::vec3 transformPoint(const Transform& t, const glm::vec3& p) {
    return t.position + t.rotation * (p * t.scale);
}

glm::vec3 transformDirection(const Transform& t, const glm::vec3& d) {
    const glm::vec3 r = t.rotation * d;
    const float len = glm::length(r);
    return len > 1e-12f ? r / len : d;
}

// The scale factor a nested particle system inherits (its extents and sizes are lengths).
float lengthScale(const Transform& t) {
    return std::cbrt(std::abs(t.scale.x * t.scale.y * t.scale.z));
}

// The JSON primitives this file used to carry its own byte-identical copies of. They live in
// `spatial/detail.hpp`, which `composition_data.cpp` next door and `graph/detail.hpp` already
// share; the duplicate here was seventy lines that had to be kept in step with those by hand,
// and nothing checked that it was. The namespace is named for the module the helpers grew up in
// rather than for what it holds -- docs/reports/qa-2026-09-22.md records that as a naming problem
// deliberately left alone, because renaming it touches seven modules to no behavioural end.
using spatial::detail::readBool;
using spatial::detail::readFloat;
using spatial::detail::readInt;
using spatial::detail::readString;
using spatial::detail::vecToJson;

// `spatial::detail` spells the vector reader `readVecN`; this file was written against `readVec`
// at twenty-seven call sites, and renaming them would have buried the deduplication in churn.
template <glm::length_t N>
Result<glm::vec<N, float>> readVec(const json& j, const char* key, const glm::vec<N, float>& def) {
    return spatial::detail::readVecN<N>(j, key, def);
}


PunctualLight defaultKeyLight() {
    PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
    key.color = glm::vec3(1.0f, 0.97f, 0.92f);
    key.intensity = 3.0f;
    // ADR-034: the key casts. A world with no authored lighting still gets contact and form,
    // which is the single largest reason the old frames read as computer generated.
    key.castsShadow = true;
    key.softness = 1.0f;
    key.temperature = 5600.0f;
    return key;
}

// ---- authored lights (ADR-278) -----------------------------------------------------------------

// Angles are **degrees** here and radians in `PunctualLight`, deliberately: every other angle a
// person writes in this repository is in degrees -- a node's `rotation`, a rig's `azimuth`,
// `elevation` and `cone` -- and a format that is radians in one file and degrees in its sibling is
// a format somebody will get wrong exactly once, in the dark.
Result<Composition::AuthoredLight> authoredLightFromJson(const json& e, std::string_view where) {
    if (!e.is_object()) {
        return fail("light must be an object");
    }
    Composition::AuthoredLight out;
    PunctualLight& l = out.light;
    if (e.contains("id") && e["id"].is_string()) {
        out.id = e["id"].get<std::string>();
    }
    auto name = readString(e, "name", "");
    if (!name) {
        return std::unexpected(name.error());
    }
    if (name->empty()) {
        // Required rather than defaulted. A light's name is how it is reported when `packLight`
        // refuses it, how the `lights` overlay labels it, and half of any parameter path it ever
        // gets; a light nobody can name is a light nobody can find in a frame that has thirty.
        return fail("light: 'name' is required");
    }
    l.name = *name;
    auto node = readString(e, "node", "");
    if (!node) {
        return std::unexpected(node.error());
    }
    out.node = *node;
    auto type = readString(e, "type", lightTypeName(l.type));
    if (!type) {
        return std::unexpected(type.error());
    }
    if (auto parsed = lightTypeFromName(*type)) {
        l.type = *parsed;
    } else {
        return fail("light '{}': unknown type '{}'", l.name, *type);
    }
    auto role = readString(e, "role", lightRoleName(l.role));
    if (!role) {
        return std::unexpected(role.error());
    }
    if (auto parsed = lightRoleFromName(*role)) {
        l.role = *parsed;
    } else {
        return fail("light '{}': unknown role '{}'", l.name, *role);
    }
    float innerDegrees = glm::degrees(l.innerConeAngle);
    float outerDegrees = glm::degrees(l.outerConeAngle);
    struct FloatField {
        const char* key;
        float* target;
    };
    for (const FloatField f : {FloatField{"intensity", &l.intensity},
                               FloatField{"temperature", &l.temperature},
                               FloatField{"tint", &l.tint},
                               FloatField{"range", &l.range},
                               FloatField{"innerCone", &innerDegrees},
                               FloatField{"outerCone", &outerDegrees},
                               FloatField{"width", &l.width},
                               FloatField{"height", &l.height},
                               FloatField{"radius", &l.radius},
                               FloatField{"shadowStrength", &l.shadowStrength},
                               FloatField{"shadowBias", &l.shadowBias},
                               FloatField{"softness", &l.softness},
                               FloatField{"volumetric", &l.volumetricStrength}}) {
        auto v = readFloat(e, f.key, *f.target);
        if (!v) {
            return fail("light '{}': {}", l.name, v.error().message);
        }
        *f.target = *v;
    }
    l.innerConeAngle = glm::radians(innerDegrees);
    l.outerConeAngle = glm::radians(outerDegrees);
    struct VecField {
        const char* key;
        glm::vec3* target;
    };
    for (const VecField v : {VecField{"position", &l.position}, VecField{"direction", &l.direction},
                             VecField{"up", &l.up}, VecField{"color", &l.color}}) {
        auto value = readVec<3>(e, v.key, *v.target);
        if (!value) {
            return fail("light '{}': {}", l.name, value.error().message);
        }
        *v.target = *value;
    }
    struct BoolField {
        const char* key;
        bool* target;
    };
    for (const BoolField b : {BoolField{"castsShadow", &l.castsShadow},
                              BoolField{"contactShadow", &l.contactShadow},
                              BoolField{"diffuseOnly", &l.diffuseOnly},
                              BoolField{"specularOnly", &l.specularOnly},
                              BoolField{"enabled", &l.enabled}}) {
        auto value = readBool(e, b.key, *b.target);
        if (!value) {
            return fail("light '{}': {}", l.name, value.error().message);
        }
        *b.target = *value;
    }

    // ADR-272 §2 one level up. `packLight` drops a light whose position, colour, intensity or range
    // is not finite and uploads it black, on purpose -- but a light that is black because its file
    // has a typo in it is a light somebody will look for in the renderer. Refused here, with the
    // file, the light and the value in the message.
    const auto finite3 = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite3(l.position) || !finite3(l.direction) || !finite3(l.color) ||
        !std::isfinite(l.intensity) || !std::isfinite(l.range)) {
        return fail("light '{}': position, direction, colour, intensity and range must be finite", l.name);
    }
    if (l.intensity < 0.0f) {
        return fail("light '{}': intensity {} is negative", l.name, l.intensity);
    }
    const bool aimed = l.type == PunctualLight::Type::Directional || l.type == PunctualLight::Type::Spot;
    if (aimed && glm::length(l.direction) < 1e-6f) {
        return fail("light '{}': a {} light needs a non-zero 'direction'", l.name, lightTypeName(l.type));
    }
    if (glm::length(l.direction) > 1e-6f) {
        // The struct's comment says "normalised by users" and every producer in the engine obliges.
        // A file is a user who cannot be asked, so it is obliged here rather than trusted: the two
        // fixtures' [-0.35, -0.72, -0.6] is 1.00045 long, which is the shader's falloff off by a
        // factor nobody would ever find by looking.
        l.direction = glm::normalize(l.direction);
    }
    json_keys::warnUnknownKeys(e, kAuthoredLightKeys,
                               std::string(where) + ": light '" + l.name + "'");
    return out;
}

json authoredLightToJson(const Composition::AuthoredLight& a) {
    // `name` and `type` always, because they are the light's identity and a file that has to be
    // read by a person should say what kind of light it is even when it is the default kind.
    // Everything else only when it is not the default, so a scene round-trips to what it authored
    // rather than to a 26-key dump of a struct.
    const PunctualLight def;
    const PunctualLight& l = a.light;
    json e = json::object();
    e["name"] = l.name;
    e["type"] = lightTypeName(l.type);
    // Only when it has stopped matching the name -- i.e. only after a rename. A scene nobody has
    // renamed therefore stays byte-identical, and the key shows up exactly when it is the only
    // thing keeping the parameter paths pointing at this light.
    if (!a.id.empty() && a.id != sanitise(l.name)) {
        e["id"] = a.id;
    }
    if (!a.node.empty()) {
        e["node"] = a.node;
    }
    if (l.role != def.role) e["role"] = lightRoleName(l.role);
    if (l.position != def.position) e["position"] = vecToJson(l.position);
    if (l.direction != def.direction) e["direction"] = vecToJson(l.direction);
    if (l.up != def.up) e["up"] = vecToJson(l.up);
    if (l.color != def.color) e["color"] = vecToJson(l.color);
    if (l.intensity != def.intensity) e["intensity"] = l.intensity;
    if (l.temperature != def.temperature) e["temperature"] = l.temperature;
    if (l.tint != def.tint) e["tint"] = l.tint;
    if (l.range != def.range) e["range"] = l.range;
    if (l.innerConeAngle != def.innerConeAngle) e["innerCone"] = glm::degrees(l.innerConeAngle);
    if (l.outerConeAngle != def.outerConeAngle) e["outerCone"] = glm::degrees(l.outerConeAngle);
    if (l.width != def.width) e["width"] = l.width;
    if (l.height != def.height) e["height"] = l.height;
    if (l.radius != def.radius) e["radius"] = l.radius;
    if (l.castsShadow != def.castsShadow) e["castsShadow"] = l.castsShadow;
    if (l.contactShadow != def.contactShadow) e["contactShadow"] = l.contactShadow;
    if (l.shadowStrength != def.shadowStrength) e["shadowStrength"] = l.shadowStrength;
    if (l.shadowBias != def.shadowBias) e["shadowBias"] = l.shadowBias;
    if (l.softness != def.softness) e["softness"] = l.softness;
    if (l.volumetricStrength != def.volumetricStrength) e["volumetric"] = l.volumetricStrength;
    if (l.diffuseOnly != def.diffuseOnly) e["diffuseOnly"] = l.diffuseOnly;
    if (l.specularOnly != def.specularOnly) e["specularOnly"] = l.specularOnly;
    if (l.enabled != def.enabled) e["enabled"] = l.enabled;
    return e;
}

// Field references inside nested scenes point at the nested file's field names; after flattening
// every field is renamed "<prefix><name>", so the references follow (idempotent: a name that
// already starts with the prefix is left alone, which also covers the per-frame re-application).
// The generated ground material's name, before this composition's prefix is applied. One place, so
// the node that names it and the pass that creates it cannot drift apart.

// ADR-256: the surface class of a scatter layer, from the thing that already knows.
//
// **The asset library's own category first**, because that is where the knowledge came from and a
// second heuristic over the same asset is two classifications that can disagree -- the failure
// `entity::classifyAsset` records in its own first comment.
//
// `entity::classifyScatterLayer` is deliberately NOT used as the primary path even though it reads
// the same field, because it is a *navigation* classifier and its collapse is wrong here: it maps
// Terrain, Water, Particle and Atmosphere onto Vegetation, which is correct for "can a walker pass
// through it" and false about what the surface is. It is used only as the fallback, where it is the
// filename-and-layer-name keyword table and nothing else exists -- which is Glowmere's case, whose
// scene file predates categories entirely.
SurfaceClass scatterSurfaceClass(const world::ScatterLayer& layer) {
    if (const std::optional<assets::AssetCategory> c = assets::assetCategoryFromName(layer.category)) {
        switch (*c) {
        case assets::AssetCategory::Flora:
        case assets::AssetCategory::Fungi:
        case assets::AssetCategory::Organic:
        case assets::AssetCategory::Floating:
            return SurfaceClass::Vegetation;
        case assets::AssetCategory::Rock:
        case assets::AssetCategory::Crystal:
            return SurfaceClass::Rock;
        case assets::AssetCategory::Structure:
        case assets::AssetCategory::Architectural:
            return SurfaceClass::Architecture;
        case assets::AssetCategory::Creature:
            return SurfaceClass::Character;
        case assets::AssetCategory::Terrain:
            return SurfaceClass::Terrain;
        case assets::AssetCategory::Water:
            return SurfaceClass::Water;
        case assets::AssetCategory::Particle:
        case assets::AssetCategory::Atmosphere:
            return SurfaceClass::Effect;
        case assets::AssetCategory::Unknown:
            break;
        }
    }
    switch (entity::classifyScatterLayer(layer)) {
    case spatial::ObstacleType::Vegetation:
    case spatial::ObstacleType::Trunk: // a tree is vegetation; only navigation cares that it is solid
        return SurfaceClass::Vegetation;
    case spatial::ObstacleType::Rock:
        return SurfaceClass::Rock;
    case spatial::ObstacleType::Structure:
        return SurfaceClass::Architecture;
    case spatial::ObstacleType::Creature:
        return SurfaceClass::Character;
    case spatial::ObstacleType::Custom:
        break;
    }
    return SurfaceClass::Unclassified;
}

std::string terrainGroundProgramName(const std::string& node) {
    return node + "_ground";
}

std::string terrainWaterProgramName(const std::string& node) {
    return node + "_water";
}

std::string prefixed(const std::string& prefix, const std::string& name) {
    if (name.empty() || prefix.empty() || name.compare(0, prefix.size(), prefix) == 0) {
        return name;
    }
    return prefix + name;
}
void prefixFieldReferences(ProceduralGeometry& pg, const std::string& prefix) {
    for (auto& e : pg.effectors) {
        e.field = prefixed(prefix, e.field);
    }
    for (auto& d : pg.deformers) {
        d.field = prefixed(prefix, d.field);
        d.spline = prefixed(prefix, d.spline);
    }
    pg.emissiveField = prefixed(prefix, pg.emissiveField);
    pg.distribution.spline = prefixed(prefix, pg.distribution.spline);
    pg.source.reference = prefixed(prefix, pg.source.reference);
    pg.material.program = prefixed(prefix, pg.material.program);
}
void prefixFieldReferences(ParticleSystem& ps, const std::string& prefix) {
    for (auto& f : ps.fieldForces) {
        f.field = prefixed(prefix, f.field);
    }
    ps.spline = prefixed(prefix, ps.spline);
    // The terrain's scatter objects are named with the same prefix (scatterObjectName below).
    ps.scatterAnchor.terrain = prefixed(prefix, ps.scatterAnchor.terrain);
}
void prefixSdfReferences(spatial::SdfNode& node, const std::string& prefix) {
    node.reference = prefixed(prefix, node.reference);
    for (auto& child : node.children) {
        prefixSdfReferences(child, prefix);
    }
}
void prefixFieldReferences(SdfObject& so, const std::string& prefix) {
    prefixSdfReferences(so.tree.root, prefix);
    so.material.program = prefixed(prefix, so.material.program);
}
void prefixFieldReferences(MaterialProgram& mp, const std::string& prefix) {
    for (auto& op : mp.ops) {
        op.field = prefixed(prefix, op.field);
    }
}
// A node's transform applied to a spline: the generated control points become explicit points.
void foldSplineFrame(spatial::Spline& sp, const Transform& outer) {
    std::vector<spatial::SplinePoint> pts = sp.controlPoints();
    const glm::mat4 m = outer.matrix();
    const glm::mat3 r = glm::mat3(m);
    for (auto& pt : pts) {
        pt.position = glm::vec3(m * glm::vec4(pt.position, 1.0f));
        pt.tangent = r * pt.tangent;
    }
    sp.points = std::move(pts);
    sp.generator = spatial::SplineGenerator::Points;
    sp.noiseAmount = 0.0f; // already applied by controlPoints()
}
void prefixFieldReferences(spatial::FieldSpec& f, const std::string& prefix) {
    for (auto& child : f.children) {
        child = prefixed(prefix, child);
    }
    f.reference = prefixed(prefix, f.reference);
}
// Folds a node/world transform into the field's own frame (world = outer * field frame).
void foldFieldFrame(spatial::FieldSpec& f, const Transform& outer) {
    const Transform folded = Transform::fromMatrix(outer.matrix() * f.localToWorld());
    f.position = folded.position;
    f.rotationDegrees = eulerDegrees(folded.rotation);
    f.scale = folded.scale;
}

constexpr float kFitFovRadians = 0.87f;

// Names of a nested scene's objects inside the flattened scene. An attached child already names
// its objects "nodes_<node>_<name>" (its parameter prefix sanitised), which is unique and matches
// the parameter paths; an unattached child gets the same prefix applied here.
std::string nestedPrefixFor(const std::string& outerPrefix, const std::string& nodeName, bool childAttached) {
    return childAttached ? std::string() : sanitise(outerPrefix) + "nodes_" + nodeName + "_";
}

} // namespace

glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

// Inverse of glm::quat(vec3): that constructor builds Rz * Ry * Rx. glm::eulerAngles recovers
// the middle angle with asin, which loses precision near +-90 degrees; atan2 does not.
//
// **Which of the two answers it gives matters, and it is not the one asin picks (ADR-240).** A
// ZYX decomposition always has exactly two solutions -- (x, y, z) and (x+180, 180-y, z+180) -- and
// the textbook form returns the one with the middle angle inside [-90, 90]. For a node yawed more
// than a quarter turn that is the *flipped* one: a pure 140.97 degree yaw comes back as
// (180, 39.03, -180). Numerically identical, and wrong for this engine in two ways that cost a
// day to find.
//
//   * Everything downstream treats the triple as (pitch, yaw, roll) by position. The entity layer
//     adds a body's steering to component 1 and the ground follower adds a slope's lean to 0 and 2
//     (`applyOffsets`, `GroundFollower`). In the flipped branch component 1 is `180 - yaw`, so a
//     body turning +d degrees is *drawn turning -d*: the gap between where it walks and where it
//     faces opens at twice the rate it turns, reaching a full reversal after a quarter turn. That
//     is "moving backward while playing a forward-walking animation", and also the sideways and
//     every angle in between, because 2d takes every value.
//   * It is what a scene *saves*. An author writes `[0, 140.97, 0]` and gets `[180, 39.03, -180]`
//     back on the next save.
//
// So the branch is chosen rather than inherited: whichever of the two is nearer to upright, which
// is the one an author would have written. Both reproduce `q` exactly through
// `quatFromEulerDegrees`; this is a choice of representation, not an approximation.
glm::vec3 eulerDegrees(const glm::quat& q) {
    const glm::mat3 m = glm::mat3_cast(q); // m[column][row]
    const float m00 = m[0][0];
    const float m10 = m[0][1];
    const float m20 = m[0][2];
    const float m01 = m[1][0];
    const float m11 = m[1][1];
    const float m21 = m[1][2];
    const float m22 = m[2][2];
    const float cy = std::sqrt(m00 * m00 + m10 * m10);
    const float y = std::atan2(-m20, cy);
    float x = 0.0f;
    float z = 0.0f;
    if (cy > 1e-6f) {
        x = std::atan2(m21, m22);
        z = std::atan2(m10, m00);
    } else {
        x = std::atan2(-m20 * m01, m11); // gimbal lock: fold roll into pitch
        return glm::degrees(glm::vec3(x, y, z));
    }
    const glm::vec3 principal = glm::degrees(glm::vec3(x, y, z));
    // The other solution, folded back into (-180, 180].
    const auto fold = [](float degrees) {
        float d = std::fmod(degrees + 180.0f, 360.0f);
        if (d < 0.0f) {
            d += 360.0f;
        }
        return d - 180.0f;
    };
    const glm::vec3 other(fold(principal.x + 180.0f), fold(180.0f - principal.y),
                          fold(principal.z + 180.0f));
    const float uprightPrincipal = std::abs(principal.x) + std::abs(principal.z);
    const float uprightOther = std::abs(other.x) + std::abs(other.z);
    return uprightOther < uprightPrincipal ? other : principal;
}


// ---- node kinds --------------------------------------------------------------------------------

const char* nodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Gltf:
        return "gltf";
    case NodeKind::Orb:
        return "orb";
    case NodeKind::Grid:
        return "grid";
    case NodeKind::Particles:
        return "particles";
    case NodeKind::Scene:
        return "scene";
    case NodeKind::Procedural:
        return "procedural";
    case NodeKind::Field:
        return "field";
    case NodeKind::Spline:
        return "spline";
    case NodeKind::Sdf:
        return "sdf";
    case NodeKind::Terrain:
        return "terrain";
    case NodeKind::City:
        return "city";
    case NodeKind::Group:
        return "group";
    }
    return "gltf";
}

Result<NodeKind> nodeKindFromName(const std::string& name) {
    for (const NodeKind kind :
         {NodeKind::Gltf, NodeKind::Orb, NodeKind::Grid, NodeKind::Particles, NodeKind::Scene, NodeKind::Procedural,
          NodeKind::Field, NodeKind::Spline, NodeKind::Sdf, NodeKind::Terrain, NodeKind::Group,
          NodeKind::City}) {
        if (name == nodeKindName(kind)) {
            return kind;
        }
    }
    return fail("unknown node kind '{}'", name);
}

// ---- lifetime ----------------------------------------------------------------------------------

Composition::Composition(assets::AssetRegistry& registry, std::string name)
    : registry_(registry)
    , name_(std::move(name)) {
    scene_.environment.gridIntensity = 0.6f;
    scene_.environment.backgroundColor = glm::vec3(0.02f, 0.02f, 0.03f);
}

Composition::~Composition() = default;

// ---- nodes -------------------------------------------------------------------------------------

std::string Composition::nestedPrefix(const CompositionNode& node) const {
    return nestedPrefixFor(prefix_, node.name, node.child != nullptr && node.child->attached());
}
void Composition::rebuildProcedurals() {
    // Every object sees the complete list (Procedural sources reference siblings by name, spline
    // distributions read scene_.splines); generateCloud resolves references recursively so the
    // order of the vector does not matter.
    const GenerationContext ctx{&scene_.procedurals, &scene_.splines, 0};
    if (interactiveRebuildBudgetMs_ <= 0.0) {
        // Offline, and every non-editor caller: regenerate whatever wants to. No clock is read, so
        // the result depends only on the scene and the time, which is what determinism means.
        for (ProceduralGeometry& pg : scene_.procedurals) {
            pg.rebuild(ctx);
        }
        return;
    }

    proceduralRebuild_.resize(scene_.procedurals.size());
    const auto now = std::chrono::steady_clock::now();
    const double sinceLastFrameMs =
        lastRebuildPollTime_.time_since_epoch().count() == 0
            ? 0.0
            : std::chrono::duration<double, std::milli>(now - lastRebuildPollTime_).count();
    lastRebuildPollTime_ = now;

    for (std::size_t i = 0; i < scene_.procedurals.size(); ++i) {
        ProceduralGeometry& pg = scene_.procedurals[i];
        ProceduralRebuildState& state = proceduralRebuild_[i];

        // Cheap objects never defer: the machinery would cost more than the work.
        if (state.lastMs <= interactiveRebuildBudgetMs_) {
            const auto started = std::chrono::steady_clock::now();
            if (pg.rebuild(ctx)) {
                state.lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                        started)
                                   .count();
            }
            state.deferring = false;
            state.settledForMs = 0.0;
            state.heldForMs = 0.0;
            continue;
        }

        const std::uint64_t wanted = pg.contextualHash(ctx);
        if (!advanceRebuildDeferral(state, wanted, pg.builtHash, sinceLastFrameMs)) {
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        if (pg.rebuild(ctx)) {
            state.lastMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
        state.deferring = false;
        state.settledForMs = 0.0;
        state.heldForMs = 0.0;
    }
}

std::size_t Composition::proceduralsAwaitingRebuild() const {
    return static_cast<std::size_t>(
        std::count_if(proceduralRebuild_.begin(), proceduralRebuild_.end(),
                      [](const ProceduralRebuildState& s) { return s.deferring; }));
}

void Composition::rebuildSdfs() {
    for (SdfObject& so : scene_.sdfs) {
        so.rebuild(currentTime_, &scene_.fields);
    }
}

// ---- procedural graph (ADR-028) --------------------------------------------------------------

Result<void> Composition::setGraph(graph::Graph g, params::Modulator* modulator) {
    if (auto ok = g.validate(); !ok) {
        return ok;
    }
    if (modulator != nullptr) {
        graphModulator_ = modulator;
    }
    graph_ = std::move(g);
    graphDirty_ = true;
    return {};
}

void Composition::clearGraph() {
    for (const std::string& name : graphNodes_) {
        removeNode(name);
    }
    graphNodes_.clear();
    for (const std::string& name : graphMaterials_) {
        const auto it = std::find_if(materialPrograms_.begin(), materialPrograms_.end(),
                                     [&](const MaterialProgram& mp) { return mp.name == name; });
        if (it != materialPrograms_.end()) {
            const auto index = static_cast<std::size_t>(std::distance(materialPrograms_.begin(), it));
            if (params_ != nullptr && index < materialParams_.size()) {
                unregisterMaterialProgramParameters(*params_, materialParams_[index]);
                materialParams_.erase(materialParams_.begin() + static_cast<std::ptrdiff_t>(index));
            }
            materialPrograms_.erase(it);
        }
    }
    graphMaterials_.clear();
    if (graphModulator_ != nullptr) {
        auto& routes = graphModulator_->routes();
        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [](const params::ModRoute& r) { return r.fromGraph; }),
                     routes.end());
    }
    graph_.reset();
    graphDirty_ = false;
    graphWarnings_.clear();
    dirty_ = true;
}

Result<void> Composition::evaluateGraph(double time) {
    graphDirty_ = false;
    if (!graph_) {
        return {};
    }
    graph::GraphOutput out;
    if (auto ok = graph_->evaluate(out, time); !ok) {
        return ok;
    }
    graphWarnings_ = out.warnings;
    for (const std::string& warning : graphWarnings_) {
        log::warn("graph '{}': {}", graph_->name, warning);
    }
    // Replace what the previous evaluation installed (hand-added nodes are untouched).
    const std::vector<std::string> previousNodes = graphNodes_;
    const std::vector<std::string> previousMaterials = graphMaterials_;
    graphNodes_.clear();
    graphMaterials_.clear();
    for (const std::string& name : previousNodes) {
        removeNode(name);
    }
    for (const std::string& name : previousMaterials) {
        const auto it = std::find_if(materialPrograms_.begin(), materialPrograms_.end(),
                                     [&](const MaterialProgram& mp) { return mp.name == name; });
        if (it != materialPrograms_.end()) {
            const auto index = static_cast<std::size_t>(std::distance(materialPrograms_.begin(), it));
            if (params_ != nullptr && index < materialParams_.size()) {
                unregisterMaterialProgramParameters(*params_, materialParams_[index]);
                materialParams_.erase(materialParams_.begin() + static_cast<std::ptrdiff_t>(index));
            }
            materialPrograms_.erase(it);
        }
    }
    if (graphModulator_ != nullptr) {
        auto& routes = graphModulator_->routes();
        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [](const params::ModRoute& r) { return r.fromGraph; }),
                     routes.end());
    }
    // Material programs first: objects reference them by name.
    for (MaterialProgram& mp : out.materialPrograms) {
        const std::string name = mp.name;
        if (auto added = addMaterialProgram(std::move(mp)); !added) {
            log::warn("graph '{}': material program '{}': {}", graph_->name, name, added.error().message);
            continue;
        }
        graphMaterials_.push_back(name);
    }
    const auto install = [&](CompositionNode node) {
        const std::string wanted = node.name;
        auto added = addNode(std::move(node));
        if (!added) {
            log::warn("graph '{}': node '{}': {}", graph_->name, wanted, added.error().message);
            return;
        }
        graphNodes_.push_back((*added)->name);
    };
    for (ProceduralGeometry& pg : out.procedurals) {
        CompositionNode node;
        node.name = pg.name;
        node.kind = NodeKind::Procedural;
        node.procedural = std::move(pg);
        install(std::move(node));
    }
    for (spatial::FieldSpec& f : out.fields) {
        CompositionNode node;
        node.name = f.name;
        node.kind = NodeKind::Field;
        node.field = std::move(f);
        install(std::move(node));
    }
    for (spatial::Spline& sp : out.splines) {
        CompositionNode node;
        node.name = sp.name;
        node.kind = NodeKind::Spline;
        node.spline = std::move(sp);
        install(std::move(node));
    }
    for (SdfObject& so : out.sdfs) {
        CompositionNode node;
        node.name = so.name;
        node.kind = NodeKind::Sdf;
        node.sdf = std::move(so);
        install(std::move(node));
    }
    for (ParticleSystem& ps : out.particles) {
        CompositionNode node;
        node.name = ps.name;
        node.kind = NodeKind::Particles;
        node.particles = std::move(ps);
        install(std::move(node));
    }
    if (graphModulator_ != nullptr) {
        for (params::ModRoute& r : out.routes) {
            r.fromGraph = true;
            graphModulator_->addRoute(std::move(r));
        }
    }
    dirty_ = true;
    return {};
}

Result<void> Composition::addGrid(spatial::GridField grid) {
    if (auto ok = grid.validate(); !ok) {
        return ok;
    }
    for (const spatial::GridField& existing : grids_) {
        if (existing.name == grid.name) {
            return fail("grid '{}' already exists", grid.name);
        }
    }
    grids_.push_back(std::move(grid));
    dirty_ = true;
    return {};
}

// ADR-074. Validated as a set, and all-or-nothing: a scene that declares a hero the director cannot
// use should say so at load rather than silently render without it. Note the deliberate absence of
// `dirty_ = true` -- see the header; heroes describe nodes that are already placed.
Result<void> Composition::setEffects(std::vector<world::EffectInstance> effects) {
    // The whole set or none of it (ADR-702, and the rule `setHeroes` follows): an effect whose id
    // collides with another's is two things writing one parameter path, and an effect dropped for
    // being invalid is an effect that never fires with nothing saying why.
    if (auto ok = world::validateEffects(effects); !ok) {
        return ok;
    }
    effects_ = std::move(effects);
    // Deliberately no `dirty_`: an effect places nothing, occludes nothing and is not an obstacle,
    // so there is nothing for a flatten to do. What reads them is the Engine, per frame.
    return {};
}

// ADR-278. The whole set or none of it, for the reason `setEffects` states: a duplicate name is
// two lights that cannot be told apart in a warning, in an overlay or in a parameter path. Unlike
// the effects this *is* dirty -- a light is part of the picture, and `rebuild` is where the picture
// is assembled.
// ADR-358. `direction` is the direction the light TRAVELS; the angles describe where it comes
// FROM, which is the half a person can point at. Azimuth is measured clockwise from +Z through +X
// so that 0 and 90 land on the two axes a scene file is usually written against, and elevation is
// signed from the horizon. A light travelling straight down comes from elevation +90, where the
// azimuth is degenerate and is reported as whatever the horizontal components say -- 0 for a
// perfectly vertical light, which is the only value that round-trips.
void Composition::lightAngles(const glm::vec3& travelDirection, float& azimuthDegrees,
                              float& elevationDegrees) {
    const float length = glm::length(travelDirection);
    const glm::vec3 toSource = length > 1e-6f ? -travelDirection / length : glm::vec3(0.0f, 1.0f, 0.0f);
    elevationDegrees = glm::degrees(std::asin(std::clamp(toSource.y, -1.0f, 1.0f)));
    azimuthDegrees = glm::degrees(std::atan2(toSource.x, toSource.z));
}

glm::vec3 Composition::lightDirectionFromAngles(float azimuthDegrees, float elevationDegrees) {
    const float a = glm::radians(azimuthDegrees);
    const float e = glm::radians(std::clamp(elevationDegrees, -89.99f, 89.99f));
    const float c = std::cos(e);
    return -glm::vec3(c * std::sin(a), std::sin(e), c * std::cos(a));
}

// A light's identity, derived from its display name when the file gave none. See the note on
// `AuthoredLight` for why the two are separate at all.
std::string Composition::authoredLightId(const AuthoredLight& light) {
    return light.id.empty() ? sanitise(light.light.name) : light.id;
}

std::string Composition::uniqueAuthoredLightId(std::string_view name, std::span<const std::string> taken) {
    const std::string base = sanitise(std::string(name));
    const auto used = [&](const std::string& candidate) {
        return std::find(taken.begin(), taken.end(), candidate) != taken.end();
    };
    if (!base.empty() && !used(base)) {
        return base;
    }
    // Suffixed rather than randomised, because an id reaches a project file and a person reading
    // `lights/fill-2/intensity` can tell which light that is.
    const std::string stem = base.empty() ? std::string("light") : base;
    for (int n = 2; n < 100000; ++n) {
        std::string candidate = stem + "-" + std::to_string(n);
        if (!used(candidate)) {
            return candidate;
        }
    }
    return stem;
}

Result<void> Composition::setAuthoredLights(std::vector<AuthoredLight> lights) {
    for (std::size_t i = 0; i < lights.size(); ++i) {
        if (lights[i].light.name.empty()) {
            return fail("lights[{}]: a light needs a name", i);
        }
        // The id is stamped here rather than left to be re-derived at every use, so that
        // `authoredLights()` is self-describing and nothing downstream has to remember the rule.
        if (lights[i].id.empty()) {
            lights[i].id = sanitise(lights[i].light.name);
        }
        // `ecology.glow.` is reserved, and this guard is the whole of what stands between a user
        // and a genuinely nasty failure. `updateEcologyLights` removes last frame's glow by
        // `std::erase_if` over **all** of `scene_.lights` on this name prefix -- not over the
        // ecology range -- so an authored light called "ecology.glow.x" would be deleted on the
        // first frame the ecology produced anything, AND would shift every authored light after it
        // relative to `authoredLightFirst_`, leaving the parameter block driving the wrong light.
        // It was unreachable while only a text editor could name a light. A Lights panel is exactly
        // what makes it reachable, so it is refused here rather than left as a trap.
        // Normalised here as well as in the reader, so a light built in C++ and the same light
        // round-tripped through a file are the same light. They were not: `PunctualLight`'s default
        // direction is (-0.4, -1, -0.35), which is 1.1435 long, so a parsed light carried a unit
        // vector and a constructed one did not -- and `toJson` then wrote `direction` for one and
        // omitted it for the other. That made a light added in the editor differ from its own
        // serialisation, which is the difference `authoredLightsAgainst` is measuring.
        if (glm::length(lights[i].light.direction) > 1e-6f) {
            lights[i].light.direction = glm::normalize(lights[i].light.direction);
        }
        if (lights[i].light.name.starts_with(kEcologyLightPrefix)) {
            return fail("lights[{}]: '{}' is reserved for the procedural ecology; choose another name",
                        i, lights[i].light.name);
        }
        for (std::size_t k = 0; k < i; ++k) {
            // Ids must be unique because they are the parameter path: two lights answering to
            // `lights/key/intensity` is one knob driving whichever the loop reaches last.
            if (lights[k].id == lights[i].id) {
                return fail("lights[{}]: duplicate light id '{}'", i, lights[i].id);
            }
            // Names too, and this is a narrower claim than it looks. Under a stable id a display
            // name could be anything, but `DayNight` binds its sun and its moon to lights **by
            // name** (`sunLight`, `moonLight`), so two lights called "moon" is an ambiguity with a
            // silent winner. Relaxing this means moving that binding onto ids first.
            if (lights[k].light.name == lights[i].light.name) {
                return fail("lights[{}]: duplicate light name '{}'", i, lights[i].light.name);
            }
        }
    }
    // The knobs belong to the lights, so a new set of lights is a new set of knobs. Unregistered
    // against the OLD names first -- `unregisterAuthoredLightParameters` walks `authoredLights_` --
    // or the previous rig's paths stay in the set for ever, which is how a parameter panel comes to
    // list a light the scene no longer has (ADR-358).
    const bool attached = params_ != nullptr;
    if (attached) {
        unregisterAuthoredLightParameters();
    }
    authoredLights_ = std::move(lights);
    authoredLightsRest_ = authoredLights_;
    if (attached) {
        registerAuthoredLightParameters(*params_);
    }
    dirty_ = true;
    return {};
}

Result<void> Composition::setHeroes(std::vector<world::HeroPoint> heroes) {
    for (std::size_t i = 0; i < heroes.size(); ++i) {
        if (auto ok = heroes[i].validate(); !ok) {
            return std::unexpected(ok.error());
        }
        // Names are how everything downstream refers to a hero -- a focus request, a reaction
        // profile installed against it, a director's choice of subject. Two heroes called the same
        // thing would make every one of those ambiguous, and the ambiguity would surface as the
        // wrong object being framed rather than as an error.
        for (std::size_t j = 0; j < i; ++j) {
            if (heroes[j].name == heroes[i].name) {
                return fail("hero '{}' is declared twice", heroes[i].name);
            }
        }
    }
    heroes_ = std::move(heroes);
    // Anchored where their objects stand *now*, so the first sync after this reports no motion. A
    // hero declared at the same moment its object is placed must not read as having been dragged.
    heroAnchors_.assign(heroes_.size(), std::nullopt);
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        if (const CompositionNode* node = findNode(heroes_[i].name)) {
            heroAnchors_[i] = nodeWorldTransform(*node).position;
        }
    }
    // The authored positions start as the ones the file gave, which is the whole point: this is
    // the value `toJson` writes back, so it must survive everything the simulation does. The base
    // anchors are deliberately left unadopted -- see their declaration.
    heroBasePositions_.resize(heroes_.size());
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        heroBasePositions_[i] = heroes_[i].position;
    }
    heroBaseAnchors_.assign(heroes_.size(), std::nullopt);
    heroMotionPending_ = false;
    ++heroRevision_;
    // ADR-193: a hero is an obstacle. `rebuild()` is the only thing that calls
    // `obstaclesFromHeroes`, so a hero declared without one is a solid the navigation layer never
    // hears about -- the director frames it, the editor draws its ring, and walkers stroll through
    // it. Heroes are declared rarely and a flatten is expensive, but a flatten that does not happen
    // is a world that disagrees with itself.
    dirty_ = true;
    return {};
}

// A hero describes an object that is already placed (ADR-074), so moving the object has to move the
// description with it. Before this it did not: the position was a snapshot taken at declaration, so
// dragging a hero's object left the director framing the empty space it used to occupy, and the
// editor's own hero mark stayed behind on the ground.
//
// Translation only, and by delta. A hero's position is allowed to sit somewhere other than the
// middle of its object -- an authored one deliberately does -- so re-measuring from the bounds would
// silently discard that. Size is not followed either: scaling an object is a rarer, more deliberate
// act than moving it, and re-measuring `radius` and `height` would overwrite numbers a person may
// have chosen. Undeclaring and declaring again is the way to take a fresh measurement.
//
// A hero that names an assembly rather than a node has no anchor and is left alone; there is no one
// object whose movement would be the assembly's.
void Composition::recordFollowTrails() {
    // What has to be remembered, and for how long. Collected from the rigs each frame rather than
    // cached, because a rig's lag is a parameter somebody can change mid-session and a trail sized
    // against a stale lag is a chase that silently stops lagging.
    double longest = 0.0;
    std::vector<const std::string*> wanted;
    for (const CameraRig& rig : cameraDirection_.cameras) {
        if (rig.followNode.empty() || rig.followLagSeconds <= 0.0) {
            continue;
        }
        longest = std::max(longest, rig.followLagSeconds);
        if (std::none_of(wanted.begin(), wanted.end(),
                         [&](const std::string* n) { return *n == rig.followNode; })) {
            wanted.push_back(&rig.followNode);
        }
    }
    if (wanted.empty()) {
        // Nothing chases anything. Release rather than leave, so switching a chase off stops paying
        // for it immediately instead of at the next scene load.
        followTrails_.clear();
        return;
    }

    // A margin over the longest lag, so a query lands inside the samples rather than on the oldest
    // one; and a ceiling, because a lag of an hour is a typo and not a request.
    const double keep = std::min(longest + 1.0, 60.0);

    for (const std::string* name : wanted) {
        auto it = std::find_if(followTrails_.begin(), followTrails_.end(),
                               [&](const FollowTrail& t) { return t.node == *name; });
        if (it == followTrails_.end()) {
            followTrails_.push_back(FollowTrail{.node = *name, .samples = {}});
            it = std::prev(followTrails_.end());
        }
        const CompositionNode* node = findNode(*name);
        if (node == nullptr) {
            continue; // a chase whose subject left the scene keeps the trail it had
        }
        const Transform world = nodeWorldTransform(*node);
        // A seek is a discontinuity in a record of *when things were*, not a gap to interpolate
        // across: time going backwards, or forwards by more than a long frame, means the samples
        // after it do not describe the same play-through as the ones before. Dropping the old ones
        // is what stops a chase interpolating the subject across a cut in time.
        if (!it->samples.empty()) {
            const double last = it->samples.back().seconds;
            if (currentTime_ < last || currentTime_ - last > 0.5) {
                it->samples.clear();
                followTrailShortReported_ = false;
            }
        }
        it->samples.push_back(FollowTrail::Sample{.seconds = currentTime_,
                                                  .position = world.position,
                                                  .rotation = world.rotation});
        const auto stale = std::find_if(it->samples.begin(), it->samples.end(),
                                        [&](const FollowTrail::Sample& sm) {
                                            return sm.seconds >= currentTime_ - keep;
                                        });
        if (stale != it->samples.begin()) {
            // One sample before the window is kept deliberately: a query at exactly the window's
            // edge needs something on both sides of it to interpolate between.
            it->samples.erase(it->samples.begin(), std::prev(stale));
        }
    }

    // Trails nobody chases any more.
    std::erase_if(followTrails_, [&](const FollowTrail& t) {
        return std::none_of(wanted.begin(), wanted.end(),
                            [&](const std::string* n) { return *n == t.node; });
    });
}

FollowTrail::Sample Composition::followTrailAt(const std::string& node, double seconds,
                                               const FollowTrail::Sample& fallback) const {
    const auto trail = std::find_if(followTrails_.begin(), followTrails_.end(),
                                    [&](const FollowTrail& t) { return t.node == node; });
    if (trail == followTrails_.end() || trail->samples.empty()) {
        return fallback;
    }
    const std::vector<FollowTrail::Sample>& s = trail->samples;
    if (seconds <= s.front().seconds) {
        // Before anything this play-through has seen. The camera runs un-lagged rather than
        // pretending, and says so once -- an un-lagged chase looks exactly like a working one, so
        // nothing but a log line distinguishes "the lag is not ready" from "the lag is zero".
        if (!followTrailShortReported_) {
            followTrailShortReported_ = true;
            log::info("chase: '{}' has no trail back to {:.2f}s yet; the camera runs un-lagged "
                      "until it does (the head of a render, or just after a seek)",
                      node, seconds);
        }
        return fallback;
    }
    if (seconds >= s.back().seconds) {
        return s.back();
    }
    const auto after = std::lower_bound(s.begin(), s.end(), seconds,
                                        [](const FollowTrail::Sample& sm, double t) {
                                            return sm.seconds < t;
                                        });
    const FollowTrail::Sample& b = *after;
    const FollowTrail::Sample& a = *std::prev(after);
    const double span = b.seconds - a.seconds;
    const float u = span > 1e-9 ? static_cast<float>((seconds - a.seconds) / span) : 0.0f;
    FollowTrail::Sample out;
    out.seconds = seconds;
    out.position = glm::mix(a.position, b.position, u);
    out.rotation = glm::slerp(a.rotation, b.rotation, u);
    return out;
}

std::vector<world::HeroPoint> Composition::authoredHeroes() const {
    std::vector<world::HeroPoint> out = heroes_;
    for (std::size_t i = 0; i < out.size() && i < heroBasePositions_.size(); ++i) {
        out[i].position = heroBasePositions_[i];
    }
    return out;
}

void Composition::syncHeroesToNodes() {
    if (heroes_.empty()) {
        return;
    }
    heroAnchors_.resize(heroes_.size());
    heroBaseAnchors_.resize(heroes_.size());
    heroBasePositions_.resize(heroes_.size());
    bool moved = false;
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        const CompositionNode* node = findNode(heroes_[i].name);
        if (node == nullptr) {
            heroAnchors_[i].reset();
            heroBaseAnchors_[i].reset();
            continue;
        }
        // The authoring half, independent of the live half below. Only a change to the node's
        // *base* -- an author moving it -- carries the hero's authored position with it.
        const glm::vec3 base = nodeWorldBaseTransform(*node).position;
        if (!heroBaseAnchors_[i]) {
            heroBaseAnchors_[i] = base;
        } else {
            const glm::vec3 baseDelta = base - *heroBaseAnchors_[i];
            if (glm::dot(baseDelta, baseDelta) >= 1e-8f) {
                heroBasePositions_[i] += baseDelta;
                heroBaseAnchors_[i] = base;
            }
        }
        const glm::vec3 at = nodeWorldTransform(*node).position;
        if (!heroAnchors_[i]) {
            // First sight of this node: adopt where it is without moving the hero. A node that
            // arrives later -- an undone deletion, a renamed object -- must not teleport the hero
            // by the whole distance between them.
            heroAnchors_[i] = at;
            continue;
        }
        const glm::vec3 delta = at - *heroAnchors_[i];
        if (glm::dot(delta, delta) < 1e-8f) {
            continue;
        }
        heroes_[i].position += delta;
        heroAnchors_[i] = at;
        moved = true;
    }
    if (moved) {
        markHeroesMoved();
        return;
    }
    settleHeroes();
}

void Composition::setAimFollow(std::vector<AimFollow> shots) { aimFollow_ = std::move(shots); }

// ADR-217. Resolving the actor's hero name happens here, once, rather than in the camera director:
// the chain scenario -> actor -> body entity -> node is staging's business and the camera has no
// reason to learn it. A scenario or an actor that is not there leaves the hero empty, which makes
// the whole feature inert rather than wrong.

// The aim, and only the aim.
//
// Not the position: the camera's path is the bake, and moving it would be re-cutting the shot one
// frame at a time -- the distance to the subject, the framing, the clearance the path was lifted to
// keep, all of it is in those keys. What a walking hero breaks is which way the camera is pointed,
// and that is one vector.
//
// An offset from where the hero stood at the cut, rather than the hero's position outright. Two
// reasons, both of them things that would otherwise be lost: the shot may not have been aimed at the
// hero's centre (a look mode can aim ahead of it, or hold a direction), and the bake lifts keys
// clear of terrain and canopy. Adding the hero's *movement* to whatever the keys say keeps both.
void Composition::applyDirectedAim() {
    if (aimFollow_.empty() || heroes_.empty()) {
        return;
    }
    // ADR-245: the aim-follow table and the ADR-217 hold are the *Auto-director's* mechanisms --
    // they nudge the shot it baked onto the main camera. While an authored or event camera has the
    // frame, that shot is not what is on screen, and dragging its aim would move a camera nobody
    // pointed at the heroes. The main camera keeps them; every other camera is left alone.
    if (activeCamera_.camera != kMainCamera || activeCamera_.blending()) {
        return;
    }
    // Free mode only. The other two place the camera themselves -- an orbit around the root, a
    // spline the author drew -- and a directed sequence always writes mode 1, so this is asking
    // whether the shot on the timeline is still the one driving the camera.
    if ((cameraMode_ != nullptr ? cameraMode_->value() : cameraModeSetting_) != 1) {
        return;
    }
    const AimFollow* active = nullptr;
    for (const AimFollow& shot : aimFollow_) {
        if (currentTime_ >= shot.startSeconds && currentTime_ < shot.endSeconds) {
            active = &shot;
            break;
        }
    }
    if (active != aimFollowLast_) {
        // A cut. The delta is zero by definition at the start of a shot -- the hero is at
        // `heroAtCut` -- so the filter starts from zero rather than from the last shot's offset,
        // which would open the new shot pointing at where the previous hero had got to.
        aimFollowSmoothed_ = glm::vec3(0.0f);
        aimFollowPrimed_ = false;
        aimFollowLast_ = active;
    }
    if (active != nullptr) {
        const auto hero =
            std::find_if(heroes_.begin(), heroes_.end(),
                         [&](const world::HeroPoint& h) { return h.name == active->hero; });
        // A hero that was unstarred or renamed since the cut leaves the shot the aim it was baked
        // with, which is the last place that hero was known to be -- a stale table is inert.
        if (hero != heroes_.end()) {
            const glm::vec3 raw = hero->position - active->heroAtCut;
            glm::vec3 delta = raw;
            if (aimFollowSmoothingMs_ > 1e-3f) {
                // ADR-245. Framed in seconds of the *timeline* rather than of the wall clock, so an
                // offline render and live playback filter identically -- the same rule every other
                // smoother in this file follows.
                const double dt = std::max(currentTime_ - aimFollowPrevTime_, 0.0);
                if (!aimFollowPrimed_) {
                    // The first frame of a shot has no previous sample to move away from, and the
                    // honest starting value is the delta itself: at a cut that is zero, and after a
                    // seek it is wherever the hero actually is. Starting at zero instead would make
                    // the camera crawl to its subject over the filter's constant, every seek.
                    aimFollowSmoothed_ = raw;
                    aimFollowPrimed_ = true;
                } else if (dt > 0.0) {
                    const double tau = static_cast<double>(aimFollowSmoothingMs_) / 1000.0;
                    const auto rate = static_cast<float>(1.0 - std::exp(-dt / std::max(tau, 1e-6)));
                    aimFollowSmoothed_ += (raw - aimFollowSmoothed_) * rate;
                }
                delta = aimFollowSmoothed_;
            }
            scene_.camera.target += delta;
        }
    }
    aimFollowPrevTime_ = currentTime_;
}

// The two halves of the debounce, shared by "a hero followed its object" and "somebody edited one".
void Composition::markHeroesMoved() {
    // Everything that reads a hero's *position* -- the editor's mark, the clearance field, the
    // obstacles entities walk around -- is already correct, because it reads `heroes_`. Only the
    // revision waits, because the one thing that is expensive to redo is the directed shot.
    heroMotionPending_ = true;
    heroSettleAt_ = currentTime_ + heroSettleSeconds_;
}

void Composition::settleHeroes() {
    if (heroMotionPending_ && currentTime_ >= heroSettleAt_) {
        heroMotionPending_ = false;
        ++heroPlacementRevision_;
    }
}

Result<void> Composition::editHero(const std::string& name, const world::HeroPoint& value) {
    const auto it = std::find_if(heroes_.begin(), heroes_.end(),
                                 [&](const world::HeroPoint& h) { return h.name == name; });
    if (it == heroes_.end()) {
        return fail("no hero called '{}'", name);
    }
    if (value.name != name) {
        // Renaming is a change to the *set*: it decides which object the hero stands on and whether
        // two heroes now collide. `setHeroes` is where that is checked.
        return fail("hero '{}' cannot be renamed here", name);
    }
    if (auto ok = value.validate(); !ok) {
        return ok;
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(heroes_.begin(), it));
    *it = value;
    // An authoring edit, so it writes the authored position as well as the live one -- otherwise
    // moving a hero in the editor would be discarded by the next save.
    if (index < heroBasePositions_.size()) {
        heroBasePositions_[index] = value.position;
    }
    if (index < heroBaseAnchors_.size()) {
        // Re-adopt: the hero has been placed outright, so whatever offset it had from its node is
        // replaced rather than carried.
        heroBaseAnchors_[index].reset();
    }
    markHeroesMoved();
    return {};
}

Result<void> Composition::setEntities(std::vector<entity::EntityDesc> entities) {
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].name.empty()) {
            return fail("entities[{}] has no name", i);
        }
        for (std::size_t k = 0; k < i; ++k) {
            if (entities[k].name == entities[i].name) {
                return fail("entity '{}' is declared twice", entities[i].name);
            }
        }
    }
    entityDescs_ = std::move(entities);
    if (params_ != nullptr) {
        installEntities();
    }
    return {};
}

Result<void> Composition::setStaging(stage::StagingDesc staging) {
    stagingDesc_ = std::move(staging);
    if (params_ != nullptr) {
        staging_.unregisterParameters(*params_);
    }
    if (auto ok = staging_.setDesc(stagingDesc_); !ok) {
        return ok;
    }
    if (params_ != nullptr) {
        staging_.registerParameters(*params_, prefix_ + "staging/");
    }
    return {};
}

void Composition::installEntities() {
    if (params_ == nullptr) {
        return;
    }
    if (dirty_) {
        // Material part names and node anchors are both products of a rebuild, and an entity bound
        // against a stale one binds to the wrong thing.
        rebuild();
    }
    entityWorld_.unregisterParameters(*params_);
    entityWorld_.setEntities(entityDescs_, worldSeed());

    // What the entity layer is allowed to know about this composition: for every node, where its
    // transform parameters live, where its material knobs live, what its material parts are called
    // and where the scene put it.
    std::vector<entity::NodeBinding> bindings;
    bindings.reserve(nodes_.size());
    std::vector<std::pair<std::string, glm::vec3>> landmarks;
    std::vector<std::string> terrainLandmarks;
    landmarks.reserve(nodes_.size() + heroes_.size());
    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        entity::NodeBinding binding;
        binding.node = node.name;
        binding.exists = true;
        binding.transformPrefix = prefix_ + "nodes/" + node.name + "/";
        if (node.kind == NodeKind::Procedural) {
            binding.geometryPrefix = "procedural/" + sanitise(prefix_) + node.name + "/";
        } else if (node.kind == NodeKind::Particles) {
            // Particle knobs are registered under the system's name rather than the node's path
            // (registerParticleParameters takes no prefix), and the system is named for the node.
            binding.geometryPrefix = "particles/" + sanitise(prefix_) + node.name + "/";
        }
        binding.partNames = node.materialPartNames;
        // From the **bases**: see `nodeWorldBaseTransform`. An anchor is an authoring fact, and
        // reading it off this instant's finals made it depend on when the bindings happened to be
        // taken relative to a project load.
        const Transform placed = nodeWorldBaseTransform(node);
        binding.anchor = placed.position;
        // The facing half of the anchor (ADR-240). Read off the composed orientation rather than
        // off the euler parameter, so a node placed under a rotated parent reports the direction it
        // actually points -- which is the same rule `anchor` already follows for position.
        const glm::vec3 forward = placed.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        binding.facing = std::atan2(forward.x, forward.z);
        landmarks.emplace_back(node.name, binding.anchor);
        // Phase D §23: a terrain is the ground itself, not a place on it -- both aliens of the
        // autonomy demo opened by walking "to the ground". It stays in the landmark list (the
        // pre-Phase-D deciders and their golden traces read it) and is *tagged* "terrain", by what
        // the node is and never by its name, so an aware decider can refuse it.
        if (node.kind == NodeKind::Terrain) {
            terrainLandmarks.push_back(node.name);
        }
        bindings.push_back(std::move(binding));
    }
    // Heroes are landmarks too: "look at the elder" is the natural thing for an author to write,
    // and a hero's declared height is what makes a character look at its crown rather than its
    // roots.
    for (const world::HeroPoint& hero : heroes_) {
        landmarks.emplace_back(hero.name, hero.position + glm::vec3(0.0f, hero.height * 0.5f, 0.0f));
    }
    entityWorld_.setBindings(std::move(bindings));
    entityWorld_.setTerrainLandmarks(std::move(terrainLandmarks));
    entityWorld_.setLandmarks(std::move(landmarks));

    // The ground an entity walks on is the ground the terrain was built from -- the same WorldMap
    // and the same ecology -- so a walker can never be above or below the surface it is standing
    // on, and never needs a second description of it kept in step by hand.
    entityWorld_.setNavigator(buildNavigator());
    entityWorld_.setExtraInterestPoints(glowInterestPoints());
    // Phase D §26: how far each named world event carries, as the scene authored it.
    entityWorld_.setEventProfiles(eventProfiles_);

    // Fields go in here rather than in a pass of their own, because they bind against the same node
    // table, the same landmarks and the same entity set -- a field that resolved its source against
    // a stale anchor is the same bug as an entity bound to a stale material part. Before
    // registerParameters, because a field's knobs are parameters too and a scene swap has just
    // cleared the ones it had; bind() is what matches fields to entities once the anchors are set.
    entityWorld_.setFields(fieldDescs_);
    entityWorld_.registerParameters(*params_, prefix_ + "entity/");
    entityWorld_.bind(*params_, prefix_ + "entity/");

    // The director's knobs, registered here for the same reason a field's are: a scene swap has
    // just cleared them, and a track or a route bound before its target exists is a track that does
    // nothing, silently, for ever (ADR-075).
    staging_.unregisterParameters(*params_);
    if (auto ok = staging_.setDesc(stagingDesc_); !ok) {
        entityWorld_.recordProblems({ok.error().message});
        log::warn("staging: {}", ok.error().message);
    }
    staging_.registerParameters(*params_, prefix_ + "staging/");

    // Hand every entity that declared clips a sink onto the node's rig. An entity that declared
    // none gets one too and it does nothing -- which is the point: a craft, a rock and a character
    // are the same kind of thing here, and only the data says which.
    animationSinks_.clear();
    matchAssets_.clear();
    for (const entity::EntityDesc& desc : entityDescs_) {
        entity::Entity* live = entityWorld_.find(desc.name);
        if (live == nullptr) {
            continue;
        }
        animationSinks_.push_back(std::make_unique<AnimationSink>(*this, desc.driven(), *live));
        live->setPoseSink(animationSinks_.back().get());
        // ADR-274: and the other half of the same seam. `setSkeleton` had zero call sites for as
        // long as it existed, which is why every socket in this engine silently rode the entity's
        // own frame. One line, and it is the line between the engine and every socket, attachment,
        // carried prop and aim.
        live->setSkeleton(animationSinks_.back().get());
        // Phase B. The chain the entity advances, and the sink draws from. Installed
        // unconditionally for the same reason `setSkeleton` is: whether a body opted in is a
        // question `Entity::advanceMotion` answers per frame, and a pointer installed only for
        // opted-in bodies would have to be reinstalled every time a scene was edited.
        live->setMotionChain(&animationSinks_.back()->chain());
        // ADR-337: the third. Installed unconditionally, like the other two, because whether a
        // clip was opted in is a question about the *rig* -- which may not be built yet when this
        // runs -- and the source answers "nothing" for a body playing a clip nobody named. A
        // source that were only installed for opted-in bodies would have to be re-installed every
        // time a scene was edited, and the honest default here is the cheap one.
        live->setRootMotionSource(animationSinks_.back().get());
    }

    // ADR-337: a root-motion opt-in on a node **no entity drives** deletes motion instead of
    // transferring it. There is nothing to hand the displacement to, so the compensation lands on
    // the pose and the clip's travel simply disappears -- a body that used to descend stands
    // still. It is precisely the class of silent wrongness ADR-274 charged a metre for, so it is
    // named at load rather than discovered in a render.
    for (const auto& nodePtr : nodes_) {
        if (nodePtr->animation.rootMotion.empty()) {
            continue;
        }
        const bool driven = std::any_of(entityDescs_.begin(), entityDescs_.end(),
                                        [&](const entity::EntityDesc& d) {
                                            return d.driven() == nodePtr->name;
                                        });
        if (!driven) {
            log::warn("node '{}': root motion is opted in for {} clip(s) but no entity drives this "
                      "node, so the displacement has nowhere to go and is removed from the pose "
                      "rather than handed to a body",
                      nodePtr->name, nodePtr->animation.rootMotion.size());
        }
    }

    fieldRoutesChecked_ = false; // the "a route cannot drive a field knob" scan runs again
    for (const std::string& line : entityWorld_.fieldReport()) {
        // Unconditional, at info. Which determinism guarantee a field has (ADR-091) is a thing an
        // author has to be able to read rather than infer, and a line that is only printed when
        // something is wrong is a line nobody learns to look for.
        log::info("composition '{}': {}", name_, line);
    }

    if (modulator_ != nullptr) {
        auto& routes = modulator_->routes();
        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [](const params::ModRoute& r) { return r.fromEntity; }),
                     routes.end());
        std::vector<std::string> problems;
        std::size_t installed = 0;
        for (params::ModRoute& route : entityWorld_.compileReactions(*params_, problems)) {
            route.fromEntity = true;
            modulator_->addRoute(std::move(route));
            ++installed;
        }
        if (!entityWorld_.empty()) {
            // Say what was installed, not only what failed. "No warnings" and "nothing happened"
            // look identical in a log, and this project has shipped the second while reading it as
            // the first.
            log::info("composition '{}': {} entit{} installed, {} reaction{} bound, {} unresolved",
                      name_, entityWorld_.size(), entityWorld_.size() == 1 ? "y" : "ies",
                      installed, installed == 1 ? "" : "s", problems.size());
        }
        for (const std::string& problem : problems) {
            // Loud, by name, with the candidates that were tried. A reaction that resolves to
            // nothing is a feature that does nothing and says nothing, and this project has
            // shipped five of those.
            log::warn("{}", problem);
        }
        entityWorld_.recordProblems(problems);
    }
}

std::vector<entity::InterestPoint> Composition::glowInterestPoints() const {
    // §6 lists glowing plants first among the things a character should find interesting, and this
    // world has tens of thousands of them. The lighting pass already reduced each patch to one soft
    // emitter (ADR-053), and a patch is exactly the right granularity: a character walks to a
    // glowing hollow, not to an individual mushroom.
    std::vector<entity::InterestPoint> out;
    for (const auto& nodePtr : nodes_) {
        if (nodePtr->kind != NodeKind::Terrain || nodePtr->glow.empty()) {
            continue;
        }
        for (const world::GlowCluster& cluster : nodePtr->glow) {
            entity::InterestPoint point;
            point.position = cluster.position;
            point.kind = entity::InterestKind::Glow;
            point.weight = cluster.power;
            out.push_back(point);
        }
    }
    if (out.empty()) {
        return out;
    }
    // The brightest, and only a handful of them. A weighted pick over nine thousand near-identical
    // candidates is a uniform pick with extra steps, and it would drown every other kind of
    // interest in the registry.
    constexpr std::size_t kMaxGlow = 96;
    std::sort(out.begin(), out.end(), [](const entity::InterestPoint& a, const entity::InterestPoint& b) {
        if (a.weight != b.weight) {
            return a.weight > b.weight;
        }
        // A deterministic tie-break, so two builds of the same world offer the same places.
        if (a.position.x != b.position.x) {
            return a.position.x < b.position.x;
        }
        return a.position.z < b.position.z;
    });
    if (out.size() > kMaxGlow) {
        out.resize(kMaxGlow);
    }
    // Normalised, so "how bright" stays a preference between glowing places and does not make one
    // patch a thousand times likelier than every landmark in the world.
    const float peak = std::max(out.front().weight, 1e-6f);
    for (entity::InterestPoint& point : out) {
        point.weight = 0.35f + 0.65f * (point.weight / peak);
    }
    return out;
}

entity::Navigator Composition::buildNavigator() const {
    for (const auto& nodePtr : nodes_) {
        if (nodePtr->kind != NodeKind::Terrain) {
            continue;
        }
        world::ClearanceField field;
        field.map = &nodePtr->worldMap;
        field.ecology = &nodePtr->ecology;
        // ADR-349, and the second half of it: the obstacle field is not the only place a hero is a
        // solid. `TerrainQuery::at` asks `ClearanceField::heroPenetration` and answers `InsideHero`
        // inside `radius + cameraRadius` of any hero, so a starred character was a 1.5 m no-stand
        // zone centred on its own feet and *every* query at its own position said "not navigable".
        // A **walker's** clearance field therefore excludes the heroes that entities drive. The
        // camera's does not and must not: a camera flying through a character is a real artefact,
        // and this field is built here, for this navigator, and is not the one the camera reads.
        walkerHeroes_.clear();
        walkerHeroes_.reserve(heroes_.size());
        for (const world::HeroPoint& hero : heroes_) {
            const bool driven = std::any_of(
                entityDescs_.begin(), entityDescs_.end(),
                [&](const entity::EntityDesc& e) { return e.driven() == hero.name; });
            if (!driven) {
                walkerHeroes_.push_back(hero);
            }
        }
        field.heroes = walkerHeroes_;
        // A walker is not a camera: it stands on the ground rather than clearing it, and its
        // personal space is its own width rather than a near plane.
        field.cameraRadius = 0.6f;
        field.groundClearance = 0.0f;
        entity::Navigator nav(&nodePtr->worldMap, field);
        // Before the grid is built, because the wade band decides what the grid calls walkable.
        // Setting it afterwards would bake a graph that stops at the waterline and then hand it to
        // a walker that does not, which is the two halves of the same question disagreeing.
        // ADR-199: and the body's own width, for the same reason. The grid already inflates every
        // solid by a body radius before deciding which cells are blocked -- it just used the
        // *world's* default of 0.45 m, a person. Glowmere's inhabitants are six metres tall with a
        // 2.4 m radius, so every path was planned through gaps they do not fit in and the per-frame
        // penetration resolve then fought the walk: `sage` spent one unbroken stretch of 62 seconds
        // playing a walk cycle and going nowhere.
        if (navWadeDepth_ > 0.0f || navBodyRadius_ > 0.0f) {
            entity::NavSettings settings = nav.settings();
            if (navWadeDepth_ > 0.0f) {
                settings.wadeDepth = navWadeDepth_;
            }
            if (navBodyRadius_ > 0.0f) {
                settings.bodyRadius = navBodyRadius_;
            }
            nav.setSettings(settings);
        }
        nav.setObstacles(obstacles_, obstacles_ != nullptr ? &obstacleBridge_ : nullptr);
        // The navigation graph (ADR-093, §2). Built here rather than lazily, so its cost lands at
        // scene load where it can be seen and measured, and every walker in the world shares one.
        if (navCellSize_ > 0.0f) {
            nav.buildGrid(navCellSize_);
        }
        return nav;
    }
    return {};
}

GroundSample Composition::TerrainGroundQuery::sampleAt(const glm::vec3& worldPoint) const {
    GroundSample out;
    const world::TerrainQuery terrain = owner_.terrainQuery();
    if (!terrain.valid()) {
        return out; // no terrain in this scene: no answer, which is not the same as flat ground
    }
    const world::TerrainPoint point = terrain.at(glm::vec2(worldPoint.x, worldPoint.z));
    // **`TerrainReject` answers a different question from this one.** Its values -- `TooSteep`,
    // `Submerged`, `NoHeadroom`, `InsideHero`, `Obstructed` -- are about whether a character may
    // WALK here. Every one of those places still has a surface with a height, and a foot standing
    // on a steep bank, in shallow water or under a canopy is standing on something.
    //
    // Treating a reject as "no ground" was measured doing exactly the wrong thing: an alien's right
    // foot fell back to the body's plane precisely where the terrain was steep enough to be worth
    // sampling. Only `OutOfBounds` is genuinely no answer.
    if (point.reject == world::TerrainReject::OutOfBounds) {
        return out;
    }
    out.point = glm::vec3(worldPoint.x, point.height, worldPoint.z);
    out.normal = point.normal;
    out.distance = worldPoint.y - point.height;
    out.category = point.water ? GroundCategory::Water : GroundCategory::Terrain;
    out.valid = true;
    return out;
}

const IGroundQuery& Composition::groundQuery() const {
    return groundQuery_ != nullptr ? *groundQuery_ : terrainGround_;
}

world::TerrainQuery Composition::terrainQuery() const {
    for (const auto& nodePtr : nodes_) {
        if (nodePtr->kind != NodeKind::Terrain) {
            continue;
        }
        world::TerrainQuery query = world::terrainQuery(nodePtr->worldMap, &nodePtr->ecology, heroes_);
        if (obstacles_ != nullptr) {
            query.obstacles = &obstacleBridge_;
        }
        return query;
    }
    // A scene with no terrain is a legitimate scene: the query answers the y = 0 plane and says it
    // is not valid, which is what lets a caller run against it rather than special-casing it.
    return {};
}

std::uint32_t Composition::worldSeed() const {
    for (const auto& nodePtr : nodes_) {
        if (nodePtr->kind == NodeKind::Terrain) {
            return nodePtr->worldMap.seed;
        }
    }
    return 1u;
}

void Composition::AnimationSink::setLocomotion(const entity::LocomotionState& state) {
    // ADR-300, first, because it is the half that applies to a body with no clips at all: a craft
    // with an authored aim layer still has somewhere to put a look, and the early return below is
    // about the *state machine* having nothing to play rather than about the body having nothing to
    // do.
    driveLayers(state);
    // An action's activity first, the gait's second (ADR-096). Both are *activity names* that the
    // entity's own `clips` map turns into whatever the asset shipped -- neither the action nor the
    // gait ever names a clip, which is what lets one routine drive an alien, a deer and a robot.
    const std::string& want =
        state.action.empty() ? entity_.clipFor(state.activity) : entity_.clipFor(state.action);
    if (want.empty()) {
        return; // this entity declared no clips: it drives a craft or a prop, not a character
    }
    // ADR-820: a performance's clip cue owns the state machine for its span. Pushing the gait's
    // clip here and letting the sequencer overwrite it later in the frame re-pushed the node's
    // animation every frame; yielding leaves exactly one writer.
    if (state.clipOwned) {
        return;
    }
    // Unconditional every frame: the player treats a request for the state it is already in as a
    // no-op rather than a restart, so "what should be playing now" is the only thing a behaviour
    // has to know. The timeline second rather than a wall clock is what keeps an offline render
    // reproducible (ADR-086).
    owner_.setNodeAnimation(node_, want, state.time, state.blend, state.playbackRate);
}

// ADR-300. The other four fields.
//
// `reaction`, `lookTarget` and `hasLookTarget` have been written into `LocomotionState` every frame
// since it existed and read by nobody, which ADR-225 says is not a seam but a decoration. This is
// the read. It is a separate method from `setLocomotion` because it does a categorically different
// thing: `setLocomotion` asks the state machine for a clip, and this writes a weight and a direction
// onto the layers sitting on top of whatever clip the machine picked.
//
// **The frame (ADR-274).** `lookTarget` is a world point; a layer's target is entity-local, because
// a posed rig has no world position -- it stands in as many places as there are bodies carrying it.
// The conversion is here, in the one object that knows which node this entity drives, and it goes
// through the node's own **world** transform rather than through `state.yaw`: the node carries the
// parent chain, the behaviours' bank and nod, and -- the part ADR-274 §5 found by paying for it --
// the **scale**. Glowmere draws its aliens at 3.344x to 3.610x, and a joint offset is in the
// asset's own units, so a look-at that forgot the scale would put the head 28% of the way out from
// the body and aim from there.
//
// **Which position (ADR-260).** It reads the *drawn* transform: the node parameters' finals, which
// `applyOffsets` wrote earlier in this same `EntityWorld::update` -- so there is no frame of lag
// here, unlike attachments. It writes `SkinnedRig::layers`, which is pose intent and nothing else.
// Phase B. Build this body's provider chain against the rig it actually has.
//
// The entries come from the entity's own `clips` map and `GaitSettings` -- the two places a scene
// already authors "which clip is this body's walk" and "how fast was it authored for". Deriving
// them anywhere else would be a third answer to a question with two (ADR-260), and inventing an
// authored speed would be inventing the number ADR-540 measured as underivable.
void Composition::AnimationSink::buildChain(const SkinnedRig& rig) {
    clipProvider_.clearEntries();
    clipProvider_.setClips(&rig.clips);
    chain_.clear();

    const auto entryFor = [&](entity::Activity activity, float authored) {
        const std::string& want = entity_.clipFor(activity);
        if (want.empty()) {
            return;
        }
        const int index = findClip(rig.clips, want);
        if (index < 0) {
            return;
        }
        entity::ClipEntry entry;
        entry.clip = static_cast<std::uint32_t>(index);
        entry.mode = entity::MovementMode::Ground;
        entry.authoredSpeed = authored;
        entry.loop = true;
        clipProvider_.addEntry(std::move(entry));
    };
    entryFor(entity::Activity::Idle, 0.0f);
    entryFor(entity::Activity::Walk, entity_.desc().gait.walkSpeed);
    entryFor(entity::Activity::Run, entity_.desc().gait.runSpeed);

    // ADR-623: the matcher goes in front when this body opted in and its database could be built.
    // When the database could not be built, the chain is the clip provider alone and the log says
    // so: falling back is the designed behaviour, and doing it silently is not.
    matchAsset_.reset();
    matchProvider_.setDatabase(nullptr);
    matchProvider_.setClips(nullptr);
    if (entity_.desc().motionMatching.enabled) {
        matchAsset_ = owner_.matchAssetFor(rig, entity_.desc().motionMatching, entity_.desc().name);
        if (matchAsset_ != nullptr) {
            matchProvider_.setDatabase(&matchAsset_->db);
            matchProvider_.setClips(&matchAsset_->pack.animation);
            matchProvider_.setExpectedSkeleton(skeletonDigest(rig.skeleton));
            // The node's own scale, taken from its world transform's first basis column. Uniform
            // scale is assumed, as it is everywhere a Glowmere body is drawn.
            float scale = 1.0f;
            if (const CompositionNode* node = owner_.findNode(node_); node != nullptr) {
                scale = glm::length(glm::vec3(owner_.nodeWorldTransform(*node).matrix()[0]));
            }
            matchProvider_.setWorldScale(scale);
            // §45: the search penalties, when the scene set them.
            const entity::MotionMatchingDesc& mm = entity_.desc().motionMatching;
            entity::MatchSettings settings = matchProvider_.settings();
            if (mm.weightsVersion != 0u && mm.continuityWeight >= 0.0f) {
                settings.weights.continuity = mm.continuityWeight;
            }
            if (mm.weightsVersion != 0u && mm.transitionWeight >= 0.0f) {
                settings.weights.transition = mm.transitionWeight;
            }
            if (mm.weightsVersion != 0u && mm.styleWeight >= 0.0f) {
                settings.styleWeight = mm.styleWeight;
            }
            if (mm.weightsVersion != 0u && mm.switchMargin >= 0.0f) {
                settings.switchMargin = mm.switchMargin;
            }
            matchProvider_.setSettings(settings);
            // §44: the character's style. Not part of the database key: the database is the same
            // whichever style its reader prefers.
            std::vector<entity::MatchMotionProvider::StyleRule> rules;
            for (const auto& [style, prefixes] : mm.styles) {
                rules.push_back({style, prefixes});
            }
            matchProvider_.setStyle(mm.style, std::move(rules));
            chain_.add(&matchProvider_);
        }
    }

    // **The fallback chain, with its one implementation.** Phase C's matcher and Phase E's neural
    // provider are added in front of this; the clip provider stays at the back, because ADR-541
    // corollary 1 says every character can run in Clip mode and Clip mode is the fallback for
    // every failure.
    chain_.add(&clipProvider_);
    chainBuilt_ = true;
    // §50/§64: a body whose base pose stops coming from its clips is a large enough change that
    // it says so once, with the entries it resolved. A chain built with zero entries is the
    // silent-no-op this whole stage exists to prevent, so that case is a warning.
    if (clipProvider_.entries().empty()) {
        log::warn("entity '{}': procedural motion is on but no clip entry resolved; the body will "
                  "fall back to its clip player",
                  entity_.desc().name);
    } else {
        log::info("entity '{}': procedural motion on, {} clip entry(s), chain of {}",
                  entity_.desc().name, clipProvider_.entries().size(), chain_.size());
    }
}

void Composition::AnimationSink::prepareChain() {
    if (!entity_.desc().proceduralMotion) {
        return;
    }
    const CompositionNode* node = owner_.findNode(node_);
    if (node == nullptr || node->rigs.empty()) {
        return;
    }
    const RigId id = node->rigs.front();
    if (id >= owner_.scene_.rigs.size() || !owner_.scene_.rigs[id].skeleton.valid()) {
        return;
    }
    if (!chainBuilt_ || chainRig_ != id) {
        buildChain(owner_.scene_.rigs[id]);
        chainRig_ = id;
    }
}

std::shared_ptr<const MotionAsset> Composition::matchAssetFor(const SkinnedRig& rig,
                                                              const entity::MotionMatchingDesc& m,
                                                              const std::string& who) {
    // The key is everything the database is a function of: the skeleton, and the config.
    std::string key = skeletonDigest(rig.skeleton) + "|p" + m.packResolved + "|j";
    for (const std::string& j : m.joints) {
        key += ":" + j;
    }
    key += "|c";
    for (const std::string& c : m.contacts) {
        key += ":" + c;
    }
    key += "|k";
    for (const std::string& c : m.clips) {
        key += ":" + c;
    }
    key += fmt::format("|w{}:{}:{}:{}:{}:{}:{}:{}", m.weightsVersion, m.jointPositionWeight, m.jointVelocityWeight,
                       m.trajectoryPositionWeight, m.trajectoryFacingWeight, m.rootVelocityWeight, m.phaseWeight,
                       m.contactWeight);
    key += "|t";
    for (const float t : m.trajectory) {
        key += fmt::format(":{:.6f}", t);
    }
    if (auto it = matchAssets_.find(key); it != matchAssets_.end()) {
        return it->second;
    }
    // Built at load from the rig this scene already has. **In memory, for this process only**: it
    // is never written or shipped, so its provenance says what it is rather than claiming a
    // licence for the asset it was derived from.
    Provenance provenance;
    provenance.source = "scene rig (runtime matching)";
    provenance.license = "LicenseRef-scene-asset";
    provenance.notes = "built at load from the scene's own rig for ADR-623 motion matching; "
                       "held in memory and never written";
    provenance.processing = {"ADR-623 runtime build"};
    PackBuildOptions packOptions;
    for (const std::string& c : m.contacts.empty() ? m.joints : m.contacts) {
        packOptions.contactJoints.push_back(ContactJoint{c, ContactKind::Foot});
    }
    packOptions.toolVersion = "avgen-runtime-match";
    // §64/§92: a pack named by the scene replaces the rig's own clips. It has to be built for this
    // skeleton (ADR-650's digest); a pack for another rig would pose one character with another's
    // joints, so it is refused here and the body stays on its clip provider.
    Result<MotionPack> pack = m.packResolved.empty()
                                  ? buildMotionPack(who, rig.skeleton, rig.clips, provenance, packOptions)
                                  : readMotionPack(m.packResolved);
    if (pack && !m.packResolved.empty() && pack->skeletonDigest != skeletonDigest(rig.skeleton)) {
        pack = fail("pack '{}' was built for another skeleton", m.packResolved);
    }
    // §14: only the clips the scene admits. Filtered before the database is built, so a refused
    // clip costs nothing at search time and can never be chosen.
    if (pack && !m.clips.empty()) {
        MotionPack kept = *pack;
        kept.clips.clear();
        kept.animation.clear();
        for (std::size_t c = 0; c < pack->clips.size() && c < pack->animation.size(); ++c) {
            for (const std::string& prefix : m.clips) {
                if (pack->clips[c].name.rfind(prefix, 0) == 0) {
                    kept.clips.push_back(pack->clips[c]);
                    kept.animation.push_back(pack->animation[c]);
                    break;
                }
            }
        }
        if (kept.clips.empty()) {
            pack = fail("no clip matches motionMatching.clips");
        } else {
            pack = std::move(kept);
        }
    }
    std::shared_ptr<const MotionAsset> out;
    if (!pack) {
        log::warn("entity '{}': motion matching is on but its pack did not build ({}); the body "
                  "falls back to its clip provider",
                  who, pack.error().message);
    } else {
        MotionDatabaseOptions dbOptions;
        dbOptions.config.joints = m.joints;
        dbOptions.config.contactJoints = m.contacts;
        dbOptions.config.trajectoryTimes = m.trajectory;
        if (m.weightsVersion != 0u) {
            // §45: the scene's weights, applied at search time (the database stores unweighted
            // features, so a weight change needs no rebuild of the features themselves).
            dbOptions.config.jointPositionWeight = m.jointPositionWeight;
            dbOptions.config.jointVelocityWeight = m.jointVelocityWeight;
            dbOptions.config.trajectoryPositionWeight = m.trajectoryPositionWeight;
            dbOptions.config.trajectoryFacingWeight = m.trajectoryFacingWeight;
            dbOptions.config.rootVelocityWeight = m.rootVelocityWeight;
            dbOptions.config.phaseWeight = m.phaseWeight;
            dbOptions.config.contactWeight = m.contactWeight;
        }
        auto db = buildMotionDatabase(*pack, dbOptions);
        if (!db) {
            log::warn("entity '{}': motion matching is on but its database did not build ({}); the "
                      "body falls back to its clip provider",
                      who, db.error().message);
        } else {
            auto asset = std::make_shared<MotionAsset>();
            asset->pack = std::move(*pack);
            asset->db = std::move(*db);
            log::info("entity '{}': motion matching on, {} samples x {} dimensions over {} clips",
                      who, asset->db.sampleCount(), asset->db.dimension, asset->db.clipNames.size());
            out = std::move(asset);
        }
    }
    // A failure is cached too, so a body that cannot match does not rebuild every frame.
    matchAssets_[key] = out;
    return out;
}

Composition::MotionDebug Composition::motionDebug(std::string_view node) const {
    MotionDebug out;
    for (const auto& sink : animationSinks_) {
        if (sink->nodeName() != node) {
            continue;
        }
        const entity::Entity* live = entityWorld_.find(sink->entityName());
        out.found = true;
        out.posedByProvider = sink->posedByProvider();
        if (const CompositionNode* n = findNode(std::string(node));
            n != nullptr && !n->rigs.empty() && n->rigs.front() < scene_.rigs.size()) {
            const SkinnedRig& rig = scene_.rigs[n->rigs.front()];
            out.externalPoseFrames = rig.externalPoseFrames;
            // §50. Read from the stack as the frame left it, never re-derived: a diagnostic that
            // recomputes what it is diagnosing agrees with itself (ADR-182).
            const PoseLayerStack& stack = rig.layers;
            const std::vector<LayerResolution>& results = stack.results();
            const std::vector<IkStatus>& statuses = stack.ikStatuses();
            out.layers.reserve(stack.layers().size());
            for (std::size_t i = 0; i < stack.layers().size(); ++i) {
                const PoseLayer& layer = stack.layers()[i];
                MotionDebug::LayerRow row;
                row.name = layer.name;
                row.kind = layer.kind;
                row.requestedWeight = layer.weight;
                row.realizedWeight = layer.effectiveWeight();
                row.resolution = i < results.size() ? results[i] : LayerResolution::Inactive;
                row.ik = i < statuses.size() ? statuses[i] : IkStatus::Solved;
                row.hasTarget = layer.hasTarget;
                row.hasGround = layer.hasGround;
                out.layers.push_back(std::move(row));
            }
            out.bodyCompensation = stack.bodyCompensation().translation;
            out.unreachableAfterCompensation = stack.bodyCompensation().unreachableAfter;
        }
        const MotionContext& ctx = sink->motion();
        out.mode = ctx.mode;
        out.motionPhase = ctx.motionPhase;
        out.groundSpeed = ctx.groundSpeed;
        out.turnRate = ctx.turnRate;
        out.hasGroundPlane = ctx.hasGroundPlane;
        out.hasLookTarget = ctx.hasLookTarget;
        out.status = sink->chainResult().result.status;
        if (live != nullptr) {
            out.optedIn = live->desc().proceduralMotion;
            out.provider = live->motionMemory().provider;
            out.fellThrough = live->motionChainResult().fellThrough;
            out.generation = live->motionMemory().generation;
            out.localTime = live->motionMemory().localTime;
        }
        return out;
    }
    return out;
}

const MotionContext* Composition::motionContext(std::string_view node) const {
    for (const auto& sink : animationSinks_) {
        if (sink->nodeName() == node) {
            return &sink->motion();
        }
    }
    return nullptr;
}

// Phase B §46: how long a look layer takes to arrive. A head turn, not a reach -- 0.25s is a
// glance; the slice sets its reach layer to 0.45s because the hand has 0.8m to travel and 0.25s
// would put it at 3.2 m/s.
namespace { constexpr float kLookBlendSeconds = 0.25f; }

void Composition::AnimationSink::driveLayers(const entity::LocomotionState& state) {
    CompositionNode* node = owner_.findNode(node_);
    if (node == nullptr || node->rigs.empty()) {
        return;
    }

    // ---- Phase B: the base pose, when this body opted in ----------------------------------------
    //
    // **The split that makes this legal.** `Entity::advanceMotion` has already moved the provider
    // memory this frame, on whichever path published the seam -- so on a scrub it was advanced once
    // per replay step and holds the value a play would have reached. All that is left here is to
    // *draw* it, which is a pure function of that memory and the skeleton, and cheap enough to do
    // once per frame rather than 5,400 times per seek.
    //
    // A pose that fails to arrive leaves `hasExternalPose` false, and `SkinnedRig::evaluate` falls
    // back to the clip player. Not hidden: `posedByProvider()` and `chainResult()` report it, and
    // the lab test asserts on them.
    posedByProvider_ = false;
    if (entity_.desc().proceduralMotion) {
        const RigId id = node->rigs.front();
        if (id < owner_.scene_.rigs.size()) {
            SkinnedRig& rig = owner_.scene_.rigs[id];
            if (rig.skeleton.valid()) {
                if (!chainBuilt_ || chainRig_ != id) {
                    buildChain(rig);
                    chainRig_ = id;
                }
                chainResult_.result =
                    chain_.pose(entity_.motionMemory(), rig.skeleton, rig.externalPose);
                if (chainResult_.result.ok() &&
                    rig.externalPose.size() == rig.skeleton.joints.size()) {
                    rig.hasExternalPose = true;
                    posedByProvider_ = true;
                }
            }
        }
    }

    bool wanted = false;
    for (const RigId id : node->rigs) {
        if (id < owner_.scene_.rigs.size() && !owner_.scene_.rigs[id].layers.empty()) {
            wanted = true;
            break;
        }
    }
    if (!wanted) {
        return; // no node authored a layer: the conversion below is not worth a matrix inverse
    }
    // ADR-551. Hoisted: the per-foot ground sampling below needs the node's world transform and
    // its inverse whether or not there is a look target, which the original condition did not
    // guarantee.
    const glm::mat4 world = owner_.nodeWorldTransform(*node).matrix();
    const glm::mat4 inverse = glm::inverse(world);
    const IGroundQuery& ground = owner_.groundQuery();
    glm::vec3 localTarget(0.0f);
    bool haveTarget = false;
    glm::vec3 localGround(0.0f);
    glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
    bool haveGround = false;
    if (state.hasLookTarget || state.hasGroundPlane) {
        if (state.hasLookTarget) {
            localTarget = glm::vec3(inverse * glm::vec4(state.lookTarget, 1.0f));
            haveTarget = true;
        }
        if (state.hasGroundPlane) {
            // ADR-359. The point goes through the inverse like any point. The **normal does not**:
            // a normal is a covector, so world-to-local for it is the transpose of the
            // local-to-world basis rather than the inverse of it. The two agree only for a pure
            // rotation, and Glowmere draws these bodies at 3.3x to 3.6x -- an error that is
            // invisible on a uniform scale and a hoof rotated into the hillside the moment a scene
            // squashes one axis. Normalised afterwards, because a scale leaves it unnormalised
            // either way.
            localGround = glm::vec3(inverse * glm::vec4(state.groundPoint, 1.0f));
            const glm::vec3 n = glm::transpose(glm::mat3(world)) * state.groundNormal;
            const float len = glm::length(n);
            localNormal = len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
            haveGround = true;
        }
    }
    const float reaction = std::clamp(state.reaction, 0.0f, 1.0f);

    // Phase B §4. Built here because this is the one function that already depends on both tiers,
    // and built ONCE per frame rather than per layer: the inverse above is a matrix inverse and the
    // conversions below are the same two lines every layer would otherwise repeat.
    //
    // Everything spatial goes to the entity's own frame, for the reason ADR-274 gives -- a posed
    // rig has no world position -- and the normal goes through the transpose rather than the
    // inverse, for the reason ADR-359 gives.
    motion_ = MotionContext{};
    motion_.dt = state.dt;
    motion_.time = state.time;
    motion_.velocity = glm::mat3(inverse) * state.velocity;
    motion_.facing = glm::normalize(glm::mat3(inverse) * state.facing);
    motion_.groundSpeed =
        std::sqrt((state.velocity.x * state.velocity.x) + (state.velocity.z * state.velocity.z));
    motion_.turnRate = state.turnRate;
    motion_.acceleration = glm::mat3(inverse) * state.acceleration;
    switch (state.phase) {
    case entity::LocomotionPhase::Idle: motion_.motionPhase = MotionPhase::Idle; break;
    case entity::LocomotionPhase::Starting: motion_.motionPhase = MotionPhase::Starting; break;
    case entity::LocomotionPhase::Moving: motion_.motionPhase = MotionPhase::Moving; break;
    case entity::LocomotionPhase::Stopping: motion_.motionPhase = MotionPhase::Stopping; break;
    case entity::LocomotionPhase::Turning: motion_.motionPhase = MotionPhase::Turning; break;
    case entity::LocomotionPhase::Strafing: motion_.motionPhase = MotionPhase::Strafing; break;
    }
    motion_.phaseStride = state.phaseStride;
    motion_.strafeAngle = state.strafeAngle;
    // The intent, as the polar pair the mover authored it in: a scalar along a heading. Converted
    // to a vector here so a layer never has to know which of the two forms the seam used.
    motion_.desiredFacing =
        glm::normalize(glm::mat3(inverse) * glm::vec3(std::sin(state.yaw), 0.0f, std::cos(state.yaw)));
    motion_.desiredVelocity = motion_.desiredFacing * state.speed;
    switch (state.activity) {
    case entity::Activity::Idle: motion_.mode = LocomotionMode::Idle; break;
    case entity::Activity::Walk: motion_.mode = LocomotionMode::Walk; break;
    case entity::Activity::Run: motion_.mode = LocomotionMode::Run; break;
    case entity::Activity::Turn: motion_.mode = LocomotionMode::Turn; break;
    default: motion_.mode = LocomotionMode::Other; break;
    }
    motion_.playbackRate = state.playbackRate;
    // What the playing clip was authored for, and how badly the body disagrees with it. Measured
    // across the shipping Glowmere scene, 97 of 100 mismatches are the stride out-running the body
    // and the median is 0.250 -- so this is the number a stride warper exists to drive to 1.
    motion_.authoredSpeed = state.activity == entity::Activity::Run ? entity_.desc().gait.runSpeed
                            : state.activity == entity::Activity::Walk
                                ? entity_.desc().gait.walkSpeed
                                : 0.0f;
    motion_.strideRatio =
        entity::Gait::footSlip(entity_.desc().gait, state.activity, state.speed);
    motion_.ground = &ground;
    // §40. Once per frame, across the body's own footprint, and converted into the rig's frame
    // like everything else here. `restHeight * 0.55` is a body's stance width rather than a
    // number: a footprint measured in metres would be wrong the moment a scene scales a character.
    {
        const glm::vec3 centre = glm::vec3(world[3]);
        const EnvironmentSample env =
            sampleEnvironment(ground, centre, std::max(motion_.restHeight * 0.55f, 0.1f));
        motion_.environment = env;
        motion_.environment.downhill =
            glm::length(env.downhill) > 1e-5f ? glm::normalize(glm::mat3(inverse) * env.downhill)
                                              : glm::vec3(0.0f);
    }
    motion_.worldFromLocal = world;
    motion_.localFromWorld = inverse;
    motion_.groundPoint = localGround;
    motion_.groundNormal = localNormal;
    motion_.hasGroundPlane = haveGround;
    motion_.lookTarget = localTarget;
    motion_.hasLookTarget = haveTarget;
    motion_.reaction = reaction;
    for (const RigId id : node->rigs) {
        if (id < owner_.scene_.rigs.size()) {
            // The body's own scale, so a layer's thresholds can be ratios (ADR-552).
            const Skeleton& sk = owner_.scene_.rigs[id].skeleton;
            if (!sk.joints.empty()) {
                Pose rest;
                std::vector<glm::mat4> restModel;
                setRestPose(sk, rest);
                poseToModel(sk, rest, restModel);
                float lo = restModel.front()[3].y;
                float hi = lo;
                for (const glm::mat4& m : restModel) {
                    lo = std::min(lo, m[3].y);
                    hi = std::max(hi, m[3].y);
                }
                motion_.restHeight = std::max(hi - lo, 1e-4f);
            }
            break;
        }
    }

    for (const RigId id : node->rigs) {
        if (id >= owner_.scene_.rigs.size()) {
            continue;
        }
        std::size_t layerIndex = 0;
        for (PoseLayer& layer : owner_.scene_.rigs[id].layers.layers()) {
            // This layer's index, taken BEFORE the increment. Reading `layerIndex` here instead
            // indexes the NEXT layer -- which silently gave the left foot the right foot's chain
            // and read one past the end for the right foot. The bounds check above is belt as well
            // as braces: `chains()` is parallel to `layers()` by construction, and a loop that
            // trusted that without checking is how this went unnoticed.
            const std::size_t thisLayer = layerIndex++;
            // Phase B §7. Every stride layer gets this frame's ratio whatever drives it,
            // including `Manual` -- the ratio is a measurement of the body, not an intent a
            // timeline authors, and a manual stride layer with no ratio would be a layer that
            // silently does nothing. One source for it: `MotionContext::strideRatio`, which is
            // `Gait::footSlip` (ADR-260).
            if (layer.kind == PoseLayerKind::Stride) {
                // Two independent reasons a step should be shorter, multiplied rather than
                // fought over: **how fast the body is travelling against its authored stride**
                // (`strideRatio`, `Gait::footSlip`), and **where it is in the arc of a movement**
                // (`phaseStride`, §8's ramp in and §9's brake out). A start at a quarter speed
                // wants both, and picking one would make the other invisible.
                layer.strideRatio = motion_.strideRatio * motion_.phaseStride;
                layer.bodySlope = motion_.environment.slope;
            }
            if (layer.kind == PoseLayerKind::Secondary) {
                layer.bodySpeed = motion_.groundSpeed;
            }
            if (layer.kind == PoseLayerKind::Lean) {
                layer.bodyAcceleration = motion_.acceleration;
                layer.bodyTurnRate = motion_.turnRate;
                layer.bodySlope = motion_.environment.slope;
                layer.bodyDownhill = motion_.environment.downhill;
            }
            // §49. Every layer on this node gets the node's seed and its own, whatever its drive
            // and whatever the scene authored -- a seed is an identity, not a setting. The
            // secondary layer is the only reader today; giving it to all of them is what makes
            // the next procedural layer deterministic without anyone remembering to wire it.
            //
            // Derived from names rather than from an index, because an index changes when someone
            // reorders the nodes in a scene file, and a render that changes because two characters
            // swapped places in a JSON array is exactly the kind of irreproducibility §49 exists
            // to prevent.
            layer.characterSeed = seedFromName(node_);
            layer.layerSeed = seedFromName(layer.name);

            switch (layer.drive) {
            case PoseLayerDrive::Manual:
                break;
            case PoseLayerDrive::Look:
                // Phase B §46. Not `haveTarget ? 1 : 0` any more. The vertical slice measured what
                // that costs: a layer arriving at full weight in one frame moved what it drives
                // 0.803 m between two frames, thirty-five times the distance the body covered in
                // the same frame, and it did so identically with the motion controller bypassed --
                // which is how it was clear the controller was not the thing at fault.
                //
                // The schedule comes off the seam rather than being accumulated here, because a
                // scrub poses the rig once at frame N with no frame N-1 to have ramped from
                // (ADR-557). `lookTargetSince` is a time; the realized weight is a pure function
                // of it and `now`, so seeking into the middle of a blend gives the middle of the
                // blend. `EntityWorld` may remember -- it re-simulates on a seek -- and this tier
                // may not.
                layer.weight = haveTarget ? 1.0f : 0.0f;
                layer.weightBefore = state.lookTargetBefore ? 1.0f : 0.0f;
                layer.blendElapsed =
                    static_cast<float>(std::max(state.time - state.lookTargetSince, 0.0));
                layer.blendSeconds = kLookBlendSeconds;
                layer.target = localTarget;
                // The target stays live while the weight fades *out*, or the layer reaches weight
                // 0.4 with nothing to aim at and resolves `NoTarget`, which is a snap wearing the
                // costume of a blend.
                layer.hasTarget = haveTarget || layer.effectiveWeight() > 0.0f;
                break;
            case PoseLayerDrive::Reaction:
                layer.weight = reaction;
                break;
            case PoseLayerDrive::Ground: {
                // The weight is the *grounded* bit and not a blend: a body standing on something
                // gets its feet planted, and a body in a tractor beam does not. A scene that wants
                // the correction eased in says so with a fade on the layer it authors, which is a
                // decision about staging rather than one this seam may make for it.
                layer.weight = haveGround ? 1.0f : 0.0f;
                layer.groundPoint = localGround;
                layer.groundNormal = localNormal;
                layer.hasGround = haveGround;
                // ADR-551: the ground under THIS foot, where the terrain can say.
                //
                // The body's plane above is one plane for the whole character, and a foot dropped
                // onto the plane its own body is standing on is reachable by construction -- so
                // foot IK on a single plane can never need the body to move, and never plants on
                // anything but an idealised surface. Measured: three aliens on 0.3499 m of relief
                // across their own footprint, and not one foot clamped or compensated.
                //
                // So each foot layer that can be placed asks the terrain under its own tip.
                // §14/§15. Which contact span this foot is in, and how far through it -- read
                // from the contact track the clip already carries (Phase A) rather than from a
                // detector run again here, because two answers to "is this foot down" is how
                // ADR-260 started.
                if (layer.kind == PoseLayerKind::Foot && layer.footLock > 0.0f) {
                    layer.inContact = false;
                    layer.bodyVelocity = motion_.velocity;
                    const SkinnedRig& rig = owner_.scene_.rigs[id];
                    const int stateIndex = rig.player.currentStateIndex();
                    if (stateIndex >= 0 &&
                        static_cast<std::size_t>(stateIndex) < rig.player.states().size()) {
                        const std::uint32_t clipIndex =
                            rig.player.states()[static_cast<std::size_t>(stateIndex)].clip;
                        if (clipIndex < rig.clipContacts.size() && clipIndex < rig.clips.size()) {
                            const std::vector<ContactTrack>& tracks = rig.clipContacts[clipIndex];
                            // Matched by the tip joint's NAME, not by layer order: the contact
                            // tracks are indexed by the node's `contacts` list and the layers by
                            // the node's `layers` list, and nothing makes those parallel. An
                            // off-by-one between two lists that merely look parallel is exactly
                            // the bug ADR-551 records.
                            for (const ContactTrack& track : tracks) {
                                if (track.joint != layer.chainTip) {
                                    continue;
                                }
                                const float local =
                                    rig.player.stateTime(rig.clips, state.time);
                                for (const ContactSpan& span : track.spans) {
                                    const float length = span.clipLength;
                                    const bool inside =
                                        span.wraps() ? (local >= span.start || local <= span.end)
                                                     : (local >= span.start && local <= span.end);
                                    if (!inside) {
                                        continue;
                                    }
                                    layer.inContact = true;
                                    layer.contactElapsed =
                                        span.wraps() && local <= span.end
                                            ? (length - span.start) + local
                                            : local - span.start;
                                    const float duration = span.duration();
                                    layer.contactRemaining =
                                        std::max(duration - layer.contactElapsed, 0.0f);
                                    break;
                                }
                                break;
                            }
                        }
                    }
                }
                if (haveGround && layer.kind == PoseLayerKind::Foot &&
                    thisLayer < owner_.scene_.rigs[id].layers.chains().size()) {
                    const glm::ivec3 chain = owner_.scene_.rigs[id].layers.chains()[thisLayer];
                    if (chain.z >= 0) {
                        // The tip's LAST posed position. `EntityWorld::update` runs a whole stage
                        // before the rigs are posed (ADR-274 §5), so this is one frame old -- the
                        // same frame of lag an attachment carries, and for the same reason. A foot
                        // moves a centimetre or two in 16.7 ms and the ground under it does not
                        // move at all, so the staleness costs a centimetre of horizontal sampling
                        // position rather than a centimetre of foot height.
                        const SkinnedRig& rig = owner_.scene_.rigs[id];
                        if (static_cast<std::size_t>(chain.z) < rig.pose.size()) {
                            std::vector<glm::mat4> model;
                            poseToModel(rig.skeleton, rig.pose, model);
                            const glm::vec3 tipLocal(model[static_cast<std::size_t>(chain.z)][3]);
                            const glm::vec3 tipWorld = glm::vec3(world * glm::vec4(tipLocal, 1.0f));
                            const GroundSample under = ground.sampleAt(tipWorld);
                            // An invalid sample is "no answer", not "no ground" -- off the edge of
                            // a height field, or a scene with no terrain at all. The body's plane
                            // remains, which is the behaviour every scene had before this.
                            if (under.valid) {
                                layer.groundPoint = glm::vec3(inverse * glm::vec4(under.point, 1.0f));
                                const glm::vec3 n = glm::transpose(glm::mat3(world)) * under.normal;
                                const float len = glm::length(n);
                                layer.groundNormal = len > 1e-6f ? n / len : localNormal;
                            }
                        }
                    }
                }
                break;
            }
            }
        }
    }
}

// ADR-337. The return half of the animation seam: what the clip this node is playing has carried
// the body by, so the entity can add it to `EntityState::travel` and the simulation can stop
// disagreeing with the drawing about where the body went.
//
// **Not a read of the posed rig**, which is what makes this possible at all. `EntityWorld::update`
// runs inside `Composition::updateBehaviour`, and the rigs are posed later, in
// `Composition::update` -- so an attachment reads the previous frame's pose (ADR-274 §5) and a
// root motion that had to read one would inherit the same 16.7 ms. It does not have to: root
// motion is a function of the *clip*, and the pose is another function of the same clip, so both
// can be taken at the same second from opposite sides of the frame and agree exactly.
//
// **Which position (ADR-260).** It reads neither. It reads the player and the clips, and reports
// a displacement in the rig's model space; the entity composes its yaw and the node's scale on,
// and the entity is what writes `travel`. That split is deliberate: this object can see the node
// and could have done the conversion, and if it had then `MotionAuthority::Simulation` would be
// exercised by a file in `scene/` that ADR-300 spent a unit making structurally unable to have it.
//
// **A node may carry several rigs.** The first one with an opted-in clip answers. Two rigs on one
// node both opting the same clip in would be two bodies' worth of displacement for one body, and
// there is no scene in this repository with more than one rig per character node.
entity::RootMotionSample Composition::AnimationSink::rootMotion(double now) const {
    entity::RootMotionSample out;
    const CompositionNode* node = owner_.findNode(node_);
    if (node == nullptr || node->rigs.empty()) {
        return out;
    }
    for (const RigId id : node->rigs) {
        if (id >= owner_.scene_.rigs.size()) {
            continue;
        }
        const SkinnedRig& rig = owner_.scene_.rigs[id];
        if (rig.rootMotion.empty()) {
            continue; // every rig in this repository but one leaves here
        }
        const RootMotionSample sample = rig.rootMotionAt(now);
        if (!sample.active) {
            continue;
        }
        out.displacement = sample.displacement;
        out.generation = sample.generation;
        out.active = true;
        return out;
    }
    return out;
}

// ADR-274. The first implementation of `entity::ISkeletonQuery` this engine has had, and therefore
// the first frame in which a socket has ever been able to mean a joint.
//
// **Model space, not the palette.** `SkinnedRig::palette` entry k is `model[palette[k]] *
// inverseBind[k]` -- what the GPU multiplies a bind-pose vertex by -- and its translation is not
// where the joint is. Reading a bone position out of it is the plausible-looking mistake ADR-260
// warns about, and it is off by the bind pose, which for a T-posed arm is most of the arm. So this
// re-derives the model-space matrices from `SkinnedRig::pose`, which is the local pose the player
// produced, through the same `poseToModel` that `skinningPalette` runs before it multiplies the
// inverse binds in.
//
// **Entity space, which is what model space is.** glTF bakes the file's chain from its scene root
// into the joints, so these matrices are already in the frame the entity's own transform places --
// and they have to be, because a rig is shared: `updateRigs` poses each rig once however many
// bodies carry it. `Entity::socketTransform` composes the placement on.
bool Composition::AnimationSink::jointTransform(std::string_view joint, scene::Transform& out) const {
    const CompositionNode* node = owner_.findNode(node_);
    if (node == nullptr || node->rigs.empty()) {
        return false;
    }
    for (const RigId id : node->rigs) {
        if (id >= owner_.scene_.rigs.size()) {
            continue;
        }
        const SkinnedRig& rig = owner_.scene_.rigs[id];
        const int index = rig.skeleton.find(joint);
        if (index < 0) {
            continue; // a rig that does not carry this joint is not this socket's rig
        }
        if (rig.pose.size() != rig.skeleton.jointCount()) {
            // Never posed -- a rig culled since the scene was built, or one whose player has not
            // run yet. Reporting false here is what makes `SocketResolution::EntityFrame` honest:
            // the answer is the entity frame and it says so, rather than a rest pose nobody is in.
            return false;
        }
        if (!modelValid_ || modelRig_ != id || modelVersion_ != rig.paletteVersion) {
            poseToModel(rig.skeleton, rig.pose, model_);
            modelRig_ = id;
            modelVersion_ = rig.paletteVersion;
            modelValid_ = true;
        }
        out = Transform::fromMatrix(model_[static_cast<std::size_t>(index)]);
        return true;
    }
    return false;
}

void Composition::cullEntityNodes() {
    if (viewportHeight_ == 0) {
        return;
    }
    const float aspect = static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_);
    const world::FrustumPlanes planes = world::frustumPlanes(scene_.camera.projection(aspect) * scene_.camera.view());
    // One description of the box, in `scene::entityCullBounds`. It was written out twice here --
    // once for EntityWorld-driven characters and once for authored mesh nodes -- and two copies of
    // a rule is two places for it to drift. It is also the only way to test the property that
    // matters: a posed skeleton may reach outside its bind-pose bounds, and a composition test
    // cannot reach a rig that does (a T-pose bind is wider than the poses it animates into).
    const auto cullEntity = [&](Entity& entity) {
        if (entity.mesh == kInvalidMesh || entity.mesh >= scene_.meshes.size() || entity.style == MeshStyle::Water) {
            return;
        }
        const CullBounds bounds = entityCullBounds(scene_, entity);
        entity.cameraCulled = !world::aabbVisible(planes, bounds.min, bounds.max);
    };
    for (const auto& entityPtr : entityWorld_.entities()) {
        const CompositionNode* node = findNode(entityPtr->desc().driven());
        if (node == nullptr) {
            continue;
        }
        const std::size_t index = static_cast<std::size_t>(node - nodes_.front().get());
        (void)index;
        for (std::size_t i = 0; i < nodes_.size() && i < ranges_.size(); ++i) {
            if (nodes_[i].get() != node) {
                continue;
            }
            const NodeRange& range = ranges_[i];
            for (std::size_t k = 0; k < range.entityCount && range.firstEntity + k < scene_.entities.size(); ++k) {
                Entity& e = scene_.entities[range.firstEntity + k];
                if (e.mesh == kInvalidMesh || e.mesh >= scene_.meshes.size()) {
                    e.cameraCulled = false;
                    continue;
                }
                const CullBounds bounds = entityCullBounds(scene_, e);
                e.cameraCulled = !world::aabbVisible(planes, bounds.min, bounds.max);
            }
            break;
        }
    }
    // EntityWorld-driven characters are handled above so their semantic node ranges remain
    // authoritative. Ordinary authored mesh nodes have no EntityWorld entry, but they still need
    // the same camera visibility contract for characters and static glTF objects.
    for (Entity& entity : scene_.entities) {
        cullEntity(entity);
    }
}

Result<void> Composition::setFields(std::vector<entity::FieldDesc> fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].name.empty()) {
            return fail("fields[{}] has no name", i);
        }
        for (std::size_t k = 0; k < i; ++k) {
            if (fields[k].name == fields[i].name) {
                return fail("field '{}' is declared twice", fields[i].name);
            }
        }
    }
    fieldDescs_ = std::move(fields);
    if (params_ != nullptr) {
        installEntities(); // a field binds against the same node table an entity does
    }
    return {};
}

void Composition::updateFields(const FrameTime& time, signals::SignalBus& bus,
                               params::Modulator& modulator) {
    if (params_ == nullptr || entityWorld_.empty()) {
        return;
    }
    entity::FieldUpdate update;
    update.time = time.renderTime;
    update.dt = time.deltaTime;
    update.bus = &bus;
    update.viewPosition = scene_.camera.position;
    update.distanceDetail = scene_.detailLimits.entityDistanceCull; // ADR-186
    const std::size_t signalsBefore = bus.size();
    entityWorld_.updateFields(update, *params_);
    if (!fieldRoutesChecked_) {
        fieldRoutesChecked_ = true;
        // A field knob is read before the routes run, deliberately (ADR-097), so a route pointed at
        // one is written and then wiped by the next frame's resetFinals() without ever having been
        // read. That is a reaction that does nothing and says nothing, which is the failure this
        // project has shipped five times -- so it says something.
        const std::string fieldPrefix = entityWorld_.fieldParameterPrefix();
        for (const params::ModRoute& route : modulator.routes()) {
            if (route.target.rfind(fieldPrefix, 0) != 0) {
                continue;
            }
            const std::string message = fmt::format(
                "composition '{}': modulation route '{}' -> '{}' cannot drive a field knob: the "
                "field pass runs before the routes so a field stays a pure function of time "
                "(ADR-097). Keyframe it on the timeline instead.",
                name_, route.source, route.target);
            log::warn("{}", message);
            entityWorld_.recordProblems({message});
        }
    }
    if (bus.size() != signalsBefore) {
        // A field published itself for the first time. Anything routed from `field.<name>.*` was
        // bound at load, when that signal did not exist yet, so it resolved to nothing -- and a
        // route that resolves to nothing and says nothing is the failure this project has shipped
        // five times. One re-bind, here, the same way a new control channel is handled.
        if (auto ok = modulator.bind(bus, *params_); !ok) {
            log::warn("composition '{}': after installing fields: {}", name_, ok.error().message);
        }
    }
    // §19's seam. A field does not add a reaction; it scales the depth of the ones the entity
    // already declared, so the same `audio.bass -> emissiveGain` an author wrote for a static prop
    // becomes spatial without a second reactivity system beside the first one.
    entityWorld_.applySpatialGain(modulator.routes());
}

void Composition::setPerformers(std::vector<Performer> performers) {
    performers_ = std::move(performers);
}

void Composition::applyPerformers(double now, double dt) {
    for (const Performer& p : performers_) {
        if (!p.pose) {
            continue;
        }
        entity::Entity* e = entityWorld_.find(p.entity);
        if (e == nullptr) {
            continue;
        }
        if (now >= p.from && now < p.to) {
            const PerformerPose pose = p.pose(now);
            entity::DirectorMotion motion;
            motion.active = true;
            motion.position = pose.position;
            if (pose.yawRadians) {
                motion.yaw = *pose.yawRadians;
                motion.hasYaw = true;
            }
            // ADR-820: an entry blend, from where the simulation had the body on the span's first
            // step to the authored performance. Captured into entity state (so a checkpoint carries
            // it) the first time a step lands inside the span; smoothstep, so the hand-over starts
            // and ends without a velocity step.
            if (p.entrySeconds > 0.0f) {
                entity::PerformanceEntry entry = e->performanceEntry();
                if (!entry.active) {
                    entry.active = true;
                    entry.position = e->state().position();
                    entry.yaw = e->state().yaw;
                    e->setPerformanceEntry(entry);
                }
                const float x = std::clamp(static_cast<float>((now - p.from) / p.entrySeconds), 0.0f, 1.0f);
                const float w = x * x * (3.0f - 2.0f * x);
                if (w < 1.0f) {
                    motion.position = glm::mix(entry.position, pose.position, w);
                    const float target = motion.hasYaw ? motion.yaw : entry.yaw;
                    const float delta = std::remainder(target - entry.yaw, 2.0f * std::numbers::pi_v<float>);
                    motion.yaw = entry.yaw + delta * w;
                    motion.hasYaw = true;
                }
            }
            motion.speed = pose.speed;
            motion.hasSpeed = true;
            motion.performance = true;
            motion.clipOwned = pose.clipOwned;
            motion.timeScale = pose.timeScale;
            e->setDirectorMotion(motion);
        } else if (now >= p.to && now - dt < p.to) {
            // The step the span ends on hands the body back -- where the performance left it, since
            // `travel` still says so -- and nothing else. A pure function of (now, dt), so a replay
            // that steps across the end releases on the same step a play does.
            e->setDirectorMotion(entity::DirectorMotion{});
            e->setPerformanceEntry(entity::PerformanceEntry{});
        }
    }
}

void Composition::updateBehaviour(const FrameTime& time, const signals::SignalBus& bus) {
    if (entityWorld_.empty() || params_ == nullptr) {
        return;
    }
    // The director decides first (ADR-209). It reads the action events the *previous* entity update
    // produced and writes the overrides and the director motions this one will execute, so the
    // whole thing is a pure function of the frame sequence and an order somebody chose rather than
    // an order that fell out of where the call happened to be.
    if (!stagingDesc_.empty()) {
        stage::StageContext stageCtx;
        stageCtx.time = time.renderTime;
        stageCtx.dt = time.deltaTime;
        stageCtx.world = &entityWorld_;
        stageCtx.params = params_;
        stageCtx.bus = &bus;
        // The flattened scene, so a step may ask where a node is *drawn* rather than only where the
        // entity arithmetic says it is. One frame old by construction -- see `visualPlacement`.
        stageCtx.visuals = this;
        // ADR-800: a frame at the instant a seek has just landed on is that instant again, not the
        // next one. The seek's replay already ran the director there -- `loadProject` seeks to zero
        // and its first frame is at zero; a render seeks to its start and its first frame is at
        // the start -- and a second pass at the same instant is one more decision than a play
        // makes. On the Glowmere film it entered the saucer's second beat at t = 0 instead of
        // t = 1/60, and every beat after it stayed a frame early: 0.34 m of saucer at 150 s. The
        // director writes parameter *bases*, so skipping it loses nothing the finals reset.
        const bool repeat = time.renderTime == seekLandedSeconds_;
        if (!repeat) {
            seekLandedSeconds_ = -1.0; // the playhead has left the instant; a later return is a new frame
            staging_.update(stageCtx);
            raiseDirectorBeats(time.renderTime);
        }
    }
    applyPerformers(time.renderTime, time.deltaTime); // ADR-758: after the director, before the step
    // ADR-245: what the camera director can see of the world's events, read straight after the
    // staging tick so a scenario that began this frame can claim this frame's cut.
    observeCameraEvents(time.renderTime);
    entity::EntityUpdate update;
    update.time = time.renderTime;
    update.dt = time.deltaTime;
    update.frameIndex = time.frameIndex;
    update.bus = &bus;
    update.viewPosition = scene_.camera.position;
    update.distanceDetail = scene_.detailLimits.entityDistanceCull; // ADR-186
    for (const auto& sink : animationSinks_) {
        sink->prepareChain();
    }
    entityWorld_.update(update, *params_);
}

void Composition::raiseDirectorBeats(double time) {
    // Phase D §26/§38: a director beat is a world event -- "abduction/beam" -- raised at the
    // scenario actor's body, so a character can hear the saucer start to beam and react to it
    // the way it reacts to anything else, and no character code knows a scenario exists. The
    // authored beats stay the director's; this only tells the world they happened.
    //
    // Not reproduced by a scrub: a seek resets the director (ADR-209) and restarts its run,
    // so a beat heard on a play is not heard on a replay, exactly as the scenario itself is not.
    for (const stage::StageEvent& event : staging_.events()) {
        if (event.kind != stage::StageEventKind::Beat) {
            continue;
        }
        entity::WorldEvent w;
        w.type = entityWorld_.eventType(event.scenario + "/" + event.beat);
        w.time = time;
        for (const stage::ScenarioDesc& scenario : stagingDesc_.scenarios) {
            if (scenario.name != event.scenario) {
                continue;
            }
            for (const stage::ActorDesc& actor : stagingDesc_.actors) {
                if (actor.name == scenario.actor) {
                    const auto& all = entityWorld_.entities();
                    for (std::size_t e = 0; e < all.size(); ++e) {
                        if (all[e]->name() == actor.driven()) {
                            w.position = all[e]->state().position();
                            w.source = e;
                        }
                    }
                }
            }
        }
        for (const entity::EntityWorld::EventProfile& profile : entityWorld_.eventProfiles()) {
            if (profile.name == entityWorld_.eventName(w.type)) {
                w.radius = profile.radius;
                w.magnitude = profile.magnitude;
            }
        }
        entityWorld_.emitEvent(w);
    }
}

// ---- ADR-671: a scrub that replays the director -----------------------------------------------

void Composition::ReplayPlacement::capture() {
    const Composition& c = comp_;
    const std::size_t count = std::min(c.nodes_.size(), c.ranges_.size());
    placed_.resize(count);
    valid_.assign(count, 0u);
    // The root fold `applyParameters` puts every node through, with the same arithmetic.
    const float rootScale = (c.rootScale_ != nullptr ? c.rootScale_->value() : 1.0f) +
                            (c.rootImpulse_ != nullptr ? c.rootImpulse_->value() : 0.0f);
    Transform root;
    root.rotation = glm::angleAxis(c.rootAngle_, glm::vec3(0.0f, 1.0f, 0.0f));
    root.scale = glm::vec3(rootScale);
    root.position = c.center_ - root.rotation * (c.center_ * rootScale);
    for (std::size_t i = 0; i < count; ++i) {
        const CompositionNode& node = *c.nodes_[i];
        const NodeRange& range = c.ranges_[i];
        const Transform full = compose(root, c.nodeWorldTransform(node));
        stage::VisualPlacement& out = placed_[i];
        out.origin = full.position;
        out.centre = out.origin;
        if (node.kind == NodeKind::Particles && range.particleIndex >= 0) {
            ParticleSystem ps = node.particleRest;
            applyParticleParameters(node.particleParams, node.particleRest, ps);
            out.centre = transformPoint(full, ps.position);
            valid_[i] = 1u;
            continue;
        }
        bool any = false;
        glm::vec3 lo(0.0f);
        glm::vec3 hi(0.0f);
        for (std::size_t k = 0; k < range.entityCount && k < range.restTransforms.size() &&
                                range.firstEntity + k < c.scene_.entities.size();
             ++k) {
            const Entity& entity = c.scene_.entities[range.firstEntity + k];
            if (entity.mesh >= c.scene_.meshes.size()) {
                continue;
            }
            const Transform world = compose(full, range.restTransforms[k]);
            const auto& [bmin, bmax] = c.scene_.meshBounds(entity.mesh);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 p((corner & 1) ? bmax.x : bmin.x, (corner & 2) ? bmax.y : bmin.y,
                                  (corner & 4) ? bmax.z : bmin.z);
                const glm::vec3 w = transformPoint(world, p);
                if (!any) {
                    lo = w;
                    hi = w;
                    any = true;
                } else {
                    lo = glm::min(lo, w);
                    hi = glm::max(hi, w);
                }
            }
        }
        if (any) {
            out.centre = (lo + hi) * 0.5f;
        }
        valid_[i] = 1u;
    }
}

bool Composition::ReplayPlacement::visualPlacement(std::string_view node,
                                                   stage::VisualPlacement& out) const {
    for (std::size_t i = 0; i < placed_.size() && i < comp_.nodes_.size(); ++i) {
        if (comp_.nodes_[i]->name == node) {
            if (i >= valid_.size() || valid_[i] == 0u) {
                return false; // nothing "flattened" yet: the play's frame zero answers false too
            }
            out = placed_[i];
            return true;
        }
    }
    return false;
}

namespace {

// ADR-700: the composition's half of a checkpoint. Everything the replay hooks read across steps:
// the director, whole, with the bases it had written, and the one-step-old placement it asks.
struct DirectorCheckpoint final : entity::HostCheckpoint {
    stage::Staging::Checkpoint staging;
    std::vector<stage::VisualPlacement> placed;
    std::vector<std::uint8_t> valid;
    world::HistoryBank::Snapshot history; // ADR-703: empty when nothing subscribes to HIST
    std::size_t measured = 0;
    [[nodiscard]] std::size_t bytes() const override { return measured; }
};

// ADR-703: the host half of a checkpoint for a scene with no director -- the transform history and
// nothing else, since without a director the replay has no other host state.
struct HistoryCheckpoint final : entity::HostCheckpoint {
    world::HistoryBank::Snapshot history;
    [[nodiscard]] std::size_t bytes() const override { return history.bytes() + sizeof(HistoryCheckpoint); }
};

// The checkpoint key with HIST's inputs folded in: which nodes are recorded, how deep, and the
// automation the replay re-applies. A new subscriber has no history in any checkpoint taken before
// it, so the set must drop.
std::uint64_t withHistoryKey(std::uint64_t key, const world::HistoryBank* bank) {
    if (bank == nullptr || bank->empty()) {
        return key; // exactly the key a scene with no HIST had
    }
    return key ^ (bank->key() + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2));
}

} // namespace

std::uint64_t Composition::replayInputKey() const {
    // What the director's replay reads that is not a parameter base (the bases are hashed by the
    // entity world): the staging description and registration, and the root fold and centre
    // `ReplayPlacement` composes every node through. A structural edit of the scene re-flattens,
    // and a flatten re-installs the navigator, which moves the entity world's own epoch.
    std::uint64_t h = 0x9e3779b97f4a7c15ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    const auto bits = [](float f) {
        std::uint32_t u = 0;
        std::memcpy(&u, &f, sizeof u);
        return static_cast<std::uint64_t>(u);
    };
    mix(staging_.epoch());
    mix(stagingDesc_.empty() ? 0u : 1u);
    for (const Performer& p : performers_) { // ADR-758: a changed performance is a different replay
        mix(p.signature);
    }
    mix(bits(rootAngle_));
    mix(bits(center_.x));
    mix(bits(center_.y));
    mix(bits(center_.z));
    mix(nodes_.size());
    mix(ranges_.size());
    mix(scene_.meshes.size());
    mix(scene_.entities.size());
    return h;
}

void Composition::seekWithDirector(double seconds, params::ParameterSet& params,
                                   entity::SeekBudget budget, double step) {
    staging_.reset(&entityWorld_, &params);
    seekPlacementLive_ = false;
    // ADR-703: HIST is replayed with the simulation. Cleared first -- a replay from zero starts with
    // no history, and a restore overwrites it -- then recorded after every replayed step and carried
    // in every checkpoint. With nothing subscribed none of this runs and the hooks are unchanged.
    world::HistoryBank* const history = historyBank_ != nullptr && !historyBank_->empty() ? historyBank_ : nullptr;
    if (history != nullptr) {
        history->clear();
    }
    if (stagingDesc_.empty()) {
        entity::EntityWorld::SeekHooks hooks;
        hooks.inputKey = withHistoryKey(replayInputKey(), history);
        if (!performers_.empty()) {
            // ADR-758: the director-less replay still has the one director a sequence brings.
            hooks.before = [this](double now, double dt) { applyPerformers(now, dt); };
        }
        if (history != nullptr) {
            hooks.after = [&](double now, double) {
                // The drawn transform a play's flattening would have used: finals rebuilt from the
                // bases, every body's offsets written on, recorded, and the finals put back as the
                // director-less replay keeps them (reset, never written) so the step after reads
                // exactly what it read before HIST existed.
                params.resetFinals();
                entityWorld_.applyAllOffsets();
                recordHistory(*history, now, history->automation());
                params.resetFinals();
            };
            hooks.capture = [&]() -> std::shared_ptr<const entity::HostCheckpoint> {
                auto c = std::make_shared<HistoryCheckpoint>();
                c->history = history->snapshot();
                return c;
            };
            hooks.restore = [&](const entity::HostCheckpoint& host) {
                history->restore(static_cast<const HistoryCheckpoint&>(host).history);
            };
        }
        entityWorld_.seek(seconds, &params, nullptr, step, budget, &hooks);
        return;
    }
    ReplayPlacement placement(*this);
    placement.capture();
    placement.invalidate(); // the play's first frame has no flattening to ask
    entity::EntityWorld::SeekHooks hooks;
    hooks.before = [&](double now, double dt) {
        // A play frame's top: finals rebuilt from bases, then the director (ADR-209), then the
        // entities. Routes and reactions are not replayed -- a scrub has no signal history, the
        // limit every seek in this engine already has.
        params.resetFinals();
        stage::StageContext ctx;
        ctx.time = now;
        ctx.dt = dt;
        ctx.world = &entityWorld_;
        ctx.params = &params;
        ctx.visuals = &placement;
        ctx.bus = nullptr;
        staging_.update(ctx);
        raiseDirectorBeats(now);
        applyPerformers(now, dt); // ADR-758: the same point of the step a play applies them at
    };
    hooks.after = [&](double now, double) {
        // The offsets a play's entity pass writes, and the "flattening" the next step's director
        // will ask about.
        entityWorld_.applyAllOffsets();
        placement.capture();
        // ADR-703: the same finals the placement just read, so HIST records what a play draws.
        if (history != nullptr) {
            recordHistory(*history, now, history->automation());
        }
    };
    hooks.capture = [&]() -> std::shared_ptr<const entity::HostCheckpoint> {
        auto c = std::make_shared<DirectorCheckpoint>();
        const std::uint64_t before = core::allocCounters().bytes;
        c->staging = staging_.checkpoint(params);
        c->placed = placement.placed();
        c->valid = placement.valid();
        if (history != nullptr) {
            c->history = history->snapshot();
        }
        c->measured = static_cast<std::size_t>(core::allocCounters().bytes - before) + sizeof(DirectorCheckpoint);
        return c;
    };
    hooks.restore = [&](const entity::HostCheckpoint& host) {
        const auto& c = static_cast<const DirectorCheckpoint&>(host);
        staging_.restore(c.staging, params);
        placement.set(c.placed, c.valid);
        if (history != nullptr) {
            history->restore(c.history);
        }
    };
    hooks.inputKey = withHistoryKey(replayInputKey(), history);
    entityWorld_.seek(seconds, &params, nullptr, step, budget, &hooks);
    seekPlaced_ = placement.placed();
    seekPlacedValid_ = placement.valid();
    seekPlacementLive_ = true;
    seekLandedSeconds_ = seconds;
}

Result<void> Composition::addMaterialProgram(MaterialProgram program) {
    if (auto v = program.validate(); !v) {
        return std::unexpected(v.error());
    }
    for (const auto& existing : materialPrograms_) {
        if (existing.name == program.name) {
            return fail("material program '{}' already exists", program.name);
        }
    }
    if (params_ != nullptr) {
        materialParams_.push_back(
            registerMaterialProgramParameters(*params_, program, "material/" + sanitise(prefix_) + program.name + "/"));
    }
    materialPrograms_.push_back(std::move(program));
    dirty_ = true;
    return {};
}

std::string Composition::uniqueName(const std::string& base) const {
    auto taken = [&](const std::string& candidate) {
        return std::any_of(nodes_.begin(), nodes_.end(),
                           [&](const std::unique_ptr<CompositionNode>& n) { return n->name == candidate; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2;; ++i) {
        const std::string candidate = base + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

Result<std::unique_ptr<Composition>> Composition::loadChild(const std::filesystem::path& asset) const {
    if (asset.empty()) {
        return fail("scene node needs an asset path");
    }
    const std::filesystem::path resolved = registry_.resolve(asset);
    if (resolved == sourcePath_ ||
        std::find(ancestors_.begin(), ancestors_.end(), resolved) != ancestors_.end()) {
        return fail("scene file includes itself: '{}'", resolved.string());
    }
    if (depth_ + 1 > kMaxNestingDepth) {
        return fail("scene '{}' nests deeper than {} levels", resolved.string(), kMaxNestingDepth);
    }
    std::vector<std::filesystem::path> chain = ancestors_;
    if (!sourcePath_.empty()) {
        chain.push_back(sourcePath_);
    }
    return loadNested(resolved, registry_, depth_ + 1, std::move(chain));
}

bool Composition::wouldCycle(const std::string& node, const std::string& parent) const {
    // Walk up from `parent`; reaching `node` (or looping longer than the node count) is a cycle.
    std::string current = parent;
    for (std::size_t guard = 0; !current.empty() && guard <= nodes_.size(); ++guard) {
        if (current == node) {
            return true;
        }
        const CompositionNode* p = findNode(current);
        if (p == nullptr) {
            return false; // unresolved parent: a root
        }
        current = p->parent;
    }
    return !current.empty();
}

const CompositionNode* Composition::nodeForEntity(std::size_t entityIndex) const {
    // Ranges are parallel to nodes_ and a node's entities are contiguous, so this is a scan over
    // nodes rather than over entities: tens of comparisons, on a click.
    for (std::size_t i = 0; i < ranges_.size() && i < nodes_.size(); ++i) {
        const NodeRange& range = ranges_[i];
        if (range.entityCount == 0) {
            continue;
        }
        if (entityIndex >= range.firstEntity && entityIndex < range.firstEntity + range.entityCount) {
            return nodes_[i].get();
        }
    }
    return nullptr;
}

const CompositionNode* Composition::nodeForProcedural(std::size_t proceduralIndex) const {
    // Same shape as nodeForEntity: ranges_ is parallel to nodes_, so this is a scan over nodes.
    for (std::size_t i = 0; i < ranges_.size() && i < nodes_.size(); ++i) {
        const NodeRange& range = ranges_[i];
        if (range.proceduralIndex < 0) {
            continue;
        }
        const auto first = static_cast<std::size_t>(range.proceduralIndex);
        // The node's own procedural, plus the one-per-extra-material run that follows it.
        if (proceduralIndex >= first && proceduralIndex <= first + range.proceduralSubCount) {
            return nodes_[i].get();
        }
    }
    // A terrain's scatter layers, which carry no `proceduralIndex` of their own because they are
    // emitted in a run rather than one per node. Checked second so an authored procedural that
    // happens to share an index range with nothing still wins on the first pass.
    for (std::size_t i = 0; i < ranges_.size() && i < nodes_.size(); ++i) {
        const NodeRange& range = ranges_[i];
        if (range.ecologyCount == 0) {
            continue;
        }
        if (proceduralIndex >= range.ecologyFirst &&
            proceduralIndex < range.ecologyFirst + range.ecologyCount) {
            return nodes_[i].get();
        }
    }
    return nullptr;
}

Result<void> Composition::setParent(const std::string& name, const std::string& parent) {
    CompositionNode* node = findNode(name);
    if (node == nullptr) {
        return fail("node '{}' not found", name);
    }
    if (!parent.empty()) {
        if (parent == name || wouldCycle(name, parent)) {
            return fail("node '{}': parent '{}' would form a cycle", name, parent);
        }
        if (findNode(parent) == nullptr) {
            log::warn("composition '{}': node '{}' parent '{}' not found (treated as root)", name_, name, parent);
        }
    }
    node->parent = parent;
    dirty_ = true;
    return {};
}

// ---- ADR-703: HIST ---------------------------------------------------------------------------------

void Composition::recordHistory(world::HistoryBank& bank, double seconds,
                                const world::HistoryAutomation* automation) const {
    // The root fold `applyParameters` and `ReplayPlacement::capture` put every node through, with
    // the same arithmetic, so a played sample and a replayed one are the same number.
    const float rootScale = (rootScale_ != nullptr ? rootScale_->value() : 1.0f) +
                            (rootImpulse_ != nullptr ? rootImpulse_->value() : 0.0f);
    Transform root;
    root.rotation = glm::angleAxis(rootAngle_, glm::vec3(0.0f, 1.0f, 0.0f));
    root.scale = glm::vec3(rootScale);
    root.position = center_ - root.rotation * (center_ * rootScale);
    for (std::size_t ring = 0; ring < bank.ringCount(); ++ring) {
        // The node's index, cached in the bank and checked by name: a replay is thirteen thousand
        // steps and a name search of every node at each is not free.
        std::size_t& slot = bank.hostSlot(ring);
        if (slot >= nodes_.size() || nodes_[slot]->name != bank.ringNode(ring)) {
            slot = nodes_.size();
            for (std::size_t i = 0; i < nodes_.size(); ++i) {
                if (nodes_[i]->name == bank.ringNode(ring)) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot >= nodes_.size()) {
            continue; // subscribed but not in this scene: an orphaned owner records nothing
        }
        const CompositionNode& node = *nodes_[slot];
        const Transform local = automation != nullptr ? automatedWorldTransform(node, *automation, seconds)
                                                      : nodeWorldTransform(node);
        const Transform full = compose(root, local);
        bank.record(ring, seconds, full.position, full.rotation, full.scale);
    }
}

Transform Composition::automatedWorldTransform(const CompositionNode& node, const world::HistoryAutomation& automation,
                                               double seconds) const {
    const auto localOf = [&](const CompositionNode& n) {
        Transform t = nodeTransform(n);
        glm::vec3 delta(0.0f);
        if (n.positionParam != nullptr && automation.transformDelta(*n.positionParam, seconds, delta)) {
            t.position = n.positionParam->value() + delta;
        }
        if (n.rotationParam != nullptr && automation.transformDelta(*n.rotationParam, seconds, delta)) {
            t.rotation = quatFromEulerDegrees(n.rotationParam->value() + delta);
        }
        if (n.scaleParam != nullptr && automation.transformDelta(*n.scaleParam, seconds, delta)) {
            t.scale = n.scaleParam->value() + delta;
        }
        return t;
    };
    Transform world = localOf(node);
    const CompositionNode* current = &node;
    for (std::size_t guard = 0; !current->parent.empty() && guard < nodes_.size(); ++guard) {
        const CompositionNode* parent = findNode(current->parent);
        if (parent == nullptr || parent == &node) {
            break;
        }
        world = compose(localOf(*parent), world);
        current = parent;
    }
    return world;
}

Transform Composition::nodeWorldTransform(const CompositionNode& node) const {
    Transform world = nodeTransform(node);
    const CompositionNode* current = &node;
    for (std::size_t guard = 0; !current->parent.empty() && guard < nodes_.size(); ++guard) {
        const CompositionNode* parent = findNode(current->parent);
        if (parent == nullptr || parent == &node) {
            break; // unresolved parent (root) or a cycle (edited in place): stop here
        }
        world = compose(nodeTransform(*parent), world);
        current = parent;
    }
    return world;
}

CompositionNode cloneNodeSpec(const CompositionNode& node) {
    CompositionNode copy;
    copy.name = node.name;
    copy.kind = node.kind;
    copy.asset = node.asset;
    copy.parent = node.parent;
    copy.transform = node.transform;
    copy.visible = node.visible;
    copy.locked = node.locked;
    copy.emissiveBoost = node.emissiveBoost;
    copy.roughnessScale = node.roughnessScale;
    copy.particles = node.particles;
    copy.procedural = node.procedural;
    copy.proceduralMaterialAuthored = node.proceduralMaterialAuthored;
    copy.field = node.field;
    copy.spline = node.spline;
    copy.sdf = node.sdf;
    copy.worldMap = node.worldMap;
    copy.terrain = node.terrain;
    copy.terrainMaterial = node.terrainMaterial;
    copy.ecology = node.ecology;
    copy.animation = node.animation;
    // Added after a duplicate came back missing them. The header above says this list is the list a
    // new authored field has to be added to, and five had not been: a duplicated City node lost its
    // settings and its tiling manifest and silently rebuilt itself from defaults, a duplicated
    // terrain lost how fast its water ran, a duplicated floating layer stopped floating, and a node
    // whose scene file wrote a `material` block came back claiming it had not.
    copy.waterFlow = node.waterFlow;
    copy.materialAuthored = node.materialAuthored;
    copy.city = node.city;
    copy.cityLibrary = node.cityLibrary;
    copy.floats = node.floats;
    // The live transform, not the one the node was born with: duplicating something you have just
    // moved has to duplicate it where it is now. The parameter is the value the flattened scene
    // uses, so it is the one that is true.
    if (node.positionParam != nullptr) {
        copy.transform.position = node.positionParam->base();
    }
    if (node.rotationParam != nullptr) {
        copy.transform.rotation = quatFromEulerDegrees(node.rotationParam->base());
    }
    if (node.scaleParam != nullptr) {
        copy.transform.scale = node.scaleParam->base();
    }
    return copy;
}

// The corners themselves, rather than the axis-aligned box `nodeBounds` folds them into.
//
// Two different questions, and the difference is a metre on a cow. "Does this fit inside a circle of
// radius R" asked of the box is asked of something up to its own diagonal larger than the body: a
// 3.6x farm animal is long and thin, and a yaw that is not a multiple of ninety degrees makes its
// box very much wider than it is. The corners are the same eight points `nodeBounds` transforms --
// this is that loop, stopping one step earlier.
std::vector<glm::vec3> Composition::nodeCorners(const std::string& name) {
    std::vector<glm::vec3> out;
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return out;
    }
    ensureBuilt();
    const auto index = static_cast<std::size_t>(std::distance(nodes_.begin(), it));
    if (index >= ranges_.size()) {
        return out;
    }
    const NodeRange& range = ranges_[index];
    for (std::size_t e = range.firstEntity;
         e < range.firstEntity + range.entityCount && e < scene_.entities.size(); ++e) {
        const Entity& entity = scene_.entities[e];
        if (entity.mesh >= scene_.meshes.size()) {
            continue;
        }
        const auto& [lo, hi] = scene_.meshBounds(entity.mesh);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                              (corner & 4) ? hi.z : lo.z);
            out.push_back(transformPoint(entity.transform, p));
        }
    }
    return out;
}

bool Composition::visualPlacement(std::string_view node, stage::VisualPlacement& out) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == node; });
    if (it == nodes_.end()) {
        return false; // the only false: there is no such node, and a caller must not use `out`
    }
    // The origin never needs the flattening. `nodeWorldTransform` walks the parent chain and reads
    // the parameter finals, which is exactly what the flattening will do with it -- so this half of
    // the answer is right on the first frame and on every frame, and a `Drawn` anchor on a parented
    // node degrades to "the node's own world origin" instead of to the entity's anchor, which for
    // anything parented is a point in the wrong space entirely (the tractor beam's would be the
    // world origin, two hundred metres from the saucer).
    const auto index = static_cast<std::size_t>(std::distance(nodes_.begin(), it));
    // The first frame after a seek: the flattening is from before the jump, and the replay's last
    // step is what a play would have flattened (ADR-700; see `seekPlaced_`).
    if (seekPlacementLive_ && !dirty_) {
        if (index < seekPlaced_.size() && index < seekPlacedValid_.size() && seekPlacedValid_[index] != 0u) {
            out = seekPlaced_[index];
            return true;
        }
        out.origin = glm::vec3(0.0f);
        out.centre = out.origin;
        return false;
    }
    // Everything below comes out of the last flattening and nothing out of the parameters, for the
    // reason written on `NodeRange::world`: the finals are reset at the top of the frame and the
    // director runs before the entity pass writes them back, so a parameter read here is the
    // authored number rather than the drawn one. One frame old and self-consistent beats current
    // and mixed.
    if (dirty_ || index >= ranges_.size() || !ranges_[index].worldValid) {
        // Nothing has been flattened, so there is no drawn position to report -- and this must be
        // **false**, not a best guess. `nodeWorldTransform` here would read the parameter finals,
        // which at director time have been reset to their authored bases, and hand back the
        // position the *file* was written with. Measured: `test_abduction_poc`'s frame loop omits
        // `Composition::update`, so with a best guess the lift flew a sheep 152 m across the valley
        // to the saucer's authored spot, and did it plausibly enough that only a wander test noticed.
        // A diagnostic that cannot say "I have no answer" is the failure this whole ADR is about.
        out.origin = glm::vec3(0.0f);
        out.centre = out.origin;
        return false;
    }

    const NodeRange& range = ranges_[index];
    out.origin = range.world.position;
    out.centre = out.origin;
    // A particle node: the emitter's own world point, which `applyParameters` already produced by
    // putting the system's node-local `position` through the node's world matrix.
    if (range.particleIndex >= 0 &&
        static_cast<std::size_t>(range.particleIndex) < scene_.particles.size()) {
        out.centre = scene_.particles[static_cast<std::size_t>(range.particleIndex)].position;
        return true;
    }
    // Otherwise the box the node's meshes occupy, from the *cached* mesh bounds through each scene
    // entity's world transform -- eight corners per mesh, which is the same arithmetic
    // `nodeCorners` does and the same arithmetic the renderer's own culling does.
    bool any = false;
    glm::vec3 lo(0.0f);
    glm::vec3 hi(0.0f);
    for (std::size_t e = range.firstEntity;
         e < range.firstEntity + range.entityCount && e < scene_.entities.size(); ++e) {
        const Entity& entity = scene_.entities[e];
        if (entity.mesh >= scene_.meshes.size()) {
            continue;
        }
        const auto& [bmin, bmax] = scene_.meshBounds(entity.mesh);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? bmax.x : bmin.x, (corner & 2) ? bmax.y : bmin.y,
                              (corner & 4) ? bmax.z : bmin.z);
            const glm::vec3 w = transformPoint(entity.transform, p);
            if (!any) {
                lo = w;
                hi = w;
                any = true;
            } else {
                lo = glm::min(lo, w);
                hi = glm::max(hi, w);
            }
        }
    }
    if (any) {
        out.centre = (lo + hi) * 0.5f;
    }
    return true;
}

bool Composition::nodeView(std::string_view node, world::NodeView& out) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == node; });
    if (it == nodes_.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(std::distance(nodes_.begin(), it));
    if (dirty_ || index >= ranges_.size() || !ranges_[index].worldValid) {
        return false; // nothing flattened: no drawn answer (see `visualPlacement`)
    }
    const NodeRange& range = ranges_[index];
    out = world::NodeView{};
    out.world = range.world.matrix();
    out.firstEntity = static_cast<std::uint32_t>(range.firstEntity);
    out.entityCount = static_cast<std::uint32_t>(range.entityCount);
    for (std::size_t e = range.firstEntity;
         e < range.firstEntity + range.entityCount && e < scene_.entities.size(); ++e) {
        const Entity& entity = scene_.entities[e];
        if (entity.mesh >= scene_.meshes.size()) {
            continue;
        }
        const auto& [bmin, bmax] = scene_.meshBounds(entity.mesh);
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? bmax.x : bmin.x, (corner & 2) ? bmax.y : bmin.y,
                              (corner & 4) ? bmax.z : bmin.z);
            const glm::vec3 w = transformPoint(entity.transform, p);
            if (!out.hasBounds) {
                out.boundsMin = w;
                out.boundsMax = w;
                out.hasBounds = true;
            } else {
                out.boundsMin = glm::min(out.boundsMin, w);
                out.boundsMax = glm::max(out.boundsMax, w);
            }
        }
    }
    // A procedural node (a scatter layer, or one placed asset such as Glowmere's `visitor` saucer)
    // draws through its own cloud rather than through entities, so the loop above saw nothing and an
    // effect attached to it had no bounds to fit or to exclude. The procedural's memoised TIGHT pair,
    // exactly as `nodeBounds` reads it (ADR-703: found by Space Warp bending its own saucer).
    // ADR-703 (FXL): the procedural range, so a lane effect can address the draws that are this
    // node -- the asset and each of its other material parts (`proceduralSubCount` follow it).
    if (range.proceduralIndex >= 0 && static_cast<std::size_t>(range.proceduralIndex) < scene_.procedurals.size()) {
        out.firstProcedural = static_cast<std::uint32_t>(range.proceduralIndex);
        out.proceduralCount = static_cast<std::uint32_t>(std::min<std::size_t>(
            range.proceduralSubCount + 1, scene_.procedurals.size() - out.firstProcedural));
    }
    if (!out.hasBounds && range.proceduralIndex >= 0) {
        const auto first = static_cast<std::size_t>(range.proceduralIndex);
        for (std::size_t p = first; p <= first + range.proceduralSubCount && p < scene_.procedurals.size(); ++p) {
            const ProceduralGeometry& pg = scene_.procedurals[p];
            if (!glm::all(glm::lessThanEqual(pg.tightMin, pg.tightMax)) || pg.tightMin == pg.tightMax) {
                continue;
            }
            out.boundsMin = out.hasBounds ? glm::min(out.boundsMin, pg.tightMin) : pg.tightMin;
            out.boundsMax = out.hasBounds ? glm::max(out.boundsMax, pg.tightMax) : pg.tightMax;
            out.hasBounds = true;
        }
    }
    return true;
}

WorldBounds Composition::nodeBounds(const std::string& name) {
    WorldBounds out;
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return out;
    }
    ensureBuilt(); // ranges_ indexes scene_.entities, and a dirty scene has neither
    const std::size_t index = static_cast<std::size_t>(std::distance(nodes_.begin(), it));
    if (index < ranges_.size()) {
        const NodeRange& range = ranges_[index];
        for (std::size_t e = range.firstEntity; e < range.firstEntity + range.entityCount && e < scene_.entities.size(); ++e) {
            const Entity& entity = scene_.entities[e];
            if (entity.mesh >= scene_.meshes.size()) {
                continue;
            }
            // The *cached* bounds, not MeshData::bounds(). That one scans every vertex, and this is
            // called for every node on every frame the brush is showing a ghost -- which on a
            // painted meadow is a few hundred thousand vertex reads per frame for an answer the
            // scene already memoised against its own mesh version.
            const auto& [lo, hi] = scene_.meshBounds(entity.mesh);
            // Eight corners through the entity's own transform: transforming min and max alone is
            // wrong the moment anything is rotated, and everything a brush places is rotated.
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                                  (corner & 4) ? hi.z : lo.z);
                out.include(transformPoint(entity.transform, p));
            }
        }
        // A procedural object (a scatter layer, a generated plant) draws through its own cloud
        // rather than through entities. Its instances are not enumerated here; the node's own
        // world position stands in, which is enough to find and grab it.
        if (!out.valid && range.proceduralIndex >= 0) {
            // The instances' own extent, which the procedural already memoised at rebuild time --
            // `boundsMin`/`boundsMax`, computed from the placed instance positions and the source's
            // radius, in the same world the rest of this function reports.
            //
            // This used to stand the node's origin in for them, with a one-metre box around it, on
            // the grounds that it was "enough to find and grab it". It is enough to *select*, and it
            // is not enough to *place a gizmo*: generated geometry is built in a local frame with
            // the node at its base, so the handles appeared metres away from the thing they move --
            // selecting a tree's foliage put the gizmo on the ground below it.
            //
            // Reading the cached pair rather than enumerating instances keeps this cheap enough for
            // the brush, which calls it for every node on every frame it shows a ghost.
            const auto first = static_cast<std::size_t>(range.proceduralIndex);
            for (std::size_t p = first; p <= first + range.proceduralSubCount && p < scene_.procedurals.size();
                 ++p) {
                const ProceduralGeometry& pg = scene_.procedurals[p];
                // The *tight* pair, not the cull pair. `boundsMin/Max` puts a sphere of the
                // source's diagonal around every instance so a cull test can never drop something
                // visible; used here it draws an eight-metre cube around a stem.
                if (glm::all(glm::lessThanEqual(pg.tightMin, pg.tightMax)) && pg.tightMin != pg.tightMax) {
                    out.include(pg.tightMin);
                    out.include(pg.tightMax);
                }
            }
        }
        if (!out.valid && range.proceduralIndex >= 0) {
            // Nothing placed, or a procedural whose bounds are degenerate. A box rather than a
            // point: a zero-volume AABB is one no ray ever enters, so a single `include` would
            // leave the object visible in a box selection and impossible to click.
            const glm::vec3 p = nodeWorldTransform(**it).position;
            out.include(p - glm::vec3(0.5f));
            out.include(p + glm::vec3(0.5f));
        }
    }
    // A group owns no geometry: it is the union of what hangs off it, recursively.
    if ((*it)->kind == NodeKind::Group) {
        for (const auto& other : nodes_) {
            if (other->parent == name) {
                out.include(nodeBounds(other->name));
            }
        }
        if (!out.valid) {
            // An empty group still has to be findable, or a group whose contents were all deleted
            // becomes an object that exists, saves, loads and cannot be selected or removed.
            const glm::vec3 p = nodeWorldTransform(**it).position;
            out.include(p - glm::vec3(0.5f));
            out.include(p + glm::vec3(0.5f));
        }
    }
    return out;
}

Result<CompositionNode*> Composition::addNode(CompositionNode node) {
    const std::string base = node.name.empty() ? std::string(nodeKindName(node.kind)) : sanitise(node.name);
    node.name = uniqueName(base);
    if (!node.parent.empty()) {
        if (node.parent == node.name || wouldCycle(node.name, node.parent)) {
            return fail("node '{}': parent '{}' would form a cycle", node.name, node.parent);
        }
        if (findNode(node.parent) == nullptr) {
            log::warn("composition '{}': node '{}' parent '{}' not found (treated as root)", name_, node.name,
                      node.parent);
        }
    }
    node.sceneAsset.reset();
    node.child.reset();
    node.positionParam = nullptr;
    node.rotationParam = nullptr;
    node.scaleParam = nullptr;
    node.visibleParam = nullptr;
    node.emissiveParam = nullptr;
    node.roughnessParam = nullptr;
    node.opacityParam = nullptr;
    node.particleParams = {};

    switch (node.kind) {
    case NodeKind::Gltf: {
        if (node.asset.empty()) {
            return fail("node '{}': gltf node needs an asset path", node.name);
        }
        auto asset = registry_.loadScene(node.asset);
        if (!asset) {
            return std::unexpected(asset.error());
        }
        node.sceneAsset = std::move(*asset);
        break;
    }
    case NodeKind::Scene: {
        auto child = loadChild(node.asset);
        if (!child) {
            return std::unexpected(child.error());
        }
        node.child = std::move(*child);
        break;
    }
    case NodeKind::Particles:
        node.particles.name = node.name;
        node.particleRest = node.particles;
        break;
    case NodeKind::Procedural:
        node.procedural.name = node.name;
        if (auto v = node.procedural.validate(); !v) {
            return std::unexpected(v.error());
        }
        node.proceduralRest = node.procedural;
        break;
    case NodeKind::Field:
        node.field.name = node.name;
        if (auto v = node.field.validate(); !v) {
            return std::unexpected(v.error());
        }
        node.fieldRest = node.field;
        break;
    case NodeKind::Spline:
        node.spline.name = node.name;
        if (auto v = node.spline.validate(); !v) {
            return std::unexpected(v.error());
        }
        node.splineRest = node.spline;
        break;
    case NodeKind::Sdf:
        node.sdf.name = node.name;
        if (auto v = node.sdf.validate(); !v) {
            return std::unexpected(v.error());
        }
        node.sdfRest = node.sdf;
        break;
    case NodeKind::Terrain:
        node.worldMap.name = node.worldMap.name.empty() ? node.name : node.worldMap.name;
        node.worldMap.prepare();
        if (auto v = node.worldMap.validate(); !v) {
            return std::unexpected(v.error());
        }
        if (auto v = node.terrain.validate(); !v) {
            return std::unexpected(v.error());
        }
        // A density naming a biome that does not exist places nothing, silently: a layer that was
        // authored, parsed, built and grew no plants is the worst outcome available, so it is an
        // error at load rather than an empty forest at render.
        if (auto v = node.ecology.validate(node.worldMap.biomes); !v) {
            return std::unexpected(v.error());
        }
        break;
    case NodeKind::Orb:
    case NodeKind::Grid:
    case NodeKind::Group:
        break;
    }

    nodes_.push_back(std::make_unique<CompositionNode>(std::move(node)));
    CompositionNode* added = nodes_.back().get();
    dirty_ = true;
    if (attached()) {
        registerNodeParameters(*added);
    }
    return added;
}

bool Composition::removeNode(const std::string& name) {
    return detachNode(name) != nullptr;
}

std::unique_ptr<CompositionNode> Composition::detachNode(const std::string& name) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return nullptr;
    }
    unregisterNodeParameters(**it);
    // The entity layer caches RAW POINTERS to a node's transform parameters (`positionParam_` and
    // its two siblings), and the line above has just destroyed them. Nothing else re-binds:
    // `Engine::rebind()` re-binds the modulator and the timeline and not the entity world, and
    // `installEntities()` is a rebuild-time call that a delete does not reach. So an entity driving
    // a deleted node kept a dangling pointer, and the next `updateBehaviour` wrote through it.
    //
    // Reported as "deleting objects crashes the app", and it is a heap-use-after-free: ASan names
    // `EntityWorld::update` reading a `Parameter<vec3>` freed by `ParameterSet::remove`. It survived
    // release builds because the freed block usually still held plausible floats -- the two crash
    // reports it did produce landed in unrelated AppKit timer code, which is what corruption looks
    // like from the outside.
    //
    // `bind()` resolves each pointer by *name* through the parameter set, so a path that has just
    // been removed resolves to nullptr and the entity is correctly left driving nothing.
    if (params_ != nullptr) {
        entityWorld_.bind(*params_, prefix_ + "entity/");
    }
    const std::string grandParent = (*it)->parent;
    std::unique_ptr<CompositionNode> taken = std::move(*it);
    nodes_.erase(it);
    for (auto& other : nodes_) {
        if (other->parent == name) {
            other->parent = grandParent; // children keep their local transforms under the grandparent
        }
    }
    dirty_ = true;
    return taken;
}

CompositionNode* Composition::findNode(const std::string& name) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    return it == nodes_.end() ? nullptr : it->get();
}

const CompositionNode* Composition::findNode(const std::string& name) const {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    return it == nodes_.end() ? nullptr : it->get();
}

// ---- parameters --------------------------------------------------------------------------------

void MaterialPartParameters::apply(Material& material) const {
    if (tint != nullptr) material.baseColor *= tint->value();
    if (emissiveColor != nullptr) {
        const glm::vec3 c = emissiveColor->value();
        if (c.r + c.g + c.b > 0.0f) {
            material.emissiveColor = c;
            // A part given a colour owns its emission outright: strength 1, then emissiveGain
            // from there. Anything else makes a part's brightness depend on the shared material's,
            // and the point of a part is to be driven separately from its neighbours -- a lamp on
            // the bass and a lens on the treble cannot share one intensity. The parts an author
            // leaves black keep the shared intensity, which is what makes a single route onto
            // `material/emissive` still read as "the whole object glowing".
            material.emissiveIntensity = 1.0f;
        }
    }
    if (emissiveGain != nullptr) material.emissiveIntensity *= emissiveGain->value();
    if (roughnessScale != nullptr) material.roughness = std::clamp(material.roughness * roughnessScale->value(), 0.0f, 1.0f);
    if (opacityScale != nullptr) material.opacity = std::clamp(material.opacity * opacityScale->value(), 0.0f, 1.0f);
}

float Composition::fitDistance() const {
    return radius_ / std::tan(kFitFovRadians * 0.5f) * 1.15f;
}

void Composition::attach(params::ParameterSet& params, params::Modulator& modulator,
                         const std::string& prefix) {
    // What the set held before this composition touched it. The difference at the end of `attach`
    // is exactly what this composition registered, and that is what `unregisterParameters` removes
    // -- see `registeredPaths_`.
    std::unordered_set<std::string> before;
    before.reserve(params.size() * 2);
    for (const params::IParameter* q : params.ordered()) {
        before.insert(q->path());
    }

    params_ = &params;
    modulator_ = &modulator;
    graphModulator_ = &modulator;
    prefix_ = prefix;
    if (dirty_) {
        rebuild(); // camera defaults are fitted to the flattened bounds
    }
    const bool root = prefix_.empty();
    const float fit = fitDistance();
    cameraDistance_ = &params.add(floatDesc(prefix_ + "camera/distance", cameraDistanceSetting_.value_or(fit),
                                            0.01f, 1000.0f, fit * 0.3f, fit * 3.0f));
    cameraHeight_ = &params.add(
        floatDesc(prefix_ + "camera/height", cameraHeightSetting_.value_or(center_.y + radius_ * 0.35f),
                  -1000.0f, 1000.0f, center_.y - radius_, center_.y + radius_ * 2.0f));
    cameraOrbitSpeed_ = &params.add(
        floatDesc(prefix_ + "camera/orbitSpeed", cameraOrbitSpeedSetting_, -3.0f, 3.0f, -1.0f, 1.0f));
    cameraFov_ =
        &params.add(floatDesc(prefix_ + "camera/fov", cameraFovSetting_, 5.0f, 120.0f, 20.0f, 90.0f));
    cameraMode_ = &params.add(params::ParamDesc<int>{.path = prefix_ + "camera/mode",
                                                     .defaultValue = cameraModeSetting_,
                                                     .hardMin = 0,
                                                     .hardMax = 2,
                                                     .softMin = 0,
                                                     .softMax = 2,
                                                     .label = "camera/mode (0 orbit, 1 free, 2 spline)"});
    const float reachCam = 10.0f * std::max(radius_, 1.0f);
    cameraPosition_ = &params.add(vec3Desc(prefix_ + "camera/position", cameraPositionSetting_, -1e4f, 1e4f, -reachCam, reachCam));
    cameraTarget_ = &params.add(vec3Desc(prefix_ + "camera/target", cameraTargetSetting_, -1e4f, 1e4f, -reachCam, reachCam));
    cameraSplineT_ = &params.add(floatDesc(prefix_ + "camera/splineT", 0.0f, -10.0f, 10.0f, 0.0f, 1.0f));
    cameraLookAhead_ = &params.add(floatDesc(prefix_ + "camera/lookAhead", 2.0f, -100.0f, 100.0f, 0.0f, 10.0f));
    cameraSplineOffset_ = &params.add(vec3Desc(prefix_ + "camera/splineOffset", glm::vec3(0.0f), -1e3f, 1e3f, -5.0f, 5.0f));
    // Camera shake (ADR-098). Ordinary parameters, so a beat drives the amplitude through an
    // ordinary modulation route and a sequence keys it like anything else; `start` carries the
    // second the impulse began so the decay is `now - start` rather than an accumulated timer.
    cameraShakeAmplitude_ =
        &params.add(floatDesc(prefix_ + "camera/shake/amplitude", 0.0f, 0.0f, 100.0f, 0.0f, 1.0f));
    cameraShakeFrequency_ =
        &params.add(floatDesc(prefix_ + "camera/shake/frequency", 9.0f, 0.01f, 200.0f, 0.5f, 30.0f));
    cameraShakeDecay_ =
        &params.add(floatDesc(prefix_ + "camera/shake/decay", 0.0f, 0.0f, 120.0f, 0.0f, 4.0f));
    cameraShakeRotation_ =
        &params.add(floatDesc(prefix_ + "camera/shake/rotation", 0.0f, 0.0f, 45.0f, 0.0f, 3.0f));
    cameraShakeStart_ =
        &params.add(floatDesc(prefix_ + "camera/shake/start", 0.0f, -1e6f, 1e6f, 0.0f, 600.0f));
    registerCameraChannels(params, reachCam);
    materialParams_.clear();
    for (const MaterialProgram& mp : materialPrograms_) {
        materialParams_.push_back(
            registerMaterialProgramParameters(params, mp, "material/" + sanitise(prefix_) + mp.name + "/"));
    }
    envIntensity_ =
        &params.add(floatDesc(prefix_ + "env/intensity", envIntensitySetting_, 0.0f, 20.0f, 0.0f, 4.0f));
    envRotation_ = &params.add(
        floatDesc(prefix_ + "env/rotation", envRotationSetting_, -6.2832f, 6.2832f, -3.1416f, 3.1416f));
    // Procedural sky (ADR-036). It is only consulted when the scene has no HDR environment map, so
    // leaving `enabled` on by default gives every existing scene image-based lighting for free.
    {
        const SkySettings& sky = skySetting_;
        skyEnabled_ = &params.add(boolDesc(prefix_ + "env/sky/enabled", sky.enabled));
        skyBackground_ = &params.add(boolDesc(prefix_ + "env/sky/background", sky.showBackground));
        auto colour = [](std::string path, glm::vec3 def) {
            params::ParamDesc<glm::vec3> d = vec3Desc(std::move(path), def, 0.0f, 32.0f, 0.0f, 2.0f);
            d.isColor = true;
            return d;
        };
        skyZenith_ = &params.add(colour(prefix_ + "env/sky/zenithColor", sky.zenithColor));
        skyHorizon_ = &params.add(colour(prefix_ + "env/sky/horizonColor", sky.horizonColor));
        skyGround_ = &params.add(colour(prefix_ + "env/sky/groundColor", sky.groundColor));
        skySunColor_ = &params.add(colour(prefix_ + "env/sky/sunColor", sky.sunColor));

        // ---- ADR-350: the day/night cycle, made reachable -------------------------------------
        //
        // All of this existed and none of it was a parameter. The cycle ran, and `cycleSeconds`,
        // `dayPhase`, the sun angles and every colour were editable only by hand-editing the scene
        // JSON -- no UI, no modulation, no keyframing, no scrub. ADR-225: a setting the application
        // does not keep is not a setting.
        {
            scene::DayNightSettings& dn = dayNight_;
            dn.applyDefaults(); // so the stops below have real values to become the defaults of
            DayNightParams& p = dayNightParams_;
            const std::string base = prefix_ + "env/dayNight/";
            p.enabled = &params.add(boolDesc(base + "enabled", dn.enabled));
            p.paused = &params.add(boolDesc(base + "paused", dn.paused));
            // Normalised 0..1 so that scrubbing it is obvious: 0 midnight, 0.25 sunrise, 0.5 noon,
            // 0.75 sunset. This is the one an author reaches for first.
            p.dayPhase = &params.add(floatDesc(base + "dayPhase", dn.manualPhase, 0.0f, 1.0f, 0.0f, 1.0f));
            p.cycleSeconds =
                &params.add(floatDesc(base + "cycleSeconds", dn.cycleSeconds, 1.0f, 86400.0f, 30.0f, 1200.0f));
            p.phaseOffset =
                &params.add(floatDesc(base + "phaseOffset", dn.phaseOffset, -1.0f, 1.0f, 0.0f, 1.0f));
            // Degrees, not radians. `sunPeakElevation` at 0.78 rad means nothing on a slider;
            // 45 degrees means something immediately. The settings stay in radians -- this is a
            // presentation choice at the panel, converted on the way in and out.
            constexpr float kRadToDeg = 57.2957795f;
            p.sunPeakElevation = &params.add(floatDesc(base + "sun/peakElevationDeg",
                                                       dn.sunPeakElevation * kRadToDeg, 0.0f, 90.0f, 5.0f, 85.0f));
            p.sunAzimuthAtDawn = &params.add(floatDesc(base + "sun/azimuthAtDawnDeg",
                                                       dn.sunAzimuthAtDawn * kRadToDeg, -360.0f, 360.0f, -180.0f, 180.0f));
            p.sunAzimuthSweep = &params.add(floatDesc(base + "sun/azimuthSweepDeg",
                                                      dn.sunAzimuthSweep * kRadToDeg, -360.0f, 360.0f, 0.0f, 360.0f));
            // The `*Scale` multipliers over the authored curves. These already existed as fields
            // and were simply unreachable, which is the whole of this defect in one line.
            p.sunIntensityScale =
                &params.add(floatDesc(base + "sun/intensityScale", dn.sunIntensityScale, 0.0f, 8.0f, 0.0f, 3.0f));
            p.moonIntensityScale =
                &params.add(floatDesc(base + "moon/intensityScale", dn.moonIntensityScale, 0.0f, 8.0f, 0.0f, 3.0f));
            p.starBrightnessScale = &params.add(
                floatDesc(base + "stars/brightnessScale", dn.starBrightnessScale, 0.0f, 8.0f, 0.0f, 3.0f));
            p.hdriIntensityScale = &params.add(
                floatDesc(base + "hdri/intensityScale", dn.hdriIntensityScale, 0.0f, 8.0f, 0.0f, 3.0f));
            p.glowInfluence =
                &params.add(floatDesc(base + "glow/influence", dn.glowInfluence, 0.0f, 2.0f, 0.0f, 1.0f));
            p.fogHorizonBlend =
                &params.add(floatDesc(base + "fog/horizonBlend", dn.fogHorizonBlend, 0.0f, 1.0f, 0.0f, 1.0f));
        }
        // ADR-350, the rest of the sweep: four more environment settings that the scene file could
        // set and nothing else could reach. `skybox` and `proceduralSkyBackground` in particular
        // decide what is drawn behind the world, which is not a thing to have to edit JSON for.
        showSkybox_ = &params.add(boolDesc(prefix_ + "env/skybox", showSkyboxSetting_));
        proceduralSkyBackground_ =
            &params.add(boolDesc(prefix_ + "env/proceduralSkyBackground", proceduralSkyBackgroundSetting_));
        lightFromEnvironment_ =
            &params.add(boolDesc(prefix_ + "env/lightFromEnvironment", lightFromEnvironmentSetting_));
        skyBloom_ = &params.add(floatDesc(prefix_ + "env/skyBloom", skyBloomSetting_, 0.0f, 1.0f, 0.0f, 1.0f));
        skyHaze_ = &params.add(floatDesc(prefix_ + "env/sky/haze", sky.hazeWidth, 0.001f, 4.0f, 0.02f, 1.0f));
        skySunIntensity_ = &params.add(
            floatDesc(prefix_ + "env/sky/sunIntensity", sky.sunIntensity, 0.0f, 200.0f, 0.0f, 40.0f));
        skySunSize_ = &params.add(
            floatDesc(prefix_ + "env/sky/sunSize", sky.sunAngularRadius, 0.001f, 1.5f, 0.005f, 0.3f));
        skySunGlow_ = &params.add(
            floatDesc(prefix_ + "env/sky/sunGlow", sky.sunGlowWidth, 0.001f, 3.0f, 0.02f, 1.0f));
        skyIntensity_ = &params.add(
            floatDesc(prefix_ + "env/sky/intensity", sky.intensity, 0.0f, 20.0f, 0.0f, 4.0f));
    }
    brightness_ = &params.add(floatDesc(prefix_ + "scene/brightness", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f));
    stylized_ = &params.add(boolDesc(prefix_ + "scene/stylized", stylizedSetting_));
    fogDensity_ = &params.add(floatDesc(prefix_ + "scene/fogDensity", fogDensitySetting_, 0.0f, 2.0f, 0.0f, 0.2f));
    keyLight_ = &params.add(floatDesc(prefix_ + "scene/keyLight", 1.0f, 0.0f, 10.0f, 0.0f, 3.0f));
    // ADR-055/ADR-360. `WindParams::active()` is `enabled && speed > 0`, and until ADR-360 only
    // `speed` was reachable -- the comment that used to sit here said "speed 0 is a genuine no-op:
    // active() is false", reading the gate as speed alone, which is how a field nobody could switch
    // on shipped. Both halves are parameters now. Speed 0 is still a genuine no-op, and so is
    // enabled false, which is the default for every scene that does not say otherwise, so
    // registering these moves no existing picture.
    // ADR-387: the vortex's parameters are gone from here. They were `scene/vortex/*` on the
    // environment -- a singleton, registered whether or not a scene had one, and reachable only
    // through the Tree panel. They are now `atmos/<name>/*` on an authored effect instance, which
    // is where the aurora and the comet already live, and they arrive with instances, an enable,
    // serialisation, the World Effects panel and modulation that this block had to hand-build.
    windEnabled_ = &params.add(boolDesc(prefix_ + "scene/wind/enabled", windSetting_.enabled));
    // ADR-377: the hard maximum is 1.6 and not 4.0, and that is a measurement rather than a
    // preference. The mesh deformation's usable range on a 138 m tree is bounded by LEAF SIZE: the
    // lean's amplitude times the gentle gradient of its own height/radius weights becomes metres
    // across a single leaf card, and the card smears. The twelve-shot review caught it at 3.4 and a
    // sweep puts the onset around 1.8; the soft range already stopped the slider at 1.5, so this
    // closes the other door -- a modulation route clamps to the hard range, and 4.0 let one drive
    // the canopy apart. Raising it needs a per-leaf frame the GLB does not carry, not a bigger
    // number here.
    windSpeed_ = &params.add(floatDesc(prefix_ + "scene/windSpeed", windSetting_.speed, 0.0f, 1.6f, 0.0f, 1.5f));
    windDirection_ = &params.add(
        floatDesc(prefix_ + "scene/windDirection", windSetting_.direction, -6.2832f, 6.2832f, -3.1416f, 3.1416f));
    // Gusts: fronts that travel downwind. Sharpness 1 is a smooth swell, 8 is distinct fronts with
    // calm between them.
    windGustAmount_ = &params.add(
        floatDesc(prefix_ + "scene/wind/gustAmount", windSetting_.gustAmount, 0.0f, 4.0f, 0.0f, 1.5f));
    windGustScale_ = &params.add(
        floatDesc(prefix_ + "scene/wind/gustScale", windSetting_.gustScale, 0.5f, 500.0f, 5.0f, 120.0f));
    windGustSpeed_ = &params.add(
        floatDesc(prefix_ + "scene/wind/gustSpeed", windSetting_.gustSpeed, 0.0f, 80.0f, 0.0f, 25.0f));
    windGustSharpness_ = &params.add(
        floatDesc(prefix_ + "scene/wind/gustSharpness", windSetting_.gustSharpness, 0.25f, 16.0f, 1.0f, 8.0f));
    // Turbulence turns the local direction rather than scaling it.
    windTurbulence_ = &params.add(
        floatDesc(prefix_ + "scene/wind/turbulence", windSetting_.turbulence, 0.0f, 3.1416f, 0.0f, 1.2f));
    windTurbulenceScale_ = &params.add(
        floatDesc(prefix_ + "scene/wind/turbulenceScale", windSetting_.turbulenceScale, 0.5f, 400.0f, 2.0f, 60.0f));
    windTurbulenceSpeed_ = &params.add(
        floatDesc(prefix_ + "scene/wind/turbulenceSpeed", windSetting_.turbulenceSpeed, 0.0f, 40.0f, 0.0f, 8.0f));
    // Regional variation: which part of the map is windier at all.
    windRegionScale_ = &params.add(
        floatDesc(prefix_ + "scene/wind/regionScale", windSetting_.regionScale, 1.0f, 2000.0f, 10.0f, 300.0f));
    windRegionAmount_ = &params.add(
        floatDesc(prefix_ + "scene/wind/regionAmount", windSetting_.regionAmount, 0.0f, 1.0f, 0.0f, 0.8f));
    windRegionDrift_ = &params.add(
        floatDesc(prefix_ + "scene/wind/regionDrift", windSetting_.regionDrift, 0.0f, 4.0f, 0.0f, 0.5f));
    // The fast rattle. Only its spatial scale is a property of the air; the rate a plant rings at
    // is the plant's own resonance (`MotionResponse::flutterOmega`).
    windFlutterScale_ = &params.add(
        floatDesc(prefix_ + "scene/wind/flutterScale", windSetting_.flutterScale, 0.05f, 200.0f, 0.5f, 20.0f));
    // Volumetric atmosphere (ADR-032). volumeDensity 0 keeps the pass off, so these are free
    // until someone turns them up; every one is an ordinary parameter, so audio, the timeline,
    // presets, OSC/MIDI and macros drive fog through the usual routes.
    volumeDensity_ = &params.add(
        floatDesc(prefix_ + "scene/volumeDensity", volumeSetting_.volumeDensity, 0.0f, 2.0f, 0.0f, 0.2f));
    fogHeight_ = &params.add(floatDesc(prefix_ + "scene/fogHeight", volumeSetting_.fogHeight, -1e4f, 1e4f,
                                       -20.0f, 40.0f));
    fogHeightFalloff_ = &params.add(floatDesc(prefix_ + "scene/fogHeightFalloff", volumeSetting_.fogHeightFalloff,
                                              0.0f, 10.0f, 0.0f, 1.0f));
    // ADR-568 (§7). Both hard-clamped to 0..1: `upper` is a fraction of the layer's density and
    // `curve` a blend weight, and a modulation route driving either outside that range would be
    // asking for a profile that is not a profile -- negative density, or an extrapolation past the
    // compact family into one whose integral this does not compute.
    fogUpperDensity_ = &params.add(floatDesc(prefix_ + "scene/fogUpperDensity", volumeSetting_.fogUpperDensity,
                                             0.0f, 1.0f, 0.0f, 1.0f));
    fogHeightCurve_ = &params.add(floatDesc(prefix_ + "scene/fogHeightCurve", volumeSetting_.fogHeightCurve,
                                            0.0f, 1.0f, 0.0f, 1.0f));
    volumeScattering_ = &params.add(floatDesc(prefix_ + "scene/volumeScattering", volumeSetting_.volumeScattering,
                                              0.0f, 20.0f, 0.0f, 4.0f));
    volumeAbsorption_ = &params.add(floatDesc(prefix_ + "scene/volumeAbsorption", volumeSetting_.volumeAbsorption,
                                              0.0f, 20.0f, 0.0f, 4.0f));
    volumeAnisotropy_ = &params.add(floatDesc(prefix_ + "scene/volumeAnisotropy", volumeSetting_.volumeAnisotropy,
                                              -0.95f, 0.95f, -0.9f, 0.9f));
    volumeNoise_ = &params.add(
        floatDesc(prefix_ + "scene/volumeNoise", volumeSetting_.volumeNoiseAmount, 0.0f, 4.0f, 0.0f, 1.0f));
    volumeNoiseScale_ = &params.add(floatDesc(prefix_ + "scene/volumeNoiseScale", volumeSetting_.volumeNoiseScale,
                                              0.0f, 10.0f, 0.0f, 1.0f));
    volumeNoiseSpeed_ = &params.add(floatDesc(prefix_ + "scene/volumeNoiseSpeed", volumeSetting_.volumeNoiseSpeed,
                                              -10.0f, 10.0f, -2.0f, 2.0f));
    volumeEmission_ = &params.add(
        floatDesc(prefix_ + "scene/volumeEmission", volumeSetting_.volumeEmission, 0.0f, 20.0f, 0.0f, 4.0f));
    volumeSteps_ = &params.add(params::ParamDesc<int>{.path = prefix_ + "scene/volumeSteps",
                                                      .defaultValue = volumeSetting_.volumeSteps,
                                                      .hardMin = 4,
                                                      .hardMax = 256,
                                                      .softMin = 8,
                                                      .softMax = 96,
                                                      .label = "scene/volumeSteps"});
    // ADR-570 (§20/§22). The hard maximum is 16 rather than the march's 256: this runs INSIDE the
    // march, once per light that lights the air, so its cost is multiplicative and a slider that
    // can reach 256 is a slider that can stall a frame by a factor of a hundred. The soft range
    // stops at 8, which ADR-570's measurement says is past the point where more steps change the
    // picture.
    volumeShadowSteps_ = &params.add(params::ParamDesc<int>{.path = prefix_ + "scene/volumeShadowSteps",
                                                            .defaultValue = volumeSetting_.volumeShadowSteps,
                                                            .hardMin = 0,
                                                            .hardMax = 16,
                                                            .softMin = 0,
                                                            .softMax = 8,
                                                            .label = "scene/volumeShadowSteps"});
    volumeShadowStrength_ = &params.add(floatDesc(prefix_ + "scene/volumeShadowStrength",
                                                  volumeSetting_.volumeShadowStrength, 0.0f, 8.0f, 0.0f, 2.0f));
    // ADR-573, the fog brief's §27: "where practical, local lights should interact with the fog ...
    // this could be a major visual upgrade for Glowmere and Tree of Life." The interaction has
    // existed since ADR-053 and reaches the shader through `glow.y`. What did not exist was any
    // way to ask for it: no parameter, so no panel row, no automation, no modulation route. Twelve
    // shipped scenes set it -- every Glowmere scene among them -- and every one of them had to be
    // hand-edited to do it.
    volumeLocalLights_ = &params.add(floatDesc(prefix_ + "scene/volumeLocalLights",
                                               volumeSetting_.volumeLocalLights, 0.0f, 8.0f, 0.0f, 2.0f));
    // ADR-573: the same, for how far the march goes. Thirty-two shipped scenes set it and none of
    // them could have done so from the editor. The hard maximum is generous because a scene whose
    // subject is kilometres of air legitimately wants one; the soft range is where a frame budget
    // survives.
    volumeMaxDistance_ = &params.add(floatDesc(prefix_ + "scene/volumeMaxDistance",
                                               volumeSetting_.volumeMaxDistance, 0.01f, 20000.0f,
                                               10.0f, 4000.0f));
    // ADR-574: ADR-058's coupling -- how much of the volumetric's mist layer the SURFACE fog
    // integrates. It had no parameter, on a recorded reason that turned out to be false about the
    // four lines it sits on (see the comment at the assignment below). The feature is live: the
    // shader reads it, the renderer uploads it, two GPU cases exercise it at 0 and 1, and nine
    // shipped scenes set it -- all of them by hand, because there was no row to move.
    fogHeightAmount_ = &params.add(floatDesc(prefix_ + "scene/fogHeightAmount",
                                             volumeSetting_.fogHeightAmount, 0.0f, 1.0f, 0.0f, 1.0f));
    // ADR-461. The soft range is the whole of 0..1 because the whole of it is usable and the
    // interesting end is the low one -- a slider whose useful region is in its first hair is the
    // `scene/windSpeed` defect this project has already fixed once.
    volumeJitter_ = &params.add(
        floatDesc(prefix_ + "scene/volumeJitter", volumeSetting_.volumeJitter, 0.0f, 1.0f, 0.0f, 1.0f));
    {
        auto fogDesc = vec3Desc(prefix_ + "scene/fogColor", fogColorSet_ ? fogColorSetting_ : scene_.environment.backgroundColor,
                                0.0f, 1.0f, 0.0f, 1.0f);
        fogDesc.isColor = true;
        fogColor_ = &params.add(fogDesc);
    }
    {
        // The painterly hemisphere (ADR-058). These were copied from the scene file rather than
        // registered, on the grounds that nothing animates them. That was true and is no longer
        // sufficient: they are the two values that decide how dark a stylized world is, so a
        // generated world's art direction has to be able to set them, and anything an art direction
        // sets should be as editable, saveable and routable as everything else it sets. The
        // defaults are the same values the scene struct carries, so a scene that says nothing is
        // unchanged.
        auto skyDesc = vec3Desc(prefix_ + "scene/styledSkyAmbient", volumeSetting_.styledSkyAmbient,
                                0.0f, 4.0f, 0.0f, 1.0f);
        skyDesc.isColor = true;
        styledSkyAmbient_ = &params.add(skyDesc);
        auto groundDesc = vec3Desc(prefix_ + "scene/styledGroundAmbient",
                                   volumeSetting_.styledGroundAmbient, 0.0f, 4.0f, 0.0f, 1.0f);
        groundDesc.isColor = true;
        styledGroundAmbient_ = &params.add(groundDesc);
    }
    // ADR-112's range is automatic unless the scene says otherwise. Registered unconditionally and
    // defaulted to the scene's own value, so a scene that never sets it gets 0 -- automatic -- and
    // the knob exists to be turned rather than to be discovered by editing a file.
    shadowRange_ = &params.add(
        floatDesc(prefix_ + "scene/shadowRange", volumeSetting_.shadowRange, 0.0f, 20000.0f, 0.0f, 1000.0f));
    gridIntensity_ = &params.add(floatDesc(prefix_ + "scene/gridIntensity", 0.6f, 0.0f, 4.0f, 0.0f, 2.0f));
    rootScale_ = &params.add(floatDesc(prefix_ + "root/scale", 1.0f, 0.05f, 8.0f, 0.2f, 3.0f));
    // Nested compositions do not spin on their own by default; the enclosing root does.
    rootRotationSpeed_ = &params.add(
        floatDesc(prefix_ + "root/rotationSpeed", 0.0f, -20.0f, 20.0f, -3.0f, 3.0f)); // worlds do not spin by default
    rootImpulse_ = &params.add(floatDesc(prefix_ + "root/impulse", 0.0f, 0.0f, 4.0f, 0.0f, 1.0f));
    if (lightRig_ && lightRigParams_.all.empty()) {
        lightRigParams_ = registerLightRigParameters(params, *lightRig_, prefix_);
    }
    registerAuthoredLightParameters(params);
    for (auto& node : nodes_) {
        registerNodeParameters(*node);
    }
    if (root) {
        addDefaultRoutes(modulator);
    } else {
        dirty_ = true; // nested particle systems were renamed for their parameter paths
    }
    // Last, because an entity binds against the parameters every node above has just registered
    // and against the material part names the rebuild resolved.
    installEntities();

    registeredPaths_.clear();
    for (const params::IParameter* q : params.ordered()) {
        if (!before.contains(q->path())) {
            registeredPaths_.push_back(q->path());
        }
    }
}

// ADR-358: the authored lights' live knobs. Every light gets intensity, colour and an enable;
// an aimed one also gets the two angles and, when it casts, its angular size and shadow strength.
//
// Every default is the light's own authored value, so registering these changes no picture -- the
// parameter set gains knobs whose value is what the file already said. That is what makes this
// safe to do for every scene rather than only for the one that asked: the brief's rule is that a
// behaviour which changes under everyone is not a fix, and a knob that starts where the file left
// it changes nothing until somebody turns it.
void Composition::registerAuthoredLightParameters(params::ParameterSet& params) {
    authoredLightParams_.assign(authoredLights_.size(), AuthoredLightParams{});
    for (std::size_t i = 0; i < authoredLights_.size(); ++i) {
        const PunctualLight& l = authoredLights_[i].light;
        // Keyed on the **id**, not the display name. That is the whole of the rename fix: a route
        // or a track bound to `lights/<id>/intensity` keeps resolving after the light is renamed,
        // because nothing it names has moved. A single place keying on the name again would
        // reintroduce the orphaning silently, which is what the rename regression test is for.
        const std::string base = prefix_ + "lights/" + authoredLightId(authoredLights_[i]) + "/";
        AuthoredLightParams& p = authoredLightParams_[i];
        p.enabled = &params.add(boolDesc(base + "enabled", l.enabled));
        // The soft range tops out at three times the authored value or ten, whichever is more, so
        // a key authored at 2.45 has somewhere to go and a practical at 0.05 is not stuck at the
        // bottom of a slider calibrated for a sun.
        p.intensity = &params.add(floatDesc(base + "intensity", l.intensity, 0.0f,
                                            std::max(l.intensity * 20.0f, 200.0f), 0.0f,
                                            std::max(l.intensity * 3.0f, 10.0f)));
        p.color = &params.add(vec3Desc(base + "color", l.color, 0.0f, 8.0f, 0.0f, 1.0f));
        const bool aimed = l.type == PunctualLight::Type::Directional || l.type == PunctualLight::Type::Spot;
        if (aimed) {
            float azimuth = 0.0f;
            float elevation = 0.0f;
            lightAngles(l.direction, azimuth, elevation);
            p.azimuth = &params.add(floatDesc(base + "azimuth", azimuth, -180.0f, 180.0f, -180.0f, 180.0f));
            p.elevation = &params.add(floatDesc(base + "elevation", elevation, -89.0f, 89.0f, -89.0f, 89.0f));
        }
        p.angularSize = &params.add(floatDesc(base + "angularSize", l.softness, 0.0f, 16.0f, 0.0f, 6.0f));
        p.shadowStrength = &params.add(floatDesc(base + "shadowStrength", l.shadowStrength, 0.0f, 1.0f, 0.0f, 1.0f));

        // Position is registered for every kind including Directional. A directional light's
        // position does not shade anything -- but it is where its gizmo is, and a sun you cannot
        // pick up and put down beside the thing it lights is a sun nobody can aim.
        p.position = &params.add(vec3Desc(base + "position", l.position, -100000.0f, 100000.0f,
                                          -500.0f, 500.0f));
        p.temperature = &params.add(floatDesc(base + "temperature", l.temperature, 1500.0f, 12000.0f,
                                              1500.0f, 12000.0f));
        p.tint = &params.add(floatDesc(base + "tint", l.tint, -1.0f, 1.0f, -1.0f, 1.0f));
        p.castsShadow = &params.add(boolDesc(base + "castsShadow", l.castsShadow));
        p.contactShadow = &params.add(boolDesc(base + "contactShadow", l.contactShadow));
        p.shadowBias = &params.add(floatDesc(base + "shadowBias", l.shadowBias, 0.0f, 1.0f, 0.0f, 0.02f));
        p.volumetric = &params.add(floatDesc(base + "volumetric", l.volumetricStrength, 0.0f, 4.0f, 0.0f, 1.0f));

        // Range means nothing to a directional light -- it is the inverse-square cutoff -- so it is
        // registered only where it does something. ADR-372's lesson is the opposite failure: a
        // control that reaches the GPU and is ignored. A knob that cannot move the picture should
        // not be in the panel at all.
        const bool local = l.type != PunctualLight::Type::Directional;
        if (local) {
            p.range = &params.add(floatDesc(base + "range", l.range, 0.0f, 100000.0f, 0.0f, 200.0f));
        }
        if (l.type == PunctualLight::Type::Spot) {
            // Degrees, matching the file and every other angle a person writes here.
            // Hard max 90, not 180, and the distinction is not cosmetic: a modulation route
            // clamps to the **hard** range, and `packLight` clamps both cone angles to pi/2 before
            // it packs them. A hard range of 180 would therefore have given a route half a
            // travel that cannot move the picture -- ADR-372's defect, arriving through the range
            // rather than through the shader.
            p.innerCone = &params.add(floatDesc(base + "innerCone", glm::degrees(l.innerConeAngle),
                                                0.0f, 90.0f, 0.0f, 90.0f));
            p.outerCone = &params.add(floatDesc(base + "outerCone", glm::degrees(l.outerConeAngle),
                                                0.0f, 90.0f, 0.0f, 90.0f));
        }
        // Only the area kinds have an extent. `emitterArea` is what converts a nits-over-the-emitter
        // intensity into the candela the shader wants, so these three are not cosmetic: changing a
        // Rect's width changes how much light it throws.
        const bool area = l.type == PunctualLight::Type::Rect || l.type == PunctualLight::Type::Disk ||
                          l.type == PunctualLight::Type::Tube || l.type == PunctualLight::Type::Sphere;
        if (area) {
            p.width = &params.add(floatDesc(base + "width", l.width, 0.0f, 1000.0f, 0.0f, 10.0f));
            p.height = &params.add(floatDesc(base + "height", l.height, 0.0f, 1000.0f, 0.0f, 10.0f));
            p.radius = &params.add(floatDesc(base + "radius", l.radius, 0.0f, 1000.0f, 0.0f, 10.0f));
        }
    }
}

void Composition::unregisterAuthoredLightParameters() {
    if (params_ == nullptr) {
        authoredLightParams_.clear();
        return;
    }
    for (const AuthoredLight& a : authoredLights_) {
        const std::string base = prefix_ + "lights/" + authoredLightId(a) + "/";
        // Every leaf any kind of light can register, not only the ones this light has: a light
        // whose *type* changed from Spot to Point would otherwise leave `outerCone` behind in the
        // set for ever, which is the stale-path defect ADR-358 named.
        for (const char* leaf : {"enabled", "intensity", "color", "azimuth", "elevation",
                                 "angularSize", "shadowStrength", "position", "range", "innerCone",
                                 "outerCone", "temperature", "tint", "width", "height", "radius",
                                 "castsShadow", "contactShadow", "shadowBias", "volumetric"}) {
            params_->remove(base + leaf);
        }
    }
    authoredLightParams_.clear();
}

// ADR-360. Hand every mesh inside a wind body the SAME origin and extent, measured from the body's
// combined world bounds rather than authored, so the tree keeps its wind when it is moved or
// rescaled and so no two of its meshes can disagree about where its root is. That sharing is the
// whole reason the deformation is continuous across the five GLBs the Tree of Life is made of; see
// `meshWindOffset` in shaders/common.wgsl.
//
// Bounds come from `Scene::meshBounds`, the caching accessor, never `MeshData::bounds()`: ADR-355
// is three call sites that used the uncached one and rescanned 39.9M vertices about five times a
// frame, for a 350 ms editor frame. This runs at bake, not per frame, and still uses the cache.
// ADR-370: the world-space bounds of everything a node contains, measured from the meshes that were
// actually baked. Shared by the wind body and the canopy emitter, because both are asking the same
// question -- "where is this tree" -- and asking it twice in two ways is how the two drift apart.
//
// `Scene::meshBounds` is the caching accessor; `MeshData::bounds()` scans every vertex and ADR-355
// is what that costs. This runs at bake rather than per frame and still uses the cache.
bool Composition::subtreeWorldBounds(std::size_t node, glm::vec3& lo, glm::vec3& hi,
                                     std::vector<std::size_t>* members) const {
    if (nodes_.size() != ranges_.size()) {
        return false;
    }
    std::unordered_map<std::string, std::size_t> byName;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        byName.emplace(nodes_[i]->name, i);
    }
    auto inside = [&](std::size_t n) {
        std::size_t cur = n;
        for (std::size_t hops = 0; cur != node && hops <= nodes_.size(); ++hops) {
            const std::string& parent = nodes_[cur]->parent;
            if (parent.empty()) {
                return false;
            }
            const auto it = byName.find(parent);
            if (it == byName.end()) {
                return false;
            }
            cur = it->second;
        }
        return cur == node;
    };
    lo = glm::vec3(std::numeric_limits<float>::max());
    hi = glm::vec3(std::numeric_limits<float>::lowest());
    for (std::size_t n = 0; n < nodes_.size(); ++n) {
        if (!inside(n)) {
            continue;
        }
        if (members != nullptr) {
            members->push_back(n);
        }
        const NodeRange& r = ranges_[n];
        for (std::size_t e = r.firstEntity; e < r.firstEntity + r.entityCount && e < scene_.entities.size(); ++e) {
            const Entity& entity = scene_.entities[e];
            if (entity.mesh == kInvalidMesh) {
                continue;
            }
            const auto& [bmin, bmax] = scene_.meshBounds(entity.mesh);
            const glm::mat4 m = entity.transform.matrix();
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1) != 0 ? bmax.x : bmin.x, (corner & 2) != 0 ? bmax.y : bmin.y,
                                      (corner & 4) != 0 ? bmax.z : bmin.z);
                const glm::vec3 world = glm::vec3(m * glm::vec4(local, 1.0f));
                lo = glm::min(lo, world);
                hi = glm::max(hi, world);
            }
        }
    }
    return lo.x <= hi.x;
}

// ADR-370: a particle node may say which node's canopy it sheds from, and the emitter box is then
// MEASURED rather than typed. The brief asks for leaves that "originate from the Tree canopy rather
// than from a generic box emitter", and the difference that matters is not the shape of the box --
// it is that a measured box follows the tree when the tree is moved, rescaled or swapped for
// another asset, and a typed one silently stops describing it.
void Composition::applyCanopyEmitters() {
    if (nodes_.size() != ranges_.size()) {
        return;
    }
    std::unordered_map<std::string, std::size_t> byName;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        byName.emplace(nodes_[i]->name, i);
    }
    // ADR-380: a particle system may say that its attractor IS the vortex, rather than carrying a
    // copy of the vortex's coordinates. The brief's §9 asks for the vortex's relationship to the
    // island to survive the island moving, and §11 makes particles entrained by the vortex the
    // scene's "visual storytelling mechanism" -- neither survives a hand-typed position that stops
    // describing the thing it was copied from. Same argument as the measured canopy emitter above.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Particles || !node.vortexAttractor) {
            continue;
        }
        // ADR-387: the vortex is an authored effect now, so this looks for the first live one
        // rather than reading a field off the environment. Still no scene knowledge: it binds to
        // whatever vortex the scene contains, and to nothing if it contains none.
        const world::Vortex* found = nullptr;
        for (const world::EffectInstance& e : effects_) {
            if (e.kind == world::EffectKind::Vortex && e.enabled && e.vortex.active()) {
                found = &e.vortex;
                break;
            }
        }
        if (found == nullptr) {
            continue;
        }
        const world::Vortex& vx = *found;
        const std::string full = sanitise(prefix_) + node.name;
        for (ParticleSystem& ps : scene_.particles) {
            if (ps.name != full && ps.name != node.name) {
                continue;
            }
            // The mouth, and a reach that covers it. The particles have to feel the pull well
            // before they arrive or they fall past it rather than spiralling in.
            ps.attractorPosition = vx.field.center;
            ps.attractorRadius = std::max(vx.field.radius * node.vortexReach, 1.0f);
            node.particleRest.attractorPosition = ps.attractorPosition;
            node.particleRest.attractorRadius = ps.attractorRadius;
        }
    }
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Particles || node.canopySource.empty()) {
            continue;
        }
        const auto it = byName.find(node.canopySource);
        if (it == byName.end()) {
            log::warn("particle node '{}': canopySource '{}' names no node in this scene; the "
                      "authored emitter box is used instead",
                      node.name, node.canopySource);
            continue;
        }
        glm::vec3 lo{0.0f};
        glm::vec3 hi{0.0f};
        if (!subtreeWorldBounds(it->second, lo, hi, nullptr)) {
            continue;
        }
        // The canopy is the upper part of the crown, not the whole tree: shedding from the trunk
        // would drop leaves out of the bark. `canopyFrom` is the fraction of the height the crown
        // starts at.
        const float from = std::clamp(node.canopyFrom, 0.0f, 0.95f);
        const float base = lo.y + (hi.y - lo.y) * from;
        const glm::vec3 centre(0.5f * (lo.x + hi.x), 0.5f * (base + hi.y), 0.5f * (lo.z + hi.z));
        const glm::vec3 half(0.5f * (hi.x - lo.x), 0.5f * (hi.y - base), 0.5f * (hi.z - lo.z));
        for (std::size_t p = 0; p < scene_.particles.size(); ++p) {
            if (scene_.particles[p].name != sanitise(prefix_) + node.name &&
                scene_.particles[p].name != node.name) {
                continue;
            }
            scene_.particles[p].position = centre;
            scene_.particles[p].extent = glm::max(half, glm::vec3(0.01f));
            if (node.particleRest.name == scene_.particles[p].name || true) {
                node.particleRest.position = centre;
                node.particleRest.extent = scene_.particles[p].extent;
            }
        }
    }
}

void Composition::applyWindBodies() {
    if (nodes_.size() != ranges_.size()) {
        return; // mid-rebuild; the next bake will do it
    }
    // Which node each node's parent is, by name, so a descendant walk is a loop rather than a tree.
    std::unordered_map<std::string, std::size_t> byName;
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        byName.emplace(nodes_[i]->name, i);
    }
    auto insideBody = [&](std::size_t node, std::size_t body) {
        for (std::size_t hops = 0; node != body && hops <= nodes_.size(); ++hops) {
            const std::string& parent = nodes_[node]->parent;
            if (parent.empty()) {
                return false;
            }
            const auto it = byName.find(parent);
            if (it == byName.end()) {
                return false;
            }
            node = it->second;
        }
        return node == body;
    };

    for (std::size_t body = 0; body < nodes_.size(); ++body) {
        const CompositionNode& owner = *nodes_[body];
        if (!owner.windAuthored) {
            continue;
        }
        (void)0;
        auto pick = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->value() : fallback;
        };
        Entity::WindBody w;
        w.strength = pick(owner.windStrengthParam, owner.wind.strength);
        w.trunk = pick(owner.windTrunkParam, owner.wind.trunk);
        w.branch = pick(owner.windBranchParam, owner.wind.branch);
        w.foliage = pick(owner.windFoliageParam, owner.wind.foliage);
        w.flutter = pick(owner.windFlutterParam, owner.wind.flutter);
        w.lag = pick(owner.windLagParam, owner.wind.lag);

        // First pass: the body's world bounds, over every mesh it contains.
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        std::vector<std::size_t> members;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            if (!insideBody(n, body)) {
                continue;
            }
            members.push_back(n);
            const NodeRange& r = ranges_[n];
            for (std::size_t e = r.firstEntity; e < r.firstEntity + r.entityCount && e < scene_.entities.size();
                 ++e) {
                const Entity& entity = scene_.entities[e];
                if (entity.mesh == kInvalidMesh) {
                    continue;
                }
                const auto& [bmin, bmax] = scene_.meshBounds(entity.mesh);
                const glm::mat4 m = entity.transform.matrix();
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 local((corner & 1) != 0 ? bmax.x : bmin.x, (corner & 2) != 0 ? bmax.y : bmin.y,
                                          (corner & 4) != 0 ? bmax.z : bmin.z);
                    const glm::vec3 world = glm::vec3(m * glm::vec4(local, 1.0f));
                    lo = glm::min(lo, world);
                    hi = glm::max(hi, world);
                }
            }
        }
        if (lo.x > hi.x) {
            continue; // the body contains no geometry; nothing to stamp
        }
        // The root is the centre of the footprint at the bottom of the body: where a trunk meets
        // the ground, which is the one point the deformation must hold still.
        w.origin = glm::vec3(0.5f * (lo.x + hi.x), lo.y, 0.5f * (lo.z + hi.z));
        w.height = std::max(hi.y - lo.y, 1e-3f);
        w.radius = std::max(std::max(0.5f * (hi.x - lo.x), 0.5f * (hi.z - lo.z)), 1e-3f);

        // ADR-376: the energy rides the same span as the wind, so it is stamped in the same walk.
        Entity::TreeEnergy te;
        {
            auto pk = [](const params::Parameter<float>* p, float fb) { return p != nullptr ? p->value() : fb; };
            const TreeEnergySettings& es = owner.energy;
            te.intensity = pk(owner.energyIntensityParam, es.intensity);
            te.pulseSpeed = pk(owner.energyPulseSpeedParam, es.pulseSpeed);
            te.pulseWidth = pk(owner.energyPulseWidthParam, es.pulseWidth);
            te.propagation = pk(owner.energyPropagationParam, es.propagation);
            te.root = pk(owner.energyRootParam, es.root);
            te.trunk = pk(owner.energyTrunkParam, es.trunk);
            te.branch = pk(owner.energyBranchParam, es.branch);
            te.canopy = pk(owner.energyCanopyParam, es.canopy);
            te.noiseAmount = pk(owner.energyNoiseParam, es.noiseAmount);
            te.noiseScale = es.noiseScale;
            te.noiseSpeed = es.noiseSpeed;
            te.bloom = pk(owner.energyBloomParam, es.bloom);
            te.shimmer = pk(owner.shimmerParam, es.shimmer);
            te.shimmerSpeed = pk(owner.shimmerSpeedParam, es.shimmerSpeed);
            te.shimmerScale = pk(owner.shimmerScaleParam, es.shimmerScale);
            te.shimmerVariation = es.shimmerVariation;
            te.colorNear = owner.energyColorNearParam != nullptr ? owner.energyColorNearParam->value() : es.colorNear;
            te.colorFar = owner.energyColorFarParam != nullptr ? owner.energyColorFarParam->value() : es.colorFar;
        }
        WindBodySpan span;
        span.node = body;
        for (std::size_t n : members) {
            const NodeRange& r = ranges_[n];
            const std::size_t last = std::min(r.firstEntity + r.entityCount, scene_.entities.size());
            if (last <= r.firstEntity) {
                continue;
            }
            span.entities.emplace_back(r.firstEntity, last - r.firstEntity);
            for (std::size_t e = r.firstEntity; e < last; ++e) {
                scene_.entities[e].wind = w;
                scene_.entities[e].energy = te;
            }
        }
        windBodies_.push_back(std::move(span));
    }
}

// The cheap half, run every frame: the amounts, not the bounds.
void Composition::refreshWindBodyAmounts() {
    for (const WindBodySpan& span : windBodies_) {
        if (span.node >= nodes_.size()) {
            continue;
        }
        const CompositionNode& owner = *nodes_[span.node];
        auto pick = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->value() : fallback;
        };
        const float strength = pick(owner.windStrengthParam, owner.wind.strength);
        const float trunk = pick(owner.windTrunkParam, owner.wind.trunk);
        const float branch = pick(owner.windBranchParam, owner.wind.branch);
        const float foliage = pick(owner.windFoliageParam, owner.wind.foliage);
        const float flutter = pick(owner.windFlutterParam, owner.wind.flutter);
        const float lag = pick(owner.windLagParam, owner.wind.lag);
        auto pk = [](const params::Parameter<float>* p, float fb) { return p != nullptr ? p->value() : fb; };
        const float eIntensity = pk(owner.energyIntensityParam, owner.energy.intensity);
        const float eShimmer = pk(owner.shimmerParam, owner.energy.shimmer);
        const float ePulseSpeed = pk(owner.energyPulseSpeedParam, owner.energy.pulseSpeed);
        const float ePropagation = pk(owner.energyPropagationParam, owner.energy.propagation);
        const float eBloom = pk(owner.energyBloomParam, owner.energy.bloom);
        for (const auto& [first, count] : span.entities) {
            for (std::size_t e = first; e < first + count && e < scene_.entities.size(); ++e) {
                Entity::WindBody& w = scene_.entities[e].wind;
                w.strength = strength;
                w.trunk = trunk;
                w.branch = branch;
                w.foliage = foliage;
                w.flutter = flutter;
                w.lag = lag;
                Entity::TreeEnergy& te2 = scene_.entities[e].energy;
                te2.intensity = eIntensity;
                te2.shimmer = eShimmer;
                te2.pulseSpeed = ePulseSpeed;
                te2.propagation = ePropagation;
                te2.bloom = eBloom;
            }
        }
    }
}

// ADR-375: give a node a wind body, or take it away, from the application.
//
// Until now `windAuthored` could only be set by hand-editing the scene file, so the wind was
// tunable from the UI and not CREATABLE from it -- which is the same defect ADR-360 was written
// about, one step earlier in the workflow. The parameters are structural, so this re-registers
// them and the caller has to rebind the modulator afterwards (`Engine::rebind`), exactly as the
// world-effect panel does for its own structural edits.
Result<void> Composition::setNodeDeformers(const std::string& name, std::vector<Deformer> stack) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const auto& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return fail("no node named '{}'", name);
    }
    CompositionNode& node = **it;
    if (node.kind != NodeKind::Procedural) {
        return fail("node '{}' is a {} and only a procedural object has a deformer stack", name,
                    nodeKindName(node.kind));
    }
    if (stack.size() > static_cast<std::size_t>(kMaxDeformers)) {
        return fail("procedural '{}': at most {} deformers (got {})", name, kMaxDeformers, stack.size());
    }
    // Validated through the object's own `validate()` rather than by re-stating its rules here,
    // which is the second list that goes stale. A stack the file format would refuse must not be
    // reachable through a panel either -- ADR-387's parameter path is three things at once, and a
    // structural edit is the fourth.
    {
        ProceduralGeometry probe = node.proceduralRest;
        probe.deformers = stack;
        if (auto v = probe.validate(); !v) {
            return v;
        }
    }

    unregisterNodeParameters(node);
    node.procedural.deformers = stack;
    node.proceduralRest.deformers = std::move(stack);
    registerNodeParameters(node);
    dirty_ = true;
    return {};
}

bool Composition::setNodeWindBody(const std::string& name, bool present) {
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const auto& n) { return n->name == name; });
    if (it == nodes_.end() || (*it)->windAuthored == present) {
        return false;
    }
    CompositionNode& node = **it;
    unregisterNodeParameters(node);
    node.windAuthored = present;
    if (present && node.wind.strength <= 0.0f) {
        // A body at strength 0 is the thing ADR-360 shipped by mistake and the owner reported as
        // "it looks unchanged". Somebody who presses "add wind" means it to do something.
        node.wind.strength = 1.0f;
    }
    registerNodeParameters(node);
    return true;
}

void Composition::registerNodeParameters(CompositionNode& node) {
    if (params_ == nullptr) {
        return;
    }
    const std::string base = prefix_ + "nodes/" + node.name + "/";
    const float reach = 10.0f * std::max(radius_, 0.1f);
    node.positionParam =
        &params_->add(vec3Desc(base + "position", node.transform.position, -1e4f, 1e4f, -reach, reach));
    node.rotationParam = &params_->add(
        vec3Desc(base + "rotation", eulerDegrees(node.transform.rotation), -360.0f, 360.0f, -360.0f, 360.0f));
    node.scaleParam =
        &params_->add(vec3Desc(base + "scale", node.transform.scale, 0.001f, 100.0f, 0.01f, 5.0f));
    node.visibleParam = &params_->add(boolDesc(base + "visible", node.visible));
    node.emissiveParam =
        &params_->add(floatDesc(base + "emissiveBoost", node.emissiveBoost, 0.0f, 50.0f, 0.0f, 8.0f));
    node.roughnessParam =
        &params_->add(floatDesc(base + "roughnessScale", node.roughnessScale, 0.0f, 2.0f, 0.0f, 2.0f));
    // ADR-385: the whole-node fade. Hard range [0,1] so nothing can ramp it past opaque.
    node.opacityParam =
        &params_->add(floatDesc(base + "opacity", node.opacityScale, 0.0f, 1.0f, 0.0f, 1.0f));
    // The lights the node's asset brought with it. A scale and a tint rather than absolute values,
    // because the asset's own numbers are the authored starting point and a scene should not have to
    // restate them to dim one lamp.
    node.lightIntensityParam =
        &params_->add(floatDesc(base + "lightIntensity", 1.0f, 0.0f, 20.0f, 0.0f, 4.0f));
    node.lightColorParam = &params_->add(vec3Desc(base + "lightColor", glm::vec3(1.0f), 0.0f, 4.0f, 0.0f, 1.0f));
    // ADR-360: the wind body, when this node declares one. Registered only on a node that does, so
    // a scene full of rocks does not grow six inert sliders each. Strength 0 is a genuine no-op and
    // is the default, so a node that declares `"wind": {}` still moves nothing until it is turned
    // up -- which is the point: "off" has to be somewhere a user can leave it.
    // ADR-376: the tree's emissive life. Registered when the node declares either the wind body or
    // the energy, because the two are authored together on the same group and somebody who has
    // given a tree wind is the person who will next want it to glow.
    if (node.energyAuthored || node.windAuthored) {
        const std::string e = base + "energy/";
        node.energyIntensityParam = &params_->add(floatDesc(e + "intensity", node.energy.intensity, 0.0f, 20.0f, 0.0f, 4.0f));
        node.energyPulseSpeedParam = &params_->add(floatDesc(e + "pulseSpeed", node.energy.pulseSpeed, 0.0f, 8.0f, 0.0f, 1.0f));
        node.energyPulseWidthParam = &params_->add(floatDesc(e + "pulseWidth", node.energy.pulseWidth, 0.01f, 1.0f, 0.02f, 0.8f));
        node.energyPropagationParam = &params_->add(floatDesc(e + "propagation", node.energy.propagation, -4.0f, 4.0f, 0.0f, 1.0f));
        node.energyRootParam = &params_->add(floatDesc(e + "root", node.energy.root, 0.0f, 4.0f, 0.0f, 2.0f));
        node.energyTrunkParam = &params_->add(floatDesc(e + "trunk", node.energy.trunk, 0.0f, 4.0f, 0.0f, 2.0f));
        node.energyBranchParam = &params_->add(floatDesc(e + "branch", node.energy.branch, 0.0f, 4.0f, 0.0f, 2.0f));
        node.energyCanopyParam = &params_->add(floatDesc(e + "canopy", node.energy.canopy, 0.0f, 4.0f, 0.0f, 2.0f));
        node.energyNoiseParam = &params_->add(floatDesc(e + "noise", node.energy.noiseAmount, 0.0f, 1.0f, 0.0f, 1.0f));
        node.energyBloomParam = &params_->add(floatDesc(e + "bloom", node.energy.bloom, 0.0f, 4.0f, 0.0f, 2.0f));
        node.shimmerParam = &params_->add(floatDesc(e + "shimmer", node.energy.shimmer, 0.0f, 4.0f, 0.0f, 1.0f));
        node.shimmerSpeedParam = &params_->add(floatDesc(e + "shimmerSpeed", node.energy.shimmerSpeed, 0.0f, 2.0f, 0.0f, 0.5f));
        node.shimmerScaleParam = &params_->add(floatDesc(e + "shimmerScale", node.energy.shimmerScale, 0.001f, 1.0f, 0.005f, 0.2f));
        node.energyColorNearParam = &params_->add(vec3Desc(e + "colorNear", node.energy.colorNear, 0.0f, 8.0f, 0.0f, 2.0f));
        node.energyColorFarParam = &params_->add(vec3Desc(e + "colorFar", node.energy.colorFar, 0.0f, 8.0f, 0.0f, 2.0f));
    }
    if (node.windAuthored) {
        const std::string w = base + "wind/";
        node.windStrengthParam = &params_->add(floatDesc(w + "strength", node.wind.strength, 0.0f, 8.0f, 0.0f, 2.0f));
        node.windTrunkParam = &params_->add(floatDesc(w + "trunk", node.wind.trunk, 0.0f, 4.0f, 0.0f, 1.0f));
        node.windBranchParam = &params_->add(floatDesc(w + "branch", node.wind.branch, 0.0f, 4.0f, 0.0f, 2.0f));
        node.windFoliageParam = &params_->add(floatDesc(w + "foliage", node.wind.foliage, 0.0f, 4.0f, 0.0f, 2.0f));
        node.windFlutterParam = &params_->add(floatDesc(w + "flutter", node.wind.flutter, 0.0f, 4.0f, 0.0f, 2.0f));
        node.windLagParam = &params_->add(floatDesc(w + "lag", node.wind.lag, 0.0f, 4.0f, 0.0f, 1.5f));
    }
    if (node.kind == NodeKind::Particles) {
        // registerParticleParameters has no prefix: fold it into the system name instead
        // ("particles/nodes_a_sparks/..." for node "sparks" inside node "a").
        node.particleRest.name = sanitise(prefix_) + node.name;
        node.particleParams = registerParticleParameters(*params_, node.particleRest);
    }
    if (node.kind == NodeKind::Procedural) {
        node.proceduralParams =
            registerProceduralParameters(*params_, node.proceduralRest, "procedural/" + sanitise(prefix_) + node.name + "/");
        node.materialPartParams.clear();
        if (!node.proceduralSubRest.empty()) {
            for (std::size_t part = 0; part <= node.proceduralSubRest.size(); ++part) {
                const std::string partBase = "procedural/" + sanitise(prefix_) + node.name +
                                             "/parts/" + std::to_string(part) + "/";
                MaterialPartParameters controls;
                controls.tint = &params_->add(vec3Desc(partBase + "tint", glm::vec3(1.0f), 0.0f, 4.0f, 0.0f, 1.0f));
                controls.emissiveGain = &params_->add(floatDesc(partBase + "emissiveGain", 1.0f, 0.0f, 50.0f, 0.0f, 2.0f));
                controls.roughnessScale = &params_->add(floatDesc(partBase + "roughnessScale", 1.0f, 0.0f, 4.0f, 0.0f, 2.0f));
                controls.opacityScale = &params_->add(floatDesc(partBase + "opacityScale", 1.0f, 0.0f, 1.0f, 0.0f, 1.0f));
                params::ParamDesc<glm::vec3> emissive =
                    vec3Desc(partBase + "emissiveColor", glm::vec3(0.0f), 0.0f, 64.0f, 0.0f, 4.0f);
                emissive.isColor = true;
                controls.emissiveColor = &params_->add(std::move(emissive));
                node.materialPartParams.push_back(controls);
            }
        }
    }
    if (node.kind == NodeKind::Field) {
        node.fieldParams = registerFieldParameters(*params_, node.fieldRest, "field/" + sanitise(prefix_) + node.name + "/");
    }
    if (node.kind == NodeKind::Spline) {
        node.splineParams =
            registerSplineParameters(*params_, node.splineRest, "spline/" + sanitise(prefix_) + node.name + "/");
    }
    if (node.kind == NodeKind::Sdf) {
        node.sdfParams = registerSdfParameters(*params_, node.sdfRest, "sdf/" + sanitise(prefix_) + node.name + "/");
    }
    if (node.kind == NodeKind::Terrain) {
        // Four knobs, all of them for looking at the thing rather than art-directing it: turn LOD
        // off to see whether a shading artefact is a level boundary, turn culling off to see what
        // culling was removing, and pull the two distances to find where the budget goes.
        node.terrainLodParam = &params_->add(boolDesc(base + "terrainLod", true));
        node.terrainCullParam = &params_->add(boolDesc(base + "terrainCull", true));
        node.terrainLodDistanceParam = &params_->add(
            floatDesc(base + "terrainLodDistance", node.terrain.lodDistance, 4.0f, 4000.0f, 20.0f, 400.0f));
        node.terrainViewDistanceParam = &params_->add(
            floatDesc(base + "terrainViewDistance", node.terrain.viewDistance, 8.0f, 20000.0f, 50.0f, 2000.0f));
        // ADR-099 §15: the water's modulation surface. Ordinary parameters, so `routes` in a
        // project reaches them through the ProcessorChain every other reactive property uses --
        // attack, decay, curve, threshold, depth -- rather than through a second mapping layer, and
        // so the editor gets sliders for them for nothing. Tasteful is the artist's business: what
        // this decides is only that these six things are the ones worth moving.
        const world::WaterSettings& w = node.terrain.water;
        node.waterGlowParam = &params_->add(floatDesc(base + "water/glow", w.glow, 0.0f, 100.0f, 0.0f, 4.0f));
        node.waterSparkleParam =
            &params_->add(floatDesc(base + "water/sparkle", w.sparkle, 0.0f, 100.0f, 0.0f, 4.0f));
        node.waterRippleParam =
            &params_->add(floatDesc(base + "water/ripple", w.ripple, 0.0f, 20.0f, 0.0f, 3.0f));
        node.waterFlowSpeedParam =
            &params_->add(floatDesc(base + "water/flowSpeed", w.rippleSpeed, 0.0f, 100.0f, 0.0f, 4.0f));
        node.waterSwellParam = &params_->add(floatDesc(base + "water/swell", w.swell, 0.0f, 100.0f, 0.0f, 1.5f));
        node.waterFoamParam = &params_->add(floatDesc(base + "water/foam", w.foam, 0.0f, 10.0f, 0.0f, 2.0f));
        node.waterGlowColorParam =
            &params_->add(vec3Desc(base + "water/glowColor", w.glowColor, 0.0f, 20.0f, 0.0f, 1.0f));
        // ADR-350: the nine the water-world spec names, which were all unreachable. `clarity` and
        // `absorption` are the same idea from two ends -- metres of water the bed stays visible
        // through -- so this exposes clarity and lets the spec's "Absorption" be its reciprocal
        // rather than shipping two controls that fight.
        node.waterClarityParam =
            &params_->add(floatDesc(base + "water/clarity", w.clarity, 0.01f, 200.0f, 0.2f, 20.0f));
        node.waterMaxOpacityParam =
            &params_->add(floatDesc(base + "water/maxOpacity", w.maxOpacity, 0.0f, 1.0f, 0.5f, 1.0f));
        node.waterFresnelParam =
            &params_->add(floatDesc(base + "water/fresnel", w.fresnel, 0.0f, 1.0f, 0.0f, 0.5f));
        node.waterReflectionParam =
            &params_->add(floatDesc(base + "water/reflection", w.reflection, 0.0f, 32.0f, 0.0f, 8.0f));
        node.waterRoughnessParam =
            &params_->add(floatDesc(base + "water/roughness", w.roughness, 0.0f, 1.0f, 0.0f, 0.4f));
        node.waterRefractionParam =
            &params_->add(floatDesc(base + "water/refraction", w.refraction, 0.0f, 4.0f, 0.0f, 1.0f));
        node.waterRippleScaleParam =
            &params_->add(floatDesc(base + "water/rippleScale", w.rippleScale, 0.001f, 4.0f, 0.005f, 1.0f));
        node.waterShallowDepthParam =
            &params_->add(floatDesc(base + "water/shallowDepth", w.shallow, 0.01f, 200.0f, 0.2f, 20.0f));
        node.waterShallowColorParam =
            &params_->add(vec3Desc(base + "water/shallowColor", w.shallowColor, 0.0f, 8.0f, 0.0f, 1.0f));
        node.waterDeepColorParam =
            &params_->add(vec3Desc(base + "water/deepColor", w.deepColor, 0.0f, 8.0f, 0.0f, 1.0f));
    }
    if (node.kind == NodeKind::Scene && node.child) {
        node.child->attach(*params_, *modulator_, base);
    }
}

void Composition::unregisterNodeParameters(CompositionNode& node) {
    if (params_ != nullptr) {
        const std::string base = prefix_ + "nodes/" + node.name + "/";
        for (const char* suffix :
             {"position", "rotation", "scale", "visible", "emissiveBoost", "roughnessScale",
              "opacity",
              // Registered by `registerNodeParameters` for every node and absent here, so a node
              // carrying a light left these two behind when it was deleted. Same failure as the
              // particle visitor above: an exact list (ADR-207 is right that it must be exact) that
              // nothing keeps in step with the registrar.
              "lightIntensity", "lightColor"}) {
            params_->remove(base + suffix);
        }
        // ADR-375: the wind body's leaves, by their exact paths. A suffix table and never a prefix
        // sweep -- ADR-207 is explicit that an exact list is what stops an unregister taking
        // something that happens to share a prefix.
        if (node.windAuthored) {
            for (const char* suffix :
                 {"wind/strength", "wind/trunk", "wind/branch", "wind/foliage", "wind/flutter",
                  "wind/lag"}) {
                params_->remove(base + suffix);
            }
        }
        if (node.energyAuthored || node.windAuthored) {
            for (const char* suffix :
                 {"energy/intensity", "energy/pulseSpeed", "energy/pulseWidth", "energy/propagation",
                  "energy/root", "energy/trunk", "energy/branch", "energy/canopy", "energy/noise",
                  "energy/bloom", "energy/shimmer", "energy/shimmerSpeed", "energy/shimmerScale",
                  "energy/colorNear", "energy/colorFar"}) {
                params_->remove(base + suffix);
            }
            node.energyIntensityParam = nullptr;
            node.shimmerParam = nullptr;
            node.windStrengthParam = nullptr;
            node.windTrunkParam = nullptr;
            node.windBranchParam = nullptr;
            node.windFoliageParam = nullptr;
            node.windFlutterParam = nullptr;
            node.windLagParam = nullptr;
        }
        if (node.kind == NodeKind::Terrain) {
            for (const char* suffix :
                 {"terrainLod", "terrainCull", "terrainLodDistance", "terrainViewDistance",
                  "water/glow", "water/sparkle", "water/ripple", "water/flowSpeed", "water/swell",
                  "water/foam", "water/glowColor"}) {
                params_->remove(base + suffix);
            }
            node.terrainLodParam = nullptr;
            node.terrainCullParam = nullptr;
            node.terrainLodDistanceParam = nullptr;
            node.terrainViewDistanceParam = nullptr;
            node.waterGlowParam = nullptr;
            node.waterSparkleParam = nullptr;
            node.waterRippleParam = nullptr;
            node.waterFlowSpeedParam = nullptr;
            node.waterSwellParam = nullptr;
            node.waterFoamParam = nullptr;
            node.waterGlowColorParam = nullptr;
        }
        forEachParticleParam(node.particleParams, [&](auto* p) {
            if (p != nullptr) {
                params_->remove(p->path());
            }
        });
        if (node.kind == NodeKind::Procedural) {
            unregisterProceduralParameters(*params_, node.proceduralParams);
            for (const auto& part : node.materialPartParams) {
                for (const params::IParameter* parameter : std::array<const params::IParameter*, 4>{
                         part.tint, part.emissiveGain,
                         part.roughnessScale, part.opacityScale}) {
                    if (parameter != nullptr) params_->remove(parameter->path());
                }
            }
            node.materialPartParams.clear();
        }
        if (node.kind == NodeKind::Field) {
            unregisterFieldParameters(*params_, node.fieldParams);
        }
        if (node.kind == NodeKind::Spline) {
            unregisterSplineParameters(*params_, node.splineParams);
        }
        if (node.kind == NodeKind::Sdf) {
            unregisterSdfParameters(*params_, node.sdfParams);
        }
        if (node.child) {
            node.child->unregisterParameters();
        }
    }
    node.proceduralParams = {};
    node.fieldParams = {};
    node.splineParams = {};
    node.sdfParams = {};
    node.positionParam = nullptr;
    node.rotationParam = nullptr;
    node.scaleParam = nullptr;
    node.visibleParam = nullptr;
    node.emissiveParam = nullptr;
    node.roughnessParam = nullptr;
    node.opacityParam = nullptr;
    node.particleParams = {};
}

void Composition::unregisterParameters() {
    if (params_ != nullptr) {
        // Exactly what `attach` recorded registering. This was a list of twenty-seven path
        // strings, kept by hand beside a registrar that adds them one at a time, and it had fallen
        // sixty-four behind -- see `registeredPaths_`.
        // The unregisters that read through cached parameter pointers go FIRST. The sweep of
        // `registeredPaths_` below frees every parameter `attach` made -- particle, material-part
        // and light parameters among them -- and a node's unregister afterwards read `p->path()`
        // through a pointer to one it had just freed: the heap-use-after-free ASan found on CI in
        // `removeNode` of a nested scene (test_composition.cpp's nested-removal sweep).
        if (!lightRigParams_.all.empty()) {
            unregisterLightRigParameters(*params_, lightRigParams_);
            lightRigParams_ = {};
        }
        unregisterAuthoredLightParameters();
        for (const auto& mp : materialParams_) {
            unregisterMaterialProgramParameters(*params_, mp);
        }
        materialParams_.clear();
        for (auto& node : nodes_) {
            unregisterNodeParameters(*node);
        }
        // Then exactly what `attach` recorded registering, which catches whatever the hand-kept
        // lists above still miss. This was a list of twenty-seven path strings, kept by hand
        // beside a registrar that adds them one at a time, and it had fallen sixty-four behind --
        // see `registeredPaths_`. Removing a path already gone is a no-op.
        for (const std::string& path : registeredPaths_) {
            params_->remove(path);
        }
        registeredPaths_.clear();
    }
    detach();
}

void Composition::detach() {
    if (params_ != nullptr) {
        entityWorld_.unregisterParameters(*params_);
    }
    for (auto& node : nodes_) {
        node->positionParam = nullptr;
        node->rotationParam = nullptr;
        node->scaleParam = nullptr;
        node->visibleParam = nullptr;
        node->emissiveParam = nullptr;
        node->roughnessParam = nullptr;
        node->opacityParam = nullptr;
        node->lightIntensityParam = nullptr;
        node->lightColorParam = nullptr;
        node->windStrengthParam = nullptr;
        node->windTrunkParam = nullptr;
        node->windBranchParam = nullptr;
        node->windFoliageParam = nullptr;
        node->windFlutterParam = nullptr;
        node->windLagParam = nullptr;
        node->terrainLodParam = nullptr;
        node->terrainCullParam = nullptr;
        node->terrainLodDistanceParam = nullptr;
        node->terrainViewDistanceParam = nullptr;
        node->waterGlowParam = nullptr;
        node->waterSparkleParam = nullptr;
        node->waterRippleParam = nullptr;
        node->waterFlowSpeedParam = nullptr;
        node->waterSwellParam = nullptr;
        node->waterFoamParam = nullptr;
        node->waterGlowColorParam = nullptr;
        node->particleParams = {};
        node->materialPartParams.clear(); // see the note at the end of this function
        if (node->child) {
            node->child->detach();
        }
    }
    cameraDistance_ = nullptr;
    cameraHeight_ = nullptr;
    cameraOrbitSpeed_ = nullptr;
    cameraSplineT_ = nullptr;
    cameraLookAhead_ = nullptr;
    cameraSplineOffset_ = nullptr;
    materialParams_.clear();
    cameraFov_ = nullptr;
    cameraMode_ = nullptr;
    cameraPosition_ = nullptr;
    cameraTarget_ = nullptr;
    cameraShakeAmplitude_ = nullptr;
    cameraShakeFrequency_ = nullptr;
    cameraShakeDecay_ = nullptr;
    cameraShakeRotation_ = nullptr;
    cameraShakeStart_ = nullptr;
    cameraChannels_.clear();
    envIntensity_ = nullptr;
    envRotation_ = nullptr;
    skyEnabled_ = nullptr;
    skyBackground_ = nullptr;
    skyZenith_ = nullptr;
    skyHorizon_ = nullptr;
    skyGround_ = nullptr;
    skySunColor_ = nullptr;
    skyHaze_ = nullptr;
    skySunIntensity_ = nullptr;
    skySunSize_ = nullptr;
    skySunGlow_ = nullptr;
    skyIntensity_ = nullptr;
    stylized_ = nullptr;
    brightness_ = nullptr;
    fogDensity_ = nullptr;
    fogColor_ = nullptr;
    styledSkyAmbient_ = nullptr;
    styledGroundAmbient_ = nullptr;
    volumeDensity_ = nullptr;
    fogHeight_ = nullptr;
    fogHeightFalloff_ = nullptr;
    fogUpperDensity_ = nullptr;
    fogHeightCurve_ = nullptr;
    windEnabled_ = nullptr;
    windSpeed_ = nullptr;
    windDirection_ = nullptr;
    windGustAmount_ = nullptr;
    windGustScale_ = nullptr;
    windGustSpeed_ = nullptr;
    windGustSharpness_ = nullptr;
    windTurbulence_ = nullptr;
    windTurbulenceScale_ = nullptr;
    windTurbulenceSpeed_ = nullptr;
    windRegionScale_ = nullptr;
    windRegionAmount_ = nullptr;
    windRegionDrift_ = nullptr;
    windFlutterScale_ = nullptr;
    volumeScattering_ = nullptr;
    volumeAbsorption_ = nullptr;
    volumeAnisotropy_ = nullptr;
    volumeNoise_ = nullptr;
    volumeNoiseScale_ = nullptr;
    volumeNoiseSpeed_ = nullptr;
    volumeEmission_ = nullptr;
    volumeSteps_ = nullptr;
    volumeShadowSteps_ = nullptr;
    volumeShadowStrength_ = nullptr;
    volumeLocalLights_ = nullptr;
    volumeMaxDistance_ = nullptr;
    fogHeightAmount_ = nullptr;
    volumeJitter_ = nullptr;
    keyLight_ = nullptr;
    gridIntensity_ = nullptr;
    rootScale_ = nullptr;
    rootRotationSpeed_ = nullptr;
    rootImpulse_ = nullptr;

    // QA pass 2026-09-22: the five below, the whole `dayNightParams_` group, the authored-light
    // params and each node's material-part params were **missing from this list**, so they went on
    // pointing into a `ParameterSet` that `detach` had just released. `applyParameters` reads
    // `dayNightParams_.enabled` unconditionally, so `detach(); params.clear(); update();` -- the
    // exact sequence `test_composition.cpp`'s "detach leaves no dangling use" section performs --
    // was a heap-use-after-free. It never failed a run: freed memory reads as whatever is there,
    // so the suite was green in release for as long as this existed. AddressSanitizer found it the
    // first time the ASan build was repaired enough to link the test binary.
    //
    // `attach` re-registers every one of these, so clearing them here costs nothing: the five
    // scalars and the day/night group at its `env/*` block, `registerAuthoredLightParameters` and
    // `registerNodeParameters` for the other two.
    showSkybox_ = nullptr;
    proceduralSkyBackground_ = nullptr;
    lightFromEnvironment_ = nullptr;
    skyBloom_ = nullptr;
    shadowRange_ = nullptr;
    dayNightParams_ = {};
    authoredLightParams_.clear();

    params_ = nullptr;
    modulator_ = nullptr;
}

void Composition::addDefaultRoutes(params::Modulator& modulator) {
    using namespace params;
    if (!hasRouteTo(modulator, "root/scale")) {
        ModRoute r{.source = "audio.bass", .target = "root/scale", .amount = 0.35f};
        r.chain.curve = CurveType::Power;
        r.chain.curveAmount = 0.8f;
        r.chain.attackMs = 15.0f;
        r.chain.decayMs = 180.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "root/rotationSpeed")) {
        ModRoute r{.source = "audio.mid", .target = "root/rotationSpeed", .amount = 1.5f};
        r.chain.attackMs = 50.0f;
        r.chain.decayMs = 400.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "scene/brightness")) {
        ModRoute r{.source = "audio.rms", .target = "scene/brightness", .amount = 0.5f};
        r.chain.attackMs = 30.0f;
        r.chain.decayMs = 500.0f;
        modulator.addRoute(r);
    }
    if (!hasRouteTo(modulator, "root/impulse")) {
        ModRoute r{.source = "audio.onset", .target = "root/impulse", .amount = 0.25f};
        r.chain.envelope = EnvelopeMode::PeakHold;
        r.chain.envelopeHoldMs = 30.0f;
        r.chain.envelopeFallPerSecond = 4.0f;
        modulator.addRoute(r);
    }
}

// ---- flattening --------------------------------------------------------------------------------

Transform Composition::nodeTransform(const CompositionNode& node) const {
    Transform t = node.transform;
    if (node.positionParam != nullptr) {
        t.position = node.positionParam->value();
    }
    if (node.rotationParam != nullptr) {
        t.rotation = quatFromEulerDegrees(node.rotationParam->value());
    }
    if (node.scaleParam != nullptr) {
        t.scale = node.scaleParam->value();
    }
    return t;
}

// The same transform read from the parameter **bases** instead of their finals: where the file puts
// this node, rather than where this instant's modulation has it.
//
// The two are the same number for most of a node's life and they are not the same *fact*, and
// ADR-264 is what the difference cost. An entity's anchor is "where the scene put it: the point
// motion is relative to", and `applyOffsets` writes `travel + motion` onto the position parameter's
// final -- which is rebuilt from the **base** at the top of every frame. So the drawn position is
// `base + travel + motion` and `Entity::state().position()` is `anchor + travel`: for those to
// describe the same body, `anchor` has to be the base. Taking it from the final made it whatever
// modulation happened to have done at the instant the bindings were last built, which for a project
// load is the value the *scene* carried, one pass before the project overwrote the base with its
// own. Measured in Glowmere: 28.661 m, for the whole run, in silence.
Transform Composition::nodeBaseTransform(const CompositionNode& node) const {
    Transform t = node.transform;
    if (node.positionParam != nullptr) {
        t.position = node.positionParam->base();
    }
    if (node.rotationParam != nullptr) {
        t.rotation = quatFromEulerDegrees(node.rotationParam->base());
    }
    if (node.scaleParam != nullptr) {
        t.scale = node.scaleParam->base();
    }
    return t;
}

Transform Composition::nodeWorldBaseTransform(const CompositionNode& node) const {
    Transform world = nodeBaseTransform(node);
    const CompositionNode* current = &node;
    for (std::size_t guard = 0; !current->parent.empty() && guard < nodes_.size(); ++guard) {
        const CompositionNode* parent = findNode(current->parent);
        if (parent == nullptr || parent == &node) {
            break;
        }
        world = compose(nodeBaseTransform(*parent), world);
        current = parent;
    }
    return world;
}

bool Composition::nodeVisible(const CompositionNode& node) const {
    const CompositionNode* n = &node;
    // Bounded by the node count: a hand-edited file can describe a parent cycle, and a flatten that
    // never returns is worse than one that draws a broken scene.
    for (std::size_t guard = 0; n != nullptr && guard <= nodes_.size(); ++guard) {
        const bool own = n->visibleParam != nullptr ? n->visibleParam->value() : n->visible;
        if (!own) {
            return false;
        }
        if (n->parent.empty()) {
            break;
        }
        n = findNode(n->parent);
    }
    return true;
}

void Composition::ensureBuilt() {
    if (dirty_) {
        rebuild();
    }
}

// ADR-351: this asset's LOD chains, built once and cached. The returned chains' `base` is the
// asset's own mesh index; the caller adds the offset the asset landed at in this scene.
//
// The chain builder is pure and deterministic, so the cache is keyed on everything that can change
// its answer: the resolved path, the registry's version for that path (a reload bumps it), and the
// ladder. Nothing else reaches it.
std::shared_ptr<const std::vector<MeshLodChain>>
Composition::lodChainsFor(const assets::SceneAsset& asset, const NodeLod& lod, const std::string& owner) {
    assets::LodChainSettings settings = assets::foliageLodSettings();
    if (!lod.ratios.empty()) {
        settings.ratios = lod.ratios;
    }
    if (!lod.thinning) {
        settings.thinning.fallback = 0.0f;
    }
    std::string ladder;
    for (const float ratio : settings.ratios) {
        ladder += fmt::format("{:.4f},", ratio);
    }
    ladder += lod.thinning ? "thin" : "nothin";

    LodCacheKey key{asset.path.string(), asset.version, ladder};
    if (const auto found = lodCache_.find(key); found != lodCache_.end()) {
        return found->second;
    }
    if (auto problem = settings.validate(); !problem) {
        log::warn("node '{}': lod ladder rejected ({}); no LOD built", owner, problem.error().message);
        auto empty = std::make_shared<const std::vector<MeshLodChain>>();
        lodCache_.emplace(key, empty);
        return empty;
    }

    const auto started = std::chrono::steady_clock::now();
    auto chains = std::make_shared<std::vector<MeshLodChain>>();
    std::uint64_t sourceTriangles = 0;
    std::uint64_t rungTriangles = 0;
    for (std::size_t i = 0; i < asset.scene.meshes.size(); ++i) {
        const MeshData& mesh = asset.scene.meshes[i];
        // A skinned mesh is left alone. Every level is a re-indexed, re-ordered vertex buffer and
        // MeshData::skin is parallel to the one it came from, so a rung of a skinned mesh would
        // pose from the wrong joints -- which is §7 of the brief, enforced rather than promised.
        if (mesh.skinned()) {
            continue;
        }
        auto built = assets::buildLodChain(mesh, settings);
        if (!built) {
            log::warn("node '{}': mesh {} of '{}': {}", owner, i, asset.path.filename().string(),
                      built.error().message);
            continue;
        }
        MeshLodChain out;
        out.base = static_cast<MeshId>(i);
        out.sourceTriangles = built->sourceTriangles;
        out.sourceShells = built->sourceShells;
        out.sourceSurfaceArea = meshMetrics(mesh).surfaceArea;
        out.hysteresis = lod.hysteresis;
        out.maxScreenError = lod.maxScreenError;
        sourceTriangles += built->sourceTriangles;
        // levels[0] is the source and is deliberately not carried: scene.meshes[base] is LOD0 and
        // there must be exactly one copy of it.
        for (std::size_t level = 1; level < built->levels.size(); ++level) {
            const assets::LodLevel& src = built->levels[level];
            MeshLodLevel out_level;
            out_level.targetRatio = src.targetRatio;
            out_level.achievedRatio = src.achievedRatio;
            out_level.error = src.error;
            out_level.sloppy = src.sloppy;
            out_level.thinned = src.thinned;
            out_level.triangles = static_cast<std::uint32_t>(src.mesh.indices.size() / 3);
            out_level.surfaceArea = meshMetrics(src.mesh).surfaceArea;
            out_level.mesh = src.mesh;
            out.levels.push_back(std::move(out_level));
        }
        if (!out.levels.empty()) {
            rungTriangles += out.levels.back().triangles;
            chains->push_back(std::move(out));
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    // The achieved ratio of the coarsest rung, over the whole asset, is the one number that says
    // whether the ladder did anything -- a chain that reports five levels and reaches 0.98 at the
    // bottom of them is the failure this log line exists to make visible.
    log::info("node '{}': LOD for '{}': {} chain(s) over {} triangles, coarsest rung {:.3f} of the "
              "source, built in {:.0f} ms",
              owner, asset.path.filename().string(), chains->size(), sourceTriangles,
              sourceTriangles == 0 ? 0.0
                                   : static_cast<double>(rungTriangles) / static_cast<double>(sourceTriangles),
              ms);
    std::shared_ptr<const std::vector<MeshLodChain>> shared = std::move(chains);
    lodCache_.emplace(key, shared);
    return shared;
}

void Composition::rebuild() {
    ++flattens_;
    // Captured before anything clears the table, because `Scene::addTexture` moves the counter on
    // its own and the decision below is about the whole of this rebuild, not about the last writer.
    const std::uint64_t textureVersionBefore = scene_.textureVersion;
    // A flatten is the single most expensive thing the editor does on the main thread, it is
    // triggered by structural edits an artist makes constantly, and until ADR-092 nothing said what
    // it cost. One line per rebuild, so "why did that stutter" has an answer in the log of the run
    // it happened in rather than in a profiler nobody was running.
    const auto rebuildStart = std::chrono::steady_clock::now();
    // The obstacle set describes the world this rebuild is about to produce, so it starts empty
    // here rather than being patched: a stale solid is a character walking round nothing, and a
    // missing one is a character walking through a tree.
    obstacles_ = std::make_shared<spatial::ObstacleField>();
    scene_.meshes.clear();
    scene_.textures.clear();
    scene_.entities.clear();
    scene_.rigs.clear(); // ADR-086: rebuilt from the nodes' assets below
    scene_.particles.clear();
    scene_.procedurals.clear();
    scene_.fields.fields.clear();
    scene_.fields.grids.clear();
    // Simulated grids (ADR-032) are scene-level, not nodes: they have no transform of their own
    // (their bounds are world space) and are referenced by name from Grid fields.
    for (const spatial::GridField& src : grids_) {
        spatial::GridField g = src;
        g.name = sanitise(prefix_) + src.name;
        if (!g.injectField.empty()) {
            g.injectField = sanitise(prefix_) + src.injectField;
        }
        if (!g.velocityField.empty()) {
            g.velocityField = sanitise(prefix_) + src.velocityField;
        }
        scene_.fields.grids.push_back(std::move(g));
    }
    scene_.splines.splines.clear();
    scene_.sdfs.clear();
    // Terrain grounds first: the generated ground material has to exist before the programs are
    // copied into the scene, or the terrain names a program that is not there until the next
    // rebuild -- which, for a scene that rebuilds once, is never. Generating it here rather than
    // in the node loop is the whole of that fix.
    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        // The generated ground program is a *fallback*: it is built only for a terrain that names
        // no program of its own, and the chunks take it only on the same condition. So a terrain
        // that authors both a material program and a ground glow gets the program and silently
        // loses the glow -- `groundGlow`, `groundGlowScale`, `groundGlowCoverage`,
        // `groundGlowColor` and `groundMottle` all live in the generated program, which for this
        // terrain is never built and would not be evaluated if it were.
        //
        // Found by measurement rather than by reading: setting `groundGlow` to 5.0 on Glowmere's
        // valley produced a byte-identical frame, as did turning `groundMottle` off. Five authored
        // settings that parse, validate, and reach nothing.
        //
        // A diagnostic rather than a mechanism, which is what ADR-160 and ADR-161 both concluded
        // about this exact shape: name the two things that disagree, rather than inventing a
        // composition rule between an authored program and a generated one.
        if (node.kind == NodeKind::Terrain && !node.terrainMaterial.program.empty() &&
            node.terrain.groundGlow > 0.0f && !node.terrainGroundWarned) {
            nodePtr->terrainGroundWarned = true;
            log::warn("terrain '{}': groundGlow {:.3g} is carried by the generated ground material, "
                      "and this terrain draws with the authored program '{}' instead -- so the glow, "
                      "its scale, its coverage, its colour and groundMottle all do nothing. Clear "
                      "the program to use the generated material, or author the glow into '{}'.",
                      node.name, node.terrain.groundGlow, node.terrainMaterial.program,
                      node.terrainMaterial.program);
        }
        if (node.kind != NodeKind::Terrain || !node.terrainMaterial.program.empty() || node.worldMap.biomes.empty()) {
            continue;
        }
        const auto install = [&](const std::string& name, MaterialProgram mp) {
            if (auto v = mp.validate(); !v) {
                log::warn("terrain '{}': generated material '{}': {}", node.name, name, v.error().message);
                return;
            }
            const auto existing = std::find_if(materialPrograms_.begin(), materialPrograms_.end(),
                                               [&](const MaterialProgram& p) { return p.name == name; });
            if (existing != materialPrograms_.end()) {
                *existing = std::move(mp); // a retuned palette repaints on the next rebuild
            } else {
                materialPrograms_.push_back(std::move(mp));
            }
        };
        install(terrainGroundProgramName(node.name),
                world::terrainMaterialProgram(node.worldMap.biomes, terrainGroundProgramName(node.name),
                                              node.terrain.groundMottle, node.terrain.groundGlow,
                                              node.terrain.groundGlowScale, node.terrain.groundGlowCoverage,
                                              node.terrain.groundGlowColor));
        // ADR-099: water no longer runs a material program -- it has its own pipeline, and the
        // program it used to run could not reach the scene depth its shoreline is made of. The
        // name survives as the key `Scene::waters` is addressed by; installing a program under it
        // would spend one of the eight material-program slots on something nothing evaluates.
    }
    scene_.materialPrograms.clear();
    scene_.waters.clear();
    ownMaterialCount_ = materialPrograms_.size();
    for (const MaterialProgram& src : materialPrograms_) {
        MaterialProgram mp = src;
        mp.name = sanitise(prefix_) + src.name;
        prefixFieldReferences(mp, sanitise(prefix_));
        scene_.materialPrograms.push_back(std::move(mp));
    }
    scene_.lights.clear();
    scene_.cameras.clear();
    scene_.meshLods.clear();
    ranges_.clear();
    ranges_.reserve(nodes_.size());

    // Each unique asset's meshes and textures are stored once; instances share them by offset.
    std::map<std::string, std::pair<MeshId, TextureId>> assetOffsets;

    // Imported-mesh resolution, shared by procedural nodes and by terrain's scatter layers: load
    // the asset once, share its meshes and textures with anything else that names it, merge it into
    // one mesh for instancing and take its material.
    // Loads an asset and returns one part per material it uses, with the parts' texture references
    // remapped into this scene. Empty when the asset itself failed to load, which is a warning and
    // not an error: the object keeps whatever source it had.
    const auto resolveMeshParts = [&](const std::string& assetPath,
                                      const std::string& owner) -> std::vector<AssetPart> {
        auto loaded = registry_.loadScene(assetPath);
        if (!loaded) {
            log::warn("'{}': mesh asset '{}': {}", owner, assetPath, loaded.error().message);
            return {};
        }
        const assets::SceneAsset& asset = **loaded;
        const std::string key = asset.path.string();
        auto it = assetOffsets.find(key);
        if (it == assetOffsets.end()) {
            const auto offsets = std::make_pair(static_cast<MeshId>(scene_.meshes.size()),
                                                static_cast<TextureId>(scene_.textures.size()));
            for (const auto& mesh : asset.scene.meshes) {
                scene_.meshes.push_back(mesh);
            }
            for (const auto& texture : asset.scene.textures) {
                scene_.textures.push_back(texture);
            }
            it = assetOffsets.emplace(key, offsets).first;
        }
        std::vector<AssetPart> parts = assetMaterialParts(asset);
        for (AssetPart& part : parts) {
            offsetMaterialTextures(part.material, it->second.second);
        }
        if (parts.size() > 1) {
            log::info("'{}': mesh asset '{}' carries {} materials; one instanced draw each", owner,
                      assetPath, parts.size());
        }
        return parts;
    };

    for (const auto& nodePtr : nodes_) {
        // Non-const because a Gltf node records which rigs the flatten gave it (ADR-086); nothing
        // else in this loop writes to the node.
        CompositionNode& node = *nodePtr;
        NodeRange range;
        range.firstEntity = scene_.entities.size();
        range.firstParticle = scene_.particles.size();
        const Transform nodeT = nodeWorldTransform(node);
        const bool visible = nodeVisible(node);

        switch (node.kind) {
        case NodeKind::Gltf: {
            if (!node.sceneAsset) {
                break;
            }
            const assets::SceneAsset& asset = *node.sceneAsset;
            const std::string key = asset.path.string();
            auto it = assetOffsets.find(key);
            if (it == assetOffsets.end()) {
                const auto offsets = std::make_pair(static_cast<MeshId>(scene_.meshes.size()),
                                                    static_cast<TextureId>(scene_.textures.size()));
                for (const auto& mesh : asset.scene.meshes) {
                    scene_.meshes.push_back(mesh);
                }
                for (const auto& texture : asset.scene.textures) {
                    scene_.textures.push_back(texture);
                }
                it = assetOffsets.emplace(key, offsets).first;
            }
            const auto [meshOffset, textureOffset] = it->second;
            // ADR-351: the coarser rungs, when this node asked for them. Built once per (asset,
            // asset version, ladder) and shared by every node that asks for the same thing.
            //
            // The chain is attached to the *asset's* meshes, which every node on that asset shares,
            // so two nodes on one asset asking for different ladders would be asking one MeshId for
            // two answers. The first one to ask wins and the second is told why, rather than the
            // two silently overwriting each other on alternate flattens.
            if (node.lod.enabled) {
                const auto built = lodChainsFor(asset, node.lod, node.name);
                for (const MeshLodChain& chain : *built) {
                    MeshLodChain moved = chain;
                    moved.base += meshOffset;
                    const auto existing =
                        std::find_if(scene_.meshLods.begin(), scene_.meshLods.end(),
                                     [&](const MeshLodChain& c) { return c.base == moved.base; });
                    if (existing != scene_.meshLods.end()) {
                        if (existing->levels.size() != moved.levels.size()) {
                            log::warn("node '{}': mesh {} already has a {}-rung LOD chain from another "
                                      "node on the same asset; this node's {}-rung ladder is ignored",
                                      node.name, moved.base, existing->levels.size(),
                                      moved.levels.size());
                        }
                        continue;
                    }
                    scene_.meshLods.push_back(std::move(moved));
                }
            }
            // ADR-086: rigs are copied per node instance, not per asset. Two nodes on the same
            // character file are two characters, and they must be able to be doing different
            // things; sharing one pose between them is the bug, not the saving.
            const auto rigOffset = static_cast<RigId>(scene_.rigs.size());
            range.firstRig = scene_.rigs.size();
            for (const SkinnedRig& src : asset.scene.rigs) {
                SkinnedRig rig = src;
                rig.name = node.name + "/" + src.name;
                // All four rungs, not two. `nearDistance` and `farHz` used to keep the rig's
                // compiled-in defaults whatever the scene said, so a node asking for 30 Hz was
                // quietly served 20 past fifteen metres by a ceiling it could not name.
                rig.updateHz = node.animation.updateHz;
                rig.nearDistance = node.animation.nearDistance;
                rig.farHz = node.animation.farHz;
                rig.cullDistance = node.animation.cullDistance;
                // ADR-300. Bound here, against this rig's own skeleton and clips, because that is
                // the only place both the authored names and the asset that has to carry them are
                // in scope. Every problem is logged with the node's name on it: a mask that named a
                // joint the rig does not have used to be indistinguishable from a mask that did
                // nothing because nothing asked it to, which is the same failure as a socket
                // returning `true` on its fallback (ADR-274).
                // ADR-546/547, bound here for the same reason the layers are: the authored names
                // and the asset that has to carry them are only both in scope at this point.
                if (!node.animation.contacts.empty()) {
                    rig.contactJoints.clear();
                    for (const std::string& joint : node.animation.contacts) {
                        if (rig.skeleton.find(joint) < 0) {
                            log::warn("node '{}' rig '{}': contact joint '{}' is not in this rig, so no "
                                      "clip will be analysed for it",
                                      node.name, src.name, joint);
                            continue;
                        }
                        rig.contactJoints.push_back(ContactJoint{joint, ContactKind::Foot});
                    }
                    if (!rig.contactJoints.empty()) {
                        const std::uint32_t cyclic = rig.analyse();
                        if (cyclic == 0) {
                            log::warn("node '{}' rig '{}': none of its {} clip(s) came back cyclic, so "
                                      "phase matching has nothing to align to",
                                      node.name, src.name, rig.clips.size());
                        }
                    }
                }
                if (node.animation.matchPhase) {
                    rig.player.setMatchPhase(true);
                }
                rig.player.inertializeHalflife = node.animation.inertialize;
                if (!node.animation.layers.empty() || node.animation.bodyCompensation.enabled) {
                    // ADR-544. Set BEFORE `bind`, because `bind` is what resolves the body joint
                    // against this rig and reports a name it does not carry.
                    rig.layers.setBodyCompensation(node.animation.bodyCompensation);
                    for (const std::string& problem :
                         rig.layers.bind(node.animation.layers, rig.skeleton, rig.clips)) {
                        log::warn("node '{}' rig '{}': {}", node.name, src.name, problem);
                    }
                }
                // ADR-337, bound in the same place and for the same reason: the authored clip
                // name and the asset that has to carry it are only both in scope here.
                if (!node.animation.rootMotion.empty()) {
                    for (const std::string& problem :
                         rig.rootMotion.bind(node.animation.rootMotion, rig.skeleton, rig.clips)) {
                        log::warn("node '{}' rig '{}': {}", node.name, src.name, problem);
                    }
                }
                scene_.rigs.push_back(std::move(rig));
            }
            range.rigCount = scene_.rigs.size() - range.firstRig;
            // Two ladders, and nothing used to say when they disagreed.
            //
            // A character has two distance policies: this one, which decides whether its skeleton is
            // posed, and the entity's behaviour LOD, which decides whether it is simulated at all.
            // They are separately authored and legitimately different -- posing is joints x
            // instances of CPU and simulating is a path query -- but one ordering of them is always
            // wrong. If the rig stops being posed *nearer* than the entity stops walking, the band
            // between them draws a character travelling across the world in a frozen pose. Glowmere
            // shipped exactly that: the wanderer is simulated to 320 m and was posed to 220 m, and
            // the hundred metres in between are where "he slides" and "his animation stops when I
            // zoom out" both come from.
            //
            // A warning rather than a clamp, because which number is wrong is an art decision: the
            // fix may be to pose further, or to stop simulating sooner. Naming both numbers is what
            // lets somebody make it.
            if (range.rigCount > 0 && node.animation.cullDistance > 0.0f) {
                const auto desc = std::find_if(entityDescs_.begin(), entityDescs_.end(),
                                               [&](const entity::EntityDesc& d) { return d.node == node.name; });
                if (desc != entityDescs_.end() && desc->cullDistance > node.animation.cullDistance) {
                    log::warn("'{}': the rig stops being posed at {:.0f} m but the entity keeps "
                              "simulating to {:.0f} m; between them the character travels in a "
                              "frozen pose",
                              node.name, node.animation.cullDistance, desc->cullDistance);
                }
            }
            node.rigs.clear();
            for (std::size_t r = 0; r < range.rigCount; ++r) {
                node.rigs.push_back(rigOffset + static_cast<RigId>(r));
            }
            // Re-apply the node's request to the fresh rigs, at the timeline second it was first
            // made: a rebuild must not restart a walk cycle, so `animationAppliedAt` is kept.
            node.animationPushed = false;
            for (const Entity& src : asset.scene.entities) {
                Entity e = src;
                offsetEntityIds(e, meshOffset, textureOffset);
                if (e.rig != kInvalidRig) {
                    e.rig += rigOffset;
                }
                e.name = node.name + "/" + src.name;
                e.transform = compose(nodeT, src.transform);
                e.visible = visible && src.visible;
                scene_.entities.push_back(std::move(e));
                range.restTransforms.push_back(src.transform);
                range.restEmissive.push_back(src.material.emissiveIntensity);
                range.restRoughness.push_back(src.material.roughness);
            }
            range.firstLight = scene_.lights.size();
            for (const PunctualLight& src : asset.scene.lights) {
                PunctualLight light = src;
                light.name = node.name + "/" + src.name;
                light.position = transformPoint(nodeT, src.position);
                light.direction = transformDirection(nodeT, src.direction);
                // The asset's own numbers, kept so the per-frame scale multiplies the authored value
                // rather than compounding on last frame's.
                range.restLightIntensity.push_back(light.intensity);
                range.restLightColor.push_back(light.color);
                scene_.addLight(std::move(light));
            }
            range.lightCount = scene_.lights.size() - range.firstLight;
            break;
        }
        case NodeKind::Orb: {
            const MeshId mesh = scene_.addMesh(makeIcosphere(1.0f, 3));
            Entity& orb = scene_.addEntity(node.name, mesh);
            orb.style = MeshStyle::Lit;
            // The built-in look, and then whatever the node authored over it. The defaults used to
            // be the whole story and a `material` block on an orb did nothing at all.
            orb.material.baseColor = glm::vec3(0.75f, 0.2f, 0.9f);
            orb.material.emissiveColor = glm::vec3(0.9f, 0.45f, 1.0f);
            orb.material.emissiveIntensity = 0.15f;
            orb.material.roughness = 0.35f;
            if (node.materialAuthored) {
                orb.material = node.terrainMaterial;
            }
            orb.transform = nodeT;
            orb.visible = visible;
            range.restTransforms.emplace_back();
            range.restEmissive.push_back(orb.material.emissiveIntensity);
            range.restRoughness.push_back(orb.material.roughness);
            break;
        }
        case NodeKind::Group:
            // Nothing. A group is a transform its children read through nodeWorldTransform(), and a
            // transform that draws is not a group -- an artist who has to hide the handle of every
            // group they made has been given a chore rather than a tool. Its selection outline and
            // its gizmo are drawn by the editor, over the frame, where they cannot be rendered into
            // an offline take by accident.
            break;
        case NodeKind::Grid: {
            const MeshId mesh = scene_.addMesh(makePlane(12.0f, 48));
            Entity& grid = scene_.addEntity(node.name, mesh);
            grid.style = MeshStyle::Grid;
            grid.transform = nodeT;
            grid.visible = visible;
            range.restTransforms.emplace_back();
            range.restEmissive.push_back(0.0f);
            range.restRoughness.push_back(0.5f);
            break;
        }
        case NodeKind::City: {
            // ADR-100. The node carries `CitySettings`; the plan and the placements are derived
            // here on every rebuild, because a scatter cloud is runtime state and is not serialised.
            // Exactly the arrangement the terrain node uses for its ecology, and for the same
            // reason: the description survives a save, the placements are made again from it.
            CompositionNode& mutableNode = *nodePtr;
            auto plan = world::planCity(node.city);
            if (!plan) {
                log::warn("city '{}': {}", node.name, plan.error().message);
                break;
            }
            const std::filesystem::path manifest =
                node.cityLibrary.empty() ? std::filesystem::path{} : registry_.resolve(node.cityLibrary);
            if (manifest.empty()) {
                log::warn("city '{}': no tiling library named; nothing to place with", node.name);
                break;
            }
            auto library = assets::AssetLibrary::loadFile(manifest);
            if (!library) {
                log::warn("city '{}': {}", node.name, library.error().message);
                break;
            }
            const world::CityLibrary roles = world::CityLibrary::fromTags(*library);
            // The ground, when this scene has one. A city on a terrain sits on it; a city on its own
            // sits at y = 0, which is what an empty project gets and is a usable first frame.
            std::optional<world::TerrainQuery> ground;
            for (const auto& other : nodes_) {
                if (other->kind == NodeKind::Terrain) {
                    ground = world::terrainQuery(other->worldMap, &other->ecology, heroes_);
                    break;
                }
            }
            auto placed = world::placeCity(*plan, roles, *library, ground ? &*ground : nullptr);
            if (!placed) {
                log::warn("city '{}': {}", node.name, placed.error().message);
                break;
            }
            for (const world::CityPlacement& placement : placed->placements) {
                const assets::AssetDescriptor* asset = library->find(placement.asset);
                if (asset == nullptr || placement.cloud == nullptr) {
                    continue;
                }
                // The mesh itself, resolved the way an ecology layer's is. Setting `source.asset`
                // alone names a file and generates nothing: the registry has to load it and the
                // parts have to be attached, which is what this shares with the terrain path.
                const std::string assetPath =
                    (manifest.parent_path() / asset->file).lexically_normal().string();
                const std::vector<AssetPart> parts =
                    resolveMeshParts(assetPath, "city '" + node.name + "'");
                if (parts.empty() || parts[0].mesh == nullptr) {
                    continue;
                }
                ProceduralGeometry pg;
                pg.name = sanitise(prefix_) + node.name + "_" + placement.asset;
                pg.source.kind = PrimitiveKind::Mesh;
                pg.source.asset = assetPath;
                pg.source.assetMesh = parts[0].mesh;
                if (parts[0].hasMaterial) {
                    pg.material = parts[0].material;
                }
                pg.distribution.kind = DistributionKind::Scatter;
                pg.distribution.scatterCloud = placement.cloud;
                pg.distribution.scatterHash =
                    static_cast<std::uint64_t>(node.city.seed) * 0x9E3779B97F4A7C15ull ^
                    std::hash<std::string>{}(placement.asset);
                pg.visible = visible;
                pg.sourceTransform = nodeT;
                // Culling and the LOD ladder on, for the same reason a scatter layer has them: a
                // city is thousands of instances and every one of them off screen is free only if
                // something is asked to check.
                pg.lod.cull = true;
                // An asset with more than one material becomes one object per material over the
                // *same* cloud, which is the arrangement the scatter path already uses. Without it
                // a two-material kerb draws only half of itself.
                std::vector<ProceduralGeometry> subs;
                for (std::size_t part = 1; part < parts.size(); ++part) {
                    if (parts[part].mesh == nullptr) {
                        continue;
                    }
                    ProceduralGeometry sub = pg;
                    sub.name = pg.name + fmt::format("_m{}", part);
                    // ADR-108: one spatial instance set, shared with part 0.
                    sub.partOf = pg.name;
                    sub.source.assetPart = static_cast<int>(part);
                    sub.source.assetMesh = parts[part].mesh;
                    if (parts[part].hasMaterial) {
                        sub.material = parts[part].material;
                    }
                    subs.push_back(std::move(sub));
                }
                scene_.procedurals.push_back(std::move(pg));
                for (ProceduralGeometry& sub : subs) {
                    scene_.procedurals.push_back(std::move(sub));
                }
            }
            mutableNode.cityCells = plan->cells.size();
            log::info("city '{}': {} x {} cells, {} piece(s), {} instance(s){}", node.name,
                      plan->width, plan->depth, placed->placements.size(), placed->instanceCount(),
                      placed->undressed.empty() ? std::string{} : [&placed] {
                          std::string list;
                          for (const std::string& kind : placed->undressed) {
                              list += (list.empty() ? "" : ", ") + kind;
                          }
                          return " -- nothing tagged for: " + list;
                      }());
            break;
        }
        case NodeKind::Terrain: {
            // Scatter layers are ordinary procedural objects whose placements happen to have come
            // from an ecology pass rather than from a formula (ADR-048). Everything downstream --
            // instancing, GPU culling, LOD, variation, the material -- is the machinery an imported
            // mesh already gets, which is the whole reason the ecology emits a cloud instead of a
            // new kind of drawable.
            std::vector<world::GlowCluster> nodeGlow;
            std::unordered_map<std::string, std::shared_ptr<spatial::PointCloud>> habitats;
            // Everything this terrain produces is a pure function of these six -- the water settings
            // among them since ADR-099, because the surface mesh bakes its flow lanes into the
            // vertices, so a flow an author retunes has to move the key. A rebuild caused by
            // anything else -- a node placed, a material program added, an HDR swapped -- reuses
            // what the last one made (ADR-092); a change to any of them moves the key and the
            // terrain is built again, with nothing to remember to invalidate.
            CompositionNode& mutableNode = *nodePtr;
            const std::uint64_t terrainKey =
                node.worldMap.structuralHash() ^ (node.terrain.structuralHash() * 0x9E3779B97F4A7C15ull) ^
                (node.ecology.structuralHash() * 0xC2B2AE3D27D4EB4Full) ^
                (static_cast<std::uint64_t>(ecologyLightGain_ * 1024.0f) * 0x165667B19E3779F9ull) ^
                (static_cast<std::uint64_t>(ecologyGlowCell_ * 1024.0f) * 0x27D4EB2F165667C5ull) ^
                (node.waterFlow.structuralHash() * 0x85EBCA77C2B2AE63ull);
            // AVGEN_NO_TERRAIN_CACHE=1 turns the reuse off, so the claim "placing a node used to
            // cost a whole terrain" can be measured rather than believed. A benchmark whose
            // baseline has to be reconstructed by reverting a commit is a benchmark nobody re-runs.
            static const bool cacheDisabled = std::getenv("AVGEN_NO_TERRAIN_CACHE") != nullptr;
            const bool reuseTerrain = !cacheDisabled && mutableNode.terrainProducts.usable(terrainKey);
            CompositionNode::TerrainProducts fresh;
            fresh.hash = terrainKey;
            bool reusedProducts = false;
            std::size_t layerIndex = 0;
            range.ecologyFirst = scene_.procedurals.size();
            for (const world::ScatterLayer& layer : node.ecology.layers) {
                std::span<const glm::vec3> anchors;
                if (layer.proximity) {
                    const auto found = habitats.find(layer.proximity->layer);
                    if (found != habitats.end()) {
                        anchors = found->second->positions();
                    }
                }
                std::shared_ptr<spatial::PointCloud> cloud;
                if (reuseTerrain && layerIndex < mutableNode.terrainProducts.clouds.size()) {
                    // A shared_ptr, so reuse is free rather than a copy of a quarter of a million
                    // placements. The cloud is immutable once scattered, and the procedural object
                    // below already holds it by the same pointer.
                    cloud = mutableNode.terrainProducts.clouds[layerIndex];
                } else {
                    cloud = std::make_shared<spatial::PointCloud>(
                        world::scatter(node.worldMap, layer, anchors, node.ecology.clearances));
                }
                ++layerIndex;
                fresh.clouds.push_back(cloud);
                habitats.emplace(layer.name, cloud);
                if (cloud->count() == 0) {
                    log::warn("terrain '{}': scatter '{}' placed nothing", node.name, layer.name);
                    continue;
                }
                // One part per material the asset uses (ADR-044). Each becomes its own procedural
                // object -- same cloud, same seed, same variation, same wind -- so the parts stay
                // registered with each other and each is drawn with its own material.
                const std::vector<AssetPart> parts = resolveMeshParts(layer.asset, node.name);
                const auto [assetLo, assetHi] = partsBounds(parts);
                // Per-instance obstacles (ADR-093, §5). Built here, and only here, because this is
                // the one place that holds the layer, its placements and the asset's own bounds at
                // the same time -- `ClearanceField` can say "trees about fourteen metres tall grow
                // around here" and never "there is a trunk at this spot", which is why a character
                // could walk through one. The policy decides what is scenery: 120,000 grass
                // instances cost one comparison and produce nothing.
                {
                    const glm::vec3 extent = assetHi - assetLo;
                    const float assetHeight = std::max(extent.y, 1e-3f);
                    // Horizontal radius over height, unit-free, so the layer's own normalisation
                    // does not have to be unpicked here.
                    const float aspect = std::max(extent.x, extent.z) * 0.5f / assetHeight;
                    const std::size_t added = entity::obstaclesFromScatter(
                        layer, *cloud, aspect, assetHeight, entity::ObstaclePolicy{}, *obstacles_);
                    if (added > 0) {
                        log::info("terrain '{}': scatter '{}' contributed {} navigation obstacles ({})",
                                  node.name, layer.name, added,
                                  spatial::obstacleTypeName(entity::classifyScatterLayer(layer)));
                    } else if (layer.navigation != world::ScatterNavigation::Auto) {
                        // A declaration that produced nothing is a thing an author asked for and
                        // did not get, and this project has found too many of those by accident
                        // (ADR-196). `passable` producing nothing is the point; `blocks` producing
                        // nothing means the layer placed nothing, and either way it is said out
                        // loud. Only ever reached by a scene that set the key, so every log a
                        // scene written before it existed prints is byte-identical.
                        log::info("terrain '{}': scatter '{}' declared navigation '{}': "
                                  "contributed no navigation obstacles",
                                  node.name, layer.name,
                                  world::scatterNavigationName(layer.navigation));
                    }
                }
                ProceduralGeometry pg;
                pg.name = scatterObjectName(sanitise(prefix_) + node.name, layer.name);
                pg.source.kind = PrimitiveKind::Mesh;
                pg.source.asset = layer.asset;
                pg.source.meshBudget = layer.meshBudget;
                pg.distribution.kind = DistributionKind::Scatter;
                pg.distribution.scatterCloud = cloud;
                pg.distribution.scatterHash = layer.structuralHash() ^ node.worldMap.structuralHash();
                if (layer.proximity) {
                    pg.distribution.scatterHash ^= node.ecology.structuralHash();
                }
                pg.visible = visible;
                pg.castsShadow = layer.castsShadow;
                // The LOD ladder, switched on. It defaults to a single level, which for a scatter
                // means every instance the frustum keeps draws its full-resolution mesh however
                // small it is on screen -- 6,000 surviving instances were submitting twelve million
                // triangles, and the mesh budgets were being applied to a level nothing else used.
                // The thresholds are projected radii in pixels, so they hold at any resolution.
                pg.lod.cull = true;
                pg.lod.maxDistance =
                    layer.viewDistance > 0.0f ? layer.viewDistance : node.terrain.viewDistance;
                pg.lod.lodCount = scene::kMaxLodLevels;
                pg.lod.lodByScreenSize = true;
                pg.lod.lodDistances[0] = 28.0f; // full mesh above 28 px of radius
                pg.lod.lodDistances[1] = 11.0f; // half-resolution below that
                pg.lod.lodDistances[2] = 4.0f;  // a billboard, then a dot
                pg.lod.minScreenRadius = layer.minScreenRadius;
                if (!parts.empty()) {
                    pg.source.assetMesh = parts[0].mesh;
                    pg.source.meshBudget = partBudget(parts, 0, layer.meshBudget);
                    if (parts[0].hasMaterial) {
                        pg.material = parts[0].material;
                    }
                }
                // The layer says how tall the thing should be; the asset says how tall it is. The
                // normalisation goes on sourceTransform, which scales the mesh alone --
                // distributionTransform would scale the placements with it and move a tree scaled
                // x2 twice as far from the origin.
                pg.material.baseColor *= layer.tint;
                pg.material.surfaceClass = scatterSurfaceClass(layer); // ADR-256
                if (layer.emissiveIntensity > 0.0f) {
                    pg.material.emissiveColor = layer.emissiveColor;
                    pg.material.emissiveIntensity = layer.emissiveIntensity;
                }
                // ADR-054: colour that clusters in space, rotated perceptually so a hue change
                // reads as the colour turning rather than the brightness moving.
                pg.materialVariation.hueField = layer.hueField;
                pg.materialVariation.hueFieldScale = layer.hueFieldScale;
                pg.materialVariation.hueShift = layer.hueRandom;
                pg.materialVariation.emissiveRandom = layer.emissiveRandom;
                pg.materialVariation.emissiveSparsity = layer.emissiveSparsity;
                pg.materialVariation.chromaDrift = layer.chromaDrift;
                pg.materialVariation.chromaDriftScale = layer.chromaDriftScale;
                pg.materialVariation.chromaDriftSpeed = layer.chromaDriftSpeed;
                if (!layer.materialProgram.empty()) {
                    // Registered programs carry the composition's prefix (see the copy into
                    // scene_.materialPrograms), so a raw name from the scene file silently matches
                    // nothing and the layer renders with no program at all.
                    pg.material.program = prefixed(sanitise(prefix_), layer.materialProgram);
                }
                pg.materialVariation.perceptualHue = true;
                // ADR-055: how this species answers the wind, straight through. Nothing else in the
                // scatter path changes -- Tier 0 is a vertex-stage deformation over instances that
                // already exist, so there is no new buffer, no new pass and no new draw.
                pg.motion = layer.motion;
                if (pg.motion.active()) {
                    const wind::MotionResponse r = wind::motionResponse(windSetting_, pg.motion);
                    // The wind speed is in the line on purpose: it is the proof that an A/B which
                    // edits the scene file actually took, without which a benchmark of "wind off"
                    // is a benchmark of nothing in particular.
                    log::info("terrain '{}': scatter '{}' in wind {:.2f}: steady {:.3f} gust {:.3f} "
                              "flutter {:.3f} of height, ring {:.2f} Hz, lag {:.2f} s, curve {:.1f}",
                              node.name, layer.name, windSetting_.active() ? windSetting_.speed : 0.0f,
                              r.steadyGain, r.gustGain, r.flutterGain, r.flutterOmega / wind::kTau,
                              r.swayDelay, r.bendCurve);
                }
                pg.variation.seed = static_cast<std::uint32_t>(layer.structuralHash());
                // Measured over the whole asset, not over this part: a tree normalised to 14 m
                // must scale its bark and its leaves by the same number or the canopy comes off
                // the trunk.
                if (layer.height > 0.0f && !parts.empty()) {
                    const float authored = assetHi.y - assetLo.y;
                    if (authored > 1e-4f) {
                        pg.sourceTransform.scale = glm::vec3(layer.height / authored);
                    }
                }
                if (layer.emissiveIntensity > 0.0f && ecologyLightGain_ > 0.0f && !reuseTerrain) {
                    auto clusters = world::aggregateGlow(*cloud, layer, ecologyGlowCell_,
                                                         pg.variation.seed);
                    log::info("terrain '{}': scatter '{}' glow reduced to {} emitters", node.name,
                              layer.name, clusters.size());
                    nodeGlow.insert(nodeGlow.end(), clusters.begin(), clusters.end());
                }
                if (!reuseTerrain) {
                    log::info("terrain '{}': scatter '{}' placed {} instances", node.name, layer.name,
                              cloud->count());
                }
                // The asset's other materials, each an object identical to this one but for its
                // mesh, its material and its share of the triangle budget. The layer's tint and
                // emission are applied to each part's *own* colour, which is the whole point: the
                // tint turns the leaves green and the bark brown, not both to whichever won.
                std::vector<ProceduralGeometry> subs;
                for (std::size_t part = 1; part < parts.size(); ++part) {
                    ProceduralGeometry sub = pg;
                    sub.name = pg.name + fmt::format("_m{}", part);
                    // ADR-108: one spatial instance set, shared with part 0.
                    sub.partOf = pg.name;
                    sub.source.assetPart = static_cast<int>(part);
                    sub.source.assetMesh = parts[part].mesh;
                    sub.source.meshBudget = partBudget(parts, part, layer.meshBudget);
                    if (parts[part].hasMaterial) {
                        Material m = parts[part].material;
                        m.program = pg.material.program;
                        m.baseColor *= layer.tint;
                        if (layer.emissiveIntensity > 0.0f) {
                            m.emissiveColor = layer.emissiveColor;
                            m.emissiveIntensity = layer.emissiveIntensity;
                        }
                        sub.material = m;
                    }
                    subs.push_back(std::move(sub));
                }
                scene_.procedurals.push_back(std::move(pg));
                for (ProceduralGeometry& sub : subs) {
                    scene_.procedurals.push_back(std::move(sub));
                }
            }
            range.ecologyCount = scene_.procedurals.size() - range.ecologyFirst;
            // Heroes are the large authored solids -- the elder, the monument, the arch -- and the
            // only things in this world that already carry a volume worth colliding with.
            //
            // ...except the ones somebody starred, which is how a hero stops being scenery
            // (ADR-349). Every hero an entity drives is handed over as an exclusion, because a
            // body's collision is `Ground`'s crowd field and not a cylinder pinned to where it
            // happened to be standing when the world was built.
            std::vector<std::string_view> movingHeroes;
            movingHeroes.reserve(entityDescs_.size());
            for (const entity::EntityDesc& e : entityDescs_) {
                movingHeroes.emplace_back(e.driven());
            }
            entity::obstaclesFromHeroes(heroes_, *obstacles_, 0.72f, movingHeroes);
            obstacles_->build();
            // Publish it through §3's seam. Until this happens `TerrainQuery::isOccupied` answers a
            // narrower question than its name suggests -- heroes and the world's edge -- and
            // `hasObstacles()` is what tells a caller the difference between "nothing is there" and
            // "nobody asked".
            obstacleBridge_.setField(obstacles_);
            log::info("terrain '{}': {} navigation obstacles ({} blocking) indexed at {:.1f} m cells",
                      node.name, obstacles_->size(), obstacles_->blockingCount(),
                      obstacles_->cellSize());
            // The whole world is built here, once -- and, since ADR-092, once *ever* for a given
            // map and settings rather than once per rebuild. Chunk meshes are static: only which of
            // a chunk's four meshes is drawn, and whether it is drawn at all, changes per frame.
            // The glow is assigned inside the cache-miss branch below, not here: on a cache hit it
            // comes from the memo instead, and moving `nodeGlow` in both places would empty it.

            const auto buildStart = std::chrono::steady_clock::now();
            // ADR-099: the water bodies first -- the surface mesh's flow lanes are baked from
            // them, so they have to exist before a triangle does. Derived from the same map the
            // ground is, so there is one description of where the river goes and one of which way
            // it runs, and terrain is not asked to know the second.
            // ADR-090 §3: the query is what answers "how much water is over the bed here", and the
            // body uses it once -- to measure how far across each reach there actually is water --
            // rather than every floating leaf asking the height field again on every frame.
            //
            // Outside the reuse branch below on purpose: the bodies are what the floaters, the
            // splines and the flow parameters read every frame, so they have to exist on a cache
            // hit as well, and `node.waterFlow` is folded into the terrain key so a flow change
            // rebuilds the mesh that baked it.
            const world::TerrainQuery waterQuery =
                world::terrainQuery(node.worldMap, &node.ecology, heroes_);
            mutableNode.waterBodies = world::waterBodies(node.worldMap, node.waterFlow, &waterQuery);
            // Where this terrain's meshes start in the scene, so cached ids can be stored relative
            // to it and rebased on the way back in. The absolute ids move whenever anything else in
            // the scene flattens a mesh before this node, which is exactly what placing an asset
            // does.
            const auto meshBase = static_cast<MeshId>(scene_.meshes.size());
            if (reuseTerrain) {
                mutableNode.glow = mutableNode.terrainProducts.glow;
                for (const MeshData& mesh : mutableNode.terrainProducts.meshes) {
                    scene_.addMesh(MeshData(mesh));
                }
                mutableNode.chunks = mutableNode.terrainProducts.chunks;
                for (world::TerrainChunk& chunk : mutableNode.chunks) {
                    for (MeshId& id : chunk.meshes) {
                        if (id != kInvalidMesh && id < mutableNode.terrainProducts.meshes.size()) {
                            id += meshBase;
                        }
                    }
                    if (chunk.water != kInvalidMesh &&
                        chunk.water < mutableNode.terrainProducts.meshes.size()) {
                        chunk.water += meshBase;
                    }
                }
                // And nothing is written back. Copying the cache into `fresh` only to move it
                // straight back is a deep copy of every terrain mesh -- tens of megabytes -- on
                // every rebuild, which is a large fraction of what the cache was built to save.
                reusedProducts = true;
            } else {
                mutableNode.glow = std::move(nodeGlow);
                fresh.glow = mutableNode.glow;
                mutableNode.chunks = world::buildTerrain(
                    node.worldMap, node.terrain, [&](std::size_t, int, MeshData&& mesh) {
                        // Kept as well as installed. A copy of the terrain's meshes is tens of
                        // megabytes and it buys back a third of a second per edit; the alternative
                        // is re-meshing a world every time somebody places a flower.
                        fresh.meshes.push_back(mesh);
                        return scene_.addMesh(std::move(mesh));
                    }, &mutableNode.waterBodies);
                fresh.chunks = mutableNode.chunks;
                for (world::TerrainChunk& chunk : fresh.chunks) {
                    // `TerrainChunk::meshes` is value-initialised, so the slots above `lodLevels`
                    // hold 0 rather than kInvalidMesh. Subtracting the base from those wraps, and at
                    // a base of exactly 1 the wrapped value *is* kInvalidMesh -- which the restore
                    // would then decline to rebase, turning an unused slot into a poisoned one. The
                    // guard is on being a real id rather than on not being the sentinel.
                    for (MeshId& id : chunk.meshes) {
                        if (id != kInvalidMesh && id >= meshBase) {
                            id -= meshBase;
                        }
                    }
                    if (chunk.water != kInvalidMesh && chunk.water >= meshBase) {
                        chunk.water -= meshBase;
                    }
                }
            }
            if (!reusedProducts) {
                mutableNode.terrainProducts = std::move(fresh);
            }
            for (std::size_t c = 0; c < mutableNode.chunks.size(); ++c) {
                const world::TerrainChunk& chunk = mutableNode.chunks[c];
                Entity& e = scene_.addEntity(fmt::format("{}.chunk{}", node.name, c), chunk.meshes[0]);
                e.style = MeshStyle::Lit;
                e.material = node.terrainMaterial;
                // ADR-256: the class, set where it is known for free. The terrain builder is the
                // only thing in the pipeline that knows this mesh is the ground -- downstream it
                // is an entity with a mesh and a material program, and no consumer can recover it.
                e.material.surfaceClass = SurfaceClass::Terrain;
                if (e.material.program.empty() && !node.worldMap.biomes.empty()) {
                    e.material.program = prefixed(sanitise(prefix_), terrainGroundProgramName(node.name));
                }
                e.transform = nodeT;
                e.visible = visible;
                range.restTransforms.emplace_back();
                range.restEmissive.push_back(node.terrainMaterial.emissiveIntensity);
                range.restRoughness.push_back(node.terrainMaterial.roughness);
            }
            // Water second, so it draws after the ground it sits in: the surface is translucent at
            // its edge and the bank has to be there behind it.
            for (std::size_t c = 0; c < mutableNode.chunks.size(); ++c) {
                const world::TerrainChunk& chunk = mutableNode.chunks[c];
                if (chunk.water == kInvalidMesh) {
                    continue;
                }
                Entity& e = scene_.addEntity(fmt::format("{}.water{}", node.name, c), chunk.water);
                // ADR-099: its own style, so the renderer draws it through the water pipeline
                // rather than through the shared metallic-roughness path with an alpha on it.
                // `material.program` is no longer a program to run: it is the *name* the draw
                // finds this surface's settings by in Scene::waters.
                e.style = MeshStyle::Water;
                e.material.baseColor = node.terrain.water.shallowColor;
                e.material.roughness = node.terrain.water.roughness;
                e.material.metallic = 0.0f;
                e.material.doubleSided = true; // a surface seen from under it is still a surface
                e.material.program = prefixed(sanitise(prefix_), terrainWaterProgramName(node.name));
                e.material.surfaceClass = SurfaceClass::Water; // ADR-256
                e.castsShadow = false; // a translucent sheet casting a hard shadow on its own bed
                e.transform = nodeT;
                e.visible = visible;
                range.restTransforms.emplace_back();
                range.restEmissive.push_back(0.0f);
                range.restRoughness.push_back(node.terrain.water.roughness);
            }
            // ADR-099: every water body's centreline, published as a scene spline named
            // "<node>.<body>". A river is a curve through the world and the engine already has a
            // curve type that particle emitters, path deformers, instance distributions and the
            // camera all read -- so publishing it costs one conversion and means a mist emitter
            // that runs down the river is a scene file away, with no new emitter kind.
            for (const world::WaterBody& body : mutableNode.waterBodies.bodies) {
                if (body.centre().size() < 2) {
                    continue;
                }
                spatial::Spline sp;
                sp.name = prefixed(sanitise(prefix_), fmt::format("{}.{}", node.name, body.name()));
                sp.kind = spatial::SplineKind::Polyline;
                sp.generator = spatial::SplineGenerator::Points;
                sp.points.reserve(body.centre().size());
                for (const glm::vec3& c : body.centre()) {
                    spatial::SplinePoint sp0;
                    sp0.position = transformPoint(nodeT, c);
                    sp.points.push_back(sp0);
                }
                scene_.splines.splines.push_back(std::move(sp));
            }
            if (node.terrain.water.enabled) {
                WaterSurface surface;
                surface.program = prefixed(sanitise(prefix_), terrainWaterProgramName(node.name));
                surface.settings = node.terrain.water;
                surface.fastestFlow = std::max(mutableNode.waterBodies.fastest(), 0.05f);
                mutableNode.waterSurfaceIndex = static_cast<int>(scene_.waters.size());
                scene_.waters.push_back(std::move(surface));
            }
            const auto triangles = [&] {
                std::size_t t = 0;
                for (const world::TerrainChunk& chunk : mutableNode.chunks) {
                    t += scene_.meshes[chunk.meshes[0]].indices.size() / 3;
                }
                return t;
            }();
            const auto buildMs = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - buildStart)
                                     .count();
            const auto wet = static_cast<std::size_t>(std::count_if(
                mutableNode.chunks.begin(), mutableNode.chunks.end(),
                [](const world::TerrainChunk& c) { return c.water != kInvalidMesh; }));
            log::info("terrain '{}': {} chunks ({} with water), {} triangles at LOD 0, {} in {:.0f} ms",
                      node.name, mutableNode.chunks.size(), wet, triangles,
                      reuseTerrain ? "reused" : "built", buildMs);
            // What the water actually came out as. A river whose speed or direction is wrong is
            // invisible in a still frame and obvious in one line of log, and a body that silently
            // failed to derive is exactly the failure this codebase keeps shipping.
            for (const world::WaterBody& body : mutableNode.waterBodies.bodies) {
                const glm::vec2 dir = body.flowAt(glm::vec2(body.centre().front().x,
                                                            body.centre().front().z)).direction;
                log::info("  water '{}': {} {:.0f} m, half width {:.1f} m, descent {:.1f} m, "
                          "{:.2f} m/s, heading ({:.2f}, {:.2f})",
                          body.name(), world::waterKindName(body.course.kind), body.length(),
                          body.halfWidth(), body.course.descent, body.speed, dir.x, dir.y);
            }
            break;
        }
        case NodeKind::Particles: {
            ParticleSystem ps = node.particleRest;
            prefixFieldReferences(ps, sanitise(prefix_));
            ps.position = transformPoint(nodeT, ps.position);
            ps.attractorPosition = transformPoint(nodeT, ps.attractorPosition);
            ps.enabled = ps.enabled && visible;
            range.particleIndex = static_cast<int>(scene_.particles.size());
            scene_.particles.push_back(std::move(ps));
            break;
        }
        case NodeKind::Procedural: {
            ProceduralGeometry pg = node.proceduralRest;
            // ADR-044: a mesh source is an imported asset. Resolving it here rather than in the
            // renderer keeps makeSourceMesh a pure function of the spec, and reuses the same
            // per-asset offsets the glTF nodes use so two objects sharing an asset share its
            // meshes and textures.
            // The resolved mesh and material go into the node's rest copy as well as this
            // frame's: applyProceduralParameters() rebuilds `live` from `rest` every frame, so a
            // material written only here is overwritten before it ever reaches the GPU.
            CompositionNode& mutableNode = *nodePtr;
            mutableNode.proceduralSubRest.clear();
            // Cutout foliage: the base-colour texture whose alpha is the mask. Loaded here for the
            // same reason the mesh is, and written into the node's rest copy as well as this
            // frame's -- `applyProceduralParameters` rebuilds `live` from `rest` every frame, so a
            // texture set only on `pg` is gone before it reaches the GPU.
            if (!pg.baseColorTexturePath.empty() && !pg.material.baseColorTexture.valid()) {
                auto image = registry_.loadImage(pg.baseColorTexturePath, true);
                if (!image) {
                    log::warn("composition '{}': node '{}': base colour texture: {}", name_, node.name,
                              image.error().message);
                } else {
                    const TextureId id = scene_.addTexture((*image)->image);
                    pg.material.baseColorTexture.texture = id;
                    pg.material.baseColorTexture.wrapU = WrapMode::Clamp;
                    pg.material.baseColorTexture.wrapV = WrapMode::Clamp;
                    mutableNode.proceduralRest.material.baseColorTexture = pg.material.baseColorTexture;
                }
            }
            std::vector<ProceduralGeometry> subs;
            if (pg.source.kind == PrimitiveKind::Mesh && !pg.source.asset.empty()) {
                const std::vector<AssetPart> parts =
                    resolveMeshParts(pg.source.asset, "procedural '" + node.name + "'");
                // The asset's own material per part, unless the scene deliberately overrode it. An
                // imported material with no textures is the same "flat grey blob" problem
                // procedural geometry has, so its texture refs are remapped into this scene.
                const auto resolveMaterial = [&](const AssetPart& part) {
                    Material resolved = part.material;
                    resolved.program = node.proceduralRest.material.program;
                    if (node.proceduralMaterialAuthored) {
                        // Textures come from the asset, factors from the author. That split is
                        // what lets one curated library become several biomes: the mesh and its
                        // maps stay put while base colour, emission and roughness are retuned
                        // per scene. An author who wants the asset's own colour simply omits
                        // the material block. An authored block speaks for every part -- the
                        // author wrote one material and gets one set of factors.
                        const Material& authored = node.proceduralRest.material;
                        resolved.baseColor = authored.baseColor;
                        resolved.opacity = authored.opacity;
                        resolved.emissiveColor = authored.emissiveColor;
                        resolved.emissiveIntensity = authored.emissiveIntensity;
                        resolved.roughness = authored.roughness;
                        resolved.metallic = authored.metallic;
                    }
                    return resolved;
                };
                mutableNode.materialPartNames.clear();
                for (const AssetPart& part : parts) {
                    mutableNode.materialPartNames.push_back(part.name);
                }
                if (parts.size() > 1) {
                    // Say what the parts are called. An author writing a reaction against this
                    // asset has to know what to write, and the alternative to printing it is
                    // reading the glTF by hand or guessing at an index.
                    std::string named;
                    for (std::size_t i = 0; i < parts.size(); ++i) {
                        if (!named.empty()) {
                            named += ", ";
                        }
                        named += fmt::format("{}={}", i, parts[i].name.empty() ? "<unnamed>" : parts[i].name);
                    }
                    log::info("procedural '{}': material parts {}", node.name, named);
                }
                if (!parts.empty()) {
                    pg.source.assetMesh = parts[0].mesh;
                    pg.source.meshBudget = partBudget(parts, 0, pg.source.meshBudget);
                    mutableNode.proceduralRest.source.assetMesh = pg.source.assetMesh;
                    mutableNode.proceduralRest.source.meshBudget = pg.source.meshBudget;
                    if (parts[0].hasMaterial) {
                        pg.material = resolveMaterial(parts[0]);
                        mutableNode.proceduralRest.material = pg.material;
                    }
                }
                // The asset's other materials. Each is an object identical to part 0 -- the same
                // distribution, seed, variation, deformers and effectors -- differing only in its
                // mesh and its material, so the parts of one asset stay registered with each other
                // by construction rather than by luck.
                for (std::size_t part = 1; part < parts.size(); ++part) {
                    ProceduralGeometry sub = mutableNode.proceduralRest;
                    // ADR-108: one spatial instance set, shared with part 0. Set on the rest copy
                    // as well as this frame's, because rest is the authored baseline every later
                    // frame derives from.
                    sub.partOf = sanitise(prefix_) + node.name;
                    sub.source.assetPart = static_cast<int>(part);
                    sub.source.assetMesh = parts[part].mesh;
                    sub.source.meshBudget = partBudget(parts, part, node.proceduralRest.source.meshBudget);
                    if (parts[part].hasMaterial) {
                        sub.material = resolveMaterial(parts[part]);
                    }
                    mutableNode.proceduralSubRest.push_back(sub);
                    subs.push_back(std::move(sub));
                }
            }
            pg.name = sanitise(prefix_) + node.name;
            prefixFieldReferences(pg, sanitise(prefix_));
            pg.distributionTransform = Transform::fromMatrix(nodeT.matrix() * pg.distributionTransform.matrix());
            pg.visible = pg.visible && visible;
            range.proceduralIndex = static_cast<int>(scene_.procedurals.size());
            range.proceduralSubCount = subs.size();
            scene_.procedurals.push_back(std::move(pg)); // generated by rebuildProcedurals() after the loop
            for (std::size_t part = 0; part < subs.size(); ++part) {
                ProceduralGeometry& sub = subs[part];
                sub.name = sanitise(prefix_) + node.name + fmt::format("_m{}", part + 1);
                prefixFieldReferences(sub, sanitise(prefix_));
                sub.distributionTransform =
                    Transform::fromMatrix(nodeT.matrix() * sub.distributionTransform.matrix());
                sub.visible = sub.visible && visible;
                scene_.procedurals.push_back(std::move(sub));
            }
            break;
        }
        case NodeKind::Spline: {
            spatial::Spline sp = node.splineRest;
            sp.name = sanitise(prefix_) + node.name;
            foldSplineFrame(sp, nodeT);
            range.splineIndex = static_cast<int>(scene_.splines.splines.size());
            scene_.splines.splines.push_back(std::move(sp));
            break;
        }
        case NodeKind::Sdf: {
            SdfObject so = node.sdfRest;
            so.name = sanitise(prefix_) + node.name;
            prefixFieldReferences(so, sanitise(prefix_));
            so.transform = compose(nodeT, so.transform);
            so.visible = so.visible && visible;
            range.sdfIndex = static_cast<int>(scene_.sdfs.size());
            scene_.sdfs.push_back(std::move(so)); // meshed by rebuildSdfs() after the loop
            break;
        }
        case NodeKind::Field: {
            spatial::FieldSpec f = node.fieldRest;
            f.name = sanitise(prefix_) + node.name;
            prefixFieldReferences(f, sanitise(prefix_));
            foldFieldFrame(f, nodeT);
            f.enabled = f.enabled && visible;
            range.fieldIndex = static_cast<int>(scene_.fields.fields.size());
            scene_.fields.fields.push_back(std::move(f));
            break;
        }
        case NodeKind::Scene: {
            if (!node.child) {
                break;
            }
            node.child->ensureBuilt();
            const Scene& cs = node.child->scene();
            for (const spatial::GridField& g : cs.fields.grids) {
                scene_.fields.grids.push_back(g); // the child already prefixed the names
            }
            const auto meshOffset = static_cast<MeshId>(scene_.meshes.size());
            const auto textureOffset = static_cast<TextureId>(scene_.textures.size());
            for (const auto& mesh : cs.meshes) {
                scene_.meshes.push_back(mesh);
            }
            for (const auto& texture : cs.textures) {
                scene_.textures.push_back(texture);
            }
            for (const Entity& src : cs.entities) {
                Entity e = src;
                offsetEntityIds(e, meshOffset, textureOffset);
                e.name = node.name + "/" + src.name;
                e.transform = compose(nodeT, src.transform);
                e.visible = visible && src.visible;
                scene_.entities.push_back(std::move(e));
                range.restTransforms.push_back(src.transform);
                range.restEmissive.push_back(src.material.emissiveIntensity);
                range.restRoughness.push_back(src.material.roughness);
            }
            for (const PunctualLight& src : cs.lights) {
                PunctualLight light = src;
                light.name = node.name + "/" + src.name;
                light.position = transformPoint(nodeT, src.position);
                light.direction = transformDirection(nodeT, src.direction);
                scene_.addLight(std::move(light));
            }
            const float scale = lengthScale(nodeT);
            const std::string childPrefix = nestedPrefix(node);
            for (const ParticleSystem& src : cs.particles) {
                ParticleSystem ps = src;
                prefixFieldReferences(ps, childPrefix);
                ps.position = transformPoint(nodeT, src.position);
                ps.attractorPosition = transformPoint(nodeT, src.attractorPosition);
                ps.extent = src.extent * scale;
                ps.sizeStart = src.sizeStart * scale;
                ps.sizeEnd = src.sizeEnd * scale;
                ps.enabled = src.enabled && visible;
                scene_.particles.push_back(std::move(ps));
            }
            range.firstProcedural = scene_.procedurals.size();
            range.proceduralCount = cs.procedurals.size();
            for (const ProceduralGeometry& src : cs.procedurals) {
                ProceduralGeometry pg = src;
                pg.name = childPrefix + src.name;
                prefixFieldReferences(pg, childPrefix);
                pg.distributionTransform = Transform::fromMatrix(nodeT.matrix() * src.distributionTransform.matrix());
                pg.visible = pg.visible && visible;
                scene_.procedurals.push_back(std::move(pg));
            }
            range.firstSpline = scene_.splines.splines.size();
            range.splineCount = cs.splines.splines.size();
            for (const spatial::Spline& src : cs.splines.splines) {
                spatial::Spline sp = src;
                sp.name = childPrefix + src.name;
                foldSplineFrame(sp, nodeT);
                scene_.splines.splines.push_back(std::move(sp));
            }
            range.firstSdf = scene_.sdfs.size();
            range.sdfCount = cs.sdfs.size();
            for (const SdfObject& src : cs.sdfs) {
                SdfObject so = src;
                so.name = childPrefix + src.name;
                prefixFieldReferences(so, childPrefix);
                so.transform = compose(nodeT, src.transform);
                so.visible = src.visible && visible;
                scene_.sdfs.push_back(std::move(so));
            }
            range.firstMaterial = scene_.materialPrograms.size();
            range.materialCount = cs.materialPrograms.size();
            for (const MaterialProgram& src : cs.materialPrograms) {
                MaterialProgram mp = src;
                mp.name = childPrefix + src.name;
                prefixFieldReferences(mp, childPrefix);
                scene_.materialPrograms.push_back(std::move(mp));
            }
            range.firstField = scene_.fields.fields.size();
            range.fieldCount = cs.fields.fields.size();
            for (const spatial::FieldSpec& src : cs.fields.fields) {
                spatial::FieldSpec f = src;
                f.name = childPrefix + src.name;
                prefixFieldReferences(f, childPrefix);
                foldFieldFrame(f, nodeT);
                f.enabled = src.enabled && visible;
                scene_.fields.fields.push_back(std::move(f));
            }
            range.childMeshVersion = cs.meshVersion;
            range.childEntityCount = cs.entities.size();
            range.childParticleCount = cs.particles.size();
            break;
        }
        }
        range.entityCount = scene_.entities.size() - range.firstEntity;
        range.particleCount = scene_.particles.size() - range.firstParticle;
        ranges_.push_back(std::move(range));
    }

    windBodies_.clear();
    applyWindBodies();
    applyCanopyEmitters();

    // Composition (ADR-038) contributes reserved fields, so density filters and effectors can
    // reference them by name like any other field.
    compositionData_.appendFields(scene_.fields);
    scene_.composition = compositionData_;

    rebuildProcedurals();
    rebuildSdfs();

    // ADR-278: the lights the scene file itself authored, added after the node assets' own lights
    // and before the default key, which is the whole of the precedence. A light naming a node is
    // authored in that node's local frame; its world placement is composed here and refreshed every
    // frame in `applyParameters`, so a lamp on a moving thing moves with it.
    authoredLightFirst_ = scene_.lights.size();
    authoredLightNodeIndex_.assign(authoredLights_.size(), kNoNode);
    for (std::size_t i = 0; i < authoredLights_.size(); ++i) {
        const AuthoredLight& a = authoredLights_[i];
        PunctualLight light = a.light;
        light.name = sanitise(prefix_) + light.name;
        if (!a.node.empty()) {
            const auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const auto& n) {
                return n->name == a.node;
            });
            if (it == nodes_.end()) {
                // A warning rather than a refusal, on the same terms a node's missing parent gets:
                // the light still exists, where the file placed it, and the name is in the log.
                log::warn("composition '{}': light '{}' names node '{}', which does not exist; the "
                          "light stays where it was authored",
                          name_, a.light.name, a.node);
            } else {
                const std::size_t index = static_cast<std::size_t>(std::distance(nodes_.begin(), it));
                authoredLightNodeIndex_[i] = index;
                const Transform nodeT = nodeWorldTransform(**it);
                light.position = transformPoint(nodeT, a.light.position);
                light.direction = transformDirection(nodeT, a.light.direction);
                light.up = transformDirection(nodeT, a.light.up);
            }
        }
        scene_.addLight(std::move(light));
    }

    // A rig supplies the lighting; without one, a world with no authored lights still gets a key
    // so it is not lit by ambient alone (ADR-033/034). ADR-278: a scene that authors its own lights
    // reaches here with `scene_.lights` non-empty, so authoring one light is what turns the default
    // off -- which is the behaviour the two lab fixtures believed they already had.
    rigLightCount_ = 0;
    addedKeyLight_ = !lightRig_ && scene_.lights.empty();
    if (addedKeyLight_) {
        scene_.addLight(defaultKeyLight());
    }

    environmentTextureCache_.clear();
    resolveEnvironmentMap();

    // Framing: lit geometry when there is any, otherwise the particle emitters.
    const auto [lo, hi] = scene_.bounds();
    const bool hasLit = std::any_of(scene_.entities.begin(), scene_.entities.end(), [&](const Entity& e) {
        return e.visible && e.style == MeshStyle::Lit && e.mesh < scene_.meshes.size();
    }) || std::any_of(scene_.procedurals.begin(), scene_.procedurals.end(), [](const ProceduralGeometry& pg) {
        return pg.visible && !pg.instances.empty();
    });
    if (hasLit) {
        center_ = (lo + hi) * 0.5f;
        radius_ = std::max(glm::length(hi - lo) * 0.5f, 0.05f);
    } else if (!scene_.particles.empty()) {
        glm::vec3 plo(std::numeric_limits<float>::max());
        glm::vec3 phi(std::numeric_limits<float>::lowest());
        for (const auto& ps : scene_.particles) {
            const float reach = std::max({ps.extent.x, ps.extent.y, ps.extent.z, 0.5f});
            plo = glm::min(plo, ps.position - glm::vec3(reach));
            phi = glm::max(phi, ps.position + glm::vec3(reach));
        }
        center_ = (plo + phi) * 0.5f;
        radius_ = std::max(glm::length(phi - plo) * 0.5f, 0.05f);
    } else {
        center_ = glm::vec3(0.0f);
        radius_ = 1.0f;
    }

    ++scene_.meshVersion;
    // And the textures, **only if a texture actually changed**. This used to be unconditional, and
    // it is the renderer's signal to destroy and re-create every GPU texture in the scene: 44 of
    // them per flatten on Glowmere, for edits -- starring a hero, moving a brush -- that do not
    // touch an image. The table above is rebuilt from the same cached assets every time, so the
    // usual answer is that nothing changed and the digests agree.
    //
    // Restored rather than left alone when they do agree: `Scene::addTexture` bumps the version
    // itself, and this function calls it for the environment map and for imported materials, so a
    // rebuild that re-adds the same images has already moved the counter by the time we get here.
    // Putting it back is what makes "no texture changed" mean "no re-upload" rather than "one fewer
    // re-upload than before".
    const auto textureDigestStart = std::chrono::steady_clock::now();
    const std::uint64_t textureDigest = textureTableDigest(scene_.textures);
    const double textureDigestMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - textureDigestStart)
            .count();
    if (textureDigest == textureDigest_ && flattens_ > 1) {
        scene_.textureVersion = textureVersionBefore;
    } else {
        scene_.textureVersion = textureVersionBefore + 1;
    }
    textureDigest_ = textureDigest;
    // The procedural vector has just been rebuilt from the node list, so every index into it is
    // new. Costs measured against the old one describe objects that no longer exist.
    proceduralRebuild_.clear();
    // ADR-193: the walkers' navigator is made of the obstacle field this rebuild just replaced and
    // of a NavGrid baked from it, and `EntityWorld` holds it **by value**. It was installed once,
    // from `installEntities()`, which this function does not call -- so every rebuild left the
    // characters pathing against the previous world while `TerrainQuery`, rebuilt on every call,
    // saw the new one. The bridge above was kept in step; the characters were not.
    //
    // The same sentence this function opens with, applied one layer further out: "a stale solid is
    // a character walking round nothing, and a missing one is a character walking through a tree."
    //
    // Only when there is somebody to walk. Measured on Glowmere at 170 ms against a 1676 ms
    // flatten -- 10% of an operation that is already the expensive one, which is the right price
    // for the entity layer agreeing with the world it is standing in.
    if (!entityWorld_.empty()) {
        entityWorld_.setNavigator(buildNavigator());
    }
    log::info("composition '{}': flattened {} node(s) -> {} entities, {} meshes, {} procedurals in {:.1f} ms"
              " ({} texture(s), digest {:.2f} ms, {})",
              name_, nodes_.size(), scene_.entities.size(), scene_.meshes.size(),
              scene_.procedurals.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart)
                  .count(),
              scene_.textures.size(), textureDigestMs,
              scene_.textureVersion == textureVersionBefore ? "unchanged, no re-upload"
                                                            : "changed, re-uploading");
    // A material program that nothing carries is skipped silently and the surface keeps its authored
    // material, which is indistinguishable from a program that ran and did nothing. Said out loud
    // once per rebuild (ADR-176's dangling-name rule): it costs a string compare per material and it
    // is the difference between finding a typo now and finding it in a render.
    if (const std::vector<std::string> dangling = danglingMaterialPrograms(scene_); !dangling.empty()) {
        std::string names;
        for (const std::string& n : dangling) {
            names += names.empty() ? n : ", " + n;
        }
        log::warn("composition '{}': {} material program(s) named by a surface and not carried by the "
                  "scene: {} -- those surfaces keep their authored material and no program runs",
                  name_, dangling.size(), names);
    }
    dirty_ = false;
}

// ---- per-frame ---------------------------------------------------------------------------------

void Composition::update(const FrameTime& time) {
    // This frame flattens, so from here on `visualPlacement` answers from the flattening again
    // rather than from the last seek's replay (ADR-700).
    seekPlacementLive_ = false;
    if (graphDirty_) {
        if (auto r = evaluateGraph(time.renderTime); !r) {
            log::warn("graph '{}': {}", graph_ ? graph_->name : std::string("?"), r.error().message);
        }
    }
    if (dirty_) {
        rebuild();
    }
    // Children first so their parameters apply before this level reads their scenes; a child
    // whose structure changed (meshes added or removed) forces a re-flatten.
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Scene || !node.child) {
            continue;
        }
        node.child->update(time);
        const Scene& cs = node.child->scene();
        if (i >= ranges_.size() || cs.meshVersion != ranges_[i].childMeshVersion ||
            cs.entities.size() != ranges_[i].childEntityCount ||
            cs.particles.size() != ranges_[i].childParticleCount) {
            dirty_ = true;
        }
    }
    if (dirty_) {
        rebuild();
    }

    currentTime_ = time.renderTime;
    const auto dt = static_cast<float>(time.deltaTime);
    if (rootRotationSpeed_ != nullptr) {
        rootAngle_ += rootRotationSpeed_->value() * dt;
    }
    if (cameraOrbitSpeed_ != nullptr) {
        cameraAngle_ += cameraOrbitSpeed_->value() * dt;
    }
    applyParameters();
    // ADR-346: after `applyParameters`, not before it. The day/night cycle asks for the map swap
    // from inside that call, so resolving first meant the swap landed a frame late -- and in a
    // one-frame headless render it never landed at all, which is how this was found: the night map
    // simply stopped appearing in the log.
    if (environmentDirty_) {
        resolveEnvironmentMap();
    }
    // After the parameters, because a node's position is one of them: a hero follows the object it
    // describes, and where that object is has only just been decided for this frame.
    syncHeroesToNodes();
    // The chase trails, recorded at the same moment and for the same reason: this is the frame at
    // which where a node *is* has stopped being a question.
    recordFollowTrails();
    // And after *that*, because the aim follows where the hero is now. Putting it inside
    // `applyParameters` would have aimed at last frame's position, which is a lag nobody would ever
    // see and a wrongness anybody could later trip over.
    applyDirectedAim();
    // ADR-099 §13. After the parameters, because a floating layer's node transform is one of them,
    // and before the culling, so a drifting layer's bounds are this frame's rather than last
    // frame's -- a raft that has moved out of frame must be culled on where it is now.
    updateFloaters(time.renderTime);
    updateCharacters(time);
    cullEntityNodes();
}

// ADR-086. Two things, in order: push each node's authored animation request into its rigs (only
// when the request changed, so a behaviour driving a player directly is left alone), then pose
// every rig in the scene against this frame's timeline second.
void Composition::updateCharacters(const FrameTime& time) {
    if (scene_.rigs.empty()) {
        rigStats_ = RigStats{};
        return;
    }
    for (const auto& nodePtr : nodes_) {
        CompositionNode& node = *nodePtr;
        if (node.rigs.empty() || node.animation.state.empty()) {
            continue;
        }
        if (node.animation.state != node.animationApplied) {
            // A new request: it is made now, and it keeps that second for the rest of its life.
            //
            // Except the first one. The state a scene *file* authors has been in effect since the
            // piece began; anchoring it to `time.renderTime` anchors it to whenever the engine
            // happened to run its first update, which is not a property of the piece at all. The
            // symptom is that a character's idle sits at a different point in its loop depending on
            // where you seeked from: an engine seeked straight to 16.7 s and one that played from
            // 3.3 s to 16.7 s disagreed on 98 joint matrices at the same second (renderer forensics
            // Phase 9.2). A state requested *during* playback -- by a behaviour, a cue, the
            // sequencer -- still starts when it was requested, which is what those mean.
            const bool authoredFromTheStart = node.animationApplied.empty();
            node.animationApplied = node.animation.state;
            node.animationAppliedAt = authoredFromTheStart ? 0.0 : time.renderTime;
            node.animationPushed = false;
        }
        if (node.animationPushed) {
            continue; // already in the rigs, and a behaviour may have moved them on since
        }
        bool applied = false;
        for (const RigId id : node.rigs) {
            if (id >= scene_.rigs.size()) {
                continue;
            }
            SkinnedRig& rig = scene_.rigs[id];
            const int index = rig.player.stateIndex(node.animation.state);
            if (index < 0) {
                continue;
            }
            const float blend = node.animation.blend >= 0.0f
                                    ? node.animation.blend
                                    : rig.player.blendTimeFor(rig.player.currentState(), node.animation.state);
            // ADR-547: phase-matched when the target state asks for it and this rig has been
            // analysed. The overload falls back to frame zero on its own when either is missing,
            // so this is the only call site and there is no second code path to keep in step.
            if (rig.player.play(node.animation.state, node.animationAppliedAt, blend, rig.phaseMatch())) {
                if (node.animationRebase) {
                    // play() returns true without restarting a state it is already in, which is
                    // exactly right for a behaviour and exactly wrong for a timeline cue (ADR-089).
                    rig.player.restart(node.animationAppliedAt);
                }
                // setSpeed rebases to keep local clip time continuous; called at the phase origin
                // the elapsed time is zero, so the origin survives.
                rig.player.setSpeed(node.animation.speed, node.animationAppliedAt);
                rig.player.setLooping(node.animationLoop); // ADR-821: a cue's once, or the state's own
                applied = true;
            }
        }
        if (!applied) {
            log::warn("node '{}': no animation state named '{}'", node.name, node.animation.state);
        }
        node.animationPushed = true; // whether or not it landed: do not warn again every frame
    }
    rigStats_ = updateRigs(scene_, time);
}

const ClipSemanticsTable* Composition::clipSemanticsFor(const std::string& nodeName) const {
    const CompositionNode* node = findNode(nodeName);
    if (node == nullptr) {
        return nullptr;
    }
    for (const RigId id : node->rigs) {
        if (id < scene_.rigs.size()) {
            return scene_.rigs[id].semantics();
        }
    }
    return nullptr;
}

bool Composition::setNodeAnimation(const std::string& nodeName, const std::string& state, double now,
                                   float blend, float speed, bool rebase, std::optional<bool> loop) {
    CompositionNode* node = findNode(nodeName);
    if (node == nullptr) {
        return false;
    }
    // Idempotent, so a sequencer may call this every frame: the same state at the same phase origin
    // with the same rate is already in force and re-pushing it would restart the cross-fade.
    if (node->animationPushed && node->animation.state == state && node->animationApplied == state &&
        node->animationAppliedAt == now && node->animation.blend == blend &&
        node->animation.speed == speed && node->animationRebase == rebase && node->animationLoop == loop) {
        return true;
    }
    node->animationLoop = loop;
    node->animation.state = state;
    node->animation.blend = blend;
    node->animation.speed = speed;
    node->animationApplied = state;
    node->animationAppliedAt = now;
    node->animationPushed = false;
    node->animationRebase = rebase;
    return true;
}

// ---- multiple cameras (ADR-245) -----------------------------------------------------------------

void Composition::registerCameraChannels(params::ParameterSet& params, float reach) {
    // ADR-245: every authored camera's own channels, under `cameras/<slug>/`. Ordinary parameters,
    // which is the entire animation story: a camera is keyframed by putting timeline keys on these,
    // modulated by routing audio at them, and "static" by leaving them alone. There is no camera
    // animation system because there did not need to be one.
    cameraDirection_.ensureMainCamera();
    cameraChannels_.clear();
    for (const CameraRig& rig : cameraDirection_.cameras) {
        if (rig.id == kMainCamera) {
            continue; // its channels are the `camera/*` handles above
        }
        const std::string p = prefix_ + rig.channelPrefix();
        CameraChannels ch;
        ch.id = rig.id;
        ch.position = &params.add(vec3Desc(p + "position", rig.position, -1e5f, 1e5f, -reach, reach));
        ch.target = &params.add(vec3Desc(p + "target", rig.target, -1e5f, 1e5f, -reach, reach));
        ch.fov = &params.add(floatDesc(p + "fov", rig.fovDegrees, 5.0f, 120.0f, 20.0f, 90.0f));
        ch.focalLength = &params.add(floatDesc(p + "focalLength", rig.focalLength, 0.0f, 800.0f, 0.0f, 200.0f));
        ch.splineT = &params.add(floatDesc(p + "splineT", rig.splineT, -10.0f, 10.0f, 0.0f, 1.0f));
        ch.lookAhead = &params.add(floatDesc(p + "lookAhead", rig.lookAhead, -100.0f, 100.0f, 0.0f, 10.0f));
        ch.splineOffset =
            &params.add(vec3Desc(p + "splineOffset", rig.splineOffset, -1e3f, 1e3f, -5.0f, 5.0f));
        cameraChannels_.push_back(ch);
    }
}

CameraPose Composition::evaluateMainCamera() const {
    // Byte for byte the placement this file has always done, moved into a function so the camera
    // director can ask for it like it asks for any other camera's. Orbit lives only here: it
    // integrates `orbitSpeed * dt` into `cameraAngle_`, which makes it the one placement in this
    // engine that depends on how the playhead arrived rather than on where it is. Authored cameras
    // do not get it (scene/camera_rig.hpp).
    CameraPose pose;
    const float fit = fitDistance();
    const float distance =
        cameraDistance_ != nullptr ? cameraDistance_->value() : cameraDistanceSetting_.value_or(fit);
    const float height = cameraHeight_ != nullptr
                             ? cameraHeight_->value()
                             : cameraHeightSetting_.value_or(center_.y + radius_ * 0.35f);
    pose.fovDegrees = cameraFov_ != nullptr ? cameraFov_->value() : cameraFovSetting_;
    const int cameraMode = cameraMode_ != nullptr ? cameraMode_->value() : cameraModeSetting_;
    const spatial::Spline* cameraSpline =
        cameraMode == 2 && !cameraSplineSetting_.empty()
            ? scene_.splines.find(prefixed(sanitise(prefix_), cameraSplineSetting_))
            : nullptr;
    if (cameraMode == 2 && cameraSpline != nullptr) {
        // Spline camera: position at splineT (fraction of the length, wrapped for closed splines),
        // target lookAhead units further along, offset expressed in the local frame.
        const float length = cameraSpline->length();
        const float tRaw = cameraSplineT_ != nullptr ? cameraSplineT_->value() : 0.0f;
        const float t = cameraSpline->closed ? tRaw - std::floor(tRaw) : std::clamp(tRaw, 0.0f, 1.0f);
        const float lookAhead = cameraLookAhead_ != nullptr ? cameraLookAhead_->value() : 2.0f;
        const glm::vec3 offset = cameraSplineOffset_ != nullptr ? cameraSplineOffset_->value() : glm::vec3(0.0f);
        const spatial::SplineSample at = cameraSpline->sampleByDistance(t * length);
        const spatial::SplineSample ahead = cameraSpline->sampleByDistance(t * length + lookAhead);
        const glm::vec3 frameOffset = at.binormal * offset.x + at.normal * offset.y + at.tangent * offset.z;
        pose.position = at.position + frameOffset;
        pose.target = ahead.position + at.binormal * offset.x + at.normal * offset.y;
        ensureDistinctAim(pose, at.tangent);
    } else if (cameraMode == 1) {
        // Free camera: explicit position and target (keyable on the timeline, modulatable).
        pose.position = cameraPosition_ != nullptr ? cameraPosition_->value() : cameraPositionSetting_;
        pose.target = cameraTarget_ != nullptr ? cameraTarget_->value() : cameraTargetSetting_;
        ensureDistinctAim(pose);
        if ((frameCounter_++ % 120) == 0) {
            log::debug("free camera pos ({:.1f} {:.1f} {:.1f}) target ({:.1f} {:.1f} {:.1f})", pose.position.x,
                       pose.position.y, pose.position.z, pose.target.x, pose.target.y, pose.target.z);
        }
    } else {
        pose.position =
            center_ + glm::vec3(std::sin(cameraAngle_) * distance, 0.0f, std::cos(cameraAngle_) * distance);
        pose.position.y = height;
        pose.target = center_;
    }
    return pose;
}

CameraPose Composition::evaluateAuthoredCamera(const CameraRig& rig, const CameraChannels* channels) const {
    CameraPose pose;
    // The parameters when the composition is attached, the authored bases when it is not: the same
    // rule every other property in this file follows, and what lets a test evaluate a camera
    // without standing up a parameter set.
    pose.position = channels != nullptr ? channels->position->value() : rig.position;
    pose.target = channels != nullptr ? channels->target->value() : rig.target;
    pose.fovDegrees = channels != nullptr ? channels->fov->value() : rig.fovDegrees;
    pose.focalLength = channels != nullptr ? channels->focalLength->value() : rig.focalLength;
    if (rig.placement == CameraPlacement::Spline && !rig.spline.empty()) {
        if (const spatial::Spline* spline = scene_.splines.find(prefixed(sanitise(prefix_), rig.spline));
            spline != nullptr) {
            const float length = spline->length();
            const float tRaw = channels != nullptr ? channels->splineT->value() : rig.splineT;
            const float t = spline->closed ? tRaw - std::floor(tRaw) : std::clamp(tRaw, 0.0f, 1.0f);
            const float lookAhead = channels != nullptr ? channels->lookAhead->value() : rig.lookAhead;
            const glm::vec3 offset =
                channels != nullptr ? channels->splineOffset->value() : rig.splineOffset;
            const spatial::SplineSample at = spline->sampleByDistance(t * length);
            const spatial::SplineSample ahead = spline->sampleByDistance(t * length + lookAhead);
            pose.position = at.position + at.binormal * offset.x + at.normal * offset.y + at.tangent * offset.z;
            pose.target = ahead.position + at.binormal * offset.x + at.normal * offset.y;
            ensureDistinctAim(pose, at.tangent);
            return pose;
        }
        // A spline camera whose spline is not in the scene falls back to its free pose rather than
        // to the origin: a camera that silently jumps to (0,0,0) is a bug that looks like a cut.
    }
    // A camera that watches something: resolved against where that node is *now*, which is what
    // lets one camera cover an event that moves. The node not being there leaves the channels in
    // charge rather than sending the camera to the origin.
    if (!rig.followNode.empty()) {
        if (const CompositionNode* node = findNode(rig.followNode); node != nullptr) {
            const Transform now = nodeWorldTransform(*node);
            // Where the subject is, or -- for a chase -- where it was. Both the position and the
            // facing come from the same instant, so a camera behind a turning subject stays behind
            // the heading it had then rather than snapping to the one it has now.
            FollowTrail::Sample at{.seconds = currentTime_,
                                   .position = now.position,
                                   .rotation = now.rotation};
            if (rig.followLagSeconds > 0.0) {
                at = followTrailAt(rig.followNode, currentTime_ - rig.followLagSeconds, at);
            }
            // World axes by default, which is every rig that existed before this; the subject's own
            // frame when asked, which is what makes "behind" mean behind.
            const glm::vec3 offset =
                rig.followLocal ? at.rotation * rig.followOffset : rig.followOffset;
            pose.position = at.position + offset;

            // The whole of camera collision: keep the eye above the surface. Applied after the
            // offset rather than to the subject, because it is the camera that hits the hill.
            //
            // `surfaceAt` rather than `heightAt`, so the camera does not dive through a lake on its
            // way round a shoreline -- a shot from under water is a decision, not a side effect of
            // chasing something downhill.
            if (rig.followClearance > 0.0f) {
                const world::TerrainQuery ground = terrainQuery();
                if (ground.valid()) {
                    const float floorY =
                        ground.surfaceAt(glm::vec2(pose.position.x, pose.position.z)) +
                        rig.followClearance;
                    // Raised, never lowered. A clearance is a floor; a camera legitimately above
                    // the hill it is crossing must not be dragged down onto it.
                    pose.position.y = std::max(pose.position.y, floorY);
                }
            }
        }
    }
    if (!rig.aimNode.empty()) {
        if (const CompositionNode* node = findNode(rig.aimNode); node != nullptr) {
            pose.target = nodeWorldTransform(*node).position + rig.aimOffset;
        }
    }
    ensureDistinctAim(pose);
    return pose;
}

Result<void> Composition::setCameraDirection(CameraDirection direction) {
    direction.ensureMainCamera();
    if (auto ok = direction.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    cameraDirection_ = std::move(direction);
    // The channels of a camera that has just appeared do not exist yet. Re-registering is the
    // caller's job (Engine::refreshCameraParameters), because the timeline has to re-bind with it
    // and this object cannot do that alone; until then the authored bases are used, which is the
    // same rule an unattached composition follows.
    if (params_ != nullptr) {
        registerCameraChannels(*params_, 10.0f * std::max(radius_, 1.0f));
    }
    return {};
}

bool Composition::cameraIsAnimated(CameraId id, const params::Timeline& timeline) const {
    const CameraRig* rig = cameraDirection_.find(id);
    if (rig == nullptr) {
        return false;
    }
    const std::string p = prefix_ + rig->channelPrefix();
    // "Animated" is not a mode and not a stored flag: it is the question "does the timeline drive
    // any of this camera's channels", asked of the timeline. That is the whole of the difference
    // between a static camera and an animated one, and it means there is no state to keep in step.
    static constexpr const char* kChannels[] = {"position", "target", "fov", "focalLength", "splineT"};
    for (const char* channel : kChannels) {
        if (timeline.isAutomated(p + channel)) {
            return true;
        }
    }
    return false;
}

void Composition::observeCameraEvents(double seconds) {
    // A staging scenario is live state, not a span on the timeline (ADR-210): it is begun by
    // `autoStart` or a signal edge, and nothing anywhere records when it will end. So the director's
    // view of an event is what this composition has *seen*: an entry opens the frame a scenario's
    // role binding appears and closes the frame it lets go. An entry still open has
    // `endSeconds <= startSeconds`, which `resolveActiveCamera` reads as "still running".
    //
    // This is the only part of camera direction that is not a pure function of the playhead, and it
    // is the same compromise ADR-217's hold already makes, for the same reason. A seek clears it.
    for (const CameraRig& rig : cameraDirection_.cameras) {
        if (rig.eventScenario.empty()) {
            continue;
        }
        const std::string_view beat = staging_.beat(rig.eventScenario);
        bool engaged = false;
        if (rig.eventBeats.empty()) {
            // Running *and* holding a subject. `running()` alone goes true on the frame the scenario
            // starts looking for one, which can be seconds before there is anything to show.
            engaged = !staging_.binding(rig.eventScenario, "target").empty();
        } else {
            engaged = std::ranges::any_of(rig.eventBeats,
                                          [&](const std::string& name) { return beat == name; });
        }
        CameraEventSpan* open = nullptr;
        for (CameraEventSpan& span : cameraEvents_) {
            if (span.name == rig.eventScenario && span.endSeconds <= span.startSeconds) {
                open = &span;
                break;
            }
        }
        if (engaged && open == nullptr) {
            cameraEvents_.push_back(CameraEventSpan{rig.eventScenario, seconds, seconds});
        } else if (!engaged && open != nullptr) {
            open->endSeconds = seconds;
        }
    }
    // A span nobody can still be inside is dropped, so a ten-minute piece with sixty abductions does
    // not grow a table the resolver walks every frame.
    std::erase_if(cameraEvents_, [seconds](const CameraEventSpan& span) {
        return span.endSeconds > span.startSeconds && seconds > span.endSeconds + 30.0;
    });
}

void Composition::applyParameters() {
    // ADR-343. Resolved first because the node loop below needs `glowScale` and `starBrightness`,
    // and written last (at the end of this function) because the sky and the lights it owns are
    // set by the blocks in between. One phase, one state, one frame.
    // ADR-350: the parameters are the authority and are pushed into the settings here, the same
    // direction `sky.zenithColor = c(skyZenith_, ...)` already runs. One source of truth: the scene
    // file supplies the defaults, the parameter carries them from then on.
    {
        DayNightParams& p = dayNightParams_;
        const auto f = [](const params::Parameter<float>* q, float fallback) {
            return q != nullptr ? q->value() : fallback;
        };
        const auto b = [](const params::Parameter<bool>* q, bool fallback) {
            return q != nullptr ? q->value() : fallback;
        };
        scene::DayNightSettings& dn = dayNight_;
        dn.enabled = b(p.enabled, dn.enabled);
        dn.paused = b(p.paused, dn.paused);
        dn.manualPhase = f(p.dayPhase, dn.manualPhase);
        dn.cycleSeconds = f(p.cycleSeconds, dn.cycleSeconds);
        dn.phaseOffset = f(p.phaseOffset, dn.phaseOffset);
        constexpr float kDegToRad = 0.01745329252f;
        if (p.sunPeakElevation != nullptr) dn.sunPeakElevation = p.sunPeakElevation->value() * kDegToRad;
        if (p.sunAzimuthAtDawn != nullptr) dn.sunAzimuthAtDawn = p.sunAzimuthAtDawn->value() * kDegToRad;
        if (p.sunAzimuthSweep != nullptr) dn.sunAzimuthSweep = p.sunAzimuthSweep->value() * kDegToRad;
        dn.sunIntensityScale = f(p.sunIntensityScale, dn.sunIntensityScale);
        dn.moonIntensityScale = f(p.moonIntensityScale, dn.moonIntensityScale);
        dn.starBrightnessScale = f(p.starBrightnessScale, dn.starBrightnessScale);
        dn.hdriIntensityScale = f(p.hdriIntensityScale, dn.hdriIntensityScale);
        dn.glowInfluence = f(p.glowInfluence, dn.glowInfluence);
        dn.fogHorizonBlend = f(p.fogHorizonBlend, dn.fogHorizonBlend);
    }
    if (dayNight_.enabled) {
        dayNightState_ = resolveDayNight(dayNight_, phaseAt(dayNight_, currentTime_));
    }

    // Root: uniform scale about the bounds centre and rotation about +Y, as a Transform so it
    // composes with the node and rest transforms.
    const float rootScale = (rootScale_ != nullptr ? rootScale_->value() : 1.0f) +
                            (rootImpulse_ != nullptr ? rootImpulse_->value() : 0.0f);
    Transform root;
    root.rotation = glm::angleAxis(rootAngle_, glm::vec3(0.0f, 1.0f, 0.0f));
    root.scale = glm::vec3(rootScale);
    root.position = center_ - root.rotation * (center_ * rootScale);

    for (std::size_t i = 0; i < nodes_.size() && i < ranges_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        NodeRange& range = ranges_[i];

        // Keep the authored values in step with the parameter bases so saving and re-attaching
        // see what the user set.
        if (node.positionParam != nullptr) {
            node.transform.position = node.positionParam->base();
        }
        if (node.rotationParam != nullptr) {
            node.transform.rotation = quatFromEulerDegrees(node.rotationParam->base());
        }
        if (node.scaleParam != nullptr) {
            node.transform.scale = node.scaleParam->base();
        }
        if (node.visibleParam != nullptr) {
            node.visible = node.visibleParam->base();
        }
        if (node.emissiveParam != nullptr) {
            node.emissiveBoost = node.emissiveParam->base();
        }
        if (node.roughnessParam != nullptr) {
            node.roughnessScale = node.roughnessParam->base();
        }
        if (node.opacityParam != nullptr) {
            node.opacityScale = node.opacityParam->base();
        }

        const Transform nodeT = nodeWorldTransform(node);
        bool visible = nodeVisible(node);
        bool dayNightVisibleOverride = true;
        float emissiveBoost =
            node.emissiveParam != nullptr ? node.emissiveParam->value() : node.emissiveBoost;
        // ADR-343: the cycle scales the nodes the scene named as stars and as Glowmere layers.
        // Multiplying the boost rather than replacing it is what keeps the authored per-layer
        // intensities independently controllable, which the brief asks for explicitly.
        if (dayNight_.enabled) {
            const auto named = [&node](const std::vector<std::string>& names) {
                return std::find(names.begin(), names.end(), node.name) != names.end();
            };
            if (named(dayNight_.starNodes)) {
                emissiveBoost *= dayNightState_.starBrightness;
                // A star that has been faded to zero emission is not gone -- it is still geometry,
                // and its base colour is black, so at noon the sky filled with small DARK squares
                // instead of with nothing. Scaling emission is how a star dims; hiding it is how a
                // star sets. Seen in a frame; no number in the cycle table showed it.
                if (dayNightState_.starBrightness <= 1e-3f) {
                    dayNightVisibleOverride = false;
                }
            } else if (named(dayNight_.glowNodes)) {
                emissiveBoost *= dayNightState_.glowScale;
            }
            visible = visible && dayNightVisibleOverride;
        }
        const float roughnessScale =
            node.roughnessParam != nullptr ? node.roughnessParam->value() : node.roughnessScale;
        const float opacityScale =
            node.opacityParam != nullptr ? node.opacityParam->value() : node.opacityScale;
        // Only touch a material's opacity on a node that has been faded at all, ever. Writing
        // `rest * 1.0` every frame for every node would be correct and would also mean every node
        // in the scene had its alpha mode reassigned sixty times a second for nothing.
        const bool fading = opacityScale < 1.0f || !range.restOpacity.empty();
        const Transform full = compose(root, nodeT);
        range.world = full;
        range.worldValid = true;

        // The lights the node's asset brought in. Applied here, per frame, rather than at rebuild:
        // `rebuild` repopulates `scene_.lights` wholesale, so a value written onto a light was
        // discarded at the next rebuild -- which made lights the one thing in a scene that could not
        // be keyed, modulated or edited at all. Multiplied onto the asset's own numbers so the scale
        // does not compound frame on frame, and so a scene need not restate a lamp's candela to dim
        // it. The node also carries the light *with* it: a moved lamp lights where it now is.
        if (range.lightCount > 0 && range.firstLight + range.lightCount <= scene_.lights.size()) {
            const float lightScale =
                node.lightIntensityParam != nullptr ? node.lightIntensityParam->value() : 1.0f;
            const glm::vec3 lightTint =
                node.lightColorParam != nullptr ? node.lightColorParam->value() : glm::vec3(1.0f);
            for (std::size_t k = 0; k < range.lightCount; ++k) {
                PunctualLight& light = scene_.lights[range.firstLight + k];
                if (k < range.restLightIntensity.size()) {
                    // A hidden node's lights go out with it, which is what hiding a lamp means.
                    light.intensity = visible ? range.restLightIntensity[k] * lightScale : 0.0f;
                }
                if (k < range.restLightColor.size()) {
                    light.color = range.restLightColor[k] * lightTint;
                }
            }
        }

        const Scene* child = (node.kind == NodeKind::Scene && node.child) ? &node.child->scene() : nullptr;
        if (child != nullptr && child->entities.size() == range.entityCount) {
            // Nested parameters moved the child's entities: take its current state as the rest.
            for (std::size_t k = 0; k < range.entityCount; ++k) {
                range.restTransforms[k] = child->entities[k].transform;
                range.restEmissive[k] = child->entities[k].material.emissiveIntensity;
                range.restRoughness[k] = child->entities[k].material.roughness;
            }
        }
        for (std::size_t k = 0; k < range.entityCount && range.firstEntity + k < scene_.entities.size();
             ++k) {
            Entity& e = scene_.entities[range.firstEntity + k];
            e.transform = compose(full, range.restTransforms[k]);
            e.visible = visible && (child == nullptr || child->entities[k].visible);
            if (e.style == MeshStyle::Lit) {
                e.material.emissiveIntensity = range.restEmissive[k] * emissiveBoost;
                e.material.roughness = std::clamp(range.restRoughness[k] * roughnessScale, 0.0f, 1.0f);
            }
            if (fading) {
                // Capture before the first write, once. `restOpacity` is empty on a freshly built
                // range, which is exactly when the asset's own numbers are still in place.
                if (range.restOpacity.size() != range.entityCount) {
                    range.restOpacity.assign(range.entityCount, 1.0f);
                    range.restAlphaMode.assign(range.entityCount, 0u);
                    for (std::size_t q = 0;
                         q < range.entityCount && range.firstEntity + q < scene_.entities.size();
                         ++q) {
                        const Entity& src = scene_.entities[range.firstEntity + q];
                        range.restOpacity[q] = src.material.opacity;
                        range.restAlphaMode[q] = static_cast<std::uint8_t>(src.material.alphaMode);
                    }
                }
                e.material.opacity = std::clamp(range.restOpacity[k] * opacityScale, 0.0f, 1.0f);
                // The half without which the number does nothing: `pbr_shade.wgsl` throws an
                // OPAQUE material's alpha away. Restored to the asset's own mode the moment the
                // fade is over, so a node is never left in a blend pipeline it did not ask for.
                e.material.alphaMode = opacityScale < 1.0f
                                           ? AlphaMode::Blend
                                           : static_cast<AlphaMode>(range.restAlphaMode[k]);
            }
        }

        if (node.kind == NodeKind::Procedural && range.proceduralIndex >= 0 &&
            static_cast<std::size_t>(range.proceduralIndex) < scene_.procedurals.size()) {
            // Parameters drive a live copy; the node transform folds into the distribution
            // transform so the instance records already sit in world space.
            ProceduralGeometry& pg = scene_.procedurals[static_cast<std::size_t>(range.proceduralIndex)];
            // Values only. Generating here as well as in rebuildProcedurals() was 2 regenerations
            // per object per frame, for ever, on an editor nobody was touching: this one runs
            // against an empty context and stores *that* hash, so the one below always disagreed
            // with it and always regenerated, and next frame this one disagreed right back. On
            // glowmere-stylized that measured as 22 regenerations per idle frame -- exactly twice
            // the eleven procedural nodes -- where the right answer is none.
            if (legacyProceduralGeneration_) {
                applyProceduralParameters(node.proceduralParams, node.proceduralRest, pg);
            } else {
                applyProceduralParameterValues(node.proceduralParams, node.proceduralRest, pg);
            }
            prefixFieldReferences(pg, sanitise(prefix_));
            pg.distributionTransform = Transform::fromMatrix(full.matrix() * pg.distributionTransform.matrix());
            pg.visible = pg.visible && visible;
            // The asset's other materials (ADR-044) follow part 0 through exactly the same
            // parameters, each against its own rest copy, so a live change to the count or the
            // distribution moves every part of the asset together.
            for (std::size_t k = 0; k < range.proceduralSubCount &&
                                    static_cast<std::size_t>(range.proceduralIndex) + 1 + k < scene_.procedurals.size() &&
                                    k < node.proceduralSubRest.size();
                 ++k) {
                const ProceduralGeometry& subRest = node.proceduralSubRest[k];
                ProceduralGeometry& sub =
                    scene_.procedurals[static_cast<std::size_t>(range.proceduralIndex) + 1 + k];
                const std::string name = sub.name;
                if (legacyProceduralGeneration_) {
                    applyProceduralParameters(node.proceduralParams, subRest, sub);
                } else {
                    applyProceduralParameterValues(node.proceduralParams, subRest, sub);
                }
                sub.name = name;
                // The material parameters were registered from part 0's material, so applying them
                // here would repaint the leaves in the bark's colour -- the exact bug this split
                // exists to fix. Where a parameter still sits at part 0's authored value this part
                // keeps its own; where something moved it (an author, a preset, a route) the move
                // applies to every part, because that is what the author asked for.
                const Material& base = node.proceduralRest.material;
                const Material& own = subRest.material;
                if (sub.material.baseColor == base.baseColor) sub.material.baseColor = own.baseColor;
                if (sub.material.opacity == base.opacity) sub.material.opacity = own.opacity;
                if (sub.material.emissiveColor == base.emissiveColor) sub.material.emissiveColor = own.emissiveColor;
                if (sub.material.emissiveIntensity == base.emissiveIntensity)
                    sub.material.emissiveIntensity = own.emissiveIntensity;
                if (sub.material.roughness == base.roughness) sub.material.roughness = own.roughness;
                if (sub.material.metallic == base.metallic) sub.material.metallic = own.metallic;
                if (k + 1 < node.materialPartParams.size()) {
                    node.materialPartParams[k + 1].apply(sub.material);
                }
                prefixFieldReferences(sub, sanitise(prefix_));
                sub.distributionTransform =
                    Transform::fromMatrix(full.matrix() * sub.distributionTransform.matrix());
                sub.visible = sub.visible && visible;
            }
            if (!node.materialPartParams.empty()) {
                node.materialPartParams[0].apply(pg.material);
            }
            // generated by rebuildProcedurals() once every object and spline has its finals
        }
        if (node.kind == NodeKind::Spline && range.splineIndex >= 0 &&
            static_cast<std::size_t>(range.splineIndex) < scene_.splines.splines.size()) {
            spatial::Spline& sp = scene_.splines.splines[static_cast<std::size_t>(range.splineIndex)];
            const std::string name = sp.name;
            applySplineParameters(node.splineParams, node.splineRest, sp);
            sp.name = name;
            foldSplineFrame(sp, full);
        }
        if (node.kind == NodeKind::Sdf && range.sdfIndex >= 0 &&
            static_cast<std::size_t>(range.sdfIndex) < scene_.sdfs.size()) {
            SdfObject& so = scene_.sdfs[static_cast<std::size_t>(range.sdfIndex)];
            const std::string name = so.name;
            SdfObject live = so;
            applySdfParameters(node.sdfParams, node.sdfRest, live);
            // keep the structural outputs (mesh cache) of the scene copy
            live.structureVersion = so.structureVersion;
            live.builtHash = so.builtHash;
            live.mesh = std::move(so.mesh);
            live.meshHash = so.meshHash;
            so = std::move(live);
            so.name = name;
            prefixFieldReferences(so, sanitise(prefix_));
            so.transform = compose(full, so.transform);
            so.visible = so.visible && visible;
        }
        if (node.kind == NodeKind::Field && range.fieldIndex >= 0 &&
            static_cast<std::size_t>(range.fieldIndex) < scene_.fields.fields.size()) {
            spatial::FieldSpec& f = scene_.fields.fields[static_cast<std::size_t>(range.fieldIndex)];
            const std::string name = f.name;
            applyFieldParameters(node.fieldParams, node.fieldRest, f);
            f.name = name;
            prefixFieldReferences(f, sanitise(prefix_));
            foldFieldFrame(f, full);
            f.enabled = f.enabled && visible;
        }
        if (node.kind == NodeKind::Particles && range.particleIndex >= 0 &&
            static_cast<std::size_t>(range.particleIndex) < scene_.particles.size()) {
            ParticleSystem& ps = scene_.particles[static_cast<std::size_t>(range.particleIndex)];
            ps = node.particleRest;
            applyParticleParameters(node.particleParams, node.particleRest, ps);
            prefixFieldReferences(ps, sanitise(prefix_));
            const float scale = lengthScale(full);
            ps.position = transformPoint(full, ps.position);
            ps.attractorPosition = transformPoint(full, ps.attractorPosition);
            ps.extent *= scale;
            ps.sizeStart *= scale;
            ps.sizeEnd *= scale;
            ps.enabled = ps.enabled && visible;
        } else if (child != nullptr && child->particles.size() == range.particleCount) {
            const float scale = lengthScale(full);
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.particleCount; ++k) {
                const ParticleSystem& src = child->particles[k];
                ParticleSystem& ps = scene_.particles[range.firstParticle + k];
                ps = src;
                prefixFieldReferences(ps, childPrefix);
                ps.position = transformPoint(full, src.position);
                ps.attractorPosition = transformPoint(full, src.attractorPosition);
                ps.extent = src.extent * scale;
                ps.sizeStart = src.sizeStart * scale;
                ps.sizeEnd = src.sizeEnd * scale;
                ps.enabled = src.enabled && visible;
            }
        }
        if (child != nullptr && child->procedurals.size() == range.proceduralCount) {
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.proceduralCount; ++k) {
                const ProceduralGeometry& src = child->procedurals[k];
                ProceduralGeometry& pg = scene_.procedurals[range.firstProcedural + k];
                const std::string name = pg.name;
                // keep this level's structural outputs; the child's records are regenerated below
                ProceduralGeometry copy = src;
                copy.instances = std::move(pg.instances);
                copy.cloud = std::move(pg.cloud);
                copy.structureVersion = pg.structureVersion;
                copy.builtHash = pg.builtHash;
                copy.meshHash = pg.meshHash;
                copy.boundsMin = pg.boundsMin;
                copy.boundsMax = pg.boundsMax;
                pg = std::move(copy);
                pg.name = name;
                prefixFieldReferences(pg, childPrefix);
                pg.distributionTransform = Transform::fromMatrix(full.matrix() * src.distributionTransform.matrix());
                pg.visible = pg.visible && visible;
            }
        }
        if (child != nullptr && child->splines.splines.size() == range.splineCount) {
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.splineCount; ++k) {
                spatial::Spline& sp = scene_.splines.splines[range.firstSpline + k];
                const std::string name = sp.name;
                sp = child->splines.splines[k];
                sp.name = name;
                foldSplineFrame(sp, full);
            }
        }
        if (child != nullptr && child->sdfs.size() == range.sdfCount) {
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.sdfCount; ++k) {
                const SdfObject& src = child->sdfs[k];
                SdfObject& so = scene_.sdfs[range.firstSdf + k];
                const std::string name = so.name;
                SdfObject copy = src;
                copy.structureVersion = so.structureVersion;
                copy.builtHash = so.builtHash;
                copy.mesh = std::move(so.mesh);
                copy.meshHash = so.meshHash;
                so = std::move(copy);
                so.name = name;
                prefixFieldReferences(so, childPrefix);
                so.transform = compose(full, src.transform);
                so.visible = src.visible && visible;
            }
        }
        if (child != nullptr && child->materialPrograms.size() == range.materialCount) {
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.materialCount; ++k) {
                MaterialProgram& mp = scene_.materialPrograms[range.firstMaterial + k];
                const std::string name = mp.name;
                mp = child->materialPrograms[k];
                mp.name = name;
                prefixFieldReferences(mp, childPrefix);
            }
        }
        if (child != nullptr && child->fields.fields.size() == range.fieldCount) {
            const std::string childPrefix = nestedPrefix(node);
            for (std::size_t k = 0; k < range.fieldCount; ++k) {
                const spatial::FieldSpec& src = child->fields.fields[k];
                spatial::FieldSpec& f = scene_.fields.fields[range.firstField + k];
                const std::string name = f.name;
                f = src;
                f.name = name;
                prefixFieldReferences(f, childPrefix);
                foldFieldFrame(f, full);
                f.enabled = src.enabled && visible;
            }
        }
    }

    // ADR-358: the authored lights' own parameters, into the scene copies `rebuild` made.
    //
    // Before the node-riding block below, not after: that block re-places a light expressed in a
    // node's local frame, and it has to re-place the direction these parameters just set rather
    // than one a rebuild left behind. `base()` back into `authoredLights_` for the reason the node
    // loop above does it -- saving the scene has to see what the user set, not what the file said
    // (ADR-225) -- and `value()`, which carries any modulation on top, into the render copy.
    for (std::size_t i = 0; i < authoredLights_.size() && i < authoredLightParams_.size(); ++i) {
        const AuthoredLightParams& p = authoredLightParams_[i];
        PunctualLight& rest = authoredLights_[i].light;
        if (p.enabled != nullptr) rest.enabled = p.enabled->base();
        if (p.intensity != nullptr) rest.intensity = p.intensity->base();
        if (p.color != nullptr) rest.color = p.color->base();
        if (p.angularSize != nullptr) rest.softness = p.angularSize->base();
        if (p.shadowStrength != nullptr) rest.shadowStrength = p.shadowStrength->base();
        // Only when somebody actually moved the angles.
        //
        // This guard is not an optimisation. `lightAngles` and `lightDirectionFromAngles` are a
        // lossy pair -- a direction taken to azimuth/elevation and back lands about 6e-8 away --
        // so rewriting the direction unconditionally rewrote it *differently* on every load, and
        // `authoredLightsAgainst` then saw an edit in a scene nobody had touched. Every project
        // whose scene has a directional or a spot light would have started writing a `lights` key
        // on every save, which is precisely the byte-stability the record depends on not having.
        // Measured: (-0.35320863, -0.88302159, -0.30905756) parsed, against
        // (-0.35320857, -0.88302159, -0.30905750) after one angle round trip.
        //
        // The comparison is against the seed rather than the previous frame, because the seed is
        // exactly "what the file said" -- so an untouched light keeps the file's own floats and a
        // moved one is recomputed from the angles the user set.
        const bool aimMoved = p.azimuth != nullptr && p.elevation != nullptr &&
                              (p.azimuth->base() != p.azimuth->defaultComponent(0) ||
                               p.elevation->base() != p.elevation->defaultComponent(0));
        if (aimMoved) {
            rest.direction = lightDirectionFromAngles(p.azimuth->base(), p.elevation->base());
        }
        if (p.position != nullptr) rest.position = p.position->base();
        if (p.temperature != nullptr) rest.temperature = p.temperature->base();
        if (p.tint != nullptr) rest.tint = p.tint->base();
        if (p.castsShadow != nullptr) rest.castsShadow = p.castsShadow->base();
        if (p.contactShadow != nullptr) rest.contactShadow = p.contactShadow->base();
        if (p.shadowBias != nullptr) rest.shadowBias = p.shadowBias->base();
        if (p.volumetric != nullptr) rest.volumetricStrength = p.volumetric->base();
        if (p.range != nullptr) rest.range = p.range->base();
        if (p.innerCone != nullptr) rest.innerConeAngle = glm::radians(p.innerCone->base());
        if (p.outerCone != nullptr) rest.outerConeAngle = glm::radians(p.outerCone->base());
        if (p.width != nullptr) rest.width = p.width->base();
        if (p.height != nullptr) rest.height = p.height->base();
        if (p.radius != nullptr) rest.radius = p.radius->base();
        const std::size_t lightIndex = authoredLightFirst_ + i;
        if (lightIndex >= scene_.lights.size()) {
            continue;
        }
        PunctualLight& live = scene_.lights[lightIndex];
        live.enabled = rest.enabled;
        live.intensity = p.intensity != nullptr ? p.intensity->value() : rest.intensity;
        live.color = p.color != nullptr ? p.color->value() : rest.color;
        live.softness = p.angularSize != nullptr ? p.angularSize->value() : rest.softness;
        live.shadowStrength = p.shadowStrength != nullptr ? p.shadowStrength->value() : rest.shadowStrength;
        // The same rule for the render copy, against the *final* value so that a route or a
        // keyframe driving the aim still turns the light -- but an unmodulated, unmoved light
        // renders with the direction its file authored rather than a round trip of it.
        if (p.azimuth != nullptr && p.elevation != nullptr &&
            (p.azimuth->value() != p.azimuth->defaultComponent(0) ||
             p.elevation->value() != p.elevation->defaultComponent(0))) {
            live.direction = lightDirectionFromAngles(p.azimuth->value(), p.elevation->value());
        } else {
            live.direction = rest.direction;
        }
        // `value()` rather than `base()`, so each of these carries whatever modulation or timeline
        // automation sits on top of it. That is the difference between a light a route can drive
        // and a light a route merely names.
        live.position = p.position != nullptr ? p.position->value() : rest.position;
        live.temperature = p.temperature != nullptr ? p.temperature->value() : rest.temperature;
        live.tint = p.tint != nullptr ? p.tint->value() : rest.tint;
        live.castsShadow = rest.castsShadow;
        live.contactShadow = rest.contactShadow;
        live.shadowBias = p.shadowBias != nullptr ? p.shadowBias->value() : rest.shadowBias;
        live.volumetricStrength = p.volumetric != nullptr ? p.volumetric->value() : rest.volumetricStrength;
        if (p.range != nullptr) live.range = p.range->value();
        if (p.innerCone != nullptr) live.innerConeAngle = glm::radians(p.innerCone->value());
        if (p.outerCone != nullptr) live.outerConeAngle = glm::radians(p.outerCone->value());
        if (p.width != nullptr) live.width = p.width->value();
        if (p.height != nullptr) live.height = p.height->value();
        if (p.radius != nullptr) live.radius = p.radius->value();
    }

    // ADR-278: an authored light that rides a node. Here rather than in `rebuild` for the reason
    // the node's *own* asset lights are scaled here -- `rebuild` runs when the scene changes and a
    // node moves every frame -- and before the ecology erase and the rig's resize-from-the-back, so
    // these indices are the ones `rebuild` handed out and not a frame's worth of appended lights
    // later.
    //
    // Position **and** direction, which is the half the glTF path does not do: the loop above
    // refreshes an asset light's intensity and colour and its comment claims "a moved lamp lights
    // where it now is", but nothing re-places it, so an imported lamp's beam stays at the rebuild's
    // transform for ever. No asset in this repository carries a KHR_lights_punctual light, so that
    // has never shown up in a frame; it is recorded in ADR-278 rather than fixed here, because
    // fixing it means storing a rest transform per asset light and there is nothing to test it on.
    for (std::size_t i = 0; i < authoredLights_.size() && i < authoredLightNodeIndex_.size(); ++i) {
        const std::size_t nodeIndex = authoredLightNodeIndex_[i];
        if (nodeIndex == kNoNode || nodeIndex >= ranges_.size() || !ranges_[nodeIndex].worldValid) {
            continue;
        }
        const std::size_t lightIndex = authoredLightFirst_ + i;
        if (lightIndex >= scene_.lights.size()) {
            continue;
        }
        const AuthoredLight& a = authoredLights_[i];
        PunctualLight& light = scene_.lights[lightIndex];
        const Transform& world = ranges_[nodeIndex].world;
        light.position = transformPoint(world, a.light.position);
        light.direction = transformDirection(world, a.light.direction);
        light.up = transformDirection(world, a.light.up);
        // A hidden node's light goes out with it, which is what hiding a lamp means -- the same
        // rule, in the same words, as the asset lights ten lines up.
        light.intensity = nodeVisible(*nodes_[nodeIndex]) ? a.light.intensity : 0.0f;
    }

    // Own material programs: finals into the scene copies.
    for (std::size_t i = 0; i < materialParams_.size() && i < ownMaterialCount_ && i < scene_.materialPrograms.size(); ++i) {
        MaterialProgram& mp = scene_.materialPrograms[i];
        const std::string name = mp.name;
        applyMaterialProgramParameters(materialParams_[i], materialPrograms_[i], mp);
        mp.name = name;
        prefixFieldReferences(mp, sanitise(prefix_));
    }
    rebuildProcedurals();
    rebuildSdfs();

    // ---- which camera, then where it is (ADR-245) ----------------------------------------------
    //
    // Two steps that used to be one. `resolveActiveCamera` is a pure function of the clock and says
    // which camera owns the frame and why; the evaluation below moves only that camera (and, during
    // a blend, the one it is coming from). Every other camera in the collection costs its
    // parameters and nothing else -- no spline sample, no arithmetic, no draw.
    const CameraId cameraWas = activeCamera_.camera;
    activeCamera_ = resolveActiveCamera(cameraDirection_, cameraEvents_, currentTime_);
    // ADR-391. The viewport's own viewpoint used to be expressed here, as a redirection of the
    // resolver's *answer* to the main camera ("free roam"). It is not expressed here any more, and
    // the reason is worth keeping: redirecting the answer made `activeCamera_` stop reporting the
    // film -- it said "Viewport", so the Cameras panel could no longer tell you which camera owned
    // the cut while you were flying -- and pointing it at the main camera meant navigating still
    // wrote `camera/*`, which is the whole defect. The frame the viewport shows is now decided
    // *after* the film's camera is final, in `applyViewportView`, and `activeCamera_` is left
    // telling the truth about the film in every mode.
    // What the director just did and why, once per change (multicam-demo section 21). Cheap enough
    // to leave on: a camera that changes sixty times a second is a bug worth hearing about.
    if (activeCamera_.camera != cameraWas && cameraDirection_.directing()) {
        log::info("camera: {} ({}{}{}) at {:.2f} s", activeCamera_.name,
                  activeCameraReasonName(activeCamera_.reason),
                  activeCamera_.eventName.empty() ? "" : " ", activeCamera_.eventName, currentTime_);
    }
    const float fov0 = cameraFov_ != nullptr ? cameraFov_->value() : cameraFovSetting_;
    float fov = fov0;
    if (!cameraDirection_.directing() && activeCamera_.camera == kMainCamera) {
        // The untouched path: one camera, evaluated exactly as this file has always evaluated it.
        const CameraPose pose = evaluateMainCamera();
        scene_.camera.position = pose.position;
        scene_.camera.target = pose.target;
    } else {
        const auto poseOf = [&](CameraId id) -> CameraPose {
            if (id == kMainCamera) {
                return evaluateMainCamera();
            }
            const CameraRig* rig = cameraDirection_.find(id);
            if (rig == nullptr) {
                return evaluateMainCamera();
            }
            const CameraChannels* ch = nullptr;
            for (const CameraChannels& c : cameraChannels_) {
                if (c.id == id) {
                    ch = &c;
                    break;
                }
            }
            return evaluateAuthoredCamera(*rig, ch);
        };
        CameraPose pose = poseOf(activeCamera_.camera);
        if (activeCamera_.blending()) {
            pose = blendPoses(poseOf(activeCamera_.previous), pose, activeCamera_.blend);
        }
        ensureDistinctAim(pose);
        scene_.camera.position = pose.position;
        scene_.camera.target = pose.target;
        fov = pose.fovDegrees;
        // An authored camera focuses on what it is aimed at. Without this it inherits whatever
        // `camera/lens/focusDistance` the Auto-director baked for a shot on a different camera
        // entirely, and the subject of the shot somebody composed comes back out of focus.
        if (activeCamera_.camera != kMainCamera) {
            activeCamera_.focusDistance = glm::length(pose.target - pose.position);
        }
        // The active camera's optical identity, published for the engine's physical lens block to
        // pick up (ADR-037 owns the lens; this only says which focal length the picture is on).
        activeCamera_.focalLength = pose.focalLength;
    }
    // Shake last, and in every mode: it is an offset applied to whatever placed the camera, which
    // is what makes it compose with an orbit, a spline ride and a baked cinematic move alike
    // instead of being a fourth way to position a camera (ADR-098, brief section 14).
    if (cameraShakeAmplitude_ != nullptr) {
        CameraShake shake;
        shake.amplitude = cameraShakeAmplitude_->value();
        shake.frequency = cameraShakeFrequency_ != nullptr ? cameraShakeFrequency_->value() : 9.0f;
        shake.decaySeconds = cameraShakeDecay_ != nullptr ? cameraShakeDecay_->value() : 0.0f;
        shake.rotationDegrees = cameraShakeRotation_ != nullptr ? cameraShakeRotation_->value() : 0.0f;
        shake.startSeconds =
            cameraShakeStart_ != nullptr ? static_cast<double>(cameraShakeStart_->value()) : 0.0;
        applyCameraShake(shake, currentTime_, scene_.camera.position, scene_.camera.target);
    }
    scene_.camera.fovYRadians = glm::radians(fov);
    scene_.camera.nearPlane = std::clamp(radius_ * 0.005f, 0.01f, 0.5f);
    scene_.camera.farPlane = std::max(radius_ * 50.0f, 2000.0f); // free cameras look across whole worlds
    applyFraming();
    // ADR-391, and the position in this function is the whole of its correctness. The film's camera
    // is now final -- resolved, evaluated, shaken, framed -- and `activeCamera_` has published what
    // owns it. Only now may the viewport put a different frame on screen, and everything below this
    // line follows the frame that is actually being looked at: the terrain's LOD, the water, the
    // rig lights that stand relative to the view, the renderer's culling, and (through
    // `Scene::camera`) every ray the editor casts to pick, drag a gizmo or box-select. That is not
    // a nicety -- a picker that rays from a camera nobody is looking through selects whatever is
    // under a point in a frame that is not on screen, and it looks right until you click something.
    //
    // In `Film` mode this is a branch and a return, so every render is byte-for-byte what it was.
    applyViewportView(fov0);
    updateTerrainLod();
    updateWaterSurfaces();
    // Last frame's ecology lights come off before the rig block, which removes its own lights by
    // resizing from the back and would otherwise take these with them.
    if (ecologyLightCount_ > 0) {
        std::erase_if(scene_.lights,
                      [](const PunctualLight& l) { return l.name.starts_with(kEcologyLightPrefix); });
        ecologyLightCount_ = 0;
    }

    if (brightness_ != nullptr) {
        scene_.environment.brightness = brightness_->value();
    }
    scene_.environment.fogDensity = fogDensity_ != nullptr ? fogDensity_->value() : fogDensitySetting_;
    scene_.environment.stylized = stylized_ != nullptr ? stylized_->value() : stylizedSetting_;
    if (addedKeyLight_ && !scene_.lights.empty() && keyLight_ != nullptr) {
        scene_.lights.back().intensity = defaultKeyLight().intensity * keyLight_->value();
    }
    // ---- light rig (ADR-033) ----
    // Expanded after the camera is final so `followCamera` lights sit relative to this frame's
    // view, and re-expanded every frame so the rig's parameters are live.
    if (lightRig_) {
        if (rigLightCount_ > 0 && rigLightCount_ <= scene_.lights.size()) {
            scene_.lights.resize(scene_.lights.size() - rigLightCount_);
        }
        LightRig live = *lightRig_;
        if (!lightRigParams_.all.empty()) {
            applyLightRigParameters(lightRigParams_, *lightRig_, live);
        }
        const float scale = keyLight_ != nullptr ? std::max(keyLight_->value(), 0.0f) : 1.0f;
        live.keyIntensity *= scale;
        live.ambientIntensity *= scale;
        // A rig lights a subject, not a bounding box. When the composition names what the frame
        // is about, size the rig to that focal point; otherwise fall back to the whole scene.
        // Without this a world with distant background geometry pushes its key light hundreds of
        // units away, and an area light there contributes almost nothing.
        glm::vec3 subjectCenter = center_;
        float subjectRadius = radius_;
        const FocalPoint* focus = compositionData_.cameraTarget.empty()
                                      ? (compositionData_.focalPoints.empty()
                                             ? nullptr
                                             : &compositionData_.focalPoints.front())
                                      : compositionData_.find(compositionData_.cameraTarget);
        if (focus != nullptr) {
            subjectCenter = focus->position;
            subjectRadius = std::max(focus->radius, 1e-3f);
        }
        std::vector<PunctualLight> expanded =
            live.expand(subjectCenter, subjectRadius, scene_.camera.position, glm::vec3(0.0f, 1.0f, 0.0f));
        rigLightCount_ = expanded.size();
        for (PunctualLight& light : expanded) {
            light.name = sanitise(prefix_) + light.name;
            scene_.lights.push_back(std::move(light));
        }
    }
    updateEcologyLights();
    scene_.environment.fogColor = fogColor_ != nullptr ? fogColor_->value()
                                                       : (fogColorSet_ ? fogColorSetting_ : scene_.environment.backgroundColor);
    {
        scene::Environment& env = scene_.environment;
        auto pick = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->value() : fallback;
        };
        env.volumeDensity = pick(volumeDensity_, volumeSetting_.volumeDensity);
        env.fogHeight = pick(fogHeight_, volumeSetting_.fogHeight);
        env.fogHeightFalloff = pick(fogHeightFalloff_, volumeSetting_.fogHeightFalloff);
        env.fogUpperDensity = pick(fogUpperDensity_, volumeSetting_.fogUpperDensity);
        env.fogHeightCurve = pick(fogHeightCurve_, volumeSetting_.fogHeightCurve);
        env.volumeScattering = pick(volumeScattering_, volumeSetting_.volumeScattering);
        env.volumeAbsorption = pick(volumeAbsorption_, volumeSetting_.volumeAbsorption);
        env.volumeAnisotropy = pick(volumeAnisotropy_, volumeSetting_.volumeAnisotropy);
        env.volumeNoiseAmount = pick(volumeNoise_, volumeSetting_.volumeNoiseAmount);
        env.volumeNoiseScale = pick(volumeNoiseScale_, volumeSetting_.volumeNoiseScale);
        env.volumeNoiseSpeed = pick(volumeNoiseSpeed_, volumeSetting_.volumeNoiseSpeed);
        env.volumeEmission = pick(volumeEmission_, volumeSetting_.volumeEmission);
        env.volumeSteps = volumeSteps_ != nullptr ? volumeSteps_->value() : volumeSetting_.volumeSteps;
        env.volumeShadowSteps = volumeShadowSteps_ != nullptr ? volumeShadowSteps_->value()
                                                             : volumeSetting_.volumeShadowSteps;
        env.volumeShadowStrength = pick(volumeShadowStrength_, volumeSetting_.volumeShadowStrength);
        env.volumeJitter = pick(volumeJitter_, volumeSetting_.volumeJitter);
        env.shadowCascades = volumeSetting_.shadowCascades;
        env.shadowRange = pick(shadowRange_, volumeSetting_.shadowRange);
        env.volumeMaxDistance = pick(volumeMaxDistance_, volumeSetting_.volumeMaxDistance);
        // ADR-573 (§27): picked, not copied. It used not to be assigned here at all.
        env.volumeLocalLights = pick(volumeLocalLights_, volumeSetting_.volumeLocalLights);
        // Field names are prefixed like every other reference so a nested scene stays self-contained.
        env.volumeDensityField =
            volumeDensityFieldSetting_.empty() ? std::string() : sanitise(prefix_) + volumeDensityFieldSetting_;
        env.volumeColorField =
            volumeColorFieldSetting_.empty() ? std::string() : sanitise(prefix_) + volumeColorFieldSetting_;
        // ADR-055. A live `scene/windSpeed` parameter so the whole field can be turned up, down or
        // off without editing the file -- which is also how the A/B measurement is taken.
        // ADR-574. The comment that used to stand here said the distance fog's share of the mist
        // layer and the styled hemisphere "are copied, not picked" because "nothing about them is
        // animated". **It was false about the four lines it covered**: `styledSkyAmbient` and
        // `styledGroundAmbient` are registered parameters and ARE picked, two lines down. Someone
        // gave them parameters and left the reason saying they had not.
        //
        // And the surviving half of the reason was circular. Nothing animated `fogHeightAmount`
        // because nothing COULD: with no parameter there is no keyframe, no route and no row. A
        // reason that is true only because of the thing it justifies is not a reason (ADR-385).
        //
        // So it is picked. Still unreachable and worth saying where a reader will meet it:
        // `styledAmbientFloor` below has no parameter either, and `styledSkyAmbient` and
        // `styledGroundAmbient` have parameters that no panel draws -- ADR-058 shipped four
        // controls and not one of them is on a panel. Those three are lighting rather than fog;
        // ADR-574 records the measurement and leaves them to their owner.
        env.fogHeightAmount = pick(fogHeightAmount_, volumeSetting_.fogHeightAmount);
        env.styledSkyAmbient =
            styledSkyAmbient_ != nullptr ? styledSkyAmbient_->value() : volumeSetting_.styledSkyAmbient;
        env.styledGroundAmbient = styledGroundAmbient_ != nullptr ? styledGroundAmbient_->value()
                                                                  : volumeSetting_.styledGroundAmbient;
        env.styledAmbientFloor = volumeSetting_.styledAmbientFloor;
        // ADR-360: every field of the wind is a live parameter now, not just two, so the whole
        // field can be turned up, down, off, gustier or calmer from the UI, keyed on the timeline
        // and driven by audio (`music.build -> scene/windSpeed` is the intended idiom).
        env.wind = windSetting_;
        env.wind.enabled = windEnabled_ != nullptr ? windEnabled_->value() : windSetting_.enabled;
        env.wind.speed = pick(windSpeed_, windSetting_.speed);
        env.wind.direction = pick(windDirection_, windSetting_.direction);
        env.wind.gustAmount = pick(windGustAmount_, windSetting_.gustAmount);
        env.wind.gustScale = pick(windGustScale_, windSetting_.gustScale);
        env.wind.gustSpeed = pick(windGustSpeed_, windSetting_.gustSpeed);
        env.wind.gustSharpness = pick(windGustSharpness_, windSetting_.gustSharpness);
        env.wind.turbulence = pick(windTurbulence_, windSetting_.turbulence);
        env.wind.turbulenceScale = pick(windTurbulenceScale_, windSetting_.turbulenceScale);
        env.wind.turbulenceSpeed = pick(windTurbulenceSpeed_, windSetting_.turbulenceSpeed);
        env.wind.regionScale = pick(windRegionScale_, windSetting_.regionScale);
        env.wind.regionAmount = pick(windRegionAmount_, windSetting_.regionAmount);
        env.wind.regionDrift = pick(windRegionDrift_, windSetting_.regionDrift);
        env.wind.flutterScale = pick(windFlutterScale_, windSetting_.flutterScale);
    }
    // ADR-360: and the per-body amounts, so `nodes/<tree>/wind/strength` is a live slider.
    refreshWindBodyAmounts();
    {
    }
    if (gridIntensity_ != nullptr) {
        scene_.environment.gridIntensity = gridIntensity_->value();
    }
    scene_.environment.environmentIntensity =
        envIntensity_ != nullptr ? envIntensity_->value() : envIntensitySetting_;
    scene_.environment.environmentRotation =
        envRotation_ != nullptr ? envRotation_->value() : envRotationSetting_;
    // ADR-049: the visible sky's own two controls, independent of the shading intensity above.
    scene_.environment.showSkybox = showSkybox_ != nullptr ? showSkybox_->value() : showSkyboxSetting_;
    scene_.environment.proceduralSkyBackground = proceduralSkyBackground_ != nullptr
                                                     ? proceduralSkyBackground_->value()
                                                     : proceduralSkyBackgroundSetting_;
    scene_.environment.skyIntensity = skyIntensitySetting_;
    scene_.environment.skyBloom = skyBloom_ != nullptr ? skyBloom_->value() : skyBloomSetting_;
    scene_.environment.lightFromEnvironment =
        lightFromEnvironment_ != nullptr ? lightFromEnvironment_->value() : lightFromEnvironmentSetting_;
    if (lightFromEnvironmentSetting_ && envDominantDirection_.has_value()) {
        // Aim the key light away from the map's brightest pixel, after the rig has expanded, so
        // the moon in frame and the moonlight on the terrain are the same moon. The rig still owns
        // the light's colour, intensity and shadow settings -- only its direction is taken over,
        // and only when the scene asks. Rotating the sky rotates the light with it.
        const float a = scene_.environment.environmentRotation;
        const glm::vec3& m = *envDominantDirection_;
        const glm::vec3 towards(std::cos(a) * m.x - std::sin(a) * m.z, m.y,
                                std::sin(a) * m.x + std::cos(a) * m.z);
        if (PunctualLight* key = const_cast<PunctualLight*>(skyKeyLight(scene_.lights)); key != nullptr) {
            const float keyDistance = glm::length(key->position - center_);
            key->direction = -towards;
            key->position = center_ + towards * keyDistance; // area lights shade from the position too
        }
    }
    {
        SkySettings& sky = scene_.environment.sky;
        sky = skySetting_;
        const auto f = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->value() : fallback;
        };
        const auto c = [](const params::Parameter<glm::vec3>* p, const glm::vec3& fallback) {
            return p != nullptr ? p->value() : fallback;
        };
        if (skyEnabled_ != nullptr) {
            sky.enabled = skyEnabled_->value();
        }
        if (skyBackground_ != nullptr) {
            sky.showBackground = skyBackground_->value();
        }
        sky.zenithColor = c(skyZenith_, sky.zenithColor);
        sky.horizonColor = c(skyHorizon_, sky.horizonColor);
        sky.groundColor = c(skyGround_, sky.groundColor);
        sky.sunColor = c(skySunColor_, sky.sunColor);
        sky.hazeWidth = f(skyHaze_, sky.hazeWidth);
        sky.sunIntensity = f(skySunIntensity_, sky.sunIntensity);
        sky.sunAngularRadius = f(skySunSize_, sky.sunAngularRadius);
        sky.sunGlowWidth = f(skySunGlow_, sky.sunGlowWidth);
        sky.intensity = f(skyIntensity_, sky.intensity);
    }
    applyDayNight();
}

// ADR-346: resolving the environment map, on its own, so that changing it does not mean rebuilding
// the world.
//
// This used to live inline at the top of `rebuild()`, and `setEnvironmentMap` asked for a rebuild
// to make it run. `dirty_` has no granularity, so swapping a map re-flattened everything: on the
// Tree of Life ocean world that is 273-293 ms for 557 entities and 1,069 meshes, to change one
// texture id. A day/night cycle swaps twice per revolution and paid it both times.
//
// The texture ids are cached by path because `addTexture` appends: called repeatedly outside a
// rebuild it would grow the scene's texture list without bound, and a cycle that runs for an hour
// would leak a texture every two minutes. The cache is cleared by `rebuild()`, which clears the
// scene and with it every id this could hand back.
void Composition::resolveEnvironmentMap() {
    environmentDirty_ = false;
    scene_.environment.environmentMap = kInvalidTexture;
    if (!environmentPath_.empty()) {
        const std::string key = environmentPath_.generic_string();
        const auto cached = std::find_if(environmentTextureCache_.begin(), environmentTextureCache_.end(),
                                         [&key](const auto& e) { return e.path == key; });
        if (cached != environmentTextureCache_.end()) {
            scene_.environment.environmentMap = cached->texture;
            envDominantDirection_ = cached->dominant;
            return;
        }
        auto image = registry_.loadImage(environmentPath_, false);
        if (!image) {
            log::warn("composition '{}': environment map: {}", name_, image.error().message);
        } else if (!(*image)->image.isHdr()) {
            log::warn("composition '{}': environment map '{}' is not an HDR image", name_,
                      environmentPath_.string());
        } else {
            scene_.environment.environmentMap = scene_.addTexture((*image)->image);
            // ADR-049: find the sun/moon once, here, so `lightFromEnvironment` costs nothing per
            // frame. Reported because an author aiming a sky wants to see the number move.
            const glm::vec3 dominant = environmentDominantDirection((*image)->image);
            envDominantDirection_ = dominant;
            environmentTextureCache_.push_back({key, scene_.environment.environmentMap, dominant});
            log::info("composition '{}': environment '{}' ({}x{}); brightest direction "
                      "({:.3f}, {:.3f}, {:.3f}), elevation {:.1f} deg",
                      name_, environmentPath_.string(), (*image)->image.width, (*image)->image.height,
                      dominant.x, dominant.y, dominant.z,
                      std::asin(std::clamp(dominant.y, -1.0f, 1.0f)) * 57.2957795f);
        }
    }
    if (scene_.environment.environmentMap == kInvalidTexture) {
        envDominantDirection_.reset();
    }
}

// ADR-343: the cycle has the last word on the fields it owns. It runs after the sky block and the
// environment block above deliberately -- an author's static sky colour is the value the cycle
// starts from when it is off, and is overwritten when it is on, rather than the two fighting.
void Composition::applyDayNight() {
    if (!dayNight_.enabled) {
        return;
    }
    const DayNightState& s = dayNightState_;
    Environment& env = scene_.environment;
    env.environmentIntensity *= s.hdriIntensity;
    env.fogDensity = s.fogDensity;
    env.fogColor = s.fogColor;
    // ADR-049 splits these two and the distinction bites here: `Environment::skyIntensity` scales
    // the *visible background pass*, `SkySettings::intensity` scales the sky as an IBL source.
    // Driving only the latter left the night sky rendering at full daylight brightness -- 102/110/
    // 125 sRGB at midnight -- which is a bright sky at midnight and the end of the Glowmere
    // contrast the night depends on.
    env.skyIntensity = s.skyIntensity;
    SkySettings& sky = env.sky;
    sky.zenithColor = s.zenithColor;
    sky.horizonColor = s.horizonColor;
    sky.groundColor = s.groundColor;
    sky.hazeWidth = s.haze;
    sky.intensity = s.skyIntensity;
    // Every water surface in the scene follows the cycle too, so the sea is part of the world
    // changing state rather than a sheet the sky happens to fall on (brief §28).
    for (WaterSurface& water : scene_.waters) {
        water.settings.reflection = s.waterReflection;
        water.settings.deepColor = s.waterDeepColor;
    }
    // The sun and the moon are two directionals the scene named. The sun travels; the moon does
    // not, because its disc is a feature of the night environment map and the map does not move
    // unless `environmentRotation` moves all of it together.
    // The day/night map swap. The renderer holds one environment cube, so the two maps cannot be
    // blended in shading -- this binds whichever the curve selects. `setEnvironmentMap` marks the
    // composition dirty and the rebuild lands at the top of the next update(), so there is no
    // re-entrancy here; it also guards against redundant sets, so this is a no-op on all but the
    // two frames per cycle where the curve crosses. Those two frames pay a full rebuild, which is
    // why `hdriBlend` is shaped to cross where `hdriIntensity` is lowest.
    if (s.useNightMap != dayNightNightMap_) {
        const std::string& want = s.useNightMap ? dayNight_.nightMap : dayNight_.dayMap;
        if (!want.empty()) {
            dayNightNightMap_ = s.useNightMap;
            setEnvironmentMap(want);
        }
    }
    for (PunctualLight& light : scene_.lights) {
        if (!dayNight_.sunLight.empty() && light.name == dayNight_.sunLight) {
            light.direction = s.sunDirection;
            light.color = s.sunColor;
            light.intensity = s.sunIntensity;
            light.enabled = s.sunIntensity > 1e-3f;
        } else if (!dayNight_.moonLight.empty() && light.name == dayNight_.moonLight) {
            light.intensity = s.moonIntensity;
            light.enabled = s.moonIntensity > 1e-3f;
            // Aim the moon light at the night map's brightest feature -- its moon -- and rotate it
            // with the map. `lightFromEnvironment` does this too, but it aims `skyKeyLight`, which
            // is "the first enabled directional with role Key, else the first enabled directional
            // of any role". At night the sun is disabled and the first enabled directional is
            // whatever fill the scene happens to list first, so it aimed the wrong light and the
            // moonlight silently stopped agreeing with the visible moon the moment the map was
            // rotated. Naming the light and rotating its direction here cannot pick the wrong one.
            if (envDominantDirection_.has_value()) {
                const float a = scene_.environment.environmentRotation;
                const glm::vec3& m = *envDominantDirection_;
                const glm::vec3 towards(std::cos(a) * m.x - std::sin(a) * m.z, m.y,
                                        std::sin(a) * m.x + std::cos(a) * m.z);
                light.direction = -glm::normalize(towards);
            }
        }
    }
}

// ---- files -------------------------------------------------------------------------------------

// Composition framing (ADR-038): nudges the camera's aim so the named focal point lands at
// `targetScreenPosition` instead of the centre of frame. The position is untouched -- this is a
// pan and tilt, the way a framing decision is made on a real head -- and `framingStrength`
// interpolates between the authored aim and the fully framed one, so a shot can be nudged rather
// than snapped. The aspect comes from the lens's sensor, which is the authored intent; the render
// target's aspect is not known here and would make the framing depend on the output size.
// ADR-053: the light a glowing ecology casts. The scatter layers are reduced at build time to
// soft emitters, one per occupied cell of a coarse grid, and this picks the ones near the camera
// and makes them real lights. The count is bounded by the light budget rather than by how much is
// growing, so a meadow of ten thousand glowing ferns costs the same as a hundred.
void Composition::updateEcologyLights() {
    ecologyLightCount_ = 0;
    if (!ecologyLightsEnabled_) {
        return;
    }
    const std::size_t budget = kMaxEcologyLights > scene_.lights.size()
                                   ? kMaxEcologyLights - scene_.lights.size()
                                   : 0;
    if (budget == 0) {
        return;
    }

    struct Candidate {
        const world::GlowCluster* cluster;
        float distanceSq;
        glm::vec3 position;
    };
    std::vector<Candidate> candidates;
    const glm::vec3 eye = scene_.camera.position;
    for (std::size_t i = 0; i < nodes_.size() && i < ranges_.size(); ++i) {
        const CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Terrain || node.glow.empty()) {
            continue;
        }
        const glm::mat4 m = nodeTransform(node).matrix();
        for (const world::GlowCluster& g : node.glow) {
            const glm::vec3 world = glm::vec3(m * glm::vec4(g.position, 1.0f));
            const float d2 = glm::dot(world - eye, world - eye);
            if (d2 > ecologyLightRange_ * ecologyLightRange_) {
                continue;
            }
            candidates.push_back({&g, d2, world});
        }
    }
    if (candidates.empty()) {
        return;
    }
    // Nearest first: a patch behind the camera still lights the air and the ground it sits on, but
    // when the budget runs out the ones the frame is actually looking at are the ones to keep.
    if (candidates.size() > budget) {
        std::nth_element(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(budget),
                         candidates.end(),
                         [](const Candidate& a, const Candidate& b) { return a.distanceSq < b.distanceSq; });
        candidates.resize(budget);
    }

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const world::GlowCluster& g = *candidates[i].cluster;
        PunctualLight light;
        light.name = fmt::format("{}{}", kEcologyLightPrefix, i);
        // A point light, not a Sphere: a sphere emitter goes through the LTC area-light
        // integration, and at a couple of hundred of them that dominated the frame. What a patch
        // of glow needs is a soft falloff, which the radius already gives.
        light.type = PunctualLight::Type::Point;
        light.role = PunctualLight::Role::Practical;
        light.position = candidates[i].position;
        light.color = g.color;
        // The aggregate's power is a sum of emissive weights, not photometric candela. The scale
        // is the one free constant here: it sets how far a patch of glowing ecology throws light,
        // and it is authored per scene rather than guessed once.
        light.intensity = g.power * ecologyLightGain_;
        light.radius = std::max(g.radius, 0.25f);
        light.range = g.radius * 4.0f;
        light.castsShadow = false;   // hundreds of these; none of them can afford a shadow map
        light.contactShadow = false;
        light.volumetricStrength = 0.0f; // the volumetrics see these through the glow field, not here
        scene_.lights.push_back(std::move(light));
    }
    ecologyLightCount_ = candidates.size();
}

void Composition::setViewport(std::uint32_t width, std::uint32_t height) {
    viewportWidth_ = std::max(width, 1u);
    viewportHeight_ = std::max(height, 1u);
}

// ADR-099 §15. The water's reactive properties are ordinary parameters, so by the time this runs
// the modulator has already put the frame's signal through each route's gain, curve, threshold,
// attack/decay and depth. All that is left is to copy the finals into the surface the renderer
// reads -- which is the whole of "water music reactivity": no second modulation system, and a
// project can point `audio.bass` at `nodes/valley/water/glow` with no C++ written for it.
void Composition::updateWaterSurfaces() {
    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        if (node.kind != NodeKind::Terrain || node.waterSurfaceIndex < 0) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(node.waterSurfaceIndex);
        if (slot >= scene_.waters.size()) {
            continue;
        }
        WaterSettings& w = scene_.waters[slot].settings;
        w = node.terrain.water; // the authored rest, then whatever the routes moved
        if (node.waterGlowParam != nullptr) w.glow = node.waterGlowParam->value();
        if (node.waterSparkleParam != nullptr) w.sparkle = node.waterSparkleParam->value();
        if (node.waterRippleParam != nullptr) w.ripple = node.waterRippleParam->value();
        if (node.waterFlowSpeedParam != nullptr) w.rippleSpeed = node.waterFlowSpeedParam->value();
        if (node.waterSwellParam != nullptr) w.swell = node.waterSwellParam->value();
        if (node.waterFoamParam != nullptr) w.foam = node.waterFoamParam->value();
        if (node.waterGlowColorParam != nullptr) w.glowColor = node.waterGlowColorParam->value();
        if (node.waterClarityParam != nullptr) w.clarity = node.waterClarityParam->value();
        if (node.waterMaxOpacityParam != nullptr) w.maxOpacity = node.waterMaxOpacityParam->value();
        if (node.waterFresnelParam != nullptr) w.fresnel = node.waterFresnelParam->value();
        if (node.waterReflectionParam != nullptr) w.reflection = node.waterReflectionParam->value();
        if (node.waterRoughnessParam != nullptr) w.roughness = node.waterRoughnessParam->value();
        if (node.waterRefractionParam != nullptr) w.refraction = node.waterRefractionParam->value();
        if (node.waterRippleScaleParam != nullptr) w.rippleScale = node.waterRippleScaleParam->value();
        if (node.waterShallowDepthParam != nullptr) w.shallow = node.waterShallowDepthParam->value();
        if (node.waterShallowColorParam != nullptr) w.shallowColor = node.waterShallowColorParam->value();
        if (node.waterDeepColorParam != nullptr) w.deepColor = node.waterDeepColorParam->value();
    }
}

// ADR-099 §13. Every floating layer's instances, recomputed from the timeline second.
//
// The cost is the layer's instance count times a nearest-point query on a polyline, which for
// Glowmere's two hundred pads on a hundred-segment river is tens of microseconds -- and it buys
// the property that matters more than the microseconds: there is no state. A seek to t = 40 s puts
// every pad where a continuous playback would have had it, so a scrub, a re-render and a live
// preview of the same second agree exactly.
//
// Instances are written into the node's ProceduralGeometry and `structureVersion` is bumped, which
// is the renderer's upload key. That is a per-frame upload of count * 96 bytes -- 19 KB for two
// hundred pads -- and the bounds recompute that goes with it is a pass over the same array.
void Composition::updateFloaters(double time) {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Procedural || !node.floats.has_value()) {
            continue;
        }
        if (i >= ranges_.size() || ranges_[i].proceduralIndex < 0) {
            continue;
        }
        const auto slot = static_cast<std::size_t>(ranges_[i].proceduralIndex);
        if (slot >= scene_.procedurals.size()) {
            continue;
        }
        const CompositionNode* source = findNode(node.floats->water);
        if (source == nullptr || source->kind != NodeKind::Terrain) {
            // Named a node that is not a terrain, or nothing at all. Said once, loudly: a floating
            // layer that silently places nothing is precisely the failure this codebase keeps
            // repeating, and an empty river is not obviously a bug when you are looking at it.
            if (!node.floatWarned) {
                log::warn("node '{}': float/water names '{}', which is not a terrain node in this "
                          "composition; nothing will float",
                          node.name, node.floats->water);
                node.floatWarned = true;
            }
            continue;
        }
        if (source->waterBodies.empty()) {
            if (!node.floatWarned) {
                log::warn("node '{}': terrain '{}' has no water bodies; nothing will float", node.name,
                          node.floats->water);
                node.floatWarned = true;
            }
            continue;
        }
        evaluateFloaters(source->waterBodies, *node.floats, static_cast<float>(time), floaterScratch_);

        ProceduralGeometry& pg = scene_.procedurals[slot];
        // The node's own transform moves the world the water is in, so it moves what floats on it.
        const glm::mat4 model = nodeWorldTransform(node).matrix();
        pg.instances.resize(floaterScratch_.size());
        glm::vec3 lo(std::numeric_limits<float>::max());
        glm::vec3 hi(std::numeric_limits<float>::lowest());
        for (std::size_t k = 0; k < floaterScratch_.size(); ++k) {
            const Floater& f = floaterScratch_[k];
            const glm::vec3 world = glm::vec3(model * glm::vec4(f.position, 1.0f));
            spatial::InstanceRecord& rec = pg.instances[k];
            rec.position = glm::vec4(world, 1.0f);
            const glm::quat q = f.rotation;
            rec.rotation = glm::vec4(q.x, q.y, q.z, q.w);
            const float normalised =
                floaterScratch_.size() > 1
                    ? static_cast<float>(k) / static_cast<float>(floaterScratch_.size() - 1)
                    : 0.0f;
            rec.scale = glm::vec4(glm::vec3(f.scale), normalised);
            rec.random = glm::vec4(f.random, std::fmod(f.random * 7.13f, 1.0f),
                                   std::fmod(f.random * 3.77f, 1.0f), f.speed);
            rec.color = glm::vec4(1.0f, 1.0f, 1.0f, static_cast<float>(k));
            rec.emissive = glm::vec4(1.0f, 1.0f, 1.0f, f.random);
            lo = glm::min(lo, world);
            hi = glm::max(hi, world);
        }
        if (pg.instances.empty()) {
            lo = glm::vec3(0.0f);
            hi = glm::vec3(0.0f);
        }
        pg.boundsMin = lo;
        pg.boundsMax = hi;
        // The renderer uploads instances when this changes, which for a drifting layer is every
        // frame. Deliberately the same key rather than a second one: "the records are different"
        // is exactly what `structureVersion` means, and a layer that moves has different records.
        ++pg.structureVersion;
    }
}

void Composition::updateTerrainLod() {
    // The frustum is built at no narrower than a deliberately wide aspect: culling a chunk the
    // frame turns out to include is a hole in the ground, while keeping one it does not include
    // costs a draw call, so the error is taken on the safe side. Widening is safe in the strong
    // sense -- a wider frustum *contains* a narrower one at the same vertical field of view, which
    // `tests/unit/test_camera_lab_frustum.cpp` asserts rather than assumes.
    //
    // **What this costs, measured, because an inequality with no number attached cannot be judged.**
    // Against a grid of chunk-sized boxes in front of a real camera, the floor keeps this many more
    // than the exact aspect would: +16.7% at 2.39:1, +33.3% at 16:9, +64.7% at 4:3, +100% at 1:1 and
    // +180% at 9:16. It is free only above 2.5, and it is most expensive in exactly the portrait
    // orientation a short-form deliverable is rendered at.
    //
    // **And its original reason has expired.** The floor dates from when the composition did not
    // know the extent it would be drawn into -- the note further down this function still describes
    // the viewport as something "waiting" to be plumbed in, and it is wrong: `Engine::update` calls
    // `Composition::setViewport` before `applyParameters` runs, in the live path and in
    // `RenderJob`'s offline path both, so `viewportWidth_`/`viewportHeight_` are this frame's render
    // extent and `Composition::cullEntityNodes` twenty lines away already culls at the exact aspect
    // with no floor at all. Kept, not removed, because removing it changes how much ground is
    // submitted and the counters that certify this engine's draw budgets are somebody else's
    // baseline; the case for removing it is the five numbers above.
    constexpr float kCullAspectFloor = 2.5f;
    const float kCullAspect =
        std::max(kCullAspectFloor, static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_));
    std::optional<world::FrustumPlanes> planes;
    for (std::size_t i = 0; i < nodes_.size() && i < ranges_.size(); ++i) {
        const CompositionNode& node = *nodes_[i];
        if (node.kind != NodeKind::Terrain || node.chunks.empty()) {
            continue;
        }
        const NodeRange& range = ranges_[i];
        if (range.entityCount < node.chunks.size()) {
            continue; // a rebuild is pending; the entities and the chunks do not correspond yet
        }
        const bool lodEnabled = node.terrainLodParam == nullptr || node.terrainLodParam->value();
        const bool cullEnabled = node.terrainCullParam == nullptr || node.terrainCullParam->value();
        world::TerrainSettings settings = node.terrain;
        if (node.terrainLodDistanceParam != nullptr) {
            settings.lodDistance = node.terrainLodDistanceParam->value();
        }
        if (node.terrainViewDistanceParam != nullptr) {
            settings.viewDistance = node.terrainViewDistanceParam->value();
        }
        // LOD is a screen-space decision, so it takes this frame's actual viewport height rather
        // than the reference one `lodDistance` is authored against. Both halves now hold: changing
        // focal length re-picks levels, and so does resizing the window -- a taller window makes
        // the same ground larger in pixels and pulls the switch further out.
        const float projScale =
            world::lodProjectionScale(scene_.camera.effectiveFovY(), static_cast<float>(viewportHeight_));
        if (cullEnabled && !planes) {
            planes = world::frustumPlanes(scene_.camera.projection(kCullAspect) * scene_.camera.view());
        }
        const glm::vec3 eye = scene_.camera.position;
        // Water entities follow the ground chunks, one per chunk that has any, in chunk order. A
        // chunk's water is visible exactly when the chunk is: it is the same piece of world.
        std::size_t waterEntity = range.firstEntity + node.chunks.size();
        for (std::size_t c = 0; c < node.chunks.size(); ++c) {
            const world::TerrainChunk& chunk = node.chunks[c];
            Entity& e = scene_.entities[range.firstEntity + c];
            const std::size_t water = chunk.water != kInvalidMesh ? waterEntity++ : scene_.entities.size();
            const auto setWaterCulled = [&](bool culled) {
                if (water < scene_.entities.size()) {
                    scene_.entities[water].cameraCulled = culled;
                }
            };
            // The camera's verdict on this chunk, re-decided every frame; a chunk that was off
            // screen last frame must be able to come back.
            const auto setCameraCulled = [&](bool on) {
                e.cameraCulled = on;
                if (water < scene_.entities.size()) {
                    scene_.entities[water].cameraCulled = on;
                }
            };
            setCameraCulled(false);
            if (!e.visible) {
                setWaterCulled(true);
                continue; // the node itself is hidden; nothing below can turn it back on
            }
            // Chunk bounds are in the world map's own space; the node transform moves the world.
            const glm::mat4 m = e.transform.matrix();
            glm::vec3 lo(std::numeric_limits<float>::max());
            glm::vec3 hi(std::numeric_limits<float>::lowest());
            for (int k = 0; k < 8; ++k) {
                const glm::vec3 corner((k & 1) ? chunk.boundsMax.x : chunk.boundsMin.x,
                                       (k & 2) ? chunk.boundsMax.y : chunk.boundsMin.y,
                                       (k & 4) ? chunk.boundsMax.z : chunk.boundsMin.z);
                const glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            // Distance to the box, not to its centre: a chunk the camera is standing on is at
            // distance zero however big it is, and gets LOD 0.
            const float distance = glm::distance(eye, glm::clamp(eye, lo, hi));
            // A chunk casts into the shadow maps only while it is near enough for its shadow to be
            // resolvable. Beyond that it is still drawn -- it is on screen -- but not into three
            // cascades whose texels are far coarser than the shadow it would throw.
            const float shadowReach =
                settings.shadowDistance > 0.0f ? settings.shadowDistance : settings.viewDistance;
            if (distance > settings.viewDistance) {
                // Past the view distance the chunk is not in this world as far as the frame is
                // concerned: no mesh is picked for it and nothing it might cast could reach a
                // cascade, which only ever covers the near part of the camera's frustum.
                e.castsShadow = false;
                setCameraCulled(true);
                continue;
            }
            e.castsShadow = distance <= shadowReach;
            // The water's own flag is left where it was set at build: false. ADR-099 made the
            // surface translucent, and a translucent sheet throwing a hard shadow onto its own bed
            // was a bug nobody had looked at. The renderer excludes MeshStyle::Water from the
            // shadow passes anyway; keeping the flag honest means the two agree about why.
            if (planes && !world::aabbVisible(*planes, lo, hi)) {
                // Off screen, not absent. The camera passes skip it; the shadow passes still get
                // it as a candidate and test it against each cascade's own frustum, because a hill
                // behind the camera casts into shot (ADR-046). Its LOD mesh is still picked below
                // -- a caster needs geometry, and its silhouette is what the map records.
                setCameraCulled(true);
            }
            // LOD follows how big the ground looks, not how far away it is, and `projScale` above
            // is built from this frame's actual `viewportHeight_` -- so changing the focal length
            // re-picks levels and so does resizing the window.
            //
            // This comment used to say the opposite, in the same function as the code that had
            // already fixed it: that the composition "knows the lens but not the viewport", that
            // window size did not re-pick levels, and that the cull aspect *below* was waiting for
            // the same plumbing. Three claims, all stale, and the cull aspect is above. Left
            // recorded rather than deleted because a comment that contradicts the code thirty lines
            // above it is a specific kind of trap: it reads as authority, and the next person to
            // touch terrain LOD would have plumbed in a viewport that was already there.
            const int lod = lodEnabled ? world::chunkLod(settings, distance, projScale) : 0;
            const MeshId mesh = chunk.meshes[static_cast<std::size_t>(std::clamp(lod, 0, kMaxTerrainLodIndex))];
            if (mesh != kInvalidMesh) {
                e.mesh = mesh;
            }
        }
    }
}

// ---- what the viewport is looking through (ADR-391) --------------------------------------------

void Composition::setViewportView(ViewportView view) {
    // A `Through` that names nothing is a `Film`, normalised here rather than at every reader. The
    // id is checked at use, not here: a caller may legitimately set the mode before the camera it
    // names exists (a project load re-creates the rigs), and refusing it here would turn a reorder
    // into a silent mode change.
    if (view.mode == ViewportCamera::Through && view.camera == kNoCamera) {
        view.mode = ViewportCamera::Film;
    }
    viewportView_ = view;
}

void Composition::applyViewportView(float mainFovDegrees) {
    if (viewportView_.showsFilm()) {
        return; // every render, and every frame of every project that has not asked for anything
    }
    if (viewportView_.mode == ViewportCamera::Editor) {
        if (!editorCameraSeeded_) {
            // **Seeded from the film, once.** Switching to the editor viewpoint must not teleport
            // you: the first frame under it is the frame you were already looking at, and only then
            // does it become yours. Without this, taking the viewport off a shot threw you to
            // wherever a default pose happened to be -- which is the "there is no reliable way to
            // leave the director camera" report in a different costume.
            editorCamera_.position = scene_.camera.position;
            editorCamera_.target = scene_.camera.target;
            editorCamera_.fovDegrees = glm::degrees(scene_.camera.fovYRadians);
            editorCamera_.focalLength = 0.0f;
            editorCameraSeeded_ = true;
        }
        CameraPose pose = editorCamera_;
        ensureDistinctAim(pose);
        scene_.camera.position = pose.position;
        scene_.camera.target = pose.target;
        scene_.camera.fovYRadians =
            glm::radians(pose.fovDegrees > 0.0f ? pose.fovDegrees : mainFovDegrees);
        // The editor's viewpoint has no opinion about the lens, which is what stops an authored
        // 24 mm rig holding the film from re-imposing its focal length on a frame it is not in
        // (`Engine::update` reads these two and pushes them onto the physical lens). Focus, on the
        // other hand, is what the *viewport* is aimed at -- a frame focused on a subject somewhere
        // off screen is the same defect as a picker that rays from the wrong camera.
        activeCamera_.focalLength = 0.0f;
        activeCamera_.focusDistance = glm::length(pose.target - pose.position);
        return;
    }
    // Through a camera: the rig's own pose, its own lens, whatever the director is doing.
    const CameraRig* rig = cameraDirection_.find(viewportView_.camera);
    if (rig == nullptr) {
        return; // a camera that is gone cannot be looked through; the film keeps the frame
    }
    if (rig->id == kMainCamera) {
        // The main camera *is* `camera/*`, so "through the main camera" is the film's own answer
        // whenever the film is on it -- and when it is not, it is the pose the main camera would
        // have. Evaluated rather than taken from `scene_.camera`, which may hold an authored rig.
        const CameraPose pose = evaluateMainCamera();
        scene_.camera.position = pose.position;
        scene_.camera.target = pose.target;
        scene_.camera.fovYRadians = glm::radians(mainFovDegrees);
        activeCamera_.focalLength = 0.0f;
        activeCamera_.focusDistance = glm::length(pose.target - pose.position);
        return;
    }
    const CameraChannels* channels = nullptr;
    for (const CameraChannels& c : cameraChannels_) {
        if (c.id == rig->id) {
            channels = &c;
            break;
        }
    }
    CameraPose pose = evaluateAuthoredCamera(*rig, channels);
    ensureDistinctAim(pose);
    scene_.camera.position = pose.position;
    scene_.camera.target = pose.target;
    scene_.camera.fovYRadians = glm::radians(pose.fovDegrees);
    activeCamera_.focalLength = pose.focalLength;
    activeCamera_.focusDistance = glm::length(pose.target - pose.position);
}

void Composition::applyFraming() {
    const float strength = std::clamp(compositionData_.framingStrength, 0.0f, 1.0f);
    if (strength <= 0.0f || compositionData_.cameraTarget.empty()) {
        return;
    }
    const FocalPoint* focus = compositionData_.find(compositionData_.cameraTarget);
    if (focus == nullptr) {
        return;
    }
    const glm::vec3 toFocus = focus->position - scene_.camera.position;
    if (glm::dot(toFocus, toFocus) < 1e-8f) {
        return;
    }
    const glm::vec3 original =
        safeNormalize(scene_.camera.target - scene_.camera.position, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 worldUp = safeNormalize(scene_.camera.up, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 subject = safeNormalize(toFocus, original);

    // Where the focal point should sit, as an offset from the centre of frame. Screen coordinates
    // have a top-left origin, so y flips into NDC.
    const float tanY = std::tan(scene_.camera.effectiveFovY() * 0.5f);
    const float aspect = scene_.camera.lens.sensorHeight > 1e-4f
                             ? scene_.camera.lens.sensorWidth / scene_.camera.lens.sensorHeight
                             : 16.0f / 9.0f;
    const float ndcX = compositionData_.targetScreenPosition.x * 2.0f - 1.0f;
    const float ndcY = 1.0f - compositionData_.targetScreenPosition.y * 2.0f;

    // Each step aims so that the requested screen offset, measured in the basis the previous step
    // produced, points at the subject. Rotating the aim moves the basis too, so one step leaves a
    // fraction of a percent of frame height on the table; three converge well inside a pixel.
    glm::vec3 forward = original;
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 right = safeNormalize(glm::cross(forward, worldUp), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 up = glm::cross(right, forward);
        const glm::vec3 wanted =
            safeNormalize(forward + right * (ndcX * tanY * aspect) + up * (ndcY * tanY), forward);
        forward = safeNormalize(glm::rotation(wanted, subject) * forward, forward);
    }

    // Interpolate the aim itself, so a shot can be nudged towards the framing rather than snapped.
    const glm::vec3 aimed =
        safeNormalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(original, forward), strength) *
                          original,
                      forward);
    const float distance = std::max(glm::length(scene_.camera.target - scene_.camera.position), 1e-3f);
    scene_.camera.target = scene_.camera.position + aimed * distance;
}

Result<void> Composition::setLightRig(const std::filesystem::path& path) {
    lightRigPath_ = path;
    if (params_ != nullptr && !lightRigParams_.all.empty()) {
        unregisterLightRigParameters(*params_, lightRigParams_);
        lightRigParams_ = {};
    }
    lightRig_.reset();
    rigLightCount_ = 0;
    dirty_ = true;
    if (path.empty()) {
        return {};
    }
    auto rig = LightRig::loadFile(registry_.resolve(path));
    if (!rig) {
        return std::unexpected(rig.error());
    }
    return installLightRig(std::move(*rig), path);
}

Result<void> Composition::installLightRig(LightRig rig, const std::filesystem::path& sourcePath) {
    if (auto ok = rig.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    // Same teardown as the path overload, because installing a rig built in memory has to leave the
    // parameter set in the same state as installing one read from a file. Registering over the top
    // of the previous rig's parameters is how a scene ends up with two "lightrig/.../keyIntensity"
    // entries, one of which nothing reads.
    if (params_ != nullptr && !lightRigParams_.all.empty()) {
        unregisterLightRigParameters(*params_, lightRigParams_);
        lightRigParams_ = {};
    }
    lightRigPath_ = sourcePath;
    lightRig_ = std::move(rig);
    rigLightCount_ = 0;
    dirty_ = true;
    if (params_ != nullptr) {
        lightRigParams_ = registerLightRigParameters(*params_, *lightRig_, prefix_);
    }
    return {};
}

void Composition::setEnvironmentMap(const std::filesystem::path& path) {
    // Setting the same map twice changes nothing about the flattened scene, and `dirty_` is not a
    // request to re-read the file -- the registry caches images by path, so a second rebuild would
    // resolve to the identical texture. It is a request to rebuild everything: 256 terrain chunks,
    // 260k scatter cells, every procedural cloud.
    //
    // Which is what the load path was asking for. `attach()` rebuilds (it fits the camera to the
    // flattened bounds), and `loadComposition` then calls `reapplyEnvironment()`, which called this
    // unconditionally with the path the scene had just been loaded with -- so every scene carrying
    // an environment map built its world twice on open. Measured on
    // examples/world/terrain.scene.json: 775 ms in the first frame's engine update, 397 ms after.
    //
    // Compared as resolved paths, because the two sides genuinely differ in form: the scene file
    // stores a relative path and the engine hands back the absolute one it resolved in order to
    // load the image. Comparing the raw strings would never match and the guard would never fire.
    const bool same = environmentPath_.empty() && path.empty()
                          ? true
                          : (!environmentPath_.empty() && !path.empty() &&
                             registry_.resolve(environmentPath_) == registry_.resolve(path));
    if (same) {
        return;
    }
    environmentPath_ = path;
    // ADR-346: an environment change is an environment change. It used to set `dirty_`, and
    // `dirty_` has no granularity -- so swapping a map re-flattened 557 entities and 1,069 meshes
    // to change one texture id, 273-293 ms, twice per day/night cycle.
    environmentDirty_ = true;
}

nlohmann::json Composition::toJson() const {
    json j;
    j["format"] = kFormatName;
    j["version"] = kFormatVersion;
    j["name"] = name_;

    json camera = json::object();
    if (cameraDistance_ != nullptr) {
        camera["distance"] = cameraDistance_->base();
    } else if (cameraDistanceSetting_) {
        camera["distance"] = *cameraDistanceSetting_;
    }
    if (cameraHeight_ != nullptr) {
        camera["height"] = cameraHeight_->base();
    } else if (cameraHeightSetting_) {
        camera["height"] = *cameraHeightSetting_;
    }
    camera["orbitSpeed"] =
        cameraOrbitSpeed_ != nullptr ? cameraOrbitSpeed_->base() : cameraOrbitSpeedSetting_;
    camera["fov"] = cameraFov_ != nullptr ? cameraFov_->base() : cameraFovSetting_;
    camera["mode"] = cameraMode_ != nullptr ? cameraMode_->base() : cameraModeSetting_;
    if (!cameraSplineSetting_.empty()) {
        camera["spline"] = cameraSplineSetting_;
    }
    {
        const glm::vec3 cp = cameraPosition_ != nullptr ? cameraPosition_->base() : cameraPositionSetting_;
        const glm::vec3 ct = cameraTarget_ != nullptr ? cameraTarget_->base() : cameraTargetSetting_;
        camera["position"] = {cp.x, cp.y, cp.z};
        camera["target"] = {ct.x, ct.y, ct.z};
    }
    j["camera"] = std::move(camera);
    // ADR-245: the camera collection and the shot track, written only when there is more than the
    // one camera every scene has always had -- so a scene file untouched by the multi-camera system
    // is byte-identical to what it was.
    if (cameraDirection_.directing()) {
        j["cameraDirection"] = cameraDirection_.toJson();
    }

    json environment = json::object();
    if (!environmentPath_.empty()) {
        environment["map"] = environmentPath_.generic_string();
    }
    if (!lightRigPath_.empty()) {
        environment["lightRig"] = lightRigPath_.generic_string();
    } else if (lightRig_) {
        // A rig built in memory has no file to point at, so it is written out in full. Without this
        // a generated world's rig survives exactly as long as the process: an offline render saves
        // the project and reloads it in a fresh engine, and the world came back lit by the default
        // key with the art direction's key-to-ambient ratio silently gone.
        environment["lightRig"] = lightRig_->toJson();
    }
    environment["intensity"] = envIntensity_ != nullptr ? envIntensity_->base() : envIntensitySetting_;
    environment["rotation"] = envRotation_ != nullptr ? envRotation_->base() : envRotationSetting_;
    environment["skyIntensity"] = skyIntensitySetting_;
    environment["stylized"] = stylized_ != nullptr ? stylized_->base() : stylizedSetting_;
    environment["skybox"] = showSkyboxSetting_;
    // ADR-350: `dayNight` was READ and never written. A scene that carried a cycle and was saved
    // lost the entire block -- curves, bindings, cycle length, all of it. That is not a missing
    // UI, it is data loss, and it is the same defect as the 94 orphaned `atmos/*` parameters this
    // project has already been bitten by once.
    {
        const scene::DayNightSettings& dn = dayNight_;
        const scene::DayNightSettings def{};
        json d = json::object();
        d["enabled"] = dn.enabled;
        if (dn.paused != def.paused) d["paused"] = dn.paused;
        if (dn.manualPhase != def.manualPhase) d["dayPhase"] = dn.manualPhase;
        if (dn.cycleSeconds != def.cycleSeconds) d["cycleSeconds"] = dn.cycleSeconds;
        if (dn.phaseOffset != def.phaseOffset) d["phaseOffset"] = dn.phaseOffset;
        if (dn.sunPeakElevation != def.sunPeakElevation) d["sunPeakElevation"] = dn.sunPeakElevation;
        if (dn.sunAzimuthAtDawn != def.sunAzimuthAtDawn) d["sunAzimuthAtDawn"] = dn.sunAzimuthAtDawn;
        if (dn.sunAzimuthSweep != def.sunAzimuthSweep) d["sunAzimuthSweep"] = dn.sunAzimuthSweep;
        if (dn.sunIntensityScale != def.sunIntensityScale) d["sunIntensityScale"] = dn.sunIntensityScale;
        if (dn.moonIntensityScale != def.moonIntensityScale) d["moonIntensityScale"] = dn.moonIntensityScale;
        if (dn.starBrightnessScale != def.starBrightnessScale) d["starBrightnessScale"] = dn.starBrightnessScale;
        if (dn.hdriIntensityScale != def.hdriIntensityScale) d["hdriIntensityScale"] = dn.hdriIntensityScale;
        if (dn.glowInfluence != def.glowInfluence) d["glowInfluence"] = dn.glowInfluence;
        if (dn.fogHorizonBlend != def.fogHorizonBlend) d["fogHorizonBlend"] = dn.fogHorizonBlend;
        if (!dn.sunLight.empty()) d["sunLight"] = dn.sunLight;
        if (!dn.moonLight.empty()) d["moonLight"] = dn.moonLight;
        if (!dn.dayMap.empty()) d["dayMap"] = dn.dayMap;
        if (!dn.nightMap.empty()) d["nightMap"] = dn.nightMap;
        if (!dn.starNodes.empty()) d["starNodes"] = dn.starNodes;
        if (!dn.glowNodes.empty()) d["glowNodes"] = dn.glowNodes;
        const auto writeVec3Curve = [&d](const char* key,
                                         const std::vector<scene::PhaseKey<glm::vec3>>& keys) {
            json a = json::array();
            for (const auto& k : keys) {
                a.push_back({{"phase", k.phase},
                             {"value", json::array({k.value.x, k.value.y, k.value.z})}});
            }
            d[key] = std::move(a);
        };
        const auto writeFloatCurve = [&d](const char* key,
                                          const std::vector<scene::PhaseKey<float>>& keys) {
            json a = json::array();
            for (const auto& k : keys) {
                a.push_back({{"phase", k.phase}, {"value", k.value}});
            }
            d[key] = std::move(a);
        };
        if (dn.enabled) {
            writeVec3Curve("zenithColor", dn.zenithColor);
            writeVec3Curve("horizonColor", dn.horizonColor);
            writeVec3Curve("groundColor", dn.groundColor);
            writeVec3Curve("sunColor", dn.sunColor);
            writeVec3Curve("fogColor", dn.fogColor);
            writeVec3Curve("waterDeepColor", dn.waterDeepColor);
            writeFloatCurve("sunIntensity", dn.sunIntensity);
            writeFloatCurve("moonIntensity", dn.moonIntensity);
            writeFloatCurve("skyIntensity", dn.skyIntensity);
            writeFloatCurve("haze", dn.haze);
            writeFloatCurve("starBrightness", dn.starBrightness);
            writeFloatCurve("hdriIntensity", dn.hdriIntensity);
            writeFloatCurve("hdriBlend", dn.hdriBlend);
            writeFloatCurve("glowScale", dn.glowScale);
            writeFloatCurve("fogDensity", dn.fogDensity);
            writeFloatCurve("waterReflection", dn.waterReflection);
        }
        environment["dayNight"] = std::move(d);
    }
    environment["skyBloom"] = skyBloomSetting_;
    if (ecologyLightGain_ > 0.0f) {
        environment["ecologyLight"] = ecologyLightGain_;
        environment["ecologyLightRange"] = ecologyLightRange_;
        environment["ecologyGlowCell"] = ecologyGlowCell_;
    }
    if (lightFromEnvironmentSetting_) {
        environment["lightFromEnvironment"] = true;
    }
    environment["fogDensity"] = fogDensity_ != nullptr ? fogDensity_->base() : fogDensitySetting_;
    {
        // ADR-358. Out here rather than in the volumetric block below, which is where it was
        // first written and where a test caught it: that block is guarded by the volume being ON,
        // and this scene has no volume, so the save dropped the range and the reload cast no
        // shadow again -- ADR-207/230's silent deletion, one field along. Written only when the
        // scene has an opinion, so a file that never set it is byte-identical after a round trip.
        //
        // Note for whoever comes to `shadowCascades` next: it is still inside that guard, and a
        // scene that sets cascades without a volume loses them on save for exactly this reason.
        // Left alone here because fixing it changes files this branch does not own.
        const float range = shadowRange_ != nullptr ? shadowRange_->base() : volumeSetting_.shadowRange;
        if (range > 0.0f) {
            environment["shadowRange"] = range;
        }
    }
    {
        // Procedural sky (ADR-036): written only when it differs from the defaults, so scene files
        // that never touched it stay byte-identical.
        const SkySettings def;
        SkySettings sky = skySetting_;
        const auto f = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->base() : fallback;
        };
        const auto c = [](const params::Parameter<glm::vec3>* p, const glm::vec3& fallback) {
            return p != nullptr ? p->base() : fallback;
        };
        if (skyEnabled_ != nullptr) {
            sky.enabled = skyEnabled_->base();
        }
        if (skyBackground_ != nullptr) {
            sky.showBackground = skyBackground_->base();
        }
        sky.zenithColor = c(skyZenith_, sky.zenithColor);
        sky.horizonColor = c(skyHorizon_, sky.horizonColor);
        sky.groundColor = c(skyGround_, sky.groundColor);
        sky.sunColor = c(skySunColor_, sky.sunColor);
        sky.hazeWidth = f(skyHaze_, sky.hazeWidth);
        sky.sunIntensity = f(skySunIntensity_, sky.sunIntensity);
        sky.sunAngularRadius = f(skySunSize_, sky.sunAngularRadius);
        sky.sunGlowWidth = f(skySunGlow_, sky.sunGlowWidth);
        sky.intensity = f(skyIntensity_, sky.intensity);
        json sj = json::object();
        const auto colourEq = [](const glm::vec3& a, const glm::vec3& b) { return a == b; };
        if (sky.enabled != def.enabled) sj["enabled"] = sky.enabled;
        if (sky.showBackground != def.showBackground) sj["background"] = sky.showBackground;
        if (sky.useKeyLight != def.useKeyLight) sj["useKeyLight"] = sky.useKeyLight;
        if (!colourEq(sky.zenithColor, def.zenithColor))
            sj["zenithColor"] = {sky.zenithColor.r, sky.zenithColor.g, sky.zenithColor.b};
        if (!colourEq(sky.horizonColor, def.horizonColor))
            sj["horizonColor"] = {sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b};
        if (!colourEq(sky.groundColor, def.groundColor))
            sj["groundColor"] = {sky.groundColor.r, sky.groundColor.g, sky.groundColor.b};
        if (!colourEq(sky.sunColor, def.sunColor))
            sj["sunColor"] = {sky.sunColor.r, sky.sunColor.g, sky.sunColor.b};
        if (!colourEq(sky.sunDirection, def.sunDirection))
            sj["sunDirection"] = {sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z};
        if (sky.hazeWidth != def.hazeWidth) sj["haze"] = sky.hazeWidth;
        if (sky.sunIntensity != def.sunIntensity) sj["sunIntensity"] = sky.sunIntensity;
        if (sky.sunAngularRadius != def.sunAngularRadius) sj["sunSize"] = sky.sunAngularRadius;
        if (sky.sunGlowWidth != def.sunGlowWidth) sj["sunGlow"] = sky.sunGlowWidth;
        if (sky.intensity != def.intensity) sj["intensity"] = sky.intensity;
        if (!sj.empty()) {
            environment["sky"] = std::move(sj);
        }
    }
    {
        const glm::vec3 fc = fogColor_ != nullptr ? fogColor_->base() : (fogColorSet_ ? fogColorSetting_ : scene_.environment.backgroundColor);
        environment["fogColor"] = {fc.r, fc.g, fc.b};
        const glm::vec3 bg = scene_.environment.backgroundColor;
        environment["background"] = {bg.r, bg.g, bg.b};
        // ADR-058: written only when moved, so a scene that never mentioned them round-trips
        // byte for byte.
        const scene::Environment envDefaults;
        const auto& v = volumeSetting_;
        const auto colourEq3 = [](const glm::vec3& a, const glm::vec3& b) { return a == b; };
        // ADR-574: the PARAMETER's base where there is one, not the authored setting. A control an
        // artist can move and cannot keep is not a control -- the same half of "reachable" ADR-573's
        // case asks about, and writing `v.fogHeightAmount` here would have lost every change made
        // through the row this ADR just added.
        const float heightAmount =
            fogHeightAmount_ != nullptr ? fogHeightAmount_->base() : v.fogHeightAmount;
        if (heightAmount != envDefaults.fogHeightAmount) {
            environment["fogHeightAmount"] = heightAmount;
        }
        if (!colourEq3(v.styledSkyAmbient, envDefaults.styledSkyAmbient)) {
            environment["styledSkyAmbient"] = {v.styledSkyAmbient.r, v.styledSkyAmbient.g, v.styledSkyAmbient.b};
        }
        if (!colourEq3(v.styledGroundAmbient, envDefaults.styledGroundAmbient)) {
            environment["styledGroundAmbient"] = {v.styledGroundAmbient.r, v.styledGroundAmbient.g,
                                                  v.styledGroundAmbient.b};
        }
        if (v.styledAmbientFloor != envDefaults.styledAmbientFloor) {
            environment["styledAmbientFloor"] = v.styledAmbientFloor;
        }
    }
    {
        // Volumetric atmosphere (ADR-032); written only when it is on, so existing files are
        // unchanged by a round trip.
        const float density = volumeDensity_ != nullptr ? volumeDensity_->base() : volumeSetting_.volumeDensity;
        if (density > 0.0f || !volumeDensityFieldSetting_.empty() || !volumeColorFieldSetting_.empty()) {
            auto base = [](const params::Parameter<float>* p, float fallback) {
                return p != nullptr ? p->base() : fallback;
            };
            environment["volumeDensity"] = density;
            environment["fogHeight"] = base(fogHeight_, volumeSetting_.fogHeight);
            environment["fogHeightFalloff"] = base(fogHeightFalloff_, volumeSetting_.fogHeightFalloff);
            environment["fogUpperDensity"] = base(fogUpperDensity_, volumeSetting_.fogUpperDensity);
            environment["fogHeightCurve"] = base(fogHeightCurve_, volumeSetting_.fogHeightCurve);
            environment["volumeScattering"] = base(volumeScattering_, volumeSetting_.volumeScattering);
            environment["volumeAbsorption"] = base(volumeAbsorption_, volumeSetting_.volumeAbsorption);
            environment["volumeAnisotropy"] = base(volumeAnisotropy_, volumeSetting_.volumeAnisotropy);
            environment["volumeLocalLights"] = base(volumeLocalLights_, volumeSetting_.volumeLocalLights);
            environment["volumeNoise"] = base(volumeNoise_, volumeSetting_.volumeNoiseAmount);
            environment["volumeNoiseScale"] = base(volumeNoiseScale_, volumeSetting_.volumeNoiseScale);
            environment["volumeNoiseSpeed"] = base(volumeNoiseSpeed_, volumeSetting_.volumeNoiseSpeed);
            environment["volumeEmission"] = base(volumeEmission_, volumeSetting_.volumeEmission);
            environment["volumeSteps"] = volumeSteps_ != nullptr ? volumeSteps_->base() : volumeSetting_.volumeSteps;
            environment["volumeShadowSteps"] =
                volumeShadowSteps_ != nullptr ? volumeShadowSteps_->base() : volumeSetting_.volumeShadowSteps;
            environment["volumeShadowStrength"] =
                base(volumeShadowStrength_, volumeSetting_.volumeShadowStrength);
            environment["volumeJitter"] = base(volumeJitter_, volumeSetting_.volumeJitter);
            environment["shadowCascades"] = volumeSetting_.shadowCascades;
            environment["volumeMaxDistance"] = base(volumeMaxDistance_, volumeSetting_.volumeMaxDistance);
            if (!volumeDensityFieldSetting_.empty()) {
                environment["volumeDensityField"] = volumeDensityFieldSetting_;
            }
            if (!volumeColorFieldSetting_.empty()) {
                environment["volumeColorField"] = volumeColorFieldSetting_;
            }
        }
    }
    j["environment"] = std::move(environment);
    // ADR-278: the lights the scene authored, written back so a save cannot silently delete them --
    // which is ADR-207/230's world-effects bug, and the reason `environment["lightRig"]` twenty
    // lines up writes an in-memory rig out in full. Emitted only when there are any, so a scene that
    // authors none is byte-identical to one written before this key was read.
    if (!authoredLights_.empty()) {
        json lights = json::array();
        for (const AuthoredLight& a : authoredLights_) {
            lights.push_back(authoredLightToJson(a));
        }
        j["lights"] = std::move(lights);
    }
    // ADR-059: written back as it was read. The live values are parameters and belong to the
    // project; what the scene owns is the look it was authored with.
    if (!postJson_.is_null() && !postJson_.empty()) {
        j["post"] = postJson_;
    }
    // ADR-193. Emitted only when it is not the default, so an untouched scene is byte-identical to
    // one written before this key existed.
    if (navCellSize_ != 4.0f) {
        j["navCellSize"] = navCellSize_;
    }
    if (navBodyRadius_ > 0.0f) {
        j["navBodyRadius"] = navBodyRadius_;
    }
    if (navWadeDepth_ != 0.0f) {
        j["navWadeDepth"] = navWadeDepth_;
    }
    // ADR-360. This used to be `if (windSetting_.enabled)`, which made the writer unreachable from
    // the same place the reader was: `enabled` had no control, so it could only ever be true if the
    // file already said so, and a scene that did not say so could never start saying it. A save now
    // records whatever the parameters currently say, and emits nothing at all while the wind is
    // still at its defaults -- so a scene written before this key existed is byte-identical.
    {
        wind::WindParams w = windSetting_;
        auto base = [](const params::Parameter<float>* p, float fallback) {
            return p != nullptr ? p->base() : fallback;
        };
        w.enabled = windEnabled_ != nullptr ? windEnabled_->base() : windSetting_.enabled;
        w.speed = base(windSpeed_, windSetting_.speed);
        w.direction = base(windDirection_, windSetting_.direction);
        w.gustAmount = base(windGustAmount_, windSetting_.gustAmount);
        w.gustScale = base(windGustScale_, windSetting_.gustScale);
        w.gustSpeed = base(windGustSpeed_, windSetting_.gustSpeed);
        w.gustSharpness = base(windGustSharpness_, windSetting_.gustSharpness);
        w.turbulence = base(windTurbulence_, windSetting_.turbulence);
        w.turbulenceScale = base(windTurbulenceScale_, windSetting_.turbulenceScale);
        w.turbulenceSpeed = base(windTurbulenceSpeed_, windSetting_.turbulenceSpeed);
        w.regionScale = base(windRegionScale_, windSetting_.regionScale);
        w.regionAmount = base(windRegionAmount_, windSetting_.regionAmount);
        w.regionDrift = base(windRegionDrift_, windSetting_.regionDrift);
        w.flutterScale = base(windFlutterScale_, windSetting_.flutterScale);
        if (wind::windToJson(w) != wind::windToJson(wind::WindParams{})) {
            j["wind"] = wind::windToJson(w);
        }
    }

    json nodes = json::array();
    for (const auto& nodePtr : nodes_) {
        const CompositionNode& node = *nodePtr;
        // Graph-installed nodes are re-emitted by the graph on load; only hand-made nodes are written.
        if (std::find(graphNodes_.begin(), graphNodes_.end(), node.name) != graphNodes_.end()) {
            continue;
        }
        json n;
        n["name"] = node.name;
        n["kind"] = nodeKindName(node.kind);
        if (!node.asset.empty()) {
            n["asset"] = node.asset.generic_string();
        }
        if (!node.parent.empty()) {
            n["parent"] = node.parent;
        }
        // The parameter's base value when there is one, the authored transform when there is not.
        // Rotation always did this; position and scale did not, so a node moved or resized through
        // its parameter -- which is the only way the editor can move it, and what a preset writes --
        // came back where it started the moment the scene was saved and reloaded. Three fields that
        // disagreed about where a node's transform lives is exactly the class of bug where an edit
        // silently does nothing (ADR-092).
        n["position"] = vecToJson(node.positionParam != nullptr ? node.positionParam->base()
                                                                : node.transform.position);
        n["rotation"] = vecToJson(node.rotationParam != nullptr ? node.rotationParam->base()
                                                                : eulerDegrees(node.transform.rotation));
        n["scale"] = vecToJson(node.scaleParam != nullptr ? node.scaleParam->base()
                                                          : node.transform.scale);
        
        n["visible"] = node.visible;
        // Only when locked: an additive key that no existing scene carries, so a file written by
        // this build and read by an older one loses a working preference and nothing else, and a
        // file that was never locked round-trips byte for byte.
        if (node.locked) {
            n["locked"] = true;
        }
        n["emissiveBoost"] = node.emissiveBoost;
        n["roughnessScale"] = node.roughnessScale;
        // ADR-360. Written only when the node declares a wind body, so nothing else grows a key,
        // and written from the parameters' BASE so a session's edits survive the save -- the
        // failure this whole ADR is about was a reader whose writer could not be reached.
        // ADR-370: only when asked for, like everything else additive here.
        if (!node.canopySource.empty()) {
            n["canopySource"] = node.canopySource;
            n["canopyFrom"] = node.canopyFrom;
        }
        // ADR-380.
        if (node.vortexAttractor) {
            n["vortexAttractor"] = true;
            n["vortexReach"] = node.vortexReach;
        }
        // ADR-376: the tree's emissive life, written only when declared.
        if (node.energyAuthored) {
            auto eb = [](const params::Parameter<float>* p, float fallback) {
                return p != nullptr ? p->base() : fallback;
            };
            auto ec = [](const params::Parameter<glm::vec3>* p, const glm::vec3& fallback) {
                const glm::vec3 v = p != nullptr ? p->base() : fallback;
                return json::array({v.x, v.y, v.z});
            };
            json e;
            e["intensity"] = eb(node.energyIntensityParam, node.energy.intensity);
            e["pulseSpeed"] = eb(node.energyPulseSpeedParam, node.energy.pulseSpeed);
            e["pulseWidth"] = eb(node.energyPulseWidthParam, node.energy.pulseWidth);
            e["propagation"] = eb(node.energyPropagationParam, node.energy.propagation);
            e["root"] = eb(node.energyRootParam, node.energy.root);
            e["trunk"] = eb(node.energyTrunkParam, node.energy.trunk);
            e["branch"] = eb(node.energyBranchParam, node.energy.branch);
            e["canopy"] = eb(node.energyCanopyParam, node.energy.canopy);
            e["noise"] = eb(node.energyNoiseParam, node.energy.noiseAmount);
            e["noiseScale"] = node.energy.noiseScale;
            e["noiseSpeed"] = node.energy.noiseSpeed;
            e["bloom"] = eb(node.energyBloomParam, node.energy.bloom);
            e["shimmer"] = eb(node.shimmerParam, node.energy.shimmer);
            e["shimmerSpeed"] = eb(node.shimmerSpeedParam, node.energy.shimmerSpeed);
            e["shimmerScale"] = eb(node.shimmerScaleParam, node.energy.shimmerScale);
            e["shimmerVariation"] = node.energy.shimmerVariation;
            e["colorNear"] = ec(node.energyColorNearParam, node.energy.colorNear);
            e["colorFar"] = ec(node.energyColorFarParam, node.energy.colorFar);
            n["energy"] = std::move(e);
        }
        if (node.windAuthored) {
            auto base = [](const params::Parameter<float>* p, float fallback) {
                return p != nullptr ? p->base() : fallback;
            };
            json w;
            w["strength"] = base(node.windStrengthParam, node.wind.strength);
            w["trunk"] = base(node.windTrunkParam, node.wind.trunk);
            w["branch"] = base(node.windBranchParam, node.wind.branch);
            w["foliage"] = base(node.windFoliageParam, node.wind.foliage);
            w["flutter"] = base(node.windFlutterParam, node.wind.flutter);
            w["lag"] = base(node.windLagParam, node.wind.lag);
            n["wind"] = std::move(w);
        }
        if (node.lod.enabled) { // ADR-351; written only when asked for, so nothing else grows a key
            json lod;
            lod["enabled"] = true;
            lod["thinning"] = node.lod.thinning;
            lod["hysteresis"] = node.lod.hysteresis;
            if (node.lod.maxScreenError >= 0.0f) {
                lod["maxScreenError"] = node.lod.maxScreenError;
            }
            if (!node.lod.ratios.empty()) {
                lod["ratios"] = node.lod.ratios;
            }
            n["lod"] = std::move(lod);
        }
        if (node.kind == NodeKind::Gltf && node.animation.authored()) { // ADR-086
            json anim = json::object();
            if (!node.animation.state.empty()) {
                anim["state"] = node.animation.state;
            }
            if (node.animation.blend >= 0.0f) {
                anim["blend"] = node.animation.blend;
            }
            if (node.animation.speed != 1.0f) {
                anim["speed"] = node.animation.speed;
            }
            if (node.animation.updateHz != 0.0f) {
                anim["updateHz"] = node.animation.updateHz;
            }
            if (node.animation.nearDistance != 15.0f) {
                anim["nearDistance"] = node.animation.nearDistance;
            }
            if (node.animation.farHz != 20.0f) {
                anim["farHz"] = node.animation.farHz;
            }
            if (node.animation.cullDistance != 120.0f) {
                anim["cullDistance"] = node.animation.cullDistance;
            }
            if (!node.animation.layers.empty()) { // ADR-300
                json layers = json::array();
                for (const PoseLayer& layer : node.animation.layers) {
                    json l = json::object();
                    l["name"] = layer.name;
                    l["kind"] = poseLayerKindName(layer.kind);
                    if (layer.drive != PoseLayerDrive::Manual) {
                        l["drive"] = poseLayerDriveName(layer.drive);
                    }
                    // **`weight` is authored state for a Manual layer and per-frame state for every
                    // other drive, and only the first kind may be saved.** The parser already draws
                    // exactly this line -- `layer.weight = drive == Manual ? *weight : 0.0f` -- and
                    // this side did not, so a manual layer's authored weight was read and never
                    // written back. Loading and saving `glowmere-valley-2-multicam` turned fifteen
                    // layers off: both stride warpers and the secondary-motion layer on all five
                    // aliens, silently, in a file that still loaded cleanly (ADR-618).
                    //
                    // Written **here, before the Foot/Reach early-out below**, deliberately. There
                    // are two `push_back` sites in this loop and a key added to one of them is a
                    // key the other drops -- which is the same defect one level down, and the
                    // reason this comment is at the top of the function rather than beside a
                    // `l["weight"]` in each branch.
                    if (layer.drive == PoseLayerDrive::Manual && layer.weight != 0.0f) {
                        l["weight"] = layer.weight;
                    }
                    // ADR-359: a foot layer is a different set of keys, not the same set with some
                    // of them empty. Written through the shared path it came out with `"joints":
                    // []` and `"clip": ""`, and the file it produced would not load -- a save that
                    // breaks the scene it saved is worse than one that refuses.
                    if (layer.kind == PoseLayerKind::Lean) {
                        l["degreesPerAccel"] = layer.leanDegreesPerAccel;
                        l["degreesPerTurn"] = layer.leanDegreesPerTurn;
                        l["maxDegrees"] = layer.leanMaxDegrees;
                    }
                    if (layer.kind == PoseLayerKind::Secondary) {
                        l["degrees"] = layer.secondaryDegrees;
                        l["period"] = layer.secondaryPeriod;
                        l["phase"] = layer.secondaryPhase;
                        l["spread"] = layer.secondarySpread;
                        l["stillness"] = layer.secondaryStillness;
                        l["axis"] = json::array({layer.secondaryAxis.x, layer.secondaryAxis.y,
                                                 layer.secondaryAxis.z});
                    }
                    if (layer.kind == PoseLayerKind::Stride) {
                        l["joint"] = layer.strideJoint;
                        if (!layer.strideOrigin.empty()) {
                            l["origin"] = layer.strideOrigin;
                        }
                        l["strideMin"] = layer.strideMin;
                        l["strideMax"] = layer.strideMax;
                        l["strideLift"] = layer.strideLift;
                    }
                    if (layer.kind == PoseLayerKind::Foot ||
                        layer.kind == PoseLayerKind::Reach) {
                        l["chain"] = json::array({layer.chainRoot, layer.chainMid, layer.chainTip});
                        if (glm::dot(layer.poleDirection, layer.poleDirection) > 0.0f) {
                            l["poleDirection"] = vecToJson(layer.poleDirection);
                        }
                        if (layer.footAlign != 1.0f) {
                            l["footAlign"] = layer.footAlign;
                        }
                        if (layer.groundOffset != 0.0f) {
                            l["groundOffset"] = layer.groundOffset;
                        }
                        if (layer.extension != 1.0f) {
                            l["extension"] = layer.extension;
                        }
                        if (layer.footLock != 0.0f) {
                            l["footLock"] = layer.footLock;
                        }
                        if (glm::dot(layer.soleUp, layer.soleUp) > 0.0f) {
                            l["soleUp"] = vecToJson(layer.soleUp);
                        }
                        layers.push_back(std::move(l));
                        continue;
                    }
                    l["joints"] = layer.mask.joints;
                    if (!layer.mask.weights.empty()) {
                        l["weights"] = layer.mask.weights;
                    }
                    if (layer.mask.descendants) {
                        l["descendants"] = true;
                    }
                    if (!layer.pivot.empty()) {
                        l["pivot"] = layer.pivot;
                    }
                    if (layer.forward != glm::vec3(0.0f, 0.0f, 1.0f)) {
                        l["forward"] = vecToJson(layer.forward);
                    }
                    if (layer.kind == PoseLayerKind::Aim) {
                        l["maxYaw"] = layer.maxYawDegrees;
                        l["maxPitch"] = layer.maxPitchDegrees;
                    } else {
                        l["clip"] = layer.clip;
                        if (layer.clipRate != 1.0f) {
                            l["clipRate"] = layer.clipRate;
                        }
                    }
                    layers.push_back(std::move(l));
                }
                anim["layers"] = std::move(layers);
            }
            if (!node.animation.contacts.empty()) { // ADR-546
                anim["contacts"] = node.animation.contacts;
            }
            if (node.animation.matchPhase) { // ADR-547
                anim["matchPhase"] = true;
            }
            if (node.animation.inertialize != 0.0f) {
                anim["inertialize"] = node.animation.inertialize;
            }
            if (node.animation.bodyCompensation.enabled) { // ADR-544
                const BodyCompensationSpec& bc = node.animation.bodyCompensation;
                const BodyCompensationLimits defaults;
                json bcJson;
                bcJson["enabled"] = true;
                if (!bc.joint.empty()) {
                    bcJson["joint"] = bc.joint;
                }
                if (bc.limits.maxDown != defaults.maxDown) {
                    bcJson["maxDown"] = bc.limits.maxDown;
                }
                if (bc.limits.maxUp != defaults.maxUp) {
                    bcJson["maxUp"] = bc.limits.maxUp;
                }
                if (bc.limits.maxLateral != defaults.maxLateral) {
                    bcJson["maxLateral"] = bc.limits.maxLateral;
                }
                if (bc.limits.compliance != defaults.compliance) {
                    bcJson["compliance"] = vecToJson(bc.limits.compliance);
                }
                if (bc.limits.iterations != defaults.iterations) {
                    bcJson["iterations"] = bc.limits.iterations;
                }
                anim["bodyCompensation"] = std::move(bcJson);
            }
            if (!node.animation.rootMotion.empty()) { // ADR-337
                json rm = json::array();
                for (const RootMotionSpec& spec : node.animation.rootMotion) {
                    // The short form round-trips as the short form. A save that rewrote every
                    // `"Landing"` as `{"clip": "Landing", "axes": "xyz"}` would make a scene file
                    // a diff nobody wrote every time it was opened (ADR-271).
                    const bool plain = spec.joint.empty() && spec.axes.x && spec.axes.y && spec.axes.z;
                    if (plain) {
                        rm.push_back(spec.clip);
                        continue;
                    }
                    json e = json::object();
                    e["clip"] = spec.clip;
                    if (!spec.joint.empty()) {
                        e["joint"] = spec.joint;
                    }
                    e["axes"] = rootMotionAxesName(spec.axes);
                    rm.push_back(std::move(e));
                }
                anim["rootMotion"] = std::move(rm);
            }
            n["animation"] = anim;
        }
        if (node.kind == NodeKind::Particles) {
            n["particles"] = particlesToJson(node.particles);
        }
        if (node.kind == NodeKind::Procedural) {
            n["procedural"] = node.procedural.toJson();
            if (node.floats.has_value()) {
                n["float"] = node.floats->toJson();
            }
        }
        if (node.kind == NodeKind::City) {
            // The description, never the placements (ADR-100). A city is re-planned and re-placed
            // from these numbers on every rebuild, exactly as a terrain's scatter is.
            const world::CitySettings& cs = node.city;
            n["city"] = json{{"moduleSize", cs.moduleSize},
                             {"tileUnits", cs.tileUnits},
                             {"blocksX", cs.blocksX},
                             {"blocksZ", cs.blocksZ},
                             {"blockCells", cs.blockCells},
                             {"roadCells", cs.roadCells},
                             {"seed", cs.seed},
                             {"plazaFraction", cs.plazaFraction},
                             {"crossingFraction", cs.crossingFraction},
                             {"plotFill", cs.plotFill},
                             {"propsPerCell", cs.propsPerCell},
                             {"propSpread", cs.propSpread},
                             {"streetPropChance", cs.streetPropChance},
                             {"cornerMix", cs.cornerMix},
                             {"overlayChance", cs.overlayChance},
                             {"overlayRun", cs.overlayRun},
                             {"buildDepth", cs.buildDepth},
                             {"footwayMetres", cs.footwayMetres}};
            if (!node.cityLibrary.empty()) {
                n["cityLibrary"] = node.cityLibrary.generic_string();
            }
        }
        if (node.kind == NodeKind::Field) {
            n["field"] = node.field.toJson();
        }
        if (node.kind == NodeKind::Spline) {
            n["spline"] = node.spline.toJson();
        }
        if (node.kind == NodeKind::Sdf) {
            n["sdf"] = node.sdf.toJson();
        }
        if (node.kind == NodeKind::Terrain) {
            n["world"] = world::worldMapToJson(node.worldMap);
            if (!node.ecology.empty()) {
                n["scatter"] = world::ecologyToJson(node.ecology);
            }
            if (!node.ecology.clearances.empty()) {
                n["clearings"] = world::clearancesToJson(node.ecology.clearances);
            }
            const world::TerrainSettings& ts = node.terrain;
            n["terrain"] = json{{"chunkSize", ts.chunkSize},   {"resolution", ts.resolution},
                                {"lodLevels", ts.lodLevels},   {"lodDistance", ts.lodDistance},
                                {"viewDistance", ts.viewDistance}, {"shadowDistance", ts.shadowDistance},
                                {"skirtDepth", ts.skirtDepth},
                                {"flow", world::waterFlowToJson(node.waterFlow)},
                                {"water",
                                 json{{"enabled", ts.water.enabled},
                                      {"shallow", ts.water.shallow},
                                      {"roughness", ts.water.roughness},
                                      {"shallowColor", vecToJson(ts.water.shallowColor)},
                                      {"deepColor", vecToJson(ts.water.deepColor)},
                                      {"emissiveColor", vecToJson(ts.water.emissiveColor)},
                                      {"emissiveIntensity", ts.water.emissiveIntensity},
                                      {"clarity", ts.water.clarity},
                                      {"maxOpacity", ts.water.maxOpacity},
                                      {"edgeFade", ts.water.edgeFade},
                                      {"fresnel", ts.water.fresnel},
                                      {"reflection", ts.water.reflection},
                                      {"reflectionTint", vecToJson(ts.water.reflectionTint)},
                                      {"specular", ts.water.specular},
                                      {"ripple", ts.water.ripple},
                                      {"rippleScale", ts.water.rippleScale},
                                      {"rippleSpeed", ts.water.rippleSpeed},
                                      {"chop", ts.water.chop},
                                      {"foam", ts.water.foam},
                                      {"foamWidth", ts.water.foamWidth},
                                      {"foamColor", vecToJson(ts.water.foamColor)},
                                      {"refraction", ts.water.refraction},
                                      {"glow", ts.water.glow},
                                      {"glowColor", vecToJson(ts.water.glowColor)},
                                      {"glowScale", ts.water.glowScale},
                                      {"glowCoverage", ts.water.glowCoverage},
                                      {"glowDepth", ts.water.glowDepth},
                                      {"sparkle", ts.water.sparkle},
                                      {"sparkleColor", vecToJson(ts.water.sparkleColor)},
                                      {"swell", ts.water.swell}}}};
            const Material& m = node.terrainMaterial;
            json mat{{"baseColor", vecToJson(m.baseColor)},
                     {"opacity", m.opacity},
                     {"emissiveColor", vecToJson(m.emissiveColor)},
                     {"emissiveIntensity", m.emissiveIntensity},
                     {"roughness", m.roughness},
                     {"metallic", m.metallic}};
            if (!m.program.empty()) {
                mat["program"] = m.program;
            }
            n["material"] = std::move(mat);
        }
        nodes.push_back(std::move(n));
    }
    j["nodes"] = std::move(nodes);
    {
        const json composition = compositionData_.toJson();
        if (!composition.empty()) {
            j["composition"] = composition;
        }
    }
    // ADR-074: a top-level block, a sibling of `composition` rather than a member of it, because a
    // hero is a thing in the world and a focal point is an instruction to the camera. Written only
    // when there are heroes, so every scene that never declared one keeps the file it had.
    if (!heroes_.empty()) {
        json heroes = json::array();
        for (const world::HeroPoint& hero : authoredHeroes()) {
            heroes.push_back(hero.toJson());
        }
        j["heroes"] = std::move(heroes);
    }
    // ADR-702. Written only when there are effects, so every scene that never declared one writes
    // back exactly the file it had. Stored grouped by owner in stack order, so the file lists every
    // stack top to bottom -- the order a person sees in the panel.
    if (!effects_.empty()) {
        json effects = json::array();
        for (const world::EffectInstance& effect : effects_) {
            effects.push_back(effect.toJson());
        }
        j["effects"] = std::move(effects);
    }
    if (!profileLibraryPath_.empty()) {
        j["entityProfiles"] = profileLibraryPath_;
    }
    if (!entityDescs_.empty()) {
        j["entities"] = entity::entitiesToJson(entityDescs_);
    }
    // Phase D §26. Written when authored and never otherwise (the diff-noise rule, ADR-225).
    if (!eventProfiles_.empty()) {
        json events = json::array();
        for (const entity::EntityWorld::EventProfile& e : eventProfiles_) {
            json ej{{"name", e.name}, {"radius", e.radius}, {"magnitude", e.magnitude}};
            if (!e.signal.empty()) {
                ej["signal"] = e.signal;
                ej["threshold"] = e.threshold;
                ej["minInterval"] = e.minInterval;
            }
            if (!e.at.empty()) {
                ej["at"] = e.at;
            }
            events.push_back(std::move(ej));
        }
        j["worldEvents"] = std::move(events);
    }
    if (!fieldDescs_.empty()) {
        j["fields"] = entity::fieldsToJson(fieldDescs_);
    }
    // ADR-209. Written only when there is one, so every scene that never declared a director keeps
    // the file it had, byte for byte.
    if (!stagingDesc_.empty() || !stagingDesc_.actors.empty()) {
        j["staging"] = stage::stagingToJson(stagingDesc_);
    }
    if (graph_) {
        j["graph"] = graph_->toJson();
    }
    if (!grids_.empty()) {
        json gridsJson = json::array();
        for (const spatial::GridField& g : grids_) {
            gridsJson.push_back(g.toJson()); // settings only, never the cell values
        }
        j["grids"] = std::move(gridsJson);
    }
    if (!materialPrograms_.empty()) {
        json programs = json::array();
        for (const MaterialProgram& mp : materialPrograms_) {
            if (std::find(graphMaterials_.begin(), graphMaterials_.end(), mp.name) != graphMaterials_.end()) {
                continue;
            }
            programs.push_back(mp.toJson());
        }
        j["materialPrograms"] = std::move(programs);
    }
    return j;
}

Result<std::unique_ptr<Composition>> Composition::fromJson(const nlohmann::json& j,
                                                           assets::AssetRegistry& registry, int depth) {
    return fromJsonImpl(j, registry, depth, {}, {});
}

Result<std::unique_ptr<Composition>> Composition::fromJsonImpl(const nlohmann::json& j,
                                                               assets::AssetRegistry& registry, int depth,
                                                               std::vector<std::filesystem::path> ancestors,
                                                               std::filesystem::path sourcePath) {
    if (!j.is_object()) {
        return fail("scene file: root is not an object");
    }
    auto format = readString(j, "format", "");
    if (!format) {
        return std::unexpected(format.error());
    }
    if (*format != kFormatName) {
        return fail("not an avgen scene file (format '{}')", *format);
    }
    if (!j.contains("version") || !j.at("version").is_number_integer()) {
        return fail("scene file: missing 'version'");
    }
    if (const int version = j.at("version").get<int>(); version != kFormatVersion) {
        return fail("unsupported scene file version {} (expected {})", version, kFormatVersion);
    }
    if (depth > kMaxNestingDepth) {
        return fail("scene nests deeper than {} levels", kMaxNestingDepth);
    }
    auto name =
        readString(j, "name", sourcePath.empty() ? std::string("composition") : sourcePath.stem().string());
    if (!name) {
        return std::unexpected(name.error());
    }

    auto comp = std::make_unique<Composition>(registry, *name);
    comp->depth_ = depth;
    comp->ancestors_ = std::move(ancestors);
    comp->sourcePath_ = std::move(sourcePath);
    // `sourcePath` is moved-from above: everything below uses the composition's copy.
    const std::filesystem::path& scenePath = comp->sourcePath_;

    // ADR-278. Before this, a top-level key the parser did not read was walked past in silence --
    // which is how `"lights"` sat in two lab fixtures for weeks while both scenes were lit by
    // `defaultKeyLight()` at a different angle, colour and intensity than their own files stated.
    // A warning rather than a refusal; `core/json_keys.hpp` gives the reasons and the `_` exemption.
    const std::string where =
        scenePath.empty() ? ("scene '" + comp->name_ + "'") : ("scene file '" + scenePath.string() + "'");
    json_keys::warnUnknownKeys(j, kSceneKeys, where);
    if (j.contains("environment") && j.at("environment").is_object()) {
        json_keys::warnUnknownKeys(j.at("environment"), kEnvironmentKeys, where + ": environment");
        const json& envJson = j.at("environment");
        if (envJson.contains("sky") && envJson.at("sky").is_object()) {
            json_keys::warnUnknownKeys(envJson.at("sky"), kSkyKeys, where + ": environment.sky");
        }
    }

    if (j.contains("camera")) {
        const json& c = j.at("camera");
        if (!c.is_object()) {
            return fail("'camera' must be an object");
        }
        if (c.contains("distance")) {
            auto v = readFloat(c, "distance", 0.0f);
            if (!v) {
                return std::unexpected(v.error());
            }
            if (*v > 0.0f) { // 0 (or negative) means "fit to the bounds"
                comp->cameraDistanceSetting_ = *v;
            }
        }
        if (c.contains("height")) {
            auto v = readFloat(c, "height", 0.0f);
            if (!v) {
                return std::unexpected(v.error());
            }
            comp->cameraHeightSetting_ = *v;
        }
        auto orbit = readFloat(c, "orbitSpeed", comp->cameraOrbitSpeedSetting_);
        if (!orbit) {
            return std::unexpected(orbit.error());
        }
        comp->cameraOrbitSpeedSetting_ = *orbit;
        if (c.contains("mode") && c["mode"].is_number_integer()) {
            comp->cameraModeSetting_ = std::clamp(c["mode"].get<int>(), 0, 2);
        }
        if (c.contains("spline") && c["spline"].is_string()) {
            comp->cameraSplineSetting_ = c["spline"].get<std::string>();
        }
        auto readVec = [&](const char* key, glm::vec3& out) {
            if (c.contains(key) && c[key].is_array() && c[key].size() == 3 && c[key][0].is_number()) {
                out = glm::vec3(c[key][0].get<float>(), c[key][1].get<float>(), c[key][2].get<float>());
            }
        };
        readVec("position", comp->cameraPositionSetting_);
        readVec("target", comp->cameraTargetSetting_);
        auto fov = readFloat(c, "fov", comp->cameraFovSetting_);
        if (!fov) {
            return std::unexpected(fov.error());
        }
        comp->cameraFovSetting_ = *fov;
    }
    if (j.contains("cameraDirection")) {
        auto direction = CameraDirection::fromJson(j.at("cameraDirection"));
        if (!direction) {
            return std::unexpected(direction.error());
        }
        comp->cameraDirection_ = std::move(*direction);
    }
    comp->cameraDirection_.ensureMainCamera();
    // A rig is a scene-level idea, so it is accepted at the top level as well as inside
    // `environment`; the environment block wins when both name one.
    if (j.contains("lightRig")) {
        if (j.at("lightRig").is_object()) {
            auto inline_ = LightRig::fromJson(j.at("lightRig"));
            if (!inline_) {
                log::warn("composition '{}': light rig: {}", comp->name_, inline_.error().message);
            } else if (auto r = comp->installLightRig(std::move(*inline_)); !r) {
                log::warn("composition '{}': light rig: {}", comp->name_, r.error().message);
            }
        } else {
            auto topRig = readString(j, "lightRig", "");
            if (!topRig) {
                return std::unexpected(topRig.error());
            }
            if (!topRig->empty()) {
                if (auto r = comp->setLightRig(*topRig); !r) {
                    log::warn("composition '{}': light rig: {}", comp->name_, r.error().message);
                }
            }
        }
    }
    // ADR-278: the lights the scene file authors. A malformed one fails the whole file rather than
    // being dropped, for the reason a malformed hero does and with more force: a light that quietly
    // did not load looks exactly like a light nobody wrote, which is the state this key was in.
    if (j.contains("lights")) {
        const json& lightsJson = j.at("lights");
        if (!lightsJson.is_array()) {
            return fail("scene file '{}': 'lights' must be an array", scenePath.string());
        }
        std::vector<AuthoredLight> lights;
        lights.reserve(lightsJson.size());
        for (std::size_t i = 0; i < lightsJson.size(); ++i) {
            auto light = authoredLightFromJson(lightsJson[i], where);
            if (!light) {
                return fail("scene file '{}': lights[{}]: {}", scenePath.string(), i,
                            light.error().message);
            }
            lights.push_back(std::move(*light));
        }
        if (auto ok = comp->setAuthoredLights(std::move(lights)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // ADR-055: the wind is a top-level block, a sibling of `environment` rather than a member of
    // it, because it is the weather rather than the sky: it moves geometry, and a later tier will
    // move cloth and particles with the same numbers.
    // ADR-193: how coarse the navigation graph is, in metres. 0 disables pathfinding and leaves the
    // straight-line steering, which is correct and cannot route. Validated here rather than clamped
    // silently: a scene asking for a 0.1 m grid over a kilometre of world is asking for a hundred
    // million cells, and finding that out as a warning beats finding it as a stall.
    // ADR-199: the widest body the graph has to serve. 0 keeps the navigator's own person-sized
    // default, which is what every scene written before this gets.
    if (j.contains("navBodyRadius")) {
        if (!j.at("navBodyRadius").is_number()) {
            return fail("'navBodyRadius' must be a number of metres");
        }
        const auto metres = j.at("navBodyRadius").get<float>();
        if (metres < 0.0f || metres > 40.0f) {
            return fail("'navBodyRadius' {} is out of range; use 0 for the default or 0..40 metres",
                        metres);
        }
        comp->setNavBodyRadius(metres);
    }
    if (j.contains("navCellSize")) {
        if (!j.at("navCellSize").is_number()) {
            return fail("'navCellSize' must be a number of metres (0 disables pathfinding)");
        }
        const auto metres = j.at("navCellSize").get<float>();
        if (metres < 0.0f || metres > 64.0f) {
            return fail("'navCellSize' {} is out of range; use 0 to disable or 0.5..64 metres", metres);
        }
        comp->setNavCellSize(metres);
    }
    // How deep a walker wades. 0 keeps water binary, which is what it has always been. Validated
    // rather than clamped for the same reason as the cell size: a scene asking for a four-metre
    // wade band is asking for its characters to walk along the bed of the river, and a number
    // silently pulled back to something sensible is a scene that does not do what it says.
    if (j.contains("navWadeDepth")) {
        if (!j.at("navWadeDepth").is_number()) {
            return fail("'navWadeDepth' must be a number of metres (0 stops walkers at the water)");
        }
        const auto metres = j.at("navWadeDepth").get<float>();
        if (metres < 0.0f || metres > 8.0f) {
            return fail("'navWadeDepth' {} is out of range; use 0 to stop at the water or 0..8 metres",
                        metres);
        }
        comp->setNavWadeDepth(metres);
    }
    if (j.contains("wind")) {
        if (!j.at("wind").is_object()) {
            return fail("'wind' must be an object");
        }
        comp->windSetting_ = wind::windFromJson(j.at("wind"));
    }
    // ADR-059: kept verbatim; the Engine turns it into parameter base values when the scene loads.
    if (j.contains("post")) {
        comp->postJson_ = j.at("post");
    }
    // ADR-387 §19: a scene saved before the vortex became an authored effect carries it under
    // `environment.vortex`. It is read into this and converted into an effect instance after the
    // `effects` array is read, because the conversion has to know what ids are taken.
    std::optional<world::Vortex> legacyVortex;
    if (j.contains("environment")) {
        const json& e = j.at("environment");
        if (!e.is_object()) {
            return fail("'environment' must be an object");
        }
        auto map = readString(e, "map", "");
        if (!map) {
            return std::unexpected(map.error());
        }
        comp->environmentPath_ = *map;
        // ADR-343: the day/night cycle. Absent or `enabled: false` leaves the environment exactly
        // as it was, so every existing scene is untouched by this.
        if (e.contains("dayNight") && e.at("dayNight").is_object()) {
            const json& d = e.at("dayNight");
            DayNightSettings& dn = comp->dayNight_;
            const auto rf = [&d](const char* key, float& out) {
                if (d.contains(key) && d.at(key).is_number()) {
                    out = d.at(key).get<float>();
                }
            };
            const auto rb = [&d](const char* key, bool& out) {
                if (d.contains(key) && d.at(key).is_boolean()) {
                    out = d.at(key).get<bool>();
                }
            };
            const auto rs = [&d](const char* key, std::string& out) {
                if (d.contains(key) && d.at(key).is_string()) {
                    out = d.at(key).get<std::string>();
                }
            };
            const auto rl = [&d](const char* key, std::vector<std::string>& out) {
                if (d.contains(key) && d.at(key).is_array()) {
                    out.clear();
                    for (const json& item : d.at(key)) {
                        if (item.is_string()) {
                            out.push_back(item.get<std::string>());
                        }
                    }
                }
            };
            rb("enabled", dn.enabled);
            rb("paused", dn.paused);
            rf("cycleSeconds", dn.cycleSeconds);
            rf("phaseOffset", dn.phaseOffset);
            rf("dayPhase", dn.manualPhase);
            rf("sunPeakElevation", dn.sunPeakElevation);
            rf("sunAzimuthAtDawn", dn.sunAzimuthAtDawn);
            rf("sunAzimuthSweep", dn.sunAzimuthSweep);
            rf("sunIntensityScale", dn.sunIntensityScale);
            rf("moonIntensityScale", dn.moonIntensityScale);
            rf("starBrightnessScale", dn.starBrightnessScale);
            rf("hdriIntensityScale", dn.hdriIntensityScale);
            rf("glowInfluence", dn.glowInfluence);
            rs("sunLight", dn.sunLight);
            rs("moonLight", dn.moonLight);
            rs("dayMap", dn.dayMap);
            rs("nightMap", dn.nightMap);
            rl("starNodes", dn.starNodes);
            rl("glowNodes", dn.glowNodes);
            // ADR-350: read the curves back. The writer emits them, so without this a save and a
            // reload would silently restore the defaults and an author's edited colour stop would
            // vanish -- the same defect as the missing writer, one layer along.
            const auto readFloatCurve = [&d](const char* key, std::vector<PhaseKey<float>>& out) {
                if (!d.contains(key) || !d.at(key).is_array()) {
                    return;
                }
                std::vector<PhaseKey<float>> keys;
                for (const json& k : d.at(key)) {
                    if (k.is_object() && k.contains("phase") && k.contains("value") &&
                        k.at("phase").is_number() && k.at("value").is_number()) {
                        keys.push_back({k.at("phase").get<float>(), k.at("value").get<float>()});
                    }
                }
                if (!keys.empty()) {
                    out = std::move(keys);
                }
            };
            const auto readVec3Curve = [&d](const char* key, std::vector<PhaseKey<glm::vec3>>& out) {
                if (!d.contains(key) || !d.at(key).is_array()) {
                    return;
                }
                std::vector<PhaseKey<glm::vec3>> keys;
                for (const json& k : d.at(key)) {
                    if (k.is_object() && k.contains("phase") && k.at("phase").is_number() &&
                        k.contains("value") && k.at("value").is_array() && k.at("value").size() == 3) {
                        const json& v = k.at("value");
                        keys.push_back({k.at("phase").get<float>(),
                                        glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>())});
                    }
                }
                if (!keys.empty()) {
                    out = std::move(keys);
                }
            };
            readVec3Curve("zenithColor", dn.zenithColor);
            readVec3Curve("horizonColor", dn.horizonColor);
            readVec3Curve("groundColor", dn.groundColor);
            readVec3Curve("sunColor", dn.sunColor);
            readVec3Curve("fogColor", dn.fogColor);
            readVec3Curve("waterDeepColor", dn.waterDeepColor);
            readFloatCurve("sunIntensity", dn.sunIntensity);
            readFloatCurve("moonIntensity", dn.moonIntensity);
            readFloatCurve("skyIntensity", dn.skyIntensity);
            readFloatCurve("haze", dn.haze);
            readFloatCurve("starBrightness", dn.starBrightness);
            readFloatCurve("hdriIntensity", dn.hdriIntensity);
            readFloatCurve("hdriBlend", dn.hdriBlend);
            readFloatCurve("glowScale", dn.glowScale);
            readFloatCurve("fogDensity", dn.fogDensity);
            readFloatCurve("waterReflection", dn.waterReflection);
            // Curves the scene did not override get the Tree of Life defaults, so `enabled: true`
            // on its own is a complete cycle rather than a black world. `fill` only writes into an
            // empty list, so anything read above survives this.
            dn.applyDefaults();
        }
        // Either a path to a rig file or the rig itself. Generated worlds write the latter, because
        // their rig comes from an art-direction profile rather than from a file somebody authored.
        if (e.contains("lightRig") && e.at("lightRig").is_object()) {
            auto inline_ = LightRig::fromJson(e.at("lightRig"));
            if (!inline_) {
                log::warn("composition '{}': light rig: {}", comp->name_, inline_.error().message);
            } else if (auto r = comp->installLightRig(std::move(*inline_)); !r) {
                log::warn("composition '{}': light rig: {}", comp->name_, r.error().message);
            }
        } else {
            auto lightRig = readString(e, "lightRig", "");
            if (!lightRig) {
                return std::unexpected(lightRig.error());
            }
            if (!lightRig->empty()) {
                // A missing or malformed rig is a warning: the world still loads, lit by its
                // default key.
                if (auto r = comp->setLightRig(*lightRig); !r) {
                    log::warn("composition '{}': light rig: {}", comp->name_, r.error().message);
                }
            }
        }
        auto intensity = readFloat(e, "intensity", comp->envIntensitySetting_);
        if (!intensity) {
            return std::unexpected(intensity.error());
        }
        comp->envIntensitySetting_ = *intensity;
        auto fog = readFloat(e, "fogDensity", comp->fogDensitySetting_);
        if (!fog) {
            return std::unexpected(fog.error());
        }
        comp->fogDensitySetting_ = *fog;
        if (e.contains("stylized")) {
            if (!e["stylized"].is_boolean()) {
                return fail("'stylized' must be a boolean");
            }
            comp->stylizedSetting_ = e["stylized"].get<bool>();
        }
        // ADR-049. `rotation` had a parameter but no scene-file key, so until now an HDRI could
        // only be aimed by hand at runtime; `skyIntensity` and `skyBloom` belong to the visible
        // sky alone, and `intensity` above stays the lighting control.
        for (const std::pair<const char*, float*> key :
             {std::pair<const char*, float*>{"rotation", &comp->envRotationSetting_},
              std::pair<const char*, float*>{"skyIntensity", &comp->skyIntensitySetting_},
              std::pair<const char*, float*>{"skyBloom", &comp->skyBloomSetting_},
              // ADR-053. Off unless a scene asks for it: turning a glowing ecology into hundreds
              // of lights changes what every other scene costs, so it is opt-in.
              std::pair<const char*, float*>{"ecologyLight", &comp->ecologyLightGain_},
              std::pair<const char*, float*>{"ecologyLightRange", &comp->ecologyLightRange_},
              std::pair<const char*, float*>{"ecologyGlowCell", &comp->ecologyGlowCell_}}) {
            auto value = readFloat(e, key.first, *key.second);
            if (!value) {
                return std::unexpected(value.error());
            }
            *key.second = *value;
        }
        // `showSkybox` is the other Environment field that had no scene-file key: a scene could
        // light itself from a map but not choose whether to stand it behind the world.
        if (e.contains("skybox")) {
            if (!e["skybox"].is_boolean()) {
                return fail("'skybox' must be a boolean");
            }
            comp->showSkyboxSetting_ = e["skybox"].get<bool>();
        }
        // ADR-345: light from the map, stand under the procedural sky. Off unless asked for.
        if (e.contains("proceduralSkyBackground")) {
            if (!e["proceduralSkyBackground"].is_boolean()) {
                return fail("'proceduralSkyBackground' must be a boolean");
            }
            comp->proceduralSkyBackgroundSetting_ = e["proceduralSkyBackground"].get<bool>();
        }
        if (e.contains("lightFromEnvironment")) {
            if (!e["lightFromEnvironment"].is_boolean()) {
                return fail("'lightFromEnvironment' must be a boolean");
            }
            comp->lightFromEnvironmentSetting_ = e["lightFromEnvironment"].get<bool>();
        }
        auto readColour = [&](const char* key, glm::vec3& out) -> bool {
            if (e.contains(key) && e[key].is_array() && e[key].size() == 3 && e[key][0].is_number()) {
                out = glm::vec3(e[key][0].get<float>(), e[key][1].get<float>(), e[key][2].get<float>());
                return true;
            }
            return false;
        };
        comp->fogColorSet_ = readColour("fogColor", comp->fogColorSetting_);
        // ADR-058: the surface fog's share of the mist layer, and the styled hemisphere. All four
        // default to what the shader used to hard-code, so an existing scene is unchanged.
        {
            auto value = readFloat(e, "fogHeightAmount", comp->volumeSetting_.fogHeightAmount);
            if (!value) {
                return std::unexpected(value.error());
            }
            comp->volumeSetting_.fogHeightAmount = *value;
        }
        readColour("styledSkyAmbient", comp->volumeSetting_.styledSkyAmbient);
        readColour("styledGroundAmbient", comp->volumeSetting_.styledGroundAmbient);
        {
            auto value = readFloat(e, "styledAmbientFloor", comp->volumeSetting_.styledAmbientFloor);
            if (!value) {
                return std::unexpected(value.error());
            }
            comp->volumeSetting_.styledAmbientFloor = *value;
        }
        {
            scene::Environment& v = comp->volumeSetting_;
            struct FloatKey {
                const char* key;
                float* target;
            };
            for (const FloatKey fk : {FloatKey{"volumeDensity", &v.volumeDensity},
                                      FloatKey{"fogHeight", &v.fogHeight},
                                      FloatKey{"fogHeightFalloff", &v.fogHeightFalloff},
                                      FloatKey{"fogUpperDensity", &v.fogUpperDensity},
                                      FloatKey{"fogHeightCurve", &v.fogHeightCurve},
                                      FloatKey{"volumeShadowStrength", &v.volumeShadowStrength},
                                      FloatKey{"volumeScattering", &v.volumeScattering},
                                      FloatKey{"volumeAbsorption", &v.volumeAbsorption},
                                      FloatKey{"volumeAnisotropy", &v.volumeAnisotropy},
                                      FloatKey{"volumeLocalLights", &v.volumeLocalLights},
                                      FloatKey{"volumeNoise", &v.volumeNoiseAmount},
                                      FloatKey{"volumeNoiseScale", &v.volumeNoiseScale},
                                      FloatKey{"volumeNoiseSpeed", &v.volumeNoiseSpeed},
                                      FloatKey{"volumeEmission", &v.volumeEmission},
                                      FloatKey{"volumeMaxDistance", &v.volumeMaxDistance},
                                      FloatKey{"shadowRange", &v.shadowRange}}) {
                auto value = readFloat(e, fk.key, *fk.target);
                if (!value) {
                    return std::unexpected(value.error());
                }
                *fk.target = *value;
            }
            // ADR-387, consolidation §19: the legacy `environment.vortex` block. It was ADR-371's
            // singleton; the vortex is an authored atmospheric effect now. Reading it here and
            // converting it below preserves every value of every scene saved in the old form
            // rather than silently discarding the configuration -- the file keeps loading, and the
            // next save writes it in the new place.
            if (e.contains("vortex") && e.at("vortex").is_object()) {
                const nlohmann::json& vj = e.at("vortex");
                world::Vortex& vx = legacyVortex.emplace();
                auto f = [&vj](const char* key, float& out) {
                    if (vj.contains(key) && vj.at(key).is_number()) {
                        out = vj.at(key).get<float>();
                    }
                };
                auto c3 = [&vj](const char* key, glm::vec3& out) {
                    if (vj.contains(key) && vj.at(key).is_array() && vj.at(key).size() == 3) {
                        out = glm::vec3(vj.at(key)[0].get<float>(), vj.at(key)[1].get<float>(),
                                        vj.at(key)[2].get<float>());
                    }
                };
                c3("center", vx.field.center);
                f("radius", vx.field.radius);
                f("thickness", vx.field.thickness);
                f("swirl", vx.field.swirl);
                f("rotationSpeed", vx.field.rotationSpeed);
                f("density", vx.density);
                f("innerVoid", vx.field.innerVoid);
                f("contrast", vx.field.contrast);
                f("turbulence", vx.field.turbulence);
                f("turbulenceScale", vx.field.turbulenceScale);
                f("breathAmount", vx.field.breathAmount);
                f("breathSpeed", vx.field.breathSpeed);
                f("emission", vx.emission);
                f("filaments", vx.filaments);
                f("spill", vx.spill);
                f("cometResponse", vx.cometResponse);
                f("cometReach", vx.cometReach);
                f("funnelDepth", vx.field.funnelDepth);
                f("throat", vx.field.throat);
                f("throatDensity", vx.field.throatDensity);
                c3("colorDeep", vx.colorDeep);
                c3("colorMid", vx.colorMid);
                c3("colorAccent", vx.colorAccent);
            }
            if (e.contains("shadowCascades")) {
                if (!e["shadowCascades"].is_number_unsigned()) {
                    return fail("'shadowCascades' must be an unsigned integer (0 = the quality tier)");
                }
                v.shadowCascades = std::min(e["shadowCascades"].get<std::uint32_t>(), 4u);
            }
            if (e.contains("volumeSteps")) {
                if (!e["volumeSteps"].is_number_integer()) {
                    return fail("'volumeSteps' must be an integer");
                }
                v.volumeSteps = std::clamp(e["volumeSteps"].get<int>(), 4, 256);
            }
            // ADR-570: clamped to the same 0..16 the parameter is, so a hand-written scene cannot
            // ask for a cost the slider refuses to offer.
            if (e.contains("volumeShadowSteps")) {
                if (!e["volumeShadowSteps"].is_number_integer()) {
                    return fail("'volumeShadowSteps' must be an integer");
                }
                v.volumeShadowSteps = std::clamp(e["volumeShadowSteps"].get<int>(), 0, 16);
            }
            if (e.contains("volumeJitter")) {
                if (!e["volumeJitter"].is_number()) {
                    return fail("'volumeJitter' must be a number");
                }
                v.volumeJitter = std::clamp(e["volumeJitter"].get<float>(), 0.0f, 1.0f);
            }
            auto densityField = readString(e, "volumeDensityField", comp->volumeDensityFieldSetting_);
            if (!densityField) {
                return std::unexpected(densityField.error());
            }
            comp->volumeDensityFieldSetting_ = *densityField;
            auto colourField = readString(e, "volumeColorField", comp->volumeColorFieldSetting_);
            if (!colourField) {
                return std::unexpected(colourField.error());
            }
            comp->volumeColorFieldSetting_ = *colourField;
        }
        glm::vec3 background;
        if (readColour("background", background)) {
            comp->scene_.environment.backgroundColor = background;
        }
        // Procedural sky (ADR-036). Every member has a default, so an older scene file loads with
        // the sky on and simply gains reflections.
        if (e.contains("sky")) {
            const json& sj = e.at("sky");
            if (!sj.is_object()) {
                return fail("'sky' must be an object");
            }
            SkySettings& sky = comp->skySetting_;
            struct SkyFloat {
                const char* key;
                float* target;
            };
            for (const SkyFloat sf : {SkyFloat{"haze", &sky.hazeWidth},
                                      SkyFloat{"sunIntensity", &sky.sunIntensity},
                                      SkyFloat{"sunSize", &sky.sunAngularRadius},
                                      SkyFloat{"sunGlow", &sky.sunGlowWidth},
                                      SkyFloat{"intensity", &sky.intensity}}) {
                auto value = readFloat(sj, sf.key, *sf.target);
                if (!value) {
                    return std::unexpected(value.error());
                }
                *sf.target = *value;
            }
            struct SkyColour {
                const char* key;
                glm::vec3* target;
            };
            for (const SkyColour sc : {SkyColour{"zenithColor", &sky.zenithColor},
                                       SkyColour{"horizonColor", &sky.horizonColor},
                                       SkyColour{"groundColor", &sky.groundColor},
                                       SkyColour{"sunColor", &sky.sunColor},
                                       SkyColour{"sunDirection", &sky.sunDirection}}) {
                if (sj.contains(sc.key) && sj[sc.key].is_array() && sj[sc.key].size() == 3 &&
                    sj[sc.key][0].is_number()) {
                    *sc.target = glm::vec3(sj[sc.key][0].get<float>(), sj[sc.key][1].get<float>(),
                                           sj[sc.key][2].get<float>());
                }
            }
            struct SkyFlag {
                const char* key;
                bool* target;
            };
            for (const SkyFlag sb : {SkyFlag{"enabled", &sky.enabled}, SkyFlag{"background", &sky.showBackground},
                                     SkyFlag{"useKeyLight", &sky.useKeyLight}}) {
                if (sj.contains(sb.key)) {
                    if (!sj[sb.key].is_boolean()) {
                        return fail("sky '{}' must be a boolean", sb.key);
                    }
                    *sb.target = sj[sb.key].get<bool>();
                }
            }
        }
    }

    if (j.contains("composition")) {
        auto data = CompositionData::fromJson(j.at("composition"));
        if (!data) {
            return fail("scene file '{}': composition: {}", scenePath.string(), data.error().message);
        }
        comp->compositionData_ = std::move(*data);
    }
    // ADR-074. Read through HeroPoint::fromJson, which validates, so the failure modes the type
    // already knows about -- an activation radius inside the stand-off, an unknown reaction profile
    // -- are refused here with the hero's own name in the message. Refusing the file rather than
    // skipping the entry is the point: this whole block exists because an offline render reloads
    // the project before drawing it, and a hero that quietly failed to load would look exactly like
    // a hero nobody declared (ADR-067, ADR-070 -- twice bitten).
    if (j.contains("heroes")) {
        const json& heroesJson = j.at("heroes");
        if (!heroesJson.is_array()) {
            return fail("'heroes' must be an array");
        }
        std::vector<world::HeroPoint> heroes;
        heroes.reserve(heroesJson.size());
        for (std::size_t i = 0; i < heroesJson.size(); ++i) {
            auto hero = world::HeroPoint::fromJson(heroesJson[i]);
            if (!hero) {
                return fail("scene file '{}': heroes[{}]: {}", scenePath.string(), i,
                            hero.error().message);
            }
            heroes.push_back(std::move(*hero));
        }
        if (auto ok = comp->setHeroes(std::move(heroes)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // ADR-702: the effects, every owner's, in one array. Read after the heroes, because an effect can
    // be attached to one and a `hero` source names one. A malformed entry is an error rather than a
    // silent omission, because an effect that does not load is an effect that never fires with
    // nothing saying why -- and that includes an entry in the pre-ADR-702 format, which is named.
    for (const char* legacy : {"worldEffects", "atmosphericEffects"}) {
        if (j.contains(legacy)) {
            return fail("scene file '{}': '{}' is the pre-ADR-702 effect format; effects are one "
                        "'effects' array now (tools/migrate_effects.py converts a file)",
                        scenePath.string(), legacy);
        }
    }
    if (j.contains("effects")) {
        const json& effectsJson = j.at("effects");
        if (!effectsJson.is_array()) {
            return fail("scene file '{}': 'effects' must be an array", scenePath.string());
        }
        std::vector<world::EffectInstance> effects;
        effects.reserve(effectsJson.size());
        for (std::size_t i = 0; i < effectsJson.size(); ++i) {
            auto effect = world::EffectInstance::fromJson(effectsJson[i]);
            if (!effect) {
                return fail("scene file '{}': effects[{}]: {}", scenePath.string(), i, effect.error().message);
            }
            effects.push_back(std::move(*effect));
        }
        // Stored in stack order whatever order the file listed them in; `validateEffects` below then
        // checks the orders the file stated were a real stack.
        world::normaliseEffectOrder(effects);
        if (auto ok = comp->setEffects(std::move(effects)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // ADR-387 §19: the migration. Only a vortex that was actually on is carried over -- a zero
    // radius was ADR-371's "off", and turning it into a disabled effect instance would put a row
    // in the World Effects panel for something the scene never had. Values are copied, not
    // re-defaulted: §10 asks that the Tree of Life look identical, and the only way to be sure of
    // that is for the numbers to be the same numbers.
    if (legacyVortex && legacyVortex->active()) {
        std::vector<world::EffectInstance> effects = comp->effects_;
        const bool alreadyAuthored =
            std::any_of(effects.begin(), effects.end(), [](const world::EffectInstance& e) {
                return e.kind == world::EffectKind::Vortex;
            });
        if (!alreadyAuthored) {
            world::EffectInstance e;
            e.id = world::uniqueEffectId(effects, "vortex");
            e.name = "vortex";
            e.kind = world::EffectKind::Vortex;
            e.enabled = true;
            e.activation = world::Activation::Always;
            e.vortex = *legacyVortex;
            if (auto added = world::insertEffect(effects, std::move(e)); !added) {
                return fail("scene file '{}': migrating 'environment.vortex': {}", scenePath.string(),
                            added.error().message);
            }
            if (auto ok = comp->setEffects(std::move(effects)); !ok) {
                return fail("scene file '{}': migrating 'environment.vortex': {}", scenePath.string(),
                            ok.error().message);
            }
        }
    }
    // ADR-097: the profile library the scene names, read once before the entities that reference
    // it by name. A scene with none behaves exactly as it did -- a `"profile"` is then a path.
    entity::ProfileLibrary profileLibrary;
    if (j.contains("entityProfiles")) {
        if (!j.at("entityProfiles").is_string()) {
            return fail("scene file '{}': 'entityProfiles' must be a path to a profile library",
                        scenePath.string());
        }
        const std::string relative = j.at("entityProfiles").get<std::string>();
        std::filesystem::path path = relative;
        if (path.is_relative() && !scenePath.empty()) {
            path = scenePath.parent_path() / path;
        }
        auto loaded = entity::loadProfileLibrary(path);
        if (!loaded) {
            return fail("scene file '{}': {}", scenePath.string(), loaded.error().message);
        }
        profileLibrary = std::move(*loaded);
        comp->setEntityProfileLibraryPath(relative);
        log::info("scene '{}': entity profile library '{}': {} profile(s)", scenePath.string(),
                  relative, profileLibrary.size());
    }
    if (j.contains("entities")) {
        auto entities =
            entity::entitiesFromJson(j.at("entities"), scenePath.parent_path(), &profileLibrary);
        if (!entities) {
            return fail("scene file '{}': {}", scenePath.string(), entities.error().message);
        }
        if (auto ok = comp->setEntities(std::move(*entities)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // Phase D §26: `"worldEvents": [{"name": "bloom", "radius": 45, "magnitude": 0.8}]` -- how far
    // a named event carries and how loud it is. Before the entities are bound, so the first
    // `rebuild` hands them to the world.
    if (j.contains("worldEvents")) {
        const json& events = j.at("worldEvents");
        if (!events.is_array()) {
            return fail("scene file '{}': 'worldEvents' must be an array", scenePath.string());
        }
        std::vector<entity::EntityWorld::EventProfile> profiles;
        for (const json& e : events) {
            if (!e.is_object() || !e.contains("name") || !e.at("name").is_string()) {
                return fail("scene file '{}': every world event needs a string 'name'",
                            scenePath.string());
            }
            entity::EntityWorld::EventProfile profile;
            profile.name = e.at("name").get<std::string>();
            if (e.contains("radius") && e.at("radius").is_number()) {
                profile.radius = std::max(0.0f, e.at("radius").get<float>());
            }
            if (e.contains("magnitude") && e.at("magnitude").is_number()) {
                profile.magnitude = std::clamp(e.at("magnitude").get<float>(), 0.0f, 1.0f);
            }
            if (e.contains("signal") && e.at("signal").is_string()) {
                profile.signal = e.at("signal").get<std::string>();
            }
            if (e.contains("threshold") && e.at("threshold").is_number()) {
                profile.threshold = e.at("threshold").get<float>();
            }
            if (e.contains("at") && e.at("at").is_string()) {
                profile.at = e.at("at").get<std::string>();
            }
            if (e.contains("minInterval") && e.at("minInterval").is_number()) {
                profile.minInterval = std::max(0.0f, e.at("minInterval").get<float>());
            }
            profiles.push_back(std::move(profile));
        }
        comp->setEventProfiles(std::move(profiles));
    }
    if (j.contains("fields")) {
        auto fields = entity::fieldsFromJson(j.at("fields"));
        if (!fields) {
            return fail("scene file '{}': {}", scenePath.string(), fields.error().message);
        }
        if (auto ok = comp->setFields(std::move(*fields)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // ADR-209: the director, read after the entities it directs so `setStaging`'s validation has
    // the actor list to check against. A malformed scenario fails the whole file rather than being
    // dropped, for the same reason a malformed hero does: a director that silently is not there is
    // a scene that does nothing with no explanation.
    if (j.contains("staging")) {
        auto staging = stage::stagingFromJson(j.at("staging"));
        if (!staging) {
            return fail("scene file '{}': {}", scenePath.string(), staging.error().message);
        }
        if (auto ok = comp->setStaging(std::move(*staging)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    if (j.contains("graph")) {
        const json& gj = j.at("graph");
        Result<graph::Graph> g = graph::Graph::fromJson(gj);
        if (gj.is_string()) {
            // Relative to the scene file when it has a path, else through the asset registry
            // (which knows the project's base directory).
            std::filesystem::path file = gj.get<std::string>();
            if (file.is_relative() && !scenePath.empty()) {
                file = scenePath.parent_path() / file;
            } else if (file.is_relative()) {
                file = registry.resolve(file);
            }
            g = graph::Graph::loadFile(file);
        }
        if (!g) {
            return fail("scene file '{}': graph: {}", scenePath.string(), g.error().message);
        }
        if (auto ok = comp->setGraph(std::move(*g)); !ok) {
            return fail("scene file '{}': graph: {}", scenePath.string(), ok.error().message);
        }
    }
    if (j.contains("grids")) {
        const json& gridsJson = j.at("grids");
        if (!gridsJson.is_array()) {
            return fail("'grids' must be an array");
        }
        for (const json& gj : gridsJson) {
            auto g = spatial::GridField::fromJson(gj);
            if (!g) {
                return fail("scene file '{}': grid: {}", scenePath.string(), g.error().message);
            }
            if (auto added = comp->addGrid(std::move(*g)); !added) {
                return fail("scene file '{}': {}", scenePath.string(), added.error().message);
            }
        }
    }
    if (j.contains("materialPrograms")) {
        const json& programs = j.at("materialPrograms");
        if (!programs.is_array()) {
            return fail("'materialPrograms' must be an array");
        }
        for (const json& pj : programs) {
            // An entry is either the program inline or a path to a `.material.json` file, the same
            // two forms `graph` accepts, so a library material is shared rather than pasted in.
            Result<MaterialProgram> mp = fail("material program must be an object or a file path");
            if (pj.is_string()) {
                std::filesystem::path file = pj.get<std::string>();
                if (file.is_relative() && !scenePath.empty()) {
                    file = scenePath.parent_path() / file;
                } else if (file.is_relative()) {
                    file = registry.resolve(file);
                }
                mp = MaterialProgram::loadFile(file);
            } else if (pj.is_object()) {
                mp = MaterialProgram::fromJson(pj);
            }
            if (!mp) {
                return fail("scene file '{}': material program: {}", scenePath.string(), mp.error().message);
            }
            if (auto added = comp->addMaterialProgram(std::move(*mp)); !added) {
                return fail("scene file '{}': {}", scenePath.string(), added.error().message);
            }
        }
    }
    if (j.contains("nodes")) {
        const json& nodes = j.at("nodes");
        if (!nodes.is_array()) {
            return fail("'nodes' must be an array");
        }
        std::vector<std::pair<std::string, std::string>> pendingParents; // (node, parent)
        for (const json& item : nodes) {
            if (!item.is_object()) {
                return fail("scene node must be an object");
            }
            CompositionNode node;
            auto nodeName = readString(item, "name", "");
            if (!nodeName) {
                return std::unexpected(nodeName.error());
            }
            node.name = *nodeName;
            auto kindName = readString(item, "kind", "");
            if (!kindName) {
                return std::unexpected(kindName.error());
            }
            auto kind = nodeKindFromName(*kindName);
            if (!kind) {
                return fail("node '{}': {}", node.name, kind.error().message);
            }
            node.kind = *kind;
            auto asset = readString(item, "asset", "");
            if (!asset) {
                return std::unexpected(asset.error());
            }
            node.asset = *asset;
            auto parent = readString(item, "parent", "");
            if (!parent) {
                return std::unexpected(parent.error());
            }
            const std::string parentName = *parent; // applied after every node exists (forward references)
            auto position = readVec<3>(item, "position", node.transform.position);
            auto rotation = readVec<3>(item, "rotation", glm::vec3(0.0f));
            auto scale = readVec<3>(item, "scale", node.transform.scale);
            auto visible = readBool(item, "visible", true);
            auto locked = readBool(item, "locked", false);
            auto emissive = readFloat(item, "emissiveBoost", 1.0f);
            auto roughness = readFloat(item, "roughnessScale", 1.0f);
            // ADR-370: which node's canopy a particle emitter is measured from.
            if (item.contains("canopySource") && item.at("canopySource").is_string()) {
                node.canopySource = item.at("canopySource").get<std::string>();
            }
            if (item.contains("canopyFrom") && item.at("canopyFrom").is_number()) {
                node.canopyFrom = item.at("canopyFrom").get<float>();
            }
            if (item.contains("vortexAttractor") && item.at("vortexAttractor").is_boolean()) {
                node.vortexAttractor = item.at("vortexAttractor").get<bool>();
            }
            if (item.contains("vortexReach") && item.at("vortexReach").is_number()) {
                node.vortexReach = item.at("vortexReach").get<float>();
            }
            // ADR-376: the tree's emissive life. Absent is off.
            if (item.contains("energy") && item.at("energy").is_object()) {
                const nlohmann::json& ej = item.at("energy");
                node.energyAuthored = true;
                auto f = [&ej](const char* key, float& out) {
                    if (ej.contains(key) && ej.at(key).is_number()) { out = ej.at(key).get<float>(); }
                };
                auto c3 = [&ej](const char* key, glm::vec3& out) {
                    if (ej.contains(key) && ej.at(key).is_array() && ej.at(key).size() == 3) {
                        out = glm::vec3(ej.at(key)[0].get<float>(), ej.at(key)[1].get<float>(),
                                        ej.at(key)[2].get<float>());
                    }
                };
                f("intensity", node.energy.intensity);
                f("pulseSpeed", node.energy.pulseSpeed);
                f("pulseWidth", node.energy.pulseWidth);
                f("propagation", node.energy.propagation);
                f("root", node.energy.root);
                f("trunk", node.energy.trunk);
                f("branch", node.energy.branch);
                f("canopy", node.energy.canopy);
                f("noise", node.energy.noiseAmount);
                f("noiseScale", node.energy.noiseScale);
                f("noiseSpeed", node.energy.noiseSpeed);
                f("bloom", node.energy.bloom);
                f("shimmer", node.energy.shimmer);
                f("shimmerSpeed", node.energy.shimmerSpeed);
                f("shimmerScale", node.energy.shimmerScale);
                f("shimmerVariation", node.energy.shimmerVariation);
                c3("colorNear", node.energy.colorNear);
                c3("colorFar", node.energy.colorFar);
            }
            // ADR-360: the wind body. Absent is the ordinary state, not a fault.
            if (item.contains("wind") && item.at("wind").is_object()) {
                const nlohmann::json& w = item.at("wind");
                node.windAuthored = true;
                auto read = [&w](const char* key, float& out) {
                    if (w.contains(key) && w.at(key).is_number()) {
                        out = w.at(key).get<float>();
                    }
                };
                read("strength", node.wind.strength);
                read("trunk", node.wind.trunk);
                read("branch", node.wind.branch);
                read("foliage", node.wind.foliage);
                read("flutter", node.wind.flutter);
                read("lag", node.wind.lag);
            }
            const Error* fieldError = nullptr;
            auto check = [&](const auto& r) {
                if (!r && fieldError == nullptr) {
                    fieldError = &r.error();
                }
            };
            check(position);
            check(rotation);
            check(scale);
            check(visible);
            check(emissive);
            check(roughness);
            if (fieldError != nullptr) {
                return fail("node '{}': {}", node.name, fieldError->message);
            }
            node.transform.position = *position;
            node.transform.rotation = quatFromEulerDegrees(*rotation);
            node.transform.scale = *scale;
            node.visible = *visible;
            node.locked = *locked;
            node.emissiveBoost = *emissive;
            node.roughnessScale = *roughness;
            if (item.contains("lod")) { // ADR-351: runtime LOD for this node's imported meshes
                const json& lod = item.at("lod");
                if (!lod.is_object()) {
                    return fail("node '{}': 'lod' must be an object", node.name);
                }
                auto enabled = readBool(lod, "enabled", true);
                auto thinning = readBool(lod, "thinning", true);
                auto hysteresis = readFloat(lod, "hysteresis", 0.0f);
                auto screenError = readFloat(lod, "maxScreenError", -1.0f);
                if (!enabled) return std::unexpected(enabled.error());
                if (!thinning) return std::unexpected(thinning.error());
                if (!hysteresis) return std::unexpected(hysteresis.error());
                if (!screenError) return std::unexpected(screenError.error());
                node.lod.enabled = *enabled;
                node.lod.thinning = *thinning;
                node.lod.hysteresis = *hysteresis;
                node.lod.maxScreenError = *screenError;
                node.lod.ratios.clear();
                if (lod.contains("ratios")) {
                    const json& ratios = lod.at("ratios");
                    if (!ratios.is_array()) {
                        return fail("node '{}': 'lod.ratios' must be an array of numbers", node.name);
                    }
                    for (const json& r : ratios) {
                        if (!r.is_number()) {
                            return fail("node '{}': 'lod.ratios' must be an array of numbers", node.name);
                        }
                        node.lod.ratios.push_back(r.get<float>());
                    }
                }
                // A key this block does not know is named rather than ignored, for the reason the
                // animation block below gives: a misspelt setting that parses is a setting that is
                // configured in the file and absent from the engine.
                for (const auto& [key, unused] : lod.items()) {
                    if (key != "enabled" && key != "thinning" && key != "hysteresis" &&
                        key != "ratios" && key != "maxScreenError") {
                        return fail("node '{}': unknown key 'lod.{}'", node.name, key);
                    }
                }
                if (node.lod.enabled && node.kind != NodeKind::Gltf) {
                    log::warn("node '{}': 'lod' is only read on a gltf node; this one is a {}",
                              node.name, nodeKindName(node.kind));
                }
            }
            if (item.contains("animation")) { // ADR-086: a skinned character's opening state
                const json& anim = item.at("animation");
                if (!anim.is_object()) {
                    return fail("node '{}': 'animation' must be an object", node.name);
                }
                auto state = readString(anim, "state", "");
                auto blend = readFloat(anim, "blend", -1.0f);
                auto speed = readFloat(anim, "speed", 1.0f);
                auto hz = readFloat(anim, "updateHz", 0.0f);
                auto near = readFloat(anim, "nearDistance", 15.0f);
                auto farHz = readFloat(anim, "farHz", 20.0f);
                auto cull = readFloat(anim, "cullDistance", 120.0f);
                if (!state) return std::unexpected(state.error());
                if (!blend) return std::unexpected(blend.error());
                if (!speed) return std::unexpected(speed.error());
                if (!hz) return std::unexpected(hz.error());
                if (!near) return std::unexpected(near.error());
                if (!farHz) return std::unexpected(farHz.error());
                if (!cull) return std::unexpected(cull.error());
                // A key this block does not know is named rather than ignored. `"lodCount"` was
                // silently accepted for months against a parser that reads `"count"`, and the
                // ladder read as configured in the file while being a single rung in the engine.
                // ADR-278 generalised this loop out of here; what is left is the key list.
                static constexpr std::string_view kAnimationKeys[] = {
                    "state", "blend",        "speed",  "updateHz",  "nearDistance",
                    "farHz",   "cullDistance", "layers",    "rootMotion",
                    "bodyCompensation", "contacts", "matchPhase", "inertialize"};
                json_keys::warnUnknownKeys(anim, kAnimationKeys,
                                           where + ": node '" + node.name + "': animation");
                node.animation.state = *state;
                node.animation.blend = *blend;
                node.animation.speed = *speed;
                node.animation.updateHz = *hz;
                node.animation.nearDistance = *near;
                node.animation.farHz = *farHz;
                node.animation.cullDistance = *cull;
                // ADR-300: the layer stack. Every field of a mask is named by the scene because
                // every rig names its joints differently -- `head.x` on the alien pack, `Head01` on
                // the bull, `Head` on the chicken. A name this rig does not carry is warned about
                // when the rig is built, never silently dropped.
                // ADR-546/547: which joints to analyse, and what to do with the result.
                if (anim.contains("contacts")) {
                    const json& contacts = anim.at("contacts");
                    if (!contacts.is_array() ||
                        !std::all_of(contacts.begin(), contacts.end(),
                                     [](const json& j) { return j.is_string(); })) {
                        return fail("node '{}': animation 'contacts' must be an array of joint names",
                                    node.name);
                    }
                    node.animation.contacts.clear();
                    for (const json& entry : contacts) {
                        node.animation.contacts.push_back(entry.get<std::string>());
                    }
                }
                node.animation.matchPhase = anim.value("matchPhase", false);
                {
                    auto inert = readFloat(anim, "inertialize", 0.0f);
                    if (!inert) {
                        return fail("node '{}': animation 'inertialize': {}", node.name,
                                    inert.error().message);
                    }
                    if (*inert < 0.0f) {
                        return fail("node '{}': animation 'inertialize' is a half-life in seconds and "
                                    "cannot be negative (got {})",
                                    node.name, *inert);
                    }
                    node.animation.inertialize = *inert;
                }
                // ADR-544: reachable contact solving. Parsed before the layers so a scene that
                // enables it and authors no foot layer still gets its body joint resolved and
                // still hears about a joint name this rig does not carry.
                if (anim.contains("bodyCompensation")) {
                    const json& bc = anim.at("bodyCompensation");
                    if (!bc.is_object()) {
                        return fail("node '{}': animation 'bodyCompensation' must be an object", node.name);
                    }
                    static constexpr std::string_view kBodyKeys[] = {
                        "enabled", "joint", "maxDown", "maxUp", "maxLateral", "compliance", "iterations"};
                    json_keys::warnUnknownKeys(bc, kBodyKeys,
                                               where + ": node '" + node.name + "': bodyCompensation");
                    BodyCompensationSpec spec;
                    spec.enabled = bc.value("enabled", true);
                    spec.joint = bc.value("joint", std::string());
                    auto down = readFloat(bc, "maxDown", spec.limits.maxDown);
                    auto up = readFloat(bc, "maxUp", spec.limits.maxUp);
                    auto lateral = readFloat(bc, "maxLateral", spec.limits.maxLateral);
                    if (!down || !up || !lateral) {
                        return fail("node '{}': animation bodyCompensation: {}", node.name,
                                    (!down ? down : (!up ? up : lateral)).error().message);
                    }
                    if (*down < 0.0f || *up < 0.0f || *lateral < 0.0f) {
                        return fail("node '{}': animation bodyCompensation: a limit is a distance and "
                                    "cannot be negative (down {}, up {}, lateral {})",
                                    node.name, *down, *up, *lateral);
                    }
                    spec.limits.maxDown = *down;
                    spec.limits.maxUp = *up;
                    spec.limits.maxLateral = *lateral;
                    if (bc.contains("compliance")) {
                        auto compliance = readVec<3>(bc, "compliance", spec.limits.compliance);
                        if (!compliance) {
                            return fail("node '{}': animation bodyCompensation: 'compliance' must be three "
                                        "numbers, one per axis ({})",
                                        node.name, compliance.error().message);
                        }
                        spec.limits.compliance = *compliance;
                    }
                    if (bc.contains("iterations")) {
                        if (!bc.at("iterations").is_number_unsigned()) {
                            return fail("node '{}': animation bodyCompensation: 'iterations' must be a "
                                        "positive whole number of solver sweeps",
                                        node.name);
                        }
                        spec.limits.iterations =
                            std::max(1u, bc.at("iterations").get<std::uint32_t>());
                    }
                    node.animation.bodyCompensation = std::move(spec);
                }
                if (anim.contains("layers")) {
                    const json& layers = anim.at("layers");
                    if (!layers.is_array()) {
                        return fail("node '{}': animation 'layers' must be an array", node.name);
                    }
                    for (const json& entry : layers) {
                        if (!entry.is_object()) {
                            return fail("node '{}': every animation layer must be an object", node.name);
                        }
                        PoseLayer layer;
                        auto lname = readString(entry, "name", "");
                        auto kind = readString(entry, "kind", "aim");
                        auto drive = readString(entry, "drive", "manual");
                        auto pivot = readString(entry, "pivot", "");
                        auto clipName = readString(entry, "clip", "");
                        auto clipRate = readFloat(entry, "clipRate", 1.0f);
                        auto maxYaw = readFloat(entry, "maxYaw", 70.0f);
                        auto maxPitch = readFloat(entry, "maxPitch", 35.0f);
                        auto weight = readFloat(entry, "weight", 0.0f);
                        if (!lname) return std::unexpected(lname.error());
                        if (!kind) return std::unexpected(kind.error());
                        if (!drive) return std::unexpected(drive.error());
                        if (!pivot) return std::unexpected(pivot.error());
                        if (!clipName) return std::unexpected(clipName.error());
                        if (!clipRate) return std::unexpected(clipRate.error());
                        if (!maxYaw) return std::unexpected(maxYaw.error());
                        if (!maxPitch) return std::unexpected(maxPitch.error());
                        if (!weight) return std::unexpected(weight.error());
                        layer.name = *lname;
                        if (!poseLayerKindFromName(*kind, layer.kind)) {
                            // The list is generated rather than written out. It said
                            // "(aim, additive)" while the engine had supported `foot` for a whole
                            // phase -- a hand-maintained enumeration in an error message is a
                            // claim about every value that will ever be added, and it was already
                            // wrong before anyone read it.
                            std::string known;
                            for (const PoseLayerKind k :
                                 {PoseLayerKind::Aim, PoseLayerKind::Additive, PoseLayerKind::Foot,
                                  PoseLayerKind::Stride, PoseLayerKind::Secondary,
                                  PoseLayerKind::Lean, PoseLayerKind::Reach}) {
                                if (!known.empty()) {
                                    known += ", ";
                                }
                                known += poseLayerKindName(k);
                            }
                            return fail("node '{}': animation layer '{}': unknown kind '{}' ({})",
                                        node.name, layer.name, *kind, known);
                        }
                        if (!poseLayerDriveFromName(*drive, layer.drive)) {
                            return fail("node '{}': animation layer '{}': unknown drive '{}' (manual, look, "
                                        "reaction, ground)",
                                        node.name, layer.name, *drive);
                        }
                        layer.pivot = *pivot;
                        layer.clip = *clipName;
                        layer.clipRate = *clipRate;
                        layer.maxYawDegrees = *maxYaw;
                        layer.maxPitchDegrees = *maxPitch;
                        // A manual layer is one a tool or a test drives, so its authored weight is
                        // its opening value; a driven layer's weight is written every frame from
                        // the seam and an authored one would be overwritten before it was read.
                        layer.weight = layer.drive == PoseLayerDrive::Manual ? *weight : 0.0f;
                        if (entry.contains("forward")) {
                            auto fwd = readVec<3>(entry, "forward", layer.forward);
                            if (!fwd) return std::unexpected(fwd.error());
                            layer.forward = *fwd;
                        }
                        if (entry.contains("joints")) {
                            const json& joints = entry.at("joints");
                            if (!joints.is_array()) {
                                return fail("node '{}': animation layer '{}': 'joints' must be an array of "
                                            "joint names",
                                            node.name, layer.name);
                            }
                            for (const json& j : joints) {
                                if (!j.is_string()) {
                                    return fail("node '{}': animation layer '{}': 'joints' must be an array "
                                                "of joint names",
                                                node.name, layer.name);
                                }
                                layer.mask.joints.push_back(j.get<std::string>());
                            }
                        }
                        if (entry.contains("weights")) {
                            const json& weights = entry.at("weights");
                            if (!weights.is_array()) {
                                return fail("node '{}': animation layer '{}': 'weights' must be an array",
                                            node.name, layer.name);
                            }
                            for (const json& w : weights) {
                                if (!w.is_number()) {
                                    return fail("node '{}': animation layer '{}': 'weights' must be numbers",
                                                node.name, layer.name);
                                }
                                layer.mask.weights.push_back(w.get<float>());
                            }
                            if (layer.mask.weights.size() > layer.mask.joints.size()) {
                                return fail("node '{}': animation layer '{}': {} weights for {} joints",
                                            node.name, layer.name, layer.mask.weights.size(),
                                            layer.mask.joints.size());
                            }
                        }
                        auto descendants = readBool(entry, "descendants", false);
                        if (!descendants) return std::unexpected(descendants.error());
                        layer.mask.descendants = *descendants;
                        // ADR-359: a foot layer is addressed by a chain and never by a mask, so it
                        // reads a different set of keys and refuses a mask outright here rather
                        // than letting `bind` report a no-op after the scene has loaded.
                        if (layer.kind == PoseLayerKind::Lean) {
                            auto accel = readFloat(entry, "degreesPerAccel", layer.leanDegreesPerAccel);
                            auto turn = readFloat(entry, "degreesPerTurn", layer.leanDegreesPerTurn);
                            auto cap = readFloat(entry, "maxDegrees", layer.leanMaxDegrees);
                            if (!accel) return std::unexpected(accel.error());
                            if (!turn) return std::unexpected(turn.error());
                            if (!cap) return std::unexpected(cap.error());
                            layer.leanDegreesPerAccel = *accel;
                            layer.leanDegreesPerTurn = *turn;
                            layer.leanMaxDegrees = *cap;
                            if (layer.leanMaxDegrees < 0.0f) {
                                return fail("node '{}': animation layer '{}': 'maxDegrees' cannot be "
                                            "negative",
                                            node.name, layer.name);
                            }
                        }
                        if (layer.kind == PoseLayerKind::Secondary) {
                            // Phase B §26-§28. Masked like an aim layer, because the oscillation
                            // is applied per joint and a chest and a head are different amounts of
                            // it -- the opposite of a foot layer, where half a knee is meaningless.
                            auto degrees = readFloat(entry, "degrees", layer.secondaryDegrees);
                            auto period = readFloat(entry, "period", layer.secondaryPeriod);
                            auto phase = readFloat(entry, "phase", layer.secondaryPhase);
                            auto spread = readFloat(entry, "spread", layer.secondarySpread);
                            auto still = readFloat(entry, "stillness", layer.secondaryStillness);
                            if (!degrees) return std::unexpected(degrees.error());
                            if (!period) return std::unexpected(period.error());
                            if (!phase) return std::unexpected(phase.error());
                            if (!spread) return std::unexpected(spread.error());
                            if (!still) return std::unexpected(still.error());
                            layer.secondaryDegrees = *degrees;
                            layer.secondaryPeriod = *period;
                            layer.secondaryPhase = *phase;
                            layer.secondarySpread = *spread;
                            layer.secondaryStillness = *still;
                            if (layer.secondaryPeriod <= 0.0f) {
                                return fail("node '{}': animation layer '{}': 'period' must be above "
                                            "zero seconds",
                                            node.name, layer.name);
                            }
                            if (entry.contains("axis")) {
                                auto axis = readVec<3>(entry, "axis", layer.secondaryAxis);
                                if (!axis) return std::unexpected(axis.error());
                                layer.secondaryAxis = *axis;
                            }
                        }
                        if (layer.kind == PoseLayerKind::Stride) {
                            // Phase B §7. Addressed by one joint and a body to measure it from,
                            // not by a mask: what it scales is an excursion, which needs two ends.
                            if (!entry.contains("joint") || !entry.at("joint").is_string()) {
                                return fail("node '{}': animation layer '{}': a stride layer needs a "
                                            "'joint' -- the foot whose step is shortened",
                                            node.name, layer.name);
                            }
                            layer.strideJoint = entry.at("joint").get<std::string>();
                            if (entry.contains("origin") && entry.at("origin").is_string()) {
                                layer.strideOrigin = entry.at("origin").get<std::string>();
                            }
                            auto lo = readFloat(entry, "strideMin", layer.strideMin);
                            auto hi = readFloat(entry, "strideMax", layer.strideMax);
                            auto lift = readFloat(entry, "strideLift", layer.strideLift);
                            if (!lo) return std::unexpected(lo.error());
                            if (!hi) return std::unexpected(hi.error());
                            if (!lift) return std::unexpected(lift.error());
                            layer.strideMin = *lo;
                            layer.strideMax = *hi;
                            layer.strideLift = *lift;
                            if (layer.strideMin > layer.strideMax) {
                                return fail("node '{}': animation layer '{}': strideMin {} is above "
                                            "strideMax {}, so every ratio clamps to the wrong end",
                                            node.name, layer.name, layer.strideMin, layer.strideMax);
                            }
                        }
                        if (layer.kind == PoseLayerKind::Foot ||
                            layer.kind == PoseLayerKind::Reach) {
                            if (!entry.contains("chain")) {
                                return fail("node '{}': animation layer '{}': a {} layer needs a "
                                            "'chain' of exactly three joint names -- root, mid and tip",
                                            node.name, layer.name, poseLayerKindName(layer.kind));
                            }
                            const json& chain = entry.at("chain");
                            if (!chain.is_array() || chain.size() != 3 ||
                                !std::all_of(chain.begin(), chain.end(),
                                             [](const json& j) { return j.is_string(); })) {
                                return fail("node '{}': animation layer '{}': 'chain' must be exactly three "
                                            "joint names -- the hip, the knee and the foot",
                                            node.name, layer.name);
                            }
                            layer.chainRoot = chain[0].get<std::string>();
                            layer.chainMid = chain[1].get<std::string>();
                            layer.chainTip = chain[2].get<std::string>();
                            if (!layer.mask.joints.empty()) {
                                return fail("node '{}': animation layer '{}': a foot layer is driven by its "
                                            "chain and has no use for 'joints'; a two-bone solve cannot be "
                                            "applied to some of its joints and not others",
                                            node.name, layer.name);
                            }
                            auto align = readFloat(entry, "footAlign", 1.0f);
                            auto offset = readFloat(entry, "groundOffset", 0.0f);
                            auto reach = readFloat(entry, "extension", 1.0f);
                            // ADR-615: `footLock` is parsed here because its four neighbours above
                            // always were and it never was -- an omission, not a decision. Until
                            // this line existed a scene that authored the key was told it "is not
                            // one this build reads and was ignored", which was true and made the
                            // whole foot-lock subsystem (ADR-557's derived anchor, `inContact`,
                            // `contactElapsed`, `bodyVelocity`) unreachable from any scene. The
                            // default stays 0 -- off -- so nothing changes until an author asks.
                            auto lock = readFloat(entry, "footLock", 0.0f);
                            if (!align) return std::unexpected(align.error());
                            if (!offset) return std::unexpected(offset.error());
                            if (!reach) return std::unexpected(reach.error());
                            if (!lock) return std::unexpected(lock.error());
                            layer.footAlign = *align;
                            layer.groundOffset = *offset;
                            layer.extension = *reach;
                            layer.footLock = *lock;
                            if (entry.contains("poleDirection")) {
                                auto pole = readVec<3>(entry, "poleDirection", layer.poleDirection);
                                if (!pole) return std::unexpected(pole.error());
                                layer.poleDirection = *pole;
                            }
                            if (entry.contains("soleUp")) {
                                auto sole = readVec<3>(entry, "soleUp", layer.soleUp);
                                if (!sole) return std::unexpected(sole.error());
                                layer.soleUp = *sole;
                            }
                        } else if (layer.kind != PoseLayerKind::Stride && layer.mask.joints.empty()) {
                            // **A stride layer is addressed by `joint`, not by a mask** -- the same
                            // way a foot layer is addressed by `chain`. Without this exemption the
                            // check refused every stride layer with "masks no joints", which is the
                            // third catch-all over this enum to fire tonight: `rebind`'s clip
                            // lookup, the error message's hand-written kind list, and this.
                            //
                            // The pattern is worth naming. A branch that says "anything that is
                            // not X" is a claim about every value that will ever be added to the
                            // enum, and it is the author of the *next* value who pays for it.
                            return fail("node '{}': animation layer '{}' masks no joints, so it could only "
                                        "ever do nothing",
                                        node.name, layer.name);
                        }
                        for (const auto& key : entry.items()) {
                            // The size is deduced rather than written, because a hand-kept count
                            // beside a hand-kept list is two things that can disagree.
                            static constexpr std::array kLayerKeys{
                                std::string_view{"name"},      std::string_view{"kind"},
                                std::string_view{"drive"},     std::string_view{"joints"},
                                std::string_view{"weights"},   std::string_view{"descendants"},
                                std::string_view{"pivot"},     std::string_view{"forward"},
                                std::string_view{"maxYaw"},    std::string_view{"maxPitch"},
                                std::string_view{"clip"},      std::string_view{"clipRate"},
                                std::string_view{"weight"},    std::string_view{"chain"},
                                std::string_view{"poleDirection"}, std::string_view{"footAlign"},
                                std::string_view{"groundOffset"},  std::string_view{"extension"},
                                std::string_view{"soleUp"},        std::string_view{"footLock"},
                                // Stride (Phase B §7)
                                std::string_view{"joint"},     std::string_view{"origin"},
                                std::string_view{"strideMin"}, std::string_view{"strideMax"},
                                std::string_view{"strideLift"},
                                // Secondary motion (Phase B §26-§28)
                                std::string_view{"degrees"},   std::string_view{"period"},
                                std::string_view{"phase"},     std::string_view{"spread"},
                                std::string_view{"stillness"}, std::string_view{"axis"},
                                // Lean (Phase B §19)
                                std::string_view{"degreesPerAccel"},
                                std::string_view{"degreesPerTurn"},
                                std::string_view{"maxDegrees"}};
                            if (std::find(kLayerKeys.begin(), kLayerKeys.end(), key.key()) ==
                                kLayerKeys.end()) {
                                log::warn("scene file '{}': node '{}': animation layer key '{}' is not one "
                                          "this build reads and was ignored",
                                          scenePath.string(), node.name, key.key());
                            }
                        }
                        node.animation.layers.push_back(std::move(layer));
                    }
                }
                // ADR-337: the per-clip root-motion opt-in. By clip *name*, because the thing
                // being opted in is a take an animator authored and the engine's own vocabulary
                // for a take is its name. A clip this rig does not have is warned about when the
                // rig is built, with the node's name on it -- never silently dropped, which is
                // the ADR-274 lesson applied to an opt-in that could otherwise look like it was
                // working while doing nothing.
                if (anim.contains("rootMotion")) {
                    const json& rm = anim.at("rootMotion");
                    if (!rm.is_array()) {
                        return fail("node '{}': animation 'rootMotion' must be an array", node.name);
                    }
                    for (const json& entry : rm) {
                        RootMotionSpec spec;
                        if (entry.is_string()) {
                            // The short form. Every axis, the clip's own root joint: what an
                            // author means nine times in ten, spelled as `"rootMotion": ["Landing"]`.
                            spec.clip = entry.get<std::string>();
                        } else if (entry.is_object()) {
                            auto clipName = readString(entry, "clip", "");
                            auto jointName = readString(entry, "joint", "");
                            auto axes = readString(entry, "axes", "xyz");
                            if (!clipName) return std::unexpected(clipName.error());
                            if (!jointName) return std::unexpected(jointName.error());
                            if (!axes) return std::unexpected(axes.error());
                            spec.clip = *clipName;
                            spec.joint = *jointName;
                            if (!rootMotionAxesFromName(*axes, spec.axes)) {
                                return fail("node '{}': root motion for clip '{}': unknown axes '{}' "
                                            "(any of x, y and z, or 'none')",
                                            node.name, spec.clip, *axes);
                            }
                            for (const auto& key : entry.items()) {
                                static constexpr std::array<std::string_view, 3> kRootKeys{
                                    "clip", "joint", "axes"};
                                if (std::find(kRootKeys.begin(), kRootKeys.end(), key.key()) ==
                                    kRootKeys.end()) {
                                    log::warn("scene file '{}': node '{}': root motion key '{}' is not "
                                              "one this build reads and was ignored",
                                              scenePath.string(), node.name, key.key());
                                }
                            }
                        } else {
                            return fail("node '{}': every 'rootMotion' entry must be a clip name or "
                                        "an object",
                                        node.name);
                        }
                        if (spec.clip.empty()) {
                            return fail("node '{}': a 'rootMotion' entry names no clip", node.name);
                        }
                        node.animation.rootMotion.push_back(std::move(spec));
                    }
                }
            }
            if (item.contains("procedural")) {
                auto pg = ProceduralGeometry::fromJson(item.at("procedural"));
                if (!pg) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name, pg.error().message);
                }
                node.procedural = std::move(*pg);
                node.proceduralMaterialAuthored = item.at("procedural").contains("material");
            }
            // ADR-099 §13: a procedural node that floats. Read after "procedural" so a scene can
            // say "these are lily pads" and "they drift on the valley's river" in the same node.
            if (item.contains("float")) {
                auto spec = FloatSpec::fromJson(item.at("float"));
                if (!spec) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name,
                                spec.error().message);
                }
                node.floats = std::move(*spec);
            }
            if (node.kind == NodeKind::City) {
                // Everything optional, so `{"kind": "city"}` alone is a city at the shipped
                // settings rather than nothing -- the same courtesy the terrain node extends.
                if (item.contains("city")) {
                    const json& c = item.at("city");
                    if (!c.is_object()) {
                        return fail("node '{}': 'city' must be an object", node.name);
                    }
                    world::CitySettings& cs = node.city;
                    cs.moduleSize = c.value("moduleSize", cs.moduleSize);
                    cs.tileUnits = c.value("tileUnits", cs.tileUnits);
                    cs.blocksX = c.value("blocksX", cs.blocksX);
                    cs.blocksZ = c.value("blocksZ", cs.blocksZ);
                    cs.blockCells = c.value("blockCells", cs.blockCells);
                    cs.roadCells = c.value("roadCells", cs.roadCells);
                    cs.seed = c.value("seed", cs.seed);
                    cs.plazaFraction = c.value("plazaFraction", cs.plazaFraction);
                    cs.crossingFraction = c.value("crossingFraction", cs.crossingFraction);
                    cs.plotFill = c.value("plotFill", cs.plotFill);
                    cs.propsPerCell = c.value("propsPerCell", cs.propsPerCell);
                    cs.propSpread = c.value("propSpread", cs.propSpread);
                    cs.streetPropChance = c.value("streetPropChance", cs.streetPropChance);
                    cs.cornerMix = c.value("cornerMix", cs.cornerMix);
                    cs.overlayChance = c.value("overlayChance", cs.overlayChance);
                    cs.overlayRun = c.value("overlayRun", cs.overlayRun);
                    cs.buildDepth = c.value("buildDepth", cs.buildDepth);
                    cs.footwayMetres = c.value("footwayMetres", cs.footwayMetres);
                    // Refused at load rather than at rebuild: a city that cannot be planned is a
                    // scene file somebody has to fix, and the error names the field.
                    if (auto r = cs.validate(); !r) {
                        return fail("node '{}': {}", node.name, r.error().message);
                    }
                }
                if (item.contains("cityLibrary")) {
                    node.cityLibrary = item.at("cityLibrary").get<std::string>();
                }
            }
            if (item.contains("material")) {
                node.materialAuthored = true;
                const json& m = item.at("material");
                if (!m.is_object()) {
                    return fail("node '{}': 'material' must be an object", node.name);
                }
                Material& mat = node.terrainMaterial;
                auto baseColor = readVec<3>(m, "baseColor", mat.baseColor);
                auto emissiveColor = readVec<3>(m, "emissiveColor", mat.emissiveColor);
                if (!baseColor) {
                    return fail("node '{}': material: {}", node.name, baseColor.error().message);
                }
                if (!emissiveColor) {
                    return fail("node '{}': material: {}", node.name, emissiveColor.error().message);
                }
                mat.baseColor = *baseColor;
                mat.emissiveColor = *emissiveColor;
                struct MatFloat { const char* key; float* target; };
                for (const MatFloat& f :
                     {MatFloat{"opacity", &mat.opacity}, MatFloat{"emissiveIntensity", &mat.emissiveIntensity},
                      MatFloat{"roughness", &mat.roughness}, MatFloat{"metallic", &mat.metallic}}) {
                    auto v = readFloat(m, f.key, *f.target);
                    if (!v) {
                        return fail("node '{}': material: {}", node.name, v.error().message);
                    }
                    *f.target = *v;
                }
                if (m.contains("program")) {
                    if (!m.at("program").is_string()) {
                        return fail("node '{}': material 'program' must be a string", node.name);
                    }
                    mat.program = m.at("program").get<std::string>();
                }
                if (m.contains("alphaMode")) {
                    // Parsed here rather than nowhere. A scene that wrote `"alphaMode":
                    // "blend"` was validated, ignored, and drawn opaque: RendererQA's
                    // `transparent-orb` has said blend since it was written and has never once
                    // been transparent, which is also why the QA scene the plan describes as
                    // covering transparency covers none.
                    if (!m.at("alphaMode").is_string()) {
                        return fail("node '{}': material 'alphaMode' must be a string", node.name);
                    }
                    const std::string mode = m.at("alphaMode").get<std::string>();
                    if (mode == "opaque") {
                        mat.alphaMode = AlphaMode::Opaque;
                    } else if (mode == "mask") {
                        mat.alphaMode = AlphaMode::Mask;
                    } else if (mode == "blend") {
                        mat.alphaMode = AlphaMode::Blend;
                    } else {
                        return fail("node '{}': material alphaMode '{}' is not opaque, mask or blend",
                                    node.name, mode);
                    }
                }
                // Which kinds actually read it. Terrain always did; an orb draws one mesh of its
                // own and can honestly wear an authored material. Everything else takes its
                // surface from somewhere the node cannot override -- a glTF asset's own
                // materials, a procedural's material block, a particle system's colours -- and
                // for those the block is dropped. Said out loud, because a material that is
                // parsed, validated and then ignored is exactly the kind of silent nothing this
                // scene format has shipped before.
                if (node.kind != NodeKind::Terrain && node.kind != NodeKind::Orb) {
                    log::warn("node '{}' ({}): a 'material' block on this kind is not used; "
                              "its surface comes from {}",
                              node.name, nodeKindName(node.kind),
                              node.kind == NodeKind::Gltf      ? "the asset's own materials"
                              : node.kind == NodeKind::Procedural ? "the procedural's material"
                              : node.kind == NodeKind::Particles  ? "the particle colours"
                                                                  : "elsewhere");
                }
            }
            if (node.kind == NodeKind::Terrain) {
                // "world" is the geography and "terrain" is how it is turned into meshes; both are
                // optional, so `{"kind": "terrain"}` alone gives the shipped world at shipped
                // settings rather than nothing.
                if (item.contains("world")) {
                    auto map = world::worldMapFromJson(item.at("world"));
                    if (!map) {
                        return fail("scene file '{}': node '{}': world: {}", scenePath.string(), node.name,
                                    map.error().message);
                    }
                    node.worldMap = std::move(*map);
                } else {
                    node.worldMap = world::defaultWorld();
                }
                if (item.contains("terrain")) {
                    const json& t = item.at("terrain");
                    if (!t.is_object()) {
                        return fail("node '{}': 'terrain' must be an object", node.name);
                    }
                    world::TerrainSettings& ts = node.terrain;
                    struct TerrainFloat { const char* key; float* target; };
                    for (const TerrainFloat& f :
                         {TerrainFloat{"chunkSize", &ts.chunkSize}, TerrainFloat{"lodDistance", &ts.lodDistance},
                          TerrainFloat{"viewDistance", &ts.viewDistance},
                          TerrainFloat{"shadowDistance", &ts.shadowDistance},
                          TerrainFloat{"skirtDepth", &ts.skirtDepth},
                          TerrainFloat{"groundGlow", &ts.groundGlow},
                          TerrainFloat{"groundGlowScale", &ts.groundGlowScale},
                          TerrainFloat{"groundGlowCoverage", &ts.groundGlowCoverage}}) {
                        auto v = readFloat(t, f.key, *f.target);
                        if (!v) {
                            return fail("node '{}': terrain: {}", node.name, v.error().message);
                        }
                        *f.target = *v;
                    }
                    if (t.contains("groundGlowColor")) {
                        const json& a = t.at("groundGlowColor");
                        if (!a.is_array() || a.size() != 3 || !a.at(0).is_number()) {
                            return fail("node '{}': 'groundGlowColor' must be an array of 3 numbers", node.name);
                        }
                        ts.groundGlowColor = glm::vec3(a.at(0).get<float>(), a.at(1).get<float>(),
                                                       a.at(2).get<float>());
                    }
                    if (t.contains("groundMottle")) {
                        if (!t.at("groundMottle").is_boolean()) {
                            return fail("node '{}': terrain 'groundMottle' must be a boolean", node.name);
                        }
                        ts.groundMottle = t.at("groundMottle").get<bool>();
                    }
                    if (t.contains("water")) {
                        const json& wj = t.at("water");
                        if (!wj.is_object()) {
                            return fail("node '{}': terrain 'water' must be an object", node.name);
                        }
                        world::WaterSettings& w = ts.water;
                        if (wj.contains("enabled")) {
                            if (!wj.at("enabled").is_boolean()) {
                                return fail("node '{}': water 'enabled' must be a boolean", node.name);
                            }
                            w.enabled = wj.at("enabled").get<bool>();
                        }
                        for (const TerrainFloat& f :
                             {TerrainFloat{"shallow", &w.shallow}, TerrainFloat{"roughness", &w.roughness},
                              TerrainFloat{"emissiveIntensity", &w.emissiveIntensity},
                              TerrainFloat{"clarity", &w.clarity}, TerrainFloat{"maxOpacity", &w.maxOpacity},
                              TerrainFloat{"edgeFade", &w.edgeFade}, TerrainFloat{"fresnel", &w.fresnel},
                              TerrainFloat{"reflection", &w.reflection},
                              TerrainFloat{"specular", &w.specular}, TerrainFloat{"ripple", &w.ripple},
                              TerrainFloat{"rippleScale", &w.rippleScale},
                              TerrainFloat{"rippleSpeed", &w.rippleSpeed}, TerrainFloat{"chop", &w.chop},
                              TerrainFloat{"foam", &w.foam}, TerrainFloat{"foamWidth", &w.foamWidth},
                              TerrainFloat{"refraction", &w.refraction}, TerrainFloat{"glow", &w.glow},
                              TerrainFloat{"glowScale", &w.glowScale},
                              TerrainFloat{"glowCoverage", &w.glowCoverage},
                              TerrainFloat{"glowDepth", &w.glowDepth},
                              TerrainFloat{"sparkle", &w.sparkle}, TerrainFloat{"swell", &w.swell}}) {
                            auto v = readFloat(wj, f.key, *f.target);
                            if (!v) {
                                return fail("node '{}': water: {}", node.name, v.error().message);
                            }
                            *f.target = *v;
                        }
                        for (const auto& [key, target] : {std::pair{"shallowColor", &w.shallowColor},
                                                          std::pair{"deepColor", &w.deepColor},
                                                          std::pair{"emissiveColor", &w.emissiveColor},
                                                          std::pair{"reflectionTint", &w.reflectionTint},
                                                          std::pair{"foamColor", &w.foamColor},
                                                          std::pair{"glowColor", &w.glowColor},
                                                          std::pair{"sparkleColor", &w.sparkleColor}}) {
                            auto v = readVec<3>(wj, key, *target);
                            if (!v) {
                                return fail("node '{}': water: {}", node.name, v.error().message);
                            }
                            *target = *v;
                        }
                        if (auto vr = w.validate(); !vr) {
                            return fail("node '{}': {}", node.name, vr.error().message);
                        }
                    }
                    if (t.contains("flow")) {
                        auto flow = world::waterFlowFromJson(t.at("flow"));
                        if (!flow) {
                            return fail("node '{}': terrain: {}", node.name, flow.error().message);
                        }
                        node.waterFlow = *flow;
                    }
                    struct TerrainInt { const char* key; int* target; };
                    for (const TerrainInt& f :
                         {TerrainInt{"resolution", &ts.resolution}, TerrainInt{"lodLevels", &ts.lodLevels}}) {
                        auto v = readInt(t, f.key, *f.target);
                        if (!v) {
                            return fail("node '{}': terrain: {}", node.name, v.error().message);
                        }
                        *f.target = *v;
                    }
                }
                if (item.contains("scatter")) {
                    auto ecology = world::ecologyFromJson(item.at("scatter"));
                    if (!ecology) {
                        return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name,
                                    ecology.error().message);
                    }
                    node.ecology = std::move(*ecology);
                }
                if (item.contains("clearings")) {
                    auto clearances = world::clearancesFromJson(item.at("clearings"));
                    if (!clearances) {
                        return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name,
                                    clearances.error().message);
                    }
                    node.ecology.clearances = std::move(*clearances);
                }
            }
            if (item.contains("particles")) {
                auto particles = particlesFromJson(item.at("particles"));
                if (!particles) {
                    return fail("node '{}': {}", node.name, particles.error().message);
                }
                node.particles = std::move(*particles);
            }
            if (item.contains("field")) {
                auto field = spatial::FieldSpec::fromJson(item.at("field"));
                if (!field) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name, field.error().message);
                }
                node.field = std::move(*field);
            }
            if (item.contains("spline")) {
                auto spline = spatial::Spline::fromJson(item.at("spline"));
                if (!spline) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name, spline.error().message);
                }
                node.spline = std::move(*spline);
            }
            if (item.contains("sdf")) {
                auto sdf = SdfObject::fromJson(item.at("sdf"));
                if (!sdf) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name, sdf.error().message);
                }
                node.sdf = std::move(*sdf);
            }

            const std::string label = node.name.empty() ? std::string(nodeKindName(node.kind)) : node.name;
            const NodeKind nodeKind = node.kind;
            const std::filesystem::path assetPath = node.asset;
            auto added = comp->addNode(std::move(node));
            if (added) {
                if (!parentName.empty()) {
                    pendingParents.emplace_back((*added)->name, parentName);
                }
            } else {
                // A nested scene file that exists but fails is a structural error (cycle, depth,
                // malformed); a missing asset only costs its node.
                std::error_code ec;
                if (nodeKind == NodeKind::Scene && !assetPath.empty() &&
                    std::filesystem::exists(registry.resolve(assetPath), ec)) {
                    return fail("node '{}': {}", label, added.error().message);
                }
                log::warn("scene '{}': node '{}' skipped: {}", comp->name_, label, added.error().message);
            }
        }
        for (const auto& [child, parentName] : pendingParents) {
            if (parentName == child || comp->wouldCycle(child, parentName)) {
                return fail("node '{}': parent '{}' forms a cycle", child, parentName);
            }
            if (comp->findNode(parentName) == nullptr) {
                log::warn("scene '{}': node '{}' parent '{}' not found (treated as root)", comp->name_, child,
                          parentName);
            }
            comp->findNode(child)->parent = parentName;
        }
    }
    return comp;
}

Result<std::unique_ptr<Composition>> Composition::loadNested(const std::filesystem::path& path,
                                                             assets::AssetRegistry& registry, int depth,
                                                             std::vector<std::filesystem::path> ancestors,
                                                             const nlohmann::json& nodeEdits) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("scene file not found: '{}'", path.string());
    }
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open scene file '{}'", path.string());
    }
    json j = json::parse(in, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded()) {
        return fail("scene file '{}': invalid JSON", path.string());
    }
    // ADR-330: the project's node edits, over the document, before it is parsed. A no-op for every
    // load but a project's, and for a project whose session never added or removed an object.
    const std::size_t authored = j.contains("nodes") && j["nodes"].is_array() ? j["nodes"].size() : 0;
    applyNodeEdits(j, nodeEdits);
    const std::size_t edited = j.contains("nodes") && j["nodes"].is_array() ? j["nodes"].size() : 0;
    auto comp = fromJsonImpl(j, registry, depth, std::move(ancestors), path);
    if (!comp) {
        return fail("scene file '{}': {}", path.filename().string(), comp.error().message);
    }
    if (edited != authored) {
        // Said out loud, because "the file has 80 objects and the window has 79" is the question
        // this whole mechanism exists to answer and the load is the only place that knows both.
        log::info("loaded scene '{}': {} node(s) ({} authored, {} after the project's edits)",
                  path.filename().string(), (*comp)->nodeCount(), authored, edited);
    } else {
        log::info("loaded scene '{}': {} node(s)", path.filename().string(), (*comp)->nodeCount());
    }
    return comp;
}

Result<std::unique_ptr<Composition>> Composition::loadFile(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry, int depth) {
    return loadNested(registry.resolve(path), registry, depth, {}, json());
}

Result<std::unique_ptr<Composition>> Composition::loadFile(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry,
                                                           const nlohmann::json& nodeEdits, int depth) {
    return loadNested(registry.resolve(path), registry, depth, {}, nodeEdits);
}

// ---- ADR-330: a project's node edits over the scene file it saves by reference -----------------

nlohmann::json nodeEditsAgainst(const nlohmann::json& liveNodes, const nlohmann::json& sceneDoc) {
    // A scene document this build could not read is not evidence that the session's node set is an
    // edit -- it is no evidence at all. Writing the difference against an empty list would record
    // every node in the world as an addition and bake a copy of the scene into the project, which
    // is the one outcome worse than the defect. So: no document, no record.
    if (!sceneDoc.is_object() || !liveNodes.is_array()) {
        return json();
    }
    const auto names = [](const json& nodes) {
        std::vector<std::string> out;
        if (nodes.is_array()) {
            for (const json& n : nodes) {
                if (n.is_object() && n.contains("name") && n["name"].is_string()) {
                    out.push_back(n["name"].get<std::string>());
                }
            }
        }
        return out;
    };
    const json& sceneNodes = sceneDoc.contains("nodes") ? sceneDoc.at("nodes") : json::array();
    const std::vector<std::string> onDisk = names(sceneNodes);
    const std::vector<std::string> live = names(liveNodes);
    const auto has = [](const std::vector<std::string>& v, const std::string& n) {
        return std::find(v.begin(), v.end(), n) != v.end();
    };

    json removed = json::array();
    for (const std::string& name : onDisk) {
        if (!has(live, name)) {
            removed.push_back(name);
        }
    }
    json added = json::array();
    for (const json& n : liveNodes) {
        if (!n.is_object() || !n.contains("name") || !n["name"].is_string()) {
            continue;
        }
        if (!has(onDisk, n["name"].get<std::string>())) {
            added.push_back(n);
        }
    }
    if (removed.empty() && added.empty()) {
        return json();
    }
    json edits = json::object();
    if (!removed.empty()) {
        edits["removed"] = std::move(removed);
    }
    if (!added.empty()) {
        edits["added"] = std::move(added);
    }
    return edits;
}

// ---- a project's authored lights over the scene file it saves by reference --------------------
//
// The fifth instance of the defect ADR-207, ADR-230, ADR-276 and ADR-330 each fixed once: a
// composition saved *by reference* carries only the scene's path, so anything the session authored
// and the scene file does not have is lost on save. `setAuthoredLights` gaining a UI caller is what
// makes that reachable for lights, and this is the key that makes it stick.
//
// Not `nodeEditsAgainst`'s shape, and the difference is the whole reason this exists. That function
// compares **by name**, a set difference, because a node's numbers already live in the `parameters`
// block. Lights are the opposite: most of a light's 25 fields -- `type`, `role`, `node`, `width`,
// `up` -- will never be parameters, so the list has to be copied whole, the way `effects` and
// `heroes` are, and a whole-list copy can only be decided by comparing the lists.
//
// Which forces the canonicalisation below. `authoredLightToJson` omits defaults, so comparing the
// live list against the raw document would report a scene that spells out `"intensity": 1.0` as an
// edit, and **every untouched project would start writing a `lights` key it does not need** --
// destroying the byte-stability control those four ADRs all rely on. So the scene's own list goes
// out and back through the same converters the live list came from, and only then are they compared.
Result<std::vector<Composition::AuthoredLight>> authoredLightsFromJson(const nlohmann::json& array,
                                                                       std::string_view where) {
    std::vector<Composition::AuthoredLight> out;
    if (!array.is_array()) {
        return fail("{}: 'lights' must be an array", where);
    }
    out.reserve(array.size());
    for (std::size_t i = 0; i < array.size(); ++i) {
        auto one = authoredLightFromJson(array[i], where);
        if (!one) {
            return fail("{}: lights[{}]: {}", where, i, one.error().message);
        }
        out.push_back(std::move(*one));
    }
    return out;
}

nlohmann::json Composition::authoredLightsRestJson() const {
    nlohmann::json out = nlohmann::json::array();
    for (const AuthoredLight& a : authoredLightsRest_) {
        out.push_back(authoredLightToJson(a));
    }
    return out;
}

nlohmann::json authoredLightsAgainst(const nlohmann::json& liveLights, const nlohmann::json& sceneDoc) {
    // A scene document this build could not read is not evidence that the session's lights are an
    // edit -- it is no evidence at all, and recording the difference against nothing would bake a
    // copy of the scene's lighting into the project. No document, no record. (`nodeEditsAgainst`
    // above, same reasoning, same guard.)
    if (!sceneDoc.is_object() || !liveLights.is_array()) {
        return json();
    }
    const json& sceneLights = sceneDoc.contains("lights") ? sceneDoc.at("lights") : json::array();
    json canonical = json::array();
    if (sceneLights.is_array()) {
        for (const json& e : sceneLights) {
            auto one = authoredLightFromJson(e, "scene");
            if (!one) {
                // Unreadable is the same as absent: no evidence. Refusing here rather than treating
                // the light as missing is what stops a parse failure looking like a deletion.
                return json();
            }
            canonical.push_back(authoredLightToJson(*one));
        }
    }
    if (canonical == liveLights) {
        return json();
    }
    return liveLights;
}


void applyNodeEdits(nlohmann::json& sceneDoc, const nlohmann::json& edits) {
    if (!edits.is_object() || edits.empty() || !sceneDoc.is_object()) {
        return;
    }
    if (!sceneDoc.contains("nodes") || !sceneDoc["nodes"].is_array()) {
        sceneDoc["nodes"] = json::array();
    }
    json& nodes = sceneDoc["nodes"];
    if (const auto it = edits.find("removed"); it != edits.end() && it->is_array()) {
        for (const json& entry : *it) {
            if (!entry.is_string()) {
                continue;
            }
            const std::string name = entry.get<std::string>();
            // `detachNode`'s own rule, reproduced: a child of the removed node keeps its local
            // transform under the grandparent. Dropping the entry and leaving the child naming a
            // parent that is gone would move it, and "delete a group and its contents jump" is a
            // worse bug than the one this fixes.
            std::string grandParent;
            for (const json& n : nodes) {
                if (n.is_object() && n.value("name", std::string{}) == name) {
                    grandParent = n.value("parent", std::string{});
                    break;
                }
            }
            json kept = json::array();
            for (json& n : nodes) {
                if (n.is_object() && n.value("name", std::string{}) == name) {
                    continue;
                }
                if (n.is_object() && n.value("parent", std::string{}) == name) {
                    if (grandParent.empty()) {
                        n.erase("parent");
                    } else {
                        n["parent"] = grandParent;
                    }
                }
                kept.push_back(std::move(n));
            }
            nodes = std::move(kept);
        }
    }
    // Additions after removals, so a name the session deleted and then reused is the session's
    // node and not the scene's.
    if (const auto it = edits.find("added"); it != edits.end() && it->is_array()) {
        for (const json& entry : *it) {
            if (entry.is_object()) {
                nodes.push_back(entry);
            }
        }
    }
}

Result<void> Composition::saveFile(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write scene file '{}'", path.string());
    }
    out << toJson().dump(2) << '\n';
    if (!out) {
        return fail("failed writing scene file '{}'", path.string());
    }
    return {};
}

} // namespace avgen::scene
