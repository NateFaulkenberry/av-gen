#pragma once

// Milestone 0.1 scene: a luminous orb over a grid in a dark environment. Every animated property
// is a parameter; audio reaches the scene only through modulation routes (ADR-011).
//   audio.bass  -> orb/scale            audio.mid    -> orb/rotationSpeed
//   audio.treble-> orb/emissive         audio.rms    -> scene/brightness
//   audio.onset -> orb/impulse (envelope)

#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene.hpp"
#include "scene/scene_controller.hpp"

namespace avgen::scene {

class OrbScene final : public SceneController {
public:
    // Registers parameters in `params` and default routes in `modulator`. The caller binds the
    // modulator once signals are declared.
    OrbScene(params::ParameterSet& params, params::Modulator& modulator);

    [[nodiscard]] std::string name() const override { return "orb"; }
    // Reads parameter finals and writes the Scene. Rotation integrates rotationSpeed * dt.
    void update(const FrameTime& time) override;
    [[nodiscard]] const Scene& scene() const override { return scene_; }
    [[nodiscard]] Scene& scene() override { return scene_; }

    // Adds the five default modulation routes (idempotent by target).
    static void addDefaultRoutes(params::Modulator& modulator);

    // Parameter handles.
    params::Parameter<float>& scale() { return *scale_; }
    params::Parameter<float>& rotationSpeed() { return *rotationSpeed_; }
    params::Parameter<float>& emissive() { return *emissive_; }
    params::Parameter<float>& impulse() { return *impulse_; }
    params::Parameter<float>& brightness() { return *brightness_; }
    params::Parameter<glm::vec3>& baseColor() { return *baseColor_; }
    params::Parameter<glm::vec3>& emissiveColor() { return *emissiveColor_; }
    params::Parameter<float>& cameraDistance() { return *cameraDistance_; }
    params::Parameter<float>& cameraHeight() { return *cameraHeight_; }
    params::Parameter<float>& cameraOrbitSpeed() { return *cameraOrbitSpeed_; }
    params::Parameter<float>& gridIntensity() { return *gridIntensity_; }

    [[nodiscard]] float currentAngle() const { return angle_; }
    [[nodiscard]] const ParticleParameters& sparks() const { return sparks_; }
    [[nodiscard]] float currentCameraAngle() const { return cameraAngle_; }

private:
    Scene scene_;
    std::size_t orbEntity_ = 0;
    std::size_t gridEntity_ = 0;
    float angle_ = 0.0f;
    float cameraAngle_ = 0.0f;

    params::Parameter<float>* scale_ = nullptr;
    params::Parameter<float>* rotationSpeed_ = nullptr;
    params::Parameter<float>* emissive_ = nullptr;
    params::Parameter<float>* impulse_ = nullptr;
    params::Parameter<float>* brightness_ = nullptr;
    params::Parameter<glm::vec3>* baseColor_ = nullptr;
    params::Parameter<glm::vec3>* emissiveColor_ = nullptr;
    params::Parameter<float>* cameraDistance_ = nullptr;
    params::Parameter<float>* cameraHeight_ = nullptr;
    params::Parameter<float>* cameraOrbitSpeed_ = nullptr;
    params::Parameter<float>* gridIntensity_ = nullptr;
    ParticleParameters sparks_;
    ParticleSystem sparksRest_;
};

} // namespace avgen::scene
