#include "scene/gltf_scene.hpp"

#include "assets/gltf_loader.hpp"
#include "core/log.hpp"
#include "scene/mesh_generators.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {
params::ParamDesc<float> floatDesc(std::string path, float def, float lo, float hi, float softLo, float softHi) {
    params::ParamDesc<float> d;
    d.path = std::move(path);
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = softLo;
    d.softMax = softHi;
    return d;
}

std::string sanitise(std::string name) {
    for (auto& c : name) {
        if (c == '/' || c == ' ' || c == '.') {
            c = '_';
        }
    }
    return name;
}
} // namespace

Result<std::unique_ptr<GltfScene>> GltfScene::load(const std::filesystem::path& path, params::ParameterSet& params,
                                                   params::Modulator& modulator) {
    std::unique_ptr<GltfScene> ctrl(new GltfScene());
    auto summary = assets::loadGltf(path, ctrl->scene_);
    if (!summary) {
        return std::unexpected(summary.error());
    }
    for (const auto& warning : summary->warnings) {
        log::warn("glTF '{}': {}", path.filename().string(), warning);
    }
    ctrl->path_ = path;
    ctrl->name_ = path.stem().string();

    // Rest state: imported transforms and material values the parameters modulate around.
    ctrl->rest_.reserve(ctrl->scene_.entities.size());
    for (const auto& e : ctrl->scene_.entities) {
        ctrl->rest_.push_back({e.transform, e.material.emissiveIntensity, e.material.roughness});
    }
    const auto [lo, hi] = ctrl->scene_.bounds();
    ctrl->center_ = (lo + hi) * 0.5f;
    ctrl->radius_ = std::max(glm::length(hi - lo) * 0.5f, 0.05f);

    // Optional floor grid just under the scene (off by default via scene/gridIntensity = 0).
    {
        const MeshId grid = ctrl->scene_.addMesh(makePlane(std::max(ctrl->radius_ * 6.0f, 4.0f), 48));
        auto& g = ctrl->scene_.addEntity("grid", grid);
        g.style = MeshStyle::Grid;
        g.transform.position = glm::vec3(ctrl->center_.x, lo.y - 0.001f, ctrl->center_.z);
        ctrl->rest_.push_back({g.transform, 0.0f, 0.5f});
    }
    // A key light if the file has none, so untextured scenes are visible without an environment.
    if (ctrl->scene_.lights.empty()) {
        PunctualLight key;
        key.name = "key";
        key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
        key.color = glm::vec3(1.0f, 0.97f, 0.92f);
        key.intensity = 3.0f;
        ctrl->scene_.addLight(key);
    }
    if (!ctrl->scene_.cameras.empty()) {
        ctrl->scene_.camera = ctrl->scene_.cameras.front();
    }
    ctrl->scene_.environment.gridIntensity = 0.0f;
    ctrl->scene_.environment.backgroundColor = glm::vec3(0.02f, 0.02f, 0.03f);

    ctrl->registerParameters(params);
    addDefaultRoutes(modulator);
    log::info("scene '{}': {} entities, {} meshes, {} textures, {} lights, radius {:.2f}", ctrl->name_,
              summary->entities, summary->meshes, summary->textures, summary->lights, ctrl->radius_);
    return ctrl;
}

void GltfScene::registerParameters(params::ParameterSet& params) {
    const float fitDistance = radius_ / std::tan(0.87f * 0.5f) * 1.15f;
    rootScale_ = &params.add(floatDesc("root/scale", 1.0f, 0.05f, 8.0f, 0.2f, 3.0f));
    rootRotationSpeed_ = &params.add(floatDesc("root/rotationSpeed", 0.15f, -20.0f, 20.0f, -3.0f, 3.0f));
    rootImpulse_ = &params.add(floatDesc("root/impulse", 0.0f, 0.0f, 4.0f, 0.0f, 1.0f));
    rootHeight_ = &params.add(floatDesc("root/height", 0.0f, -50.0f, 50.0f, -radius_, radius_));
    emissiveBoost_ = &params.add(floatDesc("material/emissiveBoost", 1.0f, 0.0f, 50.0f, 0.0f, 8.0f));
    roughnessScale_ = &params.add(floatDesc("material/roughnessScale", 1.0f, 0.0f, 2.0f, 0.0f, 2.0f));
    cameraDistance_ = &params.add(floatDesc("camera/distance", fitDistance, 0.01f, 1000.0f, fitDistance * 0.3f, fitDistance * 3.0f));
    cameraHeight_ = &params.add(floatDesc("camera/height", center_.y + radius_ * 0.35f, -1000.0f, 1000.0f,
                                          center_.y - radius_, center_.y + radius_ * 2.0f));
    cameraOrbitSpeed_ = &params.add(floatDesc("camera/orbitSpeed", 0.12f, -3.0f, 3.0f, -1.0f, 1.0f));
    cameraFov_ = &params.add(floatDesc("camera/fov", 50.0f, 5.0f, 120.0f, 20.0f, 90.0f));
    envIntensity_ = &params.add(floatDesc("env/intensity", 1.0f, 0.0f, 20.0f, 0.0f, 4.0f));
    envRotation_ = &params.add(floatDesc("env/rotation", 0.0f, -6.2832f, 6.2832f, -3.1416f, 3.1416f));
    skyboxBlur_ = &params.add(floatDesc("env/skyboxBlur", 0.0f, 0.0f, 1.0f, 0.0f, 1.0f));
    brightness_ = &params.add(floatDesc("scene/brightness", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f));
    gridIntensity_ = &params.add(floatDesc("scene/gridIntensity", 0.0f, 0.0f, 4.0f, 0.0f, 2.0f));
    for (std::size_t i = 0; i < scene_.lights.size(); ++i) {
        auto& light = scene_.lights[i];
        const std::string base = "lights/" + sanitise(light.name.empty() ? "light" + std::to_string(i) : light.name);
        auto* p = &params.add(floatDesc(base + "/intensity", light.intensity, 0.0f, std::max(light.intensity * 10.0f, 100.0f),
                                        0.0f, std::max(light.intensity * 3.0f, 10.0f)));
        lightBindings_.push_back({i, p, light.intensity});
    }
}

