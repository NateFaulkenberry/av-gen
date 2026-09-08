#pragma once

// glTF-driven scene controller: imports a file, frames it with an orbiting camera, and exposes
// a curated parameter surface (audiovisual-systems lesson 8) for modulation:
//   root/scale, root/rotationSpeed, root/impulse, root/height   -> whole-scene transform
//   material/emissiveBoost, material/roughnessScale            -> applied to every material
//   camera/distance, camera/height, camera/orbitSpeed, camera/fov
//   env/intensity, env/rotation, env/skyboxBlur, scene/brightness, scene/gridIntensity
//   lights/<name>/intensity for imported lights
// Default routes: bass -> root/scale, mid -> root/rotationSpeed, treble -> material/emissiveBoost,
// rms -> scene/brightness, onset -> root/impulse.

#include "core/error.hpp"
#include "scene/scene_controller.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace avgen::scene {

class GltfScene final : public SceneController {
public:
    // Loads the file; registers parameters and routes on success.
    static Result<std::unique_ptr<GltfScene>> load(const std::filesystem::path& path, params::ParameterSet& params,
                                                   params::Modulator& modulator);

    [[nodiscard]] std::string name() const override { return name_; }
    void update(const FrameTime& time) override;
    [[nodiscard]] const Scene& scene() const override { return scene_; }
    [[nodiscard]] Scene& scene() override { return scene_; }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] glm::vec3 boundsCenter() const { return center_; }
    [[nodiscard]] float boundsRadius() const { return radius_; }
    [[nodiscard]] float currentAngle() const { return angle_; }

    static void addDefaultRoutes(params::Modulator& modulator);

private:
    GltfScene() = default;
    void registerParameters(params::ParameterSet& params);

    struct RestState {
        Transform transform;
        float emissiveIntensity = 0.0f;
        float roughness = 0.5f;
    };
    struct LightBinding {
        std::size_t index;
        params::Parameter<float>* intensity;
        float rest;
    };

    Scene scene_;
    std::string name_;
    std::filesystem::path path_;
    std::vector<RestState> rest_;
    std::vector<LightBinding> lightBindings_;
    glm::vec3 center_{0.0f};
    float radius_ = 1.0f;
    float angle_ = 0.0f;
    float cameraAngle_ = 0.0f;

    params::Parameter<float>* rootScale_ = nullptr;
    params::Parameter<float>* rootRotationSpeed_ = nullptr;
    params::Parameter<float>* rootImpulse_ = nullptr;
    params::Parameter<float>* rootHeight_ = nullptr;
    params::Parameter<float>* emissiveBoost_ = nullptr;
    params::Parameter<float>* roughnessScale_ = nullptr;
    params::Parameter<float>* cameraDistance_ = nullptr;
    params::Parameter<float>* cameraHeight_ = nullptr;
    params::Parameter<float>* cameraOrbitSpeed_ = nullptr;
    params::Parameter<float>* cameraFov_ = nullptr;
    params::Parameter<float>* envIntensity_ = nullptr;
    params::Parameter<float>* envRotation_ = nullptr;
    params::Parameter<float>* skyboxBlur_ = nullptr;
    params::Parameter<float>* brightness_ = nullptr;
    params::Parameter<float>* gridIntensity_ = nullptr;
};

} // namespace avgen::scene
