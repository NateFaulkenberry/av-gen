// Light rigs and colour temperature (ADR-033). A rig is a lighting setup expressed relative to
// the subject and the camera; `expand()` turns it into ordinary world lights, so nothing
// downstream (packing, clustering, shadows) knows a rig existed.

#include "scene/light_rig.hpp"

#include "core/json_keys.hpp"
#include "core/log.hpp"
#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <span>
#include <string_view>
#include <unordered_set>

namespace avgen::scene {

// ADR-278. Every key `LightRig::fromJson` reads, at the rig's root and in one of its lights.
// This lab's own fixture was written with `"coneDegrees"` first and silently got the 45-degree
// default; reading the parser is what caught it, which is not a check. `core/json_keys.hpp` carries
// the reasoning for warning rather than refusing, and the `_`-prefix exemption.
constexpr std::string_view kRigKeys[] = {"format",           "version",          "name",
                                         "description",      "keyIntensity",     "ambientIntensity",
                                         "ambientColor",     "ambientTemperature", "lights"};
constexpr std::string_view kRigLightKeys[] = {
    "name",     "type",       "role",     "azimuth",        "elevation", "distance",
    "intensity", "color",     "temperature", "tint",        "size",      "aspect",
    "castsShadow", "contactShadow", "shadowStrength", "softness", "volumetric", "cone",
    "followCamera"};

std::span<const std::string_view> rigFileKeys() { return kRigKeys; }
std::span<const std::string_view> rigLightKeys() { return kRigLightKeys; }

namespace {

using nlohmann::json;
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegToRad = kPi / 180.0f;

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : fallback;
}

json vecToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

Result<glm::vec3> readVec3(const json& j, const char* key, const glm::vec3& def) {
    if (!j.contains(key)) {
        return def;
    }
    const json& v = j.at(key);
    if (!v.is_array() || v.size() != 3 || !v[0].is_number()) {
        return fail("light rig: '{}' must be an array of 3 numbers", key);
    }
    return glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
}

Result<float> readFloat(const json& j, const char* key, float def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_number()) {
        return fail("light rig: '{}' must be a number", key);
    }
    return j.at(key).get<float>();
}

Result<bool> readBool(const json& j, const char* key, bool def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_boolean()) {
        return fail("light rig: '{}' must be a boolean", key);
    }
    return j.at(key).get<bool>();
}

Result<std::string> readString(const json& j, const char* key, const std::string& def) {
    if (!j.contains(key)) {
        return def;
    }
    if (!j.at(key).is_string()) {
        return fail("light rig: '{}' must be a string", key);
    }
    return j.at(key).get<std::string>();
}

std::string sanitise(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == ' ') {
            c = '_';
        }
    }
    return name;
}

params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = lo;
    d.softMax = hi;
    return d;
}

} // namespace

// ---- light type and role names -----------------------------------------------------------------

const char* lightTypeName(PunctualLight::Type type) {
    switch (type) {
    case PunctualLight::Type::Directional: return "directional";
    case PunctualLight::Type::Point: return "point";
    case PunctualLight::Type::Spot: return "spot";
    case PunctualLight::Type::Rect: return "rect";
    case PunctualLight::Type::Disk: return "disk";
    case PunctualLight::Type::Tube: return "tube";
    case PunctualLight::Type::Sphere: return "sphere";
    }
    return "directional";
}

std::optional<PunctualLight::Type> lightTypeFromName(std::string_view name) {
    for (const auto t : {PunctualLight::Type::Directional, PunctualLight::Type::Point, PunctualLight::Type::Spot,
                         PunctualLight::Type::Rect, PunctualLight::Type::Disk, PunctualLight::Type::Tube,
                         PunctualLight::Type::Sphere}) {
        if (name == lightTypeName(t)) {
            return t;
        }
    }
    return std::nullopt;
}

const char* lightRoleName(PunctualLight::Role role) {
    switch (role) {
    case PunctualLight::Role::Key: return "key";
    case PunctualLight::Role::Fill: return "fill";
    case PunctualLight::Role::Rim: return "rim";
    case PunctualLight::Role::Back: return "back";
    case PunctualLight::Role::Ambient: return "ambient";
    case PunctualLight::Role::Practical: return "practical";
    }
    return "key";
}

std::optional<PunctualLight::Role> lightRoleFromName(std::string_view name) {
    for (const auto r : {PunctualLight::Role::Key, PunctualLight::Role::Fill, PunctualLight::Role::Rim,
                         PunctualLight::Role::Back, PunctualLight::Role::Ambient, PunctualLight::Role::Practical}) {
        if (name == lightRoleName(r)) {
            return r;
        }
    }
    return std::nullopt;
}

