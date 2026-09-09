#include "scene/particles.hpp"

#include <algorithm>
#include <string>

namespace avgen::scene {

const char* fieldForceModeName(FieldForceMode mode) {
    switch (mode) {
    case FieldForceMode::Force:
        return "force";
    case FieldForceMode::Velocity:
        return "velocity";
    case FieldForceMode::Turbulence:
        return "turbulence";
    case FieldForceMode::Kill:
        return "kill";
    }
    return "force";
}

std::optional<FieldForceMode> fieldForceModeFromName(std::string_view name) {
    for (const auto mode : {FieldForceMode::Force, FieldForceMode::Velocity, FieldForceMode::Turbulence, FieldForceMode::Kill}) {
        if (name == fieldForceModeName(mode)) {
            return mode;
        }
    }
    return std::nullopt;
}

namespace {
params::ParamDesc<float> f(const std::string& base, const char* name, float def, float lo, float hi, float slo, float shi) {
    params::ParamDesc<float> d;
    d.path = base + name;
    d.defaultValue = def;
    d.hardMin = lo;
    d.hardMax = hi;
    d.softMin = slo;
    d.softMax = shi;
    return d;
}
params::ParamDesc<glm::vec3> v3(const std::string& base, const char* name, glm::vec3 def, float lo, float hi) {
    params::ParamDesc<glm::vec3> d;
    d.path = base + name;
    d.defaultValue = def;
    d.hardMin = glm::vec3(lo);
    d.hardMax = glm::vec3(hi);
    return d;
}
params::ParamDesc<glm::vec4> color(const std::string& base, const char* name, glm::vec4 def) {
    params::ParamDesc<glm::vec4> d;
    d.path = base + name;
    d.defaultValue = def;
    d.hardMin = glm::vec4(0.0f);
    d.hardMax = glm::vec4(1.0f);
    d.isColor = true;
    return d;
}
} // namespace

ParticleParameters registerParticleParameters(params::ParameterSet& params, const ParticleSystem& s) {
    const std::string base = "particles/" + s.name + "/";
    ParticleParameters p;
    p.spawnRate = &params.add(f(base, "spawnRate", s.spawnRate, 0.0f, 2000000.0f, 0.0f, std::max(s.spawnRate * 4.0f, 1000.0f)));
    p.burst = &params.add(f(base, "burst", 0.0f, 0.0f, 100000.0f, 0.0f, 500.0f));
    p.lifetime = &params.add(f(base, "lifetime", 1.0f, 0.01f, 20.0f, 0.1f, 4.0f));
    p.speed = &params.add(f(base, "speed", 1.0f, 0.0f, 50.0f, 0.0f, 5.0f));
    p.spread = &params.add(f(base, "spread", s.spread, 0.0f, 1.0f, 0.0f, 1.0f));
    p.position = &params.add(v3(base, "position", s.position, -1000.0f, 1000.0f));
    p.extent = &params.add(f(base, "extent", 1.0f, 0.0f, 100.0f, 0.0f, 5.0f));
    p.gravity = &params.add(v3(base, "gravity", s.gravity, -50.0f, 50.0f));
    p.drag = &params.add(f(base, "drag", s.drag, 0.0f, 20.0f, 0.0f, 3.0f));
    p.turbulence = &params.add(f(base, "turbulence", s.turbulence, 0.0f, 50.0f, 0.0f, 5.0f));
    p.turbulenceScale = &params.add(f(base, "turbulenceScale", s.turbulenceScale, 0.01f, 50.0f, 0.1f, 5.0f));
    p.turbulenceSpeed = &params.add(f(base, "turbulenceSpeed", s.turbulenceSpeed, 0.0f, 20.0f, 0.0f, 3.0f));
    p.attractorPosition = &params.add(v3(base, "attractorPosition", s.attractorPosition, -1000.0f, 1000.0f));
    p.attractorStrength = &params.add(f(base, "attractorStrength", s.attractorStrength, -100.0f, 100.0f, -10.0f, 10.0f));
    p.orbit = &params.add(f(base, "orbit", s.orbit, -50.0f, 50.0f, -5.0f, 5.0f));
    // Field forces (ADR-025): one strength per existing slot (1-based).
    const std::size_t fieldSlots = std::min(s.fieldForces.size(), static_cast<std::size_t>(kMaxFieldForces));
    for (std::size_t slot = 0; slot < fieldSlots; ++slot) {
        const std::string rel = "fieldForce/" + std::to_string(slot + 1) + "/strength";
        params::ParamDesc<float> d = f(base, rel.c_str(), s.fieldForces[slot].strength, -100.0f, 100.0f, -10.0f, 10.0f);
        d.label = std::string(fieldForceModeName(s.fieldForces[slot].mode)) + "/strength";
        p.fieldStrength[slot] = &params.add(std::move(d));
    }
    p.size = &params.add(f(base, "size", 1.0f, 0.0f, 100.0f, 0.0f, 5.0f));
    p.colorStart = &params.add(color(base, "colorStart", s.colorStart));
    p.colorEnd = &params.add(color(base, "colorEnd", s.colorEnd));
    p.emissive = &params.add(f(base, "emissive", s.emissive, 0.0f, 100.0f, 0.0f, 20.0f));
    {
        params::ParamDesc<bool> d;
        d.path = base + "enabled";
        d.defaultValue = s.enabled;
        d.hardMin = false;
        d.hardMax = true;
        p.enabled = &params.add(std::move(d));
    }
    return p;
}

void applyParticleParameters(const ParticleParameters& p, const ParticleSystem& rest, ParticleSystem& s) {
    if (p.spawnRate == nullptr) {
        return;
    }
    s.spawnRate = p.spawnRate->value();
    s.burst = p.burst->value();
    s.lifetimeMin = rest.lifetimeMin * p.lifetime->value();
    s.lifetimeMax = rest.lifetimeMax * p.lifetime->value();
    s.speedMin = rest.speedMin * p.speed->value();
    s.speedMax = rest.speedMax * p.speed->value();
    s.spread = p.spread->value();
    s.position = p.position->value();
    s.extent = rest.extent * p.extent->value();
    s.gravity = p.gravity->value();
    s.drag = p.drag->value();
    s.turbulence = p.turbulence->value();
    s.turbulenceScale = p.turbulenceScale->value();
    s.turbulenceSpeed = p.turbulenceSpeed->value();
    s.attractorPosition = p.attractorPosition->value();
    s.attractorStrength = p.attractorStrength->value();
    s.orbit = p.orbit->value();
    if (s.fieldForces.size() != rest.fieldForces.size()) {
        s.fieldForces = rest.fieldForces;
    }
    for (std::size_t slot = 0; slot < s.fieldForces.size() && slot < p.fieldStrength.size(); ++slot) {
        if (p.fieldStrength[slot] != nullptr) {
            s.fieldForces[slot].strength = p.fieldStrength[slot]->value();
        }
    }
    s.sizeStart = rest.sizeStart * p.size->value();
    s.sizeEnd = rest.sizeEnd * p.size->value();
    s.colorStart = p.colorStart->value();
    s.colorEnd = p.colorEnd->value();
    s.emissive = p.emissive->value();
    s.enabled = p.enabled->value();
}

} // namespace avgen::scene
