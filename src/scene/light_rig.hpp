#pragma once

// Light rigs (ADR-033): a named lighting setup expressed relative to the subject and the camera,
// so the same rig lights any world. A rig expands into ordinary `PunctualLight`s at build time;
// nothing downstream knows a rig existed.
//
// Placement frame: azimuth is measured from the camera's view direction about the subject's up
// axis (0 = behind the camera pointing at the subject, +90 = camera right), elevation from the
// horizon, and distance in subject radii. That makes a rig reusable at any scale: "key at 35
// degrees, 20 up, 2.5 radii out" means the same thing in a room and in a cathedral.

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::scene {

struct RigLight {
    std::string name;
    PunctualLight::Type type = PunctualLight::Type::Rect;
    PunctualLight::Role role = PunctualLight::Role::Key;
    float azimuthDegrees = 35.0f;
    float elevationDegrees = 25.0f;
    float distanceRadii = 2.5f;   // multiplied by the subject radius
    float intensity = 1.0f;       // relative to the rig's `keyIntensity`
    glm::vec3 color{1.0f};
    float temperature = 5600.0f;
    float tint = 0.0f;
    float sizeRadii = 0.5f;       // area emitters: size in subject radii
    float aspect = 1.0f;          // Rect: width / height
    bool castsShadow = false;
    float shadowStrength = 1.0f;
    float softness = 1.0f;
    float volumetricStrength = 0.0f;
    float coneDegrees = 45.0f;    // Spot
    bool followCamera = true;     // azimuth is relative to the camera when true, to world +Z when false
};

struct LightRig {
    std::string name = "rig";
    std::string description;
    float keyIntensity = 3.0f;    // the key's absolute intensity; other lights scale from it
    float ambientIntensity = 0.15f;
    glm::vec3 ambientColor{0.5f, 0.6f, 0.8f};
    float ambientTemperature = 9000.0f; // sky-cool ambient by default
    std::vector<RigLight> lights;

    [[nodiscard]] Result<void> validate() const;
    // Expands into world lights around `subjectCenter` with `subjectRadius`, oriented against
    // `cameraPosition`. Deterministic and pure.
    [[nodiscard]] std::vector<PunctualLight> expand(const glm::vec3& subjectCenter, float subjectRadius,
                                                    const glm::vec3& cameraPosition, const glm::vec3& up) const;
    [[nodiscard]] std::uint64_t structuralHash() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<LightRig> fromJson(const nlohmann::json& j);
    static Result<LightRig> loadFile(const std::filesystem::path& path);
};

// Parameters: "lightrig/<name>/keyIntensity|ambientIntensity|ambientColor|ambientTemperature" and
// "lightrig/<name>/<light>/intensity|azimuth|elevation|distance|temperature|size|shadowStrength".
struct LightRigParameters {
    std::string prefix;
    std::vector<params::IParameter*> all;
    params::Parameter<float>* keyIntensity = nullptr;
    params::Parameter<float>* ambientIntensity = nullptr;
    std::vector<params::Parameter<float>*> lightIntensity;
};
[[nodiscard]] LightRigParameters registerLightRigParameters(params::ParameterSet& params, const LightRig& rest,
                                                            const std::string& prefix);
void applyLightRigParameters(const LightRigParameters& p, const LightRig& rest, LightRig& live);
void unregisterLightRigParameters(params::ParameterSet& params, const LightRigParameters& p);

} // namespace avgen::scene
