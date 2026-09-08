#include "scene/orb_scene.hpp"

#include "scene/mesh_generators.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

params::ParamDesc<float> floatDesc(const char* path, float def, float hardMin, float hardMax) {
    return params::ParamDesc<float>{
        .path = path, .defaultValue = def, .hardMin = hardMin, .hardMax = hardMax};
}

params::ParamDesc<float> floatDesc(const char* path, float def, float hardMin, float hardMax, float softMin,
                                   float softMax) {
    auto desc = floatDesc(path, def, hardMin, hardMax);
    desc.softMin = softMin;
    desc.softMax = softMax;
    return desc;
}

params::ParamDesc<glm::vec3> colorDesc(const char* path, glm::vec3 def) {
    return params::ParamDesc<glm::vec3>{.path = path,
                                        .defaultValue = def,
                                        .hardMin = glm::vec3(0.0f),
                                        .hardMax = glm::vec3(1.0f),
                                        .isColor = true};
}

bool hasRouteTo(const params::Modulator& modulator, const std::string& target) {
    const auto& routes = modulator.routes();
    return std::any_of(routes.begin(), routes.end(),
                       [&](const params::ModRoute& r) { return r.target == target; });
}

} // namespace

OrbScene::OrbScene(params::ParameterSet& params, params::Modulator& modulator) {
    scale_ = &params.add(floatDesc("orb/scale", 1.0f, 0.05f, 8.0f, 0.2f, 3.0f));
    rotationSpeed_ = &params.add(floatDesc("orb/rotationSpeed", 0.4f, -20.0f, 20.0f, -3.0f, 3.0f));
    emissive_ = &params.add(floatDesc("orb/emissive", 0.15f, 0.0f, 40.0f, 0.0f, 8.0f));
    impulse_ = &params.add(floatDesc("orb/impulse", 0.0f, 0.0f, 4.0f, 0.0f, 1.0f));
    baseColor_ = &params.add(colorDesc("orb/baseColor", glm::vec3(0.75f, 0.2f, 0.9f)));
    emissiveColor_ = &params.add(colorDesc("orb/emissiveColor", glm::vec3(0.9f, 0.45f, 1.0f)));
    brightness_ = &params.add(floatDesc("scene/brightness", 1.0f, 0.0f, 8.0f, 0.0f, 3.0f));
    gridIntensity_ = &params.add(floatDesc("scene/gridIntensity", 0.6f, 0.0f, 4.0f));
    cameraDistance_ = &params.add(floatDesc("camera/distance", 7.0f, 1.0f, 40.0f, 2.0f, 15.0f));
    cameraHeight_ = &params.add(floatDesc("camera/height", 2.2f, -5.0f, 15.0f));
    cameraOrbitSpeed_ = &params.add(floatDesc("camera/orbitSpeed", 0.08f, -3.0f, 3.0f));

    const MeshId orbMesh = scene_.addMesh(makeIcosphere(1.0f, 3));
    const MeshId gridMesh = scene_.addMesh(makePlane(12.0f, 48));

    orbEntity_ = scene_.entities.size();
    Entity& orb = scene_.addEntity("orb", orbMesh);
    orb.style = MeshStyle::Lit;
    orb.transform.position = glm::vec3(0.0f, 1.5f, 0.0f);

    gridEntity_ = scene_.entities.size();
    Entity& grid = scene_.addEntity("grid", gridMesh);
    grid.style = MeshStyle::Grid;
    grid.transform.position = glm::vec3(0.0f);
    // Key light (glTF directional semantics: `direction` is the way the light travels).
    scene::PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
    key.color = glm::vec3(1.0f, 0.95f, 0.9f);
    key.intensity = 3.0f;
    scene_.addLight(key);

    // Sparks: an orbiting spark cloud around the orb, driven by bass (spawn), highs (turbulence)
    // and onsets (bursts) through the default routes.
    ParticleSystem sparks;
    sparks.name = "sparks";
    sparks.capacity = 131072;
    sparks.shape = EmitterShape::Sphere;
    sparks.position = glm::vec3(0.0f, 1.5f, 0.0f);
    sparks.extent = glm::vec3(1.1f);
    sparks.spawnRate = 1500.0f;
    sparks.lifetimeMin = 1.5f;
    sparks.lifetimeMax = 3.5f;
    sparks.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    sparks.spread = 1.0f;
    sparks.speedMin = 0.2f;
    sparks.speedMax = 0.8f;
    sparks.gravity = glm::vec3(0.0f, 0.15f, 0.0f);
    sparks.drag = 0.6f;
    sparks.turbulence = 1.2f;
    sparks.turbulenceScale = 0.8f;
    sparks.attractorPosition = glm::vec3(0.0f, 1.5f, 0.0f);
    sparks.attractorStrength = 0.6f;
    sparks.attractorRadius = 4.0f;
    sparks.orbit = 1.2f;
    sparks.sizeStart = 0.022f;
    sparks.sizeEnd = 0.0f;
    sparks.colorStart = glm::vec4(1.0f, 0.45f, 0.85f, 0.9f);
    sparks.colorEnd = glm::vec4(0.3f, 0.5f, 1.0f, 0.0f);
    sparks.emissive = 1.2f;
    sparksRest_ = sparks;
    scene_.particles.push_back(sparks);
    sparks_ = registerParticleParameters(params, sparksRest_);

    addDefaultRoutes(modulator);
    update(FrameTime{});
}

