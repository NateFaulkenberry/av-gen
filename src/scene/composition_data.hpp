#pragma once

// Composition data (ADR-038): what a frame is about. Focal points, depth layers and exclusion
// regions are ordinary spatial data; they reach generators, lighting and the camera as fields and
// attributes, so no system needs a special case for them.

#include "core/error.hpp"
#include "spatial/field.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::scene {

// Something the frame is about. Generators thin out inside `clearance`, lighting raises contrast
// within `radius`, and a camera behaviour can frame it.
struct FocalPoint {
    std::string name = "focus";
    glm::vec3 position{0.0f};
    float radius = 8.0f;       // the region of interest
    float clearance = 0.0f;    // generators leave this radius empty (0 = no clearance)
    float weight = 1.0f;       // relative importance when several exist
    std::string boundObject;   // optional: track this object's bounds centre instead of `position`
};

// A distance band from the camera with its own look. Bands are ordered near to far and clamp at
// the last one; a scene with no bands behaves exactly as today.
struct DepthLayer {
    std::string name = "mid";
    float start = 0.0f;        // metres from the camera
    float end = 60.0f;
    float density = 1.0f;      // multiplies generator density in the band
    float contrast = 1.0f;     // multiplies lighting contrast
    float saturation = 1.0f;
    float detail = 1.0f;       // multiplies material detail amplitude and LOD bias
};

// A volume generators must avoid (negative space as an instruction).
struct ExclusionRegion {
    enum class Shape : std::uint8_t { Sphere, Box };
    std::string name = "clear";
    Shape shape = Shape::Sphere;
    glm::vec3 position{0.0f};
    glm::vec3 size{10.0f};     // box half extents
    float radius = 10.0f;      // sphere
    float softness = 2.0f;     // edge width, so the boundary is not a hard cut
    bool invert = false;       // keep only what is inside
};

struct CompositionData {
    std::vector<FocalPoint> focalPoints;
    std::vector<DepthLayer> layers;
    std::vector<ExclusionRegion> exclusions;
    // Which focal point the camera frames, and where on screen it should sit (normalised, with
    // (0.5, 0.5) the centre and (0.333, 0.5) the left third).
    std::string cameraTarget;
    glm::vec2 targetScreenPosition{0.5f, 0.5f};
    float framingStrength = 0.0f; // 0 = the camera behaviour is untouched, 1 = fully framed

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] const FocalPoint* find(std::string_view name) const;
    // The composition as fields, appended to `out` with reserved names ("composition.clearance",
    // "composition.exclusion", "composition.weight"), so density filters and effectors can use
    // them with no new mechanism.
    void appendFields(spatial::FieldSet& out) const;
    // The band a distance falls in (the last band when past the end; identity when empty).
    [[nodiscard]] DepthLayer layerAt(float distance) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<CompositionData> fromJson(const nlohmann::json& j);
};

} // namespace avgen::scene