// Planckian locus (Kim et al. cubic fit) to CIE xy, then to linear sRGB, normalised to luminance 1.
glm::vec3 colorTemperatureToRgb(float kelvin, float tint) {
    const float t = std::clamp(kelvin, 1500.0f, 12000.0f);
    const float invT = 1000.0f / t;
    const float invT2 = invT * invT;
    const float invT3 = invT2 * invT;
    const float x = t <= 4000.0f ? -0.2661239f * invT3 - 0.2343589f * invT2 + 0.8776956f * invT + 0.179910f
                                 : -3.0258469f * invT3 + 2.1070379f * invT2 + 0.2226347f * invT + 0.240390f;
    const float x2 = x * x;
    const float x3 = x2 * x;
    float y = 0.0f;
    if (t <= 2222.0f) {
        y = -1.1063814f * x3 - 1.34811020f * x2 + 2.18555832f * x - 0.20219683f;
    } else if (t <= 4000.0f) {
        y = -0.9549476f * x3 - 1.37418593f * x2 + 2.09137015f * x - 0.16748867f;
    } else {
        y = 3.0817580f * x3 - 5.87338670f * x2 + 3.75112997f * x - 0.37001483f;
    }
    // Tint runs perpendicular to the locus: positive is magenta (below the locus, less green),
    // negative is green, matching the sign convention every grading tool uses.
    y -= tint * 0.05f;
    const float yy = std::max(y, 1e-4f);
    const glm::vec3 xyz(x / yy, 1.0f, (1.0f - x - yy) / yy);
    glm::vec3 rgb(3.2404542f * xyz.x - 1.5371385f * xyz.y - 0.4985314f * xyz.z,
                  -0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
                  0.0556434f * xyz.x - 0.2040259f * xyz.y + 1.0572252f * xyz.z);
    // A 6500 K black body is close to, but not exactly, the D65 white point sRGB is defined
    // against. Dividing through by the value at 6500 K makes the neutral point exactly neutral,
    // which is the contract an artist expects from the control, and leaves every relationship
    // between temperatures unchanged.
    constexpr glm::vec3 kNeutral6500(1.042630f, 0.983783f, 1.035076f);
    rgb /= kNeutral6500;
    const glm::vec3 positive = glm::max(rgb, glm::vec3(0.0f));
    const float luminance = 0.2126f * positive.r + 0.7152f * positive.g + 0.0722f * positive.b;
    return luminance > 1e-4f ? positive / luminance : glm::vec3(1.0f);
}

// Declared in scene_types.hpp beside `colorTemperatureToRgb`, and implemented here for the same
// reason that one is: both are the physics of a light rather than of a renderer, and both are
// needed on either side of the scene/rendering boundary. It used to be private to this file, which
// meant `rendering::lightInfluenceRadius` had no way to ask how big an emitter was and computed a
// reach for a light of no extent -- see ADR-272.
float emitterArea(PunctualLight::Type type, float width, float height, float radius) {
    switch (type) {
    case PunctualLight::Type::Rect: return std::max(width * height, 1e-4f);
    case PunctualLight::Type::Disk:
    case PunctualLight::Type::Sphere: return std::max(kPi * radius * radius, 1e-4f);
    case PunctualLight::Type::Tube: return std::max(2.0f * radius * width, 1e-4f);
    default: return 1.0f;
    }
}

float emitterArea(const PunctualLight& light) {
    return emitterArea(light.type, light.width, light.height, light.radius);
}

// ---- validation ---------------------------------------------------------------------------------