void OrbScene::addDefaultRoutes(params::Modulator& modulator) {
    using params::ModRoute;
    using params::ProcessorChain;

    auto addIfMissing = [&](ModRoute route) {
        if (!hasRouteTo(modulator, route.target)) {
            modulator.addRoute(std::move(route));
        }
    };

    {
        ModRoute r{.source = "audio.bass", .target = "orb/scale", .amount = 0.9f};
        r.chain.curve = params::CurveType::Power;
        r.chain.curveAmount = 0.8f;
        r.chain.attackMs = 15.0f;
        r.chain.decayMs = 180.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.mid", .target = "orb/rotationSpeed", .amount = 3.0f};
        r.chain.attackMs = 50.0f;
        r.chain.decayMs = 400.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.treble", .target = "orb/emissive", .amount = 2.0f};
        r.chain.attackMs = 10.0f;
        r.chain.decayMs = 250.0f;
        addIfMissing(std::move(r));
    }

    {
        ModRoute r{.source = "audio.rms", .target = "scene/brightness", .amount = 0.5f};
        r.chain.attackMs = 30.0f;
        r.chain.decayMs = 500.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.onset", .target = "orb/impulse", .amount = 0.6f};
        r.chain.envelope = params::EnvelopeMode::PeakHold;
        r.chain.envelopeHoldMs = 30.0f;
        r.chain.envelopeFallPerSecond = 4.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.bass", .target = "particles/sparks/spawnRate", .amount = 12000.0f};
        r.chain.curve = params::CurveType::Power;
        r.chain.curveAmount = 1.5f;
        r.chain.attackMs = 20.0f;
        r.chain.decayMs = 250.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.treble", .target = "particles/sparks/turbulence", .amount = 4.0f};
        r.chain.attackMs = 10.0f;
        r.chain.decayMs = 300.0f;
        addIfMissing(std::move(r));
    }
    {
        ModRoute r{.source = "audio.onset", .target = "particles/sparks/burst", .amount = 400.0f};
        addIfMissing(std::move(r));
    }
}

void OrbScene::update(const FrameTime& time) {
    const auto dt = static_cast<float>(time.deltaTime);
    angle_ += rotationSpeed_->value() * dt;
    cameraAngle_ += cameraOrbitSpeed_->value() * dt;

    Entity& orb = scene_.entities[orbEntity_];
    orb.transform.scale = glm::vec3(scale_->value() + impulse_->value());
    // Float above the grid: the orb's bottom stays 0.5 m over the floor however large it gets.
    orb.transform.position.y = 0.5f + orb.transform.scale.y;
    orb.transform.rotation = glm::angleAxis(angle_, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    orb.material.baseColor = baseColor_->value();
    orb.material.emissiveColor = emissiveColor_->value();
    orb.material.emissiveIntensity = emissive_->value();

    if (!scene_.particles.empty()) {
        applyParticleParameters(sparks_, sparksRest_, scene_.particles[0]);
        // Sparks follow the orb.
        scene_.particles[0].position = orb.transform.position;
        scene_.particles[0].attractorPosition = orb.transform.position;
        scene_.particles[0].extent = sparksRest_.extent * sparks_.extent->value() * orb.transform.scale.x;
    }
    scene_.environment.brightness = brightness_->value();
    scene_.environment.gridIntensity = gridIntensity_->value();

    const float distance = cameraDistance_->value();
    scene_.camera.position = glm::vec3(std::sin(cameraAngle_) * distance, cameraHeight_->value(),
                                       std::cos(cameraAngle_) * distance);
    scene_.camera.target = glm::vec3(0.0f, orb.transform.position.y - 0.3f, 0.0f);
}

} // namespace avgen::scene
