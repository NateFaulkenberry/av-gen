#include "scene/composition.hpp"

#include "params/timeline.hpp"

#include "core/json_keys.hpp"
#include "core/log.hpp"
#include "assets/asset_library.hpp"
#include "entity/obstacles.hpp"
#include "scene/camera.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/sky.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string_view>
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
    "worldEffects", "atmosphericEffects", "entityProfiles", "entities", "fields",
    "staging",    "graph",          "grids",          "materialPrograms", "nodes"};
constexpr std::string_view kEnvironmentKeys[] = {
    "map", "lightRig", "intensity", "fogDensity", "stylized", "rotation", "skyIntensity",
    "skyBloom", "ecologyLight", "ecologyLightRange", "ecologyGlowCell", "skybox",
    "lightFromEnvironment", "fogColor", "background", "fogHeightAmount", "styledSkyAmbient",
    "styledGroundAmbient", "styledAmbientFloor", "volumeDensity", "fogHeight", "fogHeightFalloff",
    "volumeScattering", "volumeAbsorption", "volumeAnisotropy", "volumeLocalLights", "volumeNoise",
    "volumeNoiseScale", "volumeNoiseSpeed", "volumeEmission", "volumeMaxDistance",
    "shadowCascades", "volumeSteps", "volumeDensityField", "volumeColorField", "sky"};
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
    "specularOnly", "enabled"};

std::span<const std::string_view> sceneFileKeys() { return kSceneKeys; }
std::span<const std::string_view> sceneEnvironmentKeys() { return kEnvironmentKeys; }
std::span<const std::string_view> sceneSkyKeys() { return kSkyKeys; }
std::span<const std::string_view> sceneLightKeys() { return kAuthoredLightKeys; }

namespace {
// Ecology lights are rebuilt every frame and identified by name, because the rig removes its own
// lights by resizing from the back and the two sets must not be able to eat each other.
constexpr std::string_view kEcologyLightPrefix = "ecology.glow.";
// The clustered path takes 256 lights in total (kMaxSceneLights); leave room for the rig.
constexpr std::size_t kMaxEcologyLights = 224;
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

// ---- particle JSON -----------------------------------------------------------------------------

const char* shapeName(EmitterShape shape) {
    switch (shape) {
    case EmitterShape::Point:
        return "point";
    case EmitterShape::Sphere:
        return "sphere";
    case EmitterShape::Disc:
        return "disc";
    case EmitterShape::Box:
        return "box";
    case EmitterShape::Spline:
        return "spline";
    }
    return "sphere";
}

Result<EmitterShape> shapeFromName(const std::string& name) {
    if (name == "point") {
        return EmitterShape::Point;
    }
    if (name == "sphere") {
        return EmitterShape::Sphere;
    }
    if (name == "disc") {
        return EmitterShape::Disc;
    }
    if (name == "box") {
        return EmitterShape::Box;
    }
    if (name == "spline") {
        return EmitterShape::Spline;
    }
    return fail("unknown emitter shape '{}'", name);
}

const char* blendName(ParticleBlend blend) {
    return blend == ParticleBlend::Alpha ? "alpha" : "additive";
}

Result<ParticleBlend> blendFromName(const std::string& name) {
    if (name == "additive") {
        return ParticleBlend::Additive;
    }
    if (name == "alpha") {
        return ParticleBlend::Alpha;
    }
    return fail("unknown particle blend '{}'", name);
}

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json vecToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

template <glm::length_t N>
Result<glm::vec<N, float>> readVec(const json& j, const char* key, const glm::vec<N, float>& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != static_cast<std::size_t>(N)) {
        return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
    }
    glm::vec<N, float> out{};
    for (glm::length_t i = 0; i < N; ++i) {
        const json& e = a.at(static_cast<std::size_t>(i));
        if (!e.is_number()) {
            return fail("'{}' must be an array of {} numbers", key, static_cast<int>(N));
        }
        out[i] = e.get<float>();
    }
    return out;
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number()) {
        return fail("'{}' must be a number", key);
    }
    return v.get<float>();
}

Result<int> readInt(const json& j, const char* key, int def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    return v.get<int>();
}

Result<bool> readBool(const json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    return v.get<bool>();
}

Result<std::string> readString(const json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_string()) {
        return fail("'{}' must be a string", key);
    }
    return v.get<std::string>();
}

