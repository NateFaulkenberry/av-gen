// TEMPORARY: minimal definitions for the cinematic-phase headers so the tree links while the
// camera and composition agents implement them. The lighting half moved to scene/light_rig.cpp
// (ADR-033); what remains is the lens/exposure maths (ADR-037) and CompositionData (ADR-038),
// each replaced by its own agent's implementation. Delete this file when both land.
#include "scene/composition_data.hpp"
#include "scene/light_rig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

float LensSettings::fovYRadians() const {
    return 2.0f * std::atan(sensorHeight / (2.0f * std::max(focalLength, 1e-3f)));
}
float LensSettings::circleOfConfusion(float distance) const {
    const float f = focalLength;
    const float s = std::max(focusDistance * 1000.0f, f + 1e-3f); // millimetres
    const float d = std::max(distance * 1000.0f, 1e-3f);
    const float aperture_ = f / std::max(aperture, 0.05f);
    return std::abs(aperture_ * f * (d - s) / (d * (s - f)));
}
float Camera::effectiveFovY() const {
    return lens.useExplicitFov ? fovYRadians : lens.fovYRadians();
}

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