void GltfScene::addDefaultRoutes(params::Modulator& modulator) {
    using namespace params;
    auto hasTarget = [&](const char* target) {
        for (const auto& r : modulator.routes()) {
            if (r.target == target) {
                return true;
            }
        }
        return false;
    };
    if (!hasTarget("root/scale")) {
        ModRoute r{.source = "audio.bass", .target = "root/scale", .amount = 0.35f};
        r.chain.curve = CurveType::Power;
        r.chain.curveAmount = 0.8f;
        r.chain.attackMs = 15.0f;
        r.chain.decayMs = 180.0f;
        modulator.addRoute(r);
    }
    if (!hasTarget("root/rotationSpeed")) {
        ModRoute r{.source = "audio.mid", .target = "root/rotationSpeed", .amount = 1.5f};
        r.chain.attackMs = 50.0f;
        r.chain.decayMs = 400.0f;
        modulator.addRoute(r);
    }
    if (!hasTarget("material/emissiveBoost")) {
        ModRoute r{.source = "audio.treble", .target = "material/emissiveBoost", .amount = 3.0f};
        r.chain.attackMs = 10.0f;
        r.chain.decayMs = 250.0f;
        modulator.addRoute(r);
    }
    if (!hasTarget("scene/brightness")) {
        ModRoute r{.source = "audio.rms", .target = "scene/brightness", .amount = 0.5f};
        r.chain.attackMs = 30.0f;
        r.chain.decayMs = 500.0f;
        modulator.addRoute(r);
    }
    if (!hasTarget("root/impulse")) {
        ModRoute r{.source = "audio.onset", .target = "root/impulse", .amount = 0.25f};
        r.chain.envelope = EnvelopeMode::PeakHold;
        r.chain.envelopeHoldMs = 30.0f;
        r.chain.envelopeFallPerSecond = 4.0f;
        modulator.addRoute(r);
    }
}

void GltfScene::update(const FrameTime& time) {
    const float dt = static_cast<float>(time.deltaTime);
    angle_ += rootRotationSpeed_->value() * dt;
    cameraAngle_ += cameraOrbitSpeed_->value() * dt;

    // Root transform: uniform scale about the bounds centre, rotation about +Y, vertical offset.
    const float scale = rootScale_->value() + rootImpulse_->value();
    const glm::quat rootRot = glm::angleAxis(angle_, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 rootPos = center_ + glm::vec3(0.0f, rootHeight_->value(), 0.0f);
    for (std::size_t i = 0; i < scene_.entities.size() && i < rest_.size(); ++i) {
        auto& e = scene_.entities[i];
        const auto& rest = rest_[i];
        const glm::vec3 local = rest.transform.position - center_;
        e.transform.position = rootPos + rootRot * (local * scale);
        e.transform.rotation = rootRot * rest.transform.rotation;
        e.transform.scale = rest.transform.scale * scale;
        if (e.style == MeshStyle::Lit) {
            e.material.emissiveIntensity = rest.emissiveIntensity * emissiveBoost_->value();
            e.material.roughness = std::clamp(rest.roughness * roughnessScale_->value(), 0.0f, 1.0f);
        }
    }
    for (const auto& lb : lightBindings_) {
        if (lb.index < scene_.lights.size()) {
            scene_.lights[lb.index].intensity = lb.intensity->value();
        }
    }

    // Orbit camera around the (offset) centre.
    const float dist = cameraDistance_->value();
    scene_.camera.position = rootPos + glm::vec3(std::sin(cameraAngle_) * dist, 0.0f, std::cos(cameraAngle_) * dist);
    scene_.camera.position.y = cameraHeight_->value() + rootHeight_->value();
    scene_.camera.target = rootPos;
    scene_.camera.fovYRadians = glm::radians(cameraFov_->value());
    scene_.camera.nearPlane = std::max(radius_ * 0.01f, 0.01f);
    scene_.camera.farPlane = std::max(radius_ * 50.0f, 100.0f);

    scene_.environment.brightness = brightness_->value();
    scene_.environment.gridIntensity = gridIntensity_->value();
    scene_.environment.environmentIntensity = envIntensity_->value();
    scene_.environment.environmentRotation = envRotation_->value();
    scene_.environment.skyboxBlur = skyboxBlur_->value();
}

} // namespace avgen::scene