json particlesToJson(const ParticleSystem& s) {
    json j;
    j["enabled"] = s.enabled;
    j["capacity"] = s.capacity;
    j["seed"] = s.seed;
    j["shape"] = shapeName(s.shape);
    if (!s.spline.empty()) {
        j["spline"] = s.spline;
    }
    j["position"] = vecToJson(s.position);
    j["extent"] = vecToJson(s.extent);
    j["spawnRate"] = s.spawnRate;
    j["burst"] = s.burst;
    j["lifetimeMin"] = s.lifetimeMin;
    j["lifetimeMax"] = s.lifetimeMax;
    j["direction"] = vecToJson(s.direction);
    j["spread"] = s.spread;
    j["speedMin"] = s.speedMin;
    j["speedMax"] = s.speedMax;
    j["gravity"] = vecToJson(s.gravity);
    j["drag"] = s.drag;
    j["turbulence"] = s.turbulence;
    j["turbulenceScale"] = s.turbulenceScale;
    j["turbulenceSpeed"] = s.turbulenceSpeed;
    j["attractorPosition"] = vecToJson(s.attractorPosition);
    j["attractorStrength"] = s.attractorStrength;
    j["attractorRadius"] = s.attractorRadius;
    j["orbit"] = s.orbit;
    j["sizeStart"] = s.sizeStart;
    j["sizeEnd"] = s.sizeEnd;
    j["colorStart"] = vecToJson(s.colorStart);
    j["colorEnd"] = vecToJson(s.colorEnd);
    j["emissive"] = s.emissive;
    j["blend"] = blendName(s.blend);
    j["softness"] = s.softness;
    // ADR-040. Only written when they differ from the defaults so existing files stay short and
    // round-tripping a pre-ADR-040 scene produces the same JSON it started with.
    if (s.velocityStretch != 0.0f) {
        j["velocityStretch"] = s.velocityStretch;
        j["stretchMax"] = s.stretchMax;
        j["stretchMin"] = s.stretchMin;
    }
    if (s.trailEnabled) {
        j["trailEnabled"] = true;
        j["trailLength"] = s.trailLength;
        j["trailStride"] = s.trailStride;
        j["trailWidth"] = s.trailWidth;
        j["trailTaper"] = s.trailTaper;
        j["trailFade"] = s.trailFade;
        j["trailTint"] = vecToJson(s.trailTint);
    }
    if (s.fogCoupling != 1.0f) {
        j["fogCoupling"] = s.fogCoupling;
    }
    if (s.volumeGlow != 0.0f) {
        j["volumeGlow"] = s.volumeGlow;
    }
    auto scalarCurve = [](const ParticleCurve& c) {
        json keys = json::array();
        for (const CurveKey& k : c.keys) {
            keys.push_back(json{{"t", k.t}, {"value", k.value}});
        }
        return keys;
    };
    if (!s.sizeCurve.keys.empty()) {
        j["sizeCurve"] = scalarCurve(s.sizeCurve);
    }
    if (!s.opacityCurve.keys.empty()) {
        j["opacityCurve"] = scalarCurve(s.opacityCurve);
    }
    if (!s.colorCurve.keys.empty()) {
        json keys = json::array();
        for (const ColorKey& k : s.colorCurve.keys) {
            keys.push_back(json{{"t", k.t}, {"color", vecToJson(k.color)}});
        }
        j["colorCurve"] = std::move(keys);
    }
    if (!s.fieldForces.empty()) {
        json forces = json::array();
        for (const FieldForce& f : s.fieldForces) {
            json fj;
            fj["field"] = f.field;
            fj["mode"] = fieldForceModeName(f.mode);
            fj["enabled"] = f.enabled;
            fj["strength"] = f.strength;
            fj["mix"] = f.mix;
            fj["axis"] = vecToJson(f.axis);
            forces.push_back(std::move(fj));
        }
        j["fieldForces"] = std::move(forces);
    }
    return j;
}

// Missing fields keep the ParticleSystem defaults so hand-written files can stay short.
Result<ParticleSystem> particlesFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("'particles' must be an object");
    }
    ParticleSystem s;
