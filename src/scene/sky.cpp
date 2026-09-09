// Procedural sky environment (ADR-036): the CPU reference of the analytic sky, the key-light
// resolution and the deterministic irradiance quadrature. `shaders/environment.wgsl`'s fs_sky is
// the transliteration; docs/lighting.md documents the model and its parameters.

#include "scene/sky.hpp"

#include "scene/scene_types.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace avgen::scene {

namespace {

// The aureole around the disc, as a fraction of the disc's radiance. Small: a plausible sky
// puts far more energy in the disc than in the halo, and coupling them any harder floods the
// whole environment when the sun is turned up. Mirrored by SKY_AUREOLE in environment.wgsl.
constexpr float kSunAureole = 0.02f;

float saturate1(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

float smoothstep1(float e0, float e1, float x) {
    if (e0 == e1) {
        return x < e0 ? 0.0f : 1.0f;
    }
    const float t = saturate1((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v / std::sqrt(len2) : fallback;
}

class Hash64 {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h_ = (h_ ^ ((v >> (8 * i)) & 0xFFu)) * 0x100000001b3ULL;
        }
    }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v == 0.0f ? 0.0f : v)); }
    void v3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    [[nodiscard]] std::uint64_t value() const { return h_; }

private:
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

} // namespace

std::uint64_t SkyRuntime::hash() const {
    Hash64 h;
    h.v3(zenithColor);
    h.v3(horizonColor);
    h.v3(groundColor);
    h.f32(hazeWidth);
    h.v3(sunColor);
    h.f32(sunIntensity);
    h.f32(sunAngularRadius);
    h.f32(sunGlowWidth);
    h.f32(intensity);
    h.v3(sunDirection);
    return h.value();
}

const PunctualLight* skyKeyLight(const std::vector<PunctualLight>& lights) {
    const PunctualLight* firstDirectional = nullptr;
    for (const PunctualLight& light : lights) {
        if (!light.enabled || light.type != PunctualLight::Type::Directional) {
            continue;
        }
        if (light.role == PunctualLight::Role::Key) {
            return &light;
        }
        if (firstDirectional == nullptr) {
            firstDirectional = &light;
        }
    }
    return firstDirectional;
}

SkyRuntime resolveSky(const SkySettings& settings, const std::vector<PunctualLight>& lights) {
    SkyRuntime sky;
    sky.zenithColor = glm::max(settings.zenithColor, glm::vec3(0.0f));
    sky.horizonColor = glm::max(settings.horizonColor, glm::vec3(0.0f));
    sky.groundColor = glm::max(settings.groundColor, glm::vec3(0.0f));
    sky.hazeWidth = std::max(settings.hazeWidth, 1e-3f);
    sky.sunIntensity = std::max(settings.sunIntensity, 0.0f);
    sky.sunAngularRadius = std::clamp(settings.sunAngularRadius, 1e-3f, 1.5f);
    sky.sunGlowWidth = std::max(settings.sunGlowWidth, 1e-3f);
    sky.intensity = std::max(settings.intensity, 0.0f);
    sky.sunColor = glm::max(settings.sunColor, glm::vec3(0.0f));
    sky.sunDirection = safeNormalize(settings.sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));

    const PunctualLight* key = settings.useKeyLight ? skyKeyLight(lights) : nullptr;
    if (key != nullptr) {
        // A light's `direction` is the direction the light travels, so the sun sits the other way.
        sky.sunDirection = safeNormalize(-key->direction, sky.sunDirection);
        // Tint by the key light's colour and temperature, normalised so the light's intensity
        // never doubles as sky brightness (that is `env/sky/sunIntensity`'s job).
        const glm::vec3 tint = key->color * colorTemperatureToRgb(key->temperature, key->tint);
        const float luminance = 0.2126f * tint.r + 0.7152f * tint.g + 0.0722f * tint.b;
        if (luminance > 1e-4f) {
            sky.sunColor = sky.sunColor * (tint / luminance);
        }
    }
    return sky;
}

glm::vec3 skyRadiance(const SkyRuntime& sky, const glm::vec3& dir, float minRadius) {
    const glm::vec3 d = safeNormalize(dir, glm::vec3(0.0f, 1.0f, 0.0f));
    const float h = saturate1(d.y);
    const float haze = std::exp(-h / std::max(sky.hazeWidth, 1e-3f));
    const glm::vec3 gradient = sky.zenithColor * (1.0f - haze) + sky.horizonColor * haze;
    const float band = smoothstep1(-0.03f, 0.03f, d.y);
    const glm::vec3 base = sky.groundColor * (1.0f - band) + gradient * band;

    const float cosTheta = std::clamp(glm::dot(d, sky.sunDirection), -1.0f, 1.0f);
    const float theta = std::acos(cosTheta);
    const float radius = std::max(sky.sunAngularRadius, std::max(minRadius, 0.0f));
    const float energy = (sky.sunAngularRadius / radius) * (sky.sunAngularRadius / radius);
    const float disc = (1.0f - smoothstep1(radius * 0.85f, radius * 1.15f, theta)) * energy;
    const float glow = std::exp(-theta / std::max(sky.sunGlowWidth, 1e-3f)) * kSunAureole;
    const glm::vec3 sun = sky.sunColor * sky.sunIntensity * (disc + glow) * band;
    return (base + sun) * sky.intensity;
}

glm::vec3 skyIrradiance(const SkyRuntime& sky, const glm::vec3& n, int samples) {
    const int count = std::max(samples, 1);
    const glm::vec3 normal = safeNormalize(n, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 up = std::abs(normal.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, normal));
    const glm::vec3 bitangent = glm::cross(normal, tangent);
    // Fibonacci hemisphere with a cosine-distributed elevation: the estimator is the plain average.
    constexpr float kGolden = 2.39996322972865332f; // pi * (3 - sqrt(5))
    glm::vec3 acc(0.0f);
    for (int i = 0; i < count; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
        const float phi = kGolden * static_cast<float>(i);
        const float cosTheta = std::sqrt(1.0f - u);
        const float sinTheta = std::sqrt(u);
        const glm::vec3 l = tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) +
                            normal * cosTheta;
        acc += skyRadiance(sky, l);
    }
    return acc / static_cast<float>(count);
}

} // namespace avgen::scene
