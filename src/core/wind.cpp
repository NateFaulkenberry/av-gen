#include "core/wind.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::wind {
namespace {

constexpr float kMinScale = 0.01f;

[[nodiscard]] float wavenumber(float metres) {
    return kTau / std::max(metres, kMinScale);
}

// The same mixing constants the shader uses. They are deliberately irrational-looking: two waves at
// a ratio near 1.63 beat with a period long enough that no viewer will see the field repeat, and
// the direction cosines are not axis-aligned so a front is never parallel to a world axis.
constexpr float kRegionAx = 0.94f, kRegionAy = 0.34f;
constexpr float kRegionBx = 0.61f, kRegionBy = -0.79f;
constexpr float kRegionRatio = 1.63f;
constexpr float kTurbAx = 0.31f, kTurbAy = 0.95f;
constexpr float kTurbBx = -0.87f, kTurbBy = 0.5f;
constexpr float kTurbRatio = 1.41f;

} // namespace

WindUniforms packWind(const WindParams& p) {
    WindUniforms u;
    const bool on = p.active();
    u.dir = glm::vec4(std::cos(p.direction), std::sin(p.direction), on ? std::max(p.speed, 0.0f) : 0.0f,
                      on ? 1.0f : 0.0f);
    u.region = glm::vec4(wavenumber(p.regionScale), std::max(p.regionAmount, 0.0f), p.regionDrift * kTau,
                         p.turbulence);
    u.gust = glm::vec4(wavenumber(p.gustScale), p.gustSpeed, std::max(p.gustAmount, 0.0f),
                       std::max(p.gustSharpness, 0.05f));
    u.turbulence = glm::vec4(wavenumber(p.turbulenceScale), p.turbulenceSpeed, wavenumber(p.flutterScale), 0.0f);
    return u;
}

WindSample sampleWind(const WindUniforms& w, const glm::vec3& p, float t) {
    WindSample out;
    const glm::vec2 d(w.dir.x, w.dir.y);
    const glm::vec2 perp(-d.y, d.x);
    const glm::vec2 xz(p.x, p.z);
    const float a = glm::dot(xz, d); // metres downwind
    const float c = glm::dot(xz, perp); // metres across the wind

    // Regional strength. Two long travelling waves; the amplitude the caller asks for is the
    // amplitude they get (the half on each wave sums back to one), because a field that quietly
    // delivers a third of its setting is a field nobody can tune.
    const float kr = w.region.x;
    const float drift = w.region.z;
    const float r1 = std::sin(kr * (kRegionAx * a + kRegionAy * c) - t * drift);
    const float r2 = std::sin(kr * kRegionRatio * (kRegionBx * a + kRegionBy * c) + t * drift * 0.61f + 2.1f);
    const float region = std::max(1.0f + w.region.y * 0.5f * (r1 + r2), 0.0f);

    // The gust front: a sharpened pulse whose phase advances downwind at gustSpeed metres a second.
    // The extra `sin(c * ...)` bends the front so it is not a straight line sweeping the map -- it
    // arrives at one end of a meadow before the other, which is what a real gust looks like.
    const float kg = w.gust.x;
    const float gp = kg * (a - t * w.gust.y) + 0.8f * std::sin(c * kg * 0.37f);
    const float envelope = std::pow(std::max(0.5f + 0.5f * std::sin(gp), 0.0f), w.gust.w);

    // Turbulence turns the local direction rather than scaling it: gusts change how hard, eddies
    // change which way.
    const float kt = w.turbulence.x;
    const float ts = w.turbulence.y;
    const float s1 = std::sin(kt * (kTurbAx * a + kTurbAy * c) - t * ts * kt);
    const float s2 = std::sin(kt * kTurbRatio * (kTurbBx * a + kTurbBy * c) + t * ts * kt * 0.83f + 1.3f);
    const float turn = w.region.w * 0.5f * (s1 + s2);
    const float ct = std::cos(turn);
    const float st = std::sin(turn);

    out.direction = d * ct + perp * st;
    out.strength = w.dir.z * region;
    out.gust = envelope * w.gust.z;
    out.phase = w.turbulence.z * (0.7f * a + 0.71f * c);
    return out;
}

