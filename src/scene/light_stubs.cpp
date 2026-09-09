// TEMPORARY: minimal definitions for the cinematic-phase headers so the tree links while the
// lighting and composition agents implement them. Every function here is replaced by a real
// implementation in scene/light_rig.cpp and scene/composition_data.cpp; delete this file then.
#include "scene/composition_data.hpp"
#include "scene/light_rig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

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
    y += tint * 0.05f; // perpendicular-ish nudge: green below the locus, magenta above
    const float yy = std::max(y, 1e-4f);
    const glm::vec3 xyz(x / yy, 1.0f, (1.0f - x - yy) / yy);
    const glm::vec3 rgb(3.2404542f * xyz.x - 1.5371385f * xyz.y - 0.4985314f * xyz.z,
                        -0.9692660f * xyz.x + 1.8760108f * xyz.y + 0.0415560f * xyz.z,
                        0.0556434f * xyz.x - 0.2040259f * xyz.y + 1.0572252f * xyz.z);
    const glm::vec3 positive = glm::max(rgb, glm::vec3(0.0f));
    const float luminance = 0.2126f * positive.r + 0.7152f * positive.g + 0.0722f * positive.b;
    return luminance > 1e-4f ? positive / luminance : glm::vec3(1.0f);
}

// The lens and exposure members live in scene/camera.cpp (ADR-037), no longer stubbed here.

Result<void> LightRig::validate() const { return {}; }
std::vector<PunctualLight> LightRig::expand(const glm::vec3&, float, const glm::vec3&, const glm::vec3&) const {
    return {};
}
std::uint64_t LightRig::structuralHash() const { return 0; }
nlohmann::json LightRig::toJson() const { return nlohmann::json::object(); }
Result<LightRig> LightRig::fromJson(const nlohmann::json&) { return LightRig{}; }
Result<LightRig> LightRig::loadFile(const std::filesystem::path&) { return LightRig{}; }
LightRigParameters registerLightRigParameters(params::ParameterSet&, const LightRig&, const std::string& prefix) {
    LightRigParameters p;
    p.prefix = prefix;
    return p;
}
void applyLightRigParameters(const LightRigParameters&, const LightRig& rest, LightRig& live) { live = rest; }
void unregisterLightRigParameters(params::ParameterSet&, const LightRigParameters&) {}

Result<void> CompositionData::validate() const { return {}; }
const FocalPoint* CompositionData::find(std::string_view name) const {
    for (const FocalPoint& f : focalPoints) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}
void CompositionData::appendFields(spatial::FieldSet&) const {}
DepthLayer CompositionData::layerAt(float) const { return DepthLayer{}; }
std::uint64_t CompositionData::structuralHash() const { return 0; }
nlohmann::json CompositionData::toJson() const { return nlohmann::json::object(); }
Result<CompositionData> CompositionData::fromJson(const nlohmann::json&) { return CompositionData{}; }

} // namespace avgen::scene
