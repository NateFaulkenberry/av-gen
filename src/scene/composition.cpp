#include "scene/composition.hpp"

#include "core/log.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/sky.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace avgen::scene {

namespace {
// Ecology lights are rebuilt every frame and identified by name, because the rig removes its own
// lights by resizing from the back and the two sets must not be able to eat each other.
constexpr std::string_view kEcologyLightPrefix = "ecology.glow.";
// The clustered path takes 256 lights in total (kMaxSceneLights); leave room for the rig.
constexpr std::size_t kMaxEcologyLights = 224;
constexpr int kMaxTerrainLodIndex = world::kMaxTerrainLods - 1;
} // namespace

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

glm::quat quatFromEulerDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

// Inverse of glm::quat(vec3): that constructor builds Rz * Ry * Rx. glm::eulerAngles recovers
// the middle angle with asin, which loses precision near +-90 degrees; atan2 does not.
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
    }
    return glm::degrees(glm::vec3(x, y, z));
}

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
    }
    return "gltf";
}

Result<NodeKind> nodeKindFromName(const std::string& name) {
    for (const NodeKind kind :
         {NodeKind::Gltf, NodeKind::Orb, NodeKind::Grid, NodeKind::Particles, NodeKind::Scene, NodeKind::Procedural,
          NodeKind::Field, NodeKind::Spline, NodeKind::Sdf, NodeKind::Terrain}) {
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

// ---- interactive rebuild policy (docs/application-performance.md) -------------------------------
//
// Pure, so it can be reasoned about and tested without a scene, a clock or a GPU. `state` is the
// object's record, `wanted` the hash its inputs currently ask for, `built` the hash it is actually
// at, `elapsedMs` the wall time since the last poll. Returns true when the caller should regenerate
// now.
//
// The shape of it: an object whose inputs just moved restarts a settle timer, so a drag -- which
// moves them every frame -- never reaches the regeneration until it stops. Once they hold still for
// kSettleMs it regenerates. And because a drag can go on for seconds, there is a ceiling on how
// long it may be held back, scaled by what the object costs: something costing 150 ms refreshes at
// most every 600 ms while the drag continues, something costing 5 ms every 90, so no object spends
// more than about a fifth of the time regenerating. A fixed ceiling would either starve the cheap
// objects or hand the expensive ones the frame back again.
bool advanceRebuildDeferral(Composition::ProceduralRebuildState& state, std::uint64_t wanted,
                            std::uint64_t built, double elapsedMs) {
    // About five frames at 60 Hz: short enough that letting go of a slider feels immediate, long
    // enough that a drag cannot sneak a regeneration in between two of its own frames.
    constexpr double kSettleMs = 90.0;
    constexpr double kCostMultiple = 4.0;

    if (wanted == built) {
        state.deferring = false;
        state.settledForMs = 0.0;
        state.heldForMs = 0.0;
        return false;
    }
    if (!state.deferring) {
        state.deferring = true;
        state.deferredHash = wanted;
        state.settledForMs = 0.0;
        state.heldForMs = 0.0;
        return false;
    }
    state.heldForMs += elapsedMs;
    if (wanted != state.deferredHash) {
        state.deferredHash = wanted; // the inputs moved again: still wrong, but not yet still
        state.settledForMs = 0.0;
    } else {
        state.settledForMs += elapsedMs;
    }
    const double ceilingMs = std::max(kSettleMs, state.lastMs * kCostMultiple);
    if (state.settledForMs < kSettleMs && state.heldForMs < ceilingMs) {
        return false;
    }
    state.deferring = false;
    state.settledForMs = 0.0;
    state.heldForMs = 0.0;
    return true;
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
        binding.anchor = nodeWorldTransform(node).position;
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

    entityWorld_.registerParameters(*params_, prefix_ + "entity/");
    entityWorld_.bind(*params_, prefix_ + "entity/");

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
        return entity::Navigator(&nodePtr->worldMap, field);
    }
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
    const std::string& want = entity_.clipFor(state.activity);
    if (want.empty()) {
        return; // this entity declared no clips: it drives a craft or a prop, not a character
    }
    // Unconditional every frame: the player treats a request for the state it is already in as a
    // no-op rather than a restart, so "what should be playing now" is the only thing a behaviour
    // has to know. The timeline second rather than a wall clock is what keeps an offline render
    // reproducible (ADR-086).
    owner_.setNodeAnimation(node_, want, state.time);
}

void Composition::cullEntityNodes() {
    if (entityWorld_.empty() || viewportHeight_ == 0) {
        return;
    }
    const float aspect = static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_);
    const world::FrustumPlanes planes = world::frustumPlanes(scene_.camera.projection(aspect) * scene_.camera.view());
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
                const auto [lo, hi] = scene_.meshes[e.mesh].bounds();
                // The mesh's bounds are its bind pose; a posed skeleton can reach outside them, so
                // pad by a quarter of the box before testing. A character culled one frame early
                // is a character that pops.
                const glm::vec3 pad = (hi - lo) * 0.25f + glm::vec3(0.25f);
                const glm::mat4 m = e.transform.matrix();
                glm::vec3 wlo(std::numeric_limits<float>::max());
                glm::vec3 whi(std::numeric_limits<float>::lowest());
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 p((corner & 1) ? hi.x + pad.x : lo.x - pad.x,
                                      (corner & 2) ? hi.y + pad.y : lo.y - pad.y,
                                      (corner & 4) ? hi.z + pad.z : lo.z - pad.z);
                    const glm::vec3 w = glm::vec3(m * glm::vec4(p, 1.0f));
                    wlo = glm::min(wlo, w);
                    whi = glm::max(whi, w);
                }
                e.cameraCulled = !world::aabbVisible(planes, wlo, whi);
            }
            break;
        }
    }
}