Result<void> LightRig::validate() const {
    if (name.empty()) {
        return fail("light rig: 'name' must not be empty");
    }
    if (!(keyIntensity >= 0.0f) || !std::isfinite(keyIntensity)) {
        return fail("light rig '{}': keyIntensity must be a finite value >= 0", name);
    }
    if (!(ambientIntensity >= 0.0f) || !std::isfinite(ambientIntensity)) {
        return fail("light rig '{}': ambientIntensity must be a finite value >= 0", name);
    }
    if (ambientTemperature < 1500.0f || ambientTemperature > 12000.0f) {
        return fail("light rig '{}': ambientTemperature {} is outside 1500..12000 K", name, ambientTemperature);
    }
    if (lights.empty()) {
        return fail("light rig '{}': needs at least one light", name);
    }
    std::unordered_set<std::string> seen;
    for (const RigLight& l : lights) {
        if (l.name.empty()) {
            return fail("light rig '{}': a light has no name", name);
        }
        if (!seen.insert(l.name).second) {
            return fail("light rig '{}': duplicate light name '{}'", name, l.name);
        }
        if (!std::isfinite(l.azimuthDegrees) || !std::isfinite(l.elevationDegrees)) {
            return fail("light rig '{}': light '{}' has a non-finite angle", name, l.name);
        }
        if (l.elevationDegrees < -90.0f || l.elevationDegrees > 90.0f) {
            return fail("light rig '{}': light '{}' elevation {} is outside -90..90", name, l.name,
                        l.elevationDegrees);
        }
        if (!(l.distanceRadii > 0.0f) || !std::isfinite(l.distanceRadii)) {
            return fail("light rig '{}': light '{}' distanceRadii must be > 0", name, l.name);
        }
        if (!(l.intensity >= 0.0f) || !std::isfinite(l.intensity)) {
            return fail("light rig '{}': light '{}' intensity must be a finite value >= 0", name, l.name);
        }
        if (l.temperature < 1500.0f || l.temperature > 12000.0f) {
            return fail("light rig '{}': light '{}' temperature {} is outside 1500..12000 K", name, l.name,
                        l.temperature);
        }
        if (!(l.sizeRadii >= 0.0f) || !std::isfinite(l.sizeRadii)) {
            return fail("light rig '{}': light '{}' sizeRadii must be a finite value >= 0", name, l.name);
        }
        if (!(l.aspect > 0.0f) || !std::isfinite(l.aspect)) {
            return fail("light rig '{}': light '{}' aspect must be > 0", name, l.name);
        }
        if (!(l.coneDegrees > 0.0f) || l.coneDegrees >= 180.0f) {
            return fail("light rig '{}': light '{}' coneDegrees {} is outside (0, 180)", name, l.name,
                        l.coneDegrees);
        }
    }
    return {};
}

// ---- expansion ----------------------------------------------------------------------------------