#define AVGEN_READ(field, reader)                                                                            \
    do {                                                                                                     \
        auto r = reader(j, #field, s.field);                                                                 \
        if (!r) {                                                                                            \
            return std::unexpected(r.error());                                                               \
        }                                                                                                    \
        s.field = *r;                                                                                        \
    } while (false)
    AVGEN_READ(enabled, readBool);
    if (j.contains("capacity")) {
        if (!j.at("capacity").is_number_unsigned()) {
            return fail("'capacity' must be a positive integer");
        }
        s.capacity = j.at("capacity").get<std::uint32_t>();
    }
    if (j.contains("seed")) {
        if (!j.at("seed").is_number_unsigned()) {
            return fail("'seed' must be a positive integer");
        }
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    if (j.contains("shape")) {
        auto name = readString(j, "shape", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto shape = shapeFromName(*name);
        if (!shape) {
            return std::unexpected(shape.error());
        }
        s.shape = *shape;
    }
    if (j.contains("spline")) {
        auto splineName = readString(j, "spline", "");
        if (!splineName) {
            return std::unexpected(splineName.error());
        }
        s.spline = *splineName;
    }
    AVGEN_READ(position, readVec<3>);
    AVGEN_READ(extent, readVec<3>);
    AVGEN_READ(spawnRate, readFloat);
    AVGEN_READ(burst, readFloat);
    AVGEN_READ(lifetimeMin, readFloat);
    AVGEN_READ(lifetimeMax, readFloat);
    AVGEN_READ(direction, readVec<3>);
    AVGEN_READ(spread, readFloat);
    AVGEN_READ(speedMin, readFloat);
    AVGEN_READ(speedMax, readFloat);
    AVGEN_READ(gravity, readVec<3>);
    AVGEN_READ(drag, readFloat);
    AVGEN_READ(turbulence, readFloat);
    AVGEN_READ(turbulenceScale, readFloat);
    AVGEN_READ(turbulenceSpeed, readFloat);
    AVGEN_READ(attractorPosition, readVec<3>);
    AVGEN_READ(attractorStrength, readFloat);
    AVGEN_READ(attractorRadius, readFloat);
    AVGEN_READ(orbit, readFloat);
    AVGEN_READ(sizeStart, readFloat);
    AVGEN_READ(sizeEnd, readFloat);
    AVGEN_READ(colorStart, readVec<4>);
    AVGEN_READ(colorEnd, readVec<4>);
    AVGEN_READ(emissive, readFloat);
    if (j.contains("blend")) {
        auto name = readString(j, "blend", "");
        if (!name) {
            return std::unexpected(name.error());
        }
        auto blend = blendFromName(*name);
        if (!blend) {
            return std::unexpected(blend.error());
        }
        s.blend = *blend;
    }
    AVGEN_READ(softness, readFloat);
    // ---- ADR-040: stretching, trails, atmosphere coupling and lifetime curves ----
    AVGEN_READ(velocityStretch, readFloat);
    AVGEN_READ(stretchMax, readFloat);
    AVGEN_READ(stretchMin, readFloat);
    AVGEN_READ(trailEnabled, readBool);
    AVGEN_READ(trailWidth, readFloat);
    AVGEN_READ(trailTaper, readFloat);
    AVGEN_READ(trailFade, readFloat);
    AVGEN_READ(trailTint, readVec<3>);
    AVGEN_READ(fogCoupling, readFloat);
    AVGEN_READ(volumeGlow, readFloat);
#undef AVGEN_READ
    for (const auto& [key, target] : {std::pair<const char*, std::uint32_t*>{"trailLength", &s.trailLength},
                                      std::pair<const char*, std::uint32_t*>{"trailStride", &s.trailStride}}) {
        if (j.contains(key)) {
            if (!j.at(key).is_number_unsigned()) {
                return fail("'{}' must be a positive integer", key);
            }
            *target = j.at(key).get<std::uint32_t>();
        }
    }
    {
        auto readScalarCurve = [&](const char* key, ParticleCurve& curve) -> Result<void> {
            if (!j.contains(key)) {
                return Result<void>{};
            }
            const json& keys = j.at(key);
            if (!keys.is_array()) {
                return fail("'{}' must be an array of keys", key);
            }
            for (const json& kj : keys) {
                if (!kj.is_object()) {
                    return fail("'{}' entries must be objects", key);
                }
                auto t = readFloat(kj, "t", 0.0f);
                auto v = readFloat(kj, "value", 0.0f);
                if (!t || !v) {
                    return fail("'{}': keys need numeric 't' and 'value'", key);
                }
                curve.keys.push_back(CurveKey{*t, *v});
            }
            return Result<void>{};
        };
        if (auto r = readScalarCurve("sizeCurve", s.sizeCurve); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = readScalarCurve("opacityCurve", s.opacityCurve); !r) {
            return std::unexpected(r.error());
        }
        if (j.contains("colorCurve")) {
            const json& keys = j.at("colorCurve");
            if (!keys.is_array()) {
                return fail("'colorCurve' must be an array of keys");
            }
            for (const json& kj : keys) {
                if (!kj.is_object()) {
                    return fail("'colorCurve' entries must be objects");
                }
                auto t = readFloat(kj, "t", 0.0f);
                auto c = readVec<3>(kj, "color", glm::vec3(1.0f));
                if (!t || !c) {
                    return fail("'colorCurve': keys need a numeric 't' and a 'color'");
                }
                s.colorCurve.keys.push_back(ColorKey{*t, *c});
            }
        }
    }
    if (auto r = validateParticleSystem(s); !r) {
        return std::unexpected(r.error());
    }
    if (j.contains("fieldForces")) {
        const json& forces = j.at("fieldForces");
        if (!forces.is_array()) {
            return fail("'fieldForces' must be an array");
        }
        if (forces.size() > static_cast<std::size_t>(kMaxFieldForces)) {
            return fail("at most {} field forces", kMaxFieldForces);
        }
        for (const json& fj : forces) {
            if (!fj.is_object()) {
                return fail("'fieldForces' entries must be objects");
            }
            FieldForce f;
            auto field = readString(fj, "field", "");
            if (!field) {
                return std::unexpected(field.error());
            }
            f.field = *field;
            if (f.field.empty()) {
                return fail("field force needs a 'field' name");
            }
            if (fj.contains("mode")) {
                auto mode = readString(fj, "mode", "force");
                if (!mode) {
                    return std::unexpected(mode.error());
                }
                auto m = fieldForceModeFromName(*mode);
                if (!m) {
                    return fail("unknown field force mode '{}'", *mode);
                }
                f.mode = *m;
            }
            auto enabled = readBool(fj, "enabled", true);
            auto strength = readFloat(fj, "strength", 1.0f);
            auto mix = readFloat(fj, "mix", 1.0f);
            auto axis = readVec<3>(fj, "axis", f.axis);
            if (!enabled || !strength || !mix || !axis) {
                return fail("field force '{}': invalid fields", f.field);
            }
            f.enabled = *enabled;
            f.strength = *strength;
            f.mix = *mix;
            f.axis = *axis;
            s.fieldForces.push_back(std::move(f));
        }
    }
    return s;
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

void offsetTextureRef(TextureRef& ref, TextureId offset) {
    if (ref.valid()) {
        ref.texture += offset;
    }
}

void offsetEntityIds(Entity& e, MeshId meshOffset, TextureId textureOffset) {
    if (e.mesh != kInvalidMesh) {
        e.mesh += meshOffset;
    }
    offsetTextureRef(e.material.baseColorTexture, textureOffset);
    offsetTextureRef(e.material.metallicRoughnessTexture, textureOffset);
    offsetTextureRef(e.material.normalTexture, textureOffset);
    offsetTextureRef(e.material.emissiveTexture, textureOffset);
    offsetTextureRef(e.material.occlusionTexture, textureOffset);
}

// ---- imported meshes as instanced sources (ADR-044) ---------------------------------------------

void offsetMaterialTextures(Material& m, TextureId textureOffset) {
    offsetTextureRef(m.baseColorTexture, textureOffset);
    offsetTextureRef(m.metallicRoughnessTexture, textureOffset);
    offsetTextureRef(m.normalTexture, textureOffset);
    offsetTextureRef(m.emissiveTexture, textureOffset);
    offsetTextureRef(m.occlusionTexture, textureOffset);
}

// ---- an asset's sub-materials (ADR-044) --------------------------------------------------------
//
// An asset's entities are grouped by material and each group merged into one instanceable mesh,
// baked by each entity's own transform. Instancing wants a single mesh *per draw*, not per asset:
// merging everything into one mesh forced one material onto the whole thing, so a tree drew its
// leaves with the bark's texture and a mushroom's cap took its stem's colour. One part per material
// is one procedural object per material, which is one draw per material -- everything downstream
// (instancing, culling, LOD, variation) is untouched, because each part is an ordinary object.
//
// Parts come back ordered by surface area, largest first, so part 0 is the material a viewer reads
// as the asset's own. Area rather than vertex count: a trunk is a smooth tube carrying plenty of
// vertices for very little of what you see, while a canopy is hundreds of small leaf cards.
struct AssetPart {
    std::shared_ptr<const MeshData> mesh;
    Material material;
    bool hasMaterial = false;
    double area = 0.0;
    std::size_t firstEntity = 0; // for a stable order when two parts have the same area
    // The name the asset gave this material, when it gave one. A label, never a grouping key:
    // grouping stays by value so two materials that shade identically keep sharing a draw. What
    // the name is for is addressing -- "parts/Blue/emissiveGain" instead of an index that is an
    // internal ordering by surface area and changes the day someone edits the model.
    std::string name;
};

bool sameMaterial(const TextureRef& a, const TextureRef& b) {
    return a.texture == b.texture && a.uvSet == b.uvSet && a.wrapU == b.wrapU && a.wrapV == b.wrapV &&
           a.linearFilter == b.linearFilter;
}

bool sameMaterial(const Material& a, const Material& b) {
    return a.baseColor == b.baseColor && a.opacity == b.opacity && a.emissiveColor == b.emissiveColor &&
           a.emissiveIntensity == b.emissiveIntensity && a.roughness == b.roughness &&
           a.metallic == b.metallic && a.normalScale == b.normalScale &&
           a.occlusionStrength == b.occlusionStrength && a.alphaMode == b.alphaMode &&
           a.alphaCutoff == b.alphaCutoff && a.doubleSided == b.doubleSided && a.unlit == b.unlit &&
           a.program == b.program && sameMaterial(a.baseColorTexture, b.baseColorTexture) &&
           sameMaterial(a.metallicRoughnessTexture, b.metallicRoughnessTexture) &&
           sameMaterial(a.normalTexture, b.normalTexture) &&
           sameMaterial(a.emissiveTexture, b.emissiveTexture) &&
           sameMaterial(a.occlusionTexture, b.occlusionTexture);
}

double meshAreaUnder(const MeshData& mesh, const glm::mat4& model) {
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
            continue;
        }
        const glm::vec3 pa = glm::vec3(model * glm::vec4(mesh.vertices[a].position, 1.0f));
        const glm::vec3 pb = glm::vec3(model * glm::vec4(mesh.vertices[b].position, 1.0f));
        const glm::vec3 pc = glm::vec3(model * glm::vec4(mesh.vertices[c].position, 1.0f));
        area += 0.5 * static_cast<double>(glm::length(glm::cross(pb - pa, pc - pa)));
    }
    return area;
}

std::vector<AssetPart> assetMaterialParts(const assets::SceneAsset& asset) {
    struct Group {
        Material material;
        std::vector<std::size_t> entities;
        double area = 0.0;
        std::size_t firstEntity = 0;
    };
    std::vector<Group> groups;
    for (std::size_t e = 0; e < asset.scene.entities.size(); ++e) {
        const Entity& entity = asset.scene.entities[e];
        if (!entity.visible || entity.mesh == kInvalidMesh || entity.mesh >= asset.scene.meshes.size()) {
            continue;
        }
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&](const Group& g) { return sameMaterial(g.material, entity.material); });
        if (it == groups.end()) {
            groups.push_back(Group{entity.material, {}, 0.0, e});
            it = std::prev(groups.end());
        }
        it->entities.push_back(e);
        it->area += meshAreaUnder(asset.scene.meshes[entity.mesh], entity.transform.matrix());
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        return a.area != b.area ? a.area > b.area : a.firstEntity < b.firstEntity;
    });

    std::vector<AssetPart> parts;
    parts.reserve(groups.size());
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto merged = std::make_shared<MeshData>();
        merged->name = groups.size() > 1 ? fmt::format("{}#{}", asset.path.stem().string(), g)
                                         : asset.path.stem().string();
        for (const std::size_t e : groups[g].entities) {
            const Entity& entity = asset.scene.entities[e];
            const MeshData& src = asset.scene.meshes[entity.mesh];
            const glm::mat4 model = entity.transform.matrix();
            const glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));
            const auto base = static_cast<std::uint32_t>(merged->vertices.size());
            merged->vertices.reserve(merged->vertices.size() + src.vertices.size());
            for (const Vertex& v : src.vertices) {
                Vertex out = v;
                out.position = glm::vec3(model * glm::vec4(v.position, 1.0f));
                const glm::vec3 n = normalMatrix * v.normal;
                out.normal = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : v.normal;
                merged->vertices.push_back(out);
            }
            merged->indices.reserve(merged->indices.size() + src.indices.size());
            for (const std::uint32_t index : src.indices) {
                merged->indices.push_back(base + index);
            }
        }
        parts.push_back(AssetPart{std::move(merged), groups[g].material, true, groups[g].area,
                                  groups[g].firstEntity,
                                  asset.scene.entities[groups[g].firstEntity].materialName});
    }
    if (parts.empty()) {
        // Nothing drawable in the asset. One empty part keeps every caller on one code path, and
        // the object ends up with no mesh exactly as it did before.
        parts.push_back(AssetPart{std::make_shared<MeshData>(), Material{}, false, 0.0, 0, {}});
    }
    return parts;
}

