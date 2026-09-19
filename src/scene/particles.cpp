#include "scene/particles.hpp"

#include <algorithm>
#include <cmath>
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
// The exact rule shaders/particles.wgsl `curveAt` implements: clamp below the first key, clamp
// above the last, linear between the bracketing pair. Keys are assumed ascending in t.
template <typename Key, typename Value, typename Get>
Value evaluateCurve(const std::vector<Key>& keys, float t, Get get, Value fallback) {
    if (keys.size() < 2) {
        return keys.empty() ? fallback : get(keys.front());
    }
    const std::size_t n = std::min(keys.size(), static_cast<std::size_t>(kMaxCurveKeys));
    if (t <= keys[0].t) {
        return get(keys[0]);
    }
    for (std::size_t i = 1; i < n; ++i) {
        if (t <= keys[i].t) {
            const float span = keys[i].t - keys[i - 1].t;
            const float u = span > 1e-6f ? (t - keys[i - 1].t) / span : 0.0f;
            return get(keys[i - 1]) + (get(keys[i]) - get(keys[i - 1])) * u;
        }
    }
    return get(keys[n - 1]);
}
} // namespace

float ParticleCurve::evaluate(float t) const {
    return evaluateCurve<CurveKey, float>(
        keys, t, [](const CurveKey& k) { return k.value; }, 0.0f);
}

glm::vec3 ParticleColorCurve::evaluate(float t) const {
    return evaluateCurve<ColorKey, glm::vec3>(
        keys, t, [](const ColorKey& k) { return k.color; }, glm::vec3(1.0f));
}

float particleStretchLength(float speed, float shutterSeconds, const ParticleSystem& s) {
    if (s.velocityStretch <= 0.0f || shutterSeconds <= 0.0f) {
        return 0.0f;
    }
    const float length = std::max(0.0f, speed) * shutterSeconds * s.velocityStretch;
    if (length < s.stretchMin) {
        return 0.0f; // slow particles stay round
    }
    return std::min(length, std::max(0.0f, s.stretchMax));
}

std::uint32_t trailHistoryPoints(const ParticleSystem& s) {
    if (!s.trailEnabled) {
        return 0;
    }
    return std::clamp<std::uint32_t>(s.trailLength, 2, kMaxTrailPoints) - 1;
}

std::uint64_t trailMemoryBytes(const ParticleSystem& s) {
    return static_cast<std::uint64_t>(s.capacity) * trailHistoryPoints(s) * kTrailBytesPerPoint;
}

Result<void> validateParticleSystem(const ParticleSystem& s) {
    auto checkCurve = [&](const char* what, std::size_t count, auto at) -> Result<void> {
        if (count > static_cast<std::size_t>(kMaxCurveKeys)) {
            return fail("particle system '{}': {} has {} keys, at most {} are allowed", s.name, what, count,
                        kMaxCurveKeys);
        }
        for (std::size_t i = 1; i < count; ++i) {
            if (at(i) < at(i - 1)) {
                return fail("particle system '{}': {} keys must be sorted by t", s.name, what);
            }
        }
        return Result<void>{};
    };
    if (auto r = checkCurve("sizeCurve", s.sizeCurve.keys.size(), [&](std::size_t i) { return s.sizeCurve.keys[i].t; }); !r) {
        return r;
    }
    if (auto r = checkCurve("opacityCurve", s.opacityCurve.keys.size(),
                            [&](std::size_t i) { return s.opacityCurve.keys[i].t; });
        !r) {
        return r;
    }
    if (auto r = checkCurve("colorCurve", s.colorCurve.keys.size(), [&](std::size_t i) { return s.colorCurve.keys[i].t; });
        !r) {
        return r;
    }
    if (s.trailEnabled) {
        if (s.trailLength < 2 || s.trailLength > kMaxTrailPoints) {
            return fail("particle system '{}': trailLength {} is out of range 2..{}", s.name, s.trailLength,
                        kMaxTrailPoints);
        }
        if (s.trailStride == 0 || s.trailStride > 64) {
            return fail("particle system '{}': trailStride {} is out of range 1..64", s.name, s.trailStride);
        }
        const std::uint64_t bytes = trailMemoryBytes(s);
        if (bytes > kMaxTrailBytes) {
            return fail("particle system '{}': a {}-point trail over {} particles needs {} MiB of history, over the "
                        "{} MiB budget; lower the capacity or the trail length, or use velocityStretch instead",
                        s.name, s.trailLength, s.capacity, bytes >> 20, kMaxTrailBytes >> 20);
        }
    }
    return Result<void>{};
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

glm::vec3 particleExtentFromRadius(const glm::vec3& authored, float radius) {
    const float base = std::max(authored.x, 1e-4f);
    const float ratio = radius / base;
    return glm::vec3(radius, authored.y * ratio, authored.z * ratio);
}

ParticleParameters registerParticleParameters(params::ParameterSet& params, const ParticleSystem& s) {
    const std::string base = "particles/" + s.name + "/";
    ParticleParameters p;
    p.spawnRate = &params.add(f(base, "spawnRate", s.spawnRate, 0.0f, 2000000.0f, 0.0f, std::max(s.spawnRate * 4.0f, 1000.0f)));
    p.burst = &params.add(f(base, "burst", 0.0f, 0.0f, 100000.0f, 0.0f, 500.0f));
    p.lifetime = &params.add(f(base, "lifetime", 1.0f, 0.01f, 20.0f, 0.1f, 4.0f));
    p.speed = &params.add(f(base, "speed", 1.0f, 0.0f, 50.0f, 0.0f, 5.0f));
    p.spread = &params.add(f(base, "spread", s.spread, 0.0f, 1.0f, 0.0f, 1.0f));
    p.position = &params.add(v3(base, "position", s.position, -1000.0f, 1000.0f));
    // Absolute metres, seeded from the scene, exactly like `position` above and `spread` below.
    //
    // It was a multiplier over the authored value, defaulting to 1.0, and that is what this defect
    // was. `lifetime`, `speed` and `size` are multipliers for a reason a scalar cannot avoid: each
    // scales a *pair* (min/max, start/end) that one number cannot replace, and each says "x" on the
    // control that writes it. `extent` scales a single vector and the control that writes it says
    // "m" -- so a project file recorded `0.0538` for a beam somebody had set to 0.42 m, a number
    // meaningless without opening the scene file to find the 7.8 it multiplied. The owner set a
    // beam radius through the UI and what persisted was neither the radius nor findable as one.
    p.extent = &params.add(v3(base, "extent", s.extent, 0.0f, 10000.0f));
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
    // ADR-367: reachable now that it does something. A value that reaches the GPU and is ignored is
    // one defect; a value that reaches the GPU, is obeyed, and cannot be turned is the next one.
    p.softness = &params.add(f(base, "softness", s.softness, 0.0f, 50.0f, 0.0f, 4.0f));
    p.stretch = &params.add(f(base, "stretch", s.velocityStretch, 0.0f, 20.0f, 0.0f, 4.0f));
    p.trailWidth = &params.add(f(base, "trailWidth", s.trailWidth, 0.0f, 20.0f, 0.0f, 4.0f));
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
    s.extent = p.extent->value();
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
    s.softness = p.softness != nullptr ? p.softness->value() : rest.softness; // ADR-367
    s.velocityStretch = p.stretch->value();
    s.trailWidth = p.trailWidth->value();
    s.enabled = p.enabled->value();
}

} // namespace avgen::scene