void Composition::updateBehaviour(const FrameTime& time, const signals::SignalBus& bus) {
    if (entityWorld_.empty() || params_ == nullptr) {
        return;
    }
    entity::EntityUpdate update;
    update.time = time.renderTime;
    update.dt = time.deltaTime;
    update.frameIndex = time.frameIndex;
    update.bus = &bus;
    update.viewPosition = scene_.camera.position;
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
    const auto it = std::find_if(nodes_.begin(), nodes_.end(),
                                 [&](const std::unique_ptr<CompositionNode>& n) { return n->name == name; });
    if (it == nodes_.end()) {
        return false;
    }
    unregisterNodeParameters(**it);
    const std::string grandParent = (*it)->parent;
    nodes_.erase(it);
    for (auto& other : nodes_) {
        if (other->parent == name) {
            other->parent = grandParent; // children keep their local transforms under the grandparent
        }
    }
    dirty_ = true;
    return true;
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
                 {"terrainLod", "terrainCull", "terrainLodDistance", "terrainViewDistance"}) {
                params_->remove(base + suffix);
            }
            node.terrainLodParam = nullptr;
            node.terrainCullParam = nullptr;
            node.terrainLodDistanceParam = nullptr;
            node.terrainViewDistanceParam = nullptr;
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

bool Composition::nodeVisible(const CompositionNode& node) {
    return node.visibleParam != nullptr ? node.visibleParam->value() : node.visible;
}

void Composition::ensureBuilt() {
    if (dirty_) {
        rebuild();
    }
}