std::vector<PunctualLight> LightRig::expand(const glm::vec3& subjectCenter, float subjectRadius,
                                            const glm::vec3& cameraPosition, const glm::vec3& upIn) const {
    const glm::vec3 up = safeNormalize(upIn, glm::vec3(0.0f, 1.0f, 0.0f));
    const float radius = std::max(subjectRadius, 1e-3f);
    // The camera's view direction flattened into the subject's horizon plane. Azimuth 0 puts the
    // light behind the camera (pointing the way the camera looks), +90 to the camera's right.
    glm::vec3 toSubject = subjectCenter - cameraPosition;
    toSubject -= up * glm::dot(toSubject, up);
    const glm::vec3 worldForward = safeNormalize(glm::vec3(0.0f, 0.0f, -1.0f) -
                                                     up * glm::dot(glm::vec3(0.0f, 0.0f, -1.0f), up),
                                                 glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 cameraForward = safeNormalize(toSubject, worldForward);

    std::vector<PunctualLight> out;
    out.reserve(lights.size());
    for (const RigLight& r : lights) {
        const glm::vec3 forward = r.followCamera ? cameraForward : worldForward;
        const glm::vec3 right = safeNormalize(glm::cross(forward, up), glm::vec3(1.0f, 0.0f, 0.0f));
        const float az = r.azimuthDegrees * kDegToRad;
        const float el = r.elevationDegrees * kDegToRad;
        const glm::vec3 horizontal = -forward * std::cos(az) + right * std::sin(az);
        const glm::vec3 offset = safeNormalize(horizontal * std::cos(el) + up * std::sin(el),
                                               glm::vec3(0.0f, 1.0f, 0.0f));
        const float distance = r.distanceRadii * radius;

        PunctualLight light;
        light.name = r.name;
        light.type = r.type;
        light.role = r.role;
        light.position = subjectCenter + offset * distance;
        light.direction = -offset; // the direction the light travels: towards the subject
        light.up = up;
        const bool ambient = r.role == PunctualLight::Role::Ambient;
        light.color = ambient ? ambientColor : r.color;
        light.temperature = ambient ? ambientTemperature : r.temperature;
        light.tint = r.tint;

        const float size = std::max(r.sizeRadii * radius, 1e-3f);
        light.width = size * std::sqrt(r.aspect);
        light.height = size / std::sqrt(r.aspect);
        light.radius = size * 0.5f;
        if (r.type == PunctualLight::Type::Tube) {
            light.radius = std::max(size * 0.05f, 1e-3f);
        }

        // Photometric scaling. `intensity` is a ratio against the rig's key, meaning "this much
        // illuminance at the subject"; converting it to the light's own unit keeps a rig looking
        // the same in a room and in a cathedral, and whatever size the emitter is.
        const float base = (ambient ? ambientIntensity : keyIntensity) * std::max(r.intensity, 0.0f);
        if (r.type == PunctualLight::Type::Directional) {
            light.intensity = base;
        } else if (r.type == PunctualLight::Type::Point || r.type == PunctualLight::Type::Spot) {
            light.intensity = base * distance * distance;
            light.range = distance * 6.0f;
        } else {
            light.intensity = base * distance * distance /
                              emitterArea(r.type, light.width, light.height, light.radius);
            light.range = distance * 6.0f;
        }
        light.innerConeAngle = r.coneDegrees * kDegToRad * 0.5f * 0.75f;
        light.outerConeAngle = r.coneDegrees * kDegToRad * 0.5f;
        light.castsShadow = r.castsShadow;
        light.contactShadow = r.contactShadow;
        light.shadowStrength = std::clamp(r.shadowStrength, 0.0f, 1.0f);
        light.softness = std::max(r.softness, 0.0f);
        light.volumetricStrength = std::max(r.volumetricStrength, 0.0f);
        light.enabled = true;
        out.push_back(std::move(light));
    }
    return out;
}

std::uint64_t LightRig::structuralHash() const {
    detail::StructHash h;
    h.str(name);
    h.f32(keyIntensity);
    h.f32(ambientIntensity);
    h.v3(ambientColor);
    h.f32(ambientTemperature);
    h.u64(lights.size());
    for (const RigLight& l : lights) {
        h.str(l.name);
        h.u32(static_cast<std::uint32_t>(l.type));
        h.u32(static_cast<std::uint32_t>(l.role));
        h.f32(l.azimuthDegrees);
        h.f32(l.elevationDegrees);
        h.f32(l.distanceRadii);
        h.f32(l.intensity);
        h.v3(l.color);
        h.f32(l.temperature);
        h.f32(l.tint);
        h.f32(l.sizeRadii);
        h.f32(l.aspect);
        h.boolean(l.castsShadow);
        h.boolean(l.contactShadow);
        h.f32(l.shadowStrength);
        h.f32(l.softness);
        h.f32(l.volumetricStrength);
        h.f32(l.coneDegrees);
        h.boolean(l.followCamera);
    }
    return h.value();
}

// ---- JSON ---------------------------------------------------------------------------------------

nlohmann::json LightRig::toJson() const {
    json j = json::object();
    j["format"] = "avgen-lightrig";
    j["version"] = 1;
    j["name"] = name;
    if (!description.empty()) {
        j["description"] = description;
    }
    j["keyIntensity"] = keyIntensity;
    j["ambientIntensity"] = ambientIntensity;
    j["ambientColor"] = vecToJson(ambientColor);
    j["ambientTemperature"] = ambientTemperature;
    json array = json::array();
    for (const RigLight& l : lights) {
        json e = json::object();
        e["name"] = l.name;
        e["type"] = lightTypeName(l.type);
        e["role"] = lightRoleName(l.role);
        e["azimuth"] = l.azimuthDegrees;
        e["elevation"] = l.elevationDegrees;
        e["distance"] = l.distanceRadii;
        e["intensity"] = l.intensity;
        e["color"] = vecToJson(l.color);
        e["temperature"] = l.temperature;
        e["tint"] = l.tint;
        e["size"] = l.sizeRadii;
        e["aspect"] = l.aspect;
        e["castsShadow"] = l.castsShadow;
        e["contactShadow"] = l.contactShadow;
        e["shadowStrength"] = l.shadowStrength;
        e["softness"] = l.softness;
        e["volumetric"] = l.volumetricStrength;
        e["cone"] = l.coneDegrees;
        e["followCamera"] = l.followCamera;
        array.push_back(std::move(e));
    }
    j["lights"] = std::move(array);
    return j;
}

Result<LightRig> LightRig::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("light rig: root is not an object");
    }
    auto format = readString(j, "format", "avgen-lightrig");
    if (!format) {
        return std::unexpected(format.error());
    }
    if (*format != "avgen-lightrig") {
        return fail("not a light rig file (format '{}')", *format);
    }
    LightRig rig;
    auto name = readString(j, "name", rig.name);
    if (!name) {
        return std::unexpected(name.error());
    }
    rig.name = *name;
    json_keys::warnUnknownKeys(j, kRigKeys, "light rig '" + rig.name + "'");
    auto description = readString(j, "description", "");
    if (!description) {
        return std::unexpected(description.error());
    }
    rig.description = *description;
    auto keyIntensity = readFloat(j, "keyIntensity", rig.keyIntensity);
    if (!keyIntensity) {
        return std::unexpected(keyIntensity.error());
    }
    rig.keyIntensity = *keyIntensity;
    auto ambientIntensity = readFloat(j, "ambientIntensity", rig.ambientIntensity);
    if (!ambientIntensity) {
        return std::unexpected(ambientIntensity.error());
    }
    rig.ambientIntensity = *ambientIntensity;
    auto ambientColor = readVec3(j, "ambientColor", rig.ambientColor);
    if (!ambientColor) {
        return std::unexpected(ambientColor.error());
    }
    rig.ambientColor = *ambientColor;
    auto ambientTemperature = readFloat(j, "ambientTemperature", rig.ambientTemperature);
    if (!ambientTemperature) {
        return std::unexpected(ambientTemperature.error());
    }
    rig.ambientTemperature = *ambientTemperature;

    if (!j.contains("lights")) {
        return fail("light rig '{}': missing 'lights'", rig.name);
    }
    const json& array = j.at("lights");
    if (!array.is_array()) {
        return fail("light rig '{}': 'lights' must be an array", rig.name);
    }
    for (const json& e : array) {
        if (!e.is_object()) {
            return fail("light rig '{}': every light must be an object", rig.name);
        }
        RigLight l;
        auto lname = readString(e, "name", "");
        if (!lname) {
            return std::unexpected(lname.error());
        }
        l.name = *lname;
        auto type = readString(e, "type", lightTypeName(l.type));
        if (!type) {
            return std::unexpected(type.error());
        }
        if (auto parsed = lightTypeFromName(*type)) {
            l.type = *parsed;
        } else {
            return fail("light rig '{}': unknown light type '{}'", rig.name, *type);
        }
        auto role = readString(e, "role", lightRoleName(l.role));
        if (!role) {
            return std::unexpected(role.error());
        }
        if (auto parsed = lightRoleFromName(*role)) {
            l.role = *parsed;
        } else {
            return fail("light rig '{}': unknown light role '{}'", rig.name, *role);
        }
        struct FloatField {
            const char* key;
            float* target;
        };
        const FloatField floats[] = {
            {"azimuth", &l.azimuthDegrees},   {"elevation", &l.elevationDegrees},
            {"distance", &l.distanceRadii},   {"intensity", &l.intensity},
            {"temperature", &l.temperature},  {"tint", &l.tint},
            {"size", &l.sizeRadii},           {"aspect", &l.aspect},
            {"shadowStrength", &l.shadowStrength}, {"softness", &l.softness},
            {"volumetric", &l.volumetricStrength}, {"cone", &l.coneDegrees},
        };
        for (const FloatField& f : floats) {
            auto v = readFloat(e, f.key, *f.target);
            if (!v) {
                return std::unexpected(v.error());
            }
            *f.target = *v;
        }
        auto color = readVec3(e, "color", l.color);
        if (!color) {
            return std::unexpected(color.error());
        }
        l.color = *color;
        auto casts = readBool(e, "castsShadow", l.castsShadow);
        if (!casts) {
            return std::unexpected(casts.error());
        }
        l.castsShadow = *casts;
        auto contact = readBool(e, "contactShadow", l.contactShadow);
        if (!contact) {
            return std::unexpected(contact.error());
        }
        l.contactShadow = *contact;
        auto follow = readBool(e, "followCamera", l.followCamera);
        if (!follow) {
            return std::unexpected(follow.error());
        }
        l.followCamera = *follow;
        json_keys::warnUnknownKeys(e, kRigLightKeys,
                                   "light rig '" + rig.name + "': light '" + l.name + "'");
        rig.lights.push_back(std::move(l));
    }
    if (auto r = rig.validate(); !r) {
        return std::unexpected(r.error());
    }
    return rig;
}

