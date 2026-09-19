#include "ui/lights_panel_logic.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::ui {
namespace {

bool isArea(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    return type == T::Rect || type == T::Disk || type == T::Tube || type == T::Sphere;
}

bool isAimed(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    return type == T::Directional || type == T::Spot;
}

} // namespace

std::vector<std::string_view> lightParameterLeaves(scene::PunctualLight::Type type) {
    using T = scene::PunctualLight::Type;
    // Registered for every kind.
    std::vector<std::string_view> out{"enabled",      "intensity",   "color",        "position",
                                      "temperature",  "tint",        "angularSize",  "shadowStrength",
                                      "castsShadow",  "contactShadow", "shadowBias", "volumetric"};
    if (isAimed(type)) {
        out.emplace_back("azimuth");
        out.emplace_back("elevation");
    }
    if (type != T::Directional) {
        out.emplace_back("range");
    }
    if (type == T::Spot) {
        out.emplace_back("innerCone");
        out.emplace_back("outerCone");
    }
    if (isArea(type)) {
        out.emplace_back("width");
        out.emplace_back("height");
        out.emplace_back("radius");
    }
    return out;
}

LightPlacement placeNewLight(const glm::vec3& cameraPosition, const glm::vec3& cameraTarget,
                             scene::PunctualLight::Type type) {
    LightPlacement out;
    glm::vec3 forward = cameraTarget - cameraPosition;
    const float length = glm::length(forward);
    // A camera whose target is its own position has no forward. -Z rather than a NaN: a direction
    // that is not finite is refused by `setAuthoredLights`, so the button would fail rather than
    // place a light somewhere merely arbitrary.
    forward = length > 1e-5f ? forward / length : glm::vec3(0.0f, 0.0f, -1.0f);

    // Far enough in front to be visible and near enough to be reachable, and scaled to how far the
    // camera is looking so that it lands near the subject in a 300 m valley as well as in a 3 m
    // still life.
    const float standOff = std::clamp(length * 0.45f, 1.5f, 40.0f);
    out.position = cameraPosition + forward * standOff;

    if (type == scene::PunctualLight::Type::Directional) {
        // A sun is placed above and behind the camera's subject rather than in front of it: a
        // directional light's position does not shade anything, but it is where the gizmo sits, and
        // a sun inside the scene reads as a mistake.
        out.position = cameraPosition + forward * standOff + glm::vec3(0.0f, standOff * 0.8f, 0.0f);
    }
    out.direction = forward;
    return out;
}

std::string uniqueLightName(std::string_view desired, const std::vector<std::string>& taken) {
    const auto used = [&](const std::string& candidate) {
        return std::find(taken.begin(), taken.end(), candidate) != taken.end();
    };
    std::string base(desired.empty() ? std::string_view("Light") : desired);
    if (!used(base)) {
        return base;
    }
    for (int n = 2; n < 100000; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!used(candidate)) {
            return candidate;
        }
    }
    return base;
}

std::string duplicateLightName(std::string_view source, const std::vector<std::string>& taken) {
    // "Key Light Copy" first, and only then "Key Light Copy 2" -- the brief's own example. Going
    // straight to a number would make the first duplicate read like the second.
    return uniqueLightName(std::string(source) + " Copy", taken);
}

scene::Composition::AuthoredLight makeLight(scene::PunctualLight::Type type,
                                            const LightPlacement& placement, std::string name,
                                            std::string id) {
    using T = scene::PunctualLight::Type;
    scene::Composition::AuthoredLight out;
    scene::PunctualLight& l = out.light;
    l.name = std::move(name);
    out.id = std::move(id);
    l.type = type;
    l.position = placement.position;
    l.direction = placement.direction;
    l.enabled = true;

    // Intensity is per-kind because the unit is per-kind: a directional light is lux, a point or
    // spot is candela, and an area light is nits over its emitter. One number for all three would
    // make two of them invisible.
    switch (type) {
    case T::Directional:
        l.intensity = 3.0f;
        l.castsShadow = true;   // a sun that casts nothing does not look like a sun
        break;
    case T::Point:
        l.intensity = 40.0f;
        l.range = 0.0f;         // 0 is "no cutoff"; the inverse square is the falloff
        break;
    case T::Spot:
        l.intensity = 120.0f;   // a cone concentrates, and reads dim beside a point at the same number
        l.innerConeAngle = glm::radians(16.0f);
        l.outerConeAngle = glm::radians(28.0f);
        l.castsShadow = true;   // the whole point of a hero spot
        break;
    case T::Rect:
    default:
        l.type = T::Rect;
        l.intensity = 12.0f;
        l.width = 2.0f;
        l.height = 1.2f;
        break;
    }
    return out;
}

bool aimDirection(const glm::vec3& from, const glm::vec3& at, glm::vec3& out) {
    const glm::vec3 delta = at - from;
    if (glm::length(delta) < 1e-5f) {
        return false;
    }
    out = glm::normalize(delta);
    return true;
}

std::vector<scene::PunctualLight::Type> creatableLightTypes() {
    using T = scene::PunctualLight::Type;
    // Rect stands for "area" in the menu. Disk, Tube and Sphere are real kinds the renderer shades,
    // but they are a shape choice made after the light exists rather than four entries in a create
    // menu -- the brief asks for one "Area".
    return {T::Point, T::Spot, T::Directional, T::Rect};
}

} // namespace avgen::ui