void Composition::rebuild() {
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
        if (node.terrain.water.enabled) {
            install(terrainWaterProgramName(node.name),
                    world::waterMaterialProgram(node.terrain.water, terrainWaterProgramName(node.name)));
        }
    }
    scene_.materialPrograms.clear();
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
                rig.updateHz = node.animation.updateHz;
                rig.cullDistance = node.animation.cullDistance;
                scene_.rigs.push_back(std::move(rig));
            }
            range.rigCount = scene_.rigs.size() - range.firstRig;
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
            for (const PunctualLight& src : asset.scene.lights) {
                PunctualLight light = src;
                light.name = node.name + "/" + src.name;
                light.position = transformPoint(nodeT, src.position);
                light.direction = transformDirection(nodeT, src.direction);
                scene_.addLight(std::move(light));
            }
            break;
        }
        case NodeKind::Orb: {
            const MeshId mesh = scene_.addMesh(makeIcosphere(1.0f, 3));
            Entity& orb = scene_.addEntity(node.name, mesh);
            orb.style = MeshStyle::Lit;
            orb.material.baseColor = glm::vec3(0.75f, 0.2f, 0.9f);
            orb.material.emissiveColor = glm::vec3(0.9f, 0.45f, 1.0f);
            orb.material.emissiveIntensity = 0.15f;
            orb.material.roughness = 0.35f;
            orb.transform = nodeT;
            orb.visible = visible;
            range.restTransforms.emplace_back();
            range.restEmissive.push_back(0.15f);
            range.restRoughness.push_back(0.35f);
            break;
        }
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
        case NodeKind::Terrain: {
            // Scatter layers are ordinary procedural objects whose placements happen to have come
            // from an ecology pass rather than from a formula (ADR-048). Everything downstream --
            // instancing, GPU culling, LOD, variation, the material -- is the machinery an imported
            // mesh already gets, which is the whole reason the ecology emits a cloud instead of a
            // new kind of drawable.
            std::vector<world::GlowCluster> nodeGlow;
            std::unordered_map<std::string, std::shared_ptr<spatial::PointCloud>> habitats;
            for (const world::ScatterLayer& layer : node.ecology.layers) {
                std::span<const glm::vec3> anchors;
                if (layer.proximity) {
                    const auto found = habitats.find(layer.proximity->layer);
                    if (found != habitats.end()) {
                        anchors = found->second->positions();
                    }
                }
                auto cloud = std::make_shared<spatial::PointCloud>(world::scatter(node.worldMap, layer, anchors, node.ecology.clearances));
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
                if (layer.emissiveIntensity > 0.0f && ecologyLightGain_ > 0.0f) {
                    auto clusters = world::aggregateGlow(*cloud, layer, ecologyGlowCell_,
                                                         pg.variation.seed);
                    log::info("terrain '{}': scatter '{}' glow reduced to {} emitters", node.name,
                              layer.name, clusters.size());
                    nodeGlow.insert(nodeGlow.end(), clusters.begin(), clusters.end());
                }
                log::info("terrain '{}': scatter '{}' placed {} instances", node.name, layer.name,
                          cloud->count());
                // The asset's other materials, each an object identical to this one but for its
                // mesh, its material and its share of the triangle budget. The layer's tint and
                // emission are applied to each part's *own* colour, which is the whole point: the
                // tint turns the leaves green and the bark brown, not both to whichever won.
                std::vector<ProceduralGeometry> subs;
                for (std::size_t part = 1; part < parts.size(); ++part) {
                    ProceduralGeometry sub = pg;
                    sub.name = pg.name + fmt::format("_m{}", part);
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
            // The whole world is built here, once. Chunk meshes are static: only which of a chunk's
            // four meshes is drawn, and whether it is drawn at all, changes per frame.
            CompositionNode& mutableNode = *nodePtr;
            mutableNode.glow = std::move(nodeGlow);
            const auto buildStart = std::chrono::steady_clock::now();
            mutableNode.chunks = world::buildTerrain(
                node.worldMap, node.terrain,
                [&](std::size_t, int, MeshData&& mesh) { return scene_.addMesh(std::move(mesh)); });
            for (std::size_t c = 0; c < mutableNode.chunks.size(); ++c) {
                const world::TerrainChunk& chunk = mutableNode.chunks[c];
                Entity& e = scene_.addEntity(fmt::format("{}.chunk{}", node.name, c), chunk.meshes[0]);
                e.style = MeshStyle::Lit;
                e.material = node.terrainMaterial;
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
                e.style = MeshStyle::Lit;
                e.material.baseColor = node.terrain.water.shallowColor;
                e.material.roughness = node.terrain.water.roughness;
                e.material.metallic = 0.0f;
                e.material.doubleSided = true; // a surface seen from under it is still a surface
                e.material.program = prefixed(sanitise(prefix_), terrainWaterProgramName(node.name));
                e.transform = nodeT;
                e.visible = visible;
                range.restTransforms.emplace_back();
                range.restEmissive.push_back(0.0f);
                range.restRoughness.push_back(node.terrain.water.roughness);
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
            log::info("terrain '{}': {} chunks ({} with water), {} triangles at LOD 0, built in {:.0f} ms",
                      node.name, mutableNode.chunks.size(), wet, triangles, buildMs);
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

    // A rig supplies the lighting; without one, a world with no authored lights still gets a key
    // so it is not lit by ambient alone (ADR-033/034).
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
    ++scene_.textureVersion;
    // The procedural vector has just been rebuilt from the node list, so every index into it is
    // new. Costs measured against the old one describe objects that no longer exist.
    proceduralRebuild_.clear();
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
    cullEntityNodes();
    updateCharacters(time);
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
            node.animationApplied = node.animation.state;
            node.animationAppliedAt = time.renderTime;
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
                                   float blend) {
    CompositionNode* node = findNode(nodeName);
    if (node == nullptr) {
        return false;
    }
    node->animation.state = state;
    node->animation.blend = blend;
    node->animationApplied = state;
    node->animationAppliedAt = now;
    node->animationPushed = false;
    return true;
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
            applyProceduralParameters(node.proceduralParams, node.proceduralRest, pg);
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
                applyProceduralParameters(node.proceduralParams, subRest, sub);
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

    // Orbit camera around the bounds centre (distance fitted to the radius by default).
    const float fit = fitDistance();
    const float distance =
        cameraDistance_ != nullptr ? cameraDistance_->value() : cameraDistanceSetting_.value_or(fit);
    const float height = cameraHeight_ != nullptr
                             ? cameraHeight_->value()
                             : cameraHeightSetting_.value_or(center_.y + radius_ * 0.35f);
    const float fov = cameraFov_ != nullptr ? cameraFov_->value() : cameraFovSetting_;
    const int cameraMode = cameraMode_ != nullptr ? cameraMode_->value() : cameraModeSetting_;
    const spatial::Spline* cameraSpline =
        cameraMode == 2 && !cameraSplineSetting_.empty() ? scene_.splines.find(prefixed(sanitise(prefix_), cameraSplineSetting_)) : nullptr;
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
        scene_.camera.position = at.position + frameOffset;
        scene_.camera.target = ahead.position + at.binormal * offset.x + at.normal * offset.y;
        if (glm::length(scene_.camera.target - scene_.camera.position) < 1e-4f) {
            scene_.camera.target = scene_.camera.position + at.tangent;
        }
    } else if (cameraMode == 1) {
        // Free camera: explicit position and target (keyable on the timeline, modulatable).
        scene_.camera.position = cameraPosition_ != nullptr ? cameraPosition_->value() : cameraPositionSetting_;
        scene_.camera.target = cameraTarget_ != nullptr ? cameraTarget_->value() : cameraTargetSetting_;
        if (glm::length(scene_.camera.target - scene_.camera.position) < 1e-4f) {
            scene_.camera.target = scene_.camera.position + glm::vec3(0.0f, 0.0f, -1.0f);
        }
        if ((frameCounter_++ % 120) == 0) {
            log::debug("free camera pos ({:.1f} {:.1f} {:.1f}) target ({:.1f} {:.1f} {:.1f})", scene_.camera.position.x,
                       scene_.camera.position.y, scene_.camera.position.z, scene_.camera.target.x, scene_.camera.target.y,
                       scene_.camera.target.z);
        }
    } else {
        scene_.camera.position =
            center_ + glm::vec3(std::sin(cameraAngle_) * distance, 0.0f, std::cos(cameraAngle_) * distance);
        scene_.camera.position.y = height;
        scene_.camera.target = center_;
    }
    scene_.camera.fovYRadians = glm::radians(fov);
    scene_.camera.nearPlane = std::clamp(radius_ * 0.005f, 0.01f, 0.5f);
    scene_.camera.farPlane = std::max(radius_ * 50.0f, 2000.0f); // free cameras look across whole worlds
    applyFraming();
    updateTerrainLod();
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

void Composition::updateTerrainLod() {
    // The frustum is built at no narrower than a deliberately wide aspect. The viewport is known
    // now (setViewport), but culling a chunk the frame turns out to include is a hole in the ground
    // while keeping one it does not include costs a draw call, so the error is still taken on the
    // safe side: a wider viewport widens the frustum, a narrower one does not narrow it.
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
            const auto setWaterShadow = [&](bool on) {
                if (water < scene_.entities.size()) {
                    scene_.entities[water].castsShadow = on;
                }
            };
            const auto setWater = [&](bool on) {
                if (water < scene_.entities.size()) {
                    scene_.entities[water].visible = scene_.entities[water].visible && on;
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
                setWater(false);
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
                e.visible = false;
                setWater(false);
                continue;
            }
            e.castsShadow = distance <= shadowReach;
            setWaterShadow(e.castsShadow);
            if (planes && !world::aabbVisible(*planes, lo, hi)) {
                // Off screen, not absent. The camera passes skip it; the shadow passes still get
                // it as a candidate and test it against each cascade's own frustum, because a hill
                // behind the camera casts into shot (ADR-046). Its LOD mesh is still picked below
                // -- a caster needs geometry, and its silhouette is what the map records.
                setCameraCulled(true);
            }
            // LOD follows how big the ground looks, not how far away it is. The composition knows
            // the lens but not the viewport it will be drawn into, so the height is the reference
            // one `lodDistance` is authored against: changing focal length re-picks levels
            // correctly, changing window size does not. Fixing that needs the viewport plumbed in,
            // which is the same thing the cull aspect below is waiting for.
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
    // ADR-059: written back as it was read. The live values are parameters and belong to the
    // project; what the scene owns is the look it was authored with.
    if (!postJson_.is_null() && !postJson_.empty()) {
        j["post"] = postJson_;
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
        n["position"] = vecToJson(node.transform.position);
        n["rotation"] = vecToJson(node.rotationParam != nullptr ? node.rotationParam->base()
                                                                : eulerDegrees(node.transform.rotation));
        n["scale"] = vecToJson(node.transform.scale);
        n["visible"] = node.visible;
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
            if (node.animation.cullDistance != 120.0f) {
                anim["cullDistance"] = node.animation.cullDistance;
            }
            n["animation"] = anim;
        }
        if (node.kind == NodeKind::Particles) {
            n["particles"] = particlesToJson(node.particles);
        }
        if (node.kind == NodeKind::Procedural) {
            n["procedural"] = node.procedural.toJson();
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
                                {"water",
                                 json{{"enabled", ts.water.enabled},
                                      {"shallow", ts.water.shallow},
                                      {"roughness", ts.water.roughness},
                                      {"shoreFade", ts.water.shoreFade},
                                      {"shallowColor", vecToJson(ts.water.shallowColor)},
                                      {"deepColor", vecToJson(ts.water.deepColor)},
                                      {"emissiveColor", vecToJson(ts.water.emissiveColor)},
                                      {"emissiveIntensity", ts.water.emissiveIntensity}}}};
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
    if (!entityDescs_.empty()) {
        j["entities"] = entity::entitiesToJson(entityDescs_);
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
    // ADR-055: the wind is a top-level block, a sibling of `environment` rather than a member of
    // it, because it is the weather rather than the sky: it moves geometry, and a later tier will
    // move cloth and particles with the same numbers.
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
    if (j.contains("entities")) {
        auto entities = entity::entitiesFromJson(j.at("entities"));
        if (!entities) {
            return fail("scene file '{}': {}", scenePath.string(), entities.error().message);
        }
        if (auto ok = comp->setEntities(std::move(*entities)); !ok) {
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
                auto cull = readFloat(anim, "cullDistance", 120.0f);
                if (!state) return std::unexpected(state.error());
                if (!blend) return std::unexpected(blend.error());
                if (!speed) return std::unexpected(speed.error());
                if (!hz) return std::unexpected(hz.error());
                if (!cull) return std::unexpected(cull.error());
                node.animation.state = *state;
                node.animation.blend = *blend;
                node.animation.speed = *speed;
                node.animation.updateHz = *hz;
                node.animation.cullDistance = *cull;
            }
            if (item.contains("procedural")) {
                auto pg = ProceduralGeometry::fromJson(item.at("procedural"));
                if (!pg) {
                    return fail("scene file '{}': node '{}': {}", scenePath.string(), node.name, pg.error().message);
                }
                node.procedural = std::move(*pg);
                node.proceduralMaterialAuthored = item.at("procedural").contains("material");
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
                              TerrainFloat{"shoreFade", &w.shoreFade},
                              TerrainFloat{"emissiveIntensity", &w.emissiveIntensity}}) {
                            auto v = readFloat(wj, f.key, *f.target);
                            if (!v) {
                                return fail("node '{}': water: {}", node.name, v.error().message);
                            }
                            *f.target = *v;
                        }
                        for (const auto& [key, target] : {std::pair{"shallowColor", &w.shallowColor},
                                                          std::pair{"deepColor", &w.deepColor},
                                                          std::pair{"emissiveColor", &w.emissiveColor}}) {
                            auto v = readVec<3>(wj, key, *target);
                            if (!v) {
                                return fail("node '{}': water: {}", node.name, v.error().message);
                            }
                            *target = *v;
                        }
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
                if (item.contains("material")) {
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
                                                             std::vector<std::filesystem::path> ancestors) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("scene file not found: '{}'", path.string());
    }
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open scene file '{}'", path.string());
    }
    const json j = json::parse(in, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded()) {
        return fail("scene file '{}': invalid JSON", path.string());
    }
    auto comp = fromJsonImpl(j, registry, depth, std::move(ancestors), path);
    if (!comp) {
        return fail("scene file '{}': {}", path.filename().string(), comp.error().message);
    }
    log::info("loaded scene '{}': {} node(s)", path.filename().string(), (*comp)->nodeCount());
    return comp;
}

Result<std::unique_ptr<Composition>> Composition::loadFile(const std::filesystem::path& path,
                                                           assets::AssetRegistry& registry, int depth) {
    return loadNested(registry.resolve(path), registry, depth, {});
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