float oscillatorGain(float omega, float omega0, float zeta) {
    const float w0 = std::max(omega0, 1e-4f);
    const float r = omega / w0;
    const float z = std::clamp(zeta, 0.02f, 4.0f);
    const float re = 1.0f - r * r;
    const float im = 2.0f * z * r;
    return 1.0f / std::max(std::sqrt(re * re + im * im), 1e-4f);
}

float oscillatorLag(float omega, float omega0, float zeta) {
    const float w0 = std::max(omega0, 1e-4f);
    const float r = omega / w0;
    const float z = std::clamp(zeta, 0.02f, 4.0f);
    return std::atan2(2.0f * z * r, 1.0f - r * r); // 0 well below resonance, pi well above
}

MotionResponse motionResponse(const WindParams& wind, const VegetationMotion& plant) {
    MotionResponse out;
    out.bendCurve = std::max(plant.bendCurve, 0.05f);
    out.bendLimit = std::max(plant.bendLimit, 0.0f);
    out.amplitudeVariance = std::clamp(plant.amplitudeVariance, 0.0f, 1.0f);
    if (!plant.active()) {
        return out;
    }
    const float k = std::max(plant.stiffness, 1e-3f);
    const float m = std::max(plant.mass, 1e-6f);
    const float zeta = std::clamp(plant.damping, 0.02f, 4.0f);
    const float omega0 = std::sqrt(k / m);

    // The static deflection: how far the tip goes under a steady push. Everything else multiplies it.
    const float statics = plant.tipAmplitude * std::max(plant.windSensitivity, 0.0f) / k;

    // The gust band is the actual temporal content of the travelling fronts: a front of wavelength
    // gustScale passing at gustSpeed drives the plant at 2*pi*speed/scale rad/s.
    const float omegaGust = kTau * std::max(wind.gustSpeed, 0.0f) / std::max(wind.gustScale, kMinScale);
    const float gustGain = oscillatorGain(omegaGust, omega0, zeta);

    // The regional swing is slow enough that anything but a felled tree passes it, but price it
    // honestly rather than assuming: a very soft plant is nearly static at that rate too.
    const float omegaRegion = kTau * std::max(wind.regionDrift, 0.0f);
    const float steadyGain = oscillatorGain(omegaRegion, omega0, zeta);

    out.steadyGain = statics * steadyGain;
    out.gustGain = statics * gustGain * std::max(plant.gustResponse, 0.0f);
    // Broadband turbulence excites the plant's own mode; how loudly it rings is set by its damping,
    // which is the one thing damping is for in a model with no simulation in it.
    out.flutterGain = statics * std::min(0.5f / zeta, 4.0f) * std::max(wind.turbulence, 0.0f);
    out.flutterOmega = omega0;
    // A phase lag in radians at the gust frequency is a delay in seconds; delaying the whole field
    // is exactly equivalent for a linear system and costs the shader nothing.
    out.swayDelay = omegaGust > 1e-4f ? oscillatorLag(omegaGust, omega0, zeta) / omegaGust : 0.0f;
    return out;
}

glm::vec3 vegetationDisplacement(float objectY, float baseY, float extentY, float instanceScaleY,
                                 const WindSample& w, const MotionResponse& r, const glm::vec4& random,
                                 float tFlutter) {
    // Height along the plant, 0 at the root. This is the whole anchoring story: whatever the
    // displacement turns out to be, it is multiplied by a curve that is exactly zero where the stem
    // meets soil, so the mesh is never rotated rigidly.
    const float h = std::clamp((objectY - baseY) / std::max(extentY, 1e-6f), 0.0f, 1.0f);
    const float profile = std::pow(h, r.bendCurve);
    const float height = extentY * instanceScaleY;
    // Per-instance amplitude, so neighbours differ while the region they share stays coherent.
    const float amp = 1.0f + r.amplitudeVariance * (random.z * 2.0f - 1.0f);

    const float s = w.strength;
    const float along = r.steadyGain * s + r.gustGain * s * w.gust;
    const float flutter = r.flutterGain * s * std::sin(w.phase + r.flutterOmega * tFlutter + random.x * kTau);
    const glm::vec2 perp(-w.direction.y, w.direction.x);
    glm::vec2 off = (w.direction * along + perp * flutter) * (amp * profile * height);

    // A soft ceiling on tip travel: len for small offsets, -> maxLen for large, with no corner where
    // a hard clamp would make a stalk visibly hit a wall.
    const float maxLen = r.bendLimit * height * profile;
    const float len = glm::length(off);
    off *= maxLen / (len + maxLen + 1e-6f);

    // A stem that bends keeps its length, so the tip also drops. Without this the plant stretches
    // sideways and reads as a shear rather than a bend.
    const float dy = -0.5f * glm::dot(off, off) / std::max(height * std::max(h, 0.05f), 1e-4f);
    return glm::vec3(off.x, dy, off.y);
}