Result<LightRig> LightRig::loadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("light rig: cannot open '{}'", path.string());
    }
    json doc;
    try {
        file >> doc;
    } catch (const std::exception& e) {
        return fail("light rig '{}': {}", path.string(), e.what());
    }
    auto rig = LightRig::fromJson(doc);
    if (!rig && rig.error().message.find(path.string()) == std::string::npos) {
        return fail("light rig '{}': {}", path.string(), rig.error().message);
    }
    return rig;
}

// ---- parameters ----------------------------------------------------------------------------------

LightRigParameters registerLightRigParameters(params::ParameterSet& params, const LightRig& rest,
                                              const std::string& prefix) {
    LightRigParameters p;
    p.prefix = prefix;
    const std::string base = prefix + "lightrig/" + sanitise(rest.name) + "/";
    p.keyIntensity = &params.add(floatDesc(base + "keyIntensity", rest.keyIntensity, 0.0f, 200.0f));
    p.ambientIntensity = &params.add(floatDesc(base + "ambientIntensity", rest.ambientIntensity, 0.0f, 20.0f));
    p.all.push_back(p.keyIntensity);
    p.all.push_back(p.ambientIntensity);
    {
        params::ParamDesc<glm::vec3> desc;
        desc.path = base + "ambientColor";
        desc.defaultValue = rest.ambientColor;
        desc.hardMin = glm::vec3(0.0f);
        desc.hardMax = glm::vec3(4.0f);
        desc.softMin = glm::vec3(0.0f);
        desc.softMax = glm::vec3(1.0f);
        desc.isColor = true;
        p.all.push_back(&params.add(std::move(desc)));
    }
    p.all.push_back(&params.add(floatDesc(base + "ambientTemperature", rest.ambientTemperature, 1500.0f, 12000.0f)));
    for (const RigLight& l : rest.lights) {
        const std::string lightBase = base + sanitise(l.name) + "/";
        auto* intensity = &params.add(floatDesc(lightBase + "intensity", l.intensity, 0.0f, 20.0f));
        p.lightIntensity.push_back(intensity);
        p.all.push_back(intensity);
        p.all.push_back(&params.add(floatDesc(lightBase + "azimuth", l.azimuthDegrees, -180.0f, 180.0f)));
        p.all.push_back(&params.add(floatDesc(lightBase + "elevation", l.elevationDegrees, -90.0f, 90.0f)));
        p.all.push_back(&params.add(floatDesc(lightBase + "distance", l.distanceRadii, 0.05f, 40.0f)));
        p.all.push_back(&params.add(floatDesc(lightBase + "temperature", l.temperature, 1500.0f, 12000.0f)));
        p.all.push_back(&params.add(floatDesc(lightBase + "size", l.sizeRadii, 0.0f, 10.0f)));
        p.all.push_back(&params.add(floatDesc(lightBase + "shadowStrength", l.shadowStrength, 0.0f, 1.0f)));
    }
    return p;
}