// The bounds of the whole asset, over every part. A layer that normalises an asset to a height
// must scale every part by the same factor, or a tree's leaves come off its trunk.
std::pair<glm::vec3, glm::vec3> partsBounds(const std::vector<AssetPart>& parts) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const AssetPart& part : parts) {
        if (!part.mesh || part.mesh->vertices.empty()) {
            continue;
        }
        const auto [plo, phi] = part.mesh->bounds();
        lo = glm::min(lo, plo);
        hi = glm::max(hi, phi);
        any = true;
    }
    return any ? std::pair{lo, hi} : std::pair{glm::vec3(0.0f), glm::vec3(0.0f)};
}

// A triangle budget authored for the whole asset, shared out between its parts in proportion to
// how many triangles each one has. Giving every part the whole budget would multiply it by the
// part count, which is how a "10k triangle" tree becomes 20k the moment it grows a second material.
int partBudget(const std::vector<AssetPart>& parts, std::size_t index, int assetBudget) {
    if (assetBudget <= 0 || parts.size() <= 1) {
        return assetBudget;
    }
    std::size_t total = 0;
    for (const AssetPart& part : parts) {
        total += part.mesh ? part.mesh->indices.size() / 3 : 0;
    }
    const std::size_t mine = parts[index].mesh ? parts[index].mesh->indices.size() / 3 : 0;
    if (total == 0 || mine == 0) {
        return assetBudget;
    }
    const auto share = static_cast<double>(assetBudget) * static_cast<double>(mine) / static_cast<double>(total);
    return std::max(1, static_cast<int>(std::lround(share)));
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
Result<void> Composition::setWorldEffects(std::vector<world::WorldEffect> effects) {
    // The whole set or none of it (ADR-207, and the same rule `setHeroes` follows): an effect whose
    // name collides with another's is two things writing one parameter path, and an effect dropped
    // for being invalid is an effect that never fires with nothing saying why.
    if (auto ok = world::validateWorldEffects(effects); !ok) {
        return ok;
    }
    worldEffects_ = std::move(effects);
    // Deliberately no `dirty_`: a world effect places nothing, occludes nothing and is not an
    // obstacle, so there is nothing for a flatten to do. What reads them is the Engine, per frame.
    return {};
}

// ADR-230, on exactly the terms `setWorldEffects` states above: the whole set or none of it, and a
// duplicate name refused because a name is half of a parameter path. No `dirty_` for the same reason
// again -- an aurora places nothing and occludes nothing.
Result<void> Composition::setAtmosphericEffects(std::vector<world::AtmosphericEffect> effects) {
    if (auto ok = world::validateAtmosphericEffects(effects); !ok) {
        return ok;
    }
    atmosphericEffects_ = std::move(effects);
    return {};
}

// ADR-278. The whole set or none of it, for the reason `setWorldEffects` states: a duplicate name is
// two lights that cannot be told apart in a warning, in an overlay or in a parameter path. Unlike
// the effects this *is* dirty -- a light is part of the picture, and `rebuild` is where the picture
// is assembled.
Result<void> Composition::setAuthoredLights(std::vector<AuthoredLight> lights) {
    for (std::size_t i = 0; i < lights.size(); ++i) {
        if (lights[i].light.name.empty()) {
            return fail("lights[{}]: a light needs a name", i);
        }
        for (std::size_t k = 0; k < i; ++k) {
            if (lights[k].light.name == lights[i].light.name) {
                return fail("lights[{}]: duplicate light name '{}'", i, lights[i].light.name);
            }
        }
    }
    authoredLights_ = std::move(lights);
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

void Composition::syncHeroesToNodes() {
    if (heroes_.empty()) {
        return;
    }
    heroAnchors_.resize(heroes_.size());
    bool moved = false;
    for (std::size_t i = 0; i < heroes_.size(); ++i) {
        const CompositionNode* node = findNode(heroes_[i].name);
        if (node == nullptr) {
            heroAnchors_[i].reset();
            continue;
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
    *it = value;
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
        bindings.push_back(std::move(binding));
    }
    // Heroes are landmarks too: "look at the elder" is the natural thing for an author to write,
    // and a hero's declared height is what makes a character look at its crown rather than its
    // roots.
    for (const world::HeroPoint& hero : heroes_) {
        landmarks.emplace_back(hero.name, hero.position + glm::vec3(0.0f, hero.height * 0.5f, 0.0f));
    }
    entityWorld_.setBindings(std::move(bindings));
    entityWorld_.setLandmarks(std::move(landmarks));

    // The ground an entity walks on is the ground the terrain was built from -- the same WorldMap
    // and the same ecology -- so a walker can never be above or below the surface it is standing
    // on, and never needs a second description of it kept in step by hand.
    entityWorld_.setNavigator(buildNavigator());
    entityWorld_.setExtraInterestPoints(glowInterestPoints());

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
        field.heroes = heroes_;
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
void Composition::AnimationSink::driveLayers(const entity::LocomotionState& state) {
    CompositionNode* node = owner_.findNode(node_);
    if (node == nullptr || node->rigs.empty()) {
        return;
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
    glm::vec3 localTarget(0.0f);
    bool haveTarget = false;
    glm::vec3 localGround(0.0f);
    glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
    bool haveGround = false;
    if (state.hasLookTarget || state.hasGroundPlane) {
        const glm::mat4 world = owner_.nodeWorldTransform(*node).matrix();
        const glm::mat4 inverse = glm::inverse(world);
        if (state.hasLookTarget) {
            localTarget = glm::vec3(inverse * glm::vec4(state.lookTarget, 1.0f));
            haveTarget = true;
        }
        if (state.hasGroundPlane) {
            // ADR-344. The point goes through the inverse like any point. The **normal does not**:
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
    for (const RigId id : node->rigs) {
        if (id >= owner_.scene_.rigs.size()) {
            continue;
        }
        for (PoseLayer& layer : owner_.scene_.rigs[id].layers.layers()) {
            switch (layer.drive) {
            case PoseLayerDrive::Manual:
                break;
            case PoseLayerDrive::Look:
                layer.weight = haveTarget ? 1.0f : 0.0f;
                layer.target = localTarget;
                layer.hasTarget = haveTarget;
                break;
            case PoseLayerDrive::Reaction:
                layer.weight = reaction;
                break;
            case PoseLayerDrive::Ground:
                // The weight is the *grounded* bit and not a blend: a body standing on something
                // gets its feet planted, and a body in a tractor beam does not. A scene that wants
                // the correction eased in says so with a fade on the layer it authors, which is a
                // decision about staging rather than one this seam may make for it.
                layer.weight = haveGround ? 1.0f : 0.0f;
                layer.groundPoint = localGround;
                layer.groundNormal = localNormal;
                layer.hasGround = haveGround;
                break;
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
        staging_.update(stageCtx);
    }
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
    entityWorld_.update(update, *params_);
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
    // ADR-055. Speed 0 is a genuine no-op: `WindParams::active()` is false and every draw's wind
    // gate goes to 0, which is also how the A/B measurement of what this costs is taken.
    windSpeed_ = &params.add(floatDesc(prefix_ + "scene/windSpeed", windSetting_.speed, 0.0f, 4.0f, 0.0f, 1.5f));
    windDirection_ = &params.add(
        floatDesc(prefix_ + "scene/windDirection", windSetting_.direction, -6.2832f, 6.2832f, -3.1416f, 3.1416f));
    // Volumetric atmosphere (ADR-032). volumeDensity 0 keeps the pass off, so these are free
    // until someone turns them up; every one is an ordinary parameter, so audio, the timeline,
    // presets, OSC/MIDI and macros drive fog through the usual routes.
    volumeDensity_ = &params.add(
        floatDesc(prefix_ + "scene/volumeDensity", volumeSetting_.volumeDensity, 0.0f, 2.0f, 0.0f, 0.2f));
    fogHeight_ = &params.add(floatDesc(prefix_ + "scene/fogHeight", volumeSetting_.fogHeight, -1e4f, 1e4f,
                                       -20.0f, 40.0f));
    fogHeightFalloff_ = &params.add(floatDesc(prefix_ + "scene/fogHeightFalloff", volumeSetting_.fogHeightFalloff,
                                              0.0f, 10.0f, 0.0f, 1.0f));
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
    gridIntensity_ = &params.add(floatDesc(prefix_ + "scene/gridIntensity", 0.6f, 0.0f, 4.0f, 0.0f, 2.0f));
    rootScale_ = &params.add(floatDesc(prefix_ + "root/scale", 1.0f, 0.05f, 8.0f, 0.2f, 3.0f));
    // Nested compositions do not spin on their own by default; the enclosing root does.
    rootRotationSpeed_ = &params.add(
        floatDesc(prefix_ + "root/rotationSpeed", 0.0f, -20.0f, 20.0f, -3.0f, 3.0f)); // worlds do not spin by default
    rootImpulse_ = &params.add(floatDesc(prefix_ + "root/impulse", 0.0f, 0.0f, 4.0f, 0.0f, 1.0f));
    if (lightRig_ && lightRigParams_.all.empty()) {
        lightRigParams_ = registerLightRigParameters(params, *lightRig_, prefix_);
    }
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
    // The lights the node's asset brought with it. A scale and a tint rather than absolute values,
    // because the asset's own numbers are the authored starting point and a scene should not have to
    // restate them to dim one lamp.
    node.lightIntensityParam =
        &params_->add(floatDesc(base + "lightIntensity", 1.0f, 0.0f, 20.0f, 0.0f, 4.0f));
    node.lightColorParam = &params_->add(vec3Desc(base + "lightColor", glm::vec3(1.0f), 0.0f, 4.0f, 0.0f, 1.0f));
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
    }
    if (node.kind == NodeKind::Scene && node.child) {
        node.child->attach(*params_, *modulator_, base);
    }
}

void Composition::unregisterNodeParameters(CompositionNode& node) {
    if (params_ != nullptr) {
        const std::string base = prefix_ + "nodes/" + node.name + "/";
        for (const char* suffix :
             {"position", "rotation", "scale", "visible", "emissiveBoost", "roughnessScale"}) {
            params_->remove(base + suffix);
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
    node.particleParams = {};
}

void Composition::unregisterParameters() {
    if (params_ != nullptr) {
        for (const char* path : {"camera/distance", "camera/height", "camera/orbitSpeed", "camera/fov",
                                 "camera/splineT", "camera/lookAhead", "camera/splineOffset",
                                 "env/intensity", "env/rotation", "scene/brightness", "scene/gridIntensity", "scene/stylized",
                                 "env/sky/enabled", "env/sky/background", "env/sky/zenithColor",
                                 "env/sky/horizonColor", "env/sky/groundColor", "env/sky/sunColor",
                                 "env/sky/haze", "env/sky/sunIntensity", "env/sky/sunSize",
                                 "env/sky/sunGlow", "env/sky/intensity",
                                 "root/scale", "root/rotationSpeed", "root/impulse"}) {
            params_->remove(prefix_ + path);
        }
        if (!lightRigParams_.all.empty()) {
            unregisterLightRigParameters(*params_, lightRigParams_);
            lightRigParams_ = {};
        }
        for (const auto& mp : materialParams_) {
            unregisterMaterialProgramParameters(*params_, mp);
        }
        materialParams_.clear();
        for (auto& node : nodes_) {
            unregisterNodeParameters(*node);
        }
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
        node->lightIntensityParam = nullptr;
        node->lightColorParam = nullptr;
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
    windSpeed_ = nullptr;
    windDirection_ = nullptr;
    volumeScattering_ = nullptr;
    volumeAbsorption_ = nullptr;
    volumeAnisotropy_ = nullptr;
    volumeNoise_ = nullptr;
    volumeNoiseScale_ = nullptr;
    volumeNoiseSpeed_ = nullptr;
    volumeEmission_ = nullptr;
    volumeSteps_ = nullptr;
    keyLight_ = nullptr;
    gridIntensity_ = nullptr;
    rootScale_ = nullptr;
    rootRotationSpeed_ = nullptr;
    rootImpulse_ = nullptr;
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
                if (!node.animation.layers.empty()) {
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
                pg.name = sanitise(prefix_) + node.name + "_" + layer.name;
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
            entity::obstaclesFromHeroes(heroes_, *obstacles_);
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

    scene_.environment.environmentMap = kInvalidTexture;
    if (!environmentPath_.empty()) {
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
            if (rig.player.play(node.animation.state, node.animationAppliedAt, blend)) {
                if (node.animationRebase) {
                    // play() returns true without restarting a state it is already in, which is
                    // exactly right for a behaviour and exactly wrong for a timeline cue (ADR-089).
                    rig.player.restart(node.animationAppliedAt);
                }
                // setSpeed rebases to keep local clip time continuous; called at the phase origin
                // the elapsed time is zero, so the origin survives.
                rig.player.setSpeed(node.animation.speed, node.animationAppliedAt);
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

bool Composition::setNodeAnimation(const std::string& nodeName, const std::string& state, double now,
                                   float blend, float speed, bool rebase) {
    CompositionNode* node = findNode(nodeName);
    if (node == nullptr) {
        return false;
    }
    // Idempotent, so a sequencer may call this every frame: the same state at the same phase origin
    // with the same rate is already in force and re-pushing it would restart the cross-fade.
    if (node->animationPushed && node->animation.state == state && node->animationApplied == state &&
        node->animationAppliedAt == now && node->animation.blend == blend &&
        node->animation.speed == speed && node->animationRebase == rebase) {
        return true;
    }
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

        const Transform nodeT = nodeWorldTransform(node);
        const bool visible = nodeVisible(node);
        const float emissiveBoost =
            node.emissiveParam != nullptr ? node.emissiveParam->value() : node.emissiveBoost;
        const float roughnessScale =
            node.roughnessParam != nullptr ? node.roughnessParam->value() : node.roughnessScale;
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
    // ---- the viewport may stand the director down ------------------------------------------------
    //
    // **Applied to the resolver's answer, never inside it.** `resolveActiveCamera` is a pure
    // function of (cameras, shots, events, time) and ADR-091 rests on that: it is what makes a cut
    // scrubbable and an offline render identical to the live one. Feeding an editor's navigation
    // state into it would make the film depend on where somebody had flown the viewport, which is
    // the opposite of the property it exists to have.
    //
    // So free-roam is expressed as an override *on the result*: the director still decides what the
    // film does, `activeCamera_` still reports it truthfully -- the Cameras panel keeps saying which
    // camera is live and why -- and only the pose written into `scene_.camera` is taken from the
    // main camera instead. An offline render never sets this, so the deliverable is unchanged.
    //
    // Why the *main* camera rather than a fourth kind of pose: it already is the free-roam camera.
    // Before ADR-245 there was one camera and flying the viewport moved it; `camera/position` is
    // still what `W`/`A`/`S`/`D` and every viewport drag write. Pointing free-roam back at it gets
    // the old behaviour exactly rather than a reimplementation of it.
    if (viewportFreeRoam_ && activeCamera_.camera != kMainCamera) {
        activeCamera_.camera = kMainCamera;
        activeCamera_.previous = kNoCamera;   // a blend from a camera the viewport is not showing
        activeCamera_.blend = 0.0f;           // would slide the free-roam view across the screen
        activeCamera_.reason = ActiveCameraReason::Default;
        activeCamera_.name = "Viewport";
        activeCamera_.eventName.clear();
    }
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
        env.volumeScattering = pick(volumeScattering_, volumeSetting_.volumeScattering);
        env.volumeAbsorption = pick(volumeAbsorption_, volumeSetting_.volumeAbsorption);
        env.volumeAnisotropy = pick(volumeAnisotropy_, volumeSetting_.volumeAnisotropy);
        env.volumeNoiseAmount = pick(volumeNoise_, volumeSetting_.volumeNoiseAmount);
        env.volumeNoiseScale = pick(volumeNoiseScale_, volumeSetting_.volumeNoiseScale);
        env.volumeNoiseSpeed = pick(volumeNoiseSpeed_, volumeSetting_.volumeNoiseSpeed);
        env.volumeEmission = pick(volumeEmission_, volumeSetting_.volumeEmission);
        env.volumeSteps = volumeSteps_ != nullptr ? volumeSteps_->value() : volumeSetting_.volumeSteps;
        env.shadowCascades = volumeSetting_.shadowCascades;
        env.volumeMaxDistance = volumeSetting_.volumeMaxDistance;
        // Field names are prefixed like every other reference so a nested scene stays self-contained.
        env.volumeDensityField =
            volumeDensityFieldSetting_.empty() ? std::string() : sanitise(prefix_) + volumeDensityFieldSetting_;
        env.volumeColorField =
            volumeColorFieldSetting_.empty() ? std::string() : sanitise(prefix_) + volumeColorFieldSetting_;
        // ADR-055. A live `scene/windSpeed` parameter so the whole field can be turned up, down or
        // off without editing the file -- which is also how the A/B measurement is taken.
        // ADR-058: the distance fog's share of the mist layer and the styled hemisphere travel with
        // the rest of the atmosphere; nothing about them is animated, so they are copied, not picked.
        env.fogHeightAmount = volumeSetting_.fogHeightAmount;
        env.styledSkyAmbient =
            styledSkyAmbient_ != nullptr ? styledSkyAmbient_->value() : volumeSetting_.styledSkyAmbient;
        env.styledGroundAmbient = styledGroundAmbient_ != nullptr ? styledGroundAmbient_->value()
                                                                  : volumeSetting_.styledGroundAmbient;
        env.styledAmbientFloor = volumeSetting_.styledAmbientFloor;
        env.wind = windSetting_;
        env.wind.speed = windSpeed_ != nullptr ? windSpeed_->value() : windSetting_.speed;
        env.wind.direction = windDirection_ != nullptr ? windDirection_->value() : windSetting_.direction;
    }
    if (gridIntensity_ != nullptr) {
        scene_.environment.gridIntensity = gridIntensity_->value();
    }
    scene_.environment.environmentIntensity =
        envIntensity_ != nullptr ? envIntensity_->value() : envIntensitySetting_;
    scene_.environment.environmentRotation =
        envRotation_ != nullptr ? envRotation_->value() : envRotationSetting_;
    // ADR-049: the visible sky's own two controls, independent of the shading intensity above.
    scene_.environment.showSkybox = showSkyboxSetting_;
    scene_.environment.skyIntensity = skyIntensitySetting_;
    scene_.environment.skyBloom = skyBloomSetting_;
    scene_.environment.lightFromEnvironment = lightFromEnvironmentSetting_;
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
    dirty_ = true;
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
        if (v.fogHeightAmount != envDefaults.fogHeightAmount) {
            environment["fogHeightAmount"] = v.fogHeightAmount;
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
            environment["volumeScattering"] = base(volumeScattering_, volumeSetting_.volumeScattering);
            environment["volumeAbsorption"] = base(volumeAbsorption_, volumeSetting_.volumeAbsorption);
            environment["volumeAnisotropy"] = base(volumeAnisotropy_, volumeSetting_.volumeAnisotropy);
            environment["volumeLocalLights"] = volumeSetting_.volumeLocalLights;
            environment["volumeNoise"] = base(volumeNoise_, volumeSetting_.volumeNoiseAmount);
            environment["volumeNoiseScale"] = base(volumeNoiseScale_, volumeSetting_.volumeNoiseScale);
            environment["volumeNoiseSpeed"] = base(volumeNoiseSpeed_, volumeSetting_.volumeNoiseSpeed);
            environment["volumeEmission"] = base(volumeEmission_, volumeSetting_.volumeEmission);
            environment["volumeSteps"] = volumeSteps_ != nullptr ? volumeSteps_->base() : volumeSetting_.volumeSteps;
            environment["shadowCascades"] = volumeSetting_.shadowCascades;
            environment["volumeMaxDistance"] = volumeSetting_.volumeMaxDistance;
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
    if (windSetting_.enabled) {
        json w = wind::windToJson(windSetting_);
        w["speed"] = windSpeed_ != nullptr ? windSpeed_->base() : windSetting_.speed;
        w["direction"] = windDirection_ != nullptr ? windDirection_->base() : windSetting_.direction;
        j["wind"] = std::move(w);
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
        for (const world::HeroPoint& hero : heroes_) {
            heroes.push_back(hero.toJson());
        }
        j["heroes"] = std::move(heroes);
    }
    // ADR-207. Written only when there are effects, so every scene that never declared one writes
    // back exactly the file it had.
    if (!worldEffects_.empty()) {
        json effects = json::array();
        for (const world::WorldEffect& effect : worldEffects_) {
            effects.push_back(effect.toJson());
        }
        j["worldEffects"] = std::move(effects);
    }
    // ADR-230. Written only when there are effects, so every scene that never declared one writes
    // back exactly the file it had.
    if (!atmosphericEffects_.empty()) {
        json atmospherics = json::array();
        for (const world::AtmosphericEffect& effect : atmosphericEffects_) {
            atmospherics.push_back(effect.toJson());
        }
        j["atmosphericEffects"] = std::move(atmospherics);
    }
    if (!profileLibraryPath_.empty()) {
        j["entityProfiles"] = profileLibraryPath_;
    }
    if (!entityDescs_.empty()) {
        j["entities"] = entity::entitiesToJson(entityDescs_);
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
                                      FloatKey{"volumeScattering", &v.volumeScattering},
                                      FloatKey{"volumeAbsorption", &v.volumeAbsorption},
                                      FloatKey{"volumeAnisotropy", &v.volumeAnisotropy},
                                      FloatKey{"volumeLocalLights", &v.volumeLocalLights},
                                      FloatKey{"volumeNoise", &v.volumeNoiseAmount},
                                      FloatKey{"volumeNoiseScale", &v.volumeNoiseScale},
                                      FloatKey{"volumeNoiseSpeed", &v.volumeNoiseSpeed},
                                      FloatKey{"volumeEmission", &v.volumeEmission},
                                      FloatKey{"volumeMaxDistance", &v.volumeMaxDistance}}) {
                auto value = readFloat(e, fk.key, *fk.target);
                if (!value) {
                    return std::unexpected(value.error());
                }
                *fk.target = *value;
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
    // ADR-207: the world effects. Read after the heroes, because a `hero` source names one and a
    // reader who sees the effects first cannot say whether that name is real.
    if (j.contains("worldEffects")) {
        const json& effectsJson = j.at("worldEffects");
        if (!effectsJson.is_array()) {
            return fail("scene file '{}': 'worldEffects' must be an array", scenePath.string());
        }
        std::vector<world::WorldEffect> effects;
        effects.reserve(effectsJson.size());
        for (std::size_t i = 0; i < effectsJson.size(); ++i) {
            auto effect = world::WorldEffect::fromJson(effectsJson[i]);
            if (!effect) {
                return fail("scene file '{}': worldEffects[{}]: {}", scenePath.string(), i,
                            effect.error().message);
            }
            effects.push_back(std::move(*effect));
        }
        if (auto ok = comp->setWorldEffects(std::move(effects)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
        }
    }
    // ADR-230: the atmospheric effects. Read beside the world effects and on the same terms -- a
    // malformed one is an error rather than a silent omission, because an effect that does not load
    // is an effect that never fires with nothing saying why.
    if (j.contains("atmosphericEffects")) {
        const json& atmosJson = j.at("atmosphericEffects");
        if (!atmosJson.is_array()) {
            return fail("scene file '{}': 'atmosphericEffects' must be an array", scenePath.string());
        }
        std::vector<world::AtmosphericEffect> atmospherics;
        atmospherics.reserve(atmosJson.size());
        for (std::size_t i = 0; i < atmosJson.size(); ++i) {
            auto effect = world::AtmosphericEffect::fromJson(atmosJson[i]);
            if (!effect) {
                return fail("scene file '{}': atmosphericEffects[{}]: {}", scenePath.string(), i,
                            effect.error().message);
            }
            atmospherics.push_back(std::move(*effect));
        }
        if (auto ok = comp->setAtmosphericEffects(std::move(atmospherics)); !ok) {
            return fail("scene file '{}': {}", scenePath.string(), ok.error().message);
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
                    "farHz", "cullDistance", "layers", "rootMotion"};
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
                            return fail("node '{}': animation layer '{}': unknown kind '{}' (aim, additive)",
                                        node.name, layer.name, *kind);
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
                        // ADR-344: a foot layer is addressed by a chain and never by a mask, so it
                        // reads a different set of keys and refuses a mask outright here rather
                        // than letting `bind` report a no-op after the scene has loaded.
                        if (layer.kind == PoseLayerKind::Foot) {
                            if (!entry.contains("chain")) {
                                return fail("node '{}': animation layer '{}': a foot layer needs a 'chain' "
                                            "of exactly three joint names -- the hip, the knee and the foot",
                                            node.name, layer.name);
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
                            if (!align) return std::unexpected(align.error());
                            if (!offset) return std::unexpected(offset.error());
                            if (!reach) return std::unexpected(reach.error());
                            layer.footAlign = *align;
                            layer.groundOffset = *offset;
                            layer.extension = *reach;
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
                        } else if (layer.mask.joints.empty()) {
                            return fail("node '{}': animation layer '{}' masks no joints, so it could only "
                                        "ever do nothing",
                                        node.name, layer.name);
                        }
                        for (const auto& key : entry.items()) {
                            static constexpr std::array<std::string_view, 19> kLayerKeys{
                                "name",         "kind",     "drive",         "joints",
                                "weights",      "descendants", "pivot",      "forward",
                                "maxYaw",       "maxPitch", "clip",          "clipRate",
                                "weight",       "chain",    "poleDirection", "footAlign",
                                "groundOffset", "extension", "soleUp"};
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