// ---- serialisation -----------------------------------------------------------------------------

namespace {
void readFloat(const nlohmann::json& j, const char* key, float& target) {
    if (j.contains(key) && j.at(key).is_number()) {
        target = j.at(key).get<float>();
    }
}
} // namespace

WindParams windFromJson(const nlohmann::json& j) {
    WindParams p;
    if (!j.is_object()) {
        return p;
    }
    p.enabled = true;
    if (j.contains("enabled") && j.at("enabled").is_boolean()) {
        p.enabled = j.at("enabled").get<bool>();
    }
    readFloat(j, "direction", p.direction);
    readFloat(j, "speed", p.speed);
    readFloat(j, "regionScale", p.regionScale);
    readFloat(j, "regionAmount", p.regionAmount);
    readFloat(j, "regionDrift", p.regionDrift);
    readFloat(j, "turbulence", p.turbulence);
    readFloat(j, "turbulenceScale", p.turbulenceScale);
    readFloat(j, "turbulenceSpeed", p.turbulenceSpeed);
    readFloat(j, "gustAmount", p.gustAmount);
    readFloat(j, "gustScale", p.gustScale);
    readFloat(j, "gustSpeed", p.gustSpeed);
    readFloat(j, "gustSharpness", p.gustSharpness);
    readFloat(j, "flutterScale", p.flutterScale);
    return p;
}

nlohmann::json windToJson(const WindParams& p) {
    return nlohmann::json{{"enabled", p.enabled},
                          {"direction", p.direction},
                          {"speed", p.speed},
                          {"regionScale", p.regionScale},
                          {"regionAmount", p.regionAmount},
                          {"regionDrift", p.regionDrift},
                          {"turbulence", p.turbulence},
                          {"turbulenceScale", p.turbulenceScale},
                          {"turbulenceSpeed", p.turbulenceSpeed},
                          {"gustAmount", p.gustAmount},
                          {"gustScale", p.gustScale},
                          {"gustSpeed", p.gustSpeed},
                          {"gustSharpness", p.gustSharpness},
                          {"flutterScale", p.flutterScale}};
}

VegetationMotion motionFromJson(const nlohmann::json& j, const VegetationMotion& base) {
    VegetationMotion m = base;
    if (!j.is_object()) {
        return m;
    }
    readFloat(j, "stiffness", m.stiffness);
    readFloat(j, "mass", m.mass);
    readFloat(j, "damping", m.damping);
    readFloat(j, "windSensitivity", m.windSensitivity);
    readFloat(j, "bendLimit", m.bendLimit);
    readFloat(j, "tipAmplitude", m.tipAmplitude);
    readFloat(j, "gustResponse", m.gustResponse);
    readFloat(j, "bendCurve", m.bendCurve);
    readFloat(j, "amplitudeVariance", m.amplitudeVariance);
    return m;
}

nlohmann::json motionToJson(const VegetationMotion& m) {
    return nlohmann::json{{"stiffness", m.stiffness},
                          {"mass", m.mass},
                          {"damping", m.damping},
                          {"windSensitivity", m.windSensitivity},
                          {"bendLimit", m.bendLimit},
                          {"tipAmplitude", m.tipAmplitude},
                          {"gustResponse", m.gustResponse},
                          {"bendCurve", m.bendCurve},
                          {"amplitudeVariance", m.amplitudeVariance}};
}


} // namespace avgen::wind