void applyLightRigParameters(const LightRigParameters& p, const LightRig& rest, LightRig& live) {
    live = rest;
    if (p.keyIntensity != nullptr) {
        live.keyIntensity = p.keyIntensity->value();
    }
    if (p.ambientIntensity != nullptr) {
        live.ambientIntensity = p.ambientIntensity->value();
    }
    // The remaining parameters are looked up positionally: `all` holds ambientColor and
    // ambientTemperature after the two intensities, then seven entries per light.
    std::size_t index = 2;
    if (index + 1 < p.all.size()) {
        if (const auto* colour = dynamic_cast<const params::Parameter<glm::vec3>*>(p.all[index])) {
            live.ambientColor = colour->value();
        }
        if (const auto* temperature = dynamic_cast<const params::Parameter<float>*>(p.all[index + 1])) {
            live.ambientTemperature = temperature->value();
        }
    }
    index += 2;
    for (RigLight& l : live.lights) {
        if (index + 6 >= p.all.size()) {
            break;
        }
        const auto get = [&](std::size_t offset, float fallback) {
            const auto* param = dynamic_cast<const params::Parameter<float>*>(p.all[index + offset]);
            return param != nullptr ? param->value() : fallback;
        };
        l.intensity = get(0, l.intensity);
        l.azimuthDegrees = get(1, l.azimuthDegrees);
        l.elevationDegrees = get(2, l.elevationDegrees);
        l.distanceRadii = get(3, l.distanceRadii);
        l.temperature = get(4, l.temperature);
        l.sizeRadii = get(5, l.sizeRadii);
        l.shadowStrength = get(6, l.shadowStrength);
        index += 7;
    }
}

void unregisterLightRigParameters(params::ParameterSet& params, const LightRigParameters& p) {
    for (const params::IParameter* param : p.all) {
        if (param != nullptr) {
            params.remove(param->path());
        }
    }
}

} // namespace avgen::scene
